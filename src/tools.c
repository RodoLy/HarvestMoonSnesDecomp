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

/* ─────────────────────────────────────────────────────────────────────────
 * CheckToolSuccess ($82:AA71, JSL, RTL). X, Y = the tile in front. Returns
 * A = 1 if the tool in hand works there, 0 if not, 2 if the watering can
 * gets refilled (water tiles F0/F9-FD on the farm, maps 0-3, and on maps
 * $3D and up; F0/F4 on the mountain maps $10-$13; no watering at all on
 * maps 04-0F and 14-3C).
 * Paint only works on the farm (maps 0-3), on tile C3 or E2-E4.
 * Quirk: any other item (or none) answers 1, "works".
 * Tiles A0 and up, and 0, never take a tool. C = 1 except where the last
 * comparison leaves it clear (see the code).
 * ───────────────────────────────────────────────────────────────────────── */
static bool property(uint8_t *ram, HmRegs *r, uint16_t ret)
{
  r->a = 1;
  hm_jsl(ram, r, ret, 0x82);
  hm_UNK_CheckTileProperty(ram, r);
  hm_rtl(ram, r);
  return true;
}

/* CPX #$A0; BCS fail; CPX #0; BEQ fail; AND #bit; BEQ fail */
static void tool_works_if(uint8_t *ram, HmRegs *r, uint16_t ret, uint8_t bit)
{
  property(ram, r, ret);
  bool ok = r->x < 0xA0 && r->x != 0 && ((uint8_t)r->a & bit);
  r->c = 1;
  r->a = ok;
}

void hm_CheckToolSuccess(uint8_t *ram, HmRegs *r)
{
  uint8_t tool = hm_bus_rd8(ram, ((uint32_t)r->db << 16) + HM_TOOL_SELECTED);
  switch (tool) {
  case 0x01: case 0x11: tool_works_if(ram, r, 0xAAED, 0x01); return;   /* sickle */
  case 0x02: case 0x12: tool_works_if(ram, r, 0xAB12, 0x02); return;   /* hoe */
  case 0x03: case 0x13: tool_works_if(ram, r, 0xAB37, 0x04); return;   /* hammer */
  case 0x04: case 0x14: tool_works_if(ram, r, 0xAB5C, 0x08); return;   /* axe */
  case 0x15:            tool_works_if(ram, r, 0xABF3, 0x10); return;   /* sprinkler */
  case 0x0C: case 0x05: case 0x06: case 0x07: case 0x08:
                        tool_works_if(ram, r, 0xAC18, 0x20); return;   /* seeds */
  case 0x10: {                                                          /* watering can */
    uint8_t map = ram[HM_MAP_ID];
    if (map >= 0x04 && map < 0x10) { r->a = 0; r->c = 0; return; }
    if (map >= 0x14 && map < 0x3D) { r->a = 0; r->c = 1; return; }
    if (map >= 0x10 && map < 0x14) {                                    /* mountain */
      property(ram, r, 0xAB95);
      if (r->x == 0xF0 || r->x == 0xF4) { r->a = 2; r->c = 1; return; }
      r->a = 0;
      r->c = r->x >= 0xF4;
      return;
    }
    property(ram, r, 0xABAA);
    uint16_t t = r->x;
    if (t == 0xF0 || (t >= 0xF9 && t <= 0xFD)) { r->a = 2; r->c = 1; return; }
    r->c = 1;
    r->a = t < 0xA0 && t != 0 && ((uint8_t)r->a & 0x10);
    return;
  }
  case 0x0D: {                                                          /* paint */
    if (ram[HM_MAP_ID] >= 4) { r->a = 0; r->c = 1; return; }
    property(ram, r, 0xAC45);
    uint16_t t = r->x;
    if (t == 0xC3) { r->a = 1; r->c = 1; return; }
    r->c = t >= 0xE5;                            /* from CPX #$E2 / #$E5 */
    r->a = t >= 0xE2 && t < 0xE5;
    return;
  }
  default:
    r->a = 1;                                    /* quirk: "works" */
    r->c = tool >= 0x0D;
    return;
  }
}

/* ─────────────────────────────────────────────────────────────────────────
 * The tile a tool acts on.
 *
 * CalculateTileinFront ($81:D14E, JSL, RTL). A = how many tiles ahead,
 * X / Y = extra pixels sideways / ahead. $0985/$0987 = the player's
 * position ($D6/$D8) moved by 16 A + the extra in the direction the player
 * faces ($DA: 0 down = +Y, 1 up = -Y, 2 = +X, anything else = -X).
 * Leftovers: $7E = 16 A, $80 = X, $82 = Y; A = the coordinate written
 * last, C from its add / subtract (carry chained through both terms).
 *
 * TileAtOffsetFromPlayer ($81:D1A4, JSL, RTL; CODE_81D1A4 in HM-Decomp).
 * X, Y = an offset in tiles; $0985/$0987 = player position + 16 X / 16 Y,
 * whatever the direction. Leftovers: $7E = 16 X, $80 = 16 Y, A = the new Y,
 * C from its add.
 * ───────────────────────────────────────────────────────────────────────── */
#define HM_PLAYER_X       0xD6   /* direct page, 16b, pixels */
#define HM_TILE_IN_FRONT_X 0x00985 /* 16b: where the tool lands */
#define HM_TILE_IN_FRONT_Y 0x00987
#define HM_PLAYER_Y       0xD8

/* CLC; ADC b; ADC c — or SEC; SBC b; SBC c — with the carry chained. */
static uint16_t add2(uint16_t a, uint16_t b, uint16_t c, uint8_t *carry)
{
  uint32_t s = (uint32_t)a + b;
  s = (s & 0xFFFF) + c + (s > 0xFFFF);
  *carry = s > 0xFFFF;
  return (uint16_t)s;
}
static uint16_t sub2(uint16_t a, uint16_t b, uint16_t c, uint8_t *carry)
{
  uint32_t d = (uint32_t)a - b;                   /* borrow = bit 16 */
  uint8_t borrow = (d >> 16) & 1;
  d = (d & 0xFFFF) - c - borrow;
  *carry = !((d >> 16) & 1);
  return (uint16_t)d;
}

void hm_CalculateTileinFront(uint8_t *ram, HmRegs *r)
{
  uint16_t ahead = (uint16_t)(r->a << 4);
  hm_dp_w16(ram, r, 0x7E, ahead);
  hm_dp_w16(ram, r, 0x80, r->x);
  hm_dp_w16(ram, r, 0x82, r->y);
  uint16_t px = hm_dp_r16(ram, r, HM_PLAYER_X), py = hm_dp_r16(ram, r, HM_PLAYER_Y);
  hm_wr16(ram, HM_TILE_IN_FRONT_X, px);
  hm_wr16(ram, HM_TILE_IN_FRONT_Y, py);
  switch (hm_dp_r16(ram, r, HM_PLAYER_DIRECTION)) {
  case 0:  r->a = add2(py, ahead, r->y, &r->c); hm_wr16(ram, HM_TILE_IN_FRONT_Y, r->a); break;
  case 1:  r->a = sub2(py, ahead, r->y, &r->c); hm_wr16(ram, HM_TILE_IN_FRONT_Y, r->a); break;
  case 2:  r->a = add2(px, ahead, r->x, &r->c); hm_wr16(ram, HM_TILE_IN_FRONT_X, r->a); break;
  default: r->a = sub2(px, ahead, r->x, &r->c); hm_wr16(ram, HM_TILE_IN_FRONT_X, r->a); break;
  }
}

void hm_TileAtOffsetFromPlayer(uint8_t *ram, HmRegs *r)
{
  uint16_t dx = (uint16_t)(r->x << 4), dy = (uint16_t)(r->y << 4);
  hm_dp_w16(ram, r, 0x7E, dx);
  hm_dp_w16(ram, r, 0x80, dy);
  uint32_t x = (uint32_t)hm_dp_r16(ram, r, HM_PLAYER_X) + dx;
  hm_wr16(ram, HM_TILE_IN_FRONT_X, (uint16_t)x);
  uint32_t y = (uint32_t)hm_dp_r16(ram, r, HM_PLAYER_Y) + dy;
  hm_wr16(ram, HM_TILE_IN_FRONT_Y, (uint16_t)y);
  r->a = (uint16_t)y;
  r->c = y > 0xFFFF;
}
