/*
 * suitors.c — the men of the town who can be courted (an improvement, off
 * by default), as the girls are: hearts, gifts and their lines. The plan,
 * and what was found about them: docs/suitors/PLAN.md.
 *
 * This is the first step: hearts and gifts, for the first suitor, Eve's
 * fan (the young man with short hair in the bar, map $1E, sprites
 * $269-$274; text $120: "Good evening. I'm a fan of Eve's, you know.").
 * The proposal, the wedding and married life come later, with the same
 * pieces as the Goddess's (goddess.c).
 *
 * Who he is. Not by the slot (the town's scripts may put him in another
 * one): any event slot in use whose picture is one of his sprites, on his
 * map. The table kSuitors below says who each suitor is; adding the next
 * one (a carpenter, $25A-$264) is a line there and his lines.
 *
 * Talking. A in front of him, hands free: his line, in the game's own
 * text box (textbox.c), by how much he likes the player now; the first talk
 * of the day is worth a little love (TALK_LOVE). His own words in the game
 * (text $120) are not shown: his lines take their place.
 *
 * Gifts. A in front of him, holding something the bag could take (a crop,
 * a product, a forage; native/mods.c, bag_item): he takes it. The first
 * gift of the day counts: his favourites much more, the things he dislikes
 * take love away, anything else what it sells for (the game's own sale
 * table, $81:A094). A second gift that day is not taken (it stays in the
 * hands), and he says once a day is plenty. The thing goes
 * the game's own way, as the bag takes it: its map object to state 8 (the
 * step after shipping), which takes it away and frees the hands.
 *
 * Love goes from 0 to 1000 (ten hearts, 100 each, as the Goddess's) and is
 * kept in the diary (src/save.c), with the bits that say he was talked to
 * or given something today.
 *
 * His words are in English, as the game's.
 */
#include <string.h>

#include "hm_decomp.h"
#include "hm_ram.h"
#include "hm_vars.h"
#include "native.h"
#include "config.h"
#include "host.h"
#include "textbox.h"
#include "suitors.h"

#define SALE_TABLE 0x81A094            /* per item 3 bytes: its text, then the money */
#define SLOTS      0x0B586u            /* the event slots: $7E:B586 + $40 x n (fields: SLOT_*) */
enum {
  SLOT_COUNT = 0x20,
  LOVE = 0x03FF, TALKED = 0x0400, GIFTED = 0x0800,   /* a suitor's word (g_hm_suitor) */
  MOST_LOVE = 1000,
  TALK_LOVE = 3,                       /* the first talk of the day */
  FAVOURITE_LOVE = 120,                /* a favourite, given */
  DISLIKED_LOVE = -50,
  PLAIN_LOVE = 5,                      /* a thing that sells for nothing */
  CARRIED = 2, SHIPPED = 8,            /* a map object's state: in the hands; taken away */
};
enum { DISLIKED, PLAIN, FINE, FAVOURITE };

/* ── Who they are ───────────────────────────────────────────────────── */
typedef struct {
  int sprite_from, sprite_to;          /* his pictures, all of them (front, back, side, steps) */
  int map;                             /* where he is courted */
  uint8_t favourites[4], disliked[6];  /* item numbers, 0 ends the list */
  const char *talk[4][2];              /* by how much he likes the player: two lines each */
  const char *gift[4][4];              /* by how much he likes the player, and the thing */
} Suitor;

static const Suitor kSuitors[] = {
  { /* Eve's fan, in the bar */
    0x269, 0x274, 0x1E,
    { 0x03, 0x07, 0x06 },              /* Wild Grapes, Fish, Cake */
    { 0x02, 0x09, 0x0A, 0x0B, 0x0C },  /* Poison Mushroom, the weeds */
    { { "Good evening. I come here every night to hear Eve sing.",
        "Oh, it's you, the farmer. Busy day?" },
      { "Back again? Sit down, the night is young.",
        "You smell like fresh hay. I don't mind it." },
      { "I was hoping you'd come in tonight.",
        "Funny... I've stopped watching the stage since you started coming." },
      { "Eve sings, but I only hear you talking.",
        "Every night I wait for that door to open, and for it to be you." } },
    { { "Weeds? Is that a joke?",
        "For me? Uh, thanks.",
        "Hey, that's nice of you.",
        "No way! This is my favourite!" },
      { "...You know I don't like that.",
        "Thanks. I'll take it home.",
        "You always bring something good.",
        "My favourite! You remembered!" },
      { "That hurts, coming from you.",
        "Thanks. It's the thought that counts.",
        "I'll keep it. Not because of what it is, because of who.",
        "You know me better than anyone in this town." },
      { "I'll pretend you didn't give me that.",
        "Anything from you makes my night.",
        "You spoil me, you know that?",
        "This... I think I love you." } } },
};
#define COUNT(a) (int)(sizeof (a) / sizeof (a)[0])
_Static_assert(COUNT(kSuitors) <= HM_SUITORS, "the diary keeps three suitors");

static int today(void) { return g_ram[HM_YEAR] * 120 + (g_ram[HM_SEASON] & 3) * 30 + g_ram[HM_DAY]; }
static int love(int who) { return g_hm_suitor[who] & LOVE; }
static void add_love(int who, int d)
{
  int v = love(who) + d;
  if (v < 0) v = 0;
  if (v > MOST_LOVE) v = MOST_LOVE;
  g_hm_suitor[who] = (uint16_t)((g_hm_suitor[who] & ~LOVE) | v);
}
static int tier(int who)               /* 0-3: up to 299, 599, 899, more */
{
  int l = love(who);
  return l < 300 ? 0 : l < 600 ? 1 : l < 900 ? 2 : 3;
}

/* ── What he says ───────────────────────────────────────────────────────
 * Wrapped to the box's width, a page each say(), as the Goddess's
 * (goddess.c); the box opens at the end of the frame. */
static char s_said[6][TEXTBOX_LINES][TEXTBOX_COLUMNS + 1];
static int s_said_pages;
static bool s_want_dialog;
static TextboxTalk s_talk;

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
  s_want_dialog = true;
}

/* ── Talking and gifts ──────────────────────────────────────────────── */
static bool listed(const uint8_t *list, int n, uint8_t item)
{
  for (int i = 0; i < n && list[i]; i++) if (list[i] == item) return true;
  return false;
}
static unsigned sale_money(uint8_t item)
{
  uint32_t a = SALE_TABLE + 3u * item + 2u;
  return g_rom[(a >> 16 & 0x3F) << 15 | (a & 0x7FFF)];
}
/* what the bag takes (native/mods.c, bag_item): the things one would give */
static bool giftable(uint8_t it)
{
  return (it >= 0x01 && it <= 0x0C) || (it >= 0x10 && it <= 0x19) || it == 0x24 || (it >= 0x58 && it <= 0x5A);
}
/* the thing in the hands goes, the game's own way (as the bag) */
static bool take_from_hands(uint8_t it)
{
  for (int i = 0; i < 10; i++) {
    uint8_t *o = &g_ram[HM_MAP_OBJECTS + 16 * i];
    if ((o[0] & 1) && o[3] == it && o[2] == CARRIED) { o[2] = SHIPPED; return true; }
  }
  return false;
}

static void talk(int who)
{
  const Suitor *s = &kSuitors[who];
  int t = tier(who);
  say(s->talk[t][(g_hm_suitor[who] & TALKED) ? 1 : 0]);   /* the second line once talked to today */
  if (!(g_hm_suitor[who] & TALKED)) { g_hm_suitor[who] |= TALKED; add_love(who, TALK_LOVE); }
}

static void give(int who, uint8_t it)
{
  const Suitor *s = &kSuitors[who];
  if (!giftable(it)) { say("I can't take that, sorry."); return; }
  if (g_hm_suitor[who] & GIFTED) { say("Another one? Thanks, but once a day is plenty."); return; }   /* it stays in the hands */
  if (!take_from_hands(it)) return;                  /* not found in the hands: leave it */
  int t = tier(who), like, d;
  if (listed(s->favourites, COUNT(s->favourites), it)) { like = FAVOURITE; d = FAVOURITE_LOVE; }
  else if (listed(s->disliked, COUNT(s->disliked), it)) { like = DISLIKED; d = DISLIKED_LOVE; }
  else {
    unsigned m = sale_money(it);
    like = m >= 30 ? FINE : PLAIN;
    d = m ? (int)m : PLAIN_LOVE;
  }
  g_hm_suitor[who] |= GIFTED;
  add_love(who, d);
  say(s->gift[t][like]);
}

/* ── Finding him in front of the player ─────────────────────────────── */
static int slot16(uint32_t at, int field) { return g_ram[at + field] | g_ram[at + field + 1] << 8; }
static int suitor_in_front(void)
{
  static const int ahead[4][2] = { { 0, 16 }, { 0, -16 }, { 16, 0 }, { -16, 0 } };
  int f = hm_rd16(g_ram, HM_PLAYER_DIRECTION) & 3;
  int fx = hm_rd16(g_ram, HM_PLAYER_X) + ahead[f][0], fy = hm_rd16(g_ram, HM_PLAYER_Y) + ahead[f][1];
  for (int n = 0; n < SLOT_COUNT; n++) {
    uint32_t at = SLOTS + 0x40u * (uint32_t)n;
    if (!g_ram[at + SLOT_IN_USE]) continue;
    int sprite = slot16(at, SLOT_SPRITE);
    for (int who = 0; who < COUNT(kSuitors); who++) {
      const Suitor *s = &kSuitors[who];
      if (g_ram[HM_MAP_ID] != s->map || sprite < s->sprite_from || sprite > s->sprite_to) continue;
      int dx = slot16(at, SLOT_X) - fx, dy = slot16(at, SLOT_Y) - fy;
      if (dx >= -12 && dx <= 12 && dy >= -12 && dy <= 12) return who;
    }
  }
  return -1;
}

/* the player can talk now: in control, no box open, not busy (as the
 * Goddess's player_free, but holding something is fine) */
static bool can_talk(void)
{
  uint16_t act = hm_rd16(g_ram, HM_PLAYER_ACTION), state = hm_rd16(g_ram, HM_GAME_STATE);
  return hm_diary_loaded(g_ram) && g_ram[HM_INPUT_STATE] == 1 &&
         (act == 0 || act == 1 || act == 2 || act == 9) &&
         !(state & (0x0010 | 0x0800)) &&                              /* the dog, carried */
         !(hm_rd16(g_ram, HM_FLAGS_60) & 0x0006) &&                   /* the hot spring */
         !(hm_rd16(g_ram, HM_FLAGS_5A) & 0x4000);                     /* a box is open */
}

/* the player's buttons, before the game reads them: A in front of him talks
 * (or gives what is held), and is kept from the game until let go */
uint16_t suitors_pad(uint16_t pad)
{
  static uint16_t prev;
  static bool hold;
  bool pressed = (pad & B_A) && !(prev & B_A);
  prev = pad;
  if (hold) { if (pad & B_A) return (uint16_t)(pad & ~B_A); hold = false; }
  if (!pressed || !g_cfg.male_suitors || !can_talk()) return pad;
  int who = suitor_in_front();
  if (who < 0) return pad;
  bool holding = hm_rd16(g_ram, HM_GAME_STATE) & 0x0002;
  uint8_t it = g_ram[HM_ITEM_ON_HAND];
  if (holding && it) give(who, it);
  else if (!holding) talk(who);
  else return pad;
  hold = true;
  return (uint16_t)(pad & ~B_A);
}

/* ── The box ────────────────────────────────────────────────────────── */
bool suitors_dialog_wanted(void)
{
  bool b = s_want_dialog && s_said_pages;
  s_want_dialog = false;
  return b;
}
void suitors_dialog_open(void)
{
  memset(&s_talk, 0, sizeof s_talk);
  s_talk.pages = s_said_pages;
  for (int p = 0; p < s_said_pages; p++)
    for (int i = 0; i < TEXTBOX_LINES; i++) s_talk.lines[p][i] = s_said[p][i][0] ? s_said[p][i] : NULL;
  textbox_talk_open(&s_talk);
}
bool suitors_dialog_update(const UiInput *in)
{
  bool over = textbox_talk_update(&s_talk, in);
  if (over) { s_said_pages = 0; memset(s_said, 0, sizeof s_said); }   /* heard */
  return over;
}
void suitors_dialog_draw(uint32_t *dst, int w) { textbox_talk_draw(&s_talk, dst, w); }

/* ── Every frame ────────────────────────────────────────────────────── */
void suitors_frame(void)
{
  g_hm_suitors_on = g_cfg.male_suitors;              /* the diary keeps them (src/save.c) */
  if (!g_cfg.male_suitors || !hm_diary_loaded(g_ram)) return;
  uint8_t day = (uint8_t)today();
  if (g_hm_suitor_day != day) {                      /* a new day: nobody talked to or given anything yet */
    g_hm_suitor_day = day;
    for (int i = 0; i < HM_SUITORS; i++) g_hm_suitor[i] &= (uint16_t)~(TALKED | GIFTED);
  }
}

void suitors_reset(void)
{
  /* the diary loaded next brings their love back (src/save.c) */
  s_said_pages = 0;
  memset(s_said, 0, sizeof s_said);
  s_want_dialog = false;
}
