/* pause.h — the Harvest Moon 64-style pause menu (pause.c). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

bool pause_can_open(void);          /* a diary is loaded */
void pause_open(void);
bool pause_is_open(void);
/* u d l r (move), L R (pages), A X Y, B (close) */
void pause_action(char a);
void pause_draw(uint32_t *dst, int dst_w);
/* the mouse, with mouse controls on: (mx, my) in the menu's 256 x 224
 * (-1: outside); a left click is A where it points, the right click closes */
void pause_mouse(int mx, int my, bool moved, bool click, bool rclick, int wheel);
/* a second screen: the menu as it is now, 256 x 224 (0xFFRRGGBB), while the
 * game goes on; a touch there (action 0 down, 1 move, 2 up) */
void pause_live_render(uint32_t *dst);
void pause_live_touch(int action, int x, int y);
