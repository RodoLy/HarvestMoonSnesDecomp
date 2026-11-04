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
 * Married life. A week after the proposal, GODDESS_MARRIED is set. From
 * then on, which day it is decides where she is: about one day in four,
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
  if (g_hm_goddess_hearts >= 1000 && item == IT_BLUE_FEATHER && (hm_rd16(g_ram, HM_FLAGS_64) & 0x0040)) {
    g_hm_goddess_day = (uint16_t)today();
    g_hm_goddess_flags |= GODDESS_ENGAGED;
    host_toast(T("The Goddess of Spring: \"A Blue Feather... for me? Yes. Yes, I'll marry you! In a week, here at my pond.\""));
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

  /* the wedding, a week after the proposal */
  if ((g_hm_goddess_flags & GODDESS_ENGAGED) && !(g_hm_goddess_flags & GODDESS_MARRIED)
      && today() >= g_hm_goddess_day + 7) {
    g_hm_goddess_flags = (uint8_t)((g_hm_goddess_flags & ~GODDESS_ENGAGED) | GODDESS_MARRIED);
    host_toast(T("The Goddess of Spring: \"I'm yours now, for as long as the spring returns.\""));
  }
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
  /* the diary loaded next brings her hearts back (src/save.c) */
}
