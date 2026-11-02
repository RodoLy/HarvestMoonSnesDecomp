/*
 * interact.c — pressing A in front of something.
 *
 * PressAOnTile ($82:AD0E, JSL, RTL) is called by PlayerKeyA with X = the type
 * of the tile in front (GetTileProperty; 0 when it does not face the
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
 *            byte +2 bit 0, look for an event object in front (CollideEventObjects marks
 *            it: that is how the player talks to people and animals).
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

/* Also used, from hm_ram.h: HM_INPUT_STATE ($019A), HM_ITEM_ON_HAND ($091D),
 * HM_GAME_STATE ($D2; bit 6 = a change of map / an action is pending) and
 * HM_PLAYER_ACTION ($D4) */

/* WRAM $7F:1Fxx (bus addresses) */
#define SEASON              0x7F1F19 /* 8b: 0 spring ... 3 winter (HM_SEASON) */
#define WEEKDAY             0x7F1F1A /* 0 Sunday ... 6 Saturday */
#define HOUR                0x7F1F1C /* 8b: the hour (HM_HOUR) */
#define FLAGS_5A            0x7F1F5A /* 16b flags; bit 4 set by the jingle */

/* ROM tables */
#define RULES               0x80B6F5 /* Unk_Table13 */
#define PICK_ITEMS          0x82CFB4 /* DATA8_82CFB4 */
#define SONG_INSTRUMENTS    0x80B8CD /* per song, its instrument (- 1) */

/* WRAM of the data bank */
#define INSTRUMENT_COUNT    0x0103   /* 8b: the instruments noted (PrepareSoundEffect's slot) */
#define SOUND_EFFECT        0x0114   /* 8b: the sound effect to play / prepare */
#define SOUND_BANK          0x0115   /* 8b: its bank */
#define SONG_PLAYING        0x0118   /* 8b: the song playing (0 for none) */
#define LOADED_TOOL_SOUND   0x0119   /* 8b: the item whose noises are loaded; 0 = load again */
#define TEXT_BOX_LINES      0x0191   /* 8b: the lines of the next text box (0 = pick) */
#define WEATHER_FLAGS       0x0196   /* 16b: weather and day flags; bit 4 opens some doors */
#define PICKED_TILE_BYTE    0x091F   /* 8b: byte +0 of the tile picked up */
#define INTERACTION         0x096E   /* 8b: the place a door goes to / the action to run */
#define INTERACTION_STEP    0x096F   /* 8b: its step */
#define INTERACTION_FRAMES  0x0970   /* 8b: its frame counter */

/* Direct page */
#define TILE_PROPERTIES     0x0D     /* long $0D-$0F: the map's tile property table */
#define PICK_AT             0x7E     /* 16b: 4 x the tile type (the table offset) */
#define RULE_NUMBER         0x92     /* 8b: the door's opening-hours rule */
#define RULE_TEXT           0x93     /* 8b: the door's text */
#define HM_PLAYER_X         0xD6     /* 16b: the player's position, pixels */
#define HM_PLAYER_Y         0xD8     /* 16b */
#define HM_PLAYER_DIRECTION 0xDA     /* 16b: the way the player faces */
#define COLLIDE_X           0xDF     /* 16b: CollideEventObjects' input: the position */
#define COLLIDE_Y           0xE1     /* 16b */
#define COLLIDE_AHEAD       0xE3     /* 16b: how far ahead to look */
#define COLLIDE_STEP_X      0xE5     /* 16b: the step */
#define COLLIDE_STEP_Y      0xE7     /* 16b */

static uint32_t db_addr(const HmRegs *r, uint16_t a) { return (uint32_t)r->db << 16 | a; }
static uint16_t dp16(uint8_t *ram, const HmRegs *r, unsigned a) { return hm_dp_r16(ram, r, a); }
static void set16(uint8_t *ram, const HmRegs *r, unsigned a, uint16_t v) { hm_dp_w16(ram, r, a, v); }
static uint8_t lda8(HmRegs *r, uint8_t v)
{
  r->a = (uint16_t)((r->a & 0xFF00) | v);
  return v;
}
static uint8_t table8(uint8_t *ram, HmRegs *r, uint16_t y) { return hm_bus_rd8(ram, hm_dp_long(ram, r, TILE_PROPERTIES, y)); }
/* CPX #k: C = X >= k */
static bool below(HmRegs *r, uint16_t k) { r->c = r->x >= k; return !r->c; }

static void show_text(uint8_t *ram, HmRegs *r, uint16_t text, uint16_t ret)
{
  hm_bus_wr8(ram, db_addr(r, HM_INPUT_STATE), 2);          /* input: a text box */
  hm_bus_wr8(ram, db_addr(r, TEXT_BOX_LINES), 0);
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
  r->a = hm_bus_rd16(ram, db_addr(r, WEATHER_FLAGS)) & 0x0010;
  return r->a != 0;
}

void hm_PressAOnTile(uint8_t *ram, HmRegs *r)
{
  uint16_t type = r->x;
  uint16_t at = (uint16_t)(type << 2);                     /* TXA; ASL; ASL */

  if (!below(r, 0xF0)) {                                   /* a door */
    r->a = at;
    r->c = (type >> 14) & 1;                               /* ASL's carry */
    r->y = (uint16_t)(at + 1);
    if (!(lda8(r, table8(ram, r, r->y)) & 0x01)) { lda8(r, 0); return; }
    r->y = at;
    hm_bus_wr8(ram, db_addr(r, INTERACTION), lda8(r, table8(ram, r, at)));
    hm_bus_wr8(ram, db_addr(r, INTERACTION_STEP), 0);
    hm_bus_wr8(ram, db_addr(r, INTERACTION_FRAMES), 0);
    set16(ram, r, HM_GAME_STATE, (uint16_t)(dp16(ram, r, HM_GAME_STATE) | 0x0040));
    r->a = 0;
    set16(ram, r, HM_PLAYER_ACTION, 0);
    return;
  }
  if (!below(r, 0xE0)) return;
  if (!below(r, 0xD0)) {                                   /* a sign */
    r->y = at;
    r->c = (type >> 14) & 1;
    uint16_t text = hm_bus_rd16(ram, hm_dp_long(ram, r, TILE_PROPERTIES, at));
    r->a = (uint16_t)((text & 0xFF00) | 0x00);
    show_text(ram, r, text, 0xAEB5);
    return;
  }
  if (!below(r, 0xC0)) {                                   /* a door with opening hours */
    r->y = at;
    r->c = (type >> 14) & 1;
    uint8_t rule_no = table8(ram, r, at);
    *hm_dp(ram, r, RULE_NUMBER) = rule_no;
    r->y = (uint16_t)(at + 1);
    uint8_t text = table8(ram, r, r->y);
    *hm_dp(ram, r, RULE_TEXT) = text;
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
    hm_bus_wr8(ram, db_addr(r, PICKED_TILE_BYTE), lda8(r, table8(ram, r, at)));
    set16(ram, r, PICK_AT, at);
    uint32_t s = (uint32_t)at + hm_bus_rd8(ram, SEASON);   /* CLC; ADC $7E */
    r->c = s > 0xFFFF;
    r->x = (uint16_t)s;
    r->a = (uint16_t)s;
    hm_bus_wr8(ram, db_addr(r, HM_ITEM_ON_HAND), lda8(r, hm_bus_rd8(ram, PICK_ITEMS + r->x)));
    r->a = 4;
    set16(ram, r, HM_PLAYER_ACTION, 4);
    return;
  }
  r->y = (uint16_t)(at + 2);
  if (lda8(r, table8(ram, r, r->y)) & 0x01) {
    r->a = dp16(ram, r, HM_GAME_STATE) & 0x0020;           /* (both ways return) */
    return;
  }
  set16(ram, r, COLLIDE_X, dp16(ram, r, HM_PLAYER_X));
  set16(ram, r, COLLIDE_Y, dp16(ram, r, HM_PLAYER_Y));
  set16(ram, r, COLLIDE_STEP_X, 0);
  set16(ram, r, COLLIDE_STEP_Y, 0);
  set16(ram, r, COLLIDE_AHEAD, 0x0010);
  r->a = dp16(ram, r, HM_PLAYER_DIRECTION);
  hm_jsl(ram, r, 0xAEFE, 0x83);
  hm_CollideEventObjects(ram, r);
  hm_rtl(ram, r);
}

/* ─────────────────────────────────────────────────────────────────────────
 * The player's actions ($096E, set by pressing A on a door-like tile: see
 * PressAOnTile), run by PlayerInteractionSelector through the table $81:D593
 * (JSR, RTS each). Most entries are a lone RTS (Unused01-11). The others
 * end by clearing $D2 bit 6 (the "an action is pending" bit):
 *   2 GetFenceFromStack ($81:D644): take lumber from the stack (AddWood
 *     with -2): if there was enough, you hold item $57 and $D4 = 4 (the
 *     "carrying" action);
 *   6 / 7 PlayerAction_06_MapAnimation11 / PlayerAction_07_MapAnimation12: start map animation $11 / $12.
 * Leftovers: A = the new $D2, C from the last compare.
 * ───────────────────────────────────────────────────────────────────────── */
static void action_done(uint8_t *ram, HmRegs *r)
{
  r->a = (uint16_t)(hm_dp_r16(ram, r, HM_GAME_STATE) & ~0x0040);
  hm_dp_w16(ram, r, HM_GAME_STATE, r->a);
}

void hm_GetFenceFromStack(uint8_t *ram, HmRegs *r)
{
  r->a = 0xFFFE;
  hm_jsl(ram, r, 0xD64C, 0x83);
  hm_AddWood(ram, r);
  hm_rtl(ram, r);
  r->c = r->a >= 1;                                         /* CMP #1 */
  if (r->a != 1) {                                          /* 1 = none left */
    hm_bus_wr8(ram, (uint32_t)r->db << 16 | HM_ITEM_ON_HAND, 0x57);
    hm_dp_w16(ram, r, HM_PLAYER_ACTION, 0x0004);
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
void hm_PlayerAction_06_MapAnimation11(uint8_t *ram, HmRegs *r) { map_animation_action(ram, r, 0x11, 0xD6EA); }
void hm_PlayerAction_07_MapAnimation12(uint8_t *ram, HmRegs *r) { map_animation_action(ram, r, 0x12, 0xD700); }

/* the actions that do nothing: a lone RTS. Also the other routines that
 * are only a return: COP_Return ($80:872A, RTS), ScreenTransitionReturn
 * ($80:972B, RTL), ShippingReturn ($82:81BF, RTL; the end ShippingScene
 * and the next routine share) and CODE_83F4D7 ($83:F4D7, RTS). */
void hm_ActionNothing(uint8_t *ram, HmRegs *r) { (void)ram; (void)r; }

/* ─────────────────────────────────────────────────────────────────────────
 * PlayerAction_04_Jingle ($81:D670, JSR, RTS): action 4, run every frame while it
 * lasts. The first time ($096F != 1): the sound effect $19 of bank 6 with
 * the song's instrument (PrepareSoundEffect, played by PlaySoundEffect0A), $7F1F5A
 * bit 4, the map animation $44 (StartMapAnimation), $096F + 1. Then it
 * counts $0970 up to $20: there the action is over ($D2 bit 6 cleared) and
 * the noises of the item in hand are loaded again ($0119 = 0,
 * ToolUsedSound2).
 * PrepareSoundEffect keeps V, D and I of the entry flags (nothing here changes
 * them).
 * Leftovers: those of StartMapAnimation / ToolUsedSound2, or A = the
 * counter and C from its compare.
 * ───────────────────────────────────────────────────────────────────────── */
static uint32_t db_at(const HmRegs *r, uint16_t a) { return (uint32_t)r->db << 16 | a; }

void hm_PlayerAction_04_Jingle(uint8_t *ram, HmRegs *r)
{
  uint8_t entry = r->p;
  uint8_t state = hm_bus_rd8(ram, db_at(r, INTERACTION_STEP));
  r->a = (uint16_t)((r->a & 0xFF00) | state);
  r->c = state >= 1;
  if (state != 1) {
    hm_bus_wr8(ram, db_at(r, SOUND_EFFECT), 0x19);
    hm_bus_wr8(ram, db_at(r, SOUND_BANK), 0x06);
    r->a = hm_bus_rd8(ram, db_at(r, SONG_PLAYING));        /* LDA #0; XBA; LDA */
    r->x = r->a;
    uint8_t n = (uint8_t)(hm_bus_rd8(ram, (SONG_INSTRUMENTS + r->x) & 0xFFFFFF) + 1);
    r->a = (uint16_t)((r->a & 0xFF00) | n);
    hm_bus_wr8(ram, db_at(r, INSTRUMENT_COUNT), n);
    hm_jsl(ram, r, 0xD69D, 0x83);
    hm_PrepareSoundEffect(ram, r);
    hm_rtl(ram, r);
    hm_jsl(ram, r, 0xD6A1, 0x83);
    hm_PlaySoundEffect0A(ram, r);
    hm_rtl(ram, r);
    r->a = (uint16_t)(hm_bus_rd16(ram, FLAGS_5A) | 0x0010);
    hm_bus_wr16(ram, FLAGS_5A, r->a);
    r->a = 0x0044;
    hm_jsl(ram, r, 0xD6B7, 0x81);
    hm_StartMapAnimation(ram, r);
    hm_rtl(ram, r);
    hm_bus_wr8(ram, db_at(r, INTERACTION_STEP), (uint8_t)(hm_bus_rd8(ram, db_at(r, INTERACTION_STEP)) + 1));
    return;
  }
  uint8_t count = (uint8_t)(hm_bus_rd8(ram, db_at(r, INTERACTION_FRAMES)) + 1);
  hm_bus_wr8(ram, db_at(r, INTERACTION_FRAMES), count);
  r->a = (uint16_t)((r->a & 0xFF00) | count);
  r->c = count >= 0x20;
  if (count != 0x20) return;
  action_done(ram, r);                                       /* REP #$30 */
  hm_bus_wr8(ram, db_at(r, LOADED_TOOL_SOUND), 0);
  r->p = entry;
  hm_jsl(ram, r, 0xD6DF, 0x82);
  hm_ToolUsedSound2(ram, r);
  hm_rtl(ram, r);
  r->p = entry;
}
