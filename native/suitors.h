/* suitors.h — the men of the town who can be courted (an improvement, off
 * by default): their hearts, gifts and lines. suitors.c. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "host.h"

void suitors_frame(void);                    /* the end of every frame: on or off, a new day */
void suitors_reset(void);                    /* a new game, a reset, another ROM */
uint16_t suitors_pad(uint16_t pad);          /* the buttons the game is given: A in front of him talks */
bool suitors_dialog_wanted(void);            /* he was just spoken to */
void suitors_dialog_open(void);
bool suitors_dialog_update(const UiInput *in);   /* true: the talk is over */
void suitors_dialog_draw(uint32_t *dst, int w);
