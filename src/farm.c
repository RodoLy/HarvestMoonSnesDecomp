/*
 * farm.c — the farm field: tiles and the damage bad weather does to them.
 *
 * The field is a 64 x 64 grid of tiles (HM_FARM_MAP, one byte each, row by
 * row). A tile is 16 x 16 pixels. Tile codes seen here:
 *   02 plain soil          05 fence (wood)      06 broken fence
 *   07, 08 tilled soil     1D grass, just sown  1E..6F crops (all stages)
 *   70..79 grass growing (79 = fully grown)     A0 and up: not field
 *
 * At night (NightReset) ClimateFarmDamagePrep looks at today's weather flags
 * and, for rain, snow, hurricane and the winter storm, rolls a die for every
 * tile: broken fences, grass and crops turned back into plain soil. The new
 * year (end of winter) does the same with its own chances.
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

#define HM_FARM_MAP        0x0A4E6   /* $7E:A4E6, 64 x 64 bytes */
#define HM_MAP_WIDTH_CODE  0x00181   /* 8b: 1 = 16 tiles wide, 2 = 32, else 64 */
#define HM_GROWN_GRASS_N   0x0092E   /* 16b: fully grown grass tiles */
#define HM_PLANTED_GRASS   0x11F29   /* 16b: $7F1F29 */
#define WEATHER_FLAGS      0x00196   /* 16b, SetWeatherFlags (weather.c) */

enum {
  TILE_SOIL          = 0x02,
  TILE_FENCE         = 0x05,
  TILE_BROKEN_FENCE  = 0x06,
  TILE_TILLED_A      = 0x07,
  TILE_TILLED_B      = 0x08,
  TILE_GRASS_SOWN    = 0x1D,
  TILE_CROPS_FIRST   = 0x1E,
  TILE_GRASS_FIRST   = 0x70,
  TILE_GRASS_GROWN   = 0x79,
  TILE_NOT_FIELD     = 0xA0,
};

static void push16(uint8_t *ram, HmRegs *r, uint16_t v)
{
  hm_push8(ram, r, (uint8_t)(v >> 8));
  hm_push8(ram, r, (uint8_t)v);
}
static uint16_t pull16(uint8_t *ram, HmRegs *r)
{
  uint8_t lo = hm_pull8(ram, r);
  return (uint16_t)(lo | hm_pull8(ram, r) << 8);
}
/* JSR: like JSL but only the 16-bit address is pushed (same bank). */
static void jsr(uint8_t *ram, HmRegs *r, uint16_t return_pc) { push16(ram, r, return_pc); }
static void rts(uint8_t *ram, HmRegs *r) { pull16(ram, r); }

/* ─────────────────────────────────────────────────────────────────────────
 * GetTileIndex ($82:B13C, JSR, RTS). X, Y = a position in pixels; returns
 * the tile index (column + row * width) in A and X. The width comes from
 * $0181: 1 -> 16 tiles, 2 -> 32, anything else -> 64 (the farm uses 4).
 * Leftovers: $7E = column, $80 = row offset, C = carry of the final add.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_GetTileIndex(uint8_t *ram, HmRegs *r)
{
  uint16_t column = (uint16_t)(r->x >> 4);
  uint16_t row = (uint16_t)(r->y & 0xFFF0);      /* row * 16 */
  hm_dp_w16(ram, r, 0x7E, column);
  hm_dp_w16(ram, r, 0x80, row);
  uint8_t width = ram[HM_MAP_WIDTH_CODE];
  if (width != 1) {
    row = (uint16_t)(row << 1);                  /* row * 32 */
    hm_dp_w16(ram, r, 0x80, row);
    if (width != 2) {
      row = (uint16_t)(row << 1);                /* row * 64 */
      hm_dp_w16(ram, r, 0x80, row);
    }
  }
  uint32_t index = (uint32_t)column + row;
  r->c = index > 0xFFFF;
  r->a = r->x = (uint16_t)index;
}

/* ─────────────────────────────────────────────────────────────────────────
 * LoadsDamageProbabilities ($82:8270, JSR, RTS). A = weather row; returns
 * "1 chance in n" for each kind of damage, 0 = never:
 *   A = fences, X = grass, Y = crops / tilled soil.
 * (Each row has a fourth byte, always 0, that nothing reads.) The values go
 * through the stack (PHA, PHA, PLX, PLA), so they stay below S.
 * C = bit 14 of the row (the two ASLs); 0 for every row the game uses.
 * ───────────────────────────────────────────────────────────────────────── */
static const uint8_t kDamageChance[5][4] = {   /* $82:8298 */
  /* fence grass crops */
  { 0x60, 0x00, 0x00, 0 },   /* 0 rain: fences 1 in 96 */
  { 0x40, 0x00, 0x00, 0 },   /* 1 snow: fences 1 in 64 */
  { 0x08, 0x10, 0x04, 0 },   /* 2 hurricane: 1/8, 1/16, 1/4 */
  { 0x08, 0x00, 0x04, 0 },   /* 3 first-year winter storm */
  { 0x00, 0x40, 0x20, 0 },   /* 4 new year: grass 1/64, crops 1/32 */
};

void hm_LoadsDamageProbabilities(uint8_t *ram, HmRegs *r)
{
  unsigned row = r->a;                           /* the game passes 0..4 */
  r->c = (row >> 14) & 1;
  const uint8_t *chance = kDamageChance[row % 5];
  push16(ram, r, chance[0]);
  push16(ram, r, chance[1]);
  r->y = chance[2];
  r->x = pull16(ram, r);
  r->a = pull16(ram, r);
}

/* ─────────────────────────────────────────────────────────────────────────
 * ClimateFarmDamageCheck ($82:A713, JSL, RTL). A, X, Y = the three chances
 * (fences, grass, crops; see above). Visits all 64 x 64 tiles; for each one
 * of the right kind rolls RNGReturn0toA(chance) and on a 0:
 *   fence             -> broken fence
 *   grass (sown or growing) -> plain soil, and one less planted grass
 *                        ($7F1F29, not below 0; sown grass doesn't count)
 *   tilled soil, crops -> plain soil
 * A chance of 0 means "never" and skips the roll.
 *
 * Quirk: fully grown grass is meant to lower the grown-grass counter $092E,
 * but the test compares 16 bits whose high byte is the high byte of the
 * tile index. It only matches in the first 256 tiles (the top 4 rows).
 *
 * Ends with A = X = Y = $0400 and C = 1. Every tile leaves its pushes on the
 * stack (PHY, PHX, the JSR, and PHX + JSL when it rolls).
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_GET_TILE_INDEX  0xA732   /* JSR GetTileIndex at $82:A730 */
#define RET_ROLL_FENCE      0xA76B   /* JSL RNGReturn0toA at $82:A768 */
#define RET_ROLL_GRASS      0xA78E
#define RET_ROLL_SOWN_GRASS 0xA7C2
#define RET_ROLL_CROPS      0xA7DE

/* PHX; JSL RNGReturn0toA; PLX. True when it rolled 0. */
static bool roll_zero(uint8_t *ram, HmRegs *r, uint16_t chance, uint16_t ret)
{
  uint16_t index = r->x;
  r->a = chance;
  push16(ram, r, index);
  hm_jsl(ram, r, ret, 0x80);
  hm_RNGReturn0toA(ram, r);
  hm_rtl(ram, r);
  r->x = pull16(ram, r);
  return (uint8_t)r->a == 0;
}

void hm_ClimateFarmDamageCheck(uint8_t *ram, HmRegs *r)
{
  uint16_t fence_chance = r->a, grass_chance = r->x, crop_chance = r->y;
  hm_dp_w16(ram, r, 0x86, fence_chance);
  hm_dp_w16(ram, r, 0x88, grass_chance);
  hm_dp_w16(ram, r, 0x8A, crop_chance);
  ram[HM_MAP_WIDTH_CODE] = 4;                    /* 64 tiles wide */

  for (uint16_t y = 0; y != 0x400; y = (uint16_t)(y + 16)) {
    for (uint16_t x = 0; x != 0x400; x = (uint16_t)(x + 16)) {
      push16(ram, r, y);
      push16(ram, r, x);
      hm_dp_w16(ram, r, 0x82, x);
      hm_dp_w16(ram, r, 0x84, y);
      r->x = x; r->y = y;
      jsr(ram, r, RET_GET_TILE_INDEX);
      hm_GetTileIndex(ram, r);
      rts(ram, r);
      uint16_t index = r->x;
      uint8_t *tile = &ram[HM_FARM_MAP + index];
      uint8_t t = *tile;
      /* What the 16-bit compare sees: the tile with the index's high byte. */
      uint16_t a16 = (uint16_t)((index & 0xFF00) | t);

      if (t >= TILE_NOT_FIELD) {
        /* not part of the field */
      } else if (t == TILE_FENCE) {
        if (fence_chance && roll_zero(ram, r, fence_chance, RET_ROLL_FENCE))
          *tile = TILE_BROKEN_FENCE;
      } else if (t == TILE_GRASS_SOWN) {
        if (grass_chance && roll_zero(ram, r, grass_chance, RET_ROLL_SOWN_GRASS))
          *tile = TILE_SOIL;
      } else if (t >= TILE_GRASS_FIRST) {
        if (a16 == TILE_GRASS_GROWN)
          hm_wr16(ram, HM_GROWN_GRASS_N, (uint16_t)(hm_rd16(ram, HM_GROWN_GRASS_N) - 1));
        if (grass_chance && roll_zero(ram, r, grass_chance, RET_ROLL_GRASS)) {
          *tile = TILE_SOIL;
          uint16_t planted = hm_rd16(ram, HM_PLANTED_GRASS);
          if (planted) hm_wr16(ram, HM_PLANTED_GRASS, (uint16_t)(planted - 1));
        }
      } else if (t == TILE_TILLED_A || t == TILE_TILLED_B || t >= TILE_CROPS_FIRST) {
        if (crop_chance && roll_zero(ram, r, crop_chance, RET_ROLL_CROPS))
          *tile = TILE_SOIL;
      }
      pull16(ram, r);                            /* PLX; PLY */
      pull16(ram, r);
    }
  }
  r->a = r->x = r->y = 0x0400;
  r->c = 1;                                      /* CPY #$0400 matched */
}

/* ─────────────────────────────────────────────────────────────────────────
 * ClimateFarmDamagePrep ($82:8209, JSL, RTL). Chooses the damage row from
 * today's weather flags ($0196), first match wins:
 *   rain -> row 0, snow -> row 1, hurricane -> row 2,
 *   first-year summer storm (0100) -> no damage,
 *   first-year winter storm (0200) -> row 3.
 * Then LoadsDamageProbabilities + ClimateFarmDamageCheck.
 * With no damage, A = the last flag test (0 or $0100) and X, Y, C are
 * untouched.
 * ───────────────────────────────────────────────────────────────────────── */
static const struct { uint16_t flag; int8_t row; uint16_t ret_jsr, ret_jsl; } kDamageByWeather[] = {
  { 0x0002, 0, 0x823B, 0x823F },   /* rain */
  { 0x0008, 1, 0x8249, 0x824D },   /* snow */
  { 0x0010, 2, 0x8257, 0x825B },   /* hurricane */
  { 0x0100, -1, 0, 0 },            /* summer storm: nothing */
  { 0x0200, 3, 0x8267, 0x826B },   /* winter storm */
};

void hm_ClimateFarmDamagePrep(uint8_t *ram, HmRegs *r)
{
  uint16_t flags = hm_rd16(ram, WEATHER_FLAGS);
  for (unsigned i = 0; i < sizeof kDamageByWeather / sizeof kDamageByWeather[0]; i++) {
    r->a = flags & kDamageByWeather[i].flag;
    if (!r->a)
      continue;
    if (kDamageByWeather[i].row < 0)
      return;
    r->a = (uint16_t)kDamageByWeather[i].row;
    jsr(ram, r, kDamageByWeather[i].ret_jsr);
    hm_LoadsDamageProbabilities(ram, r);
    rts(ram, r);
    hm_jsl(ram, r, kDamageByWeather[i].ret_jsl, 0x82);
    hm_ClimateFarmDamageCheck(ram, r);
    hm_rtl(ram, r);
    return;
  }
}
