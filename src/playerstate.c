/*
 * playerstate.c — the player each frame (HM-Decomp's SUB_81BFB7 block).
 *
 * SUB_81BFB7 ($81:BFB7, JSL, RTL) runs once a frame. With $D2 bit 0 clear
 * it only does the end of the frame; with $D2 bit 6 an action runs instead
 * (PlayerInteractionSelector, actions.c); otherwise it jumps (table
 * DATA16_81C027) by the player's action ($D4) to its step:
 *
 *   0, 1, 2, 6, 7, $0E  on foot (PlayerState_00: walking, running, ...)
 *   3 jumping, 4 picking up, 5 throwing, 8 eating, 9 an idle animation,
 *   $0A using a tool, $0B, $0C (the watch), $0D, $0F-$1B other animations
 *   ($1C, swapping the tools, waits frames and stays in the original).
 *
 * Most steps set the player's animation ($0901, often + his facing) and
 * play it (CODE_81CFE6); when it ends ($0915 = $FFFF) the action changes.
 * They all end in the frame's end (ResetIdleAnimatioTimer, $81:C002):
 * standing still (action 0) goes to the standing step CODE_81C556; the
 * others clear the idle timer ($0925) and then SUB_809F61 (the camera),
 * $1E = 0 and the facing back from $0911.
 *
 * CODE_81CB77 moves the player $1E pixels in his facing: the event objects
 * (CBBBB) and the map (CEEEE) can stop him or make him slide round a
 * corner; then the camera follows, and every 8 steps a footprint.
 *
 * As everywhere, the bodies keep A, X, Y and C as the original leaves them.
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

#define GAME_STATE    0xD2       /* DP */
#define ACTION        0xD4       /* DP: the player's action */
#define FACING        0xDA       /* DP */
#define STEP_SIZE     0x1E       /* DP: pixels to move this frame */
#define ANIMATION     0x0901     /* DB: the player's animation */
#define ANIM_TIMER    0x0915     /* DB: $FFFF when it is over */
#define IDLE_TIMER    0x0925     /* DB */

typedef void Fn(uint8_t *ram, HmRegs *r);

static uint32_t db(const HmRegs *r, uint16_t a) { return (uint32_t)r->db << 16 | a; }
static uint8_t rd8(uint8_t *ram, const HmRegs *r, uint16_t a) { return hm_bus_rd8(ram, db(r, a)); }
static uint16_t rd16(uint8_t *ram, const HmRegs *r, uint16_t a) { return hm_bus_rd16(ram, db(r, a)); }
static void wr8(uint8_t *ram, const HmRegs *r, uint16_t a, uint8_t v) { hm_bus_wr8(ram, db(r, a), v); }
static void wr16(uint8_t *ram, const HmRegs *r, uint16_t a, uint16_t v) { hm_bus_wr16(ram, db(r, a), v); }
static void set_a8(HmRegs *r, uint8_t v) { r->a = (uint16_t)((r->a & 0xFF00) | v); }
static uint16_t dp16(uint8_t *ram, const HmRegs *r, unsigned a) { return hm_dp_r16(ram, r, a); }
static void set_dp16(uint8_t *ram, const HmRegs *r, unsigned a, uint16_t v) { hm_dp_w16(ram, r, a, v); }

static void jsl(uint8_t *ram, HmRegs *r, uint16_t at, uint8_t bank, Fn *fn)
{
  hm_jsl(ram, r, (uint16_t)(at + 3), bank);
  fn(ram, r);
  hm_rtl(ram, r);
}
static void jsr(uint8_t *ram, HmRegs *r, uint16_t at, Fn *fn)
{
  uint16_t ret = (uint16_t)(at + 2);
  hm_push8(ram, r, (uint8_t)(ret >> 8));
  hm_push8(ram, r, (uint8_t)ret);
  fn(ram, r);
  hm_pull8(ram, r);
  hm_pull8(ram, r);
}

/* JSR CODE_81CFE6 at a: play the player's animation */
static void animate(uint8_t *ram, HmRegs *r, uint16_t at) { jsr(ram, r, at, hm_CODE_81CFE6); }
/* LDA $0915; CMP #$FFFF: is the animation over? */
static bool anim_over(uint8_t *ram, HmRegs *r)
{
  r->a = rd16(ram, r, ANIM_TIMER);
  r->c = r->a >= 0xFFFF;
  return r->a == 0xFFFF;
}
/* $0901 = base + facing (CLC; ADC $DA) */
static void animation_facing(uint8_t *ram, HmRegs *r, uint16_t base)
{
  uint32_t sum = (uint32_t)base + dp16(ram, r, FACING);
  r->c = sum > 0xFFFF;
  r->a = (uint16_t)sum;
  wr16(ram, r, ANIMATION, r->a);
}
static void animation(uint8_t *ram, HmRegs *r, uint16_t v)
{
  r->a = v;
  wr16(ram, r, ANIMATION, v);
}
static void set_action(uint8_t *ram, HmRegs *r, uint16_t a)
{
  r->a = a;
  set_dp16(ram, r, ACTION, a);
}
static void state_off(uint8_t *ram, HmRegs *r, uint16_t bits)
{
  r->a = (uint16_t)(dp16(ram, r, GAME_STATE) & ~bits);
  set_dp16(ram, r, GAME_STATE, r->a);
}
static void state_on(uint8_t *ram, HmRegs *r, uint16_t bits)
{
  r->a = (uint16_t)(dp16(ram, r, GAME_STATE) | bits);
  set_dp16(ram, r, GAME_STATE, r->a);
}
/* LDA $D2; AND #$8000; BEQ: the step's first frame? (then $D2 bit 15 is set) */
static bool first_frame(uint8_t *ram, HmRegs *r)
{
  r->a = (uint16_t)(dp16(ram, r, GAME_STATE) & 0x8000);
  if (r->a) return false;
  state_on(ram, r, 0x8000);
  return true;
}
static void long_or(uint8_t *ram, HmRegs *r, uint32_t at, uint16_t bits)
{
  r->a = (uint16_t)(hm_bus_rd16(ram, at) | bits);
  hm_bus_wr16(ram, at, r->a);
}
static void set_step_size(uint8_t *ram, HmRegs *r, uint8_t n)
{
  set_a8(r, n);
  *hm_dp(ram, r, STEP_SIZE) = n;
}
/* sound effect e of bank 6 (UNK_Audio19) */
static void sound(uint8_t *ram, HmRegs *r, uint8_t e, uint16_t at)
{
  set_a8(r, e);
  wr8(ram, r, 0x0114, e);
  set_a8(r, 0x06);
  wr8(ram, r, 0x0115, 0x06);
  jsl(ram, r, at, 0x83, hm_UNK_Audio19);
}
static void event_long(uint8_t *ram, HmRegs *r, uint16_t a, uint16_t x, uint16_t y, uint16_t at)
{
  r->a = a; r->x = x; r->y = y;
  jsl(ram, r, at, 0x84, hm_UNK_LoadCCDataLong);
}

/* ─────────────────────────────────────────────────────────────────────────
 * CODE_81CB77 ($81:CB77, JSR, RTS): move the player. Plays his animation,
 * then by his facing (0 down, 1 up, 2 right, 3 left): unless he is
 * jumping (actions 3, $17, $18) or walked by a script ($D2 bit 7), the
 * event objects ahead (CBBBB with the step in $E7 / $E5) or a solid tile
 * (CEEEE) stop him ($1E = 0). When only one of the front corners is
 * blocked he slides sideways ($1E = 1, facing the free side) if the tile
 * allows it (types 0 and $A0-$AF always; otherwise by which half of the
 * tile he is in); when the front is blocked, $0878 / $087A = the tiles hit
 * and, unless one is a door ($C0-$CF), the step is cut to the edge. Then
 * the move, the camera (SUB_809EBC and the scroll of that direction), and
 * every 8 moves a footprint (CODE_81A58F; $0907 / $0909 = where).
 * Leftovers: those of the last routine called and the stores after it.
 * ───────────────────────────────────────────────────────────────────────── */
typedef struct Dir {
  uint16_t at_objects, at_map;     /* JSL CBBBB, JSL CEEEE */
  uint16_t at_camera, at_scroll;   /* JSL SUB_809EBC, the scroll */
  Fn *scroll;
  bool y_axis;                     /* moves on Y ($D8; step in $E7, Y = depth) */
  bool minus;                      /* towards smaller coordinates */
  uint8_t slide1, slide2;          /* the facing to slide to per corner */
} Dir;

static const Dir k_dirs[4] = {
  { 0xCBE7, 0xCBF7, 0xCC37, 0xCC3B, hm_UpdateBGOffset,        true,  false, 3, 2 },
  { 0xCC8B, 0xCC9B, 0xCCDB, 0xCCDF, hm_SUB_80A0E1,            true,  true,  3, 2 },
  { 0xCD2F, 0xCD3F, 0xCD7F, 0xCD83, hm_UNK_StaticMapScroling, false, false, 1, 0 },
  { 0xCDD3, 0xCDE3, 0xCE23, 0xCE27, hm_SUB_80A152,            false, true,  1, 0 },
};

static void stop_moving(uint8_t *ram, HmRegs *r)               /* $81:CE53 */
{
  set_dp16(ram, r, STEP_SIZE, 0);
}

/* $81:CE2B: one more step; every 8, a footprint */
static void count_step(uint8_t *ram, HmRegs *r)
{
  uint8_t n = (uint8_t)(rd8(ram, r, 0x0919) + 1);
  wr8(ram, r, 0x0919, n);
  set_a8(r, n);
  r->c = n >= 8;
  if (n != 8) return;
  jsl(ram, r, 0xCE3D, 0x81, hm_CODE_81A58F);
  r->a = dp16(ram, r, 0xD6);
  wr16(ram, r, 0x0907, r->a);
  r->a = dp16(ram, r, 0xD8);
  wr16(ram, r, 0x0909, r->a);
  wr8(ram, r, 0x0919, 0);
}

void hm_CODE_81CB77(uint8_t *ram, HmRegs *r)
{
  animate(ram, r, 0xCB77);
  uint16_t f = dp16(ram, r, FACING);
  r->a = f;
  int d = 0;
  for (int k = 0; k < 4; k++) {
    r->c = f >= k;
    if (f == k) { d = k; break; }
  }
  /* the corner slides can turn him; the original would loop for ever if
   * the turns never ended (the guard only stops a run that never would) */
  for (int guard = 0; guard < 4096; guard++) {
    const Dir *dir = &k_dirs[d];
    bool free_move = false;
    static const uint16_t jumps[] = { 0x0003, 0x0017, 0x0018 };
    for (int i = 0; i < 3 && !free_move; i++) {
      r->a = dp16(ram, r, ACTION);
      r->c = r->a >= jumps[i];
      free_move = r->a == jumps[i];
    }
    if (!free_move) {
      r->a = (uint16_t)(dp16(ram, r, GAME_STATE) & 0x0080);
      free_move = r->a != 0;
    }
    if (!free_move) {
      r->a = dp16(ram, r, 0xD6); set_dp16(ram, r, 0xDF, r->a);
      r->a = dp16(ram, r, 0xD8); set_dp16(ram, r, 0xE1, r->a);
      if (dir->y_axis) {
        r->a = 0; set_dp16(ram, r, 0xE5, 0);
        r->a = dp16(ram, r, STEP_SIZE); set_dp16(ram, r, 0xE7, r->a);
      } else {
        r->a = dp16(ram, r, STEP_SIZE); set_dp16(ram, r, 0xE5, r->a);
        r->a = 0; set_dp16(ram, r, 0xE7, 0);
      }
      set_dp16(ram, r, 0xE3, 0);
      r->a = dp16(ram, r, FACING);
      jsl(ram, r, dir->at_objects, 0x83, hm_CBBBB);
      r->c = 1;                                              /* CMP #0 */
      if (r->a != 0) { stop_moving(ram, r); return; }
      r->a = dp16(ram, r, FACING);
      jsl(ram, r, dir->at_map, 0x83, hm_CEEEE);
      uint16_t depth_reg = dir->y_axis ? r->y : r->x;
      set_dp16(ram, r, 0x7E, depth_reg);
      int which = 0;
      r->c = r->a >= 1;
      if (r->a == 1) which = 1;
      else { r->c = r->a >= 2; if (r->a == 2) which = 2; }
      if (which) {
        uint16_t v = dp16(ram, r, which == 1 ? 0xEB : 0xE9);
        uint16_t pos = dp16(ram, r, dir->y_axis ? 0xD6 : 0xD8);
        /* corner 1 ($EB) slides when the half bit (8) of his other
         * coordinate is set, corner 2 ($E9) when it is clear; tile types
         * 0 and $A0-$AF always let him slide */
        bool go;
        {
          r->a = v;
          if (v == 0) go = true;
          else {
            r->c = v >= 0xA0;
            go = false;
            if (v >= 0xA0) { r->c = v >= 0xB0; if (v < 0xB0) go = true; }
            if (!go) {
              r->a = (uint16_t)(pos & 0x0008);
              go = which == 1 ? r->a != 0 : r->a == 0;
            }
          }
        }
        if (go) {
          r->a = 1; set_dp16(ram, r, STEP_SIZE, 1);
          uint8_t nd = which == 1 ? dir->slide1 : dir->slide2;
          r->a = nd; set_dp16(ram, r, FACING, nd);
          d = nd;
          continue;
        }
      }
      /* the front: the tiles hit, and the step cut to the edge */
      r->a = dp16(ram, r, 0xEB); wr16(ram, r, 0x087A, r->a);
      r->a = dp16(ram, r, 0xE9); wr16(ram, r, 0x0878, r->a);
      r->a = (uint16_t)(r->a - 0x00C0);                      /* SEC; SBC #$C0 */
      r->c = r->a >= 0x0010;
      if (r->a < 0x0010) { stop_moving(ram, r); return; }
      set_dp16(ram, r, 0x7E, dir->y_axis ? r->y : r->x);
      uint16_t step = dp16(ram, r, STEP_SIZE), depth = dp16(ram, r, 0x7E);
      r->c = step >= depth;
      r->a = (uint16_t)(step - depth);
      set_dp16(ram, r, STEP_SIZE, r->a);
    }
    unsigned coord = dir->y_axis ? 0xD8 : 0xD6;
    uint16_t pos = dp16(ram, r, coord), step = dp16(ram, r, STEP_SIZE);
    if (dir->minus) { r->c = pos >= step; r->a = (uint16_t)(pos - step); }
    else { uint32_t s = (uint32_t)pos + step; r->c = s > 0xFFFF; r->a = (uint16_t)s; }
    set_dp16(ram, r, coord, r->a);
    jsl(ram, r, dir->at_camera, 0x80, hm_SUB_809EBC);
    jsl(ram, r, dir->at_scroll, 0x80, dir->scroll);
    count_step(ram, r);
    return;
  }
}

/* ─────────────────────────────────────────────────────────────────────────
 * The frame's end. ResetIdleAnimatioTimer ($81:C002): standing still goes
 * to CODE_81C556; otherwise CODE_81C00E clears the idle timer and
 * CODE_81C013 runs the camera (SUB_809F61), $1E = 0, and the facing ($DA,
 * $0913) back from $0911. RTL.
 * ───────────────────────────────────────────────────────────────────────── */
static void frame_end(uint8_t *ram, HmRegs *r)                 /* CODE_81C013 */
{
  jsl(ram, r, 0xC013, 0x80, hm_SUB_809F61);
  r->a = 0;
  set_dp16(ram, r, STEP_SIZE, 0);
  r->a = rd16(ram, r, 0x0911);
  set_dp16(ram, r, FACING, r->a);
  wr16(ram, r, 0x0913, r->a);
}
static void not_idle(uint8_t *ram, HmRegs *r)                  /* CODE_81C00E */
{
  wr8(ram, r, IDLE_TIMER, 0);
  frame_end(ram, r);
}
void hm_ResetIdleAnimatioTimer(uint8_t *ram, HmRegs *r)
{
  r->a = dp16(ram, r, ACTION);
  r->c = 1;                                                /* CMP #0 */
  if (r->a == 0) { hm_CODE_81C556(ram, r); return; }
  not_idle(ram, r);
}
#define END hm_ResetIdleAnimatioTimer

/* ─────────────────────────────────────────────────────────────────────────
 * Standing still (CODE_81C556): $D2 bit 15 off. With $D2 bit 13 only the
 * animation. With bit 14 (nothing to do) the idle timer runs: at $F0 it
 * goes back to $78 and an idle animation starts (action 9, animation
 * $3C + 2 x facing + 0-2 at random). Otherwise the standing animation
 * for the state: holding something (bit 1) $14, bits 4 / 5 / 8 / 11 $B0
 * / $C0 / $4F / $D0, $7F1F60 bits 1-2 $EA, else 0 (+ facing, except $4F).
 * ───────────────────────────────────────────────────────────────────────── */
void hm_CODE_81C556(uint8_t *ram, HmRegs *r)
{
  state_off(ram, r, 0x8000);
  r->a = (uint16_t)(dp16(ram, r, GAME_STATE) & 0x2000);
  if (r->a) { animate(ram, r, 0xC668); not_idle(ram, r); return; }
  r->a = (uint16_t)(dp16(ram, r, GAME_STATE) & 0x4000);
  if (r->a) {
    uint8_t t = rd8(ram, r, IDLE_TIMER);
    set_a8(r, t);
    r->c = t >= 0xF0;
    if (t == 0xF0) {
      set_a8(r, 0x78);
      wr8(ram, r, IDLE_TIMER, 0x78);
      set_action(ram, r, 9);
      r->a = dp16(ram, r, FACING);
      r->c = r->a >> 15;
      uint32_t sum = (uint32_t)(uint16_t)(r->a << 1) + 0x3C;
      r->c = sum > 0xFFFF;
      r->a = (uint16_t)sum;
      wr16(ram, r, ANIMATION, r->a);
      set_a8(r, 2);
      jsl(ram, r, 0xC59F, 0x80, hm_RNGReturn0toA);
      r->a = (uint8_t)r->a;                                /* XBA; LDA #0; XBA */
      sum = (uint32_t)r->a + rd16(ram, r, ANIMATION);
      r->c = sum > 0xFFFF;
      r->a = (uint16_t)sum;
      wr16(ram, r, ANIMATION, r->a);
      hm_CODE_81CB5E(ram, r);
      return;
    }
    set_a8(r, (uint8_t)(t + 1));
    wr8(ram, r, IDLE_TIMER, (uint8_t)(t + 1));
  }
  static const struct { uint16_t bit, base; bool facing; } pose[] = {
    { 0x0002, 0x0014, true }, { 0x0010, 0x00B0, true }, { 0x0020, 0x00C0, true },
    { 0x0100, 0x004F, false }, { 0x0800, 0x00D0, true },
  };
  for (unsigned i = 0; i < 5; i++) {
    r->a = (uint16_t)(dp16(ram, r, GAME_STATE) & pose[i].bit);
    if (r->a) {
      if (pose[i].facing) animation_facing(ram, r, pose[i].base);
      else animation(ram, r, pose[i].base);
      animate(ram, r, 0xC662);
      not_idle(ram, r);
      return;
    }
  }
  r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F60) & 0x0006);
  if (r->a) {
    animation_facing(ram, r, 0x00EA);
    animate(ram, r, 0xC662);
    not_idle(ram, r);
    return;
  }
  animation_facing(ram, r, 0x0000);
  animate(ram, r, 0xC65C);
  frame_end(ram, r);
}

/* ─────────────────────────────────────────────────────────────────────────
 * Walking (CODE_81C66E, action 1), running (CODE_81C736, action 2) and
 * being pushed (CODE_81C83B, action $0E): the animation for the state
 * (as standing; walking carrying something $28, running $2C...), the
 * step (1 pixel; 2 running; 3 in deep snow, $D2 bit 4) and the move
 * (CODE_81CB77); then action 0 and $096D = 0. Running only runs with $D2
 * bit 12, or bit 14 without bit 13; and a step sound every 16 frames
 * while running or in snow ($091A).
 * ───────────────────────────────────────────────────────────────────────── */
typedef struct Gait { uint16_t bit, base; uint8_t step; bool sound; } Gait;

static void after_move(uint8_t *ram, HmRegs *r)                /* $81:C82A */
{
  set_action(ram, r, 0);
  wr8(ram, r, 0x096D, 0);
  not_idle(ram, r);
}
static void step_sound_and_move(uint8_t *ram, HmRegs *r)       /* $81:C806 */
{
  uint8_t n = (uint8_t)((rd8(ram, r, 0x091A) + 1) & 0x0F);
  set_a8(r, n);
  wr8(ram, r, 0x091A, n);
  if (!n) sound(ram, r, 0x05, 0xC823);
  jsr(ram, r, 0xC827, hm_CODE_81CB77);
  after_move(ram, r);
}

static void gait(uint8_t *ram, HmRegs *r, const Gait *g, unsigned n, const Gait *dflt, uint16_t at_move)
{
  const Gait *pick = NULL;
  for (unsigned i = 0; i < n && !pick; i++) {
    r->a = (uint16_t)(dp16(ram, r, GAME_STATE) & g[i].bit);
    if (r->a) pick = &g[i];
  }
  if (!pick) {
    r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F60) & 0x0006);
    pick = r->a ? &g[n] : dflt;
  }
  animation_facing(ram, r, pick->base);
  set_step_size(ram, r, pick->step);
  if (pick->sound) { step_sound_and_move(ram, r); return; }
  jsr(ram, r, at_move, hm_CODE_81CB77);
  after_move(ram, r);
}

void hm_CODE_81C66E(uint8_t *ram, HmRegs *r)
{
  static const Gait g[] = {
    { 0x0002, 0x0028, 1, false }, { 0x0010, 0x00B4, 3, true }, { 0x0020, 0x00C4, 1, false },
    { 0x0800, 0x00D4, 1, false }, { 0, 0x00EE, 1, false },
  };
  static const Gait dflt = { 0, 0x0004, 1, false };
  state_off(ram, r, 0x8000);
  gait(ram, r, g, 4, &dflt, 0xC722);
}

void hm_CODE_81C736(uint8_t *ram, HmRegs *r)
{
  static const Gait g[] = {
    { 0x0002, 0x002C, 2, true }, { 0x0010, 0x00B4, 3, true }, { 0x0020, 0x00C4, 1, false },
    { 0x0800, 0x00D4, 1, false }, { 0, 0x00EE, 1, false },
  };
  static const Gait dflt = { 0, 0x0008, 2, true };
  state_off(ram, r, 0x8000);
  r->a = (uint16_t)(dp16(ram, r, GAME_STATE) & 0x1000);
  if (!r->a) {
    r->a = (uint16_t)(dp16(ram, r, GAME_STATE) & 0x2000);
    if (r->a) { after_move(ram, r); return; }
    r->a = (uint16_t)(dp16(ram, r, GAME_STATE) & 0x4000);
    if (!r->a) { after_move(ram, r); return; }
  }
  gait(ram, r, g, 4, &dflt, 0xC827);
}

void hm_CODE_81C83B(uint8_t *ram, HmRegs *r)
{
  state_off(ram, r, 0x8000);
  animation(ram, r, 0x0045);
  set_step_size(ram, r, 1);
  jsr(ram, r, 0xC857, hm_CODE_81CB77);
  after_move(ram, r);
}

/* PlayerState_00 ($81:BFDC; actions 0, 1, 2, 6, 7, $0E): $D2 bit 2 goes to
 * CODE_81CB14; then by the action: running, walking, pushed, or the end. */
void hm_PlayerState_00(uint8_t *ram, HmRegs *r)
{
  r->a = (uint16_t)(dp16(ram, r, GAME_STATE) & 0x0004);
  if (r->a) { hm_CODE_81CB14(ram, r); return; }
  r->a = dp16(ram, r, ACTION);
  r->c = r->a >= 2;
  if (r->a == 2) { hm_CODE_81C736(ram, r); return; }
  r->c = r->a >= 1;
  if (r->a == 1) { hm_CODE_81C66E(ram, r); return; }
  r->c = r->a >= 0x0E;
  if (r->a == 0x0E) { hm_CODE_81C83B(ram, r); return; }
  END(ram, r);
}

/* CODE_81CB14 ($D2 bit 2): animation $30 + $0924 (its low byte only), and
 * when it ends action 0 and $D2 bit 2 off. */
void hm_CODE_81CB14(uint8_t *ram, HmRegs *r)
{
  if (first_frame(ram, r)) {
    uint16_t sum = (uint16_t)(rd8(ram, r, 0x0924) + 0x30);
    r->c = sum > 0xFF;
    set_a8(r, (uint8_t)sum);
    wr8(ram, r, ANIMATION, (uint8_t)sum);
  }
  animate(ram, r, 0xCB38);
  if (!anim_over(ram, r)) { not_idle(ram, r); return; }
  set_action(ram, r, 0);
  state_off(ram, r, 0x0004);
  END(ram, r);
}

/* CODE_81CB5E (action 9, an idle animation): to its end, then action 0. */
void hm_CODE_81CB5E(uint8_t *ram, HmRegs *r)
{
  animate(ram, r, 0xCB60);
  if (anim_over(ram, r)) set_action(ram, r, 0);
  frame_end(ram, r);
}

/* ─────────────────────────────────────────────────────────────────────────
 * The other actions: an animation to its end, and what then follows.
 * ───────────────────────────────────────────────────────────────────────── */
/* animation, played; when over the action becomes `next` (if >= 0) */
static bool play(uint8_t *ram, HmRegs *r, uint16_t at)
{
  animate(ram, r, at);
  return anim_over(ram, r);
}

/* $1A ($81:C0B5): animation $F2 + facing; then action 0 and event $14 / $45
 * (script 0, or $15 / script 3 with $7F1F60 bit 2); $7F1F60 bits 1-2 off. */
void hm_PlayerState_1A(uint8_t *ram, HmRegs *r)
{
  animation_facing(ram, r, 0x00F2);
  if (play(ram, r, 0xC0C2)) {
    set_action(ram, r, 0);
    r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F60) & 0x0004);
    if (!r->a) event_long(ram, r, 0x0014, 0x0045, 0x0000, 0xC0EC);
    else event_long(ram, r, 0x0015, 0x0045, 0x0003, 0xC0FD);
    r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F60) & 0xFFF9);
    hm_bus_wr16(ram, 0x7F1F60, r->a);
  }
  END(ram, r);
}

/* $17 ($81:C111): facing back from $0913, animation $DC + facing, a step of
 * 1 (CODE_81CB77); then action 0 and $7F1F5C bit 5. */
void hm_PlayerState_17(uint8_t *ram, HmRegs *r)
{
  r->a = rd16(ram, r, 0x0913);
  set_dp16(ram, r, FACING, r->a);
  wr16(ram, r, 0x0911, r->a);
  animation_facing(ram, r, 0x00DC);
  set_step_size(ram, r, 1);
  jsr(ram, r, 0xC12C, hm_CODE_81CB77);
  if (play(ram, r, 0xC12F)) {
    set_action(ram, r, 0);
    long_or(ram, r, 0x7F1F5C, 0x0020);
  }
  END(ram, r);
}

/* $18 ($81:C153): animation $E0 + $0911, facing from $0911, a step of 1;
 * then action 0, $D2 bit 4 off and $7F1F5C bit 11. */
void hm_PlayerState_18(uint8_t *ram, HmRegs *r)
{
  uint32_t sum = 0x00E0u + rd16(ram, r, 0x0911);
  r->c = sum > 0xFFFF;
  r->a = (uint16_t)sum;
  wr16(ram, r, ANIMATION, r->a);
  r->a = rd16(ram, r, 0x0911);
  set_dp16(ram, r, FACING, r->a);
  set_step_size(ram, r, 1);
  jsr(ram, r, 0xC16E, hm_CODE_81CB77);
  if (play(ram, r, 0xC171)) {
    set_action(ram, r, 0);
    state_off(ram, r, 0x0010);
    long_or(ram, r, 0x7F1F5C, 0x0800);
  }
  END(ram, r);
}

/* $16 ($81:C1A1): animation $D8 + facing; then action 0, event $16 / $12
 * at $7F1F2C / $7F1F2E (SUB_8480F8), $7F1F58 = 1, $0938 = 0. */
void hm_PlayerState_16(uint8_t *ram, HmRegs *r)
{
  animation_facing(ram, r, 0x00D8);
  if (play(ram, r, 0xC1AE)) {
    set_action(ram, r, 0);
    r->a = hm_bus_rd16(ram, 0x7F1F2C);
    wr16(ram, r, 0x0985, r->a);
    r->a = hm_bus_rd16(ram, 0x7F1F2E);
    wr16(ram, r, 0x0987, r->a);
    r->a = 0x0016; r->x = 0; r->y = 0x0012;
    jsl(ram, r, 0xC1DB, 0x84, hm_SUB_8480F8);
    r->a = 1;
    hm_bus_wr16(ram, 0x7F1F58, 1);
    wr8(ram, r, 0x0938, 0);
  }
  END(ram, r);
}

/* $15, $14, $0F, $10, $12: a fixed animation; when over, the next action */
static void fixed(uint8_t *ram, HmRegs *r, uint16_t anim, uint16_t at, int next)
{
  animation(ram, r, anim);
  if (play(ram, r, at) && next >= 0) set_action(ram, r, (uint16_t)next);
  END(ram, r);
}
void hm_PlayerState_15(uint8_t *ram, HmRegs *r) { fixed(ram, r, 0x00CC, 0xC1FA, 0x14); }
void hm_PlayerState_14(uint8_t *ram, HmRegs *r) { fixed(ram, r, 0x008F, 0xC21B, 0x00); }
void hm_PlayerState_10(uint8_t *ram, HmRegs *r) { fixed(ram, r, 0x0089, 0xC24C, 0x11); }
void hm_PlayerState_12(uint8_t *ram, HmRegs *r) { fixed(ram, r, 0x008A, 0xC298, 0x11); }
void hm_PlayerState_0F(uint8_t *ram, HmRegs *r)
{
  animation(ram, r, 0x0088);
  animate(ram, r, 0xC23C);
  END(ram, r);
}

/* $11 ($81:C263): one chance in 256 x 5 to go on to action $12 */
void hm_PlayerState_11(uint8_t *ram, HmRegs *r)
{
  set_a8(r, 0xFF);
  jsl(ram, r, 0xC269, 0x80, hm_RNGReturn0toA);
  if (!(r->a & 0xFF)) {
    set_a8(r, 0x04);
    jsl(ram, r, 0xC273, 0x80, hm_RNGReturn0toA);
    if (!(r->a & 0xFF)) set_action(ram, r, 0x12);
  }
  animation(ram, r, 0x008E);
  animate(ram, r, 0xC288);
  END(ram, r);
}

/* $13 ($81:C2AF): to the animation's end; then action $0F and the event in
 * slot $14 (freed if in use): script $35 for animation $8B (+2 happiness),
 * $34 for $8C, $33 otherwise. */
void hm_PlayerState_13(uint8_t *ram, HmRegs *r)
{
  if (play(ram, r, 0xC2B1)) {
    set_action(ram, r, 0x000F);
    r->a = 0x0014;
    jsl(ram, r, 0xC2CA, 0x84, hm_SetCCPoiner);
    r->y = 0;
    set_a8(r, hm_bus_rd8(ram, hm_dp_long(ram, r, 0xCC, 0)));
    if (r->a & 0xFF) {
      r->a = 0x0014;
      jsl(ram, r, 0xC2DE, 0x84, hm_SUB_848020);
    }
    r->a = rd16(ram, r, ANIMATION);
    r->c = r->a >= 0x008B;
    if (r->a == 0x008B) {
      event_long(ram, r, 0x0014, 0, 0x0035, 0xC30E);
      r->a = 2;
      jsl(ram, r, 0xC317, 0x83, hm_AddPlayerHappiness);
    } else {
      r->c = r->a >= 0x008C;
      if (r->a == 0x008C) event_long(ram, r, 0x0014, 0, 0x0034, 0xC329);
      else event_long(ram, r, 0x0014, 0, 0x0033, 0xC2FC);
    }
  }
  END(ram, r);
}

/* $1B / $0D ($81:C333, $81:C364): $7F1F60 bit 4 / $7F1F5C bit 10, then
 * animation $BC + facing; action 0 at its end. */
static void flag_and_bow(uint8_t *ram, HmRegs *r, uint32_t at_flag, uint16_t bit, uint16_t at)
{
  long_or(ram, r, at_flag, bit);
  animation_facing(ram, r, 0x00BC);
  if (play(ram, r, at)) set_action(ram, r, 0);
  END(ram, r);
}
void hm_PlayerState_1B(uint8_t *ram, HmRegs *r) { flag_and_bow(ram, r, 0x7F1F60, 0x0010, 0xC34D); }
void hm_PlayerState_0D(uint8_t *ram, HmRegs *r) { flag_and_bow(ram, r, 0x7F1F5C, 0x0400, 0xC37E); }

/* $0C ($81:C395): looking at the watch: animation $90 + the tool's number
 * (the tool in hand), and the time's text: $046A unless $7F1F6E bit 14 and
 * not $7F1F60 bit 9; then by the hour, 6 to 18: $046B-$0475, at 17 $0476
 * ($0478 with one of $7F1F66 bits 0-4), at 18 $0477, others $046B. At the
 * animation's end action 0, unless $7F1F5A bit 14. */
void hm_PlayerState_0C(uint8_t *ram, HmRegs *r)
{
  if (first_frame(ram, r)) {
    uint16_t sum = (uint16_t)(rd8(ram, r, 0x0921) + 0x90);
    r->c = sum > 0xFF;
    r->a = (uint8_t)sum;                                   /* LDA #0; XBA; ...; REP */
    wr16(ram, r, ANIMATION, r->a);
    uint16_t text = 0x046A;
    r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F6E) & 0x4000);
    if (r->a) {
      r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F60) & 0x0200);
      if (!r->a) {
        uint8_t h = hm_bus_rd8(ram, 0x7F1F1C);
        set_a8(r, h);
        text = 0x046B;
        bool found = false;
        for (uint8_t k = 6; k <= 0x12 && !found; k++) {
          r->c = h >= k;
          if (h != k) continue;
          found = true;
          if (k <= 0x10) text = (uint16_t)(0x046B + (k - 6));
          else if (k == 0x12) text = 0x0477;
          else {
            text = 0x0476;
            for (uint16_t bit = 1; bit <= 0x10; bit = (uint16_t)(bit << 1)) {
              r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F66) & bit);
              if (r->a) { text = 0x0478; break; }
            }
          }
        }
      }
    }
    r->x = text;
    set_a8(r, 2);
    wr8(ram, r, 0x019A, 2);
    set_a8(r, 0);
    wr8(ram, r, 0x0191, 0);
    jsl(ram, r, 0xC4C6, 0x83, hm_StartTextBox);
  }
  if (play(ram, r, 0xC4CA)) {
    r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F5A) & 0x4000);
    if (!r->a) set_action(ram, r, 0);
  }
  END(ram, r);
}

/* $0B ($81:C4EC): the animation as set; action 0 at its end. */
void hm_PlayerState_0B(uint8_t *ram, HmRegs *r)
{
  first_frame(ram, r);
  if (play(ram, r, 0xC503)) set_action(ram, r, 0);
  END(ram, r);
}

/* $0A ($81:C51A): using a tool: at the animation's end the tool acts
 * (SUB_829260); at frame $18 of the use ($091B) its sound (ToolUsedSound1). */
void hm_PlayerState_0A(uint8_t *ram, HmRegs *r)
{
  first_frame(ram, r);
  if (play(ram, r, 0xC531)) jsl(ram, r, 0xC53E, 0x82, hm_SUB_829260);
  uint8_t n = (uint8_t)(rd8(ram, r, 0x091B) + 1);
  set_a8(r, n);
  wr8(ram, r, 0x091B, n);
  r->c = n >= 0x18;
  if (n == 0x18) jsl(ram, r, 0xC54F, 0x82, hm_ToolUsedSound1);
  END(ram, r);
}

/* $03 ($81:C86B): a jump: the first frame its animation ($0C, or $20
 * carrying, $B8 in snow, $C8 with $D2 bit 5; + facing) and sound effect 6;
 * the step is 1 (2 with bit 5, or every other frame with $D2 bit 9); at
 * the end action 0, $D2 bit 9 off, and facing left also bit 5. */
void hm_PlayerState_03(uint8_t *ram, HmRegs *r)
{
  bool big = false;
  if (first_frame(ram, r)) {
    static const struct { uint16_t bit, base, at; } jump[] = {
      { 0x0002, 0x0020, 0xC8DF }, { 0x0010, 0x00B8, 0xC8FE }, { 0x0020, 0x00C8, 0xC91D },
    };
    int k = -1;
    for (int i = 0; i < 3 && k < 0; i++) {
      r->a = (uint16_t)(dp16(ram, r, GAME_STATE) & jump[i].bit);
      if (r->a) k = i;
    }
    if (k < 0) {
      animation_facing(ram, r, 0x000C);
      sound(ram, r, 0x06, 0xC8BB);
      wr8(ram, r, 0x0971, 0);
    } else {
      animation_facing(ram, r, jump[k].base);
      sound(ram, r, 0x06, jump[k].at);
      big = k == 2;
    }
  }
  if (big) {
    set_step_size(ram, r, 2);
  } else {
    set_step_size(ram, r, 1);
    r->a = (uint16_t)(dp16(ram, r, GAME_STATE) & 0x0200);
    if (r->a) {
      uint8_t t = (uint8_t)(rd8(ram, r, 0x0971) ^ 1);
      set_a8(r, t);
      wr8(ram, r, 0x0971, t);
      if (t) set_step_size(ram, r, 2);
    }
  }
  jsr(ram, r, 0xC951, hm_CODE_81CB77);
  animate(ram, r, 0xC954);
  if (anim_over(ram, r)) {
    set_action(ram, r, 0);
    state_off(ram, r, 0x0200);
    r->a = dp16(ram, r, FACING);
    r->c = r->a >= 3;
    if (r->a == 3) state_off(ram, r, 0x0020);
  }
  END(ram, r);
}

/* $04 ($81:C991): picking up: animation $10 + facing and the item down
 * (SUB_818000) the first frame; at the end action 0, holding ($D2 bit 1)
 * and $091E = the item. */
void hm_PlayerState_04(uint8_t *ram, HmRegs *r)
{
  if (first_frame(ram, r)) {
    animation_facing(ram, r, 0x0010);
    jsl(ram, r, 0xC9B3, 0x81, hm_SUB_818000);
  }
  if (play(ram, r, 0xC9B7)) {
    set_action(ram, r, 0);
    state_on(ram, r, 0x0002);
    set_a8(r, rd8(ram, r, 0x091D));
    wr8(ram, r, 0x091E, (uint8_t)r->a);
  }
  END(ram, r);
}

/* $05 ($81:C9DF): throwing: animation $1C + facing and sound effect 7; at
 * the end action 0 and the hands free ($D2 bit 1 off). */
void hm_PlayerState_05(uint8_t *ram, HmRegs *r)
{
  if (first_frame(ram, r)) {
    animation_facing(ram, r, 0x001C);
    sound(ram, r, 0x07, 0xCA0F);
  }
  if (play(ram, r, 0xCA13)) {
    set_action(ram, r, 0);
    state_off(ram, r, 0x0002);
  }
  END(ram, r);
}

/* $08 ($81:CA36): eating item $0984: animation $33 + the item; at the end
 * the hands are free and action 0, but item 2 goes on to action $14; item
 * 8 (the power berry?) also counts in $7F1F36, gives +10 happiness and 10
 * more maximum stamina ($0917), and marks where it was eaten ($7F1F6C bit
 * 14 in the houses $15-$17, $7F1F6A bit 6 on map $29). */
void hm_PlayerState_08(uint8_t *ram, HmRegs *r)
{
  if (first_frame(ram, r)) {
    uint32_t sum = (uint32_t)rd8(ram, r, 0x0984) + 0x33;
    r->c = sum > 0xFFFF;
    r->a = (uint16_t)sum;
    wr16(ram, r, ANIMATION, r->a);
  }
  if (!play(ram, r, 0xCA60)) { END(ram, r); return; }
  uint8_t item = rd8(ram, r, 0x0984);
  set_a8(r, item);
  r->c = item >= 2;
  if (item == 2) {
    set_action(ram, r, 0x0014);
    state_off(ram, r, 0x0002);
    END(ram, r);
    return;
  }
  r->c = item >= 8;
  if (item != 8) {
    set_action(ram, r, 0);
    state_off(ram, r, 0x0002);
    END(ram, r);
    return;
  }
  uint8_t n = (uint8_t)(hm_bus_rd8(ram, 0x7F1F36) + 1);
  set_a8(r, n);
  hm_bus_wr8(ram, 0x7F1F36, n);
  r->a = 0x000A;
  jsl(ram, r, 0xCABB, 0x83, hm_AddPlayerHappiness);
  uint16_t s = (uint16_t)(rd8(ram, r, 0x0917) + 0x0A);
  r->c = s > 0xFF;
  set_a8(r, (uint8_t)s);
  wr8(ram, r, 0x0917, (uint8_t)s);
  set_action(ram, r, 0);
  state_off(ram, r, 0x0002);
  uint8_t map = *hm_dp(ram, r, 0x22);
  set_a8(r, map);
  bool house = false;
  for (uint8_t k = 0x15; k <= 0x17 && !house; k++) {
    r->c = map >= k;
    house = map == k;
  }
  if (house) long_or(ram, r, 0x7F1F6C, 0x4000);
  else {
    r->c = map >= 0x29;
    if (map == 0x29) long_or(ram, r, 0x7F1F6A, 0x0040);
  }
  END(ram, r);
}

/* ─────────────────────────────────────────────────────────────────────────
 * SUB_809D0B ($80:9D0B, JSL, RTL): the mountain (maps $10-$13), on days
 * without $0196 bits 1, 3, 4. Where the player stands ($0878 / $087A, the
 * tiles under him):
 *   - tile $F9 in spring, $FA in autumn, $FB in winter: one chance in 16,
 *     once a day ($7F1F5C bit 13 / 14 / 15), of a find: event $11 / $2C,
 *     $13 / $2B, $12 / $2A;
 *   - both tiles $F8 (the water's edge) with the fishing rod ($7F1F5A
 *     bit 9), his hands free and not in snow or bit 11: he starts fishing
 *     (action $0F), unless he already is (actions $10-$13).
 * Leftovers: those of the last routine called and the stores after it.
 * ───────────────────────────────────────────────────────────────────────── */
static bool under_player(uint8_t *ram, HmRegs *r, uint16_t tile)
{
  r->a = rd16(ram, r, 0x0878);
  r->c = r->a >= tile;
  if (r->a == tile) return true;
  r->a = rd16(ram, r, 0x087A);
  r->c = r->a >= tile;
  return r->a == tile;
}
static void mountain_find(uint8_t *ram, HmRegs *r, uint8_t season, uint16_t bit,
                          uint16_t a, uint16_t x, uint16_t at_rng, uint16_t at_event)
{
  uint8_t s = hm_bus_rd8(ram, 0x7F1F19);
  set_a8(r, s);
  if (season == 0) { if (s) return; }
  else { r->c = s >= season; if (s != season) return; }
  set_a8(r, 0x10);
  jsl(ram, r, at_rng, 0x80, hm_RNGReturn0toA);
  if (r->a & 0xFF) return;
  r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F5C) & bit);
  if (r->a) return;
  long_or(ram, r, 0x7F1F5C, bit);
  event_long(ram, r, a, x, 0x0000, at_event);
}

void hm_SUB_809D0B(uint8_t *ram, HmRegs *r)
{
  uint8_t map = *hm_dp(ram, r, 0x22);
  set_a8(r, map);
  r->c = map >= 0x04;
  if (map < 0x04) return;
  r->c = map >= 0x10;
  if (map < 0x10) return;
  r->c = map >= 0x14;
  if (map >= 0x14) return;
  r->a = (uint16_t)(rd16(ram, r, 0x0196) & 0x001A);
  if (r->a) return;
  if (under_player(ram, r, 0x00F9)) {
    mountain_find(ram, r, 0, 0x2000, 0x0011, 0x002C, 0x9E0C, 0x9E39);
    return;
  }
  if (under_player(ram, r, 0x00FA)) {
    mountain_find(ram, r, 2, 0x4000, 0x0013, 0x002B, 0x9E4D, 0x9E77);
    return;
  }
  if (under_player(ram, r, 0x00FB)) {
    mountain_find(ram, r, 3, 0x8000, 0x0012, 0x002A, 0x9E8B, 0x9EB5);
    return;
  }
  r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F5A) & 0x0200);
  if (!r->a) return;
  static const uint16_t busy[] = { 0x0002, 0x0010, 0x0800 };
  for (unsigned i = 0; i < 3; i++) {
    r->a = (uint16_t)(dp16(ram, r, GAME_STATE) & busy[i]);
    if (r->a) return;
  }
  r->a = rd16(ram, r, 0x0878);
  r->c = r->a >= 0x00F8;
  if (r->a != 0x00F8) return;
  r->a = rd16(ram, r, 0x087A);
  r->c = r->a >= 0x00F8;
  if (r->a != 0x00F8) return;
  for (uint16_t k = 0x10; k <= 0x13; k++) {
    r->a = dp16(ram, r, ACTION);
    r->c = r->a >= k;
    if (r->a == k) return;
  }
  set_action(ram, r, 0x000F);
}

/* ─────────────────────────────────────────────────────────────────────────
 * SUB_809A64 ($80:9A64, JSL, RTL): the doors. The tiles under the player
 * ($0878 / $087A) are both of the types $C0-$CF (an exit) or the routine
 * leaves; a type from $D0 goes on to SUB_809D0B (the mountain). Not while
 * $D2 bit 4 or $7F1F60 bits 1-2; with $7F1F6C bit 0, not the exits whose
 * entry has destination $1C.
 * The exit's entry: the tile's property byte ([$0D] + 4 tile) x 8 into the
 * door table (Unk_Table13, $80:B6F5): +0 the destination map, +2 the map
 * animation (0 = none), +3 flags, +4 / +6 the X / Y where he arrives.
 * $098A counts the frames of the walk out:
 *   0: the first; the destination ($098B): + the season (flag 0); flag 6:
 *      on day 1 of spring $3A, on day 24 of winter $39, on days 10-12 + 4
 *      (the festivals). The door is shut (he stays, the tiles under him are
 *      forgotten) in map $0B, or by its flags: 1 from 17:00, 2 before 17:00,
 *      3 on weekdays 1-5, 4 on the 10th of winter or Saturdays, 5 on
 *      Sundays, 7 with $0196 bit 4. Open: the clock stops ($0973 = 0), the
 *      arrival point goes to $017D / $017F and the map animation starts;
 *   1: waits while $00CF;
 *   then up to $0D he walks on (action 1) in his facing (3 = any other);
 *   at $0D: leaving a farm map (below 4) the farm is saved; leaving the
 *      maps $0C-$0F an hour passes (not past 18:00) and HaveLunch; then
 *      $0196 bit 14 (the transition).
 * Leftovers: the original's A, X, Y and C on each path.
 * ───────────────────────────────────────────────────────────────────────── */
#define DOORS 0x80B6F5   /* Unk_Table13: 8 bytes an exit */

static uint8_t door(uint8_t *ram, uint16_t x) { return hm_bus_rd8(ram, DOORS + x); }
static bool cmp8(HmRegs *r, uint8_t a, uint8_t v) { r->c = a >= v; return a == v; }

/* LDA $0878; ASL; ASL; TAY; LDA #0; XBA; LDA [$0D],Y; ASL x3; TAX */
static uint16_t door_entry(uint8_t *ram, HmRegs *r)
{
  uint16_t tile = rd16(ram, r, 0x0878);
  r->y = (uint16_t)(tile << 2);
  r->a = (uint16_t)(hm_bus_rd8(ram, hm_dp_long(ram, r, 0x0D, r->y)) << 3);
  r->c = 0;                                       /* a byte x 8 */
  r->x = r->a;
  return r->x;
}
static void door_shut(uint8_t *ram, HmRegs *r)
{
  state_off(ram, r, 0x0080);
  wr16(ram, r, 0x0878, 0);
  wr16(ram, r, 0x087A, 0);
  wr8(ram, r, 0x098A, 0);
}
/* the flags 1-5 and 7 (C as the CMPs leave it: the closed door keeps it) */
static bool shut_by_flags(uint8_t *ram, HmRegs *r, uint8_t flags)
{
  uint8_t hour = hm_bus_rd8(ram, 0x7F1F1C), weekday = hm_bus_rd8(ram, 0x7F1F1A);
  uint8_t season = hm_bus_rd8(ram, 0x7F1F19), day = hm_bus_rd8(ram, 0x7F1F1B);
  if (flags & 0x02) { r->c = hour >= 0x11; if (r->c) return true; }       /* from 17:00 */
  if (flags & 0x04) { r->c = hour >= 0x11; if (!r->c) return true; }      /* before 17:00 */
  if ((flags & 0x08) && weekday != 0 && !cmp8(r, weekday, 0x06)) return true;
  if (flags & 0x10) {
    if (cmp8(r, season, 0x03) && cmp8(r, day, 0x0A)) return true;
    if (cmp8(r, weekday, 0x06)) return true;
  }
  if ((flags & 0x20) && weekday == 0) return true;
  if ((flags & 0x80) && (rd16(ram, r, 0x0196) & 0x0010)) return true;
  return false;
}
/* the first frame on the exit; X = the entry */
static void door_open(uint8_t *ram, HmRegs *r, uint16_t x)
{
  uint8_t flags = door(ram, x + 3);
  uint8_t dest = rd8(ram, r, 0x098B);
  if (flags & 0x01) dest = (uint8_t)(dest + hm_bus_rd8(ram, 0x7F1F19));
  if (flags & 0x40) {                                     /* the festivals */
    uint8_t day = hm_bus_rd8(ram, 0x7F1F1B), season = hm_bus_rd8(ram, 0x7F1F19);
    if (day == 0x01) { if (season == 0) dest = 0x3A; }
    else if (day == 0x18) { if (season == 3) dest = 0x39; }
    else if (day >= 0x0A && day < 0x0D) dest = (uint8_t)(dest + 4);
  }
  wr8(ram, r, 0x098B, dest);
  uint8_t map = *hm_dp(ram, r, 0x22);
  if (cmp8(r, map, 0x0B) || shut_by_flags(ram, r, flags)) {
    door_shut(ram, r);
    return;
  }
  wr8(ram, r, 0x0973, 0);                                 /* the clock stops */
  r->x = (uint16_t)(x + 6);
  wr16(ram, r, 0x017D, hm_bus_rd16(ram, DOORS + x + 4));
  wr16(ram, r, 0x017F, hm_bus_rd16(ram, DOORS + x + 6));
  uint8_t animation = *hm_dp(ram, r, 0x92);
  r->a = animation;                                       /* LDA #0; XBA; LDA $92 */
  r->c = 1;                                               /* CMP #0 */
  if (animation) jsl(ram, r, 0x9C56, 0x81, hm_StartMapAnimation);
}

void hm_SUB_809A64(uint8_t *ram, HmRegs *r)
{
  static const uint16_t under[] = { 0x0878, 0x087A };
  for (unsigned i = 0; i < 2; i++) {
    r->a = rd16(ram, r, under[i]);
    r->c = r->a >= 0x00C0;
    if (!r->c) return;
    r->c = r->a >= 0x00D0;
    if (r->c) { hm_SUB_809D0B(ram, r); return; }
  }
  r->a = (uint16_t)(dp16(ram, r, GAME_STATE) & 0x0010);
  if (r->a) return;
  r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F60) & 0x0006);
  if (r->a) return;
  r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F6C) & 0x0001);
  if (r->a) {
    uint16_t x = door_entry(ram, r);
    set_a8(r, door(ram, x));
    if (cmp8(r, (uint8_t)r->a, 0x1C)) return;
  }
  set_action(ram, r, 0x0000);
  state_on(ram, r, 0x0080);

  uint8_t step = rd8(ram, r, 0x098A);
  set_a8(r, step);
  bool walk = false;
  if (cmp8(r, step, 0x01)) {
    r->a = rd16(ram, r, 0x00CF);
    if (r->a) return;
    walk = true;
  } else if (step >= 0x02) walk = true;
  if (!walk) {                                            /* the first frame */
    set_a8(r, (uint8_t)(step + 1));
    wr8(ram, r, 0x098A, (uint8_t)(step + 1));
    uint16_t x = door_entry(ram, r);
    wr8(ram, r, 0x098B, door(ram, x));                    /* the destination */
    *hm_dp(ram, r, 0x92) = door(ram, x + 2);
    r->x = (uint16_t)(x + 3);
    door_open(ram, r, x);
    return;
  }

  step = rd8(ram, r, 0x098A);
  set_a8(r, step);
  if (!cmp8(r, step, 0x0D)) {                             /* walking out */
    set_a8(r, (uint8_t)(step + 1));
    wr8(ram, r, 0x098A, (uint8_t)(step + 1));
    set_action(ram, r, 0x0001);
    uint16_t facing = dp16(ram, r, FACING);
    r->a = facing;
    if (facing > 2) { r->c = facing >= 2; facing = 3; }
    else r->c = 1;
    set_dp16(ram, r, FACING, facing);
    r->a = facing;
    wr16(ram, r, 0x0911, facing);
    return;
  }
  uint8_t map = rd8(ram, r, 0x0022);                      /* LDA $0022 (absolute) */
  set_a8(r, map);
  r->c = map >= 0x04;
  if (!r->c) jsl(ram, r, 0x9CDC, 0x82, hm_CopyCurrentMaptoFarmMap);
  map = *hm_dp(ram, r, 0x22);
  set_a8(r, map);
  r->c = map >= 0x0C;
  if (r->c && (r->c = map >= 0x10, !r->c)) {
    uint8_t hour = hm_bus_rd8(ram, 0x7F1F1C);
    set_a8(r, hour);
    if (!cmp8(r, hour, 0x12)) {
      set_a8(r, (uint8_t)(hour + 1));
      hm_bus_wr8(ram, 0x7F1F1C, (uint8_t)(hour + 1));
      jsl(ram, r, 0x9CFB, 0x82, hm_HaveLunch);
    }
  }
  r->a = (uint16_t)(rd16(ram, r, 0x0196) | 0x4000);
  wr16(ram, r, 0x0196, r->a);
}
