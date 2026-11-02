/*
 * decompress.c — the game's compressed data (LZSS).
 *
 * DecompressTileMap ($83:81F8, JSL, RTL): [$72] -> [$75]. The data starts
 * with its size (a word; then two more bytes that are skipped) and then
 * groups of one control byte and 8 items. Each bit of the control byte,
 * lowest first, says what the next item is:
 *   1  a literal byte, copied as it is;
 *   0  two bytes: a distance of 11 bits (low byte, then the top 3 bits of
 *      the second byte) and a length (its low 5 bits + 3, so 3-34): copy
 *      that many bytes from that place of the window.
 * The window is the last 2 KB written, kept at $7F:0000-$7F:07FF (cleared
 * at the start; the writing starts at $07DE = 2048 - 34, the usual LZSS
 * layout). It stops when the size is used up (with size 0 it would run
 * 65536 bytes).
 * Leftovers: A = 0, X = the last window index used, the flags (and the
 * 8 / 16-bit widths) back as they were (PHP / PLP).
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

/* ── Names of the memory locations used here ────────────────────────────── */

#define WINDOW 0x7F0000          /* 2 KB: the last bytes written (bus address) */

/* Direct page */
#define SOURCE        0x72       /* 24b $72-$74: the compressed data */
#define DEST          0x75       /* 24b $75-$77: where the output goes */
#define SIZE_LEFT     0x7E       /* 16b: bytes still to write */
#define CONTROL       0x80       /* 16b: the control byte, shifted right per item */
#define CONTROL_BITS  0x82       /* 16b: its bits left - 1 (negative = read a new one) */
#define WINDOW_POS    0x84       /* 16b: where the next byte goes in the window */
#define COPY_FROM     0x86       /* 16b: a copy's place in the window */
#define COPY_LEFT     0x88       /* 16b: a copy's bytes left */

static uint16_t dp16(uint8_t *ram, const HmRegs *r, unsigned a) { return hm_dp_r16(ram, r, a); }
static void set16(uint8_t *ram, const HmRegs *r, unsigned a, uint16_t v) { hm_dp_w16(ram, r, a, v); }
/* LDA [$72]; INC $72; AND #$00FF */
static uint8_t next_byte(uint8_t *ram, HmRegs *r)
{
  uint8_t b = hm_bus_rd8(ram, hm_dp_long(ram, r, SOURCE, 0));
  set16(ram, r, SOURCE, (uint16_t)(dp16(ram, r, SOURCE) + 1));
  return b;
}
/* STA [$75]; INC $75; DEC $7E: false when the size is used up */
static bool put(uint8_t *ram, HmRegs *r, uint8_t b)
{
  hm_bus_wr8(ram, hm_dp_long(ram, r, DEST, 0), b);
  set16(ram, r, DEST, (uint16_t)(dp16(ram, r, DEST) + 1));
  uint16_t left = (uint16_t)(dp16(ram, r, SIZE_LEFT) - 1);
  set16(ram, r, SIZE_LEFT, left);
  if (!left) return false;
  r->x = dp16(ram, r, WINDOW_POS);                         /* and into the window */
  hm_bus_wr8(ram, WINDOW + r->x, b);
  set16(ram, r, WINDOW_POS, (uint16_t)((r->x + 1) & 0x07FF));
  return true;
}

void hm_DecompressTileMap(uint8_t *ram, HmRegs *r)
{
  hm_push8(ram, r, r->p);                                  /* PHP */
  for (uint16_t x = 0; x < 0x0800; x += 2) hm_bus_wr16(ram, WINDOW + x, 0);
  r->x = 0x0800;
  set16(ram, r, SIZE_LEFT, hm_bus_rd16(ram, hm_dp_long(ram, r, SOURCE, 0)));
  set16(ram, r, SOURCE, (uint16_t)(dp16(ram, r, SOURCE) + 4));
  set16(ram, r, CONTROL, 0);
  set16(ram, r, CONTROL_BITS, 0);
  set16(ram, r, WINDOW_POS, 0x07DE);
  for (;;) {
    uint16_t bit = (uint16_t)(dp16(ram, r, CONTROL_BITS) - 1);   /* DEC $82 */
    set16(ram, r, CONTROL_BITS, bit);
    if (bit & 0x8000) {                                    /* a new control byte */
      set16(ram, r, CONTROL, next_byte(ram, r));
      set16(ram, r, CONTROL_BITS, 7);
    }
    uint16_t control = dp16(ram, r, CONTROL);
    set16(ram, r, CONTROL, (uint16_t)(control >> 1));      /* LSR $80 */
    if (control & 1) {                                     /* a literal */
      if (!put(ram, r, next_byte(ram, r))) break;
      continue;
    }
    uint8_t low = next_byte(ram, r);
    uint8_t second = next_byte(ram, r);
    r->x = second;
    set16(ram, r, COPY_LEFT, (uint16_t)((second & 0x1F) + 3));
    set16(ram, r, COPY_FROM, (uint16_t)((uint16_t)(second & 0xE0) << 3 | low));
    bool more = true;
    do {
      r->x = dp16(ram, r, COPY_FROM);
      more = put(ram, r, hm_bus_rd8(ram, WINDOW + r->x));
      if (!more) break;
      set16(ram, r, COPY_FROM, (uint16_t)((dp16(ram, r, COPY_FROM) + 1) & 0x07FF));
      set16(ram, r, COPY_LEFT, (uint16_t)(dp16(ram, r, COPY_LEFT) - 1));
    } while (dp16(ram, r, COPY_LEFT) != 0);
    if (!more) break;
  }
  r->a = dp16(ram, r, SIZE_LEFT);                          /* 0 */
  r->c = hm_pull8(ram, r) & 1;                             /* PLP */
}
