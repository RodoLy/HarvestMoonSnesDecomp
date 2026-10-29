/* ui.h — the native build's own screens (launcher, pause menu): a small
 * pixel canvas at the console's resolution, drawn in the look of the game's
 * menus (wooden frames, parchment, a pointing hand), shown enlarged. */
#pragma once
#include <stdbool.h>
#include <stdint.h>


/* ── The canvas: 0xFFRRGGBB pixels; alpha 0 is "do not draw" ─────────── */
extern uint32_t *g_ui_px;
extern int g_ui_w, g_ui_h;
void ui_target(uint32_t *px, int w, int h);

#define UI_RGB(c) (0xFF000000u | (uint32_t)(c))
#define C_CHECK_A    UI_RGB(0xD9A867)
#define C_CHECK_B    UI_RGB(0xC4904F)
#define C_WOOD_EDGE  UI_RGB(0x3A1E0C)
#define C_WOOD_DARK  UI_RGB(0x7A4320)
#define C_WOOD       UI_RGB(0xA4622E)
#define C_WOOD_LIGHT UI_RGB(0xD08F4C)
#define C_WOOD_SHINE UI_RGB(0xEDB873)
#define C_PAPER      UI_RGB(0xF7E7BC)
#define C_PAPER_DARK UI_RGB(0xE4CB8E)
#define C_PAPER_LINE UI_RGB(0xD2B272)
#define C_INK        UI_RGB(0x4A2812)
#define C_INK_SOFT   UI_RGB(0x8A6236)
#define C_CREAM      UI_RGB(0xFFF4D6)
#define C_GREEN      UI_RGB(0x3E9A3C)
#define C_GREEN_DARK UI_RGB(0x1F5E22)
#define C_BLUE       UI_RGB(0x3A70D0)
#define C_BLUE_DARK  UI_RGB(0x1C3C7A)
#define C_RED        UI_RGB(0xD8382E)
#define C_GOLD       UI_RGB(0xE8B030)
#define C_GOLD_DARK  UI_RGB(0x9A6A10)
#define C_SELECT     UI_RGB(0xFFF08A)
#define C_AMBER      UI_RGB(0xF2B33D)

static inline void ui_px(int x, int y, uint32_t c)
{
  if ((unsigned)x < (unsigned)g_ui_w && (unsigned)y < (unsigned)g_ui_h && (c >> 24))
    g_ui_px[y * g_ui_w + x] = c;
}
void ui_rect(int x, int y, int w, int h, uint32_t c);
void ui_hline(int x, int y, int w, uint32_t c);
void ui_vline(int x, int y, int h, uint32_t c);
void ui_dim(int x, int y, int w, int h, int shift);     /* darken: >> shift */
void ui_frame(int x, int y, int w, int h, uint32_t fill);   /* wood around `fill` */
void ui_slot(int x, int y, int sz, uint32_t c, uint32_t cdark, bool sel);
void ui_icon(int x, int y, uint8_t id);                 /* a 16x16 item icon */
void ui_image(int x, int y, const uint32_t *p, int w, int h);
void ui_hand(int x, int y);                             /* the pointing-hand cursor */
void ui_heart(int x, int y, uint32_t fill, bool full);  /* 7x6 */
void ui_checker(void);                                  /* the pause menu's floor */
void ui_scene(uint32_t t);                              /* a farm at night (t: frames) */
void ui_switch(int x, int y, bool on);                  /* an on/off toggle, 20x9 */
void ui_bar(int x, int y, int w, int v, int max);        /* a level, 5 high */

/* ── Pixel text (`y` is the baseline; UTF-8) ─────────────────────────── */
enum { UI_BODY, UI_SMALL };          /* Pixelify Sans 15 px, Tiny5 9 px (OFL) */
int ui_text_w(int f, const char *t);
int ui_text(int f, int x, int y, const char *t, uint32_t col);
int ui_text_sh(int f, int x, int y, const char *t, uint32_t col);
void ui_text_r(int f, int rx, int y, const char *t, uint32_t col);
void ui_text_c(int f, int cx, int y, const char *t, uint32_t col);
int ui_text_big(int x, int y, const char *t, uint32_t col, uint32_t edge);   /* body, doubled */
int ui_text_big_w(const char *t);
/* wrapped to `w`; lines `lh` apart; at most `max` lines; returns lines used */
int ui_text_wrap(int f, int x, int y, int w, int lh, int max, const char *t, uint32_t col);
uint32_t ui_utf8_next(const char **p);
