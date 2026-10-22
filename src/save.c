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
 *
 * The checksum is the 16-bit sum of the slot's 2048 words, taken with +2E
 * and +2F-+30 at 0.
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

#define SLOT_1        0x700000
#define SLOT_2        0x701000
#define FARM_TILES    0x7EA4E6
#define COWS          0x7EC1C6
#define CHICKENS      0x7EC286

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
  hm_dp_w16(ram, r, 0x72, 0x0000);
  *hm_dp(ram, r, 0x74) = 0x70;
  if (slot == 0) return slot;
  hm_dp_w16(ram, r, 0x72, 0x1000);
  return 0x1070;
}
static uint8_t sram8(uint8_t *ram, HmRegs *r, uint16_t y) { return hm_bus_rd8(ram, hm_dp_long(ram, r, 0x72, y)); }
static uint16_t sram16(uint8_t *ram, HmRegs *r, uint16_t y) { return hm_bus_rd16(ram, hm_dp_long(ram, r, 0x72, y)); }
static void sram_w8(uint8_t *ram, HmRegs *r, uint16_t y, uint8_t v) { hm_bus_wr8(ram, hm_dp_long(ram, r, 0x72, y), v); }
static void sram_w16(uint8_t *ram, HmRegs *r, uint16_t y, uint16_t v) { hm_bus_wr16(ram, hm_dp_long(ram, r, 0x72, y), v); }

/* What goes where: offset in the slot, the variable, its size, and whether
 * the code reads it with the data bank (LDA.W) or a long address (LDA.L). */
typedef struct { uint16_t at; uint32_t from; uint8_t size, data_bank; } Field;
static const Field FIELDS[] = {
  { 0x00, 0x7F1F18, 1, 0 }, { 0x01, 0x7F1F19, 1, 0 }, { 0x02, 0x7F1F1A, 1, 0 },
  { 0x03, 0x7F1F1B, 1, 0 },                                   /* year, season, weekday, day */
  { 0x04, 0x0927, 1, 1 }, { 0x05, 0x0928, 1, 1 }, { 0x06, 0x0929, 1, 1 },
  { 0x07, 0x092A, 1, 1 }, { 0x08, 0x092B, 1, 1 },             /* seed bags */
  { 0x09, 0x092C, 1, 1 }, { 0x0A, 0x092D, 1, 1 },             /* feed */
  { 0x0B, 0x7F1F0A, 1, 0 }, { 0x0C, 0x7F1F0B, 1, 0 },         /* cows, chickens */
  { 0x0D, 0x098C, 1, 1 }, { 0x0E, 0x0917, 1, 1 }, { 0x0F, 0x0921, 1, 1 },
  { 0x10, 0x0926, 1, 1 },
  { 0x11, 0x7F1F12, 1, 0 }, { 0x12, 0x7F1F2B, 1, 0 }, { 0x13, 0x7F1F30, 1, 0 },   /* dog map */
  { 0x14, 0x7F1F31, 1, 0 }, { 0x15, 0x7F1F32, 1, 0 }, { 0x16, 0x7F1F35, 1, 0 },   /* development */
  { 0x17, 0x7F1F36, 1, 0 },                                   /* power berries */
  { 0x18, 0x09A3, 1, 1 }, { 0x19, 0x0937, 1, 1 }, { 0x1A, 0x0923, 1, 1 },         /* backpack */
  { 0x40, 0x7F1F0C, 2, 0 }, { 0x42, 0x7F1F10, 2, 0 },         /* wood, fodder */
  { 0x44, 0x0196, 2, 1 }, { 0x46, 0x7F1F29, 2, 0 },           /* planted grass */
  { 0x48, 0x7F1F1F, 2, 0 }, { 0x4A, 0x7F1F21, 2, 0 }, { 0x4C, 0x7F1F23, 2, 0 },
  { 0x4E, 0x7F1F25, 2, 0 }, { 0x50, 0x7F1F27, 2, 0 },         /* hearts: Maria .. Eve */
  { 0x60, 0x7F1F64, 2, 0 }, { 0x62, 0x7F1F66, 2, 0 }, { 0x64, 0x7F1F68, 2, 0 },
  { 0x66, 0x7F1F6A, 2, 0 },
  { 0x68, 0x7F1F2C, 2, 0 }, { 0x6A, 0x7F1F2E, 2, 0 },         /* dog position */
  { 0x6C, 0x7F1F33, 2, 0 }, { 0x6E, 0x7F1F45, 2, 0 },         /* happiness */
  { 0x70, 0x7F1F6C, 2, 0 }, { 0x72, 0x7F1F6E, 2, 0 }, { 0x74, 0x7F1F70, 2, 0 },
  { 0x76, 0x7F1F72, 2, 0 },
  { 0x78, 0x7F1F3B, 2, 0 }, { 0x7A, 0x7F1F37, 2, 0 }, { 0x7C, 0x7F1F39, 2, 0 },   /* family */
  { 0x31, 0x7F1F4A, 2, 0 }, { 0x33, 0x7F1F4C, 2, 0 }, { 0x35, 0x7F1F4E, 2, 0 },
  { 0x37, 0x7F1F50, 2, 0 },                                   /* shipped */
  { 0x7E, 0x7F1F52, 2, 0 },                                   /* dog hugs */
  { 0x39, 0x7F1F04, 2, 0 }, { 0x3B, 0x7F1F06, 1, 0 },         /* money */
  { 0x80, 0x0881, 1, 1 }, { 0x81, 0x0882, 1, 1 }, { 0x82, 0x0883, 1, 1 },
  { 0x83, 0x0884, 1, 1 },                                     /* player's name */
  { 0x84, 0x7F1F00, 1, 0 }, { 0x85, 0x7F1F01, 1, 0 }, { 0x86, 0x7F1F02, 1, 0 },
  { 0x87, 0x7F1F03, 1, 0 },                                   /* the shed */
  { 0x88, 0x0899, 1, 1 }, { 0x89, 0x089A, 1, 1 }, { 0x8A, 0x089B, 1, 1 },
  { 0x8B, 0x089C, 1, 1 },                                     /* dog's name */
  { 0x8C, 0x089D, 1, 1 }, { 0x8D, 0x089E, 1, 1 }, { 0x8E, 0x089F, 1, 1 },
  { 0x8F, 0x08A0, 1, 1 },                                     /* horse's name */
  { 0x90, 0x7F1F3D, 1, 0 }, { 0x91, 0x7F1F3E, 1, 0 }, { 0x92, 0x7F1F3F, 1, 0 },
  { 0x93, 0x7F1F40, 1, 0 },                                   /* first child */
  { 0x94, 0x7F1F41, 1, 0 }, { 0x95, 0x7F1F42, 1, 0 }, { 0x96, 0x7F1F43, 1, 0 },
  { 0x97, 0x7F1F44, 1, 0 },                                   /* second child */
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
  for (uint16_t x = 0; x < 0x68; x++) sram_w8(ram, r, (uint16_t)(0x098 + x), hm_bus_rd8(ram, CHICKENS + x));
  for (uint16_t x = 0; x < 0xC0; x++) sram_w8(ram, r, (uint16_t)(0x100 + x), hm_bus_rd8(ram, COWS + x));
  for (uint16_t x = 0xC0; x < 0xF00; x++)
    sram_w8(ram, r, (uint16_t)(0x1C0 + x - 0xC0), hm_bus_rd8(ram, FARM_TILES + x));

  sram_w8(ram, r, 0x2E, 0);
  sram_w16(ram, r, 0x2F, 0);
  uint16_t sum = 0;
  hm_dp_w16(ram, r, 0x7E, 0);
  for (uint16_t y = 0; y < 0x1000; y += 2) sum = (uint16_t)(sum + sram16(ram, r, y));
  hm_dp_w16(ram, r, 0x7E, sum);
  sram_w16(ram, r, 0x2F, sum);

  slot = pull16(ram, r);                              /* PLA; STA $7E */
  hm_dp_w16(ram, r, 0x7E, slot);
  point_at_slot(ram, r, 0);
  sram_w8(ram, r, 0x2E, slot == 0 ? 1 : 0);
  hm_dp_w16(ram, r, 0x72, 0x1000);
  sram_w8(ram, r, 0x2E, slot == 0 ? 0 : 1);
  r->a = (uint16_t)((slot & 0xFF00) | (slot == 0 ? 0 : 1));
  r->x = 0x0F00;
  r->y = 0x002E;
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
    { 0x00, 0x7F1F18 }, { 0x01, 0x7F1F19 }, { 0x03, 0x7F1F1B } };
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
    r->y = (uint16_t)(0x3C + i);
    v = sram8(ram, r, r->y);
    r->a = (uint16_t)((a & 0xFF00) | v);
    r->c = v >= (uint8_t)FARM[i];
    if (v != (uint8_t)FARM[i]) return;
  }
  for (unsigned i = 0; i < 4; i++) {
    r->y = (uint16_t)(0x80 + i);
    v = sram8(ram, r, r->y);
    hm_bus_wr8(ram, db_addr(r, (uint16_t)(0x0881 + i)), v);
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
    r->y = (uint16_t)(0x3C + i);
    uint8_t v = sram8(ram, r, r->y);
    *a = (uint16_t)((*a & 0xFF00) | v);
    r->c = v >= (uint8_t)FARM[i];
    if (v != (uint8_t)FARM[i]) return false;
  }
  uint8_t last = sram8(ram, r, 0x2E);
  *hm_dp(ram, r, 0x92) = last;
  sram_w8(ram, r, 0x2E, 0);
  uint16_t stored = sram16(ram, r, 0x2F);
  hm_dp_w16(ram, r, 0x7E, stored);
  sram_w16(ram, r, 0x2F, 0);
  uint16_t sum = 0;
  for (uint16_t y = 0; y < 0x1000; y += 2) sum = (uint16_t)(sum + sram16(ram, r, y));
  hm_dp_w16(ram, r, 0x80, sum);
  *a = stored;
  r->c = stored >= sum;                               /* CMP $80 */
  if (stored != sum) return false;
  sram_w16(ram, r, 0x2F, stored);
  r->y = 0x2E;
  sram_w8(ram, r, 0x2E, last);
  *a = (uint16_t)((stored & 0xFF00) | last);
  return true;
}
static void clear_slot(uint8_t *ram, HmRegs *r, uint16_t *a)
{
  for (uint16_t y = 0; y < 0x0800; y += 2) sram_w16(ram, r, y, 0);
  r->c = 1;                                           /* CPY #$0800 */
  static const char FARM[4] = { 'F', 'A', 'R', 'M' };
  for (unsigned i = 0; i < 4; i++) sram_w8(ram, r, (uint16_t)(0x3C + i), (uint8_t)FARM[i]);
  r->y = 0x3F;
  *a = 'M';
}

void hm_CheckSRAMIntegrity(uint8_t *ram, HmRegs *r)
{
  hm_bus_wr8(ram, db_addr(r, 0x098E), 0);
  uint16_t a = (uint16_t)(hm_rd16(ram, 0x11F60) & 0xF7FF);
  hm_wr16(ram, 0x11F60, a);
  hm_dp_w16(ram, r, 0x82, 0);
  point_at_slot(ram, r, 0);
  a = 0x0070;                                         /* LDA #0; SEP; LDA #$70 */
  if (!slot_valid(ram, r, &a)) {
    clear_slot(ram, r, &a);
    a = 1;
    hm_dp_w16(ram, r, 0x82, 1);
  }
  point_at_slot(ram, r, 1);
  a = 0x1070;
  if (slot_valid(ram, r, &a)) {
    r->c = (uint8_t)a >= 1;                           /* CMP #1 */
    if ((uint8_t)a == 1) hm_bus_wr8(ram, db_addr(r, 0x098E), 1);
  } else {
    clear_slot(ram, r, &a);
    a = hm_dp_r16(ram, r, 0x82);
    if (a) {
      a = (uint16_t)(hm_rd16(ram, 0x11F60) | 0x0800);
      hm_wr16(ram, 0x11F60, a);
    }
  }
  r->a = a;
}
