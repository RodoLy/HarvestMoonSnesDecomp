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
/* the improvements' own marks over the picture (the seeds left; px NULL:
 * only whether there are any); false: none */
bool mods_hud(uint32_t *px, int w);
/* mouse controls: the pointer on the field (-1: outside), a button (0 none,
 * 1 left, 2 right) going down or up; the buttons it presses this frame */
void mods_mouse(int fx, int fy, int button, bool down);
uint16_t mods_mouse_pad(uint16_t pad);
/* the crop bag: key = its key is down (Q, left trigger, middle button) */
uint16_t mods_bag_pad(uint16_t pad, bool key);
/* for the pause menu's Bag tab: the items (0 = empty) and how many; take
 * item i out when the game goes on; its 12x12 picture */
int mods_bag_items(uint8_t out[9]);
bool mods_bag_choose(int i);
void mods_bag_icon(int x, int y, uint8_t it);
/* an item's own sprite (case: SUB_8180B7's, 1 = carried), 16 x 16, colours
 * from the game's palettes (0 = see-through); false if it has none */
bool mods_item_sprite(uint8_t it, int kase, uint32_t out[256]);
/* any sprite of the game (its first frame), centred in w x h */
bool mods_sprite(uint16_t sprite, bool flip, uint32_t *out, int w, int h);
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
