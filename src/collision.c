/*
 * collision.c — can something move there? The checks the player (and the
 * objects) make before taking a step.
 *
 * Inputs in the direct page: $DF / $E1 the position (x, y) of who moves,
 * $E3 how far ahead to look, $E5 / $E7 the step (x, y) and A the direction:
 * 0 down (y grows), 1 up, 2 right (x grows), 3 left. (HM-Decomp's button
 * constants swap left and right: on the SNES pad $0100 is Right, $0200
 * Left, so its PlayerActionLeft is really "right".)
 *
 *   CEEEE  the map: tests the two corners of the side that moves (6 pixels
 *          each way from the middle) against the tile properties.
 *   CFFFF  one corner: is that point of the tile solid?
 *   CBBBB  the objects of the event slots: is one of them in the way?
 *   CDDDD  the player: is the player in the way?
 *
 * They answer A = 0 (free) or not 0 (blocked) and, for the map, how far one
 * can still go.
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

#define GAME_STATE   0xD2
#define PLAYER_X     0xD6
#define PLAYER_Y     0xD8
#define SLOTS        0xB586   /* the event slots, $7E:B586, 49 of $40 bytes */
#define FLAGS_60     0x7F1F60

static uint16_t dp16(uint8_t *ram, const HmRegs *r, unsigned a) { return hm_dp_r16(ram, r, a); }
static void set16(uint8_t *ram, const HmRegs *r, unsigned a, uint16_t v) { hm_dp_w16(ram, r, a, v); }
static void push16(uint8_t *ram, HmRegs *r, uint16_t v)
{
  hm_push8(ram, r, (uint8_t)(v >> 8));
  hm_push8(ram, r, (uint8_t)v);
}
static uint16_t pull16(uint8_t *ram, HmRegs *r)
{
  uint16_t lo = hm_pull8(ram, r);
  return (uint16_t)(lo | hm_pull8(ram, r) << 8);
}
/* ADC / SBC with the carry, 16 bits */
static uint16_t adc(HmRegs *r, uint16_t a, uint16_t b)
{
  uint32_t s = (uint32_t)a + b + r->c;
  r->c = s > 0xFFFF;
  return (uint16_t)s;
}
static uint16_t sbc(HmRegs *r, uint16_t a, uint16_t b)
{
  uint32_t s = (uint32_t)a - b - (r->c ? 0 : 1);
  r->c = s <= 0xFFFF;
  return (uint16_t)s;
}
static uint16_t add(HmRegs *r, uint16_t a, uint16_t b) { r->c = 0; return adc(r, a, b); }
static uint16_t sub(HmRegs *r, uint16_t a, uint16_t b) { r->c = 1; return sbc(r, a, b); }

/* ─────────────────────────────────────────────────────────────────────────
 * CFFFF ($83:B0F6, JSR, RTS). X, Y = a point; A = 0 for the first corner,
 * 1 for the second. Reads the tile's property (UNK_CheckTileProperty):
 *   bit 0       the whole tile is solid;
 *   bits 1 / 2  only the half with x & 8 clear / set is solid;
 *   bits 3 / 4  only the half with y & 8 clear / set is solid;
 *   none        free, except that carrying something ($D2 bit 4) the tile
 *               types $C0-$CF block.
 * Returns A = 1 blocked, 0 free. Leaves for CEEEE: $86 / $88 the point,
 * $8A / $8C the tile's corner, $8E / $90 = 16 / 0 (8 when a half blocks and
 * the step on that axis is 0); the tile type in $E9 (first corner) or $EB
 * (second).
 * ───────────────────────────────────────────────────────────────────────── */
void hm_CFFFF(uint8_t *ram, HmRegs *r)
{
  uint16_t which = r->a;
  push16(ram, r, which);                                   /* PHA */
  set16(ram, r, 0x86, r->x);
  set16(ram, r, 0x88, r->y);
  set16(ram, r, 0x8A, r->x & 0xFFF0);
  set16(ram, r, 0x8C, r->y & 0xFFF0);
  set16(ram, r, 0x8E, 0x0010);
  set16(ram, r, 0x90, 0x0000);
  r->a = 0x0002;
  hm_jsl(ram, r, 0xB119, 0x82);
  hm_UNK_CheckTileProperty(ram, r);
  hm_rtl(ram, r);
  uint8_t prop = (uint8_t)r->a;
  *hm_dp(ram, r, 0x92) = prop;
  r->a = pull16(ram, r);
  r->c = r->a >= 1;                                        /* CMP #1 */
  set16(ram, r, r->a == 1 ? 0xEB : 0xE9, r->x);

  bool blocked;
  if (prop & 0x01) {
    blocked = true;
  } else if (prop & 0x02) {
    if (dp16(ram, r, 0xE7) == 0) set16(ram, r, 0x8E, 8);
    blocked = !(dp16(ram, r, 0x86) & 8);
  } else if (prop & 0x04) {
    if (dp16(ram, r, 0xE7) == 0) set16(ram, r, 0x90, 8);
    blocked = (dp16(ram, r, 0x86) & 8) != 0;
  } else if (prop & 0x08) {
    if (dp16(ram, r, 0xE5) == 0) set16(ram, r, 0x8E, 8);
    blocked = !(dp16(ram, r, 0x88) & 8);
  } else if (prop & 0x10) {
    if (dp16(ram, r, 0xE5) == 0) set16(ram, r, 0x90, 8);
    blocked = (dp16(ram, r, 0x88) & 8) != 0;
  } else {
    blocked = false;
    if (dp16(ram, r, GAME_STATE) & 0x0010) {               /* carrying */
      r->c = r->x >= 0xC0;
      if (r->c) {
        r->c = r->x >= 0xD0;
        blocked = !r->c;
      }
    }
  }
  r->a = blocked ? 1 : 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * CEEEE ($83:AF37, JSL, RTL): the map. A = direction. The two corners are
 * the front side's ends, 6 pixels to each side; the front is $E3 + the step
 * + 6 pixels ahead. A = (first blocked) << 1 | (second blocked), and when
 * blocked, Y (for y moves) or X (for x moves) = how far into the tile the
 * point is, so the caller can slide or stop at the edge.
 * ───────────────────────────────────────────────────────────────────────── */
typedef struct Corner { uint16_t ret1, ret2; } Corner;

static void corner(uint8_t *ram, HmRegs *r, uint16_t which, uint16_t ret)
{
  r->a = which;
  hm_push8(ram, r, (uint8_t)(ret >> 8));                   /* JSR CFFFF */
  hm_push8(ram, r, (uint8_t)ret);
  hm_CFFFF(ram, r);
  hm_pull8(ram, r); hm_pull8(ram, r);
}
/* the two calls: the first corner at (x1, y1); then the second, after
 * keeping the first answer and the margin `keep` ($8E or $90) of the first
 * call on the stack. `along_y`: the first point's y was pushed (y moves),
 * otherwise its x. Returns the code; `kept` = the margin of the first call. */
static uint16_t two_corners(uint8_t *ram, HmRegs *r, bool along_y, uint16_t first, unsigned keep,
                            uint16_t second, const Corner *c, uint16_t *kept)
{
  push16(ram, r, first);                                   /* PHA */
  if (along_y) r->y = first; else r->x = first;            /* TAY / TAX */
  corner(ram, r, 0, c->ret1);
  uint16_t p = pull16(ram, r);                             /* PLY / PLX */
  if (along_y) r->y = p; else r->x = p;
  push16(ram, r, r->a);                                    /* PHA: the first answer */
  push16(ram, r, dp16(ram, r, keep));                      /* LDA $8E / $90; PHA */
  r->a = second;
  if (along_y) r->x = second; else r->y = second;
  corner(ram, r, 1, c->ret2);
  set16(ram, r, 0x7E, r->a);
  *kept = pull16(ram, r);                                  /* PLY / PLX */
  if (along_y) r->y = *kept; else r->x = *kept;
  uint16_t first_answer = pull16(ram, r);                  /* PLA */
  r->a = (uint16_t)((uint16_t)(first_answer << 1) | dp16(ram, r, 0x7E));
  r->c = (first_answer & 0x8000) != 0;                     /* ASL */
  set16(ram, r, 0x7E, r->a);
  return r->a;
}

void hm_CEEEE(uint8_t *ram, HmRegs *r)
{
  static const Corner C[4] = { { 0xAF5E, 0xAF72 }, { 0xAFC6, 0xAFDA },
                               { 0xB02E, 0xB042 }, { 0xB094, 0xB0A8 } };
  uint16_t dir = r->a;
  r->c = (dir & 0x8000) != 0;                              /* ASL; TAX */
  r->x = (uint16_t)(dir << 1);
  uint16_t x = dp16(ram, r, 0xDF), y = dp16(ram, r, 0xE1);
  uint16_t e3 = dp16(ram, r, 0xE3), e5 = dp16(ram, r, 0xE5), e7 = dp16(ram, r, 0xE7);
  uint16_t code, kept;
  switch (dir) {
  case 0:                                                  /* y grows */
    r->x = sub(r, x, 6);
    { uint16_t f = adc(r, adc(r, add(r, y, e7), 6), e3);
      code = two_corners(ram, r, true, f, 0x90, add(r, x, 6), &C[0], &kept); }
    if (!code) { set16(ram, r, 0x7E, 0); r->y = 0; break; }
    r->c = code >= 3;
    if (code != 3) { r->c = code >= 2; if (code == 2) set16(ram, r, 0x90, r->y); }
    r->a = sbc(r, sub(r, dp16(ram, r, 0x88), dp16(ram, r, 0x8C)), dp16(ram, r, 0x90));
    r->y = (uint16_t)(r->a + 1);
    break;
  case 1:                                                  /* y shrinks */
    r->x = sub(r, x, 6);
    { uint16_t f = sbc(r, sbc(r, sub(r, y, e7), 6), e3);
      code = two_corners(ram, r, true, f, 0x8E, add(r, x, 6), &C[1], &kept); }
    if (!code) { set16(ram, r, 0x7E, 0); r->y = 0; break; }
    r->c = code >= 3;
    if (code != 3) { r->c = code >= 2; if (code == 2) set16(ram, r, 0x8E, r->y); }
    r->a = sub(r, add(r, dp16(ram, r, 0x8C), dp16(ram, r, 0x8E)), dp16(ram, r, 0x88));
    r->y = r->a;
    break;
  case 2:                                                  /* x grows */
    { uint16_t f = adc(r, adc(r, add(r, x, e5), 6), e3);
      r->y = sub(r, y, 6);                                 /* (PHA; TAX in two_corners) */
      code = two_corners(ram, r, false, f, 0x90, add(r, y, 6), &C[2], &kept); }
    if (!code) { set16(ram, r, 0x7E, 0); r->x = 0; break; }
    r->c = code >= 3;
    if (code != 3) { r->c = code >= 2; if (code == 2) set16(ram, r, 0x90, r->x); }
    r->a = sbc(r, sub(r, dp16(ram, r, 0x86), dp16(ram, r, 0x8A)), dp16(ram, r, 0x90));
    r->x = (uint16_t)(r->a + 1);
    break;
  default:                                                 /* 3: x shrinks */
    { uint16_t f = sbc(r, sbc(r, sub(r, x, e5), 6), e3);
      r->y = sub(r, y, 6);
      code = two_corners(ram, r, false, f, 0x8E, add(r, y, 6), &C[3], &kept); }
    if (!code) { set16(ram, r, 0x7E, 0); r->x = 0; break; }
    r->c = code >= 3;
    if (code != 3) { r->c = code >= 2; if (code == 2) set16(ram, r, 0x8E, r->x); }
    r->a = sub(r, add(r, dp16(ram, r, 0x8A), dp16(ram, r, 0x8E)), dp16(ram, r, 0x86));
    r->x = r->a;
    break;
  }
  r->a = dp16(ram, r, 0x7E);
}

/* ─────────────────────────────────────────────────────────────────────────
 * CBBBB ($83:AD91, JSL, RTL): the event objects. A = direction. The point
 * looked at is 12 pixels plus the step and $E3 ahead; an object is in the
 * way when its position (+$1A, +$1C of its slot) is within 12 pixels of it
 * on both axes. Every slot's byte +$0C is cleared, and set to 1 on the
 * first objects found (until one without bit 1 of its byte +0, which stops
 * the search: objects with that bit do not block). With $7F1F60 bit $4000
 * nothing blocks.
 * Returns A = 1, X = Y = 0 if blocked; else A = 0, X = $E5, Y = $E7.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_CBBBB(uint8_t *ram, HmRegs *r)
{
  uint16_t dir = r->a;
  r->x = dir;                                              /* TAX */
  r->a = hm_bus_rd16(ram, FLAGS_60) & 0x4000;
  if (!r->a) {
    r->c = (dir & 0x8000) != 0;                            /* TXA; ASL; TAX */
    r->x = (uint16_t)(dir << 1);
    uint16_t x = dp16(ram, r, 0xDF), y = dp16(ram, r, 0xE1);
    uint16_t e3 = dp16(ram, r, 0xE3), e5 = dp16(ram, r, 0xE5), e7 = dp16(ram, r, 0xE7);
    uint16_t px, py;
    switch (dir) {
    case 0: px = add(r, x, 0x0C); py = adc(r, adc(r, add(r, y, e7), e3), 0x0C); break;
    case 1: px = add(r, x, 0x0C); py = add(r, sbc(r, sub(r, y, e7), e3), 0x0C); break;
    case 2: px = adc(r, adc(r, add(r, x, e5), e3), 0x0C); py = add(r, y, 0x0C); break;
    default: px = add(r, sbc(r, sub(r, x, e5), e3), 0x0C); py = add(r, y, 0x0C); break;
    }
    set16(ram, r, 0x80, px);
    set16(ram, r, 0x82, py);

    uint16_t saved_cc = dp16(ram, r, 0xCC);
    uint8_t saved_ce = *hm_dp(ram, r, 0xCE);
    push16(ram, r, saved_cc);
    hm_push8(ram, r, saved_ce);
    set16(ram, r, 0xCC, SLOTS);
    *hm_dp(ram, r, 0xCE) = 0x7E;
    set16(ram, r, 0x8C, 0);
    for (uint16_t i = 0; i < 0x31; i++) {
      hm_bus_wr8(ram, hm_dp_long(ram, r, 0xCC, 0x0C), 0);
      uint8_t used = hm_bus_rd8(ram, hm_dp_long(ram, r, 0xCC, 0));
      if (used) {
        r->a = sub(r, px, hm_bus_rd16(ram, hm_dp_long(ram, r, 0xCC, 0x1A)));
        r->c = r->a >= 0x19;
        if (!r->c) {
          r->a = sub(r, py, hm_bus_rd16(ram, hm_dp_long(ram, r, 0xCC, 0x1C)));
          r->c = r->a >= 0x19;
          if (!r->c && dp16(ram, r, 0x8C) == 0) {
            hm_bus_wr8(ram, hm_dp_long(ram, r, 0xCC, 0x0C), 1);
            uint8_t b = hm_bus_rd8(ram, hm_dp_long(ram, r, 0xCC, 0));
            if (!(b & 2)) set16(ram, r, 0x8C, 1);
          }
        }
      }
      set16(ram, r, 0xCC, add(r, dp16(ram, r, 0xCC), 0x40));
      r->c = (uint16_t)(i + 1) >= 0x31;                    /* INX; CPX #$31 */
    }
    bool hit = dp16(ram, r, 0x8C) != 0;
    *hm_dp(ram, r, 0xCE) = hm_pull8(ram, r);
    set16(ram, r, 0xCC, pull16(ram, r));
    if (hit) {
      r->x = 0; r->y = 0; r->a = 1;
      return;
    }
  }
  r->x = dp16(ram, r, 0xE5);
  r->y = dp16(ram, r, 0xE7);
  r->a = 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * CDDDD ($83:AEC3, JSL, RTL): the player. A = direction. The point is the
 * position plus the step (no $E3 here); the player is in the way when
 * within 12 pixels of it on both axes.
 * Returns A = 1, X = Y = 0 if so; else A = 0, X = $E5, Y = $E7.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_CDDDD(uint8_t *ram, HmRegs *r)
{
  uint16_t dir = r->a;
  r->c = (dir & 0x8000) != 0;
  r->x = (uint16_t)(dir << 1);
  uint16_t x = dp16(ram, r, 0xDF), y = dp16(ram, r, 0xE1);
  uint16_t e5 = dp16(ram, r, 0xE5), e7 = dp16(ram, r, 0xE7);
  uint16_t px = x, py = y;
  switch (dir) {
  case 0: py = add(r, y, e7); break;
  case 1: py = sub(r, y, e7); break;
  case 2: px = add(r, x, e5); break;
  default: px = sub(r, x, e5); break;
  }
  set16(ram, r, 0x80, px);
  set16(ram, r, 0x82, py);
  r->a = add(r, sub(r, px, dp16(ram, r, PLAYER_X)), 0x0C);
  r->c = r->a >= 0x19;
  if (!r->c) {
    r->a = add(r, sub(r, py, dp16(ram, r, PLAYER_Y)), 0x0C);
    r->c = r->a >= 0x19;
    if (!r->c) {
      r->a = 1; r->x = 0; r->y = 0;
      return;
    }
  }
  r->a = 0;
  r->x = dp16(ram, r, 0xE5);
  r->y = dp16(ram, r, 0xE7);
}
