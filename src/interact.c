/*
 * interact.c — pressing A in front of something.
 *
 * SUB_82AD0E ($82:AD0E, JSL, RTL) is called by PlayerKeyA with X = the type
 * of the tile in front (UNK_CheckTileProperty; 0 when it does not face the
 * player's side). The map's tile property table [$0D] has 4 bytes per type;
 * what pressing A does depends on the type:
 *
 *   $F0-$FF  a door: if byte +1 bit 0, go to place [+0] ($096E; $D2 bit 6
 *            starts the change of map)
 *   $E0-$EF  nothing
 *   $D0-$DF  a sign: show text [+0] (a word)
 *   $C0-$CF  a door with opening hours: [+0] picks a rule of Unk_Table13
 *            ($80:B6F5, 8 bytes each, byte 3 the rule's bits) and [+1] the
 *            text to show when the rule says so (0: never). Rule bits:
 *            $02 from 17:00, $04 before 17:00, $08 Monday-Friday,
 *            $10 Saturday, $20 Sunday, $80 when $0196 bit 4.
 *   below    in autumn and winter tiles $38 $39 $52 $53 $60 $61 $6E $6F
 *            do nothing; if byte +1 bit 7, pick the thing up: item
 *            DATA8_82CFB4[type * 4 + season] in hand ($091D), the tile's
 *            byte +0 in $091F, action 4 (holding it). Otherwise, unless
 *            byte +2 bit 0, look for an event object in front (CBBBB marks
 *            it: that is how the player talks to people and animals).
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

#define SEASON      0x7F1F19
#define WEEKDAY     0x7F1F1A   /* 0 Sunday ... 6 Saturday */
#define HOUR        0x7F1F1C
#define RULES       0x80B6F5   /* Unk_Table13 */
#define PICK_ITEMS  0x82CFB4   /* DATA8_82CFB4 */

static uint32_t db_addr(const HmRegs *r, uint16_t a) { return (uint32_t)r->db << 16 | a; }
static uint16_t dp16(uint8_t *ram, const HmRegs *r, unsigned a) { return hm_dp_r16(ram, r, a); }
static void set16(uint8_t *ram, const HmRegs *r, unsigned a, uint16_t v) { hm_dp_w16(ram, r, a, v); }
static uint8_t lda8(HmRegs *r, uint8_t v)
{
  r->a = (uint16_t)((r->a & 0xFF00) | v);
  return v;
}
static uint8_t table8(uint8_t *ram, HmRegs *r, uint16_t y) { return hm_bus_rd8(ram, hm_dp_long(ram, r, 0x0D, y)); }
/* CPX #k: C = X >= k */
static bool below(HmRegs *r, uint16_t k) { r->c = r->x >= k; return !r->c; }

static void show_text(uint8_t *ram, HmRegs *r, uint16_t text, uint16_t ret)
{
  hm_bus_wr8(ram, db_addr(r, 0x019A), 2);                  /* input: a text box */
  hm_bus_wr8(ram, db_addr(r, 0x0191), 0);
  r->x = text;
  hm_jsl(ram, r, ret, 0x83);
  hm_StartTextBox(ram, r);
  hm_rtl(ram, r);
}

/* the opening-hours rule of a $C0-$CF door: true = show the text */
static bool rule_applies(uint8_t *ram, HmRegs *r, uint8_t rule)
{
  lda8(r, rule & 0x02);
  if (rule & 0x02) {
    uint8_t h = lda8(r, hm_bus_rd8(ram, HOUR));
    r->c = h >= 0x11;
    if (r->c) return true;
  }
  lda8(r, rule & 0x04);
  if (rule & 0x04) {
    uint8_t h = lda8(r, hm_bus_rd8(ram, HOUR));
    r->c = h >= 0x11;
    if (!r->c) return true;
  }
  lda8(r, rule & 0x08);
  if (rule & 0x08) {
    uint8_t d = lda8(r, hm_bus_rd8(ram, WEEKDAY));
    if (d != 0) {
      r->c = d >= 6;
      if (d != 6) return true;
    }
  }
  lda8(r, rule & 0x10);
  if (rule & 0x10) {
    uint8_t d = lda8(r, hm_bus_rd8(ram, WEEKDAY));
    r->c = d >= 6;
    if (d == 6) return true;
  }
  lda8(r, rule & 0x20);
  if (rule & 0x20) {
    if (lda8(r, hm_bus_rd8(ram, WEEKDAY)) == 0) return true;
  }
  lda8(r, rule & 0x80);
  if (!(rule & 0x80)) return false;
  r->a = hm_bus_rd16(ram, db_addr(r, 0x0196)) & 0x0010;
  return r->a != 0;
}

void hm_SUB_82AD0E(uint8_t *ram, HmRegs *r)
{
  uint16_t type = r->x;
  uint16_t at = (uint16_t)(type << 2);                     /* TXA; ASL; ASL */

  if (!below(r, 0xF0)) {                                   /* a door */
    r->a = at;
    r->c = (type >> 14) & 1;                               /* ASL's carry */
    r->y = (uint16_t)(at + 1);
    if (!(lda8(r, table8(ram, r, r->y)) & 0x01)) { lda8(r, 0); return; }
    r->y = at;
    hm_bus_wr8(ram, db_addr(r, 0x096E), lda8(r, table8(ram, r, at)));
    hm_bus_wr8(ram, db_addr(r, 0x096F), 0);
    hm_bus_wr8(ram, db_addr(r, 0x0970), 0);
    set16(ram, r, 0xD2, (uint16_t)(dp16(ram, r, 0xD2) | 0x0040));
    r->a = 0;
    set16(ram, r, 0xD4, 0);
    return;
  }
  if (!below(r, 0xE0)) return;
  if (!below(r, 0xD0)) {                                   /* a sign */
    r->y = at;
    r->c = (type >> 14) & 1;
    uint16_t text = hm_bus_rd16(ram, hm_dp_long(ram, r, 0x0D, at));
    r->a = (uint16_t)((text & 0xFF00) | 0x00);
    show_text(ram, r, text, 0xAEB5);
    return;
  }
  if (!below(r, 0xC0)) {                                   /* a door with opening hours */
    r->y = at;
    r->c = (type >> 14) & 1;
    uint8_t rule_no = table8(ram, r, at);
    *hm_dp(ram, r, 0x92) = rule_no;
    r->y = (uint16_t)(at + 1);
    uint8_t text = table8(ram, r, r->y);
    *hm_dp(ram, r, 0x93) = text;
    r->a = text;
    if (!text) return;
    r->c = 0;                                              /* ASL x3 of a byte */
    r->x = (uint16_t)((uint16_t)(rule_no << 3) + 3);
    uint8_t rule = hm_bus_rd8(ram, RULES + r->x);
    r->a = (uint16_t)((r->x & 0xFF00) | rule);
    if (!rule_applies(ram, r, rule)) return;
    r->a = text;                                           /* LDA #0; XBA; LDA $93 */
    show_text(ram, r, text, 0xAE98);
    return;
  }

  /* the rest: things to pick up, or people and animals */
  uint8_t season = lda8(r, hm_bus_rd8(ram, SEASON));
  r->c = season >= 2;                                      /* CMP #2; CMP #3 */
  if (season != 2) r->c = season >= 3;
  if (season == 2 || season == 3) {
    static const uint8_t bare[] = { 0x38, 0x39, 0x52, 0x53, 0x60, 0x61, 0x6E, 0x6F };
    for (unsigned i = 0; i < sizeof bare; i++) {
      r->c = type >= bare[i];
      if (type == bare[i]) return;
    }
  }
  r->a = at;
  r->c = (type >> 14) & 1;
  r->y = (uint16_t)(at + 1);
  if (lda8(r, table8(ram, r, r->y)) & 0x80) {              /* pick it up */
    r->y = at;
    hm_bus_wr8(ram, db_addr(r, 0x091F), lda8(r, table8(ram, r, at)));
    set16(ram, r, 0x7E, at);
    uint32_t s = (uint32_t)at + hm_bus_rd8(ram, SEASON);   /* CLC; ADC $7E */
    r->c = s > 0xFFFF;
    r->x = (uint16_t)s;
    r->a = (uint16_t)s;
    hm_bus_wr8(ram, db_addr(r, 0x091D), lda8(r, hm_bus_rd8(ram, PICK_ITEMS + r->x)));
    r->a = 4;
    set16(ram, r, 0xD4, 4);
    return;
  }
  r->y = (uint16_t)(at + 2);
  if (lda8(r, table8(ram, r, r->y)) & 0x01) {
    r->a = dp16(ram, r, 0xD2) & 0x0020;                    /* (both ways return) */
    return;
  }
  set16(ram, r, 0xDF, dp16(ram, r, 0xD6));
  set16(ram, r, 0xE1, dp16(ram, r, 0xD8));
  set16(ram, r, 0xE5, 0);
  set16(ram, r, 0xE7, 0);
  set16(ram, r, 0xE3, 0x0010);
  r->a = dp16(ram, r, 0xDA);
  hm_jsl(ram, r, 0xAEFE, 0x83);
  hm_CBBBB(ram, r);
  hm_rtl(ram, r);
}

/* ─────────────────────────────────────────────────────────────────────────
 * The player's actions ($096E, set by pressing A on a door-like tile: see
 * SUB_82AD0E), run by PlayerInteractionSelector through the table $81:D593
 * (JSR, RTS each). Most entries are a lone RTS (Unused01-11). The others
 * end by clearing $D2 bit 6 (the "an action is pending" bit):
 *   2 GetFenceFromStack ($81:D644): take lumber from the stack (AddWood
 *     with -2): if there was enough, you hold item $57 and $D4 = 4 (the
 *     "carrying" action);
 *   6 / 7 CODE_81D6E2 / CODE_81D6F8: start map animation $11 / $12.
 * Leftovers: A = the new $D2, C from the last compare.
 * ───────────────────────────────────────────────────────────────────────── */
#define GAME_STATE    0xD2
#define PLAYER_ACTION 0xD4

static void action_done(uint8_t *ram, HmRegs *r)
{
  r->a = (uint16_t)(hm_dp_r16(ram, r, GAME_STATE) & ~0x0040);
  hm_dp_w16(ram, r, GAME_STATE, r->a);
}

void hm_GetFenceFromStack(uint8_t *ram, HmRegs *r)
{
  r->a = 0xFFFE;
  hm_jsl(ram, r, 0xD64C, 0x83);
  hm_AddWood(ram, r);
  hm_rtl(ram, r);
  r->c = r->a >= 1;                                         /* CMP #1 */
  if (r->a != 1) {                                          /* 1 = none left */
    hm_bus_wr8(ram, (uint32_t)r->db << 16 | 0x091D, 0x57);
    hm_dp_w16(ram, r, PLAYER_ACTION, 0x0004);
  }
  action_done(ram, r);
}

static void map_animation_action(uint8_t *ram, HmRegs *r, uint16_t animation, uint16_t ret)
{
  r->a = animation;
  hm_jsl(ram, r, ret, 0x81);
  hm_StartMapAnimation(ram, r);
  hm_rtl(ram, r);
  action_done(ram, r);
}
void hm_CODE_81D6E2(uint8_t *ram, HmRegs *r) { map_animation_action(ram, r, 0x11, 0xD6EA); }
void hm_CODE_81D6F8(uint8_t *ram, HmRegs *r) { map_animation_action(ram, r, 0x12, 0xD700); }

/* the actions that do nothing: a lone RTS. Also the other routines that
 * are only a return: COP_Return ($80:872A, RTS), ScreenTransitionReturn
 * ($80:972B, RTL), ShippingReturn ($82:81BF, RTL; the end ShippingScene
 * and the next routine share) and CODE_83F4D7 ($83:F4D7, RTS). */
void hm_ActionNothing(uint8_t *ram, HmRegs *r) { (void)ram; (void)r; }
