/* textbox.h — the game's own text box, drawn by the host (textbox.c):
 * the frame, the letters in the game's font coming one by one with the
 * game's letter sound. Used when someone the game does not know about
 * talks: the wife's offer (wife.c), the Goddess (goddess.c). */
#pragma once
#include <stdbool.h>

#include "host.h"

enum { TEXTBOX_LINES = 3, TEXTBOX_COLUMNS = 27 };   /* what fits in the box */

/* the box, 240 x 64 at (8, 151) of the 256 columns that start at `ox` */
void textbox_frame(int ox);
/* text at (x, y), at most *left letters of it (*left goes down by those drawn) */
void textbox_text(int x, int y, const char *s, int *left);
/* one of the box's three lines (0-2) */
void textbox_line(int ox, int line, const char *s, int *left);
/* the arrow the game blinks in the corner when there is more to read */
void textbox_more(int ox, unsigned frame);
int textbox_letters(const char *s);

/* The printer: one letter every 4 frames, every frame while A or B is held
 * (or with fast text on), each with the letter sound. Returns true once
 * `total` letters are shown. */
typedef struct { int shown, tick; } TextboxPrinter;
bool textbox_print(TextboxPrinter *p, int total, const UiInput *in);

/* A talk: up to 3 pages of up to 3 lines, each line as the box shows it
 * (no wrapping is done; TEXTBOX_COLUMNS letters at most). A turns the page
 * once it is all printed; after the last page the talk is over. */
typedef struct {
  const char *lines[3][TEXTBOX_LINES];
  int pages, page;
  TextboxPrinter printer;
  unsigned frame;
} TextboxTalk;
void textbox_talk_open(TextboxTalk *t);
bool textbox_talk_update(TextboxTalk *t, const UiInput *in);   /* true: over */
void textbox_talk_draw(TextboxTalk *t, uint32_t *dst, int w);
