/*
 * menus.c — the buttons in menus (bank $84).
 *
 * InputTypeSelector ($84:C034) reads the input mode $019A each frame:
 * 1 walking, 2 no movement, 3 a list in a text box, 4 menus, 5 the name
 * screen, 6 another. In mode 4, MenuInputs looks at the menu $95:
 *   1    playing (the player's buttons, and Start opens the pause menu)
 *   2, 3 (Start: $95 = 6)
 *   5    the title menu: three options in $098D (0 load, 1 save..., 2 ...)
 *   7    choose a slot to save: $098E = the slot
 *   8    choose a slot to load: only a slot with a game can be taken
 * Each answer is in $94 (1 = B, cancel; 2 = A, accept). Every move makes a
 * sound effect (3, 6, $0A: move; 1: accept; 2: refused).
 *
 * Buttons (Joy1, $0128 = pressed this frame, $012C = with auto-repeat):
 * $8000 B, $1000 Start, $0800 up, $0400 down, $0200 left, $0100 right,
 * $0080 A.
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

#define JOY_NEW     0x0128
#define JOY_REPEAT  0x012C
#define BTN_B       0x8000
#define BTN_START   0x1000
#define BTN_UP      0x0800
#define BTN_DOWN    0x0400
#define BTN_LEFT    0x0200
#define BTN_RIGHT   0x0100
#define BTN_A       0x0080
#define LIST_TEXTS  0x839467   /* DATA8_839467: the text of each of 11 places */

static uint32_t db_addr(const HmRegs *r, uint16_t a) { return (uint32_t)r->db << 16 | a; }
static uint8_t rd8(uint8_t *ram, const HmRegs *r, uint16_t a) { return hm_bus_rd8(ram, db_addr(r, a)); }
static uint16_t rd16(uint8_t *ram, const HmRegs *r, uint16_t a) { return hm_bus_rd16(ram, db_addr(r, a)); }
static void wr8(uint8_t *ram, const HmRegs *r, uint16_t a, uint8_t v) { hm_bus_wr8(ram, db_addr(r, a), v); }
static void set_a8(HmRegs *r, uint8_t v) { r->a = (uint16_t)((r->a & 0xFF00) | v); }
static void jsl83(uint8_t *ram, HmRegs *r, uint16_t ret, void (*fn)(uint8_t *, HmRegs *))
{
  hm_jsl(ram, r, ret, 0x83);
  fn(ram, r);
  hm_rtl(ram, r);
}
/* $0114 = sound, $0115 = 6, UNK_Audio19 (effect sound, 6, $0A) */
static void beep(uint8_t *ram, HmRegs *r, uint8_t sound, uint16_t ret)
{
  wr8(ram, r, 0x0114, sound);
  set_a8(r, 6);
  wr8(ram, r, 0x0115, 6);
  jsl83(ram, r, ret, hm_UNK_Audio19);
}
/* LDA Joy; BIT #button (A = the buttons) */
static bool pressed(uint8_t *ram, HmRegs *r, uint16_t at, uint16_t button)
{
  r->a = rd16(ram, r, at);
  return r->a & button;
}
/* the slot menus: up or down switch the slot ($098E 0 <-> 1), $97 = 1
 * (redraw) */
static void other_slot(uint8_t *ram, HmRegs *r, uint16_t ret)
{
  wr8(ram, r, 0x098E, (uint8_t)(rd8(ram, r, 0x098E) ^ 0x01));
  set_a8(r, 1);
  *hm_dp(ram, r, 0x97) = 1;
  beep(ram, r, 3, ret);
}
static void answer(uint8_t *ram, HmRegs *r, uint8_t which, uint8_t sound, uint16_t ret)
{
  set_a8(r, which);
  *hm_dp(ram, r, 0x94) = which;
  beep(ram, r, sound, ret);
}

/* ─────────────────────────────────────────────────────────────────────────
 * SaveSlotMenuInputs ($84:C2FE, RTL; CODE_84C2FE, menu 7): up / down
 * change the slot, B cancels ($94 = 1), A accepts ($94 = 2).
 * Leftovers: A = the buttons if none was pressed; otherwise those of
 * UNK_Audio19.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_SaveSlotMenuInputs(uint8_t *ram, HmRegs *r)
{
  if (pressed(ram, r, JOY_NEW, BTN_DOWN) || pressed(ram, r, JOY_NEW, BTN_UP)) other_slot(ram, r, 0xC34C);
  else if (pressed(ram, r, JOY_NEW, BTN_B)) answer(ram, r, 1, 3, 0xC365);
  else if (pressed(ram, r, JOY_NEW, BTN_A)) answer(ram, r, 2, 1, 0xC37E);
}

/* ─────────────────────────────────────────────────────────────────────────
 * LoadSlotMenuInputs ($84:C380, RTL; CODE_84C380, menu 8): the same, but
 * A only accepts a slot with a game in it (LoadGameSlotResume answers
 * X = 1); an empty one gives the "refused" sound (2).
 * ───────────────────────────────────────────────────────────────────────── */
void hm_LoadSlotMenuInputs(uint8_t *ram, HmRegs *r)
{
  if (pressed(ram, r, JOY_NEW, BTN_DOWN) || pressed(ram, r, JOY_NEW, BTN_UP)) { other_slot(ram, r, 0xC3CE); return; }
  if (pressed(ram, r, JOY_NEW, BTN_B)) { answer(ram, r, 1, 3, 0xC3E7); return; }
  if (!pressed(ram, r, JOY_NEW, BTN_A)) return;
  r->a = rd8(ram, r, 0x098E);                         /* LDA #0; XBA; LDA $098E */
  jsl83(ram, r, 0xC3F4, hm_LoadGameSlotResume);
  r->c = 1;                                           /* CPX #0 */
  if (r->x != 0) answer(ram, r, 2, 1, 0xC413);
  else beep(ram, r, 2, 0xC426);
}

/* ─────────────────────────────────────────────────────────────────────────
 * StartMenuInputs ($84:C428, RTL; CODE_84C428, menus 2 and 3): with
 * $7F1F5A bit $8000, Start goes to menu 6.
 * Leftovers: A = $7F1F5A & $8000 or the buttons; else UNK_Audio19's.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_StartMenuInputs(uint8_t *ram, HmRegs *r)
{
  r->a = hm_bus_rd16(ram, 0x7F1F5A) & 0x8000;
  if (!r->a) return;
  if (!pressed(ram, r, JOY_NEW, BTN_START)) return;
  set_a8(r, 6);
  *hm_dp(ram, r, 0x95) = 6;
  beep(ram, r, 3, 0xC456);
}

/* ─────────────────────────────────────────────────────────────────────────
 * TitleMenuInputs ($84:C458, RTL; CODE_84C458, menu 5): three options in
 * $098D; down / up move (wrapping around), $90 = 0, $97 = 1 (redraw). A
 * opens the next menu: option 0 -> 8 (load a game), 1 -> 7, 2 -> $0A.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_TitleMenuInputs(uint8_t *ram, HmRegs *r)
{
  if (pressed(ram, r, JOY_NEW, BTN_DOWN)) {
    hm_dp_w16(ram, r, 0x90, 0);
    uint8_t at = rd8(ram, r, 0x098D);
    r->c = at >= 2;                                   /* CMP #2 */
    uint8_t next = at == 2 ? 0 : (uint8_t)(at + 1);
    wr8(ram, r, 0x098D, next);
    set_a8(r, 1);
    *hm_dp(ram, r, 0x97) = 1;
    beep(ram, r, 3, at == 2 ? 0xC4C2 : 0xC4A2);
  } else if (pressed(ram, r, JOY_NEW, BTN_UP)) {
    hm_dp_w16(ram, r, 0x90, 0);
    uint8_t at = rd8(ram, r, 0x098D);
    wr8(ram, r, 0x098D, at == 0 ? 2 : (uint8_t)(at - 1));
    set_a8(r, 1);
    *hm_dp(ram, r, 0x97) = 1;
    beep(ram, r, 3, at == 0 ? 0xC50A : 0xC4E8);
  } else if (pressed(ram, r, JOY_NEW, BTN_A)) {
    uint8_t at = rd8(ram, r, 0x098D);
    if (at != 0) r->c = at >= 1;                      /* CMP #1 */
    uint8_t menu = at == 0 ? 0x08 : at == 1 ? 0x07 : 0x0A;
    set_a8(r, menu);
    *hm_dp(ram, r, 0x95) = menu;
    beep(ram, r, 1, 0xC53E);
  }
}

/* ─────────────────────────────────────────────────────────────────────────
 * ListInputs ($84:C540, RTL; CODE_84C540, input mode 3): a list of 22
 * places in a text box, two columns of 11 ($018A = the place; the arrow is
 * drawn at it). Down / up move in the column (wrapping 0 <-> 10), right /
 * left jump 11 (to the other column); these use the auto-repeat and first
 * erase the arrow (DrawTextBoxArrow frame 1). A closes the list: in the
 * first column it starts that place's text (DATA8_839467) in a box of
 * one line and goes back to "no movement" (mode 2); in the second it
 * just erases the arrow and goes back to walking (mode 1). Either way
 * BG2 scrolls back ($0142 = $100).
 * Note: the second column only works with right / left; up / down do
 * nothing there.
 * ───────────────────────────────────────────────────────────────────────── */
static bool erase_arrow_if(uint8_t *ram, HmRegs *r, bool first_column, uint16_t ret)
{
  uint8_t at = rd8(ram, r, 0x018A);
  set_a8(r, at);
  r->c = at >= 0x0B;
  if (first_column ? at >= 0x0B : at < 0x0B) return false;
  r->a = 1;
  jsl83(ram, r, ret, hm_DrawTextBoxArrow);
  return true;
}
static void moved(uint8_t *ram, HmRegs *r, uint8_t to)
{
  wr8(ram, r, 0x018A, to);
  set_a8(r, to);
  wr8(ram, r, 0x018B, 0);
}

void hm_ListInputs(uint8_t *ram, HmRegs *r)
{
  if (pressed(ram, r, JOY_REPEAT, BTN_DOWN)) {
    beep(ram, r, 3, 0xC5E2);
    if (!erase_arrow_if(ram, r, true, 0xC5F4)) return;
    uint8_t at = rd8(ram, r, 0x018A);
    r->c = at >= 0x0A;
    if (at == 0x0A) { set_a8(r, 0); wr8(ram, r, 0x018A, 0); }
    else moved(ram, r, (uint8_t)(at + 1));
  } else if (pressed(ram, r, JOY_REPEAT, BTN_UP)) {
    beep(ram, r, 3, 0xC623);
    if (!erase_arrow_if(ram, r, true, 0xC635)) return;
    uint8_t at = rd8(ram, r, 0x018A);
    if (at == 0) { set_a8(r, 0x0A); wr8(ram, r, 0x018A, 0x0A); }
    else moved(ram, r, (uint8_t)(at - 1));
  } else if (pressed(ram, r, JOY_REPEAT, BTN_RIGHT)) {
    beep(ram, r, 3, 0xC662);
    if (!erase_arrow_if(ram, r, true, 0xC674)) return;
    r->c = 0;                                         /* CLC; ADC #$0B */
    moved(ram, r, (uint8_t)(rd8(ram, r, 0x018A) + 0x0B));
  } else if (pressed(ram, r, JOY_REPEAT, BTN_LEFT)) {
    beep(ram, r, 3, 0xC695);
    if (!erase_arrow_if(ram, r, false, 0xC6A7)) return;
    r->c = 1;                                         /* SEC; SBC #$0B */
    moved(ram, r, (uint8_t)(rd8(ram, r, 0x018A) - 0x0B));
  } else if (pressed(ram, r, JOY_NEW, BTN_A)) {
    beep(ram, r, 1, 0xC58B);
    wr8(ram, r, 0x019B, 0);
    wr8(ram, r, 0x019A, 2);
    uint8_t at = rd8(ram, r, 0x018A);
    r->a = at;                                        /* LDA #0; XBA; LDA $018A */
    r->c = at >= 0x0B;
    if (at >= 0x0B) {
      wr8(ram, r, 0x019A, 1);
      r->a = 1;
      jsl83(ram, r, 0xC5B1, hm_DrawTextBoxArrow);
    } else {
      r->x = hm_bus_rd16(ram, LIST_TEXTS + 2u * at);
      r->a = (uint16_t)(r->x & 0xFF00);               /* LDA.L ...; TAX; SEP; LDA #0 */
      wr8(ram, r, 0x0191, 0);
      jsl83(ram, r, 0xC5C7, hm_StartTextBox);
    }
    r->a = 0x0100;
    hm_bus_wr16(ram, db_addr(r, 0x0142), 0x0100);
  }
}
