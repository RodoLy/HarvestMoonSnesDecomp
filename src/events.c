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
