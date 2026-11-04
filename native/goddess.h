/* goddess.h — the Harvest Goddess (an improvement, off by default): her
 * hearts, the proposal, the wedding and married life.
 * goddess.c. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "host.h"
enum { GODDESS_HINTED = 0x01, GODDESS_ENGAGED = 0x02, GODDESS_MARRIED = 0x04 };
void goddess_frame(void);   /* the end of every frame: the improvement on or off */
void goddess_married_tick(uint8_t *ram);   /* every frame: the wedding, then where she is */
void goddess_reset(void);   /* a new game, a reset, another ROM */
uint16_t goddess_pad(uint16_t pad);          /* the buttons the game is given: A in front of her talks */
bool goddess_dialog_wanted(void);            /* she was just spoken to */
void goddess_dialog_open(void);
bool goddess_dialog_update(const UiInput *in);   /* true: the talk is over */
void goddess_dialog_draw(uint32_t *dst, int w);
