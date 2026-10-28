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
 *
 * Leftovers: frozen: A = 8 and X, Y, C untouched. Otherwise, after a gain A = Y = the new stage, C from the last halving
 * compared; after a loss A = max >> (stage + 1) (stage 3: A = 3, C = 1),
 * Y = stage, or A = $000B, C = 0 when the stage advances. The high byte
 * of A is whatever the energy update left (the game state's, or 0).
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
   * sign-extended to 16 bits (with the caller's N, see above). */
  *hm_dp(ram, r, 0x92) = (uint8_t)change;
  *hm_dp(ram, r, 0x82) = (uint8_t)change;
  *hm_dp(ram, r, 0x83) = r->n ? 0xFF : 0x00;
  if (hm_rd16(ram, STAMINA_FROZEN_FLAGS) & 0x0008) {
    r->a = 0x0008;                       /* LDA flags; AND #8: X, Y, C untouched */
    return;
  }

  uint8_t max = ram[HM_MAX_STAMINA];
  uint16_t after = (uint16_t)(ram[HM_CURRENT_STAMINA] + hm_dp_r16(ram, r, 0x82));
  hm_dp_w16(ram, r, 0x7E, max);
  hm_dp_w16(ram, r, 0x80, after);

  /* A after the energy update (only its high byte survives below) */
  uint16_t a;
  uint16_t state = hm_rd16(ram, HM_GAME_STATE);
  if (after == 0 || (after & 0x8000)) {
    ram[HM_CURRENT_STAMINA] = 0;
    hm_wr16(ram, HM_GAME_STATE, state | EXHAUSTED);
    hm_wr16(ram, HM_ANIM_REQUEST, ANIM_COLLAPSE);
    hm_wr16(ram, HM_PLAYER_ACTION, ACTION_TIRED);
    a = ACTION_TIRED;
  } else if (after >= max) {
    ram[HM_CURRENT_STAMINA] = max;
    hm_wr16(ram, HM_GAME_STATE, a = (uint16_t)(state & ~EXHAUSTED));
  } else {
    hm_wr16(ram, HM_GAME_STATE, (uint16_t)(state & ~EXHAUSTED));
    ram[HM_CURRENT_STAMINA] = (uint8_t)(ram[HM_CURRENT_STAMINA] + change);
    a = (uint16_t)((state & ~EXHAUSTED & 0xFF00) | ram[HM_CURRENT_STAMINA]);
  }
  /* SEP #$30. On the real SNES this also clears the high bytes of X and
   * Y; the recompilation (our reference) keeps them, so X comes back with
   * its high byte (Y is reloaded with LDY #0, which clears it, except at
   * stage 3). */
  uint16_t high = a & 0xFF00;

  uint8_t cur = ram[HM_CURRENT_STAMINA];
  uint8_t stage = ram[HM_EXHAUSTION];
  if (change >= 0) {
    /* stage = how many halvings of max stay above current energy */
    uint8_t level = 0, half = max;
    for (;;) {
      half >>= 1;
      r->c = half >= cur;                /* CMP current */
      if (half <= cur) break;
      level++;
    }
    ram[HM_EXHAUSTION] = level;
    r->y = level;
    r->a = (uint16_t)(high | level);     /* TYA */
  } else if (stage == 3) {
    r->c = 1;                            /* CMP #3 */
    r->a = (uint16_t)(high | 3);
  } else {
    uint8_t threshold = (uint8_t)(max >> (stage + 1));
    r->y = stage;
    r->c = threshold >= cur;
    r->a = (uint16_t)(high | threshold);
    if (threshold >= cur) {
      hm_wr16(ram, HM_ANIM_REQUEST, (uint16_t)(stage + ANIM_TIRED_BASE));
      ram[HM_EXHAUSTION] = (uint8_t)(stage + 1);
      hm_wr16(ram, HM_PLAYER_ACTION, ACTION_TIRED);
      r->a = ACTION_TIRED;
      r->c = 0;                          /* the ADC #$4A */
    }
  }
}

/* ─────────────────────────────────────────────────────────────────────────
 * HaveLunch ($82:80AA, JSL, RTL). At 12:00 the player gets +20 energy
 * (ChangeStamina) and, unless busy, sits down to eat: $0924 = which lunch
 * (RNGReturn0toA(3): 0..2) and game-state bits $0004 and $0400 are set.
 * "Busy" is game state $0430 or $0800, or fishing (actions $0F-$13) or
 * jumping ($17, $18).
 * Leftovers: A, X, Y and C of the test it stops at or of the last call
 * (C = 1 on a busy action, from its CMP; past them, action >= $18).
 * ───────────────────────────────────────────────────────────────────────── */
#define HM_LUNCH_CHOICE   0x0924   /* DB */
#define RET_LUNCH_STAMINA 0x80C0   /* JSL ChangeStamina at $82:80BD */
#define RET_LUNCH_ROLL    0x8118   /* JSL RNGReturn0toA at $82:8115 */

void hm_HaveLunch(uint8_t *ram, HmRegs *r)
{
  uint8_t hour = ram[HM_HOUR];
  r->a = (uint16_t)((r->a & 0xFF00) | hour);
  r->c = hour >= 12;
  if (hour != 12) return;
  r->a = (uint16_t)((r->a & 0xFF00) | 20);       /* LDA #20 (8-bit) */
  r->n = 0;                                      /* ...which clears N */
  hm_jsl(ram, r, RET_LUNCH_STAMINA, 0x81);
  hm_ChangeStamina(ram, r);
  hm_rtl(ram, r);

  r->a = (uint16_t)(hm_dp_r16(ram, r, 0xD2) & 0x0430);
  if (r->a) return;
  uint16_t action = hm_dp_r16(ram, r, 0xD4);
  r->a = action;
  static const uint16_t busy[] = { 0x0F, 0x10, 0x11, 0x12, 0x13, 0x17, 0x18 };
  for (unsigned i = 0; i < sizeof busy / sizeof busy[0]; i++) {
    r->c = action >= busy[i];
    if (action == busy[i]) return;
  }
  r->a = (uint16_t)(hm_dp_r16(ram, r, 0xD2) & 0x0800);
  if (r->a) return;

  r->a = (uint16_t)((r->a & 0xFF00) | 3);
  hm_jsl(ram, r, RET_LUNCH_ROLL, 0x80);
  hm_RNGReturn0toA(ram, r);
  hm_rtl(ram, r);
  hm_bus_wr8(ram, (uint32_t)r->db << 16 | HM_LUNCH_CHOICE, (uint8_t)r->a);
  hm_dp_w16(ram, r, 0xD2, (uint16_t)(hm_dp_r16(ram, r, 0xD2) | 0x0004));
  r->a = (uint16_t)(hm_dp_r16(ram, r, 0xD2) | 0x0400);
  hm_dp_w16(ram, r, 0xD2, r->a);
}
