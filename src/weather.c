/*
 * weather.c — tomorrow's weather.
 *
 * Every night the game decides tomorrow's weather (WeatherTomorrow) and
 * stores a code in HM_WEATHER_TOMORROW ($098C). Next morning
 * SetWeatherFlags turns that code into the flags in $0196 that the maps
 * and the TV read.
 *
 * Codes (hm_ram.h): 0 sunny, 1 rain, 2 snow, 3 hurricane, 4 and 5 the two
 * scripted storms of the first year, 6..0B festival days.
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

#define WEATHER_FLAGS      0x00196   /* 16b, written by SetWeatherFlags */
#define STORY_FLAGS_64     0x11F64   /* $7F1F64: bit 0 summer storm seen,
                                      * bit 1 winter storm seen */
#define STORY_FLAGS_6E     0x11F6E   /* $7F1F6E */
#define ITEM_FLAGS_6C      0x11F6C   /* $7F1F6C: bit 12 = turtle shell */
#define HAS_TURTLE_SHELL   0x1000

enum { SPRING, SUMMER, FALL, WINTER };

enum {   /* festival codes: the night before, "weather" becomes the festival */
  WEATHER_FLOWER_FESTIVAL  = 0x06,
  WEATHER_HARVEST_FESTIVAL = 0x07,
  WEATHER_THANKSGIVING     = 0x08,
  WEATHER_STAR_NIGHT       = 0x09,
  WEATHER_NEW_YEAR         = 0x0A,
  WEATHER_EGG_FESTIVAL     = 0x0B,
};

/* ─────────────────────────────────────────────────────────────────────────
 * SetWeatherFlags ($82:8C09, JSL, RTL). $0196 = flag for tomorrow's code,
 * from the table at $82:8CED. Rules, quirks included:
 *  - sunny only sets a flag in SUMMER (the hot-sun flag 0004);
 *  - rain in winter shows as snow (0008), snow in spring as rain (0002);
 *  - the two first-year storms also mark themselves as seen in $7F1F64
 *    (so they never repeat) and set a bit in $7F1F6E;
 *  - festival codes (6 and up) leave $0196 at 0.
 * Leftovers: X = 2 * table index when the table is indexed, A = last value
 * loaded, C = from the last comparison.
 */
static const uint16_t kWeatherFlagTable[6] = {   /* $82:8CED */
  0x0004,  /* 0: summer sun */
  0x0002,  /* 1: rain */
  0x0008,  /* 2: snow */
  0x0010,  /* 3: hurricane */
  0x0200,  /* 4: first-year winter storm */
  0x0100,  /* 5: first-year summer storm */
};

static void set_weather_flag(uint8_t *ram, HmRegs *r, int index)
{
  r->x = (uint16_t)(index * 2);
  r->a = kWeatherFlagTable[index];
  hm_wr16(ram, WEATHER_FLAGS, r->a);
}

static void mark_story_storm(uint8_t *ram, HmRegs *r, uint16_t bit64, uint16_t bit6e)
{
  hm_wr16(ram, STORY_FLAGS_64, hm_rd16(ram, STORY_FLAGS_64) | bit64);
  r->a = hm_rd16(ram, STORY_FLAGS_6E) | bit6e;
  hm_wr16(ram, STORY_FLAGS_6E, r->a);
}

void hm_SetWeatherFlags(uint8_t *ram, HmRegs *r)
{
  uint8_t weather = ram[HM_WEATHER_TOMORROW];
  uint8_t season = ram[HM_SEASON];
  hm_wr16(ram, WEATHER_FLAGS, 0);
  r->a = weather;                                /* LDA #0; XBA; LDA weather */
  r->c = 1;                                      /* CMP #n with weather >= n */

  switch (weather) {
  case HM_WEATHER_SUNNY:
    r->a = season;
    r->c = season >= SUMMER;
    if (season == SUMMER) {                      /* no LDX here: X untouched */
      r->a = kWeatherFlagTable[0];
      hm_wr16(ram, WEATHER_FLAGS, r->a);
    }
    break;
  case HM_WEATHER_RAIN:
    r->c = season >= WINTER;
    set_weather_flag(ram, r, season == WINTER ? 2 : 1);
    break;
  case HM_WEATHER_SNOW:
    set_weather_flag(ram, r, season == SPRING ? 1 : 2);
    break;
  case HM_WEATHER_HURRICANE:
    set_weather_flag(ram, r, 3);
    break;
  case HM_WEATHER_FAIR_STORY:
    set_weather_flag(ram, r, 4);
    mark_story_storm(ram, r, 0x0002, 0x0100);
    break;
  case HM_WEATHER_THUNDER:
    set_weather_flag(ram, r, 5);
    mark_story_storm(ram, r, 0x0001, 0x0080);
    break;
  default:                                       /* festivals: no flag */
    break;
  }
}

/* ─────────────────────────────────────────────────────────────────────────
 * WeatherTomorrow ($82:8CF9, JSL, RTL). Runs at night with TODAY's date.
 *
 * 1. Festival eves: tomorrow is the festival.
 *      Spring 22 -> flower festival      Fall 11   -> harvest festival
 *      Fall 19   -> egg festival         Winter 9  -> thanksgiving
 *      Winter 23 -> star night           Winter 30 -> new year
 * 2. First year only, once each:
 *      Winter: guaranteed storm on day 8 (decided on the 7th), before that
 *              1 chance in 8 every night.          (code 4, flag 64 bit 1)
 *      Summer: guaranteed storm on day 30 (decided on the 29th), before
 *              that 1 chance in 30 every night.    (code 5, flag 64 bit 0)
 * 3. Summer hurricane (any year, not decided on day 30): 1 in 30, or
 *    1 in 60 if the player has the turtle shell.
 * 4. Rain: 1 in 6 in spring, 1 in 10 in summer and fall, never in winter.
 * 5. Snow (only winter): 1 in 3.
 * 6. Otherwise sunny.
 * "1 in n" is RNGReturn0toA(n) == 0, which is slightly more likely than
 * 1/n (see rng.c).
 *
 * Quirks: the first-year winter storm is checked on any winter night that
 * is not a festival eve, but the guaranteed one only on day 7; on the 29th
 * of summer the summer storm skips the dice. A season above 3 (never
 * happens) reads past the chance tables, here as in the original.
 * ───────────────────────────────────────────────────────────────────────── */
/* The chance tables, one byte per season, read from the ROM (a season
 * above 3, which the game never has, reads on past them, as the
 * original does) */
#define kHurricaneChance 0x828EB2u   /* 0, 30, 0, 0 */
#define kRainChance      0x828EB6u   /* 6, 10, 10, 0 */
#define kSnowChance      0x828EBAu   /* 0, 0, 0, 3 */
#define kThunderChance   0x828EBEu   /* 0, 30, 0, 0 */
#define kSnowstormChance 0x828EC2u   /* 0, 0, 0, 8 */
static uint8_t chance(uint8_t *ram, uint32_t table, uint8_t season) { return hm_bus_rd8(ram, table + season); }

/* Where each JSL RNGReturn0toA returns to (its frame stays on the stack). */
#define RET_SNOWSTORM  0x8DCD
#define RET_THUNDER    0x8E11
#define RET_HURRICANE  0x8E58
#define RET_RAIN       0x8E7B
#define RET_SNOW       0x8E9E

/* JSL RNGReturn0toA with `faces` in A; true when it rolled 0. */
static bool roll_zero(uint8_t *ram, HmRegs *r, uint8_t faces, uint16_t ret)
{
  r->a = faces;
  hm_jsl(ram, r, ret, 0x80);                     /* JSL.L $80:89F9 */
  hm_RNGReturn0toA(ram, r);
  hm_rtl(ram, r);
  return r->a == 0;
}

/* "1 chance in table[season]": LDA #0; XBA; LDA season; TAX; LDA table,X */
static bool one_in(uint8_t *ram, HmRegs *r, uint32_t table, uint16_t ret)
{
  uint8_t season = ram[HM_SEASON];
  r->x = season;
  uint8_t n = chance(ram, table, season);
  r->a = n;
  if (n == 0)
    return false;
  return roll_zero(ram, r, n, ret);
}

static void set_weather(uint8_t *ram, HmRegs *r, uint8_t code, uint16_t a)
{
  ram[HM_WEATHER_TOMORROW] = code;
  r->a = a;
}

/* Festival eves keep A's high byte from the caller (8-bit LDA #code). */
static void set_festival(uint8_t *ram, HmRegs *r, uint8_t code)
{
  set_weather(ram, r, code, (uint16_t)((r->a & 0xFF00) | code));
  r->c = 1;                                      /* CMP #day matched */
}

static void normal_day(uint8_t *ram, HmRegs *r)
{
  if (one_in(ram, r, kRainChance, RET_RAIN))
    set_weather(ram, r, HM_WEATHER_RAIN, HM_WEATHER_RAIN);
  else if (one_in(ram, r, kSnowChance, RET_SNOW))
    set_weather(ram, r, HM_WEATHER_SNOW, HM_WEATHER_SNOW);
  else
    set_weather(ram, r, HM_WEATHER_SUNNY, HM_WEATHER_SUNNY);
}

static void summer_night(uint8_t *ram, HmRegs *r)
{
  uint8_t day = ram[HM_DAY];
  r->c = 0;                                      /* CMP #3 with season 1 */

  /* First-year summer storm. */
  if (ram[HM_YEAR] == 0 && !(hm_rd16(ram, STORY_FLAGS_64) & 0x0001)) {
    r->c = day >= 29;
    if (day == 29) {
      set_weather(ram, r, HM_WEATHER_THUNDER, HM_WEATHER_THUNDER);
      return;
    }
    if (one_in(ram, r, kThunderChance, RET_THUNDER)) {
      set_weather(ram, r, HM_WEATHER_THUNDER, HM_WEATHER_THUNDER);
      return;
    }
  }

  /* Hurricane: not decided on the last day of summer. */
  r->c = day >= 30;
  if (day == 30) {
    normal_day(ram, r);
    return;
  }
  uint8_t season = ram[HM_SEASON];
  r->x = season;
  r->a = chance(ram, kHurricaneChance, season);
  if (chance(ram, kHurricaneChance, season) != 0) {
    uint16_t faces = chance(ram, kHurricaneChance, season);
    if (hm_rd16(ram, ITEM_FLAGS_6C) & HAS_TURTLE_SHELL)
      faces = (uint16_t)(faces << 1);            /* the shell halves the chance */
    r->y = faces;
    if (roll_zero(ram, r, (uint8_t)faces, RET_HURRICANE)) {
      set_weather(ram, r, HM_WEATHER_HURRICANE, HM_WEATHER_HURRICANE);
      return;
    }
  }
  normal_day(ram, r);
}

static void winter_night(uint8_t *ram, HmRegs *r)
{
  uint8_t day = ram[HM_DAY];
  if (day == 9)  { set_festival(ram, r, WEATHER_THANKSGIVING); return; }
  if (day == 23) { set_festival(ram, r, WEATHER_STAR_NIGHT); return; }
  if (day == 30) { set_festival(ram, r, WEATHER_NEW_YEAR); return; }
  r->c = day >= 30;

  /* First-year winter storm. */
  if (ram[HM_YEAR] == 0 && !(hm_rd16(ram, STORY_FLAGS_64) & 0x0002)) {
    r->c = day >= 7;
    if (day == 7) {
      set_weather(ram, r, HM_WEATHER_FAIR_STORY, HM_WEATHER_FAIR_STORY);
      return;
    }
    if (one_in(ram, r, kSnowstormChance, RET_SNOWSTORM)) {
      set_weather(ram, r, HM_WEATHER_FAIR_STORY, HM_WEATHER_FAIR_STORY);
      return;
    }
  }
  normal_day(ram, r);
}

void hm_WeatherTomorrow(uint8_t *ram, HmRegs *r)
{
  uint8_t season = ram[HM_SEASON], day = ram[HM_DAY];

  switch (season) {
  case SPRING:
    r->c = day >= 22;
    if (day == 22) set_festival(ram, r, WEATHER_FLOWER_FESTIVAL);
    else normal_day(ram, r);
    return;
  case FALL:
    if (day == 11) { set_festival(ram, r, WEATHER_HARVEST_FESTIVAL); return; }
    r->c = day >= 19;
    if (day == 19) { set_festival(ram, r, WEATHER_EGG_FESTIVAL); return; }
    normal_day(ram, r);
    return;
  case WINTER:
    winter_night(ram, r);
    return;
  default:
    summer_night(ram, r);
    return;
  }
}

/* ─────────────────────────────────────────────────────────────────────────
 * SUB_8281C0 ($82:81C0, JSL, RTL): the TV weather report. $0990 = what the
 * forecast shows (the TV code at $81:D9xx picks the text with it and the
 * season): table $82:81FD maps tomorrow's code (0-3 as they are, the two
 * scripted storms as 0, festivals 6..0B as 5..0A), then a hurricane day
 * shows 0B and the winter storm 4 (from today's flags $0196).
 * The table is read as the game does, even past its 12 entries.
 * Leftovers: X = tomorrow's code; A = 0, or the value just stored;
 * C untouched.
 * ───────────────────────────────────────────────────────────────────────── */
#define TV_FORECAST      0x00990
#define TV_FORECAST_MAP  0x8281FD

void hm_SUB_8281C0(uint8_t *ram, HmRegs *r)
{
  r->x = ram[HM_WEATHER_TOMORROW];
  ram[TV_FORECAST] = hm_bus_rd8(ram, TV_FORECAST_MAP + r->x);
  uint16_t flags = hm_rd16(ram, WEATHER_FLAGS);
  if (flags & 0x0010) {
    ram[TV_FORECAST] = 0x0B;
    r->a = 0x000B;
  } else if (flags & 0x0200) {
    ram[TV_FORECAST] = 0x04;
    r->a = 0x0204;
  } else {
    r->a = 0;
  }
}
