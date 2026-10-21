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
