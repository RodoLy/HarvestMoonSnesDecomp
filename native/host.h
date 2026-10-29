/* host.h — what the launcher (launcher.c) asks of the program (main.c). */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <SDL3/SDL.h>

/* One frame of input for a menu. */
typedef struct {
  uint16_t pressed;          /* B_* newly pressed this frame (directions repeat when held) */
  uint16_t held;
  bool accept, back;         /* Enter / Space, Escape / Backspace (and the pad's A / B) */
  SDL_Scancode key;          /* a key pressed this frame (for setting the keys), or UNKNOWN */
  int pad_button;            /* a gamepad button pressed this frame, or -1 */
  int mouse_x, mouse_y;      /* on the menu's canvas; -1 when outside */
  bool mouse_moved, click, rclick;
  int wheel;                 /* + up, - down */
} UiInput;

void host_apply_video(void);           /* g_cfg's window size, full screen, vsync, picture */
void host_apply_sound(void);           /* g_cfg's volume */
void host_apply_language(void);
int host_gamepad_count(void);
const char *host_gamepad_name(int i);
int host_gamepad_current(void);        /* -1: none */
void host_gamepad_select(int i);
bool host_gamepad_down(SDL_GamepadButton b);
const char *host_gamepad_label(SDL_GamepadButton b);   /* "A", "Cross", "LB"... */
void host_rom_dialog(void);             /* the system's "open file" window */
bool host_rom_dialog_result(char *out, size_t n);
bool host_running(void);               /* a game is going (not before the first Play) */
const char *host_rom(void);            /* the ROM being played, or "" */
bool host_state_info(int slot, char *when, size_t n);
void host_toast(const char *text);
