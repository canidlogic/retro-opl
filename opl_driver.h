#ifndef OPL_DRIVER_H_INCLUDED
#define OPL_DRIVER_H_INCLUDED

/*
 * opl_driver.h
 * ============
 * 
 * Unified header for all OPL emulation drivers.
 * 
 * Each OPL emulation driver has a different C source file
 * implementation of this header.
 */

#include <stddef.h>
#include <stdint.h>

/*
 * Initialize the driver.
 * 
 * This is called at the start of emulation.  The provided sample_rate
 * must either be 44100 or 48000.  The sample rate is specified in Hz.
 * 
 * Parameters:
 * 
 *   sample_rate - the sample rate, either 44100 or 48000
 */
void opl_init(int32_t sample_rate);

/*
 * Finish the driver.
 * 
 * This is called at the end of emulation.
 */
void opl_finish(void);

/*
 * Write a register in the emulated OPL hardware.
 * 
 * Parameters:
 * 
 *   reg - the OPL hardware register index
 * 
 *   val - the unsigned byte value to write (0-255)
 */
void opl_write(int32_t reg, int32_t val);

/*
 * Generate samples in the emulated OPL hardware using the current state
 * of the emulated hardware registers.
 * 
 * Parameters:
 * 
 *   pbuf - pointer to the sample buffer to fill
 * 
 *   count - the number of (one-channel) PCM samples to generate and
 *   write into the buffer; must be greater than zero
 */
void opl_generate(int16_t *pbuf, int32_t count);

#endif
