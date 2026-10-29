/*
 * day.c — the clock and the new day.
 *
 * UpdateTime runs once a frame from the game loop: a second a frame, the
 * hours of the day, and at night NightReset, which turns the calendar and
 * does everything that happens while the player sleeps, then wakes him
 * up in his house.
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

typedef void Fn(uint8_t *ram, HmRegs *r);

#define YEAR     0x7F1F18
#define SEASON   0x7F1F19
#define WEEKDAY  0x7F1F1A
#define DAY      0x7F1F1B
#define HOUR     0x7F1F1C
#define MINUTES  0x7F1F1D
#define SECONDS  0x7F1F1E

static uint32_t db(const HmRegs *r, uint16_t a) { return (uint32_t)r->db << 16 | a; }
static uint8_t rd8(uint8_t *ram, const HmRegs *r, uint16_t a) { return hm_bus_rd8(ram, db(r, a)); }
static uint16_t rd16(uint8_t *ram, const HmRegs *r, uint16_t a) { return hm_bus_rd16(ram, db(r, a)); }
static void wr8(uint8_t *ram, const HmRegs *r, uint16_t a, uint8_t v) { hm_bus_wr8(ram, db(r, a), v); }
static void wr16(uint8_t *ram, const HmRegs *r, uint16_t a, uint16_t v) { hm_bus_wr16(ram, db(r, a), v); }
static void set_a8(HmRegs *r, uint8_t v) { r->a = (uint16_t)((r->a & 0xFF00) | v); }

/* JSL at `at` to a routine of `bank`. Those that wait frames save the
 * flags (PHP): r->p carries the carry as it is now, 8-bit A, 16-bit
 * indexes, and D and I of the entry (N, Z and V are not followed:
 * HM_LOOSE). */
static void jsl(uint8_t *ram, HmRegs *r, uint16_t at, uint8_t bank, Fn *fn)
{
  uint8_t keep = r->p;
  r->p = (uint8_t)((keep & 0x0C) | 0x20 | (r->c & 1));
  hm_jsl(ram, r, (uint16_t)(at + 3), bank);
  fn(ram, r);
  hm_rtl(ram, r);
  r->p = keep;
}
/* LDA.L addr; AND #mask (16 bits) */
static bool flag(uint8_t *ram, HmRegs *r, uint32_t addr, uint16_t mask)
{
  r->a = (uint16_t)(hm_bus_rd16(ram, addr) & mask);
  return r->a != 0;
}
static void flag_on(uint8_t *ram, HmRegs *r, uint32_t addr, uint16_t bits)
{
  r->a = (uint16_t)(hm_bus_rd16(ram, addr) | bits);
  hm_bus_wr16(ram, addr, r->a);
}
static void flag_off(uint8_t *ram, HmRegs *r, uint32_t addr, uint16_t bits)
{
  r->a = (uint16_t)(hm_bus_rd16(ram, addr) & ~bits);
  hm_bus_wr16(ram, addr, r->a);
}
/* SEP #$20; LDA.L addr; ORA #bits; STA.L addr */
static void bits_on8(uint8_t *ram, HmRegs *r, uint32_t addr, uint8_t bits)
{
  uint8_t v = (uint8_t)(hm_bus_rd8(ram, addr) | bits);
  set_a8(r, v);
  hm_bus_wr8(ram, addr, v);
}
/* SEP #$20; LDA.L addr; INC; STA.L addr; CMP #n: true when it got to n */
static bool count8(uint8_t *ram, HmRegs *r, uint32_t addr, uint8_t n)
{
  uint8_t v = (uint8_t)(hm_bus_rd8(ram, addr) + 1);
  set_a8(r, v);
  hm_bus_wr8(ram, addr, v);
  r->c = v >= n;
  return v == n;
}

/* ─────────────────────────────────────────────────────────────────────────
 * NightReset ($82:82AC, JSL, RTL; UpdateTime jumps here when $0973 bit 1
 * is set, the player went to bed): the new day.
 *  - The calendar: the weekday (0-6), the day (1-30), the season (0-3)
 *    and the year; on a new year, the damage odds of 4 (winter's, with
 *    LoadsDamageProbabilities) and ClimateFarmDamageCheck.
 *  - The music stops (UNK_Audio25 with $0117 = $FF), the screen fades out
 *    (from 15, 3 frames a level, to 1) and is blanked; map $0F for the
 *    music of the day.
 *  - Today's special days: $7F1F64 bits 4-5 and $7F1F70 bit 1 from
 *    $0196 bits 4, 9 and 8, around SetWeatherFlags (which makes the new
 *    $0196); the new day's music (UNK_Audio5 past its 6 pm test).
 *  - 6:00:00, and the night's work: LoadsDateNames,
 *    NightlyFarmTilesCheck, ClimateFarmDamagePrep, CowFeedingandStatus,
 *    WeatherTomorrow, FindMostLovedName, WifePregnanacyandChilds.
 *  - The player rested: the state ($D2 = 1, action 0, facing down), the
 *    stamina full ($0918 = $0917), the shipped goods' money paid
 *    (AddMoney, $7F1F07-$7F1F09 then 0), the animals not fed, the day's
 *    flags ($7F1F5A-$7F1F62, $7F1F74-$7F1F78) cleared, $7F1F68 bit 0 (a
 *    day went by).
 *  - The story: $7F1F66 bit 5 becomes bit 6; while the house is being
 *    built ($7F1F66 bit 7), on the 4th night ($7F1F35) it is done: bit 8,
 *    500 wood needed for the next one ($7F1F0E), and the house's level
 *    ($7F1F64 bit 6, then bit 7). $7F1F68 bit 11 becomes bit 12; with bit
 *    15, the gold hammer in the shed ($7F1F02 bit 2). $7F1F6A bit 7: the
 *    first time bits 1 and 8; the second the hoe goes (from the hands
 *    $0921, the backpack $0923 and the shed's $7F1F00 bit 1) for the gold
 *    one ($7F1F02 bit 1). $7F1F6A bit 10 becomes bit 11. Five visits
 *    ($7F1F6A bits 12 and 14, $7F1F6C bits 0, 2 and 4) count nights in
 *    $09A3 and on the third their next bit takes over. $7F1F6E bit 15:
 *    the hoe back in the shed ($7F1F02 bit 0). $0196 bit 4: $7F1F60 bit
 *    0.
 *  - The diary: with $0973 bit 2, SaveGameSlot of slot $098E.
 *  - A trick: holding L, R and Select ($0124 = $2030) makes it summer 30
 *    of the third year, the last day.
 *  - The clock stops, the hands are emptied ($091D), $D2 bit 1 off; the
 *    player wakes in the house ($15) at (136, 120) (UNK_ScreenTransition),
 *    with 20 of stamina more (ChangeStamina); unless $7F1F5E bit 2, the
 *    breakfast ($0924 = 0-3, RNGReturn0toA) and $D2 bit 2.
 * Leftovers: those of RNGReturn0toA (or of ChangeStamina, with A = 4).
 * ───────────────────────────────────────────────────────────────────────── */
static void visit_nights(uint8_t *ram, HmRegs *r, uint32_t addr, uint16_t now, uint16_t next)
{
  if (!flag(ram, r, addr, now) || flag(ram, r, addr, next)) return;
  if (!count8(ram, r, db(r, 0x09A3), 3)) return;
  flag_on(ram, r, addr, next);
  flag_off(ram, r, addr, now);
}

static void new_date(uint8_t *ram, HmRegs *r)
{
  uint8_t wd = (uint8_t)(hm_bus_rd8(ram, WEEKDAY) + 1);
  r->c = wd >= 7;
  if (wd == 7) wd = 0;
  set_a8(r, wd);
  hm_bus_wr8(ram, WEEKDAY, wd);
  uint8_t day = (uint8_t)(hm_bus_rd8(ram, DAY) + 1);
  set_a8(r, day);
  r->c = day >= 0x1F;
  if (day == 0x1F) {
    uint8_t season = (uint8_t)(hm_bus_rd8(ram, SEASON) + 1);
    set_a8(r, season);
    r->c = season >= 4;
    if (season == 4) {
      uint8_t year = (uint8_t)(hm_bus_rd8(ram, YEAR) + 1);
      set_a8(r, year);
      hm_bus_wr8(ram, YEAR, year);
      r->a = 0x0004;                                       /* REP #$20 */
      hm_push8(ram, r, 0x82); hm_push8(ram, r, 0xE5);      /* JSR */
      hm_LoadsDamageProbabilities(ram, r);
      hm_pull8(ram, r); hm_pull8(ram, r);
      jsl(ram, r, 0x82E6, 0x82, hm_ClimateFarmDamageCheck);
      season = 0;
      set_a8(r, 0);
    }
    hm_bus_wr8(ram, SEASON, season);
    day = 1;
    set_a8(r, 1);
  }
  hm_bus_wr8(ram, DAY, day);
}

static void story(uint8_t *ram, HmRegs *r)
{
  if (flag(ram, r, 0x7F1F66, 0x0020)) {
    flag_off(ram, r, 0x7F1F66, 0x0020);
    flag_on(ram, r, 0x7F1F66, 0x0040);
  }
  if (flag(ram, r, 0x7F1F66, 0x0080) && count8(ram, r, 0x7F1F35, 4)) {
    flag_on(ram, r, 0x7F1F66, 0x0100);
    r->a = 500;
    hm_bus_wr16(ram, 0x7F1F0E, 500);
    flag_off(ram, r, 0x7F1F66, 0x0080);
    if (flag(ram, r, 0x7F1F64, 0x0040)) flag_on(ram, r, 0x7F1F64, 0x0080);
    else flag_on(ram, r, 0x7F1F64, 0x0040);
  }
  if (flag(ram, r, 0x7F1F68, 0x0800)) {
    flag_off(ram, r, 0x7F1F68, 0x0800);
    flag_on(ram, r, 0x7F1F68, 0x1000);
  }
  if (flag(ram, r, 0x7F1F68, 0x8000)) {
    flag_off(ram, r, 0x7F1F68, 0x8000);
    bits_on8(ram, r, 0x7F1F02, 0x04);
  }
  if (flag(ram, r, 0x7F1F6A, 0x0080)) {
    flag_off(ram, r, 0x7F1F6A, 0x0080);
    if (!flag(ram, r, 0x7F1F6A, 0x0100)) flag_on(ram, r, 0x7F1F6A, 0x0102);
    else {
      static const uint16_t places[2] = { 0x0921, 0x0923 };
      for (int i = 0; i < 2; i++) {
        uint8_t v = rd8(ram, r, places[i]);
        set_a8(r, v);
        r->c = v >= 2;
        if (v == 2) wr8(ram, r, places[i], 0);
      }
      uint8_t v = (uint8_t)(hm_bus_rd8(ram, 0x7F1F00) & 0xFD);
      set_a8(r, v);
      hm_bus_wr8(ram, 0x7F1F00, v);
      bits_on8(ram, r, 0x7F1F02, 0x02);
    }
  }
  if (flag(ram, r, 0x7F1F6A, 0x0400) && !flag(ram, r, 0x7F1F6A, 0x0800)) {
    flag_on(ram, r, 0x7F1F6A, 0x0800);
    flag_off(ram, r, 0x7F1F6A, 0x0400);
  }
  visit_nights(ram, r, 0x7F1F6A, 0x1000, 0x2000);
  visit_nights(ram, r, 0x7F1F6A, 0x4000, 0x8000);
  visit_nights(ram, r, 0x7F1F6C, 0x0001, 0x0002);
  visit_nights(ram, r, 0x7F1F6C, 0x0004, 0x0008);
  visit_nights(ram, r, 0x7F1F6C, 0x0010, 0x0020);
  if (flag(ram, r, 0x7F1F6E, 0x8000)) {
    flag_off(ram, r, 0x7F1F6E, 0x8000);
    bits_on8(ram, r, 0x7F1F02, 0x01);
  }
  r->a = (uint16_t)(rd16(ram, r, 0x0196) & 0x0010);
  if (r->a) flag_on(ram, r, 0x7F1F60, 0x0001);
}

void hm_NightReset(uint8_t *ram, HmRegs *r)
{
  new_date(ram, r);
  set_a8(r, 0xFF);
  wr8(ram, r, 0x0117, 0xFF);
  jsl(ram, r, 0x8301, 0x83, hm_UNK_Audio25);
  set_a8(r, 0x0F); *hm_dp(ram, r, 0x92) = 0x0F;
  set_a8(r, 0x03); *hm_dp(ram, r, 0x93) = 0x03;
  set_a8(r, 0x01); *hm_dp(ram, r, 0x94) = 0x01;
  jsl(ram, r, 0x8313, 0x80, hm_ScreenFadeout);
  jsl(ram, r, 0x8317, 0x80, hm_ForceBlank);
  set_a8(r, 0x0F);
  *hm_dp(ram, r, 0x22) = 0x0F;

  flag_off(ram, r, 0x7F1F64, 0x0030);                      /* REP #$30 */
  flag_off(ram, r, 0x7F1F70, 0x0002);
  r->a = (uint16_t)(rd16(ram, r, 0x0196) & 0x0010);
  if (r->a) flag_on(ram, r, 0x7F1F64, 0x0020);
  jsl(ram, r, 0x834E, 0x82, hm_SetWeatherFlags);
  r->a = (uint16_t)(rd16(ram, r, 0x0196) & 0x0200);
  if (r->a) flag_on(ram, r, 0x7F1F64, 0x0010);
  r->a = (uint16_t)(rd16(ram, r, 0x0196) & 0x0100);
  if (r->a) flag_on(ram, r, 0x7F1F70, 0x0002);
  jsl(ram, r, 0x837C, 0x80, hm_UNK_Audio5_95F5);

  set_a8(r, 0x06);                                         /* SEP #$20 */
  hm_bus_wr8(ram, HOUR, 6);
  set_a8(r, 0);
  hm_bus_wr8(ram, MINUTES, 0);
  hm_bus_wr8(ram, SECONDS, 0);
  jsl(ram, r, 0x8394, 0x82, hm_LoadsDateNames);
  jsl(ram, r, 0x8398, 0x82, hm_NightlyFarmTilesCheck);
  jsl(ram, r, 0x839C, 0x82, hm_ClimateFarmDamagePrep);
  jsl(ram, r, 0x83A0, 0x83, hm_CowFeedingandStatus);
  jsl(ram, r, 0x83A4, 0x82, hm_WeatherTomorrow);
  jsl(ram, r, 0x83A8, 0x82, hm_FindMostLovedName);
  jsl(ram, r, 0x83AC, 0x82, hm_WifePregnanacyandChilds);

  wr16(ram, r, 0x0915, 0);                                 /* REP #$20 */
  hm_dp_w16(ram, r, 0xD2, 0);
  hm_dp_w16(ram, r, 0xD4, 0);
  uint8_t max = rd8(ram, r, 0x0917);                       /* SEP #$20 */
  set_a8(r, max);
  wr8(ram, r, 0x0918, max);
  wr8(ram, r, 0x0925, 0);
  r->a = (uint16_t)(hm_dp_r16(ram, r, 0xD2) | 0x0001);     /* REP #$30 */
  hm_dp_w16(ram, r, 0xD2, r->a);
  r->a = 0;
  hm_dp_w16(ram, r, 0xD4, 0);
  hm_dp_w16(ram, r, 0xDA, 0);
  wr16(ram, r, 0x0911, 0);
  wr16(ram, r, 0x0901, 0);
  r->a = hm_bus_rd16(ram, 0x7F1F07);                       /* the shipped goods' money */
  hm_dp_w16(ram, r, 0x72, r->a);
  set_a8(r, hm_bus_rd8(ram, 0x7F1F09));
  *hm_dp(ram, r, 0x74) = (uint8_t)r->a;
  jsl(ram, r, 0x83FB, 0x83, hm_AddMoney);
  r->a = 0;
  hm_bus_wr16(ram, 0x7F1F07, 0);
  hm_bus_wr8(ram, 0x7F1F09, 0);
  wr8(ram, r, 0x0930, 0);                                  /* the animals fed */
  wr8(ram, r, 0x0931, 0);
  wr16(ram, r, 0x0932, 0);
  wr16(ram, r, 0x0934, 0);
  static const uint32_t days_flags[] = { 0x7F1F5A, 0x7F1F5C, 0x7F1F5E, 0x7F1F60, 0x7F1F62,
                                         0x7F1F74, 0x7F1F76, 0x7F1F78 };
  for (unsigned i = 0; i < 8; i++) hm_bus_wr16(ram, days_flags[i], 0);
  flag_on(ram, r, 0x7F1F68, 0x0001);
  story(ram, r);

  uint8_t clock = rd8(ram, r, 0x0973);                     /* SEP #$20 */
  set_a8(r, clock & 0x04);
  if (clock & 0x04) {
    r->a = rd8(ram, r, 0x098E);                            /* LDA #0; XBA; LDA */
    jsl(ram, r, 0x870E, 0x83, hm_SaveGameSlot);
  }
  r->a = (uint16_t)((rd16(ram, r, 0x0124) & 0x2030) ^ 0x2030);   /* REP #$20 */
  if (!r->a) {
    set_a8(r, 0x02); hm_bus_wr8(ram, YEAR, 0x02);
    set_a8(r, 0x01); hm_bus_wr8(ram, SEASON, 0x01);
    set_a8(r, 0x1E); hm_bus_wr8(ram, DAY, 0x1E);
  }
  wr8(ram, r, 0x0973, 0);                                  /* SEP #$20 */
  wr8(ram, r, 0x091D, 0);
  r->a = (uint16_t)(0xFFFD & hm_dp_r16(ram, r, 0xD2));     /* REP #$30 */
  hm_dp_w16(ram, r, 0xD2, r->a);
  r->a = 0x0088;
  wr16(ram, r, 0x017D, 0x0088);
  r->a = 0x0078;
  wr16(ram, r, 0x017F, 0x0078);
  set_a8(r, 0x15);                                         /* SEP #$20 */
  wr8(ram, r, 0x098B, 0x15);
  jsl(ram, r, 0x875E, 0x80, hm_UNK_ScreenTransition);
  set_a8(r, 0x14);                                         /* SEP #$20; REP #$10; LDA #$14 */
  r->n = 0;                                                /* (ChangeStamina reads its N) */
  jsl(ram, r, 0x8768, 0x81, hm_ChangeStamina);
  r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F5E) & 0x0004);  /* REP #$30 */
  if (r->a) return;
  set_a8(r, 0x03);
  jsl(ram, r, 0x877B, 0x80, hm_RNGReturn0toA);
  wr8(ram, r, 0x0924, (uint8_t)r->a);
  r->a = (uint16_t)(hm_dp_r16(ram, r, 0xD2) | 0x0004);
  hm_dp_w16(ram, r, 0xD2, r->a);
}

/* ─────────────────────────────────────────────────────────────────────────
 * UpdateTime ($82:8000, JSL, RTL; once a frame): the clock ($0973).
 *  - Bit 1 (the player went to bed): the new day (NightReset).
 *  - Bit 0 off: the clock is stopped.
 *  - $7F1F5A bit 10: the shipping box's talk (ShippingSceneDialogue)
 *    instead.
 *  - A second a frame; 60 seconds a minute and 15 minutes an hour (so an
 *    hour is 900 frames, 15 s); the clock does not go past 18:00. On each
 *    new hour, the lunch (HaveLunch) and the shipping (ShippingScene); at
 *    18:00, unless $7F1F5E bit 15, the music stops (UNK_Audio25 with
 *    $0117 = $FF, then $0110 = $FF).
 *  - Then the palette of the hour (SetPaletteToLoad); if it changed, the
 *    colour cycles go (ZeroesPartial42Pointers from 4) and the new ones
 *    start (CallIndexedSubrutines).
 * Leftovers: those of the routines called, or of the tests.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_UpdateTime(uint8_t *ram, HmRegs *r)
{
  uint8_t clock = rd8(ram, r, 0x0973);                     /* SEP #$20; REP #$10 */
  set_a8(r, clock & 0x02);
  if (clock & 0x02) { hm_NightReset(ram, r); return; }     /* JMP */
  set_a8(r, clock & 0x01);
  if (!(clock & 0x01)) return;
  r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F5A) & 0x0400);  /* REP #$20 */
  if (r->a) { hm_ShippingSceneDialogue(ram, r); return; }  /* JMP */
  if (count8(ram, r, SECONDS, 60)) {
    set_a8(r, 0);
    hm_bus_wr8(ram, SECONDS, 0);
    if (count8(ram, r, MINUTES, 15)) {
      set_a8(r, 0);
      hm_bus_wr8(ram, MINUTES, 0);
      uint8_t hour = hm_bus_rd8(ram, HOUR);
      set_a8(r, hour);
      r->c = hour >= 18;
      if (hour != 18) {
        set_a8(r, (uint8_t)(hour + 1));
        hm_bus_wr8(ram, HOUR, (uint8_t)(hour + 1));
        jsl(ram, r, 0x805B, 0x82, hm_HaveLunch);
        jsl(ram, r, 0x805F, 0x82, hm_ShippingScene);
        hour = hm_bus_rd8(ram, HOUR);                      /* SEP #$20 */
        set_a8(r, hour);
        r->c = hour >= 18;
        if (hour == 18) {
          r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F5E) & 0x8000);   /* REP #$20 */
          if (!r->a) {
            set_a8(r, 0xFF);
            wr8(ram, r, 0x0117, 0xFF);
            jsl(ram, r, 0x8081, 0x83, hm_UNK_Audio25);
            set_a8(r, 0xFF);
            wr8(ram, r, 0x0110, 0xFF);
          }
        }
      }
    }
  }
  uint8_t palette = rd8(ram, r, 0x017B);                   /* SEP #$20 */
  set_a8(r, palette);
  hm_push8(ram, r, palette);                               /* PHA */
  jsl(ram, r, 0x8092, 0x80, hm_SetPaletteToLoad);
  palette = hm_pull8(ram, r);                              /* SEP #$20; PLA */
  set_a8(r, palette);
  uint8_t now = rd8(ram, r, 0x017B);
  r->c = palette >= now;
  if (palette == now) return;
  r->y = 0x0004;
  jsl(ram, r, 0x80A1, 0x80, hm_ZeroesPartial42Pointers);
  jsl(ram, r, 0x80A5, 0x80, hm_CallIndexedSubrutines);
}

/* ─────────────────────────────────────────────────────────────────────────
 * FirstNightReset ($83:ABF0, JSL, RTL; when a game starts, new or loaded):
 * the day's state from zero: the scroll and DMA bytes, the money shipped,
 * the date's names (LoadsDateNames), the player's state and position
 * ($D2 = 1, action 0, facing down, stamina full), what he holds, the
 * sounds ($0110-$0118), the animals fed, the map ($098B = 0), the day's
 * flags ($7F1F5A-$7F1F62, $7F1F74-$7F1F7A), 6:00:00; the wood the next
 * house needs: 250, or 500 once it was built once ($7F1F64 bit 6). Then
 * one frame (WaitForNMI).
 * Leftovers: those of WaitForNMI.
 * ───────────────────────────────────────────────────────────────────────── */
static void zero8(uint8_t *ram, HmRegs *r, const uint16_t *a, unsigned n)
{
  for (unsigned i = 0; i < n; i++) wr8(ram, r, a[i], 0);
}
static void zero16(uint8_t *ram, HmRegs *r, const uint16_t *a, unsigned n)
{
  for (unsigned i = 0; i < n; i++) wr16(ram, r, a[i], 0);
}
static void zero_dp8(uint8_t *ram, HmRegs *r, const uint8_t *a, unsigned n)
{
  for (unsigned i = 0; i < n; i++) *hm_dp(ram, r, a[i]) = 0;
}
static void zero_dp16(uint8_t *ram, HmRegs *r, const uint8_t *a, unsigned n)
{
  for (unsigned i = 0; i < n; i++) hm_dp_w16(ram, r, a[i], 0);
}
#define N(t) (sizeof t / sizeof t[0])

void hm_FirstNightReset(uint8_t *ram, HmRegs *r)
{
  static const uint16_t w1[] = { 0x0183, 0x0185, 0x0187 };
  static const uint16_t b1[] = { 0x019B, 0x019A };
  static const uint8_t dpw1[] = { 0x04, 0x42, 0x45, 0x48 };
  static const uint8_t dpb1[] = { 0x28, 0x06, 0x44, 0x47, 0x4A };
  zero16(ram, r, w1, N(w1));
  zero8(ram, r, b1, N(b1));
  zero_dp16(ram, r, dpw1, N(dpw1));
  zero_dp8(ram, r, dpb1, N(dpb1));
  r->a = 0;
  hm_bus_wr16(ram, 0x7F1F07, 0);
  hm_bus_wr8(ram, 0x7F1F09, 0);
  jsl(ram, r, 0xAC2E, 0x82, hm_LoadsDateNames);

  static const uint16_t b2[] = { 0x096D, 0x098A, 0x0972, 0x0919, 0x0993, 0x0994, 0x018B };
  static const uint16_t w2[] = { 0x0878, 0x087A, 0x0973, 0x0907, 0x0909, 0x0991 };
  static const uint8_t dpw2[] = { 0xD2, 0xE9, 0xEB, 0xCF, 0xD6, 0xD8 };
  static const uint8_t dpb2[] = { 0xD1 };
  zero8(ram, r, b2, N(b2));
  zero16(ram, r, w2, N(w2));
  zero_dp16(ram, r, dpw2, N(dpw2));
  zero_dp8(ram, r, dpb2, N(dpb2));
  set_a8(r, 0x06);                                         /* SEP #$20 */
  hm_bus_wr8(ram, HOUR, 6);
  set_a8(r, 0);
  hm_bus_wr8(ram, MINUTES, 0);
  hm_bus_wr8(ram, SECONDS, 0);
  r->a = 0;                                                /* REP #$20 */
  for (uint32_t f = 0x7F1F5A; f <= 0x7F1F62; f += 2) hm_bus_wr16(ram, f, 0);
  for (uint32_t f = 0x7F1F74; f <= 0x7F1F7A; f += 2) hm_bus_wr16(ram, f, 0);
  r->a = (uint16_t)(hm_dp_r16(ram, r, 0xD2) | 0x0001);     /* REP #$30 */
  hm_dp_w16(ram, r, 0xD2, r->a);
  r->a = 0;
  hm_dp_w16(ram, r, 0xD4, 0);
  hm_dp_w16(ram, r, 0xDA, 0);
  wr16(ram, r, 0x0911, 0);
  wr16(ram, r, 0x0901, 0);
  wr16(ram, r, 0x0915, 0);
  uint8_t max = rd8(ram, r, 0x0917);                       /* SEP #$20 */
  set_a8(r, max);
  wr8(ram, r, 0x0918, max);

  static const uint16_t b3[] = { 0x091A, 0x091B, 0x0925, 0x0990, 0x091D, 0x091E, 0x091F, 0x0920,
                                 0x096B, 0x098F, 0x096C, 0x0922, 0x0110, 0x0114, 0x0117, 0x0118,
                                 0x0930, 0x0931, 0x098B, 0x0880 };
  static const uint16_t w3[] = { 0x0932, 0x0934, 0x084A, 0x087C, 0x087E };
  zero8(ram, r, b3, N(b3));
  zero16(ram, r, w3, N(w3));
  hm_dp_w16(ram, r, 0xDC, 0);
  r->a = 250;                                              /* REP #$30 */
  hm_bus_wr16(ram, 0x7F1F0E, 250);
  if (flag(ram, r, 0x7F1F64, 0x0040)) { r->a = 500; hm_bus_wr16(ram, 0x7F1F0E, 500); }
  *hm_dp(ram, r, 0x00) = 0;                                /* SEP #$20 */
  wr8(ram, r, 0x0148, 0);
  jsl(ram, r, 0xAD8C, 0x80, hm_WaitForNMI);
}

/* ─────────────────────────────────────────────────────────────────────────
 * SUB_82878E ($82:878E): BRA to itself, for ever. Nothing calls it (a
 * leftover between NightReset and WifePregnanacyandChilds). Its check
 * stops after one turn, when it comes back to its start (HM_TOLOOP): it
 * changes nothing.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_SUB_82878E(uint8_t *ram, HmRegs *r) { (void)ram; (void)r; }
