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

/* ─────────────────────────────────────────────────────────────────────────
 * AddMoney ($83:B1C9, JSL, RTL). Adds the signed 24-bit amount in $72-$74
 * to the money ($7F1F04, 24-bit; the game shows it with an extra 0, so
 * this is tens of G). Returns A = 1 and changes nothing if the result
 * would be negative; otherwise caps it at 999999 and returns A = 0.
 * The sum is left in $75-$77. C = 1 when capped, 0 when it fit, the
 * add's carry when negative.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_AddMoney(uint8_t *ram, HmRegs *r)
{
  uint32_t money = hm_rd24(ram, HM_MONEY);
  uint32_t change = hm_dp_r16(ram, r, 0x72) | (uint32_t)*hm_dp(ram, r, 0x74) << 16;
  uint32_t sum = money + change;                 /* 16-bit ADC, then 8-bit ADC */
  uint32_t total = sum & 0xFFFFFF;
  hm_dp_w16(ram, r, 0x75, (uint16_t)total);
  *hm_dp(ram, r, 0x77) = (uint8_t)(total >> 16);
  if (total & 0x800000) {                        /* BMI on the top byte */
    r->c = sum > 0xFFFFFF;
    r->a = 1;
    return;
  }
  if (total >= HM_MONEY_MAX) {
    hm_wr24(ram, HM_MONEY, HM_MONEY_MAX);
    r->c = 1;
  } else {
    hm_wr24(ram, HM_MONEY, total);
    r->c = 0;
  }
  r->a = 0;
}
