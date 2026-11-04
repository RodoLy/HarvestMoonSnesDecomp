/* goddess.h — the Harvest Goddess's hearts (an improvement, off by default).
 * goddess.c. */
#pragma once
#include <stdint.h>
enum { GODDESS_HINTED = 0x01, GODDESS_ENGAGED = 0x02, GODDESS_MARRIED = 0x04 };
void goddess_frame(void);   /* the end of every frame: the improvement on or off */
void goddess_married_tick(uint8_t *ram);   /* every frame, once married: her day, her lines */
void goddess_reset(void);   /* a new game, a reset, another ROM */
