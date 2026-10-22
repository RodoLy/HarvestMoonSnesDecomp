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

/* ═════════════════════════════════════════════════════════════════════════
 * Letters. The font is in the ROM in groups of 64 letters (table $83:98AE,
 * a long pointer per group: $94:9843 + $1000 k for groups 0-5, $95:8000 +
 * $1000 (k - 6) for 6-9). In a group, 8 letters per row of $200 bytes,
 * 32 bytes apart; a letter is two 8x8 tiles one above the other, the
 * bottom one $100 bytes after the top one. Codes $BC-$C5 are the digits
 * 0-9, $B1 the "waiting" mark.
 *
 * A letter is not written on a fixed tilemap: the box's tilemap points at
 * tiles in order, and the printer copies each letter's graphics into the
 * next tile ($0185, VRAM; +8 words per letter; a line is 16 tiles, its
 * bottom halves $80 words further, so at $xx80 the cursor jumps $80).
 * ═════════════════════════════════════════════════════════════════════════ */
#define FONT_GROUPS       0x8398AE
#define POWERS_OF_TEN     0x8398D6   /* 8 x 3 bytes: 1, 10, ... 10,000,000 */
#define LETTER_WAITING    0x00B1
#define LETTER_ZERO       0x00BC

static void push16(uint8_t *ram, HmRegs *r, uint16_t v)
{
  hm_push8(ram, r, (uint8_t)(v >> 8));
  hm_push8(ram, r, (uint8_t)v);
}
static void jsr(uint8_t *ram, HmRegs *r, uint16_t ret, void (*fn)(uint8_t *, HmRegs *))
{
  push16(ram, r, ret);
  fn(ram, r);
  hm_pull8(ram, r); hm_pull8(ram, r);
}
static void jsl(uint8_t *ram, HmRegs *r, uint16_t ret, void (*fn)(uint8_t *, HmRegs *))
{
  hm_jsl(ram, r, ret, 0x83);
  fn(ram, r);
  hm_rtl(ram, r);
}

/* ─────────────────────────────────────────────────────────────────────────
 * FindLetterGraphics ($83:9862, JSR, RTS; CODE_839862 in HM-Decomp).
 * A = letter code; $72-$74 = where its graphics are.
 * Leftovers: A = $72, X = 3 (code / 64) + 2, $7E = 32 (code & 7),
 * $80 = $200 ((code & $3F) / 8), C from the last ADC.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_FindLetterGraphics(uint8_t *ram, HmRegs *r)
{
  uint16_t code = r->a, group = (uint16_t)(code >> 6);
  hm_dp_w16(ram, r, 0x7E, code);
  hm_dp_w16(ram, r, 0x80, group);
  uint16_t x = (uint16_t)(group * 3);
  hm_dp_w16(ram, r, 0x72, hm_bus_rd16(ram, FONT_GROUPS + x));
  x = (uint16_t)(x + 2);
  *hm_dp(ram, r, 0x74) = hm_bus_rd8(ram, FONT_GROUPS + x);
  uint16_t row = (uint16_t)(((code & 0x3F) >> 3) << 9);
  uint16_t column = (uint16_t)((code & 7) << 5);
  hm_dp_w16(ram, r, 0x80, row);
  hm_dp_w16(ram, r, 0x7E, column);
  uint32_t s = (uint32_t)hm_dp_r16(ram, r, 0x72) + column;       /* CLC; ADC $7E */
  s = (s & 0xFFFF) + row + (s >> 16);                             /* ADC $80 */
  r->c = s > 0xFFFF;
  r->a = (uint16_t)s;
  hm_dp_w16(ram, r, 0x72, r->a);
  r->x = x;
}

/* ─────────────────────────────────────────────────────────────────────────
 * DrawLetter ($83:9823, JSL, RTL; CODE_839823 in HM-Decomp). A = letter
 * code: copies it to the tile at the cursor $0185 (UNK_SetDMA1). Callers
 * pass X = 1 or 0 (a wide letter?), but the routine starts with LDX #0
 * and the width is always 0: one 8x8 tile per half.
 * Leftovers: those of UNK_SetDMA1. The PHX of 0 stays on the stack.
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_823_FIND    0x982B
#define RET_823_COPY    0x9836

void hm_DrawLetter(uint8_t *ram, HmRegs *r)
{
  r->x = 0;
  push16(ram, r, 0);                                  /* LDX #0; PHX */
  jsr(ram, r, RET_823_FIND, hm_FindLetterGraphics);
  hm_pull8(ram, r); hm_pull8(ram, r);                 /* PLX */
  r->a = 0;                                           /* TXA: the width */
  r->x = rd16(ram, r, 0x0185);
  jsl(ram, r, RET_823_COPY, hm_UNK_SetDMA1);
}

/* ─────────────────────────────────────────────────────────────────────────
 * AdvanceLetterCursor ($83:9838, JSR, RTS; CODE_839838 in HM-Decomp). The
 * cursor $0185 goes one tile (8 words) forward; at the end of a line
 * ($xx80) it skips the $80 words of the bottom halves. Like DrawLetter,
 * it ignores the X its callers pass (LDX #0): $80 = 8 always.
 * Leftovers: X = 0; A = the low byte of $0185 and C from CMP #$80, or
 * A = the new $0185 and C from that ADC.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_AdvanceLetterCursor(uint8_t *ram, HmRegs *r)
{
  r->x = 0;
  hm_dp_w16(ram, r, 0x80, 8);                         /* TXA; ASL x3; ADC #8 */
  uint16_t at = (uint16_t)(rd16(ram, r, 0x0185) + 8);
  wr16(ram, r, 0x0185, at);
  if ((at & 0xFF) != 0x80) {
    r->a = at & 0xFF;
    r->c = r->a >= 0x80;
    return;
  }
  uint32_t next = (uint32_t)at + 0x80;
  r->c = next > 0xFFFF;
  r->a = (uint16_t)next;
  wr16(ram, r, 0x0185, r->a);
}

/* ─────────────────────────────────────────────────────────────────────────
 * BlinkTextCursor ($83:975F, JSR, RTS; CODE_83975F in HM-Decomp). Like
 * BlinkTextBoxArrow, but inside the text: every 20 frames it alternates
 * between letter A (in) and the mark $B1 at the cursor. Used while the
 * text waits for a button (codes $00A2 and $FFFE).
 * Leftovers: A = (high byte of DrawLetter's A : the new $018B); X, Y, C
 * those of DrawLetter.
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_75F_LETTER  0x978D
#define RET_75F_MARK    0x979B

void hm_BlinkTextCursor(uint8_t *ram, HmRegs *r)
{
  hm_dp_w16(ram, r, 0x7E, r->a);
  if ((rd16(ram, r, 0x018B) & 0x007F) == 0x14)
    wr8(ram, r, 0x018B, (uint8_t)((rd8(ram, r, 0x018B) & 0x80) ^ 0x80));
  r->x = 1;
  if (rd8(ram, r, 0x018B) & 0x80) {
    r->a = LETTER_WAITING;
    jsl(ram, r, RET_75F_MARK, hm_DrawLetter);
  } else {
    r->a = hm_dp_r16(ram, r, 0x7E);
    jsl(ram, r, RET_75F_LETTER, hm_DrawLetter);
  }
  uint8_t counter = (uint8_t)(rd8(ram, r, 0x018B) + 1);
  wr8(ram, r, 0x018B, counter);
  r->a = (uint16_t)((r->a & 0xFF00) | counter);
}

/* ─────────────────────────────────────────────────────────────────────────
 * PrintDigit ($83:97A6, JSL, RTL; CODE_8397A6 in HM-Decomp). Prints one
 * digit of the 24-bit number at $0192-$0194: the one worth 10^$018C. It
 * counts how many times that power fits (subtracting until the number
 * goes negative, then adding the last one back), leaves the rest in
 * $0192-$0194 and draws the digit. $019B bit 7 = "a digit was already
 * printed": while it is clear, zeros are skipped (no leading zeros).
 * $7E-$7F / $80 = the power, then $7E = the digit.
 * Leftovers if a zero is skipped: A = (high byte of $0192 : 0), X = 0,
 * C = 1 (CPX #0). Otherwise those of DrawLetter.
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_7A6_DIGIT   0x9821

void hm_PrintDigit(uint8_t *ram, HmRegs *r)
{
  uint8_t n = rd8(ram, r, 0x018C);
  uint16_t x = (uint8_t)(n * 3);                       /* 8-bit ASL; CLC; ADC */
  uint16_t power_lo = hm_bus_rd16(ram, POWERS_OF_TEN + x);
  hm_dp_w16(ram, r, 0x7E, power_lo);
  uint8_t power_hi = hm_bus_rd8(ram, POWERS_OF_TEN + (uint16_t)(x + 2));
  *hm_dp(ram, r, 0x80) = power_hi;
  uint32_t power = (uint32_t)power_hi << 16 | power_lo;

  uint32_t number = rd16(ram, r, 0x0192) | (uint32_t)rd8(ram, r, 0x0194) << 16;
  uint16_t digit = 0;
  for (;;) {
    number = (number - power) & 0xFFFFFF;
    if (number & 0x800000) break;                       /* BMI */
    digit++;
  }
  number = (number + power) & 0xFFFFFF;                 /* add the last one back */
  wr16(ram, r, 0x0192, (uint16_t)number);
  wr8(ram, r, 0x0194, (uint8_t)(number >> 16));

  uint8_t flags = rd8(ram, r, 0x019B);
  if (!(flags & 0x80)) {
    if (digit == 0) {                                   /* a leading zero */
      r->a = (uint16_t)(number & 0xFF00);
      r->x = 0;
      r->c = 1;
      return;
    }
    wr8(ram, r, 0x019B, flags | 0x80);
  }
  hm_dp_w16(ram, r, 0x7E, digit);
  r->a = (uint16_t)(LETTER_ZERO + digit);
  r->x = 0;
  jsl(ram, r, RET_7A6_DIGIT, hm_DrawLetter);
}

/* ═════════════════════════════════════════════════════════════════════════
 * PrintTextFrame ($83:951C, JSL, RTL; ADDDDFFFF in HM-Decomp): the text
 * printer, once per frame. The text is a list of 16-bit codes at [$01]
 * ($0187 = which one). A normal code is a letter: one every 5 frames
 * ($0189 counts 0-4), with a sound effect (3, 6, $0A) each. Special codes:
 *   $00A2  wait for a button: the cursor goes to VRAM $5528 and blinks
 *          (bit 3 of $019B)
 *   $00B1  a space: the cursor moves and the next code is read at once
 *   $FFFC n k  a number of n digits, variable k (Text_Number_Pointer_Table,
 *          $83:98EE: 3-byte address and size), one digit per frame
 *   $FFFD n k  a name of n letters (Text_Name_Pointer_Table, $83:9AAE),
 *          one letter every 5 frames, no sound
 *   $FFFE n  a choice of n options: the cursor goes to the place of option
 *          $018F (table $83:98CC) and blinks; $018E = n - 1 (bit 4)
 *   $FFFF  the end (bit 2)
 * $019B: bit 0 printing, bit 1 "a letter was printed this frame", bit 5
 * "waiting at the end of the box": only the arrow blinks (TextBoxArrow),
 * bit 7 "a digit was printed" (no more leading zeros).
 * Codes $BC-$C5 (digits) and $270 are "narrow" ($0190 = 0), others 1; the
 * width is ignored anyway (see DrawLetter).
 * Quirk: after a number or a name, $0187 += 2 is done in 8 bits (only the
 * low byte), so a text longer than 256 codes would jump back.
 * ═════════════════════════════════════════════════════════════════════════ */
#define TEXT_NUMBERS      0x8398EE
#define TEXT_NAMES        0x839AAE
#define CHOICE_PLACES     0x8398CC
#define CODE_WAIT         0x00A2
#define CODE_SPACE        0x00B1
#define CODE_NUMBER       0xFFFC
#define CODE_NAME         0xFFFD
#define CODE_CHOICE       0xFFFE
#define CODE_END          0xFFFF

static uint16_t text_word(uint8_t *ram, HmRegs *r, uint16_t y) { return hm_bus_rd16(ram, hm_dp_long(ram, r, 0x01, y)); }
static uint8_t text_byte(uint8_t *ram, HmRegs *r, uint16_t y) { return hm_bus_rd8(ram, hm_dp_long(ram, r, 0x01, y)); }
static void set_a8(HmRegs *r, uint8_t v) { r->a = (uint16_t)((r->a & 0xFF00) | v); }
static void set_flags_019B(uint8_t *ram, HmRegs *r, uint8_t bits)
{
  uint8_t v = (uint8_t)(rd8(ram, r, 0x019B) | bits);
  wr8(ram, r, 0x019B, v);
  set_a8(r, v);
}

/* 8395C9: unless a number / name is half-way ($018C), the next code; the
 * cursor moves; $0189 counts the frame. */
static void next_letter(uint8_t *ram, HmRegs *r)
{
  if (rd8(ram, r, 0x018C) == 0) {
    r->a = (uint16_t)(rd16(ram, r, 0x0187) + 1);
    wr16(ram, r, 0x0187, r->a);
  }
  r->a = r->x = rd8(ram, r, 0x0190);                  /* LDA #0; XBA; LDA $0190; TAX */
  jsr(ram, r, 0x95E6, hm_AdvanceLetterCursor);
  uint8_t frames = (uint8_t)(rd8(ram, r, 0x0189) + 1);   /* 8395E7 */
  wr8(ram, r, 0x0189, frames);
  set_a8(r, frames);
}
/* LDA $0187; INC; INC; STA $0187 in 8 bits */
static void skip_two_codes8(uint8_t *ram, HmRegs *r)
{
  uint8_t v = (uint8_t)(rd8(ram, r, 0x0187) + 2);
  wr8(ram, r, 0x0187, v);
  set_a8(r, v);
}

static void print_number(uint8_t *ram, HmRegs *r)
{
  if (rd8(ram, r, 0x018C) == 0) {                     /* first digit: copy the value */
    uint16_t y = (uint16_t)(rd16(ram, r, 0x0187) * 2 + 2);
    wr8(ram, r, 0x018C, text_byte(ram, r, y));       /* digits */
    y = (uint16_t)(y + 2);
    uint16_t x = (uint16_t)((text_word(ram, r, y) - 1) << 2);
    hm_dp_w16(ram, r, 0x72, hm_bus_rd16(ram, (TEXT_NUMBERS + x) & 0xFFFFFF));
    x = (uint16_t)(x + 2);
    *hm_dp(ram, r, 0x74) = hm_bus_rd8(ram, (TEXT_NUMBERS + x) & 0xFFFFFF);
    wr8(ram, r, 0x0192, 0); wr8(ram, r, 0x0193, 0); wr8(ram, r, 0x0194, 0);
    wr8(ram, r, 0x019B, rd8(ram, r, 0x019B) & 0x7F);
    x = (uint16_t)(x + 1);
    uint8_t size = hm_bus_rd8(ram, (TEXT_NUMBERS + x) & 0xFFFFFF);
    uint16_t i = 0;
    do {                                              /* PHA; LDA [$72],Y; STA $0192,X; ... DEC; BNE */
      hm_push8(ram, r, size);
      wr8(ram, r, (uint16_t)(0x0192 + i), hm_bus_rd8(ram, hm_dp_long(ram, r, 0x72, i)));
      i++;
      hm_pull8(ram, r);
    } while (--size);
    r->x = r->y = i;
  }
  wr8(ram, r, 0x018C, (uint8_t)(rd8(ram, r, 0x018C) - 1));
  set_a8(r, rd8(ram, r, 0x018C));
  jsl(ram, r, 0x9686, hm_PrintDigit);
  wr8(ram, r, 0x0190, 0);
  wr8(ram, r, 0x0189, 0);
  set_a8(r, rd8(ram, r, 0x018C));
  if (rd8(ram, r, 0x018C) == 0) skip_two_codes8(ram, r);
  next_letter(ram, r);
}

static void print_name(uint8_t *ram, HmRegs *r)
{
  if (rd8(ram, r, 0x018C) == 0) {
    uint16_t y = (uint16_t)(rd16(ram, r, 0x0187) * 2 + 2);
    uint8_t n = text_byte(ram, r, y);
    wr8(ram, r, 0x018C, n);
    wr8(ram, r, 0x018D, (uint8_t)(n - 1));
  }
  uint16_t y = (uint16_t)(rd16(ram, r, 0x0187) * 2 + 4);
  r->y = y;
  uint16_t x = (uint16_t)((text_word(ram, r, y) - 1) << 2);
  hm_dp_w16(ram, r, 0x72, hm_bus_rd16(ram, (TEXT_NAMES + x) & 0xFFFFFF));
  x = (uint16_t)(x + 2);
  *hm_dp(ram, r, 0x74) = hm_bus_rd8(ram, (TEXT_NAMES + x) & 0xFFFFFF);
  uint8_t left = (uint8_t)(rd8(ram, r, 0x018C) - 1);
  wr8(ram, r, 0x018C, left);
  uint8_t which = (uint8_t)(rd8(ram, r, 0x018D) - left);  /* SEC; SBC $018C */
  r->y = (uint16_t)(which << 1);
  r->a = hm_bus_rd16(ram, hm_dp_long(ram, r, 0x72, r->y));
  r->x = 1;
  jsl(ram, r, 0x96FC, hm_DrawLetter);
  wr8(ram, r, 0x0190, 1);
  wr8(ram, r, 0x0189, 0);
  set_a8(r, rd8(ram, r, 0x018C));
  if (rd8(ram, r, 0x018C) == 0) skip_two_codes8(ram, r);
  next_letter(ram, r);
}

void hm_PrintTextFrame(uint8_t *ram, HmRegs *r)
{
  uint8_t flags = rd8(ram, r, 0x019B);
  set_a8(r, flags & 0x20);                            /* AND #$20 */
  if (flags & 0x20) {                                 /* JMP CODE_839447 */
    jsr(ram, r, RET_447_BLINK, hm_BlinkTextBoxArrow);
    return;
  }
  if (!(flags & 0x01)) { set_a8(r, 0); return; }      /* AND #$01 */

  for (;;) {                                          /* 839534 */
    set_a8(r, rd8(ram, r, 0x019B) & 0xFD);
    wr8(ram, r, 0x019B, (uint8_t)r->a);
    uint16_t at = rd16(ram, r, 0x0187);
    r->y = (uint16_t)(at << 1);
    uint16_t code = text_word(ram, r, r->y);
    r->a = code;
    r->c = 1;                                         /* the CMP that matched */
    switch (code) {
    case CODE_WAIT:
      set_flags_019B(ram, r, 0x08);
      wr16(ram, r, 0x0185, 0x5528);
      r->a = CODE_WAIT;
      jsr(ram, r, 0x960A, hm_BlinkTextCursor);
      return;
    case CODE_SPACE:
      r->a = (uint16_t)(at + 1);
      wr16(ram, r, 0x0187, r->a);
      r->x = 1;
      jsr(ram, r, 0x961D, hm_AdvanceLetterCursor);
      continue;                                       /* JMP 839534 */
    case CODE_NUMBER:
      print_number(ram, r);
      return;
    case CODE_CHOICE: {
      set_flags_019B(ram, r, 0x10);
      uint8_t n = text_byte(ram, r, (uint16_t)(r->y + 2));
      wr8(ram, r, 0x018E, (uint8_t)(n - 1));
      r->y = (uint16_t)(r->y + 2);
      uint16_t x = (uint8_t)(rd8(ram, r, 0x018F) << 1);  /* 8-bit ASL */
      r->x = x;
      wr16(ram, r, 0x0185, hm_bus_rd16(ram, CHOICE_PLACES + x));
      r->a = 0x0275;
      jsr(ram, r, 0x974E, hm_BlinkTextCursor);
      return;
    }
    case CODE_END:
      set_flags_019B(ram, r, 0x04);
      return;
    }
    r->c = code >= CODE_END;                          /* CMP #$FFFF, not equal */

    uint8_t frames = rd8(ram, r, 0x0189);             /* 83956F */
    set_a8(r, frames);
    r->c = frames >= 4;
    if (frames != 4) {                                /* not yet: count the frame */
      wr8(ram, r, 0x0189, (uint8_t)(frames + 1));
      set_a8(r, (uint8_t)(frames + 1));
      return;
    }
    wr8(ram, r, 0x0189, 0);
    if (code == CODE_NAME) { print_name(ram, r); return; }

    uint8_t narrow = (code >= 0x00BC && code < 0x00C6) || code == 0x0270;
    r->x = (uint16_t)((r->x & 0xFF00) | (narrow ? 0 : 1));   /* SEP #$10; LDX */
    wr8(ram, r, 0x0190, (uint8_t)r->x);
    r->a = code;
    jsl(ram, r, 0x95AA, hm_DrawLetter);
    set_a8(r, 6);
    wr8(ram, r, 0x0114, 3);
    wr8(ram, r, 0x0115, 6);
    jsl(ram, r, 0x95BC, hm_UNK_Audio19);
    set_flags_019B(ram, r, 0x02);
    next_letter(ram, r);
    return;
  }
}
