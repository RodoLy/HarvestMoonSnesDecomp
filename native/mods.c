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
#include "ui.h"
#include "host.h"
#include "i18n.h"

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

/* ── Walking through the crops ──────────────────────────────────────────
 * The collision (src/collision.c, g_hm_crops_passable) lets the player
 * through the crop tiles of the farm (maps 0-3, types 1E..6F). Here the
 * plants move: when the player steps into a crop's tile, and again when
 * the player steps out of it, the plant sways (its top leans to one side
 * and back, a little less each time; the bottom row stays put). While the
 * player stands in it, it stays still, under the player's sprite.
 * How: for a moment the plant is drawn as a host object of the picture chip
 * over its own tile: the soil that was under the plant (the tile's nearest
 * soil pixels, so the texture goes on) and the plant, leaning. The soil
 * around it is the picture chip's own and never moves. Which pixels are the
 * plant: the tile's colours that are not brown soil (the game's tilled-soil
 * tiles, the tile-change table $81:B363 entries 17 / 18, say which browns).
 * Nothing of the game's memory is written; when the sway is over the
 * objects go and the map is as the game drew it. */
#define FARM_TILE_TYPES 0x009B6
#define BG1_WRAM_MAP    0x02000u
#define TILE_CHANGES    0x81B363u
#define TILE_GRAPHICS   0xA6A096u
enum { SWAYS = 4, SWAY_FRAMES = 32, SWAY_M = 3, SWAY_W = 16 + 2 * SWAY_M };
typedef struct { bool on; int tx, ty, age, dir, amp; } Sway;   /* amp: percent */
static Sway s_sway[SWAYS];
static struct {
  int last_tx, last_ty;
  bool shown;
  uint16_t back[SWAYS][16 * SWAY_W];
  PpuExtraObject obj[SWAYS];
} s_crops = { -1, -1 };

static bool crop_type(uint8_t t) { return t >= 0x1E && t <= 0x6F; }
static uint8_t farm_type(int tx, int ty)
{
  if (tx < 0 || ty < 0 || tx >= 64 || ty >= 64) return 0;
  return g_ram[FARM_TILE_TYPES + ty * 64 + tx];
}
/* a 16x16 tile from its four BG1 tilemap entries: colours (0 = see-through) */
static void tile16(const uint16_t e[4], uint16_t out[256])
{
  uint32_t chars = PPU_bgTileAdr(g_ppu, 0);
  for (int q = 0; q < 4; q++) {
    unsigned t = e[q] & 0x3ff, pal = (e[q] >> 10) & 7;
    bool hf = e[q] & 0x4000, vf = e[q] & 0x8000;
    uint32_t base = chars + t * 16u;
    for (int y = 0; y < 8; y++)
      for (int x = 0; x < 8; x++) {
        int sx = hf ? 7 - x : x, sy = vf ? 7 - y : y, b = 7 - sx;
        uint16_t p01 = vram(base + sy), p23 = vram(base + 8 + sy);
        int ci = ((p01 >> b) & 1) | ((p01 >> (8 + b)) & 1) << 1 | ((p23 >> b) & 1) << 2 | ((p23 >> (8 + b)) & 1) << 3;
        out[((q >> 1) * 8 + y) * 16 + (q & 1) * 8 + x] = ci ? (uint16_t)(0x8000 | g_ppu->cgram[pal * 16 + ci]) : 0;
      }
  }
}
/* the tile's entries in the game's copy of the whole BG1 map ($7E:2000) */
static void map_entries(int tx, int ty, uint16_t e[4])
{
  uint8_t wc = g_ram[0x0181];
  unsigned w = wc == 1 ? 32 : wc == 2 ? 64 : 128;          /* entries per row */
  for (int q = 0; q < 4; q++) {
    unsigned r = (unsigned)ty * 2 + (q >> 1), c = (unsigned)tx * 2 + (q & 1);
    e[q] = hm_rd16(g_ram, BG1_WRAM_MAP + (r * w + c) * 2);
  }
}
/* the entries of a tile change ($81:B363: graphics at $A6:A096 + offset,
 * rows $40 bytes apart) */
static void change_entries(int change, uint16_t e[4])
{
  uint32_t src = TILE_GRAPHICS + hm_bus_rd16(g_ram, TILE_CHANGES + 6u * change);
  e[0] = hm_bus_rd16(g_ram, src);        e[1] = hm_bus_rd16(g_ram, src + 2);
  e[2] = hm_bus_rd16(g_ram, src + 0x40); e[3] = hm_bus_rd16(g_ram, src + 0x42);
}
/* how far (pixels) row `row` of the plant leans at this age */
static int lean(const Sway *s, int row)
{
  static const int8_t wave[SWAY_FRAMES] = {   /* the top row, in quarter pixels */
    4, 7, 9, 10, 10, 8, 5, 1, -3, -6, -8, -8, -6, -3, 0, 3, 5, 6, 5, 3, 1, -1, -3, -3, -2, -1, 0, 1, 1, 0, 0, 0 };
  int q = wave[s->age] * s->dir * s->amp * (15 - row);     /* the bottom row stays put */
  return (q >= 0 ? q + 3000 : q - 3000) / 6000;            /* / 100 / 15 rows / 4, rounded */
}
/* the sprite slot drawn last (behind every other sprite) and first: with
 * OAM priority rotation on, the first slot is the OAM address's */
static uint8_t slot_first(void) { return (uint8_t)(PPU_objPriority(g_ppu) ? (g_ppu->oamaddl & 0xfe) >> 1 : 0); }
static uint8_t slot_last(void) { return (uint8_t)((slot_first() + 127) & 127); }

/* is this colour of a crop tile soil (and not plant)? The tile's own soil
 * is the tilled soil's colours, a shade or two off (the tile's palette), and
 * brown (red over green) */
static bool soil_like(uint16_t c, const uint16_t soil[2][256])
{
  int b = (c >> 10) & 31, g = (c >> 5) & 31, r = c & 31;
  if (r <= g) return false;
  for (int v = 0; v < 2; v++)
    for (int i = 0; i < 256; i++) {
      uint16_t d = soil[v][i];
      if (!d) continue;
      int db = ((d >> 10) & 31) - b, dg = ((d >> 5) & 31) - g, dr = (d & 31) - r;
      if (db >= -1 && db <= 1 && dg >= -1 && dg <= 1 && dr >= -1 && dr <= 1) return true;
    }
  return false;
}

/* the object of sway k (one, at obj[*n]): over the tile, the soil that was
 * under the plant (the tile's own nearest soil pixel in the row, so the
 * texture goes on) and the plant, leaning. The soil that is not under the
 * plant is left to the picture chip: it never moves. The player's sprite
 * stays in front of all of it. */
static bool sway_draw(int k, int *n)
{
  const Sway *s = &s_sway[k];
  if (!crop_type(farm_type(s->tx, s->ty))) return false;
  uint16_t e[4], soil_e[4], plant[256], soil[2][256];
  map_entries(s->tx, s->ty, e);
  tile16(e, plant);
  for (int v = 0; v < 2; v++) { change_entries(0x17 + v, soil_e); tile16(soil_e, soil[v]); }
  bool is_plant[256];
  for (int i = 0; i < 256; i++) is_plant[i] = plant[i] && !soil_like(plant[i], soil);

  /* the plant's dark outline and shading are close to the soil's browns: a
   * pixel with most of its neighbours in the plant is in the plant too */
  for (int y = 0; y < 16; y++)
    for (int x = 0; x < 16; x++) {
      int i = y * 16 + x, m = 0;
      if (!plant[i] || is_plant[i]) continue;
      for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
          int yy = y + dy, xx = x + dx;
          if ((dy || dx) && yy >= 0 && yy < 16 && xx >= 0 && xx < 16 && is_plant[yy * 16 + xx]) m++;
        }
      if (m >= 5) is_plant[i] = true;
    }
  uint16_t *back = s_crops.back[k];
  memset(back, 0, sizeof s_crops.back[k]);
  for (int y = 0; y < 16; y++)                                /* soil where the plant was */
    for (int x = 0; x < 16; x++) {
      if (!is_plant[y * 16 + x]) continue;
      uint16_t c = 0;
      for (int d = 1; d < 16 && !c; d++) {
        if (x - d >= 0 && plant[y * 16 + x - d] && !is_plant[y * 16 + x - d]) c = plant[y * 16 + x - d];
        else if (x + d < 16 && plant[y * 16 + x + d] && !is_plant[y * 16 + x + d]) c = plant[y * 16 + x + d];
      }
      back[y * SWAY_W + SWAY_M + x] = c ? c : soil[0][y * 16 + x];
    }
  for (int y = 0; y < 16; y++) {                              /* the plant, leaning */
    int d = lean(s, y);
    for (int x = 0; x < 16; x++) {
      if (!is_plant[y * 16 + x]) continue;
      int X = SWAY_M + x + d;
      if (X >= 0 && X < SWAY_W) back[y * SWAY_W + X] = plant[y * 16 + x];
    }
  }
  /* where the picture chip draws the tile: its BG1 scroll (10 bits) */
  int sx = ((s->tx * 16 - (int)g_ppu->hScroll[0]) % 1024 + 1024 + 512) % 1024 - 512;
  int sy = ((s->ty * 16 - (int)g_ppu->vScroll[0]) % 1024 + 1024 + 512) % 1024 - 512;
  uint8_t prio = (uint8_t)((e[0] & 0x2000) ? 3 : 2), math = (uint8_t)(g_ppu->cgadsub & 0x01);
  PpuExtraObject o = { .x = sx - SWAY_M, .y = sy, .width = SWAY_W, .height = 16, .stride = SWAY_W,
                       .priority = prio, .math = math, .oamSlot = slot_last(), .order = (uint32_t)(1 + k),
                       .pixels = back };
  s_crops.obj[(*n)++] = o;
  return true;
}
static void crops_hide(void)
{
  if (s_crops.shown && g_ppu) PpuSetExtraObjects(g_ppu, NULL, 0);
  s_crops.shown = false;
}
static void sway_start(int tx, int ty, int dir, int amp)
{
  int k = 0;
  for (int i = 0; i < SWAYS; i++) if (s_sway[i].on && s_sway[i].tx == tx && s_sway[i].ty == ty) { k = i; goto found; }
  for (int i = 0; i < SWAYS; i++) if (!s_sway[i].on) { k = i; goto found; }
  for (int i = 1; i < SWAYS; i++) if (s_sway[i].age > s_sway[k].age) k = i;   /* the oldest */
found:
  s_sway[k] = (Sway){ true, tx, ty, 0, dir, amp };
}
static void crops_frame(void)
{
  g_hm_crops_passable = g_cfg.walk_crops;
  g_hm_single_seed = g_cfg.single_seed;
  if (!g_cfg.walk_crops || g_ram[HM_MAP_ID] > 3) {
    memset(s_sway, 0, sizeof s_sway);
    s_crops.last_tx = s_crops.last_ty = -1;
    crops_hide();
    return;
  }
  int tx = hm_rd16(g_ram, 0xD6) >> 4, ty = hm_rd16(g_ram, 0xD8) >> 4;
  int lx = s_crops.last_tx, ly = s_crops.last_ty;
  if (lx >= 0 && (tx != lx || ty != ly)) {
    /* lean the way the player goes (up and down: alternately) */
    int dir = tx > lx ? 1 : tx < lx ? -1 : ((tx + ty) & 1) ? 1 : -1;
    if (crop_type(farm_type(lx, ly))) sway_start(lx, ly, dir, 70);     /* stepping out */
    if (crop_type(farm_type(tx, ty))) sway_start(tx, ty, dir, 100);    /* stepping in */
  }
  s_crops.last_tx = tx; s_crops.last_ty = ty;
  int n = 0;
  for (int i = 0; i < SWAYS; i++) {
    if (!s_sway[i].on) continue;
    /* over: gone, unless the player still stands in it (then it stays,
     * still, under the player) */
    if (s_sway[i].age >= SWAY_FRAMES) {
      if (s_sway[i].tx == tx && s_sway[i].ty == ty) s_sway[i].age = SWAY_FRAMES - 1;
      else { s_sway[i].on = false; continue; }
    }
    if (!sway_draw(i, &n)) { s_sway[i].on = false; continue; }
    s_sway[i].age++;
  }
  if (n) { PpuSetExtraObjects(g_ppu, s_crops.obj, (size_t)n); s_crops.shown = true; }
  else crops_hide();
}

/* ── Mouse controls (Stardew Valley style) ──────────────────────────────
 * Only the buttons the game already has are pressed, as a player would:
 *   - left click on the tile next to the player (up, down, left, right):
 *     the player turns to it and uses the tool in hand (Y); on the
 *     player's own tile: the tool, the way the player faces;
 *   - left click farther away: the player walks there (holding the
 *     button, the player follows the pointer);
 *   - right click: the player walks next to that tile, turns to it and
 *     presses A (talk, pick up, open, put in the shipping box...);
 *   - with no control of the player (text, menus, the title): left click
 *     is A and right click is B.
 * The arrows or the pad take over at once. A square shows the tile under
 * the pointer: solid within reach of the tool, faint farther away. */
enum { MT_NONE, MT_WALK, MT_TOOL, MT_ACT };
static struct {
  int fx, fy;                 /* the pointer on the field, -1: outside */
  bool left_held, follow;
  int click;                  /* 1 left, 2 right: waiting for the next frame */
  int task, tx, ty, stage, frames, still, lastx, lasty, axis_only, press_left;
  uint16_t press;
} s_mouse = { -1, -1 };

void mods_mouse(int fx, int fy, int button, bool down)
{
  s_mouse.fx = fx; s_mouse.fy = fy;
  if (button == 1) { s_mouse.left_held = down; if (down) s_mouse.click = 1; else s_mouse.follow = false; }
  else if (button == 2 && down) s_mouse.click = 2;
}
static bool mouse_controls_player(void)
{
  return g_ram[HM_MAP_ID] < HM_MAP_FIRST_PICTURE && g_ram[HM_INPUT_STATE] == 1 && hm_diary_loaded(g_ram);
}
/* the world tile under the pointer */
static bool mouse_tile(int *tx, int *ty)
{
  if (s_mouse.fx < 0) return false;
  int gx = s_mouse.fx - (mods_field_width() - 256) / 2, gy = s_mouse.fy;
  int wx = hm_rd16(g_ram, HM_CAM_X) + gx, wy = hm_rd16(g_ram, HM_CAM_Y) + gy;
  if (wx < 0 || wy < 0) return false;
  *tx = wx >> 4; *ty = wy >> 4;
  return true;
}
static void player_tile(int *x, int *y) { *x = hm_rd16(g_ram, 0xD6) >> 4; *y = hm_rd16(g_ram, 0xD8) >> 4; }
static int iabs(int v) { return v < 0 ? -v : v; }
static bool next_to(int ax, int ay, int bx, int by) { return iabs(ax - bx) + iabs(ay - by) == 1; }
/* toward the middle of tile (tx, ty): the arrows to hold, 0 there */
static uint16_t steer(int tx, int ty)
{
  int px = hm_rd16(g_ram, 0xD6), py = hm_rd16(g_ram, 0xD8);
  int dx = tx * 16 + 8 - px, dy = ty * 16 + 8 - py;
  uint16_t m = 0;
  if (dx > 2) m |= B_RIGHT; else if (dx < -2) m |= B_LEFT;
  if (dy > 2) m |= B_DOWN; else if (dy < -2) m |= B_UP;
  /* stuck on something: one axis at a time, the other first */
  if (s_mouse.axis_only == 1 && (m & (B_UP | B_DOWN))) m &= (uint16_t)(B_UP | B_DOWN);
  else if (s_mouse.axis_only == 2 && (m & (B_LEFT | B_RIGHT))) m &= (uint16_t)(B_LEFT | B_RIGHT);
  return m;
}
static void mouse_task(int task, int tx, int ty)
{
  s_mouse.task = task; s_mouse.tx = tx; s_mouse.ty = ty;
  s_mouse.stage = 0; s_mouse.frames = 0; s_mouse.still = 0; s_mouse.axis_only = 0;
  s_mouse.lastx = s_mouse.lasty = -1;
}
/* the tile next to (tx, ty) closest to the player */
static void beside(int tx, int ty, int *bx, int *by)
{
  static const int d[4][2] = { { 0, 1 }, { 0, -1 }, { 1, 0 }, { -1, 0 } };
  int px = hm_rd16(g_ram, 0xD6), py = hm_rd16(g_ram, 0xD8), best = 1 << 30;
  for (int i = 0; i < 4; i++) {
    int x = tx + d[i][0], y = ty + d[i][1];
    int e = iabs(x * 16 + 8 - px) + iabs(y * 16 + 8 - py);
    if (e < best) { best = e; *bx = x; *by = y; }
  }
}
uint16_t mods_mouse_pad(uint16_t pad)
{
  if (!g_cfg.mouse) { s_mouse.task = MT_NONE; s_mouse.click = 0; return pad; }
  int click = s_mouse.click; s_mouse.click = 0;
  if (s_mouse.press_left > 0) {                  /* a button being pressed */
    s_mouse.press_left--;
    return pad | s_mouse.press;
  }
  if (pad & (B_UP | B_DOWN | B_LEFT | B_RIGHT)) { s_mouse.task = MT_NONE; s_mouse.follow = false; return pad; }
  if (!mouse_controls_player()) {
    s_mouse.task = MT_NONE; s_mouse.follow = false;
    if (click) { s_mouse.press = click == 1 ? B_A : B_B; s_mouse.press_left = 3; return pad | s_mouse.press; }
    return pad;
  }
  int ptx, pty, tx, ty;
  player_tile(&ptx, &pty);
  if (click && mouse_tile(&tx, &ty)) {
    if (click == 2) mouse_task(MT_ACT, tx, ty);
    else if ((tx == ptx && ty == pty) || next_to(tx, ty, ptx, pty)) mouse_task(MT_TOOL, tx, ty);
    else { mouse_task(MT_WALK, tx, ty); s_mouse.follow = true; }
  }
  if (s_mouse.task == MT_WALK && s_mouse.follow && s_mouse.left_held && mouse_tile(&tx, &ty) &&
      (tx != s_mouse.tx || ty != s_mouse.ty)) { s_mouse.tx = tx; s_mouse.ty = ty; s_mouse.still = 0; s_mouse.axis_only = 0; }
  if (s_mouse.task == MT_NONE) return pad;

  uint16_t action = g_ram[HM_PLAYER_ACTION];
  if (++s_mouse.frames > 60 * 8) { s_mouse.task = MT_NONE; return pad; }   /* never forever */
  /* where to stand */
  int gx = s_mouse.tx, gy = s_mouse.ty;
  bool there;
  if (s_mouse.task == MT_WALK) there = false;
  else if ((ptx == gx && pty == gy) || next_to(ptx, pty, gx, gy)) there = true;
  else { beside(gx, gy, &gx, &gy); there = false; }
  if (!there) {
    uint16_t m = steer(gx, gy);
    if (!m) {                                   /* arrived */
      if (s_mouse.task == MT_WALK) { if (!s_mouse.left_held) s_mouse.task = MT_NONE; return pad; }
      return pad;                               /* next frame: turn and act */
    }
    int px = hm_rd16(g_ram, 0xD6), py = hm_rd16(g_ram, 0xD8);
    if (px == s_mouse.lastx && py == s_mouse.lasty && action != 0x0A) {
      if (++s_mouse.still == 10) s_mouse.axis_only = s_mouse.axis_only == 1 ? 2 : 1;
      if (s_mouse.still >= 40) { s_mouse.task = MT_NONE; return pad; }   /* blocked: give up */
    } else { s_mouse.still = 0; }
    s_mouse.lastx = px; s_mouse.lasty = py;
    return pad | m;
  }
  /* next to the tile (or on it): turn to it, then the button */
  if (action != 0 && action != 1 && action != 2) return pad;     /* busy: wait */
  int dx = s_mouse.tx - ptx, dy = s_mouse.ty - pty;
  if (dx || dy) {
    int want = dy > 0 ? 0 : dy < 0 ? 1 : dx > 0 ? 2 : 3;          /* $DA: down, up, right, left */
    if (hm_rd16(g_ram, 0xDA) != want) {
      static const uint16_t arrow[4] = { B_DOWN, B_UP, B_RIGHT, B_LEFT };
      if (++s_mouse.stage > 6) { s_mouse.task = MT_NONE; return pad; }
      return pad | arrow[want];
    }
  }
  s_mouse.press = s_mouse.task == MT_ACT ? B_A : B_Y;
  s_mouse.press_left = 3;
  s_mouse.task = MT_NONE;
  return pad | s_mouse.press;
}

/* ── One seed at a time (src/toolused.c, g_hm_single_seed) ──────────────
 * The sowing itself is the game's (one tile per use, 9 per bag, the bag's
 * seeds kept in the saved game); the pause menu shows the seeds left.
 * Here: the toss (the feed's animation, $74 + facing, about a second) runs
 * one and a half times as fast: every other frame, the player's game object
 * ($0905: record $019C + $24 x it) has one frame less to wait on its
 * picture (+12), as if the game had counted twice (CODE_8582C7). */
static void seeds_frame(uint8_t *ram)
{
  static unsigned tick;
  if (!g_cfg.single_seed || hm_rd16(ram, HM_PLAYER_ACTION) != 0x0A) return;
  uint8_t tool = ram[HM_TOOL_SELECTED];
  uint16_t anim = hm_rd16(ram, 0x0901);
  if (!((tool >= 5 && tool <= 8) || tool == 0x0C) || anim < 0x74 || anim > 0x77) return;
  if (++tick & 1) return;
  uint16_t obj = hm_rd16(ram, 0x0905);
  if (obj >= 40) return;
  uint8_t *timer = &ram[0x019C + 0x24 * obj + 0x12];
  if (*timer >= 2 && *timer < 0xFE) (*timer)--;
}

/* ── The crop bag (src/items.c, g_hm_bag) ───────────────────────────────
 * (Nothing is drawn over the game: the pause menu's Harvest tab shows the
 * bag.) Nine places for what the player carries over his head (crops, eggs,
 * milk, food, goods; not tools, animals, lumber or the things that go to a
 * place). The bag key (Q, the pad's left trigger, the mouse's middle
 * button):
 *   - holding such an item: it goes into the bag. The game's own way: its
 *     map object is set to state 8 (the step after shipping), which takes
 *     it away and frees the hands; nothing is shipped or counted;
 *   - hands free: the last item comes out, as when picking something up
 *     (action 4: the animation, then holding it), but nothing is left on
 *     the map (g_hm_bag_take);
 *   - the key held a second, hands free, where the last item was shipped
 *     (the player threw one
 *     into the shipping box from this very tile, facing this way, today:
 *     the day's shipped money, $7F1F07-$7F1F09, went up), before 5 pm:
 *     every item with a price, one after the other: out, then A to throw
 *     it. The game ships each one itself, so money and counts are its
 *     own. It stops when the bag is empty, at an item the box does not
 *     take, when a throw shipped nothing, or with the arrows.
 * The bag goes with the diary (src/save.c). It is part of the pause menu
 * (on only with it): the menu's Bag tab shows it and takes out the one
 * chosen (mods_bag_choose: moved to the end, then out as above). */
#define MAP_OBJECTS_AT 0x0B4E6          /* 10 x 16 bytes: +0 flags, +2 state, +3 item */
#define GAME_STATE     0x000D2
static struct {
  bool prev; int ship, wait, held;     /* held: frames the key has been down (hands free) */
  uint32_t shipped;                     /* the day's shipped money, last frame */
  bool spot; int sx, sy, sdir, sday;    /* where the last shipping throw was */
} s_bag = { .held = -1 };

static bool bag_item(uint8_t it)
{
  return (it >= 0x01 && it <= 0x0C) || (it >= 0x10 && it <= 0x19) || it == 0x24 || (it >= 0x58 && it <= 0x5A);
}
static int bag_count(void) { int n = 0; while (n < 9 && g_hm_bag[n]) n++; return n; }
static bool holding(void) { return hm_rd16(g_ram, GAME_STATE) & 0x0002; }
static uint16_t player_action(void) { return hm_rd16(g_ram, HM_PLAYER_ACTION); }
static uint32_t shipped_money(void) { return hm_rd24(g_ram, 0x11F07); }
static bool at_box(void)
{
  int x, y; player_tile(&x, &y);
  return s_bag.spot && x == s_bag.sx && y == s_bag.sy && hm_rd16(g_ram, 0xDA) == s_bag.sdir &&
         g_ram[0x11F1B] == s_bag.sday && g_ram[HM_MAP_ID] <= 3 && g_ram[0x11F1C] < 0x11;
}
/* the box takes it: it has a price ($81:9FDE, 2 bytes per item, +1) */
static bool sellable(uint8_t it) { return hm_bus_rd8(g_ram, 0x819FDE + 2u * it + 1) != 0; }
static bool bag_put(void)
{
  uint8_t it = g_ram[HM_ITEM_ON_HAND];
  int n = bag_count();
  if (!bag_item(it)) { host_toast(T("That does not go in the bag.")); return false; }
  if (n >= 9) { host_toast(T("The bag is full (9).")); return false; }
  for (int i = 0; i < 10; i++) {
    uint8_t *o = &g_ram[MAP_OBJECTS_AT + 16 * i];
    if ((o[0] & 1) && o[3] == it && o[2] == 2) {
      o[2] = 8;
      g_hm_bag[n] = it;
      return true;
    }
  }
  return false;
}
static bool bag_take(void)
{
  int n = bag_count();
  if (!n) return false;
  g_ram[HM_ITEM_ON_HAND] = g_hm_bag[n - 1];
  g_hm_bag[n - 1] = 0;
  g_hm_bag_take = 1;
  hm_wr16(g_ram, HM_PLAYER_ACTION, 4);
  return true;
}
static bool bag_on(void) { return g_cfg.crop_bag && g_cfg.pause_menu; }   /* part of the pause menu */
static int s_bag_pending = -1;          /* the pause menu chose one to take out */
int mods_bag_items(uint8_t out[9]) { int n = bag_on() ? bag_count() : 0; for (int i = 0; i < 9; i++) out[i] = i < n ? g_hm_bag[i] : 0; return n; }
bool mods_bag_choose(int i)
{
  if (!bag_on() || i < 0 || i >= bag_count()) return false;
  s_bag_pending = i;
  return true;
}
uint16_t mods_bag_pad(uint16_t pad, bool key)
{
  g_hm_bag_on = bag_on();
  bool press = key && !s_bag.prev;
  s_bag.prev = key;
  if (!bag_on() || !hm_diary_loaded(g_ram) || g_ram[HM_MAP_ID] >= HM_MAP_FIRST_PICTURE ||
      g_ram[HM_INPUT_STATE] != 1) { s_bag.ship = 0; s_bag_pending = -1; return pad; }
  uint16_t act = player_action();
  uint32_t money = shipped_money();
  bool shipped_now = money > s_bag.shipped;
  if (shipped_now) {                          /* a throw into the box: remember where */
    int x, y; player_tile(&x, &y);
    s_bag.spot = true; s_bag.sx = x; s_bag.sy = y; s_bag.sdir = hm_rd16(g_ram, 0xDA); s_bag.sday = g_ram[0x11F1B];
  }
  if (money < s_bag.shipped) s_bag.spot = false;               /* a new day */
  s_bag.shipped = money;
  if (s_bag.ship) {
    if (pad & (B_UP | B_DOWN | B_LEFT | B_RIGHT)) { s_bag.ship = 0; return pad; }
    if (++s_bag.wait > 180) { s_bag.ship = 0; return pad; }        /* never forever */
    switch (s_bag.ship) {
    case 1:                                    /* hands free: the next one out */
      if (holding() || (act != 0 && act != 9)) return pad;
      if (!bag_count() || !at_box() || !sellable(g_hm_bag[bag_count() - 1])) { s_bag.ship = 0; return pad; }
      bag_take(); s_bag.ship = 2; s_bag.wait = 0;
      return pad;
    case 2:                                    /* holding it: throw it into the box */
      if (!holding() || (act != 0 && act != 9)) return pad;
      if (!at_box()) { s_bag.ship = 0; return pad; }               /* it stays in the hands */
      s_bag.ship = 3; s_bag.wait = 0;
      return pad | B_A;
    case 3:                                    /* thrown: wait until it is shipped */
      if (s_bag.wait < 3) return pad | B_A;
      if (shipped_now) { s_bag.ship = 1; s_bag.wait = 0; }
      else if (s_bag.wait > 120) s_bag.ship = 0;                 /* nothing shipped: stop */
      return pad;
    }
    return pad;
  }
  if (s_bag_pending >= 0) {                 /* chosen in the pause menu: that one to the end, out */
    int i = s_bag_pending, n = bag_count();
    if (holding() || (act != 0 && act != 9)) return pad;    /* wait until the hands are free */
    s_bag_pending = -1;
    if (i < n) {
      uint8_t it = g_hm_bag[i];
      for (int j = i; j < n - 1; j++) g_hm_bag[j] = g_hm_bag[j + 1];
      g_hm_bag[n - 1] = it;
      bag_take();
    }
    return pad;
  }
  /* holding something: in at once. Hands free: a tap takes the last one
   * out (on letting go); held a second at the shipping spot, every one
   * goes into the box */
  if (press && holding()) { s_bag.held = -1; if (act <= 2) bag_put(); return pad; }
  if (press) s_bag.held = 0;
  if (s_bag.held < 0) return pad;
  if (key) {
    if (++s_bag.held == 60 && !holding() && (act == 0 || act == 9) && bag_count() && at_box()) {
      s_bag.held = -1; s_bag.ship = 1; s_bag.wait = 0;
    }
    return pad;
  }
  /* let go after a tap */
  s_bag.held = -1;
  if (!holding() && (act == 0 || act == 9) && bag_count()) bag_take();
  return pad;
}
/* The item's own sprite, the one the game draws over the player's head
 * (SUB_8180B7, case 1): DATA8_8196AF gives the item's list, its case 1 the
 * sprite; the sprite's entry ($86/$87:8080 + 2 x the number) its first
 * frame's piece list: a count, then 5 bytes a piece (the piece number,
 * X and Y offsets, attributes: palette bits 0-2, flips bits 4-5). A
 * piece's graphics are where the loader (CODE_858CB2) takes them from:
 * bank $88 + (piece >> 8), $8000 + $2000 x (bits 6-7) + 32 x the byte at
 * $86:8000 + 2 x (bits 0-5); a 16x16 is the 8x8 tiles at +0, +$20, +$200,
 * +$220. The colours: the OBJ palettes of the game's CGRAM copy. */
static uint32_t snes_rgb(uint16_t c)
{
  uint32_t r = (c & 31) << 3, g = ((c >> 5) & 31) << 3, b = ((c >> 10) & 31) << 3;
  return UI_RGB(r << 16 | g << 8 | b);
}
static bool item_sprite(uint8_t it, uint32_t out[256])
{
  memset(out, 0, 256 * sizeof *out);
  if (it == 0 || it > 0x5A) return false;
  uint32_t list = hm_bus_rd16(g_ram, 0x8196AF + it * 3u) | (uint32_t)hm_bus_rd8(g_ram, 0x8196AF + it * 3u + 2) << 16;
  uint16_t sprite = hm_bus_rd16(g_ram, list + 3);
  bool item_flip = hm_bus_rd8(g_ram, list + 5) & 1;
  uint32_t bank = sprite >= 0x262 ? 0x870000 : 0x860000;
  uint16_t number = sprite >= 0x262 ? (uint16_t)(sprite - 0x262) : sprite;
  uint16_t entry = hm_bus_rd16(g_ram, bank + 0x8080 + 2u * number);
  uint32_t pl = bank | hm_bus_rd16(g_ram, bank | entry);
  int count = hm_bus_rd8(g_ram, pl);
  if (count <= 0 || count > 8) return false;
  int minx = 99, miny = 99;
  for (int i = 0; i < count; i++) {
    int ox = (int8_t)hm_bus_rd8(g_ram, pl + 3 + 5u * i), oy = (int8_t)hm_bus_rd8(g_ram, pl + 4 + 5u * i);
    if (ox < minx) minx = ox;
    if (oy < miny) miny = oy;
  }
  bool any = false;
  for (int i = 0; i < count; i++) {
    uint16_t piece = hm_bus_rd16(g_ram, pl + 1 + 5u * i);
    int ox = (int8_t)hm_bus_rd8(g_ram, pl + 3 + 5u * i) - minx, oy = (int8_t)hm_bus_rd8(g_ram, pl + 4 + 5u * i) - miny;
    uint8_t attr = hm_bus_rd8(g_ram, pl + 5 + 5u * i);
    bool hf = ((attr >> 4) & 1) ^ item_flip, vf = (attr >> 5) & 1;
    uint16_t part = (uint16_t)((piece & 0xFFC0) >> 6), low = piece & 0x3F;
    uint32_t src = (uint32_t)(0x88 + (part >> 2)) << 16 |
                   (uint16_t)((hm_bus_rd16(g_ram, 0x868000 + 2u * low) & 0xFF) * 32u + ((part & 3u) << 13) + 0x8000);
    const uint8_t *cg = &g_ram[HM_CGRAM_SHADOW + 256 + (attr & 7) * 32];
    for (int y = 0; y < 16; y++)
      for (int x = 0; x < 16; x++) {
        int sx = hf ? 15 - x : x, sy = vf ? 15 - y : y;
        uint32_t t = src + (sx >= 8 ? 0x20 : 0) + (sy >= 8 ? 0x200 : 0);
        int r = sy & 7, b = 7 - (sx & 7);
        uint8_t p0 = hm_bus_rd8(g_ram, t + 2u * r), p1 = hm_bus_rd8(g_ram, t + 2u * r + 1);
        uint8_t p2 = hm_bus_rd8(g_ram, t + 16 + 2u * r), p3 = hm_bus_rd8(g_ram, t + 17 + 2u * r);
        int ci = ((p0 >> b) & 1) | ((p1 >> b) & 1) << 1 | ((p2 >> b) & 1) << 2 | ((p3 >> b) & 1) << 3;
        int X = ox + x, Y = oy + y;
        if (!ci || X < 0 || Y < 0 || X >= 16 || Y >= 16) continue;
        out[Y * 16 + X] = snes_rgb((uint16_t)(cg[2 * ci] | cg[2 * ci + 1] << 8));
        any = true;
      }
  }
  return any;
}

/* a picture of an item in the bag, 16 x 16: its own sprite, or, if it has
 * none, a little drawing (12 x 12, centred) */
void mods_bag_icon(int x, int y, uint8_t it)
{
  uint32_t px[256];
  if (item_sprite(it, px)) {
    for (int j = 0; j < 16; j++)
      for (int i = 0; i < 16; i++) if (px[j * 16 + i]) ui_rect(x + i, y + j, 1, 1, px[j * 16 + i]);
    return;
  }
  x += 2; y += 2;
  static const char *const TURNIP[12] = { "...g..g.....", "....gggg....", "...gggggg...", "....gggg....",
    "....pppp....", "...pwwwwp...", "..wwwwwwww..", "..wwwwwwww..", "..wwwwwwww..", "...wwwwww...", ".....ww.....", "......w....." };
  static const char *const POTATO[12] = { "............", "............", "...bbbbb....", "..bbbbbbbb..", ".bbbkbbbbbb.",
    ".bbbbbbbkbb.", ".bbkbbbbbbb.", ".bbbbbbbbbb.", "..bbbbkbbb..", "...bbbbbb...", "............", "............" };
  static const char *const TOMATO[12] = { "............", ".....gg.....", "...gggggg...", "..rrggggrr..", ".rrrrrrrrrr.",
    ".rrwrrrrrrr.", ".rrrrrrrrrr.", ".rrrrrrrrrr.", ".rrrrrrrrrr.", "..rrrrrrrr..", "...rrrrrr...", "............" };
  static const char *const CORN[12] = { "......g.....", ".....yyg....", "....yyyyg...", "...yyyyyg...", "...yyyyygg..",
    "..yyyyyygg..", "..yyyyygg...", ".gyyyyyg....", ".ggyyygg....", "..ggggg.....", "...gg.......", "............" };
  static const char *const EGG[12] = { "............", ".....ww.....", "....wwww....", "...wwwwww...", "...wwwwww...",
    "..wwwwwwww..", "..wwwwwwww..", "..wwwwwwww..", "..wwwwwwkw..", "...wwwwkw...", "....wwww....", "............" };
  static const char *const MILK[12] = { "....uuuu....", "....uuuu....", ".....ww.....", ".....ww.....", "....wwww....",
    "...wwwwww...", "...wuuuuw...", "...wuuuuw...", "...wwwwww...", "...wwwwww...", "...wwwwww...", "....kkkk...." };
  static const char *const SACK[12] = { "............", ".....kk.....", "....bbbb....", ".....bb.....", "...bbbbbb...",
    "..bbbbbbbb..", ".bbbbbbbbbb.", ".bbbbbbbbbb.", ".bbbbbbbbbb.", ".bbbbbbbbbb.", "..bbbbbbbb..", "............" };
  const char *const *p = it == 0x13 ? TURNIP : it == 0x12 ? POTATO : it == 0x11 ? TOMATO : it == 0x10 ? CORN :
                         it == 0x14 ? EGG : (it >= 0x15 && it <= 0x17) ? MILK : SACK;
  for (int j = 0; j < 12; j++)
    for (int i = 0; i < 12; i++) {
      uint32_t c;
      switch (p[j][i]) {
      case 'g': c = UI_RGB(0x4C9A3C); break;  case 'p': c = UI_RGB(0x9A4CA0); break;
      case 'w': c = UI_RGB(0xF8F4E4); break;  case 'b': c = UI_RGB(0xB07A40); break;
      case 'k': c = UI_RGB(0x6A4220); break;  case 'r': c = UI_RGB(0xD8402C); break;
      case 'y': c = UI_RGB(0xF0C838); break;  case 'u': c = UI_RGB(0x4878C8); break;
      default: continue;
      }
      ui_rect(x + i, y + j, 1, 1, c);
    }
}

/* the square on the tile under the pointer */
static bool mouse_hud(uint32_t *px, int w)
{
  int tx, ty, ptx, pty;
  if (!g_cfg.mouse || !mouse_controls_player() || !mouse_tile(&tx, &ty)) return false;
  if (!px) return true;
  player_tile(&ptx, &pty);
  bool reach = (tx == ptx && ty == pty) || next_to(tx, ty, ptx, pty);
  int x = tx * 16 - hm_rd16(g_ram, HM_CAM_X) + (w - 256) / 2, y = ty * 16 - hm_rd16(g_ram, HM_CAM_Y);
  ui_target(px, w, 224);
  uint32_t c = reach ? C_CREAM : UI_RGB(0x8A6236);
  ui_hline(x, y, 16, c); ui_hline(x, y + 15, 16, c);
  ui_vline(x, y, 16, c); ui_vline(x + 15, y, 16, c);
  if (reach) { ui_hline(x + 1, y + 1, 14, c); ui_hline(x + 1, y + 14, 14, c); ui_vline(x + 1, y + 1, 14, c); ui_vline(x + 14, y + 1, 14, c); }
  return true;
}
bool mods_hud(uint32_t *px, int w)
{
  return mouse_hud(px, w);    /* the seeds and the bag show in the pause menu, not over the game */
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
  memset(s_sway, 0, sizeof s_sway);
  s_crops.last_tx = s_crops.last_ty = -1;
  if (g_ppu) crops_hide();
  g_hm_oam_right = 0x0100; g_hm_oam_left = 0xFFF0;
}

void mods_frame(uint8_t *ram)
{
  cheats_frame(ram);
  qol_frame(ram);
  endless_frame(ram);
  seeds_frame(ram);
  if (g_ppu) { title_frame(); ws_frame(); crops_frame(); }
}
