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
