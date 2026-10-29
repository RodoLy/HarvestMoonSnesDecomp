/* pause.h — the Harvest Moon 64-style pause menu (pause.c). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

bool pause_can_open(void);          /* a diary is loaded */
void pause_open(void);
bool pause_is_open(void);
/* u d l r (move), L R (pages), A X Y, B (close) */
void pause_action(char a);
void pause_draw(uint32_t *dst, int dst_w);
