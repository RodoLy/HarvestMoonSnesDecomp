/*
 * playerinput.c — the buttons while walking (input mode 1).
 *
 * PlayerMovementInputs ($84:C6B7) runs every frame while the player can
 * move. HM-Decomp writes it as many labels (PlayerActionKeysPressed,
 * PlayerActionDown..., PlayerKeyA..., CODE_84C7D6...) joined by JMPs: it is
 * one routine with many exits. The arrows set the action "walk" ($D4 = 1)
 * and the direction ($DA: 0 down, 1 up, 2 right, 3 left; HM-Decomp's button
 * names swap left and right), then one button is looked at, in this order:
 * A, B, Select, X, R, L, Y.
 *
 * During events ($D2 bit $1000) the pad is not read: the event's script
 * presses the buttons in $08FD instead (same bits).
 *
 * Pad bits ($0124 held, $0128 new): B $8000, Y $4000, Select $2000,
 * Start $1000, up $0800, down $0400, left $0200, right $0100, A $0080,
 * X $0040, L $0020, R $0010.
 *
 * Code that nothing runs: after each arrow's JMP there are a few dead
 * instructions, and the block at $84:C852-$84:C934 (it would turn $0911
 * into actions 6 / 7) is never called.
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

#define GAME_STATE   0xD2
#define ACTION       0xD4
#define PLAYER_X     0xD6
#define PLAYER_Y     0xD8
#define FACING       0xDA
#define PAD_HELD     0x0124
#define PAD_NEW      0x0128
#define PAD_SCRIPT   0x08FD
#define TURN         0x0911   /* the direction being tried */
#define FRONT_X      0x0985
#define FRONT_Y      0x0987
#define FLAGS_5C     0x7F1F5C
#define FLAGS_60     0x7F1F60
#define OWNED        0x7F1F68   /* bit $80 the dog, $100 the horse */
#define CAN_INTERACT 0x84C7B6   /* PlayerActionCanInteract: 1 = no */

enum { K_B = 0x8000, K_Y = 0x4000, K_SELECT = 0x2000, K_UP = 0x0800, K_DOWN = 0x0400,
       K_LEFT = 0x0200, K_RIGHT = 0x0100, K_A = 0x0080, K_X = 0x0040, K_L = 0x0020,
       K_R = 0x0010 };

static uint32_t db_addr(const HmRegs *r, uint16_t a) { return (uint32_t)r->db << 16 | a; }
static uint16_t dp16(uint8_t *ram, const HmRegs *r, unsigned a) { return hm_dp_r16(ram, r, a); }
static void set16(uint8_t *ram, const HmRegs *r, unsigned a, uint16_t v) { hm_dp_w16(ram, r, a, v); }
static uint16_t rdw(uint8_t *ram, const HmRegs *r, uint16_t a) { return hm_bus_rd16(ram, db_addr(r, a)); }
static void wrw(uint8_t *ram, const HmRegs *r, uint16_t a, uint16_t v) { hm_bus_wr16(ram, db_addr(r, a), v); }
static void jsl(uint8_t *ram, HmRegs *r, uint16_t ret, uint8_t bank, void (*fn)(uint8_t *, HmRegs *))
{
  hm_jsl(ram, r, ret, bank);
  fn(ram, r);
  hm_rtl(ram, r);
}
/* LDA $D2; AND #bit (A = the result) */
static bool state(uint8_t *ram, HmRegs *r, uint16_t bit)
{
  r->a = dp16(ram, r, GAME_STATE) & bit;
  return r->a != 0;
}
/* LDA #v; STA $D4 */
static void act(uint8_t *ram, HmRegs *r, uint16_t v) { r->a = v; set16(ram, r, ACTION, v); }
/* CMP #k: C = A >= k */
static bool is(HmRegs *r, uint16_t k) { r->c = r->a >= k; return r->a == k; }

static void keys_pressed(uint8_t *ram, HmRegs *r);
static void key_a(uint8_t *ram, HmRegs *r);
static void key_b(uint8_t *ram, HmRegs *r);
static void key_y(uint8_t *ram, HmRegs *r);

/* the busy states: holding an item ($02), carrying the dog ($10... see A),
 * and $800 — none of R, L, Select, X, Y works then */
static bool busy(uint8_t *ram, HmRegs *r)
{
  return state(ram, r, 0x0002) || state(ram, r, 0x0010) || state(ram, r, 0x0800);
}
static void key_r(uint8_t *ram, HmRegs *r)                 /* whistle for the horse */
{
  if (busy(ram, r)) return;
  r->a = hm_bus_rd16(ram, OWNED) & 0x0100;
  if (r->a) act(ram, r, 0x000D);
}
static void key_l(uint8_t *ram, HmRegs *r)                 /* whistle for the dog */
{
  if (busy(ram, r)) return;
  r->a = hm_bus_rd16(ram, OWNED) & 0x0080;
  if (r->a) act(ram, r, 0x001B);
}
static void key_select(uint8_t *ram, HmRegs *r)            /* show the item in hand */
{
  if (!busy(ram, r)) act(ram, r, 0x000C);
}
static void key_x(uint8_t *ram, HmRegs *r)
{
  if (!busy(ram, r)) act(ram, r, 0x001C);
}

/* ─────────────────────────────────────────────────────────────────────────
 * Y: the tool, or fishing (the rod: $0F ready to cast -> $10 casting;
 * $11 waiting -> $0F reel in empty; $12 a bite -> $13 reel in, with one of
 * four catch animations $8B-$8E at random).
 * ───────────────────────────────────────────────────────────────────────── */
static void key_y(uint8_t *ram, HmRegs *r)
{
  if (busy(ram, r)) return;
  r->a = dp16(ram, r, ACTION);
  if (is(r, 0x000F)) { act(ram, r, 0x0010); return; }
  r->a = dp16(ram, r, ACTION);
  if (is(r, 0x0011)) { act(ram, r, 0x000F); return; }
  r->a = dp16(ram, r, ACTION);
  if (is(r, 0x0012)) {
    act(ram, r, 0x0013);
    r->a = (uint16_t)((r->a & 0xFF00) | 0x03);
    jsl(ram, r, 0xCDFB, 0x80, hm_RNGReturn0toA);
    uint32_t s = (uint32_t)(uint8_t)r->a + 0x8B;          /* XBA; LDA #0; XBA; CLC; ADC */
    r->c = s > 0xFFFF;
    r->a = (uint16_t)s;
    wrw(ram, r, 0x0901, r->a);
    return;
  }
  r->a = hm_bus_rd16(ram, FLAGS_60) & 0x8000;
  if (r->a) return;
  uint8_t tool = hm_bus_rd8(ram, db_addr(r, 0x0921));
  r->a = (uint16_t)((r->a & 0xFF00) | tool);
  if (!tool) return;
  act(ram, r, 0x000A);                                     /* using a tool */
  jsl(ram, r, 0xCDDB, 0x82, hm_UNK_ToolUsed);
}

/* ─────────────────────────────────────────────────────────────────────────
 * B: run; or jump, when the tile in front has property bit $20 (a short
 * jump, 28 pixels) or $40 (a long one, 33 pixels, $D2 bit $200) and there
 * is room to land (CEEEE: the map; CBBBB: objects).
 * ───────────────────────────────────────────────────────────────────────── */
static void tile_in_front(uint8_t *ram, HmRegs *r, uint16_t a, uint16_t x, uint16_t y,
                          uint16_t ret_front, uint16_t prop, uint16_t ret_prop)
{
  r->a = a; r->x = x; r->y = y;
  jsl(ram, r, ret_front, 0x81, hm_CalculateTileinFront);
  r->x = rdw(ram, r, FRONT_X);
  r->y = rdw(ram, r, FRONT_Y);
  r->a = prop;
  jsl(ram, r, ret_prop, 0x82, hm_UNK_CheckTileProperty);
}
/* $DF / $E1 = the player, no step, $E3 = how far */
static void from_player(uint8_t *ram, HmRegs *r, uint16_t far)
{
  set16(ram, r, 0xE3, far);
  set16(ram, r, 0xDF, dp16(ram, r, PLAYER_X));
  set16(ram, r, 0xE1, dp16(ram, r, PLAYER_Y));
  set16(ram, r, 0xE5, 0);
  set16(ram, r, 0xE7, 0);
}
static void key_b(uint8_t *ram, HmRegs *r)
{
  if (!state(ram, r, 0x0800)) {
    tile_in_front(ram, r, 0, 7, 7, 0xCA35, 2, 0xCA44);
    uint8_t p = (uint8_t)(r->a & 0x60);
    r->a = (uint16_t)((r->a & 0xFF00) | p);
    if (p) {
      uint16_t far;
      if (p & 0x20) {
        r->a = (uint16_t)((r->a & 0xFF00) | 0x20);
        far = 0x001C;
      } else {
        r->a = (uint16_t)(r->a & 0xFF00);
        set16(ram, r, GAME_STATE, (uint16_t)(dp16(ram, r, GAME_STATE) | 0x0200));
        far = 0x0021;
      }
      from_player(ram, r, far);
      r->a = dp16(ram, r, FACING);
      jsl(ram, r, 0xCA7B, 0x83, hm_CEEEE);
      if (r->a == 0) {
        set16(ram, r, 0xE3, 0x0020);
        r->a = dp16(ram, r, FACING);
        jsl(ram, r, 0xCA8C, 0x83, hm_CBBBB);
        if (is(r, 0)) { act(ram, r, 0x0003); return; }    /* jump */
      }
    }
  }
  act(ram, r, 0x0002);                                     /* run */
}

/* ─────────────────────────────────────────────────────────────────────────
 * A.
 * ───────────────────────────────────────────────────────────────────────── */
/* CODE_84CD16 (JSL, RTL): is the way in direction $0911 blocked? The map
 * (CEEEE), a tile $C0-$CF at either corner, or an object (CBBBB).
 * A = 1 blocked, 0 free. */
static void way_blocked(uint8_t *ram, HmRegs *r)
{
  from_player(ram, r, 0x0010);
  r->a = rdw(ram, r, TURN);
  jsl(ram, r, 0xCD2F, 0x83, hm_CEEEE);
  bool blocked = r->a != 0;
  if (!blocked) {
    for (unsigned at = 0xE9; at <= 0xEB && !blocked; at += 2) {
      uint16_t v = dp16(ram, r, at);
      r->a = (uint16_t)(v - 0xC0);
      r->c = v >= 0xC0;                                    /* SEC; SBC */
      r->c = r->a >= 0x10;                                 /* CMP #$10 */
      blocked = !r->c;
    }
  }
  if (!blocked) {
    set16(ram, r, 0xE3, 0x0010);
    r->a = rdw(ram, r, TURN);
    jsl(ram, r, 0xCD60, 0x83, hm_CBBBB);
    blocked = !is(r, 0);
  }
  r->a = blocked ? 1 : 0;
}
/* with something in the arms ($D2 bit $10): put it down on a free side.
 * The two directions tried for each facing, and where to look if neither
 * is free (then the player only turns: $DA = $0911 = facing). */
static void put_down(uint8_t *ram, HmRegs *r)
{
  static const struct { uint16_t first, second, ret1, ret2; } T[4] = {
    { 2, 3, 0xCC4B, 0xCC5C }, { 3, 2, 0xCC7A, 0xCC88 },
    { 0, 1, 0xCCA3, 0xCCB1 }, { 0, 1, 0xCCCB, 0xCCD9 },
  };
  r->a = dp16(ram, r, FACING);
  unsigned f = 0;
  if (!is(r, 0)) { if (is(r, 1)) f = 1; else if (is(r, 2)) f = 2; else if (is(r, 3)) f = 3; }
  bool free_side = false;
  for (unsigned k = 0; k < 2 && !free_side; k++) {
    r->a = k ? T[f].second : T[f].first;
    wrw(ram, r, TURN, r->a);
    hm_jsl(ram, r, k ? T[f].ret2 : T[f].ret1, 0x84);
    way_blocked(ram, r);
    hm_rtl(ram, r);
    free_side = r->a == 0;
  }
  if (!free_side) {
    r->a = (uint16_t)f;
    set16(ram, r, FACING, r->a);
    wrw(ram, r, TURN, r->a);
    return;
  }
  act(ram, r, 0x0018);
  wrw(ram, r, FRONT_X, dp16(ram, r, PLAYER_X));
  wrw(ram, r, FRONT_Y, dp16(ram, r, PLAYER_Y));
  r->a = 0x0017; r->x = 0; r->y = 0x0014;
  jsl(ram, r, 0xCD07, 0x84, hm_SUB_8480F8);               /* the putting-down script */
  wrw(ram, r, 0x0878, 0);
  wrw(ram, r, 0x087A, 0);
  hm_bus_wr8(ram, db_addr(r, 0x0939), 0);
}
/* carrying the dog ($D2 bit $800): let it go in front, if no object is in
 * the way and three points ahead (CalculateTileinFront with three offsets)
 * have no solid property bits and are not tile $FF (the last one, also not
 * $E1 nor $C0-$CF) */
static void let_dog_go(uint8_t *ram, HmRegs *r)
{
  static const struct { uint16_t a, xy, ret_front, ret_prop; } T[3] = {
    { 0, 7, 0xCB5C, 0xCB6B }, { 1, 0x0A, 0xCB8B, 0xCB9A }, { 1, 0, 0xCBBA, 0xCBC9 },
  };
  from_player(ram, r, 0x0010);
  r->a = dp16(ram, r, FACING);
  jsl(ram, r, 0xCB43, 0x83, hm_CBBBB);
  if (!is(r, 0)) return;
  for (unsigned i = 0; i < 3; i++) {
    tile_in_front(ram, r, T[i].a, T[i].xy, T[i].xy, T[i].ret_front, 2, T[i].ret_prop);
    uint8_t p = (uint8_t)(r->a & 0x1F);
    r->a = (uint16_t)((r->a & 0xFF00) | p);
    if (p) return;
    if (i == 2) {
      r->c = r->x >= 0xE1;
      if (r->x == 0xE1) return;
    }
    r->c = r->x >= 0xFF;
    if (r->x == 0xFF) return;
  }
  r->c = r->x >= 0xC0;
  if (r->c) {
    r->c = r->x >= 0xD0;
    if (!r->c) return;
  }
  r->a = rdw(ram, r, FRONT_X);
  hm_bus_wr16(ram, 0x7F1F2C, r->a);                        /* the dog's place */
  r->a = rdw(ram, r, FRONT_Y);
  hm_bus_wr16(ram, 0x7F1F2E, r->a);
  r->a = (uint16_t)((r->a & 0xFF00) | *hm_dp(ram, r, 0x22));
  hm_bus_wr8(ram, 0x7F1F30, *hm_dp(ram, r, 0x22));         /* and map */
  act(ram, r, 0x0016);
  r->a = (uint16_t)(0x0800 ^ 0xFFFF) & dp16(ram, r, GAME_STATE);
  set16(ram, r, GAME_STATE, r->a);
}
static void key_a(uint8_t *ram, HmRegs *r)
{
  if (state(ram, r, 0x0800)) { let_dog_go(ram, r); return; }
  if (state(ram, r, 0x0010)) { put_down(ram, r); return; }
  r->a = hm_bus_rd16(ram, FLAGS_60) & 0x0006;
  if (r->a) {                                              /* (in the hot spring?) */
    tile_in_front(ram, r, 0, 7, 7, 0xCB0C, 0, 0xCB1B);
    r->c = r->x >= 0xF5;
    if (r->x == 0xF5) act(ram, r, 0x001A);
    return;
  }
  if (state(ram, r, 0x0002)) { act(ram, r, 0x0005); return; }   /* drop the item */
  tile_in_front(ram, r, 0, 7, 7, 0xCAE1, 2, 0xCAF0);
  jsl(ram, r, 0xCAF4, 0x82, hm_SUB_82AD0E);               /* what is in front */
}

/* ─────────────────────────────────────────────────────────────────────────
 * The buttons. `pad` is $0128 (new presses; B is read from $0124, held),
 * or $08FD during events.
 * ───────────────────────────────────────────────────────────────────────── */
static void script_buttons(uint8_t *ram, HmRegs *r)        /* CODE_84C804 */
{
  static const struct { uint16_t bit; void (*fn)(uint8_t *, HmRegs *); } B[] = {
    { K_A, key_a }, { K_B, key_b }, { K_SELECT, key_select }, { K_X, key_x },
    { K_R, key_r }, { K_L, key_l }, { K_Y, key_y },
  };
  r->a = rdw(ram, r, PAD_SCRIPT);
  for (unsigned i = 0; i < sizeof B / sizeof B[0]; i++)
    if (r->a & B[i].bit) { B[i].fn(ram, r); return; }
}
static void keys_pressed(uint8_t *ram, HmRegs *r)          /* PlayerActionKeysPressed */
{
  if (state(ram, r, 0x1000)) { script_buttons(ram, r); return; }
  r->a = dp16(ram, r, ACTION);
  if (is(r, 0x000F)) goto y;                               /* the rod, ready */
  r->a = rdw(ram, r, PAD_NEW);
  if (r->a & K_A) { key_a(ram, r); return; }
  if (state(ram, r, 0x0020)) return;
  r->a = rdw(ram, r, PAD_HELD);
  if (r->a & K_B) { key_b(ram, r); return; }
  r->a = rdw(ram, r, PAD_NEW);
  if (r->a & K_SELECT) { key_select(ram, r); return; }
  if (r->a & K_X) { key_x(ram, r); return; }
  if (r->a & K_R) { key_r(ram, r); return; }
  if (r->a & K_L) { key_l(ram, r); return; }
y:
  r->a = rdw(ram, r, PAD_NEW);
  if (r->a & K_Y) key_y(ram, r);
}
/* an arrow: walk that way, then the buttons */
static void walk(uint8_t *ram, HmRegs *r, uint16_t dir)
{
  act(ram, r, 0x0001);
  set16(ram, r, FACING, dir);
  r->a = dir;
  wrw(ram, r, TURN, dir);
  keys_pressed(ram, r);
}
static bool arrows(uint8_t *ram, HmRegs *r, uint16_t pad_at)
{
  static const uint16_t bit[4] = { K_DOWN, K_UP, K_RIGHT, K_LEFT };
  for (uint16_t d = 0; d < 4; d++) {
    r->a = rdw(ram, r, pad_at);
    if (r->a & bit[d]) { walk(ram, r, d); return true; }
  }
  return false;
}

/* CODE_84C7D6 ($84:C7D6, also called with JSL from MenuInputs, menu 1):
 * the event's pad, $08FD: an arrow, else a button. */
void hm_CODE_84C7D6(uint8_t *ram, HmRegs *r)
{
  if (!arrows(ram, r, PAD_SCRIPT)) script_buttons(ram, r);
}

void hm_PlayerMovementInputs(uint8_t *ram, HmRegs *r)
{
  if (state(ram, r, 0x00C4)) return;                       /* can't act now */
  r->a = dp16(ram, r, ACTION);
  if (is(r, 0x0003)) return;                               /* in the air */
  r->a = hm_bus_rd16(ram, FLAGS_5C) & 0x0020;
  if (r->a) return;
  if (state(ram, r, 0x1000)) { hm_CODE_84C7D6(ram, r); return; }   /* an event's pad */
  if (!state(ram, r, 0x4000)) return;
  r->a = dp16(ram, r, ACTION);
  if (is(r, 0x0011) || is(r, 0x0012)) {                    /* fishing: only Y */
    r->a = rdw(ram, r, PAD_NEW);
    if (r->a & K_Y) key_y(ram, r);
    return;
  }
  r->x = dp16(ram, r, ACTION);
  uint8_t no = hm_bus_rd8(ram, (CAN_INTERACT + r->x) & 0xFFFFFF);
  r->a = (uint16_t)((r->a & 0xFF00) | no);
  if (no) return;                                          /* busy with an action */
  if (!arrows(ram, r, PAD_HELD)) keys_pressed(ram, r);
}
