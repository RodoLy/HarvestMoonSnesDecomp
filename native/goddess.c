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
 * At ten hearts, the Blue Feather proposes — the same item the game's
 * own girls are asked for — rather than a favourite offering.
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
 * Married life. From then on, which day it is decides where she is: about one day in four,
 * she is at the house; the rest, she is by the farm's barn. Neither is
 * drawn as her — the house day is a line when the player is home, and the
 * farm day is a scatter of sparkles (native/main.c's overlay) where she'd
 * be standing, nothing more. Talking to her is not possible today: there
 * is no identified sprite of hers to place and animate as the wife's is
 * (native/wife.c), so this stays a line and a light, not a figure.
 */
#include <stdio.h>
#include <string.h>

#include "hm_decomp.h"
#include "hm_ram.h"
#include "hm_vars.h"
#include "native.h"
#include "config.h"
#include "i18n.h"
#include "host.h"
#include "goddess.h"

#define SALE_TABLE 0x81A094            /* per item 3 bytes: its text, then the money */
/* GODDESS_HINTED/ENGAGED/MARRIED: goddess.h. Engaged: the wedding is a week
 * after the proposal (the day of the last offering). */
enum {
  IT_POISON_MUSHROOM = 0x02, IT_FULLMOON_BERRY = 0x05, IT_CAKE = 0x06,
  IT_LARGE_MILK = 0x17, IT_BLUE_FEATHER = 0x41,
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
  { "The Goddess of Spring: \"...Poison? Please don't.\"",
    "The Goddess of Spring: \"Oh, an offering. Thank you, traveler.\"",
    "The Goddess of Spring: \"That's a fine thing to give. Thank you.\"",
    "The Goddess of Spring: \"Oh! This is one of my favourites. How did you know?\"" },
  { "The Goddess of Spring: \"That's not something I can accept.\"",
    "The Goddess of Spring: \"You come here often. The pond is glad.\"",
    "The Goddess of Spring: \"You always bring such nice things.\"",
    "The Goddess of Spring: \"My favourite! You remembered!\"" },
  { "The Goddess of Spring: \"...I thought you liked me.\"",
    "The Goddess of Spring: \"I was hoping you'd come today.\"",
    "The Goddess of Spring: \"Thank you. I think about your farm a lot.\"",
    "The Goddess of Spring: \"You spoil me... I'll treasure it.\"" },
  { "The Goddess of Spring: \"Why would you bring me this?\"",
    "The Goddess of Spring: \"Seeing you is the best part of my day.\"",
    "The Goddess of Spring: \"Every time you come, the water shines brighter.\"",
    "The Goddess of Spring: \"I love it... and I think I love you, too.\"" },
};

static void offer(uint8_t *ram, uint8_t item)
{
  (void)ram;
  if (!g_cfg.goddess || item == 0) return;
  if (g_hm_goddess_flags & GODDESS_ENGAGED) {
    host_toast(T("The Goddess of Spring: \"I'm getting ready for our wedding. See you soon!\""));
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
    host_toast(T("The Goddess of Spring: \"You already brought me something today. Come back tomorrow!\""));
    return;
  }
  /* the proposal: her ten hearts, the house built up once, and the Blue Feather */
  if (g_hm_goddess_hearts >= 1000 && item == IT_BLUE_FEATHER && (hm_rd16(g_ram, HM_FLAGS_64) & 0x0040) &&
      !(hm_rd16(g_ram, HM_FLAGS_66) & 0x001F)) {     /* not married to one of the girls */
    g_hm_goddess_day = (uint16_t)today();
    g_hm_goddess_flags |= GODDESS_ENGAGED;
    host_toast(T("The Goddess of Spring: \"A Blue Feather... for me? Yes. Yes, I'll marry you! In a week, in front of the church.\""));
    return;
  }
  int tier = g_hm_goddess_hearts < 300 ? 0 : g_hm_goddess_hearts < 600 ? 1 : g_hm_goddess_hearts < 900 ? 2 : 3;
  g_hm_goddess_day = (uint16_t)today();
  add_hearts(d);
  host_toast(T(kLines[tier][like]));
  if (g_hm_goddess_hearts >= 1000 && !(g_hm_goddess_flags & GODDESS_HINTED)) {
    g_hm_goddess_flags |= GODDESS_HINTED;
    host_toast(T("The Goddess of Spring: \"...If you ever brought me a Blue Feather, I might say something important.\""));
  }
}

void goddess_frame(void)
{
  g_hm_goddess_on = g_cfg.goddess;
  g_hm_spring_offer = g_cfg.goddess ? offer : NULL;
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
    host_toast(T("The Goddess of Spring: \"I'm yours now, for as long as the spring returns.\""));
  }
  return true;
}

/* ── Married life ─────────────────────────────────────────────────────── */
enum { BARN_TX = 20, BARN_TY = 22 };               /* in front of the barn's door (native/wife.c) */
static const char *const kHomeLines[4] = {
  "The Goddess of Spring: \"I wanted to see how the house looks today.\"",
  "The Goddess of Spring: \"It's warm in here. I like it.\"",
  "The Goddess of Spring: \"I'm not used to walls... but I don't mind these.\"",
  "The Goddess of Spring: \"Tell me about your day, won't you?\"",
};
static const char *const kFarmLines[4] = {
  "The Goddess of Spring: \"The barn's animals are doing well.\"",
  "The Goddess of Spring: \"I like it out here, close to the ground.\"",
  "The Goddess of Spring: \"Don't mind the sparkles. They follow me around.\"",
  "The Goddess of Spring: \"Go on with your work. I'm happy just watching.\"",
};

static bool home_day(void)
{
  unsigned d = ((unsigned)today() + 7919u) * 2654435761u;
  return ((d >> 16) & 3) == 0;                      /* about one day in four */
}

void goddess_married_tick(uint8_t *ram)
{
  g_hm_goddess_at_home = false;
  g_hm_goddess_sparkle = false;
  if (!g_cfg.goddess) return;

  if (wedding(ram)) return;
  if (!(g_hm_goddess_flags & GODDESS_MARRIED)) return;

  static int last_toast_day = -1;
  bool home = home_day();
  int map = ram[HM_MAP_ID];
  bool house = map >= 0x15 && map <= 0x17, farm = map <= 3;

  if (home && house) {
    g_hm_goddess_at_home = true;
    if (last_toast_day != today()) {
      last_toast_day = today();
      unsigned pick = ((unsigned)today() * 2654435761u >> 16) & 3;
      host_toast(T(kHomeLines[pick]));
    }
  } else if (!home && farm) {
    g_hm_goddess_sparkle = true;
    int px = hm_rd16(ram, HM_PLAYER_X), py = hm_rd16(ram, HM_PLAYER_Y);
    int dx = px - BARN_TX * 16, dy = py - (BARN_TY + 1) * 16;
    if (last_toast_day != today() && dx * dx + dy * dy < 48 * 48) {
      last_toast_day = today();
      unsigned pick = ((unsigned)today() * 2654435761u >> 16) & 3;
      host_toast(T(kFarmLines[pick]));
    }
  }
}

void goddess_reset(void)
{
  /* the diary loaded next brings her hearts back (src/save.c); a wedding
   * cut short by a reset starts again the next time (its bit is not kept) */
  s_wedding = NO_WEDDING;
  g_hm_slot_picture = NULL;
}
