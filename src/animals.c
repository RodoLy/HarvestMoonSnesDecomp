/*
 * animals.c — where the barn and the coop live in RAM.
 *
 * GetsCowPointer ($83:C9A7) and GetChickenPointer ($83:C995): A = slot,
 * returns the long pointer $7E:xxxx to that animal's record in $72-$74.
 * Cows are 16-byte records at $7E:C1C6, chickens 8-byte records at $7E:C286.
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

static void slot_pointer(uint8_t *ram, HmRegs *r, uint16_t base, unsigned shift)
{
  uint16_t ptr = (uint16_t)((uint16_t)(r->a << shift) + base);
  hm_dp_w16(ram, r, 0x72, ptr);
  *hm_dp(ram, r, 0x74) = 0x7E;
  r->a = (uint16_t)((ptr & 0xFF00) | 0x7E);   /* SEP #$20; LDA #$7E keeps B */
}

void hm_GetsCowPointer(uint8_t *ram, HmRegs *r)    { slot_pointer(ram, r, HM_COW_DATA & 0xFFFF, 4); }
void hm_GetChickenPointer(uint8_t *ram, HmRegs *r) { slot_pointer(ram, r, HM_CHICKEN_DATA & 0xFFFF, 3); }
