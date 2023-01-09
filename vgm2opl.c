/*
 * vgm2opl.c
 * =========
 * 
 * Convert a VGM file storing OPL2 instructions into an OPL2 hardware
 * script that Retro-OPL can use.
 * 
 * The timing in the conversion is not perfect, but it should be good
 * enough.  VGM uses a control rate of 44,100 Hz.  This utility will use
 * a control rate of 980 Hz, which is exactly 1/45 times the VGM control
 * rate.
 * 
 * Program takes a two arguments.  First is the path to the VGM file.
 * Second is 1 to perform once, 2 to loop back once.  The OPL2 hardware
 * script is written to standard output.
 * 
 * If you have a compressed VGZ file, use gunzip to unzip it first, and
 * then run it through this program.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Constants
 * =========
 */

/*
 * The maximum size in bytes for the data included in a VGM file.
 * 
 * This is set to 16MB, which should definitely cover any historic VGM
 * file for the OPL2.
 */
#define MAX_DATA_SECTION UINT32_C(16*1024*1024)

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
 * The input file handle, or NULL.
 */
static FILE *pInput = NULL;

/*
 * The whole VGM data section read into memory, or NULL.
 */
static uint8_t *pData = NULL;

/*
 * The full length of the VGM data section.
 */
static uint32_t full_length = 0;

/*
 * Local functions
 * ===============
 */

/* Prototypes */
static void raiseErr(void);

static uint8_t readByte(void);
static uint32_t readDword(void);
static uint32_t readHead(int32_t offs);

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
 * Read a byte from the input file.
 * 
 * Return:
 * 
 *   the byte value read
 */
static uint8_t readByte(void) {
  int c = 0;
  
  /* Check state */
  if (pInput == NULL) {
    raiseErr();
  }
  
  /* Read a byte */
  c = fgetc(pInput);
  if (c == EOF) {
    fprintf(stderr, "%s: Failed to read byte!\n", pModule);
    raiseErr();
  }
  
  /* Return the byte */
  return (uint8_t) c;
}

/*
 * Read a 32-bit unsigned integer value in little-endian order from the
 * input file.
 * 
 * Return:
 * 
 *   the dword that was read
 */
static uint32_t readDword(void) {
  uint32_t retval = 0;
  
  retval = (uint32_t) readByte();
  retval = retval | (((uint32_t) readByte()) << 8);
  retval = retval | (((uint32_t) readByte()) << 16);
  retval = retval | (((uint32_t) readByte()) << 24);
  
  return retval;
}

/*
 * Read a 32-bit unsigned integer value in little-endian order from the
 * given file offset.
 * 
 * Parameters:
 * 
 *   offs - the byte offset of the dword
 * 
 * Return:
 * 
 *   the dword at that location
 */
static uint32_t readHead(int32_t offs) {
  /* Check parameters */
  if (offs < 0) {
    raiseErr();
  }
  
  /* Check state */
  if (pInput == NULL) {
    raiseErr();
  }
  
  /* Seek to the requested dword */
  if (fseek(pInput, (long) offs, SEEK_SET)) {
    fprintf(stderr, "%s: Input seek failed!\n", pModule);
    raiseErr();
  }
  
  /* Read the dword */
  return readDword();
}

/*
 * Program entrypoint
 * ==================
 */

int main(int argc, char *argv[]) {
  int i = 0;
  int rep_count = 0;
  const char *pPath = NULL;
  
  uint32_t file_ver = 0;
  uint32_t file_len = 0;
  uint32_t data_offs = 0;
  uint32_t data_len = 0;
  uint32_t loop_offs = 0;
  
  const uint8_t *pd = NULL;
  int wait_cmd = 0;
  int32_t wait_req = 0;
  int32_t samp_offs = 0;
  int32_t ctl_offs = 0;
  int32_t new_ctl = 0;
  double f = 0.0;
  
  /* Get the module name */
  pModule = NULL;
  if (argc > 0) {
    if (argv != NULL) {
      pModule = argv[0];
    }
  }
  if (pModule == NULL) {
    pModule = "vgm2opl";
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
    fprintf(stderr, "  vgm2opl [input.vgm] [r] > [output.opl2]\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "[input.vgm] is path to VGM file to read\n");
    fprintf(stderr, "[r] is 1 for no loop, 2 for loop once\n");
    fprintf(stderr, "OPL2 script written to standard output\n");
    fprintf(stderr, "\n");
    exit(1);
  }
  
  /* Check that two arguments beyond module name */
  if (argc != 3) {
    fprintf(stderr, "%s: Wrong number of program arguments!\n",
      pModule);
    raiseErr();
  }
  
  /* Get path and rep count */
  pPath = argv[1];
  
  if (strcmp(argv[2], "1") == 0) {
    rep_count = 1;
  } else if (strcmp(argv[2], "2") == 0) {
    rep_count = 2;
  } else {
    fprintf(stderr, "%s: Unrecognized repeat code '%s'!\n",
      pModule, argv[2]);
    raiseErr();
  }
  
  /* Open file */
  pInput = fopen(pPath, "rb");
  if (pInput == NULL) {
    fprintf(stderr, "%s: Failed to open file '%s'!\n",
            pModule, pPath);
    raiseErr();
  }
  
  /* Check file type */
  if (readHead(0) != 0x206d6756) {
    fprintf(stderr, "%s: Input file not a VGM file!\n",
            pModule);
    fprintf(stderr, "%s: (If you have a VGZ, decompress it!)\n",
            pModule);
    raiseErr();
  }
  
  /* Read the version number */
  file_ver = readHead(0x08);
  
  /* Read the file length from the header, adding four to adjust for
   * relative addressing */
  file_len = readHead(0x04) + 4;
  
  /* Get loop offset, or zero if no specific loop offset */
  loop_offs = readHead(0x1c) + 0x1c;
  if (loop_offs <= 0x1c) {
    loop_offs = 0;
  }
  
  /* Data offset is 0x40 unless version is at least 1.50, in which case
   * if header value 0x34 is non-zero, it stores 52 less than the data
   * offset */
  data_offs = 0x40;
  if (file_ver >= 0x150) {
    data_offs = readHead(0x34) + 0x34;
    if (data_offs <= 52) {
      data_offs = 0x40;
    }
  }
  
  /* If loop offset is zero, set it to data_offs */
  if (loop_offs <= 0) {
    loop_offs = data_offs;
  }
  
  /* Loop offset must be at least data_offs and less than file length */
  if ((loop_offs < data_offs) || (loop_offs >= file_len)) {
    fprintf(stderr, "%s: Invalid looping offset!\n", pModule);
    raiseErr();
  }
  
  /* Convert loop offset to be relative to data offset */
  loop_offs = loop_offs - data_offs;
  
  /* File length must be greater than data offset */
  if (file_len <= data_offs) {
    fprintf(stderr, "%s: Improper data offset and file length!\n",
            pModule);
    raiseErr();
  }
  
  /* Compute the data length and make sure it is within the limit */
  data_len = file_len - data_offs;
  if (data_len > MAX_DATA_SECTION) {
    fprintf(stderr, "%s: VGM file is too large!\n",
            pModule);
    raiseErr();
  }
  
  /* Store full length */
  full_length = data_len;
  
  /* Allocate the memory for the data section */
  pData = malloc((size_t) data_len);
  if (pData == NULL) {
    fprintf(stderr, "%s: Memory allocation failed!\n",
            pModule);
    raiseErr();
  }
  
  /* Seek to the data section */
  if (fseek(pInput, (long) data_offs, SEEK_SET)) {
    fprintf(stderr, "%s: Seek failed!\n",
            pModule);
    raiseErr();
  }
  
  /* Read data section into memory */
  if (fread(pData, 1, (size_t) data_len, pInput) != data_len) {
    fprintf(stderr, "%s: Read failed!\n",
            pModule);
    raiseErr();
  }
  
  /* Close file if open */
  if (pInput != NULL) {
    fclose(pInput);
    pInput = NULL;
  }
  
  /* Write the OPL2 header */
  printf("OPL2 980\n");
  
  /* Perform the loops */
  for(i = 0; i < rep_count; i++) {
  
    /* Start at beginning of data and reset data length */
    pd = pData;
    data_len = full_length;
    
    /* If this is a loop, advance the loop offset */
    if (i > 0) {
      pd += loop_offs;
      data_len -= loop_offs;
    }
    
    /* Keep processing while data remains in data section */
    while (data_len > 0) {
      /* Handle the different commands */
      if (*pd == 0x66) {
        /* End of sound data -- leave loop */
        break;
        
      } else if ((*pd >= 0x70) && (*pd <= 0x7f)) {
        /* Shorthand wait command */
        wait_cmd = 1;
        wait_req = (*pd - 0x70) + 1;
        
      } else if (*pd == 0x63) {
        /* Wait 882 samples */
        wait_cmd = 1;
        wait_req = 882;
        
      } else if (*pd == 0x62) {
        /* Wait 735 samples */
        wait_cmd = 1;
        wait_req = 735;
        
      } else if (*pd == 0x61) {
        /* General wait command -- must be at least three bytes in data
         * section still */
        if (data_len < 3) {
          fprintf(stderr, "%s: VGM opcode missing parameters!\n",
                  pModule);
          raiseErr();
        }
        
        /* Get the wait request */
        wait_cmd = 1;
        wait_req = ((int32_t) pd[1]) | (((int32_t) pd[2]) << 8);
        
        /* Advance two bytes to account for the parameters */
        pd += 2;
        data_len -= 2;
        
      } else if (*pd == 0x5a) {
        /* OPL2 register write -- start by clearing wait request flag */
        wait_cmd = 0;
        
        /* Must be at least three bytes in data section still */
        if (data_len < 3) {
          fprintf(stderr, "%s: VGM opcode missing parameters!\n",
                  pModule);
          raiseErr();
        }
        
        /* Produce the OPL2 hardware r command */
        printf("r %02x %02x\n",
          (unsigned int) pd[1],
          (unsigned int) pd[2]);
        
        /* Advance two bytes to account for the parameters */
        pd += 2;
        data_len -= 2;
        
      } else {
        /* Unsupported opcode */
        fprintf(stderr, "%s: Unsupported VGM opcode 0x%02x!\n",
                pModule, (unsigned int) *pd);
        raiseErr();
      }
      
      /* If we have a wait command, but request is zero, then turn off
       * the request */
      if (wait_cmd) {
        if (wait_req < 1) {
          wait_cmd = 0;
        }
      }
      
      /* If we have a wait command, handle it */
      if (wait_cmd) {
        /* Update sample offset, watching for overflow */
        if (samp_offs <= INT32_MAX - wait_req) {
          samp_offs += wait_req;
        } else {
          fprintf(stderr, "%s: Sample count overflow!\n", pModule);
          raiseErr();
        }
        
        /* Compute the position at control rate of 980 relative to VGM
         * control rate of 44100 */
        f = (((double) samp_offs) * 980.0) / 44100.0;
        
        /* Floor and make sure within integer range */
        f = floor(f);
        if (!((f >= 0) && (f <= (double) INT32_MAX))) {
          fprintf(stderr, "%s: Numeric problem!\n", pModule);
          raiseErr();
        }
        
        /* Get new control offset */
        new_ctl = (int32_t) f;
        
        /* If new control offset is ahead of current, insert appropriate
         * wait command and update control offset */
        if (new_ctl > ctl_offs) {
          printf("w %ld\n", (long) (new_ctl - ctl_offs));
          ctl_offs = new_ctl;
        }
      }
      
      /* Advance in the data section */
      pd++;
      data_len--;
    }
  }
  
  /* Free data section if allocated */
  pd = NULL;
  if (pData != NULL) {
    free(pData);
    pData = NULL;
  }
  
  /* Return successfully if we got here */
  return 0;
}
