/*
 * dma.c — the "programmed DMA" queue: copies to VRAM / CGRAM that the game
 * prepares during the frame and the NMI then starts all at once.
 *
 * Direct page:
 *   $27 next free channel     $28 channels to start (one bit each)
 *   $29 destination register (B-bus: $18 = VRAM data, else CGRAM...)
 *   $2A+ch  VRAM address-mode byte per channel (written to $2115 later)
 *   $32+2ch destination address per channel (VRAM / CGRAM address)
 *   $72-$74 source (24-bit)   $C7 budget of bytes left this frame
 * The channel itself is set up directly in the DMA registers $43x0-$43x6.
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */
#include "hm_vars.h"
#include "hm_regs.h"

/* ── Names of the memory locations used here (as in video.c) ────────────── */

#define DMA_CHANNEL_FLAGS 0x808B3C   /* ROM: db 1 << ch */

/* Direct page */
#define DMA_CHANNEL     0x27   /* 8b: the channel to set up next */
#define DMA_WAITING     0x28   /* 8b: the channels to start, one bit each */
#define DMA_B_ADDRESS   0x29   /* 8b: the destination register (B-bus, $21xx) */
#define DMA_VMAIN       0x2A   /* 8b + channel: the VRAM address mode */
#define DMA_DESTINATION 0x32   /* 16b + 2 channel: the VRAM / CGRAM address */
#define DMA_SOURCE      0x72   /* 24b $72-$74: the source */
#define DMA_SOURCE_BANK 0x74   /* 8b: its bank */
#define LETTER_EXTRA    0x7E   /* 16b: CopyLetterToVram's 16 x A (extra bytes per half) */

/* The DMA registers: $4300 + 16 x channel (through the data bank) */
#define DMA_REGS        0x4300u
#define DMAP            0      /* 8b: the transfer mode */
#define BBAD            1      /* 8b: the B-bus register */
#define A1T             2      /* 16b: the source address */
#define A1B             4      /* 8b: the source bank */
#define DAS             5      /* 16b: the size */

/* ─────────────────────────────────────────────────────────────────────────
 * AddProgrammedDMA ($80:8A33, JSL, RTL). A = VRAM address mode for the
 * channel, X = destination address, Y = size in bytes; uses channel $27
 * and destination register $29.
 *   $32+2ch = X, $2A+ch = A,
 *   $43c0 = 1 (two registers, for VRAM $18) or 0 (one register),
 *   $43c1 = $29, $43c2-4 = source $72-$74, $43c5-6 = Y,
 *   $C7 -= Y (read back from the size register), $28 |= 1 << ch.
 * The registers are written with the data bank (STA.W $4300,X).
 * Leftovers: A = the new $28, X = channel, C = 0.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_AddProgrammedDMA(uint8_t *ram, HmRegs *r)
{
  uint16_t mode = r->a, dest = r->x, size = r->y;
  hm_push8(ram, r, (uint8_t)(mode >> 8)); hm_push8(ram, r, (uint8_t)mode);   /* PHA */
  hm_push8(ram, r, (uint8_t)(dest >> 8)); hm_push8(ram, r, (uint8_t)dest);   /* TXA; PHA */
  hm_pull8(ram, r); hm_pull8(ram, r); hm_pull8(ram, r); hm_pull8(ram, r);

  uint8_t ch = *hm_dp(ram, r, DMA_CHANNEL);
  hm_dp_w16(ram, r, DMA_DESTINATION + 2u * ch, dest);
  *hm_dp(ram, r, DMA_VMAIN + ch) = (uint8_t)mode;

  uint32_t regs = ((uint32_t)r->db << 16) + DMA_REGS + (uint16_t)(ch << 4);
  uint8_t to = *hm_dp(ram, r, DMA_B_ADDRESS);
  hm_bus_wr8(ram, regs + DMAP, to == BBAD_VRAM ? 0x01 : 0x00);  /* DMAPx */
  hm_bus_wr8(ram, regs + BBAD, to);                             /* BBADx */
  hm_bus_wr16(ram, regs + A1T, hm_dp_r16(ram, r, DMA_SOURCE));  /* A1TxL/H */
  hm_bus_wr8(ram, regs + A1B, *hm_dp(ram, r, DMA_SOURCE_BANK)); /* A1Bx */
  hm_bus_wr16(ram, regs + DAS, size);                           /* DASxL/H */
  hm_dp_w16(ram, r, HM_LOAD_BUDGET, (uint16_t)(hm_dp_r16(ram, r, HM_LOAD_BUDGET) - hm_bus_rd16(ram, regs + DAS)));

  uint8_t *to_start = hm_dp(ram, r, DMA_WAITING);
  *to_start |= hm_bus_rd8(ram, DMA_CHANNEL_FLAGS + ch);
  r->a = *to_start;
  r->x = ch;
  r->c = 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * RemoveProgrammedDMA ($80:8AA0, JSL, RTL). Takes channel $27 out of the
 * channels to start ($28 &= ~(1 << ch)); its registers stay as they were.
 * With 8-bit A and 16-bit X, TAX copies the whole accumulator, so the
 * table index is (high byte of A : $27): the callers always have 0 there.
 * Leftovers: A = (high byte of A : the new $28), X = that index.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_RemoveProgrammedDMA(uint8_t *ram, HmRegs *r)
{
  r->x = (uint16_t)((r->a & 0xFF00) | *hm_dp(ram, r, DMA_CHANNEL));
  uint8_t flag = hm_bus_rd8(ram, (DMA_CHANNEL_FLAGS + r->x) & 0xFFFFFF);
  uint8_t *to_start = hm_dp(ram, r, DMA_WAITING);
  *to_start = (uint8_t)(~flag & *to_start);
  r->a = (uint16_t)((r->a & 0xFF00) | *to_start);
}

/* ─────────────────────────────────────────────────────────────────────────
 * DrawTilemapBlock2x2 ($83:81B7, JSL, RTL). Draws a 2x2 block of BG3 tilemap
 * entries: X = VRAM address of its top row, $72-$74 = the 4 words (two
 * per row). Two 4-byte DMAs: channel 6 for the top row, channel 7 for the
 * bottom one (X + $20, one tilemap row below; source $72 + 4).
 * Used for the text box arrow (DrawTextBoxArrow).
 * Leftovers: those of the second AddProgrammedDMA; Y = 4.
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_1B7_TOP     0x81CF
#define RET_1B7_BOTTOM  0x81F6

void hm_DrawTilemapBlock2x2(uint8_t *ram, HmRegs *r)
{
  uint16_t top = r->x;
  *hm_dp(ram, r, DMA_CHANNEL) = 6;
  *hm_dp(ram, r, DMA_B_ADDRESS) = BBAD_VRAM;          /* VRAM data port */
  r->y = 4;
  r->a = 0x0080;
  hm_push8(ram, r, (uint8_t)(top >> 8));              /* PHX */
  hm_push8(ram, r, (uint8_t)top);
  hm_jsl(ram, r, RET_1B7_TOP, 0x80);
  hm_AddProgrammedDMA(ram, r);
  hm_rtl(ram, r);
  *hm_dp(ram, r, DMA_CHANNEL) = 7;
  *hm_dp(ram, r, DMA_B_ADDRESS) = BBAD_VRAM;
  hm_pull8(ram, r); hm_pull8(ram, r);                 /* PLX */
  r->x = (uint16_t)(top + 0x20);
  r->y = 4;
  hm_dp_w16(ram, r, DMA_SOURCE, (uint16_t)(hm_dp_r16(ram, r, DMA_SOURCE) + 4));
  r->a = 0x0080;
  hm_jsl(ram, r, RET_1B7_BOTTOM, 0x80);
  hm_AddProgrammedDMA(ram, r);
  hm_rtl(ram, r);
}

/* ─────────────────────────────────────────────────────────────────────────
 * CopyLetterToVram ($83:8166, JSL, RTL). Copies one letter of the font to VRAM:
 * X = VRAM address, $72-$74 = its graphics, A = extra width (0: one 8x8
 * tile of 16 bytes per half; each unit adds 16 bytes). Two DMAs: the top
 * half on channel 6, the bottom half (source + $100, VRAM X + $80) on
 * channel 7. $7E = 16 A.
 * Leftovers: those of the second AddProgrammedDMA; Y = 16 + 16 A.
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_166_TOP     0x818A
#define RET_166_BOTTOM  0x81B5

void hm_CopyLetterToVram(uint8_t *ram, HmRegs *r)
{
  uint16_t extra = (uint16_t)(r->a << 4), top = r->x;
  hm_dp_w16(ram, r, LETTER_EXTRA, extra);
  *hm_dp(ram, r, DMA_CHANNEL) = 6;
  *hm_dp(ram, r, DMA_B_ADDRESS) = BBAD_VRAM;          /* VRAM data port */
  r->y = (uint16_t)(0x10 + extra);
  r->a = 0x0080;
  hm_push8(ram, r, (uint8_t)(top >> 8));              /* PHX */
  hm_push8(ram, r, (uint8_t)top);
  hm_jsl(ram, r, RET_166_TOP, 0x80);
  hm_AddProgrammedDMA(ram, r);
  hm_rtl(ram, r);
  *hm_dp(ram, r, DMA_CHANNEL) = 7;
  *hm_dp(ram, r, DMA_B_ADDRESS) = BBAD_VRAM;
  hm_pull8(ram, r); hm_pull8(ram, r);                 /* PLX */
  r->x = (uint16_t)(top + 0x80);
  r->y = (uint16_t)(0x10 + extra);
  hm_dp_w16(ram, r, DMA_SOURCE, (uint16_t)(hm_dp_r16(ram, r, DMA_SOURCE) + 0x100));
  r->a = 0x0080;
  hm_jsl(ram, r, RET_166_BOTTOM, 0x80);
  hm_AddProgrammedDMA(ram, r);
  hm_rtl(ram, r);
}
