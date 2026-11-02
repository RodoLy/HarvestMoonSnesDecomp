/*
 * save.c — the saved games, in the cartridge's battery RAM (SRAM).
 *
 * Two slots of 4 KB: slot 1 at $70:0000, slot 2 at $70:1000 (SRAM is
 * mapped in banks $70-$7D, see hm_bus_ram). The layout of a slot (what
 * SaveGameSlot writes):
 *
 *   +00  year, season, weekday, day        +04  seed bags (grass, corn,
 *        tomato, potato, turnip), cow feed, chicken feed
 *   +0B  cows, chickens   +0D  tomorrow's weather   +0E  max stamina
 *   +0F  item in hand     +10  water in the can     +11-+1A  more flags
 *   +2E  1 = this slot was saved last   +2F  checksum (16 bits)
 *   +31  shipped corn, tomatoes, turnips, potatoes (16 bits each)
 *   +39  money (24 bits)                +3C  "FARM" (the slot is valid)
 *   +40  wood, fodder, $0196, planted grass, hearts of the 5 girls
 *   +60  event flags $7F1F64-$7F1F6A, dog position, happiness, ...
 *   +78  wife's pregnancy, the children's ages   +7E  dog hugs
 *   +80  names: player, (+84 the shed's items), dog, horse, children
 *   +98  the 13 chickens (8 bytes each)   +100  the 12 cows (16 bytes)
 *   +1C0 the farm map from tile $C0 on, to the end of the slot
 *   (+1B-+2D and +52-+5F: unused. The native build's "one seed at a
 *   time" keeps its seeds in +20 = $5E and +21-+25, its crop bag in
 *   +52-+5A, with +5B = $BA; the wife's help, the last day she helps,
 *   in +5C-+5D, with +5E = $A7.)
 *
 * The checksum is the 16-bit sum of the slot's 2048 words, taken with +2E
 * and +2F-+30 at 0.
 */
#include <string.h>
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */
#include "hm_vars.h"

#define SLOT_1        0x700000    /* SRAM: the first slot */
#define SEEDS_MARK_AT 0x20        /* the native build's seeds: +20 = $5E, +21-+25 */
#define SEEDS_MARK    0x5E
#define BAG_AT        0x52        /* the native build's crop bag: +52-+5A, +5B = $BA */
#define WIFE_AT       0x5C        /* the native build's wife's help: +5C-+5D, +5E = $A7 */
#define WIFE_MARK     0xA7
#define BAG_MARK      0xBA
#define SLOT_2        0x701000    /* SRAM: the second slot */

/* ── Names of the memory locations used here ──────────────────────────────
 * The shared variables (HM_...) are named in hm_vars.h and hm_ram.h. */

/* WRAM of the data bank (through db_addr) */
#define SAVE_SLOT         0x098E   /* 8b: the slot saved last (0 or 1) */

/* Direct page */
#define SLOT_PTR          0x72   /* 24b $72-$74: the slot in SRAM */
#define SLOT_PTR_BANK     0x74   /* 8b: its bank ($70) */
#define SAVED_SLOT_PTR    0x75   /* 24b $75-$77: LoadGameSlot keeps the slot here while
                                    LoadDefaultFarmMap uses SLOT_PTR */
#define SAVED_SLOT_PTR_BANK 0x77
#define WORK_SUM          0x7E   /* 16b: SaveGameSlot: the checksum, then the slot number */
#define STORED_SUM        0x7E   /* 16b: CheckSRAMIntegrity: the slot's stored checksum */
#define COMPUTED_SUM      0x80   /* 16b: CheckSRAMIntegrity: the sum worked out */
#define SLOT1_FAILED      0x82   /* 16b: 1 = slot 1 was cleared */
#define LAST_SAVED        0x92   /* 8b: the slot's SAVE_LAST_SAVED byte */

/* A slot's fields (offsets from SLOT_1 / SLOT_2) */
#define SAVE_YEAR           0x00
#define SAVE_SEASON         0x01
#define SAVE_WEEKDAY        0x02
#define SAVE_DAY            0x03
#define SAVE_SEEDS_GRASS    0x04   /* the seed bags */
#define SAVE_SEEDS_CORN     0x05
#define SAVE_SEEDS_TOMATO   0x06
#define SAVE_SEEDS_POTATO   0x07
#define SAVE_SEEDS_TURNIP   0x08
#define SAVE_COW_FEED       0x09
#define SAVE_CHICKEN_FEED   0x0A
#define SAVE_COWS_N         0x0B
#define SAVE_CHICKENS_N     0x0C
#define SAVE_WEATHER        0x0D   /* tomorrow's weather */
#define SAVE_MAX_STAMINA    0x0E
#define SAVE_TOOL           0x0F   /* the item in hand */
#define SAVE_WATER          0x10   /* water in the can */
#define SAVE_PREGNANCY_DAYS 0x11
#define SAVE_NEWBORN_HAPPINESS 0x12
#define SAVE_DOG_MAP        0x13
#define SAVE_HORSE_MAP      0x14
#define SAVE_HORSE_AGE      0x15
#define SAVE_BUILD_NIGHTS   0x16
#define SAVE_POWER_BERRIES  0x17
#define SAVE_VISIT_NIGHTS   0x18
#define SAVE_DEAD_COW       0x19
#define SAVE_BACKPACK       0x1A
#define SAVE_LAST_SAVED     0x2E   /* 1 = this slot was saved last */
#define SAVE_CHECKSUM       0x2F   /* 16b */
#define SAVE_SHIPPED_CORN   0x31   /* 16b each */
#define SAVE_SHIPPED_TOMATOES 0x33
#define SAVE_SHIPPED_TURNIPS 0x35
#define SAVE_SHIPPED_POTATOES 0x37
#define SAVE_MONEY          0x39   /* 16b, then */
#define SAVE_MONEY_HIGH     0x3B   /* its third byte */
#define SAVE_FARM_MARK      0x3C   /* "FARM": the slot is valid */
#define SAVE_WOOD           0x40   /* 16b */
#define SAVE_FODDER         0x42
#define SAVE_WEATHER_FLAGS  0x44
#define SAVE_PLANTED_GRASS  0x46
#define SAVE_HEARTS         0x48   /* 5 words: Maria .. Eve */
#define SAVE_FLAGS_64       0x60   /* 16b each: the story flags */
#define SAVE_FLAGS_66       0x62
#define SAVE_FLAGS_68       0x64
#define SAVE_FLAGS_6A       0x66
#define SAVE_DOG_X          0x68
#define SAVE_DOG_Y          0x6A
#define SAVE_HAPPINESS      0x6C
#define SAVE_HEN_EGGS       0x6E
#define SAVE_FLAGS_6C       0x70
#define SAVE_FLAGS_6E       0x72
#define SAVE_FLAGS_70       0x74
#define SAVE_FLAGS_72       0x76
#define SAVE_PREGNANCY      0x78
#define SAVE_KID1_AGE       0x7A
#define SAVE_KID2_AGE       0x7C
#define SAVE_DOG_HUGS       0x7E
#define SAVE_PLAYER_NAME    0x80   /* 4 bytes each */
#define SAVE_SHED           0x84   /* the shed's 4 bytes */
#define SAVE_DOG_NAME       0x88
#define SAVE_HORSE_NAME     0x8C
#define SAVE_KID1_NAME      0x90
#define SAVE_KID2_NAME      0x94
#define SAVE_CHICKEN_RECORDS 0x098 /* the 13 chickens' records */
#define SAVE_COW_RECORDS    0x100  /* the 12 cows' records */
#define SAVE_FARM_MAP       0x1C0  /* the farm map from tile $C0 on */

static uint32_t db_addr(const HmRegs *r, uint16_t a) { return (uint32_t)r->db << 16 | a; }
static void push16(uint8_t *ram, HmRegs *r, uint16_t v)
{
  hm_push8(ram, r, (uint8_t)(v >> 8));
  hm_push8(ram, r, (uint8_t)v);
}
static uint16_t pull16(uint8_t *ram, HmRegs *r)
{
  uint8_t lo = hm_pull8(ram, r);
  return (uint16_t)(lo | hm_pull8(ram, r) << 8);
}
/* $72-$74 = the slot: $70:0000 for A = 0, $70:1000 for anything else.
 * Returns A as the code leaves it (LDA #$1000; SEP; LDA #$70, or the
 * slot number back from the stack). */
static uint16_t point_at_slot(uint8_t *ram, HmRegs *r, uint16_t slot)
{
  hm_dp_w16(ram, r, SLOT_PTR, (uint16_t)SLOT_1);
  *hm_dp(ram, r, SLOT_PTR_BANK) = (uint8_t)(SLOT_1 >> 16);
  if (slot == 0) return slot;
  hm_dp_w16(ram, r, SLOT_PTR, (uint16_t)SLOT_2);
  return 0x1070;
}
static uint8_t sram8(uint8_t *ram, HmRegs *r, uint16_t y) { return hm_bus_rd8(ram, hm_dp_long(ram, r, SLOT_PTR, y)); }
static uint16_t sram16(uint8_t *ram, HmRegs *r, uint16_t y) { return hm_bus_rd16(ram, hm_dp_long(ram, r, SLOT_PTR, y)); }
static void sram_w8(uint8_t *ram, HmRegs *r, uint16_t y, uint8_t v) { hm_bus_wr8(ram, hm_dp_long(ram, r, SLOT_PTR, y), v); }
static void sram_w16(uint8_t *ram, HmRegs *r, uint16_t y, uint16_t v) { hm_bus_wr16(ram, hm_dp_long(ram, r, SLOT_PTR, y), v); }

/* What goes where: offset in the slot, the variable, its size, and whether
 * the code reads it with the data bank (LDA.W) or a long address (LDA.L). */
typedef struct { uint16_t at; uint32_t from; uint8_t size, data_bank; } Field;
static const Field FIELDS[] = {
  /* year, season, weekday, day */
  { SAVE_YEAR,                 WRAM_LONG(HM_YEAR),                1, 0 },
  { SAVE_SEASON,               WRAM_LONG(HM_SEASON),              1, 0 },
  { SAVE_WEEKDAY,              WRAM_LONG(HM_WEEKDAY),             1, 0 },
  { SAVE_DAY,                  WRAM_LONG(HM_DAY),                 1, 0 },
  /* seed bags */
  { SAVE_SEEDS_GRASS,          HM_SEEDS_GRASS_N,                  1, 1 },
  { SAVE_SEEDS_CORN,           HM_SEEDS_CORN_N,                   1, 1 },
  { SAVE_SEEDS_TOMATO,         HM_SEEDS_TOMATO_N,                 1, 1 },
  { SAVE_SEEDS_POTATO,         HM_SEEDS_POTATO_N,                 1, 1 },
  { SAVE_SEEDS_TURNIP,         HM_SEEDS_TURNIP_N,                 1, 1 },
  /* feed */
  { SAVE_COW_FEED,             HM_FEED_COW_N,                     1, 1 },
  { SAVE_CHICKEN_FEED,         HM_FEED_CHICKS_N,                  1, 1 },
  /* cows, chickens */
  { SAVE_COWS_N,               WRAM_LONG(HM_COW_N),               1, 0 },
  { SAVE_CHICKENS_N,           WRAM_LONG(HM_CHICKEN_N),             1, 0 },
  { SAVE_WEATHER,              HM_WEATHER_TOMORROW,               1, 1 },
  { SAVE_MAX_STAMINA,          HM_MAX_STAMINA,                    1, 1 },
  { SAVE_TOOL,                 HM_TOOL_SELECTED,                  1, 1 },
  { SAVE_WATER,                HM_CAN_WATER,                         1, 1 },
  { SAVE_PREGNANCY_DAYS,       WRAM_LONG(HM_PREGNANCY_DAYS),         1, 0 },
  { SAVE_NEWBORN_HAPPINESS,    WRAM_LONG(HM_NEWBORN_HAPPINESS),      1, 0 },
  /* dog map */
  { SAVE_DOG_MAP,              WRAM_LONG(HM_DOG_MAP),                1, 0 },
  { SAVE_HORSE_MAP,            WRAM_LONG(HM_HORSE_MAP),              1, 0 },
  { SAVE_HORSE_AGE,            WRAM_LONG(HM_HORSE_AGE),              1, 0 },
  /* development */
  { SAVE_BUILD_NIGHTS,         WRAM_LONG(HM_BUILD_NIGHTS),           1, 0 },
  /* power berries */
  { SAVE_POWER_BERRIES,        WRAM_LONG(HM_POWER_BERRIES),          1, 0 },
  { SAVE_VISIT_NIGHTS,         HM_VISIT_NIGHTS,                      1, 1 },
  { SAVE_DEAD_COW,             HM_DEAD_COW,                          1, 1 },
  /* backpack */
  { SAVE_BACKPACK,             HM_TOOL_BACKPACK,                  1, 1 },
  /* wood, fodder */
  { SAVE_WOOD,                 WRAM_LONG(HM_STORED_WOOD),         2, 0 },
  { SAVE_FODDER,               WRAM_LONG(HM_STORED_GRASS),        2, 0 },
  { SAVE_WEATHER_FLAGS,        HM_WEATHER_FLAGS,                     2, 1 },
  /* planted grass */
  { SAVE_PLANTED_GRASS,        WRAM_LONG(HM_PLANTED_GRASS),          2, 0 },
  /* hearts: Maria .. Eve */
  { SAVE_HEARTS,               WRAM_LONG(HM_HEARTS_BASE),         2, 0 },
  { SAVE_HEARTS + 2,           WRAM_LONG(HM_HEARTS_BASE + 2),     2, 0 },
  { SAVE_HEARTS + 4,           WRAM_LONG(HM_HEARTS_BASE + 4),     2, 0 },
  { SAVE_HEARTS + 6,           WRAM_LONG(HM_HEARTS_BASE + 6),     2, 0 },
  { SAVE_HEARTS + 8,           WRAM_LONG(HM_HEARTS_BASE + 8),     2, 0 },
  { SAVE_FLAGS_64,             WRAM_LONG(HM_FLAGS_64),               2, 0 },
  { SAVE_FLAGS_66,             WRAM_LONG(HM_FLAGS_66),               2, 0 },
  { SAVE_FLAGS_68,             WRAM_LONG(HM_FLAGS_68),               2, 0 },
  { SAVE_FLAGS_6A,             WRAM_LONG(HM_FLAGS_6A),               2, 0 },
  /* dog position */
  { SAVE_DOG_X,                WRAM_LONG(HM_DOG_X),                  2, 0 },
  { SAVE_DOG_Y,                WRAM_LONG(HM_DOG_Y),                  2, 0 },
  /* happiness */
  { SAVE_HAPPINESS,            WRAM_LONG(HM_HAPPINESS),           2, 0 },
  { SAVE_HEN_EGGS,             WRAM_LONG(HM_HEN_EGGS),               2, 0 },
  { SAVE_FLAGS_6C,             WRAM_LONG(HM_FLAGS_6C),               2, 0 },
  { SAVE_FLAGS_6E,             WRAM_LONG(HM_FLAGS_6E),               2, 0 },
  { SAVE_FLAGS_70,             WRAM_LONG(HM_FLAGS_70),               2, 0 },
  { SAVE_FLAGS_72,             WRAM_LONG(HM_FLAGS_72),               2, 0 },
  /* family */
  { SAVE_PREGNANCY,            WRAM_LONG(HM_DAYS_MARRIED),              2, 0 },
  { SAVE_KID1_AGE,             WRAM_LONG(HM_KID1_AGE),               2, 0 },
  { SAVE_KID2_AGE,             WRAM_LONG(HM_KID2_AGE),               2, 0 },
  /* shipped */
  { SAVE_SHIPPED_CORN,         WRAM_LONG(HM_SHIPPED_CORN),           2, 0 },
  { SAVE_SHIPPED_TOMATOES,     WRAM_LONG(HM_SHIPPED_TOMATOES),       2, 0 },
  { SAVE_SHIPPED_TURNIPS,      WRAM_LONG(HM_SHIPPED_TURNIPS),        2, 0 },
  { SAVE_SHIPPED_POTATOES,     WRAM_LONG(HM_SHIPPED_POTATOES),       2, 0 },
  /* dog hugs */
  { SAVE_DOG_HUGS,             WRAM_LONG(HM_DOG_HUGS),            2, 0 },
  /* money */
  { SAVE_MONEY,                WRAM_LONG(HM_MONEY),               2, 0 },
  { SAVE_MONEY_HIGH,           WRAM_LONG(HM_MONEY + 2),           1, 0 },
  /* player's name */
  { SAVE_PLAYER_NAME,          HM_PLAYER_NAME,                       1, 1 },
  { SAVE_PLAYER_NAME + 1,      HM_PLAYER_NAME + 1,                   1, 1 },
  { SAVE_PLAYER_NAME + 2,      HM_PLAYER_NAME + 2,                   1, 1 },
  { SAVE_PLAYER_NAME + 3,      HM_PLAYER_NAME + 3,                   1, 1 },
  /* the shed */
  { SAVE_SHED,                 WRAM_LONG(HM_SHED_ITEMS),          1, 0 },
  { SAVE_SHED + 1,             WRAM_LONG(HM_SHED_ITEMS + 1),      1, 0 },
  { SAVE_SHED + 2,             WRAM_LONG(HM_SHED_ITEMS + 2),      1, 0 },
  { SAVE_SHED + 3,             WRAM_LONG(HM_SHED_ITEMS + 3),      1, 0 },
  /* dog's name */
  { SAVE_DOG_NAME,             HM_DOG_NAME,                          1, 1 },
  { SAVE_DOG_NAME + 1,         HM_DOG_NAME + 1,                      1, 1 },
  { SAVE_DOG_NAME + 2,         HM_DOG_NAME + 2,                      1, 1 },
  { SAVE_DOG_NAME + 3,         HM_DOG_NAME + 3,                      1, 1 },
  /* horse's name */
  { SAVE_HORSE_NAME,           HM_HORSE_NAME,                        1, 1 },
  { SAVE_HORSE_NAME + 1,       HM_HORSE_NAME + 1,                    1, 1 },
  { SAVE_HORSE_NAME + 2,       HM_HORSE_NAME + 2,                    1, 1 },
  { SAVE_HORSE_NAME + 3,       HM_HORSE_NAME + 3,                    1, 1 },
  /* first child */
  { SAVE_KID1_NAME,            WRAM_LONG(HM_KID1_NAME),              1, 0 },
  { SAVE_KID1_NAME + 1,        WRAM_LONG(HM_KID1_NAME + 1),          1, 0 },
  { SAVE_KID1_NAME + 2,        WRAM_LONG(HM_KID1_NAME + 2),          1, 0 },
  { SAVE_KID1_NAME + 3,        WRAM_LONG(HM_KID1_NAME + 3),          1, 0 },
  /* second child */
  { SAVE_KID2_NAME,            WRAM_LONG(HM_KID2_NAME),              1, 0 },
  { SAVE_KID2_NAME + 1,        WRAM_LONG(HM_KID2_NAME + 1),          1, 0 },
  { SAVE_KID2_NAME + 2,        WRAM_LONG(HM_KID2_NAME + 2),          1, 0 },
  { SAVE_KID2_NAME + 3,        WRAM_LONG(HM_KID2_NAME + 3),          1, 0 },
};

/* ─────────────────────────────────────────────────────────────────────────
 * SaveGameSlot ($83:B68E, JSL, RTL). A = the slot (0 or other). Writes the
 * fields above, the animals and the farm, the checksum, and marks this
 * slot as the last saved (+2E = 1 here, 0 in the other one). It does not
 * write "FARM": CheckSRAMIntegrity puts that when it clears a slot.
 * Leftovers: A = (high byte of the slot number : the last +2E written),
 * X = $0F00, Y = $2E, C = 1; $72-$74 = slot 2, $7E = the slot number.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_SaveGameSlot(uint8_t *ram, HmRegs *r)
{
  uint16_t slot = r->a;
  push16(ram, r, slot);                               /* PHA (PLA; PHA again) */
  point_at_slot(ram, r, slot);
  for (unsigned i = 0; i < sizeof FIELDS / sizeof FIELDS[0]; i++) {
    const Field *f = &FIELDS[i];
    uint32_t from = f->data_bank ? db_addr(r, (uint16_t)f->from) : f->from;
    if (f->size == 1) sram_w8(ram, r, f->at, hm_bus_rd8(ram, from));
    else              sram_w16(ram, r, f->at, hm_bus_rd16(ram, from));
  }
  if (g_hm_single_seed) {                             /* the native build's seeds */
    sram_w8(ram, r, SEEDS_MARK_AT, SEEDS_MARK);
    for (uint16_t i = 0; i < 5; i++) sram_w8(ram, r, (uint16_t)(SEEDS_MARK_AT + 1 + i), g_hm_seeds_used[i]);
  }
  if (g_hm_bag_on) {                                  /* the native build's crop bag */
    for (uint16_t i = 0; i < 9; i++) sram_w8(ram, r, (uint16_t)(BAG_AT + i), g_hm_bag[i]);
    sram_w8(ram, r, BAG_AT + 9, BAG_MARK);
  }
  if (g_hm_wife_on) {                                 /* the native build's wife's help */
    sram_w8(ram, r, WIFE_AT, (uint8_t)g_hm_wife_until);
    sram_w8(ram, r, WIFE_AT + 1, (uint8_t)(g_hm_wife_until >> 8));
    sram_w8(ram, r, WIFE_AT + 2, WIFE_MARK);
  }
  for (uint16_t x = 0; x < 0x68; x++) sram_w8(ram, r, (uint16_t)(SAVE_CHICKEN_RECORDS + x), hm_bus_rd8(ram, WRAM_LONG(HM_CHICKEN_DATA) + x));
  for (uint16_t x = 0; x < 0xC0; x++) sram_w8(ram, r, (uint16_t)(SAVE_COW_RECORDS + x), hm_bus_rd8(ram, WRAM_LONG(HM_COW_DATA) + x));
  for (uint16_t x = 0xC0; x < 0xF00; x++)
    sram_w8(ram, r, (uint16_t)(SAVE_FARM_MAP + x - 0xC0), hm_bus_rd8(ram, WRAM_LONG(HM_FARM_TILES) + x));

  sram_w8(ram, r, SAVE_LAST_SAVED, 0);
  sram_w16(ram, r, SAVE_CHECKSUM, 0);
  uint16_t sum = 0;
  hm_dp_w16(ram, r, WORK_SUM, 0);
  for (uint16_t y = 0; y < 0x1000; y += 2) sum = (uint16_t)(sum + sram16(ram, r, y));
  hm_dp_w16(ram, r, WORK_SUM, sum);
  sram_w16(ram, r, SAVE_CHECKSUM, sum);

  slot = pull16(ram, r);                              /* PLA; STA $7E */
  hm_dp_w16(ram, r, WORK_SUM, slot);
  point_at_slot(ram, r, 0);
  sram_w8(ram, r, SAVE_LAST_SAVED, slot == 0 ? 1 : 0);
  hm_dp_w16(ram, r, SLOT_PTR, (uint16_t)SLOT_2);
  sram_w8(ram, r, SAVE_LAST_SAVED, slot == 0 ? 0 : 1);
  r->a = (uint16_t)((slot & 0xFF00) | (slot == 0 ? 0 : 1));
  r->x = 0x0F00;
  r->y = SAVE_LAST_SAVED;
  r->c = 1;
}

/* ─────────────────────────────────────────────────────────────────────────
 * LoadGameSlotResume ($83:BA45, JSL, RTL). A = the slot. What the title
 * screen shows of a slot: year, season and day; if the day is not 0 and
 * the slot says "FARM", also the player's name ($0881-$0884) and X = 1
 * ("there is a game here"); otherwise X = 0.
 * Leftovers: A = (high byte from the slot pointer code : the last byte
 * read), Y = the last offset read, C from the last compare.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_LoadGameSlotResume(uint8_t *ram, HmRegs *r)
{
  uint16_t slot = r->a;
  push16(ram, r, slot); pull16(ram, r);               /* PHA ... PLA */
  uint16_t a = point_at_slot(ram, r, slot);
  r->c = 1;                                           /* CMP #0 */
  r->x = 0;
  static const struct { uint16_t at; uint32_t to; } DATE[3] = {
    { SAVE_YEAR, WRAM_LONG(HM_YEAR) }, { SAVE_SEASON, WRAM_LONG(HM_SEASON) },
    { SAVE_DAY, WRAM_LONG(HM_DAY) } };
  uint8_t v = 0;
  for (unsigned i = 0; i < 3; i++) {
    r->y = DATE[i].at;
    v = sram8(ram, r, r->y);
    hm_bus_wr8(ram, DATE[i].to, v);
  }
  r->a = (uint16_t)((a & 0xFF00) | v);
  if (v == 0) return;                                 /* day 0: an empty slot */
  static const char FARM[4] = { 'F', 'A', 'R', 'M' };
  for (unsigned i = 0; i < 4; i++) {
    r->y = (uint16_t)(SAVE_FARM_MARK + i);
    v = sram8(ram, r, r->y);
    r->a = (uint16_t)((a & 0xFF00) | v);
    r->c = v >= (uint8_t)FARM[i];
    if (v != (uint8_t)FARM[i]) return;
  }
  for (unsigned i = 0; i < 4; i++) {
    r->y = (uint16_t)(SAVE_PLAYER_NAME + i);
    v = sram8(ram, r, r->y);
    hm_bus_wr8(ram, db_addr(r, (uint16_t)(HM_PLAYER_NAME + i)), v);
  }
  r->a = (uint16_t)((a & 0xFF00) | v);
  r->x = 1;
}

/* ─────────────────────────────────────────────────────────────────────────
 * CheckSRAMIntegrity ($83:BAD4, JSL, RTL), at power-on. For each slot: it
 * must say "FARM" and its checksum must match; if not, its first 2 KB are
 * cleared and "FARM" is written back. $098E = 1 if slot 2 is valid and was
 * the last saved (its +2E = 1). $7F1F60 bit $0800 is cleared first and set
 * again only when slot 1 failed AND slot 2 failed too (the code checks
 * slot 1's result only on slot 2's failure path; the comment in HM-Decomp
 * says "at least one").
 * $82 = 1 if slot 1 failed, $7E / $80 = slot 2's stored / computed sums,
 * $92 = slot 2's +2E.
 * ───────────────────────────────────────────────────────────────────────── */
static bool slot_valid(uint8_t *ram, HmRegs *r, uint16_t *a)
{
  static const char FARM[4] = { 'F', 'A', 'R', 'M' };
  for (unsigned i = 0; i < 4; i++) {
    r->y = (uint16_t)(SAVE_FARM_MARK + i);
    uint8_t v = sram8(ram, r, r->y);
    *a = (uint16_t)((*a & 0xFF00) | v);
    r->c = v >= (uint8_t)FARM[i];
    if (v != (uint8_t)FARM[i]) return false;
  }
  uint8_t last = sram8(ram, r, SAVE_LAST_SAVED);
  *hm_dp(ram, r, LAST_SAVED) = last;
  sram_w8(ram, r, SAVE_LAST_SAVED, 0);
  uint16_t stored = sram16(ram, r, SAVE_CHECKSUM);
  hm_dp_w16(ram, r, STORED_SUM, stored);
  sram_w16(ram, r, SAVE_CHECKSUM, 0);
  uint16_t sum = 0;
  for (uint16_t y = 0; y < 0x1000; y += 2) sum = (uint16_t)(sum + sram16(ram, r, y));
  hm_dp_w16(ram, r, COMPUTED_SUM, sum);
  *a = stored;
  r->c = stored >= sum;                               /* CMP $80 */
  if (stored != sum) return false;
  sram_w16(ram, r, SAVE_CHECKSUM, stored);
  r->y = SAVE_LAST_SAVED;
  sram_w8(ram, r, SAVE_LAST_SAVED, last);
  *a = (uint16_t)((stored & 0xFF00) | last);
  return true;
}
static void clear_slot(uint8_t *ram, HmRegs *r, uint16_t *a)
{
  for (uint16_t y = 0; y < 0x0800; y += 2) sram_w16(ram, r, y, 0);
  r->c = 1;                                           /* CPY #$0800 */
  static const char FARM[4] = { 'F', 'A', 'R', 'M' };
  for (unsigned i = 0; i < 4; i++) sram_w8(ram, r, (uint16_t)(SAVE_FARM_MARK + i), (uint8_t)FARM[i]);
  r->y = SAVE_FARM_MARK + 3;
  *a = 'M';
}

void hm_CheckSRAMIntegrity(uint8_t *ram, HmRegs *r)
{
  hm_bus_wr8(ram, db_addr(r, SAVE_SLOT), 0);
  uint16_t a = (uint16_t)(hm_rd16(ram, HM_FLAGS_60) & 0xF7FF);
  hm_wr16(ram, HM_FLAGS_60, a);
  hm_dp_w16(ram, r, SLOT1_FAILED, 0);
  point_at_slot(ram, r, 0);
  a = 0x0070;                                         /* LDA #0; SEP; LDA #$70 */
  if (!slot_valid(ram, r, &a)) {
    clear_slot(ram, r, &a);
    a = 1;
    hm_dp_w16(ram, r, SLOT1_FAILED, 1);
  }
  point_at_slot(ram, r, 1);
  a = 0x1070;
  if (slot_valid(ram, r, &a)) {
    r->c = (uint8_t)a >= 1;                           /* CMP #1 */
    if ((uint8_t)a == 1) hm_bus_wr8(ram, db_addr(r, SAVE_SLOT), 1);
  } else {
    clear_slot(ram, r, &a);
    a = hm_dp_r16(ram, r, SLOT1_FAILED);
    if (a) {
      a = (uint16_t)(hm_rd16(ram, HM_FLAGS_60) | 0x0800);
      hm_wr16(ram, HM_FLAGS_60, a);
    }
  }
  r->a = a;
}

/* ─────────────────────────────────────────────────────────────────────────
 * LoadGameSlot ($83:B2B1, JSL, RTL). A = the slot (0 or other). The
 * inverse of SaveGameSlot: every field above back to its variable, in the
 * same order; each letter of the five names (player, dog, horse,
 * children) also as a 16-bit word in a second copy ($08D5, $08DD, $08E5,
 * $08ED, $08F5: 2 bytes per letter, the high byte 0); the chickens and
 * the cows; the default farm (LoadDefaultFarmMap) and over it the saved
 * part of the farm map (tiles $C0-$EFF). It checks nothing: neither
 * "FARM" nor the checksum (CheckSRAMIntegrity did that at power-on).
 * Leftovers: $75-$77 = the slot, $72-$74 = the default farm map's
 * pointer; A = (LoadDefaultFarmMap's high byte : the last tile), X = $F00,
 * Y = $1000, C = 1.
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_LOAD_FARM 0xB677   /* JSL LoadDefaultFarmMap at $83:B674 */

void hm_LoadGameSlot(uint8_t *ram, HmRegs *r)
{
  uint16_t slot = r->a;
  push16(ram, r, slot); pull16(ram, r);               /* PHA ... PLA */
  r->a = point_at_slot(ram, r, slot);
  r->c = 1;                                           /* CMP #0 */
  /* where each name's letters start in the slot, and their 16-bit copies */
  static const struct { uint16_t at, copy; } NAMES[5] = {
    { SAVE_PLAYER_NAME, HM_PLAYER_NAME_WORDS }, { SAVE_DOG_NAME, HM_DOG_NAME_WORDS },
    { SAVE_HORSE_NAME, HM_HORSE_NAME_WORDS }, { SAVE_KID1_NAME, HM_KID1_NAME_WORDS },
    { SAVE_KID2_NAME, HM_KID2_NAME_WORDS } };
  for (unsigned i = 0; i < sizeof FIELDS / sizeof FIELDS[0]; i++) {
    const Field *f = &FIELDS[i];
    uint32_t to = f->data_bank ? db_addr(r, (uint16_t)f->from) : f->from;
    r->y = f->at;
    for (unsigned n = 0; n < 5; n++)
      if (f->at == NAMES[n].at) r->a = (uint16_t)(r->a >> 8);   /* LDA #0; XBA */
    if (f->size == 1) {
      uint8_t v = sram8(ram, r, f->at);
      r->a = (uint16_t)((r->a & 0xFF00) | v);
      hm_bus_wr8(ram, to, v);
      for (unsigned n = 0; n < 5; n++)
        if (f->at >= NAMES[n].at && f->at < NAMES[n].at + 4)
          hm_bus_wr16(ram, db_addr(r, (uint16_t)(NAMES[n].copy + 2 * (f->at - NAMES[n].at))), r->a);
    } else {
      r->a = sram16(ram, r, f->at);
      hm_bus_wr16(ram, to, r->a);
    }
  }
  if (g_hm_single_seed) {                             /* the native build's seeds */
    bool marked = hm_bus_rd8(ram, hm_dp_long(ram, r, SLOT_PTR, SEEDS_MARK_AT)) == SEEDS_MARK;
    for (uint16_t i = 0; i < 5; i++) {
      uint8_t v = marked ? hm_bus_rd8(ram, hm_dp_long(ram, r, SLOT_PTR, (uint16_t)(SEEDS_MARK_AT + 1 + i))) : 0;
      g_hm_seeds_used[i] = v < 9 ? v : 0;
    }
  }
  if (g_hm_bag_on) {                                  /* the native build's crop bag */
    bool marked = hm_bus_rd8(ram, hm_dp_long(ram, r, SLOT_PTR, BAG_AT + 9)) == BAG_MARK;
    for (uint16_t i = 0; i < 9; i++)
      g_hm_bag[i] = marked ? hm_bus_rd8(ram, hm_dp_long(ram, r, SLOT_PTR, (uint16_t)(BAG_AT + i))) : 0;
  }
  if (g_hm_wife_on) {                                 /* the native build's wife's help */
    bool marked = hm_bus_rd8(ram, hm_dp_long(ram, r, SLOT_PTR, WIFE_AT + 2)) == WIFE_MARK;
    g_hm_wife_until = marked ? (uint16_t)(hm_bus_rd8(ram, hm_dp_long(ram, r, SLOT_PTR, WIFE_AT)) |
                                          hm_bus_rd8(ram, hm_dp_long(ram, r, SLOT_PTR, WIFE_AT + 1)) << 8) : 0;
  }
  uint16_t x;
  for (x = 0; x < 0x68; x++) {
    uint8_t v = sram8(ram, r, (uint16_t)(SAVE_CHICKEN_RECORDS + x));
    r->a = (uint16_t)((r->a & 0xFF00) | v);
    hm_bus_wr8(ram, WRAM_LONG(HM_CHICKEN_DATA) + x, v);
  }
  for (x = 0; x < 0xC0; x++) {
    uint8_t v = sram8(ram, r, (uint16_t)(SAVE_COW_RECORDS + x));
    r->a = (uint16_t)((r->a & 0xFF00) | v);
    hm_bus_wr8(ram, WRAM_LONG(HM_COW_DATA) + x, v);
  }
  r->x = 0xC0; r->y = SAVE_FARM_MAP;
  r->a = hm_dp_r16(ram, r, SLOT_PTR);
  hm_dp_w16(ram, r, SAVED_SLOT_PTR, r->a);
  uint8_t bank = *hm_dp(ram, r, SLOT_PTR_BANK);
  r->a = (uint16_t)((r->a & 0xFF00) | bank);
  *hm_dp(ram, r, SAVED_SLOT_PTR_BANK) = bank;
  hm_jsl(ram, r, RET_LOAD_FARM, 0x82);
  hm_LoadDefaultFarmMap(ram, r);
  hm_rtl(ram, r);
  for (x = 0xC0; x < 0xF00; x++) {
    uint8_t v = hm_bus_rd8(ram, hm_dp_long(ram, r, SAVED_SLOT_PTR, (uint16_t)(SAVE_FARM_MAP + x - 0xC0)));
    r->a = (uint16_t)((r->a & 0xFF00) | v);
    hm_bus_wr8(ram, WRAM_LONG(HM_FARM_TILES) + x, v);
  }
  r->x = 0xF00;
  r->y = 0x1000;
  r->c = 1;
}

/* ─────────────────────────────────────────────────────────────────────────
 * NewGameSetup ($83:A9BE, JSL, RTL): a new game. The same fields as a
 * saved game get their starting values: year 1 (0), spring, Monday the
 * 1st, one bag of grass seeds, 100 of max stamina, 300 G (30, money is
 * kept in tens), the shed with the basic tools ($0F) and the watering can
 * and the grass seeds ($88); every name "    " (character $B1, also the
 * 16-bit copies of the player's, the dog's and the horse's names), and
 * everything else 0; then the default farm (LoadDefaultFarmMap). The
 * chickens, the cows and the children's 16-bit name copies are left as
 * they are.
 * Quirk: the horse's name is written twice; the code was probably meant
 * to write something else the second time.
 * Leftovers: those of LoadDefaultFarmMap.
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_NEW_FARM 0xABEE   /* JSL LoadDefaultFarmMap at $83:ABEB */

static uint16_t new_game_value(uint16_t at)
{
  switch (at) {
  case SAVE_WEEKDAY: case SAVE_DAY: case SAVE_SEEDS_GRASS: return 1; /* weekday, day, grass seeds */
  case SAVE_MAX_STAMINA: return 100;                  /* max stamina */
  case SAVE_MONEY: return 30;                         /* money: 300 G */
  case SAVE_SHED: return 0x0F;                        /* the shed */
  case SAVE_SHED + 1: return 0x88;
  default:
    if (at >= SAVE_PLAYER_NAME && at < SAVE_CHICKEN_RECORDS &&
        (at < SAVE_SHED || at >= SAVE_DOG_NAME)) return 0xB1;        /* names */
    return 0;
  }
}

void hm_NewGameSetup(uint8_t *ram, HmRegs *r)
{
  memset(g_hm_seeds_used, 0, sizeof g_hm_seeds_used);  /* the native build's seeds */
  memset(g_hm_bag, 0, sizeof g_hm_bag);                 /* and crop bag */
  g_hm_wife_until = 0;                                  /* and the wife's help */
  for (unsigned i = 0; i < sizeof FIELDS / sizeof FIELDS[0]; i++) {
    const Field *f = &FIELDS[i];
    uint32_t to = f->data_bank ? db_addr(r, (uint16_t)f->from) : f->from;
    uint16_t v = new_game_value(f->at);
    if (f->size == 1) hm_bus_wr8(ram, to, (uint8_t)v);
    else              hm_bus_wr16(ram, to, v);
  }
  for (unsigned n = 0; n < 12; n++)                   /* player, dog, horse */
    hm_bus_wr16(ram, db_addr(r, (uint16_t)(HM_PLAYER_NAME_WORDS + 2 * n)), 0x00B1);
  r->a = 0x00B1;
  hm_jsl(ram, r, RET_NEW_FARM, 0x82);
  hm_LoadDefaultFarmMap(ram, r);
  hm_rtl(ram, r);
}
