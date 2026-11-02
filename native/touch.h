/* touch.h — on-screen controls for touch screens (phones, tablets, the AYN
 * Thor), drawn in the look of the game's menus; and touches on the field as
 * the mouse (mouse controls). touch.c. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include <SDL3/SDL.h>

enum { TOUCH_AUTO, TOUCH_ALWAYS, TOUCH_OFF };   /* g_cfg.touch */

/* a touch on the field for the mouse controls: window pixels; x < 0: the
 * pointer goes away; button 0 a move, 1 left, 2 right */
typedef void (*TouchFieldFn)(float x, float y, int button, bool down);

void touch_init(bool gamepad_present, TouchFieldFn field);
/* a finger event while the game runs (not in the menus); `game`: where the
 * picture is on the window's output (pixels); out_w/out_h the output's size */
void touch_event(const SDL_Event *ev, int win_w, int win_h, int out_w, int out_h, SDL_FRect game);
void touch_frame(void);              /* once a frame: long presses, the pointer going */
uint16_t touch_pad(void);            /* the game's buttons held on the screen */
bool touch_bag(void);                /* the crop bag's key held */
bool touch_fast(void);               /* fast forward held */
int touch_request(void);             /* once: 1 the pause menu, 2 the launcher */
void touch_input_seen(uint16_t pad); /* keys or a gamepad used: the controls hide (auto) */
void touch_release_all(void);        /* a menu opened: every finger let go */
bool touch_visible(void);
/* over the picture, before it is presented */
void touch_draw(SDL_Renderer *r, int out_w, int out_h, SDL_FRect game);
