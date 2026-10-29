/* config.h — the native build's settings (config.ini next to the program)
 * and the keys (keybinds.ini, the recompilation's own file and format). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include <SDL3/SDL.h>

enum { NBUTTONS = 12, MAX_GAMES = 16 };
/* the pads' bits as $4218/$4219 read them */
enum { B_B = 0x8000, B_Y = 0x4000, B_SELECT = 0x2000, B_START = 0x1000, B_UP = 0x0800,
       B_DOWN = 0x0400, B_LEFT = 0x0200, B_RIGHT = 0x0100, B_A = 0x0080, B_X = 0x0040,
       B_L = 0x0020, B_R = 0x0010 };
/* the buttons in the order the controls page lists them */
typedef struct { const char *ini, *label; uint16_t mask; } ButtonInfo;
extern const ButtonInfo kButtons[NBUTTONS];

enum { WEATHER_STOCK, WEATHER_SUNNY, WEATHER_RAIN, WEATHER_NO_STORMS };

typedef struct {
  /* games */
  char rom[1024];                    /* the one to play */
  char games[MAX_GAMES][1024];       /* more ROM files picked by hand */
  int ngames;
  /* video and sound */
  bool fullscreen, vsync, sprite_limits, widescreen, exact_pixels;
  int scale;                         /* window: 1..6 times the picture */
  int volume;                        /* 0..100 */
  int ff_speed;                      /* fast forward: 2, 3, 4, 6 */
  char language[8];                  /* "es", "en", "it" */
  bool show_launcher;                /* the launcher first, at start */
  /* improvements (the recompilation's quality-of-life package) */
  bool fast_text, pause_menu, title_options, endless;
  int day_length;                    /* 0 off; 1 1.5x, 2 2x, 3 3x, 4 4x */
  /* cheats */
  bool money, stamina, freeze_clock, stock, bags;
  int weather;                       /* WEATHER_* */
  /* controls */
  SDL_Scancode keys[NBUTTONS];
  SDL_GamepadButton pad[NBUTTONS];
} Config;

extern Config g_cfg;
void config_defaults(Config *c);
void config_default_keys(Config *c);
void config_default_pad(Config *c);
void config_load(void);
void config_save(void);
const char *config_dir(void);        /* the program's folder ("" when unknown) */
void config_add_game(const char *path);
