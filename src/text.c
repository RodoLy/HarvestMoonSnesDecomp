/*
 * text.c — the dialogue box: opening it, the blinking "more text" arrow and
 * closing it.
 *
 * The box is drawn on BG3, the layer for text. While it is open:
 *   $0183  number of the text being shown (index of Text_Pointer_Table)
 *   $0185  VRAM address where the next letter goes ($5010 at the start)
 *   $0187-$0190  the printer's state (cleared when a box opens):
 *          $018A = position of the arrow (0-11, one of 12 places),
 *          $018B = arrow blink: bit 7 = which frame, bits 0-6 = counter
 *   $0191  height of the box in lines of 2 rows (1 or 2), chosen once
 *   $019B  bit 0: a box is open (the printer runs every frame)
 *   $01-$03  long pointer to the text being read
 *   $7F1F5A bit 14 (FLAG5A): "a box is open", other systems wait for it
 * Opening a box scrolls BG3 up (BG3_Map_Offset_Y, $0146) so the box shows
 * and stops the clock; closing it scrolls back and lets the clock run again
 * unless $0196 bit 5 says it was already stopped.
 *
 * Text_Pointer_Table ($83:9BF6) has 3 bytes per text: a long pointer.
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

#define TEXT_POINTERS     0x839BF6
#define ARROW_PLACES      0x83947D   /* 12 words: VRAM address of each arrow place */
#define ARROW_FRAME_0     0x9457     /* $83:9457: 2x2 tilemap entries, arrow up */
#define ARROW_FRAME_1     0x945F     /* $83:945F: the other frame */
#define BOX_FLAGS         0x7F1F5A
#define BOX_OPEN_FLAG     0x4000

static uint32_t db_addr(const HmRegs *r, uint16_t addr) { return (uint32_t)r->db << 16 | addr; }
static uint8_t rd8(uint8_t *ram, const HmRegs *r, uint16_t a) { return hm_bus_rd8(ram, db_addr(r, a)); }
static uint16_t rd16(uint8_t *ram, const HmRegs *r, uint16_t a) { return hm_bus_rd16(ram, db_addr(r, a)); }
static void wr8(uint8_t *ram, const HmRegs *r, uint16_t a, uint8_t v) { hm_bus_wr8(ram, db_addr(r, a), v); }
static void wr16(uint8_t *ram, const HmRegs *r, uint16_t a, uint16_t v) { hm_bus_wr16(ram, db_addr(r, a), v); }

/* How far BG3 scrolls for a box of $0191 lines: $0100 plus 128 per extra
 * line ("LDA #0; XBA; LDA $0191; DEC A", then 7 ASL in 16 bits). */
static uint16_t extra_scroll(uint8_t *ram, const HmRegs *r)
{
  return (uint16_t)((uint8_t)(rd8(ram, r, 0x0191) - 1) << 7);
}

/* ─────────────────────────────────────────────────────────────────────────
 * StartTextBox ($83:935F, JSL, RTL). X = number of the text. Resets the
 * printer, points $01-$03 at the text, picks the box height once ($0191:
 * 2 lines if $090D < $81, else 1), scrolls BG3 up to show the box, stops
 * the clock (time_running = 0, low byte only) and sets FLAG5A.
 * Leftovers: A = the new $7F1F5A, X = 3 X + 2, C from the last SBC.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_StartTextBox(uint8_t *ram, HmRegs *r)
{
  uint16_t text = r->x;
  wr16(ram, r, 0x0183, text);
  wr16(ram, r, 0x0185, 0x5010);                       /* LDA #$5000; CLC; ADC #$10 */
  wr16(ram, r, 0x0187, 0);
  wr8(ram, r, 0x019B, rd8(ram, r, 0x019B) | 0x01);
  static const uint16_t cleared[] = { 0x0189, 0x018B, 0x018C, 0x018E, 0x018F, 0x0190 };
  for (unsigned i = 0; i < sizeof cleared / sizeof cleared[0]; i++)
    wr8(ram, r, cleared[i], 0);

  uint16_t x = (uint16_t)(text * 3);                  /* ASL; CLC; ADC $0183 */
  hm_dp_w16(ram, r, 0x01, hm_bus_rd16(ram, (TEXT_POINTERS + x) & 0xFFFFFF));
  x = (uint16_t)(x + 2);
  *hm_dp(ram, r, 0x03) = hm_bus_rd8(ram, (TEXT_POINTERS + x) & 0xFFFFFF);
  r->x = x;

  if (rd8(ram, r, 0x0191) == 0)
    wr8(ram, r, 0x0191, rd16(ram, r, 0x090D) < 0x0081 ? 2 : 1);

  uint16_t extra = extra_scroll(ram, r);
  hm_dp_w16(ram, r, 0x7E, extra);
  uint16_t y = (uint16_t)(rd16(ram, r, 0x0146) - 0x0100);   /* SEC; SBC #$0100 */
  r->c = y >= extra;                                         /* SEC; SBC $7E */
  wr16(ram, r, 0x0146, (uint16_t)(y - extra));

  wr8(ram, r, 0x0973, 0);                                    /* STZ time_running (8-bit) */
  r->a = (uint16_t)(hm_bus_rd16(ram, BOX_FLAGS) | BOX_OPEN_FLAG);
  hm_bus_wr16(ram, BOX_FLAGS, r->a);
}

/* ─────────────────────────────────────────────────────────────────────────
 * SUB_83932D ($83:932D, JSL, RTL). Wipes the box: queues a DMA of $0C00
 * bytes of the empty BG3 tilemap ($9C:BBA3) to VRAM $5000 on channel 6 and
 * drops whatever was queued on channel 7 (half of an arrow, UNK_SetDMA2).
 * Leftovers: those of RemoveProgrammedDMA; Y = $0C00.
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_32D_ADD     0x9353
#define RET_32D_REMOVE  0x935D

void hm_SUB_83932D(uint8_t *ram, HmRegs *r)
{
  *hm_dp(ram, r, 0x27) = 6;
  *hm_dp(ram, r, 0x29) = 0x18;                        /* VRAM data port */
  r->x = 0x5000;
  r->y = 0x0C00;
  hm_dp_w16(ram, r, 0x72, 0xBBA3);
  *hm_dp(ram, r, 0x74) = 0x9C;
  r->a = 0x0080;
  hm_jsl(ram, r, RET_32D_ADD, 0x80);
  hm_AddProgrammedDMA(ram, r);
  hm_rtl(ram, r);
  r->a = (uint16_t)((r->a & 0xFF00) | 7);             /* SEP; LDA #7 */
  *hm_dp(ram, r, 0x27) = 7;
  hm_jsl(ram, r, RET_32D_REMOVE, 0x80);
  hm_RemoveProgrammedDMA(ram, r);
  hm_rtl(ram, r);
}

/* ─────────────────────────────────────────────────────────────────────────
 * CloseTextBox ($83:93F9, JSL, RTL; CODE_8393F9 in HM-Decomp). The reverse
 * of StartTextBox: $019B = 0, BG3 scrolls back down, the box is wiped
 * (SUB_83932D), the clock runs again (time_running |= 1) unless $0196
 * bit 5 is set, and FLAG5A is cleared.
 * Leftovers: A = the new $7F1F5A; X, Y, C those of SUB_83932D.
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_3F9_WIPE    0x9424

void hm_CloseTextBox(uint8_t *ram, HmRegs *r)
{
  wr8(ram, r, 0x019B, 0);
  uint16_t extra = extra_scroll(ram, r);
  hm_dp_w16(ram, r, 0x7E, extra);
  wr16(ram, r, 0x0146, (uint16_t)(rd16(ram, r, 0x0146) + 0x0100 + extra));
  hm_jsl(ram, r, RET_3F9_WIPE, 0x83);
  hm_SUB_83932D(ram, r);
  hm_rtl(ram, r);
  if (rd16(ram, r, 0x0196) & 0x0020)
    wr8(ram, r, 0x0973, rd8(ram, r, 0x0973) | 0x01);
  r->a = (uint16_t)(hm_bus_rd16(ram, BOX_FLAGS) & ~BOX_OPEN_FLAG);
  hm_bus_wr16(ram, BOX_FLAGS, r->a);
}

/* ─────────────────────────────────────────────────────────────────────────
 * DrawTextBoxArrow ($83:94D7, JSL, RTL; CODE_8394D7 in HM-Decomp). A = the
 * frame (1 = $83:945F, anything else = $83:9457). Draws the 2x2 arrow at
 * place $018A (at most 11) with UNK_SetDMA2.
 * Leftovers: those of UNK_SetDMA2. The PHA / PLA of A stays on the stack.
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_4D7_DRAW    0x951A

void hm_DrawTextBoxArrow(uint8_t *ram, HmRegs *r)
{
  uint16_t frame = r->a;
  hm_push8(ram, r, (uint8_t)(frame >> 8));            /* PHA */
  hm_push8(ram, r, (uint8_t)frame);
  uint8_t place = rd8(ram, r, 0x018A);
  if (place >= 0x0B) place = 0x0B;
  r->x = hm_bus_rd16(ram, ARROW_PLACES + 2u * place);
  hm_pull8(ram, r); hm_pull8(ram, r);                 /* PLA */
  uint16_t tiles = frame == 1 ? ARROW_FRAME_1 : ARROW_FRAME_0;
  hm_dp_w16(ram, r, 0x72, tiles);
  r->a = (uint16_t)((tiles & 0xFF00) | 0x83);         /* SEP; LDA #$83 */
  *hm_dp(ram, r, 0x74) = 0x83;
  hm_jsl(ram, r, RET_4D7_DRAW, 0x83);
  hm_UNK_SetDMA2(ram, r);
  hm_rtl(ram, r);
}

/* ─────────────────────────────────────────────────────────────────────────
 * BlinkTextBoxArrow ($83:9495, JSR, RTS; CODE_839495 in HM-Decomp). Called
 * every frame while the box waits for a button. $7E = A in. Every 20
 * frames the arrow changes frame ($018B bit 7 flips, the counter restarts);
 * the current frame is drawn and the counter goes up.
 * Leftovers: A = (high byte of DrawTextBoxArrow's A : the new $018B);
 * X, Y, C those of DrawTextBoxArrow.
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_495_FRAME0  0x94C1
#define RET_495_FRAME1  0x94CC

void hm_BlinkTextBoxArrow(uint8_t *ram, HmRegs *r)
{
  hm_dp_w16(ram, r, 0x7E, r->a);
  if ((rd16(ram, r, 0x018B) & 0x007F) == 0x14)
    wr8(ram, r, 0x018B, (uint8_t)((rd8(ram, r, 0x018B) & 0x80) ^ 0x80));
  bool second = rd8(ram, r, 0x018B) & 0x80;
  r->a = second ? 1 : 0;
  hm_jsl(ram, r, second ? RET_495_FRAME1 : RET_495_FRAME0, 0x83);
  hm_DrawTextBoxArrow(ram, r);
  hm_rtl(ram, r);
  uint8_t counter = (uint8_t)(rd8(ram, r, 0x018B) + 1);
  wr8(ram, r, 0x018B, counter);
  r->a = (uint16_t)((r->a & 0xFF00) | counter);
}

/* ─────────────────────────────────────────────────────────────────────────
 * TextBoxArrow ($83:9447, JSL, RTL; CODE_839447 in HM-Decomp): the long
 * entry to BlinkTextBoxArrow (JSR; RTL).
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_447_BLINK   0x944B

void hm_TextBoxArrow(uint8_t *ram, HmRegs *r)
{
  hm_push8(ram, r, (uint8_t)(RET_447_BLINK >> 8));    /* JSR */
  hm_push8(ram, r, (uint8_t)RET_447_BLINK);
  hm_BlinkTextBoxArrow(ram, r);
  hm_pull8(ram, r); hm_pull8(ram, r);                 /* RTS */
}
