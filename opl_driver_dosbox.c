/*
 * opl_driver_dosbox.c
 * ===================
 * 
 * Implementation of opl_driver.h using the DOSBox OPL emulator.
 * 
 * You must compile this with the opl.c source file containing the
 * DOSBox OPL emulator, and this implementation file must have the opl.h
 * header from the DOSBox OPL emulator in the include path while
 * compiling.
 */

#include "opl_driver.h"
#include "opl.h"

#include <stdlib.h>

/*
 * Public function implementations
 * ===============================
 * 
 * See header for specifications.
 */

/*
 * opl_init function.
 */
void opl_init(int32_t sample_rate) {
  /* Call through */
  adlib_init((Bit32u) sample_rate);
}

/*
 * opl_finish function.
 */
void opl_finish(void) {
  /* Nothing required */
}

/*
 * opl_write function.
 */
void opl_write(int32_t reg, int32_t val) {
  /* Call through */
  adlib_write((Bitu) reg, (Bit8u) val);
}

/*
 * opl_generate function.
 */
void opl_generate(int16_t *pbuf, int32_t count) {
  /* Call through */
  adlib_getsample((Bit16s *) pbuf, (Bits) count);
}
