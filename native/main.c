/*
 * main.c — Harvest Moon, played on the decompiled C.
 *
 * The game's code is the C of src/, called directly: RESET, then the
 * routine the last one went on to (g_hm_next: the title screen and its
 * frames, the demos, the names screen, the game loop...). Nothing here
 * emulates the CPU. Each NMI (each wait for the next frame, hm_nmi) ends
 * a frame: it is drawn, the pads are read, the sound chip catches up with
 * the frame's time, and the window waits for the next 1/60 s.
 *
 *   HarvestMoonNative [rom.sfc] [--scale N] [--fullscreen] [--no-vsync] [--sprite-limits]
 *                     [--no-launcher] [--script file --headless --dump-dir dir [--mods]]
 *
 * Around the game: a launcher (launcher.c: games, options, controls, the
 * improvements), the recompilation's improvements (mods.c), its pause menu
 * (pause.c), save states, and fast forward. The saved games go next to the
 * ROM (<rom>.srm, the cartridge's battery RAM); the save states too
 * (<rom>.st1 ... .st10).
 *
 * Keys (keybinds.ini, as the recompilation's): arrows; X = A, Z = B,
 * S = X, A = Y, C = L, V = R, Enter = Start, Right Shift = Select;
 * Escape the launcher, M the pause menu, Tab held fast forward, F11 or
 * Alt+Enter full screen, F1-F10 load a state, Shift+F1-F10 save one.
 * A gamepad works too (L3 or Guide the launcher, R3 the pause menu, the
 * right trigger fast forward).
 */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "hm_decomp.h"
#include "native.h"
#include "config.h"
#include "host.h"
#include "i18n.h"
#include "launcher.h"
#include "mods.h"
#include "pause.h"
#include "ui.h"

enum { W = 256, W_MAX = 256 + 2 * WS_BUDGET, H = 224, SRAM_SIZE = 0x2000 };

static uint8_t s_pixels[W_MAX * H * 4];   /* the game's picture, s_fw wide */
static uint32_t s_ui[W_MAX * H];          /* a menu, or the picture with a message over it */
static uint32_t s_bg[W_MAX * H];          /* the game's last picture behind the launcher */
static int s_fw = W;                      /* the picture's width: 256, or wider for widescreen */
static uint8_t s_sram[SRAM_SIZE], s_sram_saved[SRAM_SIZE];
static char s_sram_path[1100], s_rom_path[1024];
static uint8_t *s_rom; static uint32_t s_rom_size, s_rom_crc;
static bool s_headless, s_fast, s_quit, s_booted, s_mods = true;
static uint64_t s_frame;
static double s_apu_target;
static SDL_Window *s_window;
static SDL_Renderer *s_renderer;
static SDL_Texture *s_texture; static int s_tex_w;
static SDL_AudioStream *s_audio;
static SDL_Gamepad *s_gamepad;
static uint64_t s_next_tick;
static SDL_Texture *s_up; static int s_up_k, s_up_w;   /* the picture enlarged by a whole number */
static SDL_FRect s_dst;                   /* where the picture went on the window */
static double s_hz = 60.0988;             /* frames a second: the console's, or a 60 Hz screen's */
static bool s_screen60, s_vsync, s_cli_no_vsync, s_soft;
static char s_log_path[1100];
static uint64_t s_period_ns = 16639267;
static uint64_t s_t0, s_last_wake, s_paced, s_last_present;
static uint64_t s_work_ns, s_work_max, s_present_ns, s_late;
static uint64_t s_check_t; static int s_check_n;
static const char *s_dump_dir = "";
static FILE *s_wav;                       /* --wav: the sound, raw 32 kHz stereo 16-bit */
static uint16_t s_swallow;                /* buttons held when a menu closed: hidden until let go */
static char s_toast[160]; static uint64_t s_toast_until;
static bool s_fast_on;                    /* fast forward as last applied */

/* what the frame's end asks for */
static bool s_want_launcher, s_want_pause;
static int s_save_slot, s_load_slot;
static uint32_t s_cur_pc;                 /* the routine running (GameLoop: save states can be made) */

/* ── Saved games ──────────────────────────────────────────────────────── */
static void sram_save(void)
{
  if (!s_sram_path[0] || !memcmp(s_sram, s_sram_saved, SRAM_SIZE)) return;
  FILE *f = fopen(s_sram_path, "wb");
  if (!f) return;
  fwrite(s_sram, 1, SRAM_SIZE, f);
  fclose(f);
  memcpy(s_sram_saved, s_sram, SRAM_SIZE);
}

/* ── Scripted input (the same commands as the recompilation's --script):
 *   wait N, press <button[+button]> N, dump <tag>, poke <addr> <hex>,
 *   until <addr> ==|!= <value> [timeout], turbo on|off, quit ─────────── */
typedef struct {
  enum { S_PRESS, S_DUMP, S_POKE, S_UNTIL, S_QUIT, S_NOP } kind;
  uint16_t mask; int hold, wait;
  uint32_t addr; uint8_t bytes[64]; int nbytes;
  bool ne; uint16_t value; int width, timeout, waited;
  char tag[64];
} Step;
static Step *s_steps; static int s_nsteps, s_step, s_phase, s_count;

static uint16_t button(const char *name)
{
  static const struct { const char *n; uint16_t m; } T_[] = {
    { "start", B_START }, { "select", B_SELECT }, { "up", B_UP }, { "down", B_DOWN },
    { "left", B_LEFT }, { "right", B_RIGHT }, { "a", B_A }, { "b", B_B },
    { "x", B_X }, { "y", B_Y }, { "l", B_L }, { "r", B_R } };
  uint16_t m = 0;
  char buf[128]; snprintf(buf, sizeof buf, "%s", name);
  for (char *p = strtok(buf, "+,|"); p; p = strtok(NULL, "+,|"))
    for (size_t i = 0; i < sizeof T_ / sizeof T_[0]; i++) if (!strcmp(p, T_[i].n)) m |= T_[i].m;
  return m;
}
static void script_load(const char *path)
{
  FILE *f = fopen(path, "r");
  if (!f) { fprintf(stderr, "script: cannot open %s\n", path); exit(2); }
  char line[512]; int pending = 0, cap = 0;
  while (fgets(line, sizeof line, f)) {
    char cmd[32] = "", a1[128] = "", a2[128] = "", a3[64] = ""; int n = 0;
    n = sscanf(line, "%31s %127s %127s %63s", cmd, a1, a2, a3);
    if (n < 1 || cmd[0] == '#') continue;
    if (!strcmp(cmd, "wait")) { pending += atoi(a1); continue; }
    if (s_nsteps == cap) { cap = cap ? cap * 2 : 64; s_steps = realloc(s_steps, cap * sizeof *s_steps); }
    Step *e = &s_steps[s_nsteps]; memset(e, 0, sizeof *e);
    e->wait = pending; pending = 0;
    if (!strcmp(cmd, "press")) { e->kind = S_PRESS; e->mask = button(a1); e->hold = n >= 3 ? atoi(a2) : 1; }
    else if (!strcmp(cmd, "dump")) { e->kind = S_DUMP; snprintf(e->tag, sizeof e->tag, "%s", a1); }
    else if (!strcmp(cmd, "quit")) e->kind = S_QUIT;
    else if (!strcmp(cmd, "turbo")) e->kind = S_NOP;
    else if (!strcmp(cmd, "poke")) {
      e->kind = S_POKE; e->addr = (uint32_t)strtoul(a1, NULL, 16); e->hold = 1;
      for (size_t i = 0; a2[2 * i] && a2[2 * i + 1] && i < sizeof e->bytes; i++) {
        char h[3] = { a2[2 * i], a2[2 * i + 1], 0 };
        e->bytes[e->nbytes++] = (uint8_t)strtoul(h, NULL, 16);
      }
    } else if (!strcmp(cmd, "until") || !strcmp(cmd, "until16")) {
      e->kind = S_UNTIL; e->width = cmd[5] ? 2 : 1;
      e->addr = (uint32_t)strtoul(a1, NULL, 16); e->ne = a2[0] == '!';
      e->value = (uint16_t)strtoul(a3, NULL, 16); e->timeout = 36000;
      if (sscanf(line, "%*s %*s %*s %*s %d", &e->timeout) != 1) e->timeout = 36000;
    } else { fprintf(stderr, "script: unknown command %s\n", cmd); continue; }
    s_nsteps++;
  }
  fclose(f);
  s_step = 0; s_phase = 1; s_count = s_nsteps ? s_steps[0].wait : 0;
}
static void dump(const char *tag)
{
  char path[1200];
  snprintf(path, sizeof path, "%s%s%s.fb.bgrx", s_dump_dir, *s_dump_dir ? "/" : "", tag);
  FILE *f = fopen(path, "wb"); if (f) { fwrite(s_pixels, 1, (size_t)s_fw * H * 4, f); fclose(f); }
  snprintf(path, sizeof path, "%s%s%s.wram", s_dump_dir, *s_dump_dir ? "/" : "", tag);
  f = fopen(path, "wb"); if (f) { fwrite(g_ram, 1, sizeof g_ram, f); fclose(f); }
  snprintf(path, sizeof path, "%s%s%s.fb.ppm", s_dump_dir, *s_dump_dir ? "/" : "", tag);
  f = fopen(path, "wb");
  if (f) {
    fprintf(f, "P6\n%d %d\n255\n", s_fw, H);
    for (int i = 0; i < s_fw * H; i++) { uint8_t rgb[3] = { s_pixels[4 * i + 2], s_pixels[4 * i + 1], s_pixels[4 * i] }; fwrite(rgb, 1, 3, f); }
    fclose(f);
  }
  fprintf(stderr, "script f=%llu dump %s\n", (unsigned long long)s_frame, tag);
}
static uint16_t script_tick(void)
{
  for (;;) {
    if (s_step >= s_nsteps) return 0;
    Step *e = &s_steps[s_step];
    if (s_phase == 1) {
      if (s_count > 0) { s_count--; return 0; }
      s_phase = 0; s_count = e->hold;
    }
    if (e->kind == S_DUMP || e->kind == S_QUIT || e->kind == S_UNTIL || e->kind == S_NOP) {
      if (e->kind == S_UNTIL) {
        uint16_t v = g_ram[e->addr];
        if (e->width == 2) v |= (uint16_t)(g_ram[e->addr + 1] << 8);
        if ((v == e->value) == e->ne) {
          if (++e->waited > e->timeout) { fprintf(stderr, "script: until %05X timed out\n", e->addr); s_quit = true; }
          return 0;
        }
      }
      if (e->kind == S_DUMP) dump(e->tag);
      if (e->kind == S_QUIT) { fprintf(stderr, "script f=%llu quit\n", (unsigned long long)s_frame); s_quit = true; }
      if (++s_step < s_nsteps) { s_phase = 1; s_count = s_steps[s_step].wait; }
      if (s_quit) return 0;
      continue;
    }
    if (s_count > 0) {
      s_count--;
      if (e->kind == S_POKE) { memcpy(g_ram + e->addr, e->bytes, e->nbytes); return 0; }
      return e->mask;
    }
    if (++s_step < s_nsteps) { s_phase = 1; s_count = s_steps[s_step].wait; }
    return 0;
  }
}

/* ── Messages over the picture ─────────────────────────────────────────── */
void host_toast(const char *text)
{
  snprintf(s_toast, sizeof s_toast, "%s", text);
  s_toast_until = SDL_GetTicks() + 2500;
  fprintf(stderr, "%s\n", text);
}
static bool toast_on(void) { return s_toast[0] && SDL_GetTicks() < s_toast_until; }
static void draw_toast(uint32_t *px, int w)
{
  ui_target(px, w, H);
  int tw = ui_text_w(UI_BODY, s_toast) + 24;
  if (tw > w - 8) tw = w - 8;
  int x = (w - tw) / 2;
  ui_frame(x, 4, tw, 24, C_PAPER);
  ui_text(UI_BODY, x + 12, 20, s_toast, C_INK);
}

/* ── The gamepads ─────────────────────────────────────────────────────── */
int host_gamepad_count(void)
{
  int n = 0;
  SDL_JoystickID *ids = SDL_GetGamepads(&n);
  SDL_free(ids);
  return n;
}
const char *host_gamepad_name(int i)
{
  static char names[8][96];
  int n = 0;
  SDL_JoystickID *ids = SDL_GetGamepads(&n);
  const char *r = "?";
  if (ids && i >= 0 && i < n && i < 8) {
    const char *nm = SDL_GetGamepadNameForID(ids[i]);
    snprintf(names[i], sizeof names[i], "%s", nm ? nm : "?");
    r = names[i];
  }
  SDL_free(ids);
  return r;
}
int host_gamepad_current(void)
{
  if (!s_gamepad) return -1;
  int n = 0, cur = -1;
  SDL_JoystickID *ids = SDL_GetGamepads(&n);
  for (int i = 0; ids && i < n; i++) if (ids[i] == SDL_GetGamepadID(s_gamepad)) cur = i;
  SDL_free(ids);
  return cur;
}
void host_gamepad_select(int i)
{
  int n = 0;
  SDL_JoystickID *ids = SDL_GetGamepads(&n);
  if (ids && i >= 0 && i < n) {
    if (s_gamepad) SDL_CloseGamepad(s_gamepad);
    s_gamepad = SDL_OpenGamepad(ids[i]);
  }
  SDL_free(ids);
}
bool host_gamepad_down(SDL_GamepadButton b) { return s_gamepad && SDL_GetGamepadButton(s_gamepad, b); }
const char *host_gamepad_label(SDL_GamepadButton b)
{
  if (b <= SDL_GAMEPAD_BUTTON_NORTH && s_gamepad) {
    switch (SDL_GetGamepadButtonLabel(s_gamepad, b)) {
    case SDL_GAMEPAD_BUTTON_LABEL_A: return "A";
    case SDL_GAMEPAD_BUTTON_LABEL_B: return "B";
    case SDL_GAMEPAD_BUTTON_LABEL_X: return "X";
    case SDL_GAMEPAD_BUTTON_LABEL_Y: return "Y";
    case SDL_GAMEPAD_BUTTON_LABEL_CROSS: return T("Cross");
    case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE: return T("Circle");
    case SDL_GAMEPAD_BUTTON_LABEL_SQUARE: return T("Square");
    case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE: return T("Triangle");
    default: break;
    }
  }
  switch (b) {
  case SDL_GAMEPAD_BUTTON_SOUTH: return T("Bottom button");
  case SDL_GAMEPAD_BUTTON_EAST: return T("Right button");
  case SDL_GAMEPAD_BUTTON_WEST: return T("Left button");
  case SDL_GAMEPAD_BUTTON_NORTH: return T("Top button");
  case SDL_GAMEPAD_BUTTON_BACK: return "Back / Select";
  case SDL_GAMEPAD_BUTTON_GUIDE: return T("Guide");
  case SDL_GAMEPAD_BUTTON_START: return "Start";
  case SDL_GAMEPAD_BUTTON_LEFT_STICK: return "L3";
  case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return "R3";
  case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return "LB / L1";
  case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return "RB / R1";
  case SDL_GAMEPAD_BUTTON_DPAD_UP: return T("D-pad up");
  case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return T("D-pad down");
  case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return T("D-pad left");
  case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return T("D-pad right");
  case SDL_GAMEPAD_BUTTON_MISC1: return "Share";
  case SDL_GAMEPAD_BUTTON_TOUCHPAD: return "Touchpad";
  case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1: return "P1";
  case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1: return "P2";
  case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2: return "P3";
  case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2: return "P4";
  default: return "?";
  }
}
static bool pad_bound(SDL_GamepadButton b)
{
  for (int i = 0; i < NBUTTONS; i++) if (g_cfg.pad[i] == b) return true;
  return false;
}

/* ── The pads ─────────────────────────────────────────────────────────── */
static uint16_t read_pad(void)
{
  const bool *k = SDL_GetKeyboardState(NULL);
  uint16_t m = 0;
  for (int i = 0; i < NBUTTONS; i++)
    if (g_cfg.keys[i] != SDL_SCANCODE_UNKNOWN && k[g_cfg.keys[i]]) m |= kButtons[i].mask;
  bool fast = k[SDL_SCANCODE_TAB];
  if (s_gamepad) {
    for (int i = 0; i < NBUTTONS; i++)
      if (g_cfg.pad[i] != SDL_GAMEPAD_BUTTON_INVALID && SDL_GetGamepadButton(s_gamepad, g_cfg.pad[i])) m |= kButtons[i].mask;
    int16_t lx = SDL_GetGamepadAxis(s_gamepad, SDL_GAMEPAD_AXIS_LEFTX);
    int16_t ly = SDL_GetGamepadAxis(s_gamepad, SDL_GAMEPAD_AXIS_LEFTY);
    if (lx < -16000) m |= B_LEFT;
    if (lx > 16000) m |= B_RIGHT;
    if (ly < -16000) m |= B_UP;
    if (ly > 16000) m |= B_DOWN;
    if (SDL_GetGamepadAxis(s_gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 12000) fast = true;
  }
  s_fast = fast;
  /* never both ways at once */
  if ((m & (B_LEFT | B_RIGHT)) == (B_LEFT | B_RIGHT)) m &= (uint16_t)~(B_LEFT | B_RIGHT);
  if ((m & (B_UP | B_DOWN)) == (B_UP | B_DOWN)) m &= (uint16_t)~(B_UP | B_DOWN);
  return m;
}

static void gamepad_event(const SDL_Event *ev)
{
  if (ev->type == SDL_EVENT_GAMEPAD_ADDED) {
    if (!s_gamepad) {
      s_gamepad = SDL_OpenGamepad(ev->gdevice.which);
      if (s_gamepad) {
        char t[160]; snprintf(t, sizeof t, "%s %s", T("Gamepad connected:"), SDL_GetGamepadName(s_gamepad));
        host_toast(t);
      }
    }
  } else if (ev->type == SDL_EVENT_GAMEPAD_REMOVED) {
    if (s_gamepad && SDL_GetGamepadID(s_gamepad) == ev->gdevice.which) {
      SDL_CloseGamepad(s_gamepad); s_gamepad = NULL;
      host_toast(T("Gamepad disconnected."));
      int n = 0;
      SDL_JoystickID *ids = SDL_GetGamepads(&n);
      if (ids && n) s_gamepad = SDL_OpenGamepad(ids[0]);
      SDL_free(ids);
    }
  }
}

static void toggle_fullscreen(void)
{
  g_cfg.fullscreen = !g_cfg.fullscreen;
  host_apply_video();
  config_save();
}

static void poll_events(void)
{
  SDL_Event ev;
  while (SDL_PollEvent(&ev)) {
    switch (ev.type) {
    case SDL_EVENT_QUIT: s_quit = true; break;
    case SDL_EVENT_KEY_DOWN: {
      SDL_Keycode k = ev.key.key;
      if (ev.key.repeat) break;
      if (k == SDLK_ESCAPE) s_want_launcher = true;
      else if (k == SDLK_F11 || (k == SDLK_RETURN && (ev.key.mod & SDL_KMOD_ALT))) toggle_fullscreen();
      else if (k == SDLK_M) {
        bool bound = false;
        for (int i = 0; i < NBUTTONS; i++) if (g_cfg.keys[i] == SDL_SCANCODE_M) bound = true;
        if (!bound) s_want_pause = true;
      } else if (k >= SDLK_F1 && k <= SDLK_F10) {
        int slot = (int)(k - SDLK_F1) + 1;
        if (ev.key.mod & SDL_KMOD_SHIFT) s_save_slot = slot; else s_load_slot = slot;
      }
      break;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
      if (s_gamepad && ev.gbutton.which == SDL_GetGamepadID(s_gamepad)) {
        SDL_GamepadButton b = (SDL_GamepadButton)ev.gbutton.button;
        if ((b == SDL_GAMEPAD_BUTTON_LEFT_STICK || b == SDL_GAMEPAD_BUTTON_GUIDE) && !pad_bound(b)) s_want_launcher = true;
        if (b == SDL_GAMEPAD_BUTTON_RIGHT_STICK && !pad_bound(b)) s_want_pause = true;
      }
      break;
    case SDL_EVENT_GAMEPAD_ADDED: case SDL_EVENT_GAMEPAD_REMOVED: gamepad_event(&ev); break;
    default: break;
    }
  }
}

/* ── The picture on the window ────────────────────────────────────────────
 * Sharp (the default): first enlarged by a whole number with square
 * pixels (nearest), then to the window's size smoothly. A plain nearest
 * scale to a size that is not a whole multiple (full screen, a maximized
 * window) makes some rows and columns of pixels wider than others, and
 * fine patterns (the town's bricks and roofs) shimmer as they scroll; this
 * keeps them steady and sharp. Exact pixels: whole multiples only. */
static void present(const void *pixels, int w)
{
  if (w != s_tex_w) {
    if (s_texture) SDL_DestroyTexture(s_texture);
    s_texture = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, w, H);
    SDL_SetTextureScaleMode(s_texture, SDL_SCALEMODE_NEAREST);
    s_tex_w = w;
  }
  SDL_UpdateTexture(s_texture, NULL, pixels, w * 4);
  int ow = w, oh = H;
  SDL_GetCurrentRenderOutputSize(s_renderer, &ow, &oh);
  float scale = (float)ow / w < (float)oh / H ? (float)ow / w : (float)oh / H;
  int k = (int)scale;
  if (k < 1) k = 1;
  if (k > 8) k = 8;
  if (g_cfg.exact_pixels && scale >= 1) scale = (float)k;
  SDL_FRect dst = { (float)(int)((ow - w * scale) / 2), (float)(int)((oh - H * scale) / 2), w * scale, H * scale };
  s_dst = dst;
  SDL_SetRenderDrawColor(s_renderer, 0, 0, 0, 255);
  if (s_soft || (float)k == scale) {
    SDL_RenderClear(s_renderer);
    SDL_RenderTexture(s_renderer, s_texture, NULL, &dst);
    SDL_RenderPresent(s_renderer);
    return;
  }
  if (k != s_up_k || w != s_up_w) {
    if (s_up) SDL_DestroyTexture(s_up);
    s_up = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_TARGET, w * k, H * k);
    if (s_up) SDL_SetTextureScaleMode(s_up, SDL_SCALEMODE_LINEAR);
    s_up_k = k; s_up_w = w;
  }
  if (s_up && SDL_SetRenderTarget(s_renderer, s_up)) {
    SDL_RenderTexture(s_renderer, s_texture, NULL, NULL);
    SDL_SetRenderTarget(s_renderer, NULL);
    SDL_RenderClear(s_renderer);
    SDL_RenderTexture(s_renderer, s_up, NULL, &dst);
  } else {
    SDL_RenderClear(s_renderer);
    SDL_RenderTexture(s_renderer, s_texture, NULL, &dst);
  }
  SDL_RenderPresent(s_renderer);
}
static void present_game(void)
{
  if (toast_on()) {
    memcpy(s_ui, s_pixels, (size_t)s_fw * H * 4);
    draw_toast(s_ui, s_fw);
    present(s_ui, s_fw);
  } else {
    present(s_pixels, s_fw);
  }
}

/* ── Applying the settings ─────────────────────────────────────────────── */
static void set_vsync(bool on)
{
  s_vsync = false;
  if (on && s_screen60 && (SDL_SetRenderVSync(s_renderer, -1) || SDL_SetRenderVSync(s_renderer, 1))) s_vsync = true;
  else SDL_SetRenderVSync(s_renderer, 0);
  s_next_tick = 0; s_check_n = 0; s_last_wake = 0;
}
void host_apply_video(void)
{
  static int a_full = -1, a_scale = -1, a_vsync = -1, a_fw = -1;
  g_native_ppu_flags = g_cfg.sprite_limits ? (g_native_ppu_flags & ~8u) : (g_native_ppu_flags | 8u);
  int fw = s_mods ? mods_field_width() : W;
  if (s_headless) { s_fw = fw; return; }
  bool fw_changed = fw != s_fw || fw != a_fw;
  s_fw = fw;
  if ((int)g_cfg.fullscreen != a_full) {
    SDL_SetWindowFullscreen(s_window, g_cfg.fullscreen);
    a_full = g_cfg.fullscreen;
  }
  bool maximized = (SDL_GetWindowFlags(s_window) & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_FULLSCREEN)) != 0;
  if (!g_cfg.fullscreen && !maximized && (g_cfg.scale != a_scale || fw_changed) && a_fw != -1)
    SDL_SetWindowSize(s_window, fw * g_cfg.scale, H * g_cfg.scale);
  a_scale = g_cfg.scale; a_fw = fw;
  bool want = g_cfg.vsync && !s_cli_no_vsync;
  if ((int)want != a_vsync) { set_vsync(want); a_vsync = want; }
}
void host_apply_sound(void)
{
  if (s_audio) SDL_SetAudioStreamGain(s_audio, g_cfg.volume / 100.0f);
}
void host_apply_language(void) { i18n_set(g_cfg.language); }

/* ── The sound ─────────────────────────────────────────────────────────── */
static void run_sound(void)
{
  /* the sound chip: 1,024,000 cycles a second (a frame's share: on a 60 Hz
   * screen the game runs at the screen's rate, and the sound follows it,
   * 0.16 % slower at most) */
  s_apu_target += 1024000.0 / s_hz;
  native_apu_run_to((uint64_t)s_apu_target);
  if (getenv("HM_APU_DEBUG") && s_frame % 60 == 0) fprintf(stderr, "[apu] frame %llu: target %.0f run %llu\n", (unsigned long long)s_frame, s_apu_target, (unsigned long long)native_apu_cycles());
  int16_t samples[2 * 4096];
  uint32_t n = native_audio_samples(samples, 4096);
  if (s_audio && n) {
    /* more than 0.2 s waiting (of the game's time): the screen runs slower
     * than the sound (a slow monitor, a busy computer): drop these */
    int speed = s_fast_on ? g_cfg.ff_speed : 1;
    if (SDL_GetAudioStreamQueued(s_audio) < 32000 * 4 / 5 * speed) SDL_PutAudioStreamData(s_audio, samples, (int)(n * 4));
  }
  if (s_wav && n) fwrite(samples, 4, n, s_wav);
}

/* ── The pace ──────────────────────────────────────────────────────────── */
static void log_line(const char *fmt, const char *a, double b)
{
  FILE *lg = s_log_path[0] ? fopen(s_log_path, "a") : NULL;
  if (!lg) return;
  fprintf(lg, fmt, (unsigned long long)s_frame, a, b);
  fclose(lg);
}
static void pace(bool count)
{
  static int no_pace = -1;                         /* HM_NO_PACE=1: as fast as it goes (tests) */
  if (no_pace < 0) no_pace = getenv("HM_NO_PACE") != NULL;
  if (no_pace) return;
  uint64_t now = SDL_GetTicksNS();
  if (!s_t0) s_t0 = now;
  if (count) s_paced++;
  if (s_fast_on) {
    /* fast forward: the clock, ff_speed frames to a frame's time */
    uint64_t period = s_period_ns / (uint64_t)g_cfg.ff_speed;
    if (s_next_tick == 0 || now > s_next_tick + 4 * period) s_next_tick = now;
    s_next_tick += period;
    if (s_next_tick > now) SDL_DelayPrecise(s_next_tick - now);
    s_last_wake = 0;
    return;
  }
  if (s_vsync) {
    /* the screen's refresh paces the frames (the present waited for it).
     * If they fall behind (under 55 a second over two seconds: frames
     * missing the refresh and waiting a whole one), the clock takes over */
    if (s_check_n == 0) s_check_t = now;
    if (++s_check_n == 121) {
      double fps = 120e9 / (double)(now - s_check_t);
      s_check_n = 0;
      if (fps < 55.0) {
        SDL_SetRenderVSync(s_renderer, 0);
        s_vsync = false;
        log_line("frame %llu: %s %.1f\n", "frames a second with vsync, now the clock:", fps);
      }
    }
    if (count && s_last_wake && now - s_last_wake > s_period_ns * 3 / 2) s_late++;
    s_last_wake = now;
    return;
  }
  /* an even pace from the clock, never nudged, so movement stays smooth */
  if (s_next_tick == 0 || now > s_next_tick + 4 * s_period_ns) s_next_tick = now;
  if (count && now > s_next_tick + s_period_ns / 2) s_late++;
  s_next_tick += s_period_ns;
  if (s_next_tick > now) SDL_DelayPrecise(s_next_tick - now);
  s_last_wake = SDL_GetTicksNS();
}
static void set_fast(bool on)
{
  if (on == s_fast_on) return;
  s_fast_on = on;
  if (s_audio) SDL_SetAudioStreamFrequencyRatio(s_audio, on ? (float)g_cfg.ff_speed : 1.0f);
  if (s_vsync && !(on ? SDL_SetRenderVSync(s_renderer, 0) : (SDL_SetRenderVSync(s_renderer, -1) || SDL_SetRenderVSync(s_renderer, 1)))) s_vsync = false;
  s_next_tick = 0; s_check_n = 0; s_last_wake = 0;
}

/* ── Quitting ──────────────────────────────────────────────────────────── */
static void quit_now(void)
{
  sram_save();
  if (s_log_path[0] && s_t0) {
    FILE *lg = fopen(s_log_path, "a");
    double secs = (SDL_GetTicksNS() - s_t0) / 1e9;
    if (lg) {
      fprintf(lg, "%llu frames in %.1f s (%.2f a second, fast forward and menus left out); work %.2f ms a frame"
                  " (at most %.1f), showing it %.2f ms; %llu frames late; vsync %s\n",
              (unsigned long long)s_paced, secs, secs > 0 ? s_paced / secs : 0,
              s_paced ? s_work_ns / 1e6 / s_paced : 0, s_work_max / 1e6,
              s_paced ? s_present_ns / 1e6 / s_paced : 0, (unsigned long long)s_late,
              s_vsync ? "on" : "off");
      fclose(lg);
    }
  }
  if (s_wav) fclose(s_wav);
  if (s_audio) SDL_DestroyAudioStream(s_audio);
  SDL_Quit();
  exit(0);
}

/* ── Save states ───────────────────────────────────────────────────────────
 * <rom>.st1 ... .st10: the whole machine at the start of a GameLoop frame
 * (the one point where no routine of the game is half done), so a state
 * is made there and loads back there. The diaries (the battery RAM) are
 * not in it: loading a state never undoes a diary. */
static HmRegs s_regs;
typedef struct { char magic[8]; uint32_t crc, size; HmRegs regs; } StateHeader;
static void state_path(int slot, char *out, size_t n)
{
  snprintf(out, n, "%s", s_sram_path);
  size_t l = strlen(out);
  if (l > 4) out[l - 4] = 0;               /* ".srm" */
  char ext[8]; snprintf(ext, sizeof ext, ".st%d", slot);
  strncat(out, ext, n - strlen(out) - 1);
}
bool host_state_info(int slot, char *when, size_t n)
{
  if (!s_sram_path[0]) return false;
  char path[1200]; state_path(slot, path, sizeof path);
  SDL_PathInfo info;
  if (!SDL_GetPathInfo(path, &info) || info.type != SDL_PATHTYPE_FILE) return false;
  if (when && n) {
    SDL_DateTime dt;
    if (SDL_TimeToDateTime(info.modify_time, &dt, true))
      snprintf(when, n, "%02d/%02d/%04d  %02d:%02d", dt.day, dt.month, dt.year, dt.hour, dt.minute);
    else snprintf(when, n, "?");
  }
  return true;
}
static void state_save_file(int slot)
{
  size_t size = native_state_size();
  uint8_t *buf = malloc(size);
  if (!buf || !native_state_save(buf, size)) { free(buf); host_toast(T("The state could not be saved.")); return; }
  StateHeader h; memset(&h, 0, sizeof h);
  memcpy(h.magic, "HMNATST1", 8); h.crc = s_rom_crc; h.size = (uint32_t)size; h.regs = s_regs;
  char path[1200]; state_path(slot, path, sizeof path);
  FILE *f = fopen(path, "wb");
  bool ok = f && fwrite(&h, sizeof h, 1, f) == 1 && fwrite(buf, 1, size, f) == size;
  if (f) fclose(f);
  free(buf);
  char t[96]; snprintf(t, sizeof t, ok ? T("State saved in slot %d.") : T("The state could not be saved."), slot);
  host_toast(t);
}

/* the restarts: longjmp back to main's loop, from anywhere in a frame */
static jmp_buf s_jmp;
static int s_jump;
enum { J_START, J_RESET, J_SWITCH, J_STATE, J_ENDLESS };
static uint8_t *s_load_buf; static size_t s_load_len; static HmRegs s_load_regs;
static uint8_t *s_keep_buf; static size_t s_keep_cap; static HmRegs s_keep_regs; static bool s_keep_ok;

static bool state_read_file(int slot)
{
  char path[1200]; state_path(slot, path, sizeof path);
  FILE *f = fopen(path, "rb");
  if (!f) { char t[96]; snprintf(t, sizeof t, T("Slot %d is empty."), slot); host_toast(t); return false; }
  StateHeader h;
  size_t size = native_state_size();
  bool ok = fread(&h, sizeof h, 1, f) == 1 && !memcmp(h.magic, "HMNATST1", 8) && h.size == size && h.crc == s_rom_crc;
  if (ok) {
    free(s_load_buf);
    s_load_buf = malloc(size);
    ok = s_load_buf && fread(s_load_buf, 1, size, f) == size;
    s_load_len = size; s_load_regs = h.regs;
  }
  fclose(f);
  if (!ok) host_toast(T("That state is from another ROM or version."));
  return ok;
}

/* ── The menus: the game stands still, the music goes on ──────────────── */
static bool s_menu_mouse;
static uint16_t s_test_press;
static void menu_input(UiInput *in, uint16_t *prev, uint32_t *held_frames)
{
  memset(in, 0, sizeof *in);
  in->key = SDL_SCANCODE_UNKNOWN; in->pad_button = -1; in->mouse_x = in->mouse_y = -1;
  static float mx = -1, my = -1;
  bool tab_l = false, tab_r = false;
  SDL_Event ev;
  while (SDL_PollEvent(&ev)) {
    switch (ev.type) {
    case SDL_EVENT_QUIT: s_quit = true; break;
    case SDL_EVENT_KEY_DOWN:
      if (ev.key.repeat) break;
      if (in->key == SDL_SCANCODE_UNKNOWN) in->key = ev.key.scancode;
      if (ev.key.key == SDLK_RETURN && (ev.key.mod & SDL_KMOD_ALT)) { toggle_fullscreen(); in->key = SDL_SCANCODE_UNKNOWN; break; }
      if (ev.key.key == SDLK_F11) { toggle_fullscreen(); break; }
      if (ev.key.key == SDLK_RETURN || ev.key.key == SDLK_KP_ENTER || ev.key.key == SDLK_SPACE) in->accept = true;
      if (ev.key.key == SDLK_ESCAPE || ev.key.key == SDLK_BACKSPACE) in->back = true;
      if (ev.key.key == SDLK_PAGEUP || ev.key.key == SDLK_Q) tab_l = true;
      if (ev.key.key == SDLK_PAGEDOWN || ev.key.key == SDLK_E || ev.key.key == SDLK_TAB) tab_r = true;
      break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
      if (s_gamepad && ev.gbutton.which == SDL_GetGamepadID(s_gamepad)) {
        if (in->pad_button < 0) in->pad_button = ev.gbutton.button;
        if ((ev.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_STICK || ev.gbutton.button == SDL_GAMEPAD_BUTTON_GUIDE ||
             ev.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_STICK) && !pad_bound((SDL_GamepadButton)ev.gbutton.button))
          in->back = true;
      }
      break;
    case SDL_EVENT_GAMEPAD_ADDED: case SDL_EVENT_GAMEPAD_REMOVED: gamepad_event(&ev); break;
    case SDL_EVENT_MOUSE_MOTION: mx = ev.motion.x; my = ev.motion.y; in->mouse_moved = true; s_menu_mouse = true; break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
      mx = ev.button.x; my = ev.button.y; s_menu_mouse = true;
      if (ev.button.button == SDL_BUTTON_LEFT) in->click = true;
      if (ev.button.button == SDL_BUTTON_RIGHT) in->rclick = true;
      break;
    case SDL_EVENT_MOUSE_WHEEL: in->wheel += ev.wheel.y > 0 ? 1 : ev.wheel.y < 0 ? -1 : 0; break;
    default: break;
    }
  }
  if (s_menu_mouse && mx >= 0 && s_dst.w > 0) {
    float rx = mx, ry = my;
    SDL_RenderCoordinatesFromWindow(s_renderer, mx, my, &rx, &ry);
    float cx = (rx - s_dst.x) * s_fw / s_dst.w, cy = (ry - s_dst.y) * H / s_dst.h;
    if (cx >= 0 && cy >= 0 && cx < s_fw && cy < H) { in->mouse_x = (int)cx; in->mouse_y = (int)cy; }
  }
  /* the game's buttons, and the arrows whatever keys the game has */
  uint16_t held = read_pad();
  const bool *k = SDL_GetKeyboardState(NULL);
  if (k[SDL_SCANCODE_UP]) held |= B_UP;
  if (k[SDL_SCANCODE_DOWN]) held |= B_DOWN;
  if (k[SDL_SCANCODE_LEFT]) held |= B_LEFT;
  if (k[SDL_SCANCODE_RIGHT]) held |= B_RIGHT;
  held |= s_test_press;
  s_test_press = 0;
  uint16_t pressed = held & (uint16_t)~*prev;
  /* directions held repeat */
  uint16_t dirs = held & (B_UP | B_DOWN | B_LEFT | B_RIGHT);
  if (dirs && dirs == (*prev & (B_UP | B_DOWN | B_LEFT | B_RIGHT))) {
    if (++*held_frames > 18 && (*held_frames - 18) % 5 == 0) pressed |= dirs;
  } else *held_frames = 0;
  *prev = held;
  if (tab_l) pressed |= B_L;
  if (tab_r) pressed |= B_R;
  in->held = held; in->pressed = pressed;
  if (pressed & (B_A | B_START)) in->accept = true;
  if (pressed & B_B) in->back = true;
}

enum { MENU_LAUNCHER, MENU_PAUSE };
/* HM_UI_TEST=keys (u d l r A B L R X Y, one every 10 frames) and
 * HM_UI_SHOT=dir: the menus' pictures, for checking them without a screen */
static void ui_test(uint32_t *t, int kind)
{
  static const char *keys; static int pos, shot;
  if (!keys) { keys = getenv("HM_UI_TEST"); if (!keys) keys = ""; }
  const char *dir = getenv("HM_UI_SHOT");
  if (!dir || *t % 10 != 9) return;
  char path[1200];
  snprintf(path, sizeof path, "%s/ui%03d.ppm", dir, shot++);
  FILE *f = fopen(path, "wb");
  if (f) {
    fprintf(f, "P6\n%d %d\n255\n", s_fw, H);
    for (int i = 0; i < s_fw * H; i++) { uint8_t rgb[3] = { (uint8_t)(s_ui[i] >> 16), (uint8_t)(s_ui[i] >> 8), (uint8_t)s_ui[i] }; fwrite(rgb, 1, 3, f); }
    fclose(f);
  }
  char k = keys[pos];
  if (!k) { if (kind == MENU_PAUSE) pause_action('B'); else s_quit = true; return; }
  pos++;
  static const struct { char c; uint16_t m; } M[] = { { 'u', B_UP }, { 'd', B_DOWN }, { 'l', B_LEFT }, { 'r', B_RIGHT },
    { 'A', B_A }, { 'B', B_B }, { 'L', B_L }, { 'R', B_R }, { 'X', B_X }, { 'Y', B_Y } };
  for (size_t i = 0; i < sizeof M / sizeof M[0]; i++) if (M[i].c == k) s_test_press = M[i].m;
  if (k == '.') { /* stay */ }
  if (k == 'q') s_quit = true;
}
static int s_bg_w;
static int run_menu(int kind, bool in_game)
{
  if (s_headless) return LA_PLAY;
  uint16_t prev = read_pad();
  uint32_t held_frames = 0;
  int result = LA_NONE;
  if (kind == MENU_LAUNCHER) launcher_open(in_game);
  else pause_open();
  if (in_game) { memcpy(s_bg, s_pixels, (size_t)s_fw * H * 4); s_bg_w = s_fw; }
  set_fast(false);
  s_menu_mouse = false;
  SDL_ShowCursor();
  uint32_t t = 0;
  for (;;) {
    UiInput in;
    menu_input(&in, &prev, &held_frames);
    if (s_quit) quit_now();
    if (kind == MENU_PAUSE) {
      if (in.back || in.key == SDL_SCANCODE_M) pause_action('B');
      else {
        if (in.accept) pause_action('A');
        if (in.pressed & B_X) pause_action('X');
        if (in.pressed & B_Y) pause_action('Y');
        if (in.pressed & B_L) pause_action('L');
        if (in.pressed & B_R) pause_action('R');
        if (in.pressed & B_UP) pause_action('u');
        if (in.pressed & B_DOWN) pause_action('d');
        if (in.pressed & B_LEFT) pause_action('l');
        if (in.pressed & B_RIGHT) pause_action('r');
      }
      if (!pause_is_open()) break;
      pause_draw(s_ui, s_fw);
    } else {
      result = launcher_update(&in);
      if (s_quit) quit_now();
      if (result != LA_NONE) break;
      const uint32_t *bg = NULL;
      if (in_game) {
        if (s_bg_w != s_fw) {   /* widescreen turned on or off: the old picture, centred */
          static uint32_t tmp[W_MAX * H];
          memset(tmp, 0, sizeof tmp);
          int off = (s_fw - s_bg_w) / 2;
          for (int y = 0; y < H; y++)
            for (int x = 0; x < s_bg_w; x++) if (x + off >= 0 && x + off < s_fw) tmp[y * s_fw + x + off] = s_bg[y * s_bg_w + x];
          memcpy(s_bg, tmp, (size_t)s_fw * H * 4);
          s_bg_w = s_fw;
        }
        bg = s_bg;
      }
      launcher_draw(s_ui, s_fw, H, bg, t);
    }
    if (toast_on()) draw_toast(s_ui, s_fw);
    ui_test(&t, kind);
    present(s_ui, s_fw);
    if (s_booted) run_sound();
    pace(false);
    t++;
  }
  if (g_cfg.fullscreen) SDL_HideCursor();
  s_swallow = read_pad();            /* what closed the menu stays out of the game */
  s_next_tick = 0; s_last_wake = 0; s_check_n = 0;
  return result;
}
static void launcher_in_game(void)
{
  int r = run_menu(MENU_LAUNCHER, true);
  switch (r) {
  case LA_QUIT: quit_now(); break;
  case LA_RESET: longjmp(s_jmp, J_RESET);
  case LA_SWITCH_ROM: longjmp(s_jmp, J_SWITCH);
  case LA_SAVE_STATE: s_save_slot = launcher_slot(); break;
  case LA_LOAD_STATE: s_load_slot = launcher_slot(); break;
  default: break;
  }
}

/* ── The end of a frame (every NMI) ───────────────────────────────────── */
static void end_of_frame(uint8_t *ram)
{
  if (s_mods) mods_frame(ram);
  int ml = 0, mr = 0;
  if (s_mods) mods_ws_margins(&ml, &mr);
  native_draw_frame(s_pixels, s_fw, ml, mr);
  s_frame++;
  run_sound();

  uint16_t pad = 0;
  if (!s_headless) {
    poll_events();
    pad = read_pad();
    s_swallow &= pad;
    pad &= (uint16_t)~s_swallow;
    set_fast(s_fast);
    uint64_t t = SDL_GetTicksNS();
    if (s_last_wake && !s_fast_on) {
      uint64_t w = t - s_last_wake;
      s_work_ns += w;
      if (w > s_work_max) s_work_max = w;
    }
    if (!s_fast_on || t - s_last_present >= 16000000) {   /* fast: about 60 pictures a second */
      present_game();
      s_last_present = t;
    }
    if (!s_fast_on) s_present_ns += SDL_GetTicksNS() - t;
  }
  { static long d0 = -1, d1 = -1;                     /* HM_DUMP_FRAMES=a:b: every frame's picture and WRAM */
    if (d0 < 0) { const char *e = getenv("HM_DUMP_FRAMES"); d0 = 0; if (e) sscanf(e, "%ld:%ld", &d0, &d1); }
    if ((long)s_frame >= d0 && (long)s_frame <= d1) { char t[32]; snprintf(t, sizeof t, "fr%06ld", (long)s_frame); dump(t); } }
  if (s_nsteps) pad |= script_tick();
  if (s_mods) { bool open = false; pad = mods_pad(pad, &open); if (open) s_want_launcher = true; }
  native_set_pads(pad, 0);
  if (s_frame % 60 == 0) sram_save();
  if (s_quit) quit_now();
  if (s_mods && mods_endless_restore() && s_keep_ok) {
    free(s_load_buf);
    s_load_buf = malloc(s_keep_cap);
    if (s_load_buf) {
      memcpy(s_load_buf, s_keep_buf, s_keep_cap);
      s_load_len = s_keep_cap; s_load_regs = s_keep_regs;
      longjmp(s_jmp, J_ENDLESS);
    }
  }
  if (s_headless) return;

  { const char *e = getenv("HM_MENU_AT");            /* HM_MENU_AT=frame:p|l[,frame:p|l] (tests) */
    for (; e && *e; e = strchr(e, ',') ? strchr(e, ',') + 1 : NULL) {
      const char *c = strchr(e, ':');
      if (atol(e) == (long)s_frame) { if (c && c[1] == 'p') s_want_pause = true; else s_want_launcher = true; }
    } }
  if (s_want_pause) {
    s_want_pause = false;
    if (g_cfg.pause_menu && pause_can_open()) { run_menu(MENU_PAUSE, true); native_set_pads(0, 0); }
  }
  if (s_want_launcher) {
    s_want_launcher = false;
    launcher_in_game();
    native_set_pads(0, 0);
  }
  if (s_save_slot && s_cur_pc != 0x808083) {
    s_save_slot = 0;
    host_toast(T("States can be saved only while playing (not on the title or in the intro)."));
  }
  if (s_load_slot) {
    int slot = s_load_slot;
    s_load_slot = 0;
    if (state_read_file(slot)) {
      char t[96]; snprintf(t, sizeof t, T("State %d loaded."), slot);
      host_toast(t);
      longjmp(s_jmp, J_STATE);
    }
  }
  pace(true);
}

/* the start of a GameLoop frame: nothing of the game is half done */
static void at_clean_point(void)
{
  if (s_save_slot) { state_save_file(s_save_slot); s_save_slot = 0; }
  if (s_mods && mods_endless_keep()) {
    if (!s_keep_buf) { s_keep_cap = native_state_size(); s_keep_buf = malloc(s_keep_cap); }
    s_keep_ok = s_keep_buf && native_state_save(s_keep_buf, s_keep_cap);
    s_keep_regs = s_regs;
    mods_endless_kept(s_keep_ok);
  }
}

/* a frame that ends inside a wait for the sound chip (hw.c) */
static void lag_frame(void)
{
  if (native_nmi_enabled()) {
    /* the NMI saves and restores everything; the C's live flags too */
    HmRegs keep = s_regs;
    hm_nmi(g_ram, &s_regs, 0x0000, s_regs.p);
    s_regs = keep;
  } else {
    end_of_frame(g_ram);
  }
}

/* ── The routines the game goes on to ─────────────────────────────────── */
typedef void Fn(uint8_t *ram, HmRegs *r);
static Fn *routine_at(uint32_t pc)
{
  static const struct { uint32_t pc; Fn *fn; } T_[] = {
    { 0x82D75E, hm_IntroScreen }, { 0x82D80D, hm_IntroScreen_D80D },
    { 0x82D93F, hm_IntroScreen_D93F }, { 0x82E0EB, hm_IntroScreen_E0EB },
    { 0x82D871, hm_IntroScreen_D871 },
    { 0x82E1F1, hm_SUB_82E1F1 }, { 0x82E337, hm_SUB_82E1F1_E337 },
    { 0x82D1C0, hm_SUB_82D1C0 }, { 0x82D347, hm_SUB_82D1C0_D347 },
    { 0x82D3C7, hm_DemoPresentation }, { 0x82D6C9, hm_DemoPresentation_D6C9 },
    { 0x82E7F9, hm_StartFromNewGame }, { 0x82E7E9, hm_StartFromLoad },
    { 0x808083, hm_GameLoop },
    { 0x82E80C, hm_Unk_NamesInput }, { 0x82E95F, hm_Unk_NamesInput_E95F },
    { 0x8080ED, hm_SetPlayerName }, { 0x80815F, hm_SetCowNameBought },
    { 0x8081D2, hm_SetCowNameBorn }, { 0x808254, hm_SetDogName },
    { 0x8082C6, hm_SetHorseName }, { 0x808338, hm_SetKid1Name }, { 0x8083AE, hm_SetKid2Name },
  };
  for (size_t i = 0; i < sizeof T_ / sizeof T_[0]; i++) if (T_[i].pc == pc) return T_[i].fn;
  return NULL;
}

/* ── The ROM ───────────────────────────────────────────────────────────── */
static uint8_t *load_rom(const char *path, uint32_t *size)
{
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  if (n <= 0) { fclose(f); return NULL; }
  uint8_t *d = malloc((size_t)n);
  if (!d || fread(d, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(d); return NULL; }
  fclose(f);
  long skip = (n % 1024) == 512 ? 512 : 0;       /* a copier's header */
  if (skip) memmove(d, d + skip, (size_t)(n - skip));
  *size = (uint32_t)(n - skip);
  return d;
}
static uint32_t crc32_of(const uint8_t *d, size_t n)
{
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++) { c ^= d[i]; for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; }
  return c ^ 0xFFFFFFFFu;
}
/* the ROM at `path` in use, with its battery RAM */
static bool use_rom(const char *path)
{
  uint32_t size = 0;
  uint8_t *rom = load_rom(path, &size);
  if (!rom) return false;
  sram_save();
  free(s_rom);
  s_rom = rom; s_rom_size = size; s_rom_crc = crc32_of(rom, size);
  snprintf(s_rom_path, sizeof s_rom_path, "%s", path);
  snprintf(s_sram_path, sizeof s_sram_path, "%s", path);
  char *dot = strrchr(s_sram_path, '.');
  if (dot && !strchr(dot, '/') && !strchr(dot, '\\')) *dot = 0;
  strncat(s_sram_path, ".srm", sizeof s_sram_path - strlen(s_sram_path) - 1);
  memset(s_sram, 0, sizeof s_sram);
  FILE *f = fopen(s_sram_path, "rb");
  if (f) { size_t got = fread(s_sram, 1, SRAM_SIZE, f); (void)got; fclose(f); }
  memcpy(s_sram_saved, s_sram, SRAM_SIZE);
  return true;
}
bool host_running(void) { return s_booted; }
const char *host_rom(void) { return s_rom_path; }

/* the system's file window (its answer may come on another thread) */
static SDL_AtomicInt s_dialog_done;
static char s_dialog_path[1024];
static void SDLCALL dialog_done(void *u, const char *const *files, int filter)
{
  (void)u; (void)filter;
  if (files && files[0]) { snprintf(s_dialog_path, sizeof s_dialog_path, "%s", files[0]); SDL_SetAtomicInt(&s_dialog_done, 1); }
}
void host_rom_dialog(void)
{
  static const SDL_DialogFileFilter filters[] = { { "SNES ROM (*.sfc, *.smc)", "sfc;smc" }, { "*", "*" } };
  SDL_ShowOpenFileDialog(dialog_done, NULL, s_window, filters, 2, config_dir(), false);
}
bool host_rom_dialog_result(char *out, size_t n)
{
  if (!SDL_GetAtomicInt(&s_dialog_done)) return false;
  snprintf(out, n, "%s", s_dialog_path);
  SDL_SetAtomicInt(&s_dialog_done, 0);
  return true;
}

/* a window icon: the harvest moon over a hill, drawn here */
static void window_icon(void)
{
  static uint32_t px[32 * 32];
  ui_target(px, 32, 32);
  ui_rect(0, 0, 32, 32, UI_RGB(0x18204C));
  for (int y = 0; y < 32; y++)
    for (int x = 0; x < 32; x++) {
      int dx = x - 18, dy = y - 12;
      if (dx * dx + dy * dy <= 81) ui_px(x, y, dy > 3 ? UI_RGB(0xF2A23A) : UI_RGB(0xFFE28C));
      if (y > 24 - (int)(4 * SDL_sin(x * 0.2))) ui_px(x, y, UI_RGB(0x2E5426));
    }
  ui_rect(5, 18, 8, 7, UI_RGB(0x7E2620));
  for (int j = 0; j < 5; j++) ui_rect(4 + j, 17 - j, 10 - 2 * j, 1, UI_RGB(0x4A1614));
  SDL_Surface *s = SDL_CreateSurfaceFrom(32, 32, SDL_PIXELFORMAT_ARGB8888, px, 32 * 4);
  if (s) { SDL_SetWindowIcon(s_window, s); SDL_DestroySurface(s); }
}

static void boot(void)
{
  memset(&s_regs, 0, sizeof s_regs);
  s_regs.s = 0x1EFC; s_regs.p = 0x34; s_regs.pb = 0x00; s_regs.db = 0x00;
  g_hm_next = 0;
  if (s_nsteps) native_set_pads(script_tick(), 0);
  s_cur_pc = 0x808000;
  hm_RESET(g_ram, &s_regs);
  s_regs.s = 0x1F00;   /* RESET's stack (the rewrite gives the checker's own back) */
}

static void find_rom(const char *arg)
{
  if (arg) { snprintf(g_cfg.rom, sizeof g_cfg.rom, "%s", arg); return; }
  if (g_cfg.rom[0]) { FILE *f = fopen(g_cfg.rom, "rb"); if (f) { fclose(f); return; } }
  static const char *names[] = { "rom.sfc", "harvestmoon.sfc", "harvestmoon.smc", "Harvest Moon (USA).sfc" };
  const char *base = config_dir();
  for (int where = 0; where < 2; where++)
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
      char path[1100];
      snprintf(path, sizeof path, "%s%s", where ? base : "", names[i]);
      FILE *f = fopen(path, "rb");
      if (f) { fclose(f); snprintf(g_cfg.rom, sizeof g_cfg.rom, "%s", path); return; }
    }
}

int main(int argc, char **argv)
{
  const char *rom_arg = NULL, *script = NULL;
  int cli_scale = 0; bool cli_full = false, cli_limits = false, no_launcher = false, cli_mods = false, cli_ws = false;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--script") && i + 1 < argc) script = argv[++i];
    else if (!strcmp(argv[i], "--headless")) s_headless = true;
    else if (!strcmp(argv[i], "--dump-dir") && i + 1 < argc) s_dump_dir = argv[++i];
    else if (!strcmp(argv[i], "--scale") && i + 1 < argc) cli_scale = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--fullscreen")) cli_full = true;
    else if (!strcmp(argv[i], "--no-vsync")) s_cli_no_vsync = true;
    else if (!strcmp(argv[i], "--sprite-limits")) cli_limits = true;
    else if (!strcmp(argv[i], "--no-launcher")) no_launcher = true;
    else if (!strcmp(argv[i], "--mods")) cli_mods = true;
    else if (!strcmp(argv[i], "--widescreen")) cli_ws = true;
    else if (!strcmp(argv[i], "--wav") && i + 1 < argc) s_wav = fopen(argv[++i], "wb");
    else if (argv[i][0] != '-') rom_arg = argv[i];
  }
  if (s_headless && !cli_mods) {
    /* the tests: the game alone, as the console runs it */
    config_defaults(&g_cfg);
    g_cfg.widescreen = cli_ws; g_cfg.pause_menu = g_cfg.title_options = g_cfg.endless = false;
    s_mods = cli_ws;
  } else {
    config_load();
    char path[1100]; snprintf(path, sizeof path, "%sconfig.ini", config_dir());
    FILE *f = fopen(path, "r");
    if (f) fclose(f); else if (!s_headless) config_save();       /* the first start: write both files */
    if (cli_ws) g_cfg.widescreen = true;
  }
  if (cli_scale >= 1 && cli_scale <= 6) g_cfg.scale = cli_scale;
  if (cli_full) g_cfg.fullscreen = true;
  if (cli_limits) g_cfg.sprite_limits = true;
  i18n_set(g_cfg.language);
  find_rom(rom_arg);
  if (script) script_load(script);
  { const char *e = getenv("HM_PPU_FLAGS"); if (e) g_native_ppu_flags = (uint32_t)strtoul(e, NULL, 0); }
  if (g_cfg.sprite_limits) g_native_ppu_flags &= ~8u;
  s_fw = s_mods ? mods_field_width() : W;

  if (!s_headless) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
      fprintf(stderr, "SDL: %s\n", SDL_GetError());
      return 1;
    }
    if (!SDL_CreateWindowAndRenderer("Harvest Moon", s_fw * g_cfg.scale, H * g_cfg.scale, SDL_WINDOW_RESIZABLE,
                                     &s_window, &s_renderer)) {
      fprintf(stderr, "SDL: %s\n", SDL_GetError());
      return 1;
    }
    window_icon();
    /* the pace: on a 60 Hz screen, the game at the screen's own rate (each
     * frame shown once, evenly; the sound follows, 0.16 % off at most),
     * with vsync unless it is turned off; on other screens, our own clock */
    const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(s_window));
    float hz = mode ? mode->refresh_rate : 0;
    s_screen60 = hz >= 58.5f && hz <= 61.5f;
    if (s_screen60) { s_hz = hz; s_period_ns = (uint64_t)(1e9 / hz); }
    const char *rname = SDL_GetRendererName(s_renderer);
    s_soft = rname && !strcmp(rname, "software");
    snprintf(s_log_path, sizeof s_log_path, "%sHarvestMoonNative.log", config_dir());
    SDL_AudioSpec spec = { SDL_AUDIO_S16, 2, 32000 };
    s_audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (s_audio) {
      static int16_t quiet[2 * 1600];                   /* 50 ms of silence first: room for the pace to wobble */
      SDL_PutAudioStreamData(s_audio, quiet, sizeof quiet);
      SDL_ResumeAudioStreamDevice(s_audio);
    }
    else fprintf(stderr, "no sound: %s\n", SDL_GetError());
    host_apply_video();
    host_apply_sound();
    FILE *lg = fopen(s_log_path, "w");
    if (lg) {
      fprintf(lg, "renderer %s, screen %.2f Hz, vsync %s\n", rname ? rname : "?", hz, s_vsync ? "on" : "off");
      fclose(lg);
    }
    int n = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&n);
    if (ids && n) s_gamepad = SDL_OpenGamepad(ids[0]);
    SDL_free(ids);
  }

  /* the launcher first (unless it is turned off and the ROM is there) */
  for (;;) {
    char why[200] = "";
    bool ok = g_cfg.rom[0] && launcher_rom_ok(g_cfg.rom, why, sizeof why);
    if (s_headless) {
      if (!ok || !use_rom(g_cfg.rom)) {
        fprintf(stderr, "Harvest Moon: no ROM. Put your rom.sfc next to the program, or pass its path.\n");
        return 1;
      }
      break;
    }
    if (ok && (no_launcher || !g_cfg.show_launcher) && use_rom(g_cfg.rom)) break;
    int r = run_menu(MENU_LAUNCHER, false);
    if (r == LA_QUIT) quit_now();
    if (g_cfg.rom[0] && launcher_rom_ok(g_cfg.rom, NULL, 0) && use_rom(g_cfg.rom)) break;
    host_toast(T("That ROM could not be opened."));
    no_launcher = false;
  }
  if (s_log_path[0]) { FILE *lg = fopen(s_log_path, "a"); if (lg) { fprintf(lg, "rom %s\n", s_rom_path); fclose(lg); } }
  if (!native_hw_init(s_rom, s_rom_size, s_sram, SRAM_SIZE)) { fprintf(stderr, "out of memory\n"); return 1; }
  if (!s_headless && g_cfg.fullscreen) SDL_HideCursor();

  g_hm_frame_hook = end_of_frame;
  g_native_lag_frame = lag_frame;
  s_booted = true;
  switch ((s_jump = setjmp(s_jmp))) {
  case J_START: boot(); break;
  case J_SWITCH:
    if (!use_rom(g_cfg.rom)) host_toast(T("That ROM could not be opened."));
    /* fall through */
  case J_RESET:
    sram_save();
    native_hw_reset(s_rom, s_rom_size);
    mods_reset();
    s_want_launcher = s_want_pause = false; s_save_slot = s_load_slot = 0;
    boot();
    break;
  case J_STATE:
  case J_ENDLESS:
    native_state_load(s_load_buf, s_load_len);
    if (s_jump == J_ENDLESS) mods_endless_restored();
    s_regs = s_load_regs;
    g_hm_next = 0x808083;
    s_want_launcher = s_want_pause = false;
    s_next_tick = 0;
    break;
  }
  for (;;) {
    uint32_t pc = g_hm_next;
    Fn *fn = routine_at(pc);
    if (!fn) {
      fprintf(stderr, "native: the game went to $%06X, which this build does not know\n", (unsigned)pc);
      sram_save();
      return 3;
    }
    s_cur_pc = pc;
    if (pc == 0x808083) at_clean_point();
    g_hm_next = 0;
    fn(g_ram, &s_regs);
  }
}
