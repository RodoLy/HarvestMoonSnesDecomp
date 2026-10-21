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

/* ─────────────────────────────────────────────────────────────────────────
 * SUB_81A83A ($81:A83A, JSL, RTL). A = tile change, X/Y = position in
 * pixels. Copies its graphics (rows x columns of 16x16 tiles, each 2x2
 * tilemap entries) from $A6:A096 into the BG1 tilemap at $7E:2000. The
 * tilemap is 32 entries wide per screen and the map is $0181 screens wide
 * (1, 2 or 4); crossing to the next 32-entry screen skips $40 bytes.
 * Leftovers: $72-$7A pointers, $86 start, $88 row stride, $8E/$90 the
 * screen-crossing counters; A = the final $7E, X = $80, Y = rows, C = 1.
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_83A_CHANGES 0xA88A   /* JSL SUB_81A801 at $81:A887 */

void hm_SUB_81A83A(uint8_t *ram, HmRegs *r)
{
  uint16_t change = r->a;
  push16(ram, r, change);                                /* PHA */
  hm_dp_w16(ram, r, 0x7E, (uint16_t)((r->x >> 4) << 2));
  uint16_t row = (uint16_t)((r->y >> 4) << 7), stride = 0x40;
  hm_dp_w16(ram, r, 0x80, row);
  hm_dp_w16(ram, r, 0x88, stride);
  uint8_t width = hm_bus_rd8(ram, ((uint32_t)r->db << 16) + HM_MAP_WIDTH);
  if (width != 1) {
    row = (uint16_t)(row << 1); stride = (uint16_t)(stride << 1);
    hm_dp_w16(ram, r, 0x80, row); hm_dp_w16(ram, r, 0x88, stride);
    if (width != 2) {
      row = (uint16_t)(row << 1); stride = (uint16_t)(stride << 1);
      hm_dp_w16(ram, r, 0x80, row); hm_dp_w16(ram, r, 0x88, stride);
    }
  }
  hm_dp_w16(ram, r, 0x86, (uint16_t)(hm_dp_r16(ram, r, 0x7E) + row));
  r->a = pull16(ram, r);                                 /* PLA */
  hm_jsl(ram, r, RET_83A_CHANGES, 0x81);
  hm_SUB_81A801(ram, r);
  hm_rtl(ram, r);

  uint16_t cols4 = (uint16_t)(hm_dp_r16(ram, r, 0x80) << 2);
  hm_dp_w16(ram, r, 0x80, cols4);
  uint16_t src = (uint16_t)(0xA096 + hm_dp_r16(ram, r, 0x7E));
  hm_dp_w16(ram, r, 0x72, src); hm_dp_w16(ram, r, 0x78, src);
  *hm_dp(ram, r, 0x74) = 0xA6; *hm_dp(ram, r, 0x7A) = 0xA6;
  hm_dp_w16(ram, r, 0x75, (uint16_t)(0x2000 + hm_dp_r16(ram, r, 0x86)));
  *hm_dp(ram, r, 0x77) = 0x7E;
  hm_dp_w16(ram, r, 0x8E, 0);
  hm_dp_w16(ram, r, 0x90, 0);

  uint16_t rows = hm_dp_r16(ram, r, 0x82);
  uint16_t y = 0;
  do {
    push16(ram, r, y);                                   /* PHY */
    for (uint16_t pass = 0; pass != 2; pass++) {         /* the two halves */
      push16(ram, r, pass);                              /* PHA */
      uint16_t sy = 0, dx = 0;
      hm_dp_w16(ram, r, 0x90, 0);
      do {
        uint16_t v = hm_bus_rd16(ram, hm_dp_long(ram, r, 0x72, sy));
        push16(ram, r, sy);                              /* PHY; TXY */
        hm_bus_wr16(ram, hm_dp_long(ram, r, 0x75, dx), v);
        sy = (uint16_t)(pull16(ram, r) + 2);             /* PLY; INY; INY */
        dx = (uint16_t)(dx + 2);
        pull16(ram, r); push16(ram, r, pass);            /* PLA; PHA */
        uint16_t crossed = (uint16_t)(hm_dp_r16(ram, r, 0x90) + 2);
        hm_dp_w16(ram, r, 0x90, crossed);
        if ((uint16_t)(hm_dp_r16(ram, r, 0x7E) + crossed) & 0x0040) {
          hm_dp_w16(ram, r, 0x8E, 0x40);                 /* next screen */
          hm_dp_w16(ram, r, 0x90, (uint16_t)(crossed + 0x40));
          sy = (uint16_t)(sy + 0x40);
        }
      } while (dx != hm_dp_r16(ram, r, 0x80));
      hm_dp_w16(ram, r, 0x72, (uint16_t)(hm_dp_r16(ram, r, 0x72) + 0x40));
      hm_dp_w16(ram, r, 0x75, (uint16_t)(hm_dp_r16(ram, r, 0x75) + hm_dp_r16(ram, r, 0x88)));
      pull16(ram, r);                                    /* PLA */
      r->x = dx;
    }
    uint32_t next = (uint32_t)hm_dp_r16(ram, r, 0x78) + hm_dp_r16(ram, r, 0x80);
    next = (next & 0xFFFF) + hm_dp_r16(ram, r, 0x8E) + (next > 0xFFFF);
    hm_dp_w16(ram, r, 0x78, (uint16_t)next);
    hm_dp_w16(ram, r, 0x72, (uint16_t)next);
    hm_dp_w16(ram, r, 0x8E, 0);
    r->a = (uint16_t)(hm_dp_r16(ram, r, 0x7E) + hm_dp_r16(ram, r, 0x90));
    hm_dp_w16(ram, r, 0x7E, r->a);
    y = (uint16_t)(pull16(ram, r) + 1);                  /* PLY; INY */
  } while (y != rows);
  r->y = y;
  r->c = 1;
}

/* ─────────────────────────────────────────────────────────────────────────
 * SUB_82B060 ($82:B060, JSL, RTL). A (8-bit) = a tile type; writes it into
 * the map's tile types ($09B6) for a block of $86 columns x $88 rows of
 * tiles whose top-left corner is X, Y (pixels). The per-tile offsets are
 * kept 8-bit in $8E/$90 but read back as 16-bit words, so the bytes $8F
 * and $91 left by whoever ran before count too (they are 0 in practice).
 * Leftovers: $99 = type, $82/$84 = X/Y; A = (index high byte : type),
 * X = $86, Y = $88, C = 1.
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_B060_INDEX 0xB090   /* JSR GetTileIndex at $82:B08E */

void hm_SUB_82B060(uint8_t *ram, HmRegs *r)
{
  uint8_t type = (uint8_t)r->a;
  *hm_dp(ram, r, 0x99) = type;
  hm_dp_w16(ram, r, 0x82, r->x);
  hm_dp_w16(ram, r, 0x84, r->y);
  uint16_t row = 0;
  do {
    push16(ram, r, row);                                 /* PHY */
    *hm_dp(ram, r, 0x8E) = (uint8_t)(row << 4);
    uint16_t col = 0;
    do {
      push16(ram, r, col);                               /* PHX */
      *hm_dp(ram, r, 0x90) = (uint8_t)(col << 4);
      r->x = (uint16_t)(hm_dp_r16(ram, r, 0x82) + hm_dp_r16(ram, r, 0x8E));
      r->y = (uint16_t)(hm_dp_r16(ram, r, 0x84) + hm_dp_r16(ram, r, 0x90));
      hm_push8(ram, r, (uint8_t)(RET_B060_INDEX >> 8));
      hm_push8(ram, r, (uint8_t)RET_B060_INDEX);
      hm_GetTileIndex(ram, r);
      hm_pull8(ram, r); hm_pull8(ram, r);
      r->a = (uint16_t)((r->a & 0xFF00) | type);
      hm_bus_wr8(ram, (((uint32_t)r->db << 16) + HM_MAP_TILES + r->x) & 0xFFFFFF, type);
      col = (uint16_t)(pull16(ram, r) + 1);              /* PLX; INX */
    } while (col != hm_dp_r16(ram, r, 0x86));
    row = (uint16_t)(pull16(ram, r) + 1);                /* PLY; INY */
  } while (row != hm_dp_r16(ram, r, 0x88));
  r->x = hm_dp_r16(ram, r, 0x86);
  r->y = row;
  r->c = 1;
}
