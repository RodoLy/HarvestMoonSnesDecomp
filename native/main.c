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
 *   HarvestMoonNative [rom.sfc] [--scale N] [--fullscreen]
 *                     [--script file --headless --dump-dir dir]
 *
 * The saved games go next to the ROM (<rom>.srm), as the cartridge's
 * battery RAM.
 *
 * Keys (keybinds.ini, as the recompilation's): arrows; X = A, Z = B,
 * S = X, A = Y, C = L, V = R, Enter = Start, Right Shift = Select;
 * F11 / Alt+Enter full screen; Tab held = fast; Escape quits. A gamepad
 * works too.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "hm_decomp.h"
#include "native.h"

enum { W = 256, H = 224, SRAM_SIZE = 0x2000 };
/* the pads' bits as $4218/$4219 read them */
enum { B_B = 0x8000, B_Y = 0x4000, B_SELECT = 0x2000, B_START = 0x1000, B_UP = 0x0800,
       B_DOWN = 0x0400, B_LEFT = 0x0200, B_RIGHT = 0x0100, B_A = 0x0080, B_X = 0x0040,
       B_L = 0x0020, B_R = 0x0010 };

static uint8_t s_pixels[W * H * 4];
static uint8_t s_sram[SRAM_SIZE], s_sram_saved[SRAM_SIZE];
static char s_sram_path[1024];
static bool s_headless, s_fast, s_quit;
static uint64_t s_frame;
static double s_apu_target;
static SDL_Window *s_window;
static SDL_Renderer *s_renderer;
static SDL_Texture *s_texture;
static SDL_AudioStream *s_audio;
static SDL_Gamepad *s_gamepad;
static uint64_t s_next_tick;
static SDL_Texture *s_up;           /* the picture enlarged by a whole number (sharp scaling) */
static int s_up_k;
static double s_hz = 60.0988;       /* frames a second: the console's, or the screen's with vsync */
static bool s_vsync, s_want_vsync, s_soft;
static char s_log_path[1024];
static uint64_t s_t0, s_busy_ns, s_last_wake;    /* for the log: real time, time spent working */
static const char *s_dump_dir = "";
static FILE *s_wav;            /* --wav: the sound, raw 32 kHz stereo 16-bit */

/* ── Saved games ──────────────────────────────────────────────────────── */
static void sram_save(void)
{
  if (!memcmp(s_sram, s_sram_saved, SRAM_SIZE)) return;
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
  static const struct { const char *n; uint16_t m; } T[] = {
    { "start", B_START }, { "select", B_SELECT }, { "up", B_UP }, { "down", B_DOWN },
    { "left", B_LEFT }, { "right", B_RIGHT }, { "a", B_A }, { "b", B_B },
    { "x", B_X }, { "y", B_Y }, { "l", B_L }, { "r", B_R } };
  uint16_t m = 0;
  char buf[128]; snprintf(buf, sizeof buf, "%s", name);
  for (char *p = strtok(buf, "+,|"); p; p = strtok(NULL, "+,|"))
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) if (!strcmp(p, T[i].n)) m |= T[i].m;
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
  FILE *f = fopen(path, "wb"); if (f) { fwrite(s_pixels, 1, sizeof s_pixels, f); fclose(f); }
  snprintf(path, sizeof path, "%s%s%s.wram", s_dump_dir, *s_dump_dir ? "/" : "", tag);
  f = fopen(path, "wb"); if (f) { fwrite(g_ram, 1, sizeof g_ram, f); fclose(f); }
  /* and a picture to look at */
  snprintf(path, sizeof path, "%s%s%s.fb.ppm", s_dump_dir, *s_dump_dir ? "/" : "", tag);
  f = fopen(path, "wb");
  if (f) {
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) { uint8_t rgb[3] = { s_pixels[4 * i + 2], s_pixels[4 * i + 1], s_pixels[4 * i] }; fwrite(rgb, 1, 3, f); }
    fclose(f);
  }
  fprintf(stderr, "script f=%llu dump %s\n", (unsigned long long)s_frame, tag);
}
/* the buttons for the next frame */
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

/* ── The pads ───────────────────────────────────────────────────────────
 * The keys: keybinds.ini next to the program, the same file and format as
 * the recompilation's ([player1], a = X ...), so a player's own keys carry
 * over; written with the same defaults when it is not there. */
static struct { const char *name; uint16_t mask; SDL_Scancode key; } s_keys[] = {
  { "a", B_A, SDL_SCANCODE_X }, { "b", B_B, SDL_SCANCODE_Z }, { "x", B_X, SDL_SCANCODE_S },
  { "y", B_Y, SDL_SCANCODE_A }, { "l", B_L, SDL_SCANCODE_C }, { "r", B_R, SDL_SCANCODE_V },
  { "start", B_START, SDL_SCANCODE_RETURN }, { "select", B_SELECT, SDL_SCANCODE_RSHIFT },
  { "up", B_UP, SDL_SCANCODE_UP }, { "down", B_DOWN, SDL_SCANCODE_DOWN },
  { "left", B_LEFT, SDL_SCANCODE_LEFT }, { "right", B_RIGHT, SDL_SCANCODE_RIGHT },
};
enum { N_KEYS = sizeof s_keys / sizeof s_keys[0] };
static void trim(char *t)
{
  size_t n = strlen(t);
  while (n && (t[n - 1] == ' ' || t[n - 1] == '\t' || t[n - 1] == '\r' || t[n - 1] == '\n')) t[--n] = 0;
  size_t i = 0;
  while (t[i] == ' ' || t[i] == '\t') i++;
  if (i) memmove(t, t + i, strlen(t + i) + 1);
}
static void load_keys(void)
{
  char path[1024];
  const char *base = SDL_GetBasePath();
  snprintf(path, sizeof path, "%skeybinds.ini", base ? base : "");
  FILE *f = fopen(path, "r");
  if (!f) {
    f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "# SNES Controller Keybinds\n# Edit values to customize. Restart the game to apply.\n"
               "# Use SDL key names (A ... Z, Up, Return, Right Shift...); \"None\" leaves a button unbound.\n\n[player1]\n");
    for (int i = 0; i < N_KEYS; i++) fprintf(f, "%s = %s\n", s_keys[i].name, SDL_GetScancodeName(s_keys[i].key));
    fclose(f);
    return;
  }
  char line[256]; bool p1 = false;
  while (fgets(line, sizeof line, f)) {
    trim(line);
    if (!line[0] || line[0] == '#' || line[0] == ';') continue;
    if (line[0] == '[') { p1 = !strncmp(line, "[player1]", 9); continue; }
    char *eq = strchr(line, '=');
    if (!eq || !p1) continue;
    *eq = 0;
    char *key = line, *val = eq + 1;
    trim(key); trim(val);
    for (char *c = key; *c; c++) if (*c >= 'A' && *c <= 'Z') *c = (char)(*c + 32);
    SDL_Scancode sc = SDL_GetScancodeFromName(val);
    if (sc == SDL_SCANCODE_UNKNOWN) {
      if (!SDL_strcasecmp(val, "enter") || !SDL_strcasecmp(val, "return")) sc = SDL_SCANCODE_RETURN;
      else if (!SDL_strcasecmp(val, "rshift")) sc = SDL_SCANCODE_RSHIFT;
      else if (!SDL_strcasecmp(val, "lshift")) sc = SDL_SCANCODE_LSHIFT;
      else if (!SDL_strcasecmp(val, "space")) sc = SDL_SCANCODE_SPACE;
    }
    for (int i = 0; i < N_KEYS; i++) if (!strcmp(key, s_keys[i].name)) s_keys[i].key = sc;
  }
  fclose(f);
}
static uint16_t read_pad(void)
{
  const bool *k = SDL_GetKeyboardState(NULL);
  uint16_t m = 0;
  for (int i = 0; i < N_KEYS; i++)
    if (s_keys[i].key != SDL_SCANCODE_UNKNOWN && k[s_keys[i].key]) m |= s_keys[i].mask;
  s_fast = k[SDL_SCANCODE_TAB];
  if (s_gamepad) {
    static const struct { SDL_GamepadButton b; uint16_t m; } P[] = {
      { SDL_GAMEPAD_BUTTON_DPAD_UP, B_UP }, { SDL_GAMEPAD_BUTTON_DPAD_DOWN, B_DOWN },
      { SDL_GAMEPAD_BUTTON_DPAD_LEFT, B_LEFT }, { SDL_GAMEPAD_BUTTON_DPAD_RIGHT, B_RIGHT },
      /* the SNES layout: A on the right, B below, X above, Y on the left */
      { SDL_GAMEPAD_BUTTON_EAST, B_A }, { SDL_GAMEPAD_BUTTON_SOUTH, B_B },
      { SDL_GAMEPAD_BUTTON_NORTH, B_X }, { SDL_GAMEPAD_BUTTON_WEST, B_Y },
      { SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, B_L }, { SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, B_R },
      { SDL_GAMEPAD_BUTTON_START, B_START }, { SDL_GAMEPAD_BUTTON_BACK, B_SELECT } };
    for (size_t i = 0; i < sizeof P / sizeof P[0]; i++)
      if (SDL_GetGamepadButton(s_gamepad, P[i].b)) m |= P[i].m;
    int16_t lx = SDL_GetGamepadAxis(s_gamepad, SDL_GAMEPAD_AXIS_LEFTX);
    int16_t ly = SDL_GetGamepadAxis(s_gamepad, SDL_GAMEPAD_AXIS_LEFTY);
    if (lx < -16000) m |= B_LEFT;
    if (lx > 16000) m |= B_RIGHT;
    if (ly < -16000) m |= B_UP;
    if (ly > 16000) m |= B_DOWN;
  }
  /* never both ways at once */
  if ((m & (B_LEFT | B_RIGHT)) == (B_LEFT | B_RIGHT)) m &= (uint16_t)~(B_LEFT | B_RIGHT);
  if ((m & (B_UP | B_DOWN)) == (B_UP | B_DOWN)) m &= (uint16_t)~(B_UP | B_DOWN);
  return m;
}
static void poll_events(void)
{
  SDL_Event ev;
  while (SDL_PollEvent(&ev)) {
    switch (ev.type) {
    case SDL_EVENT_QUIT: s_quit = true; break;
    case SDL_EVENT_KEY_DOWN:
      if (ev.key.key == SDLK_ESCAPE) s_quit = true;
      if (ev.key.key == SDLK_F11 || (ev.key.key == SDLK_RETURN && (ev.key.mod & SDL_KMOD_ALT))) {
        bool full = (SDL_GetWindowFlags(s_window) & SDL_WINDOW_FULLSCREEN) != 0;
        SDL_SetWindowFullscreen(s_window, !full);
      }
      break;
    case SDL_EVENT_GAMEPAD_ADDED:
      if (!s_gamepad) s_gamepad = SDL_OpenGamepad(ev.gdevice.which);
      break;
    case SDL_EVENT_GAMEPAD_REMOVED:
      if (s_gamepad && SDL_GetGamepadID(s_gamepad) == ev.gdevice.which) { SDL_CloseGamepad(s_gamepad); s_gamepad = NULL; }
      break;
    default: break;
    }
  }
}

/* ── The end of a frame (every NMI) ───────────────────────────────────── */
static void quit_now(void)
{
  sram_save();
  if (s_log_path[0] && s_t0) {
    FILE *lg = fopen(s_log_path, "a");
    double secs = (SDL_GetTicksNS() - s_t0) / 1e9;
    if (lg) {
      fprintf(lg, "%llu frames in %.1f s: %.2f a second; %.2f ms of work a frame\n",
              (unsigned long long)s_frame, secs, secs > 0 ? s_frame / secs : 0,
              s_frame ? s_busy_ns / 1e6 / s_frame : 0);
      fclose(lg);
    }
  }
  if (s_wav) fclose(s_wav);
  if (s_audio) SDL_DestroyAudioStream(s_audio);
  SDL_Quit();
  exit(0);
}
/* The picture on the window: first enlarged by a whole number with
 * square pixels (nearest), then to the window's size smoothly. A plain
 * nearest scale to a size that is not a whole multiple (full screen,
 * a maximized window) makes some rows and columns of pixels wider than
 * others, and fine patterns (the town's bricks and roofs) shimmer as
 * they scroll; this keeps them steady and sharp. */
static void present(void)
{
  SDL_UpdateTexture(s_texture, NULL, s_pixels, W * 4);
  int ow = W, oh = H;
  SDL_GetCurrentRenderOutputSize(s_renderer, &ow, &oh);
  float scale = (float)ow / W < (float)oh / H ? (float)ow / W : (float)oh / H;
  int k = (int)scale;                  /* the whole part; the rest, smoothly */
  if (k < 1) k = 1;
  if (k > 8) k = 8;
  SDL_FRect dst = { (ow - W * scale) / 2, (oh - H * scale) / 2, W * scale, H * scale };
  if (s_soft || (float)k == scale) {   /* a whole multiple (or no graphics card): straight */
    SDL_SetRenderDrawColor(s_renderer, 0, 0, 0, 255);
    SDL_RenderClear(s_renderer);
    SDL_RenderTexture(s_renderer, s_texture, NULL, &dst);
    SDL_RenderPresent(s_renderer);
    return;
  }
  if (k != s_up_k) {
    if (s_up) SDL_DestroyTexture(s_up);
    s_up = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_TARGET, W * k, H * k);
    if (s_up) SDL_SetTextureScaleMode(s_up, SDL_SCALEMODE_LINEAR);
    s_up_k = k;
  }
  if (s_up && SDL_SetRenderTarget(s_renderer, s_up)) {
    SDL_RenderTexture(s_renderer, s_texture, NULL, NULL);
    SDL_SetRenderTarget(s_renderer, NULL);
    SDL_SetRenderDrawColor(s_renderer, 0, 0, 0, 255);
    SDL_RenderClear(s_renderer);
    SDL_RenderTexture(s_renderer, s_up, NULL, &dst);
  } else {
    SDL_SetRenderDrawColor(s_renderer, 0, 0, 0, 255);
    SDL_RenderClear(s_renderer);
    SDL_RenderTexture(s_renderer, s_texture, NULL, &dst);
  }
  SDL_RenderPresent(s_renderer);
}

static void end_of_frame(uint8_t *ram)
{
  (void)ram;
  native_draw_frame(s_pixels);
  s_frame++;
  /* the sound chip: 1,024,000 cycles a second (a frame's share: with
   * vsync on a 60 Hz screen the game runs at the screen's rate, and the
   * sound follows it, 0.16 % slower at most) */
  s_apu_target += 1024000.0 / s_hz;
  native_apu_run_to((uint64_t)s_apu_target);
  if (getenv("HM_APU_DEBUG") && s_frame % 60 == 0) fprintf(stderr, "[apu] frame %llu: target %.0f run %llu\n", (unsigned long long)s_frame, s_apu_target, (unsigned long long)native_apu_cycles());
  int16_t samples[2 * 4096];
  uint32_t n = native_audio_samples(samples, 4096);
  if (s_audio && !s_fast && n) {
    /* more than 0.2 s waiting: the screen runs slower than the sound
     * (a slow monitor, a busy computer): drop this frame's samples */
    if (SDL_GetAudioStreamQueued(s_audio) < 32000 * 4 / 5) SDL_PutAudioStreamData(s_audio, samples, (int)(n * 4));
  }
  if (s_wav && n) fwrite(samples, 4, n, s_wav);

  uint16_t pad = 0;
  if (!s_headless) {
    poll_events();
    pad = read_pad();
    present();
  }
  { static long d0 = -1, d1 = -1;                     /* HM_DUMP_FRAMES=a:b: every frame's picture and WRAM */
    if (d0 < 0) { const char *e = getenv("HM_DUMP_FRAMES"); d0 = 0; if (e) sscanf(e, "%ld:%ld", &d0, &d1); }
    if ((long)s_frame >= d0 && (long)s_frame <= d1) { char t[32]; snprintf(t, sizeof t, "fr%06ld", (long)s_frame); dump(t); } }
  if (s_nsteps) pad |= script_tick();
  native_set_pads(pad, 0);
  if (s_frame % 60 == 0) sram_save();
  if (s_quit) quit_now();

  if (!s_headless) {
    if (s_fast) { s_next_tick = 0; if (s_audio) SDL_ClearAudioStream(s_audio); return; }
    uint64_t woke = SDL_GetTicksNS();
    if (s_last_wake) s_busy_ns += woke - s_last_wake;
    if (!s_t0) s_t0 = woke;
    if (s_vsync) { s_last_wake = SDL_GetTicksNS(); return; }   /* --vsync: the screen's refresh paces the frames */
    /* an even 60.0988 frames a second from the clock, never nudged, so
     * movement stays smooth */
    const uint64_t frame_ns = 16639267;
    uint64_t now = SDL_GetTicksNS();
    if (s_next_tick == 0 || now > s_next_tick + 4 * frame_ns) s_next_tick = now;
    s_next_tick += frame_ns;
    now = SDL_GetTicksNS();
    if (s_next_tick > now) SDL_DelayPrecise(s_next_tick - now);
    s_last_wake = SDL_GetTicksNS();
  }
}

/* a frame that ends inside a wait for the sound chip (hw.c) */
static HmRegs s_regs;
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
  static const struct { uint32_t pc; Fn *fn; } T[] = {
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
  for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) if (T[i].pc == pc) return T[i].fn;
  return NULL;
}

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

int main(int argc, char **argv)
{
  const char *rom_path = NULL, *script = NULL;
  int scale = 3; bool fullscreen = false;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--script") && i + 1 < argc) script = argv[++i];
    else if (!strcmp(argv[i], "--headless")) s_headless = true;
    else if (!strcmp(argv[i], "--dump-dir") && i + 1 < argc) s_dump_dir = argv[++i];
    else if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--fullscreen")) fullscreen = true;
    else if (!strcmp(argv[i], "--vsync")) s_want_vsync = true;
    else if (!strcmp(argv[i], "--wav") && i + 1 < argc) s_wav = fopen(argv[++i], "wb");
    else if (argv[i][0] != '-') rom_path = argv[i];
  }
  static const char *names[] = { "rom.sfc", "harvestmoon.sfc", "harvestmoon.smc", "Harvest Moon (USA).sfc" };
  uint32_t rom_size = 0; uint8_t *rom = NULL;
  if (rom_path) rom = load_rom(rom_path, &rom_size);
  else {
    /* in the current folder, then next to the program */
    static char path[1024];
    const char *base = SDL_GetBasePath();
    for (int where = 0; where < 2 && !rom; where++)
      for (size_t i = 0; i < sizeof names / sizeof names[0] && !rom; i++) {
        snprintf(path, sizeof path, "%s%s", where && base ? base : "", names[i]);
        rom = load_rom(rom_path = path, &rom_size);
      }
  }
  if (!rom) {
    const char *msg = "Harvest Moon: no ROM. Put your rom.sfc next to the program, or pass its path.";
    fprintf(stderr, "%s\n", msg);
    if (!s_headless) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Harvest Moon", msg, NULL);
    return 1;
  }
  snprintf(s_sram_path, sizeof s_sram_path, "%s", rom_path);
  char *dot = strrchr(s_sram_path, '.');
  if (dot && !strchr(dot, '/') && !strchr(dot, '\\')) *dot = 0;
  strncat(s_sram_path, ".srm", sizeof s_sram_path - strlen(s_sram_path) - 1);
  FILE *f = fopen(s_sram_path, "rb");
  if (f) { size_t got = fread(s_sram, 1, SRAM_SIZE, f); (void)got; fclose(f); }
  memcpy(s_sram_saved, s_sram, SRAM_SIZE);
  if (script) script_load(script);

  if (!native_hw_init(rom, rom_size, s_sram, SRAM_SIZE)) { fprintf(stderr, "out of memory\n"); return 1; }

  if (!s_headless) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
      fprintf(stderr, "SDL: %s\n", SDL_GetError());
      return 1;
    }
    if (!SDL_CreateWindowAndRenderer("Harvest Moon", W * scale, H * scale, SDL_WINDOW_RESIZABLE,
                                     &s_window, &s_renderer)) {
      fprintf(stderr, "SDL: %s\n", SDL_GetError());
      return 1;
    }
    /* the pace: our own clock; with --vsync on a 60 Hz screen, the
     * screen's refresh (smoother there, but a frame that misses it waits
     * a whole refresh) */
    const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(s_window));
    float hz = mode ? mode->refresh_rate : 0;
    if (s_want_vsync && hz >= 58.5f && hz <= 61.5f && SDL_SetRenderVSync(s_renderer, 1)) { s_vsync = true; s_hz = hz; }
    else SDL_SetRenderVSync(s_renderer, 0);
    const char *rname = SDL_GetRendererName(s_renderer);
    s_soft = rname && !strcmp(rname, "software");
    /* what was chosen, next to the program (for reporting problems) */
    const char *base = SDL_GetBasePath();
    snprintf(s_log_path, sizeof s_log_path, "%sHarvestMoonNative.log", base ? base : "");
    FILE *lg = fopen(s_log_path, "w");
    if (lg) {
      fprintf(lg, "renderer %s, screen %.2f Hz, vsync %s, rom %s\n", rname ? rname : "?", hz,
              s_vsync ? "on" : "off", rom_path);
      fclose(lg);
    }
    load_keys();
    s_texture = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, W, H);
    SDL_SetTextureScaleMode(s_texture, SDL_SCALEMODE_NEAREST);
    if (fullscreen) SDL_SetWindowFullscreen(s_window, true);
    SDL_AudioSpec spec = { SDL_AUDIO_S16, 2, 32000 };
    s_audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (s_audio) {
      static int16_t quiet[2 * 1600];                   /* 50 ms of silence first: room for the pace to wobble */
      SDL_PutAudioStreamData(s_audio, quiet, sizeof quiet);
      SDL_ResumeAudioStreamDevice(s_audio);
    }
    else fprintf(stderr, "no sound: %s\n", SDL_GetError());
  }

  g_hm_frame_hook = end_of_frame;
  g_native_lag_frame = lag_frame;
  s_regs.s = 0x1EFC; s_regs.p = 0x34; s_regs.pb = 0x00; s_regs.db = 0x00;
  if (s_nsteps) native_set_pads(script_tick(), 0);
  hm_RESET(g_ram, &s_regs);
  s_regs.s = 0x1F00;   /* RESET's stack (the rewrite gives the checker's own back) */
  for (;;) {
    uint32_t pc = g_hm_next;
    Fn *fn = routine_at(pc);
    if (!fn) {
      fprintf(stderr, "native: the game went to $%06X, which this build does not know\n", (unsigned)pc);
      sram_save();
      return 3;
    }
    g_hm_next = 0;
    fn(g_ram, &s_regs);
  }
}
