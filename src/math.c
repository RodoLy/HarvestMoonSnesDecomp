/*
 * math.c — arithmetic helpers.
 *
 * DivisionUnsigned ($83:8082, JSL, RTL). Divides the 16-bit dividend in $7E
 * by the 16-bit divisor in $80 (direct page). Returns the quotient in A and
 * leaves the remainder in $7E. Always exits with 16-bit A/X/Y, Y = 0, C = 0.
 *
 * Two ways to divide:
 *  - Divisor below 255: the SNES hardware divider ($4204/$4205 dividend,
 *    $4206 divisor, quotient in $4214, remainder in $4216). Only the low
 *    byte of the divisor reaches the hardware. The routine does not check
 *    for 0: the hardware then answers quotient $FFFF, remainder = dividend.
 *  - Divisor 255 and up: long division by hand, 16 rounds of "shift one
 *    bit of the dividend into the remainder; if it fits, subtract and set a
 *    quotient bit". The quotient is built in $82.
 *    Quirk: the running remainder is only 16 bits, so with a divisor above
 *    $8000 its top bit can fall off (ROL) and the result is wrong. Nobody
 *    in the game divides by such numbers; the rewrite copies it anyway.
 *
 * The hand path saves Y on the stack (PHY, Y = 0 there), so two zero bytes
 * are left just below the stack pointer.
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

#define DIVIDEND   0x7E   /* direct page; the remainder ends up here too */
#define DIVISOR    0x80
#define QUOTIENT   0x82   /* only used (and written) by the hand path */

void hm_DivisionUnsigned(uint8_t *ram, HmRegs *r)
{
  uint16_t dividend = hm_dp_r16(ram, r, DIVIDEND);
  uint16_t divisor = hm_dp_r16(ram, r, DIVISOR);
  r->y = 0;

  if (divisor < 0xFF) {
    /* Hardware divider: 16-bit dividend / 8-bit divisor. */
    uint8_t d = (uint8_t)divisor;
    uint16_t quotient = d ? (uint16_t)(dividend / d) : 0xFFFF;
    uint16_t remainder = d ? (uint16_t)(dividend % d) : dividend;
    hm_dp_w16(ram, r, DIVIDEND, remainder);
    r->a = quotient;
  } else {
    hm_push8(ram, r, 0);                 /* PHY (Y = 0): high byte, then low */
    hm_push8(ram, r, 0);
    uint16_t quotient = 0, remainder = 0, bits = dividend;
    for (int i = 0; i < 16; i++) {
      quotient = (uint16_t)(quotient << 1);
      remainder = (uint16_t)(remainder << 1 | bits >> 15);   /* top bit may fall off */
      bits = (uint16_t)(bits << 1);
      if (remainder >= divisor) {
        remainder = (uint16_t)(remainder - divisor);
        quotient++;
      }
    }
    hm_dp_w16(ram, r, QUOTIENT, quotient);
    hm_dp_w16(ram, r, DIVIDEND, remainder);
    hm_pull8(ram, r);                    /* PLA */
    hm_pull8(ram, r);
    r->a = quotient;
  }
  r->c = 0;                              /* TYA / PLA of 0, then LSR */
}

/* ─────────────────────────────────────────────────────────────────────────
 * DivisionSigned ($83:80D0, JSL, RTL): the same with signs. Dividend and
 * divisor are made positive first (Y counts how many were negative), the
 * division is as above, and the quotient is negated when Y is odd. The
 * remainder in $7E stays positive. Leftovers: Y = that count on the
 * hardware path, 0 on the hand path (its loop counter); C = Y & 1.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_DivisionSigned(uint8_t *ram, HmRegs *r)
{
  uint16_t negatives = 0;
  uint16_t dividend = hm_dp_r16(ram, r, DIVIDEND);
  if (dividend & 0x8000) {
    dividend = (uint16_t)(~dividend + 1);
    hm_dp_w16(ram, r, DIVIDEND, dividend);
    negatives++;
  }
  uint16_t divisor = hm_dp_r16(ram, r, DIVISOR);
  if (divisor & 0x8000) {
    divisor = (uint16_t)(~divisor + 1);
    hm_dp_w16(ram, r, DIVISOR, divisor);
    negatives++;
  }
  uint16_t quotient;
  if (divisor < 0xFF) {
    uint8_t d = (uint8_t)divisor;
    quotient = d ? (uint16_t)(dividend / d) : 0xFFFF;
    hm_dp_w16(ram, r, DIVIDEND, d ? (uint16_t)(dividend % d) : dividend);
    r->y = negatives;
  } else {
    hm_push8(ram, r, (uint8_t)(negatives >> 8));        /* PHY */
    hm_push8(ram, r, (uint8_t)negatives);
    uint16_t remainder = 0, bits = dividend;
    quotient = 0;
    for (int i = 0; i < 16; i++) {
      quotient = (uint16_t)(quotient << 1);
      remainder = (uint16_t)(remainder << 1 | bits >> 15);
      bits = (uint16_t)(bits << 1);
      if (remainder >= divisor) {
        remainder = (uint16_t)(remainder - divisor);
        quotient++;
      }
    }
    hm_dp_w16(ram, r, QUOTIENT, quotient);
    hm_dp_w16(ram, r, DIVIDEND, remainder);
    hm_pull8(ram, r);                                   /* PLA */
    hm_pull8(ram, r);
    r->y = 0;
  }
  r->c = negatives & 1;                                 /* TYA / PLA; LSR */
  r->a = r->c ? (uint16_t)(~quotient + 1) : quotient;
}

/* ─────────────────────────────────────────────────────────────────────────
 * MultiplicationUnsigned ($83:8067, JSR, RTS): A = $011A x $011B (two
 * bytes, a 16-bit product), with the console's multiplier ($4202/$4203,
 * the answer in $4216 after a few cycles: the four NOPs). Nothing else
 * changes.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_MultiplicationUnsigned(uint8_t *ram, HmRegs *r)
{
  uint8_t a = hm_bus_rd8(ram, (uint32_t)r->db << 16 | 0x011A);
  uint8_t b = hm_bus_rd8(ram, (uint32_t)r->db << 16 | 0x011B);
  r->a = (uint16_t)(a * b);
}
