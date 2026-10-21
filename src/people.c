/*
 * people.c — the girls, the wife and the children.
 *
 * Hearts: 16-bit, 0..999, at $7F1F1F + 2 i for i = Maria, Ann, Nina,
 * Ellen, Eve (HM_HEARTS_BASE). The wife: $7F1F66 bits 0..4 in the same
 * order (only one is ever set).
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

enum { MARIA, ANN, NINA, ELLEN, EVE, GIRLS };

/* ─────────────────────────────────────────────────────────────────────────
 * FindMostLovedName ($82:8EC6, JSL, RTL; no compiled body). Writes the name
 * of the girl with the most hearts into $08A1-$08AA (five 16-bit text
 * codes; see the text encoding in hm_ram.h's notes: a-z = 00-19, A-Z =
 * 1A-33, B1 = space). On a tie the later girl in the list wins (BCS).
 * HM-Decomp's branch labels are swapped: index 1 writes "Ann", 2 "Nina".
 * Leftovers: $7E = the most hearts, X = $0A, Y = the girl, A = the last
 * code written, C = 1.
 * ───────────────────────────────────────────────────────────────────────── */
#define MOST_LOVED_NAME 0x008A1   /* 5 x 16b */

static const uint16_t kGirlNames[GIRLS][5] = {
  { 0x26, 0x00, 0x11, 0x08, 0x00 },   /* "Maria" */
  { 0x1A, 0x0D, 0x0D, 0xB1, 0xB1 },   /* "Ann  " */
  { 0x27, 0x08, 0x0D, 0x00, 0xB1 },   /* "Nina " */
  { 0x1E, 0x0B, 0x0B, 0x04, 0x0D },   /* "Ellen" */
  { 0x1E, 0x15, 0x04, 0xB1, 0xB1 },   /* "Eve  " */
};

void hm_FindMostLovedName(uint8_t *ram, HmRegs *r)
{
  uint16_t best = hm_rd16(ram, HM_HEARTS_BASE);
  unsigned girl = MARIA;
  for (unsigned i = 1; i < GIRLS; i++) {
    uint16_t hearts = hm_rd16(ram, HM_HEARTS_BASE + 2 * i);
    if (hearts >= best) { best = hearts; girl = i; }
  }
  hm_dp_w16(ram, r, 0x7E, best);
  for (unsigned k = 0; k < 5; k++)
    hm_wr16(ram, MOST_LOVED_NAME + 2 * k, kGirlNames[girl][k]);
  r->a = kGirlNames[girl][4];
  r->x = 0x0A;
  r->y = (uint16_t)girl;
  r->c = 1;
}

/* ─────────────────────────────────────────────────────────────────────────
 * WifePregnanacyandChilds ($82:8790, JSL, RTL; no compiled body). Once
 * married, every night:
 *  - $7F1F3B (days married) + 1; every 120th day sets $7F1F6C bit $0080
 *    (the wedding anniversary).
 *  - First child ($7F1F6E bit $0004): the wife needs 450 hearts; second
 *    (bit $0008): 650 hearts and the first child 90 days old. Both need 20
 *    days of marriage and the house at level 2 ($7F1F64 bit $0080).
 *  - A child's age ($7F1F37 / $7F1F39) counts up from conception: at 60
 *    days it is born ($7F1F6E bit $0010, player's happiness +50 / +100;
 *    the first child's birth night skips the second child's count), and
 *    every 120 days after that it has a birthday ($7F1F6C bit $0100/$0200).
 *  - $7F1F6C bits $0080, $0100, $0200 are cleared first: they last a day.
 * ───────────────────────────────────────────────────────────────────────── */
#define WIFE_FLAGS     0x11F66
#define DAYS_MARRIED   0x11F3B
#define KID1_AGE       0x11F37
#define KID2_AGE       0x11F39
#define FLAGS_64       0x11F64
#define FLAGS_6C       0x11F6C
#define FLAGS_6E       0x11F6E
#define CHILD1         0x0004
#define CHILD2         0x0008
#define CHILD_BORN     0x0010

/* $7E = n; $80 = 120; JSL DivisionUnsigned. True if n is a multiple of 120. */
static bool every_120_days(uint8_t *ram, HmRegs *r, uint16_t n, uint16_t ret)
{
  hm_dp_w16(ram, r, 0x7E, n);
  hm_dp_w16(ram, r, 0x80, 120);
  hm_jsl(ram, r, ret, 0x83);
  hm_DivisionUnsigned(ram, r);
  hm_rtl(ram, r);
  r->a = hm_dp_r16(ram, r, 0x7E);                /* LDA $7E: the remainder */
  return r->a == 0;
}

static void set_flag(uint8_t *ram, HmRegs *r, uint32_t at, uint16_t bit)
{
  r->a = hm_rd16(ram, at) | bit;
  hm_wr16(ram, at, r->a);
}

/* A child's day. Returns true when it was born tonight (the routine then
 * stops). */
static bool child_day(uint8_t *ram, HmRegs *r, uint32_t age_at, int16_t joy,
                      uint16_t ret_joy, uint16_t ret_div, uint16_t birthday_bit)
{
  uint16_t age = (uint16_t)(hm_rd16(ram, age_at) + 1);
  hm_wr16(ram, age_at, age);
  r->a = age;
  r->c = age >= 60;
  if (age < 60) return false;
  if (age == 60) {
    set_flag(ram, r, FLAGS_6E, CHILD_BORN);
    r->a = (uint16_t)joy;
    hm_jsl(ram, r, ret_joy, 0x83);
    hm_AddPlayerHappiness(ram, r);
    hm_rtl(ram, r);
    return true;
  }
  if (every_120_days(ram, r, (uint16_t)(age - 60), ret_div))
    set_flag(ram, r, FLAGS_6C, birthday_bit);
  return false;
}

void hm_WifePregnanacyandChilds(uint8_t *ram, HmRegs *r)
{
  hm_wr16(ram, FLAGS_6C, hm_rd16(ram, FLAGS_6C) & 0xFC7F);
  uint16_t wife = hm_rd16(ram, WIFE_FLAGS);
  unsigned who = GIRLS;
  for (unsigned i = 0; i < GIRLS && who == GIRLS; i++)
    if (wife & (1u << i)) who = i;
  r->a = wife & (1u << (who == GIRLS ? 4 : who));
  if (who == GIRLS) return;                      /* not married */

  uint16_t days = (uint16_t)(hm_rd16(ram, DAYS_MARRIED) + 1);
  hm_wr16(ram, DAYS_MARRIED, days);
  if (every_120_days(ram, r, days, 0x87E2))
    set_flag(ram, r, FLAGS_6C, 0x0080);
  r->a = wife & (1u << who);

  /* Another child? */
  bool second = hm_rd16(ram, FLAGS_6E) & CHILD1;
  uint16_t hearts = hm_rd16(ram, HM_HEARTS_BASE + 2 * who);
  r->a = hearts;
  r->c = hearts >= (second ? 650 : 450);
  if (r->c) {
    r->c = days >= 20;
    if (r->c && (hm_rd16(ram, FLAGS_64) & 0x0080)) {
      if (!(hm_rd16(ram, FLAGS_6E) & CHILD1)) {
        set_flag(ram, r, FLAGS_6E, CHILD1);
      } else {
        uint16_t age1 = hm_rd16(ram, KID1_AGE);
        r->a = age1;
        r->c = age1 >= 90;
        if (r->c) set_flag(ram, r, FLAGS_6E, CHILD2);
      }
    } else if (r->c) {
      r->a = hm_rd16(ram, FLAGS_64) & 0x0080;
    } else {
      r->a = days;
    }
  }

  r->a = hm_rd16(ram, FLAGS_6E) & CHILD1;
  if (r->a && child_day(ram, r, KID1_AGE, 50, 0x8955, 0x896C, 0x0100))
    return;
  r->a = hm_rd16(ram, FLAGS_6E) & CHILD2;
  if (r->a) child_day(ram, r, KID2_AGE, 100, 0x89AC, 0x89C3, 0x0200);
}
