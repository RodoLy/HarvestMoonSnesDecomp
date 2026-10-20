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
