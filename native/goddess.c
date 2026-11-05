/*
 * goddess.c — the Goddess of Spring (an improvement, off by default): her
 * hearts, her lines and the proposal. She is the game's own (event group
 * $17, the small pond on the mountain that the winter quake opens, where
 * using the axe brings her out once).
 *
 * Offerings. Whatever the player throws into her small pond (tile $F4 on
 * the mountain, src/items.c) is an offering; the game's own scene with the
 * axe is left as it is. The first one each day counts: it raises (or
 * lowers) her hearts by what the thing is worth, read from the game's own
 * sale table ($81:A094, the money a sale pays), and her favourites — the
 * dear ones — count much more. Later offerings that day only get a line.
 *
 *   favourites  Fullmoon Berry, Large Milk, Cake
 *   disliked    Poison Mushroom
 *   the rest    their sale money (10 to 30)
 *
 * At ten hearts, a feather proposes, shown at her pond (see The proposal,
 * below): not the Blue Feather the girls are shown, but the Golden
 * Feather, sold by the same woman for more (see The Golden Feather).
 *
 * Hearts go from 0 to 1000 (ten hearts, 100 each, as the girls' in the
 * pause menu) and are kept in the diary (src/save.c, +26 to +2B).
 *
 * The wedding. A week after the proposal, the next time the player is on
 * the farm and free to move, the game's own wedding plays: the scene in
 * front of the church (map $2D, event group $1E) that a girl's yes leads
 * to. Its first script picks the bride by the wife's bit ($7F1F66 bits
 * 0-4), so Maria's bit is lent for that moment and taken back as soon as
 * the scene is on (nothing later in it reads the bit, and the night that
 * follows the scene must not find a wife). While it plays, the slots are
 * dressed through g_hm_slot_picture (UpdateSlotSprite, src/events.c):
 *   - the bride (slot 1, Maria's wedding sprites $330-$333) is the
 *     Goddess ($315 / $316, the frames of her scene at the pond);
 *   - slot 3 and the six guests (slots 4-9) are the seven Harvest Sprites
 *     (the elves: $309-$30E, front, back and side), facing the couple and
 *     stepping in place now and then;
 *   - the pastor (slot 2) and the groom (slot $0A) stay as they are.
 * Her frames use OBJ palette 0, the elves' palette 5. Palettes 4-7 take
 * the town's colour math (they come out washed), so the elves are moved
 * to palette 0 too: the slot's flip word is XORed into each piece's OAM
 * attributes (DrawGameObject, src/objects.c), and $0A there turns 5 into
 * 0. In the town palette 0 holds a townsman's colours; no one left on
 * screen needs them, so it is given hers ($A9:E000, the OBJ table's last,
 * which is the elves' too) every frame of the scene.
 *
 * Married life. From then on she is seen, as herself: the game's event
 * slot $13 (the one it gives a wife) holds her picture ($315 / $316, now
 * and then the other, as if breathing) running an idle script of the
 * game's own ($B3:8DED: wait a frame, again). Where:
 *   - about one day in four she spends at home: in the house all day;
 *   - the other days she is by the barn's door from 6:00 until 18:00, and
 *     at home after that;
 *   - and on those days, at midday (11:00 to 15:00, when the wives go to
 *     their chores: Maria sweeps), she has her own: she floats over the
 *     field and blesses it, with the sparkles of her scene at the pond
 *     ($317, in slot $14, a child's: she has none). Only then.
 * She is a goddess: she does not walk, she floats. She bobs up and down
 * (a pixel or two, slowly) and glides from one of a few places to another
 * (around the barn's door; at home: the bed, the
 * table, the TV), resting a while at each. She
 * keeps still while the player is close, so she can be talked to.
 * Her pictures use OBJ palette 0 and her sparkles palette 3; with her as
 * the wife there is no other wife or child to need palette 3, so both are
 * drawn with it (the flip word's palette bits, $06 for her) and it is given
 * her colours while she is on the map.
 *
 * Everything she says, at the pond too (offerings, the hint, the
 * proposal), is in the game's own text box with its letter sound.
 *
 * Talking to her: A in front of her opens the game's own text box
 * (textbox.c: its frame and font, the letters one by one with the game's
 * letter sound), with her line for the place and the day; talking again
 * that day brings the next line. Her words are in English, as the game's.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ppu.h"
#include "hm_decomp.h"
#include "hm_ram.h"
#include "hm_vars.h"
#include "native.h"
#include "config.h"
#include "i18n.h"
#include "host.h"
#include "goddess.h"

extern Ppu *g_ppu;
#include "textbox.h"
#include "ui.h"

#define SALE_TABLE 0x81A094            /* per item 3 bytes: its text, then the money */
/* GODDESS_HINTED/ENGAGED/MARRIED: goddess.h. Engaged: the wedding is a week
 * after the proposal (the day of the last offering). */
enum {
  IT_POISON_MUSHROOM = 0x02, IT_FULLMOON_BERRY = 0x05, IT_CAKE = 0x06,
  IT_LARGE_MILK = 0x17,
};

static int today(void) { return g_ram[HM_YEAR] * 120 + (g_ram[HM_SEASON] & 3) * 30 + g_ram[HM_DAY]; }

static unsigned sale_money(uint8_t item)
{
  uint32_t a = SALE_TABLE + 3u * item + 2u;
  return g_rom[(a >> 16 & 0x3F) << 15 | (a & 0x7FFF)];
}

static void add_hearts(int d)
{
  int v = (int)g_hm_goddess_hearts + d;
  if (v < 0) v = 0;
  if (v > 1000) v = 1000;
  g_hm_goddess_hearts = (uint16_t)v;
}

/* her line for an offering, by how much she likes you now (0-3: up to 299,
 * 599, 899, more) and how much she liked the thing (0 disliked, 1 plain,
 * 2 fine, 3 a favourite) */
static const char *const kLines[4][4] = {
  { "...Poison? Please don't.",
    "Oh, an offering. Thank you, traveler.",
    "That's a fine thing to give. Thank you.",
    "Oh! This is one of my favourites. How did you know?" },
  { "That's not something I can accept.",
    "You come here often. The pond is glad.",
    "You always bring such nice things.",
    "My favourite! You remembered!" },
  { "...I thought you liked me.",
    "I was hoping you'd come today.",
    "Thank you. I think about your farm a lot.",
    "You spoil me... I'll treasure it." },
  { "Why would you bring me this?",
    "Seeing you is the best part of my day.",
    "Every time you come, the water shines brighter.",
    "I love it... and I think I love you, too." },
};

/* What she says goes in the game's own text box (textbox.c), as the
 * game's people talk: each say() is wrapped to the box's width and starts
 * a page of its own; the box opens at the end of the frame
 * (goddess_dialog_wanted). */
static char s_said[6][TEXTBOX_LINES][TEXTBOX_COLUMNS + 1];
static int s_said_pages;
static void say(const char *words)
{
  int line = 0;
  const char *p = words;
  while (*p && s_said_pages < 6) {
    char *out = s_said[s_said_pages][line];
    int n = 0;
    while (*p == ' ') p++;
    for (;;) {                                       /* whole words while they fit */
      const char *end = p;
      while (*end && *end != ' ') end++;
      int len = (int)(end - p);
      if (n && n + 1 + len > TEXTBOX_COLUMNS) break;
      if (!n && len > TEXTBOX_COLUMNS) len = TEXTBOX_COLUMNS;   /* a word longer than a line */
      if (n) out[n++] = ' ';
      memcpy(out + n, p, (size_t)len); n += len; out[n] = 0;
      p += len;
      while (*p == ' ') p++;
      if (!*p) break;
    }
    if (++line == TEXTBOX_LINES || !*p) { s_said_pages++; line = 0; }
  }
}

static void offer(uint8_t *ram, uint8_t item)
{
  (void)ram;
  if (!g_cfg.goddess || item == 0) return;
  if (g_hm_goddess_flags & GODDESS_ENGAGED) {
    say("I'm getting ready for our wedding. See you soon!");
    return;
  }
  int d, like;
  switch (item) {
  case IT_FULLMOON_BERRY: d = 160; like = 3; break;
  case IT_LARGE_MILK:     d = 120; like = 3; break;
  case IT_CAKE:           d = 120; like = 3; break;
  case IT_POISON_MUSHROOM: d = -60; like = 0; break;
  default: {
    unsigned m = sale_money(item);
    d = m ? (int)m : 5;
    like = m >= 30 ? 2 : 1;
  }
  }
  if (g_hm_goddess_day == (uint16_t)today()) {
    say("You already brought me something today. Come back tomorrow!");
    return;
  }
  int tier = g_hm_goddess_hearts < 300 ? 0 : g_hm_goddess_hearts < 600 ? 1 : g_hm_goddess_hearts < 900 ? 2 : 3;
  g_hm_goddess_day = (uint16_t)today();
  add_hearts(d);
  say(kLines[tier][like]);
  if (g_hm_goddess_hearts >= 1000 && !(g_hm_goddess_flags & GODDESS_HINTED)) {
    g_hm_goddess_flags |= GODDESS_HINTED;
    say("...If you ever showed me a Golden Feather here, I might say something important.");
  }
}

/* ── The proposal ───────────────────────────────────────────────────────
 * The Blue Feather is a tool, not a thing to throw: it is shown (used),
 * as to the girls. Shown facing her small pond (tile type $F4 in front, on
 * the mountain), with her ten hearts, the house built up once and no other
 * wife, it is a yes; the day is kept, the wedding is a week later.
 * ToolUsedBlueFeather calls feather_used during the game's frame; the
 * pond is looked at once the frame is over.
 *
 * She comes out to answer, in the game's own scene: the one the axe calls
 * at her pond (event group $17, script 0, slot 0). Its helper (slot 1)
 * rises out of the water as her ($315), with her sparkles (slot 2), waits
 * half a second and starts its text ($31E: her greeting and the gold axe's
 * question); then the answer picks what follows (0 / 1, texts $31F /
 * $320), and either way she goes back ($316) and the scene ends. Here that
 * text is not opened (g_hm_script_text): her answer to the feather is said
 * in its place, in the game's text box, and the answer is left as neither
 * 0 nor 1, so the scene goes straight to her leaving. The gold axe that
 * floats up and down to the player meanwhile (slot 2, $B4:8E94 / 8EAE) is
 * her sparkles instead ($317, moved to her palette 0 by the flip word),
 * in this scene only: the axe's own is left as it is. */
enum { POND_SCENE_GROUP = 0x17, POND_SCENE_TEXT = 0x31E, NO_ANSWER = 0xFF, POND_SPARKLES = 0x317 };
static int slot16(uint32_t at, int field);          /* below, with the wedding */
static void dress(uint32_t at, int sprite, bool flip, int palette_xor);
static enum { NO_SCENE, SCENE_STARTED, SCENE_TALKING } s_scene;

static bool shop_text(uint8_t *ram, uint16_t text);   /* The Golden Feather, below */
static void seller_sunday(void);
static void golden_shown(void);
static void bride_lift(void);                         /* the wedding, below */
static void night_wife(uint8_t *ram, bool done);   /* Children, below */
static void lend_for_ending(uint8_t *ram);
static bool scene_text(uint8_t *ram, uint16_t text)
{
  if (shop_text(ram, text)) return true;
  if (s_scene != SCENE_STARTED || text != POND_SCENE_TEXT) return false;
  ram[HM_ANSWER] = NO_ANSWER;
  s_scene = SCENE_TALKING;                           /* her words, held until now, may show */
  return true;
}
static int s_scene_frames;                           /* the scene's frames: its sparkles, while it lasts */
static void scene_picture(uint8_t *ram, uint32_t at)
{
  (void)ram;
  if (!s_scene_frames || g_ram[at + SLOT_SCRIPT_BANK] != 0xB4) return;
  int pc = slot16(at, SLOT_SCRIPT);
  if (pc >= 0x8E94 && pc < 0x8EC5) dress(at, POND_SPARKLES, false, 3);   /* the gold axe's scripts */
}
static void call_her_out(void)
{
  HmRegs r;
  memset(&r, 0, sizeof r);
  r.db = 0x7E;
  r.a = 0; r.x = POND_SCENE_GROUP; r.y = 0;          /* slot 0, as the axe starts it */
  hm_StartEventScriptLong(g_ram, &r);
  s_scene = SCENE_STARTED;
  s_scene_frames = 1;
  g_hm_slot_picture = scene_picture;
}
static bool s_feather_used;
static void feather_used(void) { s_feather_used = true; }

static bool facing_her_pond(void)
{
  int map = g_ram[HM_MAP_ID];
  if (map < 0x10 || map > 0x13) return false;      /* the mountain, each season */
  static const int ahead[4][2] = { { 0, 16 }, { 0, -16 }, { 16, 0 }, { -16, 0 } };
  int f = hm_rd16(g_ram, 0xDA) & 3;                  /* the way the player faces */
  HmRegs r;
  memset(&r, 0, sizeof r);
  r.db = 0x7E;
  r.a = 2;
  r.x = (uint16_t)(hm_rd16(g_ram, HM_PLAYER_X) + ahead[f][0]);
  r.y = (uint16_t)(hm_rd16(g_ram, HM_PLAYER_Y) + ahead[f][1]);
  hm_GetTileProperty(g_ram, &r);                     /* X = the tile's type */
  return r.x == 0xF4;
}

static void propose(void)
{
  if (g_hm_goddess_flags & GODDESS_MARRIED) { say("I still have your feather, dear. It's by the pond."); return; }
  if (g_hm_goddess_flags & GODDESS_ENGAGED) { say("I'm getting ready for our wedding. See you soon!"); return; }
  if (hm_rd16(g_ram, HM_FLAGS_66) & 0x001F) { say("A feather...? You already gave your heart to someone. Be good to her."); return; }
  if (g_hm_goddess_hearts < 1000) { say("A feather...? How pretty. But we hardly know each other yet."); return; }
  if (!(g_hm_goddess_flags & GODDESS_GOLDEN_FEATHER)) {
    say("A Blue Feather...? It's lovely, but it's for the girls in town. A goddess's feather shines like gold.");
    return;
  }
  if (!(hm_rd16(g_ram, HM_FLAGS_64) & 0x0040)) { say("A Golden Feather... for me? Oh... But where would we live? Your house is so small."); return; }
  g_hm_goddess_day = (uint16_t)today();
  g_hm_goddess_flags |= GODDESS_ENGAGED;
  say("A Golden Feather... for me? Yes. Yes, I'll marry you! In a week, at the church.");
}

void goddess_frame(void)
{
  g_hm_goddess_on = g_cfg.goddess;
  g_hm_spring_offer = g_cfg.goddess ? offer : NULL;
  g_hm_feather_used = g_cfg.goddess ? feather_used : NULL;
  g_hm_script_text = g_cfg.goddess ? scene_text : NULL;
  g_hm_night_wife = g_cfg.goddess ? night_wife : NULL;
  if (g_cfg.goddess && hm_diary_loaded(g_ram)) { seller_sunday(); golden_shown(); lend_for_ending(g_ram); }
  if (g_cfg.goddess) bride_lift();
  if (s_feather_used) {
    s_feather_used = false;
    if (g_cfg.goddess && hm_diary_loaded(g_ram) && facing_her_pond() && s_scene == NO_SCENE) {
      propose();                                     /* her words, said when she is out */
      call_her_out();
    }
  }
  if (s_scene_frames && ++s_scene_frames > 900) {   /* the scene is long over (15 s): the axe's is the game's again */
    s_scene_frames = 0;
    if (g_hm_slot_picture == scene_picture) g_hm_slot_picture = NULL;
  }
  static int waited;                                 /* her text never came (the scene cut short): give up */
  if (s_scene == SCENE_STARTED) { if (++waited > 600) s_scene = NO_SCENE; } else waited = 0;
}

/* ── The wedding ──────────────────────────────────────────────────────── */
enum {
  WEDDING_MAP = 0x2D,                  /* the transition's map: the town, set for the wedding */
  LENT_WIFE = 0x0001,                  /* Maria's bit: her branch of the scene is the one dressed */
  SLOT_BRIDE = 1, SLOT_FIRST_GUEST = 3, SLOT_LAST_GUEST = 9,
  BRIDE_SPRITES = 0x330,               /* $330-$333: Maria in her wedding dress */
  GODDESS_SPRITE = 0x315,              /* and $316: the Goddess, her hands together */
  ELF_FRONT = 0x309, ELF_BACK = 0x30B, ELF_SIDE = 0x30D,   /* each and its step frame (+1) */
  COUPLE_X = 368,                      /* between the groom and the bride, at the door */
  GROUP_1E_FROM = 0x967A, GROUP_1E_TO = 0x9A3F,   /* the scene's scripts, $B4:967A-$B4:9A3E */
};
#define GODDESS_PALETTE 0xA9E000       /* file 0x14E000: green hair, white dress (the elves' too) */
static enum { NO_WEDDING, WEDDING_ASKED, WEDDING_ON } s_wedding;
static unsigned s_wedding_frames;

static bool player_free(void)          /* as the pause menu's (native/pause.c) */
{
  uint16_t act = hm_rd16(g_ram, HM_PLAYER_ACTION);
  return hm_diary_loaded(g_ram) && g_ram[HM_INPUT_STATE] == 1 &&
         (act == 0 || act == 1 || act == 2 || act == 9) && g_ram[HM_ITEM_ON_HAND] == 0;
}
static int slot16(uint32_t at, int field) { return g_ram[at + field] | g_ram[at + field + 1] << 8; }
/* the slot's picture: a sprite, the flip ($40) and a palette change XORed
 * into its pieces' palettes (bits 1-3 of the flip word: $0A turns 5 into 0) */
static void dress(uint32_t at, int sprite, bool flip, int palette_xor)
{
  int anim = (flip ? 0x40 : 0) | palette_xor << 1;
  if (slot16(at, SLOT_SPRITE) == sprite && (g_ram[at + SLOT_ANIMATION] & 0x4E) == anim) return;
  g_ram[at + SLOT_SPRITE] = (uint8_t)sprite; g_ram[at + SLOT_SPRITE + 1] = (uint8_t)(sprite >> 8);
  g_ram[at + SLOT_ANIMATION] = (uint8_t)((g_ram[at + SLOT_ANIMATION] & ~0x4E) | anim);
  g_ram[at + SLOT_FLAGS] |= 0x04;                     /* the picture changed */
}
/* g_hm_slot_picture during the scene: the bride and the guests */
static void wedding_picture(uint8_t *ram, uint32_t at)
{
  (void)ram;
  if (s_wedding == NO_WEDDING || g_ram[at + SLOT_SCRIPT_BANK] != 0xB4) return;
  int script = slot16(at, SLOT_SCRIPT);
  if (script < GROUP_1E_FROM || script >= GROUP_1E_TO) return;
  int n = g_ram[at + SLOT_NUMBER], sprite = slot16(at, SLOT_SPRITE);
  if (n == SLOT_BRIDE && sprite >= BRIDE_SPRITES && sprite < BRIDE_SPRITES + 4) {
    dress(at, GODDESS_SPRITE + (sprite >= BRIDE_SPRITES + 2), false, 0);   /* the last two: her second frame */
  } else if (n >= SLOT_FIRST_GUEST && n <= SLOT_LAST_GUEST) {
    int dx = slot16(at, SLOT_X) - COUPLE_X;
    bool step = ((s_wedding_frames + (unsigned)n * 13u) / 20u) % 4u == 0;   /* a hop now and then */
    if (dx > -24 && dx < 24) dress(at, ELF_BACK + step, false, 5);   /* right in front: looking up at them */
    else dress(at, ELF_SIDE + step, dx < 0, 5);                      /* the side frame looks left; flipped, right */
  }
}
static void wedding_colours(void)              /* OBJ palette 0: hers */
{
  for (int i = 0; i < 32; i++) g_ram[HM_CGRAM_SHADOW + 256 + i] = hm_bus_rd8(g_ram, GODDESS_PALETTE + (uint32_t)i);
}
/* Her figure stands 4 pixels lower in its frame than the bride's she
 * replaces: next to the groom her head and feet were under his. Lifted in
 * the picture chip's object list, after the game made it and before the
 * frame is drawn: the bride's own pieces only (around her slot, palette 0;
 * the elves stand farther off, the Water Imp beside her on the right). */
enum { BRIDE_LIFT = 4 };
static void bride_lift(void)
{
  if (s_wedding != WEDDING_ON || !g_ppu) return;
  uint32_t bride = 0xB586u + 64u * SLOT_BRIDE;
  int sprite = slot16(bride, SLOT_SPRITE);
  if (!g_ram[bride] || (sprite != GODDESS_SPRITE && sprite != GODDESS_SPRITE + 1)) return;
  int sx = slot16(bride, SLOT_X) - hm_rd16(g_ram, HM_CAM_X), sy = slot16(bride, SLOT_Y) - hm_rd16(g_ram, HM_CAM_Y);
  for (int i = 0; i < 128; i++) {
    uint16_t pos = g_ppu->oam[2 * i], attr = g_ppu->oam[2 * i + 1];
    int x = pos & 0xFF, y = pos >> 8;
    if (((attr >> 9) & 7) != 0 || y >= 224) continue;
    if (x < sx - 16 || x > sx + 8 || y < sy - 40 || y > sy + 8) continue;
    g_ppu->oam[2 * i] = (uint16_t)(x | (y - BRIDE_LIFT) << 8);
  }
}
/* every frame; true while the wedding is being started or played */
static bool wedding(uint8_t *ram)
{
  int map = ram[HM_MAP_ID];
  if (s_wedding == NO_WEDDING) {
    if (!(g_hm_goddess_flags & GODDESS_ENGAGED) || (g_hm_goddess_flags & GODDESS_MARRIED)) return false;
    if (today() < g_hm_goddess_day + 7 || map > 3 || !player_free()) return false;
    if (hm_rd16(ram, HM_FLAGS_66) & 0x001F) return false;   /* married to a girl since: no wedding */
    /* as the girls' yes does it: her bit, then Event_TransitionToMap $2D */
    hm_wr16(ram, HM_FLAGS_66, (uint16_t)(hm_rd16(ram, HM_FLAGS_66) | LENT_WIFE));
    ram[HM_TRANSITION_MAP] = WEDDING_MAP;
    hm_wr16(ram, HM_WEATHER_FLAGS, (uint16_t)(hm_rd16(ram, HM_WEATHER_FLAGS) | 0x4000));
    s_wedding = WEDDING_ASKED;
    s_wedding_frames = 0;
    g_hm_slot_picture = wedding_picture;
    return true;
  }
  s_wedding_frames++;
  bool town = map >= 4 && map <= 7;
  if (town) wedding_colours();
  if (s_wedding == WEDDING_ASKED) {
    /* the scene is on once the bride's slot runs its script (group $1E) */
    uint32_t bride = 0xB586u + 64u * SLOT_BRIDE;
    int script = slot16(bride, SLOT_SCRIPT);
    if (town && ram[bride] && ram[bride + SLOT_SCRIPT_BANK] == 0xB4 && script >= GROUP_1E_FROM && script < GROUP_1E_TO) {
      hm_wr16(ram, HM_FLAGS_66, (uint16_t)(hm_rd16(ram, HM_FLAGS_66) & ~LENT_WIFE));   /* given back */
      g_hm_goddess_flags = (uint8_t)((g_hm_goddess_flags & ~GODDESS_ENGAGED) | GODDESS_MARRIED);
      s_wedding = WEDDING_ON;
    } else if (s_wedding_frames > 600) {               /* it never came: give the bit back, try again later */
      hm_wr16(ram, HM_FLAGS_66, (uint16_t)(hm_rd16(ram, HM_FLAGS_66) & ~LENT_WIFE));
      s_wedding = NO_WEDDING;
      g_hm_slot_picture = NULL;
      return false;
    }
  }
  if (s_wedding == WEDDING_ON) {
    if (town) return true;
    /* the scene is over (the next morning at home) */
    s_wedding = NO_WEDDING;
    g_hm_slot_picture = NULL;
    say("I'm yours now, for as long as the spring returns.");
  }
  return true;
}

/* ── The Golden Feather ─────────────────────────────────────────────────
 * Sold by the woman who sells the Blue Feather: on Sundays in town, until
 * 17:00 (event slot 6, script $B3:BAC5, started by the town's Sunday
 * script, group $03, once the house is built up and a girl has 250
 * hearts, while the Blue Feather is not bought, $7F1F6C bit 6). Once the
 * Goddess has her ten hearts, the woman is there on Sundays all the same
 * (put in her slot here if the town's script did not), and her offer
 * (text $1C3, the Blue Feather for 1,000 G, yes / no) becomes a choice in
 * the game's text box: the Blue Feather (her own sale goes on: answer 0),
 * the Golden Feather for 5,000 G (paid here; then answer 1, her "no" text
 * $1C5 kept closed), or no (answer 1, her "no"). (Her answer 0 goes on to
 * the money check, $2FF when it is short, and her sale.)
 *
 * The Golden Feather is the feather tool ($18) in its shed place
 * ($7F1F02 bit 7), golden (GODDESS_GOLDEN_FEATHER, kept in the diary): in
 * the pause menu it is named so and drawn gold, and when it is shown, the
 * piece the player holds up (tiles $80-$91, the tool's, in the player's
 * palette 4) takes a copy of palette 4 with the feather's two blues made
 * gold (OBJ palette 3, in the picture chip, for those frames only). The
 * pieces are known by their picture: the feather's four tiles in the ROM
 * (file 0x64800, 0x64820, 0x64A00, 0x64A20), compared with each piece's
 * tile in the picture chip's memory. The girls take it as the Blue
 * Feather it is underneath. */
enum {
  FEATHER_TOOL = 0x18,
  SELLER_SLOT = 6, SELLER_SCRIPT = 0xBAC5,     /* $B3:BAC5 */
  SELLER_OFFER = 0x1C3, SELLER_NO = 0x1C5,
  SELLER_UNTIL = 17,
  BLUE_BOUGHT = 0x0040,                       /* $7F1F6C */
  GOLDEN_PRICE = 500,                         /* in tens of G: 5,000 G */
  FEATHER_PLACE = 2, FEATHER_BIT = 0x80,      /* $7F1F00 + 2, its shed place */
  GOLD_PALETTE = 3,
};
static enum { NO_SHOP, SHOP_ASKED, SHOP_OPEN } s_shop;
static bool s_keep_no_text;                  /* her "no" after a golden sale: not said */
static int s_shop_sel;
static TextboxPrinter s_shop_printer;
static const char *s_shop_lines[2] = { "A feather for your love?", "Blue 1000G, golden 5000G." };
static const char *const kShopOpts[3] = { "Blue", "Golden", "No" };

bool goddess_golden_feather(void) { return g_cfg.goddess && (g_hm_goddess_flags & GODDESS_GOLDEN_FEATHER); }
static bool blue_bought(void) { return hm_rd16(g_ram, HM_FLAGS_6C) & BLUE_BOUGHT; }
static bool offers_gold(void)
{
  return g_cfg.goddess && g_hm_goddess_hearts >= 1000 && !(g_hm_goddess_flags & GODDESS_GOLDEN_FEATHER);
}

static bool shop_text(uint8_t *ram, uint16_t text)
{
  if (text == SELLER_NO && s_keep_no_text) { s_keep_no_text = false; return true; }
  if (text != SELLER_OFFER || !offers_gold()) return false;
  ram[HM_ANSWER] = 1;                                /* "no", unless chosen otherwise */
  s_shop = SHOP_ASKED;
  return true;
}

static void sell_golden(void)
{
  uint32_t money = hm_rd24(g_ram, HM_MONEY);
  if (money < GOLDEN_PRICE) { say("Oh, dear. You don't have enough money for it."); return; }
  money -= GOLDEN_PRICE;
  g_ram[HM_MONEY] = (uint8_t)money; g_ram[HM_MONEY + 1] = (uint8_t)(money >> 8); g_ram[HM_MONEY + 2] = (uint8_t)(money >> 16);
  g_hm_goddess_flags |= GODDESS_GOLDEN_FEATHER;
  /* the feather tool in the shed, unless it is already in hand or in the backpack */
  if (g_ram[HM_TOOL_SELECTED] != FEATHER_TOOL && g_ram[HM_TOOL_BACKPACK] != FEATHER_TOOL)
    g_ram[HM_SHED_ITEMS + FEATHER_PLACE] |= FEATHER_BIT;
  s_keep_no_text = true;
  say("Here you are. A Golden Feather, it shines like spring water. It's in your tool shed.");
}

/* her choice, drawn as the wife's offer is: two lines, the choices on the third */
static int shop_opt_x(int i) { static const int at[3] = { 1, 7, 15 }; return 16 + 8 * at[i]; }
static bool shop_opt_ok(int i) { return i != 0 || !blue_bought(); }
static int shop_letters(void)
{
  int n = textbox_letters(s_shop_lines[0]) + textbox_letters(s_shop_lines[1]);
  for (int i = 0; i < 3; i++) if (shop_opt_ok(i)) n += textbox_letters(kShopOpts[i]);
  return n;
}
static void shop_open(void)
{
  s_shop = SHOP_OPEN;
  s_shop_sel = 1;                                    /* the golden one */
  s_shop_printer.shown = s_shop_printer.tick = 0;
}
static bool shop_update(const UiInput *in)
{
  if (!textbox_print(&s_shop_printer, shop_letters(), in)) return false;
  if (in->pressed & (B_LEFT | B_UP)) do s_shop_sel = (s_shop_sel + 2) % 3; while (!shop_opt_ok(s_shop_sel));
  if (in->pressed & (B_RIGHT | B_DOWN)) do s_shop_sel = (s_shop_sel + 1) % 3; while (!shop_opt_ok(s_shop_sel));
  if (in->back || in->rclick) s_shop_sel = 2;
  else if (!(in->accept || in->click)) return false;
  s_shop = NO_SHOP;
  if (s_shop_sel == 0) g_ram[HM_ANSWER] = 0;         /* her own sale of the Blue Feather goes on */
  else if (s_shop_sel == 1) sell_golden();           /* answer 1: her "no" path, kept quiet */
  return true;
}
static void shop_draw(uint32_t *dst, int w)
{
  ui_target(dst, w, 224);
  int ox = (w - 256) / 2;
  textbox_frame(ox);
  int left = s_shop_printer.shown;
  for (int i = 0; i < 2; i++) textbox_line(ox, i, s_shop_lines[i], &left);
  for (int i = 0; i < 3; i++) {
    if (!shop_opt_ok(i)) continue;
    textbox_text(ox + shop_opt_x(i), 191, kShopOpts[i], &left);
    if (i == s_shop_sel && s_shop_printer.shown >= shop_letters()) {   /* the arrow, once all is printed */
      int ax = ox + shop_opt_x(i) - 7, ay = 191 + 6;
      for (int k = 0; k < 4; k++) ui_vline(ax + k, ay + k, 7 - 2 * k, UI_RGB(0xFFFFFF));
    }
  }
}

/* Sundays in town: she is there for the Goddess too */
static void seller_sunday(void)
{
  /* once a visit: half a second after the town comes in (a transition,
   * $0196 bit $4000, starts each visit), so its own Sunday script has
   * placed everyone first */
  static int since = -1;
  int map = g_ram[HM_MAP_ID];
  if (map < 4 || map > 7 || (hm_rd16(g_ram, HM_WEATHER_FLAGS) & 0x4000)) { since = 0; return; }
  if (since < 0 || ++since < 30) return;
  since = -1;
  if (!offers_gold() || g_ram[HM_WEEKDAY] != 0 || g_ram[HM_HOUR] >= SELLER_UNTIL) return;
  uint32_t at = 0xB586u + 64u * SELLER_SLOT;
  if (g_ram[at]) return;                             /* there already (or the slot is taken) */
  memset(&g_ram[at], 0, 0x40);
  g_ram[at + SLOT_IN_USE] = 1;
  g_ram[at + SLOT_FRAMES] = 1;
  g_ram[at + SLOT_SCRIPT] = (uint8_t)SELLER_SCRIPT; g_ram[at + SLOT_SCRIPT + 1] = (uint8_t)(SELLER_SCRIPT >> 8);
  g_ram[at + SLOT_SCRIPT_BANK] = 0xB3;
  g_ram[at + SLOT_NUMBER] = SELLER_SLOT;
}

/* the feather held up, golden: the tool's piece in a golden copy of the
 * player's palette, in the picture chip, while it is shown */
static void golden_shown(void)
{
  static bool was;
  bool now = g_ppu && goddess_golden_feather() && g_ram[HM_TOOL_SELECTED] == FEATHER_TOOL &&
             hm_rd16(g_ram, HM_PLAYER_ACTION) == 0x0A;        /* using the tool in hand */
  if (!g_ppu) return;
  uint16_t *cg = &g_ppu->cgram[128 + GOLD_PALETTE * 16];
  if (!now) {
    if (was)                                         /* palette 3 back as the game has it */
      for (int i = 0; i < 16; i++)
        cg[i] = (uint16_t)(g_ram[HM_CGRAM_SHADOW + 256 + GOLD_PALETTE * 32 + 2 * i] | g_ram[HM_CGRAM_SHADOW + 256 + GOLD_PALETTE * 32 + 2 * i + 1] << 8);
    was = false;
    return;
  }
  was = true;
  const uint16_t *p4 = &g_ppu->cgram[128 + 4 * 16];
  for (int i = 0; i < 16; i++) cg[i] = p4[i];
  cg[6] = 0x237F;                                    /* the light blue: gold (248, 216, 64) */
  cg[7] = 0x0E38;                                    /* the dark blue: dark gold (192, 136, 24) */
  static const uint32_t feather[4] = { 0x64800, 0x64820, 0x64A00, 0x64A20 };   /* its tiles, in the ROM */
  /* only the player's: the first such piece near him on screen (the
   * player is drawn first; the one on the seller's stall, the Blue
   * Feather for sale, comes after) */
  int sx = hm_rd16(g_ram, HM_PLAYER_X) - hm_rd16(g_ram, HM_CAM_X), sy = hm_rd16(g_ram, HM_PLAYER_Y) - hm_rd16(g_ram, HM_CAM_Y);
  for (int i = 0; i < 128; i++) {
    uint16_t w = g_ppu->oam[2 * i + 1];
    int tile = (w & 0xFF) | (w >> 8 & 1) << 8, pal = (w >> 9) & 7;
    if (pal != 4) continue;
    int ox = g_ppu->oam[2 * i] & 0xFF, oy = g_ppu->oam[2 * i] >> 8;
    if (abs(ox - sx) > 32 || abs(oy - sy) > 32) continue;
    uint32_t at = (tile < 256 ? PPU_objTileAdr1(g_ppu) : PPU_objTileAdr2(g_ppu)) + (uint32_t)(tile & 0xFF) * 16;
    const uint16_t *vram = &g_ppu->vram[at & 0x7FFF];
    bool is_feather = false;
    for (int k = 0; k < 4 && !is_feather; k++) {
      const uint8_t *t = &g_rom[feather[k]];
      is_feather = true;
      for (int j = 0; j < 16 && is_feather; j++) is_feather = vram[j] == (uint16_t)(t[2 * j] | t[2 * j + 1] << 8);
    }
    if (is_feather) { g_ppu->oam[2 * i + 1] = (uint16_t)((w & ~0x0E00) | GOLD_PALETTE << 9); break; }
  }
}

/* ── Married life ─────────────────────────────────────────────────────── */
#define HER_SLOT      0x0BA46u     /* event slot $13: $7E:B586 + $40 x $13 (fields: SLOT_*) */
#define SPARKLE_SLOT  0x0BB86u     /* event slot $18 ($14 and $15 are the children's) */
enum {
  IDLE_SCRIPT = 0x8DED,            /* $B3:8DED: WaitFrames 1, JumpToInstruction $8DED */
  SPARKLE_SPRITE = 0x317,
  HER_PALETTE = 3,                 /* OBJ palette 3: hers while she is on the map */
  GLIDE_SUBPIXELS = 16,            /* positions are kept in 1/16 pixels */
  GLIDE_SPEED = 6,                 /* 6/16 of a pixel a frame: slow */
  REST_FRAMES = 300,               /* 5 seconds at each place, more or less */
  BOB_PERIOD = 96,                 /* up and down every 1.6 s */
  NEAR_PLAYER = 40,                /* closer than this, she keeps still */
  HOBBY_FROM = 11, HOBBY_TO = 15,  /* her midday blessing of the field */
  AWAY_FROM = 18,                  /* her farm days: home from 18:00 */
};

/* ── Children ─────────────────────────────────────────────────────────
 * The game's own way. Each night WifePregnancyAndChildren (src/people.c)
 * counts the days married and, with the wife's love at 450 (650 for the
 * second), 20 days married and the house's second extension, a child is on
 * the way; 60 days later it is born. The routine finds the wife by her bit
 * in $7F1F66 and her love among the girls'; for that routine only, Maria's
 * bit and love are lent to the Goddess, and given back as soon as it is
 * done.
 *
 * The birth's days ($7F1F6E bit $10, group $1F in the house) show the wife
 * in bed, in OBJ palette 3, the wife's; her lines there are the same for
 * every wife. Without a girl's bit the game leaves her family out and keeps
 * the colours it has: the hair (colours 8-10) is made the Goddess's green. */
enum {
  MOST_LOVE = 999,                     /* the girls' love goes no higher */
  BIRTH_DAYS = 0x0010,                 /* $7F1F6E: the wife in bed (group $1F) */
  BORN_AT = 60,                        /* days from the child's start ($7F1F37 / $7F1F39) */
  BED_HAIR = 8,                        /* the wife in bed: her hair, colours 8-10 */
};
static bool s_lent_for_night;
static uint16_t s_marias_love;

static bool goddess_wife(uint8_t *ram)
{
  return (g_hm_goddess_flags & GODDESS_MARRIED) && !(hm_rd16(ram, HM_FLAGS_66) & 0x1F);
}
static void night_wife(uint8_t *ram, bool done)
{
  if (!done) {
    s_lent_for_night = goddess_wife(ram);
    if (!s_lent_for_night) return;
    s_marias_love = hm_rd16(ram, HM_HEARTS_BASE);
    hm_wr16(ram, HM_FLAGS_66, (uint16_t)(hm_rd16(ram, HM_FLAGS_66) | LENT_WIFE));
    hm_wr16(ram, HM_HEARTS_BASE, g_hm_goddess_hearts < MOST_LOVE ? g_hm_goddess_hearts : MOST_LOVE);
  } else if (s_lent_for_night) {
    s_lent_for_night = false;
    hm_wr16(ram, HM_FLAGS_66, (uint16_t)(hm_rd16(ram, HM_FLAGS_66) & ~LENT_WIFE));
    hm_wr16(ram, HM_HEARTS_BASE, s_marias_love);
  }
  if (done) lend_for_ending(ram);
}
/* The ending (summer 30 of the third year, from the morning: StartEnding,
 * src/animals.c, event group $2E) walks the wife and her family through
 * the farm and the town by the wife's bit. For the Goddess, Maria's bit is
 * lent for the whole last day (the ending ends the game), and the wife's
 * hair (OBJ palette 3, the same sprites as in bed) is her green. */
static bool s_lent_for_ending;
static bool ending_day(uint8_t *ram)
{
  return ram[HM_YEAR] == 2 && ram[HM_SEASON] == 1 && ram[HM_DAY] == 30;
}
static void lend_for_ending(uint8_t *ram)
{
  if (s_lent_for_ending || !ending_day(ram) || !goddess_wife(ram)) return;
  s_lent_for_ending = true;
  hm_wr16(ram, HM_FLAGS_66, (uint16_t)(hm_rd16(ram, HM_FLAGS_66) | LENT_WIFE));
}
static bool expecting(uint8_t *ram)
{
  uint16_t kids = hm_rd16(ram, HM_FLAGS_6E);
  return ((kids & 0x0004) && hm_rd16(ram, HM_KID1_AGE) < BORN_AT) ||
         ((kids & 0x0008) && hm_rd16(ram, HM_KID2_AGE) < BORN_AT);
}
static bool a_mother(uint8_t *ram)
{
  return (hm_rd16(ram, HM_FLAGS_6E) & 0x0004) && hm_rd16(ram, HM_KID1_AGE) >= BORN_AT;
}
static bool in_childbed(uint8_t *ram)
{
  return goddess_wife(ram) && (hm_rd16(ram, HM_FLAGS_6E) & BIRTH_DAYS);
}
/* her hair on the wife in bed: the Goddess's greens (her colours 6, 7, 5,
 * light to dark) in the wife's palette, in the buffers and the chip */
static void childbed_colours(void)
{
  static const uint32_t to[3] = { 0x10D00, 0x10B00, HM_CGRAM_SHADOW };
  static const int hers[3] = { 6, 7, 5 };
  for (int c = 0; c < 3; c++) {
    uint16_t colour = (uint16_t)hm_bus_rd16(g_ram, GODDESS_PALETTE + 2u * (uint32_t)hers[c]);
    int at = 256 + HER_PALETTE * 32 + 2 * (BED_HAIR + c);
    for (int k = 0; k < 3; k++) { g_ram[to[k] + at] = (uint8_t)colour; g_ram[to[k] + at + 1] = (uint8_t)(colour >> 8); }
    if (g_ppu) g_ppu->cgram[128 + HER_PALETTE * 16 + BED_HAIR + c] = colour;
  }
}

static bool home_day(void)
{
  unsigned d = ((unsigned)today() + 7919u) * 2654435761u;
  return ((d >> 16) & 3) == 0;                      /* about one day in four */
}
static bool slot_runs_idle(uint32_t at)
{
  int pc = slot16(at, SLOT_SCRIPT);                  /* somewhere in its two commands (6 bytes) */
  return g_ram[at] && g_ram[at + SLOT_SCRIPT_BANK] == 0xB3 && pc >= IDLE_SCRIPT && pc < IDLE_SCRIPT + 6;
}
/* the slot as one of hers: a picture standing still, its object made by the
 * game on its next frame (flag $02) */
static void put(uint32_t at, int number, int sprite, int palette_xor, int x, int y)
{
  memset(&g_ram[at], 0, 0x40);
  g_ram[at + SLOT_IN_USE] = 1;
  g_ram[at + SLOT_FLAGS] = 0x03;                    /* a picture; make its game object */
  g_ram[at + SLOT_FRAMES] = 1;
  g_ram[at + SLOT_SPRITE] = (uint8_t)sprite; g_ram[at + SLOT_SPRITE + 1] = (uint8_t)(sprite >> 8);
  g_ram[at + SLOT_LAST_SPRITE] = g_ram[at + SLOT_SPRITE]; g_ram[at + SLOT_LAST_SPRITE + 1] = g_ram[at + SLOT_SPRITE + 1];
  g_ram[at + SLOT_ANIMATION] = (uint8_t)(palette_xor << 1);
  g_ram[at + SLOT_X] = (uint8_t)x; g_ram[at + SLOT_X + 1] = (uint8_t)(x >> 8);
  g_ram[at + SLOT_Y] = (uint8_t)y; g_ram[at + SLOT_Y + 1] = (uint8_t)(y >> 8);
  memcpy(&g_ram[at + SLOT_HOME_X], &g_ram[at + SLOT_X], 4);
  g_ram[at + SLOT_SCRIPT] = (uint8_t)IDLE_SCRIPT; g_ram[at + SLOT_SCRIPT + 1] = (uint8_t)(IDLE_SCRIPT >> 8);
  g_ram[at + SLOT_SCRIPT_BANK] = 0xB3;
  g_ram[at + SLOT_NUMBER] = (uint8_t)number;
}
/* gone: the game drops its object (flag $40), and the slot is freed the
 * frame after */
static void take_away(uint32_t at)
{
  if (!slot_runs_idle(at)) return;
  if (g_ram[at + SLOT_FLAGS] & 0x40) { g_ram[at + SLOT_IN_USE] = 0; return; }
  g_ram[at + SLOT_FLAGS] |= 0x40;
}
/* her colours in OBJ palette 3: in the colours the game shows ($7F:0D00,
 * copied to the picture chip's every frame, src/palette.c), the night's
 * it prepares ($7F:0B00) and the copy itself */
static void colours_in(int palette, uint32_t from)
{
  static const uint32_t to[3] = { 0x10D00, 0x10B00, HM_CGRAM_SHADOW };
  for (int k = 0; k < 3; k++)
    for (int i = 0; i < 32; i++)
      g_ram[to[k] + 256 + palette * 32 + i] = hm_bus_rd8(g_ram, from + (uint32_t)i);
  /* and the chip's own: the game sends its copy only when it changes it */
  if (g_ppu)
    for (int i = 0; i < 16; i++)
      g_ppu->cgram[128 + palette * 16 + i] = (uint16_t)(g_ram[HM_CGRAM_SHADOW + 256 + palette * 32 + 2 * i] |
                                                       g_ram[HM_CGRAM_SHADOW + 256 + palette * 32 + 2 * i + 1] << 8);
}
static void her_colours(void) { colours_in(HER_PALETTE, GODDESS_PALETTE); }

/* the places she floats between (pixels in the map): around the barn's
 * door (tile 20, 21); at midday over the field, south of the fence; at
 * home the bed, above the table, the TV */
typedef struct { int x, y; } Spot;
static const Spot kFarmSpots[] = { { 20 * 16 + 8, 23 * 16 }, { 23 * 16, 25 * 16 }, { 18 * 16, 24 * 16 }, { 21 * 16, 26 * 16 } };
static const Spot kFieldSpots[] = { { 20 * 16, 34 * 16 }, { 24 * 16, 35 * 16 }, { 17 * 16, 36 * 16 } };
static const Spot kHouseSpots[] = { { 0x58, 0x60 }, { 0x88, 0x78 }, { 0xBC, 0x60 }, { 0x70, 0x90 } };

/* where she is now: 1 the farm, 2 the house, 0 neither (as the day goes) */
static int her_place(int map, int hour)
{
  bool house = map >= 0x15 && map <= 0x17, farm = map <= 3;
  bool at_home = home_day() || hour >= AWAY_FROM || hour < 6;
  if (farm && !at_home) return 1;
  if (house && at_home) return 2;
  return 0;
}

/* her lines: up to 3 pages of up to 3 lines, as they show in the box */
typedef const char *Page[TEXTBOX_LINES];
typedef struct { int pages; Page page[3]; } Line;
static const Line kFarmLines[] = {
  { 1, { { "The animals in the barn", "are doing well today." } } },
  { 2, { { "I like it out here,", "close to the ground." }, { "The seeds can hear me", "if I sing to them." } } },
  { 1, { { "Don't mind the sparkles.", "They follow me around." } } },
  { 1, { { "Go on with your work,", "dear. I'm happy just", "watching you." } } },
  { 2, { { "The spring water was", "sweet this morning." }, { "It's a good sign for", "the farm, you know." } } },
};
static const Line kHomeLines[] = {
  { 2, { { "Welcome home, dear!" }, { "I'm not used to walls...", "but I like these ones." } } },
  { 1, { { "It's warm in here.", "I like it." } } },
  { 1, { { "Tell me about your day,", "won't you?" } } },
  { 2, { { "Rest well tonight." }, { "The spring keeps watch", "over the farm while", "you sleep." } } },
};
/* with a child on the way, and once she is a mother */
static const Line kExpectingLines[] = {
  { 2, { { "I can feel the little one", "moving..." }, { "Even a goddess gets", "butterflies, dear." } } },
  { 1, { { "I'm fine. Really!", "Don't worry so much." } } },
  { 2, { { "The spring sings to the", "baby every night." }, { "I think it listens." } } },
};
static const Line kMotherLines[] = {
  { 1, { { "The little one smiled at", "the sparkles today!" } } },
  { 2, { { "Our child has your eyes." }, { "And my laugh, I think." } } },
  { 1, { { "Who knew a goddess could", "be this happy?" } } },
};
#define COUNT(a) (int)(sizeof (a) / sizeof (a)[0])

static struct {
  int place;                       /* where she was put (her_place), 0 none */
  bool hobby;                      /* blessing the field (midday, her farm days) */
  int x, y;                        /* where she floats, in 1/16 pixels */
  int spot, rest;                  /* the place she floats to; frames still resting */
  int day, talks;                  /* talks today, for the next line */
  bool want_dialog;
  TextboxTalk talk;
  unsigned frame;
} G;

/* the player's buttons, before the game reads them: A in front of her
 * opens her talk (and is kept from the game until let go) */
uint16_t goddess_pad(uint16_t pad)
{
  static uint16_t prev;
  static bool hold;
  bool pressed = (pad & B_A) && !(prev & B_A);
  prev = pad;
  if (hold) { if (pad & B_A) return (uint16_t)(pad & ~B_A); hold = false; }
  if (!pressed || !g_cfg.goddess || !(g_hm_goddess_flags & GODDESS_MARRIED) || !slot_runs_idle(HER_SLOT)) return pad;
  if (!player_free() || (hm_rd16(g_ram, HM_FLAGS_5A) & 0x4000)) return pad;   /* a box is open */
  static const int ahead[4][2] = { { 0, 16 }, { 0, -16 }, { 16, 0 }, { -16, 0 } };
  int f = hm_rd16(g_ram, 0xDA) & 3;                  /* the way the player faces */
  int fx = hm_rd16(g_ram, HM_PLAYER_X) + ahead[f][0], fy = hm_rd16(g_ram, HM_PLAYER_Y) + ahead[f][1];
  int dx = slot16(HER_SLOT, SLOT_X) - fx, dy = slot16(HER_SLOT, SLOT_Y) - fy;
  if (dx < -12 || dx > 12 || dy < -12 || dy > 12) return pad;   /* not in front of her */
  G.want_dialog = true;
  hold = true;
  return (uint16_t)(pad & ~B_A);
}

bool goddess_dialog_wanted(void)
{
  if (s_shop == SHOP_ASKED) return true;
  if (s_scene == SCENE_STARTED) return false;       /* she is still coming out of the water */
  if (s_scene == SCENE_TALKING) s_scene = NO_SCENE;
  bool b = G.want_dialog || s_said_pages;
  G.want_dialog = false;
  return b;
}
void goddess_dialog_open(void)
{
  if (s_shop == SHOP_ASKED) { shop_open(); return; }
  if (s_said_pages) {                                /* what she just said (an offering, the feather...) */
    memset(&G.talk, 0, sizeof G.talk);
    G.talk.pages = s_said_pages;
    for (int p = 0; p < s_said_pages; p++)
      for (int i = 0; i < TEXTBOX_LINES; i++) G.talk.lines[p][i] = s_said[p][i][0] ? s_said[p][i] : NULL;
    textbox_talk_open(&G.talk);
    return;
  }
  if (G.day != today()) { G.day = today(); G.talks = 0; }
  bool farm = g_ram[HM_MAP_ID] <= 3;
  const Line *lines = farm ? kFarmLines : kHomeLines;
  int n = farm ? COUNT(kFarmLines) : COUNT(kHomeLines);
  if (expecting(g_ram)) { lines = kExpectingLines; n = COUNT(kExpectingLines); }
  else if (a_mother(g_ram) && G.talks % 2 == 0) { lines = kMotherLines; n = COUNT(kMotherLines); }
  unsigned first = ((unsigned)today() * 2654435761u >> 16) % (unsigned)n;
  const Line *l = &lines[(first + (unsigned)G.talks) % (unsigned)n];
  G.talks++;
  memset(&G.talk, 0, sizeof G.talk);
  G.talk.pages = l->pages;
  for (int p = 0; p < l->pages; p++)
    for (int i = 0; i < TEXTBOX_LINES; i++) G.talk.lines[p][i] = l->page[p][i];
  textbox_talk_open(&G.talk);
}
bool goddess_dialog_update(const UiInput *in)
{
  if (s_shop == SHOP_OPEN) {
    if (!shop_update(in)) return false;
    if (!s_said_pages) return true;                  /* the Blue Feather, or no */
    textbox_talk_open(&G.talk);                      /* what the golden sale says, next */
    G.talk.pages = s_said_pages;
    for (int p = 0; p < s_said_pages; p++)
      for (int i = 0; i < TEXTBOX_LINES; i++) G.talk.lines[p][i] = s_said[p][i][0] ? s_said[p][i] : NULL;
    return false;
  }
  bool over = textbox_talk_update(&G.talk, in);
  if (over) { s_said_pages = 0; memset(s_said, 0, sizeof s_said); }   /* heard */
  return over;
}
void goddess_dialog_draw(uint32_t *dst, int w) { if (s_shop == SHOP_OPEN) shop_draw(dst, w); else textbox_talk_draw(&G.talk, dst, w); }

/* a frame of floating: towards the spot, then resting there a while;
 * never while the player is close */
static void glide(const Spot *spots, int n)
{
  int px = hm_rd16(g_ram, HM_PLAYER_X) * GLIDE_SUBPIXELS, py = hm_rd16(g_ram, HM_PLAYER_Y) * GLIDE_SUBPIXELS;
  int near = NEAR_PLAYER * GLIDE_SUBPIXELS;
  if (abs(px - G.x) < near && abs(py - G.y) < near) return;
  if (G.rest > 0) { G.rest--; return; }
  int tx = spots[G.spot].x * GLIDE_SUBPIXELS, ty = spots[G.spot].y * GLIDE_SUBPIXELS;
  int dx = tx - G.x, dy = ty - G.y;
  if (abs(dx) <= GLIDE_SPEED && abs(dy) <= GLIDE_SPEED) {   /* there: rest, then any other one */
    G.x = tx; G.y = ty;
    G.rest = REST_FRAMES + (int)(G.frame % 120);
    G.spot = (G.spot + 1 + (int)(G.frame % (unsigned)(n - 1))) % n;
    return;
  }
  /* a straight line, the longer way at full speed */
  if (abs(dx) >= abs(dy)) { G.x += dx > 0 ? GLIDE_SPEED : -GLIDE_SPEED; G.y += dy * GLIDE_SPEED / abs(dx); }
  else { G.y += dy > 0 ? GLIDE_SPEED : -GLIDE_SPEED; G.x += dx * GLIDE_SPEED / abs(dy); }
}
static void move_slot(uint32_t at, int x, int y)
{
  g_ram[at + SLOT_X] = (uint8_t)x; g_ram[at + SLOT_X + 1] = (uint8_t)(x >> 8);
  g_ram[at + SLOT_Y] = (uint8_t)y; g_ram[at + SLOT_Y + 1] = (uint8_t)(y >> 8);
}

/* ── Her family: the Water Imp ────────────────────────────────────────
 * The spring's spirit (sprite $308: the game puts him in
 * the spring when a fish is given back, event group $10) is her family.
 * Where a girl's family comes, he does: among the guests at the
 * wedding (standing still, between the elves on the right), by the bed at the child's birth, and in the ending waiting in
 * front of the family's house in the town, as Maria's family does (in the
 * game the family does not walk with the couple: it waits there). He
 * stands in event slot $17 as a still picture, in the mountain's colours:
 * OBJ palette 1 (the birth's and the ending's figures leave it free; 0-3
 * are the ones the town's colour effects leave alone), or palette 0 with
 * hers at the wedding (wedding_colours). */
#define IMP_SLOT 0x0BB46u              /* event slot $17 */
#define IMP_COLOURS 0xA9DE00           /* file 0x14DE00: the mountain's figures (his, by the spring) */
enum {
  IMP_SPRITE = 0x308,
  WEDDING_GUEST_X = 424, WEDDING_GUEST_Y = 224,   /* the wedding: on the right, between the elves */
  FAMILY_HOUSE_X = 206, FAMILY_HOUSE_Y = 182,   /* the ending: in front of the house, where her family waits */
  BEDSIDE_X = 336, BEDSIDE_Y = 120,    /* where a girl's family stands at the birth */
  TOWN_SQUARE = 0x05,
  NO_PALETTE = -1, IMP_PALETTE = 1,
};
static int s_imp_palette = NO_PALETTE;

static bool imp_place(uint8_t *ram, int *x, int *y)
{
  if (s_wedding == WEDDING_ON) {                     /* among the guests, as the family stands */
    *x = WEDDING_GUEST_X; *y = WEDDING_GUEST_Y;
    return true;
  }
  int map = ram[HM_MAP_ID];
  if (in_childbed(ram) && map >= 0x15 && map <= 0x17) { *x = BEDSIDE_X; *y = BEDSIDE_Y; return true; }
  if (s_lent_for_ending && map == TOWN_SQUARE) {     /* waiting at the family's house */
    *x = FAMILY_HOUSE_X; *y = FAMILY_HOUSE_Y;
    return true;
  }
  return false;
}
static void imp_tick(uint8_t *ram)
{
  int x, y;
  if (!hm_diary_loaded(ram) || !imp_place(ram, &x, &y)) {
    take_away(IMP_SLOT);
    s_imp_palette = NO_PALETTE;
    return;
  }
  s_imp_palette = s_wedding == WEDDING_ON ? 0 : IMP_PALETTE;
  if (s_wedding != WEDDING_ON) colours_in(s_imp_palette, IMP_COLOURS);
  if (!slot_runs_idle(IMP_SLOT)) {
    if (ram[IMP_SLOT]) return;                       /* the slot is the game's now */
    put(IMP_SLOT, 0x17, IMP_SPRITE, s_imp_palette, x, y);
  }
  if ((ram[IMP_SLOT + SLOT_ANIMATION] >> 1 & 7) != s_imp_palette) {
    ram[IMP_SLOT + SLOT_ANIMATION] = (uint8_t)((ram[IMP_SLOT + SLOT_ANIMATION] & ~0x0E) | s_imp_palette << 1);
    ram[IMP_SLOT + SLOT_FLAGS] |= 0x04;
    ram[IMP_SLOT + SLOT_LAST_SPRITE] ^= 1;           /* so the game redraws it */
  }
  move_slot(IMP_SLOT, x, y);
}

void goddess_married_tick(uint8_t *ram)
{
  if (!g_cfg.goddess) return;
  bool wed = wedding(ram);
  imp_tick(ram);
  if (wed) return;
  int place = (g_hm_goddess_flags & GODDESS_MARRIED) && hm_diary_loaded(ram) ? her_place(ram[HM_MAP_ID], ram[HM_HOUR]) : 0;
  if (s_lent_for_ending) {                            /* the game's wife till the game ends */
    childbed_colours();
    take_away(HER_SLOT); take_away(SPARKLE_SLOT); G.place = 0;
    return;
  }
  if (in_childbed(ram)) {                             /* in bed, the game's wife: not floating about */
    if (ram[HM_MAP_ID] >= 0x15 && ram[HM_MAP_ID] <= 0x17) childbed_colours();
    place = 0;
  }
  if (!place) { take_away(HER_SLOT); take_away(SPARKLE_SLOT); G.place = 0; return; }

  bool hobby = place == 1 && ram[HM_HOUR] >= HOBBY_FROM && ram[HM_HOUR] < HOBBY_TO;
  const Spot *spots = hobby ? kFieldSpots : place == 1 ? kFarmSpots : kHouseSpots;
  int n = hobby ? COUNT(kFieldSpots) : place == 1 ? COUNT(kFarmSpots) : COUNT(kHouseSpots);
  if (hobby != G.hobby) { G.hobby = hobby; G.spot = 0; G.rest = 0; }   /* off to the field, or back */
  if (G.place != place || !slot_runs_idle(HER_SLOT)) {    /* she arrives: at her first place */
    if (!slot_runs_idle(HER_SLOT) && ram[HER_SLOT]) return;   /* the slot is the game's now */
    G.place = place;
    G.x = spots[0].x * GLIDE_SUBPIXELS; G.y = spots[0].y * GLIDE_SUBPIXELS;
    G.spot = 1; G.rest = REST_FRAMES;
    if (!slot_runs_idle(HER_SLOT)) put(HER_SLOT, 0x13, GODDESS_SPRITE, HER_PALETTE ^ 0, spots[0].x, spots[0].y);
  }
  if (!hobby) take_away(SPARKLE_SLOT);              /* the sparkles: only at her blessing */
  else if (!slot_runs_idle(SPARKLE_SLOT) && !ram[SPARKLE_SLOT])
    put(SPARKLE_SLOT, 0x14, SPARKLE_SPRITE, 0, G.x / GLIDE_SUBPIXELS, G.y / GLIDE_SUBPIXELS - 4);
  her_colours();

  G.frame++;
  glide(spots, n);
  /* floating: a slow wave of a pixel or two */
  static const int8_t wave[16] = { 0, 0, 1, 1, 2, 2, 2, 1, 1, 0, 0, -1, -1, -1, -1, 0 };
  int bob = wave[(G.frame * 16 / BOB_PERIOD) % 16];
  int x = G.x / GLIDE_SUBPIXELS, y = G.y / GLIDE_SUBPIXELS - 2 - bob;   /* a little off the ground */
  move_slot(HER_SLOT, x, y);
  if (hobby && slot_runs_idle(SPARKLE_SLOT)) move_slot(SPARKLE_SLOT, x, y - 4);

  /* now and then her other picture, and the sparkles turning */
  int sprite = (G.frame % 150) < 12 ? GODDESS_SPRITE + 1 : GODDESS_SPRITE;
  if (slot16(HER_SLOT, SLOT_SPRITE) != sprite) {
    ram[HER_SLOT + SLOT_SPRITE] = (uint8_t)sprite; ram[HER_SLOT + SLOT_SPRITE + 1] = (uint8_t)(sprite >> 8);
    ram[HER_SLOT + SLOT_FLAGS] |= 0x04;
  }
  if (hobby && slot_runs_idle(SPARKLE_SLOT) && G.frame % 24 == 0) {
    ram[SPARKLE_SLOT + SLOT_ANIMATION] ^= 0x40;      /* mirrored: the sparkles move */
    ram[SPARKLE_SLOT + SLOT_FLAGS] |= 0x04;
    ram[SPARKLE_SLOT + SLOT_LAST_SPRITE] ^= 1;       /* so the game redraws it */
  }
}

void goddess_reset(void)
{
  /* the diary loaded next brings her hearts back (src/save.c); a wedding
   * cut short by a reset starts again the next time (its bit is not kept) */
  s_wedding = NO_WEDDING;
  g_hm_slot_picture = NULL;
  memset(&G, 0, sizeof G);
  s_said_pages = 0;
  s_scene = NO_SCENE;
  s_lent_for_ending = false;                         /* the game starts over: its RAM too */
  s_imp_palette = NO_PALETTE;
  s_scene_frames = 0;
  s_shop = NO_SHOP;
  s_keep_no_text = false;
}
