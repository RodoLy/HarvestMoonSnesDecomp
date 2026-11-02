/* ui.c — drawing for the native build's own screens (see ui.h).
 *
 * The frames, slots and type follow the recompilation's pause menu
 * (HarvestMoonSNESRecomp, hm_pause.c): the same wood and parchment, the
 * same pixel fonts (Pixelify Sans and Tiny5, SIL Open Font License, see
 * fonts/) and item icons (original drawings, hm_icons.h). The night farm
 * behind the launcher is drawn here from plain shapes: no game graphics. */
#include <math.h>
#include <string.h>

#include "ui.h"
#include "hm_pixfont.h"
#include "hm_icons.h"

uint32_t *g_ui_px;
int g_ui_w, g_ui_h;

void ui_target(uint32_t *px, int w, int h) { g_ui_px = px; g_ui_w = w; g_ui_h = h; }

void ui_rect(int x, int y, int w, int h, uint32_t c)
{
  if (!(c >> 24)) return;
  int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
  int x1 = x + w > g_ui_w ? g_ui_w : x + w, y1 = y + h > g_ui_h ? g_ui_h : y + h;
  for (int j = y0; j < y1; j++)
    for (int i = x0; i < x1; i++) g_ui_px[j * g_ui_w + i] = c;
}
void ui_hline(int x, int y, int w, uint32_t c) { ui_rect(x, y, w, 1, c); }
void ui_vline(int x, int y, int h, uint32_t c) { ui_rect(x, y, 1, h, c); }
void ui_dim(int x, int y, int w, int h, int shift)
{
  int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
  int x1 = x + w > g_ui_w ? g_ui_w : x + w, y1 = y + h > g_ui_h ? g_ui_h : y + h;
  uint32_t m = shift == 1 ? 0x7F7F7F : shift == 2 ? 0x3F3F3F : 0x1F1F1F;
  for (int j = y0; j < y1; j++)
    for (int i = x0; i < x1; i++) {
      uint32_t *p = &g_ui_px[j * g_ui_w + i];
      *p = 0xFF000000u | ((*p >> shift) & m);
    }
}

void ui_frame(int x, int y, int w, int h, uint32_t fill)
{
  ui_rect(x + 2, y, w - 4, h, C_WOOD_EDGE);
  ui_rect(x, y + 2, w, h - 4, C_WOOD_EDGE);
  ui_rect(x + 1, y + 1, w - 2, h - 2, C_WOOD_EDGE);
  ui_rect(x + 2, y + 1, w - 4, h - 2, C_WOOD);
  ui_rect(x + 1, y + 2, w - 2, h - 4, C_WOOD);
  ui_hline(x + 2, y + 1, w - 4, C_WOOD_SHINE);
  ui_hline(x + 2, y + 2, w - 4, C_WOOD_LIGHT);
  ui_vline(x + 1, y + 2, h - 4, C_WOOD_LIGHT);
  ui_hline(x + 2, y + h - 2, w - 4, C_WOOD_DARK);
  ui_vline(x + w - 2, y + 2, h - 4, C_WOOD_DARK);
  for (int i = x + 6; i < x + w - 6; i += 9) ui_px(i, y + 2, C_WOOD), ui_px(i + 3, y + h - 3, C_WOOD_DARK);
  ui_rect(x + 3, y + 3, w - 6, h - 6, C_WOOD_EDGE);
  ui_rect(x + 4, y + 4, w - 8, h - 8, fill);
  if (fill == C_PAPER) {
    ui_hline(x + 4, y + 4, w - 8, C_PAPER_DARK);
    ui_vline(x + 4, y + 5, h - 9, C_PAPER_DARK);
  }
}

void ui_slot(int x, int y, int sz, uint32_t c, uint32_t cdark, bool sel)
{
  ui_rect(x + 1, y, sz - 2, sz, C_WOOD_EDGE);
  ui_rect(x, y + 1, sz, sz - 2, C_WOOD_EDGE);
  ui_rect(x + 1, y + 1, sz - 2, sz - 2, c);
  ui_hline(x + 2, y + sz - 2, sz - 4, cdark);
  ui_vline(x + sz - 2, y + 2, sz - 4, cdark);
  ui_rect(x + 3, y + 3, sz - 6, sz - 6, C_WOOD_EDGE);
  ui_rect(x + 4, y + 4, sz - 8, sz - 8, sel ? C_SELECT : C_PAPER);
  ui_hline(x + 4, y + 4, sz - 8, sel ? C_GOLD : C_PAPER_DARK);
}

void ui_icon(int x, int y, uint8_t id)
{
  if (id == 0 || id >= HM_ICON_COUNT) return;
  ui_image(x, y, kHmIcons[id], 16, 16);
}
void ui_image(int x, int y, const uint32_t *p, int w, int h)
{
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++) ui_px(x + i, y + j, p[j * w + i]);
}

void ui_hand(int x, int y)
{
  static const char *hand[7] = { "..##....", ".#cc####", "#cccccc#", "#ccc####", "#cccc#..", ".####...", "........" };
  for (int j = 0; j < 7; j++)
    for (int i = 0; i < 8; i++)
      if (hand[j][i] == '#') ui_px(x + i, y + j, C_WOOD_EDGE);
      else if (hand[j][i] == 'c') ui_px(x + i, y + j, C_CREAM);
}

void ui_heart(int x, int y, uint32_t fill, bool full)
{
  static const char *art[6] = { ".##.##.", "#######", "#######", ".#####.", "..###..", "...#..." };
  for (int j = 0; j < 6; j++)
    for (int i = 0; i < 7; i++) {
      if (art[j][i] != '#') continue;
      bool edge = j == 0 || i == 0 || i == 6 || art[j - 1][i] != '#' || (j < 5 && art[j + 1][i] != '#') ||
                  art[j][i - 1] != '#' || art[j][i + 1] != '#';
      ui_px(x + i, y + j, edge ? C_WOOD_EDGE : (full ? fill : C_PAPER_DARK));
    }
  if (full) ui_px(x + 2, y + 1, C_CREAM);
}

void ui_checker(void)
{
  int ox = (g_ui_w - 256) / 2;
  for (int y = 0; y < g_ui_h; y++)
    for (int x = 0; x < g_ui_w; x++)
      g_ui_px[y * g_ui_w + x] = ((((x - ox) >> 4) ^ (y >> 4)) & 1) ? C_CHECK_B : C_CHECK_A;
}

void ui_switch(int x, int y, bool on)
{
  ui_rect(x + 1, y, 18, 9, C_WOOD_EDGE);
  ui_rect(x, y + 1, 20, 7, C_WOOD_EDGE);
  ui_rect(x + 1, y + 1, 18, 7, on ? C_GREEN : C_PAPER_DARK);
  int k = on ? x + 11 : x + 1;
  ui_rect(k, y + 1, 8, 7, C_WOOD_EDGE);
  ui_rect(k + 1, y + 2, 6, 5, on ? C_CREAM : C_WOOD_LIGHT);
}
void ui_bar(int x, int y, int w, int v, int max)
{
  if (max < 1) max = 1;
  if (v < 0) v = 0;
  if (v > max) v = max;
  ui_rect(x, y, w, 5, C_WOOD_EDGE);
  ui_rect(x + 1, y + 1, w - 2, 3, C_PAPER_DARK);
  ui_rect(x + 1, y + 1, (w - 2) * v / max, 3, C_GREEN);
}

/* ── Text ────────────────────────────────────────────────────────────── */
static const HmPixFont *font(int f) { return f == UI_SMALL ? &kPix_small : &kPix_body; }

uint32_t ui_utf8_next(const char **p)
{
  const unsigned char *s8 = (const unsigned char *)*p;
  uint32_t c = s8[0];
  int n = 1;
  if (c >= 0xF0 && s8[1] && s8[2] && s8[3]) { c = ((c & 7) << 18) | ((s8[1] & 63) << 12) | ((s8[2] & 63) << 6) | (s8[3] & 63); n = 4; }
  else if (c >= 0xE0 && s8[1] && s8[2]) { c = ((c & 15) << 12) | ((s8[1] & 63) << 6) | (s8[2] & 63); n = 3; }
  else if (c >= 0xC0 && s8[1]) { c = ((c & 31) << 6) | (s8[1] & 63); n = 2; }
  *p += n;
  return c;
}
static const HmPixGlyph *glyph(const HmPixFont *f, uint32_t cp)
{
  for (int i = 0; i < f->n; i++) if (f->g[i].cp == cp) return &f->g[i];
  for (int i = 0; i < f->n; i++) if (f->g[i].cp == '?') return &f->g[i];
  return &f->g[0];
}
int ui_text_w(int fi, const char *t)
{
  const HmPixFont *f = font(fi);
  int w = 0;
  while (*t) w += glyph(f, ui_utf8_next(&t))->adv;
  return w;
}
static int text_scaled(const HmPixFont *f, int x, int y, const char *t, uint32_t col, int k)
{
  int top = y - f->ascent * k;
  while (*t) {
    const HmPixGlyph *g = glyph(f, ui_utf8_next(&t));
    for (int j = 0; j < f->h; j++) {
      uint16_t bits = f->rows[g->off + j];
      for (int i = 0; bits; i++, bits >>= 1)
        if (bits & 1) {
          if (k == 1) ui_px(x + i, top + j, col);
          else ui_rect(x + i * k, top + j * k, k, k, col);
        }
    }
    x += g->adv * k;
  }
  return x;
}
int ui_text(int f, int x, int y, const char *t, uint32_t col) { return text_scaled(font(f), x, y, t, col, 1); }
int ui_text_sh(int f, int x, int y, const char *t, uint32_t col)
{
  ui_text(f, x + 1, y + 1, t, C_WOOD_EDGE);
  return ui_text(f, x, y, t, col);
}
void ui_text_r(int f, int rx, int y, const char *t, uint32_t col) { ui_text(f, rx - ui_text_w(f, t), y, t, col); }
void ui_text_c(int f, int cx, int y, const char *t, uint32_t col) { ui_text(f, cx - ui_text_w(f, t) / 2, y, t, col); }
int ui_text_big_w(const char *t) { return 2 * ui_text_w(UI_BODY, t); }
int ui_text_big(int x, int y, const char *t, uint32_t col, uint32_t edge)
{
  for (int dy = -2; dy <= 2; dy += 2)
    for (int dx = -2; dx <= 2; dx += 2)
      if (dx || dy) text_scaled(&kPix_body, x + dx, y + dy, t, edge, 2);
  text_scaled(&kPix_body, x + 2, y + 4, t, edge, 2);
  return text_scaled(&kPix_body, x, y, t, col, 2);
}
int ui_text_wrap(int fi, int x, int y, int w, int lh, int max, const char *t, uint32_t col)
{
  const HmPixFont *f = font(fi);
  int lines = 0;
  while (*t && lines < max) {
    /* the longest run of whole words that fits */
    const char *p = t, *cut = NULL;
    int width = 0;
    while (*p && *p != '\n') {
      const char *q = p;
      uint32_t cp = ui_utf8_next(&q);
      int a = glyph(f, cp)->adv;
      if (width + a > w) break;
      width += a;
      if (cp == ' ') cut = p;
      p = q;
    }
    const char *end = (!*p || *p == '\n' || !cut) ? p : cut;
    char line[256];
    size_t n = (size_t)(end - t);
    if (n >= sizeof line) n = sizeof line - 1;
    memcpy(line, t, n); line[n] = 0;
    text_scaled(f, x, y + lines * lh, line, col, 1);
    lines++;
    t = end;
    while (*t == ' ' || *t == '\n') t++;
  }
  return lines;
}

/* ── The farm by day ─────────────────────────────────────────────────── */
static uint32_t hash(uint32_t x)
{
  x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
  return x;
}
static uint32_t mix(uint32_t a, uint32_t b, int t, int n)   /* a..b, t of n */
{
  int r = (int)((a >> 16) & 255), g = (int)((a >> 8) & 255), bl = (int)(a & 255);
  int r2 = (int)((b >> 16) & 255), g2 = (int)((b >> 8) & 255), b2 = (int)(b & 255);
  r += (r2 - r) * t / n; g += (g2 - g) * t / n; bl += (b2 - bl) * t / n;
  return 0xFF000000u | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)bl;
}
static const uint8_t kBayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };

void ui_scene(uint32_t t)
{
  int W = g_ui_w, H = g_ui_h;
  int horizon = H * 62 / 100;
  /* the sky: bands of blue, lighter towards the hills, dithered into each
   * other as the console would */
  static const uint32_t sky[] = { 0x3A6ED8, 0x4478DC, 0x4E84E0, 0x5A90E6, 0x689EEA,
                                  0x78ACEE, 0x8ABAF2, 0x9CC8F4, 0xB0D6F6 };
  enum { NSKY = sizeof sky / sizeof sky[0] };
  for (int y = 0; y < horizon; y++) {
    int pos = y * (NSKY - 1) * 16 / horizon;
    int band = pos >> 4, frac = pos & 15;
    for (int x = 0; x < W; x++) {
      int b = band + (frac > kBayer[y & 3][x & 3]);
      if (b >= NSKY) b = NSKY - 1;
      g_ui_px[y * W + x] = UI_RGB(sky[b]);
    }
  }
  /* white clouds drifting slowly past, each a few round puffs */
  for (int c = 0; c < 4; c++) {
    uint32_t h = hash((uint32_t)c * 7919 + 11);
    int len = 40 + (int)(h % 30), cy = 14 + c * 17 + (int)((h >> 8) % 8);
    int cx = (int)((t / (6 + c * 2) + (h >> 12) % (uint32_t)W) % (uint32_t)(W + 2 * len)) - len;
    for (int k = 0; k < 4; k++) {
      int px = cx + k * len / 4 + 6, py = cy - (k == 1 || k == 2 ? 4 : 0), r = (k == 1 || k == 2) ? 9 : 6;
      for (int y = py - r; y <= py + r + 2; y++)
        for (int x = px - r - 2; x <= px + r + 2; x++) {
          if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)horizon) continue;
          int dx = x - px, dy = y - py;
          if (dx * dx + dy * dy * 2 > r * r * 2) continue;
          g_ui_px[y * W + x] = dy > r / 2 ? UI_RGB(0xD8E6F6) : UI_RGB(0xFFFFFF);
        }
    }
  }
  /* far hills, then near ones */
  for (int x = 0; x < W; x++) {
    int h1 = horizon - 14 - (int)(9 * sin(x * 0.021 + 1.3) + 5 * sin(x * 0.057));
    int h2 = horizon - 4 - (int)(6 * sin(x * 0.033 + 4.1) + 3 * sin(x * 0.11));
    for (int y = h1; y < H; y++) if (y >= 0) g_ui_px[y * W + x] = UI_RGB(0x6C9C8C);
    for (int y = h2; y < H; y++) if (y >= 0) g_ui_px[y * W + x] = UI_RGB(0x3E8A3A);
  }
  /* the field: furrows running towards the hills */
  for (int y = horizon; y < H; y++) {
    int depth = y - horizon;
    for (int x = 0; x < W; x++) {
      int u = (x - W / 2) * 16 / (depth + 8);
      bool furrow = ((u % 6) + 6) % 6 == 0;
      uint32_t c = depth < 6 ? 0x4A9A38 : furrow ? 0x4E9232 : 0x62B040;
      if (!furrow && ((x * 7 + y * 13) % 23) == 0) c = 0x86CC58;
      g_ui_px[y * W + x] = UI_RGB(c);
    }
  }
  /* a tree on the left, the barn and its silo */
  int base = horizon + 2;
  int tx = W * 8 / 100;
  ui_rect(tx + 6, base - 18, 3, 18, UI_RGB(0x6A4426));
  for (int j = -16; j <= 16; j++)
    for (int i = -14; i <= 14; i++)
      if (i * i * 2 + j * j * 3 < 260 + (int)(hash((uint32_t)(i * 31 + j)) % 60))
        ui_px(tx + 7 + i, base - 30 + j, UI_RGB(i + j < -6 ? 0x4CA444 : 0x2E7A2E));
  int bx = W * 18 / 100, bw = 46, bh = 26;
  ui_rect(bx, base - bh, bw, bh, UI_RGB(0xC83A2A));                 /* walls */
  for (int y = base - bh; y < base; y += 3) ui_hline(bx, y, bw, UI_RGB(0xA82E22));
  for (int j = 0; j < 14; j++) ui_rect(bx - 3 + j, base - bh - j, bw + 6 - 2 * j, 1, UI_RGB(0x7A2418));   /* roof */
  ui_rect(bx + 16, base - 16, 14, 16, UI_RGB(0xF4ECDC));             /* the door, white trim */
  ui_rect(bx + 17, base - 15, 12, 15, UI_RGB(0xA82E22));
  for (int k = 0; k < 12; k++) { ui_px(bx + 17 + k, base - 15 + k * 15 / 12, UI_RGB(0xF4ECDC)); ui_px(bx + 28 - k, base - 15 + k * 15 / 12, UI_RGB(0xF4ECDC)); }
  ui_rect(bx + 20, base - bh - 8, 6, 5, UI_RGB(0x3A2A24));            /* the loft's window */
  ui_rect(bx + 22, base - bh - 8, 1, 5, UI_RGB(0x7A2418));
  int sx = bx + bw + 4;
  ui_rect(sx, base - 34, 14, 34, UI_RGB(0xC4CAD4));                  /* silo */
  for (int y = base - 34; y < base; y += 4) ui_hline(sx, y, 14, UI_RGB(0xA4ACB8));
  ui_vline(sx + 11, base - 34, 34, UI_RGB(0xA4ACB8));
  for (int j = 0; j < 7; j++) ui_rect(sx + j / 2, base - 34 - 7 + j, 14 - j, 1, UI_RGB(0x7C8494));
  /* the fence in front */
  int fy = H - 22;
  for (int x = 0; x < W; x += 12) {
    ui_rect(x, fy, 3, 16, UI_RGB(0x8A5A30));
    ui_rect(x, fy, 3, 1, UI_RGB(0xC08A4A));
  }
  ui_rect(0, fy + 3, W, 2, UI_RGB(0xA06C3A));
  ui_rect(0, fy + 10, W, 2, UI_RGB(0xA06C3A));
  ui_hline(0, fy + 3, W, UI_RGB(0xC89A5A));
  ui_hline(0, fy + 10, W, UI_RGB(0xC89A5A));
  /* butterflies over the field */
  for (int i = 0; i < 5; i++) {
    uint32_t h = hash((uint32_t)i * 977 + 3);
    double a = t * 0.013 + (h & 255) * 0.1;
    int x = (int)(h % (uint32_t)W) + (int)(14 * sin(a + i));
    int y = horizon + 6 + (int)((h >> 9) % (uint32_t)(H - horizon - 26)) + (int)(5 * cos(a * 1.7));
    uint32_t col = (h >> 20) & 1 ? UI_RGB(0xFFFFFF) : UI_RGB(0xF8E058);
    bool open = ((t / 6 + (h >> 22)) & 1) != 0;              /* the wings flap */
    ui_px(x, y, UI_RGB(0x3A3020));
    if (open) { ui_px(x - 1, y - 1, col); ui_px(x + 1, y - 1, col); ui_px(x - 1, y, col); ui_px(x + 1, y, col); }
    else { ui_px(x, y - 1, col); ui_px(x, y - 2, col); }
  }
}
