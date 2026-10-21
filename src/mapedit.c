/*
 * mapedit.c — changing tiles of the current map (a tool hits the ground, a
 * crop grows, a fence breaks...).
 *
 * A map exists three times: the tile types the game logic reads
 * ($09B6, GetTileContent), the full BG1 tilemap in WRAM ($7E:2000, what
 * the screen shows, see MEMORIA.md) and the 64x64 window of it in VRAM.
 * Changing a tile updates the first two here; CODE_81A688 then queues DMA
 * transfers for the VRAM (that part writes DMA registers and is not
 * rewritten yet).
 *
 * The "tile changes" table $81:B363 has 6 bytes per entry:
 *   +0 word  offset of the graphics in $A6:A096
 *   +2 byte  rows (of 16x16 tiles)      +3 byte  columns
 *   +4 word  the tile type to write in $09B6 (0 = leave it)
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

#define TILE_CHANGES      0x81B363
#define TILE_GRAPHICS     0xA6A096   /* ROM, bank $A6 */
#define HM_MAP_TILES      0x009B6    /* DB: */
#define HM_MAP_WIDTH      0x00181    /* DB: 1, 2 or other (= 4) screens */

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
 * SUB_81A801 ($81:A801, JSL, RTL). A = entry of the tile-changes table;
 * $7E = graphics offset, $82 = rows, $80 = columns, $84 = tile type.
 * Leftovers: A = $84, X = 6 A + 4, C from the 6 A sum.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_SUB_81A801(uint8_t *ram, HmRegs *r)
{
  uint16_t n = r->a;
  uint32_t s = (uint32_t)(uint16_t)(n << 2) + n;      /* ASL; ASL; CLC; ADC */
  s = (s & 0xFFFF) + n + (s > 0xFFFF);                 /* ADC (carry chained) */
  r->c = s > 0xFFFF;
  uint16_t x = (uint16_t)s;
  hm_dp_w16(ram, r, 0x7E, hm_bus_rd16(ram, TILE_CHANGES + x));
  hm_dp_w16(ram, r, 0x82, hm_bus_rd8(ram, TILE_CHANGES + (uint16_t)(x + 2)));
  hm_dp_w16(ram, r, 0x80, hm_bus_rd8(ram, TILE_CHANGES + (uint16_t)(x + 3)));
  r->a = hm_bus_rd16(ram, TILE_CHANGES + (uint16_t)(x + 4));
  hm_dp_w16(ram, r, 0x84, r->a);
  r->x = (uint16_t)(x + 4);
}
