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
#include "hm_vars.h"
#include "hm_regs.h"

/* ── Names of the memory locations used here ────────────────────────────── */
/* ROM tables */
#define TILE_CHANGES      0x81B363   /* the tile changes, 6 bytes each: */
#define CHG_GFX           0          /*   16b: the graphics' offset in TILE_GRAPHICS */
#define CHG_ROWS          2          /*   8b: rows of 16x16 tiles */
#define CHG_COLS          3          /*   8b: columns */
#define CHG_TYPE          4          /*   16b: the tile type to write (0 = leave it) */
#define TILE_GRAPHICS     0xA6A096   /* ROM, bank $A6 */
#define MAP_ANIMATIONS    0x81B963   /* 3 bytes each: a long pointer to a map animation */
#define MAP_ANIM_BANK_AT  2          /*   8b: the pointer's bank */
#define JOINS             0x82B02A   /* DATA8_82B02A: 16 entries */
/* A map animation's steps (7 bytes, at [HM_MAP_ANIM]) */
#define STEP_CHANGE       0          /* 16b: the tile change ($FFFF end, $FFFE jump) */
#define STEP_X            2          /* 16b: where, pixels */
#define STEP_Y            4          /* 16b */
#define STEP_FRAMES       6          /* 8b: frames to wait after it */
#define STEP_BYTES        7
#define JUMP_ADDR         2          /* in a $FFFE step: 16b the address to go on at, */
#define JUMP_BANK         4          /*   8b its bank */

/* WRAM */
#define BG1_TILEMAP       0x2000     /* $7E:2000: the map's whole BG1 tilemap (bank $7E) */
#define ANIMATION_WAIT    0x0989     /* DB: frames left before the map animation's next step */
#define VRAM_TILEMAP      0x6000     /* VRAM: the 64x64 window of the BG1 tilemap */

/* Direct page. $72-$99 mean different things in different routines: the names
 * below say whose use each one is. ReadTileChange's results (also read by its
 * callers): */
#define CHANGE_GFX        0x7E   /* 16b: the graphics' offset in TILE_GRAPHICS */
#define CHANGE_COLS       0x80   /* 16b: the columns (the callers then make it 4 x: the
                                  * bytes of a row of the graphics) */
#define CHANGE_ROWS       0x82   /* 16b: the rows */
#define CHANGE_TYPE       0x84   /* 16b: the tile type to write (0 = none) */
/* The graphics being copied: */
#define SOURCE_PTR        0x72   /* long $72-$74: the graphics (AddProgrammedDMA's source) */
#define SOURCE_BANK       0x74
/* DrawTileChange's own uses: */
#define DRAW_COL          0x7E   /* first the tilemap offset of the column (CHANGE_GFX
                                  * after ReadTileChange) */
#define DRAW_ROW          0x80   /* first the tilemap offset of the row (CHANGE_COLS
                                  * after ReadTileChange) */
#define DRAW_STRIDE       0x88   /* tilemap bytes from one row to the next */
#define DRAW_START        0x86   /* the tilemap offset of the top-left corner */
#define TILEMAP_PTR       0x75   /* long $75-$77: where the row goes in BG1_TILEMAP */
#define TILEMAP_PTR_BANK  0x77
#define GFX_ROW_PTR       0x78   /* long $78-$7A: the start of the graphics' row */
#define GFX_ROW_PTR_BANK  0x7A
#define SCREEN_SKIP       0x8E   /* $40 once the row crossed into the next screen */
#define CROSSED           0x90   /* bytes done in the row (+ $40 at a screen crossing) */
/* SetTileTypeBlock's own uses (DrawWholeMap: TILE_X / TILE_Y too): */
#define BLOCK_TYPE        0x99   /* 8b: the tile type */
#define TILE_X            0x82   /* 16b: the position, pixels (CHANGE_ROWS elsewhere) */
#define TILE_Y            0x84   /* 16b (CHANGE_TYPE elsewhere) */
#define BLOCK_COLS        0x86   /* 16b: the block's columns */
#define BLOCK_ROWS        0x88   /* 16b: its rows */
#define BLOCK_ROW16       0x8E   /* 8b (read as 16b): 16 x the row */
#define BLOCK_COL16       0x90   /* 8b (read as 16b): 16 x the column */
/* QueueTileGraphicsDMA's own uses: */
#define QUEUE_COL         0x7E   /* the tile's column (CHANGE_GFX after ReadTileChange) */
#define QUEUE_ROW         0x80   /* the tile's row (CHANGE_COLS after ReadTileChange) */
#define QUEUE_VRAM        0x86   /* the VRAM address of the top-left corner */
#define ROW_SOURCE        0x75   /* long $75-$77: the start of the graphics' row */
#define ROW_SOURCE_BANK   0x77
#define FIRST_CHANNEL     0x92   /* 8b: the pair's first DMA channel (2, then 0) */
#define DMA_CHANNEL       0x27   /* 8b: AddProgrammedDMA's channel */
#define DMA_B_ADDRESS     0x29   /* 8b: its destination register (B-bus, $21xx) */
/* DrawWholeMap's own uses: */
#define JOIN_MASK         0x86   /* the neighbours of the same kind (left 8, right 2,
                                  * up 4, down 1) */
#define JOIN_ADD          0x94   /* 8b: JOINS[mask] - 1, added to the drawing */
/* The map animation (StartMapAnimation, RunMapAnimation): */
#define ANIM_ENTRY        0x7E   /* StartMapAnimation: the entry asked for */
#define JUMP_PTR          0x72   /* RunMapAnimation: long $72-$74, a jump's target */
#define JUMP_PTR_BANK     0x74
/* Others: */
#define MAP_OBJ_PTR       0x07   /* long $07-$09: the map object worked on (objects.c) */

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
 * ReadTileChange ($81:A801, JSL, RTL). A = entry of the tile-changes table;
 * $7E = graphics offset, $82 = rows, $80 = columns, $84 = tile type.
 * Leftovers: A = $84, X = 6 A + 4, C from the 6 A sum.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_ReadTileChange(uint8_t *ram, HmRegs *r)
{
  uint16_t n = r->a;
  uint32_t s = (uint32_t)(uint16_t)(n << 2) + n;      /* ASL; ASL; CLC; ADC */
  s = (s & 0xFFFF) + n + (s > 0xFFFF);                 /* ADC (carry chained) */
  r->c = s > 0xFFFF;
  uint16_t x = (uint16_t)s;
  hm_dp_w16(ram, r, CHANGE_GFX, hm_bus_rd16(ram, TILE_CHANGES + x));
  hm_dp_w16(ram, r, CHANGE_ROWS, hm_bus_rd8(ram, TILE_CHANGES + (uint16_t)(x + CHG_ROWS)));
  hm_dp_w16(ram, r, CHANGE_COLS, hm_bus_rd8(ram, TILE_CHANGES + (uint16_t)(x + CHG_COLS)));
  r->a = hm_bus_rd16(ram, TILE_CHANGES + (uint16_t)(x + CHG_TYPE));
  hm_dp_w16(ram, r, CHANGE_TYPE, r->a);
  r->x = (uint16_t)(x + CHG_TYPE);
}

/* ─────────────────────────────────────────────────────────────────────────
 * DrawTileChange ($81:A83A, JSL, RTL). A = tile change, X/Y = position in
 * pixels. Copies its graphics (rows x columns of 16x16 tiles, each 2x2
 * tilemap entries) from $A6:A096 into the BG1 tilemap at $7E:2000. The
 * tilemap is 32 entries wide per screen and the map is $0181 screens wide
 * (1, 2 or 4); crossing to the next 32-entry screen skips $40 bytes.
 * Leftovers: $72-$7A pointers, $86 start, $88 row stride, $8E/$90 the
 * screen-crossing counters; A = the final $7E, X = $80, Y = rows, C = 1.
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_83A_CHANGES 0xA88A   /* JSL ReadTileChange at $81:A887 */

void hm_DrawTileChange(uint8_t *ram, HmRegs *r)
{
  uint16_t change = r->a;
  push16(ram, r, change);                                /* PHA */
  hm_dp_w16(ram, r, DRAW_COL, (uint16_t)((r->x >> 4) << 2));
  uint16_t row = (uint16_t)((r->y >> 4) << 7), stride = 0x40;
  hm_dp_w16(ram, r, DRAW_ROW, row);
  hm_dp_w16(ram, r, DRAW_STRIDE, stride);
  uint8_t width = hm_bus_rd8(ram, ((uint32_t)r->db << 16) + HM_MAP_WIDTH);
  if (width != 1) {
    row = (uint16_t)(row << 1); stride = (uint16_t)(stride << 1);
    hm_dp_w16(ram, r, DRAW_ROW, row); hm_dp_w16(ram, r, DRAW_STRIDE, stride);
    if (width != 2) {
      row = (uint16_t)(row << 1); stride = (uint16_t)(stride << 1);
      hm_dp_w16(ram, r, DRAW_ROW, row); hm_dp_w16(ram, r, DRAW_STRIDE, stride);
    }
  }
  hm_dp_w16(ram, r, DRAW_START, (uint16_t)(hm_dp_r16(ram, r, DRAW_COL) + row));
  r->a = pull16(ram, r);                                 /* PLA */
  hm_jsl(ram, r, RET_83A_CHANGES, 0x81);
  hm_ReadTileChange(ram, r);
  hm_rtl(ram, r);

  uint16_t cols4 = (uint16_t)(hm_dp_r16(ram, r, CHANGE_COLS) << 2);
  hm_dp_w16(ram, r, CHANGE_COLS, cols4);
  uint16_t src = (uint16_t)((uint16_t)TILE_GRAPHICS + hm_dp_r16(ram, r, CHANGE_GFX));
  hm_dp_w16(ram, r, SOURCE_PTR, src); hm_dp_w16(ram, r, GFX_ROW_PTR, src);
  *hm_dp(ram, r, SOURCE_BANK) = 0xA6; *hm_dp(ram, r, GFX_ROW_PTR_BANK) = 0xA6;
  hm_dp_w16(ram, r, TILEMAP_PTR, (uint16_t)(BG1_TILEMAP + hm_dp_r16(ram, r, DRAW_START)));
  *hm_dp(ram, r, TILEMAP_PTR_BANK) = 0x7E;
  hm_dp_w16(ram, r, SCREEN_SKIP, 0);
  hm_dp_w16(ram, r, CROSSED, 0);

  uint16_t rows = hm_dp_r16(ram, r, CHANGE_ROWS);
  uint16_t y = 0;
  do {
    push16(ram, r, y);                                   /* PHY */
    for (uint16_t pass = 0; pass != 2; pass++) {         /* the two halves */
      push16(ram, r, pass);                              /* PHA */
      uint16_t sy = 0, dx = 0;
      hm_dp_w16(ram, r, CROSSED, 0);
      do {
        uint16_t v = hm_bus_rd16(ram, hm_dp_long(ram, r, SOURCE_PTR, sy));
        push16(ram, r, sy);                              /* PHY; TXY */
        hm_bus_wr16(ram, hm_dp_long(ram, r, TILEMAP_PTR, dx), v);
        sy = (uint16_t)(pull16(ram, r) + 2);             /* PLY; INY; INY */
        dx = (uint16_t)(dx + 2);
        pull16(ram, r); push16(ram, r, pass);            /* PLA; PHA */
        uint16_t crossed = (uint16_t)(hm_dp_r16(ram, r, CROSSED) + 2);
        hm_dp_w16(ram, r, CROSSED, crossed);
        if ((uint16_t)(hm_dp_r16(ram, r, CHANGE_GFX) + crossed) & 0x0040) {
          hm_dp_w16(ram, r, SCREEN_SKIP, 0x40);          /* next screen */
          hm_dp_w16(ram, r, CROSSED, (uint16_t)(crossed + 0x40));
          sy = (uint16_t)(sy + 0x40);
        }
      } while (dx != hm_dp_r16(ram, r, CHANGE_COLS));
      hm_dp_w16(ram, r, SOURCE_PTR, (uint16_t)(hm_dp_r16(ram, r, SOURCE_PTR) + 0x40));
      hm_dp_w16(ram, r, TILEMAP_PTR, (uint16_t)(hm_dp_r16(ram, r, TILEMAP_PTR) + hm_dp_r16(ram, r, DRAW_STRIDE)));
      pull16(ram, r);                                    /* PLA */
      r->x = dx;
    }
    uint32_t next = (uint32_t)hm_dp_r16(ram, r, GFX_ROW_PTR) + hm_dp_r16(ram, r, CHANGE_COLS);
    next = (next & 0xFFFF) + hm_dp_r16(ram, r, SCREEN_SKIP) + (next > 0xFFFF);
    hm_dp_w16(ram, r, GFX_ROW_PTR, (uint16_t)next);
    hm_dp_w16(ram, r, SOURCE_PTR, (uint16_t)next);
    hm_dp_w16(ram, r, SCREEN_SKIP, 0);
    r->a = (uint16_t)(hm_dp_r16(ram, r, CHANGE_GFX) + hm_dp_r16(ram, r, CROSSED));
    hm_dp_w16(ram, r, CHANGE_GFX, r->a);
    y = (uint16_t)(pull16(ram, r) + 1);                  /* PLY; INY */
  } while (y != rows);
  r->y = y;
  r->c = 1;
}

/* ─────────────────────────────────────────────────────────────────────────
 * SetTileTypeBlock ($82:B060, JSL, RTL). A (8-bit) = a tile type; writes it into
 * the map's tile types ($09B6) for a block of $86 columns x $88 rows of
 * tiles whose top-left corner is X, Y (pixels). The per-tile offsets are
 * kept 8-bit in $8E/$90 but read back as 16-bit words, so the bytes $8F
 * and $91 left by whoever ran before count too (they are 0 in practice).
 * Leftovers: $99 = type, $82/$84 = X/Y; A = (index high byte : type),
 * X = $86, Y = $88, C = 1.
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_B060_INDEX 0xB090   /* JSR GetTileIndex at $82:B08E */

void hm_SetTileTypeBlock(uint8_t *ram, HmRegs *r)
{
  uint8_t type = (uint8_t)r->a;
  *hm_dp(ram, r, BLOCK_TYPE) = type;
  hm_dp_w16(ram, r, TILE_X, r->x);
  hm_dp_w16(ram, r, TILE_Y, r->y);
  uint16_t row = 0;
  do {
    push16(ram, r, row);                                 /* PHY */
    *hm_dp(ram, r, BLOCK_ROW16) = (uint8_t)(row << 4);
    uint16_t col = 0;
    do {
      push16(ram, r, col);                               /* PHX */
      *hm_dp(ram, r, BLOCK_COL16) = (uint8_t)(col << 4);
      r->x = (uint16_t)(hm_dp_r16(ram, r, TILE_X) + hm_dp_r16(ram, r, BLOCK_ROW16));
      r->y = (uint16_t)(hm_dp_r16(ram, r, TILE_Y) + hm_dp_r16(ram, r, BLOCK_COL16));
      hm_push8(ram, r, (uint8_t)(RET_B060_INDEX >> 8));
      hm_push8(ram, r, (uint8_t)RET_B060_INDEX);
      hm_GetTileIndex(ram, r);
      hm_pull8(ram, r); hm_pull8(ram, r);
      r->a = (uint16_t)((r->a & 0xFF00) | type);
      hm_bus_wr8(ram, (((uint32_t)r->db << 16) + HM_MAP_TILES + r->x) & 0xFFFFFF, type);
      col = (uint16_t)(pull16(ram, r) + 1);              /* PLX; INX */
    } while (col != hm_dp_r16(ram, r, BLOCK_COLS));
    row = (uint16_t)(pull16(ram, r) + 1);                /* PLY; INY */
  } while (row != hm_dp_r16(ram, r, BLOCK_ROWS));
  r->x = hm_dp_r16(ram, r, BLOCK_COLS);
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
 * Leftovers: $7E/$80/$82/$84 (ReadTileChange, then $80 = 4 cols, $82 = 2 rows),
 * $72/$75 source pointers, $86 base, $92 = 0; A = the last source, X =
 * rows*2, Y = 4 cols, C = 1.
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_6C1_CHANGES 0xA747   /* JSL ReadTileChange at $81:A744 */
#define RET_6C1_DMA_A   0xA788   /* JSL AddProgrammedDMA at $81:A785 */
#define RET_6C1_DMA_B   0xA7B7   /* ... at $81:A7B4 */

/* col or row: wrap at 32 */
static uint16_t screen_part(uint16_t v) { return v >= 0x20 ? (uint16_t)(v - 0x20) : v; }
static bool second_screen(uint16_t v) { return (v >= 0x10 && v < 0x20) || v >= 0x30; }

static void queue_half(uint8_t *ram, HmRegs *r, uint8_t channel, uint16_t row_x, uint16_t ret)
{
  *hm_dp(ram, r, DMA_CHANNEL) = channel;
  *hm_dp(ram, r, DMA_B_ADDRESS) = BBAD_VRAM;             /* VRAM */
  r->y = hm_dp_r16(ram, r, CHANGE_COLS);
  r->x = (uint16_t)((row_x << 5) + hm_dp_r16(ram, r, QUEUE_VRAM));
  r->a = 0x0080;
  hm_jsl(ram, r, ret, 0x80);
  hm_AddProgrammedDMA(ram, r);
  hm_rtl(ram, r);
}

void hm_QueueTileGraphicsDMA(uint8_t *ram, HmRegs *r)
{
  push16(ram, r, r->a);                                  /* PHA */
  uint16_t col = (uint16_t)(r->x >> 4), row = (uint16_t)(r->y >> 4);
  hm_dp_w16(ram, r, QUEUE_COL, col);
  hm_dp_w16(ram, r, QUEUE_ROW, row);
  uint16_t vram = (uint16_t)(VRAM_TILEMAP + (screen_part(col) << 1));
  if (second_screen(col)) vram = (uint16_t)(vram + 0x03E0);
  vram = (uint16_t)(vram + (screen_part(row) << 6));
  if (second_screen(row)) vram = (uint16_t)(vram + 0x0400);
  hm_dp_w16(ram, r, QUEUE_VRAM, vram);
  hm_dp_w16(ram, r, SOURCE_PTR, (uint16_t)TILE_GRAPHICS); hm_dp_w16(ram, r, ROW_SOURCE, (uint16_t)TILE_GRAPHICS);
  *hm_dp(ram, r, SOURCE_BANK) = 0xA6; *hm_dp(ram, r, ROW_SOURCE_BANK) = 0xA6;

  r->a = pull16(ram, r);                                 /* PLA */
  hm_jsl(ram, r, RET_6C1_CHANGES, 0x81);
  hm_ReadTileChange(ram, r);
  hm_rtl(ram, r);

  hm_dp_w16(ram, r, CHANGE_COLS, (uint16_t)(hm_dp_r16(ram, r, CHANGE_COLS) << 2));
  hm_dp_w16(ram, r, CHANGE_ROWS, (uint16_t)(hm_dp_r16(ram, r, CHANGE_ROWS) << 1));
  uint16_t src = (uint16_t)(hm_dp_r16(ram, r, SOURCE_PTR) + hm_dp_r16(ram, r, CHANGE_GFX));
  hm_dp_w16(ram, r, SOURCE_PTR, src); hm_dp_w16(ram, r, ROW_SOURCE, src);
  *hm_dp(ram, r, FIRST_CHANNEL) = 2;

  uint16_t x = 0;
  do {
    push16(ram, r, x);                                   /* PHX */
    queue_half(ram, r, *hm_dp(ram, r, FIRST_CHANNEL), x, RET_6C1_DMA_A);
    hm_dp_w16(ram, r, SOURCE_PTR, (uint16_t)(hm_dp_r16(ram, r, SOURCE_PTR) + 0x40));
    x = (uint16_t)(pull16(ram, r) + 1);                  /* PLX; INX; PHX */
    push16(ram, r, x);
    queue_half(ram, r, (uint8_t)(*hm_dp(ram, r, FIRST_CHANNEL) + 1), x, RET_6C1_DMA_B);
    r->a = (uint16_t)(hm_dp_r16(ram, r, ROW_SOURCE) + hm_dp_r16(ram, r, CHANGE_COLS));
    hm_dp_w16(ram, r, ROW_SOURCE, r->a); hm_dp_w16(ram, r, SOURCE_PTR, r->a);
    *hm_dp(ram, r, FIRST_CHANNEL) = 0;
    x = (uint16_t)(pull16(ram, r) + 1);                  /* PLX; INX */
  } while (x != hm_dp_r16(ram, r, CHANGE_ROWS));
  r->x = x;
  r->c = 1;
}

/* ─────────────────────────────────────────────────────────────────────────
 * ChangeMapTile ($81:A688, JSL, RTL; CODE_81A688 in HM-Decomp). A = tile
 * change (0 = nothing), X/Y = position in pixels. Does all of it: queues
 * the VRAM copy, writes the BG1 tilemap in WRAM, and, if the change has a
 * tile type, writes it into $09B6 for the whole block.
 * Leftovers: $86/$88 = columns / rows; registers from SetTileTypeBlock when it
 * runs, otherwise A = 0, X/Y = the position and C from ReadTileChange.
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_688_QUEUE   0xA698   /* JSL QueueTileGraphicsDMA at $81:A695 */
#define RET_688_BG1     0xA6A4   /* JSL DrawTileChange at $81:A6A1 */
#define RET_688_CHANGES 0xA6AB   /* JSL ReadTileChange at $81:A6A8 */
#define RET_688_TYPES   0xA6BF   /* JSL SetTileTypeBlock at $81:A6BC */

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
  hm_DrawTileChange(ram, r);
  hm_rtl(ram, r);
  r->a = pull16(ram, r);                                 /* PLA */
  hm_jsl(ram, r, RET_688_CHANGES, 0x81);
  hm_ReadTileChange(ram, r);
  hm_rtl(ram, r);
  r->x = pull16(ram, r);                                 /* PLX; PLY */
  r->y = pull16(ram, r);
  hm_dp_w16(ram, r, BLOCK_COLS, hm_dp_r16(ram, r, CHANGE_COLS));
  hm_dp_w16(ram, r, BLOCK_ROWS, hm_dp_r16(ram, r, CHANGE_ROWS));
  r->a = hm_dp_r16(ram, r, CHANGE_TYPE);
  if (r->a == 0) return;
  hm_jsl(ram, r, RET_688_TYPES, 0x82);
  hm_SetTileTypeBlock(ram, r);
  hm_rtl(ram, r);
}

/* ─────────────────────────────────────────────────────────────────────────
 * StartMapAnimation ($81:A5E1, JSL, RTL; CODE_81A5E1 in HM-Decomp). A =
 * an entry of the table $81:B963 (3 bytes each: a long pointer to a map
 * animation script). Points $CF-$D1 at it and clears $0989 (the script's
 * wait counter); RunMapAnimation then plays it frame by frame (the magic
 * beans growing, for one).
 * Leftovers: $7E = A in; A = (high byte of the pointer word : bank),
 * X = 3 A + 2, C from the 3 A sum.
 * ───────────────────────────────────────────────────────────────────────── */

void hm_StartMapAnimation(uint8_t *ram, HmRegs *r)
{
  uint16_t n = r->a;
  hm_dp_w16(ram, r, ANIM_ENTRY, n);
  uint32_t sum = (uint32_t)(uint16_t)(n << 1) + n;       /* ASL; CLC; ADC $7E */
  r->c = sum > 0xFFFF;
  uint16_t x = (uint16_t)sum;
  uint16_t word = hm_bus_rd16(ram, (MAP_ANIMATIONS + x) & 0xFFFFFF);
  hm_dp_w16(ram, r, HM_MAP_ANIM, word);
  x = (uint16_t)(x + MAP_ANIM_BANK_AT);
  uint8_t bank = hm_bus_rd8(ram, (MAP_ANIMATIONS + x) & 0xFFFFFF);
  *hm_dp(ram, r, HM_MAP_ANIM_BANK) = bank;
  r->a = (uint16_t)((word & 0xFF00) | bank);
  r->x = x;
  hm_bus_wr8(ram, ((uint32_t)r->db << 16) | ANIMATION_WAIT, 0);
}

/* ─────────────────────────────────────────────────────────────────────────
 * EditTileOnMap ($82:B03A, JSL, RTL). A (low byte) = a tile type, X / Y =
 * a position in pixels: writes the type into the current map ($09B6 +
 * GetTileIndex). Only the logic map changes, not the picture (that is
 * ChangeMapTile's job).
 * EditFarmTile ($82:B049): the same on the farm's own copy
 * ($7E:A4E6), first setting the width code $0181 = 4 (64 tiles).
 * Leftovers: those of GetTileIndex, with A's low byte = the type. The PHA
 * of the type stays on the stack.
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_03A_INDEX   0xB041
#define RET_049_INDEX   0xB057

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

void hm_EditTileOnMap(uint8_t *ram, HmRegs *r)
{
  uint8_t type = (uint8_t)r->a;
  uint16_t x = tile_index_keeping(ram, r, type, RET_03A_INDEX);
  hm_bus_wr8(ram, (((uint32_t)r->db << 16) + HM_MAP_TILES + x) & 0xFFFFFF, type);
}

void hm_EditFarmTile(uint8_t *ram, HmRegs *r)
{
  uint8_t type = (uint8_t)r->a;
  hm_bus_wr8(ram, ((uint32_t)r->db << 16) + HM_MAP_WIDTH, 4);
  uint16_t x = tile_index_keeping(ram, r, type, RET_049_INDEX);
  hm_bus_wr8(ram, (WRAM_LONG(HM_FARM_TILES) + x) & 0xFFFFFF, type);
}

/* ─────────────────────────────────────────────────────────────────────────
 * DrawWholeMap ($82:AF00, JSL, RTL): draw the whole map (64 x 64 tiles of
 * 16 pixels) from its tile types ($09B6). For each tile type t (1-$9F):
 * the drawing is byte +0 of its entry in the property table [$0D] (0:
 * nothing to draw). The types $73-$79 join with their neighbours of the
 * same kind ("autotiling", fences and the like): the four neighbours give
 * a mask (left 8, right 2, up 4, down 1) and DATA8_82B02A[mask] - 1 is
 * added to the drawing. Each tile is drawn with DrawTileChange.
 * ───────────────────────────────────────────────────────────────────────── */

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

void hm_DrawWholeMap(uint8_t *ram, HmRegs *r)
{
  for (uint16_t y = 0; y != 0x0400; ) {
    for (uint16_t x = 0; x != 0x0400; ) {
      push16(ram, r, y);                                   /* PHY; PHX */
      push16(ram, r, x);
      hm_dp_w16(ram, r, TILE_X, x);
      hm_dp_w16(ram, r, TILE_Y, y);
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
            hm_dp_w16(ram, r, JOIN_MASK, 0);
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
                hm_dp_w16(ram, r, JOIN_MASK, mask);
              }
            }
            push16(ram, r, at);                            /* PHX; LDA $86; TAX */
            r->x = mask;
            uint8_t join = (uint8_t)(hm_bus_rd8(ram, JOINS + mask) - 1);
            *hm_dp(ram, r, JOIN_ADD) = join;
            pull16(ram, r);
            r->x = at;
            uint16_t yy = (uint16_t)(t << 2);              /* LDA #0; XBA; LDA; ASL; ASL */
            r->c = 0;
            r->y = yy;
            uint8_t base = hm_bus_rd8(ram, hm_dp_long(ram, r, HM_TILE_PROPS, yy));
            uint16_t s = (uint16_t)(base + join);          /* CLC; ADC $94 (8 bits) */
            r->c = s > 0xFF;
            pic = (uint8_t)s;
            draw = true;
          } else {
            uint16_t yy = (uint16_t)(t << 2);
            r->c = 0;
            r->y = yy;
            pic = hm_bus_rd8(ram, hm_dp_long(ram, r, HM_TILE_PROPS, yy));
            r->a = pic;
            draw = pic != 0;
          }
        }
      }
      if (draw) {
        r->a = pic;
        r->x = hm_dp_r16(ram, r, TILE_X);
        r->y = hm_dp_r16(ram, r, TILE_Y);
        hm_jsl(ram, r, 0xB008, 0x81);
        hm_DrawTileChange(ram, r);
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

/* ─────────────────────────────────────────────────────────────────────────
 * SnowFootprints ($81:A58F, JSL, RTL): footprints in the snow. Called every 8
 * steps of the player with $0907 / $0909 = where the player was 8 steps
 * before. In winter, on the farm (maps 0-3) or the mountain ($10-$14), if
 * that tile's type is 1, 2, $A8 or $F6 (snow), the picture of change $11
 * is queued for the VRAM there (QueueTileGraphicsDMA): only the screen
 * changes, not the map, so the prints are gone when the map is loaded
 * again. Leftovers: A, X, Y, C from the last compare or call.
 * ───────────────────────────────────────────────────────────────────────── */

void hm_SnowFootprints(uint8_t *ram, HmRegs *r)
{
  uint8_t season = hm_bus_rd8(ram, WRAM_LONG(HM_SEASON));
  r->a = (uint16_t)((r->a & 0xFF00) | season);
  r->c = season >= 3;
  if (season != 3) return;                                 /* not winter */
  uint8_t map = *hm_dp(ram, r, HM_MAP_ID);
  r->a = (uint16_t)((r->a & 0xFF00) | map);
  r->c = map >= 4;
  if (map >= 4) {
    r->c = map >= 0x10;
    if (map < 0x10) return;
    r->c = map >= 0x15;
    if (map >= 0x15) return;
  }
  uint32_t db = (uint32_t)r->db << 16;
  r->a = 0;
  r->x = hm_bus_rd16(ram, db | HM_FOOTPRINT_X);
  r->y = hm_bus_rd16(ram, db | HM_FOOTPRINT_Y);
  hm_jsl(ram, r, 0xA5B7, 0x82);
  hm_GetTileProperty(ram, r);
  hm_rtl(ram, r);
  r->a = r->x;                                             /* TXA */
  static const uint16_t snow[4] = { 0x0001, 0x0002, 0x00A8, 0x00F6 };
  bool on_snow = false;
  for (int i = 0; i < 4 && !on_snow; i++) {
    r->c = r->a >= snow[i];
    on_snow = r->a == snow[i];
  }
  if (!on_snow) return;
  r->a = 0x0011;
  r->x = hm_bus_rd16(ram, db | HM_FOOTPRINT_X);
  r->y = hm_bus_rd16(ram, db | HM_FOOTPRINT_Y);
  hm_jsl(ram, r, 0xA5DF, 0x81);
  hm_QueueTileGraphicsDMA(ram, r);
  hm_rtl(ram, r);
}

/* ─────────────────────────────────────────────────────────────────────────
 * RunMapAnimation ($81:A600, JSL, RTL): plays the map animation started by
 * StartMapAnimation, one step per call: [$CF-$D1] points at 7-byte steps
 * (tile change, X, Y words, then frames to wait); each is done with
 * ChangeMapTile and $0989 counts the wait down. $FFFF ends it ($CF-$D1 =
 * 0), $FFFE jumps to the long pointer that follows (whose first step is
 * done at once, unchecked).
 * Leftovers: A, X, Y from the last step, C from its add (or a compare).
 * ───────────────────────────────────────────────────────────────────────── */

void hm_RunMapAnimation(uint8_t *ram, HmRegs *r)
{
  uint32_t wait = (uint32_t)r->db << 16 | ANIMATION_WAIT;
  r->a = hm_dp_r16(ram, r, HM_MAP_ANIM);
  if (!r->a) {
    r->a = (uint16_t)((r->a & 0xFF00) | *hm_dp(ram, r, HM_MAP_ANIM_BANK));
    if (!(r->a & 0xFF)) return;                            /* no animation */
  }
  uint8_t w = hm_bus_rd8(ram, wait);
  r->a = (uint16_t)((r->a & 0xFF00) | w);
  if (w) { hm_bus_wr8(ram, wait, (uint8_t)(w - 1)); return; }
  r->a = hm_bus_rd16(ram, hm_dp_long(ram, r, HM_MAP_ANIM, STEP_CHANGE));
  r->c = r->a >= 0xFFFF;
  if (r->a == 0xFFFF) {                                    /* the end */
    hm_dp_w16(ram, r, HM_MAP_ANIM, 0);
    *hm_dp(ram, r, HM_MAP_ANIM_BANK) = 0;
    return;
  }
  r->c = r->a >= 0xFFFE;
  if (r->a == 0xFFFE) {                                    /* jump */
    r->y = JUMP_ADDR;
    hm_dp_w16(ram, r, JUMP_PTR, hm_bus_rd16(ram, hm_dp_long(ram, r, HM_MAP_ANIM, JUMP_ADDR)));
    r->y = JUMP_BANK;
    uint8_t bank = hm_bus_rd8(ram, hm_dp_long(ram, r, HM_MAP_ANIM, JUMP_BANK));
    *hm_dp(ram, r, JUMP_PTR_BANK) = bank;
    r->a = hm_dp_r16(ram, r, JUMP_PTR);
    hm_dp_w16(ram, r, HM_MAP_ANIM, r->a);
    r->a = (uint16_t)((r->a & 0xFF00) | bank);
    *hm_dp(ram, r, HM_MAP_ANIM_BANK) = bank;
  }
  uint16_t change = hm_bus_rd16(ram, hm_dp_long(ram, r, HM_MAP_ANIM, STEP_CHANGE));
  push16(ram, r, change);
  uint16_t x = hm_bus_rd16(ram, hm_dp_long(ram, r, HM_MAP_ANIM, STEP_X));
  push16(ram, r, x);
  uint16_t y = hm_bus_rd16(ram, hm_dp_long(ram, r, HM_MAP_ANIM, STEP_Y));
  push16(ram, r, y);
  uint8_t frames = hm_bus_rd8(ram, hm_dp_long(ram, r, HM_MAP_ANIM, STEP_FRAMES));
  hm_bus_wr8(ram, wait, frames);
  r->y = pull16(ram, r); r->x = pull16(ram, r); r->a = pull16(ram, r);
  hm_jsl(ram, r, 0xA64C, 0x81);
  hm_ChangeMapTile(ram, r);
  hm_rtl(ram, r);
  uint32_t next = (uint32_t)hm_dp_r16(ram, r, HM_MAP_ANIM) + STEP_BYTES;
  r->c = next > 0xFFFF;
  r->a = (uint16_t)next;
  hm_dp_w16(ram, r, HM_MAP_ANIM, r->a);
}

/* FreeMapObject ($81:A4F1, JSL, RTL): frees the map object at [$07] (its
 * flags = 0) and clears $0974 (its flags' copy). A's low byte = 0. */
void hm_FreeMapObject(uint8_t *ram, HmRegs *r)
{
  r->y = 0;
  r->a &= 0xFF00;
  hm_bus_wr8(ram, hm_dp_long(ram, r, MAP_OBJ_PTR, 0), 0);
  hm_bus_wr8(ram, (uint32_t)r->db << 16 | HM_ITEM_FLAGS, 0);
}
