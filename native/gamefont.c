/*
 * gamefont.c — text in the game's own font, read from your ROM (as the
 * game's text boxes print it, src/text.c FindLetterGraphics): letters of
 * 8 x 16 pixels, two 2-bit tiles one above the other (the bottom one $100
 * bytes after), in groups of 64 (long pointers at $83:98AE). Colour 3 is
 * the ink.
 *
 * Codes: a-z $00-$19, A-Z $1A-$33, then ' ! . ? " / , ( ) % - ... & :
 * ($34-$41), digits $BC-$C5. The game has no accents: á é í ó ú ü ñ and
 * ¿ ¡ are made here from its own letters (a mark above the letter, the
 * marks turned over), so Spanish and Italian read right in its font.
 */
#include <string.h>

#include "hm_decomp.h"
#include "native.h"
#include "ui.h"
#include "gamefont.h"

#define FONT_GROUPS 0x8398AEu

static void glyph(uint8_t code, uint8_t px[16][8])
{
  unsigned g = code >> 6;
  uint32_t base = hm_bus_rd16(g_ram, FONT_GROUPS + 3 * g) | (uint32_t)hm_bus_rd8(g_ram, FONT_GROUPS + 3 * g + 2) << 16;
  uint32_t a = base + ((code & 0x3Fu) >> 3) * 0x200u + (code & 7u) * 32u;
  for (int half = 0; half < 2; half++)
    for (int y = 0; y < 8; y++) {
      uint8_t lo = hm_bus_rd8(g_ram, a + half * 0x100u + 2u * y), hi = hm_bus_rd8(g_ram, a + half * 0x100u + 2u * y + 1);
      for (int x = 0; x < 8; x++) px[half * 8 + y][x] = (uint8_t)(((lo >> (7 - x)) & 1) | ((hi >> (7 - x)) & 1) << 1);
    }
}
static int top_ink(uint8_t px[16][8])
{
  for (int y = 0; y < 16; y++) for (int x = 0; x < 8; x++) if (px[y][x] == 3) return y;
  return 6;
}
/* the mark over a letter: 'a' acute, 'd' dieresis, 'g' grave, 't' tilde */
static void mark(uint8_t px[16][8], char kind, bool dotless)
{
  if (dotless)                                    /* i: its dot goes */
    for (int y = 0; y < 16; y++) {
      bool row = false;
      for (int x = 0; x < 8; x++) if (px[y][x] == 3) row = true;
      if (!row) continue;
      bool next = false;
      for (int x = 0; x < 8 && y + 1 < 16; x++) if (px[y + 1][x] == 3) next = true;
      for (int x = 0; x < 8; x++) if (px[y][x] == 3) px[y][x] = 0;
      if (!next) break;                           /* the dot was this row (and maybe the one above) */
    }
  int t = top_ink(px) - 3;
  if (t < 0) t = 0;
  switch (kind) {
  case 'a': px[t][4] = px[t][5] = 3; px[t + 1][3] = px[t + 1][4] = 3; break;
  case 'g': px[t][2] = px[t][3] = 3; px[t + 1][3] = px[t + 1][4] = 3; break;
  case 'd': px[t + 1][2] = px[t + 1][5] = 3; break;
  case 't': px[t][2] = px[t][3] = px[t][6] = 3; px[t + 1][1] = px[t + 1][4] = px[t + 1][5] = 3; break;
  }
}
static void turn(uint8_t px[16][8])               /* upside down (¿ ¡), at the same height */
{
  uint8_t c[16][8];
  memcpy(c, px, sizeof c);
  int top = 16, bottom = -1;
  for (int y = 0; y < 16; y++) for (int x = 0; x < 8; x++) if (c[y][x] == 3) { if (y < top) top = y; bottom = y; }
  int shift = bottom < 0 ? 0 : top - (15 - bottom);
  memset(px, 0, 16 * 8);
  for (int y = 0; y < 16; y++)
    for (int x = 0; x < 8; x++) {
      int ty = 15 - y + shift;
      if (ty >= 0 && ty < 16) px[ty][7 - x] = c[y][x];
    }
}

/* the next character of `s` as a glyph; false: nothing to draw (a space) */
static bool next_glyph(const char **s, uint8_t px[16][8])
{
  uint32_t c = ui_utf8_next(s);
  if (c >= 'a' && c <= 'z') { glyph((uint8_t)(c - 'a'), px); return true; }
  if (c >= 'A' && c <= 'Z') { glyph((uint8_t)(0x1A + c - 'A'), px); return true; }
  if (c >= '0' && c <= '9') { glyph((uint8_t)(0xBC + c - '0'), px); return true; }
  static const struct { uint32_t c; uint8_t code; } kPunct[] = {
    { '\'', 0x34 }, { 0x2019, 0x34 }, { '!', 0x35 }, { '.', 0x36 }, { '?', 0x37 }, { '"', 0x38 },
    { '/', 0x39 }, { ',', 0x3A }, { '(', 0x3B }, { ')', 0x3C }, { '%', 0x3D }, { '-', 0x3E },
    { 0x2026, 0x3F }, { '&', 0x40 }, { ':', 0x41 },
  };
  for (size_t i = 0; i < sizeof kPunct / sizeof kPunct[0]; i++)
    if (c == kPunct[i].c) { glyph(kPunct[i].code, px); return true; }
  static const struct { uint32_t c; char base, kind; } kMarked[] = {
    { 0xE1, 'a', 'a' }, { 0xE9, 'e', 'a' }, { 0xED, 'i', 'a' }, { 0xF3, 'o', 'a' }, { 0xFA, 'u', 'a' },
    { 0xE0, 'a', 'g' }, { 0xE8, 'e', 'g' }, { 0xEC, 'i', 'g' }, { 0xF2, 'o', 'g' }, { 0xF9, 'u', 'g' },
    { 0xFC, 'u', 'd' }, { 0xF1, 'n', 't' },
    { 0xC1, 'A', 'a' }, { 0xC9, 'E', 'a' }, { 0xCD, 'I', 'a' }, { 0xD3, 'O', 'a' }, { 0xDA, 'U', 'a' }, { 0xD1, 'N', 't' },
  };
  for (size_t i = 0; i < sizeof kMarked / sizeof kMarked[0]; i++)
    if (c == kMarked[i].c) {
      char b = kMarked[i].base;
      glyph((uint8_t)(b >= 'a' ? b - 'a' : 0x1A + b - 'A'), px);
      mark(px, kMarked[i].kind, b == 'i');
      return true;
    }
  if (c == 0xBF) { glyph(0x37, px); turn(px); return true; }   /* ¿ */
  if (c == 0xA1) { glyph(0x35, px); turn(px); return true; }   /* ¡ */
  return false;                                                /* a space, or a letter it has not */
}

int gamefont_width(const char *s)
{
  int n = 0;
  while (*s) { ui_utf8_next(&s); n++; }
  return n * 8;
}

int gamefont_text(int x, int y, const char *s, uint32_t ink)
{
  int x0 = x;
  while (*s) {
    uint8_t px[16][8];
    if (next_glyph(&s, px))
      for (int j = 0; j < 16; j++)
        for (int i = 0; i < 8; i++) if (px[j][i] == 3) ui_px(x + i, y + j, ink);
    x += 8;
  }
  return x - x0;
}

int gamefont_wrap(int x, int y, int w, const char *s, uint32_t ink)
{
  char line[160];
  int lines = 0, max = w / 8;
  while (*s) {
    /* as many whole words as fit */
    const char *p = s, *cut = NULL;
    int n = 0;
    while (*p) {
      const char *q = p;
      uint32_t c = ui_utf8_next(&q);
      if (c == ' ') cut = p;
      if (++n > max) break;
      p = q;
    }
    const char *end = *p && cut ? cut : p;
    size_t len = (size_t)(end - s);
    if (len >= sizeof line) len = sizeof line - 1;
    memcpy(line, s, len); line[len] = 0;
    gamefont_text(x, y + 16 * lines, line, ink);
    lines++;
    s = end;
    while (*s == ' ') s++;
  }
  return lines;
}
