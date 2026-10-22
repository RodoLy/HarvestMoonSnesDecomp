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

/* ─────────────────────────────────────────────────────────────────────────
 * QueueTileGraphicsDMA ($81:A6C1, JSL, RTL; CODE_81A6C1 in HM-Decomp).
 * A = tile change, X/Y = position. Queues the DMA transfers that copy the
 * change's graphics ($A6:A096 + offset) into the VRAM tilemap: two per
 * tile row (the two halves), $80 bytes each, VRAM address mode $80.
 * The VRAM address: $6000 + 2 col + 64 row, where col / row wrap at 32
 * (the 64x64 window is four 32x32 screens) and the right / bottom screens
 * add $3E0 / $400. The first pair uses DMA channels 2 and 3, the next
 * ones 0 and 1.
 * Leftovers: $7E/$80/$82/$84 (SUB_81A801, then $80 = 4 cols, $82 = 2 rows),
 * $72/$75 source pointers, $86 base, $92 = 0; A = the last source, X =
 * rows*2, Y = 4 cols, C = 1.
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_6C1_CHANGES 0xA747   /* JSL SUB_81A801 at $81:A744 */
#define RET_6C1_DMA_A   0xA788   /* JSL AddProgrammedDMA at $81:A785 */
#define RET_6C1_DMA_B   0xA7B7   /* ... at $81:A7B4 */

/* col or row: wrap at 32 */
static uint16_t screen_part(uint16_t v) { return v >= 0x20 ? (uint16_t)(v - 0x20) : v; }
static bool second_screen(uint16_t v) { return (v >= 0x10 && v < 0x20) || v >= 0x30; }

static void queue_half(uint8_t *ram, HmRegs *r, uint8_t channel, uint16_t row_x, uint16_t ret)
{
  *hm_dp(ram, r, 0x27) = channel;
  *hm_dp(ram, r, 0x29) = 0x18;                           /* VRAM */
  r->y = hm_dp_r16(ram, r, 0x80);
  r->x = (uint16_t)((row_x << 5) + hm_dp_r16(ram, r, 0x86));
  r->a = 0x0080;
  hm_jsl(ram, r, ret, 0x80);
  hm_AddProgrammedDMA(ram, r);
  hm_rtl(ram, r);
}

void hm_QueueTileGraphicsDMA(uint8_t *ram, HmRegs *r)
{
  push16(ram, r, r->a);                                  /* PHA */
  uint16_t col = (uint16_t)(r->x >> 4), row = (uint16_t)(r->y >> 4);
  hm_dp_w16(ram, r, 0x7E, col);
  hm_dp_w16(ram, r, 0x80, row);
  uint16_t vram = (uint16_t)(0x6000 + (screen_part(col) << 1));
  if (second_screen(col)) vram = (uint16_t)(vram + 0x03E0);
  vram = (uint16_t)(vram + (screen_part(row) << 6));
  if (second_screen(row)) vram = (uint16_t)(vram + 0x0400);
  hm_dp_w16(ram, r, 0x86, vram);
  hm_dp_w16(ram, r, 0x72, 0xA096); hm_dp_w16(ram, r, 0x75, 0xA096);
  *hm_dp(ram, r, 0x74) = 0xA6; *hm_dp(ram, r, 0x77) = 0xA6;

  r->a = pull16(ram, r);                                 /* PLA */
  hm_jsl(ram, r, RET_6C1_CHANGES, 0x81);
  hm_SUB_81A801(ram, r);
  hm_rtl(ram, r);

  hm_dp_w16(ram, r, 0x80, (uint16_t)(hm_dp_r16(ram, r, 0x80) << 2));
  hm_dp_w16(ram, r, 0x82, (uint16_t)(hm_dp_r16(ram, r, 0x82) << 1));
  uint16_t src = (uint16_t)(hm_dp_r16(ram, r, 0x72) + hm_dp_r16(ram, r, 0x7E));
  hm_dp_w16(ram, r, 0x72, src); hm_dp_w16(ram, r, 0x75, src);
  *hm_dp(ram, r, 0x92) = 2;

  uint16_t x = 0;
  do {
    push16(ram, r, x);                                   /* PHX */
    queue_half(ram, r, *hm_dp(ram, r, 0x92), x, RET_6C1_DMA_A);
    hm_dp_w16(ram, r, 0x72, (uint16_t)(hm_dp_r16(ram, r, 0x72) + 0x40));
    x = (uint16_t)(pull16(ram, r) + 1);                  /* PLX; INX; PHX */
    push16(ram, r, x);
    queue_half(ram, r, (uint8_t)(*hm_dp(ram, r, 0x92) + 1), x, RET_6C1_DMA_B);
    r->a = (uint16_t)(hm_dp_r16(ram, r, 0x75) + hm_dp_r16(ram, r, 0x80));
    hm_dp_w16(ram, r, 0x75, r->a); hm_dp_w16(ram, r, 0x72, r->a);
    *hm_dp(ram, r, 0x92) = 0;
    x = (uint16_t)(pull16(ram, r) + 1);                  /* PLX; INX */
  } while (x != hm_dp_r16(ram, r, 0x82));
  r->x = x;
  r->c = 1;
}

/* ─────────────────────────────────────────────────────────────────────────
 * ChangeMapTile ($81:A688, JSL, RTL; CODE_81A688 in HM-Decomp). A = tile
 * change (0 = nothing), X/Y = position in pixels. Does all of it: queues
 * the VRAM copy, writes the BG1 tilemap in WRAM, and, if the change has a
 * tile type, writes it into $09B6 for the whole block.
 * Leftovers: $86/$88 = columns / rows; registers from SUB_82B060 when it
 * runs, otherwise A = 0, X/Y = the position and C from SUB_81A801.
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_688_QUEUE   0xA698   /* JSL QueueTileGraphicsDMA at $81:A695 */
#define RET_688_BG1     0xA6A4   /* JSL SUB_81A83A at $81:A6A1 */
#define RET_688_CHANGES 0xA6AB   /* JSL SUB_81A801 at $81:A6A8 */
#define RET_688_TYPES   0xA6BF   /* JSL SUB_82B060 at $81:A6BC */

void hm_ChangeMapTile(uint8_t *ram, HmRegs *r)
{
  if (r->a == 0) { r->c = 1; return; }                    /* CMP #0 */
  uint16_t change = r->a, x = r->x, y = r->y;
  push16(ram, r, y); push16(ram, r, x); push16(ram, r, change);
  hm_jsl(ram, r, RET_688_QUEUE, 0x81);
  hm_QueueTileGraphicsDMA(ram, r);
  hm_rtl(ram, r);
  pull16(ram, r); pull16(ram, r); pull16(ram, r);
  push16(ram, r, y); push16(ram, r, x); push16(ram, r, change);
  r->a = change; r->x = x; r->y = y;
  hm_jsl(ram, r, RET_688_BG1, 0x81);
  hm_SUB_81A83A(ram, r);
  hm_rtl(ram, r);
  r->a = pull16(ram, r);                                 /* PLA */
  hm_jsl(ram, r, RET_688_CHANGES, 0x81);
  hm_SUB_81A801(ram, r);
  hm_rtl(ram, r);
  r->x = pull16(ram, r);                                 /* PLX; PLY */
  r->y = pull16(ram, r);
  hm_dp_w16(ram, r, 0x86, hm_dp_r16(ram, r, 0x80));
  hm_dp_w16(ram, r, 0x88, hm_dp_r16(ram, r, 0x82));
  r->a = hm_dp_r16(ram, r, 0x84);
  if (r->a == 0) return;
  hm_jsl(ram, r, RET_688_TYPES, 0x82);
  hm_SUB_82B060(ram, r);
  hm_rtl(ram, r);
}

/* ─────────────────────────────────────────────────────────────────────────
 * StartMapAnimation ($81:A5E1, JSL, RTL; CODE_81A5E1 in HM-Decomp). A =
 * an entry of the table $81:B963 (3 bytes each: a long pointer to a map
 * animation script). Points $CF-$D1 at it and clears $0989 (the script's
 * wait counter); CODE_81A600 then plays it frame by frame (the magic
 * beans growing, for one).
 * Leftovers: $7E = A in; A = (high byte of the pointer word : bank),
 * X = 3 A + 2, C from the 3 A sum.
 * ───────────────────────────────────────────────────────────────────────── */
#define MAP_ANIMATIONS 0x81B963

void hm_StartMapAnimation(uint8_t *ram, HmRegs *r)
{
  uint16_t n = r->a;
  hm_dp_w16(ram, r, 0x7E, n);
  uint32_t sum = (uint32_t)(uint16_t)(n << 1) + n;       /* ASL; CLC; ADC $7E */
  r->c = sum > 0xFFFF;
  uint16_t x = (uint16_t)sum;
  uint16_t word = hm_bus_rd16(ram, (MAP_ANIMATIONS + x) & 0xFFFFFF);
  hm_dp_w16(ram, r, 0xCF, word);
  x = (uint16_t)(x + 2);
  uint8_t bank = hm_bus_rd8(ram, (MAP_ANIMATIONS + x) & 0xFFFFFF);
  *hm_dp(ram, r, 0xD1) = bank;
  r->a = (uint16_t)((word & 0xFF00) | bank);
  r->x = x;
  hm_bus_wr8(ram, ((uint32_t)r->db << 16) | 0x0989, 0);
}

/* ─────────────────────────────────────────────────────────────────────────
 * EditTileonMap ($82:B03A, JSL, RTL). A (low byte) = a tile type, X / Y =
 * a position in pixels: writes the type into the current map ($09B6 +
 * GetTileIndex). Only the logic map changes, not the picture (that is
 * ChangeMapTile's job).
 * EditTileonMapandsets0181 ($82:B049): the same on the farm's own copy
 * ($7E:A4E6), first setting the width code $0181 = 4 (64 tiles).
 * Leftovers: those of GetTileIndex, with A's low byte = the type. The PHA
 * of the type stays on the stack.
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_03A_INDEX   0xB041
#define RET_049_INDEX   0xB057
#define HM_FARM_TILES   0x7EA4E6

static uint16_t tile_index_keeping(uint8_t *ram, HmRegs *r, uint8_t type, uint16_t ret)
{
  hm_push8(ram, r, type);                                /* PHA */
  push16(ram, r, ret);                                   /* JSR GetTileIndex */
  hm_GetTileIndex(ram, r);
  pull16(ram, r);
  hm_pull8(ram, r);                                      /* PLA */
  r->a = (uint16_t)((r->a & 0xFF00) | type);
  return r->x;
}

void hm_EditTileonMap(uint8_t *ram, HmRegs *r)
{
  uint8_t type = (uint8_t)r->a;
  uint16_t x = tile_index_keeping(ram, r, type, RET_03A_INDEX);
  hm_bus_wr8(ram, (((uint32_t)r->db << 16) + HM_MAP_TILES + x) & 0xFFFFFF, type);
}

void hm_EditTileonMapandsets0181(uint8_t *ram, HmRegs *r)
{
  uint8_t type = (uint8_t)r->a;
  hm_bus_wr8(ram, ((uint32_t)r->db << 16) + HM_MAP_WIDTH, 4);
  uint16_t x = tile_index_keeping(ram, r, type, RET_049_INDEX);
  hm_bus_wr8(ram, (HM_FARM_TILES + x) & 0xFFFFFF, type);
}

/* ─────────────────────────────────────────────────────────────────────────
 * UNK_PartialMap ($82:AF00, JSL, RTL): draw the whole map (64 x 64 tiles of
 * 16 pixels) from its tile types ($09B6). For each tile type t (1-$9F):
 * the drawing is byte +0 of its entry in the property table [$0D] (0:
 * nothing to draw). The types $73-$79 join with their neighbours of the
 * same kind ("autotiling", fences and the like): the four neighbours give
 * a mask (left 8, right 2, up 4, down 1) and DATA8_82B02A[mask] - 1 is
 * added to the drawing. Each tile is drawn with SUB_81A83A.
 * ───────────────────────────────────────────────────────────────────────── */
#define JOINS        0x82B02A   /* DATA8_82B02A: 16 entries */

static uint8_t tile_at(uint8_t *ram, const HmRegs *r, uint16_t x)
{
  return hm_bus_rd8(ram, (((uint32_t)r->db << 16) + HM_MAP_TILES + x) & 0xFFFFFF);
}
static bool joins(HmRegs *r, uint8_t t)                    /* CMP #$73; CMP #$7A */
{
  r->c = t >= 0x73;
  if (!r->c) return false;
  r->c = t >= 0x7A;
  return !r->c;
}

void hm_UNK_PartialMap(uint8_t *ram, HmRegs *r)
{
  for (uint16_t y = 0; y != 0x0400; ) {
    for (uint16_t x = 0; x != 0x0400; ) {
      push16(ram, r, y);                                   /* PHY; PHX */
      push16(ram, r, x);
      hm_dp_w16(ram, r, 0x82, x);
      hm_dp_w16(ram, r, 0x84, y);
      r->x = x; r->y = y;
      push16(ram, r, 0xAF10);                              /* JSR GetTileIndex */
      hm_GetTileIndex(ram, r);
      pull16(ram, r);
      uint16_t at = r->x;
      uint8_t t = tile_at(ram, r, at);
      r->a = (uint16_t)((r->a & 0xFF00) | t);
      bool draw = false;
      uint8_t pic = 0;
      if (t != 0) {
        r->c = t >= 0xA0;
        if (!r->c) {
          bool fence = joins(r, t);
          if (fence) {
            uint16_t mask = 0;
            static const struct { int16_t d; uint16_t bit; } N[4] = {
              { -1, 8 }, { 1, 2 }, { -0x40, 4 }, { 0x40, 1 } };
            hm_dp_w16(ram, r, 0x86, 0);
            for (unsigned k = 0; k < 4; k++) {
              push16(ram, r, at);                          /* PHX ... PLX */
              uint16_t nx = (uint16_t)(at + N[k].d);
              r->c = N[k].d < 0 ? at >= (uint16_t)-N[k].d : (uint32_t)at + N[k].d > 0xFFFF;
              r->x = nx;
              uint8_t nt = tile_at(ram, r, nx);
              r->a = (uint16_t)((nx & 0xFF00) | nt);
              pull16(ram, r);
              r->x = at;
              if (joins(r, nt)) {
                mask |= N[k].bit;
                r->a = mask;
                hm_dp_w16(ram, r, 0x86, mask);
              }
            }
            push16(ram, r, at);                            /* PHX; LDA $86; TAX */
            r->x = mask;
            uint8_t join = (uint8_t)(hm_bus_rd8(ram, JOINS + mask) - 1);
            *hm_dp(ram, r, 0x94) = join;
            pull16(ram, r);
            r->x = at;
            uint16_t yy = (uint16_t)(t << 2);              /* LDA #0; XBA; LDA; ASL; ASL */
            r->c = 0;
            r->y = yy;
            uint8_t base = hm_bus_rd8(ram, hm_dp_long(ram, r, 0x0D, yy));
            uint16_t s = (uint16_t)(base + join);          /* CLC; ADC $94 (8 bits) */
            r->c = s > 0xFF;
            pic = (uint8_t)s;
            draw = true;
          } else {
            uint16_t yy = (uint16_t)(t << 2);
            r->c = 0;
            r->y = yy;
            pic = hm_bus_rd8(ram, hm_dp_long(ram, r, 0x0D, yy));
            r->a = pic;
            draw = pic != 0;
          }
        }
      }
      if (draw) {
        r->a = pic;
        r->x = hm_dp_r16(ram, r, 0x82);
        r->y = hm_dp_r16(ram, r, 0x84);
        hm_jsl(ram, r, 0xB008, 0x81);
        hm_SUB_81A83A(ram, r);
        hm_rtl(ram, r);
      }
      x = pull16(ram, r);                                  /* PLX; PLY */
      y = pull16(ram, r);
      x = (uint16_t)(x + 0x10);
      r->a = x;
      r->c = x >= 0x0400;
      r->x = x;
      r->y = y;
    }
    y = (uint16_t)(y + 0x10);
    r->a = y;
    r->c = y >= 0x0400;
    r->y = y;
  }
}
