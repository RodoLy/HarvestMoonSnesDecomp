/*
 * rng.c — the random number generator.
 *
 * GetRNG ($83:8138, JSL, RTL). Harvest Moon keeps 24 bits of state in
 * $7E:0100-0102 (HM_RNG_STATE). Each call shifts the state right by one as
 * a 24-bit LFSR whose feedback bit is bit 1 of (state[1] XOR state[0]), then
 * mixes the three bytes into a new state[0], which is also the result
 * (0..255, returned in A with the high byte cleared).
 *
 * Everything that is "random" in the game goes through here: weather,
 * animal moods, which items grow, what villagers say... Event scripts keep a
 * copy of the last value in $00DE.
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

/* ROR through carry, 8-bit, as the 65816 does it. */
static uint8_t ror8(uint8_t v, uint8_t *carry)
{
  uint8_t out = (uint8_t)((v >> 1) | (*carry << 7));
  *carry = v & 1;
  return out;
}

void hm_GetRNG(uint8_t *ram, HmRegs *r)
{
  uint8_t *s0 = &ram[HM_RNG_STATE + 0], *s1 = &ram[HM_RNG_STATE + 1],
          *s2 = &ram[HM_RNG_STATE + 2];

  /* Step the LFSR: shift [s1 s0 s2] right, feeding in bit 1 of s1^s0. */
  uint8_t c = ((*s1 ^ *s0) & 0x02) != 0;
  *s1 = ror8(*s1, &c);
  *s0 = ror8(*s0, &c);
  *s2 = ror8(*s2, &c);

  /* Mix: ((s1 + 0x47) rotated right twice through carry) ^ s0, + s2 + C. */
  unsigned sum = *s1 + 0x47u;
  uint8_t a = (uint8_t)sum;
  c = sum > 0xFF;
  a = ror8(a, &c);
  a = ror8(a, &c);
  a ^= *s0;
  sum = a + *s2 + c;
  a = (uint8_t)sum;
  c = sum > 0xFF;

  *s0 = a;
  r->a = a;          /* REP #$20; AND #$00FF */
  r->c = c;
}

/*
 * RNGReturn0toA ($80:89F9, JSL, RTL). "Roll a die with n faces": A = n
 * (8-bit) in, a number 0..n-1 out in A and X. The game uses it as
 * "1 chance in n" by testing the result for 0 (weather, and much more).
 *
 * How it works: step = 255 / n (DivisionUnsigned), r = GetRNG() (0..255).
 * The result is how many multiples of `step` are still below r:
 * 0 if r <= step, 1 if r <= 2*step, ... capped at n-1.
 *
 * So it is NOT perfectly fair: with n = 6, step = 42 and 0 comes out for
 * r = 0..42 (43 of 256 values) while 5 takes everything above 210 (45).
 * Edge cases: n = 1 always gives 0; n = 0 divides by zero (the hardware
 * answers $FFFF, step = $FF) and also always gives 0.
 *
 * Leftovers the checker compares: $92 = n-1, $93 = step, $94 = r, $7E/$80
 * from the division, the stack bytes (PHA of n and the two JSL frames),
 * 8-bit A/X/Y on exit, Y = 0 and C = 1 (both loop exits compare "≥").
 */
#define RNG_RET_DIVISION  0x8A0E   /* JSL DivisionUnsigned at $80:8A0B */
#define RNG_RET_GETRNG    0x8A16   /* JSL GetRNG at $80:8A13 */

void hm_RNGReturn0toA(uint8_t *ram, HmRegs *r)
{
  uint8_t faces = (uint8_t)r->a;
  r->x &= 0xFF; r->y &= 0xFF;                    /* SEP #$30 */
  *hm_dp(ram, r, 0x92) = faces;
  hm_push8(ram, r, faces);                       /* PHA */
  *hm_dp(ram, r, 0x93) = 0;

  /* step = 255 / faces */
  hm_dp_w16(ram, r, 0x7E, 0x00FF);
  hm_dp_w16(ram, r, 0x80, faces);
  hm_jsl(ram, r, RNG_RET_DIVISION, 0x83);
  hm_DivisionUnsigned(ram, r);
  hm_rtl(ram, r);
  uint8_t step = (uint8_t)r->a;
  *hm_dp(ram, r, 0x93) = step;

  hm_jsl(ram, r, RNG_RET_GETRNG, 0x83);
  hm_GetRNG(ram, r);
  hm_rtl(ram, r);
  r->x &= 0xFF; r->y &= 0xFF;                    /* SEP #$30 */
  uint8_t roll = (uint8_t)r->a;
  *hm_dp(ram, r, 0x94) = roll;

  uint8_t last = (uint8_t)(hm_pull8(ram, r) - 1);  /* PLA; DEC */
  *hm_dp(ram, r, 0x92) = last;

  uint8_t result = 0, edge = step;
  while (edge < roll && ++result != last)
    edge = (uint8_t)(edge + step);

  r->x = result;
  r->a = result;                                 /* TXA; high byte is 0 */
  r->c = 1;
}
