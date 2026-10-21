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
