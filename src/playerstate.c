/*
 * playerstate.c — the player each frame (HM-Decomp's PlayerFrame block).
 *
 * PlayerFrame ($81:BFB7, JSL, RTL) runs once a frame. With $D2 bit 0 clear
 * it only does the end of the frame; with $D2 bit 6 an action runs instead
 * (PlayerInteractionSelector, actions.c); otherwise it jumps (table
 * DATA16_81C027) by the player's action ($D4) to its step:
 *
 *   0, 1, 2, 6, 7, $0E  on foot (PlayerState_00_StandWalkRun: walking, running, ...)
 *   3 jumping, 4 picking up, 5 throwing, 8 eating, 9 an idle animation,
 *   $0A using a tool, $0B, $0C (the watch), $0D, $0F-$1B other animations,
 *   $1C swapping the tools.
 *
 * Most steps set the player's animation ($0901, often + his facing) and
 * play it (UpdatePlayerObject); when it ends ($0915 = $FFFF) the action changes.
 * They all end in the frame's end (ResetIdleAnimationTimer, $81:C002):
 * standing still (action 0) goes to the standing step PlayerStandStill; the
 * others clear the idle timer ($0925) and then MoveSecondBackground (the camera),
 * $1E = 0 and the facing back from $0911.
 *
 * MovePlayer moves the player $1E pixels in his facing: the event objects
 * (CollideEventObjects) and the map (CollideMap) can stop him or make him slide round a
 * corner; then the camera follows, and every 8 steps a footprint.
 *
 * As everywhere, the bodies keep A, X, Y and C as the original leaves them.
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

/* ── Names of the memory locations used here ────────────────────────────── */
/* Direct page ($00-$FF; the hm_dp_* / dp16 helpers take these offsets) */
#define GAME_STATE      HM_GAME_STATE  /* $D2, 16b flags: bit 0 off = only the frame's end,
                                        * 1 holding something, 2 eating, 4 deep snow,
                                        * 6 an action runs (PlayerInteractionSelector),
                                        * 7 walked by a script, 9 slow jump, 15 the
                                        * step's first frame is done */
#define ACTION          HM_PLAYER_ACTION /* $D4: the player's action (the step it runs) */
#define FACING          0xDA       /* 16b: the way he faces: 0 down, 1 up, 2 right, 3 left */
#define STEP_SIZE       0x1E       /* 16b: pixels to move this frame */
#define PLAYER_X        0xD6       /* 16b: the player's position, pixels */
#define PLAYER_Y        0xD8       /* 16b */
/* HM_MAP_ID ($22, hm_ram.h): the map being shown */
#define TILE_PROPS      0x0D       /* 24b $0D-$0F: the map's tile property table (4 bytes
                                    * a tile type) */
#define DOOR_MAP_ANIM   0x92       /* 8b: the exit's map animation (0 = none) */
#define EVENT_SLOT      0xCC       /* 24b $CC-$CE: the current event slot (SetEventSlotPointer) */
#define MAP_ANIM_RUNNING 0x00CF    /* 16b: a map animation is running (read as $00CF) */
/* CollideEventObjects / CollideMap: their inputs and outputs */
#define COLLIDE_X       0xDF       /* 16b: the position of who moves */
#define COLLIDE_Y       0xE1       /* 16b */
#define COLLIDE_AHEAD   0xE3       /* 16b: how far ahead to look */
#define COLLIDE_STEP_X  0xE5       /* 16b: the step */
#define COLLIDE_STEP_Y  0xE7       /* 16b */
#define COLLIDE_TILE_1  0xE9       /* 16b: CollideMap's output: the first front corner's
                                    * tile type (the one MovePlayer checks when A = 2) */
#define COLLIDE_TILE_2  0xEB       /* 16b: the second corner's tile type (checked when
                                    * A = 1) */
#define MOVE_DEPTH      0x7E       /* 16b, MovePlayer: CollideMap's X or Y, how far the step
                                    * would go into the blocking tile */

/* WRAM of the data bank ($0100-$1FFF; through rd8 / rd16 / wr8 / wr16) */
#define SOUND_EFFECT    0x0114     /* 8b: the sound effect PlaySoundEffect0A plays */
#define SOUND_BANK      0x0115     /* 8b: its bank */
#define TRANSITION_X    0x017D     /* 16b: where the player appears after a transition */
#define TRANSITION_Y    0x017F     /* 16b */
#define TEXT_BOX_LINES  0x0191     /* 8b: the lines of the next text box */
#define WEATHER_FLAGS   0x0196     /* 16b: weather and day flags; bits 1, 3, 4: no mountain
                                    * finds (bit 4 also shuts some doors); bit $4000
                                    * starts a map transition */
/* HM_INPUT_STATE ($019A, hm_ram.h): 2 = no control */
#define PLAYER_TILE_1   0x0878     /* 16b: the tile types under the player's front corners
                                    * (COLLIDE_TILE_1 / _2 of the last blocked move) */
#define PLAYER_TILE_2   0x087A     /* 16b */
#define ANIMATION       HM_ANIM_REQUEST /* $0901: the player's animation */
#define FOOTPRINT_X     0x0907     /* 16b: where the last footprint was left */
#define FOOTPRINT_Y     0x0909     /* 16b */
#define PLAYER_TURN     0x0911     /* 16b: the direction being tried (playerinput.c) */
#define PLAYER_FACING   0x0913     /* 16b: a copy of FACING, set from PLAYER_TURN at the
                                    * frame's end */
#define ANIM_TIMER      0x0915     /* 16b: $FFFF when the animation is over */
/* HM_MAX_STAMINA ($0917, hm_ram.h) */
#define STEP_COUNT      0x0919     /* 8b: moves since the last footprint (to 8) */
#define STEP_SOUND_TIMER 0x091A    /* 8b: frames to the next step sound (to 16) */
#define TOOL_FRAMES     0x091B     /* 8b: frames of the tool's use (its sound at $18) */
/* HM_ITEM_ON_HAND ($091D, hm_ram.h): what the player carries */
#define SHOWN_ITEM      0x091E     /* 8b: the item the player holds up */
/* HM_TOOL_SELECTED ($0921) and HM_TOOL_BACKPACK ($0923, hm_ram.h): the tools in
 * hand and in the bag */
#define BREAKFAST       0x0924     /* 8b: the breakfast rolled at waking (stamina.c):
                                    * which eating animation */
#define IDLE_TIMER      0x0925     /* 8b: frames standing idle (to $F0) */
#define DOG_TIMER       0x0938     /* 8b: the dog's frames between moods */
#define HIT_COUNTER     0x096D     /* 8b: the hammer's and the axe's hits */
#define INTERACTION     0x096E     /* 8b: the action PlayerInteractionSelector runs */
#define JUMP_TOGGLE     0x0971     /* 8b: the slow jump moves every other frame */
/* HM_TIME_RUNNING ($0973, hm_ram.h): bit 1 = stopped for the day's end */
#define ITEM            0x0984     /* 8b: the item being eaten */
#define FRONT_X         0x0985     /* 16b: the tile in front (StartScriptAtTileInFront) */
#define FRONT_Y         0x0987     /* 16b */
#define DOOR_STEP       0x098A     /* 8b: frames of the walk out of a door (to $0D) */
#define TRANSITION_MAP  0x098B     /* 8b: the map a transition goes to */

/* WRAM $7F:1Fxx: the game's saved state (ram[] offsets 0x11Fxx; WRAM_LONG gives
 * the bus address $7F:1Fxx for hm_bus_*). Also used, from hm_ram.h: HM_SEASON,
 * HM_WEEKDAY, HM_DAY and HM_HOUR. */
#define WRAM_LONG(a)    (0x7E0000 + (a))
#define DOG_X           0x11F2C    /* 16b: the dog's position */
#define DOG_Y           0x11F2E    /* 16b */
#define POWER_BERRIES   0x11F36    /* 8b: power berries eaten */
#define DOG_MOOD        0x11F58    /* 16b: one mood bit */
#define FLAGS_5A        0x11F5A    /* 16b flags: $0200 the fishing rod, $4000 a text box
                                    * is open */
#define FLAGS_5C        0x11F5C    /* 16b flags: the horse's requests ($20, $0400, $0800),
                                    * the mountain's finds of the day ($2000-$8000) */
#define FLAGS_60        0x11F60    /* 16b flags: $02 / $04 a child carried, $10 calls the
                                    * dog, $0200 the watch shows text $046A */
#define FLAGS_66        0x11F66    /* 16b story flags: bits 0-4 the girl married (one
                                    * set: at 17:00 the watch's text $0478) */
#define FLAGS_6A        0x11F6A    /* 16b story flags: $40 a power berry eaten on map $29 */
#define FLAGS_6C        0x11F6C    /* 16b story flags: $01 the exits to map $1C closed,
                                    * $4000 a power berry eaten at home */
#define FLAGS_6E        0x11F6E    /* 16b story flags: $4000 the clock */

/* ROM: the exits (Unk_Table13, $80:B6F5, 8 bytes an exit) */
#define DOORS           0x80B6F5
#define DOOR_DEST       0          /* 8b: the destination map */
#define DOOR_ANIM       2          /* 8b: the map animation (0 = none) */
#define DOOR_FLAGS      3          /* 8b: when the door is shut, + season, festivals */
#define DOOR_ARRIVE_X   4          /* 16b: where he arrives */
#define DOOR_ARRIVE_Y   6          /* 16b */

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

/* JSR UpdatePlayerObject at a: play the player's animation */
static void animate(uint8_t *ram, HmRegs *r, uint16_t at) { jsr(ram, r, at, hm_UpdatePlayerObject); }
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
/* sound effect e of bank 6 (PlaySoundEffect0A) */
static void sound(uint8_t *ram, HmRegs *r, uint8_t e, uint16_t at)
{
  set_a8(r, e);
  wr8(ram, r, SOUND_EFFECT, e);
  set_a8(r, 0x06);
  wr8(ram, r, SOUND_BANK, 0x06);
  jsl(ram, r, at, 0x83, hm_PlaySoundEffect0A);
}
static void event_long(uint8_t *ram, HmRegs *r, uint16_t a, uint16_t x, uint16_t y, uint16_t at)
{
  r->a = a; r->x = x; r->y = y;
  jsl(ram, r, at, 0x84, hm_StartEventScriptLong);
}

/* ─────────────────────────────────────────────────────────────────────────
 * MovePlayer ($81:CB77, JSR, RTS): move the player. Plays his animation,
 * then by his facing (0 down, 1 up, 2 right, 3 left): unless he is
 * jumping (actions 3, $17, $18) or walked by a script ($D2 bit 7), the
 * event objects ahead (CollideEventObjects with the step in $E7 / $E5) or a solid tile
 * (CollideMap) stop him ($1E = 0). When only one of the front corners is
 * blocked he slides sideways ($1E = 1, facing the free side) if the tile
 * allows it (types 0 and $A0-$AF always; otherwise by which half of the
 * tile he is in); when the front is blocked, $0878 / $087A = the tiles hit
 * and, unless one is a door ($C0-$CF), the step is cut to the edge. Then
 * the move, the camera (PlaceCameraOnMapEntry and the scroll of that direction), and
 * every 8 moves a footprint (SnowFootprints; $0907 / $0909 = where).
 * Leftovers: those of the last routine called and the stores after it.
 * ───────────────────────────────────────────────────────────────────────── */
typedef struct Dir {
  uint16_t at_objects, at_map;     /* JSL CollideEventObjects, JSL CollideMap */
  uint16_t at_camera, at_scroll;   /* JSL PlaceCameraOnMapEntry, the scroll */
  Fn *scroll;
  bool y_axis;                     /* moves on Y ($D8; step in $E7, Y = depth) */
  bool minus;                      /* towards smaller coordinates */
  uint8_t slide1, slide2;          /* the facing to slide to per corner */
} Dir;

static const Dir k_dirs[4] = {
  { 0xCBE7, 0xCBF7, 0xCC37, 0xCC3B, hm_UpdateBGOffset,        true,  false, 3, 2 },
  { 0xCC8B, 0xCC9B, 0xCCDB, 0xCCDF, hm_ScrollCameraUp,            true,  true,  3, 2 },
  { 0xCD2F, 0xCD3F, 0xCD7F, 0xCD83, hm_ScrollCameraRight, false, false, 1, 0 },
  { 0xCDD3, 0xCDE3, 0xCE23, 0xCE27, hm_ScrollCameraLeft,            false, true,  1, 0 },
};

static void stop_moving(uint8_t *ram, HmRegs *r)               /* $81:CE53 */
{
  set_dp16(ram, r, STEP_SIZE, 0);
}

/* $81:CE2B: one more step; every 8, a footprint */
static void count_step(uint8_t *ram, HmRegs *r)
{
  uint8_t n = (uint8_t)(rd8(ram, r, STEP_COUNT) + 1);
  wr8(ram, r, STEP_COUNT, n);
  set_a8(r, n);
  r->c = n >= 8;
  if (n != 8) return;
  jsl(ram, r, 0xCE3D, 0x81, hm_SnowFootprints);
  r->a = dp16(ram, r, PLAYER_X);
  wr16(ram, r, FOOTPRINT_X, r->a);
  r->a = dp16(ram, r, PLAYER_Y);
  wr16(ram, r, FOOTPRINT_Y, r->a);
  wr8(ram, r, STEP_COUNT, 0);
}

void hm_MovePlayer(uint8_t *ram, HmRegs *r)
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
      r->a = dp16(ram, r, PLAYER_X); set_dp16(ram, r, COLLIDE_X, r->a);
      r->a = dp16(ram, r, PLAYER_Y); set_dp16(ram, r, COLLIDE_Y, r->a);
      if (dir->y_axis) {
        r->a = 0; set_dp16(ram, r, COLLIDE_STEP_X, 0);
        r->a = dp16(ram, r, STEP_SIZE); set_dp16(ram, r, COLLIDE_STEP_Y, r->a);
      } else {
        r->a = dp16(ram, r, STEP_SIZE); set_dp16(ram, r, COLLIDE_STEP_X, r->a);
        r->a = 0; set_dp16(ram, r, COLLIDE_STEP_Y, 0);
      }
      set_dp16(ram, r, COLLIDE_AHEAD, 0);
      r->a = dp16(ram, r, FACING);
      jsl(ram, r, dir->at_objects, 0x83, hm_CollideEventObjects);
      r->c = 1;                                              /* CMP #0 */
      if (r->a != 0) { stop_moving(ram, r); return; }
      r->a = dp16(ram, r, FACING);
      jsl(ram, r, dir->at_map, 0x83, hm_CollideMap);
      uint16_t depth_reg = dir->y_axis ? r->y : r->x;
      set_dp16(ram, r, MOVE_DEPTH, depth_reg);
      int which = 0;
      r->c = r->a >= 1;
      if (r->a == 1) which = 1;
      else { r->c = r->a >= 2; if (r->a == 2) which = 2; }
      if (which) {
        uint16_t v = dp16(ram, r, which == 1 ? COLLIDE_TILE_2 : COLLIDE_TILE_1);
        uint16_t pos = dp16(ram, r, dir->y_axis ? PLAYER_X : PLAYER_Y);
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
      r->a = dp16(ram, r, COLLIDE_TILE_2); wr16(ram, r, PLAYER_TILE_2, r->a);
      r->a = dp16(ram, r, COLLIDE_TILE_1); wr16(ram, r, PLAYER_TILE_1, r->a);
      r->a = (uint16_t)(r->a - 0x00C0);                      /* SEC; SBC #$C0 */
      r->c = r->a >= 0x0010;
      if (r->a < 0x0010) { stop_moving(ram, r); return; }
      set_dp16(ram, r, MOVE_DEPTH, dir->y_axis ? r->y : r->x);
      uint16_t step = dp16(ram, r, STEP_SIZE), depth = dp16(ram, r, MOVE_DEPTH);
      r->c = step >= depth;
      r->a = (uint16_t)(step - depth);
      set_dp16(ram, r, STEP_SIZE, r->a);
    }
    unsigned coord = dir->y_axis ? PLAYER_Y : PLAYER_X;
    uint16_t pos = dp16(ram, r, coord), step = dp16(ram, r, STEP_SIZE);
    if (dir->minus) { r->c = pos >= step; r->a = (uint16_t)(pos - step); }
    else { uint32_t s = (uint32_t)pos + step; r->c = s > 0xFFFF; r->a = (uint16_t)s; }
    set_dp16(ram, r, coord, r->a);
    jsl(ram, r, dir->at_camera, 0x80, hm_PlaceCameraOnMapEntry);
    jsl(ram, r, dir->at_scroll, 0x80, dir->scroll);
    count_step(ram, r);
    return;
  }
}

/* ─────────────────────────────────────────────────────────────────────────
 * The frame's end. ResetIdleAnimationTimer ($81:C002): standing still goes
 * to PlayerStandStill; otherwise CODE_81C00E clears the idle timer and
 * CODE_81C013 runs the camera (MoveSecondBackground), $1E = 0, and the facing ($DA,
 * $0913) back from $0911. RTL.
 * ───────────────────────────────────────────────────────────────────────── */
static void frame_end(uint8_t *ram, HmRegs *r)                 /* CODE_81C013 */
{
  jsl(ram, r, 0xC013, 0x80, hm_MoveSecondBackground);
  r->a = 0;
  set_dp16(ram, r, STEP_SIZE, 0);
  r->a = rd16(ram, r, PLAYER_TURN);
  set_dp16(ram, r, FACING, r->a);
  wr16(ram, r, PLAYER_FACING, r->a);
}
static void not_idle(uint8_t *ram, HmRegs *r)                  /* CODE_81C00E */
{
  wr8(ram, r, IDLE_TIMER, 0);
  frame_end(ram, r);
}
void hm_ResetIdleAnimationTimer(uint8_t *ram, HmRegs *r)
{
  r->a = dp16(ram, r, ACTION);
  r->c = 1;                                                /* CMP #0 */
  if (r->a == 0) { hm_PlayerStandStill(ram, r); return; }
  not_idle(ram, r);
}
#define END hm_ResetIdleAnimationTimer

/* ─────────────────────────────────────────────────────────────────────────
 * Standing still (PlayerStandStill): $D2 bit 15 off. With $D2 bit 13 only the
 * animation. With bit 14 (nothing to do) the idle timer runs: at $F0 it
 * goes back to $78 and an idle animation starts (action 9, animation
 * $3C + 2 x facing + 0-2 at random). Otherwise the standing animation
 * for the state: holding something (bit 1) $14, bits 4 / 5 / 8 / 11 $B0
 * / $C0 / $4F / $D0, $7F1F60 bits 1-2 $EA, else 0 (+ facing, except $4F).
 * ───────────────────────────────────────────────────────────────────────── */
void hm_PlayerStandStill(uint8_t *ram, HmRegs *r)
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
      hm_PlayerIdleAnimation(ram, r);
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
  r->a = (uint16_t)(hm_bus_rd16(ram, WRAM_LONG(FLAGS_60)) & 0x0006);
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
 * Walking (PlayerWalk, action 1), running (PlayerRun, action 2) and
 * being pushed (PlayerPushed, action $0E): the animation for the state
 * (as standing; walking carrying something $28, running $2C...), the
 * step (1 pixel; 2 running; 3 in deep snow, $D2 bit 4) and the move
 * (MovePlayer); then action 0 and $096D = 0. Running only runs with $D2
 * bit 12, or bit 14 without bit 13; and a step sound every 16 frames
 * while running or in snow ($091A).
 * ───────────────────────────────────────────────────────────────────────── */
typedef struct Gait { uint16_t bit, base; uint8_t step; bool sound; } Gait;

static void after_move(uint8_t *ram, HmRegs *r)                /* $81:C82A */
{
  set_action(ram, r, 0);
  wr8(ram, r, HIT_COUNTER, 0);
  not_idle(ram, r);
}
static void step_sound_and_move(uint8_t *ram, HmRegs *r)       /* $81:C806 */
{
  uint8_t n = (uint8_t)((rd8(ram, r, STEP_SOUND_TIMER) + 1) & 0x0F);
  set_a8(r, n);
  wr8(ram, r, STEP_SOUND_TIMER, n);
  if (!n) sound(ram, r, 0x05, 0xC823);
  jsr(ram, r, 0xC827, hm_MovePlayer);
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
    r->a = (uint16_t)(hm_bus_rd16(ram, WRAM_LONG(FLAGS_60)) & 0x0006);
    pick = r->a ? &g[n] : dflt;
  }
  animation_facing(ram, r, pick->base);
  set_step_size(ram, r, pick->step);
  if (pick->sound) { step_sound_and_move(ram, r); return; }
  jsr(ram, r, at_move, hm_MovePlayer);
  after_move(ram, r);
}

void hm_PlayerWalk(uint8_t *ram, HmRegs *r)
{
  static const Gait g[] = {
    { 0x0002, 0x0028, 1, false }, { 0x0010, 0x00B4, 3, true }, { 0x0020, 0x00C4, 1, false },
    { 0x0800, 0x00D4, 1, false }, { 0, 0x00EE, 1, false },
  };
  static const Gait dflt = { 0, 0x0004, 1, false };
  state_off(ram, r, 0x8000);
  gait(ram, r, g, 4, &dflt, 0xC722);
}

void hm_PlayerRun(uint8_t *ram, HmRegs *r)
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

void hm_PlayerPushed(uint8_t *ram, HmRegs *r)
{
  state_off(ram, r, 0x8000);
  animation(ram, r, 0x0045);
  set_step_size(ram, r, 1);
  jsr(ram, r, 0xC857, hm_MovePlayer);
  after_move(ram, r);
}

/* PlayerState_00_StandWalkRun ($81:BFDC; actions 0, 1, 2, 6, 7, $0E): $D2 bit 2 goes to
 * PlayerEatAnimation; then by the action: running, walking, pushed, or the end. */
void hm_PlayerState_00_StandWalkRun(uint8_t *ram, HmRegs *r)
{
  r->a = (uint16_t)(dp16(ram, r, GAME_STATE) & 0x0004);
  if (r->a) { hm_PlayerEatAnimation(ram, r); return; }
  r->a = dp16(ram, r, ACTION);
  r->c = r->a >= 2;
  if (r->a == 2) { hm_PlayerRun(ram, r); return; }
  r->c = r->a >= 1;
  if (r->a == 1) { hm_PlayerWalk(ram, r); return; }
  r->c = r->a >= 0x0E;
  if (r->a == 0x0E) { hm_PlayerPushed(ram, r); return; }
  END(ram, r);
}

/* PlayerEatAnimation ($D2 bit 2): animation $30 + $0924 (its low byte only), and
 * when it ends action 0 and $D2 bit 2 off. */
void hm_PlayerEatAnimation(uint8_t *ram, HmRegs *r)
{
  if (first_frame(ram, r)) {
    uint16_t sum = (uint16_t)(rd8(ram, r, BREAKFAST) + 0x30);
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

/* PlayerIdleAnimation (action 9, an idle animation): to its end, then action 0. */
void hm_PlayerIdleAnimation(uint8_t *ram, HmRegs *r)
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
void hm_PlayerState_1A_PutChildBack(uint8_t *ram, HmRegs *r)
{
  animation_facing(ram, r, 0x00F2);
  if (play(ram, r, 0xC0C2)) {
    set_action(ram, r, 0);
    r->a = (uint16_t)(hm_bus_rd16(ram, WRAM_LONG(FLAGS_60)) & 0x0004);
    if (!r->a) event_long(ram, r, 0x0014, 0x0045, 0x0000, 0xC0EC);
    else event_long(ram, r, 0x0015, 0x0045, 0x0003, 0xC0FD);
    r->a = (uint16_t)(hm_bus_rd16(ram, WRAM_LONG(FLAGS_60)) & 0xFFF9);
    hm_bus_wr16(ram, WRAM_LONG(FLAGS_60), r->a);
  }
  END(ram, r);
}

/* $17 ($81:C111): facing back from $0913, animation $DC + facing, a step of
 * 1 (MovePlayer); then action 0 and $7F1F5C bit 5. */
void hm_PlayerState_17_PickUpFoal(uint8_t *ram, HmRegs *r)
{
  r->a = rd16(ram, r, PLAYER_FACING);
  set_dp16(ram, r, FACING, r->a);
  wr16(ram, r, PLAYER_TURN, r->a);
  animation_facing(ram, r, 0x00DC);
  set_step_size(ram, r, 1);
  jsr(ram, r, 0xC12C, hm_MovePlayer);
  if (play(ram, r, 0xC12F)) {
    set_action(ram, r, 0);
    long_or(ram, r, WRAM_LONG(FLAGS_5C), 0x0020);
  }
  END(ram, r);
}

/* $18 ($81:C153): animation $E0 + $0911, facing from $0911, a step of 1;
 * then action 0, $D2 bit 4 off and $7F1F5C bit 11. */
void hm_PlayerState_18_PutDownFoal(uint8_t *ram, HmRegs *r)
{
  uint32_t sum = 0x00E0u + rd16(ram, r, PLAYER_TURN);
  r->c = sum > 0xFFFF;
  r->a = (uint16_t)sum;
  wr16(ram, r, ANIMATION, r->a);
  r->a = rd16(ram, r, PLAYER_TURN);
  set_dp16(ram, r, FACING, r->a);
  set_step_size(ram, r, 1);
  jsr(ram, r, 0xC16E, hm_MovePlayer);
  if (play(ram, r, 0xC171)) {
    set_action(ram, r, 0);
    state_off(ram, r, 0x0010);
    long_or(ram, r, WRAM_LONG(FLAGS_5C), 0x0800);
  }
  END(ram, r);
}

/* $16 ($81:C1A1): animation $D8 + facing; then action 0, event $16 / $12
 * at $7F1F2C / $7F1F2E (StartScriptAtTileInFront), $7F1F58 = 1, $0938 = 0. */
void hm_PlayerState_16_ReleaseDog(uint8_t *ram, HmRegs *r)
{
  animation_facing(ram, r, 0x00D8);
  if (play(ram, r, 0xC1AE)) {
    set_action(ram, r, 0);
    r->a = hm_bus_rd16(ram, WRAM_LONG(DOG_X));
    wr16(ram, r, FRONT_X, r->a);
    r->a = hm_bus_rd16(ram, WRAM_LONG(DOG_Y));
    wr16(ram, r, FRONT_Y, r->a);
    r->a = 0x0016; r->x = 0; r->y = 0x0012;
    jsl(ram, r, 0xC1DB, 0x84, hm_StartScriptAtTileInFront);
    r->a = 1;
    hm_bus_wr16(ram, WRAM_LONG(DOG_MOOD), 1);
    wr8(ram, r, DOG_TIMER, 0);
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
void hm_PlayerState_15_BeforeFeelSick(uint8_t *ram, HmRegs *r) { fixed(ram, r, 0x00CC, 0xC1FA, 0x14); }
void hm_PlayerState_14_FeelSick(uint8_t *ram, HmRegs *r) { fixed(ram, r, 0x008F, 0xC21B, 0x00); }
void hm_PlayerState_10_CastLine(uint8_t *ram, HmRegs *r) { fixed(ram, r, 0x0089, 0xC24C, 0x11); }
void hm_PlayerState_12_FishBites(uint8_t *ram, HmRegs *r) { fixed(ram, r, 0x008A, 0xC298, 0x11); }
void hm_PlayerState_0F_HoldRod(uint8_t *ram, HmRegs *r)
{
  animation(ram, r, 0x0088);
  animate(ram, r, 0xC23C);
  END(ram, r);
}

/* $11 ($81:C263): one chance in 256 x 5 to go on to action $12 */
void hm_PlayerState_11_WaitForBite(uint8_t *ram, HmRegs *r)
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
void hm_PlayerState_13_ReelInCatch(uint8_t *ram, HmRegs *r)
{
  if (play(ram, r, 0xC2B1)) {
    set_action(ram, r, 0x000F);
    r->a = 0x0014;
    jsl(ram, r, 0xC2CA, 0x84, hm_SetEventSlotPointer);
    r->y = 0;
    set_a8(r, hm_bus_rd8(ram, hm_dp_long(ram, r, EVENT_SLOT, 0)));
    if (r->a & 0xFF) {
      r->a = 0x0014;
      jsl(ram, r, 0xC2DE, 0x84, hm_FreeEventSlot);
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
void hm_PlayerState_1B_WhistleDog(uint8_t *ram, HmRegs *r) { flag_and_bow(ram, r, WRAM_LONG(FLAGS_60), 0x0010, 0xC34D); }
void hm_PlayerState_0D_WhistleHorse(uint8_t *ram, HmRegs *r) { flag_and_bow(ram, r, WRAM_LONG(FLAGS_5C), 0x0400, 0xC37E); }

/* $0C ($81:C395): looking at the watch: animation $90 + the tool's number
 * (the tool in hand), and the time's text: $046A unless $7F1F6E bit 14 and
 * not $7F1F60 bit 9; then by the hour, 6 to 18: $046B-$0475, at 17 $0476
 * ($0478 with one of $7F1F66 bits 0-4), at 18 $0477, others $046B. At the
 * animation's end action 0, unless $7F1F5A bit 14. */
void hm_PlayerState_0C_ShowToolAndTime(uint8_t *ram, HmRegs *r)
{
  if (first_frame(ram, r)) {
    uint16_t sum = (uint16_t)(rd8(ram, r, HM_TOOL_SELECTED) + 0x90);
    r->c = sum > 0xFF;
    r->a = (uint8_t)sum;                                   /* LDA #0; XBA; ...; REP */
    wr16(ram, r, ANIMATION, r->a);
    uint16_t text = 0x046A;
    r->a = (uint16_t)(hm_bus_rd16(ram, WRAM_LONG(FLAGS_6E)) & 0x4000);
    if (r->a) {
      r->a = (uint16_t)(hm_bus_rd16(ram, WRAM_LONG(FLAGS_60)) & 0x0200);
      if (!r->a) {
        uint8_t h = hm_bus_rd8(ram, WRAM_LONG(HM_HOUR));
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
              r->a = (uint16_t)(hm_bus_rd16(ram, WRAM_LONG(FLAGS_66)) & bit);
              if (r->a) { text = 0x0478; break; }
            }
          }
        }
      }
    }
    r->x = text;
    set_a8(r, 2);
    wr8(ram, r, HM_INPUT_STATE, 2);
    set_a8(r, 0);
    wr8(ram, r, TEXT_BOX_LINES, 0);
    jsl(ram, r, 0xC4C6, 0x83, hm_StartTextBox);
  }
  if (play(ram, r, 0xC4CA)) {
    r->a = (uint16_t)(hm_bus_rd16(ram, WRAM_LONG(FLAGS_5A)) & 0x4000);
    if (!r->a) set_action(ram, r, 0);
  }
  END(ram, r);
}

/* $0B ($81:C4EC): the animation as set; action 0 at its end. */
void hm_PlayerState_0B_Tired(uint8_t *ram, HmRegs *r)
{
  first_frame(ram, r);
  if (play(ram, r, 0xC503)) set_action(ram, r, 0);
  END(ram, r);
}

/* $0A ($81:C51A): using a tool: at the animation's end the tool acts
 * (UseToolInHand); at frame $18 of the use ($091B) its sound (ToolUsedSound1). */
void hm_PlayerState_0A_UseTool(uint8_t *ram, HmRegs *r)
{
  first_frame(ram, r);
  if (play(ram, r, 0xC531)) jsl(ram, r, 0xC53E, 0x82, hm_UseToolInHand);
  uint8_t n = (uint8_t)(rd8(ram, r, TOOL_FRAMES) + 1);
  set_a8(r, n);
  wr8(ram, r, TOOL_FRAMES, n);
  r->c = n >= 0x18;
  if (n == 0x18) jsl(ram, r, 0xC54F, 0x82, hm_ToolUsedSound1);
  END(ram, r);
}

/* $03 ($81:C86B): a jump: the first frame its animation ($0C, or $20
 * carrying, $B8 in snow, $C8 with $D2 bit 5; + facing) and sound effect 6;
 * the step is 1 (2 with bit 5, or every other frame with $D2 bit 9); at
 * the end action 0, $D2 bit 9 off, and facing left also bit 5. */
void hm_PlayerState_03_Jump(uint8_t *ram, HmRegs *r)
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
      wr8(ram, r, JUMP_TOGGLE, 0);
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
      uint8_t t = (uint8_t)(rd8(ram, r, JUMP_TOGGLE) ^ 1);
      set_a8(r, t);
      wr8(ram, r, JUMP_TOGGLE, t);
      if (t) set_step_size(ram, r, 2);
    }
  }
  jsr(ram, r, 0xC951, hm_MovePlayer);
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
 * (PutItemDown) the first frame; at the end action 0, holding ($D2 bit 1)
 * and $091E = the item. */
void hm_PlayerState_04_PickUp(uint8_t *ram, HmRegs *r)
{
  if (first_frame(ram, r)) {
    animation_facing(ram, r, 0x0010);
    jsl(ram, r, 0xC9B3, 0x81, hm_PutItemDown);
  }
  if (play(ram, r, 0xC9B7)) {
    set_action(ram, r, 0);
    state_on(ram, r, 0x0002);
    set_a8(r, rd8(ram, r, HM_ITEM_ON_HAND));
    wr8(ram, r, SHOWN_ITEM, (uint8_t)r->a);
  }
  END(ram, r);
}

/* $05 ($81:C9DF): throwing: animation $1C + facing and sound effect 7; at
 * the end action 0 and the hands free ($D2 bit 1 off). */
void hm_PlayerState_05_Throw(uint8_t *ram, HmRegs *r)
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
void hm_PlayerState_08_Eat(uint8_t *ram, HmRegs *r)
{
  if (first_frame(ram, r)) {
    uint32_t sum = (uint32_t)rd8(ram, r, ITEM) + 0x33;
    r->c = sum > 0xFFFF;
    r->a = (uint16_t)sum;
    wr16(ram, r, ANIMATION, r->a);
  }
  if (!play(ram, r, 0xCA60)) { END(ram, r); return; }
  uint8_t item = rd8(ram, r, ITEM);
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
  uint8_t n = (uint8_t)(hm_bus_rd8(ram, WRAM_LONG(POWER_BERRIES)) + 1);
  set_a8(r, n);
  hm_bus_wr8(ram, WRAM_LONG(POWER_BERRIES), n);
  r->a = 0x000A;
  jsl(ram, r, 0xCABB, 0x83, hm_AddPlayerHappiness);
  uint16_t s = (uint16_t)(rd8(ram, r, HM_MAX_STAMINA) + 0x0A);
  r->c = s > 0xFF;
  set_a8(r, (uint8_t)s);
  wr8(ram, r, HM_MAX_STAMINA, (uint8_t)s);
  set_action(ram, r, 0);
  state_off(ram, r, 0x0002);
  uint8_t map = *hm_dp(ram, r, HM_MAP_ID);
  set_a8(r, map);
  bool house = false;
  for (uint8_t k = 0x15; k <= 0x17 && !house; k++) {
    r->c = map >= k;
    house = map == k;
  }
  if (house) long_or(ram, r, WRAM_LONG(FLAGS_6C), 0x4000);
  else {
    r->c = map >= 0x29;
    if (map == 0x29) long_or(ram, r, WRAM_LONG(FLAGS_6A), 0x0040);
  }
  END(ram, r);
}

/* ─────────────────────────────────────────────────────────────────────────
 * CheckMountainFinds ($80:9D0B, JSL, RTL): the mountain (maps $10-$13), on days
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
  r->a = rd16(ram, r, PLAYER_TILE_1);
  r->c = r->a >= tile;
  if (r->a == tile) return true;
  r->a = rd16(ram, r, PLAYER_TILE_2);
  r->c = r->a >= tile;
  return r->a == tile;
}
static void mountain_find(uint8_t *ram, HmRegs *r, uint8_t season, uint16_t bit,
                          uint16_t a, uint16_t x, uint16_t at_rng, uint16_t at_event)
{
  uint8_t s = hm_bus_rd8(ram, WRAM_LONG(HM_SEASON));
  set_a8(r, s);
  if (season == 0) { if (s) return; }
  else { r->c = s >= season; if (s != season) return; }
  set_a8(r, 0x10);
  jsl(ram, r, at_rng, 0x80, hm_RNGReturn0toA);
  if (r->a & 0xFF) return;
  r->a = (uint16_t)(hm_bus_rd16(ram, WRAM_LONG(FLAGS_5C)) & bit);
  if (r->a) return;
  long_or(ram, r, WRAM_LONG(FLAGS_5C), bit);
  event_long(ram, r, a, x, 0x0000, at_event);
}

void hm_CheckMountainFinds(uint8_t *ram, HmRegs *r)
{
  uint8_t map = *hm_dp(ram, r, HM_MAP_ID);
  set_a8(r, map);
  r->c = map >= 0x04;
  if (map < 0x04) return;
  r->c = map >= 0x10;
  if (map < 0x10) return;
  r->c = map >= 0x14;
  if (map >= 0x14) return;
  r->a = (uint16_t)(rd16(ram, r, WEATHER_FLAGS) & 0x001A);
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
  r->a = (uint16_t)(hm_bus_rd16(ram, WRAM_LONG(FLAGS_5A)) & 0x0200);
  if (!r->a) return;
  static const uint16_t busy[] = { 0x0002, 0x0010, 0x0800 };
  for (unsigned i = 0; i < 3; i++) {
    r->a = (uint16_t)(dp16(ram, r, GAME_STATE) & busy[i]);
    if (r->a) return;
  }
  r->a = rd16(ram, r, PLAYER_TILE_1);
  r->c = r->a >= 0x00F8;
  if (r->a != 0x00F8) return;
  r->a = rd16(ram, r, PLAYER_TILE_2);
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
 * CheckDoors ($80:9A64, JSL, RTL): the doors. The tiles under the player
 * ($0878 / $087A) are both of the types $C0-$CF (an exit) or the routine
 * leaves; a type from $D0 goes on to CheckMountainFinds (the mountain). Not while
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

static uint8_t door(uint8_t *ram, uint16_t x) { return hm_bus_rd8(ram, DOORS + x); }
static bool cmp8(HmRegs *r, uint8_t a, uint8_t v) { r->c = a >= v; return a == v; }

/* LDA $0878; ASL; ASL; TAY; LDA #0; XBA; LDA [$0D],Y; ASL x3; TAX */
static uint16_t door_entry(uint8_t *ram, HmRegs *r)
{
  uint16_t tile = rd16(ram, r, PLAYER_TILE_1);
  r->y = (uint16_t)(tile << 2);
  r->a = (uint16_t)(hm_bus_rd8(ram, hm_dp_long(ram, r, TILE_PROPS, r->y)) << 3);
  r->c = 0;                                       /* a byte x 8 */
  r->x = r->a;
  return r->x;
}
static void door_shut(uint8_t *ram, HmRegs *r)
{
  state_off(ram, r, 0x0080);
  wr16(ram, r, PLAYER_TILE_1, 0);
  wr16(ram, r, PLAYER_TILE_2, 0);
  wr8(ram, r, DOOR_STEP, 0);
}
/* the flags 1-5 and 7 (C as the CMPs leave it: the closed door keeps it) */
static bool shut_by_flags(uint8_t *ram, HmRegs *r, uint8_t flags)
{
  uint8_t hour = hm_bus_rd8(ram, WRAM_LONG(HM_HOUR)), weekday = hm_bus_rd8(ram, WRAM_LONG(HM_WEEKDAY));
  uint8_t season = hm_bus_rd8(ram, WRAM_LONG(HM_SEASON)), day = hm_bus_rd8(ram, WRAM_LONG(HM_DAY));
  if (flags & 0x02) { r->c = hour >= 0x11; if (r->c) return true; }       /* from 17:00 */
  if (flags & 0x04) { r->c = hour >= 0x11; if (!r->c) return true; }      /* before 17:00 */
  if ((flags & 0x08) && weekday != 0 && !cmp8(r, weekday, 0x06)) return true;
  if (flags & 0x10) {
    if (cmp8(r, season, 0x03) && cmp8(r, day, 0x0A)) return true;
    if (cmp8(r, weekday, 0x06)) return true;
  }
  if ((flags & 0x20) && weekday == 0) return true;
  if ((flags & 0x80) && (rd16(ram, r, WEATHER_FLAGS) & 0x0010)) return true;
  return false;
}
/* the first frame on the exit; X = the entry */
static void door_open(uint8_t *ram, HmRegs *r, uint16_t x)
{
  uint8_t flags = door(ram, x + DOOR_FLAGS);
  uint8_t dest = rd8(ram, r, TRANSITION_MAP);
  if (flags & 0x01) dest = (uint8_t)(dest + hm_bus_rd8(ram, WRAM_LONG(HM_SEASON)));
  if (flags & 0x40) {                                     /* the festivals */
    uint8_t day = hm_bus_rd8(ram, WRAM_LONG(HM_DAY)), season = hm_bus_rd8(ram, WRAM_LONG(HM_SEASON));
    if (day == 0x01) { if (season == 0) dest = 0x3A; }
    else if (day == 0x18) { if (season == 3) dest = 0x39; }
    else if (day >= 0x0A && day < 0x0D) dest = (uint8_t)(dest + 4);
  }
  wr8(ram, r, TRANSITION_MAP, dest);
  uint8_t map = *hm_dp(ram, r, HM_MAP_ID);
  if (cmp8(r, map, 0x0B) || shut_by_flags(ram, r, flags)) {
    door_shut(ram, r);
    return;
  }
  wr8(ram, r, HM_TIME_RUNNING, 0);                        /* the clock stops */
  r->x = (uint16_t)(x + DOOR_ARRIVE_Y);
  wr16(ram, r, TRANSITION_X, hm_bus_rd16(ram, DOORS + x + DOOR_ARRIVE_X));
  wr16(ram, r, TRANSITION_Y, hm_bus_rd16(ram, DOORS + x + DOOR_ARRIVE_Y));
  uint8_t animation = *hm_dp(ram, r, DOOR_MAP_ANIM);
  r->a = animation;                                       /* LDA #0; XBA; LDA $92 */
  r->c = 1;                                               /* CMP #0 */
  if (animation) jsl(ram, r, 0x9C56, 0x81, hm_StartMapAnimation);
}

void hm_CheckDoors(uint8_t *ram, HmRegs *r)
{
  static const uint16_t under[] = { PLAYER_TILE_1, PLAYER_TILE_2 };
  for (unsigned i = 0; i < 2; i++) {
    r->a = rd16(ram, r, under[i]);
    r->c = r->a >= 0x00C0;
    if (!r->c) return;
    r->c = r->a >= 0x00D0;
    if (r->c) { hm_CheckMountainFinds(ram, r); return; }
  }
  r->a = (uint16_t)(dp16(ram, r, GAME_STATE) & 0x0010);
  if (r->a) return;
  r->a = (uint16_t)(hm_bus_rd16(ram, WRAM_LONG(FLAGS_60)) & 0x0006);
  if (r->a) return;
  r->a = (uint16_t)(hm_bus_rd16(ram, WRAM_LONG(FLAGS_6C)) & 0x0001);
  if (r->a) {
    uint16_t x = door_entry(ram, r);
    set_a8(r, door(ram, x));
    if (cmp8(r, (uint8_t)r->a, 0x1C)) return;
  }
  set_action(ram, r, 0x0000);
  state_on(ram, r, 0x0080);

  uint8_t step = rd8(ram, r, DOOR_STEP);
  set_a8(r, step);
  bool walk = false;
  if (cmp8(r, step, 0x01)) {
    r->a = rd16(ram, r, MAP_ANIM_RUNNING);
    if (r->a) return;
    walk = true;
  } else if (step >= 0x02) walk = true;
  if (!walk) {                                            /* the first frame */
    set_a8(r, (uint8_t)(step + 1));
    wr8(ram, r, DOOR_STEP, (uint8_t)(step + 1));
    uint16_t x = door_entry(ram, r);
    wr8(ram, r, TRANSITION_MAP, door(ram, x));            /* the destination */
    *hm_dp(ram, r, DOOR_MAP_ANIM) = door(ram, x + DOOR_ANIM);
    r->x = (uint16_t)(x + DOOR_FLAGS);
    door_open(ram, r, x);
    return;
  }

  step = rd8(ram, r, DOOR_STEP);
  set_a8(r, step);
  if (!cmp8(r, step, 0x0D)) {                             /* walking out */
    set_a8(r, (uint8_t)(step + 1));
    wr8(ram, r, DOOR_STEP, (uint8_t)(step + 1));
    set_action(ram, r, 0x0001);
    uint16_t facing = dp16(ram, r, FACING);
    r->a = facing;
    if (facing > 2) { r->c = facing >= 2; facing = 3; }
    else r->c = 1;
    set_dp16(ram, r, FACING, facing);
    r->a = facing;
    wr16(ram, r, PLAYER_TURN, facing);
    return;
  }
  uint8_t map = rd8(ram, r, HM_MAP_ID);                   /* LDA $0022 (absolute) */
  set_a8(r, map);
  r->c = map >= 0x04;
  if (!r->c) jsl(ram, r, 0x9CDC, 0x82, hm_CopyCurrentMapToFarmMap);
  map = *hm_dp(ram, r, HM_MAP_ID);
  set_a8(r, map);
  r->c = map >= 0x0C;
  if (r->c && (r->c = map >= 0x10, !r->c)) {
    uint8_t hour = hm_bus_rd8(ram, WRAM_LONG(HM_HOUR));
    set_a8(r, hour);
    if (!cmp8(r, hour, 0x12)) {
      set_a8(r, (uint8_t)(hour + 1));
      hm_bus_wr8(ram, WRAM_LONG(HM_HOUR), (uint8_t)(hour + 1));
      jsl(ram, r, 0x9CFB, 0x82, hm_HaveLunch);
    }
  }
  r->a = (uint16_t)(rd16(ram, r, WEATHER_FLAGS) | 0x4000);
  wr16(ram, r, WEATHER_FLAGS, r->a);
}

/* ─────────────────────────────────────────────────────────────────────────
 * $1C ($81:C061): swapping the tool in hand and the one in the bag. The
 * first frame ($D2 bit 15 off, then on) the animation is $90 + the bag's
 * tool; when it is over: action 0, the two tools change places ($0921 /
 * $0923) and the new tool's noises are loaded (ToolUsedSound2, which waits
 * a frame when it changes instruments).
 * ───────────────────────────────────────────────────────────────────────── */
void hm_PlayerState_1C_SwapTools(uint8_t *ram, HmRegs *r)
{
  uint8_t entry = r->p;
  r->a = (uint16_t)(dp16(ram, r, GAME_STATE) & 0x8000);
  if (!r->a) {
    state_on(ram, r, 0x8000);
    uint8_t bag = rd8(ram, r, HM_TOOL_BACKPACK);
    uint32_t sum = (uint32_t)bag + 0x90;                   /* LDA #0; XBA; LDA; CLC; ADC #$90 */
    r->c = sum > 0xFF;
    r->a = (uint8_t)sum;
    wr16(ram, r, ANIMATION, r->a);                         /* REP #$20; STA */
  }
  animate(ram, r, 0xC08A);
  if (anim_over(ram, r)) {
    set_action(ram, r, 0);
    uint8_t hand = rd8(ram, r, HM_TOOL_SELECTED), bag = rd8(ram, r, HM_TOOL_BACKPACK);
    hm_push8(ram, r, hand);                                /* SEP #$20; PHA */
    set_a8(r, bag);
    wr8(ram, r, HM_TOOL_SELECTED, bag);
    set_a8(r, hm_pull8(ram, r));                           /* PLA */
    wr8(ram, r, HM_TOOL_BACKPACK, hand);
    r->p = entry;
    jsl(ram, r, 0xC0AE, 0x82, hm_ToolUsedSound2);
    r->p = entry;
  }
  END(ram, r);
}

/* ─────────────────────────────────────────────────────────────────────────
 * PlayerFrame ($81:BFB7, JSL, RTL): the player each frame (see the top of
 * this file). Without $D2 bit 0 only the frame's end; with bit 6 the
 * pending action (PlayerInteractionSelector); otherwise the step of action
 * $D4 (JMP (DATA16_81C027,X): 29 words; $19 goes straight to the frame's
 * end).
 * ───────────────────────────────────────────────────────────────────────── */
static void not_idle_step(uint8_t *ram, HmRegs *r) { not_idle(ram, r); }
static Fn *const PLAYER_STEPS[29] = {
  hm_PlayerState_00_StandWalkRun, hm_PlayerState_00_StandWalkRun, hm_PlayerState_00_StandWalkRun, hm_PlayerState_03_Jump, hm_PlayerState_04_PickUp,
  hm_PlayerState_05_Throw, hm_PlayerState_00_StandWalkRun, hm_PlayerState_00_StandWalkRun, hm_PlayerState_08_Eat, hm_PlayerIdleAnimation,
  hm_PlayerState_0A_UseTool, hm_PlayerState_0B_Tired, hm_PlayerState_0C_ShowToolAndTime, hm_PlayerState_0D_WhistleHorse, hm_PlayerState_00_StandWalkRun,
  hm_PlayerState_0F_HoldRod, hm_PlayerState_10_CastLine, hm_PlayerState_11_WaitForBite, hm_PlayerState_12_FishBites, hm_PlayerState_13_ReelInCatch,
  hm_PlayerState_14_FeelSick, hm_PlayerState_15_BeforeFeelSick, hm_PlayerState_16_ReleaseDog, hm_PlayerState_17_PickUpFoal, hm_PlayerState_18_PutDownFoal,
  not_idle_step, hm_PlayerState_1A_PutChildBack, hm_PlayerState_1B_WhistleDog, hm_PlayerState_1C_SwapTools };

static void step_of_action(uint8_t *ram, HmRegs *r)        /* CODE_81BFD3 */
{
  uint16_t action = dp16(ram, r, ACTION);
  r->c = (action & 0x8000) != 0;                           /* ASL; TAX */
  r->a = (uint16_t)(action << 1);
  r->x = r->a;
  if (action < 29) PLAYER_STEPS[action](ram, r);
}

void hm_PlayerFrame(uint8_t *ram, HmRegs *r)
{
  r->a = (uint16_t)(dp16(ram, r, GAME_STATE) & 0x0001);
  if (!r->a) { hm_ResetIdleAnimationTimer(ram, r); return; }
  r->a = (uint16_t)(dp16(ram, r, GAME_STATE) & 0x0040);
  if (r->a) { hm_PlayerInteractionSelector(ram, r); return; }
  step_of_action(ram, r);
}

/* ─────────────────────────────────────────────────────────────────────────
 * PlayerInteractionSelector ($81:D570, JMP from PlayerFrame): the pending
 * action $096E (JSR through PlayerActionPointerTable, $81:D593: 88 words,
 * actions.c and interact.c); then the idle timer is cleared and, if the
 * time is stopped for the day's end ($0973 bit 1), only the frame's end
 * follows, else the step of the player's action too.
 * ───────────────────────────────────────────────────────────────────────── */
static Fn *const PLAYER_ACTIONS[0x58] = {
  hm_ActionNothing, hm_GetFenceFromStack, hm_ActionNothing, hm_PlayerAction_04_Jingle,
  hm_ActionNothing, hm_PlayerAction_06_MapAnimation11, hm_PlayerAction_07_MapAnimation12, hm_ActionNothing,
  hm_ActionNothing, hm_ActionNothing, hm_ActionNothing, hm_ActionNothing,
  hm_ActionNothing, hm_ActionNothing, hm_ActionNothing, hm_PlayerAction_0F_Tv,
  hm_PlayerAction_10, hm_PlayerAction_11, hm_PlayerAction_12, hm_PlayerAction_13,
  hm_PlayerAction_14, hm_PlayerAction_15, hm_ActionNothing, hm_ActionNothing,
  hm_ActionNothing, hm_ActionNothing, hm_ActionNothing, hm_ActionNothing,
  hm_ActionNothing, hm_PlayerAction_1D, hm_PlayerAction_1E, hm_ActionNothing,
  hm_ActionNothing, hm_ActionNothing, hm_ActionNothing, hm_ActionNothing,
  hm_ActionNothing, hm_ActionNothing, hm_PlayerAction_26, hm_PlayerAction_27,
  hm_PlayerAction_28, hm_PlayerAction_29, hm_PlayerAction_2A, hm_PlayerAction_2B,
  hm_PlayerAction_2C, hm_PlayerAction_2D, hm_PlayerAction_2E, hm_PlayerAction_2F,
  hm_PlayerAction_30, hm_PlayerAction_31, hm_PlayerAction_32, hm_PlayerAction_33,
  hm_PlayerAction_34, hm_PlayerAction_35, hm_PlayerAction_36, hm_PlayerAction_37,
  hm_PlayerAction_38, hm_PlayerAction_39, hm_PlayerAction_3A, hm_PlayerAction_3B,
  hm_PlayerAction_3C, hm_PlayerAction_3D, hm_PlayerAction_3E, hm_PlayerAction_3F,
  hm_PlayerAction_40, hm_PlayerAction_41, hm_PlayerAction_42, hm_PlayerAction_43,
  hm_PlayerAction_44, hm_PlayerAction_45, hm_PlayerAction_46, hm_PlayerAction_47,
  hm_PlayerAction_48, hm_PlayerAction_49, hm_PlayerAction_4A, hm_PlayerAction_4B,
  hm_PlayerAction_4C, hm_PlayerAction_4D, hm_PlayerAction_4E, hm_PlayerAction_4F,
  hm_PlayerAction_50, hm_PlayerAction_51, hm_PlayerAction_52, hm_PlayerAction_53,
  hm_PlayerAction_54, hm_PlayerAction_55, hm_PlayerAction_56, hm_PlayerAction_57,
};

void hm_PlayerInteractionSelector(uint8_t *ram, HmRegs *r)
{
  uint8_t action = rd8(ram, r, INTERACTION);               /* LDA #0; XBA; LDA; ASL */
  r->c = action >> 7;
  r->a = (uint8_t)(action << 1);
  r->x = r->a;                                             /* REP #$20; TAX */
  if (action < 0x58) jsr(ram, r, 0xD57E, PLAYER_ACTIONS[action]);
  wr8(ram, r, IDLE_TIMER, 0);                              /* SEP #$20; STZ $0925 */
  uint8_t time = rd8(ram, r, HM_TIME_RUNNING);
  set_a8(r, time & 0x02);
  if (time & 0x02) not_idle(ram, r);
  else step_of_action(ram, r);
}
