/*
 * suitors.c — the men of the town who can be courted (an improvement, off
 * by default), as the girls are. The plan, and what was found about them:
 * docs/suitors/PLAN.md. Everything here is in a few parts, in this order:
 *
 *   1. Who they are (kSuitors): five men, their pictures, their favourite
 *      things, their lines.
 *   2. Talking and gifts: their hearts.
 *   3. The heart event: once each, someone comes to the farm to ask for
 *      help; the day ends when it is over.
 *   4. The festivals: asking one of them to dance.
 *   5. The proposal: the Blue Feather, shown to him.
 *   6. The wedding: the game's own scene, with him as the bride.
 *   7. Married life: his day on the farm and at home.
 *   8. The children: the game's own counting; the baby is adopted, brought
 *      by the Harvest Sprites.
 *   9. The ending: the game's own, with him as the wife.
 *  10. Every frame: putting it all together.
 *
 * Who he is. Not by the slot or the map (the town's scripts put people in
 * different slots and places): any event slot in use whose picture is one
 * of his sprites. Nobody else uses them, except the carpenter's: the
 * three carpenters share one set, so he is the one whose script is his
 * (the one who says "We are carpenters", on the mountain).
 *
 * Love goes from 0 to 1000 (ten hearts, 100 each, as the Goddess's), in
 * steps of 4, and is kept in the diary (src/save.c) with the heart events
 * and the wedding. What happens in a day is not kept: the diary is written
 * at night, when the day is over.
 *
 * Everything they say is in the game's own text box (textbox.c), in
 * English, as the game's.
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
#include "host.h"
#include "textbox.h"
#include "ui.h"
#include "suitors.h"

extern Ppu *g_ppu;

#define SALE_TABLE 0x81A094            /* per item 3 bytes: its text, then the money */
#define SLOTS      0x0B586u            /* the event slots: $7E:B586 + $40 x n (fields: SLOT_*) */
#define SLOT_AT(n) (SLOTS + 0x40u * (uint32_t)(n))
enum {
  SLOT_COUNT = 0x20,
  LOVE = 0x03FF, TALKED = 0x0400, GIFTED = 0x0800, DANCED = 0x1000,   /* a suitor's word (g_hm_suitor) */
  MOST_LOVE = 1000,
  TALK_LOVE = 4,                       /* the first talk of the day */
  FAVOURITE_LOVE = 120,                /* a favourite, given */
  DISLIKED_LOVE = -52,
  PLAIN_LOVE = 4,                      /* a thing that sells for nothing */
  DANCE_LOVE = 80,
  EVENT_LOVE = 500,                    /* five hearts: his heart event */
  /* love moves in steps of 4: the diary keeps it as love / 4 (src/save.c) */
  CARRIED = 2, SHIPPED = 8,            /* a map object's state: in the hands; taken away */
  IT_FLOWER = 0x19,
};
enum { DISLIKED, PLAIN, FINE, FAVOURITE };
#define COUNT(a) (int)(sizeof (a) / sizeof (a)[0])

/* g_hm_suitor_wed (hm_decomp.h): who, engaged, married, nights left */
enum { WED_WHO = 0x07, WED_ENGAGED = 0x08, WED_MARRIED = 0x10, WED_NIGHTS_SHIFT = 5 };

/* ── 1. Who they are ──────────────────────────────────────────────────── */

/* His heart event: who comes to the farm and what they say, where he is
 * found and what he says there, a choice, and what is said the next day. */
typedef struct {
  int messenger;                       /* the one who comes (a front picture) */
  const char *call;                    /* what they say at the farm */
  int map_from, map_to, x, y;          /* where he is found (the maps of the seasons) */
  int pose;                            /* 0 front, 1 back, 2 side */
  const char *scene;                   /* what he says when the player comes */
  const char *question, *opt[2];       /* the choice (none when question is NULL) */
  const char *answer[2];               /* what he says to each */
  int love[2];                         /* and what each is worth */
  const char *flower;                  /* holding a Flower: said instead, worth love[0] + 100 */
  const char *next_day;                /* the next morning, on the farm */
} HeartEvent;

typedef struct {
  const char *name;                    /* in the pause menu's hearts */
  int sprite_from, sprite_to;          /* his pictures, all of them; nobody else uses them, */
  int script_from, script_to;          /* except the carpenters: then only this script's slot ($B3) */
  int front, back, side;               /* the first of two frames each; the side looks left */
  uint32_t face;                       /* his head's tiles in the ROM (the pause menu) */
  uint8_t favourites[4], disliked[6];  /* item numbers, 0 ends the list */
  const char *talk[4][2];              /* by how much he likes the player: two lines each */
  const char *gift[4][4];              /* by how much he likes the player, and the thing */
  const char *home[2];                 /* married: his words at home and on the farm */
  HeartEvent event;
} Suitor;

static const Suitor kSuitors[HM_SUITORS] = {
  { "Visitor",                          /* Eve's fan, in the bar at night (text $120) */
    0x269, 0x26F, 0, 0, 0x269, 0x26B, 0x26D, 0x07CDC0,
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
    { { "Weeds? Is that a joke?", "For me? Uh, thanks.", "Hey, that's nice of you.", "No way! This is my favourite!" },
      { "...You know I don't like that.", "Thanks. I'll take it home.", "You always bring something good.", "My favourite! You remembered!" },
      { "That hurts, coming from you.", "Thanks. It's the thought that counts.", "I'll keep it. Not because of what it is, because of who.", "You know me better than anyone in this town." },
      { "I'll pretend you didn't give me that.", "Anything from you makes my night.", "You spoil me, you know that?", "This... I think I love you." } },
    { "I wrote another verse today. It's about you, again.",
      "I don't miss the bar at all. The best song is right here." },
    { 0x244,
      "Excuse me! My regular fan didn't come to the bar last night, and he never misses a night. Could you look for him? I'm worried.",
      0x10, 0x13, 648, 432, 1,
      "Oh! You found me. I came up to the lake to write... a song. It isn't for Eve. It's for you. Will you let me sing it for you, from now on?",
      "Will you let him?", { "Yes", "No" },
      { "Then I'll come by the farm instead of the bar. Every night.", "...I see. I'll keep it, anyway. Maybe someday." },
      { 200, -40 }, NULL,
      "There's a folded paper under the door: the words of a song, and your name at the top." } },

  { "Carpenter",                        /* the one by the house, on the mountain (text $128) */
    0x25A, 0x264, 0xEE81, 0xEEA6, 0x25A, 0x25C, 0x25E, 0x07C400,
    { 0x17, 0x10, 0x01 },              /* Large Milk, Corn, Mushroom */
    { 0x02, 0x19, 0x58, 0x59, 0x5A },  /* Poison Mushroom, Flower, the perfumes */
    { { "Need a bigger house? My brothers and I can build it.",
        "Mind the saw. It doesn't care who you are." },
      { "Ah, the farmer. Your fences holding up?",
        "Wood's like people. Treat it right and it lasts." },
      { "I made a little stool from the last of the oak. It's yours, if you want it.",
        "My brothers say I talk about you too much. They're right." },
      { "Every house I build, I think: would you like living here?",
        "I'd build you anything. Starting with a reason to stay." } },
    { { "What am I supposed to do with that?", "Thanks. That'll do.", "Now that's useful. Thanks.", "Ha! Just what a working man needs!" },
      { "Hm. Not my kind of thing.", "Thanks, I'll share it with my brothers.", "You've got a good eye.", "You remembered! Thanks a lot." },
      { "I'll take it... because it's from you.", "Anything from your hands is good enough for me.", "You always know what I need.", "You make a tired man very happy." },
      { "Even that looks nice when you hold it.", "I'll keep it on the workbench, where I can see it.", "You spoil me, farmer.", "I don't need gifts. I only need you. But thanks!" } },
    { "I fixed the squeaky step today. You didn't notice? Good. That's the idea.",
      "This farm is the best thing I ever helped build." },
    { 0x25A,
      "Hey, farmer! My brother went up to cut wood this morning and hasn't come down. Could you go look in the woods? I have to stay with the job.",
      0x10, 0x13, 100, 172, 0,
      "Ow... I slipped under the logs and twisted my ankle. I was starting to think nobody would come. Thanks. Lean on you? Just to get down the mountain.",
      "What do you say?", { "Thank him", "Stay a while" },
      { "No, thank YOU. I'll make it up to you, I promise.", "...You'd stay? Then let's sit a bit. The woods are nice with you here." },
      { 120, 200 }, NULL,
      "By the door there's a small oak stool, and a note: \"So you can rest. You work harder than we do.\"" } },

  { "Ann's dad",                        /* the tool shop's owner: a widower */
    0x296, 0x29C, 0, 0, 0x296, 0x298, 0x29A, 0x080C00,
    { 0x12, 0x14, 0x18 },              /* Potato, Egg, Herb */
    { 0x02, 0x09, 0x0A, 0x0B, 0x0C },  /* Poison Mushroom, the weeds */
    { { "Welcome! A good tool is half the work done.",
        "Ann's in the back, if you're looking for her." },
      { "Keep your hoe sharp and your back straight.",
        "Since my wife passed, it's been just Ann and me. It's quiet." },
      { "Ann says I hum when you come in. I don't hum.",
        "I keep forgetting to close the shop when you're here." },
      { "I thought I was done with all this. Then you walked in.",
        "Ann says I should tell you how I feel. So... I'm telling you." } },
    { { "That's... not something I'd sell.", "Oh, thank you.", "Well, now. That's kind.", "You know what I like! Thank you!" },
      { "Hm. Let's not do that again.", "Thanks. Ann and I will have it for supper.", "That's very thoughtful of you.", "Ha! You know me well." },
      { "From you I'll take even that.", "It's not the gift. It's that you thought of me.", "You make this old shop feel brighter.", "My wife used to bring me these. You remind me of happy days." },
      { "I'll smile anyway, because it's you.", "Everything you bring, I keep.", "You're too good to me.", "You've given me more than this. You've given me a reason." } },
    { "Ann came by with a cake. She says the shop runs better without me. Hmph.",
      "I sharpened all your tools. Don't tell anyone, they'll all want it free." },
    { 0x226,
      "Have you seen my dad? He went out this morning and hasn't come back. That's not like him at all...",
      0x04, 0x07, 432, 172, 1,
      "Oh... it's you. This is my wife's grave. I come every Sunday; Ann doesn't know. I was telling her about you. I think she would have liked you.",
      "What will you do?", { "Stay with him", "Leave" },
      { "...Thank you. It's easier to talk to her with you here.", "Of course. I'll stay a little longer." },
      { 120, 0 },
      "You lay your Flower beside his. He looks at it for a long time. \"...She loved these. Thank you.\"",
      "My dad came home happy yesterday... humming, even. Do you know why?" } },

  { "Shipper",                          /* the one in the vest, in charge of shipping */
    0x270, 0x275, 0, 0, 0x270, 0x272, 0x274, 0x07D540,
    { 0x11, 0x04, 0x17 },              /* Tomato, Tropical Fruit, Large Milk */
    { 0x02, 0x09, 0x0A, 0x0B, 0x0C },  /* Poison Mushroom, the weeds */
    { { "Put it in the box before five and we'll pick it up. Simple!",
        "You can't use the tool when you're worn out. Rest a little." },
      { "Your box gets heavier every day. Nice work.",
        "I carry crates all day. Talking to you is the easy part." },
      { "I always save your farm for last. So I can stay a little longer.",
        "My trainee asked why I'm smiling so much. I didn't tell him." },
      { "I'd carry anything for you. Even my own heart, if you asked.",
        "Every evening I hope there's a note in your box. For me." } },
    { { "I ship stuff, I don't eat weeds.", "Oh, for me? Thanks.", "Nice! That's a good one.", "No way! My favourite! Thanks!" },
      { "Uh... I'll pass that on to someone else.", "Thanks. I'll have it on my break.", "You always pick the good ones.", "You remembered! I'm so happy!" },
      { "Ha, even that, if it's from you.", "From you? I'll treasure it.", "Careful, I could get used to this.", "You're the best thing on my whole route." },
      { "I'll take it. I'd take anything you gave me.", "I'll keep it in my bag, close by.", "You make every day feel like a festival.", "I love it. And... I love you." } },
    { "The truck can wait. I'd rather watch you work.",
      "Five o'clock is still my favourite hour. Now I don't have to leave." },
    { 0x276,
      "Help! My boss lost the bag with the day's payments, and if it doesn't turn up he's in big trouble! He's looking in town. Please!",
      0x04, 0x07, 488, 282, 0,
      "You found it! It was right behind the fence the whole time... Thank you. Stay a minute? The sun's going down. I always save the sunset for the last stop.",
      NULL, { NULL, NULL }, { NULL, NULL }, { 160, 0 }, NULL,
      "There's a note in the shipping box: \"Thanks for the sunset.\"" } },

  { "Trainee",                          /* the one with the cap, still learning the job */
    0x276, 0x27B, 0, 0, 0x276, 0x278, 0x27A, 0x07DCC0,
    { 0x14, 0x05, 0x06 },              /* Egg, Fullmoon Berry, Cake */
    { 0x02, 0x09, 0x0A, 0x0B, 0x0C },  /* Poison Mushroom, the weeds */
    { { "I'm still learning the shipping job. I'll get it right!",
        "Let's both work hard today, okay?" },
      { "My boss says I'm getting faster. It's because I want to see your farm sooner.",
        "Need a lift home? Ha, I'm kidding. Unless you do." },
      { "I practise my lines before your farm. They never come out right.",
        "You make me want to work twice as hard. Never give up!" },
      { "My goal used to be the truck. Now it's you.",
        "When I'm a real shipper, I'll still stop here first." } },
    { { "Ugh! Weeds? Not cool.", "Oh! Thanks!", "Wow, that's great!", "No way! My favourite! You're the best!" },
      { "Hm... no thanks.", "Thanks a lot!", "This is a good one, I can tell!", "You remembered! Never giving up... on you!" },
      { "From you? Okay... I guess.", "I'll eat it on the truck, thinking of you.", "You always know!", "I'm the luckiest trainee in the world." },
      { "Even this makes me smile. Weird.", "Everything you give me goes in my lucky bag.", "I don't deserve you.", "I love it. I love you. There, I said it!" } },
    { "I finished my training! Now I'm a shipper... and a husband. Never give up!",
      "I'll carry the heavy things. That's what I'm good at!" },
    { 0x270,
      "Excuse me, rancher! My trainee went to deliver to the mountain this morning and never came back. Could you check on him?",
      0x10, 0x13, 480, 616, 0,
      "Rancher! I got lost... I kept walking in circles. I was scared, but I said to myself, never give up! And then you came. You're my hero.",
      "What do you say?", { "Walk him back", "Tease him" },
      { "Together? ...Yeah. Let's go together.", "Hey! ...Okay, I deserve that. Thanks for coming anyway." },
      { 160, 80 }, NULL,
      "There's a crate by your door with a big drawing of you on it, and the words: \"My hero. Never give up!\"" } },
};

/* At the festivals, before the dance: what each says (the Flower Festival,
 * the Harvest Festival), as the town's people have their festival lines. */
static const char *const kFestival[HM_SUITORS][2] = {
  { "Eve's singing tonight too, but I wanted to see the flowers. And you.",
    "Harvest time! I wrote a little song about pumpkins. Don't laugh." },
  { "We built the stage last night. Don't jump on it too hard.",
    "Good harvest this year? Then the barn I'll build you next will be full." },
  { "My wife loved this festival. Ann's wearing her ribbon today.",
    "Ann made the stew this year. Try it, then tell her it's better than mine." },
  { "No shipping today! Even I get a day off for the flowers.",
    "All the crops of the valley went through my hands this year. Yours were the best." },
  { "My boss said I could come! I've never seen so many flowers.",
    "I carried all the pumpkins up here by myself. Never give up!" },
};

/* The next morning: who comes up the path to say it (as the day before),
 * or 0 when it is something left at the door (said as it is found). */
static const int kNextMorningBy[HM_SUITORS] = { 0, 0, 0x226 /* Ann */, 0, 0 };

/* ── What they say ──────────────────────────────────────────────────────
 * Wrapped to the box's width, a page each say(); the box opens at the end
 * of the frame. A question may follow the pages: two answers on the
 * box's last line, as the game's own choices. */
enum { PAGES = 6 };   /* as many as the box takes (textbox.h) */
static char s_said[PAGES][TEXTBOX_LINES][TEXTBOX_COLUMNS + 1];
static int s_said_pages;
static bool s_want_dialog;
static TextboxTalk s_talk;
static struct {
  bool asked, open;                    /* a question follows the pages / it is on screen */
  char line[2][TEXTBOX_COLUMNS + 1];
  const char *opt[2];
  int sel;
  TextboxPrinter printer;
  void (*answered)(int choice);
} Q;

static void say(const char *words)
{
  int line = 0;
  const char *p = words;
  while (*p && s_said_pages < PAGES) {
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
/* a question after what is said: two lines at most, two answers */
static void ask(const char *question, const char *a, const char *b, void (*answered)(int))
{
  memset(&Q, 0, sizeof Q);
  snprintf(Q.line[0], sizeof Q.line[0], "%s", question);
  Q.opt[0] = a; Q.opt[1] = b;
  Q.answered = answered;
  Q.asked = true;
  s_want_dialog = true;
}

/* ── Small helpers ──────────────────────────────────────────────────── */
static int today(void) { return g_ram[HM_YEAR] * 120 + (g_ram[HM_SEASON] & 3) * 30 + g_ram[HM_DAY]; }
static int love(int who) { return g_hm_suitor[who] & LOVE; }
static void add_love(int who, int d)
{
  d = d >= 0 ? (d + 3) / 4 * 4 : -((-d + 3) / 4 * 4);   /* steps of 4 (src/save.c) */
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
static int slot16(uint32_t at, int field) { return g_ram[at + field] | g_ram[at + field + 1] << 8; }
static void set16(uint32_t at, int field, int v) { g_ram[at + field] = (uint8_t)v; g_ram[at + field + 1] = (uint8_t)(v >> 8); }
static bool bad_weather(void) { return hm_rd16(g_ram, HM_WEATHER_FLAGS) & (0x0002 | 0x0008); }   /* rain, snow */
static bool special_day(void) { return hm_rd16(g_ram, HM_WEATHER_FLAGS) & 0x0100; }               /* a festival */
static bool on_farm(void) { return g_ram[HM_MAP_ID] <= 3; }
static bool in_house(void) { int m = g_ram[HM_MAP_ID]; return m >= 0x15 && m <= 0x17; }
static int husband(void)               /* the suitor married (0-4), or -1 */
{
  return (g_hm_suitor_wed & WED_MARRIED) && (g_hm_suitor_wed & WED_WHO) ? (g_hm_suitor_wed & WED_WHO) - 1 : -1;
}
static bool a_wife(void) { return hm_rd16(g_ram, HM_FLAGS_66) & 0x001F; }   /* married to a girl */
static bool goddess_taken(void) { return g_hm_goddess_on && (g_hm_goddess_flags & 0x06); }   /* engaged or married */

/* the player is in control, no box open, not busy (as the pause menu's) */
static bool player_free(bool holding_is_fine)
{
  uint16_t act = hm_rd16(g_ram, HM_PLAYER_ACTION), state = hm_rd16(g_ram, HM_GAME_STATE);
  return hm_diary_loaded(g_ram) && g_ram[HM_INPUT_STATE] == 1 &&
         (act == 0 || act == 1 || act == 2 || act == 9) &&
         (holding_is_fine || g_ram[HM_ITEM_ON_HAND] == 0) &&
         !(state & (0x0010 | 0x0800)) &&                              /* the dog, carried */
         !(hm_rd16(g_ram, HM_FLAGS_60) & 0x0006) &&                   /* the hot spring */
         (hm_rd16(g_ram, HM_FLAGS_5A) & 0xC000) == 0x8000;            /* faded in, no box open */
}
static void player_ahead(int *x, int *y)
{
  static const int ahead[4][2] = { { 0, 16 }, { 0, -16 }, { 16, 0 }, { -16, 0 } };
  int f = hm_rd16(g_ram, HM_PLAYER_DIRECTION) & 3;
  *x = hm_rd16(g_ram, HM_PLAYER_X) + ahead[f][0];
  *y = hm_rd16(g_ram, HM_PLAYER_Y) + ahead[f][1];
}
static bool near_player(int x, int y, int d)
{
  return abs(hm_rd16(g_ram, HM_PLAYER_X) - x) < d && abs(hm_rd16(g_ram, HM_PLAYER_Y) - y) < d;
}

/* Free ground, as the game's own collision sees it (collision.c, and the
 * wife's help in wife.c): the tile under a point is free when its property
 * blocks no part of it; the doors' tiles ($C0-$CF) are left alone. A person
 * stands on his feet: the point (x, y) and 6 pixels to each side must be
 * free, and nobody else may be standing within 14 pixels. */
static int map_w(void) { uint8_t c = g_ram[HM_MAP_WIDTH]; return c == 1 ? 16 : c == 2 ? 32 : 64; }
static uint32_t tile_props(void) { return (uint32_t)(g_ram[HM_TILE_PROPS] | g_ram[HM_TILE_PROPS + 1] << 8 | g_ram[HM_TILE_PROPS_BANK] << 16); }
static bool free_point(int x, int y)
{
  int tx = x >> 4, ty = y >> 4, w = map_w();
  if (x < 16 || y < 16 || tx >= w - 1 || ty >= 63) return false;
  uint8_t t = g_ram[HM_MAP_TILES + ty * w + tx];
  if (t >= 0xC0 && t <= 0xCF) return false;
  return !(hm_bus_rd8(g_ram, tile_props() + 4u * t + 2) & 0x1F);
}
static bool someone_at(int x, int y, int except)
{
  for (int n = 0; n < SLOT_COUNT; n++) {
    uint32_t at = SLOT_AT(n);
    if (n == except || !g_ram[at] || (g_ram[at + SLOT_FLAGS] & 0x40)) continue;
    if (abs(slot16(at, SLOT_X) - x) < 14 && abs(slot16(at, SLOT_Y) - y) < 14) return true;
  }
  return abs(hm_rd16(g_ram, HM_PLAYER_X) - x) < 14 && abs(hm_rd16(g_ram, HM_PLAYER_Y) - y) < 14;
}
static bool free_ground(int x, int y) { return free_point(x - 6, y) && free_point(x + 6, y) && free_point(x, y - 4); }
/* the free place nearest to (x, y), looked for in rings 8 pixels apart, up
 * to 64 pixels away; (x, y) itself when nothing is free */
static void nearest_free(int *x, int *y, int except)
{
  for (int r = 0; r <= 64; r += 8)
    for (int dy = -r; dy <= r; dy += 8)
      for (int dx = -r; dx <= r; dx += 8) {
        if (abs(dx) != r && abs(dy) != r) continue;         /* the ring only */
        int nx = *x + dx, ny = *y + dy;
        if (free_ground(nx, ny) && !someone_at(nx, ny, except)) { *x = nx; *y = ny; return; }
      }
}

/* the slot as a still picture of ours, made by the game on its next frame
 * (as goddess.c's put): an idle script of the game's own, $B3:8DED */
enum { IDLE_SCRIPT = 0x8DED };
static bool ours(uint32_t at)
{
  int pc = slot16(at, SLOT_SCRIPT);
  return g_ram[at] && g_ram[at + SLOT_SCRIPT_BANK] == 0xB3 && pc >= IDLE_SCRIPT && pc < IDLE_SCRIPT + 6;
}
static void put(int number, int sprite, bool flip, int x, int y)
{
  uint32_t at = SLOT_AT(number);
  nearest_free(&x, &y, number);                      /* never on a fence, a stump or someone */
  memset(&g_ram[at], 0, 0x40);
  g_ram[at + SLOT_IN_USE] = 1;
  g_ram[at + SLOT_FLAGS] = 0x03;                    /* a picture; make its game object */
  g_ram[at + SLOT_FRAMES] = 1;
  set16(at, SLOT_SPRITE, sprite); set16(at, SLOT_LAST_SPRITE, sprite);
  g_ram[at + SLOT_ANIMATION] = flip ? 0x40 : 0;
  set16(at, SLOT_X, x); set16(at, SLOT_Y, y);
  memcpy(&g_ram[at + SLOT_HOME_X], &g_ram[at + SLOT_X], 4);
  set16(at, SLOT_SCRIPT, IDLE_SCRIPT);
  g_ram[at + SLOT_SCRIPT_BANK] = 0xB3;
  g_ram[at + SLOT_NUMBER] = (uint8_t)number;
}
/* gone: the game drops its object, and the slot is freed the frame after */
static void take_away(int number)
{
  uint32_t at = SLOT_AT(number);
  if (!ours(at)) return;
  if (g_ram[at + SLOT_FLAGS] & 0x40) { g_ram[at + SLOT_IN_USE] = 0; return; }
  g_ram[at + SLOT_FLAGS] |= 0x40;
}
/* a new picture for a slot of ours (or the flip) */
static void show(int number, int sprite, bool flip)
{
  uint32_t at = SLOT_AT(number);
  if (slot16(at, SLOT_SPRITE) == sprite && !!(g_ram[at + SLOT_ANIMATION] & 0x40) == flip) return;
  set16(at, SLOT_SPRITE, sprite);
  g_ram[at + SLOT_ANIMATION] = (uint8_t)((g_ram[at + SLOT_ANIMATION] & ~0x40) | (flip ? 0x40 : 0));
  if (g_ram[at + SLOT_FLAGS] & 0x02) {              /* not on screen yet: it is made with this picture */
    set16(at, SLOT_LAST_SPRITE, sprite);
    return;
  }
  g_ram[at + SLOT_FLAGS] |= 0x04;                   /* the picture changed */
}

/* Our slots: the game's own people use the first ones on each map; the
 * wife's is $13 (goddess.c), the children's $14 and $15. */
/* the dog takes $16; the Goddess (goddess.c) her imp $17 and her sparkles $18;
 * the festival guests $1B-$1F are only on the festival maps */
enum { HUSBAND_SLOT = 0x13, VISITOR_SLOT = 0x1C, HELPER_SLOT = 0x1D, BABY_SLOT = 0x1E, GUEST_SLOT = 0x1B };

/* ── 2. Talking and gifts ─────────────────────────────────────────────── */
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
  if (who == husband()) { say(s->home[on_farm() ? 1 : 0]); return; }
  say(s->talk[tier(who)][(g_hm_suitor[who] & TALKED) ? 1 : 0]);   /* the second line once talked to today */
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

/* the slot is suitor `who`'s, by its picture (and, for the carpenter, its script) */
static bool is_him(uint32_t at, int who)
{
  const Suitor *s = &kSuitors[who];
  int sprite = slot16(at, SLOT_SPRITE);
  if (!g_ram[at + SLOT_IN_USE] || sprite < s->sprite_from || sprite > s->sprite_to) return false;
  if (!s->script_from || ours(at)) return true;
  int pc = slot16(at, SLOT_SCRIPT);
  return g_ram[at + SLOT_SCRIPT_BANK] == 0xB3 && pc >= s->script_from && pc < s->script_to;
}
static int who_in_slot(uint32_t at)
{
  for (int who = 0; who < HM_SUITORS; who++) if (is_him(at, who)) return who;
  return -1;
}
static bool festival_map(void);
/* the suitor in front of the player (and his slot), or -1 */
static int suitor_in_front(int *slot)
{
  int fx, fy;
  player_ahead(&fx, &fy);
  for (int n = 0; n < SLOT_COUNT; n++) {
    if (!festival_map() && (n == VISITOR_SLOT || n == HELPER_SLOT || n == BABY_SLOT)) continue;   /* the ones who come, not him
                                                     * (at the festivals these slots are the men brought there) */
    uint32_t at = SLOT_AT(n);
    int who = who_in_slot(at);
    if (who < 0) continue;
    int dx = slot16(at, SLOT_X) - fx, dy = slot16(at, SLOT_Y) - fy;
    if (dx >= -12 && dx <= 12 && dy >= -12 && dy <= 12) { if (slot) *slot = n; return who; }
  }
  return -1;
}

/* ── 3. The heart event ─────────────────────────────────────────────────
 * As the girls' are in the game: once his love reaches five hearts, on a
 * morning of good weather that is not a festival, someone comes to the
 * farm (in front of the house's door) and asks for help. He is then
 * somewhere else (the mountain, the town) until the player finds him;
 * finding him is his scene, with a choice, and then the day is over (the
 * night comes, as when the player goes to bed). The next morning there is
 * something on the farm. Once each: g_hm_suitor_events. */
enum { CALL_FROM = 6, CALL_TO = 12 };          /* the morning: the hours someone may come */
static struct {
  enum { EV_NONE, EV_ARRIVING, EV_CALLED, EV_FOUND, EV_OVER } state;
  int who;
  int day;                                     /* the day it happened (the next one says what came of it) */
  int next_day_said;                           /* that day's line was said */
  bool end_day;                                /* the day ends once the box is closed */
} E = { EV_NONE, -1, -1, -1, false };

/* the game's visitor days (mapevents.c, visitor_day): 2-7, 13-17, 25-28 */
static bool visitor_day(void)
{
  int d = g_ram[HM_DAY];
  return (d >= 2 && d <= 7) || (d >= 13 && d <= 17) || (d >= 25 && d <= 28);
}
static int event_due(void)
{
  if (husband() >= 0 || (g_hm_suitor_wed & WED_ENGAGED)) return -1;
  for (int who = 0; who < HM_SUITORS; who++)
    if (love(who) >= EVENT_LOVE && !(g_hm_suitor_events & (1u << who))) return who;
  return -1;
}
static void event_answer(int choice)
{
  const HeartEvent *e = &kSuitors[E.who].event;
  add_love(E.who, e->love[choice]);
  say(e->answer[choice]);
  E.end_day = true;
}
enum { SETTLE_FRAMES = 60 };                   /* a map just entered: let it be seen a moment first */
/* The player held still while someone walks up, as the game's own scenes
 * hold him (Event_DisablePlayerControl: game state bit $4000 off, the
 * script's buttons and wait cleared); everybody else keeps moving. Given
 * back only if it was ours to give. (Not the input state 2: that is "a
 * text box is waiting", and it stops every person on the map.) */
static bool s_holding;
static void hold_player(void)
{
  if (!s_holding && !(hm_rd16(g_ram, HM_GAME_STATE) & 0x4000)) return;   /* a scene of the game's holds him */
  s_holding = true;
  hm_wr16(g_ram, HM_GAME_STATE, (uint16_t)(hm_rd16(g_ram, HM_GAME_STATE) & ~0x4000));
  hm_wr16(g_ram, HM_SCRIPT_BUTTONS, 0);
  hm_wr16(g_ram, HM_WAIT_FRAMES, 0);
}
static void release_player(void)
{
  if (!s_holding) return;
  s_holding = false;
  hm_wr16(g_ram, HM_GAME_STATE, (uint16_t)(hm_rd16(g_ram, HM_GAME_STATE) | 0x4000));
}
/* As the game's own visits (the farm's map events, src/mapevents.c: at
 * 6:00:00 on a visitor day; watched in a test with Maria's): the time
 * stands still indoors, so the first step out of the house in the morning
 * is at 6:00:00; the player is held at the door (hold_player) and
 * whoever comes walks up the
 * stone path from below the screen, about a pixel a frame, stops 16
 * pixels in front, and talks; then walks back the way they came. Walking: two frames each way, the back
 * ones (front + 2) going up, the front ones going down. */
enum { MESSENGER_FROM = 112, MESSENGER_STOP = 16, MESSENGER_STEP = 8, MESSENGER_WAIT = 180 };
static struct { int x, y, to_y, frames, sprite; const char *words; bool going, gone, next_morning; } M;
static void messenger_show(bool up)
{
  int front = M.sprite;
  show(VISITOR_SLOT, front + (up ? 2 : 0) + (M.frames / MESSENGER_STEP) % 2, false);
  set16(SLOT_AT(VISITOR_SLOT), SLOT_X, M.x);
  set16(SLOT_AT(VISITOR_SLOT), SLOT_Y, M.y);
}
static bool player_held(void) { return E.state == EV_ARRIVING; }
static void event_tick(void)
{
  static int map_was = -1, came_from = -1, settled;
  int map = g_ram[HM_MAP_ID];
  if (map != map_was) { came_from = map_was; map_was = map; settled = 0; }
  /* out of the house, onto the farm, on a morning someone is coming: hold the player at once */
  if (E.state == EV_NONE && on_farm() && came_from >= 0x15 && came_from <= 0x17 && settled == 0 &&
      !bad_weather() && !special_day() && E.day != today() && visitor_day() &&
      g_ram[HM_HOUR] == 6 && g_ram[HM_MINUTES] == 0) {
    if (E.who >= 0 && today() == E.day + 1 && kNextMorningBy[E.who] && E.next_day_said != today()) {
      E.next_day_said = today();                     /* the next morning: someone comes to say what came of it */
      memset(&M, 0, sizeof M);
      M.sprite = kNextMorningBy[E.who]; M.words = kSuitors[E.who].event.next_day; M.next_morning = true;
      E.state = EV_ARRIVING; hold_player();
    } else {
      int who = event_due();
      if (who >= 0) {
        E.who = who; E.state = EV_ARRIVING; E.day = today(); E.end_day = false;
        memset(&M, 0, sizeof M);
        M.sprite = kSuitors[who].event.messenger; M.words = kSuitors[who].event.call;
        hold_player();
      }
    }
  }
  if (settled < SETTLE_FRAMES && E.state != EV_ARRIVING) { settled++; return; }
  if (settled < SETTLE_FRAMES) settled++;
  /* the next morning: what came of it, found at the door (unless someone comes to say it) */
  if (E.day >= 0 && E.who >= 0 && !kNextMorningBy[E.who] && today() == E.day + 1 && E.next_day_said != today() &&
      on_farm() && player_free(false)) {
    E.next_day_said = today();
    say(kSuitors[E.who >= 0 ? E.who : 0].event.next_day);
  }
  switch (E.state) {
  case EV_NONE:
    return;
  case EV_ARRIVING:                                  /* they come walking up the path */
    if (!on_farm()) { release_player(); E.state = EV_NONE; if (!M.next_morning) E.day = -1; return; }
    /* (the player keeps facing the way he came out: down the path) */
    if (!M.going) {
      hold_player();                                 /* the player waits at the door */
      if (settled < SETTLE_FRAMES || !(hm_rd16(g_ram, HM_FLAGS_5A) & 0x8000)) return;   /* until the farm is all in */
      for (int n = 1; n < HUSBAND_SLOT; n++)         /* a visit of the game's own this morning (its people: */
        if (g_ram[SLOT_AT(n)] && !ours(SLOT_AT(n))) {   /* the first slots): ours another day */
          release_player(); E.state = EV_NONE; if (!M.next_morning) E.day = -1; return;
        }
      if (g_ram[SLOT_AT(VISITOR_SLOT)]) {            /* taken: never keep the player waiting for long */
        if (++M.frames > MESSENGER_WAIT) { release_player(); E.state = EV_NONE; if (!M.next_morning) E.day = -1; }
        return;
      }
      M.x = hm_rd16(g_ram, HM_PLAYER_X);
      M.y = hm_rd16(g_ram, HM_PLAYER_Y) + MESSENGER_FROM;
      M.to_y = hm_rd16(g_ram, HM_PLAYER_Y) + MESSENGER_STOP;
      M.going = true;
      put(VISITOR_SLOT, M.sprite + 2, false, M.x, M.y);
      messenger_show(true);                          /* exactly there: below the screen, on the path */
      return;
    }
    M.to_y = hm_rd16(g_ram, HM_PLAYER_Y) + MESSENGER_STOP;
    hold_player();
    if (M.y > M.to_y) { M.y--; M.frames++; messenger_show(true); return; }
    show(VISITOR_SLOT, M.sprite, false);            /* there: facing the player */
    say(M.words);
    release_player();                              /* the box holds the game now; then the player is free */
    E.state = EV_CALLED;
    M.frames = 0;
    return;
  case EV_CALLED: {                                  /* off they go; he is where he is */
    const HeartEvent *e = &kSuitors[E.who].event;
    if (on_farm() && !M.gone && ours(SLOT_AT(VISITOR_SLOT))) {   /* heard: back down the path */
      if (s_said_pages) return;
      if (M.frames++ < MESSENGER_FROM) { M.y++; messenger_show(false); return; }
      take_away(VISITOR_SLOT);
      M.gone = true;
      if (M.next_morning) E.state = EV_NONE;       /* said: nothing more to it */
      return;
    }
    if (M.next_morning) { if (!on_farm()) E.state = EV_NONE; return; }
    if (today() != E.day) { E.state = EV_NONE; return; }   /* never found: another day */
    if (map < e->map_from || map > e->map_to) return;
    const Suitor *s = &kSuitors[E.who];
    int sprite = e->pose == 1 ? s->back : e->pose == 2 ? s->side : s->front;
    if (!ours(SLOT_AT(VISITOR_SLOT))) { if (g_ram[SLOT_AT(VISITOR_SLOT)]) return; put(VISITOR_SLOT, sprite, false, e->x, e->y); }
    if (near_player(e->x, e->y, 36) && player_free(true)) {
      show(VISITOR_SLOT, s->front, false);           /* he turns to the player */
      bool flower = e->flower && g_ram[HM_ITEM_ON_HAND] == IT_FLOWER && (hm_rd16(g_ram, HM_GAME_STATE) & 0x0002);
      say(e->scene);
      if (flower && take_from_hands(IT_FLOWER)) {
        say(e->flower);
        add_love(E.who, e->love[0] + 100);
        E.end_day = true;
      } else if (e->question) ask(e->question, e->opt[0], e->opt[1], event_answer);
      else { add_love(E.who, e->love[0]); E.end_day = true; }
      g_hm_suitor_events |= (uint8_t)(1u << E.who);
      E.state = EV_FOUND;
    }
    return;
  }
  case EV_FOUND:
    if (!s_said_pages && !Q.asked && !Q.open && E.end_day) {   /* all said: the night comes */
      E.end_day = false;
      take_away(VISITOR_SLOT);
      g_ram[HM_TIME_RUNNING] |= 0x02;                          /* as going to bed does (UpdateTime) */
      E.state = EV_OVER;
    }
    return;
  case EV_OVER:
    take_away(VISITOR_SLOT);
    if (today() != E.day) E.state = EV_NONE;
    return;
  }
}

/* ── 4. The festivals: a dance ──────────────────────────────────────────
 * As the game's own (watched in tests at the Flower Festival): everybody
 * gathers in the square (map $08; the town sends the player there); the
 * mayor asks whether to start the dance and, with a yes, says "pick your
 * partner" (texts $234 / $235): from then on ($7F1F74 bit $0800) talking
 * to a girl turns her to the player and starts the dance: the square again
 * (map $08 reloaded), the couples dancing, and the partner in slot 7 at
 * the player's side, facing him ($7F1F7B = 2 during it).
 *
 * Before that, each of the men has his festival line (kFestival). The men
 * the game does not bring are put among the people. After "pick your
 * partner", A in front of one of them is the same choice: he turns, says a
 * word, and the same dance starts (the same change of map the girls'
 * scripts make); in it, the partner in slot 7 is him: the game's steps
 * with his pictures, as at the wedding (dressed). What the dance gives the
 * girls' hearts is given back; he gets his. */
enum { FLOWER_FESTIVAL = 0x08, HARVEST_FESTIVAL = 0x09, PARTNER_TIME = 0x0800, DANCING = 2, PARTNER_SLOT = 7 };
/* where the men the game does not bring stand: away from the mayor and his
 * wife (left, (88, 232) and (120, 248)) and the ways to them: the open
 * right side and the front of the square, 64 pixels apart */
static const int kGuestSpots[HM_SUITORS][2] = { { 200, 376 }, { 376, 280 }, { 328, 376 }, { 376, 328 }, { 264, 376 } };
static int s_danced_day = -1;
static struct { int who; bool asked, on; uint16_t girls[5]; } P = { -1 };   /* on: from the change of map to the end */
static bool festival_map(void) { int m = g_ram[HM_MAP_ID]; return m == FLOWER_FESTIVAL || m == HARVEST_FESTIVAL; }
static bool partner_time(void) { return festival_map() && (hm_rd16(g_ram, 0x11F74) & PARTNER_TIME) && g_ram[0x11F7B] != DANCING; }
static bool dancing(void) { return festival_map() && g_ram[0x11F7B] == DANCING; }
static bool present(int who)
{
  for (int n = 0; n < SLOT_COUNT; n++) if (is_him(SLOT_AT(n), who)) return true;
  return false;
}
static void festival_tick(void)
{
  static int map_was = -1;
  int map = g_ram[HM_MAP_ID];
  bool fest = festival_map() && hm_diary_loaded(g_ram) && !dancing() && !P.on;
  if (P.who >= 0 && !festival_map()) {             /* the festival is over: the girls' hearts as they were */
    for (int i = 0; i < 5; i++) hm_wr16(g_ram, HM_HEARTS_BASE + 2 * i, P.girls[i]);
    P.who = -1; P.on = false;
  }
  if (!fest) { if (map_was >= 0) for (int i = 0; i < HM_SUITORS; i++) take_away(GUEST_SLOT + i); map_was = -1; return; }
  if (map_was != map) { map_was = map; return; }      /* a frame later: the game's people are placed */
  for (int who = 0; who < HM_SUITORS; who++) {
    int n = GUEST_SLOT + who;
    if (ours(SLOT_AT(n)) || g_ram[SLOT_AT(n)]) continue;
    if (present(who)) continue;
    put(n, kSuitors[who].front, false, kGuestSpots[who][0], kGuestSpots[who][1]);
  }
}
/* he is chosen: once his word is heard, the dance starts as the girls' scripts start it */
static void choose_partner(int who, int slot)
{
  show(slot, kSuitors[who].front, false);           /* he turns to the player */
  say(who == husband() ? "Of course, dear. Like the day we met." : "Me? ...I was hoping you'd ask.");
  P.who = who; P.asked = true;
  for (int i = 0; i < 5; i++) P.girls[i] = hm_rd16(g_ram, HM_HEARTS_BASE + 2 * i);
  s_danced_day = today();
  add_love(who, DANCE_LOVE);
}
static void partner_tick(void)
{
  if (!P.asked || s_said_pages) return;
  P.asked = false;
  /* The dance's script finds the partner by the festival's marks of the
   * five girls ($7F1F74 bits 0-4, cleared as each is talked to; the one
   * chosen keeps hers): the first one marked dances, in her own script
   * (B3:FBD7 for the first). So the first is marked alone, and she is him
   * (partner_picture). */
  hm_wr16(g_ram, 0x11F74, (uint16_t)((hm_rd16(g_ram, 0x11F74) & ~0x001F) | 0x0001));
  g_ram[HM_TRANSITION_MAP] = g_ram[HM_MAP_ID];      /* the square again: the dance */
  P.on = true;                                       /* dressed from the partner's first frame */
  hm_wr16(g_ram, HM_WEATHER_FLAGS, (uint16_t)(hm_rd16(g_ram, HM_WEATHER_FLAGS) | 0x4000));
}
/* in the dance, the partner's pictures (the girls' dancing set $21E-$225:
 * front, back, side, two each, then a back and a side) are his */
static bool partner_picture(uint32_t at)
{
  if (P.who < 0 || !P.on || at != SLOT_AT(PARTNER_SLOT)) return false;
  int sprite = slot16(at, SLOT_SPRITE);
  if (sprite < 0x21E || sprite > 0x225) return false;
  const Suitor *s = &kSuitors[P.who];
  static const int8_t kind[8] = { 0, 0, 1, 1, 2, 2, 1, 2 };   /* 0 front, 1 back, 2 side */
  int k = sprite - 0x21E, base = kind[k] == 0 ? s->front : kind[k] == 1 ? s->back : s->side;
  int to = base + (k < 6 ? (k & 1) : 0);
  set16(at, SLOT_SPRITE, to);
  g_ram[at + SLOT_ANIMATION] &= (uint8_t)~0x0E;      /* his own colours */
  g_ram[at + SLOT_FLAGS] |= 0x04;
  return true;
}

/* ── 5. The proposal ────────────────────────────────────────────────────
 * The Blue Feather, shown to him (ToolUsedBlueFeather calls
 * g_hm_feather_used): with his ten hearts, the house built up once and
 * nobody else, it is a yes, and the wedding is a week later. */
static bool s_feather_used;
static void (*s_prev_feather)(void);
static void feather_used(void) { s_feather_used = true; if (s_prev_feather) s_prev_feather(); }
static void propose(int who)
{
  if (husband() == who) { say("I still keep your feather by the bed."); return; }
  if (husband() >= 0 || a_wife() || goddess_taken()) { say("A feather...? You already gave your heart to someone."); return; }
  if (g_hm_suitor_wed & WED_ENGAGED) {
    say((g_hm_suitor_wed & WED_WHO) == who + 1 ? "I'm counting the days. Only a few more!" : "A feather...? But you're engaged to someone else!");
    return;
  }
  if (love(who) < MOST_LOVE) { say("A feather...? It's pretty. But we hardly know each other yet."); return; }
  if (!(hm_rd16(g_ram, HM_FLAGS_64) & 0x0040)) { say("A Blue Feather... for me? Oh... but where would we live? Your house is so small."); return; }
  g_hm_suitor_wed = (uint8_t)((who + 1) | WED_ENGAGED | 7 << WED_NIGHTS_SHIFT);
  say("A Blue Feather... for me? Yes. Yes! I'll marry you! In a week, at the church.");
}

/* ── 6. The wedding ─────────────────────────────────────────────────────
 * A week after the proposal, the next time the player is on the farm and
 * free, the game's own wedding plays (the scene in front of the church,
 * map $2D, event group $1E), as goddess.c does it: its first script picks
 * the bride by the wife's bit, so Maria's is lent for that moment and
 * taken back as soon as the scene is on. The bride's slot (1, Maria's
 * wedding pictures $330-$333) shows him instead (his front frames). */
enum {
  WEDDING_MAP = 0x2D, LENT_WIFE = 0x0001,
  SLOT_BRIDE = 1, BRIDE_SPRITES = 0x330,
  GROUP_1E_FROM = 0x967A, GROUP_1E_TO = 0x9A3F,
};
static enum { NO_WEDDING, WEDDING_ASKED, WEDDING_ON } s_wedding;
static unsigned s_wedding_frames;
static void (*s_prev_picture)(uint8_t *ram, uint32_t at);
static bool s_lent_for_ending;
static void dressed(uint8_t *ram, uint32_t at);

static void wedding_tick(void)
{
  uint8_t *ram = g_ram;
  int map = ram[HM_MAP_ID];
  if (s_wedding == NO_WEDDING) {
    if (!(g_hm_suitor_wed & WED_ENGAGED) || (g_hm_suitor_wed >> WED_NIGHTS_SHIFT)) return;
    if (map > 3 || !player_free(false) || a_wife()) return;
    hm_wr16(ram, HM_FLAGS_66, (uint16_t)(hm_rd16(ram, HM_FLAGS_66) | LENT_WIFE));
    ram[HM_TRANSITION_MAP] = WEDDING_MAP;
    hm_wr16(ram, HM_WEATHER_FLAGS, (uint16_t)(hm_rd16(ram, HM_WEATHER_FLAGS) | 0x4000));
    s_wedding = WEDDING_ASKED;
    s_wedding_frames = 0;
    return;
  }
  s_wedding_frames++;
  bool town = map >= 4 && map <= 7;
  if (s_wedding == WEDDING_ASKED) {
    uint32_t bride = SLOT_AT(SLOT_BRIDE);
    int script = slot16(bride, SLOT_SCRIPT);
    if (town && ram[bride] && ram[bride + SLOT_SCRIPT_BANK] == 0xB4 && script >= GROUP_1E_FROM && script < GROUP_1E_TO) {
      hm_wr16(ram, HM_FLAGS_66, (uint16_t)(hm_rd16(ram, HM_FLAGS_66) & ~LENT_WIFE));   /* given back */
      g_hm_suitor_wed = (uint8_t)((g_hm_suitor_wed & WED_WHO) | WED_MARRIED);
      s_wedding = WEDDING_ON;
    } else if (s_wedding_frames > 600) {             /* it never came: give the bit back, try again later */
      hm_wr16(ram, HM_FLAGS_66, (uint16_t)(hm_rd16(ram, HM_FLAGS_66) & ~LENT_WIFE));
      s_wedding = NO_WEDDING;
    }
    return;
  }
  if (!town) {                                       /* the scene is over (the next morning at home) */
    s_wedding = NO_WEDDING;
    say("Good morning... husband. I still can't believe it. Let's make this farm a home.");
  }
}

/* ── 7. Married life ────────────────────────────────────────────────────
 * His day, as a wife's: at home in the morning, out on the farm from 9:00
 * (walking between a few places around the house and the field), home
 * again from 18:00; at home all day when it rains or snows. He walks with
 * his own pictures: front, back and side, two frames each, the side
 * mirrored to go right. He keeps still while the player is close, so he
 * can be talked to. In the game's wife slot, $13. */
typedef struct { int x, y; } Spot;
static const Spot kFarmSpots[] = { { 184, 380 }, { 168, 450 }, { 88, 450 }, { 104, 388 } };   /* around the house's front */
/* The house has three sizes (maps $15, $16, $17: built, extended once,
 * twice), each with its door somewhere else: the door is where the player
 * stands when he has just come in (house_door_tick), until then the size's
 * likely place (the visitor of the original's birth comes in at (128, 200)
 * in the largest). His places at home are around it, each moved to the
 * nearest free ground. */
static Spot s_house_door[3] = { { 0x78, 0xC0 }, { 0x78, 0xC0 }, { 128, 200 } };
static const Spot kHouseSpots[] = { { 0, -56 }, { -40, -72 }, { 40, -72 }, { 0, -104 } };   /* from the door */
static const Spot kFarmDoor = { 136, 352 };   /* the house's door, on the farm */
static Spot house_door(void) { int m = g_ram[HM_MAP_ID]; return s_house_door[m >= 0x15 && m <= 0x17 ? m - 0x15 : 0]; }
static void house_door_tick(void)
{
  static int map_was = -1; static bool learned;
  int map = g_ram[HM_MAP_ID];
  if (map != map_was) { map_was = map; learned = false; }
  if (learned || !in_house() || !(hm_rd16(g_ram, HM_FLAGS_5A) & 0x8000) || g_ram[HM_INPUT_STATE] != 1) return;
  learned = true;
  int x = hm_rd16(g_ram, HM_PLAYER_X), y = hm_rd16(g_ram, HM_PLAYER_Y);
  if (y >= 160) s_house_door[map - 0x15] = (Spot){ x, y };   /* came in by the door (not woken up in bed) */
}
enum { OUT_FROM = 9, HOME_FROM = 18, WALK_SPEED = 1, STEP_FRAMES = 10, REST_FRAMES = 240 };
static struct {
  int place;                                   /* 1 the farm, 2 the house, 0 neither */
  int map;
  int x, y, spot, rest, dir;                   /* where he is; the spot he walks to; resting; facing */
  bool leaving;
  unsigned frame;
} H;

static int his_place(void)
{
  int map = g_ram[HM_MAP_ID];
  bool out = !bad_weather() && g_ram[HM_HOUR] >= OUT_FROM && g_ram[HM_HOUR] < HOME_FROM;
  if (map <= 3 && out) return 1;
  if (map >= 0x15 && map <= 0x17 && !out) return 2;
  return 0;
}
/* a step towards (tx, ty); true once there */
/* A step towards (tx, ty), along the longer way first, the other when that
 * one is blocked (free_ground): 1 there, 0 a step taken, -1 blocked both ways.
 * `through` skips the checks (the last steps into the door). */
static int walk_to(int tx, int ty, bool through)
{
  int dx = tx - H.x, dy = ty - H.y;
  if (!dx && !dy) return 1;
  int sx = dx > 0 ? WALK_SPEED : -WALK_SPEED, sy = dy > 0 ? WALK_SPEED : -WALK_SPEED;
  bool x_first = abs(dx) > abs(dy);
  for (int k = 0; k < 2; k++, x_first = !x_first) {
    if (x_first ? !dx : !dy) continue;
    int nx = H.x + (x_first ? sx : 0), ny = H.y + (x_first ? 0 : sy);
    if (!through && !free_ground(nx, ny)) continue;
    H.x = nx; H.y = ny;
    H.dir = x_first ? (sx > 0 ? 3 : 2) : (sy > 0 ? 0 : 1);
    return 0;
  }
  return -1;
}
static void his_picture(int who, bool walking)
{
  const Suitor *s = &kSuitors[who];
  int step = walking && (H.frame / STEP_FRAMES) % 2;
  int base = H.dir == 0 ? s->front : H.dir == 1 ? s->back : s->side;
  show(HUSBAND_SLOT, base + step, H.dir == 3);       /* the side looks left: mirrored for right */
  set16(SLOT_AT(HUSBAND_SLOT), SLOT_X, H.x);
  set16(SLOT_AT(HUSBAND_SLOT), SLOT_Y, H.y);
}
static void married_tick(void)
{
  int who = husband();
  int map = g_ram[HM_MAP_ID];
  bool same_map = map == H.map;
  H.map = map;
  if (who < 0 || s_wedding != NO_WEDDING || s_lent_for_ending || !hm_diary_loaded(g_ram)) {
    if (ours(SLOT_AT(HUSBAND_SLOT))) take_away(HUSBAND_SLOT);
    H.place = 0; H.leaving = false;
    return;
  }
  int place = his_place();
  Spot door = map <= 3 ? kFarmDoor : house_door();
  if (!place && H.place && same_map && ours(SLOT_AT(HUSBAND_SLOT))) H.leaving = true;   /* his hour to go */
  if (H.leaving) {
    if (place || !same_map) H.leaving = false;
    else {
      H.frame++;
      if (walk_to(door.x, door.y, abs(H.x - door.x) + abs(H.y - door.y) < 40) != 0) {   /* in (or stuck): gone */
        take_away(HUSBAND_SLOT); H.place = 0; H.leaving = false; return;
      }
      his_picture(who, true);
      return;
    }
  }
  if (!place) { take_away(HUSBAND_SLOT); H.place = 0; return; }
  Spot home[COUNT(kHouseSpots)];
  for (int i = 0; i < COUNT(kHouseSpots); i++) home[i] = (Spot){ house_door().x + kHouseSpots[i].x, house_door().y + kHouseSpots[i].y };
  const Spot *spots = place == 1 ? kFarmSpots : home;
  int n = place == 1 ? COUNT(kFarmSpots) : COUNT(kHouseSpots);
  if (H.place != place || !ours(SLOT_AT(HUSBAND_SLOT))) {   /* he arrives */
    if (!ours(SLOT_AT(HUSBAND_SLOT)) && g_ram[SLOT_AT(HUSBAND_SLOT)]) return;   /* the slot is the game's now */
    bool by_the_door = H.place == 0 && same_map;
    H.place = place;
    Spot from = by_the_door ? door : spots[0];
    H.x = from.x; H.y = from.y; H.dir = 0;
    H.spot = by_the_door ? 0 : 1 % n; H.rest = by_the_door ? 0 : REST_FRAMES;
    put(HUSBAND_SLOT, kSuitors[who].front, false, H.x, H.y);
    H.x = slot16(SLOT_AT(HUSBAND_SLOT), SLOT_X);     /* where put() found free ground */
    H.y = slot16(SLOT_AT(HUSBAND_SLOT), SLOT_Y);
  }
  H.frame++;
  bool walking = false;
  if (near_player(H.x, H.y, 32)) {                   /* the player is here: he turns and waits */
    int px = hm_rd16(g_ram, HM_PLAYER_X), py = hm_rd16(g_ram, HM_PLAYER_Y);
    if (abs(px - H.x) > abs(py - H.y)) H.dir = px > H.x ? 3 : 2; else H.dir = py > H.y ? 0 : 1;
  } else if (H.rest > 0) H.rest--;
  else {
    Spot to = spots[H.spot];
    nearest_free(&to.x, &to.y, HUSBAND_SLOT);         /* the spot, or the free ground nearest it */
    int step = walk_to(to.x, to.y, false);
    if (step != 0) {                                 /* there, or blocked: rest, then another spot */
      H.rest = REST_FRAMES + (int)(H.frame % 120);
      H.dir = 0;
      if (n > 1) H.spot = (H.spot + 1 + (int)(H.frame % (unsigned)(n - 1))) % n;
    } else walking = true;
  }
  his_picture(who, walking);
}

/* ── 8. The children ────────────────────────────────────────────────────
 * The game's own counting (WifePregnancyAndChildren, src/people.c): it
 * finds the wife by her bit in $7F1F66 and her love among the girls'; for
 * that routine only, Maria's bit and love are lent to him (as goddess.c
 * does for the Goddess). The nights until the wedding are counted here
 * too. When the child "is born" ($7F1F6E bit $10, the days the wife would
 * be in bed), there is no bed: the next time the player is home, the
 * Harvest Sprites bring the baby, and only then the bit is cleared. */
enum { BIRTH_DAYS = 0x0010, ELF_FRONT = 0x309, BABY_SPRITE = 0x300, MOST_GIRL_LOVE = 999 };
static void (*s_prev_night)(uint8_t *ram, bool done);
static bool s_lent_for_night;
static uint16_t s_marias_love, s_children_before;
static bool s_adoption_due;
static bool s_talk_of_child;                         /* a child on its way: he talks of it (adoption_tick) */
static void suitors_night(uint8_t *ram, bool done)
{
  if (s_prev_night) s_prev_night(ram, done);
  if (!done) {
    if (g_hm_suitor_wed & WED_ENGAGED) {            /* a night closer to the wedding */
      int nights = g_hm_suitor_wed >> WED_NIGHTS_SHIFT;
      if (nights) g_hm_suitor_wed = (uint8_t)((g_hm_suitor_wed & 0x1F) | (nights - 1) << WED_NIGHTS_SHIFT);
    }
    int who = husband();
    s_lent_for_night = who >= 0 && !a_wife();
    if (!s_lent_for_night) return;
    s_marias_love = hm_rd16(ram, HM_HEARTS_BASE);
    s_children_before = hm_rd16(ram, HM_FLAGS_6E) & 0x000C;
    hm_wr16(ram, HM_FLAGS_66, (uint16_t)(hm_rd16(ram, HM_FLAGS_66) | LENT_WIFE));
    hm_wr16(ram, HM_HEARTS_BASE, (uint16_t)(love(who) < MOST_GIRL_LOVE ? love(who) : MOST_GIRL_LOVE));
  } else if (s_lent_for_night) {
    s_lent_for_night = false;
    if ((hm_rd16(ram, HM_FLAGS_6E) & 0x000C) != s_children_before) s_talk_of_child = true;   /* a child on its way */
    hm_wr16(ram, HM_FLAGS_66, (uint16_t)(hm_rd16(ram, HM_FLAGS_66) & ~LENT_WIFE));
    hm_wr16(ram, HM_HEARTS_BASE, s_marias_love);
    if (hm_rd16(ram, HM_FLAGS_6E) & BIRTH_DAYS) s_adoption_due = true;   /* a child: adopted, not born (the bit stays,
                                                     * kept in the diary, until the Harvest Sprites have come) */
  }
}
/* As the original's birth (watched in a test: the largest house, the
 * wife in bed, a visitor coming in at the door): the Harvest Sprites come
 * in by the house's door, two of them hopping with the baby between them,
 * up to the room; they talk, the husband takes the baby, and they hop out
 * the way they came. From then on the child is the game's own (the house
 * shows it once it is 60 days old: src/mapevents.c, the family at home).
 * The morning after the night the game counts a child on its way, the
 * husband says what he has been thinking, as a wife tells her news. */
enum { HOP_IN = 40, HOP = 8 };
static struct { int stage; unsigned frames; int x, y; } A;
static void sprites_at(int dy)
{
  int hop = (A.frames / HOP) % 2 ? -2 : 0;
  set16(SLOT_AT(VISITOR_SLOT), SLOT_X, A.x - 14); set16(SLOT_AT(VISITOR_SLOT), SLOT_Y, A.y + dy + hop);
  set16(SLOT_AT(HELPER_SLOT), SLOT_X, A.x + 14);  set16(SLOT_AT(HELPER_SLOT), SLOT_Y, A.y + dy - hop - 2);
  set16(SLOT_AT(BABY_SLOT), SLOT_X, A.x);         set16(SLOT_AT(BABY_SLOT), SLOT_Y, A.y + dy - 4);
}
static void adoption_tick(void)
{
  int who = husband();
  if (who < 0) { s_adoption_due = s_talk_of_child = false; A.stage = 0; return; }
  if (hm_rd16(g_ram, HM_FLAGS_6E) & BIRTH_DAYS) s_adoption_due = true;   /* also from a loaded diary */
  if (s_talk_of_child && (on_farm() || in_house()) && player_free(false) && !A.stage) {
    s_talk_of_child = false;
    say("I've been thinking... This house is big enough now, and there's so much love in it. What if we gave a home to a child who has none?");
    say("The Harvest Sprites know every little one in the valley. I'll ask them. It may take a season.");
    return;
  }
  if (!s_adoption_due && !A.stage) return;
  if (!in_house()) { if (A.stage) { take_away(HELPER_SLOT); take_away(VISITOR_SLOT); take_away(BABY_SLOT); A.stage = 0; } return; }
  switch (A.stage) {
  case 0:                                            /* a knock: they come in by the door */
    if (!player_free(false)) return;
    A.x = house_door().x; A.y = house_door().y; A.frames = 0;
    put(VISITOR_SLOT, ELF_FRONT, false, A.x - 14, A.y);
    put(HELPER_SLOT, ELF_FRONT, true, A.x + 14, A.y);
    put(BABY_SLOT, BABY_SPRITE, false, A.x, A.y);
    sprites_at(0);
    hold_player();                                 /* the player waits, as in the game's scenes */
    A.stage = 1;
    return;
  case 1:                                            /* hopping in */
    hold_player();
    A.frames++;
    sprites_at(-(int)(A.frames / 2 < HOP_IN ? A.frames / 2 : HOP_IN));
    if (A.frames / 2 < HOP_IN) return;
    say("Knock, knock! The Harvest Sprites come in, holding something wrapped in leaves.");
    say("\"We heard there's a farm here with a lot of love and no baby. This little one has no family. Will you take care of it?\"");
    release_player();
    A.stage = 2;
    return;
  case 2:
    if (s_said_pages) return;
    take_away(BABY_SLOT);                            /* in his arms */
    say(kSuitors[who].name[0] == 'V' ? "Your husband takes the baby in his arms. \"...I'll write this little one a song every day.\""
                                     : "Your husband takes the baby in his arms, very carefully. \"Hello, little one. Welcome home.\"");
    say("The Harvest Sprites wave goodbye and hop away into the fields.");
    A.stage = 3; A.frames = 0;
    return;
  case 3:                                            /* hopping out */
    if (s_said_pages) return;
    A.frames++;
    sprites_at(-HOP_IN + (int)(A.frames / 2));
    if (A.frames / 2 < HOP_IN) return;
    take_away(VISITOR_SLOT); take_away(HELPER_SLOT);
    hm_wr16(g_ram, HM_FLAGS_6E, (uint16_t)(hm_rd16(g_ram, HM_FLAGS_6E) & ~BIRTH_DAYS));   /* done */
    s_adoption_due = false;
    A.stage = 0;
    return;
  }
}

/* ── 9. The ending ──────────────────────────────────────────────────────
 * The ending (summer 30 of the third year: StartEnding, src/animals.c,
 * event group $2E) walks the wife and her family through the farm and the
 * town by the wife's bit. For him, Maria's bit is lent for the whole last
 * day (the ending ends the game), and wherever the scenes show the wife
 * (her pictures in the ending, $2E2-$2EF, coloured by the girl) they
 * show him: front, back and side as she faces, in his own colours. */
enum { ENDING_WIFE = 0x2E2, ENDING_WIFE_LAST = 0x2EF };   /* the wife in the ending, whoever she is */
static bool ending_day(void) { return g_ram[HM_YEAR] == 2 && g_ram[HM_SEASON] == 1 && g_ram[HM_DAY] == 30; }
static void ending_tick(void)
{
  if (s_lent_for_ending || !ending_day() || husband() < 0 || a_wife()) return;
  s_lent_for_ending = true;
  hm_wr16(g_ram, HM_FLAGS_66, (uint16_t)(hm_rd16(g_ram, HM_FLAGS_66) | LENT_WIFE));
}
/* g_hm_slot_picture while the wedding or the ending is on (Maria is him),
 * and while dancing (partner_picture) */
static void dressed(uint8_t *ram, uint32_t at)
{
  if (s_prev_picture && s_prev_picture != dressed) s_prev_picture(ram, at);
  if (partner_picture(at)) return;
  int who = s_wedding != NO_WEDDING ? (g_hm_suitor_wed & WED_WHO) - 1 : s_lent_for_ending ? husband() : -1;
  if (who < 0 || who >= HM_SUITORS) return;
  const Suitor *s = &kSuitors[who];
  int sprite = slot16(at, SLOT_SPRITE), to = -1;
  if (sprite >= BRIDE_SPRITES && sprite < BRIDE_SPRITES + 4) to = s->front + (sprite >= BRIDE_SPRITES + 2);
  else if (s_lent_for_ending && sprite >= ENDING_WIFE && sprite <= ENDING_WIFE_LAST) {
    int k = sprite - ENDING_WIFE;                    /* front x2, back x2, side x2, then her own gestures */
    to = (k < 2 ? s->front : k < 4 ? s->back : k < 6 ? s->side : s->front) + (k & 1);
  }
  if (to < 0) return;
  g_ram[at + SLOT_SPRITE] = (uint8_t)to; g_ram[at + SLOT_SPRITE + 1] = (uint8_t)(to >> 8);
  g_ram[at + SLOT_ANIMATION] &= (uint8_t)~0x0E;      /* his own colours, not hers */
  g_ram[at + SLOT_FLAGS] |= 0x04;
}

/* he turns to the player: his back to one above him, his side (mirrored to
 * look right) to one beside him */
static void face_player(int who, int slot)
{
  const Suitor *s = &kSuitors[who];
  uint32_t at = SLOT_AT(slot);
  int dx = hm_rd16(g_ram, HM_PLAYER_X) - slot16(at, SLOT_X), dy = hm_rd16(g_ram, HM_PLAYER_Y) - slot16(at, SLOT_Y);
  if (abs(dx) > abs(dy)) show(slot, s->side, dx > 0);
  else show(slot, dy < 0 ? s->back : s->front, false);
}

/* ── The player's A ───────────────────────────────────────────────────── */
uint16_t suitors_pad(uint16_t pad)
{
  if (player_held()) return 0;                       /* someone is coming: the player waits (as the game holds him) */
  static uint16_t prev;
  static bool hold;
  bool pressed = (pad & B_A) && !(prev & B_A);
  prev = pad;
  if (hold) { if (pad & B_A) return (uint16_t)(pad & ~B_A); hold = false; }
  if (!pressed || !g_cfg.male_suitors || !player_free(true)) return pad;
  if (today() == 1) return pad;                      /* the first day: the shipper's own lesson, as the game has it */
  int slot = -1;
  int who = suitor_in_front(&slot);
  if (who < 0) return pad;
  /* At the festivals, one the game brings itself answers himself: the game
   * marks each one talked to ($7F1F74), and the mayor's wife waits for them
   * all before she moves aside for the player. Only after "pick your
   * partner" is the choice ours. */
  if (festival_map() && !ours(SLOT_AT(slot)) && !partner_time() && !(hm_rd16(g_ram, HM_GAME_STATE) & 0x0002)) return pad;
  face_player(who, slot);                            /* as the town's people do: he turns to whoever talks to him */
  bool holding = hm_rd16(g_ram, HM_GAME_STATE) & 0x0002;
  uint8_t it = g_ram[HM_ITEM_ON_HAND];
  if (holding && it) give(who, it);
  else if (holding) return pad;
  else if (partner_time() && s_danced_day != today()) choose_partner(who, slot);
  else if (festival_map() && !(g_hm_suitor[who] & TALKED)) {   /* the festival: his line for it */
    say(kFestival[who][g_ram[HM_MAP_ID] == HARVEST_FESTIVAL]);
    g_hm_suitor[who] |= TALKED; add_love(who, TALK_LOVE);
  }
  else talk(who);
  hold = true;
  return (uint16_t)(pad & ~B_A);
}

/* ── The box ──────────────────────────────────────────────────────────── */
bool suitors_dialog_wanted(void)
{
  bool b = s_want_dialog && (s_said_pages || Q.asked);
  s_want_dialog = false;
  return b;
}
static void open_pages(void)
{
  memset(&s_talk, 0, sizeof s_talk);
  s_talk.pages = s_said_pages;
  for (int p = 0; p < s_said_pages; p++)
    for (int i = 0; i < TEXTBOX_LINES; i++) s_talk.lines[p][i] = s_said[p][i][0] ? s_said[p][i] : NULL;
  textbox_talk_open(&s_talk);
}
void suitors_dialog_open(void)
{
  if (s_said_pages) open_pages();
  else if (Q.asked) { Q.open = true; Q.printer.shown = Q.printer.tick = 0; }
}
static int q_letters(void) { return textbox_letters(Q.line[0]) + textbox_letters(Q.opt[0]) + textbox_letters(Q.opt[1]); }
static int q_opt_x(int i) { return 16 + 8 * (i ? 15 : 1); }
bool suitors_dialog_update(const UiInput *in)
{
  if (Q.open) {
    if (!textbox_print(&Q.printer, q_letters(), in)) return false;
    if (in->pressed & (B_LEFT | B_RIGHT | B_UP | B_DOWN)) Q.sel ^= 1;
    if (in->back || in->rclick) Q.sel = 1;
    else if (!(in->accept || in->click)) return false;
    Q.open = Q.asked = false;
    void (*answered)(int) = Q.answered;
    memset(s_said, 0, sizeof s_said); s_said_pages = 0;
    if (answered) answered(Q.sel);                   /* what he says to it comes next */
    if (!s_said_pages) return true;
    open_pages();
    return false;
  }
  if (!textbox_talk_update(&s_talk, in)) return false;
  s_said_pages = 0; memset(s_said, 0, sizeof s_said);  /* heard */
  if (Q.asked) { Q.open = true; Q.printer.shown = Q.printer.tick = 0; return false; }
  return true;
}
void suitors_dialog_draw(uint32_t *dst, int w)
{
  if (!Q.open) { textbox_talk_draw(&s_talk, dst, w); return; }
  ui_target(dst, w, 224);
  int ox = (w - 256) / 2;
  textbox_frame(ox);
  int left = Q.printer.shown;
  textbox_line(ox, 0, Q.line[0], &left);
  for (int i = 0; i < 2; i++) {
    textbox_text(ox + q_opt_x(i), 191, Q.opt[i], &left);
    if (i == Q.sel && Q.printer.shown >= q_letters()) {   /* the arrow, once all is printed */
      int ax = ox + q_opt_x(i) - 7, ay = 191 + 6;
      for (int k = 0; k < 4; k++) ui_vline(ax + k, ay + k, 7 - 2 * k, UI_RGB(0xFFFFFF));
    }
  }
}

/* ── For the pause menu ───────────────────────────────────────────────── */
int suitors_count(void) { return HM_SUITORS; }
const char *suitors_name(int who) { return kSuitors[who].name; }
int suitors_love(int who) { return love(who); }
uint32_t suitors_face(int who) { return kSuitors[who].face; }
bool suitors_is_husband(int who) { return who == husband(); }

/* ── 10. Every frame ──────────────────────────────────────────────────── */
void suitors_frame(void)
{
  static int day = -1;
  g_hm_suitors_on = g_cfg.male_suitors;              /* the diary keeps them (src/save.c) */
  if (!g_cfg.male_suitors || !hm_diary_loaded(g_ram)) return;
  if (day != today()) {                              /* a new day: nobody talked to or given anything yet */
    day = today();
    for (int i = 0; i < HM_SUITORS; i++) g_hm_suitor[i] &= (uint16_t)~(TALKED | GIFTED | DANCED);
  }
  /* the game's hooks, shared with the Goddess (goddess.c sets hers first, every frame) */
  if (g_hm_feather_used != feather_used) { s_prev_feather = g_hm_feather_used; g_hm_feather_used = feather_used; }
  if (g_hm_night_wife != suitors_night) { s_prev_night = g_hm_night_wife; g_hm_night_wife = suitors_night; }
  bool dressing = s_wedding != NO_WEDDING || s_lent_for_ending || (P.who >= 0 && P.on);
  if (dressing && g_hm_slot_picture != dressed) { s_prev_picture = g_hm_slot_picture; g_hm_slot_picture = dressed; }
  if (!dressing && g_hm_slot_picture == dressed) { g_hm_slot_picture = s_prev_picture; s_prev_picture = NULL; }

  for (int n = 0; n < SLOT_COUNT; n++)               /* ours, dropped by the game a frame ago: free */
    if (ours(SLOT_AT(n)) && (g_ram[SLOT_AT(n) + SLOT_FLAGS] & 0x40)) take_away(n);

  if (s_feather_used) {
    s_feather_used = false;
    int who = suitor_in_front(NULL);
    if (who >= 0) propose(who);
  }
  event_tick();
  festival_tick();
  partner_tick();
  wedding_tick();
  house_door_tick();
  married_tick();
  adoption_tick();
  ending_tick();
}

void suitors_reset(void)
{
  /* the diary loaded next brings their love back (src/save.c) */
  s_said_pages = 0;
  memset(s_said, 0, sizeof s_said);
  memset(&Q, 0, sizeof Q);
  s_want_dialog = false;
  E.state = EV_NONE; E.who = -1; E.day = -1; E.next_day_said = -1; E.end_day = false;
  s_danced_day = -1;
  P.who = -1; P.asked = P.on = false;
  s_wedding = NO_WEDDING;
  s_lent_for_ending = false;
  s_adoption_due = false;
  memset(&H, 0, sizeof H);
  memset(&A, 0, sizeof A);
  if (g_hm_slot_picture == dressed) g_hm_slot_picture = s_prev_picture;
}
