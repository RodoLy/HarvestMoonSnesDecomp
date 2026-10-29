/* config.c — config.ini and keybinds.ini (see config.h).
 *
 * Both are plain text next to the program and can be edited by hand; the
 * launcher writes them back when something changes. keybinds.ini is the
 * recompilation's file, in its format ([player1], a = X ...), so the keys
 * a player set there carry over. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

Config g_cfg;

const ButtonInfo kButtons[NBUTTONS] = {
  { "a", "A", B_A }, { "b", "B", B_B }, { "x", "X", B_X }, { "y", "Y", B_Y },
  { "l", "L", B_L }, { "r", "R", B_R }, { "start", "Start", B_START }, { "select", "Select", B_SELECT },
  { "up", "Up", B_UP }, { "down", "Down", B_DOWN }, { "left", "Left", B_LEFT }, { "right", "Right", B_RIGHT },
};

const char *config_dir(void)
{
  const char *b = SDL_GetBasePath();
  return b ? b : "";
}

void config_default_keys(Config *c)
{
  static const SDL_Scancode k[NBUTTONS] = {
    SDL_SCANCODE_X, SDL_SCANCODE_Z, SDL_SCANCODE_S, SDL_SCANCODE_A, SDL_SCANCODE_C, SDL_SCANCODE_V,
    SDL_SCANCODE_RETURN, SDL_SCANCODE_RSHIFT, SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT,
    SDL_SCANCODE_RIGHT };
  memcpy(c->keys, k, sizeof k);
}
void config_default_pad(Config *c)
{
  /* the SNES layout: A on the right, B below, X above, Y on the left */
  static const SDL_GamepadButton p[NBUTTONS] = {
    SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_WEST,
    SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_START,
    SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN,
    SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT };
  memcpy(c->pad, p, sizeof p);
}
static void default_language(Config *c)
{
  snprintf(c->language, sizeof c->language, "es");
  int n = 0;
  SDL_Locale **loc = SDL_GetPreferredLocales(&n);
  if (loc) {
    for (int i = 0; i < n; i++) {
      const char *l = loc[i]->language;
      if (!l) continue;
      if (!strcmp(l, "es") || !strcmp(l, "en") || !strcmp(l, "it")) { snprintf(c->language, sizeof c->language, "%s", l); break; }
    }
    SDL_free(loc);
  }
}
void config_defaults(Config *c)
{
  memset(c, 0, sizeof *c);
  c->vsync = true; c->scale = 3; c->volume = 100; c->ff_speed = 3; c->show_launcher = true;
  c->widescreen = true;
  c->pause_menu = true; c->title_options = true; c->endless = true;
  c->day_length = 0;
  config_default_keys(c);
  config_default_pad(c);
  default_language(c);
}

static void trim(char *t)
{
  size_t n = strlen(t);
  while (n && (t[n - 1] == ' ' || t[n - 1] == '\t' || t[n - 1] == '\r' || t[n - 1] == '\n')) t[--n] = 0;
  size_t i = 0;
  while (t[i] == ' ' || t[i] == '\t') i++;
  if (i) memmove(t, t + i, strlen(t + i) + 1);
}
static void lower(char *t) { for (; *t; t++) if (*t >= 'A' && *t <= 'Z') *t = (char)(*t + 32); }
static bool yes(const char *v) { return !SDL_strcasecmp(v, "1") || !SDL_strcasecmp(v, "true") || !SDL_strcasecmp(v, "yes") || !SDL_strcasecmp(v, "on"); }

static SDL_Scancode key_from_name(const char *val)
{
  if (!SDL_strcasecmp(val, "none") || !*val) return SDL_SCANCODE_UNKNOWN;
  SDL_Scancode sc = SDL_GetScancodeFromName(val);
  if (sc != SDL_SCANCODE_UNKNOWN) return sc;
  if (!SDL_strcasecmp(val, "enter") || !SDL_strcasecmp(val, "return")) return SDL_SCANCODE_RETURN;
  if (!SDL_strcasecmp(val, "rshift")) return SDL_SCANCODE_RSHIFT;
  if (!SDL_strcasecmp(val, "lshift")) return SDL_SCANCODE_LSHIFT;
  if (!SDL_strcasecmp(val, "space")) return SDL_SCANCODE_SPACE;
  return SDL_SCANCODE_UNKNOWN;
}

static void path_of(char *out, size_t n, const char *name) { snprintf(out, n, "%s%s", config_dir(), name); }

static void keys_load(void)
{
  char path[1100]; path_of(path, sizeof path, "keybinds.ini");
  FILE *f = fopen(path, "r");
  if (!f) return;
  char line[512]; bool p1 = false;
  while (fgets(line, sizeof line, f)) {
    trim(line);
    if (!line[0] || line[0] == '#' || line[0] == ';') continue;
    if (line[0] == '[') { p1 = !strncmp(line, "[player1]", 9); continue; }
    char *eq = strchr(line, '=');
    if (!eq || !p1) continue;
    *eq = 0;
    char *key = line, *val = eq + 1;
    trim(key); trim(val); lower(key);
    for (int i = 0; i < NBUTTONS; i++) if (!strcmp(key, kButtons[i].ini)) g_cfg.keys[i] = key_from_name(val);
  }
  fclose(f);
}
static void keys_save(void)
{
  char path[1100]; path_of(path, sizeof path, "keybinds.ini");
  FILE *f = fopen(path, "w");
  if (!f) return;
  fprintf(f, "# SNES Controller Keybinds\n# Edit values to customize (or use the launcher: Esc, Controls).\n"
             "# Use SDL key names (A ... Z, Up, Return, Right Shift...); \"None\" leaves a button unbound.\n\n[player1]\n");
  for (int i = 0; i < NBUTTONS; i++)
    fprintf(f, "%s = %s\n", kButtons[i].ini, g_cfg.keys[i] == SDL_SCANCODE_UNKNOWN ? "None" : SDL_GetScancodeName(g_cfg.keys[i]));
  fclose(f);
}

void config_add_game(const char *path)
{
  for (int i = 0; i < g_cfg.ngames; i++) if (!strcmp(g_cfg.games[i], path)) return;
  if (g_cfg.ngames == MAX_GAMES) { memmove(g_cfg.games[0], g_cfg.games[1], sizeof g_cfg.games[0] * (MAX_GAMES - 1)); g_cfg.ngames--; }
  snprintf(g_cfg.games[g_cfg.ngames++], sizeof g_cfg.games[0], "%s", path);
}

void config_load(void)
{
  config_defaults(&g_cfg);
  char path[1100]; path_of(path, sizeof path, "config.ini");
  FILE *f = fopen(path, "r");
  bool have_language = false;
  if (f) {
    char line[1400], section[32] = "";
    while (fgets(line, sizeof line, f)) {
      trim(line);
      if (!line[0] || line[0] == '#' || line[0] == ';') continue;
      if (line[0] == '[') { snprintf(section, sizeof section, "%s", line); lower(section); continue; }
      char *eq = strchr(line, '=');
      if (!eq) continue;
      *eq = 0;
      char *k = line, *v = eq + 1;
      trim(k); trim(v); lower(k);
      Config *c = &g_cfg;
      if (!strcmp(section, "[games]")) {
        if (!strcmp(k, "rom")) snprintf(c->rom, sizeof c->rom, "%s", v);
        else if (!strncmp(k, "game", 4) && *v) config_add_game(v);
      } else if (!strcmp(section, "[video]")) {
        if (!strcmp(k, "fullscreen")) c->fullscreen = yes(v);
        else if (!strcmp(k, "scale")) { c->scale = atoi(v); if (c->scale < 1 || c->scale > 6) c->scale = 3; }
        else if (!strcmp(k, "vsync")) c->vsync = yes(v);
        else if (!strcmp(k, "spritelimits")) c->sprite_limits = yes(v);
        else if (!strcmp(k, "widescreen")) c->widescreen = yes(v);
        else if (!strcmp(k, "exactpixels")) c->exact_pixels = yes(v);
      } else if (!strcmp(section, "[sound]")) {
        if (!strcmp(k, "volume")) { c->volume = atoi(v); if (c->volume < 0) c->volume = 0; if (c->volume > 100) c->volume = 100; }
      } else if (!strcmp(section, "[general]")) {
        if (!strcmp(k, "language") && *v) { snprintf(c->language, sizeof c->language, "%s", v); have_language = true; }
        else if (!strcmp(k, "showlauncher")) c->show_launcher = yes(v);
        else if (!strcmp(k, "fastforward")) { c->ff_speed = atoi(v); if (c->ff_speed < 2 || c->ff_speed > 8) c->ff_speed = 3; }
      } else if (!strcmp(section, "[improvements]")) {
        if (!strcmp(k, "fasttext")) c->fast_text = yes(v);
        else if (!strcmp(k, "daylength")) { c->day_length = atoi(v); if (c->day_length < 0 || c->day_length > 4) c->day_length = 0; }
        else if (!strcmp(k, "pausemenu")) c->pause_menu = yes(v);
        else if (!strcmp(k, "titleoptions")) c->title_options = yes(v);
        else if (!strcmp(k, "endless")) c->endless = yes(v);
      } else if (!strcmp(section, "[cheats]")) {
        if (!strcmp(k, "money")) c->money = yes(v);
        else if (!strcmp(k, "stamina")) c->stamina = yes(v);
        else if (!strcmp(k, "freezeclock")) c->freeze_clock = yes(v);
        else if (!strcmp(k, "stock")) c->stock = yes(v);
        else if (!strcmp(k, "bags")) c->bags = yes(v);
        else if (!strcmp(k, "weather")) { c->weather = atoi(v); if (c->weather < 0 || c->weather > 3) c->weather = 0; }
      } else if (!strcmp(section, "[gamepad]")) {
        for (int i = 0; i < NBUTTONS; i++)
          if (!strcmp(k, kButtons[i].ini)) {
            SDL_GamepadButton b = SDL_GetGamepadButtonFromString(v);
            c->pad[i] = (!SDL_strcasecmp(v, "none")) ? SDL_GAMEPAD_BUTTON_INVALID : b;
          }
      }
    }
    fclose(f);
  }
  if (!have_language) default_language(&g_cfg);
  keys_load();
}

void config_save(void)
{
  char path[1100]; path_of(path, sizeof path, "config.ini");
  FILE *f = fopen(path, "w");
  if (f) {
    const Config *c = &g_cfg;
    fprintf(f, "# Harvest Moon (native build). The launcher (Esc) writes this file;\n"
               "# it can be edited by hand too.\n\n");
    fprintf(f, "[General]\nLanguage = %s\nShowLauncher = %d\nFastForward = %d\n\n", c->language, c->show_launcher, c->ff_speed);
    fprintf(f, "[Games]\nRom = %s\n", c->rom);
    for (int i = 0; i < c->ngames; i++) fprintf(f, "Game%d = %s\n", i + 1, c->games[i]);
    fprintf(f, "\n[Video]\nFullscreen = %d\nScale = %d\nVSync = %d\nSpriteLimits = %d\nWidescreen = %d\nExactPixels = %d\n\n",
            c->fullscreen, c->scale, c->vsync, c->sprite_limits, c->widescreen, c->exact_pixels);
    fprintf(f, "[Sound]\nVolume = %d\n\n", c->volume);
    fprintf(f, "[Improvements]\nFastText = %d\nDayLength = %d\nPauseMenu = %d\nTitleOptions = %d\nEndless = %d\n\n",
            c->fast_text, c->day_length, c->pause_menu, c->title_options, c->endless);
    fprintf(f, "[Cheats]\nMoney = %d\nStamina = %d\nFreezeClock = %d\nStock = %d\nBags = %d\nWeather = %d\n\n",
            c->money, c->stamina, c->freeze_clock, c->stock, c->bags, c->weather);
    fprintf(f, "[Gamepad]\n# SDL gamepad button names: a (bottom), b (right), x (left), y (top),\n"
               "# back, start, leftshoulder, rightshoulder, dpup, dpdown, dpleft, dpright...\n");
    for (int i = 0; i < NBUTTONS; i++) {
      const char *n = c->pad[i] == SDL_GAMEPAD_BUTTON_INVALID ? NULL : SDL_GetGamepadStringForButton(c->pad[i]);
      fprintf(f, "%s = %s\n", kButtons[i].ini, n ? n : "none");
    }
    fclose(f);
  }
  keys_save();
}
