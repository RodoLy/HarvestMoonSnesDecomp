/*
 * wife.c — the wife helps on the farm (an improvement, off by default).
 *
 * The offer. With 950 hearts or more (the pause menu's ten), the second
 * time in a day the player talks to her, when her line is over, she asks
 * whether she may help on the farm: today (or tomorrow, if it is already
 * past her morning), three days or a week. The last day she helps is kept
 * in the diary (g_hm_wife_until, src/save.c).
 *
 * The help. On those days, from 6:00 until 11:00, she works on the farm:
 * she fills troughs in the barn and feed boxes in the coop, then waters dry
 * crops and tilled soil, the nearest first. At most half of each (the
 * work there was when her day began): the rest is the player's. When the
 * player is on the farm she is seen doing it: the game's own event slot $13
 * (the one the game gives her on the farm) walks from task to task, in her
 * own pictures, and talking to her gives her usual lines; she goes into the
 * barn and the coop by their doors. Elsewhere the work is done all the same,
 * a task every second and a half.
 *
 * At 11:00 she stops and does what the game has her do at midday: Maria,
 * Nina and Ellen go to their place on the farm and their chores there
 * (group $44's own picture, at (360, 360)); Ann and Eve go into the tool
 * shed.
 *
 * Her clothes. Some days (one in two, by the day) she wears the clothes she
 * wore before the wedding: her own girl's pictures instead of the wife's,
 * on the farm and at home.
 *
 * Everything here happens at the start of a GameLoop frame, where nothing of
 * the game is half done; tiles change through the game's own ChangeMapTile,
 * as the watering can's (src/toolused.c water_tile).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hm_decomp.h"
#include "hm_ram.h"
#include "native.h"
#include "config.h"
#include "i18n.h"
#include "ui.h"
#include "gamefont.h"
#include "wife.h"

#define SLOT          0x0BA46u     /* event slot $13: $7E:B586 + $40 x $13 */
#define WIFE_FLAGS    0x11F66u
#define FARM_TILES    0x009B6u     /* the current map's tile types */
#define FARM_KEEP     0x0A4E6u     /* the farm's own copy, while elsewhere */
#define BOX_OPEN      0x11F5Au     /* bit 14: a text box is open */
#define SILO          0x11F10u     /* fodder in the silo (16 bits) */
#define HEN_FEED      0x0092Du     /* chicken feed */
#define COW_BITS      0x00932u
#define COW_COUNT     0x00930u
#define HEN_BITS      0x00934u
#define HEN_COUNT     0x00931u
#define PLAYER_X      0x000D6u
#define PLAYER_Y      0x000D8u
#define HELP_UNTIL_HOUR 11

enum { MARIA, ANN, NINA, ELLEN, EVE };
static const char *const kNames[5] = { "Maria", "Ann", "Nina", "Ellen", "Eve" };
/* her farm script's talking loop ($B5:xxxx), its picture table and entry (group $44) */
static const uint16_t kLoop[5] = { 0x9836, 0x9A34, 0x9C28, 0x9E10, 0xA017 };
static const uint16_t kFarmTable[5] = { 0x8AEC, 0x8AF8, 0x8B04, 0x8B04, 0x8AF8 };
static const uint8_t kFarmEntry[5] = { 0, 0, 0, 0, 3 };
/* pictures: down, down walking, up, up walking, side, side walking (the
 * side ones look left; flipped for right) */
static const uint16_t kWife[6] = { 0x2E2, 0x2E3, 0x2E4, 0x2E5, 0x2E6, 0x2E7 };
static const uint16_t kGirl[5][6] = {
  { 0x21E, 0x21F, 0x220, 0x221, 0x222, 0x223 },   /* Maria */
  { 0x226, 0x227, 0x228, 0x229, 0x22A, 0x22B },   /* Ann */
  { 0x24B, 0x24C, 0x24D, 0x24E, 0x24F, 0x250 },   /* Nina */
  { 0x23C, 0x23D, 0x23E, 0x23F, 0x240, 0x241 },   /* Ellen */
  { 0x244, 0x245, 0x246, 0x247, 0x248, 0x249 },   /* Eve */
};
/* the farm's doors (tiles) and where one stands to go in */
enum { BARN_X = 20, BARN_Y = 21, COOP_X = 28, COOP_Y = 21, SHED_X = 26, SHED_Y = 29, HOUSE_X = 8, HOUSE_Y = 20 };

enum { T_NONE, T_COWS, T_HENS, T_WATER, T_REST, T_ACTIVITY, T_DONE };
static struct {
  int day;                       /* the day the counts are for (absolute) */
  int talks;                     /* times she was talked to that day */
  bool offered, talking, want_dialog;
  /* the day's work */
  bool planned;
  int water_quota, water_done, cow_quota, cow_done, hen_quota, hen_done;
  int task;                      /* T_* */
  int px, py;                    /* her position (pixels), for the farm */
  int tx, ty;                    /* the tile she goes to */
  int path[96][2], plen, pi;     /* the tiles on the way */
  int wait;                      /* frames left for what she is doing */
  int inside;                    /* frames left in the barn / coop */
  int facing;                    /* 0 down, 1 up, 2 right, 3 left */
  unsigned frame;
  int offmap_tick;
  bool spawned;                  /* we put her in slot $13 */
  bool controlled;               /* she worked here, as we moved her */
  bool moving;                   /* she took a step this frame */
  /* the dialog */
  int sel, page;
} W;

static int rd16(uint32_t a) { return g_ram[a] | g_ram[a + 1] << 8; }
static void wr16(uint32_t a, int v) { g_ram[a] = (uint8_t)v; g_ram[a + 1] = (uint8_t)(v >> 8); }
static int wife_of(void)
{
  int f = g_ram[WIFE_FLAGS] & 0x1F;
  for (int i = 0; i < 5; i++) if (f & (1 << i)) return i;
  return -1;
}
static int hearts_of(int w) { return rd16(HM_HEARTS_BASE + 2u * (unsigned)w); }
static int today(void) { return g_ram[HM_YEAR] * 120 + (g_ram[HM_SEASON] & 3) * 30 + g_ram[HM_DAY]; }
static bool single_day(void) { unsigned d = (unsigned)today(); d = d * 2654435761u; return (d >> 16) & 1; }
static bool on_farm(void) { return g_ram[HM_MAP_ID] <= 3; }
static bool helping_today(void) { return g_hm_wife_until && today() <= g_hm_wife_until; }

/* ── The slot ──────────────────────────────────────────────────────────── */
static bool slot_is_wife(void)
{
  if (!g_ram[SLOT]) return false;
  int bank = g_ram[SLOT + 0x32], at = rd16(SLOT + 0x30);
  return bank == 0xB5 && at >= 0x8000 && at < 0xA200;
}
static void slot_picture(uint16_t sprite, bool flip)
{
  if (rd16(SLOT + 0x16) == sprite && ((g_ram[SLOT + 0x14] & 0x40) != 0) == flip) return;
  wr16(SLOT + 0x16, sprite);
  wr16(SLOT + 0x14, flip ? 0x40 : 0);
  g_ram[SLOT + 0x01] |= 0x04;                    /* the picture changed */
}
static const uint16_t *clothes(int w) { return single_day() ? kGirl[w] : kWife; }
/* her picture for a facing, standing or in mid-step */
static void pose(int w, int facing, bool step)
{
  const uint16_t *c = clothes(w);
  int k = facing == 0 ? 0 : facing == 1 ? 2 : 4;
  slot_picture(c[k + (step ? 1 : 0)], facing == 2);
}
/* the slot, as her farm script would have it, talking as there */
static void spawn(int w, int x, int y)
{
  memset(&g_ram[SLOT], 0, 0x40);
  g_ram[SLOT + 0x00] = 1;
  g_ram[SLOT + 0x01] = 0x03;                     /* a picture; make its game object */
  g_ram[SLOT + 0x10] = 1;
  wr16(SLOT + 0x16, clothes(w)[0]); wr16(SLOT + 0x18, clothes(w)[0]);
  wr16(SLOT + 0x1A, x); wr16(SLOT + 0x1C, y);
  wr16(SLOT + 0x1E, x); wr16(SLOT + 0x20, y);
  wr16(SLOT + 0x30, kLoop[w]); g_ram[SLOT + 0x32] = 0xB5;
  wr16(SLOT + 0x33, kFarmTable[w]);
  g_ram[SLOT + 0x3F] = 0x13;
  W.spawned = true;
}
static void unspawn(void)
{
  if (g_ram[SLOT] && slot_is_wife()) g_ram[SLOT + 0x01] |= 0x40;   /* the game frees it */
  W.spawned = false;
}
static void place(int x, int y) { wr16(SLOT + 0x1A, x); wr16(SLOT + 0x1C, y); }

/* ── The farm's tiles ──────────────────────────────────────────────────── */
static uint8_t *tiles(void) { return &g_ram[on_farm() ? FARM_TILES : FARM_KEEP]; }
static bool dry(uint8_t t) { return t == 0x07 || t == 0x1E || (t >= 0x20 && t <= 0x6E && !(t & 1)); }
static uint32_t props(void) { return (uint32_t)(g_ram[0x0D] | g_ram[0x0E] << 8 | g_ram[0x0F] << 16); }
static bool walkable(int x, int y)
{
  if (x < 1 || y < 1 || x >= 63 || y >= 63) return false;
  uint8_t t = tiles()[y * 64 + x];
  if (t >= 0x1E && t <= 0x6F) return false;              /* never through the crops */
  if (t >= 0xC0 && t <= 0xCF) return false;              /* nor the doors */
  return !(hm_bus_rd8(g_ram, props() + 4u * t + 2) & 0x1F);
}
static int count_dry(void)
{
  int n = 0;
  const uint8_t *t = &g_ram[FARM_KEEP];
  if (on_farm()) t = &g_ram[FARM_TILES];
  for (int i = 0; i < 64 * 64; i++) if (dry(t[i])) n++;
  return n;
}

/* the way to a tile next to (tx, ty) (or onto it, when `onto`), the nearest
 * dry tile when tx < 0: breadth first over the walkable tiles */
static bool route(int fx, int fy, int tx, int ty, bool onto)
{
  static int16_t from[64 * 64];
  static int queue[64 * 64];
  memset(from, 0xFF, sizeof from);
  int qh = 0, qt = 0, start = fy * 64 + fx, goal = -1;
  from[start] = (int16_t)start; queue[qt++] = start;
  static const int d4[4][2] = { { 0, 1 }, { 0, -1 }, { 1, 0 }, { -1, 0 } };
  while (qh < qt && goal < 0) {
    int c = queue[qh++], cx = c % 64, cy = c / 64;
    if (tx < 0) {                                    /* the nearest dry tile, beside */
      for (int k = 0; k < 4 && goal < 0; k++) {
        int nx = cx + d4[k][0], ny = cy + d4[k][1];
        if (nx >= 0 && ny >= 0 && nx < 64 && ny < 64 && dry(tiles()[ny * 64 + nx])) { goal = c; W.tx = nx; W.ty = ny; }
      }
    } else if ((onto && cx == tx && cy == ty) || (!onto && abs(cx - tx) + abs(cy - ty) == 1)) goal = c;
    if (goal >= 0) break;
    for (int k = 0; k < 4; k++) {
      int nx = cx + d4[k][0], ny = cy + d4[k][1], n = ny * 64 + nx;
      if (nx < 0 || ny < 0 || nx >= 64 || ny >= 64 || from[n] != -1) continue;
      if (!walkable(nx, ny) && !(onto && nx == tx && ny == ty)) continue;
      from[n] = (int16_t)c; queue[qt++] = n;
    }
  }
  if (goal < 0) return false;
  int n = 0, rev[64 * 64 > 96 ? 96 : 96][2], c = goal;
  while (c != start && n < 96) { rev[n][0] = c % 64; rev[n][1] = c / 64; n++; c = from[c]; }
  if (c != start) return false;                      /* too far: not this time */
  W.plen = n; W.pi = 0;
  for (int i = 0; i < n; i++) { W.path[i][0] = rev[n - 1 - i][0]; W.path[i][1] = rev[n - 1 - i][1]; }
  if (tx >= 0) { W.tx = tx; W.ty = ty; }
  return true;
}

/* ── The work itself ───────────────────────────────────────────────────── */
static void water_now(uint8_t *ram, const HmRegs *regs, int x, int y)
{
  uint8_t t = tiles()[y * 64 + x];
  if (!dry(t)) return;
  uint8_t wet = (uint8_t)(t + 1);
  if (on_farm()) {
    HmRegs r = *regs;
    r.db = 0x7E; r.d = 0;
    r.a = wet; r.x = (uint16_t)(x * 16); r.y = (uint16_t)(y * 16);
    hm_EditTileonMap(ram, &r);
    uint8_t change = hm_bus_rd8(ram, props() + 4u * wet);
    r = *regs; r.db = 0x7E; r.d = 0;
    r.a = change; r.x = (uint16_t)(x * 16); r.y = (uint16_t)(y * 16);
    hm_ChangeMapTile(ram, &r);
  } else {
    g_ram[FARM_KEEP + y * 64 + x] = wet;
  }
  W.water_done++;
}
static bool feed_cows(void)                        /* one trough; false: nothing to do */
{
  if (g_ram[HM_MAP_ID] == 0x27 || W.cow_done >= W.cow_quota) return false;
  int bits = rd16(COW_BITS);
  for (int i = 0; i < 12; i++) {
    const uint8_t *c = &g_ram[HM_COW_DATA + 16 * i];
    if (!(c[0] & 0x08) || c[2] != 0x27) continue;   /* an adult cow in the barn */
    int bit = (c[0] & 0x40) ? 1 << 12 : 1 << i;     /* a pregnant one: trough 12 */
    if (bits & bit) continue;
    if (rd16(SILO) <= 0) return false;
    wr16(SILO, rd16(SILO) - 1);
    wr16(COW_BITS, bits | bit);
    g_ram[COW_COUNT]++;
    W.cow_done++;
    return true;
  }
  return false;
}
static bool feed_hens(void)
{
  if (g_ram[HM_MAP_ID] == 0x28 || W.hen_done >= W.hen_quota || g_ram[HEN_FEED] == 0) return false;
  int bits = rd16(HEN_BITS);
  for (int i = 0; i < 12; i++)
    if (!(bits & (1 << i))) {
      g_ram[HEN_FEED]--;
      wr16(HEN_BITS, bits | 1 << i);
      g_ram[HEN_COUNT]++;
      W.hen_done++;
      return true;
    }
  return false;
}

static void plan_day(void)
{
  int cows = 0, hens = 0;
  for (int i = 0; i < 12; i++) { const uint8_t *c = &g_ram[HM_COW_DATA + 16 * i]; if ((c[0] & 0x08) && c[2] == 0x27) cows++; }
  for (int i = 0; i < 13; i++) { const uint8_t *h = &g_ram[HM_CHICKEN_DATA + 8 * i]; if ((h[0] & 0x08) && h[1] == 0x28) hens++; }
  int fed_c = 0, fed_h = g_ram[HEN_COUNT];
  for (int b = rd16(COW_BITS); b; b &= b - 1) fed_c++;
  W.cow_quota = (cows - 0) / 2; W.hen_quota = hens / 2;
  (void)fed_c; (void)fed_h;
  W.water_quota = count_dry() / 2;
  W.water_done = W.cow_done = W.hen_done = 0;
  W.task = W.cow_quota ? T_COWS : W.hen_quota ? T_HENS : T_WATER;
  W.px = HOUSE_X * 16 + 8; W.py = (HOUSE_Y + 1) * 16 + 8;
  W.plen = W.pi = 0; W.wait = 0; W.inside = 0; W.facing = 0;
  W.planned = true;
}

/* the next task once one is done */
static void next_task(void)
{
  W.plen = W.pi = 0; W.wait = 0;
  if (W.task == T_COWS) W.task = W.hen_quota > W.hen_done ? T_HENS : T_WATER;
  else if (W.task == T_HENS) W.task = T_WATER;
  else if (W.task == T_WATER && W.water_done >= W.water_quota) W.task = T_REST;
}

/* elsewhere than the farm: a task now and then */
static void work_away(uint8_t *ram, const HmRegs *regs)
{
  if (++W.offmap_tick < 90) return;
  W.offmap_tick = 0;
  switch (W.task) {
  case T_COWS: if (!feed_cows()) next_task(); break;
  case T_HENS: if (!feed_hens()) next_task(); break;
  case T_WATER:
    if (W.water_done >= W.water_quota) { next_task(); break; }
    for (int i = 0; i < 64 * 64; i++)
      if (dry(g_ram[FARM_KEEP + i])) { water_now(ram, regs, i % 64, i / 64); return; }
    W.task = T_REST;
    break;
  default: break;
  }
}

/* one step along the way; true when there */
static bool walk(int w)
{
  if (W.pi >= W.plen) return true;
  int gx = W.path[W.pi][0] * 16 + 8, gy = W.path[W.pi][1] * 16 + 8;
  int dx = gx - W.px, dy = gy - W.py;
  if (dx) { W.px += dx > 0 ? 1 : -1; W.facing = dx > 0 ? 2 : 3; }
  else if (dy) { W.py += dy > 0 ? 1 : -1; W.facing = dy > 0 ? 0 : 1; }
  W.moving = true;
  if (W.px == gx && W.py == gy) W.pi++;
  pose(w, W.facing, (W.frame / 10) & 1);
  place(W.px, W.py);
  return W.pi >= W.plen;
}
static void face_tile(int w)
{
  int dx = W.tx * 16 + 8 - W.px, dy = W.ty * 16 + 8 - W.py;
  W.facing = abs(dx) > abs(dy) ? (dx > 0 ? 2 : 3) : (dy > 0 ? 0 : 1);
  pose(w, W.facing, false);
}

/* on the farm, seen */
static void work_here(uint8_t *ram, const HmRegs *regs, int w)
{
  int hx = W.px / 16, hy = W.py / 16;
  if (W.inside > 0) {                                /* in the barn or the coop */
    if (--W.inside == 0) {
      if (W.task == T_COWS) while (feed_cows()) {}
      else while (feed_hens()) {}
      W.px = W.tx * 16 + 8; W.py = (W.ty + 1) * 16 + 8; W.facing = 0;
      place(W.px, W.py); pose(w, 0, false);
      next_task();
    }
    return;
  }
  if (W.wait > 0) {                                  /* watering */
    if (--W.wait == 0) { water_now(ram, regs, W.tx, W.ty); next_task(); }
    return;
  }
  switch (W.task) {
  case T_COWS: case T_HENS: {
    int dx = W.task == T_COWS ? BARN_X : COOP_X, dy = W.task == T_COWS ? BARN_Y : COOP_Y;
    if (!W.plen && !route(hx, hy, dx, dy + 1, true)) { next_task(); return; }
    W.tx = dx; W.ty = dy;
    if (walk(w)) { W.inside = 150; place(0x0F00, 0x0F00); W.plen = 0; }
    return;
  }
  case T_WATER:
    if (W.water_done >= W.water_quota) { W.task = T_REST; return; }
    if (!W.plen) {
      if (!route(hx, hy, -1, -1, false)) { W.task = T_REST; return; }
    }
    if (walk(w)) {
      if (!dry(tiles()[W.ty * 64 + W.tx])) { W.plen = 0; return; }   /* the player was quicker */
      face_tile(w); W.wait = 40;
    }
    return;
  case T_REST:                                       /* done: by the house's door */
    if (!W.plen && !(hx == HOUSE_X + 1 && hy == HOUSE_Y + 1)) {
      if (!route(hx, hy, HOUSE_X + 1, HOUSE_Y + 1, true)) { W.plen = 0; W.task = T_DONE; return; }
    }
    if (W.plen && walk(w)) { W.plen = 0; W.facing = 0; pose(w, 0, false); }
    return;
  default: return;
  }
}

/* at 11:00: what the game has her do at midday */
static void activity(int w)
{
  int hx = W.px / 16, hy = W.py / 16;
  bool shed = w == ANN || w == EVE;
  int gx = shed ? SHED_X : 22, gy = shed ? SHED_Y + 1 : 22;
  if (!W.plen && !(hx == gx && hy == gy) && !route(hx, hy, gx, gy, true)) W.plen = 0;
  if (W.plen && !walk(w)) return;
  if (shed) { unspawn(); W.task = T_DONE; return; }
  /* her chores, as her farm script puts her */
  W.px = 0x168; W.py = 0x168; place(W.px, W.py);
  uint32_t tab = 0xB30000u | kFarmTable[w];
  uint16_t sprite = hm_bus_rd16(g_ram, tab + 3u * kFarmEntry[w]);
  uint8_t flip = hm_bus_rd8(g_ram, tab + 3u * kFarmEntry[w] + 2);
  g_ram[SLOT + 0x02] = kFarmEntry[w];
  slot_picture(sprite, flip & 1);
  W.task = T_DONE;
}

/* ── Every frame ───────────────────────────────────────────────────────── */
void wife_reset(void) { memset(&W, 0, sizeof W); }

void wife_clean_point(uint8_t *ram, const HmRegs *regs)
{
  g_hm_wife_on = g_cfg.wife_help;
  if (!g_cfg.wife_help || !hm_diary_loaded(ram)) return;
  int w = wife_of();
  if (w < 0) return;
  W.frame++;
  int day = today(), hour = ram[HM_HOUR];
  if (day != W.day) { int keep_until = 0; (void)keep_until; memset(&W, 0, sizeof W); W.day = day; }

  /* the offer: the second talk of the day, once her line is over */
  bool box = rd16(BOX_OPEN) & 0x4000;
  if (box && !W.talking && slot_is_wife()) {
    int dx = rd16(SLOT + 0x1A) - rd16(PLAYER_X), dy = rd16(SLOT + 0x1C) - rd16(PLAYER_Y);
    if (dx * dx + dy * dy < 40 * 40) { W.talking = true; W.talks++; }
  }
  if (!box && W.talking) {
    W.talking = false;
    if (W.talks >= 2 && !W.offered && hearts_of(w) >= 950 && hour < 18 && !helping_today()) {
      W.offered = true;
      W.want_dialog = true;
    }
  }

  /* her clothes, wherever the game has her */
  if (slot_is_wife() && single_day()) {
    int s = rd16(SLOT + 0x16);
    for (int k = 0; k < 6; k++) if (s == kWife[k]) slot_picture(kGirl[w][k], g_ram[SLOT + 0x14] & 0x40);
  }

  /* the help (it waits while someone talks) */
  if (!helping_today() || hour < 6 || box) return;
  if (!W.planned) plan_day();
  bool house = ram[HM_MAP_ID] >= 0x15 && ram[HM_MAP_ID] <= 0x17;
  if (hour < HELP_UNTIL_HOUR) {
    if (house && slot_is_wife()) place(0x0F00, 0x0F00);   /* out working */
    if (!on_farm()) { W.spawned = W.controlled = false; work_away(ram, regs); return; }
    if (!slot_is_wife()) { spawn(w, W.px, W.py); return; }  /* the next frame, with its object */
    if (!(g_ram[SLOT + 0x01] & 0x02) && W.task != T_DONE) {
      W.controlled = true; W.moving = false;
      work_here(ram, regs, w);
      if (!W.inside && !W.moving) pose(w, W.facing, false);   /* after a talk, as she was */
    }
    return;
  }
  if (on_farm() && W.task != T_DONE) {
    if (!slot_is_wife() || !W.controlled) { W.task = T_DONE; return; }   /* the game's own midday */
    if (W.task != T_ACTIVITY) { W.task = T_ACTIVITY; W.plen = 0; }
    activity(w);
  }
}

/* ── The offer ─────────────────────────────────────────────────────────── */
bool wife_dialog_wanted(void) { bool b = W.want_dialog; W.want_dialog = false; return b; }
void wife_dialog_open(void) { W.sel = 0; W.page = 0; }

static const int kDays[3] = { 1, 3, 7 };
static int s_ox;                       /* where the 256 columns start (widescreen) */
static void accept(int days)
{
  int start = g_ram[HM_HOUR] < HELP_UNTIL_HOUR ? today() : today() + 1;
  g_hm_wife_until = (uint16_t)(start + days - 1);
  W.planned = false;
}
bool wife_dialog_update(const UiInput *in)
{
  if (W.page == 1) return in->accept || in->back || in->click || in->rclick;
  int n = 4;
  if (in->pressed & B_UP) W.sel = (W.sel + n - 1) % n;
  if (in->pressed & B_DOWN) W.sel = (W.sel + 1) % n;
  for (int i = 0; i < n; i++) {
    int y = 90 + i * 16;
    if (in->mouse_x >= s_ox + 138 && in->mouse_x < s_ox + 242 && in->mouse_y >= y && in->mouse_y < y + 16) {
      if (in->mouse_moved) W.sel = i;
      if (in->click) { W.sel = i; goto chosen; }
    }
  }
  if (in->back || in->rclick) { W.sel = 3; goto chosen; }
  if (!in->accept) return false;
chosen:
  if (W.sel < 3) accept(kDays[W.sel]);
  W.page = W.sel < 3 ? 1 : 2;
  return W.page == 2;
}
void wife_dialog_draw(uint32_t *dst, int w)
{
  ui_target(dst, w, 224);
  int ox = (w - 256) / 2;
  s_ox = ox;
  /* in the game's own letters (gamefont.c) */
  ui_frame(ox + 4, 160, 248, 60, C_PAPER);
  if (W.page == 1) {
    gamefont_wrap(ox + 14, 166, 232, T("Leave the farm to me, darling! I'll do half of the work every morning."), C_INK);
    return;
  }
  gamefont_wrap(ox + 14, 166, 232, T("Darling, may I help you on the farm?"), C_INK);
  static const char *opts[4] = { "One day", "Three days", "A week", "No, thanks" };
  ui_frame(ox + 132, 84, 116, 74, C_PAPER);
  for (int i = 0; i < 4; i++) {
    int y = 90 + i * 16;
    if (i == W.sel) { ui_rect(ox + 138, y, 104, 16, C_SELECT); ui_hand(ox + 133, y + 4); }
    gamefont_text(ox + 148, y, T(opts[i]), C_INK);
  }
}
