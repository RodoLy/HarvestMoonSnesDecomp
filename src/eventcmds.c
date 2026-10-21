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
