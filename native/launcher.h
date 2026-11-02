/* launcher.h — the native build's launcher: games, options, controls,
 * gamepads and the improvements, in the look of the game (launcher.c). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "host.h"

enum LauncherAction {
  LA_NONE,
  LA_PLAY,         /* close: start the game, or go back to it */
  LA_QUIT,
  LA_RESET,        /* start the game again */
  LA_SWITCH_ROM,   /* g_cfg.rom changed while a game was going: start it */
  LA_SAVE_STATE,   /* launcher_slot() */
  LA_LOAD_STATE,
};

void launcher_open(bool in_game);
int launcher_update(const UiInput *in);          /* an LA_* */
int launcher_slot(void);                         /* 1..10, for LA_SAVE_STATE / LA_LOAD_STATE */
/* the launcher into dst (w x 224); behind it the game's last picture
 * (in game; NULL: the night farm) */
void launcher_draw(uint32_t *dst, int w, int h, const uint32_t *game, uint32_t frame);
/* is `path` a ROM this program plays? A one-line verdict in out */
bool launcher_rom_ok(const char *path, char *out, size_t n);
