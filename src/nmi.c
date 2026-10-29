/*
 * nmi.c — the interrupts and the waits for the next frame.
 *
 * Every frame the picture chip raises the NMI at the start of the vertical
 * blank. The CPU then pushes the bank, the address it was at and the flags,
 * sets I, clears decimal mode and runs the handler of the vector $FFEA in
 * bank $00 (NMI_Interrupt, $00:867B); the handler's RTI takes all three
 * back. The game's main loop and anything that waits a frame (the fades,
 * the sound commands) stop in WaitForNMI: clear $00 and loop until the
 * handler sets it again (UpdateGraphics does, at its end).
 *
 * In the recompiled game the wait is where the compiled code hands control
 * to the frame scheduler, so a live call never completes and the live
 * check skips it; the fuzzer runs these routines in the interpreter and,
 * when the original reaches the wait loop with $00 = 0, makes the NMI
 * happen right there (HM_WAIT in hm_decomp.h). hm_nmi below is the same on
 * the C side: the rewrites wait by running the handler at that point.
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

#define NMI_STATUS 0x00       /* DP: set by the NMI (UpdateGraphics) */
#define NMITIMEN   0x004200   /* NMI on, the line IRQ on, the pads read */
#define NMI_ON     0xA1

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
static void set_a8(HmRegs *r, uint8_t v) { r->a = (uint16_t)((r->a & 0xFF00) | v); }
/* RTI and PLP: the flags back; with 8-bit indexes their high bytes are 0 */
static void flags_back(HmRegs *r, uint8_t p)
{
  r->c = p & 1;
  r->n = (uint8_t)((p >> 7) & 1);
  if (p & 0x10) { r->x &= 0x00FF; r->y &= 0x00FF; }
}

/* ─────────────────────────────────────────────────────────────────────────
 * NMI_Interrupt ($80:867B, the NMI vector; RTI): saves A, X, Y (16-bit),
 * D and the data bank, keeps the NMI on ($4200 = $A1), lets the line IRQ
 * in (CLI) and runs UpdateGraphics (JSR) with the interrupted code's direct
 * page and data bank; then takes everything back and returns (RTI).
 * Inside, A = $00A1 (LDA #0; XBA; LDA #$A1) and the flags UpdateGraphics
 * saves are N and M set, the rest clear but V and C.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_NMI_Interrupt(uint8_t *ram, HmRegs *r)
{
  push16(ram, r, r->a);                                    /* REP #$30; PHA PHX PHY PHD PHB */
  push16(ram, r, r->x);
  push16(ram, r, r->y);
  push16(ram, r, r->d);
  hm_push8(ram, r, r->db);
  r->a = 0x00A1;
  hm_bus_wr8(ram, NMITIMEN, NMI_ON);
  uint8_t p_in = r->p;                                     /* CLI */
  r->p = (uint8_t)((p_in & 0x41) | 0xA0);
  r->n = 1;
  hm_push8(ram, r, 0x86); hm_push8(ram, r, 0x90);          /* JSR UpdateGraphics at $868E */
  hm_UpdateGraphics(ram, r);
  hm_pull8(ram, r); hm_pull8(ram, r);
  r->p = p_in;
  r->db = hm_pull8(ram, r);                                /* REP #$30; PLB PLD PLY PLX PLA */
  r->d = pull16(ram, r);
  r->y = pull16(ram, r);
  r->x = pull16(ram, r);
  r->a = pull16(ram, r);
  uint8_t p = hm_pull8(ram, r);                            /* RTI */
  hm_pull8(ram, r);
  hm_pull8(ram, r);
  r->pb = hm_pull8(ram, r);
  flags_back(r, p);
}

/* ─────────────────────────────────────────────────────────────────────────
 * COP_Interrupt ($80:8699, the IRQ vector despite HM-Decomp's name; RTI):
 * the line IRQ. Saves the data bank, A, X, Y, reads TIMEUP ($4211, which
 * lets the IRQ go) and calls a routine that only returns (COP_Return, JSR);
 * then takes them back. The COP and BRK vectors point at the RTI at the end
 * of NMI_Interrupt ($80:8698).
 * ───────────────────────────────────────────────────────────────────────── */
void hm_COP_Interrupt(uint8_t *ram, HmRegs *r)
{
  hm_push8(ram, r, r->db);                                 /* REP #$30; PHB PHA PHX PHY */
  push16(ram, r, r->a);
  push16(ram, r, r->x);
  push16(ram, r, r->y);
  r->a = (uint16_t)(r->a >> 8);                            /* LDA #0; XBA */
  set_a8(r, hm_bus_rd8(ram, (uint32_t)r->db << 16 | 0x4211));
  hm_push8(ram, r, 0x86); hm_push8(ram, r, 0xA9);          /* JSR COP_Return at $86A7 */
  hm_pull8(ram, r); hm_pull8(ram, r);
  r->y = pull16(ram, r);                                   /* REP #$30; PLY PLX PLA PLB */
  r->x = pull16(ram, r);
  r->a = pull16(ram, r);
  r->db = hm_pull8(ram, r);
  uint8_t p = hm_pull8(ram, r);                            /* RTI */
  hm_pull8(ram, r);
  hm_pull8(ram, r);
  r->pb = hm_pull8(ram, r);
  flags_back(r, p);
}

uint32_t g_hm_next;
void (*g_hm_frame_hook)(uint8_t *ram);

/* The NMI arriving at `pc` with the flags `p` (hm_decomp.h). */
void hm_nmi(uint8_t *ram, HmRegs *r, uint16_t pc, uint8_t p)
{
  if (g_hm_frame_hook) g_hm_frame_hook(ram);
  uint8_t keep = r->p;
  hm_push8(ram, r, r->pb);
  hm_push8(ram, r, (uint8_t)(pc >> 8));
  hm_push8(ram, r, (uint8_t)pc);
  hm_push8(ram, r, p);
  r->pb = 0x00;
  r->p = (uint8_t)((p | 0x04) & ~0x08);
  hm_NMI_Interrupt(ram, r);
  r->p = keep;
}

/* ─────────────────────────────────────────────────────────────────────────
 * WaitForNMI ($80:8645, JSL, RTL): waits for the next frame. Saves the
 * flags and A, turns the NMI on ($4200 = $A1), lets interrupts in (CLI),
 * clears $00 and loops (LDA $00 at $80:8654) until the NMI has set it.
 * Leftovers: A and the flags as they were (PHA / PLA, PHP / PLP).
 * ───────────────────────────────────────────────────────────────────────── */
void hm_WaitForNMI(uint8_t *ram, HmRegs *r)
{
  uint8_t p = r->p;
  hm_push8(ram, r, p);                                     /* PHP */
  push16(ram, r, r->a);                                    /* REP #$20; PHA */
  set_a8(r, NMI_ON);                                       /* SEP #$20; LDA #$A1 */
  hm_bus_wr8(ram, NMITIMEN, NMI_ON);
  *hm_dp(ram, r, NMI_STATUS) = 0;                          /* CLI; STZ $00 */
  hm_nmi(ram, r, 0x8654, (uint8_t)((p & ~0x06) | 0xA0));
  set_a8(r, *hm_dp(ram, r, NMI_STATUS));                   /* LDA $00: set */
  r->a = pull16(ram, r);                                   /* REP #$20; PLA; PLP */
  flags_back(r, hm_pull8(ram, r));
}

/* ─────────────────────────────────────────────────────────────────────────
 * WaitForNMIATimes ($80:865D, JSL, RTL): the same A times (16-bit; 0 would
 * mean 65536). From the second wait on, the carry is 1 (CMP #0).
 * Leftovers: A = 0, the flags as they were (PHP / PLP).
 * ───────────────────────────────────────────────────────────────────────── */
void hm_WaitForNMIATimes(uint8_t *ram, HmRegs *r)
{
  uint8_t p = r->p, c = p & 1;
  hm_push8(ram, r, p);                                     /* PHP */
  do {
    push16(ram, r, r->a);                                  /* REP #$20; PHA */
    set_a8(r, NMI_ON);
    hm_bus_wr8(ram, NMITIMEN, NMI_ON);
    *hm_dp(ram, r, NMI_STATUS) = 0;
    hm_nmi(ram, r, 0x866C, (uint8_t)((p & ~0x07) | 0xA0 | c));
    set_a8(r, *hm_dp(ram, r, NMI_STATUS));
    r->a = (uint16_t)(pull16(ram, r) - 1);                 /* REP #$20; PLA; DEC A */
    r->c = c = 1;                                          /* CMP #0 */
  } while (r->a);
  flags_back(r, hm_pull8(ram, r));                         /* PLP */
}

/* ─────────────────────────────────────────────────────────────────────────
 * ScreenFadein ($80:87CE) and ScreenFadeout ($80:880A) (JSL, RTL): the
 * brightness goes, one level at a time, from $92 (or, if $92 = $FF, from
 * where it is, $25) to $94, up or down, $93 frames per level
 * (SetsBrightness, then WaitForNMI $93 times; $26 counts them). At the end
 * bit 15 of $7F1F5A is set (faded in) or cleared (faded out).
 * The flags are followed exactly (`p`), since SetsBrightness and
 * WaitForNMI save them (PHP) and the NMI pushes them.
 * Leftovers: A = the new $7F1F5A (16-bit), C = 1, X and Y 8-bit.
 * ───────────────────────────────────────────────────────────────────────── */
#define FADE_BRIGHTNESS 0x25   /* DP */
#define FADE_FRAME      0x26   /* DP */
#define FLAGS_5A        0x7F1F5A

static uint8_t nz8(uint8_t p, uint8_t v) { return (uint8_t)((p & ~0x82) | (v & 0x80) | (v ? 0 : 0x02)); }
static uint8_t lda8(HmRegs *r, uint8_t p, uint8_t v) { set_a8(r, v); return nz8(p, v); }
static uint8_t cmp8(HmRegs *r, uint8_t p, uint8_t a, uint8_t m)
{
  r->c = a >= m;
  return (uint8_t)((nz8(p, (uint8_t)(a - m)) & ~0x01) | r->c);
}
static void call_with_flags(uint8_t *ram, HmRegs *r, uint8_t p, uint16_t at,
                            void (*fn)(uint8_t *, HmRegs *))
{
  r->p = p;
  hm_jsl(ram, r, (uint16_t)(at + 3), 0x80);
  fn(ram, r);
  hm_rtl(ram, r);
}

static void fade(uint8_t *ram, HmRegs *r, int up)
{
  uint8_t entry = r->p;
  uint8_t p = (uint8_t)(r->p | 0x30);                      /* SEP #$30 */
  r->x &= 0x00FF; r->y &= 0x00FF;
  uint16_t at = up ? 0x87CE : 0x880A;                      /* the routine's start */
  p = lda8(r, p, *hm_dp(ram, r, 0x92));
  p = cmp8(r, p, (uint8_t)r->a, 0xFF);
  if ((uint8_t)r->a != 0xFF) {
    p = lda8(r, p, *hm_dp(ram, r, 0x92));
    *hm_dp(ram, r, FADE_BRIGHTNESS) = (uint8_t)r->a;
  }
  p = lda8(r, p, *hm_dp(ram, r, 0x93));
  *hm_dp(ram, r, FADE_FRAME) = (uint8_t)r->a;
  for (;;) {
    p = lda8(r, p, *hm_dp(ram, r, FADE_BRIGHTNESS));
    call_with_flags(ram, r, p, (uint16_t)(at + 0x12), hm_SetsBrightness);
    p = lda8(r, p, *hm_dp(ram, r, FADE_BRIGHTNESS));
    p = cmp8(r, p, (uint8_t)r->a, *hm_dp(ram, r, 0x94));
    if ((uint8_t)r->a == *hm_dp(ram, r, 0x94)) break;
    uint8_t b = (uint8_t)(*hm_dp(ram, r, FADE_BRIGHTNESS) + (up ? 1 : -1));
    *hm_dp(ram, r, FADE_BRIGHTNESS) = b;                   /* INC / DEC $25 */
    p = nz8(p, b);
    do {
      call_with_flags(ram, r, p, (uint16_t)(at + 0x1E), hm_WaitForNMI);
      uint8_t f = (uint8_t)(*hm_dp(ram, r, FADE_FRAME) - 1);
      *hm_dp(ram, r, FADE_FRAME) = f;                      /* DEC $26; LDA $26 */
      p = lda8(r, p, f);
    } while (*hm_dp(ram, r, FADE_FRAME));
    p = lda8(r, p, *hm_dp(ram, r, 0x93));
    *hm_dp(ram, r, FADE_FRAME) = (uint8_t)r->a;
  }
  uint16_t v = hm_bus_rd16(ram, FLAGS_5A);                 /* REP #$20 */
  v = up ? (uint16_t)(v | 0x8000) : (uint16_t)(v & 0x7FFF);
  r->a = v;
  hm_bus_wr16(ram, FLAGS_5A, v);
  r->n = (uint8_t)(v >> 15);
  r->p = entry;
}
void hm_ScreenFadein(uint8_t *ram, HmRegs *r) { fade(ram, r, 1); }
void hm_ScreenFadeout(uint8_t *ram, HmRegs *r) { fade(ram, r, 0); }
