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

/* ─────────────────────────────────────────────────────────────────────────
 * SUB_8483CC ($84:83CC, JSR, RTS): a slot that wanders on its own (+01
 * bit 3: villagers in town, the animals in the barn...). Each time its
 * step timers agree (+05 == +06, that is, right after a step):
 *  - if no steps are left (+08 = 0) it picks a new walk of +07 steps: the
 *    direction +0F if the script forced one (1 down, 2 up, 4 right,
 *    8 left), else $DE (0 down, 1 up, 2 right, 3 left; others: stand
 *    still);
 *  - otherwise it keeps going one step less (+08 - 1).
 * A slot without +01 bit 7 ("free") never leaves its area: the home
 * position (+1E, +20) plus or minus the range (+09 across, +0A down); a
 * step that would go past it makes the slot stand still instead (no step,
 * +08 = 0, the standing animation from the table at +33).
 * Starting a walk: direction +02, step (+03, +04) = (0, 1) down,
 * (0, -1) up, (1, 0) right, (-1, 0) left, the step delay +0B to +05/+06,
 * the walking animation from the table at +36. Either way +01 |= 4 (the
 * animation changed).
 * Then, when moving, it looks ahead ($DF/$E1 = the position, $E5/$E7 =
 * the step size, $E3 = 0):
 *  - with +01 bit 7, the map (CEEEE; the tile type of the first corner
 *    goes to +0E): blocked, or a tile $FF or $C0-$CF ahead, stops it;
 *  - the tile $EB of the second corner (also when CEEEE was not called:
 *    then it is whatever the last check left there), the same way;
 *  - the player in the way (CDDDD) stops it too.
 * Tile $E1 (a door?) only lets the slots numbered $18-$23 (+3F) through.
 * Stopping: no step, +05/+06 = +0B, and the forced direction is cleared.
 * Leftovers: $92 = +06; $7E/$80 from the area check; $72-$74 = the
 * animation table; A, Y and C as each way out leaves them.
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_STAND_ANIM   0x86DC   /* JSL SUB_848895 at $84:86D9 */
#define RET_WALK_ANIM    0x8706   /* JSL SUB_848895 at $84:8703 */
#define RET_LOOK_MAP     0x87B1   /* JSL CEEEE at $84:87AE */
#define RET_LOOK_PLAYER  0x8810   /* JSL CDDDD at $84:880D */
#define ANIMATION_BANK   0xB3

static void put8(uint8_t *ram, HmRegs *r, uint16_t at, uint8_t v)
{
  r->y = at;
  r->a = (uint16_t)((r->a & 0xFF00) | v);
  hm_bus_wr8(ram, hm_dp_long(ram, r, 0xCC, at), v);
}
static void store8(uint8_t *ram, HmRegs *r, uint16_t at)   /* STA [$CC],Y */
{
  r->y = at;
  hm_bus_wr8(ram, hm_dp_long(ram, r, 0xCC, at), (uint8_t)r->a);
}
static uint16_t slot16(uint8_t *ram, HmRegs *r, uint16_t at)
{
  r->y = at;
  return r->a = hm_bus_rd16(ram, hm_dp_long(ram, r, 0xCC, at));
}
/* A = 0; LDA (8 bits): the byte zero-extended */
static uint16_t slot8_ext(uint8_t *ram, HmRegs *r, uint16_t at)
{
  r->a = 0;
  return slot8(ram, r, at);
}

/* the area: true if the slot may go on. `across`: x (+1A against +1E,
 * range +09), else y (+1C, +20, +0A); `ahead`: the side the step goes to
 * (down / right). */
static bool inside_area(uint8_t *ram, HmRegs *r, bool across, bool ahead)
{
  uint16_t range = slot8_ext(ram, r, across ? 0x09 : 0x0A);
  hm_dp_w16(ram, r, 0x7E, range);
  uint16_t pos = slot16(ram, r, across ? 0x1A : 0x1C);
  hm_dp_w16(ram, r, 0x80, pos);
  uint16_t home = slot16(ram, r, across ? 0x1E : 0x20);
  uint32_t edge = ahead ? (uint32_t)home + range : (uint32_t)home - range + 0x10000;
  r->a = (uint16_t)edge;                         /* CLC; ADC / SEC; SBC */
  r->c = r->a >= pos;                            /* CMP $80 */
  return ahead ? r->c : !r->c;
}

/* the slot's animation from the table at +33 (standing) or +36 (walking),
 * then +01 |= 4 */
static void set_slot_animation(uint8_t *ram, HmRegs *r, uint16_t table, uint16_t ret)
{
  hm_dp_w16(ram, r, 0x72, slot16(ram, r, table));
  r->a = (uint16_t)((r->a & 0xFF00) | ANIMATION_BANK);
  *hm_dp(ram, r, 0x74) = ANIMATION_BANK;
  hm_jsl(ram, r, ret, 0x84);
  hm_SUB_848895(ram, r);
  hm_rtl(ram, r);
  put8(ram, r, 0x01, slot8(ram, r, 0x01) | 0x04);
}

static void stand_still(uint8_t *ram, HmRegs *r)        /* .CODE_8486B3 */
{
  put8(ram, r, 0x03, 0);
  put8(ram, r, 0x04, 0);
  put8(ram, r, 0x08, 0);
  set_slot_animation(ram, r, 0x33, RET_STAND_ANIM);
}

static void stop_here(uint8_t *ram, HmRegs *r)          /* .CODE_848817 */
{
  put8(ram, r, 0x03, 0);
  put8(ram, r, 0x04, 0);
  slot8(ram, r, 0x0B);
  store8(ram, r, 0x05);
  store8(ram, r, 0x06);
  put8(ram, r, 0x0F, 0);
}

/* a tile type ahead: 0 = go on, 1 = stop, 2 = tile $E1 */
static int tile_ahead(HmRegs *r, uint16_t tile)
{
  r->a = tile;
  if (tile == 0x00FF) { r->c = 1; return 1; }
  if (tile == 0x00E1) { r->c = 1; return 2; }
  r->c = tile >= 0x00C0;
  if (!r->c) return 0;
  r->c = tile >= 0x00D0;
  return r->c ? 0 : 1;
}
/* tile $E1: only the slots numbered $18-$23 go through */
static bool may_pass_e1(uint8_t *ram, HmRegs *r)
{
  uint8_t self = slot8(ram, r, 0x3F);
  r->c = self >= 0x24;
  if (r->c) return false;
  r->c = self >= 0x18;
  return r->c;
}

void hm_SUB_8483CC(uint8_t *ram, HmRegs *r)
{
  uint8_t flags = slot8(ram, r, 0x01);
  r->a = (uint16_t)((r->a & 0xFF00) | (flags & 0x08));
  if (!(flags & 0x08)) return;
  uint8_t timer = slot8(ram, r, 0x06);
  *hm_dp(ram, r, 0x92) = timer;
  uint8_t delay = slot8(ram, r, 0x05);
  r->c = delay >= timer;
  if (delay != timer) return;

  /* the direction to start, or -1 to go on, -2 to stand still */
  int dir;
  if (slot8(ram, r, 0x08) == 0) {
    slot8(ram, r, 0x07);
    store8(ram, r, 0x08);
    uint8_t forced = slot8(ram, r, 0x0F);
    static const uint8_t forced_bit[4] = { 1, 2, 4, 8 };
    dir = -2;
    for (int i = 0; forced && i < 4; i++)
      if (forced == forced_bit[i]) { dir = i; break; }
    if (dir == -2) {
      uint8_t random = *hm_dp(ram, r, 0xDE);
      r->a = (uint16_t)((r->a & 0xFF00) | random);
      if (random < 4) dir = random;
    }
  } else if (slot8(ram, r, 0x01) & 0x80) {
    r->a &= 0xFF80;
    dir = -1;
  } else {
    /* going on: the area, on the side of the current direction (0-3;
     * anything else counts as down). (The carry of the comparisons here
     * and in the choice above never survives: the area check or
     * SUB_848895 sets it.) */
    uint8_t now = slot8(ram, r, 0x02);
    switch (now < 4 ? now : 0) {
    case 0:  dir = inside_area(ram, r, false, true) ? -1 : -2; break;
    case 1:  dir = inside_area(ram, r, false, false) ? -1 : -2; break;
    case 2:  dir = inside_area(ram, r, true, true) ? -1 : -2; break;
    default: dir = inside_area(ram, r, true, false) ? -1 : -2; break;
    }
  }

  if (dir >= 0) {
    if (slot8(ram, r, 0x01) & 0x80) {
      r->a &= 0xFF80;
    } else if (!inside_area(ram, r, dir >= 2, !(dir & 1))) {
      dir = -2;
    }
  }
  if (dir == -2) {
    stand_still(ram, r);
    return;
  }
  if (dir >= 0) {
    static const int8_t step[4][2] = { { 0, 1 }, { 0, -1 }, { 1, 0 }, { -1, 0 } };
    slot8(ram, r, 0x0B);
    store8(ram, r, 0x05);
    store8(ram, r, 0x06);
    put8(ram, r, 0x03, (uint8_t)step[dir][0]);
    put8(ram, r, 0x04, (uint8_t)step[dir][1]);
    put8(ram, r, 0x02, (uint8_t)dir);
    set_slot_animation(ram, r, 0x36, RET_WALK_ANIM);
  } else {
    slot8(ram, r, 0x08);
    put8(ram, r, 0x08, (uint8_t)(r->a - 1));
  }

  /* looking ahead */
  int8_t dx = (int8_t)slot8(ram, r, 0x03);
  if (!dx && !slot8(ram, r, 0x04)) return;
  hm_dp_w16(ram, r, 0xDF, slot16(ram, r, 0x1A));
  hm_dp_w16(ram, r, 0xE1, slot16(ram, r, 0x1C));
  int8_t sx = (int8_t)slot8_ext(ram, r, 0x03);
  if (sx < 0) r->a = (uint8_t)-sx;               /* EOR #$FF; INC (8 bits) */
  hm_dp_w16(ram, r, 0xE5, r->a);
  int8_t sy = (int8_t)slot8_ext(ram, r, 0x04);
  if (sy < 0) r->a = (uint8_t)-sy;
  hm_dp_w16(ram, r, 0xE7, r->a);
  hm_dp_w16(ram, r, 0xE3, 0);

  int ahead;
  if (slot8(ram, r, 0x01) & 0x80) {
    r->a &= 0xFF80;
    slot8_ext(ram, r, 0x02);
    hm_jsl(ram, r, RET_LOOK_MAP, 0x83);
    hm_CEEEE(ram, r);
    hm_rtl(ram, r);
    uint16_t blocked = r->a;
    hm_push8(ram, r, (uint8_t)(blocked >> 8)); hm_push8(ram, r, (uint8_t)blocked);  /* PHA */
    uint16_t first = hm_dp_r16(ram, r, 0xE9);
    r->a = first;
    store8(ram, r, 0x0E);
    hm_pull8(ram, r); hm_pull8(ram, r);          /* PLA */
    r->a = blocked;
    r->c = 1;                                    /* CMP #0 */
    if (blocked) { stop_here(ram, r); return; }
    ahead = tile_ahead(r, first);
    if (ahead == 2) ahead = may_pass_e1(ram, r) ? 0 : 1;
    if (ahead) { stop_here(ram, r); return; }
  }
  ahead = tile_ahead(r, hm_dp_r16(ram, r, 0xEB));
  if (ahead == 2) ahead = may_pass_e1(ram, r) ? 0 : 1;
  if (ahead) { stop_here(ram, r); return; }

  slot8_ext(ram, r, 0x02);
  hm_jsl(ram, r, RET_LOOK_PLAYER, 0x83);
  hm_CDDDD(ram, r);
  hm_rtl(ram, r);
  r->c = 1;                                      /* CMP #0 */
  if (r->a) stop_here(ram, r);
}

/* ─────────────────────────────────────────────────────────────────────────
 * SUB_848286 ($84:8286, JSR, RTS): a slot's sprite, each frame. Unless
 * +01 bit 4 (frozen), the slot first wanders (SUB_8483CC) and steps
 * (SUB_848331). Then its game object (+12) gets the slot's position (+1A,
 * +1C), animation (+14) and sprite (+16), through $A5, $9F, $9B, $9D, $A1;
 * and by +01:
 *   bit 6  destroy the object (CODE_8581A2);
 *   bit 1  create it (CreateGameObject; the new object goes to +12);
 *   bit 2  the sprite changed (+16 != +18, the last one): CODE_858100;
 *   none   just move it (CODE_8580B9).
 * Then +18 = +16 and the one-time bits 1, 2 and 4 of +01 are cleared
 * (AND #$E9).
 * Leftovers: A = the new +01, Y = 1; X and C from the object routine.
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_WANDER       0x8297   /* JSR SUB_8483CC at $84:8295 */
#define RET_STEP         0x829A   /* JSR SUB_848331 at $84:8298 */
#define RET_MOVE_OBJECT  0x82DC   /* JSL CODE_8580B9 at $84:82D9 */
#define RET_NEW_OBJECT   0x82E2   /* JSL CreateGameObject at $84:82DF */
#define RET_NEW_SPRITE   0x8307   /* JSL CODE_858100 at $84:8304 */
#define RET_DROP_OBJECT  0x830D   /* JSL CODE_8581A2 at $84:830A */

static void object_call(uint8_t *ram, HmRegs *r, uint16_t ret, void (*fn)(uint8_t *, HmRegs *))
{
  hm_jsl(ram, r, ret, 0x85);
  fn(ram, r);
  hm_rtl(ram, r);
}

void hm_SUB_848286(uint8_t *ram, HmRegs *r)
{
  uint8_t flags = slot8(ram, r, 0x01);
  r->a = (uint16_t)((r->a & 0xFF00) | (flags & 0x10));
  if (!(flags & 0x10)) {
    jsr(ram, r, RET_WANDER, hm_SUB_8483CC);
    jsr(ram, r, RET_STEP, hm_SUB_848331);
  }
  static const struct { uint8_t field, dp; } to_object[5] = {
    { 0x12, 0xA5 }, { 0x14, 0x9F }, { 0x1A, 0x9B }, { 0x1C, 0x9D }, { 0x16, 0xA1 } };
  for (int i = 0; i < 5; i++)
    hm_dp_w16(ram, r, to_object[i].dp, slot16(ram, r, to_object[i].field));

  flags = slot8(ram, r, 0x01);
  if (flags & 0x40) {
    r->a = (uint16_t)((r->a & 0xFF00) | 0x40);
    object_call(ram, r, RET_DROP_OBJECT, hm_CODE_8581A2);
  } else if (flags & 0x02) {
    r->a = (uint16_t)((r->a & 0xFF00) | 0x02);
    object_call(ram, r, RET_NEW_OBJECT, hm_CreateGameObject);
    r->a = hm_dp_r16(ram, r, 0xA5);
    r->y = 0x12;
    hm_bus_wr16(ram, hm_dp_long(ram, r, 0xCC, 0x12), r->a);
  } else if (flags & 0x04) {
    r->a = (uint16_t)((r->a & 0xFF00) | 0x04);
    uint16_t sprite = slot16(ram, r, 0x16);
    hm_dp_w16(ram, r, 0x7E, sprite);
    uint16_t last = slot16(ram, r, 0x18);
    r->c = last >= sprite;                       /* CMP $7E */
    if (last != sprite) object_call(ram, r, RET_NEW_SPRITE, hm_CODE_858100);
  } else {
    r->a &= 0xFF00;
    object_call(ram, r, RET_MOVE_OBJECT, hm_CODE_8580B9);
  }
  slot16(ram, r, 0x16);
  r->y = 0x18;
  hm_bus_wr16(ram, hm_dp_long(ram, r, 0xCC, 0x18), r->a);
  put8(ram, r, 0x01, slot8(ram, r, 0x01) & 0xE9);
}

/* ─────────────────────────────────────────────────────────────────────────
 * EventInstructionInterpreter ($84:8249, JSR, RTS): one command of the
 * script of the slot at $CC: $C9-$CB = its position (+30, +32), the
 * command's routine from EventInstructionPointers ($84:9419, 90 words,
 * JSR (abs,X)); then the new position goes back to the slot.
 * Leftovers: A = $CB (8-bit A), Y = $32, X and C those of the command.
 * ───────────────────────────────────────────────────────────────────────── */
static void (*const EVENT_COMMANDS[90])(uint8_t *, HmRegs *) = {
  hm_Event_AudioRelated, hm_Event_TimeRunning, hm_Event_TimeStopped, hm_Event_SetHour,
  hm_Event_Empty, hm_Event_SetPlayerPosition, hm_Event_SetTransitionDest, hm_Event_SetPlayerDirection,
  hm_Event_UNKChangesGameState, hm_Event_NewCCStructure, hm_Event_ScreenFadein, hm_Event_UNKset7Aflag,
  hm_Event_UNKCompareset7Aflag, hm_Event_UNKCCchanges2, hm_Event_AudioRelated2, hm_Event_ScreenFadeout,
  hm_Event_UNKDisableCCstruct, hm_Event_UNKChangesGameState2, hm_Event_JumptoInstruction, hm_Event_UNKSetsCC10,
  hm_Event_JumpifFlagSet, hm_Event_JumpifEquals, hm_Event_JumpifBetween, hm_Event_GetRNG,
  hm_Event_JumpIfEqualsRNG, hm_Event_UNKSetAnimation, hm_Event_UNKCCchanges3, hm_Event_UNKCCchanges4,
  hm_Event_StartTextBox, hm_Event_StartTextBoxCopy, hm_Event_UNKCCchanges5, hm_Event_UNKCCchanges5,
  hm_Event_UNKJumpif018F, hm_Event_UNKTextrelated, hm_Event_UNKCCchanges6, hm_Event_SetFlag,
  hm_Event_TimeofDayPalette, hm_Event_UNKCCchanges7, hm_Event_ResetFlag, hm_Event_ResetFlag,
  hm_Event_ResetFlag, hm_Event_MapScrolling, hm_Event_UNKCFD1, hm_Event_UNKCCchanges8,
  hm_Event_AudioRelated3, hm_Event_UNKCCchanges9, hm_Event_UNKCCchanges10, hm_Event_UNKCCchanges11,
  hm_Event_UNKChickenRelated, hm_Event_UNKCowRelated, hm_Event_StoreValue, hm_Event_UNKPickupMole,
  hm_Event_UNKFishing, hm_Event_UNKPlayerPos, hm_Event_UNKDogRelated, hm_Event_UNK1,
  hm_Event_UNK2, hm_Event_UNKCCchanges12, hm_Event_UNKCCchanges13, hm_Event_ChangeItemonHand,
  hm_Event_TransitiontoHouse, hm_Event_TransitiontoX, hm_Event_SetItemonHand, hm_Event_DropItemAnimation,
  hm_Event_UNKSet5C, hm_Event_UNKCCchanges14, hm_Event_ChangeMoney, hm_Event_JumpifEqualsValue,
  hm_Event_JumpifEqualsValueLong, hm_Event_JumpifBetweenValue, hm_Event_JumpifBetweenValueLong, hm_Event_SetValueSort,
  hm_Event_SetValueLong, hm_Event_UNKCCchanges15, hm_Event_UNK3, hm_Event_EditTileonMap,
  hm_Event_UNKCCchanges16, hm_Event_UNKCCchanges17, hm_Event_UNKReset5C, hm_Event_UNKSet60,
  hm_Event_UNKChickenRelated2, hm_Event_Cmd51, hm_Event_Cmd52, hm_Event_Cmd53,
  hm_Event_Cmd54, hm_Event_Cmd55, hm_Event_Cmd56, hm_Event_Cmd57,
  hm_Event_Cmd58, hm_Event_Cmd59,
};

void hm_EventInstructionInterpreter(uint8_t *ram, HmRegs *r)
{
  r->y = 0x30;
  r->a = hm_bus_rd16(ram, hm_dp_long(ram, r, 0xCC, 0x30));
  hm_dp_w16(ram, r, 0xC9, r->a);
  r->y = 0x32;
  uint8_t bank = hm_bus_rd8(ram, hm_dp_long(ram, r, 0xCC, 0x32));
  r->a = (uint16_t)((r->a & 0xFF00) | bank);
  *hm_dp(ram, r, 0xCB) = bank;
  uint8_t cmd = hm_bus_rd8(ram, hm_dp_long(ram, r, 0xC9, 0));   /* LDA #0; XBA; LDA [$C9] */
  r->a = (uint16_t)(cmd << 1);                                    /* REP #$20; ASL; TAX */
  r->c = 0;
  r->x = r->a;
  if (cmd < 90) jsr(ram, r, 0x826C, EVENT_COMMANDS[cmd]);
  r->a = hm_dp_r16(ram, r, 0xC9);                                 /* REP #$30 */
  r->y = 0x30;
  hm_bus_wr16(ram, hm_dp_long(ram, r, 0xCC, 0x30), r->a);
  r->a = (uint16_t)((r->a & 0xFF00) | *hm_dp(ram, r, 0xCB));
  r->y = 0x32;
  hm_bus_wr8(ram, hm_dp_long(ram, r, 0xCC, 0x32), (uint8_t)r->a);
}

/* ─────────────────────────────────────────────────────────────────────────
 * IterateCCStructures ($84:816F, JSL, RTL): every frame, unless a text box
 * waits ($019A = 2 or 6) or the player is in action 3: a random number to
 * $DE (GetRNG) and then each of the 49 slots in use (+00 bit 0): while its
 * wait (+10) is 0 its script runs one command after another
 * (EventInstructionInterpreter; +03/+04 cleared first unless +01 bit 3),
 * else the wait goes down by one (and at 0 the script runs this same
 * frame); $DE + $33 for the next slot; a slot with a sprite (+01 bit 0)
 * gets it updated (SUB_848286). At the end the one-frame bits are cleared:
 * $7F1F5A bits 4, 6, 7, 8; $7F1F5C bit 10; $7F1F5E bit 6; $7F1F60 bit 4.
 * Leftovers: A = the new $7F1F60, C = 1 (or that of the compare that
 * stopped it), X = $31 after the slots.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_IterateCCStructures(uint8_t *ram, HmRegs *r)
{
  uint8_t input = hm_bus_rd8(ram, (uint32_t)r->db << 16 | 0x019A);
  r->a = (uint16_t)((r->a & 0xFF00) | input);
  r->c = input >= 2;
  bool skip = input == 2;
  if (!skip) { r->c = input >= 6; skip = input == 6; }
  if (!skip) {
    r->a = hm_dp_r16(ram, r, 0xD4);
    r->c = r->a >= 3;
    skip = r->a == 3;
  }
  if (!skip) {
    hm_jsl(ram, r, 0x8193, 0x83);
    hm_GetRNG(ram, r);
    hm_rtl(ram, r);
    *hm_dp(ram, r, 0xDE) = (uint8_t)r->a;
    hm_dp_w16(ram, r, 0xCC, 0xB586);
    r->a = 0xB57E;                                              /* SEP #$20; LDA #$7E */
    *hm_dp(ram, r, 0xCE) = 0x7E;
    r->x = 0;
    do {
      hm_push8(ram, r, (uint8_t)(r->x >> 8));                   /* PHX */
      hm_push8(ram, r, (uint8_t)r->x);
      r->y = 0;
      uint8_t used = hm_bus_rd8(ram, hm_dp_long(ram, r, 0xCC, 0));
      r->a = (uint16_t)((r->a & 0xFF00) | (used & 0x01));
      if (used & 0x01) {
        r->y = 0x10;
        r->a = hm_bus_rd16(ram, hm_dp_long(ram, r, 0xCC, 0x10));
        if (r->a) {                                             /* DEC A; STA */
          r->a = (uint16_t)(r->a - 1);
          hm_bus_wr16(ram, hm_dp_long(ram, r, 0xCC, 0x10), r->a);
        }
        for (;;) {
          r->y = 0x10;
          r->a = hm_bus_rd16(ram, hm_dp_long(ram, r, 0xCC, 0x10));
          if (r->a) break;
          r->y = 1;
          uint8_t flags = hm_bus_rd8(ram, hm_dp_long(ram, r, 0xCC, 1));
          r->a = (uint16_t)((r->a & 0xFF00) | (flags & 0x08));
          if (!(flags & 0x08)) {
            r->y = 3;
            r->a = 0;
            hm_bus_wr16(ram, hm_dp_long(ram, r, 0xCC, 3), 0);
          }
          jsr(ram, r, 0x81DC, hm_EventInstructionInterpreter);
        }
        uint32_t de = (uint32_t)*hm_dp(ram, r, 0xDE) + 0x33;   /* SEP #$20; CLC; ADC */
        r->c = de > 0xFF;
        r->a = (uint16_t)((r->a & 0xFF00) | (uint8_t)de);
        *hm_dp(ram, r, 0xDE) = (uint8_t)de;
        r->y = 1;
        uint8_t flags = hm_bus_rd8(ram, hm_dp_long(ram, r, 0xCC, 1));
        r->a = (uint16_t)((r->a & 0xFF00) | (flags & 0x01));
        if (flags & 0x01) jsr(ram, r, 0x8208, hm_SUB_848286);
      }
      uint32_t next = (uint32_t)hm_dp_r16(ram, r, 0xCC) + 0x40;  /* REP #$30; CLC; ADC */
      r->a = (uint16_t)next;
      hm_dp_w16(ram, r, 0xCC, r->a);
      uint8_t lo = hm_pull8(ram, r);                            /* PLX; INX */
      r->x = (uint16_t)((lo | hm_pull8(ram, r) << 8) + 1);
      r->c = r->x >= 0x31;
    } while (r->x != 0x31);
  }
  static const struct { uint32_t at; uint16_t keep; } clear[4] = {
    { 0x7F1F5A, 0xFE2F }, { 0x7F1F5C, 0xFBFF }, { 0x7F1F5E, 0xFFBF }, { 0x7F1F60, 0xFFEF } };
  for (int i = 0; i < 4; i++) {
    r->a = (uint16_t)(hm_bus_rd16(ram, clear[i].at) & clear[i].keep);
    hm_bus_wr16(ram, clear[i].at, r->a);
  }
}
