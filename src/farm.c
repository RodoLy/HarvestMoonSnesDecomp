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
