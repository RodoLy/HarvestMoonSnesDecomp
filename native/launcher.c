/*
 * launcher.c — the native build's launcher, in the look of the game.
 *
 * Before the game starts it is the program's first screen, on a farm at
 * night under the harvest moon; in the game it opens over the last picture
 * (Escape, the pad's L3 or Guide button, or OPTIONS on the title screen)
 * with the game standing still. Its pages:
 *   (the first time, when no ROM is found) Choose your ROM: the ROMs
 *                 found next to the program (and in roms/), or any other
 *                 picked with the system's file window; each is checked
 *                 (its header, its CRC) and the choice is remembered.
 *   Options       window, full screen, the picture, vsync, widescreen,
 *                 sprites, volume, fast forward, language.
 *   Controls      the keys and the gamepad's buttons for each SNES button,
 *                 the gamepads connected (and which one plays).
 *   Improvements  the recompilation's quality-of-life features and cheats.
 *   Save states   (in the game) ten slots.
 * Everything is saved at once in config.ini / keybinds.ini.
 *
 * Drawn on a canvas at the console's resolution (ui.c), with the menus'
 * wooden frames, parchment and the pointing hand, so it has the game's own
 * size of pixels. Keyboard (arrows, Enter, Escape, and the game's keys),
 * gamepad and mouse all work.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "i18n.h"
#include "launcher.h"
#include "native.h"
#include "ui.h"

enum { P_MAIN, P_GAMES, P_OPTIONS, P_CONTROLS, P_MODS, P_STATES, NPAGES };
enum { K_ACTION, K_TOGGLE, K_CHOICE, K_SLIDER, K_KEY, K_PAD };
enum {
  A_PLAY = 1, A_GAMES, A_OPTIONS, A_CONTROLS, A_MODS, A_STATES, A_RESET, A_QUIT, A_BROWSE,
  A_DEFAULT_OPTIONS, A_DEFAULT_KEYS, A_DEFAULT_PAD, A_HOTKEYS, A_PADHELP,
  A_GAME = 100, A_SLOT = 200,
};
enum {
  V_FULLSCREEN = 1, V_SCALE, V_PICTURE, V_VSYNC, V_WIDESCREEN, V_SPRITES, V_VOLUME, V_FF, V_LANG,
  V_SHOWLAUNCHER, V_FASTTEXT, V_DAYLEN, V_PAUSE, V_TITLEOPT, V_ENDLESS, V_MONEY, V_STAMINA,
  V_CLOCK, V_STOCK, V_BAGS, V_WEATHER, V_PADSEL,
};

typedef struct {
  int kind, id;
  const char *label, *desc;
  const char *choices[8];
  int n;                  /* choices; a slider's top */
  char value[96];         /* the right-hand text (keys, buttons, games) */
  bool lit, dim;
  uint32_t mark;          /* a coloured dot before the label (games), 0 none */
} Item;

typedef struct {
  char path[1024], file[256], title[24];
  uint32_t size, crc;
  int verdict;            /* 2 the known ROM, 1 another Harvest Moon, 0 another game, -1 unreadable */
} Game;

static struct {
  bool in_game;
  int page, tab[NPAGES], sel[NPAGES], scroll[NPAGES];
  int wait;               /* 1 a key, 2 a pad button, for button wait_i */
  int wait_i;
  uint32_t wait_frames;
  int confirm, confirm_sel;   /* an A_* asking yes / no */
  int confirm_arg;
  Game games[40];
  int ngames;
  int slot;
  uint32_t frame;
  Item items[40];
  int nitems;
  struct Hit { int x, y, w, h, i; } hits[40];
  int nhits;
  struct { int x, y, w, h, v; } chits[2];
} L;

/* ── The ROMs ────────────────────────────────────────────────────────── */
static uint32_t crc32(const uint8_t *d, size_t n)
{
  static uint32_t t[256];
  if (!t[1]) for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; t[i] = c; }
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++) c = t[(c ^ d[i]) & 255] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}
enum { KNOWN_CRC = 0xF829129Eu };        /* Harvest Moon (USA), 2 MB */

static void inspect(Game *g)
{
  const char *b = strrchr(g->path, '/'), *b2 = strrchr(g->path, '\\');
  if (b2 > b) b = b2;
  snprintf(g->file, sizeof g->file, "%s", b ? b + 1 : g->path);
  g->verdict = -1; g->title[0] = 0; g->size = 0; g->crc = 0;
  FILE *f = fopen(g->path, "rb");
  if (!f) return;
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  if (n < 0x8000 || n > 8 << 20) { fclose(f); g->verdict = 0; return; }
  uint8_t *d = malloc((size_t)n);
  if (!d) { fclose(f); return; }
  size_t got = fread(d, 1, (size_t)n, f);
  fclose(f);
  if (got != (size_t)n) { free(d); return; }
  long skip = (n % 1024) == 512 ? 512 : 0;
  const uint8_t *rom = d + skip;
  g->size = (uint32_t)(n - skip);
  g->crc = crc32(rom, g->size);
  int k = 0;
  for (int i = 0; i < 21; i++) { uint8_t c = rom[0x7FC0 + i]; g->title[k++] = (c >= 32 && c < 127) ? (char)c : ' '; }
  while (k && g->title[k - 1] == ' ') k--;
  g->title[k] = 0;
  g->verdict = g->crc == KNOWN_CRC ? 2 : !strncmp(g->title, "HARVEST MOON", 12) ? 1 : 0;
  free(d);
}
static const char *verdict_text(int v)
{
  return v == 2 ? T("Harvest Moon (USA): checked, ready to play.")
       : v == 1 ? T("Another version of Harvest Moon: it may work (the program follows the USA one).")
       : v == 0 ? T("Not Harvest Moon: this program only plays Harvest Moon (SNES).")
       : T("The file could not be read.");
}
bool launcher_rom_ok(const char *path, char *out, size_t n)
{
  static Game g;
  snprintf(g.path, sizeof g.path, "%s", path);
  inspect(&g);
  if (out) snprintf(out, n, "%s", verdict_text(g.verdict));
  return g.verdict >= 1;
}
static void add_game(const char *path)
{
  if (L.ngames == (int)(sizeof L.games / sizeof L.games[0])) return;
  for (int i = 0; i < L.ngames; i++) if (!strcmp(L.games[i].path, path)) return;
  Game *g = &L.games[L.ngames];
  snprintf(g->path, sizeof g->path, "%s", path);
  inspect(g);
  if (g->verdict < 0) return;
  for (int i = 0; i < L.ngames; i++)             /* the same file reached two ways */
    if (L.games[i].crc == g->crc && !strcmp(L.games[i].file, g->file)) return;
  L.ngames++;
}
static void scan_dir(const char *dir)
{
  static const char *pats[] = { "*.sfc", "*.smc" };
  for (int p = 0; p < 2; p++) {
    int n = 0;
    char **list = SDL_GlobDirectory(*dir ? dir : ".", pats[p], SDL_GLOB_CASEINSENSITIVE, &n);
    if (!list) continue;
    for (int i = 0; i < n; i++) {
      char full[1200];
      size_t dl = strlen(dir);
      snprintf(full, sizeof full, "%s%s%s", dir, (dl && dir[dl - 1] != '/' && dir[dl - 1] != '\\') ? "/" : "", list[i]);
      add_game(full);
    }
    SDL_free(list);
  }
}
static void scan_games(void)
{
  L.ngames = 0;
  if (g_cfg.rom[0]) add_game(g_cfg.rom);
  for (int i = 0; i < g_cfg.ngames; i++) add_game(g_cfg.games[i]);
  const char *base = config_dir();
  scan_dir(base);
  char roms[1100]; snprintf(roms, sizeof roms, "%sroms", base);
  scan_dir(roms);
  char *cwd = SDL_GetCurrentDirectory();          /* and where it was started from */
  if (cwd && strcmp(cwd, base)) scan_dir("");
  SDL_free(cwd);
}
/* ── Values ──────────────────────────────────────────────────────────── */
static const int kFF[4] = { 2, 3, 4, 6 };
static const char *kLangs[3] = { "en", "es", "it" };
static int get_value(int id)
{
  const Config *c = &g_cfg;
  switch (id) {
  case V_FULLSCREEN: return c->fullscreen;
  case V_SCALE: return c->scale - 1;
  case V_PICTURE: return c->exact_pixels;
  case V_VSYNC: return c->vsync;
  case V_WIDESCREEN: return c->widescreen;
  case V_SPRITES: return c->sprite_limits;
  case V_VOLUME: return c->volume / 10;
  case V_FF: for (int i = 0; i < 4; i++) if (kFF[i] == c->ff_speed) return i; return 1;
  case V_LANG: for (int i = 0; i < 3; i++) if (!strcmp(kLangs[i], c->language)) return i; return 0;
  case V_SHOWLAUNCHER: return c->show_launcher;
  case V_FASTTEXT: return c->fast_text;
  case V_DAYLEN: return c->day_length;
  case V_PAUSE: return c->pause_menu;
  case V_TITLEOPT: return c->title_options;
  case V_ENDLESS: return c->endless;
  case V_MONEY: return c->money;
  case V_STAMINA: return c->stamina;
  case V_CLOCK: return c->freeze_clock;
  case V_STOCK: return c->stock;
  case V_BAGS: return c->bags;
  case V_WEATHER: return c->weather;
  case V_PADSEL: return host_gamepad_current();
  }
  return 0;
}
static void set_value(int id, int v)
{
  Config *c = &g_cfg;
  switch (id) {
  case V_FULLSCREEN: c->fullscreen = v; break;
  case V_SCALE: c->scale = v + 1; break;
  case V_PICTURE: c->exact_pixels = v; break;
  case V_VSYNC: c->vsync = v; break;
  case V_WIDESCREEN: c->widescreen = v; break;
  case V_SPRITES: c->sprite_limits = v; break;
  case V_VOLUME: c->volume = v * 10; break;
  case V_FF: c->ff_speed = kFF[v]; break;
  case V_LANG: snprintf(c->language, sizeof c->language, "%s", kLangs[v]); host_apply_language(); break;
  case V_SHOWLAUNCHER: c->show_launcher = v; break;
  case V_FASTTEXT: c->fast_text = v; break;
  case V_DAYLEN: c->day_length = v; break;
  case V_PAUSE: c->pause_menu = v; break;
  case V_TITLEOPT: c->title_options = v; break;
  case V_ENDLESS: c->endless = v; break;
  case V_MONEY: c->money = v; break;
  case V_STAMINA: c->stamina = v; break;
  case V_CLOCK: c->freeze_clock = v; break;
  case V_STOCK: c->stock = v; break;
  case V_BAGS: c->bags = v; break;
  case V_WEATHER: c->weather = v; break;
  case V_PADSEL: host_gamepad_select(v); return;
  }
  host_apply_video();
  host_apply_sound();
  config_save();
}

/* ── The pages' rows ─────────────────────────────────────────────────── */
static Item *add(int kind, int id, const char *label, const char *desc)
{
  Item *it = &L.items[L.nitems++];
  memset(it, 0, sizeof *it);
  it->kind = kind; it->id = id; it->label = label; it->desc = desc;
  return it;
}
static void toggle(int id, const char *label, const char *desc) { add(K_TOGGLE, id, T(label), T(desc)); }
static Item *choice(int id, const char *label, const char *desc, int n, ...)
{
  Item *it = add(K_CHOICE, id, T(label), T(desc));
  va_list ap; va_start(ap, n);
  for (int i = 0; i < n && i < 8; i++) it->choices[i] = va_arg(ap, const char *);
  va_end(ap);
  it->n = n;
  return it;
}

static const char *key_name(SDL_Scancode k)
{
  switch (k) {
  case SDL_SCANCODE_UNKNOWN: return T("(none)");
  case SDL_SCANCODE_UP: return T("Up");
  case SDL_SCANCODE_DOWN: return T("Down");
  case SDL_SCANCODE_LEFT: return T("Left");
  case SDL_SCANCODE_RIGHT: return T("Right");
  case SDL_SCANCODE_RETURN: return "Enter";
  case SDL_SCANCODE_RSHIFT: return T("Right Shift");
  case SDL_SCANCODE_LSHIFT: return T("Left Shift");
  case SDL_SCANCODE_RCTRL: return T("Right Ctrl");
  case SDL_SCANCODE_LCTRL: return T("Left Ctrl");
  case SDL_SCANCODE_SPACE: return T("Space");
  default: break;
  }
  const char *n = SDL_GetScancodeName(k);
  return n && *n ? n : "?";
}

static void build(void)
{
  L.nitems = 0;
  Config *c = &g_cfg;
  switch (L.page) {
  case P_MAIN:
    if (L.in_game) {
      add(K_ACTION, A_PLAY, T("Back to the game"), T("Close this menu and go on playing."));
      add(K_ACTION, A_STATES, T("Save states"), T("Ten slots that keep the game exactly as it is, apart from the diaries. Also F1-F10 to load and Shift+F1-F10 to save."));
      add(K_ACTION, A_RESET, T("Restart"), T("Start the game again from its first screen (as the console's RESET button). Diaries saved in the game are kept."));
    } else {
      add(K_ACTION, A_PLAY, T("Play"), T("Start Harvest Moon with your ROM."));
    }
    add(K_ACTION, A_OPTIONS, T("Options"), T("Window, full screen, the picture, widescreen, sound, fast forward and language."));
    add(K_ACTION, A_CONTROLS, T("Controls"), T("The keys and the gamepad's buttons. Connect a gamepad (USB or Bluetooth) at any time: it appears here."));
    add(K_ACTION, A_MODS, T("Improvements"), T("The recompilation's improvements: fast text, longer days, a pause menu, OPTIONS on the title, an endless game, and cheats."));
    add(K_ACTION, A_QUIT, L.in_game ? T("Quit the game") : T("Quit"), T("Close the program. The game saves only in its diaries (when you sleep)."));
    break;
  case P_GAMES:
    for (int i = 0; i < L.ngames; i++) {
      Game *g = &L.games[i];
      Item *it = add(K_ACTION, A_GAME + i, g->file, NULL);
      it->mark = g->verdict == 2 ? C_GREEN : g->verdict == 1 ? C_GOLD : C_RED;
      bool cur = !strcmp(g->path, c->rom);
      snprintf(it->value, sizeof it->value, "%s", cur ? T("chosen") : "");
      it->dim = g->verdict == 0;
    }
    add(K_ACTION, A_BROWSE, T("Look for another file..."), T("Open the system's file window to pick a ROM (.sfc or .smc) anywhere on this computer."));
    add(K_ACTION, A_QUIT, T("Quit"), T("Close the program."));
    break;
  case P_OPTIONS:
    toggle(V_FULLSCREEN, "Full screen", "The game over the whole screen (also F11 or Alt+Enter).");
    choice(V_SCALE, "Window size", "How many times the console's picture the window is (when it is not full screen).",
           6, "×1", "×2", "×3", "×4", "×5", "×6");
    choice(V_PICTURE, "Picture", "Sharp: as large as the window allows, with no uneven pixels. Exact: whole multiples only, with a border.",
           2, T("Sharp"), T("Exact pixels"));
    toggle(V_VSYNC, "Vertical sync", "Frames follow the screen's refresh (smoothest on 60 Hz screens). Turned off by itself if the screen cannot keep up.");
    toggle(V_WIDESCREEN, "Widescreen (16:9)", "Shows more of the map at the sides instead of black bars. At a map's edge, in small rooms and on the title screen it looks like the original (4:3).");
    choice(V_SPRITES, "Sprites", "Without limit: no flicker when many things share a line. Console: its limit of 32 sprites a line, as on the SNES.",
           2, T("Without limit"), T("As the console"));
    add(K_SLIDER, V_VOLUME, T("Volume"), T("The sound's volume."))->n = 10;
    choice(V_FF, "Fast forward (Tab)", "How fast the game goes while Tab (or the gamepad's right trigger) is held. The music speeds up with it.",
           4, "×2", "×3", "×4", "×6");
    choice(V_LANG, "Language", "The language of these menus and of the pause menu (the game's own text is the ROM's).",
           3, "English", "Español", "Italiano");
    toggle(V_SHOWLAUNCHER, "Show this menu at start", "Off: the game starts at once with the chosen ROM (this menu is still on Escape).");
    add(K_ACTION, A_DEFAULT_OPTIONS, T("Default options"), T("Put every option on this page back as it first was."));
    break;
  case P_CONTROLS:
    if (L.tab[P_CONTROLS] == 0) {
      for (int i = 0; i < NBUTTONS; i++) {
        Item *it = add(K_KEY, i, T(kButtons[i].label), T("Enter: press the new key (Escape keeps the old one, Delete leaves it empty)."));
        snprintf(it->value, sizeof it->value, "%s", key_name(c->keys[i]));
      }
      add(K_ACTION, A_DEFAULT_KEYS, T("Default keys"), T("X = A, Z = B, S = X, A = Y, C = L, V = R, Enter = Start, Right Shift = Select, arrows."));
      add(K_ACTION, A_HOTKEYS, T("Other keys"), T("Tab: fast forward · M: pause menu · Esc: this menu · F11 / Alt+Enter: full screen · F1-F10: load a state · Shift+F1-F10: save one."));
    } else {
      int n = host_gamepad_count();
      Item *it = add(K_CHOICE, V_PADSEL, T("Gamepad"), n ? T("The gamepad that plays. Left / right: another one, when more are connected.")
                                                          : T("No gamepad connected. Plug one in (USB) or pair it (Bluetooth): it appears here at once."));
      it->n = n;
      for (int i = 0; i < n && i < 8; i++) it->choices[i] = host_gamepad_name(i);
      if (!n) snprintf(it->value, sizeof it->value, "%s", T("(none)"));
      for (int i = 0; i < NBUTTONS; i++) {
        Item *b = add(K_PAD, i, T(kButtons[i].label), T("A / Enter: press the new button on the gamepad (Escape keeps the old one, Delete leaves it empty)."));
        snprintf(b->value, sizeof b->value, "%s", c->pad[i] == SDL_GAMEPAD_BUTTON_INVALID ? T("(none)") : host_gamepad_label(c->pad[i]));
        b->lit = c->pad[i] != SDL_GAMEPAD_BUTTON_INVALID && host_gamepad_down(c->pad[i]);
        b->dim = !n;
      }
      add(K_ACTION, A_DEFAULT_PAD, T("Default buttons"), T("The SNES layout: A on the right, B below, X above, Y on the left; shoulders L / R."));
      add(K_ACTION, A_PADHELP, T("Other buttons"), T("Left stick: moves too · R3: pause menu · L3 or Guide: this menu · right trigger: fast forward."));
    }
    break;
  case P_MODS:
    if (L.tab[P_MODS] == 0) {
      toggle(V_FASTTEXT, "Fast text", "Dialogue is written 4 times faster, as if you held A all the time.");
      choice(V_DAYLEN, "Longer days", "The game clock runs slower. Normally a day (6:00 to 18:00) lasts 3 real minutes.",
             5, T("Off"), T("1.5x (4.5 minutes)"), T("2x (6 minutes)"), T("3x (9 minutes)"), T("4x (12 minutes)"));
      toggle(V_PAUSE, "Pause menu (Harvest Moon 64 style)", "M key or right-stick click (R3): tools, the girls' hearts and your farm's status. The game is paused while it is open.");
      toggle(V_TITLEOPT, "OPTIONS on the title screen", "Adds a fourth entry, OPTIONS, below HOW TO PLAY, which opens this options menu.");
      toggle(V_ENDLESS, "Endless game", "The game no longer stops after Year 3: you watch the ending, then wake up on Summer 30 and keep farming through Year 4, 5 and beyond.");
      toggle(V_WIDESCREEN, "Widescreen (16:9)", "Shows more of the map at the sides instead of black bars. At a map's edge, in small rooms and on the title screen it looks like the original (4:3).");
    } else {
      toggle(V_MONEY, "Infinite money", "Keeps your money at the most the game allows: 9,999,990 G.");
      toggle(V_STAMINA, "Infinite stamina", "Your stamina is always topped up: you never get tired.");
      toggle(V_CLOCK, "Stop the clock", "Time does not pass during the day. Going to bed still moves on to the next day.");
      toggle(V_STOCK, "Infinite lumber and fodder", "Keeps the lumber and the fodder in the silo at 999 (the game's maximum).");
      toggle(V_BAGS, "Infinite seeds and feed", "Seed and feed bags you already have are refilled to 99.");
      choice(V_WEATHER, "Weather", "Choose the weather of ordinary days. Festivals are left alone.",
             4, T("As the game"), T("Always sunny"), T("Always rain (waters your crops; snow in winter)"), T("No hurricanes or storms"));
    }
    break;
  case P_STATES:
    for (int i = 1; i <= 10; i++) {
      char when[64];
      bool have = host_state_info(i, when, sizeof when);
      Item *it = add(K_ACTION, A_SLOT + i, NULL, NULL);
      snprintf(it->value, sizeof it->value, "%s", have ? when : T("(empty)"));
      it->dim = L.tab[P_STATES] == 1 && !have;
      it->desc = L.tab[P_STATES] == 0 ? T("Keep the game as it is now in this slot (what was in it is replaced).")
                                      : T("Go back to the moment kept in this slot.");
    }
    break;
  }
}

static const char *kSlotLabels[11] = { "", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10" };

/* ── Acting ──────────────────────────────────────────────────────────── */
static int act_result;
static void go(int page)
{
  L.page = page;
  if (page == P_GAMES) scan_games();
  if (L.sel[page] < 0) L.sel[page] = 0;
  if (page == P_GAMES) {          /* the chosen one under the cursor */
    for (int i = 0; i < L.ngames; i++) if (!strcmp(L.games[i].path, g_cfg.rom)) L.sel[page] = i;
  }
}
static void confirm(int what, int arg) { L.confirm = what; L.confirm_arg = arg; L.confirm_sel = 1; }

static void choose_game(int i)
{
  Game *g = &L.games[i];
  if (g->verdict < 1) { host_toast(T("This file is not Harvest Moon.")); return; }
  bool changed = strcmp(g->path, g_cfg.rom) != 0;
  if (L.in_game && changed && strcmp(g->path, host_rom())) { confirm(A_GAME, i); return; }
  snprintf(g_cfg.rom, sizeof g_cfg.rom, "%s", g->path);
  config_save();
  if (!L.in_game) { L.page = P_MAIN; L.sel[P_MAIN] = 0; }
}
static bool have_rom(void) { return g_cfg.rom[0] && launcher_rom_ok(g_cfg.rom, NULL, 0); }

static void activate(Item *it, int dir)
{
  switch (it->kind) {
  case K_TOGGLE: set_value(it->id, !get_value(it->id)); return;
  case K_CHOICE: {
    if (it->n <= 1 && it->id != V_PADSEL) return;
    if (it->n == 0) return;
    int v = get_value(it->id);
    if (v < 0) v = 0;
    set_value(it->id, (v + it->n + (dir ? dir : 1)) % it->n);
    return;
  }
  case K_SLIDER: {
    int v = get_value(it->id) + (dir ? dir : 1);
    if (!dir && v > it->n) v = 0;
    if (v < 0) v = 0;
    if (v > it->n) v = it->n;
    set_value(it->id, v);
    return;
  }
  case K_KEY: if (!dir) { L.wait = 1; L.wait_i = it->id; L.wait_frames = 0; } return;
  case K_PAD: if (!dir && host_gamepad_count()) { L.wait = 2; L.wait_i = it->id; L.wait_frames = 0; } return;
  }
  if (dir) return;
  int id = it->id;
  if (id >= A_SLOT) {
    L.slot = id - A_SLOT;
    if (L.tab[P_STATES] == 0) act_result = LA_SAVE_STATE;
    else if (host_state_info(L.slot, NULL, 0)) act_result = LA_LOAD_STATE;
    return;
  }
  if (id >= A_GAME) { choose_game(id - A_GAME); return; }
  switch (id) {
  case A_PLAY: {
    if (!L.in_game) {
      char why[160];
      if (!g_cfg.rom[0] || !launcher_rom_ok(g_cfg.rom, why, sizeof why)) {
        host_toast(T("First choose a Harvest Moon ROM."));
        go(P_GAMES);
        return;
      }
    }
    act_result = LA_PLAY;
    return;
  }
  case A_GAMES: go(P_GAMES); return;
  case A_OPTIONS: go(P_OPTIONS); return;
  case A_CONTROLS: go(P_CONTROLS); return;
  case A_MODS: go(P_MODS); return;
  case A_STATES: go(P_STATES); return;
  case A_RESET: confirm(A_RESET, 0); return;
  case A_QUIT: if (L.in_game) confirm(A_QUIT, 0); else act_result = LA_QUIT; return;
  case A_BROWSE: host_rom_dialog(); return;
  case A_DEFAULT_OPTIONS: {
    Config d; config_defaults(&d);
    Config *c = &g_cfg;
    c->fullscreen = d.fullscreen; c->scale = d.scale; c->exact_pixels = d.exact_pixels; c->vsync = d.vsync;
    c->widescreen = d.widescreen; c->sprite_limits = d.sprite_limits; c->volume = d.volume; c->ff_speed = d.ff_speed;
    c->show_launcher = d.show_launcher;
    host_apply_video(); host_apply_sound(); config_save();
    host_toast(T("Options back to their defaults."));
    return;
  }
  case A_DEFAULT_KEYS: config_default_keys(&g_cfg); config_save(); host_toast(T("Keys back to their defaults.")); return;
  case A_DEFAULT_PAD: config_default_pad(&g_cfg); config_save(); host_toast(T("Buttons back to their defaults.")); return;
  }
}

static void move_sel(int d)
{
  int n = L.nitems;
  if (!n) return;
  int *s = &L.sel[L.page];
  *s = (*s + n + d) % n;
}

void launcher_open(bool in_game)
{
  L.in_game = in_game;
  L.page = P_MAIN;
  L.sel[P_MAIN] = 0;
  L.wait = 0; L.confirm = 0;
  if (!in_game && (!g_cfg.rom[0] || !launcher_rom_ok(g_cfg.rom, NULL, 0))) {
    /* no ROM yet: one of the ones found, if any */
    scan_games();
    for (int i = 0; i < L.ngames; i++)
      if (L.games[i].verdict >= 1) { snprintf(g_cfg.rom, sizeof g_cfg.rom, "%s", L.games[i].path); config_save(); break; }
    /* none at all: the first time, choose it */
    if (!have_rom()) go(P_GAMES);
  }
}
int launcher_slot(void) { return L.slot; }

static void capture(const UiInput *in)
{
  L.wait_frames++;
  Config *c = &g_cfg;
  if (L.wait == 1) {
    if (in->key == SDL_SCANCODE_UNKNOWN) return;
    if (in->key == SDL_SCANCODE_ESCAPE) { L.wait = 0; return; }
    SDL_Scancode k = (in->key == SDL_SCANCODE_DELETE || in->key == SDL_SCANCODE_BACKSPACE) ? SDL_SCANCODE_UNKNOWN : in->key;
    for (int i = 0; i < NBUTTONS; i++)           /* a key on two buttons: they swap */
      if (i != L.wait_i && k != SDL_SCANCODE_UNKNOWN && c->keys[i] == k) c->keys[i] = c->keys[L.wait_i];
    c->keys[L.wait_i] = k;
    L.wait = 0;
    config_save();
  } else {
    if (in->key == SDL_SCANCODE_ESCAPE || L.wait_frames > 60 * 8) { L.wait = 0; return; }
    if (in->key == SDL_SCANCODE_DELETE || in->key == SDL_SCANCODE_BACKSPACE) {
      c->pad[L.wait_i] = SDL_GAMEPAD_BUTTON_INVALID; L.wait = 0; config_save(); return;
    }
    if (in->pad_button < 0) return;
    SDL_GamepadButton b = (SDL_GamepadButton)in->pad_button;
    for (int i = 0; i < NBUTTONS; i++)
      if (i != L.wait_i && c->pad[i] == b) c->pad[i] = c->pad[L.wait_i];
    c->pad[L.wait_i] = b;
    L.wait = 0;
    config_save();
  }
}

static int page_tabs(int page) { return page == P_CONTROLS || page == P_MODS || page == P_STATES ? 2 : 0; }

int launcher_update(const UiInput *in)
{
  L.frame++;
  act_result = LA_NONE;
  char picked[1024];
  if (host_rom_dialog_result(picked, sizeof picked)) {
    config_add_game(picked);
    config_save();
    scan_games();
    for (int i = 0; i < L.ngames; i++) if (!strcmp(L.games[i].path, picked)) { L.sel[P_GAMES] = i; choose_game(i); }
    char why[160];
    if (!launcher_rom_ok(picked, why, sizeof why)) host_toast(why);
  }
  if (L.wait) { capture(in); return LA_NONE; }
  if (L.confirm) {
    int yes = -1;
    if (in->pressed & (B_LEFT | B_RIGHT | B_UP | B_DOWN)) L.confirm_sel ^= 1;
    for (int k = 0; k < 2; k++)
      if (in->mouse_x >= L.chits[k].x && in->mouse_x < L.chits[k].x + L.chits[k].w &&
          in->mouse_y >= L.chits[k].y && in->mouse_y < L.chits[k].y + L.chits[k].h) {
        if (in->mouse_moved) L.confirm_sel = L.chits[k].v;
        if (in->click) yes = L.chits[k].v == 0;
      }
    if (in->accept) yes = L.confirm_sel == 0;
    if (in->back || in->rclick) yes = 0;
    if (yes < 0) return LA_NONE;
    int what = L.confirm;
    L.confirm = 0;
    if (!yes) return LA_NONE;
    if (what == A_RESET) return LA_RESET;
    if (what == A_QUIT) return LA_QUIT;
    if (what == A_GAME) {
      snprintf(g_cfg.rom, sizeof g_cfg.rom, "%s", L.games[L.confirm_arg].path);
      config_save();
      return LA_SWITCH_ROM;
    }
    return LA_NONE;
  }
  build();
  int *sel = &L.sel[L.page];
  if (*sel >= L.nitems) *sel = L.nitems ? L.nitems - 1 : 0;
  /* the mouse */
  for (int h = 0; h < L.nhits; h++)
    if (in->mouse_x >= L.hits[h].x && in->mouse_x < L.hits[h].x + L.hits[h].w &&
        in->mouse_y >= L.hits[h].y && in->mouse_y < L.hits[h].y + L.hits[h].h) {
      if (in->mouse_moved || in->click) *sel = L.hits[h].i;
      if (in->click && L.hits[h].i < L.nitems) activate(&L.items[L.hits[h].i], 0);
    }
  if (in->wheel) move_sel(in->wheel > 0 ? -1 : 1);
  if (in->pressed & B_UP) move_sel(-1);
  if (in->pressed & B_DOWN) move_sel(1);
  int tabs = page_tabs(L.page);
  if (tabs && (in->pressed & (B_L | B_R))) {
    L.tab[L.page] = (L.tab[L.page] + tabs + ((in->pressed & B_R) ? 1 : -1)) % tabs;
    L.sel[L.page] = 0; L.scroll[L.page] = 0;
    build();
  }
  if (L.nitems && *sel < L.nitems) {
    Item *it = &L.items[*sel];
    if (in->pressed & B_LEFT) {
      if (it->kind == K_TOGGLE || it->kind == K_CHOICE || it->kind == K_SLIDER) activate(it, -1);
      else if (tabs) { L.tab[L.page] = (L.tab[L.page] + tabs - 1) % tabs; L.sel[L.page] = 0; }
    }
    if (in->pressed & B_RIGHT) {
      if (it->kind == K_TOGGLE || it->kind == K_CHOICE || it->kind == K_SLIDER) activate(it, 1);
      else if (tabs) { L.tab[L.page] = (L.tab[L.page] + 1) % tabs; L.sel[L.page] = 0; }
    }
    if (in->accept && !it->dim) activate(it, 0);
  }
  if (act_result) return act_result;
  if (in->back || in->rclick) {
    if (L.page == P_GAMES && !have_rom()) { /* there is nothing to go back to yet */ }
    else if (L.page != P_MAIN) { L.page = P_MAIN; }
    else if (L.in_game) return LA_PLAY;
  }
  return LA_NONE;
}

/* ── Drawing ─────────────────────────────────────────────────────────── */
static int s_ox;   /* where the 256 columns start */

static void hit(int x, int y, int w, int h, int i)
{
  if (L.nhits < (int)(sizeof L.hits / sizeof L.hits[0])) L.hits[L.nhits++] = (struct Hit){ x, y, w, h, i };
}

static void textbox(const char *desc, const char *hint)
{
  int x = s_ox + 4;
  ui_frame(x, 176, 248, 44, C_PAPER);
  if (desc) ui_text_wrap(UI_SMALL, x + 9, 188, 230, 9, 3, desc, C_INK);
  if (hint) ui_text(UI_SMALL, x + 9, 215, hint, C_INK_SOFT);
  for (int j = 0; j < 3; j++) ui_hline(x + 237 + j, 211 + j, 5 - 2 * j, C_WOOD_DARK);
}

static void draw_main(void)
{
  int cx = s_ox + 128;
  const char *title = "Harvest Moon";
  ui_text_big(cx - ui_text_big_w(title) / 2, 34, title, C_CREAM, C_WOOD_EDGE);
  ui_text_sh(UI_SMALL, 3, 9, "v" HM_NATIVE_VERSION, C_CREAM);
  const char *sub = T("NATIVE EDITION · DECOMPILED C");
  ui_text_sh(UI_SMALL, cx - ui_text_w(UI_SMALL, sub) / 2, 47, sub, C_AMBER);
  int bw = 0;
  for (int i = 0; i < L.nitems; i++) { int w = ui_text_w(UI_BODY, L.items[i].label); if (w > bw) bw = w; }
  bw += 34;
  int rows = L.nitems, rh = 13;
  int by = 54, bh = 12 + rows * rh;
  int bx = cx - bw / 2;
  ui_frame(bx, by, bw, bh, C_PAPER);
  int sel = L.sel[P_MAIN];
  for (int i = 0; i < rows; i++) {
    int ry = by + 6 + i * rh;
    if (i == sel) ui_rect(bx + 5, ry, bw - 10, rh, C_SELECT);
    ui_text(UI_BODY, bx + 20, ry + 10, L.items[i].label, i == sel ? C_INK : C_INK_SOFT);
    if (i == sel) ui_hand(bx + 7 + (int)((L.frame / 10) & 1), ry + 3);
    hit(bx, ry, bw, rh, i);
  }
  /* the ROM in use, in the text box */
  char line[200];
  const char *rom = L.in_game ? host_rom() : g_cfg.rom;
  const char *b = strrchr(rom, '/'), *b2 = strrchr(rom, '\\');
  if (b2 > b) b = b2;
  snprintf(line, sizeof line, "%s %s", T("ROM:"), *rom ? (b ? b + 1 : rom) : T("(none chosen)"));
  const char *desc = L.nitems ? L.items[sel].desc : "";
  textbox(desc, line);
}

static void draw_header(const char *title, int tabs, const char *const *names)
{
  int x = s_ox + 4;
  ui_frame(x, 2, 248, 24, C_WOOD);
  ui_text_sh(UI_BODY, x + 124 - ui_text_w(UI_BODY, title) / 2, 18, title, C_CREAM);
  if (!tabs) return;
  int tx = x + 4;
  for (int t = 0; t < tabs; t++) {
    const char *name = names[t];
    int w = ui_text_w(UI_SMALL, name) + 12;
    bool on = t == L.tab[L.page];
    int y = on ? 26 : 28;
    ui_rect(tx + 1, y, w - 2, 38 - y, C_WOOD_EDGE);
    ui_rect(tx, y + 1, w, 37 - y, C_WOOD_EDGE);
    ui_rect(tx + 1, y + 1, w - 2, 37 - y, on ? C_PAPER : C_WOOD);
    ui_hline(tx + 2, y + 1, w - 4, on ? C_CREAM : C_WOOD_LIGHT);
    if (on) ui_text(UI_SMALL, tx + 6, y + 9, name, C_INK);
    else ui_text_sh(UI_SMALL, tx + 6, y + 9, name, C_CREAM);
    tx += w + 2;
  }
  const char *h = T("L / R  tabs");
  ui_text_sh(UI_SMALL, x + 244 - ui_text_w(UI_SMALL, h), 35, h, C_CREAM);
}

static void draw_value(const Item *it, int rx, int ry, bool sel)
{
  uint32_t ink = it->dim ? C_INK_SOFT : C_INK;
  switch (it->kind) {
  case K_TOGGLE: ui_switch(rx - 20, ry + 3, get_value(it->id)); return;
  case K_SLIDER: {
    int v = get_value(it->id);
    char t[16]; snprintf(t, sizeof t, "%d%%", v * 10);
    ui_text_r(UI_SMALL, rx, ry + 10, t, ink);
    ui_bar(rx - 74, ry + 5, 52, v, it->n);
    return;
  }
  case K_CHOICE: {
    int v = get_value(it->id);
    const char *t = it->n && v >= 0 && v < it->n ? it->choices[v] : it->value;
    int f = ui_text_w(UI_BODY, t) > 104 ? UI_SMALL : UI_BODY;
    if (f == UI_SMALL && ui_text_w(UI_SMALL, t) > 112) {
      /* too long even small: the start, with an ellipsis */
      static char cut[64];
      const char *p = t; int w = 0; size_t n = 0;
      while (*p) { const char *q = p; ui_utf8_next(&q); char tmp[8] = { 0 }; memcpy(tmp, p, (size_t)(q - p));
                   int a = ui_text_w(UI_SMALL, tmp); if (w + a > 104) break; w += a; n = (size_t)(q - t); p = q; }
      if (n > sizeof cut - 4) n = sizeof cut - 4;
      memcpy(cut, t, n); strcpy(cut + n, "...");
      t = cut;
    }
    int w = ui_text_w(f, t);
    ui_text(f, rx - 6 - w, ry + 10, t, ink);
    if (sel && it->n > 1) {
      /* the arrows either side */
      int ax = rx - 12 - w;
      for (int j = 0; j < 3; j++) ui_vline(ax - j, ry + 5 + j, 5 - 2 * j, C_WOOD_DARK);
      for (int j = 0; j < 3; j++) ui_vline(rx - 4 + j, ry + 5 + j, 5 - 2 * j, C_WOOD_DARK);
    }
    return;
  }
  default: {
    if (!it->value[0]) return;
    int f = ui_text_w(UI_BODY, it->value) > 110 ? UI_SMALL : UI_BODY;
    if (it->lit) ui_rect(rx - ui_text_w(f, it->value) - 3, ry + 1, ui_text_w(f, it->value) + 5, 12, C_GOLD);
    ui_text_r(f, rx, ry + 10, it->value, it->kind == K_ACTION && L.page == P_GAMES ? C_GREEN_DARK : ink);
  }
  }
}

static void draw_list(int top)
{
  int x = s_ox + 4, w = 248, h = 174 - top;
  ui_frame(x, top, w, h, C_PAPER);
  int rh = 14, vis = (h - 10) / rh;
  int *sel = &L.sel[L.page], *scroll = &L.scroll[L.page];
  if (*sel < *scroll) *scroll = *sel;
  if (*sel >= *scroll + vis) *scroll = *sel - vis + 1;
  if (*scroll > L.nitems - vis) *scroll = L.nitems - vis;
  if (*scroll < 0) *scroll = 0;
  for (int k = 0; k < vis && *scroll + k < L.nitems; k++) {
    int i = *scroll + k;
    Item *it = &L.items[i];
    int ry = top + 5 + k * rh;
    bool s = i == *sel;
    if (s) ui_rect(x + 5, ry, w - 10, rh, C_SELECT);
    if (s) ui_hand(x + 7 + (int)((L.frame / 10) & 1), ry + 4);
    int lx = x + 19;
    if (it->mark) {
      ui_rect(lx, ry + 4, 6, 6, C_WOOD_EDGE);
      ui_rect(lx + 1, ry + 5, 4, 4, it->mark);
      lx += 9;
    }
    const char *label = it->label;
    char buf[64];
    if (!label && L.page == P_STATES) { snprintf(buf, sizeof buf, "%s %s", T("Slot"), kSlotLabels[i + 1]); label = buf; }
    int lf = UI_BODY;
    int room = 236 - (lx - x) - (it->kind == K_TOGGLE ? 26 : it->kind == K_ACTION && !it->value[0] ? 0 : 70);
    if (ui_text_w(UI_BODY, label) > room) lf = UI_SMALL;
    ui_text(lf, lx, ry + 10 + (lf == UI_SMALL ? -1 : 0), label, it->dim ? C_INK_SOFT : C_INK);
    draw_value(it, x + 238, ry, s);
    hit(x, ry, w, rh, i);
  }
  /* more above / below */
  if (*scroll > 0) for (int j = 0; j < 3; j++) ui_hline(x + 122 - j, top + 3 + j, 1 + 2 * j, C_WOOD_DARK);
  if (*scroll + vis < L.nitems) for (int j = 0; j < 3; j++) ui_hline(x + 122 - j, top + h - 4 - j, 1 + 2 * j, C_WOOD_DARK);
}

static void draw_wait(void)
{
  int x = s_ox + 24, y = 78, w = 208, h = 52;
  ui_dim(0, 0, g_ui_w, 176, 1);
  ui_frame(x, y, w, h, C_PAPER);
  char t[128];
  snprintf(t, sizeof t, L.wait == 1 ? T("Press a key for %s") : T("Press a gamepad button for %s"), T(kButtons[L.wait_i].label));
  ui_text_c(UI_BODY, x + w / 2, y + 22, t, C_INK);
  ui_text_c(UI_SMALL, x + w / 2, y + 36, T("Escape: keep it as it was  ·  Delete: none"), C_INK_SOFT);
  if (L.wait == 2) { char s[32]; snprintf(s, sizeof s, "%u", (unsigned)(8 - L.wait_frames / 60)); ui_text_c(UI_SMALL, x + w / 2, y + 46, s, C_INK_SOFT); }
}

static void draw_confirm(void)
{
  int x = s_ox + 28, y = 70, w = 200, h = 66;
  ui_dim(0, 0, g_ui_w, g_ui_h, 1);
  ui_frame(x, y, w, h, C_PAPER);
  const char *q = L.confirm == A_RESET ? T("Restart the game?")
                : L.confirm == A_QUIT ? T("Quit the game?")
                : T("Play this other ROM? The game starts again.");
  const char *note = L.confirm == A_GAME ? "" : T("What was not saved in a diary is lost.");
  ui_text_wrap(UI_BODY, x + 10, y + 18, w - 20, 13, 2, q, C_INK);
  ui_text(UI_SMALL, x + 10, y + 42, note, C_INK_SOFT);
  const char *opt[2] = { T("Yes"), T("No") };
  for (int k = 0; k < 2; k++) {
    int ox = x + 50 + k * 70, oy = y + 46;
    bool s = L.confirm_sel == k;
    if (s) { ui_rect(ox - 4, oy, 50, 14, C_SELECT); ui_hand(ox - 2 + (int)((L.frame / 10) & 1), oy + 4); }
    ui_text(UI_BODY, ox + 10, oy + 11, opt[k], C_INK);
    L.chits[k].x = ox - 4; L.chits[k].y = oy; L.chits[k].w = 50; L.chits[k].h = 14; L.chits[k].v = k;
  }
}

void launcher_draw(uint32_t *dst, int w, int h, const uint32_t *game, uint32_t frame)
{
  ui_target(dst, w, h);
  s_ox = (w - 256) / 2;
  if (game) {
    memcpy(dst, game, (size_t)w * h * 4);
    ui_dim(0, 0, w, h, 1);
  } else {
    ui_scene(frame);
  }
  L.nhits = 0;
  build();
  if (L.sel[L.page] >= L.nitems) L.sel[L.page] = L.nitems ? L.nitems - 1 : 0;
  const char *hint = T("A / Enter: choose  ·  B / Esc: back  ·  < >: change");
  switch (L.page) {
  case P_MAIN: draw_main(); break;
  case P_GAMES: {
    draw_header(T("Choose your ROM"), 0, NULL);
    draw_list(28);
    const Item *it = L.nitems ? &L.items[L.sel[P_GAMES]] : NULL;
    if (it && it->id >= A_GAME) {
      Game *g = &L.games[it->id - A_GAME];
      char d[1400];
      snprintf(d, sizeof d, "%s\n\"%s\"  ·  %u KB  ·  CRC %08X\n%s", verdict_text(g->verdict), g->title,
               (unsigned)(g->size / 1024), (unsigned)g->crc, g->path);
      textbox(d, hint);
    } else textbox(it && it->id == A_QUIT ? it->desc
                   : T("First time: choose your own copy of Harvest Moon (USA). It is checked and remembered; next time the game opens with it."), hint);
    break;
  }
  case P_OPTIONS: draw_header(T("Options"), 0, NULL); draw_list(28); break;
  case P_CONTROLS: {
    const char *tabs[2] = { T("Keyboard"), T("Gamepad") };
    draw_header(T("Controls"), 2, tabs);
    draw_list(38);
    break;
  }
  case P_MODS: {
    const char *tabs[2] = { T("Quality of life"), T("Cheats") };
    draw_header(T("Improvements"), 2, tabs);
    draw_list(38);
    break;
  }
  case P_STATES: {
    const char *tabs[2] = { T("Save"), T("Load") };
    draw_header(T("Save states"), 2, tabs);
    draw_list(38);
    break;
  }
  }
  if (L.page != P_MAIN && L.page != P_GAMES) {
    const Item *it = L.nitems ? &L.items[L.sel[L.page]] : NULL;
    textbox(it ? it->desc : "", hint);
  }
  if (L.wait) draw_wait();
  if (L.confirm) draw_confirm();
}
