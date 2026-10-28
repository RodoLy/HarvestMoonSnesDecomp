/*
 * actions.c — the player's actions $0F-$57 (PlayerActionPointerTable,
 * $81:D593; the first ones are in interact.c).
 *
 * Pressing A on a door-like tile with byte +1 bit 0 of its property sets
 * $096E to the tile's byte +0 and $D2 bit 6 (see interact.c); each frame
 * PlayerInteractionSelector then runs action $096E (JSR; A = $096E x 2,
 * 16 bits) until the action clears $D2 bit 6. $096F counts the action's
 * steps and $0970 its frames (both start at 0). In HM-Decomp they all sit
 * in the block of CODE_81D716 (the TV), partly read as data; they are
 * named here PlayerAction_<number>.
 *
 * As everywhere, the bodies keep A, X, Y and C as the original leaves them.
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

#define STEP          0x096F     /* DB: the action's step */
#define FRAMES        0x0970     /* DB: its frame counter */
#define TIME_RUNNING  0x0973     /* DB: bit 1 = the day ends, bit 2 = sleep */
#define IN_HAND       0x091D     /* DB: the item in hand */
#define INPUT_STATE   0x019A     /* DB: 2 while a text box waits */
#define ANSWER        0x018F     /* DB: the yes / no answer (0 = yes) */
#define GAME_STATE    0xD2       /* DP: bit 6 = an action runs */
#define ACTION        0xD4       /* DP: the player's action (animation) */
#define FACING        0xDA       /* DP: the player's facing */
#define SEASON        0x7F1F19
#define PICK_UP_TABLE 0x82CFB4   /* DATA8_82CFB4: per tile type x 4 + season, an item */

typedef void Step(uint8_t *ram, HmRegs *r);

static uint32_t db(const HmRegs *r, uint16_t a) { return (uint32_t)r->db << 16 | a; }
static uint8_t rd8(uint8_t *ram, const HmRegs *r, uint16_t a) { return hm_bus_rd8(ram, db(r, a)); }
static uint16_t rd16(uint8_t *ram, const HmRegs *r, uint16_t a) { return hm_bus_rd16(ram, db(r, a)); }
static void wr8(uint8_t *ram, const HmRegs *r, uint16_t a, uint8_t v) { hm_bus_wr8(ram, db(r, a), v); }
static void wr16(uint8_t *ram, const HmRegs *r, uint16_t a, uint16_t v) { hm_bus_wr16(ram, db(r, a), v); }
static void set_a8(HmRegs *r, uint8_t v) { r->a = (uint16_t)((r->a & 0xFF00) | v); }

static void jsl(uint8_t *ram, HmRegs *r, uint16_t at, uint8_t bank, Step *fn)
{
  hm_jsl(ram, r, (uint16_t)(at + 3), bank);
  fn(ram, r);
  hm_rtl(ram, r);
}
static void jsr(uint8_t *ram, HmRegs *r, uint16_t at, Step *fn)
{
  uint16_t ret = (uint16_t)(at + 2);
  hm_push8(ram, r, (uint8_t)(ret >> 8));
  hm_push8(ram, r, (uint8_t)ret);
  fn(ram, r);
  hm_pull8(ram, r);
  hm_pull8(ram, r);
}

/* LDA $096F (8 bits) */
static uint8_t step(uint8_t *ram, HmRegs *r)
{
  uint8_t s = rd8(ram, r, STEP);
  set_a8(r, s);
  return s;
}
/* "CMP #k" on the step in A: C, and whether it is k */
static bool is(HmRegs *r, uint8_t k)
{
  r->c = (uint8_t)r->a >= k;
  return (uint8_t)r->a == k;
}
static void next_step(uint8_t *ram, HmRegs *r)               /* INC $096F */
{
  wr8(ram, r, STEP, (uint8_t)(rd8(ram, r, STEP) + 1));
}
/* INC $0970; LDA $0970; CMP #n: has the counter reached n? */
static bool count_to(uint8_t *ram, HmRegs *r, uint8_t n)
{
  uint8_t f = (uint8_t)(rd8(ram, r, FRAMES) + 1);
  wr8(ram, r, FRAMES, f);
  set_a8(r, f);
  r->c = f >= n;
  return f == n;
}
/* $D2 &= ~bits / |= bits (16 bits; A = the new $D2) */
static void state_off(uint8_t *ram, HmRegs *r, uint16_t bits)
{
  r->a = (uint16_t)(hm_dp_r16(ram, r, GAME_STATE) & ~bits);
  hm_dp_w16(ram, r, GAME_STATE, r->a);
}
static void state_on(uint8_t *ram, HmRegs *r, uint16_t bits)
{
  r->a = (uint16_t)(hm_dp_r16(ram, r, GAME_STATE) | bits);
  hm_dp_w16(ram, r, GAME_STATE, r->a);
}
/* the action is over: $D2 bit 6 off */
static void done(uint8_t *ram, HmRegs *r) { state_off(ram, r, 0x0040); }
/* LDA #$02; STA $019A; [LDX #text]; LDA #$00; STA $0191; JSL StartTextBox */
static void text_x(uint8_t *ram, HmRegs *r, uint16_t at)
{
  set_a8(r, 2);
  wr8(ram, r, INPUT_STATE, 2);
  set_a8(r, 0);
  wr8(ram, r, 0x0191, 0);
  jsl(ram, r, at, 0x83, hm_StartTextBox);
}
static void say(uint8_t *ram, HmRegs *r, uint16_t text, uint16_t at)
{
  r->x = text;
  text_x(ram, r, at);
}
/* LDA $019A; CMP #$02: is the text box still waiting? */
static bool text_open(uint8_t *ram, HmRegs *r)
{
  uint8_t v = rd8(ram, r, INPUT_STATE);
  set_a8(r, v);
  r->c = v >= 2;
  return v == 2;
}
/* LDA $018F (8 bits): 0 = yes */
static bool said_no(uint8_t *ram, HmRegs *r)
{
  uint8_t v = rd8(ram, r, ANSWER);
  set_a8(r, v);
  return v != 0;
}
static void map_animation(uint8_t *ram, HmRegs *r, uint16_t a, uint16_t at)
{
  r->a = a;
  jsl(ram, r, at, 0x81, hm_StartMapAnimation);
}
/* the player walks by himself: $D2 bit 7, action a, facing f ($0911 too) */
static void walk(uint8_t *ram, HmRegs *r, uint16_t action, uint16_t facing)
{
  state_on(ram, r, 0x0080);
  r->a = action;
  hm_dp_w16(ram, r, ACTION, action);
  r->a = facing;
  hm_dp_w16(ram, r, FACING, facing);
  wr16(ram, r, 0x0911, facing);
}
/* the player's own animation: action a with sprite s ($0901), CODE_81CFE6 */
static void pose(uint8_t *ram, HmRegs *r, uint16_t action, uint16_t sprite, uint16_t at)
{
  r->a = action;
  hm_dp_w16(ram, r, ACTION, action);
  r->a = sprite;
  wr16(ram, r, 0x0901, sprite);
  jsr(ram, r, at, hm_CODE_81CFE6);
}
/* CalculateTileinFront (A, X, Y given; JSL at a) */
static void tile_ahead(uint8_t *ram, HmRegs *r, uint16_t a, uint16_t x, uint16_t y, uint16_t at)
{
  r->a = a; r->x = x; r->y = y;
  jsl(ram, r, at, 0x81, hm_CalculateTileinFront);
}
/* LDA $0973; ORA #bits; STA $0973 (8 bits) */
static void time_flag(uint8_t *ram, HmRegs *r, uint8_t bits)
{
  uint8_t v = (uint8_t)(rd8(ram, r, TIME_RUNNING) | bits);
  set_a8(r, v);
  wr8(ram, r, TIME_RUNNING, v);
}
/* the player holds item i and the "carrying" action (4) */
static void hold(uint8_t *ram, HmRegs *r, uint8_t item)
{
  set_a8(r, item);
  wr8(ram, r, IN_HAND, item);
  r->a = 4;
  hm_dp_w16(ram, r, ACTION, 4);
}

/* ─────────────────────────────────────────────────────────────────────────
 * The TVs: $0F CODE_81D716 (the first house), $12 and $13 (the bigger
 * houses; their map animations start at $27 and $34 instead of 0).
 * Step 0: $D2 bit 8 (watching), the screen turns on (animation base,
 * unless $0196 bit 4); step 1: once the animation is over ($CF = 0), the
 * weather report (SUB_8281C0), the channel's text for the season
 * (DATA8_81D510: 8 bytes per channel $0990, a word per season; $FFFF none)
 * and its picture (base + channel + 1); step 2: once the text is closed,
 * the screen turns off (animation $46, $47, $48) and $D2 bits 6 and 8 go.
 * Leftovers: those of the last routine called and the stores after it.
 * ───────────────────────────────────────────────────────────────────────── */
#define TV_TEXTS 0x81D510

typedef struct Tv { uint16_t base, off, at_on, at_weather, at_text, at_picture, at_off; } Tv;

static void tv(uint8_t *ram, HmRegs *r, const Tv *t)
{
  step(ram, r);
  if (is(r, 1)) {
    r->a = rd16(ram, r, 0x00CF);
    if (r->a) return;
    jsl(ram, r, t->at_weather, 0x82, hm_SUB_8281C0);
    uint8_t ch = rd8(ram, r, 0x0990);
    r->a = ch;                                             /* LDA #0; XBA; LDA */
    uint8_t v = ch;
    for (int i = 0; i < 3; i++) { r->c = v >> 7; v = (uint8_t)(v << 1); }
    *hm_dp(ram, r, 0x92) = v;
    uint8_t season = hm_bus_rd8(ram, SEASON);
    r->c = season >> 7;
    uint16_t sum = (uint16_t)((uint8_t)(season << 1) + v);  /* ASL; CLC; ADC $92 */
    r->c = sum > 0xFF;
    r->a = (uint8_t)sum;
    r->x = r->a;
    r->a = hm_bus_rd16(ram, TV_TEXTS + r->x);
    r->c = r->a >= 0xFFFF;
    if (r->a != 0xFFFF) {
      r->x = r->a;
      text_x(ram, r, t->at_text);
    }
    r->a = (uint8_t)(rd8(ram, r, 0x0990) + 1);             /* LDA #0; XBA; LDA; INC */
    if (t->base) {                                         /* CLC; ADC #base */
      uint32_t sum = (uint32_t)r->a + t->base;
      r->c = sum > 0xFFFF;
      r->a = (uint16_t)sum;
    }
    map_animation(ram, r, r->a, t->at_picture);
    next_step(ram, r);
    return;
  }
  if (is(r, 2)) {
    if (text_open(ram, r)) return;
    map_animation(ram, r, t->off, t->at_off);
    done(ram, r);
    state_off(ram, r, 0x0100);
    return;
  }
  state_on(ram, r, 0x0100);
  r->a = (uint16_t)(rd16(ram, r, 0x0196) & 0x0010);
  if (!r->a) map_animation(ram, r, t->base, t->at_on);
  next_step(ram, r);
}

void hm_CODE_81D716(uint8_t *ram, HmRegs *r)
{
  static const Tv t = { 0x0000, 0x0046, 0xD73B, 0xD74E, 0xD780, 0xD78F, 0xD7A8 };
  tv(ram, r, &t);
}
void hm_PlayerAction_12(uint8_t *ram, HmRegs *r)
{
  static const Tv t = { 0x0027, 0x0047, 0xD9F1, 0xDA07, 0xDA39, 0xDA4C, 0xDA65 };
  tv(ram, r, &t);
}
void hm_PlayerAction_13(uint8_t *ram, HmRegs *r)
{
  static const Tv t = { 0x0034, 0x0048, 0xDAA7, 0xDABD, 0xDAEF, 0xDB02, 0xDB1B };
  tv(ram, r, &t);
}

/* ─────────────────────────────────────────────────────────────────────────
 * $10 ($81:D7C5): the bed. Nothing with $7F1F6E bit 4, nor while $0878 !=
 * $087A. Step 0: "go to bed?" (text $0A); step 1: yes, skip to step 3
 * with $0973 bit 2 (sleep); no, text $0C and step 2; step 2: once closed,
 * yes goes on to step 3, no ends. Step 3: the player walks to the bed
 * (action 1, facing 3) for $16 frames, or $1E frames (and $7F1F5E bit 11)
 * when one of $7F1F66 bits 0-4 is set; then he lies down (action $19,
 * sprite $4E) and the day ends ($0973 bit 1; with the long walk, step 4
 * waits for $7F1F5E bit 12 first).
 * Leftovers: those of the last routine called and the stores after it.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_PlayerAction_10(uint8_t *ram, HmRegs *r)
{
  r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F6E) & 0x0010);
  if (r->a) { done(ram, r); return; }
  step(ram, r);
  if (is(r, 1)) {
    if (text_open(ram, r)) return;
    if (!said_no(ram, r)) {
      next_step(ram, r);
      next_step(ram, r);
      time_flag(ram, r, 0x04);
      return;
    }
    say(ram, r, 0x000C, 0xD84D);
    next_step(ram, r);
    return;
  }
  if (is(r, 2)) {
    if (text_open(ram, r)) return;
    if (!said_no(ram, r)) { next_step(ram, r); return; }
    done(ram, r);
    return;
  }
  if (is(r, 3)) {
    bool long_walk = false;
    for (uint16_t bit = 1; bit <= 0x10 && !long_walk; bit = (uint16_t)(bit << 1)) {
      r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F66) & bit);
      long_walk = r->a != 0;
    }
    if (!long_walk) {
      walk(ram, r, 1, 3);
      if (!count_to(ram, r, 0x16)) return;
      pose(ram, r, 0x0019, 0x004E, 0xD8ED);
      time_flag(ram, r, 0x02);
      return;
    }
    r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F5E) | 0x0800);
    hm_bus_wr16(ram, 0x7F1F5E, r->a);
    walk(ram, r, 1, 3);
    if (!count_to(ram, r, 0x1E)) return;
    next_step(ram, r);
    pose(ram, r, 0x0019, 0x004E, 0xD94E);
    return;
  }
  if (is(r, 4)) {
    r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F5E) & 0x1000);
    if (r->a) time_flag(ram, r, 0x02);
    return;
  }
  r->a = rd16(ram, r, 0x0878);
  r->c = r->a >= rd16(ram, r, 0x087A);
  if (r->a != rd16(ram, r, 0x087A)) { done(ram, r); return; }
  say(ram, r, 0x000A, 0xD80E);
  next_step(ram, r);
}

/* ─────────────────────────────────────────────────────────────────────────
 * $11 ($81:D979): the player walks (action $0E, facing 1) for $20 frames;
 * then $0878 = $087A = $C2 and the action ends.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_PlayerAction_11(uint8_t *ram, HmRegs *r)
{
  step(ram, r);
  if (is(r, 1)) {
    r->a = 0x00C2;
    wr16(ram, r, 0x0878, 0x00C2);
    wr16(ram, r, 0x087A, 0x00C2);
    done(ram, r);
    return;
  }
  walk(ram, r, 0x000E, 0x0001);
  if (count_to(ram, r, 0x20)) next_step(ram, r);
}

/* ─────────────────────────────────────────────────────────────────────────
 * $14 ($81:DB38): the diary (the save menu): $018A = 0, then the text box
 * in mode 3 ($019A = 3, $019B bit 5, $0142 = 0, $018B = 0); when the menu
 * closes with 2 the step goes on, otherwise the action ends; step 3 goes
 * back to step 1 once the text is closed.
 * ───────────────────────────────────────────────────────────────────────── */
static void diary_open(uint8_t *ram, HmRegs *r)
{
  wr16(ram, r, 0x0142, 0);
  set_a8(r, 3);
  wr8(ram, r, INPUT_STATE, 3);
  uint8_t v = (uint8_t)(rd8(ram, r, 0x019B) | 0x20);
  set_a8(r, v);
  wr8(ram, r, 0x019B, v);
  next_step(ram, r);
  wr8(ram, r, 0x018B, 0);
}
void hm_PlayerAction_14(uint8_t *ram, HmRegs *r)
{
  step(ram, r);
  if (is(r, 1)) { diary_open(ram, r); return; }
  if (is(r, 2)) {
    uint8_t v = rd8(ram, r, INPUT_STATE);
    set_a8(r, v);
    r->c = v >= 3;
    if (v == 3) return;
    r->c = v >= 2;
    if (v == 2) { next_step(ram, r); return; }
    done(ram, r);
    return;
  }
  if (is(r, 3)) {
    if (text_open(ram, r)) return;
    set_a8(r, 1);
    wr8(ram, r, STEP, 1);
    return;
  }
  wr8(ram, r, 0x018A, 0);
  next_step(ram, r);
  diary_open(ram, r);
}

/* ─────────────────────────────────────────────────────────────────────────
 * $15 ($81:DBA1): fodder from the silo: one grass less (AddGrass -1); if
 * there was some, the player holds fodder ($1A).
 * ───────────────────────────────────────────────────────────────────────── */
void hm_PlayerAction_15(uint8_t *ram, HmRegs *r)
{
  r->a = 0xFFFF;
  jsl(ram, r, 0xDBA6, 0x83, hm_AddGrass);
  r->c = r->a >= 1;
  if (r->a != 1) hold(ram, r, 0x1A);
  done(ram, r);
}

/* ─────────────────────────────────────────────────────────────────────────
 * $1E ($81:DBF0): the calendar: "look at it?" (text $0371); yes: the
 * season's page (text $0372 + season).
 * ───────────────────────────────────────────────────────────────────────── */
void hm_PlayerAction_1E(uint8_t *ram, HmRegs *r)
{
  step(ram, r);
  if (!is(r, 1)) {
    say(ram, r, 0x0371, 0xDC0A);
    next_step(ram, r);
    return;
  }
  if (text_open(ram, r)) return;
  if (!said_no(ram, r)) {
    uint32_t sum = (uint32_t)hm_bus_rd8(ram, SEASON) + 0x0372;
    r->c = sum > 0xFFFF;
    r->x = (uint16_t)sum;
    text_x(ram, r, 0xDC46);
  }
  done(ram, r);
}

/* ─────────────────────────────────────────────────────────────────────────
 * The girls' diaries ($26, $2D, $33, $36, $3F): "read it?" (text $23);
 * yes: the page for her hearts (text $03A6 + CODE_81D1C5 of her affection,
 * $7F1F1F-$7F1F27) and one happiness less (AddPlayerHappiness -1).
 * ───────────────────────────────────────────────────────────────────────── */
static void girls_diary(uint8_t *ram, HmRegs *r, uint16_t at, uint32_t affection)
{
  step(ram, r);
  if (!is(r, 1)) {
    say(ram, r, 0x0023, (uint16_t)(at + 0x1A));
    next_step(ram, r);
    return;
  }
  if (text_open(ram, r)) return;
  if (!said_no(ram, r)) {
    r->a = hm_bus_rd16(ram, affection);
    jsr(ram, r, (uint16_t)(at + 0x40), hm_CODE_81D1C5);
    uint32_t sum = (uint32_t)r->a + 0x03A6;
    r->c = sum > 0xFFFF;
    r->a = (uint16_t)sum;
    r->x = r->a;
    text_x(ram, r, (uint16_t)(at + 0x56));
    r->a = 0xFFFF;
    jsl(ram, r, (uint16_t)(at + 0x5F), 0x83, hm_AddPlayerHappiness);
  }
  done(ram, r);
}
void hm_PlayerAction_26(uint8_t *ram, HmRegs *r) { girls_diary(ram, r, 0xDC5E, 0x7F1F1F); }
void hm_PlayerAction_2D(uint8_t *ram, HmRegs *r) { girls_diary(ram, r, 0xDDAC, 0x7F1F23); }
void hm_PlayerAction_33(uint8_t *ram, HmRegs *r) { girls_diary(ram, r, 0xE2D9, 0x7F1F25); }
void hm_PlayerAction_36(uint8_t *ram, HmRegs *r) { girls_diary(ram, r, 0xE394, 0x7F1F21); }
void hm_PlayerAction_3F(uint8_t *ram, HmRegs *r) { girls_diary(ram, r, 0xE52C, 0x7F1F27); }

/* ─────────────────────────────────────────────────────────────────────────
 * Taking something from a shelf ($27-$2C, $2F-$32, $34, $37-$3E): the
 * item of tile type k for spring (DATA8_82CFB4[k x 4]) in hand.
 * ───────────────────────────────────────────────────────────────────────── */
static void take(uint8_t *ram, HmRegs *r, uint16_t k)
{
  r->a = (uint16_t)(k << 2);
  r->c = 0;
  r->x = r->a;
  hold(ram, r, hm_bus_rd8(ram, PICK_UP_TABLE + r->x));
  done(ram, r);
}
#define TAKE(n, k) void hm_PlayerAction_##n(uint8_t *ram, HmRegs *r) { take(ram, r, k); }
TAKE(27, 0x70) TAKE(28, 0x71) TAKE(29, 0x72) TAKE(2A, 0x73) TAKE(2B, 0x74) TAKE(2C, 0x75)
TAKE(2F, 0x7E) TAKE(30, 0x7F) TAKE(31, 0x80) TAKE(32, 0x81) TAKE(34, 0x82)
TAKE(37, 0x77) TAKE(38, 0x76) TAKE(39, 0x78) TAKE(3A, 0x7D) TAKE(3B, 0x79) TAKE(3C, 0x7A)
TAKE(3D, 0x7B) TAKE(3E, 0x7C)

/* ─────────────────────────────────────────────────────────────────────────
 * The fence pieces' places ($1D, $4A-$50): the place's tile becomes $FB
 * (ChangeMapTile at $09AD / $09AF, the place's spot); if the place holds
 * a piece ($09A4 + its number) not yet put up ($7F1F78 has a bit per
 * piece, DATA8_81FAB4), the player takes it (item $1D + piece);
 * otherwise the tile is redrawn as $A1 (EditTileonMap).
 * ───────────────────────────────────────────────────────────────────────── */
#define PIECE_BITS 0x81FAB4

static void fence_place(uint8_t *ram, HmRegs *r, uint16_t x, uint16_t y, uint16_t n, uint16_t at)
{
  r->a = 0x00FB;
  r->x = x; wr16(ram, r, 0x09AD, x);
  r->y = y; wr16(ram, r, 0x09AF, y);
  jsl(ram, r, at, 0x81, hm_ChangeMapTile);
  r->a = n;
  r->x = n;
  uint8_t piece = rd8(ram, r, (uint16_t)(0x09A4 + r->x));
  set_a8(r, piece);
  if (piece) {
    r->y = r->x;
    r->a = (uint16_t)(piece - 1);                          /* XBA; LDA #0; XBA; DEC */
    r->c = (r->a >> 15) & 1;
    r->a = (uint16_t)(r->a << 1);
    r->x = r->a;
    r->a = (uint16_t)(hm_bus_rd16(ram, PIECE_BITS + r->x) & hm_bus_rd16(ram, 0x7F1F78));
    if (!r->a) {
      r->x = r->y;
      uint16_t sum = (uint16_t)(rd8(ram, r, (uint16_t)(0x09A4 + r->x)) + 0x1D);
      r->c = sum > 0xFF;
      hold(ram, r, (uint8_t)sum);
      done(ram, r);
      return;
    }
  }
  r->a = 0x00A1;
  r->x = rd16(ram, r, 0x09AD);
  r->y = rd16(ram, r, 0x09AF);
  jsl(ram, r, 0xF59F, 0x82, hm_EditTileonMap);
  done(ram, r);
}
void hm_PlayerAction_1D(uint8_t *ram, HmRegs *r) { fence_place(ram, r, 0x02E0, 0x01A0, 0, 0xDBE4); }
void hm_PlayerAction_4A(uint8_t *ram, HmRegs *r) { fence_place(ram, r, 0x0010, 0x0030, 1, 0xF4A1); }
void hm_PlayerAction_4B(uint8_t *ram, HmRegs *r) { fence_place(ram, r, 0x00B0, 0x0090, 2, 0xF4BE); }
void hm_PlayerAction_4C(uint8_t *ram, HmRegs *r) { fence_place(ram, r, 0x01C0, 0x0050, 3, 0xF4DB); }
void hm_PlayerAction_4D(uint8_t *ram, HmRegs *r) { fence_place(ram, r, 0x02A0, 0x0030, 4, 0xF4F7); }
void hm_PlayerAction_4E(uint8_t *ram, HmRegs *r) { fence_place(ram, r, 0x0050, 0x02C0, 5, 0xF513); }
void hm_PlayerAction_4F(uint8_t *ram, HmRegs *r) { fence_place(ram, r, 0x0210, 0x0250, 6, 0xF52F); }
void hm_PlayerAction_50(uint8_t *ram, HmRegs *r) { fence_place(ram, r, 0x0210, 0x0330, 7, 0xF54B); }

/* ─────────────────────────────────────────────────────────────────────────
 * $2E ($81:DE1C): the ranch's counter. Closed before the ranch's first
 * event ($7F1F68 bit 0) or on days with $0196 bits 1 / 3 (text $03D0);
 * otherwise the menu (text $0305; B closes it). The answers:
 *   0 a cow: one a day ($7F1F5A bit 5, text $03B6), at most 12 ($7F1F0A,
 *     text $03B7); "sure?" (text $0307); then the barn must have room
 *     ($7F1F29 >= 16 x (cows + 1 + chickens), text $0306), and 5,000 G
 *     (AddMoney -500, text $0304 if not): text $0308, $7F1F5A bits 1 and
 *     5 (it comes tomorrow), one more cow, +20 happiness;
 *   1 a chicken: the same with $7F1F0B, text $030A, 1,000 G, text $0311,
 *     AddNewChicken (2), +10 happiness;
 *   2 / 3 text $030B, and $7F1F5A bit 3 / bit 2 (sell a cow / a chicken).
 * "No" to "sure?" gives text $0303.
 * Leftovers: those of the last routine called and the stores after it.
 * ───────────────────────────────────────────────────────────────────────── */
#define COWS      0x7F1F0A
#define CHICKENS  0x7F1F0B
#define ROOM      0x7F1F29
#define DAY_FLAGS 0x7F1F5A

static void day_flag(uint8_t *ram, HmRegs *r, uint16_t bits)
{
  r->a = (uint16_t)(hm_bus_rd16(ram, DAY_FLAGS) | bits);
  hm_bus_wr16(ram, DAY_FLAGS, r->a);
}
static void say_done(uint8_t *ram, HmRegs *r, uint16_t text, uint16_t at)
{
  say(ram, r, text, at);
  done(ram, r);
}
/* $7E = 16 x (chickens + extra_c), $80 = 16 x (cows + extra_w), $7E += $80;
 * then LDA $7F1F29; CMP $7E */
static bool room_for(uint8_t *ram, HmRegs *r, uint16_t first, uint16_t second)
{
  uint16_t a = (uint16_t)(first << 4);
  hm_dp_w16(ram, r, 0x7E, a);
  uint16_t b = (uint16_t)(second << 4);
  hm_dp_w16(ram, r, 0x80, b);
  uint32_t sum = (uint32_t)a + b;
  hm_dp_w16(ram, r, 0x7E, (uint16_t)sum);
  r->a = hm_bus_rd16(ram, ROOM);
  r->c = r->a >= (uint16_t)sum;
  return r->c;
}
/* AddMoney with -price (24 bits: $FF in $74): true if it was paid */
static bool pay(uint8_t *ram, HmRegs *r, uint16_t minus, uint16_t at)
{
  r->a = minus;
  hm_dp_w16(ram, r, 0x72, minus);
  set_a8(r, 0xFF);
  *hm_dp(ram, r, 0x74) = 0xFF;
  jsl(ram, r, at, 0x83, hm_AddMoney);
  r->c = 1;                                                /* CMP #0 */
  return r->a == 0;
}
static void one_more(uint8_t *ram, HmRegs *r, uint32_t counter)
{
  uint8_t n = (uint8_t)(hm_bus_rd8(ram, counter) + 1);
  set_a8(r, n);
  hm_bus_wr8(ram, counter, n);
}
static void happiness(uint8_t *ram, HmRegs *r, uint16_t n, uint16_t at)
{
  r->a = n;
  jsl(ram, r, at, 0x83, hm_AddPlayerHappiness);
}

void hm_PlayerAction_2E(uint8_t *ram, HmRegs *r)
{
  step(ram, r);
  if (is(r, 1)) {
    r->a = rd16(ram, r, 0x0124);
    if (r->a & 0x8000) {                                   /* B: close */
      jsl(ram, r, 0xE222, 0x83, hm_CloseTextBox);
      set_a8(r, 1);
      wr8(ram, r, INPUT_STATE, 1);
      r->a = (uint16_t)(rd16(ram, r, 0x0128) & 0xFF7F);
      wr16(ram, r, 0x0128, r->a);
      done(ram, r);
      return;
    }
    if (text_open(ram, r)) return;
    uint8_t pick = rd8(ram, r, ANSWER);
    set_a8(r, pick);
    r->c = 1;
    if (pick != 0) {
      r->c = pick >= 1;
      if (pick == 1) {
        r->a = (uint16_t)(hm_bus_rd16(ram, DAY_FLAGS) & 0x0020);
        if (r->a) { say_done(ram, r, 0x03B6, 0xDF57); return; }
        set_a8(r, hm_bus_rd8(ram, CHICKENS));
        r->c = (uint8_t)r->a >= 0x0C;
        if ((uint8_t)r->a == 0x0C) { say_done(ram, r, 0x03B7, 0xE169); return; }
        say(ram, r, 0x030A, 0xDF3A);
        set_a8(r, 3);
        wr8(ram, r, STEP, 3);
        return;
      }
      r->c = pick >= 2;
      if (pick == 2) {
        say_done(ram, r, 0x030B, 0xDF79);
        day_flag(ram, r, 0x0008);
        return;
      }
      r->c = pick >= 3;
      if (pick == 3) {
        say_done(ram, r, 0x030B, 0xDFA8);
        day_flag(ram, r, 0x0004);
        return;
      }
    }
    r->a = (uint16_t)(hm_bus_rd16(ram, DAY_FLAGS) & 0x0020);
    if (r->a) { say_done(ram, r, 0x03B6, 0xDF00); return; }
    set_a8(r, hm_bus_rd8(ram, COWS));
    r->c = (uint8_t)r->a >= 0x0C;
    if ((uint8_t)r->a == 0x0C) { say_done(ram, r, 0x03B7, 0xE03D); return; }
    say(ram, r, 0x0307, 0xDEE3);
    set_a8(r, 2);
    wr8(ram, r, STEP, 2);
    return;
  }
  if (is(r, 2)) {                                          /* a cow: sure? */
    if (text_open(ram, r)) return;
    if (said_no(ram, r)) { say_done(ram, r, 0x0303, 0xE0DC); return; }
    if (!room_for(ram, r, hm_bus_rd8(ram, CHICKENS), (uint16_t)(hm_bus_rd8(ram, COWS) + 1))) {
      say_done(ram, r, 0x0306, 0xE01B);
      return;
    }
    if (!pay(ram, r, 0xFE0C, 0xE05D)) { say_done(ram, r, 0x0304, 0xE077); return; }
    say_done(ram, r, 0x0308, 0xE099);
    day_flag(ram, r, 0x0022);
    one_more(ram, r, COWS);
    happiness(ram, r, 0x0014, 0xE0C6);
    return;
  }
  if (is(r, 3)) {                                          /* a chicken: sure? */
    if (text_open(ram, r)) return;
    if (said_no(ram, r)) { say_done(ram, r, 0x0303, 0xE20F); return; }
    if (!room_for(ram, r, (uint16_t)(hm_bus_rd8(ram, CHICKENS) + 1), hm_bus_rd8(ram, COWS))) {
      say_done(ram, r, 0x0306, 0xE145);
      return;
    }
    if (!pay(ram, r, 0xFF9C, 0xE189)) { say_done(ram, r, 0x0304, 0xE1A3); return; }
    say_done(ram, r, 0x0311, 0xE1C4);
    r->a = 2;
    jsl(ram, r, 0xE1D9, 0x83, hm_AddNewChicken);
    day_flag(ram, r, 0x0020);
    one_more(ram, r, CHICKENS);
    happiness(ram, r, 0x000A, 0xE1FA);
    return;
  }
  r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F68) & 0x0001);
  if (!r->a) { done(ram, r); return; }
  r->a = (uint16_t)(rd16(ram, r, 0x0196) & 0x000A);
  if (r->a) { say_done(ram, r, 0x03D0, 0xDE77); return; }
  say(ram, r, 0x0305, 0xDE5C);
  next_step(ram, r);
}

/* B closes the menu: CloseTextBox (at a), $019A = 1, $0128 bit 7 off */
static bool menu_cancelled(uint8_t *ram, HmRegs *r, uint16_t at)
{
  r->a = rd16(ram, r, 0x0124);
  if (!(r->a & 0x8000)) return false;
  jsl(ram, r, at, 0x83, hm_CloseTextBox);
  set_a8(r, 1);
  wr8(ram, r, INPUT_STATE, 1);
  r->a = (uint16_t)(rd16(ram, r, 0x0128) & 0xFF7F);
  wr16(ram, r, 0x0128, r->a);
  done(ram, r);
  return true;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Signs with a menu ($40 $81:E59C, $54 $81:F86A, $55 $81:F942): the
 * question, then each answer shows its own text (an answer past the list
 * shows the first one's); B closes the menu.
 * ───────────────────────────────────────────────────────────────────────── */
typedef struct Page { uint16_t text, at; } Page;
typedef struct Sign { uint16_t question, at_question, at_close; unsigned n; Page page[5]; } Sign;

static void sign_menu(uint8_t *ram, HmRegs *r, const Sign *sg)
{
  step(ram, r);
  if (!is(r, 1)) {
    say(ram, r, sg->question, sg->at_question);
    next_step(ram, r);
    return;
  }
  if (menu_cancelled(ram, r, sg->at_close)) return;
  if (text_open(ram, r)) return;
  uint8_t pick = rd8(ram, r, ANSWER);
  set_a8(r, pick);
  unsigned k = 0;
  for (unsigned i = 0; i < sg->n; i++) {
    r->c = pick >= i;
    if (pick == i) { k = i; break; }
  }
  say_done(ram, r, sg->page[k].text, sg->page[k].at);
}
void hm_PlayerAction_40(uint8_t *ram, HmRegs *r)
{
  static const Sign sg = { 0x045F, 0xE5B6, 0xE6A0, 5, {
    { 0x0460, 0xE604 }, { 0x0462, 0xE626 }, { 0x0464, 0xE648 }, { 0x0461, 0xE66A }, { 0x0463, 0xE68C } } };
  sign_menu(ram, r, &sg);
}
void hm_PlayerAction_54(uint8_t *ram, HmRegs *r)
{
  static const Sign sg = { 0x0455, 0xF884, 0xF91F, 3, {
    { 0x0456, 0xF8C7 }, { 0x0457, 0xF8E9 }, { 0x0458, 0xF90B } } };
  sign_menu(ram, r, &sg);
}
void hm_PlayerAction_55(uint8_t *ram, HmRegs *r)
{
  static const Sign sg = { 0x0459, 0xF95C, 0xFA46, 5, {
    { 0x045A, 0xF9AA }, { 0x045B, 0xF9CC }, { 0x045C, 0xF9EE }, { 0x045D, 0xFA10 }, { 0x045E, 0xFA32 } } };
  sign_menu(ram, r, &sg);
}

/* ─────────────────────────────────────────────────────────────────────────
 * $41 ($81:E6C3): the hot spring: $D2 bit 5, action 3, and 12 stamina
 * (ChangeStamina; N clear from LDA #$0C).
 * ───────────────────────────────────────────────────────────────────────── */
void hm_PlayerAction_41(uint8_t *ram, HmRegs *r)
{
  state_on(ram, r, 0x0020);
  r->a = 3;
  hm_dp_w16(ram, r, ACTION, 3);
  done(ram, r);
  set_a8(r, 0x0C);
  r->n = 0;
  jsl(ram, r, 0xE6E5, 0x81, hm_ChangeStamina);
}

/* ─────────────────────────────────────────────────────────────────────────
 * $42 ($81:E6EA): the player walks (action 1, facing 2) for $20 frames;
 * then text $030C if $7F1F01 bit 2 is set or the tool in hand is $0B,
 * else $010E; once closed, $099F = 1, $7F1F01 bit 2 and $0196 bit 13;
 * the next frame the action ends.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_PlayerAction_42(uint8_t *ram, HmRegs *r)
{
  step(ram, r);
  if (is(r, 1)) {
    r->x = 0x030C;
    set_a8(r, (uint8_t)(hm_bus_rd8(ram, 0x7F1F01) & 0x04));
    if (!(r->a & 0xFF)) {
      uint8_t tool = rd8(ram, r, 0x0921);
      set_a8(r, tool);
      r->c = tool >= 0x0B;
      if (tool != 0x0B) r->x = 0x010E;
    }
    text_x(ram, r, 0xE755);
    set_a8(r, 2);
    wr8(ram, r, STEP, 2);
    return;
  }
  if (is(r, 2)) {
    if (text_open(ram, r)) return;
    set_a8(r, 3);
    wr8(ram, r, STEP, 3);
    set_a8(r, 1);
    wr8(ram, r, 0x099F, 1);
    uint8_t v = (uint8_t)(hm_bus_rd8(ram, 0x7F1F01) | 0x04);
    set_a8(r, v);
    hm_bus_wr8(ram, 0x7F1F01, v);
    r->a = (uint16_t)(rd16(ram, r, 0x0196) | 0x2000);
    wr16(ram, r, 0x0196, r->a);
    return;
  }
  if (is(r, 3)) { done(ram, r); return; }
  walk(ram, r, 1, 2);
  if (count_to(ram, r, 0x20)) {
    set_a8(r, 1);
    wr8(ram, r, STEP, 1);
  }
}

/* ─────────────────────────────────────────────────────────────────────────
 * $43 ($81:E79E): the cow dealer, about the cow in event slot $09A0 (cow
 * $09A0 - $18; its record from GetsCowPointer, byte +0: bits 1-2 sick or
 * pregnant, bits 4-6 its age). Steps:
 *   0  from year 2, with a healthy grown cow and once ($7F1F66 bit 15):
 *      event 8 / $7C (the cow festival entry) and step 7; otherwise event
 *      7 / $21 (the dealer comes) and step 1;
 *   1  after $40 frames: with $7F1F60 bit 13 text $0487; a sick or
 *      pregnant cow text $0310, a calf $030D (both to step 5); otherwise
 *      the offer (text $030E), step 2;
 *   2  yes: text $030F, step 3; no: text $0309, step 5;
 *   3  sold: the price ($7F1F15, 24 bits) is added, the dealer leaves
 *      (SUB_848020 7, event 7 / $23), the cow too (its slot: event $22),
 *      its record is cleared, one cow less, -50 happiness; step 4;
 *   4  after $42 frames: ends, or with $7F1F60 bit 13 back to the dealer;
 *   5  once the text is closed the dealer leaves; step 6;
 *   6  after $40 frames it ends; without $7F1F60 bit 13 the cow's event
 *      record gets bits $A8 in byte +1;
 *   7  after $42 frames, text $0149; step 8; 8 yes: $014B, step 9; no:
 *      $014C, step $0A;
 *   9  the cow goes to the festival: as in step 3 but with event 8 / $7D,
 *      without money, $7F1F02 bit 5 and $7F1F60 bit 13; step 4;
 *   $0A event 8 / $7D, step $0B; $0B after $42 frames as step 0's second
 *      half.
 * Leftovers: those of the last routine called and the stores after it.
 * ───────────────────────────────────────────────────────────────────────── */
#define DEALER_SLOT 0x09A0
#define FLAGS_60    0x7F1F60

static void set_step(uint8_t *ram, HmRegs *r, uint8_t s)
{
  set_a8(r, s);
  wr8(ram, r, STEP, s);
}
/* LDY #0; LDA [$72],Y; AND #mask */
static uint8_t cow_bits(uint8_t *ram, HmRegs *r, uint8_t mask)
{
  r->y = 0;
  uint8_t v = (uint8_t)(hm_bus_rd8(ram, hm_dp_long(ram, r, 0x72, 0)) & mask);
  set_a8(r, v);
  return v;
}
static void event_long(uint8_t *ram, HmRegs *r, uint16_t a, uint16_t y, uint16_t at)
{
  r->a = a; r->x = 0; r->y = y;
  jsl(ram, r, at, 0x84, hm_UNK_LoadCCDataLong);
}
static void dealer_comes_at(uint8_t *ram, HmRegs *r, uint16_t at)
{
  event_long(ram, r, 0x0007, 0x0021, at);
  set_step(ram, r, 1);
}
static void dealer_comes(uint8_t *ram, HmRegs *r) { dealer_comes_at(ram, r, 0xE858); }  /* $81:E84D */
static bool flag_60_bit(uint8_t *ram, HmRegs *r, uint16_t bit)
{
  r->a = (uint16_t)(hm_bus_rd16(ram, FLAGS_60) & bit);
  return r->a != 0;
}
static bool flag_60(uint8_t *ram, HmRegs *r) { return flag_60_bit(ram, r, 0x2000); }
/* the dealer (slot a) leaves: SUB_848020 and event a / y */
static void leaves(uint8_t *ram, HmRegs *r, uint16_t a, uint16_t y, uint16_t at_free, uint16_t at_event)
{
  r->a = a;
  jsl(ram, r, at_free, 0x84, hm_SUB_848020);
  event_long(ram, r, a, y, at_event);
}
/* the animal leaves too (its slot runs event y), its record goes, one less */
typedef struct Animal { uint16_t slot; uint8_t first; uint16_t event; Step *record; uint32_t count; } Animal;
static const Animal k_cow = { DEALER_SLOT, 0x18, 0x0022, hm_GetsCowPointer, COWS };
static const Animal k_chicken = { 0x09A1, 0x24, 0x0024, hm_GetChickenPointer, CHICKENS };

static void animal_record(uint8_t *ram, HmRegs *r, const Animal *an, uint16_t at)
{
  uint8_t slot = rd8(ram, r, an->slot);
  r->c = slot >= an->first;
  r->a = (uint8_t)(slot - an->first);
  jsl(ram, r, at, 0x83, an->record);
}
static void animal_leaves(uint8_t *ram, HmRegs *r, const Animal *an, uint16_t at_event, uint16_t at_rec)
{
  r->a = rd8(ram, r, an->slot);
  r->x = 0; r->y = an->event;
  jsl(ram, r, at_event, 0x84, hm_UNK_LoadCCDataShort);
  animal_record(ram, r, an, at_rec);
  r->y = 0;
  set_a8(r, 0);
  hm_bus_wr8(ram, hm_dp_long(ram, r, 0x72, 0), 0);
  uint8_t n = (uint8_t)(hm_bus_rd8(ram, an->count) - 1);
  set_a8(r, n);
  hm_bus_wr8(ram, an->count, n);
  set_step(ram, r, 4);
  wr8(ram, r, FRAMES, 0);
}
static void cow_leaves(uint8_t *ram, HmRegs *r, uint16_t at_event, uint16_t at_cow)
{
  animal_leaves(ram, r, &k_cow, at_event, at_cow);
}
/* its event record (SetCCPoiner) gets bits $A8 in byte +1 */
static void animal_stays(uint8_t *ram, HmRegs *r, uint16_t slot, uint16_t at)
{
  r->a = rd8(ram, r, slot);
  jsl(ram, r, at, 0x84, hm_SetCCPoiner);
  r->y = 1;
  uint8_t v = (uint8_t)(hm_bus_rd8(ram, hm_dp_long(ram, r, 0xCC, 1)) | 0xA8);
  set_a8(r, v);
  hm_bus_wr8(ram, hm_dp_long(ram, r, 0xCC, 1), v);
}

/* SEP; LDA #0; XBA; LDA $09A0; SEC; SBC #$18; REP; JSL GetsCowPointer */
static void dealer_cow(uint8_t *ram, HmRegs *r, uint16_t at) { animal_record(ram, r, &k_cow, at); }

void hm_PlayerAction_43(uint8_t *ram, HmRegs *r)
{
  step(ram, r);
  uint8_t s = (uint8_t)r->a;
  int k = 0;
  for (int i = 1; i <= 0x0B && !k; i++) {
    r->c = s >= i;
    if (s == i) k = i;
  }
  switch (k) {
  case 0:
    dealer_cow(ram, r, 0xE7FF);
    if (cow_bits(ram, r, 0x06) || cow_bits(ram, r, 0x70)) { dealer_comes(ram, r); return; }
    set_a8(r, hm_bus_rd8(ram, 0x7F1F18));
    if (!(r->a & 0xFF)) { dealer_comes(ram, r); return; }
    r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F66) & 0x8000);
    if (r->a) { dealer_comes(ram, r); return; }
    r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F66) | 0x8000);
    hm_bus_wr16(ram, 0x7F1F66, r->a);
    event_long(ram, r, 0x0008, 0x007C, 0xE83F);
    set_step(ram, r, 7);
    return;
  case 1:
    if (!count_to(ram, r, 0x40)) return;
    if (flag_60(ram, r)) {
      r->x = 0x0487;
      text_x(ram, r, 0xE902);
      set_step(ram, r, 5);
      return;
    }
    set_a8(r, 2);
    wr8(ram, r, INPUT_STATE, 2);
    dealer_cow(ram, r, 0xE894);
    {
      uint16_t text = 0x030E, at = 0xE8B3;
      uint8_t next = 2;
      if (cow_bits(ram, r, 0x06)) { text = 0x0310; at = 0xE8CB; next = 5; }
      else if (cow_bits(ram, r, 0x70)) { text = 0x030D; at = 0xE8E3; next = 5; }
      r->x = text;
      set_a8(r, 0);
      wr8(ram, r, 0x0191, 0);
      jsl(ram, r, at, 0x83, hm_StartTextBox);
      set_step(ram, r, next);
    }
    return;
  case 2:
    if (text_open(ram, r)) return;
    if (said_no(ram, r)) { say(ram, r, 0x0309, 0xE94F); set_step(ram, r, 5); return; }
    say(ram, r, 0x030F, 0xE932);
    set_step(ram, r, 3);
    return;
  case 3:
    if (text_open(ram, r)) return;
    r->a = hm_bus_rd16(ram, 0x7F1F15);
    hm_dp_w16(ram, r, 0x72, r->a);
    set_a8(r, hm_bus_rd8(ram, 0x7F1F17));
    *hm_dp(ram, r, 0x74) = (uint8_t)r->a;
    jsl(ram, r, 0xE979, 0x83, hm_AddMoney);
    leaves(ram, r, 0x0007, 0x0023, 0xE982, 0xE991);
    cow_leaves(ram, r, 0xE9A3, 0xE9B4);
    happiness(ram, r, 0xFFCE, 0xE9DB);
    return;
  case 4:
    if (!count_to(ram, r, 0x42)) return;
    wr8(ram, r, FRAMES, 0);
    if (flag_60(ram, r)) { dealer_comes(ram, r); return; }
    done(ram, r);
    return;
  case 5:
    if (text_open(ram, r)) return;
    leaves(ram, r, 0x0007, 0x0023, 0xEA22, 0xEA31);
    set_step(ram, r, 6);
    wr8(ram, r, FRAMES, 0);
    return;
  case 6:
    if (!count_to(ram, r, 0x40)) return;
    done(ram, r);
    if (flag_60(ram, r)) return;
    animal_stays(ram, r, DEALER_SLOT, 0xEA73);
    return;
  case 7:
    if (!count_to(ram, r, 0x42)) return;
    r->x = 0x0149;
    text_x(ram, r, 0xEAAC);
    set_step(ram, r, 8);
    return;
  case 8:
    if (text_open(ram, r)) return;
    if (said_no(ram, r)) { say(ram, r, 0x014C, 0xEAF9); set_step(ram, r, 0x0A); return; }
    say(ram, r, 0x014B, 0xEADC);
    set_step(ram, r, 9);
    return;
  case 9:
    if (text_open(ram, r)) return;
    leaves(ram, r, 0x0008, 0x007D, 0xEB18, 0xEB27);
    cow_leaves(ram, r, 0xEB39, 0xEB4A);
    {
      uint8_t v = (uint8_t)(hm_bus_rd8(ram, 0x7F1F02) | 0x20);
      set_a8(r, v);
      hm_bus_wr8(ram, 0x7F1F02, v);
    }
    happiness(ram, r, 0xFFCE, 0xEB7B);
    r->a = (uint16_t)(hm_bus_rd16(ram, FLAGS_60) | 0x2000);
    hm_bus_wr16(ram, FLAGS_60, r->a);
    return;
  case 0x0A:
    if (text_open(ram, r)) return;
    leaves(ram, r, 0x0008, 0x007D, 0xEBA0, 0xEBAF);
    set_step(ram, r, 0x0B);
    wr8(ram, r, FRAMES, 0);
    return;
  default:                                                 /* $0B */
    if (!count_to(ram, r, 0x42)) return;
    wr8(ram, r, FRAMES, 0);
    dealer_comes(ram, r);
    return;
  }
}

/* ─────────────────────────────────────────────────────────────────────────
 * $44 ($81:EBD5): the same dealer for the chicken in event slot $09A1
 * (chicken $09A1 - $24, GetChickenPointer), with $7F1F60 bit 12. Step 0:
 * in autumn, once ($7F1F66 bit 14), the chicken festival entry (event 8 /
 * $7C, step 7); otherwise the dealer comes. Step 1 waits $42 frames and
 * has no sick or young check; the sale gives -20 happiness; step 6 waits
 * $42 frames; text $0148 / $014A for the festival, where the chicken
 * starts event $10 / $1F at its place (SUB_8480F8 with $0985 / $0987
 * from its record +4 / +6).
 * Leftovers: those of the last routine called and the stores after it.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_PlayerAction_44(uint8_t *ram, HmRegs *r)
{
  step(ram, r);
  uint8_t s = (uint8_t)r->a;
  int k = 0;
  for (int i = 1; i <= 0x0B && !k; i++) {
    r->c = s >= i;
    if (s == i) k = i;
  }
  switch (k) {
  case 0: {
    uint8_t season = hm_bus_rd8(ram, SEASON);
    set_a8(r, season);
    r->c = season >= 2;
    if (season != 2) { dealer_comes_at(ram, r, 0xEC6F); return; }
    r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F66) & 0x4000);
    if (r->a) { dealer_comes_at(ram, r, 0xEC6F); return; }
    r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F66) | 0x4000);
    hm_bus_wr16(ram, 0x7F1F66, r->a);
    event_long(ram, r, 0x0008, 0x007C, 0xEC56);
    set_step(ram, r, 7);
    return;
  }
  case 1:
    if (!count_to(ram, r, 0x42)) return;
    if (flag_60_bit(ram, r, 0x1000)) {
      r->x = 0x0487;
      text_x(ram, r, 0xECC5);
      set_step(ram, r, 5);
      return;
    }
    r->x = 0x030E;
    text_x(ram, r, 0xECA6);
    set_step(ram, r, 2);
    return;
  case 2:
    if (text_open(ram, r)) return;
    if (said_no(ram, r)) { say(ram, r, 0x0309, 0xED12); set_step(ram, r, 5); return; }
    say(ram, r, 0x030F, 0xECF5);
    set_step(ram, r, 3);
    return;
  case 3:
    if (text_open(ram, r)) return;
    r->a = hm_bus_rd16(ram, 0x7F1F15);
    hm_dp_w16(ram, r, 0x72, r->a);
    set_a8(r, hm_bus_rd8(ram, 0x7F1F17));
    *hm_dp(ram, r, 0x74) = (uint8_t)r->a;
    jsl(ram, r, 0xED3C, 0x83, hm_AddMoney);
    leaves(ram, r, 0x0007, 0x0023, 0xED45, 0xED54);
    animal_leaves(ram, r, &k_chicken, 0xED66, 0xED77);
    happiness(ram, r, 0xFFEC, 0xED9E);
    return;
  case 4:
    if (!count_to(ram, r, 0x42)) return;
    wr8(ram, r, FRAMES, 0);
    if (flag_60_bit(ram, r, 0x1000)) { dealer_comes_at(ram, r, 0xEC6F); return; }
    done(ram, r);
    return;
  case 5:
    if (text_open(ram, r)) return;
    leaves(ram, r, 0x0007, 0x0023, 0xEDE5, 0xEDF4);
    set_step(ram, r, 6);
    wr8(ram, r, FRAMES, 0);
    return;
  case 6:
    if (!count_to(ram, r, 0x42)) return;
    done(ram, r);
    if (flag_60_bit(ram, r, 0x1000)) return;
    animal_stays(ram, r, 0x09A1, 0xEE36);
    return;
  case 7:
    if (!count_to(ram, r, 0x42)) return;
    r->x = 0x0148;
    text_x(ram, r, 0xEE6F);
    set_step(ram, r, 8);
    return;
  case 8:
    if (text_open(ram, r)) return;
    if (said_no(ram, r)) { say(ram, r, 0x014C, 0xEEBC); set_step(ram, r, 0x0A); return; }
    say(ram, r, 0x014A, 0xEE9F);
    set_step(ram, r, 9);
    return;
  case 9:
    if (text_open(ram, r)) return;
    leaves(ram, r, 0x0008, 0x007D, 0xEEDB, 0xEEEA);
    animal_leaves(ram, r, &k_chicken, 0xEEFC, 0xEF0D);
    r->y = 4;
    r->a = hm_bus_rd16(ram, hm_dp_long(ram, r, 0x72, 4));
    wr16(ram, r, 0x0985, r->a);
    r->y = 6;
    r->a = hm_bus_rd16(ram, hm_dp_long(ram, r, 0x72, 6));
    wr16(ram, r, 0x0987, r->a);
    r->a = 0x0010; r->x = 0; r->y = 0x001F;
    jsl(ram, r, 0xEF4A, 0x84, hm_SUB_8480F8);
    happiness(ram, r, 0xFFEC, 0xEF53);
    r->a = (uint16_t)(hm_bus_rd16(ram, FLAGS_60) | 0x1000);
    hm_bus_wr16(ram, FLAGS_60, r->a);
    return;
  case 0x0A:
    if (text_open(ram, r)) return;
    leaves(ram, r, 0x0008, 0x007D, 0xEF78, 0xEF87);
    set_step(ram, r, 0x0B);
    wr8(ram, r, FRAMES, 0);
    return;
  default:
    if (!count_to(ram, r, 0x42)) return;
    wr8(ram, r, FRAMES, 0);
    dealer_comes_at(ram, r, 0xEC6F);
    return;
  }
}

/* ─────────────────────────────────────────────────────────────────────────
 * $45 ($81:EFAD): after 8 frames the player walks (action 1, facing 2)
 * for $20 frames; text $0317; once closed, $099F = 2 and $0196 bit 13; the
 * next frame the action ends.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_PlayerAction_45(uint8_t *ram, HmRegs *r)
{
  step(ram, r);
  if (is(r, 1)) {
    walk(ram, r, 1, 2);
    if (count_to(ram, r, 0x20)) set_step(ram, r, 2);
    return;
  }
  if (is(r, 2)) {
    r->x = 0x0317;
    text_x(ram, r, 0xF022);
    set_step(ram, r, 3);
    return;
  }
  if (is(r, 3)) {
    if (text_open(ram, r)) return;
    set_step(ram, r, 4);
    set_a8(r, 2);
    wr8(ram, r, 0x099F, 2);
    r->a = (uint16_t)(rd16(ram, r, 0x0196) | 0x2000);
    wr16(ram, r, 0x0196, r->a);
    return;
  }
  if (is(r, 4)) { done(ram, r); return; }
  if (count_to(ram, r, 0x08)) {
    set_step(ram, r, 1);
    wr8(ram, r, FRAMES, 0);
  }
}

/* ─────────────────────────────────────────────────────────────────────────
 * $46 ($81:F061): the shipper's visit: event 6 / $26 (he comes); after $42
 * frames his text: $031A, or $031B when nothing was shipped ($7F1F07-09
 * = 0); once closed he leaves (SUB_848020 6, event 6 / $27), and after $42
 * frames more the action ends.
 * Bug: when the low word of the shipped money is not 0 the branch to the
 * text keeps A in 16 bits, so "LDA #$00; STA $0191" is read as "LDA
 * #$8D00; STA ($01),Y": $8D00 goes to the word at ($01),Y and $0191 keeps
 * its value.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_PlayerAction_46(uint8_t *ram, HmRegs *r)
{
  step(ram, r);
  if (is(r, 1)) {
    if (!count_to(ram, r, 0x42)) return;
    set_a8(r, 2);
    wr8(ram, r, INPUT_STATE, 2);
    r->x = 0x031A;
    r->a = hm_bus_rd16(ram, 0x7F1F07);
    if (r->a) {                                            /* the bug */
      r->a = 0x8D00;
      uint32_t at = (((uint32_t)r->db << 16) + hm_dp_r16(ram, r, 0x01) + r->y) & 0xFFFFFF;
      hm_bus_wr16(ram, at, 0x8D00);
      jsl(ram, r, 0xF0C4, 0x83, hm_StartTextBox);
    } else {
      set_a8(r, hm_bus_rd8(ram, 0x7F1F09));
      if (!(r->a & 0xFF)) r->x = 0x031B;
      set_a8(r, 0);
      wr8(ram, r, 0x0191, 0);
      jsl(ram, r, 0xF0C4, 0x83, hm_StartTextBox);
    }
    set_step(ram, r, 2);
    return;
  }
  if (is(r, 2)) {
    if (text_open(ram, r)) return;
    leaves(ram, r, 0x0006, 0x0027, 0xF0E3, 0xF0F2);
    set_step(ram, r, 3);
    wr8(ram, r, FRAMES, 0);
    return;
  }
  if (is(r, 3)) {
    if (count_to(ram, r, 0x42)) done(ram, r);
    return;
  }
  event_long(ram, r, 0x0006, 0x0026, 0xF085);
  set_step(ram, r, 1);
}

/* ─────────────────────────────────────────────────────────────────────────
 * $47 ($81:F121): the carpenter, about the house extension (lumber needed
 * $7F1F0E: 250 for the first, else 500; lumber in stock $7F1F0C). After
 * $20 frames (in the first spring or summer, $7F1F70 bit 2): with enough
 * lumber the offer (text $0134, or $013B for the second) and step 3, else
 * text $0137 and step 1. Step 1: once closed he leaves (event 9 / $2D on
 * slot 9), step 2, which ends after $C6 frames. Step 3: no gives text $0136;
 * yes pays 5,000 G (or 10,000 G for the second; text $0304 if not), uses
 * the lumber (AddWood), text $0135, $7F1F35 = 0 (the work starts) and
 * $7F1F66 bit 7; all three go to step 1.
 * Leftovers: those of the last routine called and the stores after it.
 * ───────────────────────────────────────────────────────────────────────── */
#define LUMBER_NEEDED 0x7F1F0E
#define LUMBER        0x7F1F0C

static void event_short(uint8_t *ram, HmRegs *r, uint16_t a, uint16_t y, uint16_t at)
{
  r->a = a; r->x = 0; r->y = y;
  jsl(ram, r, at, 0x84, hm_UNK_LoadCCDataShort);
}

void hm_PlayerAction_47(uint8_t *ram, HmRegs *r)
{
  step(ram, r);
  if (is(r, 1)) {
    if (text_open(ram, r)) return;
    event_short(ram, r, 0x0009, 0x002D, 0xF1E5);
    set_step(ram, r, 2);
    return;
  }
  if (is(r, 2)) {
    if (count_to(ram, r, 0xC6)) done(ram, r);
    return;
  }
  if (is(r, 3)) {
    if (text_open(ram, r)) return;
    if (said_no(ram, r)) { say(ram, r, 0x0136, 0xF2CC); set_step(ram, r, 1); return; }
    r->a = hm_bus_rd16(ram, LUMBER_NEEDED);
    r->c = r->a >= 0x00FA;
    bool paid = r->a == 0x00FA ? pay(ram, r, 0xFE0C, 0xF259) : pay(ram, r, 0xFC18, 0xF23F);
    if (!paid) { say(ram, r, 0x0304, 0xF2B0); set_step(ram, r, 1); return; }
    r->a = (uint16_t)(~hm_bus_rd16(ram, LUMBER_NEEDED) + 1);   /* EOR #$FFFF; INC */
    jsl(ram, r, 0xF26E, 0x83, hm_AddWood);
    say(ram, r, 0x0135, 0xF281);
    set_step(ram, r, 1);
    set_a8(r, 0);
    hm_bus_wr8(ram, 0x7F1F35, 0);
    r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F66) | 0x0080);
    hm_bus_wr16(ram, 0x7F1F66, r->a);
    return;
  }
  if (!count_to(ram, r, 0x20)) return;
  set_a8(r, hm_bus_rd8(ram, 0x7F1F18));
  if (!(r->a & 0xFF)) {
    uint8_t season = hm_bus_rd8(ram, SEASON);
    set_a8(r, season);
    r->c = season >= 2;
    if (season < 2) {
      r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F70) | 0x0004);
      hm_bus_wr16(ram, 0x7F1F70, r->a);
    }
  }
  wr8(ram, r, FRAMES, 0);
  uint16_t offer = 0x013B, need = 0x01F4;
  r->a = hm_bus_rd16(ram, LUMBER_NEEDED);
  r->c = r->a >= 0x00FA;
  if (r->a == 0x00FA) { offer = 0x0134; need = 0x00FA; }
  r->a = hm_bus_rd16(ram, LUMBER);
  r->c = r->a >= need;
  if (r->c) {
    r->x = offer;
    text_x(ram, r, 0xF1A3);
    set_step(ram, r, 3);
    return;
  }
  say(ram, r, 0x0137, 0xF1C0);
  set_step(ram, r, 1);
}

/* ─────────────────────────────────────────────────────────────────────────
 * $48 ($81:F2DA): the house extension finished: after $20 frames +50
 * happiness and text $013D, or +100 and text $0140 for the second
 * ($7F1F64 bit 7); step 1. Step 1: once closed, the first extension in the
 * first spring or summer ($7F1F70 bit 2) also gives text $025C,
 * $7F1F6E bit 14 and step 3; otherwise the carpenters leave (events $38
 * and $39 on slots 9 and $0A), step 2, which ends after $C6 frames. Step
 * 3 makes them leave once that text is closed.
 * ───────────────────────────────────────────────────────────────────────── */
static void carpenters_leave(uint8_t *ram, HmRegs *r, uint16_t at1, uint16_t at2)
{
  event_short(ram, r, 0x0009, 0x0038, at1);
  event_short(ram, r, 0x000A, 0x0039, at2);
  set_step(ram, r, 2);
}

void hm_PlayerAction_48(uint8_t *ram, HmRegs *r)
{
  step(ram, r);
  if (is(r, 1)) {
    if (text_open(ram, r)) return;
    r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F64) & 0x0080);
    if (!r->a) {
      r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F70) & 0x0004);
      if (r->a) {
        r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F6E) | 0x4000);
        hm_bus_wr16(ram, 0x7F1F6E, r->a);
        say(ram, r, 0x025C, 0xF386);
        set_step(ram, r, 3);
        return;
      }
    }
    carpenters_leave(ram, r, 0xF39E, 0xF3AD);
    return;
  }
  if (is(r, 2)) {
    if (count_to(ram, r, 0xC6)) done(ram, r);
    return;
  }
  if (is(r, 3)) {
    if (text_open(ram, r)) return;
    carpenters_leave(ram, r, 0xF3F0, 0xF3FF);
    return;
  }
  if (!count_to(ram, r, 0x20)) return;
  wr8(ram, r, FRAMES, 0);
  r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F64) & 0x0080);
  if (!r->a) {
    happiness(ram, r, 0x0032, 0xF315);
    r->x = 0x013D;
  } else {
    happiness(ram, r, 0x0064, 0xF325);
    r->x = 0x0140;
  }
  text_x(ram, r, 0xF33A);
  set_step(ram, r, 1);
}

/* ─────────────────────────────────────────────────────────────────────────
 * $49 ($81:F40E): after $20 frames text $0300; once closed, event $0B /
 * $1E (on slot $0B); the action ends $C6 frames later.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_PlayerAction_49(uint8_t *ram, HmRegs *r)
{
  step(ram, r);
  if (is(r, 1)) {
    if (text_open(ram, r)) return;
    event_short(ram, r, 0x000B, 0x001E, 0xF463);
    set_step(ram, r, 2);
    return;
  }
  if (is(r, 2)) {
    if (count_to(ram, r, 0xC6)) done(ram, r);
    return;
  }
  if (!count_to(ram, r, 0x20)) return;
  wr8(ram, r, FRAMES, 0);
  say(ram, r, 0x0300, 0xF43E);
  set_step(ram, r, 1);
}

/* ─────────────────────────────────────────────────────────────────────────
 * A drink at the bar ($51 $81:F5B0: 200 G, $52 $81:F6D7: 300 G): the
 * offer; no: text $024D; yes: paid (text $024C) or not enough money
 * ($0213); once paid and closed the player drinks (action $19 with
 * sprite $30 / $FB) and gains 1 / 3 happiness, and $64 frames later the
 * action ends with action 0.
 * ───────────────────────────────────────────────────────────────────────── */
typedef struct Drink {
  uint16_t offer, at_offer, price, at_pay, at_paid, at_no, at_short;
  uint16_t sprite, at_pose, joy, at_joy;
} Drink;

static void drink(uint8_t *ram, HmRegs *r, const Drink *d)
{
  step(ram, r);
  if (is(r, 1)) {
    if (text_open(ram, r)) return;
    if (said_no(ram, r)) { say(ram, r, 0x024D, d->at_no); set_step(ram, r, 3); return; }
    if (!pay(ram, r, d->price, d->at_pay)) { say(ram, r, 0x0213, d->at_short); set_step(ram, r, 3); return; }
    say(ram, r, 0x024C, d->at_paid);
    set_step(ram, r, 2);
    return;
  }
  if (is(r, 2)) {
    if (text_open(ram, r)) return;
    pose(ram, r, 0x0019, d->sprite, d->at_pose);
    happiness(ram, r, d->joy, d->at_joy);
    wr8(ram, r, FRAMES, 0);
    set_step(ram, r, 4);
    return;
  }
  if (is(r, 3)) {
    if (!text_open(ram, r)) done(ram, r);
    return;
  }
  if (is(r, 4)) {
    if (!count_to(ram, r, 0x64)) return;
    done(ram, r);
    r->a = 0;
    hm_dp_w16(ram, r, ACTION, 0);
    return;
  }
  say(ram, r, d->offer, d->at_offer);
  next_step(ram, r);
}
void hm_PlayerAction_51(uint8_t *ram, HmRegs *r)
{
  static const Drink d = { 0x024B, 0xF5DF, 0xFFEC, 0xF60B, 0xF625, 0xF642, 0xF65F,
                           0x0030, 0xF686, 0x0001, 0xF68E };
  drink(ram, r, &d);
}
void hm_PlayerAction_52(uint8_t *ram, HmRegs *r)
{
  static const Drink d = { 0x024A, 0xF706, 0xFFE2, 0xF732, 0xF74C, 0xF769, 0xF786,
                           0x00FB, 0xF7AE, 0x0003, 0xF7B6 };
  drink(ram, r, &d);
}

/* ─────────────────────────────────────────────────────────────────────────
 * $53 ($81:F7FF): an egg from a nest of the coop: the player holds an egg
 * ($14); the nest is the one of DATA16_83CA10 (13 nests, X - 8) whose
 * column is the tile in front's, and its egg bit in $7F1F45 (DATA16_81FAD4)
 * goes.
 * Bug: with no nest in that column the search loops for ever ("BRA" to
 * itself at $81:F856); the rewrite returns (the fuzz keeps the player
 * where the tile in front is always a nest's column).
 * ───────────────────────────────────────────────────────────────────────── */
#define NESTS      0x83CA10
#define NEST_BITS  0x81FAD4

void hm_PlayerAction_53(uint8_t *ram, HmRegs *r)
{
  tile_ahead(ram, r, 1, 0, 0, 0xF80E);
  hold(ram, r, 0x14);
  done(ram, r);
  uint16_t v = rd16(ram, r, 0x0985);
  for (int i = 0; i < 4; i++) { r->c = v & 1; v = (uint16_t)(v >> 1); }
  for (int i = 0; i < 4; i++) { r->c = v >> 15; v = (uint16_t)(v << 1); }
  r->a = v;
  hm_dp_w16(ram, r, 0x7E, v);
  for (r->y = 0; r->y < 0x0D; r->y++) {
    r->a = (uint16_t)(r->y << 2);
    r->x = r->a;
    r->a = (uint16_t)(hm_bus_rd16(ram, NESTS + r->x) - 8);
    r->c = r->a >= v;
    if (r->a == v) {
      r->a = (uint16_t)(r->y << 1);
      r->c = 0;
      r->x = r->a;
      r->a = (uint16_t)(hm_bus_rd16(ram, NEST_BITS + r->x) & hm_bus_rd16(ram, 0x7F1F45));
      hm_bus_wr16(ram, 0x7F1F45, r->a);
      return;
    }
  }
}

/* $56 ($81:FA69): it ends at the second frame. */
void hm_PlayerAction_56(uint8_t *ram, HmRegs *r)
{
  if (count_to(ram, r, 0x02)) done(ram, r);
}

/* $57 ($81:FA84): text $0491 with $7F1F66 bit 1, else $0246. */
void hm_PlayerAction_57(uint8_t *ram, HmRegs *r)
{
  r->x = 0x0491;
  r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F66) & 0x0002);
  if (!r->a) r->x = 0x0246;
  text_x(ram, r, 0xFAA3);
  done(ram, r);
}
