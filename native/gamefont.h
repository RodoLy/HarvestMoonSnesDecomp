/* gamefont.h — text in the game's own font, from the ROM (gamefont.c),
 * drawn on the current ui canvas (ui_target). Letters are 8 x 16; `y` is
 * the top. */
#pragma once
#include <stdint.h>

int gamefont_text(int x, int y, const char *s, uint32_t ink);   /* the width drawn */
int gamefont_width(const char *s);
int gamefont_wrap(int x, int y, int w, const char *s, uint32_t ink); /* lines, 16 apart */
