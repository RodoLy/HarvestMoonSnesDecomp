/*
 * items.c — the things the player puts down or throws (items as map
 * objects).
 *
 * An item that leaves the player's hands becomes a map object (see
 * objects.c, BAAAA): its record keeps the item ($0984 while it is worked
 * on), a state ($0976) and its sprite. SUB_818000 puts it down; then, each
 * frame, SUB_81809A runs the item's own routine (table DATA8_8197C0, one
 * per item), which acts by the state: lands, breaks, is eaten...
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

#define ITEM_SPRITES     0x8196AF   /* DATA8_8196AF: per item, a pointer to its sprites */
#define TILE_CHANGES     0x81A2AD   /* DATA8_81A2AD: per item, the tile it leaves (0: none) */
#define ITEM_SOUNDS      0x81A308   /* DATA8_81A308: per item, whether it makes a sound */
#define ITEM             0x0984     /* DB: the item being worked on */

static uint32_t db(const HmRegs *r, uint16_t a) { return (uint32_t)r->db << 16 | a; }

/* ─────────────────────────────────────────────────────────────────────────
 * SUB_8180B7 ($81:80B7, JSL, RTL; the start of HM-Decomp's SUB_8180B7
 * block): the sprite of item $0984 for the case Y (a facing, + 2 when put
 * down): DATA8_8196AF gives the item's list (3 bytes per item), and in it
 * 3 bytes per case: the sprite (to $097A) and a byte that x 64 is the flip
 * ($097E).
 * Leftovers: $72-$74 = the list, $7E = Y, A = the flip, Y = 3 Y + 2,
 * X = 3 item + 2, C = 0.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_SUB_8180B7(uint8_t *ram, HmRegs *r)
{
  uint16_t item = hm_bus_rd8(ram, db(r, ITEM));
  hm_dp_w16(ram, r, 0x7E, item);
  r->x = (uint16_t)(item * 3);                             /* ASL; CLC; ADC (no carry) */
  hm_dp_w16(ram, r, 0x72, hm_bus_rd16(ram, ITEM_SPRITES + r->x));
  r->x = (uint16_t)(r->x + 2);
  uint8_t bank = hm_bus_rd8(ram, ITEM_SPRITES + r->x);
  *hm_dp(ram, r, 0x74) = bank;
  uint16_t which = r->y;
  hm_dp_w16(ram, r, 0x7E, which);
  uint32_t y3 = (uint32_t)(uint16_t)(which << 1) + which;  /* ASL; CLC; ADC $7E */
  r->y = (uint16_t)y3;
  hm_bus_wr16(ram, db(r, 0x097A), hm_bus_rd16(ram, hm_dp_long(ram, r, 0x72, r->y)));
  r->y = (uint16_t)(r->y + 2);
  uint16_t flip = (uint16_t)(hm_bus_rd8(ram, hm_dp_long(ram, r, 0x72, r->y)) << 6);
  r->c = 0;                                                /* the last ASL */
  r->a = flip;
  hm_bus_wr16(ram, db(r, 0x097E), flip);
}

/* ─────────────────────────────────────────────────────────────────────────
 * SUB_818000 ($81:8000, JSL, RTL): the item in hand ($091D) is put down
 * in front of the player: a map object in state 0 ($0974 = $0975 = 1,
 * $0976 = 0) with the item's sprite for the player's facing (+ 2), two
 * pixels below the player (SpawnMapObject). Then the tile in front, and if
 * the item leaves something on the map (DATA8_81A2AD), that tile type
 * (from the map's property table) is put there; and if the item makes a
 * sound (DATA8_81A308), sound effect 8 of bank 6.
 * Leftovers: those of the last routine called.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_SUB_818000(uint8_t *ram, HmRegs *r)
{
  hm_bus_wr8(ram, db(r, 0x0974), 1);
  hm_bus_wr8(ram, db(r, 0x0975), 1);
  hm_bus_wr8(ram, db(r, 0x0976), 0);
  uint8_t item = hm_bus_rd8(ram, db(r, 0x091D));
  hm_bus_wr8(ram, db(r, ITEM), item);
  uint32_t sum = 2u + hm_dp_r16(ram, r, 0xDA);
  r->c = sum > 0xFFFF;
  r->a = (uint16_t)sum;
  r->y = r->a;
  hm_jsl(ram, r, 0x8025, 0x81);
  hm_SUB_8180B7(ram, r);
  hm_rtl(ram, r);
  r->a = hm_dp_r16(ram, r, 0xD6);
  hm_bus_wr16(ram, db(r, 0x0980), r->a);
  r->a = (uint16_t)(hm_dp_r16(ram, r, 0xD8) + 2);
  hm_bus_wr16(ram, db(r, 0x0982), r->a);
  hm_jsl(ram, r, 0x8037, 0x81);
  hm_SpawnMapObject(ram, r);
  hm_rtl(ram, r);
  r->a = 1; r->x = 0; r->y = 0;
  hm_jsl(ram, r, 0x8046, 0x81);
  hm_CalculateTileinFront(ram, r);
  hm_rtl(ram, r);

  uint16_t it = hm_bus_rd8(ram, db(r, ITEM));
  r->x = it;
  uint8_t change = hm_bus_rd8(ram, TILE_CHANGES + it);
  r->a = (uint16_t)((it & 0xFF00) | change);
  if (change) {
    r->c = (change >> 6) & 1;                              /* ASL; ASL (16 bits) */
    r->y = (uint16_t)(change << 2);
    r->a = hm_bus_rd8(ram, hm_dp_long(ram, r, 0x0D, r->y)); /* LDA #0; XBA; LDA [$0D],Y */
    r->x = hm_bus_rd16(ram, db(r, 0x0985));
    r->y = hm_bus_rd16(ram, db(r, 0x0987));
    hm_jsl(ram, r, 0x8071, 0x81);
    hm_ChangeMapTile(ram, r);
    hm_rtl(ram, r);
  }
  it = hm_bus_rd8(ram, db(r, ITEM));
  r->x = it;
  uint8_t sound = hm_bus_rd8(ram, ITEM_SOUNDS + it);
  r->a = (uint16_t)((it & 0xFF00) | sound);
  if (!sound) return;
  r->a = (uint16_t)((r->a & 0xFF00) | 0x06);
  hm_bus_wr8(ram, db(r, 0x0114), 0x08);
  hm_bus_wr8(ram, db(r, 0x0115), 0x06);
  hm_jsl(ram, r, 0x8098, 0x83);
  hm_UNK_Audio19(ram, r);
  hm_rtl(ram, r);
}

/* ═════════════════════════════════════════════════════════════════════════
 * The item routines (the rest of HM-Decomp's SUB_8180B7 block).
 *
 * SUB_81809A calls one per item (table DATA8_8197C0) with JSR, every frame
 * while the item is a map object. Most just call one of six state machines
 * (JSR); each machine picks, by the item's state ($0976), one of the steps
 * below (JMP), which does its part and maybe moves to another state:
 *
 *   CODE_8188E5 (most items)   0 hold, 2 carried, 3 dropped, 4 gone,
 *                              5-6 offered for sale, 7 binned, 8 shipped
 *   CODE_818855 (food, 1-8)    as 8188E5 but 0 asks "eat it?", 1 eats
 *   CODE_8188BC (item $1A)     0, 2, 3 = into a feed box, 4, 8
 *   CODE_81889A (items $0D-$0F, $57)  0, 2 thrown, 3 lands, 4 gone
 *   CODE_81883A (items $25, $26, $29) 0, 2 thrown, 3 = an animal set down
 *   CODE_818923 (shop goods)   0 "buy it?", 1 answer, 2 put back, 3 bought
 *
 * The bodies keep A, X, Y and C as the original leaves them: X and Y are
 * 16 bits throughout, A changes width as the original does (an 8-bit load
 * keeps A's high byte).
 * ═════════════════════════════════════════════════════════════════════════ */
#define STATE         0x0976     /* DB: the item's state */
#define OBJECT        0x0978     /* DB: its game object */
#define FLAGS         0x0974     /* DB: its flags (bit 2: sprite of the jump) */
#define IN_HAND       0x091D     /* DB: the item the player holds */
#define INPUT_STATE   0x019A     /* DB: 2 while a text box waits */
#define ANSWER        0x018F     /* DB: the yes / no answer (0 = yes) */
#define GAME_STATE    0xD2       /* DP: bit 1 = holding something */
#define ACTION        0xD4       /* DP: the player's action (3 jump, 5 throw...) */
#define FOOD_TABLE    0x819FC6   /* DATA8_819FC6: per item - 1, its text and stamina */
#define PRICE_TABLE   0x819FDE   /* Items_Price_Table: per item, a tile and a price */
#define SALE_TABLE    0x81A094   /* DATA8_81A094: per item, its text and a money byte */
#define SHOP_TABLE    0x81A1A5   /* DATA8_81A1A5: 12 bytes per shop article */
#define SPOT_TABLE    0x81A363   /* DATA8_81A363: X, Y of the bins and boxes */
#define STALL_BITS    0x82A571   /* Cow_Feed_Flags: one bit per feed box */

typedef void Step(uint8_t *ram, HmRegs *r);

static uint8_t rd8(uint8_t *ram, const HmRegs *r, uint16_t a) { return hm_bus_rd8(ram, db(r, a)); }
static uint16_t rd16(uint8_t *ram, const HmRegs *r, uint16_t a) { return hm_bus_rd16(ram, db(r, a)); }
static void wr8(uint8_t *ram, const HmRegs *r, uint16_t a, uint8_t v) { hm_bus_wr8(ram, db(r, a), v); }
static void wr16(uint8_t *ram, const HmRegs *r, uint16_t a, uint16_t v) { hm_bus_wr16(ram, db(r, a), v); }
static void set_a8(HmRegs *r, uint8_t v) { r->a = (uint16_t)((r->a & 0xFF00) | v); }

/* JSL at `at` to a routine of `bank`; JSR at `at` (same bank) */
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

/* "STA $7E; ASL A; CLC; ADC $7E" (16 bits): 3 v, C = the add's carry */
static uint16_t times3(uint8_t *ram, HmRegs *r, uint16_t v)
{
  hm_dp_w16(ram, r, 0x7E, v);
  uint32_t sum = (uint32_t)(uint16_t)(v << 1) + v;
  r->c = sum > 0xFFFF;
  return (uint16_t)sum;
}
/* "CLC; ADC #k" (16 bits) */
static uint16_t add16(HmRegs *r, uint16_t v, uint16_t k)
{
  uint32_t sum = (uint32_t)v + k;
  r->c = sum > 0xFFFF;
  return (uint16_t)sum;
}
/* "LDA #$00; XBA; LDA $0984" (8 bits): A = the item, high byte 0 */
static uint16_t item_word(uint8_t *ram, HmRegs *r)
{
  r->a = rd8(ram, r, ITEM);
  return r->a;
}

/* LDA $0978; STA $A5; JSL CODE_8581A2 (at a); then JSL CODE_81A4F1 (at b,
 * if any): the item's game object, and its map object, go away */
static void remove_object(uint8_t *ram, HmRegs *r, uint16_t a, uint16_t b)
{
  r->a = rd16(ram, r, OBJECT);
  hm_dp_w16(ram, r, 0xA5, r->a);
  jsl(ram, r, a, 0x85, hm_CODE_8581A2);
  if (b) jsl(ram, r, b, 0x81, hm_CODE_81A4F1);
}
/* the player stops (action 0) and no longer holds anything ($D2 bit 1) */
static void hands_free(uint8_t *ram, HmRegs *r)
{
  hm_dp_w16(ram, r, ACTION, 0);
  r->a = (uint16_t)(hm_dp_r16(ram, r, GAME_STATE) & ~0x0002);
  hm_dp_w16(ram, r, GAME_STATE, r->a);
}
static void hands_full(uint8_t *ram, HmRegs *r)
{
  r->a = (uint16_t)(hm_dp_r16(ram, r, GAME_STATE) | 0x0002);
  hm_dp_w16(ram, r, GAME_STATE, r->a);
}
static void set_state(uint8_t *ram, HmRegs *r, uint8_t s)
{
  set_a8(r, s);
  wr8(ram, r, STATE, s);
}
/* SEP #$20; LDA #$02; STA $019A; [X = text]; LDA #$00; STA $0191;
 * JSL StartTextBox (at a) */
static void say(uint8_t *ram, HmRegs *r, uint16_t at)
{
  set_a8(r, 0);
  wr8(ram, r, 0x0191, 0);
  jsl(ram, r, at, 0x83, hm_StartTextBox);
}
static void wait_for_input(uint8_t *ram, HmRegs *r)
{
  set_a8(r, 2);
  wr8(ram, r, INPUT_STATE, 2);
}
/* "LDA $019A; CMP #$02": still waiting for the text box? */
static bool text_open(uint8_t *ram, HmRegs *r)
{
  uint8_t v = rd8(ram, r, INPUT_STATE);
  set_a8(r, v);
  r->c = v >= 2;
  return v == 2;
}
/* LDY #y; JSL SUB_8180B7 (at a): the sprite of case y */
static void sprite(uint8_t *ram, HmRegs *r, uint16_t y, uint16_t at)
{
  r->y = y;
  jsl(ram, r, at, 0x81, hm_SUB_8180B7);
}
/* CalculateTileinFront (A, X, Y given; at a) */
static void tile_ahead(uint8_t *ram, HmRegs *r, uint16_t a, uint16_t x, uint16_t y, uint16_t at)
{
  r->a = a; r->x = x; r->y = y;
  jsl(ram, r, at, 0x81, hm_CalculateTileinFront);
}
/* UNK_CheckTileProperty of the tile in front (A = which byte; at a):
 * X = its type, A = the property */
static void tile_property(uint8_t *ram, HmRegs *r, uint16_t a, uint16_t at)
{
  r->x = rd16(ram, r, 0x0985);
  r->y = rd16(ram, r, 0x0987);
  r->a = a;
  jsl(ram, r, at, 0x82, hm_UNK_CheckTileProperty);
}
/* "CPX #k" */
static bool cpx(HmRegs *r, uint16_t k)
{
  r->c = r->x >= k;
  return r->x == k;
}
/* the item where the player is, two pixels lower ($0980 / $0982) */
static void follow_player(uint8_t *ram, HmRegs *r)
{
  r->a = hm_dp_r16(ram, r, 0xD6);
  wr16(ram, r, 0x0980, r->a);
  r->a = (uint16_t)(hm_dp_r16(ram, r, 0xD8) + 2);
  wr16(ram, r, 0x0982, r->a);
}
/* the item at the tile in front */
static void to_tile_ahead(uint8_t *ram, HmRegs *r)
{
  r->a = rd16(ram, r, 0x0985);
  wr16(ram, r, 0x0980, r->a);
  r->a = rd16(ram, r, 0x0987);
  wr16(ram, r, 0x0982, r->a);
}

/* ─────────────────────────────────────────────────────────────────────────
 * The machines ($81:883A-$81:8922, JSR, RTS): by the state, JMP to a step;
 * a state with no step just returns. Leftovers: A's low byte = the state,
 * C = the last CMP (the entry's C for state 0), or the step's.
 * ───────────────────────────────────────────────────────────────────────── */
static void machine(uint8_t *ram, HmRegs *r, Step *zero, const uint8_t *states, Step *const *steps, int n)
{
  uint8_t s = rd8(ram, r, STATE);
  set_a8(r, s);
  if (s == 0) { zero(ram, r); return; }
  for (int i = 0; i < n; i++) {
    r->c = s >= states[i];
    if (s == states[i]) { steps[i](ram, r); return; }
  }
}

void hm_CODE_818855(uint8_t *ram, HmRegs *r)
{
  static const uint8_t s[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
  static Step *const f[] = { hm_CODE_818BA5, hm_CODE_819049, hm_CODE_819379, hm_CODE_81968F,
                             hm_CODE_818F2A, hm_CODE_818F5A, hm_CODE_819695, hm_CODE_819021 };
  machine(ram, r, hm_CODE_818B6B, s, f, 8);
}
void hm_CODE_8188E5(uint8_t *ram, HmRegs *r)
{
  static const uint8_t s[] = { 2, 3, 4, 5, 6, 7, 8 };
  static Step *const f[] = { hm_CODE_819049, hm_CODE_819379, hm_CODE_81968F, hm_CODE_818F2A,
                             hm_CODE_818F5A, hm_CODE_819695, hm_CODE_819021 };
  machine(ram, r, hm_CODE_819010, s, f, 7);
}
void hm_CODE_8188BC(uint8_t *ram, HmRegs *r)
{
  static const uint8_t s[] = { 2, 3, 4, 8 };
  static Step *const f[] = { hm_CODE_819049, hm_CODE_818E98, hm_CODE_81968F, hm_CODE_819021 };
  machine(ram, r, hm_CODE_819010, s, f, 4);
}
void hm_CODE_81889A(uint8_t *ram, HmRegs *r)
{
  static const uint8_t s[] = { 2, 3, 4 };
  static Step *const f[] = { hm_CODE_818C24, hm_CODE_818D38, hm_CODE_818E16 };
  machine(ram, r, hm_CODE_819010, s, f, 3);
}
void hm_CODE_81883A(uint8_t *ram, HmRegs *r)
{
  static const uint8_t s[] = { 2, 3 };
  static Step *const f[] = { hm_CODE_819228, hm_CODE_818E1B };
  machine(ram, r, hm_CODE_819010, s, f, 2);
}

/* ─────────────────────────────────────────────────────────────────────────
 * CODE_818923 ($81:8923, JSR, RTS): the goods of a shop (items $1B-$1D and
 * $44-$56), picked up from the counter. DATA8_81A1A5 has 12 bytes per
 * article (items $44-$56 first, then $1B-$1D): +0 the "buy it?" text, +2
 * the "thank you" text, +4 the "no" text, +6 the "not enough money" text,
 * +8 the counter's tile and +9 the price (24 bits, negative: it is added
 * to the money).
 *   state 0: the question; state 1: on yes, pay (AddMoney): thanks and
 *   state 3, or "not enough money" and state 2; on no, that text and as
 *   state 2; state 2: once the text is closed, the article goes back on
 *   the counter (its tile) and the player's hands are free; state 3: the
 *   same without the tile, except for items $49, $56 and $1B-$1D, which
 *   stay in his hands; then state $FF (the item routine finishes the
 *   purchase).
 * Leftovers: those of the last routine called and the stores after it.
 * ───────────────────────────────────────────────────────────────────────── */
/* SEP #$20; LDA $0984; CMP #$44; ...: the article's number, A = it (with
 * the high byte A had for the items below $44) */
static uint16_t article(uint8_t *ram, HmRegs *r)
{
  uint8_t it = rd8(ram, r, ITEM);
  if (it < 0x44) {
    uint8_t v = (uint8_t)(it - 0x1B);                      /* SEC; SBC #$1B */
    uint16_t sum = (uint16_t)(v + 0x13);                   /* CLC; ADC #$13 */
    r->c = sum > 0xFF;
    set_a8(r, (uint8_t)sum);
  } else {
    r->a = (uint16_t)(it - 0x44);                          /* LDA #0; XBA; ... SEC; SBC */
    r->c = 1;
  }
  return r->a;
}
/* REP #$20; STA $7E; ASL; CLC; ADC $7E; ASL; ASL: the article x 12 */
static uint16_t article_x12(uint8_t *ram, HmRegs *r)
{
  uint16_t v = times3(ram, r, r->a);
  r->c = (v >> 15) & 1; v = (uint16_t)(v << 1);
  r->c = (v >> 15) & 1; v = (uint16_t)(v << 1);
  return v;
}

static void shop_ask(uint8_t *ram, HmRegs *r)              /* CODE_81893F */
{
  wait_for_input(ram, r);
  article(ram, r);
  r->x = article_x12(ram, r);
  r->a = hm_bus_rd16(ram, SHOP_TABLE + r->x);
  r->x = r->a;
  say(ram, r, 0x897B);
  set_state(ram, r, 1);
}

static void shop_put_back(uint8_t *ram, HmRegs *r)         /* CODE_818AA0 */
{
  remove_object(ram, r, 0x8AA7, 0);
  hands_free(ram, r);
  jsl(ram, r, 0x8ABE, 0x81, hm_CODE_81A4F1);
  wr8(ram, r, IN_HAND, 0);
  tile_ahead(ram, r, 1, 6, 6, 0x8AD2);
  article(ram, r);
  r->x = add16(r, article_x12(ram, r), 8);
  uint8_t tile = hm_bus_rd8(ram, SHOP_TABLE + r->x);
  r->a = tile;                                             /* XBA; LDA #0; XBA */
  r->x = rd16(ram, r, 0x0985);
  r->y = rd16(ram, r, 0x0987);
  jsl(ram, r, 0x8B13, 0x81, hm_ChangeMapTile);
}

static void shop_answer(uint8_t *ram, HmRegs *r)           /* CODE_818989 */
{
  if (text_open(ram, r)) return;
  uint8_t no = rd8(ram, r, ANSWER);
  set_a8(r, no);
  if (no) {                                                /* CODE_818A4E */
    wait_for_input(ram, r);
    article(ram, r);
    r->x = add16(r, article_x12(ram, r), 4);
    r->a = hm_bus_rd16(ram, SHOP_TABLE + r->x);
    r->x = r->a;
    say(ram, r, 0x8A8E);
    shop_put_back(ram, r);
    return;
  }
  article(ram, r);
  r->x = add16(r, article_x12(ram, r), 9);
  r->a = hm_bus_rd16(ram, SHOP_TABLE + r->x);
  hm_dp_w16(ram, r, 0x72, r->a);
  r->x = (uint16_t)(r->x + 2);
  set_a8(r, hm_bus_rd8(ram, SHOP_TABLE + r->x));
  *hm_dp(ram, r, 0x74) = (uint8_t)r->a;
  jsl(ram, r, 0x89DA, 0x83, hm_AddMoney);
  uint16_t short_of = r->a ? 4 : 0;                        /* BEQ on AddMoney's A */
  if (short_of) r->a = 4;
  hm_dp_w16(ram, r, 0x80, short_of);
  wait_for_input(ram, r);
  article(ram, r);
  uint16_t v = add16(r, article_x12(ram, r), 2);
  uint32_t sum = (uint32_t)v + short_of + r->c;            /* ADC $80 (no CLC) */
  r->c = sum > 0xFFFF;
  r->x = (uint16_t)sum;
  r->a = hm_bus_rd16(ram, SHOP_TABLE + r->x);
  r->x = r->a;
  say(ram, r, 0x8A2D);
  r->a = short_of;                                         /* REP #$20; LDA $80 */
  set_state(ram, r, short_of ? 2 : 3);
}

static void shop_wait(uint8_t *ram, HmRegs *r)             /* CODE_818A94 */
{
  if (text_open(ram, r)) return;
  shop_put_back(ram, r);
}

static void shop_sold(uint8_t *ram, HmRegs *r)             /* CODE_818B19 */
{
  if (text_open(ram, r)) return;
  uint8_t it = rd8(ram, r, ITEM);
  set_a8(r, it);
  static const uint8_t kept[] = { 0x49, 0x56, 0x1B, 0x1C, 0x1D };
  bool keep = false;
  for (unsigned i = 0; i < sizeof kept && !keep; i++) {
    r->c = it >= kept[i];
    keep = it == kept[i];
  }
  if (!keep) {
    remove_object(ram, r, 0x8B43, 0);
    hands_free(ram, r);
    jsl(ram, r, 0x8B5A, 0x81, hm_CODE_81A4F1);
    wr8(ram, r, IN_HAND, 0);
  }
  set_state(ram, r, 0xFF);
}

void hm_CODE_818923(uint8_t *ram, HmRegs *r)
{
  static const uint8_t s[] = { 1, 2, 3 };
  static Step *const f[] = { shop_answer, shop_wait, shop_sold };
  machine(ram, r, shop_ask, s, f, 3);
}

/* ─────────────────────────────────────────────────────────────────────────
 * Food (items 1-8, CODE_818855). DATA8_819FC6 has 3 bytes per food: the
 * "eat it?" text and the stamina it gives.
 * CODE_818B6B (state 0): the sprite in hand (case 1) and the question;
 * state 1. CODE_818BA5 (state 1): once answered: yes, the player eats
 * (action 8, $D2 bit 15 cleared), the item goes and ChangeStamina gets the
 * food's value; no, the player stops and holds it (state 2).
 * Leftovers: those of the last routine called and the stores after it.
 * ───────────────────────────────────────────────────────────────────────── */
static uint16_t food_entry(uint8_t *ram, HmRegs *r)
{
  item_word(ram, r);
  r->a = (uint16_t)((r->a & 0xFF00) | (uint8_t)(r->a - 1));   /* DEC A (8 bits) */
  return times3(ram, r, r->a);
}

void hm_CODE_818B6B(uint8_t *ram, HmRegs *r)
{
  sprite(ram, r, 1, 0x8B70);
  r->x = food_entry(ram, r);
  r->a = hm_bus_rd16(ram, FOOD_TABLE + r->x);
  r->x = r->a;
  wait_for_input(ram, r);
  say(ram, r, 0x8B97);
  set_state(ram, r, 1);
}

void hm_CODE_818BA5(uint8_t *ram, HmRegs *r)
{
  if (text_open(ram, r)) return;
  uint8_t no = rd8(ram, r, ANSWER);
  set_a8(r, no);
  if (no) {                                                /* CODE_818C0C */
    hm_dp_w16(ram, r, ACTION, 0);
    hands_full(ram, r);
    set_state(ram, r, 2);
    return;
  }
  hm_dp_w16(ram, r, ACTION, 8);
  r->a = (uint16_t)(hm_dp_r16(ram, r, GAME_STATE) & 0x7FFF);
  hm_dp_w16(ram, r, GAME_STATE, r->a);
  remove_object(ram, r, 0x8BD3, 0x8BD7);
  wr8(ram, r, IN_HAND, 0);
  r->x = (uint16_t)(food_entry(ram, r) + 2);
  uint8_t stamina = hm_bus_rd8(ram, FOOD_TABLE + r->x);
  set_a8(r, stamina);
  r->n = stamina >> 7;                                     /* ChangeStamina reads N */
  jsl(ram, r, 0x8BFA, 0x81, hm_ChangeStamina);
  r->a = (uint16_t)(hm_dp_r16(ram, r, GAME_STATE) & ~0x0002);
  hm_dp_w16(ram, r, GAME_STATE, r->a);
}

/* ─────────────────────────────────────────────────────────────────────────
 * The common steps.
 * CODE_819010 (state 0): the sprite in hand (case 1); state 2.
 * CODE_819021 (state 8): the item goes, the player stops with free hands.
 * CODE_81968F (state 4): its map object goes (CODE_81A4F1).
 * CODE_819695 (state 7): once the map animation is over ($CF-$D1 = 0) the
 * map object goes and $D2 bit 0 is set.
 * Leftovers: those of the last routine called and the stores after it.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_CODE_819010(uint8_t *ram, HmRegs *r)
{
  sprite(ram, r, 1, 0x9015);
  set_state(ram, r, 2);
}

void hm_CODE_819021(uint8_t *ram, HmRegs *r)
{
  remove_object(ram, r, 0x9028, 0x902C);
  wr8(ram, r, IN_HAND, 0);
  hands_free(ram, r);
}

void hm_CODE_81968F(uint8_t *ram, HmRegs *r)
{
  jsl(ram, r, 0x968F, 0x81, hm_CODE_81A4F1);
}

void hm_CODE_819695(uint8_t *ram, HmRegs *r)
{
  r->a = hm_dp_r16(ram, r, 0xCF);
  if (r->a) return;
  set_a8(r, *hm_dp(ram, r, 0xD1));
  if (r->a & 0xFF) return;
  jsl(ram, r, 0x96A1, 0x81, hm_CODE_81A4F1);
  r->a = (uint16_t)(hm_dp_r16(ram, r, GAME_STATE) | 0x0001);
  hm_dp_w16(ram, r, GAME_STATE, r->a);
}

/* ─────────────────────────────────────────────────────────────────────────
 * Carrying and throwing.
 * While carried (state 2) the item follows the player, two pixels lower,
 * with the sprite in hand (case 1); while he jumps (action 3) the sprite of
 * the jump (case $0B, $0974 bit 2) until its animation ends ($A7 = $FFFF
 * from CODE_8581CB). When he throws it (action 5), if nothing is in the way
 * (CBBBB) it flies (case 6 + his facing, state 3).
 * ───────────────────────────────────────────────────────────────────────── */
static void carried(uint8_t *ram, HmRegs *r, uint16_t at_hand, uint16_t at_jump, uint16_t at_anim)
{
  follow_player(ram, r);
  r->a = hm_dp_r16(ram, r, ACTION);
  r->c = r->a >= 3;
  if (r->a == 3) {
    uint8_t f = rd8(ram, r, FLAGS);
    set_a8(r, f & 0x04);
    if (!(f & 0x04)) {
      sprite(ram, r, 0x0B, at_jump);
      f = (uint8_t)(rd8(ram, r, FLAGS) | 0x04);
      set_a8(r, f);
      wr8(ram, r, FLAGS, f);
      return;
    }
    r->a = rd16(ram, r, OBJECT);
    hm_dp_w16(ram, r, 0xA5, r->a);
    jsl(ram, r, at_anim, 0x85, hm_CODE_8581CB);
    r->a = hm_dp_r16(ram, r, 0xA7);
    r->c = r->a >= 0xFFFF;
    if (r->a != 0xFFFF) return;
  }
  sprite(ram, r, 1, at_hand);
  uint8_t f = (uint8_t)(rd8(ram, r, FLAGS) & 0xFB);
  set_a8(r, f);
  wr8(ram, r, FLAGS, f);
}
/* REP #$30; LDA $D4; CMP #$05: is the player throwing it? */
static bool throwing(uint8_t *ram, HmRegs *r)
{
  r->a = hm_dp_r16(ram, r, ACTION);
  r->c = r->a >= 5;
  return r->a == 5;
}
/* CBBBB from the player's place, 16 pixels ahead (at a): A != 0 when
 * something is in the way; then CMP #0 (C = 1) */
static bool in_the_way(uint8_t *ram, HmRegs *r, uint16_t at)
{
  hm_dp_w16(ram, r, 0xDF, hm_dp_r16(ram, r, 0xD6));
  hm_dp_w16(ram, r, 0xE1, hm_dp_r16(ram, r, 0xD8));
  hm_dp_w16(ram, r, 0xE5, 0);
  hm_dp_w16(ram, r, 0xE7, 0);
  hm_dp_w16(ram, r, 0xE3, 0x0010);
  r->a = hm_dp_r16(ram, r, 0xDA);
  jsl(ram, r, at, 0x83, hm_CBBBB);
  r->c = 1;
  return r->a != 0;
}
/* LDA #6; CLC; ADC $DA; TAY; JSL SUB_8180B7 (at a); state 3: it flies */
static void fly(uint8_t *ram, HmRegs *r, uint16_t at)
{
  r->a = add16(r, 6, hm_dp_r16(ram, r, 0xDA));
  sprite(ram, r, r->a, at);
  set_state(ram, r, 3);
}
static void stop_throw(uint8_t *ram, HmRegs *r)
{
  r->a = 0;
  hm_dp_w16(ram, r, ACTION, 0);
}
/* CPX #1, #2, #7, #8 (ground: 1 is returned), then A = 0..5 and CPX #$F0,
 * #$F9..#$FD (a bin or a box: 2 + its number); 0 when none */
static int landing(HmRegs *r)
{
  static const uint16_t ground[] = { 0x01, 0x02, 0x07, 0x08 };
  static const uint16_t bins[] = { 0xF0, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD };
  for (unsigned i = 0; i < 4; i++)
    if (cpx(r, ground[i])) return 1;
  for (unsigned i = 0; i < 6; i++) {
    r->a = (uint16_t)i;
    if (cpx(r, bins[i])) return 2 + (int)i;
  }
  return 0;
}

/* CODE_818C24 (state 2 of CODE_81889A): carried; thrown only towards
 * ground or a bin. */
void hm_CODE_818C24(uint8_t *ram, HmRegs *r)
{
  carried(ram, r, 0x8C43, 0x8C61, 0x8C78);
  if (!throwing(ram, r)) return;
  tile_ahead(ram, r, 1, 6, 6, 0x8C9C);
  tile_property(ram, r, 2, 0x8CAB);
  if (landing(r) && !in_the_way(ram, r, 0x8D0E)) {
    fly(ram, r, 0x8D20);
    return;
  }
  stop_throw(ram, r);
}

/* CODE_818DAB: the item lands on the tile in front: that tile becomes the
 * item's (Items_Price_Table, first byte) and the item goes. */
static void land_here(uint8_t *ram, HmRegs *r)
{
  tile_ahead(ram, r, 1, 6, 6, 0x8DB6);
  r->a = (uint16_t)(item_word(ram, r) << 1);
  r->c = 0;                                                /* ASL of a byte */
  r->x = r->a;
  set_a8(r, hm_bus_rd8(ram, PRICE_TABLE + r->x));
  r->x = rd16(ram, r, 0x0985);
  r->y = rd16(ram, r, 0x0987);
  jsl(ram, r, 0x8DD2, 0x81, hm_ChangeMapTile);
  remove_object(ram, r, 0x8DDD, 0x8DE1);
  wr8(ram, r, IN_HAND, 0);
}
/* CODE_818DEC / CODE_819631: into bin or box A: the item goes to its
 * place (DATA8_81A363), sprite $CA, state 4. */
static void into_spot(uint8_t *ram, HmRegs *r)
{
  r->c = (r->a >> 15) & 1; r->a = (uint16_t)(r->a << 1);
  r->c = (r->a >> 15) & 1; r->a = (uint16_t)(r->a << 1);
  r->x = r->a;
  r->a = hm_bus_rd16(ram, SPOT_TABLE + r->x);
  wr16(ram, r, 0x0980, r->a);
  r->x = (uint16_t)(r->x + 2);
  r->a = hm_bus_rd16(ram, SPOT_TABLE + r->x);
  wr16(ram, r, 0x0982, r->a);
  r->a = 0x00CA;
  wr16(ram, r, 0x097A, 0x00CA);
  wr8(ram, r, IN_HAND, 0);
  set_state(ram, r, 4);
}

/* CODE_818D38 (state 3 of CODE_81889A): where it lands: ground, a bin, or
 * neither (the throw stops). */
void hm_CODE_818D38(uint8_t *ram, HmRegs *r)
{
  tile_ahead(ram, r, 1, 6, 6, 0x8D43);
  tile_property(ram, r, 2, 0x8D52);
  int where = landing(r);
  if (where == 1) { land_here(ram, r); return; }
  if (where) { into_spot(ram, r); return; }
  stop_throw(ram, r);
}

/* CODE_818E16 (state 4 of CODE_81889A): the map object goes. */
void hm_CODE_818E16(uint8_t *ram, HmRegs *r)
{
  jsl(ram, r, 0x8E16, 0x81, hm_CODE_81A4F1);
}

/* CODE_818E1B (state 3 of CODE_81883A): an animal ($0920) set down: for
 * $25 / $26 its record keeps this map and the tile in front (and loses
 * bit 5 of its flags); then its walk starts (SUB_8480F8: script 1 for
 * $26, 2 for $25, $31 otherwise) and the item goes. */
void hm_CODE_818E1B(uint8_t *ram, HmRegs *r)
{
  uint8_t h = rd8(ram, r, IN_HAND);
  set_a8(r, h);
  r->c = h >= 0x29;
  if (h != 0x29) {
    uint8_t who = rd8(ram, r, 0x0920);
    r->c = who >= 0x24;
    r->a = (uint8_t)(who - 0x24);
    jsl(ram, r, 0x8E2F, 0x83, hm_GetChickenPointer);
    r->y = 1;
    set_a8(r, *hm_dp(ram, r, 0x22));
    hm_bus_wr8(ram, hm_dp_long(ram, r, 0x72, 1), (uint8_t)r->a);
    r->y = 0;
    set_a8(r, (uint8_t)(hm_bus_rd8(ram, hm_dp_long(ram, r, 0x72, 0)) & 0xDF));
    hm_bus_wr8(ram, hm_dp_long(ram, r, 0x72, 0), (uint8_t)r->a);
    r->y = 4;
    r->a = rd16(ram, r, 0x0985);
    hm_bus_wr16(ram, hm_dp_long(ram, r, 0x72, 4), r->a);
    r->y = 6;
    r->a = rd16(ram, r, 0x0987);
    hm_bus_wr16(ram, hm_dp_long(ram, r, 0x72, 6), r->a);
  }
  r->y = 1;
  h = rd8(ram, r, IN_HAND);
  set_a8(r, h);
  r->c = h >= 0x26;
  if (h != 0x26) {
    r->y = 2;
    r->c = h >= 0x25;
    if (h != 0x25) r->y = 0x31;
  }
  r->a = rd8(ram, r, 0x0920);
  r->x = 0;
  jsl(ram, r, 0x8E7D, 0x84, hm_SUB_8480F8);
  remove_object(ram, r, 0x8E88, 0x8E8C);
  wr8(ram, r, IN_HAND, 0);
}

/* CODE_818E98 (state 3 of CODE_8188BC, item $1A): fodder thrown into a
 * feed box of the barn (map $27) or the coop ($28), tile types $E2-$EE:
 * that box's bit in $0932 / $0934 and one more in $0930 / $0931, and the
 * fodder lands (CODE_818DAB). Anywhere else, as any dropped item. */
void hm_CODE_818E98(uint8_t *ram, HmRegs *r)
{
  wr8(ram, r, IN_HAND, 0);
  tile_ahead(ram, r, 1, 6, 6, 0x8EA8);
  tile_property(ram, r, 0, 0x8EB7);
  uint8_t map = *hm_dp(ram, r, 0x22);
  set_a8(r, map);
  r->c = map >= 0x27;
  uint16_t flags = 0x0932, count = 0x0930;
  if (map != 0x27) {
    r->c = map >= 0x28;
    if (map != 0x28) { hm_CODE_819379(ram, r); return; }
    flags = 0x0934; count = 0x0931;
  }
  if (r->x < 0xE2 || r->x >= 0xEF) {
    r->c = r->x >= 0xE2;
    hm_CODE_819379(ram, r);
    return;
  }
  r->a = (uint16_t)((r->x - 0xE2) << 1);                   /* SEC; SBC; ASL */
  r->c = 0;
  r->x = r->a;
  r->a = (uint16_t)(hm_bus_rd16(ram, STALL_BITS + r->x) | rd16(ram, r, flags));
  wr16(ram, r, flags, r->a);
  uint8_t n = (uint8_t)(rd8(ram, r, count) + 1);
  set_a8(r, n);
  wr8(ram, r, count, n);
  land_here(ram, r);
}

/* ─────────────────────────────────────────────────────────────────────────
 * Selling. DATA8_81A094 has 3 bytes per item: the "sell it?" text and the
 * money (in tens of G).
 * CODE_818F2A (state 5): the question; state 6.
 * CODE_818F5A (state 6): once answered: yes (and the item can be sold:
 * text $0313 means it cannot) text $02FA, the item goes and its money is
 * added; no, text $0303; both "no" and unsellable give it back to the
 * player (sprite in hand at his place $090B / $090D, state 2, holding).
 * ───────────────────────────────────────────────────────────────────────── */
void hm_CODE_818F2A(uint8_t *ram, HmRegs *r)
{
  wait_for_input(ram, r);
  item_word(ram, r);
  r->x = times3(ram, r, r->a);
  r->a = hm_bus_rd16(ram, SALE_TABLE + r->x);
  r->x = r->a;
  say(ram, r, 0x8F4C);
  set_state(ram, r, 6);
}

static void give_back(uint8_t *ram, HmRegs *r)             /* CODE_818FEA */
{
  sprite(ram, r, 1, 0x8FEF);
  r->a = rd16(ram, r, 0x090B);
  wr16(ram, r, 0x0980, r->a);
  r->a = rd16(ram, r, 0x090D);
  wr16(ram, r, 0x0982, r->a);
  set_state(ram, r, 2);
  hands_full(ram, r);
}

void hm_CODE_818F5A(uint8_t *ram, HmRegs *r)
{
  if (text_open(ram, r)) return;
  uint8_t no = rd8(ram, r, ANSWER);
  set_a8(r, no);
  if (no) {
    wait_for_input(ram, r);
    r->x = 0x0303;
    say(ram, r, 0x8FE6);
    give_back(ram, r);
    return;
  }
  item_word(ram, r);
  r->x = times3(ram, r, r->a);
  r->a = hm_bus_rd16(ram, SALE_TABLE + r->x);
  r->c = r->a >= 0x0313;
  if (r->a == 0x0313) { give_back(ram, r); return; }
  wait_for_input(ram, r);
  r->x = 0x02FA;
  say(ram, r, 0x8F95);
  remove_object(ram, r, 0x8FA0, 0x8FA4);
  item_word(ram, r);
  r->x = (uint16_t)(times3(ram, r, r->a) + 2);
  r->a = hm_bus_rd8(ram, SALE_TABLE + r->x);
  hm_dp_w16(ram, r, 0x72, r->a);
  *hm_dp(ram, r, 0x74) = 0;
  jsl(ram, r, 0x8FCC, 0x83, hm_AddMoney);
  wr8(ram, r, IN_HAND, 0);
}

/* the item's price (Items_Price_Table, second byte) to the day's shipping
 * money ($7F1F07, 24 bits), and $7F1F5E bit 0 (something was shipped) */
static void ship(uint8_t *ram, HmRegs *r, uint8_t price)
{
  uint32_t sum = (uint32_t)price + hm_bus_rd16(ram, 0x7F1F07);
  hm_bus_wr16(ram, 0x7F1F07, (uint16_t)sum);
  uint16_t hi = (uint16_t)(hm_bus_rd8(ram, 0x7F1F09) + (sum > 0xFFFF));
  r->c = hi > 0xFF;
  r->a = (uint16_t)((sum & 0xFF00) | (uint8_t)hi);
  hm_bus_wr8(ram, 0x7F1F09, (uint8_t)hi);
  r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F5E) | 0x0001);
  hm_bus_wr16(ram, 0x7F1F5E, r->a);
}
/* LDA #0; XBA; LDA $0984; REP #$20; ASL; TAX; INX; SEP #$20; LDA.L
 * Items_Price_Table,X: the item's price */
static uint8_t price_of(uint8_t *ram, HmRegs *r)
{
  r->a = (uint16_t)(item_word(ram, r) << 1);
  r->c = 0;
  r->x = (uint16_t)(r->a + 1);
  uint8_t p = hm_bus_rd8(ram, PRICE_TABLE + r->x);
  set_a8(r, p);
  return p;
}
static bool before_5pm(uint8_t *ram, HmRegs *r)
{
  uint8_t h = hm_bus_rd8(ram, 0x7F1F1C);
  set_a8(r, h);
  r->c = h >= 0x11;
  return h < 0x11;
}

/* CODE_819049 (state 2): carried. Thrown on a Sunday before 5 pm in town
 * (maps 4-7, $0196 bit 1 clear) at a stall tile ($F0) with a sellable item:
 * it is put there and offered (state 5). Thrown at an object (CBBBB): put
 * at the tile in front and, with $7F1F5C bit 4 and a price before 5 pm,
 * shipped ($7F1F5C bit 6 too); state 8. Otherwise it flies. */
void hm_CODE_819049(uint8_t *ram, HmRegs *r)
{
  carried(ram, r, 0x9068, 0x9086, 0x909D);
  if (!throwing(ram, r)) return;
  if (in_the_way(ram, r, 0x90CB)) {                        /* CODE_819195 */
    tile_ahead(ram, r, 1, 0, 0, 0x91A0);
    to_tile_ahead(ram, r);
    sprite(ram, r, 0, 0x91B5);
    r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F5C) & 0x0010);
    if (r->a) {
      uint8_t p = price_of(ram, r);
      if (p && before_5pm(ram, r)) {
        ship(ram, r, p);
        r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F5C) | 0x0040);
        hm_bus_wr16(ram, 0x7F1F5C, r->a);
      }
    }
    set_state(ram, r, 8);
    return;
  }
  uint8_t map = *hm_dp(ram, r, 0x22);
  set_a8(r, map);
  r->c = map >= 4;
  if (map < 4) goto flies;
  r->c = map >= 8;
  if (map >= 8) goto flies;
  set_a8(r, hm_bus_rd8(ram, 0x7F1F1A));
  if (r->a & 0xFF) goto flies;
  if (!before_5pm(ram, r)) goto flies;
  r->a = (uint16_t)(rd16(ram, r, 0x0196) & 0x0002);
  if (r->a) goto flies;
  tile_ahead(ram, r, 1, 0, 0, 0x9117);
  tile_property(ram, r, 0, 0x9126);
  if (!cpx(r, 0xF0)) goto flies;
  item_word(ram, r);
  r->x = times3(ram, r, r->a);
  r->a = hm_bus_rd16(ram, SALE_TABLE + r->x);
  if (!r->a) goto flies;
  to_tile_ahead(ram, r);
  sprite(ram, r, 0, 0x915D);
  set_state(ram, r, 5);
  hands_free(ram, r);
  return;
flies:
  fly(ram, r, 0x9187);
}

/* CODE_819228 (state 2 of CODE_81883A, an animal carried): thrown only
 * where it can stand: the tiles 0, 1 and 2 ahead free (property bits 0-4
 * clear, not type $FF) and the first not $E1 nor $C0-$CF. */
void hm_CODE_819228(uint8_t *ram, HmRegs *r)
{
  carried(ram, r, 0x9247, 0x9265, 0x927C);
  if (!throwing(ram, r)) return;
  if (in_the_way(ram, r, 0x92AA)) goto stop;
  static const struct { uint16_t a, xy, at_tile, at_prop; } look[] = {
    { 0, 7, 0x92C3, 0x92D2 }, { 1, 0x0A, 0x92F2, 0x9301 }, { 1, 0, 0x9321, 0x9330 },
  };
  for (unsigned i = 0; i < 3; i++) {
    tile_ahead(ram, r, look[i].a, look[i].xy, look[i].xy, look[i].at_tile);
    tile_property(ram, r, 2, look[i].at_prop);
    set_a8(r, (uint8_t)(r->a & 0x1F));
    if (r->a & 0xFF) goto stop;
    if (cpx(r, 0xFF)) goto stop;
  }
  if (cpx(r, 0xE1)) goto stop;
  r->c = r->x >= 0xC0;
  if (r->x >= 0xC0) {
    r->c = r->x >= 0xD0;
    if (r->x < 0xD0) goto stop;
  }
  fly(ram, r, 0x9362);
  return;
stop:
  stop_throw(ram, r);
}

/* ─────────────────────────────────────────────────────────────────────────
 * CODE_819379 (state 3): where a dropped or thrown item lands.
 *  - On the farm (maps 0-3), in the barn or the coop, at a shipping bin
 *    (tile $F2; HM-Decomp's Dropedonsaleplace): an item with a price is
 *    shipped (before 5 pm; after, it is simply lost) and the bin's
 *    animation plays ($24, $25 in the barn, $26 in the coop); state 7.
 *  - An egg ($14) put in the coop's incubator (tile $F7) when no egg is
 *    hatching ($7F1F6E bit 13): a chick is on its way (event $14, AddNewChicken,
 *    the incubator's tile), and 5 happiness (Droped_on_special_place).
 *  - On the farm, into a bin or a box (tiles $F0, $F9-$FD); on the
 *    mountain (maps $10-$14), into the spring ($F0: item 7 before 5 pm,
 *    once a day, $7F1F66 bit 13, starts an event) or the pond ($F4):
 *    the item goes there (state 4).
 *  - Otherwise it falls (sprite case $0A) on the tile in front; state 4.
 * Leftovers: those of the last routine called and the stores after it.
 * ───────────────────────────────────────────────────────────────────────── */
static void falls(uint8_t *ram, HmRegs *r)                  /* CODE_81965B */
{
  wr8(ram, r, IN_HAND, 0);
  sprite(ram, r, 0x0A, 0x9667);
  tile_ahead(ram, r, 2, 0, 0, 0x9676);
  to_tile_ahead(ram, r);
  set_state(ram, r, 4);
}

static void into_spring(uint8_t *ram, HmRegs *r)            /* CODE_8195F4 */
{
  uint8_t h = rd8(ram, r, IN_HAND);
  set_a8(r, h);
  r->c = h >= 7;
  if (h == 7 && before_5pm(ram, r)) {
    r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F66) & 0x2000);
    if (!r->a) {
      r->a = 0; r->x = 0x0010; r->y = 0;
      jsl(ram, r, 0x961B, 0x84, hm_UNK_LoadCCDataLong);
      r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F66) | 0x2000);
      hm_bus_wr16(ram, 0x7F1F66, r->a);
    }
  }
  r->a = 6;
  into_spot(ram, r);
}

static void not_hatching(uint8_t *ram, HmRegs *r)           /* .nothatchableegg */
{
  uint8_t map = *hm_dp(ram, r, 0x22);
  set_a8(r, map);
  r->c = map >= 4;
  if (map < 4) {                                           /* .onthefarm */
    tile_ahead(ram, r, 1, 6, 6, 0x959B);
    tile_property(ram, r, 2, 0x95AA);
    static const uint16_t bins[] = { 0xF0, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD };
    for (unsigned i = 0; i < 6; i++) {
      r->a = (uint16_t)i;
      if (cpx(r, bins[i])) { into_spot(ram, r); return; }
    }
    falls(ram, r);
    return;
  }
  r->c = map >= 0x10;
  if (map < 0x10) { falls(ram, r); return; }
  r->c = map >= 0x15;
  if (map >= 0x15) { falls(ram, r); return; }
  tile_ahead(ram, r, 1, 6, 6, 0x9562);
  tile_property(ram, r, 2, 0x9571);
  r->a = 6;
  if (cpx(r, 0xF0)) { into_spring(ram, r); return; }
  r->a = 7;
  if (cpx(r, 0xF4)) { into_spot(ram, r); return; }
  falls(ram, r);
}

static void special_place(uint8_t *ram, HmRegs *r)          /* Droped_on_special_place */
{
  uint8_t h = rd8(ram, r, IN_HAND);
  set_a8(r, h);
  r->c = h >= 0x14;
  if (h != 0x14) { not_hatching(ram, r); return; }
  uint8_t map = *hm_dp(ram, r, 0x22);
  set_a8(r, map);
  r->c = map >= 0x28;
  if (map != 0x28) { not_hatching(ram, r); return; }
  tile_ahead(ram, r, 1, 0, 0, 0x94B9);
  tile_property(ram, r, 0, 0x94C8);
  if (!cpx(r, 0xF7)) { not_hatching(ram, r); return; }
  r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F6E) & 0x2000);
  if (r->a) { not_hatching(ram, r); return; }
  r->a = 0x0014; r->x = 0; r->y = 0;                       /* .alreadyhatchingegg */
  jsl(ram, r, 0x94ED, 0x84, hm_SUB_8480F8);
  r->y = 0;
  set_a8(r, 3);
  hm_bus_wr8(ram, hm_dp_long(ram, r, 0xCC, 0), 3);
  r->a = 0;
  jsl(ram, r, 0x9501, 0x83, hm_AddNewChicken);
  r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F6E) | 0x2000);
  hm_bus_wr16(ram, 0x7F1F6E, r->a);
  r->a = 0x00A1; r->x = 0x00E0; r->y = 0x00B0;
  jsl(ram, r, 0x951B, 0x82, hm_EditTileonMap);
  remove_object(ram, r, 0x9526, 0x952A);
  wr8(ram, r, IN_HAND, 0);
  r->a = 5;
  jsl(ram, r, 0x9538, 0x83, hm_AddPlayerHappiness);
}

static void shipping_bin(uint8_t *ram, HmRegs *r)           /* Dropedonsaleplace */
{
  uint8_t p = price_of(ram, r);
  if (!p) { special_place(ram, r); return; }
  if (before_5pm(ram, r)) ship(ram, r, p);
  remove_object(ram, r, 0x9415, 0);
  uint8_t map = *hm_dp(ram, r, 0x22);
  set_a8(r, map);
  r->c = map >= 0x27;
  uint16_t anim = 0x24, at = 0x9430;
  if (map == 0x27) { anim = 0x25; at = 0x9454; }
  else {
    r->c = map >= 0x28;
    if (map == 0x28) { anim = 0x26; at = 0x9478; }
  }
  r->a = anim;
  jsl(ram, r, at, 0x81, hm_StartMapAnimation);
  set_state(ram, r, 7);
  r->a = (uint16_t)(hm_dp_r16(ram, r, GAME_STATE) & ~0x0001);
  hm_dp_w16(ram, r, GAME_STATE, r->a);
  wr8(ram, r, IN_HAND, 0);
}

void hm_CODE_819379(uint8_t *ram, HmRegs *r)
{
  uint8_t map = *hm_dp(ram, r, 0x22);
  set_a8(r, map);
  r->c = map >= 4;
  bool bins = map < 4;
  if (!bins) {
    r->c = map >= 0x27;
    bins = map == 0x27;
    if (!bins) { r->c = map >= 0x28; bins = map == 0x28; }
  }
  if (!bins) { special_place(ram, r); return; }
  tile_ahead(ram, r, 2, 0, 0, 0x93A2);
  tile_property(ram, r, 0, 0x93B1);
  if (!cpx(r, 0xF2)) { special_place(ram, r); return; }
  shipping_bin(ram, r);
}

/* ─────────────────────────────────────────────────────────────────────────
 * The items' own routines ($81:80FE-$81:87C2, JSR, RTS; DATA8_8197C0 gives
 * one per item, some shared). Named here ItemObject_<first item that uses
 * it>; HM-Decomp has no labels for them.
 *  - most only run their machine;
 *  - the crops ($10-$13) count one more shipped (corn, tomatoes, potatoes,
 *    turnips) in the frame they are shipped ($7F1F5E bit 0);
 *  - items $1E-$22 (with $7F1F74 bit 5 they vanish at once): put down where
 *    their place is (state 0: $09AC must be their number, else the tile
 *    becomes $FC, the item goes and text $038D shows), the tile at
 *    $09AD / $09AF becomes $A1; once dropped, $7F1F74 bit 6;
 *  - the shop goods, once bought (state $FF): items $1B-$1D become $58-$5A
 *    and $49 becomes $19, $56 becomes 6 (in hand, state 0); a seed bag or a
 *    tool goes to the shed (its bit in $7F1F00-$7F1F03), leaves the
 *    player's tool slots if it was there, and a bag counts one more (up to
 *    255); a gold tool also takes the plain one's place; fodder fills its
 *    counter (12).
 *  - the 26 tools ($2A-$43, CODE_81A94A + CODE_8187C3) wait for the
 *    frame routines and stay in the original.
 * Leftovers: the machine's, then the loads and compares after it.
 * ───────────────────────────────────────────────────────────────────────── */
#define ITEM_JSR(name, at, machine_fn) \
  void hm_##name(uint8_t *ram, HmRegs *r) { jsr(ram, r, at, machine_fn); }
ITEM_JSR(ItemObject_01, 0x80FE, hm_CODE_818855)
ITEM_JSR(ItemObject_02, 0x8102, hm_CODE_818855)
ITEM_JSR(ItemObject_03, 0x8106, hm_CODE_818855)
ITEM_JSR(ItemObject_04, 0x810A, hm_CODE_818855)
ITEM_JSR(ItemObject_05, 0x810E, hm_CODE_818855)
ITEM_JSR(ItemObject_06, 0x8112, hm_CODE_818855)
ITEM_JSR(ItemObject_07, 0x8116, hm_CODE_818855)
ITEM_JSR(ItemObject_08, 0x811A, hm_CODE_818855)
ITEM_JSR(ItemObject_09, 0x811E, hm_CODE_8188E5)
ITEM_JSR(ItemObject_0A, 0x8122, hm_CODE_8188E5)
ITEM_JSR(ItemObject_0D, 0x8126, hm_CODE_81889A)
ITEM_JSR(ItemObject_0E, 0x812A, hm_CODE_81889A)
ITEM_JSR(ItemObject_0F, 0x812E, hm_CODE_81889A)
ITEM_JSR(ItemObject_14, 0x8192, hm_CODE_8188E5)
ITEM_JSR(ItemObject_15, 0x8196, hm_CODE_8188E5)
ITEM_JSR(ItemObject_16, 0x819A, hm_CODE_8188E5)
ITEM_JSR(ItemObject_17, 0x819E, hm_CODE_8188E5)
ITEM_JSR(ItemObject_18, 0x81A2, hm_CODE_8188E5)
ITEM_JSR(ItemObject_19, 0x81A6, hm_CODE_8188E5)
ITEM_JSR(ItemObject_1A, 0x81AA, hm_CODE_8188BC)
ITEM_JSR(ItemObject_24, 0x83B7, hm_CODE_8188E5)
ITEM_JSR(ItemObject_25, 0x83BB, hm_CODE_81883A)
ITEM_JSR(ItemObject_26, 0x83BF, hm_CODE_81883A)
ITEM_JSR(ItemObject_29, 0x83C5, hm_CODE_81883A)
ITEM_JSR(ItemObject_58, 0x87B7, hm_CODE_8188E5)
ITEM_JSR(ItemObject_59, 0x87BB, hm_CODE_8188E5)
ITEM_JSR(ItemObject_5A, 0x87BF, hm_CODE_8188E5)

/* $23, $27, $28: nothing (RTS) */
void hm_ItemObject_23(uint8_t *ram, HmRegs *r) { (void)ram; (void)r; }
void hm_ItemObject_27(uint8_t *ram, HmRegs *r) { (void)ram; (void)r; }
void hm_ItemObject_28(uint8_t *ram, HmRegs *r) { (void)ram; (void)r; }

static void crop(uint8_t *ram, HmRegs *r, uint16_t at, uint32_t counter)
{
  jsr(ram, r, at, hm_CODE_8188E5);
  r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F5E) & 0x0001);
  if (!r->a) return;
  r->a = (uint16_t)(hm_bus_rd16(ram, counter) + 1);
  hm_bus_wr16(ram, counter, r->a);
}
void hm_ItemObject_10(uint8_t *ram, HmRegs *r) { crop(ram, r, 0x8132, 0x7F1F4A); }  /* corn */
void hm_ItemObject_11(uint8_t *ram, HmRegs *r) { crop(ram, r, 0x814A, 0x7F1F4C); }  /* tomatoes */
void hm_ItemObject_12(uint8_t *ram, HmRegs *r) { crop(ram, r, 0x8162, 0x7F1F50); }  /* potatoes */
void hm_ItemObject_13(uint8_t *ram, HmRegs *r) { crop(ram, r, 0x817A, 0x7F1F4E); }  /* turnips */

/* LDA $0976; CMP #$FF: bought? */
static bool bought(uint8_t *ram, HmRegs *r, uint16_t at)
{
  jsr(ram, r, at, hm_CODE_818923);
  uint8_t s = rd8(ram, r, STATE);
  set_a8(r, s);
  r->c = s >= 0xFF;
  return s == 0xFF;
}
/* it becomes another item, in hand */
static void becomes(uint8_t *ram, HmRegs *r, uint16_t at, uint8_t item)
{
  if (!bought(ram, r, at)) return;
  wr8(ram, r, STATE, 0);
  set_a8(r, item);
  wr8(ram, r, ITEM, item);
  wr8(ram, r, IN_HAND, item);
}
void hm_ItemObject_1B(uint8_t *ram, HmRegs *r) { becomes(ram, r, 0x81AE, 0x58); }
void hm_ItemObject_1C(uint8_t *ram, HmRegs *r) { becomes(ram, r, 0x81C8, 0x59); }
void hm_ItemObject_1D(uint8_t *ram, HmRegs *r) { becomes(ram, r, 0x81E2, 0x5A); }
void hm_ItemObject_49(uint8_t *ram, HmRegs *r) { becomes(ram, r, 0x85C5, 0x19); }
void hm_ItemObject_56(uint8_t *ram, HmRegs *r) { becomes(ram, r, 0x879D, 0x06); }

/* LDA.L row; ORA #bit; STA.L row (8 bits) */
static void shed_add(HmRegs *r, uint8_t *ram, uint32_t row, uint8_t bit)
{
  uint8_t v = (uint8_t)(hm_bus_rd8(ram, row) | bit);
  set_a8(r, v);
  hm_bus_wr8(ram, row, v);
}
/* the tool leaves the player's slots ($0921 in hand, $0923 in the bag) */
static void out_of_slots(uint8_t *ram, HmRegs *r, uint8_t tool)
{
  static const uint16_t slots[] = { 0x0921, 0x0923 };
  for (unsigned i = 0; i < 2; i++) {
    uint8_t v = rd8(ram, r, slots[i]);
    set_a8(r, v);
    r->c = v >= tool;
    if (v == tool) wr8(ram, r, slots[i], 0);
  }
}
/* a bag: to the shed, out of the slots, one more (up to 255) */
static void seed_bag(uint8_t *ram, HmRegs *r, uint16_t at, uint32_t row, uint8_t bit,
                     uint8_t tool, uint16_t count)
{
  if (!bought(ram, r, at)) return;
  shed_add(r, ram, row, bit);
  out_of_slots(ram, r, tool);
  uint8_t n = rd8(ram, r, count);
  set_a8(r, n);
  r->c = n >= 0xFF;
  if (n == 0xFF) return;
  set_a8(r, (uint8_t)(n + 1));
  wr8(ram, r, count, (uint8_t)(n + 1));
}
void hm_ItemObject_44(uint8_t *ram, HmRegs *r) { seed_bag(ram, r, 0x8499, 0x7F1F01, 0x08, 0x0C, 0x0927); }
void hm_ItemObject_45(uint8_t *ram, HmRegs *r) { seed_bag(ram, r, 0x84D5, 0x7F1F00, 0x10, 0x05, 0x0928); }
void hm_ItemObject_46(uint8_t *ram, HmRegs *r) { seed_bag(ram, r, 0x8511, 0x7F1F00, 0x20, 0x06, 0x0929); }
void hm_ItemObject_47(uint8_t *ram, HmRegs *r) { seed_bag(ram, r, 0x854D, 0x7F1F00, 0x40, 0x07, 0x092A); }
void hm_ItemObject_48(uint8_t *ram, HmRegs *r) { seed_bag(ram, r, 0x8589, 0x7F1F00, 0x80, 0x08, 0x092B); }

/* something for the shed only */
static void to_shed(uint8_t *ram, HmRegs *r, uint16_t at, uint32_t row, uint8_t bit)
{
  if (bought(ram, r, at)) shed_add(r, ram, row, bit);
}
void hm_ItemObject_4A(uint8_t *ram, HmRegs *r) { to_shed(ram, r, 0x85DF, 0x7F1F01, 0x10); }
void hm_ItemObject_4B(uint8_t *ram, HmRegs *r) { to_shed(ram, r, 0x85F6, 0x7F1F01, 0x20); }
void hm_ItemObject_4C(uint8_t *ram, HmRegs *r) { to_shed(ram, r, 0x860D, 0x7F1F01, 0x40); }
void hm_ItemObject_52(uint8_t *ram, HmRegs *r) { to_shed(ram, r, 0x8737, 0x7F1F01, 0x01); }
void hm_ItemObject_53(uint8_t *ram, HmRegs *r) { to_shed(ram, r, 0x874E, 0x7F1F01, 0x02); }

/* a gold tool: to the shed in place of the plain one, out of the slots */
static void gold_tool(uint8_t *ram, HmRegs *r, uint16_t at, uint8_t bit,
                      uint32_t old_row, uint8_t keep, uint8_t tool)
{
  if (!bought(ram, r, at)) return;
  shed_add(r, ram, 0x7F1F02, bit);
  uint8_t v = (uint8_t)(hm_bus_rd8(ram, old_row) & keep);
  set_a8(r, v);
  hm_bus_wr8(ram, old_row, v);
  out_of_slots(ram, r, tool);
}
void hm_ItemObject_4D(uint8_t *ram, HmRegs *r) { gold_tool(ram, r, 0x8624, 0x01, 0x7F1F00, 0xFE, 0x01); }
void hm_ItemObject_4E(uint8_t *ram, HmRegs *r) { gold_tool(ram, r, 0x865B, 0x02, 0x7F1F00, 0xFD, 0x02); }
void hm_ItemObject_4F(uint8_t *ram, HmRegs *r) { gold_tool(ram, r, 0x8692, 0x04, 0x7F1F00, 0xFB, 0x03); }
void hm_ItemObject_50(uint8_t *ram, HmRegs *r) { gold_tool(ram, r, 0x86C9, 0x08, 0x7F1F00, 0xF7, 0x04); }
void hm_ItemObject_51(uint8_t *ram, HmRegs *r) { gold_tool(ram, r, 0x8700, 0x10, 0x7F1F01, 0x7F, 0x10); }

/* fodder: its shed bit, and its counter full (12) */
static void fodder(uint8_t *ram, HmRegs *r, uint16_t at, uint8_t bit, uint16_t count)
{
  if (!bought(ram, r, at)) return;
  shed_add(r, ram, 0x7F1F03, bit);
  set_a8(r, 0x0C);
  wr8(ram, r, count, 0x0C);
}
void hm_ItemObject_54(uint8_t *ram, HmRegs *r) { fodder(ram, r, 0x8765, 0x01, 0x092D); }
void hm_ItemObject_55(uint8_t *ram, HmRegs *r) { fodder(ram, r, 0x8781, 0x02, 0x092C); }

/* items $1E-$22 */
static void vanish(uint8_t *ram, HmRegs *r)                  /* CODE_818337 */
{
  remove_object(ram, r, 0x833E, 0x8342);
  wr8(ram, r, IN_HAND, 0);
  hands_free(ram, r);
}
static void placed(uint8_t *ram, HmRegs *r)                  /* CODE_81835E */
{
  r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F74) | 0x0040);
  hm_bus_wr16(ram, 0x7F1F74, r->a);
}
static void wrong_place(uint8_t *ram, HmRegs *r)             /* CODE_81836C */
{
  r->a = 0x00FC;
  r->x = rd16(ram, r, 0x09AD);
  r->y = rd16(ram, r, 0x09AF);
  jsl(ram, r, 0x8377, 0x81, hm_ChangeMapTile);
  remove_object(ram, r, 0x8382, 0x8386);
  wr8(ram, r, IN_HAND, 0);
  hands_free(ram, r);
  wait_for_input(ram, r);
  r->x = 0x038D;
  say(ram, r, 0x83B1);
}
static void piece(uint8_t *ram, HmRegs *r, uint8_t n, uint16_t at_edit, uint16_t at_jsr)
{
  r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F74) & 0x0020);
  if (r->a) { vanish(ram, r); placed(ram, r); return; }
  uint8_t s = rd8(ram, r, STATE);
  set_a8(r, s);
  if (s == 0) {
    uint8_t k = rd8(ram, r, 0x09AC);
    set_a8(r, k);
    r->c = k >= n;
    if (k != n) { wrong_place(ram, r); return; }
    r->a = 0x00A1;
    r->x = rd16(ram, r, 0x09AD);
    r->y = rd16(ram, r, 0x09AF);
    jsl(ram, r, at_edit, 0x82, hm_EditTileonMap);
  }
  jsr(ram, r, at_jsr, hm_CODE_8188E5);
  s = rd8(ram, r, STATE);
  set_a8(r, s);
  r->c = s >= 3;
  if (s == 3) placed(ram, r);
}
void hm_ItemObject_1E(uint8_t *ram, HmRegs *r) { piece(ram, r, 1, 0x8228, 0x822C); }
void hm_ItemObject_1F(uint8_t *ram, HmRegs *r) { piece(ram, r, 2, 0x826A, 0x826E); }
void hm_ItemObject_20(uint8_t *ram, HmRegs *r) { piece(ram, r, 3, 0x82AC, 0x82B0); }
void hm_ItemObject_21(uint8_t *ram, HmRegs *r) { piece(ram, r, 4, 0x82EB, 0x82EF); }
void hm_ItemObject_22(uint8_t *ram, HmRegs *r) { piece(ram, r, 5, 0x8324, 0x8328); }

/* ─────────────────────────────────────────────────────────────────────────
 * CODE_81A94A ($81:A94A, JSL, RTL): a tool goes back on its shelf. Only
 * with something in the bag ($0923). The tool in hand ($0921) has an
 * 8-byte entry at $81:BE0F: +0 X, +2 Y, +4 the tile (ChangeMapTile's
 * arguments), +6 a row of the shed's flags ($7F:1F00 + it), +7 its bit.
 * If the shop it belongs to is the map shown (tools 1-$18: map $26, tool
 * $19: map $28, from $1A: map $27), its place on the shelf is redrawn and
 * sent at once (StartProgramedDMA). Then, wherever the player is (and for
 * tool 0 too), its bit is set in the shed's row.
 * Leftovers: A = the row's new value (high byte 0), X = Y = the row, C = 0;
 * with the bag empty, A's low byte 0 and the rest untouched.
 * ───────────────────────────────────────────────────────────────────────── */
#define SHELVES 0x81BE0F   /* 8 bytes a tool */
#define SHED_ROWS 0x7F1F00

void hm_CODE_81A94A(uint8_t *ram, HmRegs *r)
{
  set_a8(r, rd8(ram, r, 0x0923));
  if (!rd8(ram, r, 0x0923)) return;
  uint8_t tool = rd8(ram, r, 0x0921);
  set_a8(r, tool);
  if (tool) {
    uint8_t shop = tool < 0x19 ? 0x26 : tool == 0x19 ? 0x28 : 0x27;
    uint8_t map = rd8(ram, r, 0x0022);                     /* LDA $0022 (absolute) */
    set_a8(r, map);
    if (map == shop) {
      uint16_t x = (uint16_t)(tool * 8);
      uint16_t tx = hm_bus_rd16(ram, SHELVES + x), ty = hm_bus_rd16(ram, SHELVES + x + 2);
      hm_push8(ram, r, (uint8_t)(tx >> 8)); hm_push8(ram, r, (uint8_t)tx);   /* PHA; PHA; PLY; PLX */
      hm_push8(ram, r, (uint8_t)(ty >> 8)); hm_push8(ram, r, (uint8_t)ty);
      hm_pull8(ram, r); hm_pull8(ram, r); hm_pull8(ram, r); hm_pull8(ram, r);
      r->a = hm_bus_rd16(ram, SHELVES + x + 4);
      r->x = tx;
      r->y = ty;
      jsl(ram, r, 0xA9A6, 0x81, hm_ChangeMapTile);
      jsl(ram, r, 0xA9AA, 0x80, hm_StartProgramedDMA);
    }
  }
  uint16_t x = (uint16_t)(tool * 8 + 6);
  uint8_t row = hm_bus_rd8(ram, SHELVES + x), bit = hm_bus_rd8(ram, SHELVES + x + 1);
  *hm_dp(ram, r, 0x92) = bit;
  r->x = r->y = row;
  uint8_t v = (uint8_t)(hm_bus_rd8(ram, SHED_ROWS + row) | bit);
  hm_bus_wr8(ram, SHED_ROWS + row, v);
  r->a = v;
  r->c = 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * CODE_8187C3 ($81:87C3, JSR, RTS): a tool taken off the shelf (items
 * $29 + n, n = 1-$1A: the tools as shop articles). If the bag is empty
 * ($0923) the tool in hand goes into it; the new one is in hand ($0921 =
 * item - $29) and its bit in the shed's rows is cleared (the entry at
 * $81:BE0F, as for CODE_81A94A). The player's action ends ($D4 = 0, $D2
 * bit 1 cleared), the item's game object goes (CODE_8581A2) with the
 * extra drawing of the map refreshed (CODE_81A4F1), nothing is held
 * ($091D = 0) and the new tool's noises are loaded (ToolUsedSound2).
 * Leftovers: those of ToolUsedSound2.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_CODE_8187C3(uint8_t *ram, HmRegs *r)
{
  uint8_t entry = r->p;
  set_a8(r, rd8(ram, r, 0x0923));
  if (!rd8(ram, r, 0x0923)) {
    set_a8(r, rd8(ram, r, 0x0921));
    wr8(ram, r, 0x0923, (uint8_t)r->a);
  }
  uint8_t item = rd8(ram, r, ITEM);
  uint8_t tool = (uint8_t)(item - 0x29);                     /* SEC; SBC #$29 */
  r->c = item >= 0x29;
  wr8(ram, r, 0x0921, tool);
  uint16_t x = (uint16_t)((tool << 3) + 6);                  /* ASL x3; CLC; ADC #6 */
  r->c = 0;
  uint8_t row = hm_bus_rd8(ram, (SHELVES + x) & 0xFFFFFF);
  uint8_t bit = hm_bus_rd8(ram, (SHELVES + x + 1) & 0xFFFFFF);
  hm_push8(ram, r, row);                                     /* PHA ... PLA */
  *hm_dp(ram, r, 0x92) = (uint8_t)~bit;
  hm_pull8(ram, r);
  r->x = row;                                                /* XBA; LDA #0; XBA; TAX */
  uint8_t v = (uint8_t)(hm_bus_rd8(ram, SHED_ROWS + row) & ~bit);
  hm_bus_wr8(ram, SHED_ROWS + row, v);
  hm_dp_w16(ram, r, ACTION, 0);                              /* REP #$30 */
  r->a = (uint16_t)(hm_dp_r16(ram, r, GAME_STATE) & ~0x0002);
  hm_dp_w16(ram, r, GAME_STATE, r->a);
  r->a = rd16(ram, r, OBJECT);
  hm_dp_w16(ram, r, 0xA5, r->a);
  jsl(ram, r, 0x8828, 0x85, hm_CODE_8581A2);
  jsl(ram, r, 0x882C, 0x81, hm_CODE_81A4F1);
  wr8(ram, r, IN_HAND, 0);                                   /* SEP #$20; STZ $091D */
  r->p = entry;
  jsl(ram, r, 0x8835, 0x82, hm_ToolUsedSound2);
  r->p = entry;
}

/* ─────────────────────────────────────────────────────────────────────────
 * The 26 tools as items ($2A-$43, $81:83C9 + 8 n): each frame the tool in
 * hand goes back on its shelf (CODE_81A94A) and this one is taken
 * (CODE_8187C3): picking a tool off the shed's wall swaps it.
 * ───────────────────────────────────────────────────────────────────────── */
#define TOOL_ITEM(name, at)                                   \
  void hm_##name(uint8_t *ram, HmRegs *r)                     \
  {                                                           \
    jsl(ram, r, at, 0x81, hm_CODE_81A94A);                    \
    jsr(ram, r, (uint16_t)((at) + 4), hm_CODE_8187C3);        \
  }
TOOL_ITEM(ItemObject_2A, 0x83C9) TOOL_ITEM(ItemObject_2B, 0x83D1) TOOL_ITEM(ItemObject_2C, 0x83D9)
TOOL_ITEM(ItemObject_2D, 0x83E1) TOOL_ITEM(ItemObject_2E, 0x83E9) TOOL_ITEM(ItemObject_2F, 0x83F1)
TOOL_ITEM(ItemObject_30, 0x83F9) TOOL_ITEM(ItemObject_31, 0x8401) TOOL_ITEM(ItemObject_32, 0x8409)
TOOL_ITEM(ItemObject_33, 0x8411) TOOL_ITEM(ItemObject_34, 0x8419) TOOL_ITEM(ItemObject_35, 0x8421)
TOOL_ITEM(ItemObject_36, 0x8429) TOOL_ITEM(ItemObject_37, 0x8431) TOOL_ITEM(ItemObject_38, 0x8439)
TOOL_ITEM(ItemObject_39, 0x8441) TOOL_ITEM(ItemObject_3A, 0x8449) TOOL_ITEM(ItemObject_3B, 0x8451)
TOOL_ITEM(ItemObject_3C, 0x8459) TOOL_ITEM(ItemObject_3D, 0x8461) TOOL_ITEM(ItemObject_3E, 0x8469)
TOOL_ITEM(ItemObject_3F, 0x8471) TOOL_ITEM(ItemObject_40, 0x8479) TOOL_ITEM(ItemObject_41, 0x8481)
TOOL_ITEM(ItemObject_42, 0x8489) TOOL_ITEM(ItemObject_43, 0x8491)

/* ─────────────────────────────────────────────────────────────────────────
 * SUB_81809A ($81:809A, JSR, RTS): each frame, the routine of the item
 * being worked on ($0984), through the table DATA8_8197C0 (JSR (abs,X),
 * X = 2 item, A's high byte 0); then bit 0 of $7F1F5E (an item shipped
 * this frame) is cleared. Items 1-$5A have a routine; the table's entry
 * for item 0 points at $81:FFFF and the game never has it here.
 * Leftovers: A = the new $7F1F5E, X and Y those of the item's routine.
 * ───────────────────────────────────────────────────────────────────────── */
static Step *const ITEM_ROUTINES[0x5A] = {
  hm_ItemObject_01, hm_ItemObject_02, hm_ItemObject_03, hm_ItemObject_04,
  hm_ItemObject_05, hm_ItemObject_06, hm_ItemObject_07, hm_ItemObject_08,
  hm_ItemObject_09, hm_ItemObject_0A, hm_ItemObject_09, hm_ItemObject_09,
  hm_ItemObject_0D, hm_ItemObject_0E, hm_ItemObject_0F, hm_ItemObject_10,
  hm_ItemObject_11, hm_ItemObject_12, hm_ItemObject_13, hm_ItemObject_14,
  hm_ItemObject_15, hm_ItemObject_16, hm_ItemObject_17, hm_ItemObject_18,
  hm_ItemObject_19, hm_ItemObject_1A, hm_ItemObject_1B, hm_ItemObject_1C,
  hm_ItemObject_1D, hm_ItemObject_1E, hm_ItemObject_1F, hm_ItemObject_20,
  hm_ItemObject_21, hm_ItemObject_22, hm_ItemObject_23, hm_ItemObject_24,
  hm_ItemObject_25, hm_ItemObject_26, hm_ItemObject_27, hm_ItemObject_28,
  hm_ItemObject_29, hm_ItemObject_2A, hm_ItemObject_2B, hm_ItemObject_2C,
  hm_ItemObject_2D, hm_ItemObject_2E, hm_ItemObject_2F, hm_ItemObject_30,
  hm_ItemObject_31, hm_ItemObject_32, hm_ItemObject_33, hm_ItemObject_34,
  hm_ItemObject_35, hm_ItemObject_36, hm_ItemObject_37, hm_ItemObject_38,
  hm_ItemObject_39, hm_ItemObject_3A, hm_ItemObject_3B, hm_ItemObject_3C,
  hm_ItemObject_3D, hm_ItemObject_3E, hm_ItemObject_3F, hm_ItemObject_40,
  hm_ItemObject_41, hm_ItemObject_42, hm_ItemObject_43, hm_ItemObject_44,
  hm_ItemObject_45, hm_ItemObject_46, hm_ItemObject_47, hm_ItemObject_48,
  hm_ItemObject_49, hm_ItemObject_4A, hm_ItemObject_4B, hm_ItemObject_4C,
  hm_ItemObject_4D, hm_ItemObject_4E, hm_ItemObject_4F, hm_ItemObject_50,
  hm_ItemObject_51, hm_ItemObject_52, hm_ItemObject_53, hm_ItemObject_54,
  hm_ItemObject_55, hm_ItemObject_56, hm_ItemObject_0F, hm_ItemObject_58,
  hm_ItemObject_59, hm_ItemObject_5A,
};

void hm_SUB_81809A(uint8_t *ram, HmRegs *r)
{
  uint8_t item = rd8(ram, r, ITEM);
  r->a = (uint8_t)(item << 1);                               /* LDA #0; XBA; LDA; ASL */
  r->c = item >> 7;
  r->x = r->a;
  if (item >= 1 && item <= 0x5A) jsr(ram, r, 0x80A6, ITEM_ROUTINES[item - 1]);
  r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F5E) & 0xFFFE);    /* REP #$30 */
  hm_bus_wr16(ram, 0x7F1F5E, r->a);
}

/* ─────────────────────────────────────────────────────────────────────────
 * BAAAA ($81:A383, JSL, RTL; HM-Decomp's name): the items left on the map,
 * every frame. Ten records of 16 bytes at $7E:B4E6 ($07-$09 points at the
 * one worked on): +0 flags (bit 0 in use, bit 1 drawn on the map, bit 2
 * kept off screen), +1 the routine (1 = SUB_81809A, through the table
 * $81:A58B; 0 = none), +2 the state, +3 the item, +4 its game object,
 * +6 / +8 its sprite and the last one, +$A-$E the flip and the position.
 * For each record in use the fields are copied to $0974-$0982 (the item
 * being worked on), the object is placed (CODE_8581CB: $A7 = $FFFD when
 * it fell out of the map, $FFFF when it is on screen); out of the map it
 * is destroyed (CODE_8581A2) and, if drawn on the map, the map's extras
 * are redrawn (CODE_81A4F1); off screen it is skipped unless bit 2. Then
 * the routine runs and everything is copied back; the object follows
 * (CODE_8580B9) and gets the new sprite if it changed (CODE_858100).
 * Only routine 1 exists: the table's other words are the bytes of the
 * code after it.
 * Leftovers: A = $B586 (the pointer past the last record), X = $0A, C = 1.
 * ───────────────────────────────────────────────────────────────────────── */
#define ITEM_RECORDS 0x7EB4E6

static uint32_t rec_at(uint8_t *ram, HmRegs *r, uint16_t y) { r->y = y; return hm_dp_long(ram, r, 0x07, y); }

void hm_BAAAA(uint8_t *ram, HmRegs *r)
{
  static const uint16_t bytes[4] = { 0x0974, 0x0975, 0x0976, ITEM };
  static const uint16_t words[6] = { OBJECT, 0x097A, 0x097C, 0x097E, 0x0980, 0x0982 };
  hm_dp_w16(ram, r, 0x07, 0xB4E6);
  r->a = 0xB47E;                                             /* SEP #$20; LDA #$7E */
  *hm_dp(ram, r, 0x09) = 0x7E;
  r->x = 0;
  for (;;) {
    hm_push8(ram, r, (uint8_t)(r->x >> 8));                  /* PHX */
    hm_push8(ram, r, (uint8_t)r->x);
    uint8_t flags = hm_bus_rd8(ram, hm_dp_long(ram, r, 0x07, 0));
    set_a8(r, flags & 0x01);
    if (flags & 0x01) {
      for (int i = 0; i < 4; i++) {
        set_a8(r, hm_bus_rd8(ram, rec_at(ram, r, (uint16_t)i)));
        wr8(ram, r, bytes[i], (uint8_t)r->a);
      }
      for (int i = 0; i < 6; i++) {
        r->a = hm_bus_rd16(ram, rec_at(ram, r, (uint16_t)(4 + 2 * i)));
        wr16(ram, r, words[i], r->a);
        if (i == 0) hm_dp_w16(ram, r, 0xA5, r->a);
      }
      jsl(ram, r, 0xA3F5, 0x85, hm_CODE_8581CB);
      r->a = hm_dp_r16(ram, r, 0xA7);
      bool run = true;
      r->c = r->a >= 0xFFFD;
      if (r->a == 0xFFFD) {                                  /* out of the map */
        r->a = rd16(ram, r, OBJECT);
        hm_dp_w16(ram, r, 0xA5, r->a);
        jsl(ram, r, 0xA420, 0x85, hm_CODE_8581A2);
        set_a8(r, rd8(ram, r, 0x0974) & 0x02);
        if (r->a & 0xFF) jsl(ram, r, 0xA42D, 0x81, hm_CODE_81A4F1);
      } else {
        r->c = r->a >= 0xFFFF;
        if (r->a != 0xFFFF) {                                /* off screen */
          set_a8(r, rd8(ram, r, 0x0974) & 0x04);
          run = (r->a & 0xFF) != 0;
        }
      }
      if (run) {
        r->a = hm_bus_rd8(ram, rec_at(ram, r, 1));           /* LDA #0; XBA; LDA */
        r->c = (r->a & 0x8000) != 0;                          /* ASL (16-bit) */
        r->a = (uint16_t)(r->a << 1);
        r->x = r->a;
        r->c = 1;                                            /* CPX #0 */
        if (r->x) {
          if (r->x == 2) jsr(ram, r, 0xA444, hm_SUB_81809A);
          static const uint16_t back8[3][2] = { { 0, 0x0974 }, { 2, 0x0976 }, { 3, ITEM } };
          for (int i = 0; i < 3; i++) {
            set_a8(r, rd8(ram, r, back8[i][1]));
            hm_bus_wr8(ram, rec_at(ram, r, back8[i][0]), (uint8_t)r->a);
          }
          static const uint8_t to_dp[6] = { 0xA5, 0, 0, 0x9F, 0x9B, 0x9D };
          for (int i = 0; i < 6; i++) {
            r->a = rd16(ram, r, words[i]);
            hm_bus_wr16(ram, rec_at(ram, r, (uint16_t)(4 + 2 * i)), r->a);
            if (to_dp[i]) hm_dp_w16(ram, r, to_dp[i], r->a);
          }
          jsl(ram, r, 0xA49B, 0x85, hm_CODE_8580B9);
          r->a = rd16(ram, r, 0x097A);
          uint16_t last = rd16(ram, r, 0x097C);
          r->c = r->a >= last;
          if (r->a != last) {
            hm_dp_w16(ram, r, 0xA1, r->a);
            hm_bus_wr16(ram, rec_at(ram, r, 8), r->a);
            jsl(ram, r, 0xA4AE, 0x85, hm_CODE_858100);
          }
        }
      }
    }
    uint8_t lo = hm_pull8(ram, r);                           /* REP #$30; PLX */
    r->x = (uint16_t)(lo | hm_pull8(ram, r) << 8);
    uint32_t next = (uint32_t)hm_dp_r16(ram, r, 0x07) + 0x10;
    r->a = (uint16_t)next;
    hm_dp_w16(ram, r, 0x07, r->a);
    r->x++;
    r->c = r->x >= 0x0A;
    if (r->x == 0x0A) return;
  }
}
