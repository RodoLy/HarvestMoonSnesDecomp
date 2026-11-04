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
 * read from your ROM when the menu opens (kFaces); the Goddess is a sixth.
 *
 * Pages (L / R):
 *   Tools   the hand and backpack at the top, the tool shed as a grid.
 *           Equip, swap and put away follow the game's own shed routine
 *           TakeToolFromShelf and the tool table at $81:BE0F (hm_ram.h).
 *   Hearts  the five girls' affection ($7F1F1F.., 0..999).
 *   Farm    money, shipping, stock, cows, happiness, tomorrow's weather.
 * With mouse controls on, the mouse works here too (pause_mouse).
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
#include "hm_vars.h"
#include "native.h"
#include "ui.h"
#include "i18n.h"
#include "pause.h"
#include "goddess.h"
#include "config.h"
#include "mods.h"

enum { GRID_COLS = 8, GRID_ROWS = 3, LW = 256, LH = 224 };

static struct {
  bool open;
  int tab;               /* 0 tools, 1 hearts, 2 farm, 3 the crop bag (when on) */
  int an, ar, ac;        /* animals: the section (0 cows, 1 chickens), its row and column */
  int cx, cy;            /* tools cursor: cy -1 = the top slots (cx 0 hand, 1 bag) */
  char message[96];      /* the tools page's feedback line */
  bool live;             /* drawn for a second screen while the game goes on */
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
  if (id == 0x18 && goddess_golden_feather()) return T("Golden Feather");   /* the Goddess's (goddess.c) */
  for (int i = 0; i < NTOOLS; i++) if (kTools[i].id == id) return T(kTools[i].name);
  return "?";
}
/* a seed bag's bags (0: not a seed bag) */
static unsigned bag_count_of(uint8_t id)
{
  int k = id >= 0x05 && id <= 0x08 ? id - 0x04 : id == 0x0C ? 0 : -1;
  return k < 0 ? 0 : g_ram[HM_SEEDS_GRASS_N + k];
}
/* a little dark label with a number, its bottom-right corner at (rx, by) */
static void count_badge(int rx, int by, unsigned n)
{
  char c[12]; snprintf(c, sizeof c, "%u", n);
  int w = ui_text_w(UI_SMALL, c) + 4;
  ui_rect(rx - w, by - 8, w, 8, C_WOOD_EDGE);
  ui_text(UI_SMALL, rx - w + 2, by - 1, c, C_CREAM);
}
/* a seed bag's count: bags; with "one seed at a time", the seeds left */
static unsigned item_count(uint8_t id)
{
  int k = id >= 0x05 && id <= 0x08 ? id - 0x04 : id == 0x0C ? 0 : -1;   /* grass, corn .. turnip */
  if (k < 0) return 0;
  unsigned bags = g_ram[HM_SEEDS_GRASS_N + k];
  if (g_cfg.single_seed && bags) return bags * 9u - g_hm_seeds_used[k];
  return bags;
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
  return shed_slot(id, &row, &mask) && (g_ram[HM_SHED_ITEMS + row] & mask);
}
static void set_in_shed(uint8_t id, bool on)
{
  int row; uint8_t mask;
  if (!shed_slot(id, &row, &mask)) return;
  if (on) g_ram[HM_SHED_ITEMS + row] |= mask; else g_ram[HM_SHED_ITEMS + row] &= (uint8_t)~mask;
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

/* a tool's picture: the game's own sprite of it on the shed's shelf (the
 * shelf items $2A-$43 are the tools 1-$1A; their case 3, or 0), else the
 * drawn icon */
static void tool_icon(int x, int y, uint8_t id)
{
  uint32_t px[256];
  if (id >= 1 && id <= 0x1A && (mods_item_sprite((uint8_t)(0x29 + id), 3, px) || mods_item_sprite((uint8_t)(0x29 + id), 0, px))) {
    if (id == 0x18 && goddess_golden_feather())      /* the Golden Feather: its blues made gold */
      for (int k = 0; k < 256; k++) {
        uint32_t c = px[k];
        int r = c >> 16 & 0xFF, g = c >> 8 & 0xFF, b = c & 0xFF;
        if (!c || b <= r || b <= g) continue;
        int l = (r + g + b * 2) / 4;                 /* its lightness, 0-255 */
        px[k] = UI_RGB((uint32_t)(l + 96 > 255 ? 255 : l + 96) << 16 | (uint32_t)(l + 64 > 255 ? 255 : l + 64) << 8 | (uint32_t)(l / 3));
      }
    for (int j = 0; j < 16; j++)
      for (int i = 0; i < 16; i++) if (px[j * 16 + i]) ui_px(x + i, y + j, px[j * 16 + i]);
    return;
  }
  ui_icon(x, y, id);
}

static void say(const char *m) { snprintf(s.message, sizeof s.message, "%s", m); }

/* the inventory's rows: the shed's 3, and the crop bag's when it is on */
enum { BAG_ROW = GRID_ROWS };
static int inv_rows(void) { return GRID_ROWS + (g_cfg.crop_bag ? 1 : 0); }
static bool on_bag(void) { return s.tab == 0 && s.cy == BAG_ROW; }

/* The item under the cursor (0 = none). */
static uint8_t cursor_item(void)
{
  if (s.cy == BAG_ROW) return 0;
  if (s.cy < 0) return g_ram[s.cx == 0 ? HM_TOOL_SELECTED : HM_TOOL_BACKPACK];
  int i = s.cy * GRID_COLS + s.cx;
  return i < NTOOLS && in_shed(kTools[i].id) ? kTools[i].id : 0;
}

/* Equip a shed item -- TakeToolFromShelf: the hand tool drops into an empty
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

static void bag_take_out(void);
static void action_a(void)
{
  if (s.tab != 0) return;
  if (on_bag()) { bag_take_out(); return; }
  if (!player_free()) { say(T("You can't change tools right now.")); return; }
  if (s.cy < 0) { if (g_ram[HM_TOOL_BACKPACK] || g_ram[HM_TOOL_SELECTED]) swap_hand_bag(); return; }
  uint8_t id = cursor_item();
  if (id) equip_from_shed(id);
}
static void action_x(void)
{
  if (s.tab != 0 || on_bag()) return;
  if (!player_free()) { say(T("You can't change tools right now.")); return; }
  swap_hand_bag();
}
static void action_y(void)   /* put the hand (or the selected bag) item away */
{
  if (s.tab != 0 || on_bag()) return;
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

/* ── The faces: the game's own sprites, read from your ROM ───────────── */
/* Front-facing frame of each one in the NPC sprite sheets ($8F:8000 on,
 * file 0x78000: 4bpp tiles, 16 per 0x200-byte row; a frame is 2 tiles
 * wide, its top two rows are the head) and the sprite palette it is drawn
 * with ($A9:DE20.., the game's OBJ palette table). The girls were found by
 * watching the flower festival: talking to each girl raised exactly one
 * heart counter, and the tiles she was drawn with matched these ROM bytes.
 * The Goddess is her praying frame (the scene at her pond), a few rows
 * below the townsfolk, with the table's last palette: green hair, white
 * dress. Hearts order: Maria, Ann, Nina, Ellen, Eve, then the Goddess. */
enum { FACES = 6, FACE_GODDESS = 5 };
static const struct { uint32_t frame, palette; } kFaces[FACES] = {
  { 0x078200, 0x14DE20 },   /* Maria: blue hair */
  { 0x078B00, 0x14DE20 },   /* Ann: orange ponytail */
  { 0x07B2C0, 0x14DE40 },   /* Nina: pink hair */
  { 0x07A280, 0x14DE80 },   /* Ellen: brown hair */
  { 0x07A3C0, 0x14DE40 },   /* Eve: blonde */
  { 0x088C00, 0x14E000 },   /* the Goddess: green hair, white dress */
};
static uint32_t s_face[FACES][16 * 16];
static bool s_faces_ok;

static void load_faces(void)
{
  if (s_faces_ok || !g_rom) return;
  for (int g = 0; g < FACES; g++) {
    uint32_t pal[16];
    for (int i = 0; i < 16; i++) {
      uint16_t v = g_rom[kFaces[g].palette + i * 2] | g_rom[kFaces[g].palette + i * 2 + 1] << 8;
      uint32_t r = (v & 31) << 3, gg = ((v >> 5) & 31) << 3, b = ((v >> 10) & 31) << 3;
      pal[i] = i ? (0xFF000000u | r << 16 | gg << 8 | b | (r >> 5) << 16 | (gg >> 5) << 8 | (b >> 5)) : 0;
    }
    for (int ty = 0; ty < 2; ty++)
      for (int tx = 0; tx < 2; tx++) {
        const uint8_t *t = g_rom + kFaces[g].frame + ty * 0x200 + tx * 0x20;
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
  tool_icon(109, 13, r[HM_TOOL_SELECTED]);
  tool_icon(139, 13, r[HM_TOOL_BACKPACK]);
  { unsigned n = item_count(r[HM_TOOL_SELECTED]); if (n) count_badge(130, 34, n); }
  { unsigned n = item_count(r[HM_TOOL_BACKPACK]); if (n) count_badge(160, 34, n); }
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

static const char *kTabs[4] = { "Inventory", "Hearts", "Farm", "Animals" };
static int ntabs(void) { return 4; }

static void draw_tabs(void)
{
  int x = 8;
  for (int t = 0; t < ntabs(); t++) {
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

/* the crop bag (mods.c): its 9 places; the one under the cursor */
static const char *bag_name(uint8_t it, char *buf, size_t n)
{
  static const char *const names[8] = { "Corn", "Tomato", "Potato", "Turnip", "Egg", "Milk (S)", "Milk (M)", "Milk (L)" };
  if (it >= 0x10 && it <= 0x17) return T(names[it - 0x10]);
  snprintf(buf, n, "%s", T("Item"));
  return buf;
}
static unsigned bag_price(uint8_t it) { return g_rom ? g_rom[0x9FDE + 2u * it + 1] * 10u : 0; }  /* $81:9FDE */
static void bag_take_out(void)
{
  uint8_t items[9];
  int n = mods_bag_items(items);
  if (s.cx >= n) { say(n ? T("(nothing)") : T("The bag is empty.")); return; }
  if (!player_free()) { say(T("You can't take things out right now.")); return; }
  if (mods_bag_choose(s.cx)) s.open = false;            /* out as the game goes on */
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
      tool_icon(sx + 2, sy + 2, kTools[i].id);
      unsigned n = item_count(kTools[i].id);
      if (n) count_badge(sx + 20, sy + 20, n);
    }
  }
  /* the crop bag, under the shed: 9 places */
  int by = y + 11 + GRID_ROWS * 21 + 1;
  uint8_t bag[9];
  int bn = g_cfg.crop_bag ? mods_bag_items(bag) : 0;
  if (g_cfg.crop_bag)
    for (int i = 0; i < 9; i++) {
      int sx = x + i * 18;
      bool sel = s.cy == BAG_ROW && s.cx == i;
      ui_rect(sx, by, 18, 18, sel ? C_GOLD_DARK : C_PAPER_LINE);
      ui_rect(sx + 1, by + 1, 16, 16, sel ? C_SELECT : (i < bn ? C_PAPER : C_PAPER_DARK));
      if (i < bn) mods_bag_icon(sx + 1, by + 1, bag[i]);
    }
  /* the cursor: a little hand pointing at the slot */
  int px0, py0;
  if (s.cy < 0) { px0 = 104 + s.cx * 30 - 9; py0 = 18; }
  else if (s.cy == BAG_ROW) { px0 = x + s.cx * 18 - 7; py0 = by + 5; }
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
  if (s.cy == BAG_ROW) {                        /* the crop bag's place */
    char c[48];
    snprintf(c, sizeof c, "%s  %d/9", T("Crop bag"), bn);
    ui_text(UI_SMALL, rx, y + 9, c, C_INK_SOFT);
    if (s.cx < bn) {
      char buf[48];
      mods_bag_icon(rx, y + 13, bag[s.cx]);
      ui_text(UI_SMALL, rx, y + 38, bag_name(bag[s.cx], buf, sizeof buf), C_INK);
      unsigned p = bag_price(bag[s.cx]);
      if (p) { snprintf(c, sizeof c, T("Sells for %u G"), p); ui_text(UI_SMALL, rx, y + 48, c, C_GREEN_DARK); }
    } else {
      ui_text(UI_SMALL, rx, y + 38, bn ? T("(nothing)") : T("The bag is empty."), C_INK_SOFT);
    }
    return;
  }
  if (id) {
    tool_icon(rx, y + 13, id);
    const char *name = tool_name(id);
    /* wrap a two-word name over two lines when it is too wide */
    char a[48]; snprintf(a, sizeof a, "%s", name);
    char *sp = strchr(a, ' ');
    if (sp && ui_text_w(UI_SMALL, a) > 58) { *sp = 0; ui_text(UI_SMALL, rx, y + 38, a, C_INK); ui_text(UI_SMALL, rx, y + 46, sp + 1, C_INK); }
    else ui_text(UI_SMALL, rx, y + 38, a, C_INK);
    unsigned bags = bag_count_of(id);
    if (bags) {                                 /* how much is left, in words */
      char c[48];
      int ly = sp && ui_text_w(UI_SMALL, name) > 58 ? y + 58 : y + 50;
      if (g_cfg.single_seed) {
        unsigned n = item_count(id);
        snprintf(c, sizeof c, n == 1 ? T("1 seed") : T("%u seeds"), n);
        ui_text(UI_SMALL, rx, ly, c, C_GREEN_DARK);
        ly += 9;
      }
      snprintf(c, sizeof c, bags == 1 ? T("1 bag") : T("%u bags"), bags);
      ui_text(UI_SMALL, rx, ly, c, g_cfg.single_seed ? C_INK_SOFT : C_GREEN_DARK);
    }
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
  int rows = g_cfg.goddess ? 6 : 5, step = rows == 6 ? 16 : 19;
  int fs = rows == 6 ? 16 : 18, ty = rows == 6 ? 12 : 13, hy = rows == 6 ? 4 : 6;   /* a frame, the text, the hearts */
  for (int i = 0; i < rows; i++) {
    int ry = y + i * step;
    unsigned v = i < 5 ? hm_rd16(g_ram, HM_HEARTS_BASE + i * 2) : g_hm_goddess_hearts;
    if (v > 999 && i < 5) v = 999;
    if (v > 1000) v = 1000;
    /* portrait frame */
    ui_rect(x, ry, fs, fs, C_WOOD_EDGE);
    ui_rect(x + 1, ry + 1, fs - 2, fs - 2, C_PAPER_DARK);
    int cut = (18 - fs) / 2;                         /* six rows: the pictures lose a pixel each side */
    if (s_faces_ok)
      for (int j = 0; j < fs - 2; j++)
        for (int k = 0; k < fs - 2; k++) ui_px(x + 1 + k, ry + 1 + j, s_face[i][(j + cut) * 16 + k + cut]);
    ui_text(UI_BODY, x + 24, ry + ty, i < FACE_GODDESS ? T(names[i]) : T("Goddess"), C_INK);
    unsigned full = (v + 50) / 100;
    uint32_t col = heart_colour(v);
    for (int h = 0; h < 10; h++) ui_heart(x + 76 + h * 9, ry + hy, col, (unsigned)h < full);
    char num[8]; snprintf(num, sizeof num, "%u", v);
    ui_text_r(UI_SMALL, x + 236, ry + ty - 1, num, C_INK_SOFT);
    if (i < rows - 1) ui_hline(x + 22, ry + step - 1, 214, C_PAPER_LINE);
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

/* ── The animals: the barn's cows and the coop's chickens ─────────────
 * Their records (animals.c): a cow 16 bytes at $7E:C1C6 (+0 status: 02
 * newborn calf, 04 calf, 08 adult, 10 cranky, 20 sick, 40 pregnant; +1
 * today: 01 brushed, 02 milked, 04 talked to; +2 the map, $27 the barn;
 * +3 days left sick; +4 happiness, the milk's size: below $60 S, below $C0
 * M, else L); a chicken 8 bytes at $7E:C286 (+0: 02 egg, 04 chick, 08 hen,
 * 10 hungry; +1 the map, $28 the coop). Pictures: the game's own sprites
 * (cow $1C7, calf $1BA, hen $212, chick $217, the egg item $23). */
enum { COWS = 12, CHICKENS = 13, ROWS_SHOWN = 5 };
static const uint8_t *cow(int i) { return &g_ram[HM_COW_DATA + 16 * i]; }
static const uint8_t *hen(int i) { return &g_ram[HM_CHICKEN_DATA + 8 * i]; }
/* the animals there are, cows first: (section 0 cow / 1 chicken, slot) */
static int animal_list(int sec[COWS + CHICKENS], int slot[COWS + CHICKENS])
{
  int n = 0;
  for (int i = 0; i < COWS; i++) if (cow(i)[0]) { sec[n] = 0; slot[n] = i; n++; }
  for (int i = 0; i < CHICKENS; i++) if (hen(i)[0]) { sec[n] = 1; slot[n] = i; n++; }
  return n;
}
static void animals_move(int dx, int dy)
{
  int sec[COWS + CHICKENS], slot[COWS + CHICKENS];
  int n = animal_list(sec, slot);
  (void)dx;
  if (!n) { s.ac = s.ar = 0; return; }
  if (dy) s.ac = (s.ac + n + dy) % n;
  if (s.ac >= n) s.ac = n - 1;
  if (s.ac < s.ar) s.ar = s.ac;
  if (s.ac >= s.ar + ROWS_SHOWN) s.ar = s.ac - ROWS_SHOWN + 1;
}
/* the animal's picture, the game's own sprite, in a w x h box at (x, y) */
static void animal_picture(int sec, int i, int x, int y, int w, int h)
{
  static uint32_t px[48 * 24];
  uint8_t st = sec == 0 ? cow(i)[0] : hen(i)[0];
  bool ok;
  if (sec == 0) ok = mods_sprite((st & 0x08) ? 0x1C7 : 0x1BA, false, px, w, h);
  else if (st & 0x02) {
    uint32_t e[256];
    ok = mods_item_sprite(0x23, 0, e);
    memset(px, 0, sizeof px);
    for (int j = 0; ok && j < 16 && j < h; j++)
      for (int k = 0; k < 16 && k + (w - 16) / 2 < w; k++) px[j * w + k + (w - 16) / 2] = e[j * 16 + k];
  } else ok = mods_sprite((st & 0x08) ? 0x212 : 0x217, false, px, w, h);
  if (!ok) return;
  for (int j = 0; j < h; j++)
    for (int k = 0; k < w; k++) if (px[j * w + k]) ui_px(x + k, y + j, px[j * w + k]);
}
static void animal_name(int sec, int i, char *buf, size_t n)
{
  snprintf(buf, n, "%s %d", sec == 0 ? T("Cow") : T("Chicken"), i + 1);
}
/* like the girls' hearts: a row each, the cows with ten hearts of
 * happiness (0-255; the chickens have none) */
static void draw_animals(int x, int y)
{
  int sec[COWS + CHICKENS], slot[COWS + CHICKENS];
  int n = animal_list(sec, slot);
  if (!n) { ui_text(UI_BODY, x + 4, y + 40, T("No animals yet."), C_INK_SOFT); return; }
  if (s.ac >= n) s.ac = n - 1;
  for (int r = 0; r < ROWS_SHOWN && s.ar + r < n; r++) {
    int k = s.ar + r, sc = sec[k], i = slot[k];
    int ry = y + 1 + r * 19;
    bool sel = k == s.ac;
    if (sel) ui_rect(x - 2, ry - 1, 240, 19, C_SELECT);
    ui_rect(x, ry, 36, 18, C_WOOD_EDGE);
    ui_rect(x + 1, ry + 1, 34, 16, C_PAPER_DARK);
    animal_picture(sc, i, x + 2, ry + 1, 32, 16);
    char name[32]; animal_name(sc, i, name, sizeof name);
    ui_text(UI_BODY, x + 42, ry + 13, name, C_INK);
    if (sc == 0) {
      unsigned v = cow(i)[4];
      unsigned full = (v * 10 + 127) / 255;
      uint32_t col = heart_colour(v * 1000 / 256);
      for (int h = 0; h < 10; h++) ui_heart(x + 112 + h * 9, ry + 6, col, (unsigned)h < full);
      char num[8]; snprintf(num, sizeof num, "%u", v);
      ui_text_r(UI_SMALL, x + 236, ry + 12, num, C_INK_SOFT);
    } else {
      uint8_t st = hen(i)[0];
      const char *kind = (st & 0x02) ? T("egg in the incubator") : (st & 0x04) ? T("chick") : T("hen");
      ui_text(UI_SMALL, x + 112, ry + 12, kind, C_INK_SOFT);
    }
    if (r < ROWS_SHOWN - 1 && k < n - 1) ui_hline(x + 40, ry + 18, 196, C_PAPER_LINE);
  }
  /* more above / below */
  if (s.ar > 0) for (int j = 0; j < 3; j++) ui_hline(x + 237 - j, y - 2 + j, 1 + 2 * j, C_WOOD_DARK);
  if (s.ar + ROWS_SHOWN < n) for (int j = 0; j < 3; j++) ui_hline(x + 235 + j, y + 96 + j, 5 - 2 * j, C_WOOD_DARK);
}
static void animal_details(int x)
{
  int sec[COWS + CHICKENS], slot[COWS + CHICKENS];
  int n = animal_list(sec, slot);
  if (!n) { ui_text(UI_BODY, x, 193, T("No animals yet."), C_INK_SOFT); return; }
  if (s.ac >= n) s.ac = n - 1;
  int i = slot[s.ac];
  char a[96], b[128];
  if (sec[s.ac] == 0) {
    const uint8_t *c = cow(i);
    if (!c[0]) { ui_text(UI_BODY, x, 193, T("(empty)"), C_INK_SOFT); return; }
    bool adult = c[0] & 0x08;
    snprintf(a, sizeof a, "%s %d · %s", T("Cow"), i + 1, adult ? T("adult") : T("calf"));
    if (c[0] & 0x20) { char t[40]; snprintf(t, sizeof t, T(" · sick (%u days)"), c[3]); strncat(a, t, sizeof a - strlen(a) - 1); }
    if (c[0] & 0x40) strncat(a, T(" · pregnant"), sizeof a - strlen(a) - 1);
    if (c[0] & 0x10) strncat(a, T(" · cranky"), sizeof a - strlen(a) - 1);
    ui_text(UI_BODY, x, 193, a, C_INK);
    const char *milk = c[4] < 0x60 ? "S" : c[4] < 0xC0 ? "M" : "L";
    snprintf(b, sizeof b, T("Happiness %u/255"), c[4]);
    if (adult && !(c[0] & 0x40)) { char t[24]; snprintf(t, sizeof t, T(" · milk %s"), milk); strncat(b, t, sizeof b - strlen(b) - 1); }
    char t[96];
    const char *yes = T("yes"), *no = T("no");
    snprintf(t, sizeof t, " · %s %s · %s %s%s%s%s%s", T("brushed"), (c[1] & 1) ? yes : no, T("talked"), (c[1] & 4) ? yes : no,
             adult ? " · " : "", adult ? T("milked") : "", adult ? " " : "", adult ? ((c[1] & 2) ? yes : no) : "");
    strncat(b, t, sizeof b - strlen(b) - 1);
    ui_text(UI_SMALL, x, 205, b, C_INK_SOFT);
  } else {
    const uint8_t *h = hen(i);
    if (!h[0]) { ui_text(UI_BODY, x, 193, T("(empty)"), C_INK_SOFT); return; }
    const char *kind = (h[0] & 0x02) ? T("egg in the incubator") : (h[0] & 0x04) ? T("chick") : T("hen");
    snprintf(a, sizeof a, "%s %d · %s", T("Chicken"), i + 1, kind);
    if (h[0] & 0x10) strncat(a, T(" · hungry"), sizeof a - strlen(a) - 1);
    ui_text(UI_BODY, x, 193, a, C_INK);
    ui_text(UI_SMALL, x, 205, h[1] == 0x28 ? T("In the coop") : T("Outside"), C_INK_SOFT);
  }
}

static void draw_textbox(void)
{
  ui_frame(4, 176, 248, 44, C_PAPER);
  int x = 12;
  if (on_bag()) {
    if (s.message[0]) ui_text(UI_BODY, x, 193, s.message, C_GREEN_DARK);
    else if (!player_free()) ui_text(UI_BODY, x, 193, T("(only while you can move)"), C_RED);
    else ui_text(UI_BODY, x, 193, s.live ? T("Tap: take it out") : T("A  take it out"), C_INK);
    ui_text(UI_SMALL, x, 205, T("Q  put what you carry in / take the last out"), C_INK_SOFT);
  } else if (s.tab == 0) {
    if (s.message[0]) ui_text(UI_BODY, x, 193, s.message, C_GREEN_DARK);
    else if (!player_free()) ui_text(UI_BODY, x, 193, T("(only while you can move)"), C_RED);
    else ui_text(UI_BODY, x, 193, s.live ? T("Tap a tool: hold it") : T("A  equip"), C_INK);
    char help[128];
    snprintf(help, sizeof help, "%s    %s %s", T("X  hand ↔ backpack"), T("Y  put away in"), T("the tool shed"));
    ui_text(UI_SMALL, x, 205, help, C_INK_SOFT);
  } else if (s.tab == 3) {
    animal_details(x);
  } else if (s.tab == 1) {
    ui_text(UI_SMALL, x, 192, T("Marks: 200 heart event · 450 first child ·"), C_INK);
    ui_text(UI_SMALL, x, 201, T("650 second child · maximum 999"), C_INK);
  } else {
    char d[96];
    snprintf(d, sizeof d, T("%s, %s %u, Year %u"), T(kDays[g_ram[HM_WEEKDAY] % 7]),
             T(kSeasons[g_ram[HM_SEASON] & 3]), g_ram[HM_DAY], g_ram[HM_YEAR] + 1u);
    ui_text(UI_BODY, x, 196, d, C_INK);
  }
  if (s.live) ui_text(UI_SMALL, x, 214, T("Tap to choose  ·  the game goes on"), C_INK_SOFT);
  else ui_text(UI_SMALL, x, 214, T("B / M / Esc  close     ·     the game is paused"), C_INK_SOFT);
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
  else if (s.tab == 3) draw_animals(10, 72);
  else draw_farm(12, 73);
  draw_textbox();
}

/* ── Input ───────────────────────────────────────────────────────────── */
static void move(int dx, int dy)
{
  if (s.tab == 3) { animals_move(dx, dy); return; }
  if (s.tab != 0) { if (dx) s.tab = (s.tab + ntabs() + dx) % ntabs(); return; }
  s.message[0] = 0;
  if (dy) {
    int rows = inv_rows();
    s.cy += dy;
    if (s.cy < -1) s.cy = rows - 1;
    if (s.cy >= rows) s.cy = -1;
    if (s.cy < 0) s.cx = s.cx >= GRID_COLS / 2 ? 1 : 0;
    else if (dy > 0 && s.cy == 0) s.cx = s.cx ? 5 : 1;
    else if (s.cy < GRID_ROWS && s.cx >= GRID_COLS) s.cx = GRID_COLS - 1;
  }
  if (dx) {
    int cols = s.cy < 0 ? 2 : s.cy == BAG_ROW ? 9 : GRID_COLS;
    s.cx = (s.cx + cols + dx) % cols;
  }
}
static void tab(int dir) { s.tab = (s.tab + ntabs() + dir) % ntabs(); s.message[0] = 0; }

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

/* ── The mouse (with mouse controls on) ───────────────────────────────
 * Pointing moves the cursor (the tabs, the hand and backpack, the shed, the
 * crop bag, the animals); a left click there is A, and on the help lines
 * does what they say (X swap, Y put away, close); the right click closes;
 * the wheel goes through the animals. (mx, my): the menu's own 256 x 224. */
static bool hit(int mx, int my, int x, int y, int w, int h) { return mx >= x && my >= y && mx < x + w && my < y + h; }
static bool text_hit(int mx, int my, int x, int base, const char *t)
{
  return hit(mx, my, x - 2, base - 9, ui_text_w(UI_SMALL, t) + 4, 12);
}
void pause_mouse(int mx, int my, bool moved, bool click, bool rclick, int wheel)
{
  if (rclick) { s.open = false; return; }
  if (wheel && s.tab == 3) animals_move(0, -wheel);
  if (mx < 0 || (!moved && !click)) return;
  for (int t = 0, x = 8; t < ntabs(); t++) {                    /* the tabs */
    int w = ui_text_w(UI_SMALL, T(kTabs[t])) + 12;
    if (hit(mx, my, x, 53, w, 17)) { if (click && s.tab != t) { s.tab = t; s.message[0] = 0; } return; }
    x += w + 2;
  }
  if (click && text_hit(mx, my, 12, 214, T("B / M / Esc  close"))) { s.open = false; return; }
  if (s.tab == 0) {
    int cx = 0, cy = -9;
    if (hit(mx, my, 104, 8, 26, 26)) { cx = 0; cy = -1; }
    else if (hit(mx, my, 134, 8, 26, 26)) { cx = 1; cy = -1; }
    for (int i = 0; i < NTOOLS && cy == -9; i++)
      if (hit(mx, my, 12 + (i % GRID_COLS) * 21, 83 + (i / GRID_COLS) * 21, 20, 20)) { cx = i % GRID_COLS; cy = i / GRID_COLS; }
    for (int i = 0; i < 9 && cy == -9 && g_cfg.crop_bag; i++)
      if (hit(mx, my, 12 + i * 18, 72 + 11 + GRID_ROWS * 21 + 1, 18, 18)) { cx = i; cy = BAG_ROW; }
    if (cy != -9) {
      if (cx != s.cx || cy != s.cy) { s.cx = cx; s.cy = cy; s.message[0] = 0; }
      if (click) action_a();
      return;
    }
    if (click && !on_bag()) {                                  /* the help line */
      const char *xs = T("X  hand ↔ backpack");
      char ys[96]; snprintf(ys, sizeof ys, "%s %s", T("Y  put away in"), T("the tool shed"));
      int x2 = 12 + ui_text_w(UI_SMALL, xs) + ui_text_w(UI_SMALL, "    ");
      if (text_hit(mx, my, 12, 205, xs)) action_x();
      else if (text_hit(mx, my, x2, 205, ys)) action_y();
    }
  } else if (s.tab == 3) {
    int sec[COWS + CHICKENS], slot[COWS + CHICKENS];
    int n = animal_list(sec, slot);
    for (int r = 0; r < ROWS_SHOWN && s.ar + r < n; r++)
      if (hit(mx, my, 8, 72 + r * 19, 242, 19)) s.ac = s.ar + r;
  }
}

/* ── Live, on a second screen (second.c) ──────────────────────────────
 * The same menu, drawn every other frame while the game goes on, and used
 * by touch: a tap is a click there (equip, swap, take out, the tabs); on
 * the Animals page a drag scrolls the list. Changes are made between two
 * frames, as the game's own code would. Before a diary is loaded: the
 * floor and a line saying so. */
void pause_live_render(uint32_t *dst)
{
  if (!hm_diary_loaded(g_ram)) {
    ui_target(dst, LW, LH);
    ui_checker();
    ui_frame(28, 92, 200, 40, C_PAPER);
    ui_text_c(UI_BODY, 128, 116, T("Load a diary to begin."), C_INK);
    return;
  }
  if (s.tab >= ntabs()) s.tab = 0;
  if (s.cy >= inv_rows() || s.cy < -1) s.cy = 0;
  s.live = true;
  render();
  s.live = false;
  memcpy(dst, s_canvas, sizeof s_canvas);
}
void pause_live_touch(int action, int x, int y)
{
  static int last_y, acc;
  if (!hm_diary_loaded(g_ram)) return;
  if (action == 0) {                                  /* down: a click there */
    last_y = y; acc = 0;
    bool keep = s.open;
    pause_mouse(x, y, true, true, false, 0);
    s.open = keep;                                    /* "close" means nothing here */
  } else if (action == 1 && s.tab == 3) {             /* a drag on the animals: scroll */
    acc += y - last_y; last_y = y;
    while (acc <= -12) { animals_move(0, +1); acc += 12; }
    while (acc >= 12) { animals_move(0, -1); acc -= 12; }
  }
}

bool pause_can_open(void) { return hm_diary_loaded(g_ram); }
void pause_open(void)
{
  s.open = true;
  s.message[0] = 0;
  if (s.tab >= ntabs()) s.tab = 0;
  if (s.cy >= inv_rows() || s.cy < -1) s.cy = 0;
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
