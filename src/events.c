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
 * ResetsCurrentCCStructureShort ($84:8961, JSR, RTS). The short reset:
 * marks the slot in use, sets +01 to 1 and clears +03..06, +0F and the word
 * +10. Ends with A = 0, Y = $10.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_ResetsCurrentCCStructureShort(uint8_t *ram, HmRegs *r)
{
  static const uint8_t zero_bytes[] = { 0x0F, 0x03, 0x04, 0x05, 0x06 };
  hm_bus_wr8(ram, hm_dp_long(ram, r, 0xCC, 0x00), 1);
  hm_bus_wr8(ram, hm_dp_long(ram, r, 0xCC, 0x01), 1);
  for (unsigned i = 0; i < sizeof zero_bytes; i++)
    hm_bus_wr8(ram, hm_dp_long(ram, r, 0xCC, zero_bytes[i]), 0);
  hm_bus_wr16(ram, hm_dp_long(ram, r, 0xCC, 0x10), 0);
  r->a = 0;
  r->y = 0x10;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Starting an event script: SUB_8480F8, UNK_LoadCCDataLong and
 * UNK_LoadCCDataShort ($84:80F8, $84:8097, $84:803F; JSL, RTL).
 *   A = CC slot, X = script group, Y = script number in the group.
 * They point the slot ($CC, SetCCPoiner) at the script: +30 = the 16-bit
 * offset, entry `script` of the group's table, +32 = its bank. The group's
 * table is entry `group` (24-bit) of $B3:8000 (only the first entry of
 * Events_Bank_Table is used). Then:
 *  - Long: full reset, +3F = the slot's number;
 *  - SUB_8480F8: the same, and it also places the event on the tile in
 *    front of the player (+1A/+1C);
 *  - Short: short reset, and the script pointer is also left in $C9-$CB
 *    (Long uses $72-$74 / $C9-$CB for the tables, Short $75-$77 / $78-$7A).
 * Leftovers: $7E = group, $80 = script number; X = 2; C = bit 15 of the
 * script number (its ASL).
 * ───────────────────────────────────────────────────────────────────────── */
typedef struct ScriptStart {
  uint16_t ret_set_pointer, ret_reset;   /* where their JSL / JSR return */
  uint8_t table_dp, group_dp;            /* $72 / $C9 or $75 / $78 */
  bool short_reset;
} ScriptStart;

static void jsr(uint8_t *ram, HmRegs *r, uint16_t ret, void (*fn)(uint8_t *, HmRegs *))
{
  hm_push8(ram, r, (uint8_t)(ret >> 8));
  hm_push8(ram, r, (uint8_t)ret);
  fn(ram, r);
  hm_pull8(ram, r); hm_pull8(ram, r);
}

static void start_script(uint8_t *ram, HmRegs *r, const ScriptStart *s)
{
  uint16_t group = r->x, script = r->y;
  hm_jsl(ram, r, s->ret_set_pointer, 0x84);
  hm_SetCCPoiner(ram, r);
  hm_rtl(ram, r);

  hm_dp_w16(ram, r, 0x7E, group);
  hm_dp_w16(ram, r, 0x80, script);
  hm_dp_w16(ram, r, s->table_dp, hm_bus_rd16(ram, EVENTS_BANK_TABLE));
  *hm_dp(ram, r, s->table_dp + 2) = hm_bus_rd8(ram, EVENTS_BANK_TABLE + 2);
  uint16_t y = (uint16_t)(group * 3);
  hm_dp_w16(ram, r, s->group_dp, hm_bus_rd16(ram, hm_dp_long(ram, r, s->table_dp, y)));
  *hm_dp(ram, r, s->group_dp + 2) = hm_bus_rd8(ram, hm_dp_long(ram, r, s->table_dp, (uint16_t)(y + 2)));
  r->c = (script >> 15) & 1;                     /* ASL of $80 */
  uint16_t offset = hm_bus_rd16(ram, hm_dp_long(ram, r, s->group_dp, (uint16_t)(script << 1)));
  uint8_t bank = *hm_dp(ram, r, s->group_dp + 2);
  hm_bus_wr16(ram, hm_dp_long(ram, r, 0xCC, 0x30), offset);
  hm_bus_wr8(ram, hm_dp_long(ram, r, 0xCC, 0x32), bank);
  r->x = 2;
  if (s->short_reset) {
    hm_dp_w16(ram, r, 0xC9, offset);
    *hm_dp(ram, r, 0xCB) = bank;
    jsr(ram, r, s->ret_reset, hm_ResetsCurrentCCStructureShort);
  } else {
    jsr(ram, r, s->ret_reset, hm_ResetsCurrentCCStructureLong);
  }
}

/* PHA ... PLA around the start, then +3F = the slot's number. */
static uint16_t start_script_numbered(uint8_t *ram, HmRegs *r, const ScriptStart *s)
{
  uint16_t slot = r->a;
  hm_push8(ram, r, (uint8_t)(slot >> 8));
  hm_push8(ram, r, (uint8_t)slot);
  start_script(ram, r, s);
  uint8_t lo = hm_pull8(ram, r);
  slot = (uint16_t)(lo | hm_pull8(ram, r) << 8);
  hm_bus_wr8(ram, hm_dp_long(ram, r, 0xCC, 0x3F), (uint8_t)slot);
  r->a = slot;
  r->y = 0x3F;
  return slot;
}

void hm_UNK_LoadCCDataShort(uint8_t *ram, HmRegs *r)
{
  static const ScriptStart s = { 0x8044, 0x8095, 0x75, 0x78, true };
  start_script(ram, r, &s);
}

void hm_UNK_LoadCCDataLong(uint8_t *ram, HmRegs *r)
{
  static const ScriptStart s = { 0x809D, 0x80EA, 0x72, 0xC9, false };
  start_script_numbered(ram, r, &s);
}

void hm_SUB_8480F8(uint8_t *ram, HmRegs *r)
{
  static const ScriptStart s = { 0x80FE, 0x814B, 0x72, 0xC9, false };
  start_script_numbered(ram, r, &s);
  hm_bus_wr16(ram, hm_dp_long(ram, r, 0xCC, 0x1A), hm_rd16(ram, HM_TILE_IN_FRONT_X));
  r->a = hm_rd16(ram, HM_TILE_IN_FRONT_Y);
  hm_bus_wr16(ram, hm_dp_long(ram, r, 0xCC, 0x1C), r->a);
  r->y = 0x1C;
}

/* ─────────────────────────────────────────────────────────────────────────
 * ClearsofAllCCStructs ($84:8000, JSL, RTL): frees the 49 script slots
 * (the first byte of each to 0), each through SetCCPoiner.
 * Leftovers: $CC-$CE = the last slot, X = $31, Y = 0, C = 1, A = what
 * SetCCPoiner left with its low byte 0.
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_CLEAR_ALL 0x800A

void hm_ClearsofAllCCStructs(uint8_t *ram, HmRegs *r)
{
  for (uint16_t x = 0; x != 0x31; x++) {
    hm_push8(ram, r, (uint8_t)(x >> 8)); hm_push8(ram, r, (uint8_t)x);   /* PHX */
    r->a = x;                                                            /* TXA */
    hm_jsl(ram, r, RET_CLEAR_ALL, 0x84);
    hm_SetCCPoiner(ram, r);
    hm_rtl(ram, r);
    r->y = 0;
    r->a &= 0xFF00;
    hm_bus_wr8(ram, hm_dp_long(ram, r, 0xCC, 0), 0);
    hm_pull8(ram, r); hm_pull8(ram, r);                                  /* PLX */
  }
  r->x = 0x31;
  r->c = 1;
}

/* ─────────────────────────────────────────────────────────────────────────
 * SUB_848895 ($84:8895, JSL, RTL): the slot's animation from a table at
 * [$72]: 3 bytes per entry, entry = the slot's +02. The word goes to +16,
 * the byte (x 64) to +14.
 * Leftovers: $7E = the entry, A = byte x 64, Y = $14, C = 0.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_SUB_848895(uint8_t *ram, HmRegs *r)
{
  uint16_t entry = hm_bus_rd8(ram, hm_dp_long(ram, r, 0xCC, 0x02));
  hm_dp_w16(ram, r, 0x7E, entry);
  uint16_t y = (uint16_t)(entry * 3);
  uint16_t word = hm_bus_rd16(ram, hm_dp_long(ram, r, 0x72, y));
  hm_push8(ram, r, (uint8_t)(y >> 8)); hm_push8(ram, r, (uint8_t)y);   /* PHY */
  hm_bus_wr16(ram, hm_dp_long(ram, r, 0xCC, 0x16), word);
  hm_pull8(ram, r); hm_pull8(ram, r);                                  /* PLY */
  r->a = (uint16_t)(hm_bus_rd8(ram, hm_dp_long(ram, r, 0x72, (uint16_t)(y + 2))) << 6);
  hm_bus_wr16(ram, hm_dp_long(ram, r, 0xCC, 0x14), r->a);
  r->y = 0x14;
  r->c = 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * SUB_848020 ($84:8020, JSL, RTL): frees slot A (its +00 = 0, through
 * SetCCPoiner: $CC-$CE then point at it) and destroys its game object (the
 * slot's +12, CODE_8581A2).
 * Leftovers: those of CODE_8581A2.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_SUB_848020(uint8_t *ram, HmRegs *r)
{
  hm_jsl(ram, r, 0x8025, 0x84);
  hm_SetCCPoiner(ram, r);
  hm_rtl(ram, r);
  r->y = 0;
  r->a &= 0xFF00;
  hm_bus_wr8(ram, hm_dp_long(ram, r, 0xCC, 0), 0);
  r->y = 0x12;
  r->a = hm_bus_rd16(ram, hm_dp_long(ram, r, 0xCC, 0x12));
  hm_dp_w16(ram, r, 0xA5, r->a);
  hm_jsl(ram, r, 0x803D, 0x85);
  hm_CODE_8581A2(ram, r);
  hm_rtl(ram, r);
}

/* ─────────────────────────────────────────────────────────────────────────
 * SUB_848331 ($84:8331, JSR, RTS): one step of a slot's walk. Only if +01
 * bit 5 (walking): the step timer +06 goes down; at 0 it is reloaded from
 * +05 and the slot moves by (+03, +04), signed bytes, added to its
 * position (+1A, +1C).
 * Leftovers: $7E / $80 = the step, A = the new Y (or the timer, or the
 * flags), Y = the last field touched, C from the add.
 * ───────────────────────────────────────────────────────────────────────── */
static uint8_t slot8(uint8_t *ram, HmRegs *r, uint16_t at)
{
  r->y = at;
  uint8_t v = hm_bus_rd8(ram, hm_dp_long(ram, r, 0xCC, at));
  r->a = (uint16_t)((r->a & 0xFF00) | v);
  return v;
}

void hm_SUB_848331(uint8_t *ram, HmRegs *r)
{
  if (!(slot8(ram, r, 0x01) & 0x20)) { r->a &= 0xFF20; return; }  /* AND #$20 */
  uint8_t timer = (uint8_t)(slot8(ram, r, 0x06) - 1);
  hm_bus_wr8(ram, hm_dp_long(ram, r, 0xCC, 0x06), timer);
  if (slot8(ram, r, 0x06)) return;
  uint8_t reload = slot8(ram, r, 0x05);
  r->y = 0x06;
  hm_bus_wr8(ram, hm_dp_long(ram, r, 0xCC, 0x06), reload);
  int8_t dx = (int8_t)slot8(ram, r, 0x03);
  hm_dp_w16(ram, r, 0x7E, (uint16_t)(int16_t)dx);
  int8_t dy = (int8_t)slot8(ram, r, 0x04);
  hm_dp_w16(ram, r, 0x80, (uint16_t)(int16_t)dy);
  static const uint8_t at[2] = { 0x1A, 0x1C };
  for (int i = 0; i < 2; i++) {
    r->y = at[i];
    uint16_t pos = hm_bus_rd16(ram, hm_dp_long(ram, r, 0xCC, at[i]));
    uint32_t sum = (uint32_t)pos + hm_dp_r16(ram, r, i ? 0x80 : 0x7E);
    r->c = sum > 0xFFFF;
    r->a = (uint16_t)sum;
    hm_bus_wr16(ram, hm_dp_long(ram, r, 0xCC, at[i]), r->a);
  }
}
