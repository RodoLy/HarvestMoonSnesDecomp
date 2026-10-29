/*
 * pause.c — a Harvest Moon 64-style pause menu, for the native build.
 *
 * Ported from the recompilation (HarvestMoonSNESRecomp, hm_pause.c). Opened
 * with M on the keyboard or the right stick's click (R3), neither of which a
 * SNES pad has, so no game button is taken. The game stands still at a
 * frame's end while it is open (the host's loop, main.c), so reading and
 * writing WRAM here is as safe as the game's own code between frames; the
 * music keeps playing (the sound chip runs on its own).
 *
 * Drawn at the console's resolution, 256x224, so its pixels are the game's
 * size: a checkered floor, wooden frames with parchment inside, pixel type
 * and 16x16 item icons (ui.c). The girls' faces are the game's own sprites,
 * read from your ROM when the menu opens (kGirls).
 *
 * Pages (L / R):
 *   Tools   the hand and backpack at the top, the tool shed as a grid.
 *           Equip, swap and put away follow the game's own shed routine
 *           CODE_8187C3 and the tool table at $81:BE0F (hm_ram.h).
 *   Hearts  the five girls' affection ($7F1F1F.., 0..999).
 *   Farm    money, shipping, stock, cows, happiness, tomorrow's weather.
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "hm_decomp.h"
#include "hm_ram.h"
#include "native.h"
#include "ui.h"
#include "i18n.h"
#include "pause.h"

enum { GRID_COLS = 8, GRID_ROWS = 3, LW = 256, LH = 224 };

static struct {
  bool open;
  int tab;               /* 0 tools, 1 hearts, 2 farm */
  int cx, cy;            /* tools cursor: cy -1 = the top slots (cx 0 hand, 1 bag) */
  char message[96];      /* the tools page's feedback line */
} s;

/* ── Game data ───────────────────────────────────────────────────────── */
typedef struct { uint8_t id; const char *name; } ToolName;
/* ids are the game's own item numbers (hm_ram.h, $0921); this order is the
 * grid's. */
static const ToolName kTools[] = {
  {0x01, "Sickle"}, {0x02, "Hoe"}, {0x03, "Hammer"}, {0x04, "Ax"},
  {0x10, "Watering Can"}, {0x0E, "Milker"}, {0x0F, "Brush"}, {0x0B, "Bell"},
  {0x05, "Corn Seeds"}, {0x06, "Tomato Seeds"}, {0x07, "Potato Seeds"}, {0x08, "Turnip Seeds"},
  {0x0C, "Grass Seeds"}, {0x09, "Cow Medicine"}, {0x0A, "Miracle Potion"}, {0x0D, "Paint"},
  {0x11, "Gold Sickle"}, {0x12, "Gold Hoe"}, {0x13, "Gold Hammer"}, {0x14, "Gold Ax"},
  {0x15, "Sprinkler"}, {0x16, "Magic Beans"}, {0x17, "Blue Diamond"}, {0x18, "Blue Feather"},
};
enum { NTOOLS = sizeof kTools / sizeof kTools[0] };

static const char *tool_name(uint8_t id)
{
  if (id == 0) return T("(nothing)");
  if (id == 0x19) return T("Chicken Feed");
  if (id == 0x1A) return T("Cow Feed");
  for (int i = 0; i < NTOOLS; i++) if (kTools[i].id == id) return T(kTools[i].name);
  return "?";
}
static unsigned item_count(uint8_t id)
{
  if (id >= 0x05 && id <= 0x08) return g_ram[HM_SEEDS_CORN_N + (id - 0x05)];
  if (id == 0x0C) return g_ram[HM_SEEDS_GRASS_N];
  return 0;
}
/* The shed slot of an item: DATA16_81BE0F, bytes 6 (row) and 7 (mask) of
 * each 8-byte entry. Read from the ROM so it can never disagree with it. */
static bool shed_slot(uint8_t id, int *row, uint8_t *mask)
{
  if (id == 0 || id > 0x1A || !g_rom) return false;
  const uint8_t *e = g_rom + 0xBE0F + id * 8;   /* $81:BE0F, LoROM file offset */
  *row = e[6]; *mask = e[7];
  return *mask != 0;
}
static bool in_shed(uint8_t id)
{
  int row; uint8_t mask;
  return shed_slot(id, &row, &mask) && (g_ram[0x11F00 + row] & mask);
}
static void set_in_shed(uint8_t id, bool on)
{
  int row; uint8_t mask;
  if (!shed_slot(id, &row, &mask)) return;
  if (on) g_ram[0x11F00 + row] |= mask; else g_ram[0x11F00 + row] &= (uint8_t)~mask;
}
static bool player_free(void)
{
  /* Normal control, nothing carried, and the player either idle (0), in the
   * standing-still idle animation (9), walking (1) or running (2): never in
   * the middle of using, showing or dropping something. */
  uint16_t act = hm_rd16(g_ram, HM_PLAYER_ACTION);
  return hm_diary_loaded(g_ram) && g_ram[HM_INPUT_STATE] == 1 &&
         (act == 0 || act == 1 || act == 2 || act == 9) && g_ram[HM_ITEM_ON_HAND] == 0;
}

static void say(const char *m) { snprintf(s.message, sizeof s.message, "%s", m); }

/* The item under the cursor (0 = none). */
static uint8_t cursor_item(void)
{
  if (s.cy < 0) return g_ram[s.cx == 0 ? HM_TOOL_SELECTED : HM_TOOL_BACKPACK];
  int i = s.cy * GRID_COLS + s.cx;
  return i < NTOOLS && in_shed(kTools[i].id) ? kTools[i].id : 0;
}

/* Equip a shed item -- CODE_8187C3: the hand tool drops into an empty
 * backpack; otherwise it goes back to the shed (the game never lets an item
 * be in two places). Then the new item leaves the shed. */
static void equip_from_shed(uint8_t id)
{
  uint8_t hand = g_ram[HM_TOOL_SELECTED];
  if (hand) {
    if (g_ram[HM_TOOL_BACKPACK] == 0) g_ram[HM_TOOL_BACKPACK] = hand;
    else set_in_shed(hand, true);
  }
  g_ram[HM_TOOL_SELECTED] = id;
  set_in_shed(id, false);
  char m[96]; snprintf(m, sizeof m, T("Now holding: %s"), tool_name(id)); say(m);
}
static void swap_hand_bag(void)
{
  uint8_t t = g_ram[HM_TOOL_SELECTED];       /* the game's own X swap */
  g_ram[HM_TOOL_SELECTED] = g_ram[HM_TOOL_BACKPACK];
  g_ram[HM_TOOL_BACKPACK] = t;
  say(T("Swapped hand and backpack."));
}

static void action_a(void)
{
  if (s.tab != 0) return;
  if (!player_free()) { say(T("You can't change tools right now.")); return; }
  if (s.cy < 0) { if (g_ram[HM_TOOL_BACKPACK] || g_ram[HM_TOOL_SELECTED]) swap_hand_bag(); return; }
  uint8_t id = cursor_item();
  if (id) equip_from_shed(id);
}
static void action_x(void)
{
  if (s.tab != 0) return;
  if (!player_free()) { say(T("You can't change tools right now.")); return; }
  swap_hand_bag();
}
static void action_y(void)   /* put the hand (or the selected bag) item away */
{
  if (s.tab != 0) return;
  if (!player_free()) { say(T("You can't change tools right now.")); return; }
  uint32_t where = (s.cy < 0 && s.cx == 1) ? HM_TOOL_BACKPACK : HM_TOOL_SELECTED;
  uint8_t id = g_ram[where];
  int row; uint8_t mask;
  if (!id) { say(T("Nothing to put away.")); return; }
  if (!shed_slot(id, &row, &mask) || row > 2) { say(T("That doesn't go in the tool shed.")); return; }
  set_in_shed(id, true);
  g_ram[where] = 0;
  char m[96]; snprintf(m, sizeof m, T("Put away: %s"), tool_name(id)); say(m);
}

/* ── The girls' faces: the game's own sprites, read from your ROM ────── */
/* Front-facing frame of each girl in the NPC sprite sheet ($8F:8000, file
 * 0x78000: 4bpp tiles, 16 per 0x200-byte row; a frame is 2 tiles wide, its
 * top two rows are the head) and her sprite palette ($A9:DE20.., the game's
 * OBJ palette table). Found by watching the flower festival: talking to each
 * girl raised exactly one heart counter, and the tiles she was drawn with
 * matched these ROM bytes. Hearts order: Maria, Ann, Nina, Ellen, Eve. */
static const struct { uint32_t frame, palette; } kGirls[5] = {
  { 0x078200, 0x14DE20 },   /* Maria: blue hair */
  { 0x078B00, 0x14DE20 },   /* Ann: orange ponytail */
  { 0x07B2C0, 0x14DE40 },   /* Nina: pink hair */
  { 0x07A280, 0x14DE80 },   /* Ellen: brown hair */
  { 0x07A3C0, 0x14DE40 },   /* Eve: blonde */
};
static uint32_t s_face[5][16 * 16];
static bool s_faces_ok;

static void load_faces(void)
{
  if (s_faces_ok || !g_rom) return;
  for (int g = 0; g < 5; g++) {
    uint32_t pal[16];
    for (int i = 0; i < 16; i++) {
      uint16_t v = g_rom[kGirls[g].palette + i * 2] | g_rom[kGirls[g].palette + i * 2 + 1] << 8;
      uint32_t r = (v & 31) << 3, gg = ((v >> 5) & 31) << 3, b = ((v >> 10) & 31) << 3;
      pal[i] = i ? (0xFF000000u | r << 16 | gg << 8 | b | (r >> 5) << 16 | (gg >> 5) << 8 | (b >> 5)) : 0;
    }
    for (int ty = 0; ty < 2; ty++)
      for (int tx = 0; tx < 2; tx++) {
        const uint8_t *t = g_rom + kGirls[g].frame + ty * 0x200 + tx * 0x20;
        for (int y = 0; y < 8; y++)
          for (int x = 0; x < 8; x++) {
            int b = 7 - x;
            int c = ((t[y * 2] >> b) & 1) | (((t[y * 2 + 1] >> b) & 1) << 1) |
                    (((t[16 + y * 2] >> b) & 1) << 2) | (((t[16 + y * 2 + 1] >> b) & 1) << 3);
            s_face[g][(ty * 8 + y) * 16 + tx * 8 + x] = pal[c];
          }
      }
  }
  s_faces_ok = true;
}

/* ── Pieces of the screen ────────────────────────────────────────────── */
static const char *kDays[7] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
static const char *kSeasons[4] = { "Spring", "Summer", "Fall", "Winter" };
static const uint32_t kSeasonCol[4] = { UI_RGB(0xE8789A), UI_RGB(0x3E9A3C), UI_RGB(0xD8762A), UI_RGB(0x4A84D8) };

static void clock_face(int cx, int cy, int r)
{
  for (int y = -r - 1; y <= r + 1; y++)
    for (int x = -r - 1; x <= r + 1; x++) {
      int d2 = x * x + y * y;
      if (d2 <= r * r) ui_px(cx + x, cy + y, C_CREAM);
      else if (d2 <= (r + 1) * (r + 1) + 1) ui_px(cx + x, cy + y, C_WOOD_EDGE);
    }
  for (int k = 0; k < 12; k++) {
    double a = k * M_PI / 6;
    ui_px(cx + (int)lround(sin(a) * (r - 1)), cy - (int)lround(cos(a) * (r - 1)), C_INK_SOFT);
  }
  unsigned hour = g_ram[HM_HOUR] % 12, minute = (g_ram[HM_MINUTES] % 15) * 4;
  double am = minute * M_PI / 30, ah = (hour + minute / 60.0) * M_PI / 6;
  for (int i = 1; i <= r - 2; i++)
    ui_px(cx + (int)lround(sin(am) * i), cy - (int)lround(cos(am) * i), C_INK_SOFT);
  for (int i = 1; i <= r - 5; i++)
    ui_px(cx + (int)lround(sin(ah) * i), cy - (int)lround(cos(ah) * i), C_RED);
  ui_px(cx, cy, C_WOOD_EDGE);
}

static void draw_top(void)
{
  uint8_t *r = g_ram;
  char buf[64];
  /* date and clock */
  ui_frame(4, 4, 96, 46, C_PAPER);
  int season = r[HM_SEASON] & 3;
  const char *sname = T(kSeasons[season]);
  int sw = ui_text_w(UI_SMALL, sname) + 8;
  ui_rect(9, 9, sw, 10, kSeasonCol[season]);
  ui_hline(9, 18, sw, C_WOOD_EDGE);
  ui_text_sh(UI_SMALL, 13, 16, sname, C_CREAM);
  snprintf(buf, sizeof buf, "%u", r[HM_DAY]);
  int nx = ui_text(UI_BODY, 10, 36, buf, C_INK);
  ui_text(UI_SMALL, nx + 3, 36, T(kDays[r[HM_WEEKDAY] % 7]), C_INK_SOFT);
  snprintf(buf, sizeof buf, T("Year %u"), r[HM_YEAR] + 1u);
  ui_text(UI_SMALL, 10, 45, buf, C_INK_SOFT);
  clock_face(84, 21, 10);                        /* hands... */
  snprintf(buf, sizeof buf, "%u:%02u", r[HM_HOUR], (r[HM_MINUTES] % 15) * 4);
  ui_text_c(UI_SMALL, 84, 45, buf, C_INK);               /* ...and the time in figures */

  /* hand and backpack */
  bool on_top = s.tab == 0 && s.cy < 0;
  ui_slot(104, 8, 26, C_GREEN, C_GREEN_DARK, on_top && s.cx == 0);
  ui_slot(134, 8, 26, C_BLUE, C_BLUE_DARK, on_top && s.cx == 1);
  ui_icon(109, 13, r[HM_TOOL_SELECTED]);
  ui_icon(139, 13, r[HM_TOOL_BACKPACK]);
  ui_text_c(UI_SMALL, 117, 45, T("Hand"), C_CREAM);
  ui_text_c(UI_SMALL, 147, 45, T("Bag"), C_CREAM);
  ui_text_c(UI_SMALL, 117, 44, T("Hand"), C_WOOD_EDGE);
  ui_text_c(UI_SMALL, 147, 44, T("Bag"), C_WOOD_EDGE);

  /* money and stamina */
  ui_frame(164, 4, 88, 46, C_PAPER);
  for (int y = -4; y <= 4; y++)                   /* coin */
    for (int x = -4; x <= 4; x++) {
      int d2 = x * x + y * y;
      if (d2 <= 16) ui_px(175 + x, 21 + y, d2 >= 10 ? C_GOLD_DARK : C_GOLD);
    }
  ui_px(174, 19, C_CREAM);
  snprintf(buf, sizeof buf, "%u G", (unsigned)hm_rd24(r, HM_MONEY) * 10u);
  ui_text_r(UI_BODY, 245, 26, buf, C_INK);
  ui_text(UI_SMALL, 170, 38, T("Stamina"), C_INK_SOFT);
  unsigned st = r[HM_CURRENT_STAMINA], mx = r[HM_MAX_STAMINA] ? r[HM_MAX_STAMINA] : 1;
  if (st > mx) st = mx;
  ui_rect(170, 41, 76, 5, C_WOOD_EDGE);
  ui_rect(171, 42, 74, 3, C_PAPER_DARK);
  ui_rect(171, 42, (int)(74 * st / mx), 3, st * 4 < mx ? C_RED : C_GREEN);
}

static const char *kTabs[3] = { "Tools", "Hearts", "Farm" };

static void draw_tabs(void)
{
  int x = 8;
  for (int t = 0; t < 3; t++) {
    const char *name = T(kTabs[t]);
    int w = ui_text_w(UI_SMALL, name) + 12;
    bool on = t == s.tab;
    int y = on ? 53 : 55;
    ui_rect(x + 1, y, w - 2, 70 - y, C_WOOD_EDGE);
    ui_rect(x, y + 1, w, 69 - y, C_WOOD_EDGE);
    ui_rect(x + 1, y + 1, w - 2, 69 - y, on ? C_PAPER : C_WOOD);
    ui_hline(x + 2, y + 1, w - 4, on ? C_CREAM : C_WOOD_LIGHT);
    if (on) ui_text(UI_SMALL, x + 6, y + 9, name, C_INK);
    else ui_text_sh(UI_SMALL, x + 6, y + 9, name, C_CREAM);
    x += w + 2;
  }
  ui_text_sh(UI_SMALL, 250 - ui_text_w(UI_SMALL, T("L / R  tabs")), 63, T("L / R  tabs"), C_CREAM);
}

static void draw_tools(int x, int y)
{
  /* the shed, every shed item in a fixed place; missing ones are empty */
  ui_text(UI_SMALL, x, y + 7, T("IN THE TOOL SHED"), C_INK_SOFT);
  for (int i = 0; i < NTOOLS; i++) {
    int gx = i % GRID_COLS, gy = i / GRID_COLS;
    int sx = x + gx * 21, sy = y + 11 + gy * 21;
    bool sel = s.cy == gy && s.cx == gx;
    bool have = in_shed(kTools[i].id);
    ui_rect(sx, sy, 20, 20, sel ? C_GOLD_DARK : C_PAPER_LINE);
    ui_rect(sx + 1, sy + 1, 18, 18, sel ? C_SELECT : (have ? C_PAPER : C_PAPER_DARK));
    if (have) {
      ui_icon(sx + 2, sy + 2, kTools[i].id);
      unsigned n = item_count(kTools[i].id);
      if (n) { char c[8]; snprintf(c, sizeof c, "%u", n); ui_text_r(UI_SMALL, sx + 20, sy + 19, c, C_INK); }
    }
  }
  /* the cursor: a little hand pointing at the slot */
  int px0, py0;
  if (s.cy < 0) { px0 = 104 + s.cx * 30 - 9; py0 = 18; }
  else { px0 = x + s.cx * 21 - 7; py0 = y + 11 + s.cy * 21 + 6; }
  static const char *hand[7] = { "..##....", ".#cc####", "#cccccc#", "#ccc####", "#cccc#..", ".####...", "........" };
  for (int j = 0; j < 7; j++)
    for (int i = 0; i < 8; i++)
      if (hand[j][i] == '#') ui_px(px0 + i, py0 + j, C_WOOD_EDGE);
      else if (hand[j][i] == 'c') ui_px(px0 + i, py0 + j, C_CREAM);

  /* right column: what is under the cursor */
  int rx = x + GRID_COLS * 21 + 6;
  uint8_t id = cursor_item();
  ui_rect(rx - 4, y + 2, 1, 76, C_PAPER_LINE);
  if (s.cy < 0) ui_text(UI_SMALL, rx, y + 9, s.cx == 0 ? T("In hand:") : T("In backpack:"), C_INK_SOFT);
  if (id) {
    ui_icon(rx, y + 13, id);
    const char *name = tool_name(id);
    /* wrap a two-word name over two lines when it is too wide */
    char a[48]; snprintf(a, sizeof a, "%s", name);
    char *sp = strchr(a, ' ');
    if (sp && ui_text_w(UI_SMALL, a) > 58) { *sp = 0; ui_text(UI_SMALL, rx, y + 38, a, C_INK); ui_text(UI_SMALL, rx, y + 46, sp + 1, C_INK); }
    else ui_text(UI_SMALL, rx, y + 38, a, C_INK);
    unsigned n = item_count(id);
    if (n) { char c[16]; snprintf(c, sizeof c, "×%u", n); ui_text(UI_SMALL, rx + 20, y + 25, c, C_INK_SOFT); }
  } else {
    ui_text(UI_SMALL, rx, y + 38, T("(nothing)"), C_INK_SOFT);
  }
}

static uint32_t heart_colour(unsigned v)
{
  static const uint32_t tier[8] = { UI_RGB(0x5A4A5A), UI_RGB(0x8A5AC8), UI_RGB(0x4A78E0), UI_RGB(0x46A846),
                                    UI_RGB(0xE0C028), UI_RGB(0xEC8A2A), UI_RGB(0xF27AA8), UI_RGB(0xE0303A) };
  static const unsigned lim[8] = { 100, 200, 350, 450, 550, 650, 800, 1000 };
  for (int i = 0; i < 8; i++) if (v < lim[i]) return tier[i];
  return tier[7];
}

static void draw_hearts(int x, int y)
{
  static const char *names[5] = { "Maria", "Ann", "Nina", "Ellen", "Eve" };
  load_faces();
  for (int i = 0; i < 5; i++) {
    int ry = y + i * 19;
    unsigned v = hm_rd16(g_ram, HM_HEARTS_BASE + i * 2);
    if (v > 999) v = 999;
    /* portrait frame */
    ui_rect(x, ry, 18, 18, C_WOOD_EDGE);
    ui_rect(x + 1, ry + 1, 16, 16, C_PAPER_DARK);
    if (s_faces_ok)
      for (int j = 0; j < 16; j++)
        for (int k = 0; k < 16; k++) ui_px(x + 1 + k, ry + 1 + j, s_face[i][j * 16 + k]);
    ui_text(UI_BODY, x + 24, ry + 13, T(names[i]), C_INK);
    unsigned full = (v + 50) / 100;
    uint32_t col = heart_colour(v);
    for (int h = 0; h < 10; h++) ui_heart(x + 76 + h * 9, ry + 6, col, (unsigned)h < full);
    char num[8]; snprintf(num, sizeof num, "%u", v);
    ui_text_r(UI_SMALL, x + 236, ry + 12, num, C_INK_SOFT);
    if (i < 4) ui_hline(x + 22, ry + 18, 214, C_PAPER_LINE);
  }
}

static void draw_farm(int x, int y)
{
  static const char *weather[12] = { "Sunny", "Rain", "Snow", "Hurricane", "Fair", "Storm",
    "Flower Festival", "Harvest Festival", "Thanksgiving",
    "Star Night Festival", "New Year", "Egg Festival" };
  uint8_t *r = g_ram;
  char v[64];
  int row = 0, col = 0;
#define ITEM(label, ...) do { snprintf(v, sizeof v, __VA_ARGS__); \
    int ix = x + col * 120, iy = y + row * 19; \
    ui_text(UI_SMALL, ix, iy + 8, T(label), C_INK_SOFT); ui_text_r(UI_BODY, ix + 114, iy + 16, v, C_INK); \
    ui_hline(ix, iy + 18, 114, C_PAPER_LINE); \
    if (++row == 5) { row = 0; col++; } } while (0)
  ITEM("Time", "%u:%02u", r[HM_HOUR], (r[HM_MINUTES] % 15) * 4);
  ITEM("Shipped today", "%u G", (unsigned)hm_rd24(r, HM_SHIPPING_MONEY) * 10u);
  ITEM("Lumber", "%u", hm_rd16(r, HM_STORED_WOOD));
  ITEM("Fodder", "%u", hm_rd16(r, HM_STORED_GRASS));
  ITEM("Cows", "%u", r[HM_COW_N]);
  ITEM("Happiness", "%u / 999", hm_rd16(r, HM_HAPPINESS));
  ITEM("Dog hugs", "%u", hm_rd16(r, HM_DOG_HUGS));
  ITEM("Tomorrow's weather", "%s", r[HM_WEATHER_TOMORROW] < 12 ? T(weather[r[HM_WEATHER_TOMORROW]]) : "?");
  ITEM("Stamina", "%u / %u", r[HM_CURRENT_STAMINA], r[HM_MAX_STAMINA]);
#undef ITEM
}

static void draw_textbox(void)
{
  ui_frame(4, 176, 248, 44, C_PAPER);
  int x = 12;
  if (s.tab == 0) {
    if (s.message[0]) ui_text(UI_BODY, x, 193, s.message, C_GREEN_DARK);
    else if (!player_free()) ui_text(UI_BODY, x, 193, T("(only while you can move)"), C_RED);
    else ui_text(UI_BODY, x, 193, T("A  equip"), C_INK);
    char help[128];
    snprintf(help, sizeof help, "%s    %s %s", T("X  hand ↔ backpack"), T("Y  put away in"), T("the tool shed"));
    ui_text(UI_SMALL, x, 205, help, C_INK_SOFT);
  } else if (s.tab == 1) {
    ui_text(UI_SMALL, x, 192, T("Marks: 200 heart event · 450 first child ·"), C_INK);
    ui_text(UI_SMALL, x, 201, T("650 second child · maximum 999"), C_INK);
  } else {
    char d[96];
    snprintf(d, sizeof d, T("%s, %s %u, Year %u"), T(kDays[g_ram[HM_WEEKDAY] % 7]),
             T(kSeasons[g_ram[HM_SEASON] & 3]), g_ram[HM_DAY], g_ram[HM_YEAR] + 1u);
    ui_text(UI_BODY, x, 196, d, C_INK);
  }
  ui_text(UI_SMALL, x, 214, T("B / M / Esc  close     ·     the game is paused"), C_INK_SOFT);
  /* the blinking-arrow corner of a game text box, standing still */
  for (int j = 0; j < 3; j++) ui_hline(241 + j, 210 + j, 5 - 2 * j, C_WOOD_DARK);
}

static uint32_t s_canvas[LW * LH];

static void render(void)
{
  ui_target(s_canvas, LW, LH);
  ui_checker();
  draw_top();
  draw_tabs();
  ui_frame(4, 68, 248, 106, C_PAPER);
  if (s.tab == 0) draw_tools(12, 72);
  else if (s.tab == 1) draw_hearts(10, 73);
  else draw_farm(12, 73);
  draw_textbox();
}

/* ── Input ───────────────────────────────────────────────────────────── */
static void move(int dx, int dy)
{
  if (s.tab != 0) { if (dx) s.tab = (s.tab + 3 + dx) % 3; return; }
  s.message[0] = 0;
  if (dy) {
    s.cy += dy;
    if (s.cy < -1) s.cy = GRID_ROWS - 1;
    if (s.cy >= GRID_ROWS) s.cy = -1;
    if (s.cy < 0) s.cx = s.cx >= GRID_COLS / 2 ? 1 : 0;
    else if (dy > 0 && s.cy == 0) s.cx = s.cx ? 5 : 1;
  }
  if (dx) {
    int cols = s.cy < 0 ? 2 : GRID_COLS;
    s.cx = (s.cx + cols + dx) % cols;
  }
}
static void tab(int dir) { s.tab = (s.tab + 3 + dir) % 3; s.message[0] = 0; }

void pause_action(char a)
{
  switch (a) {
    case 'u': move(0, -1); break;
    case 'd': move(0, +1); break;
    case 'l': move(-1, 0); break;
    case 'r': move(+1, 0); break;
    case 'L': tab(-1); break;
    case 'R': tab(+1); break;
    case 'A': action_a(); break;
    case 'X': action_x(); break;
    case 'Y': action_y(); break;
    case 'B': s.open = false; break;
  }
}

bool pause_can_open(void) { return hm_diary_loaded(g_ram); }
void pause_open(void)
{
  s.open = true;
  s.message[0] = 0;
  if (s.cy >= GRID_ROWS || s.cy < -1) s.cy = 0;
}
bool pause_is_open(void) { return s.open; }

/* The menu into dst (dst_w x 224): 256 wide in the middle, the floor
 * carrying on into a widescreen picture's margins. */
void pause_draw(uint32_t *dst, int dst_w)
{
  render();
  int ox = (dst_w - LW) / 2;
  for (int y = 0; y < LH; y++)
    for (int x = 0; x < dst_w; x++) {
      int lx = x - ox;
      dst[y * dst_w + x] = (lx >= 0 && lx < LW) ? s_canvas[y * LW + lx]
                         : ((((lx >> 4) ^ (y >> 4)) & 1) ? C_CHECK_B : C_CHECK_A);
    }
}
