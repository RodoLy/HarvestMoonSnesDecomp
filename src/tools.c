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
