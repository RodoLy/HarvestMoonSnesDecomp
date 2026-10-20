/*
 * stamina.c — the player's energy.
 *
 * ChangeStamina ($81:D061, JSL). A = signed 8-bit change (eating lunch is
 * +20, every tool use costs some). Max and current energy are 8-bit
 * (HM_MAX_STAMINA, HM_CURRENT_STAMINA).
 *
 *  - While $7F1F60 bit 3 is set, energy does not change at all.
 *  - Quirk: the 16-bit sign extension of the change ($83) comes from the N
 *    flag the CALLER left (BMI right after two STAs), not from A. Every
 *    caller loads A just before the JSL, so in practice they agree.
 *  - Going to 0 or below: energy 0, game-state bit 3 ("exhausted") set and
 *    the player collapses (animation $4D, action $0B).
 *  - Reaching the maximum: energy = max, bit 3 cleared.
 *  - Otherwise energy += change, bit 3 cleared.
 *
 * Tiredness (HM_EXHAUSTION, 0..3) then follows energy. After a gain it is
 * recomputed: the number of times max can be halved before reaching current
 * energy or less. After a loss it only goes up, one stage at a time: when
 * energy drops to max >> (stage + 1) or below, the stage advances and the
 * player plays the matching tired animation ($4A + stage).
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

#define STAMINA_FROZEN_FLAGS  0x11F60   /* $7F1F60 */
#define EXHAUSTED             0x0008    /* HM_GAME_STATE bit */
#define ACTION_TIRED          0x000B
#define ANIM_COLLAPSE         0x004D
#define ANIM_TIRED_BASE       0x4A

void hm_ChangeStamina(uint8_t *ram, HmRegs *r)
{
  int8_t change = (int8_t)r->a;
  /* Scratch the original leaves behind: $92 = change, $82/$83 = change
   * sign-extended to 16 bits. */
  *hm_dp(ram, r, 0x92) = (uint8_t)change;
  *hm_dp(ram, r, 0x82) = (uint8_t)change;
  *hm_dp(ram, r, 0x83) = r->n ? 0xFF : 0x00;
  if (hm_rd16(ram, STAMINA_FROZEN_FLAGS) & 0x0008)
    return;

  uint8_t max = ram[HM_MAX_STAMINA];
  uint16_t after = (uint16_t)(ram[HM_CURRENT_STAMINA] + hm_dp_r16(ram, r, 0x82));
  hm_dp_w16(ram, r, 0x7E, max);
  hm_dp_w16(ram, r, 0x80, after);

  uint16_t state = hm_rd16(ram, HM_GAME_STATE);
  if (after == 0 || (after & 0x8000)) {
    ram[HM_CURRENT_STAMINA] = 0;
    hm_wr16(ram, HM_GAME_STATE, state | EXHAUSTED);
    hm_wr16(ram, HM_ANIM_REQUEST, ANIM_COLLAPSE);
    hm_wr16(ram, HM_PLAYER_ACTION, ACTION_TIRED);
  } else if (after >= max) {
    ram[HM_CURRENT_STAMINA] = max;
    hm_wr16(ram, HM_GAME_STATE, state & ~EXHAUSTED);
  } else {
    hm_wr16(ram, HM_GAME_STATE, state & ~EXHAUSTED);
    ram[HM_CURRENT_STAMINA] = (uint8_t)(ram[HM_CURRENT_STAMINA] + change);
  }

  uint8_t cur = ram[HM_CURRENT_STAMINA];
  uint8_t stage = ram[HM_EXHAUSTION];
  if (change >= 0) {
    /* stage = how many halvings of max stay above current energy */
    uint8_t level = 0, a = max;
    for (;;) {
      a >>= 1;
      if (a <= cur) break;
      level++;
    }
    ram[HM_EXHAUSTION] = level;
  } else if (stage != 3) {
    uint8_t threshold = (uint8_t)(max >> (stage + 1));
    if (threshold >= cur) {
      hm_wr16(ram, HM_ANIM_REQUEST, (uint16_t)(stage + ANIM_TIRED_BASE));
      ram[HM_EXHAUSTION] = (uint8_t)(stage + 1);
      hm_wr16(ram, HM_PLAYER_ACTION, ACTION_TIRED);
    }
  }
}
