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

/* ─────────────────────────────────────────────────────────────────────────
 * AddCowHappiness ($84:A5D4, JSL, RTL; a local label inside the event
 * command Event_UNKCowRelated in HM-Decomp). $72-$74 = the cow's record
 * (GetsCowPointer), A = signed change. Byte +4 of the record is the cow's
 * happiness, kept in 0..255.
 * Leftovers: $7E = change, $80 = old happiness, Y = 4. A = old + change
 * with its low byte replaced by the stored value when it was clamped;
 * C = 0 when it fits, 1 when it went over 255, the add's carry when it went
 * below 0.
 * CowFeedingandStatus calls it with -8, -16, -24 (not fed), -12 (gets
 * sick) and -30.
 * ───────────────────────────────────────────────────────────────────────── */
#define COW_HAPPINESS 4

void hm_AddCowHappiness(uint8_t *ram, HmRegs *r)
{
  uint16_t change = r->a;
  hm_dp_w16(ram, r, 0x7E, change);
  r->y = COW_HAPPINESS;
  uint32_t at = hm_dp_long(ram, r, 0x72, COW_HAPPINESS);
  uint16_t old = hm_bus_rd8(ram, at);
  hm_dp_w16(ram, r, 0x80, old);
  uint32_t sum = (uint32_t)old + change;
  uint16_t a = (uint16_t)sum;
  if (a & 0x8000) {                     /* below 0 */
    hm_bus_wr8(ram, at, 0);
    r->a = a & 0xFF00;
    r->c = sum > 0xFFFF;
  } else if (a >= 256) {                /* over 255 */
    hm_bus_wr8(ram, at, 255);
    r->a = (uint16_t)((a & 0xFF00) | 0xFF);
    r->c = 1;
  } else {
    hm_bus_wr8(ram, at, (uint8_t)a);
    r->a = a;
    r->c = 0;
  }
}
