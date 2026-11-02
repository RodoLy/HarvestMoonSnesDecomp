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

/* ── Names of the memory locations used here ──────────────────────────────
 * Also used, from hm_ram.h: HM_MAP_ID ($22), HM_GAME_STATE ($D2),
 * HM_PLAYER_ACTION ($D4), HM_ANIM_REQUEST ($0901), HM_MAX_STAMINA ($0917),
 * HM_CURRENT_STAMINA ($0918), HM_ITEM_ON_HAND ($091D), HM_TOOL_SELECTED
 * ($0921), HM_TOOL_BACKPACK ($0923), HM_EXHAUSTION ($096C), HM_TIME_RUNNING ($0973), HM_INPUT_STATE ($019A),
 * HM_TEXT_X ($0185), HM_SHED_ITEMS ($7F:1F00) and HM_SHIPPING_MONEY
 * ($7F:1F07). */
#define WRAM_LONG(a)      (0x7E0000 + (a))   /* a ram[] offset as a bus address */

/* The calendar (bus addresses, $7F:1F18-$7F:1F1E) */
#define YEAR     0x7F1F18   /* 8b: 0 = the first year (HM_YEAR) */
#define SEASON   0x7F1F19   /* 8b: 0 spring ... 3 winter (HM_SEASON) */
#define WEEKDAY  0x7F1F1A   /* 8b: 0-6 (HM_WEEKDAY) */
#define DAY      0x7F1F1B   /* 8b: 1-30 (HM_DAY) */
#define HOUR     0x7F1F1C   /* 8b: 24h clock (HM_HOUR) */
#define MINUTES  0x7F1F1D   /* 8b: 0-14 (HM_MINUTES) */
#define SECONDS  0x7F1F1E   /* 8b: frames, 0-59 (HM_SECONDS) */

/* The saved state, $7F:1Fxx (ram[] offsets 0x11Fxx; WRAM_LONG gives the bus
 * address) */
#define SHED_ROW_2        (HM_SHED_ITEMS + 2) /* 8b: the shed's third byte (bit 1 the
                                      golden hoe, bit 2 the golden hammer) */
#define SHIPPING_MONEY_HIGH 0x11F09 /* 8b: the third byte of HM_SHIPPING_MONEY */
#define LUMBER_NEEDED     0x11F0E  /* 16b: the wood the next house extension needs */
#define BUILD_NIGHTS      0x11F35  /* 8b: nights the house has been building */
#define FLAGS_5A          0x11F5A  /* 16b day flags: $0400 the shipping box's talk */
#define FLAGS_5C          0x11F5C  /* 16b day flags */
#define FLAGS_5E          0x11F5E  /* 16b day flags: $04 no breakfast, $8000 the music
                                      goes on after 18:00 */
#define FLAGS_60          0x11F60  /* 16b day flags: $01 set with $0196 bit 4 */
#define FLAGS_62          0x11F62  /* 16b day flags */
#define FLAGS_64          0x11F64  /* 16b story flags: $10 / $20 today's special days,
                                      $40 / $80 the house's first / second extension */
#define FLAGS_66          0x11F66  /* 16b story flags: $20 -> $40; $80 the house is being
                                      built, $0100 it is done */
#define FLAGS_68          0x11F68  /* 16b story flags: $01 a day went by; $0800 -> $1000;
                                      $8000 the golden hammer */
#define FLAGS_6A          0x11F6A  /* 16b story flags: $80 the golden hoe; $0400 -> $0800;
                                      visits $1000 / $4000 */
#define FLAGS_6C          0x11F6C  /* 16b story flags: visits $01, $04, $10 */
#define FLAGS_6E          0x11F6E  /* 16b story flags: $8000 a tool back to the shed */
#define FLAGS_70          0x11F70  /* 16b: $02 today's special day ($0196 bit 8) */
#define FLAGS_74          0x11F74  /* 16b day flags */
#define FLAGS_76          0x11F76  /* 16b day flags */
#define FLAGS_78          0x11F78  /* 16b day flags */
#define FLAGS_7A          0x11F7A  /* 16b: the scene flags */

/* WRAM of the data bank ($0100-$1FFF; through db / rd8 / wr8) */
#define WANTED_SONG       0x0110   /* 8b: the song asked for */
#define SOUND_EFFECT      0x0114   /* 8b: the sound effect to play */
#define PLAYING_SONG      0x0117   /* 8b: the song playing */
#define EFFECTS_SONG      0x0118   /* 8b: the song whose instrument the effects use */
#define JOY_HELD          0x0124   /* 16b: Joy1's buttons held */
#define UNKNOWN_0148      0x0148   /* 8b: cleared here and by reset.c */
#define LOADED_PALETTE    0x017B   /* 8b: the palette in use (palette.c) */
#define TRANSITION_X      0x017D   /* 16b: where the player appears after a transition */
#define TRANSITION_Y      0x017F   /* 16b */
#define TEXT_NUMBER       0x0183   /* 16b: the text being shown */
#define TEXT_CODE_AT      0x0187   /* 16b: which code of the text the printer is at */
#define ARROW_BLINK       0x018B   /* 8b: the text box arrow's blink */
#define TEXT_BOX_STATE    0x019B   /* 8b: the text box's state bits */
#define OAM_SIZE_USED     0x084A   /* 16b: bytes of the sprite table filled (objects.c) */
#define PLAYER_TILE_1     0x0878   /* 16b: the tile types under the player's front corners */
#define PLAYER_TILE_2     0x087A   /* 16b */
#define CAMERA_SPEED_X    0x087C   /* 16b: the camera's own movement per frame */
#define CAMERA_SPEED_Y    0x087E   /* 16b */
#define CAMERA_FRAMES     0x0880   /* 8b: for how many frames */
#define FOOTPRINT_X       0x0907   /* 16b: where the last footprint was left */
#define FOOTPRINT_Y       0x0909   /* 16b */
#define PLAYER_TURN       0x0911   /* 16b: the direction being tried (playerinput.c) */
#define PLAYER_TIMER      0x0915   /* 16b: the player's animation timer */
#define STEP_COUNT        0x0919   /* 8b: moves since the last footprint */
#define STEP_SOUND_TIMER  0x091A   /* 8b: frames to the next step sound */
#define TOOL_FRAMES       0x091B   /* 8b: frames of the tool's use */
#define SHOWN_ITEM        0x091E   /* 8b: the item the player held up */
#define PICKED_TILE_BYTE  0x091F   /* 8b: byte +0 of the tile picked up */
#define CARRIED_SLOT      0x0920   /* 8b: the CC slot being carried */
#define TOOL_SOUND_CHOICE 0x0922   /* 8b: what picks the tool's sound */
#define BREAKFAST         0x0924   /* 8b: the breakfast (0-3, stamina.c HM_LUNCH_CHOICE) */
#define IDLE_TIMER        0x0925   /* 8b: frames standing idle */
#define COWS_FED          0x0930   /* 8b: troughs filled today */
#define CHICKENS_FED      0x0931   /* 8b */
#define COW_TROUGHS       0x0932   /* 16b: which troughs have food */
#define CHICKEN_TROUGHS   0x0934   /* 16b */
#define SOW_STEP          0x096B   /* 8b: which tile of a multi-tile use (HM_SOW_STEP) */
#define HIT_COUNTER       0x096D   /* 8b: the hammer's and the axe's hits */
#define UNKNOWN_0972      0x0972   /* 8b: cleared by FirstNightReset, not read here */
#define DOOR_STEP         0x098A   /* 8b: frames of the walk out of a door */
#define TRANSITION_MAP    0x098B   /* 8b: the map a transition goes to */
#define SAVE_SLOT         0x098E   /* 8b: the diary slot (0 or 1) */
#define UNKNOWN_098F      0x098F   /* 8b: cleared by FirstNightReset, not read here */
#define TV_FORECAST       0x0990   /* 8b (weather.c) */
#define NAME_CELL         0x0991   /* 16b: the name screen's cursor cell (menus.c) */
#define NAME_PAGE         0x0993   /* 8b: the name screen's page */
#define NAME_COUNT        0x0994   /* 8b: letters of the name typed */
#define WEATHER_FLAGS     0x0196   /* 16b: weather and day flags; bits 4, 8, 9 special days */
#define VISIT_NIGHTS      0x09A3   /* 8b: nights counted for a visit (to 3) */

/* Direct page */
#define NMI_STATUS        0x00   /* 8b: set by the NMI */
#define PALETTE_TARGET    0x04   /* 24b $04-$06: the palette the colours move to */
#define PALETTE_TARGET_BANK 0x06
#define DMA_WAITING       0x28   /* 8b: the DMA channels to start, one bit each */
#define COLOUR_LIST_0     0x42   /* 24b: colour animation channel 0's list ($42 + 3 n) */
#define COLOUR_LIST_0_BANK 0x44
#define COLOUR_LIST_1     0x45   /* 24b: channel 1's */
#define COLOUR_LIST_1_BANK 0x47
#define COLOUR_LIST_2     0x48   /* 24b: channel 2's */
#define COLOUR_LIST_2_BANK 0x4A
#define MONEY_AMOUNT      0x72   /* 24b $72-$74: what AddMoney adds */
#define MONEY_AMOUNT_BANK 0x74   /* 8b: its third byte */
#define FADE_FROM         0x92   /* 8b: ScreenFadeOut's starting brightness */
#define FADE_SPEED        0x93   /* 8b: frames per brightness level */
#define FADE_TO           0x94   /* 8b: the brightness it ends at */
#define MAP_ANIM          0xCF   /* long $CF-$D1: a map animation's next step */
#define MAP_ANIM_BANK     0xD1
#define PLAYER_X          0xD6   /* 16b: the player's position */
#define PLAYER_Y          0xD8   /* 16b */
#define PLAYER_DIRECTION  0xDA   /* 16b: the way the player faces: 0 down */
#define GOBJ_COUNT        0xDC   /* 16b: game objects handed out so far (objects.c) */
#define COLLIDE_TILE_1    0xE9   /* 16b: CollideMap's output: the first corner's tile type */
#define COLLIDE_TILE_2    0xEB   /* 16b: the second corner's */

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
 *    LoadDamageProbabilities) and ClimateFarmDamageCheck.
 *  - The music stops (FadeOutSongSlowly with $0117 = $FF), the screen fades out
 *    (from 15, 3 frames a level, to 1) and is blanked; map $0F for the
 *    music of the day.
 *  - Today's special days: $7F1F64 bits 4-5 and $7F1F70 bit 1 from
 *    $0196 bits 4, 9 and 8, around SetWeatherFlags (which makes the new
 *    $0196); the new day's music (PickMapMusic past its 6 pm test).
 *  - 6:00:00, and the night's work: LoadDateNames,
 *    NightlyFarmTilesCheck, ClimateFarmDamagePrep, CowFeedingAndStatus,
 *    WeatherTomorrow, FindMostLovedName, WifePregnancyAndChildren.
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
 *    player wakes in the house ($15) at (136, 120) (EnterMap),
 *    with 20 of stamina more (ChangeStamina); unless $7F1F5E bit 2, the
 *    breakfast ($0924 = 0-3, RNGReturn0toA) and $D2 bit 2.
 * Leftovers: those of RNGReturn0toA (or of ChangeStamina, with A = 4).
 * ───────────────────────────────────────────────────────────────────────── */
static void visit_nights(uint8_t *ram, HmRegs *r, uint32_t addr, uint16_t now, uint16_t next)
{
  if (!flag(ram, r, addr, now) || flag(ram, r, addr, next)) return;
  if (!count8(ram, r, db(r, VISIT_NIGHTS), 3)) return;
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
      hm_LoadDamageProbabilities(ram, r);
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
  if (flag(ram, r, WRAM_LONG(FLAGS_66), 0x0020)) {
    flag_off(ram, r, WRAM_LONG(FLAGS_66), 0x0020);
    flag_on(ram, r, WRAM_LONG(FLAGS_66), 0x0040);
  }
  if (flag(ram, r, WRAM_LONG(FLAGS_66), 0x0080) && count8(ram, r, WRAM_LONG(BUILD_NIGHTS), 4)) {
    flag_on(ram, r, WRAM_LONG(FLAGS_66), 0x0100);
    r->a = 500;
    hm_bus_wr16(ram, WRAM_LONG(LUMBER_NEEDED), 500);
    flag_off(ram, r, WRAM_LONG(FLAGS_66), 0x0080);
    if (flag(ram, r, WRAM_LONG(FLAGS_64), 0x0040)) flag_on(ram, r, WRAM_LONG(FLAGS_64), 0x0080);
    else flag_on(ram, r, WRAM_LONG(FLAGS_64), 0x0040);
  }
  if (flag(ram, r, WRAM_LONG(FLAGS_68), 0x0800)) {
    flag_off(ram, r, WRAM_LONG(FLAGS_68), 0x0800);
    flag_on(ram, r, WRAM_LONG(FLAGS_68), 0x1000);
  }
  if (flag(ram, r, WRAM_LONG(FLAGS_68), 0x8000)) {
    flag_off(ram, r, WRAM_LONG(FLAGS_68), 0x8000);
    bits_on8(ram, r, WRAM_LONG(SHED_ROW_2), 0x04);
  }
  if (flag(ram, r, WRAM_LONG(FLAGS_6A), 0x0080)) {
    flag_off(ram, r, WRAM_LONG(FLAGS_6A), 0x0080);
    if (!flag(ram, r, WRAM_LONG(FLAGS_6A), 0x0100)) flag_on(ram, r, WRAM_LONG(FLAGS_6A), 0x0102);
    else {
      static const uint16_t places[2] = { HM_TOOL_SELECTED, HM_TOOL_BACKPACK };
      for (int i = 0; i < 2; i++) {
        uint8_t v = rd8(ram, r, places[i]);
        set_a8(r, v);
        r->c = v >= 2;
        if (v == 2) wr8(ram, r, places[i], 0);
      }
      uint8_t v = (uint8_t)(hm_bus_rd8(ram, WRAM_LONG(HM_SHED_ITEMS)) & 0xFD);
      set_a8(r, v);
      hm_bus_wr8(ram, WRAM_LONG(HM_SHED_ITEMS), v);
      bits_on8(ram, r, WRAM_LONG(SHED_ROW_2), 0x02);
    }
  }
  if (flag(ram, r, WRAM_LONG(FLAGS_6A), 0x0400) && !flag(ram, r, WRAM_LONG(FLAGS_6A), 0x0800)) {
    flag_on(ram, r, WRAM_LONG(FLAGS_6A), 0x0800);
    flag_off(ram, r, WRAM_LONG(FLAGS_6A), 0x0400);
  }
  visit_nights(ram, r, WRAM_LONG(FLAGS_6A), 0x1000, 0x2000);
  visit_nights(ram, r, WRAM_LONG(FLAGS_6A), 0x4000, 0x8000);
  visit_nights(ram, r, WRAM_LONG(FLAGS_6C), 0x0001, 0x0002);
  visit_nights(ram, r, WRAM_LONG(FLAGS_6C), 0x0004, 0x0008);
  visit_nights(ram, r, WRAM_LONG(FLAGS_6C), 0x0010, 0x0020);
  if (flag(ram, r, WRAM_LONG(FLAGS_6E), 0x8000)) {
    flag_off(ram, r, WRAM_LONG(FLAGS_6E), 0x8000);
    bits_on8(ram, r, WRAM_LONG(SHED_ROW_2), 0x01);
  }
  r->a = (uint16_t)(rd16(ram, r, WEATHER_FLAGS) & 0x0010);
  if (r->a) flag_on(ram, r, WRAM_LONG(FLAGS_60), 0x0001);
}

void hm_NightReset(uint8_t *ram, HmRegs *r)
{
  new_date(ram, r);
  set_a8(r, 0xFF);
  wr8(ram, r, PLAYING_SONG, 0xFF);
  jsl(ram, r, 0x8301, 0x83, hm_FadeOutSongSlowly);
  set_a8(r, 0x0F); *hm_dp(ram, r, FADE_FROM) = 0x0F;
  set_a8(r, 0x03); *hm_dp(ram, r, FADE_SPEED) = 0x03;
  set_a8(r, 0x01); *hm_dp(ram, r, FADE_TO) = 0x01;
  jsl(ram, r, 0x8313, 0x80, hm_ScreenFadeOut);
  jsl(ram, r, 0x8317, 0x80, hm_ForceBlank);
  set_a8(r, 0x0F);
  *hm_dp(ram, r, HM_MAP_ID) = 0x0F;

  flag_off(ram, r, WRAM_LONG(FLAGS_64), 0x0030);           /* REP #$30 */
  flag_off(ram, r, WRAM_LONG(FLAGS_70), 0x0002);
  r->a = (uint16_t)(rd16(ram, r, WEATHER_FLAGS) & 0x0010);
  if (r->a) flag_on(ram, r, WRAM_LONG(FLAGS_64), 0x0020);
  jsl(ram, r, 0x834E, 0x82, hm_SetWeatherFlags);
  r->a = (uint16_t)(rd16(ram, r, WEATHER_FLAGS) & 0x0200);
  if (r->a) flag_on(ram, r, WRAM_LONG(FLAGS_64), 0x0010);
  r->a = (uint16_t)(rd16(ram, r, WEATHER_FLAGS) & 0x0100);
  if (r->a) flag_on(ram, r, WRAM_LONG(FLAGS_70), 0x0002);
  jsl(ram, r, 0x837C, 0x80, hm_PickMapMusicAnyHour);

  set_a8(r, 0x06);                                         /* SEP #$20 */
  hm_bus_wr8(ram, HOUR, 6);
  set_a8(r, 0);
  hm_bus_wr8(ram, MINUTES, 0);
  hm_bus_wr8(ram, SECONDS, 0);
  jsl(ram, r, 0x8394, 0x82, hm_LoadDateNames);
  jsl(ram, r, 0x8398, 0x82, hm_NightlyFarmTilesCheck);
  jsl(ram, r, 0x839C, 0x82, hm_ClimateFarmDamagePrep);
  jsl(ram, r, 0x83A0, 0x83, hm_CowFeedingAndStatus);
  jsl(ram, r, 0x83A4, 0x82, hm_WeatherTomorrow);
  jsl(ram, r, 0x83A8, 0x82, hm_FindMostLovedName);
  jsl(ram, r, 0x83AC, 0x82, hm_WifePregnancyAndChildren);

  wr16(ram, r, PLAYER_TIMER, 0);                           /* REP #$20 */
  hm_dp_w16(ram, r, HM_GAME_STATE, 0);
  hm_dp_w16(ram, r, HM_PLAYER_ACTION, 0);
  uint8_t max = rd8(ram, r, HM_MAX_STAMINA);               /* SEP #$20 */
  set_a8(r, max);
  wr8(ram, r, HM_CURRENT_STAMINA, max);
  wr8(ram, r, IDLE_TIMER, 0);
  r->a = (uint16_t)(hm_dp_r16(ram, r, HM_GAME_STATE) | 0x0001); /* REP #$30 */
  hm_dp_w16(ram, r, HM_GAME_STATE, r->a);
  r->a = 0;
  hm_dp_w16(ram, r, HM_PLAYER_ACTION, 0);
  hm_dp_w16(ram, r, PLAYER_DIRECTION, 0);
  wr16(ram, r, PLAYER_TURN, 0);
  wr16(ram, r, HM_ANIM_REQUEST, 0);
  r->a = hm_bus_rd16(ram, WRAM_LONG(HM_SHIPPING_MONEY));   /* the shipped goods' money */
  hm_dp_w16(ram, r, MONEY_AMOUNT, r->a);
  set_a8(r, hm_bus_rd8(ram, WRAM_LONG(SHIPPING_MONEY_HIGH)));
  *hm_dp(ram, r, MONEY_AMOUNT_BANK) = (uint8_t)r->a;
  jsl(ram, r, 0x83FB, 0x83, hm_AddMoney);
  r->a = 0;
  hm_bus_wr16(ram, WRAM_LONG(HM_SHIPPING_MONEY), 0);
  hm_bus_wr8(ram, WRAM_LONG(SHIPPING_MONEY_HIGH), 0);
  wr8(ram, r, COWS_FED, 0);                                /* the animals fed */
  wr8(ram, r, CHICKENS_FED, 0);
  wr16(ram, r, COW_TROUGHS, 0);
  wr16(ram, r, CHICKEN_TROUGHS, 0);
  static const uint32_t days_flags[] = {
    WRAM_LONG(FLAGS_5A), WRAM_LONG(FLAGS_5C), WRAM_LONG(FLAGS_5E), WRAM_LONG(FLAGS_60),
    WRAM_LONG(FLAGS_62), WRAM_LONG(FLAGS_74), WRAM_LONG(FLAGS_76), WRAM_LONG(FLAGS_78) };
  for (unsigned i = 0; i < 8; i++) hm_bus_wr16(ram, days_flags[i], 0);
  flag_on(ram, r, WRAM_LONG(FLAGS_68), 0x0001);
  story(ram, r);

  uint8_t clock = rd8(ram, r, HM_TIME_RUNNING);            /* SEP #$20 */
  set_a8(r, clock & 0x04);
  if (clock & 0x04) {
    r->a = rd8(ram, r, SAVE_SLOT);                         /* LDA #0; XBA; LDA */
    jsl(ram, r, 0x870E, 0x83, hm_SaveGameSlot);
  }
  r->a = (uint16_t)((rd16(ram, r, JOY_HELD) & 0x2030) ^ 0x2030); /* REP #$20 */
  if (!r->a) {
    set_a8(r, 0x02); hm_bus_wr8(ram, YEAR, 0x02);
    set_a8(r, 0x01); hm_bus_wr8(ram, SEASON, 0x01);
    set_a8(r, 0x1E); hm_bus_wr8(ram, DAY, 0x1E);
  }
  wr8(ram, r, HM_TIME_RUNNING, 0);                         /* SEP #$20 */
  wr8(ram, r, HM_ITEM_ON_HAND, 0);
  r->a = (uint16_t)(0xFFFD & hm_dp_r16(ram, r, HM_GAME_STATE)); /* REP #$30 */
  hm_dp_w16(ram, r, HM_GAME_STATE, r->a);
  r->a = 0x0088;
  wr16(ram, r, TRANSITION_X, 0x0088);
  r->a = 0x0078;
  wr16(ram, r, TRANSITION_Y, 0x0078);
  set_a8(r, 0x15);                                         /* SEP #$20 */
  wr8(ram, r, TRANSITION_MAP, 0x15);
  jsl(ram, r, 0x875E, 0x80, hm_EnterMap);
  set_a8(r, 0x14);                                         /* SEP #$20; REP #$10; LDA #$14 */
  r->n = 0;                                                /* (ChangeStamina reads its N) */
  jsl(ram, r, 0x8768, 0x81, hm_ChangeStamina);
  r->a = (uint16_t)(hm_bus_rd16(ram, WRAM_LONG(FLAGS_5E)) & 0x0004); /* REP #$30 */
  if (r->a) return;
  set_a8(r, 0x03);
  jsl(ram, r, 0x877B, 0x80, hm_RNGReturn0toA);
  wr8(ram, r, BREAKFAST, (uint8_t)r->a);
  r->a = (uint16_t)(hm_dp_r16(ram, r, HM_GAME_STATE) | 0x0004);
  hm_dp_w16(ram, r, HM_GAME_STATE, r->a);
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
 *    18:00, unless $7F1F5E bit 15, the music stops (FadeOutSongSlowly with
 *    $0117 = $FF, then $0110 = $FF).
 *  - Then the palette of the hour (SetPaletteToLoad); if it changed, the
 *    colour cycles go (StopColourAnimationsFrom from 4) and the new ones
 *    start (StartMapColourAnimations).
 * Leftovers: those of the routines called, or of the tests.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_UpdateTime(uint8_t *ram, HmRegs *r)
{
  uint8_t clock = rd8(ram, r, HM_TIME_RUNNING);            /* SEP #$20; REP #$10 */
  set_a8(r, clock & 0x02);
  if (clock & 0x02) { hm_NightReset(ram, r); return; }     /* JMP */
  set_a8(r, clock & 0x01);
  if (!(clock & 0x01)) return;
  r->a = (uint16_t)(hm_bus_rd16(ram, WRAM_LONG(FLAGS_5A)) & 0x0400); /* REP #$20 */
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
          r->a = (uint16_t)(hm_bus_rd16(ram, WRAM_LONG(FLAGS_5E)) & 0x8000); /* REP #$20 */
          if (!r->a) {
            set_a8(r, 0xFF);
            wr8(ram, r, PLAYING_SONG, 0xFF);
            jsl(ram, r, 0x8081, 0x83, hm_FadeOutSongSlowly);
            set_a8(r, 0xFF);
            wr8(ram, r, WANTED_SONG, 0xFF);
          }
        }
      }
    }
  }
  uint8_t palette = rd8(ram, r, LOADED_PALETTE);           /* SEP #$20 */
  set_a8(r, palette);
  hm_push8(ram, r, palette);                               /* PHA */
  jsl(ram, r, 0x8092, 0x80, hm_SetPaletteToLoad);
  palette = hm_pull8(ram, r);                              /* SEP #$20; PLA */
  set_a8(r, palette);
  uint8_t now = rd8(ram, r, LOADED_PALETTE);
  r->c = palette >= now;
  if (palette == now) return;
  r->y = 0x0004;
  jsl(ram, r, 0x80A1, 0x80, hm_StopColourAnimationsFrom);
  jsl(ram, r, 0x80A5, 0x80, hm_StartMapColourAnimations);
}

/* ─────────────────────────────────────────────────────────────────────────
 * FirstNightReset ($83:ABF0, JSL, RTL; when a game starts, new or loaded):
 * the day's state from zero: the scroll and DMA bytes, the money shipped,
 * the date's names (LoadDateNames), the player's state and position
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
  static const uint16_t w1[] = { TEXT_NUMBER, HM_TEXT_X, TEXT_CODE_AT };
  static const uint16_t b1[] = { TEXT_BOX_STATE, HM_INPUT_STATE };
  static const uint8_t dpw1[] = { PALETTE_TARGET, COLOUR_LIST_0, COLOUR_LIST_1, COLOUR_LIST_2 };
  static const uint8_t dpb1[] = { DMA_WAITING, PALETTE_TARGET_BANK, COLOUR_LIST_0_BANK,
                                  COLOUR_LIST_1_BANK, COLOUR_LIST_2_BANK };
  zero16(ram, r, w1, N(w1));
  zero8(ram, r, b1, N(b1));
  zero_dp16(ram, r, dpw1, N(dpw1));
  zero_dp8(ram, r, dpb1, N(dpb1));
  r->a = 0;
  hm_bus_wr16(ram, WRAM_LONG(HM_SHIPPING_MONEY), 0);
  hm_bus_wr8(ram, WRAM_LONG(SHIPPING_MONEY_HIGH), 0);
  jsl(ram, r, 0xAC2E, 0x82, hm_LoadDateNames);

  static const uint16_t b2[] = { HIT_COUNTER, DOOR_STEP, UNKNOWN_0972, STEP_COUNT, NAME_PAGE,
                                 NAME_COUNT, ARROW_BLINK };
  static const uint16_t w2[] = { PLAYER_TILE_1, PLAYER_TILE_2, HM_TIME_RUNNING, FOOTPRINT_X,
                                 FOOTPRINT_Y, NAME_CELL };
  static const uint8_t dpw2[] = { HM_GAME_STATE, COLLIDE_TILE_1, COLLIDE_TILE_2, MAP_ANIM,
                                  PLAYER_X, PLAYER_Y };
  static const uint8_t dpb2[] = { MAP_ANIM_BANK };
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
  for (uint32_t f = WRAM_LONG(FLAGS_5A); f <= WRAM_LONG(FLAGS_62); f += 2) hm_bus_wr16(ram, f, 0);
  for (uint32_t f = WRAM_LONG(FLAGS_74); f <= WRAM_LONG(FLAGS_7A); f += 2) hm_bus_wr16(ram, f, 0);
  r->a = (uint16_t)(hm_dp_r16(ram, r, HM_GAME_STATE) | 0x0001); /* REP #$30 */
  hm_dp_w16(ram, r, HM_GAME_STATE, r->a);
  r->a = 0;
  hm_dp_w16(ram, r, HM_PLAYER_ACTION, 0);
  hm_dp_w16(ram, r, PLAYER_DIRECTION, 0);
  wr16(ram, r, PLAYER_TURN, 0);
  wr16(ram, r, HM_ANIM_REQUEST, 0);
  wr16(ram, r, PLAYER_TIMER, 0);
  uint8_t max = rd8(ram, r, HM_MAX_STAMINA);               /* SEP #$20 */
  set_a8(r, max);
  wr8(ram, r, HM_CURRENT_STAMINA, max);

  static const uint16_t b3[] = { STEP_SOUND_TIMER, TOOL_FRAMES, IDLE_TIMER, TV_FORECAST,
                                 HM_ITEM_ON_HAND, SHOWN_ITEM, PICKED_TILE_BYTE, CARRIED_SLOT,
                                 SOW_STEP, UNKNOWN_098F, HM_EXHAUSTION, TOOL_SOUND_CHOICE,
                                 WANTED_SONG, SOUND_EFFECT, PLAYING_SONG, EFFECTS_SONG,
                                 COWS_FED, CHICKENS_FED, TRANSITION_MAP, CAMERA_FRAMES };
  static const uint16_t w3[] = { COW_TROUGHS, CHICKEN_TROUGHS, OAM_SIZE_USED, CAMERA_SPEED_X,
                                 CAMERA_SPEED_Y };
  zero8(ram, r, b3, N(b3));
  zero16(ram, r, w3, N(w3));
  hm_dp_w16(ram, r, GOBJ_COUNT, 0);
  r->a = 250;                                              /* REP #$30 */
  hm_bus_wr16(ram, WRAM_LONG(LUMBER_NEEDED), 250);
  if (flag(ram, r, WRAM_LONG(FLAGS_64), 0x0040)) { r->a = 500; hm_bus_wr16(ram, WRAM_LONG(LUMBER_NEEDED), 500); }
  *hm_dp(ram, r, NMI_STATUS) = 0;                          /* SEP #$20 */
  wr8(ram, r, UNKNOWN_0148, 0);
  jsl(ram, r, 0xAD8C, 0x80, hm_WaitForNMI);
}

/* ─────────────────────────────────────────────────────────────────────────
 * UnusedEndlessLoop ($82:878E): BRA to itself, for ever. Nothing calls it (a
 * leftover between NightReset and WifePregnancyAndChildren). Its check
 * stops after one turn, when it comes back to its start (HM_TOLOOP): it
 * changes nothing.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_UnusedEndlessLoop(uint8_t *ram, HmRegs *r) { (void)ram; (void)r; }
