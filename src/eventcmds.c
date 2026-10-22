/*
 * eventcmds.c — commands of the event scripts (bank $84).
 *
 * An event script runs in a CC slot (see events.c): $CC-$CE points at the
 * slot, $C9 is the position in the script. Each command is a routine
 * (JSR / RTS) that does its work, moves $C9 past its bytes and usually
 * counts a frame in the slot's +10.
 *
 * Slot fields used here: +00 in use, +01 flags, +0C "can be picked up",
 * +0E state ($E0 = being sold), +10 frame counter (16b), +1A / +1C the
 * position (16b), +3F the slot's number (animals: chickens $24 + n, cows
 * $18 + n; see PlaceAnimalsOnMap in animals.c).
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

#define HM_JOY_NEW        0x00128   /* 16b: buttons pressed this frame */
#define JOY_A             0x0080
#define HM_ITEM_HELD      0x0091D   /* 8b: what the player carries */
#define HM_CARRIED_SLOT   0x00920   /* 8b: the CC slot being carried */
#define CHICKEN_TIMERS    0x0093B   /* 16b per chicken */
#define FLAGS_5A          0x11F5A
#define FLAGS_60          0x11F60

/* ── access through [$CC] (the slot) and [$72] (an animal record) ─────── */
static uint8_t lda8(HmRegs *r, uint8_t v)
{
  r->a = (uint16_t)((r->a & 0xFF00) | v);
  return v;
}
static uint8_t slot8(uint8_t *ram, HmRegs *r, uint16_t y)
{
  r->y = y;
  return hm_bus_rd8(ram, hm_dp_long(ram, r, 0xCC, y));
}
static uint16_t slot16(uint8_t *ram, HmRegs *r, uint16_t y)
{
  r->y = y;
  return hm_bus_rd16(ram, hm_dp_long(ram, r, 0xCC, y));
}
static void slot_w8(uint8_t *ram, HmRegs *r, uint16_t y, uint8_t v)
{
  r->y = y;
  hm_bus_wr8(ram, hm_dp_long(ram, r, 0xCC, y), v);
}
static void slot_w16(uint8_t *ram, HmRegs *r, uint16_t y, uint16_t v)
{
  r->y = y;
  hm_bus_wr16(ram, hm_dp_long(ram, r, 0xCC, y), v);
}
static uint8_t record8(uint8_t *ram, HmRegs *r, uint16_t y)
{
  r->y = y;
  return hm_bus_rd8(ram, hm_dp_long(ram, r, 0x72, y));
}
/* LDA [$CC],Y; CLC; ADC #1; STA */
static void count_frame(uint8_t *ram, HmRegs *r)
{
  uint16_t v = slot16(ram, r, 0x10);
  r->c = v == 0xFFFF;
  r->a = (uint16_t)(v + 1);
  slot_w16(ram, r, 0x10, r->a);
}
/* LDY #$3F; LDA [$CC],Y; SEC; SBC #base; XBA; LDA #0; XBA: the animal's
 * number, 8 bits, in a clean 16-bit A */
static uint16_t animal_number(uint8_t *ram, HmRegs *r, uint8_t base)
{
  uint8_t slot = slot8(ram, r, 0x3F);
  r->c = slot >= base;
  r->a = (uint8_t)(slot - base);
  return r->a;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Event_UNKChickenRelated ($84:9857, JSR, RTS). A chicken's every frame
 * (one command byte):
 *   - its record (GetChickenPointer) follows the slot's position;
 *   - being sold (slot +0E = $E0, chicken status bit $08, $7F1F5A bit 3):
 *     the sale: price $32 in $7F1F15/17, $09A1 = its slot, dialog $44
 *     ($096E-$0970), game state bit $40, the player idle, +0E = 0,
 *     +01 &= $57;
 *   - picked up: when the slot allows it (+0C), the player presses A with
 *     empty hands and is free to (not using a tool, not in a menu or a
 *     scene: actions $0A, $0C, $0D, $1B, $17, $04 and game state bits
 *     $04, $40, $0800 and $7F1F60 bits $06 all block it), and the chicken
 *     can be carried (status bits $0E, not $40): it is marked carried
 *     (status $20), $0920 = its slot, the slot stops (+00 = 0, +01 |= $40)
 *     and the player holds it (action 4): item $26 for a chick (status
 *     $04), $25 for a hen (status $08);
 *   - otherwise a hen (status $08) counts its timer ($093B + 2 n) up to
 *     $200; then it starts script $5D ($DE >= $55) or $5C on its slot.
 * Each frame +10 counts one (not when picked up).
 * ───────────────────────────────────────────────────────────────────────── */
static bool player_free_to_pick_up(uint8_t *ram, HmRegs *r)
{
  static const uint16_t BUSY_ACTIONS[] = { 0x000A, 0x000C, 0x000D, 0x001B };
  if (lda8(r, slot8(ram, r, 0x0C)) == 0) { r->c = 1; return false; }   /* CMP #0 */
  uint16_t action = r->a = hm_dp_r16(ram, r, HM_PLAYER_ACTION);
  for (unsigned i = 0; i < 4; i++) {
    r->c = action >= BUSY_ACTIONS[i];
    if (action == BUSY_ACTIONS[i]) return false;
  }
  uint16_t state = hm_dp_r16(ram, r, 0xD2);
  if ((r->a = state & 0x0004)) return false;
  if ((r->a = state & 0x0040)) return false;
  if ((r->a = hm_rd16(ram, FLAGS_60) & 0x0006)) return false;
  r->a = hm_rd16(ram, HM_JOY_NEW);
  if (!(r->a & JOY_A)) return false;                                   /* BIT #$80 */
  if ((r->a = state & 0x0800)) return false;
  r->a = action;
  r->c = action >= 0x0017;
  if (action == 0x0017) return false;
  r->c = action >= 0x0004;
  if (action == 0x0004) return false;
  return lda8(r, ram[HM_ITEM_HELD]) == 0;
}

/* returns true when it ends the command early (the chicken was taken) */
static bool pick_up(uint8_t *ram, HmRegs *r)
{
  uint8_t status = record8(ram, r, 0);
  if (lda8(r, status & 0x40)) return false;
  if (!lda8(r, status & 0x0E)) return false;
  status = lda8(r, status | 0x20);
  hm_bus_wr8(ram, hm_dp_long(ram, r, 0x72, 0), status);
  ram[HM_CARRIED_SLOT] = lda8(r, slot8(ram, r, 0x3F));
  lda8(r, 0);
  slot_w8(ram, r, 0x00, 0);
  slot_w8(ram, r, 0x01, lda8(r, slot8(ram, r, 0x01) | 0x40));
  count_frame(ram, r);
  uint8_t slot = lda8(r, slot8(ram, r, 0x3F));
  r->c = slot >= 0x24;
  if (slot < 0x24) return false;
  uint8_t item;
  if (lda8(r, record8(ram, r, 0) & 0x04)) item = 0x26;                 /* a chick */
  else if (lda8(r, record8(ram, r, 0) & 0x08)) item = 0x25;            /* a hen */
  else return false;
  ram[HM_ITEM_HELD] = lda8(r, item);
  r->a = 4;
  hm_dp_w16(ram, r, HM_PLAYER_ACTION, 4);
  return true;
}

static void hen_timer(uint8_t *ram, HmRegs *r)
{
  if (!lda8(r, record8(ram, r, 0) & 0x08)) return;
  uint16_t n = animal_number(ram, r, 0x24);
  r->c = (n >> 15) & 1;                                                /* ASL */
  r->a = r->x = (uint16_t)(n << 1);
  uint16_t timer = r->a = hm_rd16(ram, CHICKEN_TIMERS + r->x);
  r->c = timer >= 0x0200;
  if (timer != 0x0200) {
    r->a = (uint16_t)(timer + 1);
    hm_wr16(ram, CHICKEN_TIMERS + r->x, r->a);
    return;
  }
  hm_wr16(ram, CHICKEN_TIMERS + r->x, 0);
  uint8_t de = lda8(r, *hm_dp(ram, r, 0xDE));
  r->c = de >= 0x55;
  uint16_t script = de >= 0x55 ? 0x005D : 0x005C;
  uint16_t ret = de >= 0x55 ? 0x9A1F : 0x9A3D;
  r->a = slot8(ram, r, 0x3F);                                          /* XBA; LDA #0; XBA */
  r->x = 0; r->y = script;
  hm_jsl(ram, r, ret, 0x84);
  hm_UNK_LoadCCDataShort(ram, r);
  hm_rtl(ram, r);
}

static void sell(uint8_t *ram, HmRegs *r)
{
  r->a = hm_rd16(ram, FLAGS_5A) & 0xFFF7;
  hm_wr16(ram, FLAGS_5A, r->a);
  r->a = 0x0032;
  hm_wr16(ram, 0x11F15, 0x0032);                                       /* the price */
  ram[0x11F17] = lda8(r, 0);
  ram[0x009A1] = lda8(r, slot8(ram, r, 0x3F));
  ram[0x0096E] = lda8(r, 0x44);
  ram[0x0096F] = 0; ram[0x00970] = 0;
  r->a = hm_dp_r16(ram, r, 0xD2) | 0x0040;
  hm_dp_w16(ram, r, 0xD2, r->a);
  r->a = 0;
  hm_dp_w16(ram, r, HM_PLAYER_ACTION, 0);
  slot_w8(ram, r, 0x0E, lda8(r, 0));
  slot_w8(ram, r, 0x01, lda8(r, slot8(ram, r, 0x01) & 0x57));
}

void hm_Event_UNKChickenRelated(uint8_t *ram, HmRegs *r)
{
  uint16_t pc = hm_dp_r16(ram, r, 0xC9);
  r->c = pc == 0xFFFF;
  r->a = (uint16_t)(pc + 1);
  hm_dp_w16(ram, r, 0xC9, r->a);

  r->a = animal_number(ram, r, 0x24);
  hm_jsl(ram, r, 0x9878, 0x83);
  hm_GetChickenPointer(ram, r);
  hm_rtl(ram, r);
  r->a = slot16(ram, r, 0x1A);
  r->y = 4; hm_bus_wr16(ram, hm_dp_long(ram, r, 0x72, 4), r->a);
  r->a = slot16(ram, r, 0x1C);
  r->y = 6; hm_bus_wr16(ram, hm_dp_long(ram, r, 0x72, 6), r->a);

  uint8_t state = lda8(r, slot8(ram, r, 0x0E));
  r->c = state >= 0xE0;
  if (state == 0xE0 && lda8(r, record8(ram, r, 0) & 0x08)
      && (r->a = hm_rd16(ram, FLAGS_5A) & 0x0008)) {
    sell(ram, r);
  } else if (player_free_to_pick_up(ram, r)) {
    if (pick_up(ram, r)) return;
  } else {
    hen_timer(ram, r);
  }
  count_frame(ram, r);
}

/* ─────────────────────────────────────────────────────────────────────────
 * Event_UNKDogRelated ($84:AF06, JSR, RTS). The dog's every frame (slot
 * $16, one command byte):
 *   - its position goes to $7F1F2C / $7F1F2E;
 *   - a hug: the player presses A next to it, free as for picking up a
 *     chicken (see above; also not while game state bit $10): the slot
 *     stops (+00 = 0, +01 |= $40), game state bit $0800, +1 hug
 *     ($7F1F52);
 *   - otherwise every $78 frames ($0938 counts; 2 seconds) its mood
 *     changes. $7F1F58 holds one mood bit (1, 2, 4, 8 or $10) and the
 *     random byte $DE picks a script for slot $16 (UNK_LoadCCDataShort)
 *     and maybe the next mood:
 *       1:   < $40 script $3D, mood 2;  < $C0: by $DE again, < $33 $3E,
 *            < $66 / < $99 $40, < $CC $47, else $48, mood $10
 *       2:   < $2A $3C mood 1;  < $54 $41 mood 4;  < $7E $49;  < $A8 $44
 *       4:   < $2A $3D mood 2;  < $54 $45 mood 8;  < $7E $43
 *       8:   < $2A $41 mood 4;  < $54 $46
 *       $10: < $40 $3C mood 1
 *     (otherwise it keeps doing what it does);
 *   - called ($7F1F60 bit $10): the counter restarts, the slot gets
 *     flags $28 and script $3F, speed +08 = $20 and the direction +0F
 *     towards the player along the longer axis (1 down, 2 up, 4 right,
 *     8 left, from where the player stands); $7E / $80 = |dx| / |dy|,
 *     $82 / $84 = 1 when dx / dy is negative.
 * Each frame +10 counts one.
 * ───────────────────────────────────────────────────────────────────────── */
#define DOG_SLOT     0x0016
#define DOG_X        0x11F2C
#define DOG_Y        0x11F2E
#define DOG_MOOD     0x11F58
#define DOG_HUGS     0x11F52
#define DOG_TIMER    0x00938

typedef struct Mood { uint16_t below; uint8_t script; uint16_t clear, set, ret; } Mood;
#define ALWAYS 0x100   /* taken without a comparison (BRA) */

static void dog_script(uint8_t *ram, HmRegs *r, uint8_t script, uint16_t ret)
{
  r->a = DOG_SLOT; r->x = 0; r->y = script;
  hm_jsl(ram, r, ret, 0x84);
  hm_UNK_LoadCCDataShort(ram, r);
  hm_rtl(ram, r);
}
static void set_mood(uint8_t *ram, HmRegs *r, uint16_t clear, uint16_t set)
{
  r->a = (uint16_t)((hm_rd16(ram, DOG_MOOD) & ~clear) | set);
  hm_wr16(ram, DOG_MOOD, r->a);
}
/* the first entry whose `below` the random byte is under (C from each
 * CMP); none: nothing happens */
static void choose(uint8_t *ram, HmRegs *r, uint8_t de, const Mood *m, int n)
{
  lda8(r, de);
  for (int i = 0; i < n; i++) {
    if (m[i].below != ALWAYS) {
      r->c = de >= m[i].below;
      if (de >= m[i].below) continue;
    }
    dog_script(ram, r, m[i].script, m[i].ret);
    if (m[i].clear | m[i].set) set_mood(ram, r, m[i].clear, m[i].set);
    return;
  }
}

static void dog_mood(uint8_t *ram, HmRegs *r)
{
  static const Mood MOOD_2[] = {
    { 0x2A, 0x3C, 0x02, 0x01, 0xB125 }, { 0x54, 0x41, 0x02, 0x04, 0xB147 },
    { 0x7E, 0x49, 0, 0, 0xB169 },       { 0xA8, 0x44, 0, 0, 0xB17B } };
  static const Mood MOOD_4[] = {
    { 0x2A, 0x3D, 0x04, 0x02, 0xB1A2 }, { 0x54, 0x45, 0x04, 0x08, 0xB1C4 },
    { 0x7E, 0x43, 0, 0, 0xB1E6 } };
  static const Mood MOOD_8[] = {
    { 0x2A, 0x41, 0x08, 0x04, 0xB209 }, { 0x54, 0x46, 0, 0, 0xB22A } };
  static const Mood MOOD_10[] = { { 0x40, 0x3C, 0x10, 0x01, 0xB248 } };
  static const Mood MOOD_1_PLAY[] = {
    { 0x33, 0x3E, 0x01, 0x10, 0xB0B7 }, { 0x66, 0x40, 0x01, 0x10, 0xB0C8 },
    { 0x99, 0x40, 0x01, 0x10, 0xB0D9 }, { 0xCC, 0x47, 0x01, 0x10, 0xB0EA },
    { ALWAYS, 0x48, 0x01, 0x10, 0xB0FB } };

  uint8_t de = *hm_dp(ram, r, 0xDE);
  uint16_t mood = hm_rd16(ram, DOG_MOOD);
  if ((r->a = mood & 0x0001)) {
    lda8(r, de);
    r->c = de >= 0x40;
    if (de < 0x40) {
      dog_script(ram, r, 0x3D, 0xB06A);
      set_mood(ram, r, 0x01, 0x02);
      return;
    }
    r->c = de >= 0x80;
    if (de >= 0x80) {
      r->c = de >= 0xC0;
      if (de >= 0xC0) return;
    }
    choose(ram, r, de, MOOD_1_PLAY, 5);
  } else if ((r->a = mood & 0x0002)) {
    choose(ram, r, de, MOOD_2, 4);
  } else if ((r->a = mood & 0x0004)) {
    choose(ram, r, de, MOOD_4, 3);
  } else if ((r->a = mood & 0x0008)) {
    choose(ram, r, de, MOOD_8, 2);
  } else if ((r->a = mood & 0x0010)) {
    choose(ram, r, de, MOOD_10, 1);
  }
}

/* the free-hands test of the dog: the chicken's, in a slightly different
 * order and also blocked by game state bit $10 */
static bool player_free_to_hug(uint8_t *ram, HmRegs *r)
{
  static const uint16_t BUSY_ACTIONS[] = { 0x000A, 0x000C, 0x000D, 0x001B };
  if (lda8(r, slot8(ram, r, 0x0C)) == 0) { r->c = 1; return false; }
  for (unsigned i = 0; i < 4; i++) {
    uint16_t action = r->a = hm_dp_r16(ram, r, HM_PLAYER_ACTION);
    r->c = action >= BUSY_ACTIONS[i];
    if (action == BUSY_ACTIONS[i]) return false;
  }
  uint16_t state = hm_dp_r16(ram, r, 0xD2);
  if ((r->a = state & 0x0004)) return false;
  if ((r->a = hm_rd16(ram, FLAGS_60) & 0x0006)) return false;
  r->a = hm_rd16(ram, HM_JOY_NEW);
  if (!(r->a & JOY_A)) return false;
  if (lda8(r, ram[HM_ITEM_HELD])) return false;
  if ((r->a = state & 0x0800)) return false;
  if ((r->a = state & 0x0010)) return false;
  uint16_t action = r->a = hm_dp_r16(ram, r, HM_PLAYER_ACTION);
  r->c = action >= 0x0017;
  if (action == 0x0017) return false;
  r->a = action;
  r->c = action >= 0x0004;
  return action != 0x0004;
}

/* |a - b| into dp `out`; dp `negative` = 1 if a < b */
static uint16_t distance(uint8_t *ram, HmRegs *r, uint16_t a, uint16_t b, unsigned out,
                         unsigned negative)
{
  r->c = a >= b;
  uint16_t d = (uint16_t)(a - b);
  hm_dp_w16(ram, r, out, d);
  r->a = d;
  if (d & 0x8000) {
    d = (uint16_t)(-d);
    hm_dp_w16(ram, r, out, d);
    r->a = 1;
    hm_dp_w16(ram, r, negative, 1);
  }
  return d;
}

static void dog_called(uint8_t *ram, HmRegs *r)
{
  ram[DOG_TIMER] = 0;
  slot_w8(ram, r, 0x01, lda8(r, slot8(ram, r, 0x01) | 0x28));
  r->a = slot8(ram, r, 0x3F);                              /* XBA; LDA #0; XBA */
  r->x = 0; r->y = 0x003F;
  hm_jsl(ram, r, 0xB2AD, 0x84);
  hm_UNK_LoadCCDataShort(ram, r);
  hm_rtl(ram, r);
  hm_dp_w16(ram, r, 0x82, 0);
  hm_dp_w16(ram, r, 0x84, 0);
  uint16_t dx = distance(ram, r, slot16(ram, r, 0x1A), hm_dp_r16(ram, r, 0xD6), 0x7E, 0x82);
  uint16_t dy = distance(ram, r, slot16(ram, r, 0x1C), hm_dp_r16(ram, r, 0xD8), 0x80, 0x84);
  slot_w8(ram, r, 0x08, lda8(r, 0x20));
  r->a = dy;
  r->c = dy >= dx;
  uint8_t direction;
  if (dy >= dx) direction = (r->a = hm_dp_r16(ram, r, 0x84)) ? 0x01 : 0x02;
  else          direction = (r->a = hm_dp_r16(ram, r, 0x82)) ? 0x04 : 0x08;
  slot_w8(ram, r, 0x0F, lda8(r, direction));
}

void hm_Event_UNKDogRelated(uint8_t *ram, HmRegs *r)
{
  uint16_t pc = hm_dp_r16(ram, r, 0xC9);
  r->c = pc == 0xFFFF;
  r->a = (uint16_t)(pc + 1);
  hm_dp_w16(ram, r, 0xC9, r->a);
  r->a = slot16(ram, r, 0x1A); hm_wr16(ram, DOG_X, r->a);
  r->a = slot16(ram, r, 0x1C); hm_wr16(ram, DOG_Y, r->a);

  if (player_free_to_hug(ram, r)) {
    slot_w8(ram, r, 0x00, lda8(r, 0));
    slot_w8(ram, r, 0x01, lda8(r, slot8(ram, r, 0x01) | 0x40));
    r->a = hm_dp_r16(ram, r, 0xD2) | 0x0800;
    hm_dp_w16(ram, r, 0xD2, r->a);
    r->a = (uint16_t)(hm_rd16(ram, DOG_HUGS) + 1);
    hm_wr16(ram, DOG_HUGS, r->a);
  } else {
    uint8_t t = lda8(r, ram[DOG_TIMER]);
    r->c = t >= 0x78;
    if (t != 0x78) {
      ram[DOG_TIMER] = lda8(r, (uint8_t)(t + 1));
    } else {
      ram[DOG_TIMER] = 0;
      dog_mood(ram, r);
    }
  }
  if ((r->a = hm_rd16(ram, FLAGS_60) & 0x0010)) dog_called(ram, r);
  count_frame(ram, r);
}

/* ═════════════════════════════════════════════════════════════════════════
 * The small commands. $C9-$CB points at the command byte; arguments
 * follow it. Each command moves $C9 on with CLC; ADC (A = the new $C9, C
 * its carry), which is what most of them leave behind.
 * ═════════════════════════════════════════════════════════════════════════ */
#define EVENT_FLAG_BITS 0x8494D3   /* ROM: dw 1 << n, Events_Flags_Table */
#define FLAGS_5C        0x11F5C
#define FLAGS_7A        0x11F7A
#define HM_TIME_RUNNING 0x00973    /* 0 stopped, 1 running, 2 skip the day */
#define HM_HOUR         0x11F1C

static void advance(uint8_t *ram, HmRegs *r, uint16_t n)
{
  uint16_t pc = hm_dp_r16(ram, r, 0xC9);
  r->c = (uint32_t)pc + n > 0xFFFF;
  r->a = (uint16_t)(pc + n);
  hm_dp_w16(ram, r, 0xC9, r->a);
}
static void step_back(uint8_t *ram, HmRegs *r)            /* SEC; SBC #1 */
{
  uint16_t pc = hm_dp_r16(ram, r, 0xC9);
  r->c = pc >= 1;
  r->a = (uint16_t)(pc - 1);
  hm_dp_w16(ram, r, 0xC9, r->a);
}
/* SEP #$20; LDA [$C9] */
static uint8_t arg8(uint8_t *ram, HmRegs *r)
{
  return lda8(r, hm_bus_rd8(ram, hm_dp_long(ram, r, 0xC9, 0)));
}
/* LDA #0; XBA; LDA [$C9]: the byte in a clean 16-bit A */
static uint16_t arg8_word(uint8_t *ram, HmRegs *r)
{
  return r->a = hm_bus_rd8(ram, hm_dp_long(ram, r, 0xC9, 0));
}
/* REP #$20; LDA [$C9] */
static uint16_t arg16(uint8_t *ram, HmRegs *r)
{
  return r->a = hm_bus_rd16(ram, hm_dp_long(ram, r, 0xC9, 0));
}
/* jump: $C9 = the word at [$C9] */
static void jump(uint8_t *ram, HmRegs *r)
{
  hm_dp_w16(ram, r, 0xC9, arg16(ram, r));
}
/* LDA #0; XBA; LDA [$C9]; ASL; TAX; LDA Events_Flags_Table,X */
static uint16_t flag_bit_arg(uint8_t *ram, HmRegs *r)
{
  uint16_t n = arg8_word(ram, r);
  r->c = (n >> 15) & 1;
  r->x = (uint16_t)(n << 1);
  return r->a = hm_bus_rd16(ram, EVENT_FLAG_BITS + r->x);
}
/* the 24-bit address argument of the Jumpif commands: $72-$74 */
static void address_arg(uint8_t *ram, HmRegs *r)
{
  hm_dp_w16(ram, r, 0x72, arg16(ram, r));
  advance(ram, r, 2);
  *hm_dp(ram, r, 0x74) = arg8(ram, r);
  advance(ram, r, 1);
}

/* Event_AudioRelated ($84:89BB): $0110 = the argument (a sound request).
 * Two bytes. */
void hm_Event_AudioRelated(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  ram[0x00110] = arg8(ram, r);
  advance(ram, r, 1);
  advance(ram, r, 1);
}

/* Event_TimeRunning ($84:89E3) / Event_TimeStopped ($84:8A0D): the clock
 * runs or stops ($0973), with $7F1F5C bit 0 = stopped and $0196 bit $20
 * = running. */
void hm_Event_TimeRunning(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  ram[HM_TIME_RUNNING] = lda8(r, 1);
  r->a = hm_rd16(ram, FLAGS_5C) & 0xFFFE; hm_wr16(ram, FLAGS_5C, r->a);
  r->a = hm_rd16(ram, 0x00196) | 0x0020;  hm_wr16(ram, 0x00196, r->a);
}
void hm_Event_TimeStopped(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  ram[HM_TIME_RUNNING] = lda8(r, 0);
  r->a = hm_rd16(ram, FLAGS_5C) | 0x0001; hm_wr16(ram, FLAGS_5C, r->a);
  r->a = hm_rd16(ram, 0x00196) & 0xFFDF;  hm_wr16(ram, 0x00196, r->a);
}

/* Event_SetHour ($84:8A39): the hour = the argument. */
void hm_Event_SetHour(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  ram[HM_HOUR] = arg8(ram, r);
  advance(ram, r, 1);
}

/* Event_Empty ($84:8A58): nothing; one byte. */
void hm_Event_Empty(uint8_t *ram, HmRegs *r) { advance(ram, r, 1); }

/* Event_SetPlayerPosition ($84:8A65): X, Y words; also the transition's
 * destination ($017D / $017F). */
void hm_Event_SetPlayerPosition(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  hm_wr16(ram, 0x0017D, arg16(ram, r)); hm_dp_w16(ram, r, 0xD6, r->a);
  advance(ram, r, 2);
  hm_wr16(ram, 0x0017F, arg16(ram, r)); hm_dp_w16(ram, r, 0xD8, r->a);
  advance(ram, r, 2);
}

/* Event_SetTransitionDest ($84:8A94): $098B = the map to go to. */
void hm_Event_SetTransitionDest(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  ram[0x0098B] = arg8(ram, r);
  advance(ram, r, 1);
  count_frame(ram, r);
}

/* Event_SetPlayerDirection ($84:8ABF): facing = the argument (also $0911
 * and the animation request $0901). */
void hm_Event_SetPlayerDirection(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  uint16_t d = arg8_word(ram, r);
  hm_dp_w16(ram, r, 0xDA, d);
  hm_wr16(ram, 0x00911, d);
  hm_wr16(ram, 0x00901, d);
  advance(ram, r, 1);
}

/* Event_UNKChangesGameState ($84:8AE7) / Event_UNKChangesGameState2
 * ($84:8CFD): game state bit $4000 off / on; turning it off also clears
 * $08FD / $08FF. */
void hm_Event_UNKChangesGameState(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  r->a = hm_dp_r16(ram, r, 0xD2) & 0xBFFF;
  hm_dp_w16(ram, r, 0xD2, r->a);
  hm_wr16(ram, 0x008FD, 0);
  hm_wr16(ram, 0x008FF, 0);
}
void hm_Event_UNKChangesGameState2(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  r->a = hm_dp_r16(ram, r, 0xD2) | 0x4000;
  hm_dp_w16(ram, r, 0xD2, r->a);
}

/* Event_NewCCStructure ($84:8B08): start another slot (argument 1) on a
 * script of the same bank (word argument 2, the bank taken from this
 * slot's +32): it is reset (ResetsCurrentCCStructureLong) and +3F = its
 * number; then $CC goes back to this slot. Four bytes. */
void hm_Event_NewCCStructure(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  r->a = hm_dp_r16(ram, r, 0xCC);
  hm_dp_w16(ram, r, 0x72, r->a);
  *hm_dp(ram, r, 0x74) = lda8(r, *hm_dp(ram, r, 0xCE));
  hm_push8(ram, r, lda8(r, slot8(ram, r, 0x32)));                  /* PHA */
  r->a = arg8_word(ram, r);
  hm_jsl(ram, r, 0x8B36, 0x84);
  hm_SetCCPoiner(ram, r);
  hm_rtl(ram, r);
  slot_w8(ram, r, 0x32, lda8(r, hm_pull8(ram, r)));                /* PLA */
  hm_push8(ram, r, arg8(ram, r));
  advance(ram, r, 1);
  slot_w16(ram, r, 0x30, arg16(ram, r));
  hm_push8(ram, r, 0x8B); hm_push8(ram, r, 0x5F);                  /* JSR */
  hm_ResetsCurrentCCStructureLong(ram, r);
  hm_pull8(ram, r); hm_pull8(ram, r);
  slot_w8(ram, r, 0x3F, lda8(r, hm_pull8(ram, r)));
  r->a = hm_dp_r16(ram, r, 0x72);
  hm_dp_w16(ram, r, 0xCC, r->a);
  *hm_dp(ram, r, 0xCE) = lda8(r, *hm_dp(ram, r, 0x74));
  advance(ram, r, 2);
}

/* Event_UNKset7Aflag ($84:8BAC): $7F1F7A |= bit n. Event_UNKCompare-
 * set7Aflag ($84:8BDC): waits until bit n of $7F1F7A is set (otherwise
 * $C9 steps back onto the command and +10 counts a frame). */
void hm_Event_UNKset7Aflag(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  r->a = flag_bit_arg(ram, r) | hm_rd16(ram, FLAGS_7A);
  hm_wr16(ram, FLAGS_7A, r->a);
  advance(ram, r, 1);
}
void hm_Event_UNKCompareset7Aflag(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  r->a = flag_bit_arg(ram, r) & hm_rd16(ram, FLAGS_7A);
  if (r->a) { advance(ram, r, 1); return; }
  step_back(ram, r);
  count_frame(ram, r);
}

/* Event_UNKCCchanges2 ($84:8C22): the slot's +03, +04, the low byte of
 * +10, and +05 = +06 from four argument bytes; +01 |= $20. */
void hm_Event_UNKCCchanges2(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  slot_w8(ram, r, 0x03, arg8(ram, r));
  advance(ram, r, 1);
  slot_w8(ram, r, 0x04, arg8(ram, r));
  advance(ram, r, 1);
  slot_w8(ram, r, 0x10, arg8(ram, r));
  advance(ram, r, 1);
  uint8_t v = arg8(ram, r);
  slot_w8(ram, r, 0x05, v);
  slot_w8(ram, r, 0x06, v);
  slot_w8(ram, r, 0x01, lda8(r, slot8(ram, r, 0x01) | 0x20));
  advance(ram, r, 1);
}

/* Event_UNKDisableCCstruct ($84:8CE2): the slot is no longer in use (and
 * $C9 does not move). */
void hm_Event_UNKDisableCCstruct(uint8_t *ram, HmRegs *r)
{
  slot_w8(ram, r, 0x00, lda8(r, 0));
  count_frame(ram, r);
}

/* Event_JumptoInstruction ($84:8D13): goto the word argument. */
void hm_Event_JumptoInstruction(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  jump(ram, r);
}

/* Event_UNKSetsCC10 ($84:8D24): the slot's +10 = the word argument. */
void hm_Event_UNKSetsCC10(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  slot_w16(ram, r, 0x10, arg16(ram, r));
  advance(ram, r, 2);
}

/* Event_JumpifFlagSet ($84:8D44): address (24-bit), bit number, target:
 * goto target if that bit of the word at the address is set. */
void hm_Event_JumpifFlagSet(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  address_arg(ram, r);
  hm_dp_w16(ram, r, 0x7E, flag_bit_arg(ram, r));
  advance(ram, r, 1);
  r->a = hm_bus_rd16(ram, hm_dp_long(ram, r, 0x72, 0)) & hm_dp_r16(ram, r, 0x7E);
  if (r->a) jump(ram, r);
  else advance(ram, r, 2);
}

/* Event_JumpifEquals ($84:8DA5): address, value, target: goto target if
 * the byte at the address equals the value. */
void hm_Event_JumpifEquals(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  address_arg(ram, r);
  *hm_dp(ram, r, 0x92) = arg8(ram, r);
  advance(ram, r, 1);
  uint8_t v = lda8(r, hm_bus_rd8(ram, hm_dp_long(ram, r, 0x72, 0)));
  uint8_t k = *hm_dp(ram, r, 0x92);
  r->c = v >= k;
  if (v == k) jump(ram, r);
  else advance(ram, r, 2);
}

/* Event_JumpifBetween ($84:8DFB): address, low, high, target: goto target
 * if low <= byte <= high (the high bound + 1 is kept in 8 bits: $FF means
 * nothing is below it). */
void hm_Event_JumpifBetween(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  address_arg(ram, r);
  *hm_dp(ram, r, 0x92) = arg8(ram, r);
  advance(ram, r, 1);
  *hm_dp(ram, r, 0x93) = lda8(r, (uint8_t)(arg8(ram, r) + 1));
  advance(ram, r, 1);
  uint32_t at = hm_dp_long(ram, r, 0x72, 0);
  uint8_t v = lda8(r, hm_bus_rd8(ram, at));
  r->c = v >= *hm_dp(ram, r, 0x92);
  if (v >= *hm_dp(ram, r, 0x92)) {
    v = lda8(r, hm_bus_rd8(ram, at));
    r->c = v >= *hm_dp(ram, r, 0x93);
    if (v < *hm_dp(ram, r, 0x93)) { jump(ram, r); return; }
  }
  advance(ram, r, 2);
}

/* Event_GetRNG ($84:8E68): the slot's +0D = a roll of an n-sided die. */
void hm_Event_GetRNG(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  arg8(ram, r);
  hm_jsl(ram, r, 0x8E7B, 0x80);
  hm_RNGReturn0toA(ram, r);
  hm_rtl(ram, r);
  slot_w8(ram, r, 0x0D, (uint8_t)r->a);
  advance(ram, r, 1);
}

/* Event_JumpIfEqualsRNG ($84:8E90): value, target: goto target if the
 * last roll (+0D) equals the value. */
void hm_Event_JumpIfEqualsRNG(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  hm_dp_w16(ram, r, 0x92, arg16(ram, r));
  uint8_t roll = lda8(r, slot8(ram, r, 0x0D));
  uint8_t k = *hm_dp(ram, r, 0x92);
  r->c = roll >= k;
  if (roll == k) {
    advance(ram, r, 1);
    jump(ram, r);
  } else {
    advance(ram, r, 3);
  }
}

/* Event_UNKSetAnimation ($84:8EC9): $0901 = the word argument (the
 * player's animation), $090F = the byte argument * 64, game state bit
 * $2000. */
void hm_Event_UNKSetAnimation(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  hm_wr16(ram, 0x00901, arg16(ram, r));
  advance(ram, r, 2);
  uint16_t v = arg8_word(ram, r);
  r->c = (v >> 10) & 1;                                  /* the 6th ASL */
  r->a = (uint16_t)(v << 6);
  hm_wr16(ram, 0x0090F, r->a);
  advance(ram, r, 1);
  r->a = hm_dp_r16(ram, r, 0xD2) | 0x2000;
  hm_dp_w16(ram, r, 0xD2, r->a);
}

/* ═════════════════════════════════════════════════════════════════════════
 * More commands: values at any address, picking things up, waits.
 * ═════════════════════════════════════════════════════════════════════════ */
#define WAIT_BITS        0x84B4D2   /* ROM: dw $0400, $0800, $0100, $0200, $8000 */
#define HM_WAIT_FLAGS    0x008FD    /* 16b: what the player may do to go on */
#define HM_WAIT_FRAMES   0x008FF    /* 16b: frames left */
#define FLAGS_60_        0x11F60
#define HM_MONEY_LOW     0x11F04
#define HM_MONEY_HIGH    0x11F06

static uint32_t at72(uint8_t *ram, HmRegs *r, uint16_t y)
{
  return hm_dp_long(ram, r, 0x72, y);
}
/* the word / 24-bit value at [$72] */
static uint16_t value16(uint8_t *ram, HmRegs *r) { return hm_bus_rd16(ram, at72(ram, r, 0)); }

/* ── Jumps on values ─────────────────────────────────────────────────── */

/* Event_JumpifEqualsValue ($84:B666): address, word, target: goto target
 * if the word at the address equals it. */
void hm_Event_JumpifEqualsValue(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  address_arg(ram, r);
  hm_dp_w16(ram, r, 0x7E, arg16(ram, r));
  advance(ram, r, 2);
  uint16_t v = r->a = value16(ram, r);
  r->c = v >= hm_dp_r16(ram, r, 0x7E);
  if (v == hm_dp_r16(ram, r, 0x7E)) jump(ram, r);
  else advance(ram, r, 2);
}

/* Event_JumpifEqualsValueLong ($84:B6BC): the same with a 24-bit value. */
void hm_Event_JumpifEqualsValueLong(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  address_arg(ram, r);
  hm_dp_w16(ram, r, 0x7E, arg16(ram, r));
  advance(ram, r, 2);
  *hm_dp(ram, r, 0x80) = arg8(ram, r);
  advance(ram, r, 1);
  uint16_t low = r->a = value16(ram, r);
  r->c = low >= hm_dp_r16(ram, r, 0x7E);
  if (low == hm_dp_r16(ram, r, 0x7E)) {
    r->y = 2;
    uint8_t high = lda8(r, hm_bus_rd8(ram, at72(ram, r, 2)));
    r->c = high >= *hm_dp(ram, r, 0x80);
    if (high == *hm_dp(ram, r, 0x80)) { jump(ram, r); return; }
  }
  advance(ram, r, 2);
}

/* Event_JumpifBetweenValue ($84:B72D): address, low, high (words),
 * target: goto target if low <= word <= high (high + 1 in 16 bits). */
void hm_Event_JumpifBetweenValue(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  address_arg(ram, r);
  hm_dp_w16(ram, r, 0x7E, arg16(ram, r));
  advance(ram, r, 2);
  r->a = (uint16_t)(arg16(ram, r) + 1);
  hm_dp_w16(ram, r, 0x80, r->a);
  advance(ram, r, 2);
  uint16_t v = r->a = value16(ram, r);
  r->c = v >= hm_dp_r16(ram, r, 0x7E);
  if (v >= hm_dp_r16(ram, r, 0x7E)) {
    v = r->a = value16(ram, r);
    r->c = v >= hm_dp_r16(ram, r, 0x80);
    if (v < hm_dp_r16(ram, r, 0x80)) { jump(ram, r); return; }
  }
  advance(ram, r, 2);
}

/* Event_JumpifBetweenValueLong ($84:B79A): the same with 24-bit values
 * (each compare is CMP on the low word, then SBC on the high byte). */
static bool at_least24(uint8_t *ram, HmRegs *r, unsigned bound)
{
  uint16_t low = r->a = value16(ram, r);
  r->c = low >= hm_dp_r16(ram, r, bound);                  /* CMP */
  r->y = 2;
  uint8_t high = lda8(r, hm_bus_rd8(ram, at72(ram, r, 2)));
  unsigned borrow = !r->c;
  r->c = (unsigned)high >= (unsigned)*hm_dp(ram, r, bound + 2) + borrow;   /* SBC */
  r->a = (uint16_t)((r->a & 0xFF00) | (uint8_t)(high - *hm_dp(ram, r, bound + 2) - borrow));
  return r->c;
}
void hm_Event_JumpifBetweenValueLong(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  address_arg(ram, r);
  hm_dp_w16(ram, r, 0x7E, arg16(ram, r)); advance(ram, r, 2);
  *hm_dp(ram, r, 0x80) = arg8(ram, r);     advance(ram, r, 1);
  hm_dp_w16(ram, r, 0x82, arg16(ram, r)); advance(ram, r, 2);
  *hm_dp(ram, r, 0x84) = arg8(ram, r);     advance(ram, r, 1);
  /* high + 1, 24 bits */
  uint16_t low = hm_dp_r16(ram, r, 0x82);
  r->c = low == 0xFFFF;
  hm_dp_w16(ram, r, 0x82, (uint16_t)(low + 1));
  uint8_t high = *hm_dp(ram, r, 0x84);
  unsigned sum = (unsigned)high + r->c;
  r->c = sum > 0xFF;
  *hm_dp(ram, r, 0x84) = lda8(r, (uint8_t)sum);
  if (at_least24(ram, r, 0x7E) && !at_least24(ram, r, 0x82)) {
    jump(ram, r);
    return;
  }
  advance(ram, r, 2);
}

/* Event_UNKJumpif018F ($84:919D): value, target: goto target if $018F
 * equals the value. */
void hm_Event_UNKJumpif018F(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  *hm_dp(ram, r, 0x92) = arg8(ram, r);
  uint8_t v = lda8(r, ram[0x0018F]);
  r->c = v >= *hm_dp(ram, r, 0x92);
  if (v == *hm_dp(ram, r, 0x92)) {
    advance(ram, r, 1);
    jump(ram, r);
  } else {
    advance(ram, r, 3);
  }
}

/* ── Values ──────────────────────────────────────────────────────────── */

/* Event_SetFlag ($84:9306): address, bit: the word at the address |= the
 * bit. */
void hm_Event_SetFlag(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  address_arg(ram, r);
  hm_dp_w16(ram, r, 0x7E, flag_bit_arg(ram, r));
  r->a = value16(ram, r) | hm_dp_r16(ram, r, 0x7E);
  hm_bus_wr16(ram, at72(ram, r, 0), r->a);
  advance(ram, r, 1);
}

/* Event_StoreValue ($84:A90F): address, word: store it. */
void hm_Event_StoreValue(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  address_arg(ram, r);
  hm_bus_wr16(ram, at72(ram, r, 0), arg16(ram, r));
  advance(ram, r, 2);
}

/* Event_SetValueSort ($84:B84C): address, byte: store it. */
void hm_Event_SetValueSort(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  address_arg(ram, r);
  hm_bus_wr8(ram, at72(ram, r, 0), arg8(ram, r));
  advance(ram, r, 1);
}

/* Event_SetValueLong ($84:B889): address, 24-bit value: store it. */
void hm_Event_SetValueLong(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  address_arg(ram, r);
  hm_bus_wr16(ram, at72(ram, r, 0), arg16(ram, r));
  advance(ram, r, 2);
  uint8_t high = arg8(ram, r);
  r->y = 2;
  hm_bus_wr8(ram, at72(ram, r, 2), high);
  advance(ram, r, 1);
}

/* Event_UNKTextrelated ($84:91D2; not about text): address, signed byte:
 * add it to the byte at the address, kept in 0..255. */
void hm_Event_UNKTextrelated(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  address_arg(ram, r);
  uint8_t d = (uint8_t)arg8_word(ram, r);
  r->a = (uint16_t)(int16_t)(int8_t)d;                     /* BPL; XBA; LDA #$FF; XBA */
  hm_dp_w16(ram, r, 0x7E, r->a);
  advance(ram, r, 1);
  uint16_t v = hm_bus_rd8(ram, at72(ram, r, 0));
  uint32_t sum = (uint32_t)v + hm_dp_r16(ram, r, 0x7E);
  r->c = sum > 0xFFFF;
  r->a = (uint16_t)sum;
  if (r->a & 0x8000) {
    lda8(r, 0);                                            /* below 0 */
  } else {
    r->c = r->a >= 0x00FF;
    if (r->a >= 0x00FF) lda8(r, 0xFF);                     /* above 255 */
  }
  hm_bus_wr8(ram, at72(ram, r, 0), (uint8_t)r->a);
}

/* Event_UNKCCchanges14 ($84:B5A9): address, signed word: add it to the
 * word at the address, kept in 0..999. */
void hm_Event_UNKCCchanges14(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  address_arg(ram, r);
  hm_dp_w16(ram, r, 0x7E, arg16(ram, r));
  advance(ram, r, 2);
  uint32_t sum = (uint32_t)value16(ram, r) + hm_dp_r16(ram, r, 0x7E);
  r->c = sum > 0xFFFF;
  r->a = (uint16_t)sum;
  hm_bus_wr16(ram, at72(ram, r, 0), r->a);
  if (r->a & 0x8000) {
    r->a = 0;
    hm_bus_wr16(ram, at72(ram, r, 0), 0);
    return;
  }
  r->c = r->a >= 999;
  if (r->a >= 999) {
    r->a = 999;
    hm_bus_wr16(ram, at72(ram, r, 0), 999);
  }
}

/* Event_ChangeMoney ($84:B602): two 24-bit arguments; the first is read
 * and then overwritten by the second (quirk: only the second counts),
 * which is added to the money (AddMoney, signed, in tens of G). If the
 * money would go below 0 it becomes 0: the script never "can't pay". */
void hm_Event_ChangeMoney(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  address_arg(ram, r);                                     /* read, then lost */
  address_arg(ram, r);
  hm_jsl(ram, r, 0xB651, 0x83);
  hm_AddMoney(ram, r);
  hm_rtl(ram, r);
  if (r->a != 0) {                                         /* would be negative */
    r->a = 0;
    hm_wr16(ram, HM_MONEY_LOW, 0);
    ram[HM_MONEY_HIGH] = lda8(r, 0);
  }
}

/* ── The slot ────────────────────────────────────────────────────────── */

/* Event_UNKCCchanges4 ($84:8F9B): +16 = word, +14 = byte * 64, +01 |= 4. */
void hm_Event_UNKCCchanges4(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  slot_w16(ram, r, 0x16, arg16(ram, r));
  advance(ram, r, 2);
  uint16_t v = arg8_word(ram, r);
  r->c = (v >> 10) & 1;
  r->a = (uint16_t)(v << 6);
  slot_w16(ram, r, 0x14, r->a);
  advance(ram, r, 1);
  slot_w8(ram, r, 0x01, lda8(r, slot8(ram, r, 0x01) | 0x04));
}

/* start a walk: +36 = word, +07 = +08 = byte, +01 |= flags, and the
 * starting point +1E / +20 = the position +1A / +1C */
static void start_walk(uint8_t *ram, HmRegs *r, uint8_t flags)
{
  slot_w16(ram, r, 0x36, arg16(ram, r));
  advance(ram, r, 2);
  uint8_t v = arg8(ram, r);
  slot_w8(ram, r, 0x07, v);
  slot_w8(ram, r, 0x08, v);
  advance(ram, r, 1);
  slot_w8(ram, r, 0x01, lda8(r, slot8(ram, r, 0x01) | flags));
  r->a = slot16(ram, r, 0x1A); slot_w16(ram, r, 0x1E, r->a);
  r->a = slot16(ram, r, 0x1C); slot_w16(ram, r, 0x20, r->a);
}

/* Event_UNKCCchanges6 ($84:923F): +09, +0A, +0B (= +05 = +06), then a
 * walk with flags $28. */
void hm_Event_UNKCCchanges6(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  slot_w8(ram, r, 0x09, arg8(ram, r));
  advance(ram, r, 1);
  slot_w8(ram, r, 0x0A, arg8(ram, r));
  advance(ram, r, 1);
  uint8_t v = arg8(ram, r);
  slot_w8(ram, r, 0x0B, v); slot_w8(ram, r, 0x05, v); slot_w8(ram, r, 0x06, v);
  advance(ram, r, 1);
  start_walk(ram, r, 0x28);
}

/* Event_UNKCCchanges10 ($84:9777): +0B (= +05 = +06), then a walk with
 * flags $A8. */
void hm_Event_UNKCCchanges10(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  uint8_t v = arg8(ram, r);
  slot_w8(ram, r, 0x0B, v); slot_w8(ram, r, 0x05, v); slot_w8(ram, r, 0x06, v);
  advance(ram, r, 1);
  start_walk(ram, r, 0xA8);
}

/* Event_UNKCCchanges16 ($84:BA51): +00 |= 2. */
void hm_Event_UNKCCchanges16(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  slot_w8(ram, r, 0x00, lda8(r, slot8(ram, r, 0x00) | 0x02));
}

/* ── Waiting for the player ──────────────────────────────────────────── */

/* Event_UNKCCchanges12 ($84:B389) / 13 ($84:B427): a wait. The first
 * frame: $08FD |= the allowed input (table $84:B4D2: entry n; 13 also
 * adds entry 4, $8000), $08FF = frames, game state bit $1000, and the
 * script stays on the command. Then each frame $08FF counts down; at 0
 * everything is cleared, the player goes idle and the script moves on
 * (4 bytes). */
static void wait_frames(uint8_t *ram, HmRegs *r, bool also_entry_4)
{
  if ((r->a = hm_dp_r16(ram, r, 0xD2) & 0x1000)) {
    uint16_t left = r->a = hm_rd16(ram, HM_WAIT_FRAMES);
    if (left) {
      r->a = (uint16_t)(left - 1);
      hm_wr16(ram, HM_WAIT_FRAMES, r->a);
      count_frame(ram, r);
      return;
    }
    hm_wr16(ram, HM_WAIT_FLAGS, 0);
    hm_wr16(ram, HM_WAIT_FRAMES, 0);
    r->a = hm_dp_r16(ram, r, 0xD2) & 0xEFFF;
    hm_dp_w16(ram, r, 0xD2, r->a);
    r->a = 0;
    hm_dp_w16(ram, r, HM_PLAYER_ACTION, 0);
    advance(ram, r, 4);
    return;
  }
  advance(ram, r, 1);
  uint16_t n = arg8_word(ram, r);
  r->c = (n >> 15) & 1;
  r->x = (uint16_t)(n << 1);
  r->a = hm_bus_rd16(ram, WAIT_BITS + r->x) | hm_rd16(ram, HM_WAIT_FLAGS);
  hm_wr16(ram, HM_WAIT_FLAGS, r->a);
  if (also_entry_4) {
    r->x = 8;
    r->a = hm_bus_rd16(ram, WAIT_BITS + 8) | hm_rd16(ram, HM_WAIT_FLAGS);
    hm_wr16(ram, HM_WAIT_FLAGS, r->a);
  }
  advance(ram, r, 1);
  hm_wr16(ram, HM_WAIT_FRAMES, arg16(ram, r));
  uint16_t pc = hm_dp_r16(ram, r, 0xC9);                   /* back onto the command */
  r->c = pc >= 2;                                          /* SEC; SBC #2 */
  r->a = (uint16_t)(pc - 2);
  hm_dp_w16(ram, r, 0xC9, r->a);
  count_frame(ram, r);
  r->a = hm_dp_r16(ram, r, 0xD2) | 0x1000;
  hm_dp_w16(ram, r, 0xD2, r->a);
}
void hm_Event_UNKCCchanges12(uint8_t *ram, HmRegs *r) { wait_frames(ram, r, false); }
void hm_Event_UNKCCchanges13(uint8_t *ram, HmRegs *r) { wait_frames(ram, r, true); }

/* Event_UNK2 ($84:B365): wait until $7F1F5A bit $8000. */
void hm_Event_UNK2(uint8_t *ram, HmRegs *r)
{
  if ((r->a = hm_rd16(ram, FLAGS_5A) & 0x8000)) advance(ram, r, 1);
  else count_frame(ram, r);
}

/* ── Picking things up: the mole, the fish, ... ──────────────────────── */

/* the free-hands tests, in each command's own order */
enum { T_SLOT_0C, T_BUSY, T_STATE, T_FLAGS_60, T_JOY_A, T_ITEM, T_ACTION };
typedef struct Test { uint8_t kind; uint16_t value; } Test;

static bool free_hands(uint8_t *ram, HmRegs *r, const Test *t, int n)
{
  for (int i = 0; i < n; i++) {
    switch (t[i].kind) {
    case T_SLOT_0C:
      if (lda8(r, slot8(ram, r, 0x0C)) == 0) { r->c = 1; return false; }
      break;
    case T_BUSY: {                         /* LDA action; CMP #v, each reloaded */
      static const uint16_t busy[] = { 0x000A, 0x000C, 0x000D, 0x001B };
      for (unsigned k = 0; k < 4; k++) {
        uint16_t action = r->a = hm_dp_r16(ram, r, HM_PLAYER_ACTION);
        r->c = action >= busy[k];
        if (action == busy[k]) return false;
      }
      break;
    }
    case T_STATE:
      if ((r->a = hm_dp_r16(ram, r, 0xD2) & t[i].value)) return false;
      break;
    case T_FLAGS_60:
      if ((r->a = hm_rd16(ram, FLAGS_60_) & 0x0006)) return false;
      break;
    case T_JOY_A:
      r->a = hm_rd16(ram, HM_JOY_NEW);
      if (!(r->a & JOY_A)) return false;
      break;
    case T_ITEM:
      if (lda8(r, ram[HM_ITEM_HELD])) return false;
      break;
    case T_ACTION: {
      uint16_t action = r->a = hm_dp_r16(ram, r, HM_PLAYER_ACTION);
      r->c = action >= t[i].value;
      if (action == t[i].value) return false;
      break;
    }
    }
  }
  return true;
}
/* taken: the slot stops, the player carries `item` (action 4) */
static void carry(uint8_t *ram, HmRegs *r, uint8_t item, bool remember_slot)
{
  if (remember_slot) ram[HM_CARRIED_SLOT] = lda8(r, slot8(ram, r, 0x3F));
  slot_w8(ram, r, 0x00, lda8(r, 0));
  slot_w8(ram, r, 0x01, lda8(r, slot8(ram, r, 0x01) | 0x40));
  count_frame(ram, r);
  ram[HM_ITEM_HELD] = lda8(r, item);
  r->a = 4;
  hm_dp_w16(ram, r, HM_PLAYER_ACTION, 4);
}

static const Test FREE_LONG[] = {
  { T_SLOT_0C, 0 }, { T_BUSY, 0 }, { T_STATE, 0x0004 }, { T_FLAGS_60, 0 }, { T_JOY_A, 0 },
  { T_STATE, 0x0800 }, { T_ACTION, 0x0017 }, { T_ITEM, 0 } };
static const Test FREE_SHORT[] = {
  { T_SLOT_0C, 0 }, { T_BUSY, 0 }, { T_STATE, 0x0004 }, { T_FLAGS_60, 0 }, { T_JOY_A, 0 },
  { T_ITEM, 0 } };

/* Event_UNKPickupMole ($84:A94C): the mole after the hoe finds it. Picked
 * up: item $29. Otherwise every $B4 frames (3 seconds, $093A), half the
 * time ($DE bit 7) it starts script $81 on slot $11. */
void hm_Event_UNKPickupMole(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  if (free_hands(ram, r, FREE_LONG, 8)) { carry(ram, r, 0x29, true); return; }
  uint8_t t = lda8(r, ram[0x0093A]);
  r->c = t >= 0xB4;
  if (t != 0xB4) {
    ram[0x0093A] = lda8(r, (uint8_t)(t + 1));
  } else {
    ram[0x0093A] = 0;
    if (lda8(r, *hm_dp(ram, r, 0xDE) & 0x80)) {
      r->a = 0x0011; r->x = 0; r->y = 0x0081;
      hm_jsl(ram, r, 0xAA4E, 0x84);
      hm_UNK_LoadCCDataShort(ram, r);
      hm_rtl(ram, r);
    }
  }
  count_frame(ram, r);
}

/* Event_UNKFishing ($84:AA5D): picked up: item $07 (a fish). */
void hm_Event_UNKFishing(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  if (free_hands(ram, r, FREE_SHORT, 6)) { carry(ram, r, 0x07, true); return; }
  count_frame(ram, r);
}

/* Event_UNK3 ($84:B93C): picked up: item $08 (the slot is not
 * remembered in $0920). */
void hm_Event_UNK3(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  if (free_hands(ram, r, FREE_LONG, 8)) { carry(ram, r, 0x08, false); return; }
  count_frame(ram, r);
}

/* ── The player, places, flags ───────────────────────────────────────── */

/* Event_SetItemonHand ($84:B556): the player carries the item (action 4).
 * Event_ChangeItemonHand ($84:B4DC): the item, and game state bit 2.
 * Event_DropItemAnimation ($84:B57B): action 5 (put it down). */
void hm_Event_SetItemonHand(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  ram[HM_ITEM_HELD] = arg8(ram, r);
  advance(ram, r, 1);
  r->a = 4;
  hm_dp_w16(ram, r, HM_PLAYER_ACTION, 4);
}
void hm_Event_ChangeItemonHand(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  ram[HM_ITEM_HELD] = arg8(ram, r);
  advance(ram, r, 1);
  r->a = hm_dp_r16(ram, r, 0xD2) | 0x0002;
  hm_dp_w16(ram, r, 0xD2, r->a);
}
void hm_Event_DropItemAnimation(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  r->a = 5;
  hm_dp_w16(ram, r, HM_PLAYER_ACTION, 5);
}

/* Event_TransitiontoX ($84:B52D): go to map n ($098B; $0196 bit $4000
 * starts the transition). Event_TransitiontoHouse ($84:B503): go home
 * (map $15) and $7F1F60 bit $20. */
void hm_Event_TransitiontoX(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  ram[0x0098B] = arg8(ram, r);
  advance(ram, r, 1);
  r->a = hm_rd16(ram, 0x00196) | 0x4000;
  hm_wr16(ram, 0x00196, r->a);
}
void hm_Event_TransitiontoHouse(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  ram[0x0098B] = lda8(r, 0x15);
  r->a = hm_rd16(ram, 0x00196) | 0x4000;
  hm_wr16(ram, 0x00196, r->a);
  r->a = hm_rd16(ram, FLAGS_60_) | 0x0020;
  hm_wr16(ram, FLAGS_60_, r->a);
}

/* Event_MapScrolling ($84:9513): the camera moves by itself: X speed, Y
 * speed (words) for n frames ($087C / $087E / $0880). */
void hm_Event_MapScrolling(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  hm_wr16(ram, 0x0087C, arg16(ram, r));
  advance(ram, r, 2);
  hm_wr16(ram, 0x0087E, arg16(ram, r));
  advance(ram, r, 2);
  ram[0x00880] = arg8(ram, r);
  advance(ram, r, 1);
}

/* flags of $7F1F5C / $7F1F60: Event_UNKSet5C ($84:B58F) / Event_UNKReset5C
 * ($84:BB31) bit 3 of $7F1F5C; Event_UNKSet60 ($84:BB4B) bit 15 of
 * $7F1F60. */
void hm_Event_UNKSet5C(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  r->a = hm_rd16(ram, FLAGS_5C) | 0x0008; hm_wr16(ram, FLAGS_5C, r->a);
}
void hm_Event_UNKReset5C(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  r->a = hm_rd16(ram, FLAGS_5C) & 0xFFF7; hm_wr16(ram, FLAGS_5C, r->a);
}
void hm_Event_UNKSet60(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  r->a = hm_rd16(ram, FLAGS_60_) | 0x8000; hm_wr16(ram, FLAGS_60_, r->a);
}

/* ═════════════════════════════════════════════════════════════════════════
 * Event_UNKCowRelated ($84:9AD4, JSR, RTS). A cow's every frame (one
 * command byte). The cow's record is [$72] (GetsCowPointer, 16 bytes):
 *   +00 status: $02 calf, $04 young, $08 adult, $10 runs away from the
 *       player, $20 sick, $40 pregnant / in labour
 *   +01 today's flags: $01 brushed, $02 milked, $04 talked to, $08 moving,
 *       $80 bred
 *   +02 map, +03 days pregnant ($15 = just bred), +04 happiness,
 *   +05 times hit (up to 10), +08 / +0A position
 * $0953 + 2 n: a timer per cow that counts to $100 between wanderings.
 * The slot's +02 is the cow's colour / kind (0-3): every script has four
 * versions, one per kind.
 * ═════════════════════════════════════════════════════════════════════════ */
#define COW_TIMERS     0x00953   /* 16b per cow */
#define COWS_SPAWN     0x83CA44  /* Table_CowsSpawnPos: a word per cow, 4 bytes apart */
#define HM_COWS        0x11F0A   /* 8b: cows owned */
#define HM_SEASON      0x11F19
#define HM_SALE_PRICE  0x11F15   /* 24b: what the sale pays */
#define FLAGS_64       0x11F64
#define HM_TOOL        0x00921   /* 8b: the item in hand */
#define COW_STATUS_RUNS_AWAY 0x10
#define MAP_BARN       0x27

static void rec_w8(uint8_t *ram, HmRegs *r, uint16_t y, uint8_t v)
{
  r->y = y;
  hm_bus_wr8(ram, hm_dp_long(ram, r, 0x72, y), v);
}
static void rec_w16(uint8_t *ram, HmRegs *r, uint16_t y, uint16_t v)
{
  r->y = y;
  hm_bus_wr16(ram, hm_dp_long(ram, r, 0x72, y), v);
}
static void rec_or(uint8_t *ram, HmRegs *r, uint16_t y, uint8_t bits)
{
  rec_w8(ram, r, y, lda8(r, (uint8_t)(record8(ram, r, y) | bits)));
}
static void rec_and(uint8_t *ram, HmRegs *r, uint16_t y, uint8_t bits)
{
  rec_w8(ram, r, y, lda8(r, (uint8_t)(record8(ram, r, y) & bits)));
}
/* UNK_LoadCCDataShort: script `script` on the cow's own slot */
static void cow_script(uint8_t *ram, HmRegs *r, uint16_t script, uint16_t ret)
{
  r->a = slot8(ram, r, 0x3F);
  r->x = 0;
  r->y = script;
  hm_jsl(ram, r, ret, 0x84);
  hm_UNK_LoadCCDataShort(ram, r);
  hm_rtl(ram, r);
}
/* the slot's +02 picks one of four versions of a script; any other value
 * falls into the first */
static void cow_script4(uint8_t *ram, HmRegs *r, uint16_t first, uint16_t ret)
{
  uint8_t kind = slot8(ram, r, 0x02);
  cow_script(ram, r, (uint16_t)(first + (kind <= 3 ? kind : 0)), ret);
}
static void cow_happier(uint8_t *ram, HmRegs *r, uint16_t n, uint16_t ret)
{
  r->a = n;
  hm_jsl(ram, r, ret, 0x84);
  hm_AddCowHappiness(ram, r);
  hm_rtl(ram, r);
}
/* X = 2 n: the cow's timer */
static uint16_t cow_timer_x(uint8_t *ram, HmRegs *r)
{
  r->x = (uint16_t)(animal_number(ram, r, 0x18) << 1);
  return r->x;
}
/* the timer: true when it reached $100 (and starts again from 0) */
static bool cow_timer_done(uint8_t *ram, HmRegs *r)
{
  uint16_t x = cow_timer_x(ram, r);
  uint16_t t = hm_rd16(ram, COW_TIMERS + x);
  if (t != 0x0100) { hm_wr16(ram, COW_TIMERS + x, (uint16_t)(t + 1)); return false; }
  hm_wr16(ram, COW_TIMERS + x, 0);
  return true;
}
/* adult cows: the script by how it feels */
static uint16_t adult_script(uint8_t *ram, HmRegs *r, uint16_t sick, uint16_t runs, uint16_t normal)
{
  uint8_t st = record8(ram, r, 0);
  r->x = (st & 0x20) ? sick : (st & 0x10) ? runs : normal;
  return r->x;
}

/* the reaction after talking, brushing...: by its age */
static bool reaction(uint8_t *ram, HmRegs *r)   /* true: on to the wandering */
{
  uint8_t st = record8(ram, r, 0);
  if (st & 0x30) return true;                         /* sick or runs away: none */
  if (st & 0x02) { cow_script4(ram, r, 0x64, 0x9D42); return false; }
  if (st & 0x04) { cow_script4(ram, r, 0x60, 0x9DBA); return false; }
  if (st & 0x40) cow_script4(ram, r, 0x82, 0xA0B8);
  else           cow_script4(ram, r, 0x0C, 0xA0B8);
  return true;
}

static bool talk(uint8_t *ram, HmRegs *r)
{
  for (unsigned i = 0; i < 4; i++)                    /* $0889-$088F: what it says */
    hm_wr16(ram, 0x0889 + 2 * i, record8(ram, r, (uint16_t)(0x0C + i)));
  uint8_t st = record8(ram, r, 0);
  uint16_t text = (st & 0x40) ? (record8(ram, r, 3) ? 0x0032 : 0x0145)
                : (st & 0x20) ? 0x0031 : (st & 0x10) ? 0x0030 : 0x002F;
  ram[0x0191] = 0;
  ram[0x019A] = lda8(r, 2);
  r->x = text;
  hm_jsl(ram, r, 0x9C75, 0x83);
  hm_StartTextBox(ram, r);
  hm_rtl(ram, r);
  if (record8(ram, r, 1) & 0x04) return true;         /* already talked to today */
  rec_or(ram, r, 1, 0x04);
  cow_happier(ram, r, 1, 0x9C97);
  return reaction(ram, r);
}

static bool milk(uint8_t *ram, HmRegs *r)
{
  if (!(record8(ram, r, 0) & 0x08) || (record8(ram, r, 1) & 0x02) || (record8(ram, r, 0) & 0x70))
    return true;
  rec_or(ram, r, 1, 0x02);
  uint8_t h = record8(ram, r, 4);
  ram[HM_ITEM_HELD] = h < 0x60 ? 0x15 : h < 0xC0 ? 0x16 : 0x17;   /* milk S / M / L */
  hm_dp_w16(ram, r, HM_PLAYER_ACTION, 4);
  cow_script4(ram, r, 0x0C, 0xA0B8);
  return true;
}

static bool brush(uint8_t *ram, HmRegs *r)
{
  if (record8(ram, r, 1) & 0x01) return true;
  rec_or(ram, r, 1, 0x01);
  cow_happier(ram, r, 3, 0x9ECF);
  return reaction(ram, r);
}

static bool hit(uint8_t *ram, HmRegs *r)            /* a tool hits it */
{
  uint8_t st = record8(ram, r, 0);
  uint16_t script = (st & 0x08) ? 0x0A : (st & 0x04) ? 0x68 : (st & 0x02) ? 0x6A : 0x0A;
  uint8_t kind = slot8(ram, r, 0x02);
  if (kind != 0 && kind != 2) script++;
  hm_dp_w16(ram, r, 0x7E, script);
  if (record8(ram, r, 0) & 0x70) return true;
  cow_script(ram, r, script, 0x9FA7);
  uint8_t hits = record8(ram, r, 5);
  if (hits != 0x0A) rec_w8(ram, r, 5, (uint8_t)(hits + 1));
  return true;
}

static bool breed(uint8_t *ram, HmRegs *r)          /* the miracle potion worked */
{
  if (hm_rd16(ram, FLAGS_64) & 0x0004) return true;
  if (ram[HM_COWS] == 0x0C) return true;
  hm_wr16(ram, FLAGS_64, (uint16_t)(hm_rd16(ram, FLAGS_64) | 0x0004));
  rec_and(ram, r, 0, 0xCF);
  rec_or(ram, r, 1, 0x80);
  cow_happier(ram, r, 10, 0x9E92);
  rec_w8(ram, r, 3, 0x15);
  ram[0x11F12] = 0x15;
  ram[HM_COWS]++;
  cow_script4(ram, r, 0x0C, 0xA0B8);
  return true;
}

static bool cured(uint8_t *ram, HmRegs *r)          /* the medicine worked */
{
  rec_and(ram, r, 0, 0xDF);
  rec_w8(ram, r, 3, 0);
  cow_script4(ram, r, 0x0C, 0xA0B8);
  return true;
}

static bool use_tool(uint8_t *ram, HmRegs *r)
{
  uint8_t t = ram[HM_TOOL];
  if (t == 0) return true;
  if (t < 0x05 || (t >= 0x11 && t < 0x15)) return hit(ram, r);
  if (t == 0x0E) return milk(ram, r);
  if (t == 0x0F) return brush(ram, r);
  return true;
}

/* 849B45: the player and the cow. true: go on to the wandering (84A0B9),
 * false: straight to the moving part (84A46E). */
static bool interact(uint8_t *ram, HmRegs *r)
{
  static const uint16_t BUSY[] = { 0x000A, 0x000C, 0x000D, 0x001B };
  if (slot8(ram, r, 0x0C) == 0) return true;
  uint16_t action = hm_dp_r16(ram, r, HM_PLAYER_ACTION);
  for (unsigned i = 0; i < 4; i++) if (action == BUSY[i]) return true;
  uint16_t state = hm_dp_r16(ram, r, 0xD2);
  if ((state & 0x0004) || (state & 0x0040) || (hm_rd16(ram, FLAGS_60) & 0x0006)) return true;
  if (hm_rd16(ram, HM_JOY_NEW) & JOY_A) return talk(ram, r);
  if (record8(ram, r, 0) & 0x08) {
    if (hm_rd16(ram, FLAGS_5A) & 0x0100) return breed(ram, r);
    if (hm_rd16(ram, FLAGS_5A) & 0x0080) return cured(ram, r);
  }
  if (hm_rd16(ram, FLAGS_5A) & 0x0040) return use_tool(ram, r);
  return true;
}

/* 84A0B9: every $100 frames the cow starts a wandering script, by age
 * and the dice ($DE); the moving flag (+01 $08) is set for the first. */
static void wander(uint8_t *ram, HmRegs *r)
{
  static const uint16_t CALF[4] = { 0x78, 0x79, 0x7A, 0x7B }, YOUNG[4] = { 0x74, 0x76, 0x77, 0x75 };
  static const uint16_t RET_CALF[4] = { 0xA144, 0xA16F, 0xA19A, 0xA1C5 };
  static const uint16_t RET_YOUNG[4] = { 0xA231, 0xA25C, 0xA287, 0xA2B2 };
  uint8_t st = record8(ram, r, 0);
  if (st & 0x08) {
    if (!cow_timer_done(ram, r) || (record8(ram, r, 0) & 0x40)) return;
    static const struct { uint8_t below; uint16_t sick, runs, normal, ret; } ADULT[5] = {
      { 0x33, 0x6C, 0x70, 0x5E, 0xA34E }, { 0x66, 0x6D, 0x71, 0x51, 0xA395 },
      { 0x99, 0x6E, 0x72, 0x53, 0xA3DC }, { 0xCC, 0x6F, 0x73, 0x54, 0xA423 },
      { 0x00, 0x6D, 0x71, 0x55, 0xA46A },
    };
    uint8_t dice = *hm_dp(ram, r, 0xDE);
    unsigned k = 0;
    while (k < 4 && dice >= ADULT[k].below) k++;
    if (k == 0) rec_or(ram, r, 1, 0x08); else rec_and(ram, r, 1, 0xF7);
    uint16_t s = adult_script(ram, r, ADULT[k].sick, ADULT[k].runs, ADULT[k].normal);
    cow_script(ram, r, s, ADULT[k].ret);
    return;
  }
  const uint16_t *ids, *rets;
  if (st & 0x04)      { ids = YOUNG; rets = RET_YOUNG; }
  else if (st & 0x02) { ids = CALF; rets = RET_CALF; }
  else return;
  if (!cow_timer_done(ram, r)) return;
  uint8_t dice = *hm_dp(ram, r, 0xDE);
  unsigned k = dice < 0x40 ? 0 : dice < 0x80 ? 1 : dice < 0xC0 ? 2 : 3;
  if (k == 0) rec_or(ram, r, 1, 0x08); else rec_and(ram, r, 1, 0xF7);
  cow_script(ram, r, ids[k], rets[k]);
}

/* 84A607 / 84A740: start moving (once: +01 $08), its timer back to 0 */
static void start_moving(uint8_t *ram, HmRegs *r, const uint16_t ret[3])
{
  if (record8(ram, r, 1) & 0x08) return;
  rec_or(ram, r, 1, 0x08);
  hm_wr16(ram, COW_TIMERS + cow_timer_x(ram, r), 0);
  uint8_t st = record8(ram, r, 0);
  uint16_t script, at;
  if (st & 0x08) {
    if (st & 0x40) return;
    script = adult_script(ram, r, 0x6C, 0x70, 0x5E); at = ret[2];
  } else if (st & 0x04) { script = 0x74; at = ret[1]; }
  else if (st & 0x02)   { script = 0x78; at = ret[0]; }
  else return;
  slot_w8(ram, r, 0x01, lda8(r, slot8(ram, r, 0x01) | 0x28));
  cow_script(ram, r, script, at);
}

/* 84A84A: face the player (or away from it, status $10) */
static void face_player(uint8_t *ram, HmRegs *r)
{
  hm_dp_w16(ram, r, 0x82, 0);
  hm_dp_w16(ram, r, 0x84, 0);
  uint16_t dx = distance(ram, r, slot16(ram, r, 0x1A), hm_dp_r16(ram, r, 0xD6), 0x7E, 0x82);
  uint16_t dy = distance(ram, r, slot16(ram, r, 0x1C), hm_dp_r16(ram, r, 0xD8), 0x80, 0x84);
  slot_w8(ram, r, 0x08, lda8(r, 0x20));
  bool away = record8(ram, r, 0) & COW_STATUS_RUNS_AWAY;
  uint8_t direction;
  if (dy >= dx) direction = (hm_dp_r16(ram, r, 0x84) != 0) != away ? 0x01 : 0x02;
  else          direction = (hm_dp_r16(ram, r, 0x82) != 0) != away ? 0x04 : 0x08;
  slot_w8(ram, r, 0x0F, lda8(r, direction));
}

void hm_Event_UNKCowRelated(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  animal_number(ram, r, 0x18);
  hm_jsl(ram, r, 0x9AF5, 0x83);
  hm_GetsCowPointer(ram, r);
  hm_rtl(ram, r);
  rec_w16(ram, r, 0x08, slot16(ram, r, 0x1A));       /* the record follows the slot */
  rec_w16(ram, r, 0x0A, slot16(ram, r, 0x1C));

  uint8_t map = *hm_dp(ram, r, 0x22);
  uint8_t state = slot8(ram, r, 0x0E);
  if (map == MAP_BARN && state == 0xC2) {             /* 84A595: back in the barn */
    slot_w8(ram, r, 0x00, 0);
    slot_w8(ram, r, 0x01, lda8(r, slot8(ram, r, 0x01) | 0x40));
    rec_w8(ram, r, 0x02, ram[HM_SEASON]);
    rec_w16(ram, r, 0x08, 0x0148);
    rec_w16(ram, r, 0x0A, 0x0178);
  } else if (map < 0x04 && state == 0xE0 && (hm_rd16(ram, FLAGS_5A) & 0x0004)) {
    /* 84A498: sold (the price by happiness: 500, 800 or 1000) */
    hm_wr16(ram, FLAGS_5A, (uint16_t)(hm_rd16(ram, FLAGS_5A) & 0xFFFB));
    uint8_t h = record8(ram, r, 4);
    hm_wr16(ram, HM_SALE_PRICE, h < 0x60 ? 0x01F4 : h < 0xC0 ? 0x0320 : 0x03E8);
    ram[HM_SALE_PRICE + 2] = 0;
    ram[0x09A0] = slot8(ram, r, 0x3F);
    ram[0x096E] = 0x43; ram[0x096F] = 0; ram[0x0970] = 0;   /* its dialog */
    hm_dp_w16(ram, r, 0xD2, (uint16_t)(hm_dp_r16(ram, r, 0xD2) | 0x0040));
    hm_dp_w16(ram, r, HM_PLAYER_ACTION, 0);
    slot_w8(ram, r, 0x0E, 0);
    slot_w8(ram, r, 0x01, lda8(r, slot8(ram, r, 0x01) & 0x57));
  } else if (map < 0x04 && state == 0xE1) {           /* 84A540: off the farm, to the barn */
    slot_w8(ram, r, 0x00, 0);
    slot_w8(ram, r, 0x01, lda8(r, slot8(ram, r, 0x01) | 0x40));
    r->x = (uint16_t)(animal_number(ram, r, 0x18) << 2);
    rec_w8(ram, r, 0x02, MAP_BARN);
    /* the same word for both coordinates (no INX in between) */
    rec_w16(ram, r, 0x08, hm_bus_rd16(ram, COWS_SPAWN + r->x));
    rec_w16(ram, r, 0x0A, hm_bus_rd16(ram, COWS_SPAWN + r->x));
  } else if (interact(ram, r)) {
    wander(ram, r);
  }

  /* 84A46E: moving */
  static const uint16_t RET_MOVE[3] = { 0xA688, 0xA6BA, 0xA70D };
  static const uint16_t RET_CALLED[3] = { 0xA7C1, 0xA7F3, 0xA849 };
  bool called = hm_rd16(ram, FLAGS_5A) & 0x0010;      /* the bell */
  if (!called && slot8(ram, r, 0x0C) != 0) {
    start_moving(ram, r, RET_MOVE);
    uint16_t dir = hm_dp_r16(ram, r, 0xDA);           /* the way the player looks */
    static const uint8_t FACE[4] = { 0x01, 0x02, 0x04, 0x08 };
    if (dir < 4) slot_w8(ram, r, 0x0F, lda8(r, FACE[dir]));
    else called = true;
  }
  if (called) {
    start_moving(ram, r, RET_CALLED);
    face_player(ram, r);
  }
  count_frame(ram, r);
}

/* ═════════════════════════════════════════════════════════════════════════
 * Commands 50-59 ($84:BBDA-$84:C013). HM-Decomp labels only the first
 * (Event_UNKChickenRelated2); the others are named by their number here.
 * ═════════════════════════════════════════════════════════════════════════ */
#define CHICKEN_PLACES  0x83CA10   /* 4 bytes per chicken: x, y in the coop */
#define FLAG_BITS_84    0x84C014   /* DATA16_84C014: dw 1 << n */

static uint32_t dbw(const HmRegs *r, uint16_t a) { return (uint32_t)r->db << 16 | a; }
static void push16b(uint8_t *ram, HmRegs *r, uint16_t v)
{
  hm_push8(ram, r, (uint8_t)(v >> 8));
  hm_push8(ram, r, (uint8_t)v);
}
static uint16_t pull16b(uint8_t *ram, HmRegs *r)
{
  uint16_t lo = hm_pull8(ram, r);
  return (uint16_t)(lo | hm_pull8(ram, r) << 8);
}
/* LDA #0; CLC; ADC $DA; STA $0901: the player's standing animation */
static void stand(uint8_t *ram, HmRegs *r)
{
  uint32_t s = hm_dp_r16(ram, r, 0xDA);
  r->c = s > 0xFFFF;
  r->a = (uint16_t)s;
  hm_bus_wr16(ram, dbw(r, 0x0901), r->a);
}
/* a carried chicken (slot $24 + n) goes back to its place in the coop */
static void chicken_home(uint8_t *ram, HmRegs *r, uint16_t ret)
{
  uint8_t slot = hm_bus_rd8(ram, dbw(r, 0x0920));
  uint16_t s = (uint16_t)(slot - 0x24);
  r->c = slot >= 0x24;
  uint16_t n = (uint8_t)s;
  r->a = n;
  push16b(ram, r, n);
  hm_jsl(ram, r, ret, 0x83);
  hm_GetChickenPointer(ram, r);
  hm_rtl(ram, r);
  r->y = 1;
  lda8(r, 0x28);
  hm_bus_wr8(ram, hm_dp_long(ram, r, 0x72, 1), 0x28);
  r->y = 0;
  uint8_t f = (uint8_t)(hm_bus_rd8(ram, hm_dp_long(ram, r, 0x72, 0)) & 0xDF);   /* not carried */
  lda8(r, f);
  hm_bus_wr8(ram, hm_dp_long(ram, r, 0x72, 0), f);
  n = pull16b(ram, r);
  r->c = (n >> 14) & 1;                                    /* ASL; ASL */
  r->x = (uint16_t)(n << 2);
  uint32_t x = (uint32_t)hm_bus_rd16(ram, CHICKEN_PLACES + r->x) + 0x10;
  r->c = x > 0xFFFF;
  r->y = 4;
  r->a = (uint16_t)x;
  hm_bus_wr16(ram, hm_dp_long(ram, r, 0x72, 4), r->a);
  r->x = (uint16_t)(r->x + 2);
  r->y = 6;
  r->a = hm_bus_rd16(ram, CHICKEN_PLACES + r->x);
  hm_bus_wr16(ram, hm_dp_long(ram, r, 0x72, 6), r->a);
  lda8(r, (uint8_t)r->a);
  hm_bus_wr8(ram, dbw(r, HM_ITEM_HELD & 0xFFFF), 0);
}
/* the carried dog goes back to the farm (the map of this season) */
static bool dog_home(uint8_t *ram, HmRegs *r)
{
  r->a = hm_dp_r16(ram, r, 0xD2) & 0x0800;
  if (!r->a) return false;
  r->a = (uint16_t)(0x0800 ^ 0xFFFF) & hm_dp_r16(ram, r, 0xD2);
  hm_dp_w16(ram, r, 0xD2, r->a);
  uint8_t season = lda8(r, hm_bus_rd8(ram, 0x7F1F19));
  hm_bus_wr8(ram, 0x7F1F30, season);
  r->a = 0x0078; hm_bus_wr16(ram, 0x7F1F2C, 0x0078);
  r->a = 0x01A8; hm_bus_wr16(ram, 0x7F1F2E, 0x01A8);
  return true;
}

/* Command 50, Event_UNKChickenRelated2 ($84:BBDA): put down what is carried
 * when an event starts: a chicken goes back to the coop, any other item
 * disappears, the dog goes back to the farm; the player stands. */
void hm_Event_UNKChickenRelated2(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  uint8_t item = lda8(r, hm_bus_rd8(ram, dbw(r, HM_ITEM_HELD & 0xFFFF)));
  if (item) {
    r->c = item >= 0x25;
    if (item != 0x25) r->c = item >= 0x26;
    if (item == 0x25 || item == 0x26) chicken_home(ram, r, 0xBC19);
    else hm_bus_wr8(ram, dbw(r, HM_ITEM_HELD & 0xFFFF), 0);
    stand(ram, r);
    return;
  }
  if (dog_home(ram, r)) stand(ram, r);
}
/* Command 51 ($84:BC9C): the same without the animation, and only item
 * $29 disappears (other items stay in the hands). */
void hm_Event_Cmd51(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  uint8_t item = lda8(r, hm_bus_rd8(ram, dbw(r, HM_ITEM_HELD & 0xFFFF)));
  if (item) {
    r->c = item >= 0x25;
    if (item != 0x25) r->c = item >= 0x26;
    if (item == 0x25 || item == 0x26) { chicken_home(ram, r, 0xBCD7); return; }
    r->c = item >= 0x29;
    if (item == 0x29) hm_bus_wr8(ram, dbw(r, HM_ITEM_HELD & 0xFFFF), 0);
    return;
  }
  dog_home(ram, r);
}
/* Commands 52 / 53 ($84:BD44 / $84:BE0D): if the player pressed A on this
 * object (CBBBB set its +$0C) with free hands and not busy, the object
 * goes away (+0 = 0, +1 bit 6) and $7F1F60 bit 1 (53: bit 2) is set.
 * Either way the slot's frame count +$10 goes up. */
static void pressed_on(uint8_t *ram, HmRegs *r, uint16_t flag)
{
  advance(ram, r, 1);
  bool ok = lda8(r, slot8(ram, r, 0x0C)) != 0;
  r->c = 1;                                                /* CMP #0 */
  static const uint16_t busy[] = { 0x0A, 0x0C, 0x0D, 0x1B };
  for (unsigned i = 0; ok && i < 4; i++) {
    r->a = hm_dp_r16(ram, r, 0xD4);
    r->c = r->a >= busy[i];
    if (r->a == busy[i]) ok = false;
  }
  if (ok) { r->a = hm_dp_r16(ram, r, 0xD2) & 0x0004; ok = !r->a; }
  if (ok) { r->a = hm_bus_rd16(ram, 0x7F1F60) & 0x0006; ok = !r->a; }
  if (ok) { r->a = hm_bus_rd16(ram, dbw(r, 0x0128)); ok = (r->a & 0x0080) != 0; }
  if (ok) { r->a = hm_dp_r16(ram, r, 0xD2) & 0x0800; ok = !r->a; }
  if (ok) { ok = lda8(r, hm_bus_rd8(ram, dbw(r, HM_ITEM_HELD & 0xFFFF))) == 0; }
  if (!ok) { count_frame(ram, r); return; }
  lda8(r, 0);
  slot_w8(ram, r, 0x00, 0);
  uint8_t v = (uint8_t)(slot8(ram, r, 0x01) | 0x40);
  lda8(r, v);
  slot_w8(ram, r, 0x01, v);
  count_frame(ram, r);
  r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F60) | flag);
  hm_bus_wr16(ram, 0x7F1F60, r->a);
}
void hm_Event_Cmd52(uint8_t *ram, HmRegs *r) { pressed_on(ram, r, 0x0002); }
void hm_Event_Cmd53(uint8_t *ram, HmRegs *r) { pressed_on(ram, r, 0x0004); }

/* Command 54 ($84:BED6): <text word> <lines byte>: a text box that closes
 * by itself (input mode 6, CODE_84CED2: after 120 frames). */
void hm_Event_Cmd54(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  r->x = arg16(ram, r);
  advance(ram, r, 2);
  uint8_t lines = arg8(ram, r);
  hm_bus_wr8(ram, dbw(r, 0x0191), lines);
  hm_bus_wr16(ram, dbw(r, 0x09B1), 0);
  lda8(r, 6);
  hm_bus_wr8(ram, dbw(r, 0x019A), 6);
  hm_jsl(ram, r, 0xBF07, 0x83);
  hm_StartTextBox(ram, r);
  hm_rtl(ram, r);
  advance(ram, r, 1);
  count_frame(ram, r);
}
/* Command 55 ($84:BF20): <address 24> <bit byte> <target word>: if that bit
 * of the word at the address is clear, jump to the target; else go on. */
void hm_Event_Cmd55(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  address_arg(ram, r);
  uint16_t n = arg8_word(ram, r);
  r->c = (n >> 15) & 1;
  r->x = (uint16_t)(n << 1);
  r->a = hm_bus_rd16(ram, FLAG_BITS_84 + r->x);
  hm_dp_w16(ram, r, 0x7E, r->a);
  advance(ram, r, 1);
  r->a = hm_bus_rd16(ram, hm_dp_long(ram, r, 0x72, 0)) & hm_dp_r16(ram, r, 0x7E);
  if (!r->a) { jump(ram, r); return; }
  advance(ram, r, 2);
}
/* Command 56 ($84:BF81): use the tool in hand, if any. */
void hm_Event_Cmd56(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  if (!lda8(r, hm_bus_rd8(ram, dbw(r, HM_TOOL & 0xFFFF)))) return;
  r->a = 0x000A;
  hm_dp_w16(ram, r, 0xD4, 0x000A);
  hm_jsl(ram, r, 0xBF9E, 0x82);
  hm_UNK_ToolUsed(ram, r);
  hm_rtl(ram, r);
}
/* Command 57 ($84:BFA0): <byte>: change the stamina by it (ChangeStamina). */
void hm_Event_Cmd57(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  r->n = arg8(ram, r) >> 7;                                /* ChangeStamina reads N */
  hm_jsl(ram, r, 0xBFB3, 0x81);
  hm_ChangeStamina(ram, r);
  hm_rtl(ram, r);
  advance(ram, r, 1);
}
/* Command 58 ($84:BFBF): <tile byte> <x word> <y word>: set that tile type
 * on the farm (EditTileonMapandsets0181). */
void hm_Event_Cmd58(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  hm_push8(ram, r, arg8(ram, r));
  advance(ram, r, 1);
  push16b(ram, r, arg16(ram, r));
  advance(ram, r, 2);
  push16b(ram, r, arg16(ram, r));
  advance(ram, r, 2);
  r->y = pull16b(ram, r);
  r->x = pull16b(ram, r);
  lda8(r, hm_pull8(ram, r));
  hm_jsl(ram, r, 0xBFFE, 0x82);
  hm_EditTileonMapandsets0181(ram, r);
  hm_rtl(ram, r);
}
/* Command 59 ($84:C000): the player's action $1C. */
void hm_Event_Cmd59(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  r->a = 0x001C;
  hm_dp_w16(ram, r, 0xD4, 0x001C);
}

/* ═════════════════════════════════════════════════════════════════════════
 * Event_UNKPlayerPos ($84:AB27, command 37), every frame, for an object of
 * group $17 that the player can carry (probably the child: putting it down
 * starts script $17/$14). $7F1F5C holds its requests: bit 4 "the player
 * touches it", 6 face the way of +2, 10 walk towards the player, 11 / 5
 * see below. $0939 counts 120 frames between idle animations, chosen by
 * the random byte $DE. With $7F1F32 = $15 the idle and face scripts differ.
 * ═════════════════════════════════════════════════════════════════════════ */
static void group17(uint8_t *ram, HmRegs *r, uint16_t script, uint16_t ret)
{
  r->a = 0x0017; r->x = 0; r->y = script;
  hm_jsl(ram, r, ret, 0x84);
  hm_UNK_LoadCCDataShort(ram, r);
  hm_rtl(ram, r);
}
static void request(uint8_t *ram, HmRegs *r, uint16_t and_mask, uint16_t or_mask)
{
  r->a = (uint16_t)((hm_bus_rd16(ram, 0x7F1F5C) & and_mask) | or_mask);
  hm_bus_wr16(ram, 0x7F1F5C, r->a);
}
static bool asked(uint8_t *ram, HmRegs *r, uint16_t bit)
{
  r->a = hm_bus_rd16(ram, 0x7F1F5C) & bit;
  return r->a != 0;
}
/* LDA $0939; CMP #$78; ... : true when the 120 frames are up (reset) */
static bool idle_time(uint8_t *ram, HmRegs *r)
{
  uint8_t t = lda8(r, hm_bus_rd8(ram, dbw(r, 0x0939)));
  r->c = t >= 0x78;
  if (t != 0x78) {
    lda8(r, (uint8_t)(t + 1));
    hm_bus_wr8(ram, dbw(r, 0x0939), (uint8_t)(t + 1));
    return false;
  }
  hm_bus_wr8(ram, dbw(r, 0x0939), 0);
  return true;
}
/* walk towards the player, along the longer axis (as the dog does) */
static void towards_player(uint8_t *ram, HmRegs *r, uint16_t script, uint16_t ret)
{
  hm_bus_wr8(ram, dbw(r, 0x0939), 0);
  uint8_t f = (uint8_t)(slot8(ram, r, 0x01) | 0x28);
  lda8(r, f);
  slot_w8(ram, r, 0x01, f);
  r->a = slot8(ram, r, 0x3F);                              /* XBA; LDA #0; XBA */
  r->x = 0; r->y = script;
  hm_jsl(ram, r, ret, 0x84);
  hm_UNK_LoadCCDataShort(ram, r);
  hm_rtl(ram, r);
  hm_dp_w16(ram, r, 0x82, 0);
  hm_dp_w16(ram, r, 0x84, 0);
  static const struct { uint16_t y; unsigned pos, out, neg; } A[2] = {
    { 0x1A, 0xD6, 0x7E, 0x82 }, { 0x1C, 0xD8, 0x80, 0x84 } };
  for (unsigned k = 0; k < 2; k++) {
    uint16_t v = slot16(ram, r, A[k].y);
    uint16_t p = hm_dp_r16(ram, r, A[k].pos);
    r->c = v >= p;
    uint16_t d = (uint16_t)(v - p);
    r->a = d;
    hm_dp_w16(ram, r, A[k].out, d);
    if (d & 0x8000) {
      r->a = (uint16_t)(~d + 1);
      hm_dp_w16(ram, r, A[k].out, r->a);
      r->a = 1;
      hm_dp_w16(ram, r, A[k].neg, 1);
    }
  }
  lda8(r, 0x20);
  slot_w8(ram, r, 0x08, 0x20);                             /* speed */
  r->a = hm_dp_r16(ram, r, 0x80);
  r->c = r->a >= hm_dp_r16(ram, r, 0x7E);
  uint8_t dir;
  if (r->c) { r->a = hm_dp_r16(ram, r, 0x84); dir = r->a ? 1 : 2; }
  else { r->a = hm_dp_r16(ram, r, 0x82); dir = r->a ? 4 : 8; }
  lda8(r, dir);
  slot_w8(ram, r, 0x0F, dir);
}

void hm_Event_UNKPlayerPos(uint8_t *ram, HmRegs *r)
{
  advance(ram, r, 1);
  request(ram, r, 0xFFEF, 0);                              /* not touched (yet) */
  uint8_t place = lda8(r, hm_bus_rd8(ram, 0x7F1F32));
  r->c = place >= 0x15;
  if (place != 0x15) {
    if (idle_time(ram, r)) group17(ram, r, 0x0080, 0xAB6F);
    goto end;
  }
  if (asked(ram, r, 0x0040)) {                             /* face the way of +2 */
    static const uint16_t face[4] = { 0x28, 0x29, 0x2A, 0x2B };
    uint8_t w = lda8(r, slot8(ram, r, 0x02));
    uint16_t script = 0x28;
    for (uint8_t k = 0; k < 4; k++) {
      r->c = w >= k;
      if (w == k) { script = face[k]; break; }
    }
    group17(ram, r, script, 0xABD1);
    request(ram, r, 0xFFBF, 0);
  }
  if (!asked(ram, r, 0x0800)) {
    if (asked(ram, r, 0x0020)) {                           /* taken in the arms */
      r->a = (uint16_t)(hm_dp_r16(ram, r, 0xD2) | 0x0010);
      hm_dp_w16(ram, r, 0xD2, r->a);
      request(ram, r, 0xFFDF, 0);
      lda8(r, 0);
      slot_w8(ram, r, 0x00, 0);
      uint8_t f = (uint8_t)(slot8(ram, r, 0x01) | 0x40);
      lda8(r, f);
      slot_w8(ram, r, 0x01, f);
      lda8(r, 0);
      slot_w8(ram, r, 0x0C, 0);
      count_frame(ram, r);
      return;
    }
    /* pressed A on it? (the same tests as picking up an animal) */
    bool ok = lda8(r, slot8(ram, r, 0x0C)) != 0;
    r->c = 1;
    static const uint16_t busy[] = { 0x0A, 0x0C, 0x0D, 0x1B };
    for (unsigned i = 0; ok && i < 4; i++) {
      r->a = hm_dp_r16(ram, r, 0xD4);
      r->c = r->a >= busy[i];
      if (r->a == busy[i]) ok = false;
    }
    if (ok) { r->a = hm_dp_r16(ram, r, 0xD2) & 0x0004; ok = !r->a; }
    if (ok) { r->a = hm_bus_rd16(ram, 0x7F1F60) & 0x0006; ok = !r->a; }
    if (ok) {
      request(ram, r, 0xFFFF, 0x0010);                     /* touched */
      r->a = hm_bus_rd16(ram, dbw(r, 0x0128));
      ok = (r->a & 0x0080) != 0;
    }
    if (ok) {                                              /* room in front? */
      hm_dp_w16(ram, r, 0xE3, r->a = 0x000D);
      hm_dp_w16(ram, r, 0xDF, hm_dp_r16(ram, r, 0xD6));
      hm_dp_w16(ram, r, 0xE1, hm_dp_r16(ram, r, 0xD8));
      hm_dp_w16(ram, r, 0xE5, 0);
      hm_dp_w16(ram, r, 0xE7, 0);
      r->a = hm_bus_rd16(ram, dbw(r, 0x0913));
      hm_jsl(ram, r, 0xAC84, 0x83);
      hm_CEEEE(ram, r);
      hm_rtl(ram, r);
      ok = r->a == 0;
      for (unsigned at = 0xE9; ok && at <= 0xEB; at += 2) {
        uint16_t v = hm_dp_r16(ram, r, at);
        r->a = (uint16_t)(v - 0xC0);
        r->c = r->a >= 0x10;
        ok = r->c;
      }
    }
    if (ok) {
      hm_dp_w16(ram, r, 0xE3, r->a = 0x0020);
      r->a = hm_bus_rd16(ram, dbw(r, 0x0913));
      hm_jsl(ram, r, 0xACB5, 0x83);
      hm_CBBBB(ram, r);
      hm_rtl(ram, r);
      r->c = 1;                                            /* CMP #0 */
      ok = r->a == 0;
    }
    if (ok) {
      if ((r->a = hm_dp_r16(ram, r, 0xD2) & 0x0002)) goto end;
      if ((r->a = hm_dp_r16(ram, r, 0xD2) & 0x0800)) goto end;
      r->a = hm_dp_r16(ram, r, 0xD4);
      r->c = r->a >= 4;
      if (r->a == 4) goto end;
      r->a = 0x0017;
      hm_dp_w16(ram, r, 0xD4, 0x0017);                     /* pick it up */
      goto end;
    }
  }
  /* idle: every 120 frames an animation chosen by $DE */
  request(ram, r, 0xF7FF, 0);
  if (idle_time(ram, r)) {
    static const struct { uint8_t below; uint16_t script, ret; } I[] = {
      { 0x24, 0x4B, 0xAD3C }, { 0x48, 0x4B, 0xAD3C }, { 0x6C, 0x4D, 0xAD4E },
      { 0x90, 0x4E, 0xAD60 }, { 0xB4, 0x4F, 0xAD72 }, { 0xD8, 0x4F, 0xAD72 },
    };
    uint8_t de = lda8(r, *hm_dp(ram, r, 0xDE));
    unsigned i = 0;
    for (; i < 6; i++) {
      r->c = de >= I[i].below;
      if (!r->c) break;
    }
    if (i < 6) group17(ram, r, I[i].script, I[i].ret);
    else group17(ram, r, 0x50, 0xAD84);
  }
end:
  if (asked(ram, r, 0x0400)) {                             /* walk to the player */
    uint8_t p = lda8(r, hm_bus_rd8(ram, 0x7F1F32));
    r->c = p >= 0x15;
    if (p == 0x15) towards_player(ram, r, 0x4C, 0xAE32);
    else towards_player(ram, r, 0x4A, 0xAE66);
  }
  count_frame(ram, r);
}
