/*
 * tools.c — does a tool work on the tile in front of the player?
 *
 * Every map has a tile property table, pointed at by the direct-page long
 * pointer $0D-$0F: 4 bytes per tile type (the byte in the map's tile array
 * $09B6). For tile type t:
 *   +1..+2  what the tile does (16 bits; the low byte's bits say which tools
 *           work: 01 sickle, 02 hoe, 04 hammer, 08 axe, 10 water, 20 seeds)
 *   +2 bit 7  "no default": see UNK_CheckTileProperty
 *   +3      which sides (01..08, by the player's direction) and seasons
 *           (10..80) the first property applies to
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

#define HM_MAP_TILES          0x009B6   /* the current map's tile types (DB:) */
#define HM_PLAYER_DIRECTION   0xDA      /* direct page, 16b */
#define DIRECTION_FLAG_TABLE  0x82ACFE  /* ROM: dw 01, 02, 04, 08 */
#define SEASON_FLAG_TABLE     0x82AD06  /* ROM: dw 10, 20, 40, 80 */
#define PROPERTY_FLAGS_5C     0x11F5C   /* $7F1F5C bit 3: no tile checks */

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

/* ─────────────────────────────────────────────────────────────────────────
 * GetTileContent ($82:B124, JSL, RTL). X, Y = a position in pixels; returns
 * the tile type there: A = (index high byte : type), X = the tile index.
 * Leftovers as GetTileIndex ($7E, $80, C).
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_CONTENT_INDEX 0xB128   /* JSR GetTileIndex at $82:B126 */

void hm_GetTileContent(uint8_t *ram, HmRegs *r)
{
  hm_push8(ram, r, (uint8_t)(RET_CONTENT_INDEX >> 8));
  hm_push8(ram, r, (uint8_t)RET_CONTENT_INDEX);
  hm_GetTileIndex(ram, r);
  hm_pull8(ram, r); hm_pull8(ram, r);
  uint32_t at = (((uint32_t)r->db << 16) + HM_MAP_TILES + r->x) & 0xFFFFFF;
  r->a = (uint16_t)((r->a & 0xFF00) | hm_bus_rd8(ram, at));
}

/* ─────────────────────────────────────────────────────────────────────────
 * UNK_CheckTileProperty ($82:AC61, JSL, RTL). X, Y = position, A = which
 * property word (always 1: bytes +1..+2). Returns A = the tile's property,
 * X = the tile type:
 *  - if the tile's side/season byte (+3) matches the player's direction and
 *    the season: the property;
 *  - otherwise, unless +2 has bit 7 set, the same property but X = 0 (the
 *    callers then refuse);
 *  - otherwise A = X = 0. Also A = X = 0 while $7F1F5C bit 3 is set.
 * Leftovers: $82 = A in, $84 = the last flag tested, Y = the table offset.
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_PROPERTY_CONTENT 0xAC76   /* JSL GetTileContent at $82:AC73 */

void hm_UNK_CheckTileProperty(uint8_t *ram, HmRegs *r)
{
  uint16_t which = r->a;
  hm_dp_w16(ram, r, 0x82, which);
  if (hm_rd16(ram, PROPERTY_FLAGS_5C) & 0x0008) {
    r->a = 0; r->x = 0;
    return;
  }
  hm_jsl(ram, r, RET_PROPERTY_CONTENT, 0x82);
  hm_GetTileContent(ram, r);
  hm_rtl(ram, r);
  uint16_t type = (uint8_t)r->a;
  r->x = type;
  push16(ram, r, type);                          /* PHX; PHA */
  push16(ram, r, type);
  r->y = (uint16_t)(type * 4 + 3);               /* the side/season byte */
  uint8_t sides = hm_bus_rd8(ram, hm_dp_long(ram, r, 0x0D, r->y));

  uint16_t dir = hm_dp_r16(ram, r, HM_PLAYER_DIRECTION);
  r->c = dir >> 15;                              /* ASL */
  uint16_t flag = hm_bus_rd16(ram, DIRECTION_FLAG_TABLE + (uint16_t)(dir << 1));
  hm_dp_w16(ram, r, 0x84, flag);
  bool applies = sides & flag;
  if (applies) {
    r->c = 0;
    flag = hm_bus_rd16(ram, SEASON_FLAG_TABLE + 2u * ram[HM_SEASON]);
    hm_dp_w16(ram, r, 0x84, flag);
    applies = sides & flag;
  }
  if (!applies) {
    r->y = (uint16_t)(r->y - 1);
    if (hm_bus_rd8(ram, hm_dp_long(ram, r, 0x0D, r->y)) & 0x80) {
      pull16(ram, r); pull16(ram, r);            /* PLA; PLX */
      r->a = 0; r->x = 0;
      return;
    }
  }
  pull16(ram, r);
  r->x = pull16(ram, r);
  uint32_t sum = 4u * type + which;              /* ASL; ASL; ADC $82 */
  r->c = sum > 0xFFFF;
  r->y = (uint16_t)sum;
  r->a = hm_bus_rd16(ram, hm_dp_long(ram, r, 0x0D, r->y));
  if (!applies) r->x = 0;
}
