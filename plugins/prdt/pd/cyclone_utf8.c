/* cyclone's shared/control/s_cycloneutf8.c copies Pd's s_utf8.c, whose functions then link twice; these
 * are the three it renames, forwarded to Pd's. */
#include <stdint.h>
#include "s_utf8.h"

int cyclone_u8_wc_nbytes(uint32_t ch) { return u8_wc_nbytes(ch); }
void cyclone_u8_inc(const char *s, int *i) { u8_inc(s, i); }
void cyclone_u8_dec(const char *s, int *i) { u8_dec(s, i); }
