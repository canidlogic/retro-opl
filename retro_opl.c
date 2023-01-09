/*
 * retro_opl.c
 * ===========
 * 
 * Retro OPL emulator.
 * 
 * This standalone component of the Retro synthesizer is able to run a
 * software emulation of OPL hardware to generate a WAV file.
 * 
 * You must compile with one of the opl_driver implementations, along
 * with anything that opl_driver implementation requires.
 * 
 * The program takes a two arguments.  The first is the path to the
 * output WAV file to create.  The second is either "44100" or "48000"
 * indicating the sampling rate for the output WAV file.
 * 
 * An OPL2 hardware script in the format defined by the Retro
 * Specification is read from standard input.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "opl_driver.h"

/*
 * Constants
 * =========
 */

/*
 * The number of samples in the sample buffer.
 */
#define BUFFER_SAMPLES (4096)

/*
 * The maximum number of bytes in an input line, not including the
 * terminating nul.
 */
#define LINE_MAXIMUM (1023)

/*
 * Local data
 * ==========
 */

/*
 * Executable module name, for use in error reports.
 * 
 * This is set at the start of the program entrypoint.
 */
static const char *pModule = NULL;

/*
 * The cached endian state.
 * 
 * Zero if hasn't been detected yet.  1 if little endian.  2 if big
 * endian.
 */
static int endian_state = 0;

/*
 * The line number in the input file.
 * 
 * This starts out zero so that the first line read will be line 1.
 */
static int32_t line_count = 0;

/*
 * The input line buffer and a flag indicating whether it is
 * initialized.
 * 
 * When initialized, it stores the line content as a nul-terminated
 * string, not including any line break at the end.
 */
static int     l_init = 0;
static uint8_t l_buf[LINE_MAXIMUM + 1];

/*
 * The handle to the WAV output file, or NULL if not open.
 */
static FILE *pOut = NULL;

/*
 * The total number of samples that have been written to output.
 * 
 * This is updated during flush operations, not when the samples are
 * just written into the buffer.
 */
static int32_t s_total = 0;

/*
 * The sample buffer and a count of how many samples have been written
 * into it.
 */
static int32_t s_fill = 0;
static int16_t s_buf[BUFFER_SAMPLES];

/*
 * The binary buffer is used for byte output of samples.
 */
static uint8_t b_buf[BUFFER_SAMPLES * 2];

/*
 * Local functions
 * ===============
 */

/* Prototypes */
static void raiseErr(void);
static int isLittleEndian(void);

static void writeByte(uint8_t val);
static void writeWord(uint16_t val);
static void writeDword(uint32_t val);

static void beginWAV(const char *pPath, int32_t sample_rate);
static void finishWAV(void);

static void flushBuffer(void);
static void computeSamples(int32_t count);

static int isBlankStr(const uint8_t *pstr);
static const uint8_t *parseByte(const uint8_t *pstr, uint8_t *pb);
static const uint8_t *parseInt(const uint8_t *pstr, int32_t *pv);

static int readInput(void);
static int32_t readHeader(void);

/*
 * Function called when the program is stopping on an error.
 * 
 * This function will not return.  It prints a generic message
 * indicating the program is stopping on an error and then exits with an
 * error status.
 */
static void raiseErr(void) {
  /* Print an error with the module name if available */
  if (pModule != NULL) {
    fprintf(stderr, "%s: Stopped on an error!\n", pModule);
  } else {
    fprintf(stderr, "Stopped on an error!\n");
  }
  
  /* Exit program with error status */
  exit(1);
}

/*
 * Check whether this system is little endian.
 * 
 * The result is cached after the first invocation.
 * 
 * Return:
 * 
 *   non-zero if this is a little-endian system, zero otherwise
 */
static int isLittleEndian(void) {
  int result = 0;
  uint16_t test = 0;
  uint8_t buf[2];
  
  /* Compute cached result if not cached */
  if (endian_state == 0) {
    /* Set test value */
    test = 0x0201;
    
    /* Copy to byte buffer */
    memcpy(buf, &test, 2);
    
    /* First value in byte buffer will be endian state */
    endian_state = buf[0];
  }
  
  /* Return appropriate result */
  if (endian_state == 1) {
    result = 1;
  } else if (endian_state == 2) {
    result = 2;
  } else {
    fprintf(stderr, "%s: Failed to detect endianness!\n", pModule);
    raiseErr();
  }
  return result;
}

/*
 * Write a single byte to output.
 * 
 * This function is used by all output functions, except flushBuffer,
 * which has its own bulk output function.
 * 
 * Parameters:
 * 
 *   val - the byte value to write
 */
static void writeByte(uint8_t val) {
  /* Check state */
  if (pOut == NULL) {
    raiseErr();
  }
  
  /* Write byte */
  if (fputc((int) val, pOut) != val) {
    fprintf(stderr, "%s: I/O error writing to output!\n", pModule);
    raiseErr();
  }
}

/*
 * Write a 16-bit unsigned word in little-endian order to output.
 * 
 * Parameters:
 * 
 *   val - the word value to write
 */
static void writeWord(uint16_t val) {
  writeByte((uint8_t) (val & 0xff));
  writeByte((uint8_t) (val >> 8));
}

/*
 * Write a 32-bit unsigned dword in little-endian order to output.
 * 
 * Parameters:
 * 
 *   val - the dword value to write
 */
static void writeDword(uint32_t val) {
  writeByte((uint8_t) (val & 0xff));
  writeByte((uint8_t) ((val >> 8) & 0xff));
  writeByte((uint8_t) ((val >> 16) & 0xff));
  writeByte((uint8_t) (val >> 24));
}

/*
 * Open output file and write WAVE headers.
 * 
 * A few WAVE header values are not known until all samples have been
 * written.  These are written with finishWAV().
 * 
 * Parameters:
 * 
 *   pPath - path to the output file to create
 * 
 *   sample_rate - either 44100 or 48000
 */
static void beginWAV(const char *pPath, int32_t sample_rate) {
  /* Check parameters */
  if ((pPath == NULL) ||
      ((sample_rate != 48000) && (sample_rate != 44100))) {
    raiseErr();
  }
  
  /* Check state */
  if (pOut != NULL) {
    raiseErr();
  }
  
  /* Open output file */
  pOut = fopen(pPath, "wb");
  if (pOut == NULL) {
    fprintf(stderr, "%s: Failed to create file '%s'!\n",
            pModule, pPath);
    raiseErr();
  }
  
  /* Write the WAVE header (string constants are backwards because this
   * is little endian) */
  writeDword(UINT32_C(0x46464952));   /* "RIFF" */
  writeDword(0);                      /* Chunk size (done later) */
  writeDword(UINT32_C(0x45564157));   /* "WAVE" */
  writeDword(UINT32_C(0x20746d66));   /* "fmt " */
  writeDword(UINT32_C(16));           /* Format chunk size */
  writeWord(UINT16_C(1));             /* WAVE_FORMAT_PCM */
  writeWord(UINT16_C(1));             /* Number of channels */
  writeDword((uint32_t) sample_rate); /* Sample rate */
  writeDword((uint32_t)
          (sample_rate * 2));         /* Bytes per second */
  writeWord(UINT16_C(2));             /* Block align */
  writeWord(UINT16_C(16));            /* Bits per sample */
  writeDword(UINT32_C(0x61746164));   /* "data" */
  writeDword(0);                      /* Data size (done later) */
}

/*
 * Finish writing the WAV file and close it.
 * 
 * The sample buffer will automatically be flushed by this function.
 */
static void finishWAV(void) {
  int32_t data_size = 0;
  int32_t chunk_size = 0;
  
  /* Check state */
  if (pOut == NULL) {
    raiseErr();
  }
  
  /* Flush sample buffer */
  flushBuffer();
  
  /* Compute data size in bytes, watching for overflow */
  data_size = s_total;
  if (data_size <= INT32_MAX / 2) {
    data_size *= 2;
  } else {
    fprintf(stderr, "%s: Overflow computing file size!\n", pModule);
    raiseErr();
  }
  
  /* Compute chunk size in bytes, watching for overflow */
  if (data_size <= INT32_MAX - 36) {
    chunk_size = data_size + 36;
  } else {
    fprintf(stderr, "%s: Overflow computing file size!\n", pModule);
    raiseErr();
  }
  
  /* Seek to chunk size field */
  if (fseek(pOut, 4, SEEK_SET)) {
    fprintf(stderr, "%s: I/O error seeking output!\n", pModule);
    raiseErr();
  }
  
  /* Write chunk size field */
  writeDword((uint32_t) chunk_size);
  
  /* Seek to data size field */
  if (fseek(pOut, 40, SEEK_SET)) {
    fprintf(stderr, "%s: I/O error seeking output!\n", pModule);
    raiseErr();
  }
  
  /* Write data size field */
  writeDword((uint32_t) data_size);
  
  /* Close the file */
  if (fclose(pOut)) {
    fprintf(stderr, "%s: Warning: failed to close file!\n", pModule);
  }
  pOut = NULL;
}

/*
 * Flush the sample buffer to the output file.
 */
static void flushBuffer(void) {
  int32_t i = 0;
  uint8_t b1 = 0;
  uint8_t b2 = 0;
  
  /* Only do something if there is something in the buffer */
  if (s_fill > 0) {
  
    /* Check state */
    if (pOut == NULL) {
      raiseErr();
    }
  
    /* Copy samples to byte buffer */
    memcpy(b_buf, s_buf, s_fill * 2);
    
    /* If not little endian, reverse each pair of bytes */
    if (!isLittleEndian()) {
      for(i = 0; i < s_fill; i++) {
        b1 = b_buf[(i * 2)];
        b2 = b_buf[(i * 2) + 1];
        b_buf[(i * 2)] = b2;
        b_buf[(i * 2) + 1] = b1;
      }
    }
    
    /* Write to output */
    if (fwrite(b_buf, 2, (size_t) s_fill, pOut) != s_fill) {
      fprintf(stderr, "%s: I/O error writing output!\n", pModule);
      raiseErr();
    }
    
    /* Update total sample count, watching for overflow */
    if (s_total <= INT32_MAX - s_fill) {
      s_total += s_fill;
    } else {
      fprintf(stderr, "%s: Sample count overflow!\n", pModule);
      raiseErr();
    }
    
    /* Clear the buffer */
    s_fill = 0;
  }
}

/*
 * Compute a given number of samples with the current state of the
 * emulated OPL hardware and transfer through the sample buffer.
 * 
 * Parameters:
 * 
 *   count - the number of samples to compute
 */
static void computeSamples(int32_t count) {
  int32_t work = 0;
  
  /* Check parameter */
  if (count < 1) {
    raiseErr();
  }
  
  /* Keep processing until we've done all the requested samples */
  while (count > 0) {
    /* If buffer is completely filled, flush it */
    if (s_fill >= BUFFER_SAMPLES) {
      flushBuffer();
    }
    
    /* The work count is the minimum of the remaining samples in the
     * buffer and the remaining request count */
    work = BUFFER_SAMPLES - s_fill;
    if (count < work) {
      work = count;
    }
    
    /* Generate the work number of samples and add to buffer */
    opl_generate(&(s_buf[s_fill]), work);
    s_fill += work;
    
    /* Decrease the request count by the work samples */
    count -= work;
  }
}

/*
 * Check whether a given string is blank.
 * 
 * Returns:
 * 
 *   non-zero if string is empty or only contains tabs and spaces; zero
 *   otherwise
 */
static int isBlankStr(const uint8_t *pstr) {
  
  /* Check parameter */
  if (pstr == NULL) {
    raiseErr();
  }
  
  /* Scan characters */
  while(*pstr != 0) {
    if ((*pstr != ' ') && (*pstr != '\t')) {
      return 0;
    }
    pstr++;
  }
  
  /* If we got here, string is blank */
  return 1;
}

/*
 * Parse an base-16 byte value from a string.
 * 
 * pstr points to where to start parsing.  The character indicated by
 * pstr must be a base-16 digit, space, or horizontal tab.  Parsing
 * starts with an optional sequence of space and horizontal tab
 * characters that are skipped.  The first non-space, non-tab character
 * encountered must be a base-16 digit.
 * 
 * This function will read exactly two base-16 digits and write the
 * parsed value to *pb.  After the two base-16 digits must come a
 * character that is not a base-16 digit.  The return value points to
 * this character immediately after the second base-16 digit.
 * 
 * Parameters:
 * 
 *   pstr - the location to start parsing
 * 
 *   pb - variable to receive the parsed byte value
 * 
 * Return:
 * 
 *   pointer to character immediately after byte value that was parsed
 */
static const uint8_t *parseByte(const uint8_t *pstr, uint8_t *pb) {
  
  uint8_t result = 0;
  int i = 0;
  
  /* Check parameters */
  if ((pstr == NULL) || (pb == NULL)) {
    raiseErr();
  }
  
  /* Skip over any tabs and spaces */
  while ((*pstr == '\t') || (*pstr == ' ')) {
    pstr++;
  }
  
  /* Parse two base-16 digits */
  for(i = 0; i < 2; i++) {
    if ((*pstr >= '0') && (*pstr <= '9')) {
      result = (uint8_t) ((result << 4) + (*pstr - '0'));
    
    } else if ((*pstr >= 'A') && (*pstr <= 'F')) {
      result = (uint8_t) ((result << 4) + (*pstr - 'A' + 10));
    
    } else if ((*pstr >= 'a') && (*pstr <= 'f')) {
      result = (uint8_t) ((result << 4) + (*pstr - 'a' + 10));
      
    } else {
      fprintf(stderr, "%s: Byte parse failed on line %ld!\n",
              pModule, (long) line_count);
      raiseErr();
    }
    pstr++;
  }
  
  /* Make sure we landed on something other than a base-16 digit */
  if (((*pstr >= '0') && (*pstr <= '9')) ||
      ((*pstr >= 'A') && (*pstr <= 'F')) ||
      ((*pstr >= 'a') && (*pstr <= 'f'))) {
    fprintf(stderr, "%s: Byte parse failed on line %ld!\n",
            pModule, (long) line_count);
    raiseErr();
  }
  
  /* Write result and return pointer */
  *pb = result;
  return pstr;
}

/*
 * Parse an unsigned decimal integer from a string.
 * 
 * pstr points to where to start parsing.  The character indicated by
 * pstr must be a decimal digit, space, or horizontal tab.  Parsing
 * starts with an optional sequence of space and horizontal tab
 * characters that are skipped.  The first non-space, non-tab character
 * encountered must be a decimal digit.
 * 
 * After the decimal digit is encountered, this function will read a
 * sequence of decimal digits until a character that is not a decimal
 * digit is found.  The parsed integer value is written to *pv, and the
 * return value points to the first character after the decimal
 * sequence.
 * 
 * Parameters:
 * 
 *   pstr - the location to start parsing
 * 
 *   pv - variable to receive the parsed integer value
 * 
 * Return:
 * 
 *   pointer to character immediately after decimal integer that was
 *   parsed
 */
static const uint8_t *parseInt(const uint8_t *pstr, int32_t *pv) {
  
  int32_t result = 0;
  int32_t d = 0;
  
  /* Check parameters */
  if ((pstr == NULL) || (pv == NULL)) {
    raiseErr();
  }
  
  /* Skip over any tabs and spaces */
  while ((*pstr == '\t') || (*pstr == ' ')) {
    pstr++;
  }
  
  /* Check that we found a decimal digit */
  if ((*pstr < '0') || (*pstr > '9')) {
    fprintf(stderr, "%s: Integer parse failed on line %ld!\n",
            pModule, (long) line_count);
    raiseErr();
  }
  
  /* Parse sequence of decimal digits */
  while ((*pstr >= '0') && (*pstr <= '9')) {
    /* Get numeric value of current digit */
    d = (int32_t) (*pstr - '0');
    
    /* Multiply result by 10, watching for overflow */
    if (result <= INT32_MAX / 10) {
      result *= 10;
    } else {
      fprintf(stderr, "%s: Integer value overflow on line %ld!\n",
              pModule, (long) line_count);
      raiseErr();
    }
    
    /* Add current digit to result, watching for overflow */
    if (result <= INT32_MAX - d) {
      result += d;
    } else {
      fprintf(stderr, "%s: Integer value overflow on line %ld!\n",
              pModule, (long) line_count);
      raiseErr();
    }
    
    /* Advance to next character */
    pstr++;
  }
  
  /* Write result and return pointer */
  *pv = result;
  return pstr;
}

/*
 * Read a line from input.
 * 
 * If at least one byte is read from input, then line_count, l_init, and
 * l_buf will be updated to hold the next line, and a non-zero value
 * will be returned.
 * 
 * If input returns EOF when attempting to read a character, this
 * function just returns zero right away.
 * 
 * In case of error, an error message is printed and raiseErr() is
 * called.
 * 
 * Return:
 * 
 *   non-zero if another input line was read, zero if EOF
 */
static int readInput(void) {
  int c = 0;
  int32_t written = 0;
  
  /* Read first character */
  c = getchar();
  if (c == EOF) {
    if (feof(stdin)) {
      /* Stream is EOF, so return zero */
      return 0;
      
    } else {
      /* Reading character failed due to I/O error */
      fprintf(stderr, "%s: I/O error reading input!\n", pModule);
      raiseErr();
    }
  }
  
  /* We got at least one character, so increment line count first */
  if (line_count < INT32_MAX) {
    line_count++;
  } else {
    fprintf(stderr, "%s: Too many lines in input!\n", pModule);
    raiseErr();
  }
  
  /* Initialize the line buffer */
  l_init = 1;
  memset(l_buf, 0, LINE_MAXIMUM + 1);
  
  /* Enter processing loop */
  while (1) {
    /* If character is CR, read next character, which must be LF, and
     * then proceed */
    if (c == '\r') {
      c = getchar();
      if ((c == EOF) && ferror(stdin)) {
        fprintf(stderr, "%s: I/O error reading input!\n", pModule);
        raiseErr();
      }
      if (c != '\n') {
        fprintf(stderr, "%s: CR without following LF on line %ld!\n",
                pModule, (long) line_count);
        raiseErr();
      }
    }
    
    /* If character is LF, then leave loop */
    if (c == '\n') {
      break;
    }
    
    /* If we got here, check that character is in normal printing
     * US-ASCII range */
    if ((c != '\t') && ((c < 0x20) || (c > 0x7e))) {
      fprintf(stderr, "%s: Line %ld contains invalid character!\n",
              pModule, (long) line_count);
      raiseErr();
    }
    
    /* Check that we haven't exceeded the buffer space */
    if (written >= LINE_MAXIMUM) {
      fprintf(stderr, "%s: Line %ld is too long!\n", (long) line_count);
      raiseErr();
    }
    
    /* Add character to line buffer */
    l_buf[written] = (uint8_t) c;
    written++;
    
    /* Read next character */
    c = getchar();
    if (c == EOF) {
      if (feof(stdin)) {
        /* Stream is EOF, so leave loop */
        break;
        
      } else {
        /* Reading character failed due to I/O error */
        fprintf(stderr, "%s: I/O error reading input!\n", pModule);
        raiseErr();
      }
    }
  }
  
  /* If we got here, we successfully read a line */
  return 1;
}

/*
 * Read and parse the header line from input.
 * 
 * Returns:
 * 
 *   the control rate in Hz, in range [1, 1024]
 */
static int32_t readHeader(void) {
  
  int32_t ctl_rate = 0;
  const char *pstr = NULL;
  
  /* Read a line */
  if (!readInput()) {
    fprintf(stderr, "%s: Failed to read header line!\n", pModule);
    raiseErr();
  }
  
  /* Check whether the line begins "OPL2" */
  if (memcmp(l_buf, "OPL2", 4) != 0) {
    fprintf(stderr, "%s: Input does not have OPL2 header!\n", pModule);
    raiseErr();
  }
  
  /* Parse the control rate */
  pstr = parseInt(&(l_buf[4]), &ctl_rate);
  if ((ctl_rate < 1) || (ctl_rate > 1024)) {
    fprintf(stderr, "%s: Control rate must be in range [1, 1024]!\n",
            pModule);
    raiseErr();
  }
  
  /* Make sure rest of line is blank */
  if (!isBlankStr(pstr)) {
    fprintf(stderr, "%s: Invalid header line syntax!\n", pModule);
    raiseErr();
  }
  
  /* Return control rate */
  return ctl_rate;
}

/*
 * Program entrypoint
 * ==================
 */

int main(int argc, char *argv[]) {
  
  int i = 0;
  const char *pPath = NULL;
  int32_t sample_rate = 0;
  
  const uint8_t *pstr = NULL;
  int32_t rate = 0;
  uint8_t reg = 0;
  uint8_t val = 0;
  
  int32_t t = 0;
  int32_t iv32 = 0;
  double so = 0.0;
  int32_t soi = 0;
  int32_t current = 0;
  
  /* Get the module name */
  pModule = NULL;
  if (argc > 0) {
    if (argv != NULL) {
      pModule = argv[0];
    }
  }
  if (pModule == NULL) {
    pModule = "retro_opl";
  }
  
  /* Check arguments */
  if (argc > 0) {
    if (argv == NULL) {
      raiseErr();
    }
    for(i = 0; i < argc; i++) {
      if (argv[i] == NULL) {
        raiseErr();
      }
    }
  }
  
  /* If no arguments, print syntax and return error status */
  if (argc < 2) {
    fprintf(stderr, "Syntax:\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "  retro_opl [output] [sample_rate] < [input]\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "[output] is path to output WAV file\n");
    fprintf(stderr, "[sample_rate] is 44100 or 48000\n");
    fprintf(stderr, "OPL2 script read from standard input\n");
    fprintf(stderr, "\n");
    exit(1);
  }
  
  /* Check that two arguments beyond module name */
  if (argc != 3) {
    fprintf(stderr, "%s: Wrong number of program arguments!\n",
      pModule);
    raiseErr();
  }
  
  /* Get the output file name and the sample rate */
  pPath = argv[1];
  if (strcmp(argv[2], "44100") == 0) {
    sample_rate = 44100;
  
  } else if (strcmp(argv[2], "48000") == 0) {
    sample_rate = 48000;
    
  } else {
    fprintf(stderr, "%s: Unsupported sampling rate!\n", pModule);
    raiseErr();
  }
  
  /* Start emulation */
  opl_init(sample_rate);
  
  /* Read the header from input and the rate */
  rate = readHeader();
  
  /* Start WAVE output */
  beginWAV(pPath, sample_rate);
  
  /* Process the rest of the file */
  while (readInput()) {
    
    /* If this line is blank or starts with an apostrophe, skip it */
    if ((l_buf[0] == '\'') || (isBlankStr(l_buf))) {
      continue;
    }
    
    /* Second character must be space or tab */
    if ((l_buf[1] != ' ') && (l_buf[1] != '\t')) {
      fprintf(stderr, "%s: Invalid command on line %ld!\n",
              pModule, (long) line_count);
      raiseErr();
    }
    
    /* Handle the command based on the first character */
    if (l_buf[0] == 'r') {
      /* Register command, so parse the address and data bytes */
      pstr = &(l_buf[1]);
      pstr = parseByte(pstr, &reg);
      pstr = parseByte(pstr, &val);
      if (!isBlankStr(pstr)) {
        fprintf(stderr, "%s: Invalid command syntax on line %ld!\n",
                pModule, (long) line_count);
        raiseErr();
      }
      
      /* Update register in the emulated hardware */
      opl_write(reg, val);
      
    } else if (l_buf[0] == 'w') {
      /* Wait command, so parse the control cycle count */
      pstr = &(l_buf[1]);
      pstr = parseInt(pstr, &iv32);
      if (!isBlankStr(pstr)) {
        fprintf(stderr, "%s: Invalid command syntax on line %ld!\n",
                pModule, (long) line_count);
        raiseErr();
      }
      
      /* Update the t value, watching for overflow */
      if (t <= INT32_MAX - iv32) {
        t += iv32;
      } else {
        fprintf(stderr, "%s: Time counter overflow!\n", pModule);
        raiseErr();
      }
      
      /* Compute the sample offset in floating-point space */
      so = (((double) t) * ((double) sample_rate)) / ((double) rate);
      
      /* Floor the offset to integer and make sure finite */
      so = floor(so);
      if (!isfinite(so)) {
        fprintf(stderr, "%s: Numeric problem computing offset!\n",
                pModule);
        raiseErr();
      }
      
      /* Make sure sample offset in integer range and convert to
       * integer */
      if (!((so >= 0.0) && (so <= ((double) INT32_MAX)))) {
        fprintf(stderr, "%s: Sample offset out of range!\n",
                pModule);
        raiseErr();
      }
      soi = (int32_t) so;
      if (soi <= current) {
        fprintf(stderr, "%s: Numeric problem computing offset!\n",
                pModule);
        raiseErr();
      }
      
      /* Compute samples so as to bring current sample offset up to the
       * soi we just computed */
      computeSamples(soi - current);
      
      /* Update current pointer to the soi value */
      current = soi;
      
    } else {
      fprintf(stderr, "%s: Invalid command on line %ld!\n",
              pModule, (long) line_count);
      raiseErr();
    }
  }
  
  /* Finish emulation */
  opl_finish();
  
  /* Finish WAVE output */
  finishWAV();
  
  /* If we got here, return successful status */
  return 0;
}
