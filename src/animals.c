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

/* ─────────────────────────────────────────────────────────────────────────
 * CowFeedingandStatus ($83:BC5A, JSL, RTL; no compiled body: it runs in the
 * recompiler's interpreter). The animals' night, called from NightReset.
 *
 * Cow record (16 bytes, GetsCowPointer):
 *   +0 status: 01 exists, 02 newborn calf, 04 calf, 08 adult, 10 cranky,
 *              20 sick, 40 pregnant, 80 just gave birth
 *   +1 flags (bits 0-2 are daily and cleared every night; 80 = got the
 *      miracle potion today)
 *   +2 where it is (map; $27 = barn, 0-3 = the farm)
 *   +3 a counter: days as a calf, days left cranky / sick / pregnant
 *   +4 happiness (AddCowHappiness)   +5 days out of the barn (cranky at 10)
 * Chicken record (8 bytes, GetChickenPointer):
 *   +0 status: 01 exists, 02 egg, 04 chick, 08 hen, 10 hungry
 *   +1 where it is ($28 = coop, 0-3 = the farm)   +2 a counter
 *
 * 1. Feeding (skipped when $7F1F60 bit 0 is set), cows 0..11 that are not
 *    newborn calves:
 *    - Calves on the farm just graze (eat one grown grass tile).
 *    - Grown cows in the barn are fed if their stall has fodder ($0932, one
 *      bit per stall; pregnant cows use stall 12).
 *    - Grown cows on the farm graze; with no grass, or in the rain, they go
 *      hungry; snow, hurricane and the winter storm cost happiness.
 *    - A hungry cow loses happiness and may fall sick (1 in 4 hungry, 1 in
 *      2 in snow or hurricane, 1 in 8 in the winter storm) unless pregnant,
 *      given the potion or already sick: then happiness -12, and the
 *      player's happiness -30; sick for 7 days.
 *    - Then a cow 10 days out of the barn, or one left out ($7F1F5A bit
 *      $1000: 1 in 16, -8 happiness; or $0196 bit $0400) turns cranky for
 *      3 days (happiness -30).
 * 2. Growing up, cows 0..11 (always): daily flags cleared; a newborn calf
 *    becomes a calf after 14 days, a calf an adult after 21 ($7F1F6E bit
 *    $1000 is set on growing up and for every adult). Cranky and sick days
 *    count down; a cow sick to the end dies (unless tomorrow is a festival,
 *    or $0196 has 0002/0008/0010): cow_N - 1, $7F1F6E bit $0040 (funeral),
 *    $0937 = which cow. A cow given the potion becomes pregnant; a
 *    pregnant cow counts down with the global $7F1F12 and gives birth in
 *    the barn: $7F1F64 bit 8, and $7F1F2B = 20 / 50 / 100 by happiness
 *    (below 96 / below 192 / higher).
 * 3. Chickens, feeding (skipped like 1): a chicken with no stage dies
 *    (chicks_N - 1, player's happiness -30); hens in the coop eat from the
 *    feed box ($0931); on the farm they graze; unfed hens get hungry. On
 *    a $0196 $0410 night one hen left outside (not in the coop) is lost.
 * 4. Chickens, growing (always): an egg hatches after 3 nights if there is
 *    room (fewer than 12 chickens), a chick becomes a hen after 7.
 * 5. Chickens, eggs: $7F1F45 = one bit per hen in the coop that is not
 *    hungry (table $83:CA78); hungry hens count down instead.
 * 6. If $7F1F68 bit $0100, $7F1F32 counts up to 21.
 * ───────────────────────────────────────────────────────────────────────── */
#define COW_EXISTS      0x01
#define COW_NEWBORN     0x02
#define COW_CALF        0x04
#define COW_ADULT       0x08
#define COW_CRANKY      0x10
#define COW_SICK        0x20
#define COW_PREGNANT    0x40
#define COW_GAVE_BIRTH  0x80
#define COW_POTION      0x80   /* in +1 */
#define MAP_BARN        0x27
#define MAP_COOP        0x28
#define HEN_EXISTS      0x01
#define HEN_EGG         0x02
#define HEN_CHICK       0x04
#define HEN_ADULT       0x08
#define HEN_HUNGRY      0x10

#define HM_COWS_FED_STALLS   0x00932   /* 16b, one bit per stall */
#define HM_CHICKENS_FED_N    0x00931   /* 8b */
#define HM_CHICKENS_N        0x11F0B   /* 8b */
#define HM_DEAD_COW          0x00937   /* 8b */
#define COW_FEED_FLAGS       0x82A571  /* ROM: dw 1 << n */
#define HEN_EGG_FLAGS        0x83CA78  /* ROM: dw 1 << n */
#define NO_FEEDING           0x11F60   /* bit 0 */
#define FLAGS_5A             0x11F5A
#define FLAGS_64             0x11F64
#define FLAGS_68             0x11F68
#define FLAGS_6E             0x11F6E
#define PREGNANCY_DAYS       0x11F12   /* 8b */
#define NEWBORN_HAPPINESS    0x11F2B   /* 8b */
#define HEN_EGGS             0x11F45   /* 16b */
#define COUNTER_32           0x11F32   /* 8b */

static uint8_t s_nowhere;
/* [$72],Y: a field of the animal record; LDY #off first, as the game does. */
static uint8_t *field(uint8_t *ram, HmRegs *r, uint8_t off)
{
  r->y = off;
  uint8_t *p = hm_bus_ram(ram, hm_dp_long(ram, r, 0x72, off));
  return p ? p : &s_nowhere;
}

typedef void (*HmRoutine)(uint8_t *ram, HmRegs *r);
static void call(uint8_t *ram, HmRegs *r, uint16_t ret, uint8_t bank, HmRoutine fn)
{
  hm_jsl(ram, r, ret, bank);
  fn(ram, r);
  hm_rtl(ram, r);
}
static void cow_happiness(uint8_t *ram, HmRegs *r, int16_t change, uint16_t ret)
{
  r->a = (uint16_t)change;
  call(ram, r, ret, 0x84, hm_AddCowHappiness);
}
static void player_happiness(uint8_t *ram, HmRegs *r, int16_t change, uint16_t ret)
{
  r->a = (uint16_t)change;
  call(ram, r, ret, 0x83, hm_AddPlayerHappiness);
}
static bool one_in(uint8_t *ram, HmRegs *r, uint8_t n, uint16_t ret)
{
  r->a = (uint16_t)((r->a & 0xFF00) | n);
  call(ram, r, ret, 0x80, hm_RNGReturn0toA);
  return (uint8_t)r->a == 0;
}
static void graze(uint8_t *ram, HmRegs *r, uint16_t ret) { call(ram, r, ret, 0x82, hm_Sub_82A9A0); }
static bool grass_eaten(const uint8_t *ram) { return hm_rd16(ram, 0x0092E) != 0; }
static uint16_t weather_flags(const uint8_t *ram) { return hm_rd16(ram, 0x00196); }

/* PHX; PHX; TXA; JSL Get...Pointer; PLX (one copy of X stays pushed). */
static void animal_begin(uint8_t *ram, HmRegs *r, uint16_t i, uint16_t ret, HmRoutine get)
{
  hm_push8(ram, r, (uint8_t)(i >> 8)); hm_push8(ram, r, (uint8_t)i);
  hm_push8(ram, r, (uint8_t)(i >> 8)); hm_push8(ram, r, (uint8_t)i);
  r->a = i;
  call(ram, r, ret, 0x83, get);
  hm_pull8(ram, r); hm_pull8(ram, r);
}
static void animal_end(uint8_t *ram, HmRegs *r)    /* PLX at the bottom */
{
  hm_pull8(ram, r); hm_pull8(ram, r);
}

/* 1. ── feeding the cows ── */
static void cow_cranky(uint8_t *ram, HmRegs *r)
{
  if (*field(ram, r, 0) & COW_PREGNANT) return;
  if (*field(ram, r, 1) & COW_POTION) return;
  if (*field(ram, r, 0) & COW_SICK) return;
  *field(ram, r, 0) |= COW_CRANKY;
  *field(ram, r, 3) = 3;
  *field(ram, r, 5) = 0;
  cow_happiness(ram, r, -30, 0xBE2B);
}

static void cow_after_meal(uint8_t *ram, HmRegs *r)
{
  if (*field(ram, r, 5) == 10) { cow_cranky(ram, r); return; }
  if (hm_rd16(ram, FLAGS_5A) & 0x1000) {
    if (*field(ram, r, 2) == MAP_BARN) return;
    if (one_in(ram, r, 16, 0xBDC8)) {
      cow_happiness(ram, r, -8, 0xBDD3);
      cow_cranky(ram, r);
      return;
    }
  }
  /* GAME BUG: when we got here through the 1-in-16 roll above (cow outside,
   * $7F1F5A bit $1000, roll not 0), RNGReturn0toA has left X in 8-bit mode
   * and nothing sets it back to 16 bits. If $0196 bit $0400 is also set,
   * the next instruction, LDY.W #$0002 at $83:BDE1, runs with an 8-bit X:
   * the CPU takes A0 02 as the whole instruction and the leftover 00 is a
   * BRK. The game goes off the rails there. The rewrite does what the code
   * meant (check the map, maybe turn cranky); that case is left out of the
   * comparison. */
  if (!(weather_flags(ram) & 0x0400)) return;
  if (*field(ram, r, 2) == MAP_BARN) return;
  cow_cranky(ram, r);
}

/* Hungry: lose happiness, then 1 in `sick_odds` of falling sick. */
static void cow_hungry(uint8_t *ram, HmRegs *r, int16_t change, uint16_t ret, uint8_t sick_odds)
{
  cow_happiness(ram, r, change, ret);
  if (one_in(ram, r, sick_odds, 0xBD56) &&
      !(*field(ram, r, 0) & COW_PREGNANT) &&
      !(*field(ram, r, 1) & COW_POTION) &&
      !(*field(ram, r, 0) & COW_SICK)) {
    *field(ram, r, 0) |= COW_SICK;
    *field(ram, r, 3) = 7;
    cow_happiness(ram, r, -12, 0xBD94);
    player_happiness(ram, r, -30, 0xBD9D);
  }
  cow_after_meal(ram, r);
}

static void feed_cow(uint8_t *ram, HmRegs *r, uint16_t i)
{
  uint8_t status = *field(ram, r, 0);
  if (!(status & COW_EXISTS) || (status & COW_NEWBORN)) return;
  if (*field(ram, r, 0) & COW_CALF) {
    if (*field(ram, r, 2) < 4) graze(ram, r, 0xBE3E);
    return;
  }
  if (*field(ram, r, 2) == MAP_BARN) {
    uint16_t stall = (*field(ram, r, 0) & COW_PREGNANT) ? 12 : i;
    if (hm_bus_rd16(ram, COW_FEED_FLAGS + 2u * stall) & hm_rd16(ram, HM_COWS_FED_STALLS))
      cow_after_meal(ram, r);
    else
      cow_hungry(ram, r, -8, 0xBD18, 4);
    return;
  }
  if (*field(ram, r, 2) >= 4) { cow_hungry(ram, r, -8, 0xBD18, 4); return; }
  graze(ram, r, 0xBCE3);
  uint16_t w = weather_flags(ram);
  if (!grass_eaten(ram) || (w & 0x0002)) cow_hungry(ram, r, -8, 0xBD18, 4);
  else if (w & 0x0008) cow_hungry(ram, r, -16, 0xBD28, 2);
  else if (w & 0x0010) cow_hungry(ram, r, -24, 0xBD38, 2);
  else if (w & 0x0200) cow_hungry(ram, r, -8, 0xBD48, 8);
  else cow_after_meal(ram, r);
}

/* 2. ── the cows grow up ── */
static void cow_birth(uint8_t *ram, HmRegs *r)
{
  if (*field(ram, r, 2) != MAP_BARN) return;
  hm_wr16(ram, FLAGS_64, hm_rd16(ram, FLAGS_64) | 0x0008);
  uint8_t *status = field(ram, r, 0);
  *status = (uint8_t)((*status | COW_GAVE_BIRTH) & ~COW_PREGNANT);
  ram[NEWBORN_HAPPINESS] = 20;
  uint8_t happiness = *field(ram, r, 4);
  if (happiness >= 192) ram[NEWBORN_HAPPINESS] = 100;
  else if (happiness >= 96) ram[NEWBORN_HAPPINESS] = 50;
}

static void cow_dies_if_sick(uint8_t *ram, HmRegs *r, uint16_t i)
{
  if (weather_flags(ram) & 0x001A) return;
  if (ram[HM_WEATHER_TOMORROW] >= 6) return;       /* festival tomorrow */
  *field(ram, r, 0) = 0;
  ram[HM_COW_N]--;
  hm_wr16(ram, FLAGS_6E, hm_rd16(ram, FLAGS_6E) | 0x0040);
  ram[HM_DEAD_COW] = (uint8_t)i;
}

static void cow_grows(uint8_t *ram, HmRegs *r, uint16_t i)
{
  if (!(*field(ram, r, 0) & COW_EXISTS)) return;
  (void)field(ram, r, 4);                          /* LDA happiness, unused */
  *field(ram, r, 1) &= 0xF8;
  uint8_t status = *field(ram, r, 0);
  if (status & COW_NEWBORN) {
    if (++*field(ram, r, 3) == 14) {
      *field(ram, r, 3) = 0;
      uint8_t *s = field(ram, r, 0);
      *s = (uint8_t)((*s | COW_CALF) & ~COW_NEWBORN);
    }
    return;
  }
  if (*field(ram, r, 0) & COW_CALF) {
    if (++*field(ram, r, 3) == 21) {
      *field(ram, r, 3) = 0;
      uint8_t *s = field(ram, r, 0);
      *s = (uint8_t)((*s | COW_ADULT) & ~COW_CALF);
      *field(ram, r, 3) = 0;
      hm_wr16(ram, FLAGS_6E, hm_rd16(ram, FLAGS_6E) | 0x1000);
    }
    return;
  }
  hm_wr16(ram, FLAGS_6E, hm_rd16(ram, FLAGS_6E) | 0x1000);
  if (*field(ram, r, 0) & COW_CRANKY) {
    if (--*field(ram, r, 3) == 0) *field(ram, r, 0) &= ~COW_CRANKY;
    return;
  }
  if (*field(ram, r, 0) & COW_SICK) {
    uint8_t *days = field(ram, r, 3);
    if (*days == 0 || --*days == 0) cow_dies_if_sick(ram, r, i);
    return;
  }
  if (*field(ram, r, 1) & COW_POTION) {
    *field(ram, r, 0) |= COW_PREGNANT;
    *field(ram, r, 1) &= 0x7F;
    return;
  }
  if (!(*field(ram, r, 0) & COW_PREGNANT)) return;
  uint8_t *days = field(ram, r, 3);
  if (*days != 0) {
    (*days)--;
    if (--ram[PREGNANCY_DAYS] != 0) return;
  }
  cow_birth(ram, r);
}

/* 3. ── feeding the chickens ── */
static void feed_hen(uint8_t *ram, HmRegs *r)
{
  if (!(*field(ram, r, 0) & HEN_EXISTS)) return;
  if (!(*field(ram, r, 0) & (HEN_EGG | HEN_CHICK | HEN_ADULT))) {
    *field(ram, r, 0) = 0;                          /* no stage: it dies */
    ram[HM_CHICKENS_N]--;
    player_happiness(ram, r, -30, 0xC083);
  }
  if (!(*field(ram, r, 0) & HEN_ADULT)) return;
  bool fed;
  if (*field(ram, r, 1) == MAP_COOP) {
    if (ram[HM_CHICKENS_FED_N]) { ram[HM_CHICKENS_FED_N]--; return; }
    fed = false;
  } else if (*field(ram, r, 1) >= 4) {
    fed = false;
  } else {
    graze(ram, r, 0xC0BB);
    fed = grass_eaten(ram) && !(weather_flags(ram) & 0x020A);
  }
  if (!fed) {
    *field(ram, r, 0) |= HEN_HUNGRY;
    *field(ram, r, 2) = 3;
  }
  if (!(weather_flags(ram) & 0x0410)) return;
  if (hm_rd16(ram, FLAGS_6E) & 0x0020) return;     /* only one per night */
  if (*field(ram, r, 1) >= 4) return;
  *field(ram, r, 0) = HEN_EXISTS;                  /* lost: dies tomorrow */
  hm_wr16(ram, FLAGS_6E, hm_rd16(ram, FLAGS_6E) | 0x0020);
}

/* 4. ── the chickens grow up ── */
static void hen_grows(uint8_t *ram, HmRegs *r)
{
  if (!(*field(ram, r, 0) & HEN_EXISTS)) return;
  if (*field(ram, r, 0) & HEN_EGG) {
    uint8_t *nights = field(ram, r, 2);
    if (*nights != 3 && ++*nights != 3) return;
    if (ram[HM_CHICKENS_N] == 12) return;         /* no room: keeps waiting */
    *field(ram, r, 2) = 0;
    uint8_t *s = field(ram, r, 0);
    *s = (uint8_t)((*s | HEN_CHICK) & 0xBD);
    hm_bus_wr16(ram, hm_dp_long(ram, r, 0x72, 4), 0x00D8);
    hm_bus_wr16(ram, hm_dp_long(ram, r, 0x72, 6), 0x00B8);
    r->y = 6;
    ram[HM_CHICKENS_N]++;
    hm_wr16(ram, FLAGS_6E, hm_rd16(ram, FLAGS_6E) & 0xDFFF);
    return;
  }
  if (!(*field(ram, r, 0) & HEN_CHICK)) return;
  if (++*field(ram, r, 2) != 7) return;
  *field(ram, r, 2) = 0;
  uint8_t *s = field(ram, r, 0);
  *s = (uint8_t)((*s | HEN_ADULT) & ~HEN_CHICK);
  *field(ram, r, 2) = 0;
}

/* 5. ── who lays an egg tomorrow ── */
static void hen_lays(uint8_t *ram, HmRegs *r, uint16_t i)
{
  if (!(*field(ram, r, 0) & HEN_EXISTS)) return;
  if (!(*field(ram, r, 0) & HEN_ADULT)) return;
  if (*field(ram, r, 0) & HEN_HUNGRY) {
    uint8_t *nights = field(ram, r, 2);
    if (--*nights == 0) *field(ram, r, 0) &= ~HEN_HUNGRY;
    return;
  }
  if (*field(ram, r, 1) != MAP_COOP) return;
  hm_wr16(ram, HEN_EGGS, hm_bus_rd16(ram, HEN_EGG_FLAGS + 2u * i) | hm_rd16(ram, HEN_EGGS));
  hm_wr16(ram, FLAGS_6E, hm_rd16(ram, FLAGS_6E) | 0x0400);
}

void hm_CowFeedingandStatus(uint8_t *ram, HmRegs *r)
{
  bool feeding = !(hm_rd16(ram, NO_FEEDING) & 0x0001);
  if (feeding) {
    for (uint16_t i = 0; i < 12; i++) {
      animal_begin(ram, r, i, 0xBC73, hm_GetsCowPointer);
      feed_cow(ram, r, i);
      animal_end(ram, r);
    }
  }
  hm_wr16(ram, FLAGS_6E, hm_rd16(ram, FLAGS_6E) & 0xEFBF);
  for (uint16_t i = 0; i < 12; i++) {
    animal_begin(ram, r, i, 0xBE63, hm_GetsCowPointer);
    cow_grows(ram, r, i);
    animal_end(ram, r);
  }
  if (!(hm_rd16(ram, NO_FEEDING) & 0x0001)) {
    hm_wr16(ram, FLAGS_6E, hm_rd16(ram, FLAGS_6E) & 0xFFDF);
    for (uint16_t i = 0; i < 13; i++) {
      animal_begin(ram, r, i, 0xC04C, hm_GetChickenPointer);
      feed_hen(ram, r);
      animal_end(ram, r);
    }
  }
  for (uint16_t i = 0; i < 13; i++) {
    animal_begin(ram, r, i, 0xC132, hm_GetChickenPointer);
    hen_grows(ram, r);
    animal_end(ram, r);
  }
  hm_wr16(ram, HEN_EGGS, 0);
  hm_wr16(ram, FLAGS_6E, hm_rd16(ram, FLAGS_6E) & 0xFBFF);
  for (uint16_t i = 0; i < 13; i++) {
    animal_begin(ram, r, i, 0xC207, hm_GetChickenPointer);
    hen_lays(ram, r, i);
    animal_end(ram, r);
  }

  r->x = 13;
  r->c = 1;                                        /* CPX #13 matched */
  r->a = hm_rd16(ram, FLAGS_68) & 0x0100;
  if (r->a) {
    uint8_t v = ram[COUNTER_32];
    r->c = v >= 0x15;
    if (v != 0x15) ram[COUNTER_32] = ++v;
    r->a = (uint16_t)(0x0100 | v);
  }
}

/* ─────────────────────────────────────────────────────────────────────────
 * New animals. Both look for the first free record (bit 0 of its status
 * clear), clear it and fill it in; A = 0 if there was room, 1 if not (13
 * chickens / 12 cows). Neither touches the animal counts $7F1F0A/0B: the
 * callers do.
 * ───────────────────────────────────────────────────────────────────────── */
#define CHICKEN_SPAWN 0x83CA10   /* ROM: dw x, y per chicken slot (bought) */
#define COW_SPAWN     0x83CA44   /* ROM: dw x, y per cow slot (in the barn) */

/* the search: X = first free slot (or `slots` if none), $72 = its record */
static uint16_t free_slot(uint8_t *ram, HmRegs *r, uint16_t slots, uint16_t ret,
                          void (*pointer)(uint8_t *, HmRegs *))
{
  uint16_t x = 0;
  do {
    hm_push8(ram, r, (uint8_t)(x >> 8)); hm_push8(ram, r, (uint8_t)x);   /* PHX */
    r->a = r->x = x;                                                     /* TXA */
    hm_jsl(ram, r, ret, 0x83);
    pointer(ram, r);
    hm_rtl(ram, r);
    hm_pull8(ram, r); hm_pull8(ram, r);                                  /* PLX */
    r->y = 0;
    uint8_t in_use = hm_bus_rd8(ram, hm_dp_long(ram, r, 0x72, 0)) & 1;
    r->a = (uint16_t)((r->a & 0xFF00) | in_use);
    if (!in_use) break;
    x++;
  } while (x != slots);
  r->x = x;
  r->c = x >= slots;                                                     /* CPX */
  return x;
}
static void put8(uint8_t *ram, HmRegs *r, uint16_t at, uint8_t v)
{
  r->y = at;
  hm_bus_wr8(ram, hm_dp_long(ram, r, 0x72, at), v);
}
static void put16(uint8_t *ram, HmRegs *r, uint16_t at, uint16_t v)
{
  r->y = at;
  hm_bus_wr16(ram, hm_dp_long(ram, r, 0x72, at), v);
}
/* its place from a table of (x, y) words: TXA; ASL; ASL; TAX ... */
static void spawn_from_table(uint8_t *ram, HmRegs *r, uint32_t table,
                             uint16_t at_x, uint16_t at_y)
{
  uint16_t slot = r->x;
  uint16_t x = (uint16_t)(slot << 2);
  r->c = (slot >> 14) & 1;                               /* the second ASL */
  put16(ram, r, at_x, hm_bus_rd16(ram, (table + x) & 0xFFFFFF));
  x = (uint16_t)(x + 2);
  r->a = hm_bus_rd16(ram, (table + x) & 0xFFFFFF);
  put16(ram, r, at_y, r->a);
  r->x = x;
}

/* ─────────────────────────────────────────────────────────────────────────
 * AddNewChicken ($83:C807, JSL, RTL). A = where it comes from:
 *   1 bought: status $09, at its slot's place in the coop ($83:CA10);
 *   2 hatched: status $09, +1 = season + 4, at (238, 318);
 *   anything else: status $43, at (E8, B8).
 * +1 is first set to $28 (the coop map), +2 to 0.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_AddNewChicken(uint8_t *ram, HmRegs *r)
{
  uint16_t source = r->a;
  hm_dp_w16(ram, r, 0x7E, source);
  uint16_t slot = free_slot(ram, r, 13, 0xC815, hm_GetChickenPointer);
  if (slot == 13) { r->a = 1; return; }

  for (uint16_t y = 0; y < 8; y++) put8(ram, r, y, 0);
  r->y = 8; r->c = 1;                                    /* CPY #8 */
  put8(ram, r, 1, 0x28);
  put8(ram, r, 2, 0x00);
  r->a = 0;
  r->c = source >= 1;                                    /* CMP #1 */
  if (source == 1) {
    put8(ram, r, 0, 0x09);
    spawn_from_table(ram, r, CHICKEN_SPAWN, 4, 6);
  } else if (r->c = source >= 2, source == 2) {          /* CMP #2 */
    put8(ram, r, 0, 0x09);
    uint8_t s = ram[HM_SEASON];
    r->c = (unsigned)s + 4 > 0xFF;                       /* CLC; ADC #4 */
    put8(ram, r, 1, (uint8_t)(s + 4));
    put16(ram, r, 4, 0x0238);
    put16(ram, r, 6, 0x0318);
  } else {
    put8(ram, r, 0, 0x43);
    put16(ram, r, 4, 0x00E8);
    put16(ram, r, 6, 0x00B8);
  }
  r->a = 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * AddNewCow ($83:C8DC, JSL, RTL). A = 0 bought, anything else born. The
 * record: status $05 (a young cow), +2 = $27 (the barn), pregnancy,
 * happiness and +5 = 0, and its slot's place in the barn ($83:CA44). A
 * calf born on the farm is a baby (status $03), starts with the happiness
 * the birth left in $7F1F2B, and gives the player +70 happiness.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_AddNewCow(uint8_t *ram, HmRegs *r)
{
  uint16_t source = r->a;
  hm_dp_w16(ram, r, 0x7E, source);
  uint16_t slot = free_slot(ram, r, 12, 0xC8EA, hm_GetsCowPointer);
  if (slot == 12) { r->a = 1; return; }

  for (uint16_t y = 0; y < 16; y++) put8(ram, r, y, 0);
  r->c = 1;                                              /* CPY #$10 */
  put8(ram, r, 2, 0x27);
  put8(ram, r, 3, 0);
  put8(ram, r, 4, 0);
  put8(ram, r, 5, 0);
  put8(ram, r, 1, 0);
  put8(ram, r, 0, 0x05);
  r->a = 0x0005;
  if (source != 0) {                                     /* born */
    put8(ram, r, 0, 0x03);
    put8(ram, r, 4, ram[0x11F2B]);
    r->a = 70;
    hm_jsl(ram, r, 0xC96C, 0x83);
    hm_AddPlayerHappiness(ram, r);
    hm_rtl(ram, r);
  }
  spawn_from_table(ram, r, COW_SPAWN, 8, 0x0A);
  r->a = 0;
}
