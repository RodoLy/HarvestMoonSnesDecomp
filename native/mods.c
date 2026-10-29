/* mods.c — the recompilation's improvements, on the native build (see
 * mods.h). Ported from HarvestMoonSNESRecomp (hm_cheats.c, hm_qol.c,
 * hm_title.c, hm_widescreen.c, hm_endless.c); the rules are theirs:
 *   - act at frame boundaries, only once a diary is loaded
 *     (hm_diary_loaded: the farm block is all zero before that);
 *   - only write values the game itself can produce (the caps of its own
 *     Add* routines), so a diary saved with a cheat on stays valid with it
 *     off;
 *   - with a feature off, nothing of it runs and the game is untouched. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ppu.h"
#include "hm_decomp.h"
#include "hm_ram.h"
#include "native.h"
#include "config.h"
#include "mods.h"

extern Ppu *g_ppu;

/* ── Cheats (hm_cheats.c) ────────────────────────────────────────────── */
static struct {
  bool clock_held;
  uint8_t held_hour, held_min, held_day, held_season, held_year;
} s_cheat;

static void cheats_frame(uint8_t *ram)
{
  const Config *c = &g_cfg;
  if (!hm_diary_loaded(ram)) { s_cheat.clock_held = false; return; }
  if (c->money && hm_rd24(ram, HM_MONEY) != HM_MONEY_MAX) hm_wr24(ram, HM_MONEY, HM_MONEY_MAX);
  if (c->stamina && ram[HM_CURRENT_STAMINA] < ram[HM_MAX_STAMINA]) ram[HM_CURRENT_STAMINA] = ram[HM_MAX_STAMINA];
  if (c->stock) {
    if (hm_rd16(ram, HM_STORED_WOOD) < HM_STOCK_MAX) hm_wr16(ram, HM_STORED_WOOD, HM_STOCK_MAX);
    if (hm_rd16(ram, HM_STORED_GRASS) < HM_STOCK_MAX) hm_wr16(ram, HM_STORED_GRASS, HM_STOCK_MAX);
  }
  if (c->bags) {
    /* only bags the player has (a zero count: the bag is not owned) */
    static const uint32_t bags[] = { HM_SEEDS_GRASS_N, HM_SEEDS_CORN_N, HM_SEEDS_TOMATO_N, HM_SEEDS_POTATO_N,
                                     HM_SEEDS_TURNIP_N, HM_FEED_COW_N, HM_FEED_CHICKS_N };
    for (size_t i = 0; i < sizeof bags / sizeof bags[0]; i++)
      if (ram[bags[i]] != 0 && ram[bags[i]] < 99) ram[bags[i]] = 99;
  }
  if (c->freeze_clock) {
    /* held only while the game runs the clock: when it stops time (a new
     * day, scenes, menus, sleeping) it may set the clock, and that passes */
    bool running = hm_rd16(ram, HM_TIME_RUNNING) == 1;
    bool same_day = ram[HM_DAY] == s_cheat.held_day && ram[HM_SEASON] == s_cheat.held_season &&
                    ram[HM_YEAR] == s_cheat.held_year;
    if (!running || !s_cheat.clock_held || !same_day) {
      s_cheat.clock_held = running;
      s_cheat.held_hour = ram[HM_HOUR]; s_cheat.held_min = ram[HM_MINUTES];
      s_cheat.held_day = ram[HM_DAY]; s_cheat.held_season = ram[HM_SEASON]; s_cheat.held_year = ram[HM_YEAR];
    } else {
      ram[HM_HOUR] = s_cheat.held_hour;
      ram[HM_MINUTES] = s_cheat.held_min;
    }
  }
  if (c->weather != WEATHER_STOCK) {
    uint8_t w = ram[HM_WEATHER_TOMORROW];
    bool ordinary = w == HM_WEATHER_SUNNY || w == HM_WEATHER_RAIN || w == HM_WEATHER_SNOW ||
                    w == HM_WEATHER_HURRICANE || w == HM_WEATHER_THUNDER;
    if (ordinary) {                    /* festivals and the fair are left alone */
      uint8_t want = w;
      if (c->weather == WEATHER_SUNNY) want = HM_WEATHER_SUNNY;
      else if (c->weather == WEATHER_RAIN) want = ram[HM_SEASON] == 3 ? HM_WEATHER_SNOW : HM_WEATHER_RAIN;
      else if (w == HM_WEATHER_HURRICANE || w == HM_WEATHER_THUNDER) want = HM_WEATHER_SUNNY;
      ram[HM_WEATHER_TOMORROW] = want;
    }
  }
}

/* ── Fast text and longer days (hm_qol.c) ────────────────────────────── */
static struct { unsigned phase; bool have_expect; uint8_t expect; } s_qol;

static void qol_frame(uint8_t *ram)
{
  /* fast text: what the game itself does while A is held ($84:CF38) */
  if (g_cfg.fast_text && ram[HM_TEXT_TICK] < 4) ram[HM_TEXT_TICK] = 4;
  /* longer days: UpdateTime adds 1 to the seconds every frame the clock
   * runs; to cancel the next tick, take 1 off now (only after a frame the
   * game ticked: when it stops calling UpdateTime nothing piles up). 0 - 1
   * is 255, which the game's 8-bit INC turns back into 0 with no minute
   * passing (it compares for 60). */
  if (g_cfg.day_length) {
    static const unsigned allow[5] = { 1, 2, 1, 1, 1 }, period[5] = { 1, 3, 2, 3, 4 };
    int f = g_cfg.day_length;
    if (!hm_diary_loaded(ram) || (hm_rd16(ram, HM_TIME_RUNNING) & 3) != 1) {
      s_qol.have_expect = false;
    } else {
      uint8_t cur = ram[HM_SECONDS];
      if (s_qol.have_expect && cur != s_qol.expect) {
        s_qol.phase = (s_qol.phase + 1) % period[f];
        if (s_qol.phase >= allow[f]) ram[HM_SECONDS] = (uint8_t)(cur - 1);
      }
      s_qol.expect = ram[HM_SECONDS];
      s_qol.have_expect = true;
    }
  }
}

/* ── OPTIONS on the title screen (hm_title.c) ─────────────────────────
 * The title's words are pictures on BG1 (map $6000 rows 16-21, from column
 * 8); the line under the cursor is lit by its own fill colour (CGRAM $7D
 * CONTINUE, $7F START, $29 HOW TO PLAY), which the game sets in its CGRAM
 * copy. A fourth line, OPTIONS, is made of the letters of the three words,
 * read out of VRAM (no game graphics are kept here), and drawn as a host
 * object of the picture chip; the copyright moves down a row to make room.
 * The cursor gets a fourth stop by setting $098D so that the game's own
 * move lands where it should (and plays its own sound). */
#define BG1_MAP     0x6000u
#define BG1_TILES   0x2000u
#define MAP(r, c)   (BG1_MAP + (r) * 32u + (c))
#define CG_CONTINUE 0x7D
#define CG_START    0x7F
#define CG_HOWTO    0x29
#define CG_OUTLINE  0x79
enum { OW = 96, OH = 16 };
enum { CLS_NONE, CLS_OUTLINE, CLS_FILL };

static struct {
  bool option_selected;
  uint16_t prev, swallow;
  bool glyphs_ok, glyphs_tried, shown;
  uint8_t cls[OH][OW];
  int width;
  uint16_t pink;
  uint16_t pixels[OW * OH];
  PpuExtraObject obj;
} s_title;

static uint16_t vram(uint32_t w) { return g_ppu->vram[w & 0x7fff]; }
static int tile_px(unsigned tile, int x, int y)
{
  uint32_t base = BG1_TILES + tile * 16u;
  uint16_t p01 = vram(base + y), p23 = vram(base + 8 + y);
  int b = 7 - x;
  return ((p01 >> b) & 1) | ((p01 >> (8 + b)) & 1) << 1 | ((p23 >> b) & 1) << 2 | ((p23 >> (8 + b)) & 1) << 3;
}
static bool title_on_screen(void)
{
  return (vram(MAP(16, 8)) & 0x3ff) == 0x101 && (vram(MAP(18, 8)) & 0x3ff) == 0x2ec &&
         (vram(MAP(20, 8)) & 0x3ff) == 0x10e;
}
static bool title_menu_active(void)
{
  return g_ram[HM_INPUT_STATE] == 4 && g_ram[0x95] == 5 && title_on_screen();
}
static void word_image(int r, uint8_t out[16][96])
{
  for (int c = 0; c < 12; c++)
    for (int h = 0; h < 2; h++) {
      uint16_t e = vram(MAP(r + h, 8 + c));
      unsigned t = e & 0x3ff;
      bool hf = e & 0x4000, vf = e & 0x8000;
      for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
          out[h * 8 + y][c * 8 + x] = (uint8_t)tile_px(t, hf ? 7 - x : x, vf ? 7 - y : y);
    }
}
static int runs(uint8_t img[16][96], int out[][2], int max)
{
  int n = 0;
  for (int x = 0; x < 96 && n < max;) {
    bool any = false;
    for (int y = 0; y < 16; y++) if (img[y][x]) any = true;
    if (!any) { x++; continue; }
    int x0 = x;
    for (;;) {
      bool col = false;
      if (x < 96) for (int y = 0; y < 16; y++) if (img[y][x]) col = true;
      if (!col) break;
      x++;
    }
    out[n][0] = x0; out[n][1] = x; n++;
  }
  return n;
}
/* OPTIONS from O, N, T, I (CONTINUE), S (START) and P (HOW TO PLAY); if
 * the letters are not the measured shapes (another ROM), nothing is drawn */
static void build_glyphs(void)
{
  static uint8_t cont[16][96], start[16][96], howto[16][96];
  int rc[12][2], rs[12][2], rh[12][2];
  s_title.glyphs_tried = true;
  word_image(16, cont); word_image(18, start); word_image(20, howto);
  int nc = runs(cont, rc, 12), ns = runs(start, rs, 12), nh = runs(howto, rh, 12);
  if (nc < 5 || ns < 1 || nh < 5) return;
  struct { uint8_t (*img)[96]; int x0, x1; int outline; } L[7] = {
    { cont, rc[1][0], rc[1][1], 9 }, { howto, rh[4][0], rh[4][1], 7 }, { cont, rc[3][0], rc[3][1], 9 },
    { cont, rc[4][0], rc[4][1], 9 }, { cont, rc[1][0], rc[1][1], 9 }, { cont, rc[2][0], rc[2][1], 9 },
    { start, rs[0][0], rs[0][1], 9 },
  };
  static const int widths[7] = { 12, 12, 12, 6, 12, 14, 12 };
  memset(s_title.cls, 0, sizeof s_title.cls);
  int x = 1;
  for (int i = 0; i < 7; i++) {
    int w = L[i].x1 - L[i].x0;
    if (w < widths[i] - 1 || w > widths[i] + 1) return;
    for (int y = 0; y < 16; y++)
      for (int j = 0; j < w && x + j < OW; j++) {
        int ci = L[i].img[y][L[i].x0 + j];
        if (ci) s_title.cls[y][x + j] = ci == L[i].outline ? CLS_OUTLINE : CLS_FILL;
      }
    x += w + 1;
  }
  s_title.width = x;
  s_title.glyphs_ok = true;
}
static void move_copyright(void)
{
  uint16_t blank = vram(MAP(22, 0));
  bool row23_blank = true;
  for (int c = 0; c < 32; c++) if (vram(MAP(23, c)) != blank) row23_blank = false;
  if (row23_blank) return;
  for (int r = 25; r >= 23; r--)
    for (int c = 0; c < 32; c++) g_ppu->vram[MAP(r + 1, c)] = vram(MAP(r, c));
  for (int c = 0; c < 32; c++) g_ppu->vram[MAP(23, c)] = blank;
}

static void title_hide(void)
{
  if (s_title.shown) PpuSetExtraObjects(g_ppu, NULL, 0);
  s_title.shown = false;
}
uint16_t mods_pad(uint16_t pad, bool *open_launcher)
{
  *open_launcher = false;
  uint16_t in = pad;
  uint16_t pressed = in & (uint16_t)~s_title.prev;
  s_title.prev = in;
  /* a button taken stays hidden from the game until it is let go */
  s_title.swallow &= in;
  pad &= (uint16_t)~s_title.swallow;
  if (!g_cfg.title_options || !s_title.glyphs_ok || !title_menu_active()) {
    if (!g_ppu || !title_on_screen()) s_title.option_selected = false;
    return pad;
  }
  uint8_t *cursor = &g_ram[HM_TITLE_CURSOR];
  if (s_title.option_selected) {
    if (pressed & (B_A | B_START)) {
      s_title.swallow |= in & (B_A | B_START);
      pad &= (uint16_t)~s_title.swallow;
      *open_launcher = true;
    } else if (pressed & B_UP) {
      *cursor = 0;                     /* the game's Up: 0 -> 2 (HOW TO PLAY) */
      s_title.option_selected = false;
    } else if (pressed & B_DOWN) {
      *cursor = 2;                     /* the game's Down: 2 -> 0 (CONTINUE) */
      s_title.option_selected = false;
    }
  } else {
    if ((pressed & B_DOWN) && *cursor == 2) { *cursor = 1; s_title.option_selected = true; }
    else if ((pressed & B_UP) && *cursor == 0) s_title.option_selected = true;
  }
  return pad;
}
static void title_frame(void)
{
  if (!g_cfg.title_options || !title_on_screen()) {
    title_hide();
    s_title.glyphs_tried = s_title.glyphs_ok = false;
    s_title.option_selected = false;
    return;
  }
  if (!s_title.glyphs_tried) build_glyphs();
  if (!s_title.glyphs_ok) { title_hide(); return; }
  move_copyright();
  uint8_t cursor = g_ram[HM_TITLE_CURSOR];
  static const int fills[3] = { CG_CONTINUE, CG_START, CG_HOWTO };
  uint16_t sel_fill = g_ppu->cgram[fills[cursor % 3]];
  uint16_t plain = g_ppu->cgram[fills[(cursor + 1) % 3]];
  if (sel_fill != plain) s_title.pink = sel_fill;
  uint16_t fill = plain;
  if (s_title.option_selected && title_menu_active()) {
    fill = s_title.pink ? s_title.pink : sel_fill;
    hm_wr16(g_ram, HM_CGRAM_SHADOW + CG_HOWTO * 2, plain);   /* HOW TO PLAY unlit */
    g_ppu->cgram[CG_HOWTO] = plain;
  }
  uint16_t outline = g_ppu->cgram[CG_OUTLINE];
  for (int y = 0; y < OH; y++)
    for (int x = 0; x < OW; x++) {
      uint8_t k = s_title.cls[y][x];
      s_title.pixels[y * OW + x] = k == CLS_OUTLINE ? (uint16_t)(0x8000 | outline)
                                 : k == CLS_FILL ? (uint16_t)(0x8000 | fill) : 0;
    }
  s_title.obj = (PpuExtraObject){ .x = 64, .y = 22 * 8, .width = OW, .height = OH, .stride = OW,
                                  .priority = 3, .math = 0, .oamSlot = 0, .order = 1, .pixels = s_title.pixels };
  PpuSetExtraObjects(g_ppu, &s_title.obj, 1);
  s_title.shown = true;
}

/* ── Widescreen (hm_widescreen.c) ───────────────────────────────────────
 * The picture chip draws real columns on both sides of the 256. BG1 (the
 * map) is kept well ahead of the camera in a 64x64-tile ring, so the
 * columns just outside the view hold the real map, except past the map's
 * edge: each side opens exactly as far as the camera is from the map's own
 * camera limit ($ED left, $F1 right). The middle of the farm opens fully;
 * at a map's edge that side closes; a one-screen room (limits 0/0) stays
 * 4:3. BG3 (text, the clock) stays in the 256. Screens that are not a map
 * ($22 >= $57: intro, title, menus, names) stay 4:3. Sprites: the game
 * drops a piece outside [-16, 256) (PrepareOAMData); the limits widen with
 * the margins (g_hm_oam_left / g_hm_oam_right). Nothing the game computes
 * (camera, collisions, the people, the random numbers) sees the margins. */
static int s_ws_l, s_ws_r;
int mods_field_width(void) { return g_cfg.widescreen ? 256 + 2 * WS_BUDGET : 256; }
static int imin(int a, int b) { return a < b ? a : b; }
static int iclamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static void ws_frame(void)
{
  int l = 0, r = 0;
  if (g_cfg.widescreen && g_ram[HM_MAP_ID] < HM_MAP_FIRST_PICTURE) {
    int cl = hm_rd16(g_ram, HM_CAM_MIN_X), cr = hm_rd16(g_ram, HM_CAM_MAX_X);
    int cam = hm_rd16(g_ram, HM_CAM_X);
    if (cr >= cl) {
      /* the camera the picture chip draws with (its 10-bit scroll, next to
       * the game's own): the smaller margin wins */
      int hs = g_ppu->hScroll[0] & 0x3ff;
      int pc = (cam & ~0x3ff) | hs;
      if (pc - cam > 512) pc -= 1024;
      else if (cam - pc > 512) pc += 1024;
      l = iclamp(imin(cam - cl, pc - cl), 0, WS_BUDGET);
      r = iclamp(imin(cr - cam, cr - pc), 0, WS_BUDGET);
    }
  }
  s_ws_l = l; s_ws_r = r;
  g_hm_oam_right = (uint16_t)(0x0100 + r);
  g_hm_oam_left = (uint16_t)(0xFFF0 - l);
}
void mods_ws_margins(int *left, int *right) { *left = s_ws_l; *right = s_ws_r; }

/* ── The endless game (hm_endless.c) ─────────────────────────────────────
 * On the morning of Summer 30, Year 3, the house's map code ($83:DED3)
 * sees the date and goes to the ending (the parents' visit, the verdict,
 * the credits), which ends on a night landscape that never leaves. With
 * the endless game, the ending plays in full; then the game goes back and
 * carries on: Fall of Year 3, Year 4, 5...
 *   1. On Summer 29, Year 3, while the player is free to move, the host
 *      keeps a copy of the whole machine at the start of every GameLoop
 *      frame (the native build's clean point: no routine half done; the
 *      night's sleep and the morning's house happen inside one long
 *      GameLoop frame, so the last clean point before the ending is the
 *      29th's evening).
 *   2. The ending begins ($7F1F5E bit 1): the last copy is kept.
 *   3. The final landscape (map $36, the clock on, $7F1F5E bit 15) has
 *      stayed 15 seconds: the copy is loaded back, the game is told to go
 *      on to the next day ($0973 = 2, as going to bed), and the house's
 *      date check fails this once, so Summer 30 starts like any other day.
 * A diary saved that last night and loaded wakes up on the 30th with no
 * 29th to go back to: then the date check fails at once and the game
 * simply goes on (the ending is not shown).
 * Past the third year the diary's "1st/2nd/3rd" (a three-entry table,
 * $82:F298) goes on: 4th, 5th... 21st. */
#define FLAGS_5E 0x11F5E
#define ENDING_MAP 0x36
#define SETTLE_FRAMES 900
static struct { int phase; bool have, skip, restore; int settle; } s_end;

static bool in_window(const uint8_t *r) { return r[HM_YEAR] == 2 && r[HM_SEASON] == 1 && r[HM_DAY] >= 29 && r[HM_DAY] <= 30; }
static uint16_t year_suffix(uint16_t x, uint16_t rom_value)
{
  if (x < 12) return rom_value;
  unsigned year = x / 4u + 1u;
  unsigned n100 = year % 100u, n10 = year % 10u;
  const char *s = (n100 >= 11 && n100 <= 13) ? "th" : n10 == 1 ? "st" : n10 == 2 ? "nd" : n10 == 3 ? "rd" : "th";
  return (uint16_t)(s[(x & 2) ? 1 : 0] - 'a');           /* the font: 'a' is tile 0 */
}
static void endless_frame(uint8_t *r)
{
  g_hm_year_suffix = g_cfg.endless ? year_suffix : NULL;
  if (!g_cfg.endless) { memset(&s_end, 0, sizeof s_end); g_hm_no_ending = 0; return; }
  if (!in_window(r)) { memset(&s_end, 0, sizeof s_end); g_hm_no_ending = 0; return; }
  uint16_t f = (uint16_t)(r[FLAGS_5E] | r[FLAGS_5E + 1] << 8);
  if (s_end.phase == 0) {
    if (r[HM_DAY] == 30 && !(f & 0x0002) && !s_end.have) s_end.skip = true;   /* a diary of the last night */
    if ((f & 0x0002) && s_end.have && !s_end.skip) { s_end.phase = 1; s_end.settle = 0; }
  } else {
    bool final_scene = r[HM_MAP_ID] == ENDING_MAP && (f & 0x8000) && (r[HM_TIME_RUNNING] & 1);
    s_end.settle = final_scene ? s_end.settle + 1 : 0;
    if (s_end.settle >= SETTLE_FRAMES) s_end.restore = true;
  }
  g_hm_no_ending = s_end.skip;
}
bool mods_endless_keep(void)
{
  const uint8_t *r = g_ram;
  if (getenv("HM_ENDLESS_DEBUG") && g_cfg.endless && in_window(r))
    fprintf(stderr, "endless: day %u input %u D2 %04X phase %d skip %d have %d\n", r[HM_DAY], r[HM_INPUT_STATE],
            r[0xD2] | r[0xD3] << 8, s_end.phase, s_end.skip, s_end.have);
  if (!g_cfg.endless || !in_window(r) || r[HM_DAY] != 29 || s_end.phase != 0 || s_end.skip) return false;
  return r[HM_INPUT_STATE] == 1;
}
void mods_endless_kept(bool ok) { s_end.have = ok; }
bool mods_endless_restore(void)
{
  bool want = s_end.restore;
  s_end.restore = false;
  return want;
}
void mods_endless_restored(void)
{
  /* back on the 29th's evening: on to the next morning, as after a night */
  s_end.phase = 0; s_end.skip = true; s_end.settle = 0;
  g_hm_no_ending = 1;
  hm_wr16(g_ram, HM_TIME_RUNNING, 2);
  fprintf(stderr, "endless: the ending was shown; on to Summer 30, Year 3\n");
}

void mods_reset(void)
{
  memset(&s_cheat, 0, sizeof s_cheat);
  memset(&s_qol, 0, sizeof s_qol);
  memset(&s_end, 0, sizeof s_end);
  s_title.option_selected = false; s_title.glyphs_tried = s_title.glyphs_ok = false;
  s_title.prev = s_title.swallow = 0;
  title_hide();
  g_hm_no_ending = 0;
  g_hm_oam_right = 0x0100; g_hm_oam_left = 0xFFF0;
}

void mods_frame(uint8_t *ram)
{
  cheats_frame(ram);
  qol_frame(ram);
  endless_frame(ram);
  if (g_ppu) { title_frame(); ws_frame(); }
}
