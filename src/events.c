/*
 * events.c — the event system: the "CC structures".
 *
 * Everything scripted (cutscenes, villagers walking, the dog, festival
 * crowds, animals doing things...) runs as an event script in one of the
 * CC slots: 64-byte structures from $7E:B586 (slot n at $7E:B586 + 64 n).
 * The direct-page pointer $CC-$CE points at the slot being worked on, and
 * the script code reads fields through it ([$CC],Y).
 *
 * Fields used so far (offsets in the slot):
 *   +00 in use (1)        +01 flags         +03..06, +0C, +0F  state bytes
 *   +10 +12 +14 timers / counters (16b)     +1A +1C  X, Y position (16b)
 *   +30 +32  script pointer (16b offset + bank)    +3F  the slot's own number
 *
 * Scripts live in ROM: Events_Bank_Table ($84:94CD) points at a table of
 * 24-bit "group" pointers at $B3:8000; each group is a table of 16-bit
 * script offsets in the group's bank.
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

#define CC_SLOTS_BASE      0xB586   /* $7E:B586 */
#define CC_SLOT_SIZE       64
#define EVENTS_BANK_TABLE  0x8494CD /* ROM: dl $B38000, $B48000 */
#define HM_TILE_IN_FRONT_X 0x00985  /* 16b, the tile the player faces */
#define HM_TILE_IN_FRONT_Y 0x00987

/* ─────────────────────────────────────────────────────────────────────────
 * SetCCPoiner ($84:887C, JSL, RTL). A = slot number; $CC-$CE = its address
 * ($7E:B586 + 64 A). Leftovers: $7E = 64 A; A = (high byte of the address)
 * : $7E; C = carry of the add.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_SetCCPoiner(uint8_t *ram, HmRegs *r)
{
  uint16_t offset = (uint16_t)(r->a << 6);
  hm_dp_w16(ram, r, 0x7E, offset);
  uint32_t address = (uint32_t)CC_SLOTS_BASE + offset;
  r->c = address > 0xFFFF;
  hm_dp_w16(ram, r, 0xCC, (uint16_t)address);
  *hm_dp(ram, r, 0xCE) = 0x7E;
  r->a = (uint16_t)((address & 0xFF00) | 0x7E);  /* SEP #$20; LDA #$7E */
}

/* ─────────────────────────────────────────────────────────────────────────
 * ResetsCurrentCCStructureLong ($84:88D4, JSR, RTS — despite the name).
 * Marks the slot at $CC as in use and clears its state: bytes +01, +03..06,
 * +0C, +0F and words +10, +12, +14, +1A, +1C. Ends with A = 0, Y = $14.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_ResetsCurrentCCStructureLong(uint8_t *ram, HmRegs *r)
{
  static const uint8_t bytes[] = { 0x01, 0x0F, 0x03, 0x04, 0x05, 0x06, 0x0C };
  static const uint8_t words[] = { 0x10, 0x1A, 0x1C, 0x12, 0x14 };
  hm_bus_wr8(ram, hm_dp_long(ram, r, 0xCC, 0x00), 1);
  for (unsigned i = 0; i < sizeof bytes; i++)
    hm_bus_wr8(ram, hm_dp_long(ram, r, 0xCC, bytes[i]), 0);
  for (unsigned i = 0; i < sizeof words; i++)
    hm_bus_wr16(ram, hm_dp_long(ram, r, 0xCC, words[i]), 0);
  r->a = 0;
  r->y = 0x14;
}

/* ─────────────────────────────────────────────────────────────────────────
 * SUB_8480F8 ($84:80F8, JSL, RTL): start an event script.
 *   A = CC slot, X = script group, Y = script number in the group.
 * Points the slot at the script (+30/+32), resets it, stores its own number
 * (+3F) and places it on the tile in front of the player (+1A/+1C).
 * Only the first entry of Events_Bank_Table is used ($B3:8000).
 * Leftovers: $7E = group, $80 = script number, $72-$74 = $B3:8000,
 * $C9-$CB = the group's table; A = tile-in-front Y, X = 2, Y = $1C,
 * C = bit 15 of the script number (from its ASL).
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_SET_CC_POINTER  0x80FE   /* JSL SetCCPoiner at $84:80FB */
#define RET_RESET_CC        0x814B   /* JSR ResetsCurrentCCStructureLong at $84:8149 */

void hm_SUB_8480F8(uint8_t *ram, HmRegs *r)
{
  uint16_t slot = r->a, group = r->x, script = r->y;
  hm_push8(ram, r, (uint8_t)(slot >> 8));        /* PHA (16-bit) */
  hm_push8(ram, r, (uint8_t)slot);
  hm_jsl(ram, r, RET_SET_CC_POINTER, 0x84);
  hm_SetCCPoiner(ram, r);
  hm_rtl(ram, r);

  hm_dp_w16(ram, r, 0x7E, group);
  hm_dp_w16(ram, r, 0x80, script);
  /* $72-$74 = Events_Bank_Table[0] */
  hm_dp_w16(ram, r, 0x72, hm_bus_rd16(ram, EVENTS_BANK_TABLE));
  *hm_dp(ram, r, 0x74) = hm_bus_rd8(ram, EVENTS_BANK_TABLE + 2);
  /* $C9-$CB = the group's script table: 24-bit entry `group` */
  uint16_t y = (uint16_t)(group * 3);
  hm_dp_w16(ram, r, 0xC9, hm_bus_rd16(ram, hm_dp_long(ram, r, 0x72, y)));
  *hm_dp(ram, r, 0xCB) = hm_bus_rd8(ram, hm_dp_long(ram, r, 0x72, (uint16_t)(y + 2)));
  /* script pointer = 16-bit entry `script` of that table, in its bank */
  r->c = (script >> 15) & 1;                     /* ASL of $80 */
  uint16_t offset = hm_bus_rd16(ram, hm_dp_long(ram, r, 0xC9, (uint16_t)(script << 1)));
  hm_bus_wr16(ram, hm_dp_long(ram, r, 0xCC, 0x30), offset);
  hm_bus_wr8(ram, hm_dp_long(ram, r, 0xCC, 0x32), *hm_dp(ram, r, 0xCB));

  hm_push8(ram, r, (uint8_t)(RET_RESET_CC >> 8)); /* JSR */
  hm_push8(ram, r, (uint8_t)RET_RESET_CC);
  hm_ResetsCurrentCCStructureLong(ram, r);
  hm_pull8(ram, r); hm_pull8(ram, r);           /* RTS */

  uint8_t lo = hm_pull8(ram, r);                 /* PLA */
  slot = (uint16_t)(lo | hm_pull8(ram, r) << 8);
  hm_bus_wr8(ram, hm_dp_long(ram, r, 0xCC, 0x3F), (uint8_t)slot);
  hm_bus_wr16(ram, hm_dp_long(ram, r, 0xCC, 0x1A), hm_rd16(ram, HM_TILE_IN_FRONT_X));
  r->a = hm_rd16(ram, HM_TILE_IN_FRONT_Y);
  hm_bus_wr16(ram, hm_dp_long(ram, r, 0xCC, 0x1C), r->a);
  r->x = 2;
  r->y = 0x1C;
}
