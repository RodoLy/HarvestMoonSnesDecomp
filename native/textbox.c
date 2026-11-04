/*
 * textbox.c — the game's own text box, drawn by the host.
 *
 * The game prints its talks on BG3 from texts in the ROM; someone it does
 * not know about (the wife's offer, the Goddess) cannot go through it, so
 * the box is drawn here to look and sound the same:
 *
 *   - the frame: 240 x 64 at (8, 151), a lilac and white rounded edge,
 *     see-through checks inside (every other pixel dark);
 *   - the letters: the game's own font (gamefont.c, read from the ROM),
 *     white, three lines 16 pixels apart from (16, 159);
 *   - the printing: one letter every 4 frames, every frame while A is held,
 *     as the game's printer does ($84:CF38), each with the game's letter
 *     sound (host_text_sound), at most every 4th letter when it runs fast;
 *   - the arrow in the bottom right corner, blinking, when there is more.
 *
 * Words are in English, as the game's: its font has no accents.
 */
#include <string.h>

#include "config.h"
#include "ui.h"
#include "gamefont.h"
#include "textbox.h"

#define BOX_LILAC UI_RGB(0x8484B5)
#define BOX_WHITE UI_RGB(0xFFFFFF)
#define BOX_DARK  UI_RGB(0x424252)
enum { BOX_X = 8, BOX_Y = 151, BOX_W = 240, BOX_H = 64, TEXT_X = 16, TEXT_Y = 159, LINE_H = 16 };

void textbox_frame(int ox)
{
  static const char *corner[10] = {
    "......LLLL", "....LLLWWW", "...LLWWWWW", "..LLWWWWLL", ".LLWWWLL..",
    ".LWWWL....", "LLWWL.....", "LWWWL.....", "LWWL......", "LWWL......" };
  static const char edge[4] = { 'L', 'W', 'W', 'L' };
  int x0 = ox + BOX_X, y0 = BOX_Y, w = BOX_W, h = BOX_H;
  for (int y = y0 + 4; y < y0 + h - 4; y++)          /* the checks */
    for (int x = x0 + 4; x < x0 + w - 4; x++)
      if ((x + y) & 1) ui_px(x, y, BOX_DARK);
  for (int i = 0; i < 4; i++) {                      /* the sides */
    uint32_t c = edge[i] == 'L' ? BOX_LILAC : BOX_WHITE;
    ui_hline(x0 + 10, y0 + i, w - 20, c); ui_hline(x0 + 10, y0 + h - 1 - i, w - 20, c);
    ui_vline(x0 + i, y0 + 10, h - 20, c); ui_vline(x0 + w - 1 - i, y0 + 10, h - 20, c);
  }
  for (int j = 0; j < 10; j++)                       /* the corners, mirrored */
    for (int i = 0; i < 10; i++) {
      char k = corner[j][i];
      if (k == '.') {
        if (j >= 4 && i >= 4 && j + i >= 9) {           /* inside: checks */
          if ((x0 + i + y0 + j) & 1) ui_px(x0 + i, y0 + j, BOX_DARK);
          if ((x0 + w - 1 - i + y0 + j) & 1) ui_px(x0 + w - 1 - i, y0 + j, BOX_DARK);
          if ((x0 + i + y0 + h - 1 - j) & 1) ui_px(x0 + i, y0 + h - 1 - j, BOX_DARK);
          if ((x0 + w - 1 - i + y0 + h - 1 - j) & 1) ui_px(x0 + w - 1 - i, y0 + h - 1 - j, BOX_DARK);
        }
        continue;
      }
      uint32_t c = k == 'L' ? BOX_LILAC : BOX_WHITE;
      ui_px(x0 + i, y0 + j, c); ui_px(x0 + w - 1 - i, y0 + j, c);
      ui_px(x0 + i, y0 + h - 1 - j, c); ui_px(x0 + w - 1 - i, y0 + h - 1 - j, c);
    }
}

void textbox_text(int x, int y, const char *t, int *left)
{
  char part[64];
  size_t n = 0;
  const char *p = t;
  while (*p && *left > 0 && n < sizeof part - 4) {
    const char *q = p;
    ui_utf8_next(&q);
    while (p < q) part[n++] = *p++;
    (*left)--;
  }
  part[n] = 0;
  gamefont_text_box(x, y, part, BOX_WHITE, BOX_DARK);
}

void textbox_line(int ox, int line, const char *t, int *left)
{
  textbox_text(ox + TEXT_X, TEXT_Y + LINE_H * line, t, left);
}

void textbox_more(int ox, unsigned frame)
{
  if ((frame / 16) & 1) return;                      /* blinking */
  int ax = ox + BOX_X + BOX_W - 18, ay = BOX_Y + BOX_H - 13;
  for (int k = 0; k < 4; k++) ui_hline(ax + k, ay + k, 7 - 2 * k, BOX_WHITE);   /* pointing down */
}

int textbox_letters(const char *s)
{
  int n = 0;
  while (s && *s) { ui_utf8_next(&s); n++; }
  return n;
}

bool textbox_print(TextboxPrinter *p, int total, const UiInput *in)
{
  if (p->shown >= total) return true;
  bool quick = g_cfg.fast_text || (in->held & B_A) || (in->held & B_B);
  if (quick || ++p->tick >= 4) {
    p->tick = 0;
    p->shown++;
    static int n;
    if (!quick || (++n & 3) == 0) host_text_sound();
  }
  return false;
}

/* ── A talk: pages of lines ────────────────────────────────────────────── */
static int page_letters(const TextboxTalk *t)
{
  int n = 0;
  for (int i = 0; i < TEXTBOX_LINES; i++) n += textbox_letters(t->lines[t->page][i]);
  return n;
}

void textbox_talk_open(TextboxTalk *t)
{
  t->page = 0;
  t->printer.shown = t->printer.tick = 0;
  t->frame = 0;
}

bool textbox_talk_update(TextboxTalk *t, const UiInput *in)
{
  t->frame++;
  if (!textbox_print(&t->printer, page_letters(t), in)) return false;
  if (!(in->accept || in->click || in->back || in->rclick)) return false;
  if (++t->page >= t->pages) return true;
  t->printer.shown = t->printer.tick = 0;
  return false;
}

void textbox_talk_draw(TextboxTalk *t, uint32_t *dst, int w)
{
  ui_target(dst, w, 224);
  int ox = (w - 256) / 2;
  textbox_frame(ox);
  int left = t->printer.shown;
  for (int i = 0; i < TEXTBOX_LINES; i++)
    if (t->lines[t->page][i]) textbox_line(ox, i, t->lines[t->page][i], &left);
  if (t->printer.shown >= page_letters(t) && t->page + 1 < t->pages) textbox_more(ox, t->frame);
}
