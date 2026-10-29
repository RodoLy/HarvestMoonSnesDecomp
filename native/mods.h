/* mods.h — the recompilation's improvements, for the native build:
 * cheats, fast text, longer days, OPTIONS on the title screen, widescreen
 * and the endless game. Which are on is g_cfg (config.h). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* At every frame's end, before it is drawn. */
void mods_frame(uint8_t *ram);
/* The pads the game will read next frame (the title's OPTIONS entry moves
 * its cursor through them); true in *open_launcher when A was pressed on
 * OPTIONS. */
uint16_t mods_pad(uint16_t pad, bool *open_launcher);

/* Widescreen: the width of the picture (256, or 342 while it is on) and the
 * margins to draw this frame (0 on screens that are not a map). */
int mods_field_width(void);
void mods_ws_margins(int *left, int *right);
enum { WS_BUDGET = 43 };

/* The endless game. The host keeps a copy of the whole machine at the
 * start of each GameLoop frame while mods_endless_keep() says so, and
 * loads it back when mods_endless_restore() does (once: it then returns
 * false until the next time). */
bool mods_endless_keep(void);
bool mods_endless_restore(void);
void mods_endless_kept(bool ok);       /* the copy was (or could not be) made */
void mods_endless_restored(void);      /* it was loaded back */
void mods_reset(void);                 /* a new game / ROM: forget everything */
