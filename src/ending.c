/*
 * ending.c — the game's ending.
 *
 * RanchMasteryCalculator ($83:F26D, reached with JMP from the ending's
 * script command table at $83:ECF7, returns with RTS). The "ranch
 * mastery" score the ending judges you by ($7F1F54, 0..999):
 *
 *   money          low word of the money / 128, at most 78
 *                  (= 99,840 G; the money is kept in tens of G)
 *   cows, chickens 3 points each
 *   max stamina    (max - 100) / 2   (it starts at 100)
 *   hearts         each of the five girls: (hearts & $1FF) / 16
 *   shipped crops  tomatoes, corn, potatoes, turnips: (n & $1FF) / 16
 *   happiness      / 32
 *   house          +16 first extension, +16 second
 *   children       +16 each
 *   painted house  +32 if any part is painted
 *   clock          +22        turtle shell  +21
 *   development    the ranch development score / 2 (see farm.c)
 *   each cow       happiness / 8, at most 25
 *   then capped at 999.
 *
 * Bugs of the original (HM-Decomp notes them too): the hearts and the
 * shipped counts are cut with AND #$01FF instead of being capped, so 512
 * shipped turnips count as 0 and 600 hearts as 88 (5 points). The money
 * uses only the low 16 bits of the 24-bit amount: 655,360 G count as 0.
 * The cow and chicken counts are multiplied in 8 bits (above 85 animals
 * they would wrap; the game never has that many).
 *
 * Then it starts the ending's next script (StartEventScriptLong, group 0,
 * script $46, slot $0C), sets $7F1F47 = $25 and $7F1F5E bit $8000.
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */
#include "hm_vars.h"

/* ── Names of the memory locations used here ──────────────────────────────
 * The game's saved state, $7F:1Fxx (ram[] offsets 0x11Fxx), and one
 * data-bank variable. */
#define RANCH_MASTERY      0x11F54   /* $7F1F54, 16b: the score worked out here */

/* A cow's record (see animals.c) */

#define RET_COW_POINTER    0xF46D    /* JSL GetCowPointer at $83:F46A */
#define RET_NEXT_SCRIPT    0xF4C0    /* JSL StartEventScriptLong at $83:F4BD */

static uint16_t score(uint8_t *ram) { return hm_rd16(ram, RANCH_MASTERY); }
/* CLC; ADC ranch_mastery; STA (with the carry it is given) */
static void add_score(uint8_t *ram, HmRegs *r, uint16_t points, unsigned carry)
{
  uint32_t sum = (uint32_t)points + score(ram) + carry;
  r->c = sum > 0xFFFF;
  r->a = (uint16_t)sum;
  hm_wr16(ram, RANCH_MASTERY, r->a);
}
/* an animal count times 3, done in 8 bits: ASL; CLC; ADC n; then the
 * 16-bit ADC keeps the carry of the 8-bit one */
static void add_three_each(uint8_t *ram, HmRegs *r, uint8_t n)
{
  uint8_t doubled = (uint8_t)(n << 1);
  unsigned sum8 = (unsigned)doubled + n;
  add_score(ram, r, (uint8_t)sum8, sum8 > 0xFF);
}
/* (value & $1FF) / 16 */
static void add_sixteenths(uint8_t *ram, HmRegs *r, uint32_t at)
{
  add_score(ram, r, (uint16_t)((hm_rd16(ram, at) & 0x01FF) >> 4), 0);
}
/* LDA flags; AND #bit; BEQ: if set, + points */
static void bonus(uint8_t *ram, HmRegs *r, uint32_t flags, uint16_t bit, uint16_t points)
{
  r->a = hm_rd16(ram, flags) & bit;
  if (r->a) add_score(ram, r, points, 0);
}

void hm_RanchMasteryCalculator(uint8_t *ram, HmRegs *r)
{
  uint16_t money = (uint16_t)(hm_rd16(ram, HM_MONEY) >> 7);
  r->c = money >= 78;                                    /* CMP #78 */
  if (money >= 78) money = 78;
  hm_wr16(ram, RANCH_MASTERY, money);
  r->a = money;

  add_three_each(ram, r, ram[HM_COW_N]);
  add_three_each(ram, r, ram[HM_CHICKEN_N]);

  uint8_t max = ram[HM_MAX_STAMINA];
  add_score(ram, r, (uint8_t)(max - 100) >> 1, 0);

  for (int girl = 0; girl < 5; girl++) add_sixteenths(ram, r, HM_HEARTS_BASE + 2u * girl);
  add_sixteenths(ram, r, HM_SHIPPED_TOMATOES);
  add_sixteenths(ram, r, HM_SHIPPED_CORN);
  add_sixteenths(ram, r, HM_SHIPPED_POTATOES);
  add_sixteenths(ram, r, HM_SHIPPED_TURNIPS);
  add_score(ram, r, (uint16_t)(hm_rd16(ram, HM_HAPPINESS) >> 5), 0);

  bonus(ram, r, HM_FLAGS_64, 0x0080, 16);                 /* house: second extension */
  bonus(ram, r, HM_FLAGS_64, 0x0040, 16);                 /* house: first extension */
  bonus(ram, r, HM_FLAGS_6E, 0x0008, 16);                 /* first child */
  bonus(ram, r, HM_FLAGS_6E, 0x0004, 16);                 /* second child */
  bonus(ram, r, HM_FLAGS_66, 0x001F, 32);                 /* painted house */
  bonus(ram, r, HM_FLAGS_6E, 0x4000, 22);                 /* clock */
  bonus(ram, r, HM_FLAGS_6C, 0x1000, 21);                 /* turtle shell */
  add_score(ram, r, (uint16_t)(hm_rd16(ram, HM_RANCH_DEVELOPMENT) >> 1), 0);

  for (uint16_t cow = 0; cow < 12; cow++) {
    r->x = cow;                                          /* LDX #0 / INX */
    hm_push8(ram, r, (uint8_t)(cow >> 8)); hm_push8(ram, r, (uint8_t)cow);   /* PHX */
    r->a = cow;
    hm_jsl(ram, r, RET_COW_POINTER, 0x83);
    hm_GetCowPointer(ram, r);
    hm_rtl(ram, r);
    uint32_t record = hm_dp_long(ram, r, HM_ANIMAL_PTR, ANIMAL_STATUS);
    uint8_t status = hm_bus_rd8(ram, record);
    r->a = (uint16_t)((r->a & 0xFF00) | (status & 1));
    r->y = 0;
    if (status & 1) {                                    /* the cow exists */
      r->y = 4;
      uint8_t points = (uint8_t)(hm_bus_rd8(ram, hm_dp_long(ram, r, HM_ANIMAL_PTR, COW_HAPPINESS)) >> 3);
      r->c = points >= 25;
      if (points >= 25) points = 25;
      add_score(ram, r, points, 0);
    }
    hm_pull8(ram, r); hm_pull8(ram, r);                  /* PLX */
    r->x = (uint16_t)(cow + 1);
    r->c = r->x >= 12;                                   /* CPX #12 */
  }

  r->a = score(ram);
  r->c = r->a >= 999;
  if (r->a >= 999) { r->a = 999; hm_wr16(ram, RANCH_MASTERY, 999); }

  r->a = 0; r->x = 0x0046; r->y = 0x000C;
  hm_jsl(ram, r, RET_NEXT_SCRIPT, 0x84);
  hm_StartEventScriptLong(ram, r);
  hm_rtl(ram, r);
  ram[HM_ENDING_STEP] = 0x25;
  r->a = hm_rd16(ram, HM_FLAGS_5E) | 0x8000;
  hm_wr16(ram, HM_FLAGS_5E, r->a);
}
