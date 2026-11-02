/* second.h — a second screen (the AYN Thor's lower one): the pause menu,
 * live, used by touch. second.c; the Android side is HarvestMoonActivity /
 * SecondScreen (android/app/src/main/java). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

void second_init(void);
void second_apply(void);       /* g_cfg.second_screen changed */
/* once a frame (also while a menu is open): the touches in, the picture out */
void second_frame(void);
bool second_shown(void);       /* a second screen is showing the menu */
bool second_available(void);   /* the device has another screen (the option shows only then) */
/* tests (HM_SECOND_SHOT): the picture it would show */
void second_render(uint32_t *dst);
