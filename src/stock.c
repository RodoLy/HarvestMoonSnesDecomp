/*
 * stock.c — the farm's stockpiles and the player's happiness.
 *
 * AddWood ($83:B224), AddGrass ($83:B253) and AddPlayerHappiness ($83:B282)
 * are the same routine on three counters: add A (signed, 16-bit) to the
 * counter, refuse if the result would be negative, cap it at 999.
 *
 *   returns A = 0  done (the counter changed, maybe capped at 999)
 *           A = 1  refused: the result would be negative (nothing changes)
 *
 * The amount is also left in the scratch variable $7E, as the original does.
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

#define STOCK_MAX 999

static void add_capped(uint8_t *ram, HmRegs *r, uint32_t counter)
{
  uint16_t change = r->a;
  hm_dp_w16(ram, r, 0x7E, change);
  unsigned sum = hm_rd16(ram, counter) + change;   /* CLC; ADC */
  uint16_t value = (uint16_t)sum;
  r->c = sum > 0xFFFF;
  if (value & 0x8000) {          /* negative: refuse */
    r->a = 1;
  } else if (value >= STOCK_MAX) {
    hm_wr16(ram, counter, STOCK_MAX);
    r->a = 0;
    r->c = 1;                    /* from CMP #999 */
  } else {
    hm_wr16(ram, counter, value);
    r->a = 0;
    r->c = 0;
  }
}

void hm_AddWood(uint8_t *ram, HmRegs *r)            { add_capped(ram, r, HM_STORED_WOOD); }
void hm_AddGrass(uint8_t *ram, HmRegs *r)           { add_capped(ram, r, HM_STORED_GRASS); }
void hm_AddPlayerHappiness(uint8_t *ram, HmRegs *r) { add_capped(ram, r, HM_HAPPINESS); }
