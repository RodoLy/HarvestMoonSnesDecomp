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

/* ── Names of the memory locations used here ──────────────────────────────
 * Also used, from hm_ram.h: HM_PLAYER_ACTION ($D4), HM_RNG_LAST ($DE) and
 * HM_INPUT_STATE ($019A). */
#define CC_SLOTS_BASE      0xB586   /* $7E:B586 */
#define CC_SLOT_SIZE       64
#define EVENTS_BANK_TABLE  0x8494CD /* ROM: dl $B38000, $B48000 */
#define HM_TILE_IN_FRONT_X 0x00985  /* 16b, the tile the player faces */
#define HM_TILE_IN_FRONT_Y 0x00987

/* Direct page. The scratch words $7E / $80 mean a different thing in each
 * routine (each is left behind as a leftover): */
#define SLOT_OFFSET        0x7E   /* 16b: SetEventSlotPointer: 64 x the slot number */
#define SCRIPT_GROUP       0x7E   /* 16b: starting a script: the group */
#define SCRIPT_NUMBER      0x80   /* 16b: and the script's number in it */
#define STEP_DX            0x7E   /* 16b: StepSlot: the step across (sign-extended) */
#define STEP_DY            0x80   /* 16b: and down */
#define AREA_RANGE         0x7E   /* 16b: the area check: the range (+09 / +0A) */
#define AREA_POS           0x80   /* 16b: and the slot's position */
#define ANIM_ENTRY         0x7E   /* 16b: SetSlotAnimation: the entry (the slot's +02) */
#define SPRITE_NOW         0x7E   /* 16b: UpdateSlotSprite: the slot's sprite (+16) */
/* Long pointers: */
#define ANIM_TABLE         0x72   /* 24b $72-$74: the animation table (SetSlotAnimation) */
#define ANIM_TABLE_BANK    0x74   /* 8b: its bank */
#define GROUPS_LONG        0x72   /* 24b: StartEventScriptLong: the table of groups ... */
#define GROUP_LONG         0xC9   /* 24b: ... and the group's table of scripts */
#define GROUPS_SHORT       0x75   /* 24b: StartEventScript: the same two */
#define GROUP_SHORT        0x78
#define OBJ_X              0x9B   /* 16b: the game object routines' arguments (objects.c): */
#define OBJ_Y              0x9D   /*      the position, */
#define OBJ_FLIP           0x9F   /*      the flip (here the slot's animation), */
#define OBJ_SPRITE         0xA1   /*      the sprite, */
#define OBJ_NUMBER         0xA5   /*      the object's number */
#define WANDER_TIMER       0x92   /* 8b: WanderSlot: a copy of the step timer (+06) */
#define DP_SCRIPT_POS      0xC9   /* 24b $C9-$CB: the position in the event script */
#define DP_SCRIPT_BANK     0xCB   /* 8b: its bank */
#define DP_SLOT            0xCC   /* 24b $CC-$CE: the current event slot (CC slot) */
#define DP_SLOT_BANK       0xCE   /* 8b: DP_SLOT's bank */
#define COLLIDE_X          0xDF   /* 16b: CollideMap's input: the position of who moves */
#define COLLIDE_Y          0xE1   /* 16b */
#define COLLIDE_AHEAD      0xE3   /* 16b: how far ahead to look */
#define COLLIDE_STEP_X     0xE5   /* 16b: the step */
#define COLLIDE_STEP_Y     0xE7   /* 16b */
#define COLLIDE_TILE_1     0xE9   /* 16b: CollideMap's output: the first corner's tile type */
#define COLLIDE_TILE_2     0xEB   /* 16b: the second corner's tile type */

/* WRAM $7F:1Fxx (bus addresses): flags, some of them for one frame */
#define FLAGS_5A           0x7F1F5A
#define FLAGS_5C           0x7F1F5C
#define FLAGS_5E           0x7F1F5E
#define FLAGS_60           0x7F1F60

/* Fields of an event slot ([$CC],Y) */
#define SLOT_IN_USE        0x00   /* 8b: bit 0 in use */
#define SLOT_FLAGS         0x01   /* 8b: bit 0 has a sprite, 1 create the object, 2 the
                                     sprite changed, 3 wanders, 4 frozen, 5 walks,
                                     6 destroy the object, 7 may leave its area */
#define SLOT_ANIM_ENTRY    0x02   /* 8b: the entry in its animation table (the direction) */
#define SLOT_STEP_X        0x03   /* 8b signed: the step */
#define SLOT_STEP_Y        0x04   /* 8b signed */
#define SLOT_STEP_DELAY    0x05   /* 8b: frames between steps, StepSlot reloads SLOT_STEP_TIMER
                                     from it */
#define SLOT_STEP_TIMER    0x06   /* 8b: frames until the next step */
#define SLOT_WALK_STEPS    0x07   /* 8b: steps of a walk */
#define SLOT_STEPS_LEFT    0x08   /* 8b: steps left of this walk */
#define SLOT_RANGE_X       0x09   /* 8b: how far it may wander from home, across */
#define SLOT_RANGE_Y       0x0A   /* 8b: down */
#define SLOT_WALK_DELAY    0x0B   /* 8b: a walk's frames between steps */
#define SLOT_PRESSED       0x0C   /* 8b: the player pressed A on it (CollideEventObjects) */
#define SLOT_STATE         0x0E   /* 8b: here the tile type ahead */
#define SLOT_FORCED_DIR    0x0F   /* 8b: the direction the script forces: 1 down, 2 up,
                                     4 right, 8 left */
#define SLOT_FRAMES        0x10   /* 16b: the wait, in frames */
#define SLOT_OBJECT        0x12   /* 16b: its game object */
#define SLOT_ANIMATION     0x14   /* 16b: the object's animation (a byte x 64) */
#define SLOT_SPRITE        0x16   /* 16b: the object's sprite */
#define SLOT_LAST_SPRITE   0x18   /* 16b: the sprite last given to the object */
#define SLOT_X             0x1A   /* 16b: position */
#define SLOT_Y             0x1C   /* 16b */
#define SLOT_HOME_X        0x1E   /* 16b: the middle of its area */
#define SLOT_HOME_Y        0x20   /* 16b */
#define SLOT_SCRIPT        0x30   /* 16b: where its script is */
#define SLOT_SCRIPT_BANK   0x32   /* 8b */
#define SLOT_ANIM_TABLE    0x33   /* 16b: its standing animation table, $B3:<word> */
#define SLOT_WALK_TABLE    0x36   /* 16b: its walking animation table */
#define SLOT_NUMBER        0x3F   /* 8b: its own number */

/* Slot numbers (see PlaceAnimalsOnMap in animals.c) */
#define COW_SLOT0          0x18   /* cows: $18 + n */
#define CHICKEN_SLOT0      0x24   /* chickens: $24 + n */

/* ─────────────────────────────────────────────────────────────────────────
 * SetEventSlotPointer ($84:887C, JSL, RTL). A = slot number; $CC-$CE = its address
 * ($7E:B586 + 64 A). Leftovers: $7E = 64 A; A = (high byte of the address)
 * : $7E; C = carry of the add.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_SetEventSlotPointer(uint8_t *ram, HmRegs *r)
{
  uint16_t offset = (uint16_t)(r->a << 6);
  hm_dp_w16(ram, r, SLOT_OFFSET, offset);
  uint32_t address = (uint32_t)CC_SLOTS_BASE + offset;
  r->c = address > 0xFFFF;
  hm_dp_w16(ram, r, DP_SLOT, (uint16_t)address);
  *hm_dp(ram, r, DP_SLOT_BANK) = 0x7E;
  r->a = (uint16_t)((address & 0xFF00) | 0x7E);  /* SEP #$20; LDA #$7E */
}

/* ─────────────────────────────────────────────────────────────────────────
 * ResetCurrentEventSlotLong ($84:88D4, JSR, RTS — despite the name).
 * Marks the slot at $CC as in use and clears its state: bytes +01, +03..06,
 * +0C, +0F and words +10, +12, +14, +1A, +1C. Ends with A = 0, Y = $14.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_ResetCurrentEventSlotLong(uint8_t *ram, HmRegs *r)
{
  static const uint8_t bytes[] = { SLOT_FLAGS, SLOT_FORCED_DIR, SLOT_STEP_X, SLOT_STEP_Y, SLOT_STEP_DELAY,
                                   SLOT_STEP_TIMER, SLOT_PRESSED };
  static const uint8_t words[] = { SLOT_FRAMES, SLOT_X, SLOT_Y, SLOT_OBJECT, SLOT_ANIMATION };
  hm_bus_wr8(ram, hm_dp_long(ram, r, DP_SLOT, SLOT_IN_USE), 1);
  for (unsigned i = 0; i < sizeof bytes; i++)
    hm_bus_wr8(ram, hm_dp_long(ram, r, DP_SLOT, bytes[i]), 0);
  for (unsigned i = 0; i < sizeof words; i++)
    hm_bus_wr16(ram, hm_dp_long(ram, r, DP_SLOT, words[i]), 0);
  r->a = 0;
  r->y = SLOT_ANIMATION;
}

/* ─────────────────────────────────────────────────────────────────────────
 * ResetCurrentEventSlotShort ($84:8961, JSR, RTS). The short reset:
 * marks the slot in use, sets +01 to 1 and clears +03..06, +0F and the word
 * +10. Ends with A = 0, Y = $10.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_ResetCurrentEventSlotShort(uint8_t *ram, HmRegs *r)
{
  static const uint8_t zero_bytes[] = { SLOT_FORCED_DIR, SLOT_STEP_X, SLOT_STEP_Y, SLOT_STEP_DELAY, SLOT_STEP_TIMER };
  hm_bus_wr8(ram, hm_dp_long(ram, r, DP_SLOT, SLOT_IN_USE), 1);
  hm_bus_wr8(ram, hm_dp_long(ram, r, DP_SLOT, SLOT_FLAGS), 1);
  for (unsigned i = 0; i < sizeof zero_bytes; i++)
    hm_bus_wr8(ram, hm_dp_long(ram, r, DP_SLOT, zero_bytes[i]), 0);
  hm_bus_wr16(ram, hm_dp_long(ram, r, DP_SLOT, SLOT_FRAMES), 0);
  r->a = 0;
  r->y = SLOT_FRAMES;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Starting an event script: StartScriptAtTileInFront, StartEventScriptLong and
 * StartEventScript ($84:80F8, $84:8097, $84:803F; JSL, RTL).
 *   A = CC slot, X = script group, Y = script number in the group.
 * They point the slot ($CC, SetEventSlotPointer) at the script: +30 = the 16-bit
 * offset, entry `script` of the group's table, +32 = its bank. The group's
 * table is entry `group` (24-bit) of $B3:8000 (only the first entry of
 * Events_Bank_Table is used). Then:
 *  - Long: full reset, +3F = the slot's number;
 *  - StartScriptAtTileInFront: the same, and it also places the event on the tile in
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
  hm_SetEventSlotPointer(ram, r);
  hm_rtl(ram, r);

  hm_dp_w16(ram, r, SCRIPT_GROUP, group);
  hm_dp_w16(ram, r, SCRIPT_NUMBER, script);
  hm_dp_w16(ram, r, s->table_dp, hm_bus_rd16(ram, EVENTS_BANK_TABLE));
  *hm_dp(ram, r, s->table_dp + 2) = hm_bus_rd8(ram, EVENTS_BANK_TABLE + 2);
  uint16_t y = (uint16_t)(group * 3);
  hm_dp_w16(ram, r, s->group_dp, hm_bus_rd16(ram, hm_dp_long(ram, r, s->table_dp, y)));
  *hm_dp(ram, r, s->group_dp + 2) = hm_bus_rd8(ram, hm_dp_long(ram, r, s->table_dp, (uint16_t)(y + 2)));
  r->c = (script >> 15) & 1;                     /* ASL of $80 */
  uint16_t offset = hm_bus_rd16(ram, hm_dp_long(ram, r, s->group_dp, (uint16_t)(script << 1)));
  uint8_t bank = *hm_dp(ram, r, s->group_dp + 2);
  hm_bus_wr16(ram, hm_dp_long(ram, r, DP_SLOT, SLOT_SCRIPT), offset);
  hm_bus_wr8(ram, hm_dp_long(ram, r, DP_SLOT, SLOT_SCRIPT_BANK), bank);
  r->x = 2;
  if (s->short_reset) {
    hm_dp_w16(ram, r, DP_SCRIPT_POS, offset);
    *hm_dp(ram, r, DP_SCRIPT_BANK) = bank;
    jsr(ram, r, s->ret_reset, hm_ResetCurrentEventSlotShort);
  } else {
    jsr(ram, r, s->ret_reset, hm_ResetCurrentEventSlotLong);
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
  hm_bus_wr8(ram, hm_dp_long(ram, r, DP_SLOT, SLOT_NUMBER), (uint8_t)slot);
  r->a = slot;
  r->y = SLOT_NUMBER;
  return slot;
}

void hm_StartEventScript(uint8_t *ram, HmRegs *r)
{
  static const ScriptStart s = { 0x8044, 0x8095, GROUPS_SHORT, GROUP_SHORT, true };
  start_script(ram, r, &s);
}

void hm_StartEventScriptLong(uint8_t *ram, HmRegs *r)
{
  static const ScriptStart s = { 0x809D, 0x80EA, GROUPS_LONG, GROUP_LONG, false };
  start_script_numbered(ram, r, &s);
}

void hm_StartScriptAtTileInFront(uint8_t *ram, HmRegs *r)
{
  static const ScriptStart s = { 0x80FE, 0x814B, GROUPS_LONG, GROUP_LONG, false };
  start_script_numbered(ram, r, &s);
  hm_bus_wr16(ram, hm_dp_long(ram, r, DP_SLOT, SLOT_X), hm_rd16(ram, HM_TILE_IN_FRONT_X));
  r->a = hm_rd16(ram, HM_TILE_IN_FRONT_Y);
  hm_bus_wr16(ram, hm_dp_long(ram, r, DP_SLOT, SLOT_Y), r->a);
  r->y = SLOT_Y;
}

/* ─────────────────────────────────────────────────────────────────────────
 * ClearAllEventSlots ($84:8000, JSL, RTL): frees the 49 script slots
 * (the first byte of each to 0), each through SetEventSlotPointer.
 * Leftovers: $CC-$CE = the last slot, X = $31, Y = 0, C = 1, A = what
 * SetEventSlotPointer left with its low byte 0.
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_CLEAR_ALL 0x800A

void hm_ClearAllEventSlots(uint8_t *ram, HmRegs *r)
{
  for (uint16_t x = 0; x != 0x31; x++) {
    hm_push8(ram, r, (uint8_t)(x >> 8)); hm_push8(ram, r, (uint8_t)x);   /* PHX */
    r->a = x;                                                            /* TXA */
    hm_jsl(ram, r, RET_CLEAR_ALL, 0x84);
    hm_SetEventSlotPointer(ram, r);
    hm_rtl(ram, r);
    r->y = SLOT_IN_USE;
    r->a &= 0xFF00;
    hm_bus_wr8(ram, hm_dp_long(ram, r, DP_SLOT, SLOT_IN_USE), 0);
    hm_pull8(ram, r); hm_pull8(ram, r);                                  /* PLX */
  }
  r->x = 0x31;
  r->c = 1;
}

/* ─────────────────────────────────────────────────────────────────────────
 * SetSlotAnimation ($84:8895, JSL, RTL): the slot's animation from a table at
 * [$72]: 3 bytes per entry, entry = the slot's +02. The word goes to +16,
 * the byte (x 64) to +14.
 * Leftovers: $7E = the entry, A = byte x 64, Y = $14, C = 0.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_SetSlotAnimation(uint8_t *ram, HmRegs *r)
{
  uint16_t entry = hm_bus_rd8(ram, hm_dp_long(ram, r, DP_SLOT, SLOT_ANIM_ENTRY));
  hm_dp_w16(ram, r, ANIM_ENTRY, entry);
  uint16_t y = (uint16_t)(entry * 3);
  uint16_t word = hm_bus_rd16(ram, hm_dp_long(ram, r, ANIM_TABLE, y));
  hm_push8(ram, r, (uint8_t)(y >> 8)); hm_push8(ram, r, (uint8_t)y);   /* PHY */
  hm_bus_wr16(ram, hm_dp_long(ram, r, DP_SLOT, SLOT_SPRITE), word);
  hm_pull8(ram, r); hm_pull8(ram, r);                                  /* PLY */
  r->a = (uint16_t)(hm_bus_rd8(ram, hm_dp_long(ram, r, ANIM_TABLE, (uint16_t)(y + 2))) << 6);
  hm_bus_wr16(ram, hm_dp_long(ram, r, DP_SLOT, SLOT_ANIMATION), r->a);
  r->y = SLOT_ANIMATION;
  r->c = 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * FreeEventSlot ($84:8020, JSL, RTL): frees slot A (its +00 = 0, through
 * SetEventSlotPointer: $CC-$CE then point at it) and destroys its game object (the
 * slot's +12, DestroyGameObject).
 * Leftovers: those of DestroyGameObject.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_FreeEventSlot(uint8_t *ram, HmRegs *r)
{
  hm_jsl(ram, r, 0x8025, 0x84);
  hm_SetEventSlotPointer(ram, r);
  hm_rtl(ram, r);
  r->y = SLOT_IN_USE;
  r->a &= 0xFF00;
  hm_bus_wr8(ram, hm_dp_long(ram, r, DP_SLOT, SLOT_IN_USE), 0);
  r->y = SLOT_OBJECT;
  r->a = hm_bus_rd16(ram, hm_dp_long(ram, r, DP_SLOT, SLOT_OBJECT));
  hm_dp_w16(ram, r, OBJ_NUMBER, r->a);
  hm_jsl(ram, r, 0x803D, 0x85);
  hm_DestroyGameObject(ram, r);
  hm_rtl(ram, r);
}

/* ─────────────────────────────────────────────────────────────────────────
 * StepSlot ($84:8331, JSR, RTS): one step of a slot's walk. Only if +01
 * bit 5 (walking): the step timer +06 goes down; at 0 it is reloaded from
 * +05 and the slot moves by (+03, +04), signed bytes, added to its
 * position (+1A, +1C).
 * Leftovers: $7E / $80 = the step, A = the new Y (or the timer, or the
 * flags), Y = the last field touched, C from the add.
 * ───────────────────────────────────────────────────────────────────────── */
static uint8_t slot8(uint8_t *ram, HmRegs *r, uint16_t at)
{
  r->y = at;
  uint8_t v = hm_bus_rd8(ram, hm_dp_long(ram, r, DP_SLOT, at));
  r->a = (uint16_t)((r->a & 0xFF00) | v);
  return v;
}

void hm_StepSlot(uint8_t *ram, HmRegs *r)
{
  if (!(slot8(ram, r, SLOT_FLAGS) & 0x20)) { r->a &= 0xFF20; return; }  /* AND #$20 */
  uint8_t timer = (uint8_t)(slot8(ram, r, SLOT_STEP_TIMER) - 1);
  hm_bus_wr8(ram, hm_dp_long(ram, r, DP_SLOT, SLOT_STEP_TIMER), timer);
  if (slot8(ram, r, SLOT_STEP_TIMER)) return;
  uint8_t reload = slot8(ram, r, SLOT_STEP_DELAY);
  r->y = SLOT_STEP_TIMER;
  hm_bus_wr8(ram, hm_dp_long(ram, r, DP_SLOT, SLOT_STEP_TIMER), reload);
  int8_t dx = (int8_t)slot8(ram, r, SLOT_STEP_X);
  hm_dp_w16(ram, r, STEP_DX, (uint16_t)(int16_t)dx);
  int8_t dy = (int8_t)slot8(ram, r, SLOT_STEP_Y);
  hm_dp_w16(ram, r, STEP_DY, (uint16_t)(int16_t)dy);
  static const uint8_t at[2] = { SLOT_X, SLOT_Y };
  for (int i = 0; i < 2; i++) {
    r->y = at[i];
    uint16_t pos = hm_bus_rd16(ram, hm_dp_long(ram, r, DP_SLOT, at[i]));
    uint32_t sum = (uint32_t)pos + hm_dp_r16(ram, r, i ? STEP_DY : STEP_DX);
    r->c = sum > 0xFFFF;
    r->a = (uint16_t)sum;
    hm_bus_wr16(ram, hm_dp_long(ram, r, DP_SLOT, at[i]), r->a);
  }
}

/* ─────────────────────────────────────────────────────────────────────────
 * WanderSlot ($84:83CC, JSR, RTS): a slot that wanders on its own (+01
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
 *  - with +01 bit 7, the map (CollideMap; the tile type of the first corner
 *    goes to +0E): blocked, or a tile $FF or $C0-$CF ahead, stops it;
 *  - the tile $EB of the second corner (also when CollideMap was not called:
 *    then it is whatever the last check left there), the same way;
 *  - the player in the way (CollidePlayer) stops it too.
 * Tile $E1 (a door?) only lets the slots numbered $18-$23 (+3F) through.
 * Stopping: no step, +05/+06 = +0B, and the forced direction is cleared.
 * Leftovers: $92 = +06; $7E/$80 from the area check; $72-$74 = the
 * animation table; A, Y and C as each way out leaves them.
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_STAND_ANIM   0x86DC   /* JSL SetSlotAnimation at $84:86D9 */
#define RET_WALK_ANIM    0x8706   /* JSL SetSlotAnimation at $84:8703 */
#define RET_LOOK_MAP     0x87B1   /* JSL CollideMap at $84:87AE */
#define RET_LOOK_PLAYER  0x8810   /* JSL CollidePlayer at $84:880D */
#define ANIMATION_BANK   0xB3

static void put8(uint8_t *ram, HmRegs *r, uint16_t at, uint8_t v)
{
  r->y = at;
  r->a = (uint16_t)((r->a & 0xFF00) | v);
  hm_bus_wr8(ram, hm_dp_long(ram, r, DP_SLOT, at), v);
}
static void store8(uint8_t *ram, HmRegs *r, uint16_t at)   /* STA [$CC],Y */
{
  r->y = at;
  hm_bus_wr8(ram, hm_dp_long(ram, r, DP_SLOT, at), (uint8_t)r->a);
}
static uint16_t slot16(uint8_t *ram, HmRegs *r, uint16_t at)
{
  r->y = at;
  return r->a = hm_bus_rd16(ram, hm_dp_long(ram, r, DP_SLOT, at));
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
  uint16_t range = slot8_ext(ram, r, across ? SLOT_RANGE_X : SLOT_RANGE_Y);
  hm_dp_w16(ram, r, AREA_RANGE, range);
  uint16_t pos = slot16(ram, r, across ? SLOT_X : SLOT_Y);
  hm_dp_w16(ram, r, AREA_POS, pos);
  uint16_t home = slot16(ram, r, across ? SLOT_HOME_X : SLOT_HOME_Y);
  uint32_t edge = ahead ? (uint32_t)home + range : (uint32_t)home - range + 0x10000;
  r->a = (uint16_t)edge;                         /* CLC; ADC / SEC; SBC */
  r->c = r->a >= pos;                            /* CMP $80 */
  return ahead ? r->c : !r->c;
}

/* the slot's animation from the table at +33 (standing) or +36 (walking),
 * then +01 |= 4 */
static void set_slot_animation(uint8_t *ram, HmRegs *r, uint16_t table, uint16_t ret)
{
  hm_dp_w16(ram, r, ANIM_TABLE, slot16(ram, r, table));
  r->a = (uint16_t)((r->a & 0xFF00) | ANIMATION_BANK);
  *hm_dp(ram, r, ANIM_TABLE_BANK) = ANIMATION_BANK;
  hm_jsl(ram, r, ret, 0x84);
  hm_SetSlotAnimation(ram, r);
  hm_rtl(ram, r);
  put8(ram, r, SLOT_FLAGS, slot8(ram, r, SLOT_FLAGS) | 0x04);
}

static void stand_still(uint8_t *ram, HmRegs *r)        /* .CODE_8486B3 */
{
  put8(ram, r, SLOT_STEP_X, 0);
  put8(ram, r, SLOT_STEP_Y, 0);
  put8(ram, r, SLOT_STEPS_LEFT, 0);
  set_slot_animation(ram, r, SLOT_ANIM_TABLE, RET_STAND_ANIM);
}

static void stop_here(uint8_t *ram, HmRegs *r)          /* .CODE_848817 */
{
  put8(ram, r, SLOT_STEP_X, 0);
  put8(ram, r, SLOT_STEP_Y, 0);
  slot8(ram, r, SLOT_WALK_DELAY);
  store8(ram, r, SLOT_STEP_DELAY);
  store8(ram, r, SLOT_STEP_TIMER);
  put8(ram, r, SLOT_FORCED_DIR, 0);
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
  uint8_t self = slot8(ram, r, SLOT_NUMBER);
  r->c = self >= CHICKEN_SLOT0;
  if (r->c) return false;
  r->c = self >= COW_SLOT0;
  return r->c;
}

void hm_WanderSlot(uint8_t *ram, HmRegs *r)
{
  uint8_t flags = slot8(ram, r, SLOT_FLAGS);
  r->a = (uint16_t)((r->a & 0xFF00) | (flags & 0x08));
  if (!(flags & 0x08)) return;
  uint8_t timer = slot8(ram, r, SLOT_STEP_TIMER);
  *hm_dp(ram, r, WANDER_TIMER) = timer;
  uint8_t delay = slot8(ram, r, SLOT_STEP_DELAY);
  r->c = delay >= timer;
  if (delay != timer) return;

  /* the direction to start, or -1 to go on, -2 to stand still */
  int dir;
  if (slot8(ram, r, SLOT_STEPS_LEFT) == 0) {
    slot8(ram, r, SLOT_WALK_STEPS);
    store8(ram, r, SLOT_STEPS_LEFT);
    uint8_t forced = slot8(ram, r, SLOT_FORCED_DIR);
    static const uint8_t forced_bit[4] = { 1, 2, 4, 8 };
    dir = -2;
    for (int i = 0; forced && i < 4; i++)
      if (forced == forced_bit[i]) { dir = i; break; }
    if (dir == -2) {
      uint8_t random = *hm_dp(ram, r, HM_RNG_LAST);
      r->a = (uint16_t)((r->a & 0xFF00) | random);
      if (random < 4) dir = random;
    }
  } else if (slot8(ram, r, SLOT_FLAGS) & 0x80) {
    r->a &= 0xFF80;
    dir = -1;
  } else {
    /* going on: the area, on the side of the current direction (0-3;
     * anything else counts as down). (The carry of the comparisons here
     * and in the choice above never survives: the area check or
     * SetSlotAnimation sets it.) */
    uint8_t now = slot8(ram, r, SLOT_ANIM_ENTRY);
    switch (now < 4 ? now : 0) {
    case 0:  dir = inside_area(ram, r, false, true) ? -1 : -2; break;
    case 1:  dir = inside_area(ram, r, false, false) ? -1 : -2; break;
    case 2:  dir = inside_area(ram, r, true, true) ? -1 : -2; break;
    default: dir = inside_area(ram, r, true, false) ? -1 : -2; break;
    }
  }

  if (dir >= 0) {
    if (slot8(ram, r, SLOT_FLAGS) & 0x80) {
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
    slot8(ram, r, SLOT_WALK_DELAY);
    store8(ram, r, SLOT_STEP_DELAY);
    store8(ram, r, SLOT_STEP_TIMER);
    put8(ram, r, SLOT_STEP_X, (uint8_t)step[dir][0]);
    put8(ram, r, SLOT_STEP_Y, (uint8_t)step[dir][1]);
    put8(ram, r, SLOT_ANIM_ENTRY, (uint8_t)dir);
    set_slot_animation(ram, r, SLOT_WALK_TABLE, RET_WALK_ANIM);
  } else {
    slot8(ram, r, SLOT_STEPS_LEFT);
    put8(ram, r, SLOT_STEPS_LEFT, (uint8_t)(r->a - 1));
  }

  /* looking ahead */
  int8_t dx = (int8_t)slot8(ram, r, SLOT_STEP_X);
  if (!dx && !slot8(ram, r, SLOT_STEP_Y)) return;
  hm_dp_w16(ram, r, COLLIDE_X, slot16(ram, r, SLOT_X));
  hm_dp_w16(ram, r, COLLIDE_Y, slot16(ram, r, SLOT_Y));
  int8_t sx = (int8_t)slot8_ext(ram, r, SLOT_STEP_X);
  if (sx < 0) r->a = (uint8_t)-sx;               /* EOR #$FF; INC (8 bits) */
  hm_dp_w16(ram, r, COLLIDE_STEP_X, r->a);
  int8_t sy = (int8_t)slot8_ext(ram, r, SLOT_STEP_Y);
  if (sy < 0) r->a = (uint8_t)-sy;
  hm_dp_w16(ram, r, COLLIDE_STEP_Y, r->a);
  hm_dp_w16(ram, r, COLLIDE_AHEAD, 0);

  int ahead;
  if (slot8(ram, r, SLOT_FLAGS) & 0x80) {
    r->a &= 0xFF80;
    slot8_ext(ram, r, SLOT_ANIM_ENTRY);
    hm_jsl(ram, r, RET_LOOK_MAP, 0x83);
    hm_CollideMap(ram, r);
    hm_rtl(ram, r);
    uint16_t blocked = r->a;
    hm_push8(ram, r, (uint8_t)(blocked >> 8)); hm_push8(ram, r, (uint8_t)blocked);  /* PHA */
    uint16_t first = hm_dp_r16(ram, r, COLLIDE_TILE_1);
    r->a = first;
    store8(ram, r, SLOT_STATE);
    hm_pull8(ram, r); hm_pull8(ram, r);          /* PLA */
    r->a = blocked;
    r->c = 1;                                    /* CMP #0 */
    if (blocked) { stop_here(ram, r); return; }
    ahead = tile_ahead(r, first);
    if (ahead == 2) ahead = may_pass_e1(ram, r) ? 0 : 1;
    if (ahead) { stop_here(ram, r); return; }
  }
  ahead = tile_ahead(r, hm_dp_r16(ram, r, COLLIDE_TILE_2));
  if (ahead == 2) ahead = may_pass_e1(ram, r) ? 0 : 1;
  if (ahead) { stop_here(ram, r); return; }

  slot8_ext(ram, r, SLOT_ANIM_ENTRY);
  hm_jsl(ram, r, RET_LOOK_PLAYER, 0x83);
  hm_CollidePlayer(ram, r);
  hm_rtl(ram, r);
  r->c = 1;                                      /* CMP #0 */
  if (r->a) stop_here(ram, r);
}

/* ─────────────────────────────────────────────────────────────────────────
 * UpdateSlotSprite ($84:8286, JSR, RTS): a slot's sprite, each frame. Unless
 * +01 bit 4 (frozen), the slot first wanders (WanderSlot) and steps
 * (StepSlot). Then its game object (+12) gets the slot's position (+1A,
 * +1C), animation (+14) and sprite (+16), through $A5, $9F, $9B, $9D, $A1;
 * and by +01:
 *   bit 6  destroy the object (DestroyGameObject);
 *   bit 1  create it (CreateGameObject; the new object goes to +12);
 *   bit 2  the sprite changed (+16 != +18, the last one): ChangeGameObjectSprite;
 *   none   just move it (MoveGameObject).
 * Then +18 = +16 and the one-time bits 1, 2 and 4 of +01 are cleared
 * (AND #$E9).
 * Leftovers: A = the new +01, Y = 1; X and C from the object routine.
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_WANDER       0x8297   /* JSR WanderSlot at $84:8295 */
#define RET_STEP         0x829A   /* JSR StepSlot at $84:8298 */
#define RET_MOVE_OBJECT  0x82DC   /* JSL MoveGameObject at $84:82D9 */
#define RET_NEW_OBJECT   0x82E2   /* JSL CreateGameObject at $84:82DF */
#define RET_NEW_SPRITE   0x8307   /* JSL ChangeGameObjectSprite at $84:8304 */
#define RET_DROP_OBJECT  0x830D   /* JSL DestroyGameObject at $84:830A */

static void object_call(uint8_t *ram, HmRegs *r, uint16_t ret, void (*fn)(uint8_t *, HmRegs *))
{
  hm_jsl(ram, r, ret, 0x85);
  fn(ram, r);
  hm_rtl(ram, r);
}

void hm_UpdateSlotSprite(uint8_t *ram, HmRegs *r)
{
  uint8_t flags = slot8(ram, r, SLOT_FLAGS);
  r->a = (uint16_t)((r->a & 0xFF00) | (flags & 0x10));
  if (!(flags & 0x10)) {
    jsr(ram, r, RET_WANDER, hm_WanderSlot);
    jsr(ram, r, RET_STEP, hm_StepSlot);
  }
  static const struct { uint8_t field, dp; } to_object[5] = {
    { SLOT_OBJECT, OBJ_NUMBER }, { SLOT_ANIMATION, OBJ_FLIP }, { SLOT_X, OBJ_X }, { SLOT_Y, OBJ_Y },
    { SLOT_SPRITE, OBJ_SPRITE } };
  for (int i = 0; i < 5; i++)
    hm_dp_w16(ram, r, to_object[i].dp, slot16(ram, r, to_object[i].field));

  flags = slot8(ram, r, SLOT_FLAGS);
  if (flags & 0x40) {
    r->a = (uint16_t)((r->a & 0xFF00) | 0x40);
    object_call(ram, r, RET_DROP_OBJECT, hm_DestroyGameObject);
  } else if (flags & 0x02) {
    r->a = (uint16_t)((r->a & 0xFF00) | 0x02);
    object_call(ram, r, RET_NEW_OBJECT, hm_CreateGameObject);
    r->a = hm_dp_r16(ram, r, OBJ_NUMBER);
    r->y = SLOT_OBJECT;
    hm_bus_wr16(ram, hm_dp_long(ram, r, DP_SLOT, SLOT_OBJECT), r->a);
  } else if (flags & 0x04) {
    r->a = (uint16_t)((r->a & 0xFF00) | 0x04);
    uint16_t sprite = slot16(ram, r, SLOT_SPRITE);
    hm_dp_w16(ram, r, SPRITE_NOW, sprite);
    uint16_t last = slot16(ram, r, SLOT_LAST_SPRITE);
    r->c = last >= sprite;                       /* CMP $7E */
    if (last != sprite) object_call(ram, r, RET_NEW_SPRITE, hm_ChangeGameObjectSprite);
  } else {
    r->a &= 0xFF00;
    object_call(ram, r, RET_MOVE_OBJECT, hm_MoveGameObject);
  }
  slot16(ram, r, SLOT_SPRITE);
  r->y = SLOT_LAST_SPRITE;
  hm_bus_wr16(ram, hm_dp_long(ram, r, DP_SLOT, SLOT_LAST_SPRITE), r->a);
  put8(ram, r, SLOT_FLAGS, slot8(ram, r, SLOT_FLAGS) & 0xE9);
}

/* ─────────────────────────────────────────────────────────────────────────
 * EventInstructionInterpreter ($84:8249, JSR, RTS): one command of the
 * script of the slot at $CC: $C9-$CB = its position (+30, +32), the
 * command's routine from EventInstructionPointers ($84:9419, 90 words,
 * JSR (abs,X)); then the new position goes back to the slot.
 * Leftovers: A = $CB (8-bit A), Y = $32, X and C those of the command.
 * ───────────────────────────────────────────────────────────────────────── */
static void (*const EVENT_COMMANDS[90])(uint8_t *, HmRegs *) = {
  hm_Event_SetWantedSong, hm_Event_TimeRunning, hm_Event_TimeStopped, hm_Event_SetHour,
  hm_Event_DoNothing, hm_Event_SetPlayerPosition, hm_Event_SetTransitionDest, hm_Event_SetPlayerDirection,
  hm_Event_DisablePlayerControl, hm_Event_StartOtherSlot, hm_Event_ScreenFadeIn, hm_Event_SetSceneFlag,
  hm_Event_WaitForSceneFlag, hm_Event_MoveSlot, hm_Event_ChangeToWantedSong, hm_Event_ScreenFadeOut,
  hm_Event_FreeOwnSlot, hm_Event_EnablePlayerControl, hm_Event_JumpToInstruction, hm_Event_WaitFrames,
  hm_Event_JumpIfFlagSet, hm_Event_JumpIfEquals, hm_Event_JumpIfBetween, hm_Event_GetRNG,
  hm_Event_JumpIfEqualsRNG, hm_Event_SetPlayerAnimation, hm_Event_PlaceSlot, hm_Event_SetSlotSprite,
  hm_Event_StartTextBox, hm_Event_StartTextBoxCopy, hm_Event_TalkIfSpokenTo, hm_Event_TalkIfSpokenTo,
  hm_Event_JumpIfAnswer, hm_Event_AddToByteClamped, hm_Event_WanderInArea, hm_Event_SetFlag,
  hm_Event_TimeOfDayPalette, hm_Event_FreeOtherSlot, hm_Event_ResetFlag, hm_Event_ResetFlag,
  hm_Event_ResetFlag, hm_Event_MapScrolling, hm_Event_StartMapAnimation, hm_Event_SetSlotAnimation,
  hm_Event_PlayEffectAndWait, hm_Event_TalkOrTakeShownItem, hm_Event_WanderFreely, hm_Event_SetStandingAnimation,
  hm_Event_ChickenFrame, hm_Event_CowFrame, hm_Event_StoreValue, hm_Event_PickUpMole,
  hm_Event_PickUpFish, hm_Event_HorseFrame, hm_Event_DogFrame, hm_Event_DestroyOwnObject,
  hm_Event_WaitUntilSkippable, hm_Event_WalkPlayer, hm_Event_RunPlayer, hm_Event_ChangeItemOnHand,
  hm_Event_TransitionToHouse, hm_Event_TransitionToMap, hm_Event_SetItemOnHand, hm_Event_DropItemAnimation,
  hm_Event_IgnoreTileProperties, hm_Event_AddToWordMax999, hm_Event_ChangeMoney, hm_Event_JumpIfEqualsValue,
  hm_Event_JumpIfEqualsValueLong, hm_Event_JumpIfBetweenValue, hm_Event_JumpIfBetweenValueLong, hm_Event_SetValueByte,
  hm_Event_SetValueLong, hm_Event_SetSlotAnimationPlayerDir, hm_Event_PickUpPowerBerry, hm_Event_EditTileOnMap,
  hm_Event_MakeSlotPassable, hm_Event_FacePlayerIfSpokenTo, hm_Event_UseTileProperties, hm_Event_DisableTools,
  hm_Event_PutDownCarried, hm_Event_Cmd51_PutDownCarriedQuietly, hm_Event_Cmd52_PickUpChild1, hm_Event_Cmd53_PickUpChild2,
  hm_Event_Cmd54_AutoCloseTextBox, hm_Event_Cmd55_JumpIfBitClear, hm_Event_Cmd56_UseToolInHand, hm_Event_Cmd57_ChangeStamina,
  hm_Event_Cmd58_SetFarmTile, hm_Event_Cmd59_SwapTools,
};

void hm_EventInstructionInterpreter(uint8_t *ram, HmRegs *r)
{
  r->y = SLOT_SCRIPT;
  r->a = hm_bus_rd16(ram, hm_dp_long(ram, r, DP_SLOT, SLOT_SCRIPT));
  hm_dp_w16(ram, r, DP_SCRIPT_POS, r->a);
  r->y = SLOT_SCRIPT_BANK;
  uint8_t bank = hm_bus_rd8(ram, hm_dp_long(ram, r, DP_SLOT, SLOT_SCRIPT_BANK));
  r->a = (uint16_t)((r->a & 0xFF00) | bank);
  *hm_dp(ram, r, DP_SCRIPT_BANK) = bank;
  uint8_t cmd = hm_bus_rd8(ram, hm_dp_long(ram, r, DP_SCRIPT_POS, 0));   /* LDA #0; XBA; LDA [$C9] */
  r->a = (uint16_t)(cmd << 1);                                    /* REP #$20; ASL; TAX */
  r->c = 0;
  r->x = r->a;
  if (cmd < 90) jsr(ram, r, 0x826C, EVENT_COMMANDS[cmd]);
  r->a = hm_dp_r16(ram, r, DP_SCRIPT_POS);                        /* REP #$30 */
  r->y = SLOT_SCRIPT;
  hm_bus_wr16(ram, hm_dp_long(ram, r, DP_SLOT, SLOT_SCRIPT), r->a);
  r->a = (uint16_t)((r->a & 0xFF00) | *hm_dp(ram, r, DP_SCRIPT_BANK));
  r->y = SLOT_SCRIPT_BANK;
  hm_bus_wr8(ram, hm_dp_long(ram, r, DP_SLOT, SLOT_SCRIPT_BANK), (uint8_t)r->a);
}

/* ─────────────────────────────────────────────────────────────────────────
 * RunEventSlots ($84:816F, JSL, RTL): every frame, unless a text box
 * waits ($019A = 2 or 6) or the player is in action 3: a random number to
 * $DE (GetRNG) and then each of the 49 slots in use (+00 bit 0): while its
 * wait (+10) is 0 its script runs one command after another
 * (EventInstructionInterpreter; +03/+04 cleared first unless +01 bit 3),
 * else the wait goes down by one (and at 0 the script runs this same
 * frame); $DE + $33 for the next slot; a slot with a sprite (+01 bit 0)
 * gets it updated (UpdateSlotSprite). At the end the one-frame bits are cleared:
 * $7F1F5A bits 4, 6, 7, 8; $7F1F5C bit 10; $7F1F5E bit 6; $7F1F60 bit 4.
 * Leftovers: A = the new $7F1F60, C = 1 (or that of the compare that
 * stopped it), X = $31 after the slots.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_RunEventSlots(uint8_t *ram, HmRegs *r)
{
  uint8_t input = hm_bus_rd8(ram, (uint32_t)r->db << 16 | HM_INPUT_STATE);
  r->a = (uint16_t)((r->a & 0xFF00) | input);
  r->c = input >= 2;
  bool skip = input == 2;
  if (!skip) { r->c = input >= 6; skip = input == 6; }
  if (!skip) {
    r->a = hm_dp_r16(ram, r, HM_PLAYER_ACTION);
    r->c = r->a >= 3;
    skip = r->a == 3;
  }
  if (!skip) {
    hm_jsl(ram, r, 0x8193, 0x83);
    hm_GetRNG(ram, r);
    hm_rtl(ram, r);
    *hm_dp(ram, r, HM_RNG_LAST) = (uint8_t)r->a;
    hm_dp_w16(ram, r, DP_SLOT, CC_SLOTS_BASE);
    r->a = 0xB57E;                                              /* SEP #$20; LDA #$7E */
    *hm_dp(ram, r, DP_SLOT_BANK) = 0x7E;
    r->x = 0;
    do {
      hm_push8(ram, r, (uint8_t)(r->x >> 8));                   /* PHX */
      hm_push8(ram, r, (uint8_t)r->x);
      r->y = SLOT_IN_USE;
      uint8_t used = hm_bus_rd8(ram, hm_dp_long(ram, r, DP_SLOT, SLOT_IN_USE));
      r->a = (uint16_t)((r->a & 0xFF00) | (used & 0x01));
      if (used & 0x01) {
        r->y = SLOT_FRAMES;
        r->a = hm_bus_rd16(ram, hm_dp_long(ram, r, DP_SLOT, SLOT_FRAMES));
        if (r->a) {                                             /* DEC A; STA */
          r->a = (uint16_t)(r->a - 1);
          hm_bus_wr16(ram, hm_dp_long(ram, r, DP_SLOT, SLOT_FRAMES), r->a);
        }
        for (;;) {
          r->y = SLOT_FRAMES;
          r->a = hm_bus_rd16(ram, hm_dp_long(ram, r, DP_SLOT, SLOT_FRAMES));
          if (r->a) break;
          r->y = SLOT_FLAGS;
          uint8_t flags = hm_bus_rd8(ram, hm_dp_long(ram, r, DP_SLOT, SLOT_FLAGS));
          r->a = (uint16_t)((r->a & 0xFF00) | (flags & 0x08));
          if (!(flags & 0x08)) {
            r->y = SLOT_STEP_X;
            r->a = 0;
            hm_bus_wr16(ram, hm_dp_long(ram, r, DP_SLOT, SLOT_STEP_X), 0);
          }
          jsr(ram, r, 0x81DC, hm_EventInstructionInterpreter);
        }
        uint32_t de = (uint32_t)*hm_dp(ram, r, HM_RNG_LAST) + 0x33;   /* SEP #$20; CLC; ADC */
        r->c = de > 0xFF;
        r->a = (uint16_t)((r->a & 0xFF00) | (uint8_t)de);
        *hm_dp(ram, r, HM_RNG_LAST) = (uint8_t)de;
        r->y = SLOT_FLAGS;
        uint8_t flags = hm_bus_rd8(ram, hm_dp_long(ram, r, DP_SLOT, SLOT_FLAGS));
        r->a = (uint16_t)((r->a & 0xFF00) | (flags & 0x01));
        if (flags & 0x01) jsr(ram, r, 0x8208, hm_UpdateSlotSprite);
      }
      uint32_t next = (uint32_t)hm_dp_r16(ram, r, DP_SLOT) + CC_SLOT_SIZE;  /* REP #$30; CLC; ADC */
      r->a = (uint16_t)next;
      hm_dp_w16(ram, r, DP_SLOT, r->a);
      uint8_t lo = hm_pull8(ram, r);                            /* PLX; INX */
      r->x = (uint16_t)((lo | hm_pull8(ram, r) << 8) + 1);
      r->c = r->x >= 0x31;
    } while (r->x != 0x31);
  }
  static const struct { uint32_t at; uint16_t keep; } clear[4] = {
    { FLAGS_5A, 0xFE2F }, { FLAGS_5C, 0xFBFF }, { FLAGS_5E, 0xFFBF }, { FLAGS_60, 0xFFEF } };
  for (int i = 0; i < 4; i++) {
    r->a = (uint16_t)(hm_bus_rd16(ram, clear[i].at) & clear[i].keep);
    hm_bus_wr16(ram, clear[i].at, r->a);
  }
}
