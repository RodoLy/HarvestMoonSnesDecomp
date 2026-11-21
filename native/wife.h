/* wife.h — the wife helps on the farm (an improvement, off by default).
 * wife.c. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "hm_decomp.h"
#include "host.h"

/* at the start of every GameLoop frame (nothing of the game half done):
 * `regs` are the game's own, copied before any routine is called */
void wife_clean_point(uint8_t *ram, const HmRegs *regs);
bool wife_dialog_wanted(void);             /* she has just offered to help */
void wife_dialog_open(void);
bool wife_dialog_update(const UiInput *in); /* true: closed */
void wife_dialog_draw(uint32_t *dst, int w);
void wife_reset(void);                      /* a new game, a reset, another ROM */
void wife_frame(uint8_t *ram);              /* the end of every frame */
uint16_t wife_pad(uint16_t pad);            /* the buttons the game is given */

/* Someone the game does not know as the wife standing in for one (the male
 * suitors' husband, native/suitors.c): `girl` is whose scripts the game runs
 * for him (0-4, -1 none) and `hearts` his love, as hers would be. Called
 * every frame by the one standing in; nobody when a girl is the wife. */
void wife_stand_in(int girl, int hearts);
void wife_talked_to(void);                  /* he was just talked to (his own box, not the game's) */
