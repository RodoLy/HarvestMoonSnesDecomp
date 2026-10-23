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

/* ═════════════════════════════════════════════════════════════════════════
 * ImmobileInputs ($84:CF05, RTL; input mode 2): the buttons while a text
 * box is open ($019B says in which state):
 *   bit 1, printing: holding A ($0124, "held") makes the next letter come
 *          at once ($0189 = 4);
 *   bit 2 (the end) or bit 5 (the arrow): A closes the box and the player
 *          can walk again (mode 1); the A press is used up ($0128 bit 7
 *          cleared);
 *   bit 3, waiting: A wipes the box and goes on with the next code (the
 *          cursor back to VRAM $5010, bit 3 cleared);
 *   bit 4, a choice: $018F = the option, $018E = the last one. The options
 *          are in two columns of three (0-2, 3-5); down / up move in the
 *          column, right / left jump to the other one; A closes the box.
 *          Before moving, the cursor is erased (letter $B1 at $0185).
 * ═════════════════════════════════════════════════════════════════════════ */
#define JOY_HELD     0x0124

static void erase_cursor(uint8_t *ram, HmRegs *r, uint16_t ret)
{
  r->x = 1;
  r->a = 0x00B1;
  jsl83(ram, r, ret, hm_DrawLetter);
}
static void choose(uint8_t *ram, HmRegs *r, uint8_t option)
{
  wr8(ram, r, 0x018F, option);
  set_a8(r, option);
}

static void choice_down(uint8_t *ram, HmRegs *r)
{
  beep(ram, r, 3, 0xCFFB);
  erase_cursor(ram, r, 0xD007);
  uint8_t at = rd8(ram, r, 0x018F), last = rd8(ram, r, 0x018E);
  set_a8(r, at);
  r->c = at >= 3;
  if (at >= 3) {
    r->c = at >= last;
    choose(ram, r, at == last ? (uint8_t)(at - 1) : (uint8_t)(at + 1));
  } else {
    r->c = at >= last;
    if (at != last) r->c = at >= 2;
    if (at == last || at == 2) wr8(ram, r, 0x018F, 0);
    else choose(ram, r, (uint8_t)(at + 1));
  }
  wr8(ram, r, 0x018B, 0);
}

static void choice_up(uint8_t *ram, HmRegs *r)
{
  beep(ram, r, 3, 0xD04B);
  erase_cursor(ram, r, 0xD057);
  uint8_t at = rd8(ram, r, 0x018F), last = rd8(ram, r, 0x018E);
  set_a8(r, at);
  if (at == 0) {                                     /* to the bottom of the column */
    set_a8(r, last);
    r->c = last >= 3;
    choose(ram, r, last >= 3 ? 2 : last);
  } else if (at == 3) {
    r->c = 1;
    set_a8(r, last);
    r->c = last >= 3;
    if (last != 3) choose(ram, r, 4);                /* LDA $018F; INC */
  } else {
    r->c = at >= 3;
    choose(ram, r, (uint8_t)(at - 1));
  }
  wr8(ram, r, 0x018B, 0);
}

static void choice_across(uint8_t *ram, HmRegs *r, uint16_t ret_beep, uint16_t ret_erase)
{
  beep(ram, r, 3, ret_beep);
  uint8_t last = rd8(ram, r, 0x018E);
  set_a8(r, last);
  r->c = last >= 3;
  if (last >= 3) {
    uint8_t at = rd8(ram, r, 0x018F);
    set_a8(r, at);
    r->c = at >= 2;
    if (at != 2) {
      erase_cursor(ram, r, ret_erase);
      at = rd8(ram, r, 0x018F);
      set_a8(r, at);
      r->c = at >= 2;
      if (at >= 2) { r->c = at >= 3; choose(ram, r, (uint8_t)(at - 3)); }
      else if (at == 1) {
        r->c = 1;
        set_a8(r, last);
        r->c = last >= 3;
        if (last != 3) { r->c = 0; choose(ram, r, 4); }
      } else {
        r->c = 0;
        choose(ram, r, 3);
      }
    }
  }
  wr8(ram, r, 0x018B, 0);
}

void hm_ImmobileInputs(uint8_t *ram, HmRegs *r)
{
  uint8_t box = rd8(ram, r, 0x019B);
  set_a8(r, box & 0x02);
  if (box & 0x02) {                                   /* printing: A held = faster */
    if (!pressed(ram, r, JOY_HELD, BTN_A)) return;
    set_a8(r, 4);
    wr8(ram, r, 0x0189, 4);
    return;
  }
  set_a8(r, box & 0x04);
  bool close = box & 0x04;
  if (!close) {
    set_a8(r, box & 0x08);
    if (box & 0x08) {                                 /* waiting: A = go on */
      if (!pressed(ram, r, JOY_NEW, BTN_A)) return;
      jsl83(ram, r, 0xCF70, hm_SUB_83932D);
      hm_bus_wr16(ram, db_addr(r, 0x0187), (uint16_t)(rd16(ram, r, 0x0187) + 1));
      hm_bus_wr16(ram, db_addr(r, 0x0185), 0x5010);
      r->c = 0;                                       /* CLC; ADC #$10 */
      wr8(ram, r, 0x018B, 0);
      uint8_t flags = (uint8_t)(rd8(ram, r, 0x019B) & 0xF7);
      wr8(ram, r, 0x019B, flags);
      r->a = (uint16_t)(0x5000 | flags);
      wr8(ram, r, 0x0189, 0);
      return;
    }
    set_a8(r, box & 0x10);
    if (box & 0x10) {                                 /* a choice */
      if (pressed(ram, r, JOY_NEW, BTN_DOWN))       choice_down(ram, r);
      else if (pressed(ram, r, JOY_NEW, BTN_UP))    choice_up(ram, r);
      else if (pressed(ram, r, JOY_NEW, BTN_RIGHT)) choice_across(ram, r, 0xD09F, 0xD0BB);
      else if (pressed(ram, r, JOY_NEW, BTN_LEFT))  choice_across(ram, r, 0xD105, 0xD121);
      else if (pressed(ram, r, JOY_NEW, BTN_A)) {
        beep(ram, r, 1, 0xCFDD);
        jsl83(ram, r, 0xCFE1, hm_CloseTextBox);
        set_a8(r, 1);
        wr8(ram, r, 0x019A, 1);
      }
      return;
    }
    set_a8(r, box & 0x20);
    close = box & 0x20;
    if (!close) return;
  }
  if (!pressed(ram, r, JOY_NEW, BTN_A)) return;       /* the end: A closes */
  jsl83(ram, r, 0xCF4E, hm_CloseTextBox);
  set_a8(r, 1);
  wr8(ram, r, 0x019A, 1);
  r->a = (uint16_t)(rd16(ram, r, JOY_NEW) & 0xFF7F);
  hm_bus_wr16(ram, db_addr(r, JOY_NEW), r->a);
}

/* ═════════════════════════════════════════════════════════════════════════
 * The name screen (input mode 5). The letters are a grid; NameCursorPharse
 * reads the cell under the cursor ($0991) in the grid of the page shown
 * ($0993: 0 $82:EBF8, 1 $82:EE30, else $82:F0D0). Each cell has 8 bytes:
 * x, y, the cell reached with each arrow (down, up, and the two others),
 * the letter, and a code: 1-3 switch to page 0-2, 4 "done".
 * The name being typed is at $0885 (4 letters, $B1 = empty), $0994 how many.
 * ═════════════════════════════════════════════════════════════════════════ */
#define NAME_PAGE   0x0993
#define NAME_CELL   0x0991
#define NAME_COUNT  0x0994
#define NAME        0x0885

/* NameCursorPharse ($82:EBAC, JSL, RTL): A = which byte of the cell (0-5:
 * the arrows, the letter, the code) -> A = that byte. */
void hm_NameCursorPharse(uint8_t *ram, HmRegs *r)
{
  uint16_t which = r->a;
  hm_push8(ram, r, (uint8_t)(which >> 8));                 /* PHA */
  hm_push8(ram, r, (uint8_t)which);
  uint16_t grid = 0xEBF8;
  uint8_t page = rd8(ram, r, NAME_PAGE);
  r->a = (uint16_t)((0xEB00) | page);                      /* LDA #$82 ... LDA $0993 */
  if (page != 0) {
    r->c = page >= 1;
    grid = page == 1 ? 0xEE30 : 0xF0D0;
  }
  hm_dp_w16(ram, r, 0x72, grid);
  *hm_dp(ram, r, 0x74) = 0x82;
  uint16_t lo = hm_pull8(ram, r);
  which = (uint16_t)(lo | hm_pull8(ram, r) << 8);
  hm_dp_w16(ram, r, 0x7E, (uint16_t)(which + 2));
  uint32_t s = (uint32_t)(uint16_t)(rd16(ram, r, NAME_CELL) << 3) + (uint16_t)(which + 2);
  r->c = s > 0xFFFF;
  r->y = (uint16_t)s;
  r->a = hm_bus_rd8(ram, hm_dp_long(ram, r, 0x72, r->y));  /* LDA #0; XBA; LDA [$72],Y */
}

/* SUB_82EB57 ($82:EB57, JSL, RTL): the cursor's place on the name screen:
 * the first two bytes of the current cell (x, y) into $099B / $099D.
 * Leftovers: $72-$74 = the page's grid, A = y, Y = 8 x cell + 1, C = bit
 * 13 of the cell number (the last ASL). */
void hm_SUB_82EB57(uint8_t *ram, HmRegs *r)
{
  uint16_t grid = 0xEBF8;
  uint8_t page = rd8(ram, r, NAME_PAGE);
  if (page != 0) grid = page == 1 ? 0xEE30 : 0xF0D0;
  hm_dp_w16(ram, r, 0x72, grid);
  *hm_dp(ram, r, 0x74) = 0x82;
  uint16_t cell = rd16(ram, r, NAME_CELL);
  r->c = (cell >> 13) & 1;                                 /* ASL x 3 */
  r->y = (uint16_t)(cell << 3);
  r->a = hm_bus_rd8(ram, hm_dp_long(ram, r, 0x72, r->y));
  hm_bus_wr16(ram, db_addr(r, 0x099B), r->a);
  r->y++;
  r->a = hm_bus_rd8(ram, hm_dp_long(ram, r, 0x72, r->y));
  hm_bus_wr16(ram, db_addr(r, 0x099D), r->a);
}

/* Unk_OutOfMenu ($82:EA60, JSL, RTL): draw letter A at the name's place
 * $0994 (the letter cursor $0185 from DATA16_82EB4D: $5010, $5020...). */
void hm_Unk_OutOfMenu(uint8_t *ram, HmRegs *r)
{
  uint16_t letter = r->a;
  hm_push8(ram, r, (uint8_t)(letter >> 8));
  hm_push8(ram, r, (uint8_t)letter);
  uint16_t i = rd8(ram, r, NAME_COUNT);
  r->c = (i << 1) > 0xFFFF;
  r->x = (uint16_t)(i << 1);
  r->a = hm_bus_rd16(ram, 0x82EB4Du + r->x);
  hm_bus_wr16(ram, 0x800185, r->a);
  uint16_t lo = hm_pull8(ram, r);
  r->a = (uint16_t)(lo | hm_pull8(ram, r) << 8);
  r->x = 0x0001;
  hm_jsl(ram, r, 0xEA7E, 0x83);
  hm_DrawLetter(ram, r);
  hm_rtl(ram, r);
}

/* the arrows: move the cursor to the cell the grid says */
static void name_move(uint8_t *ram, HmRegs *r, uint16_t which, uint16_t ret1, uint16_t ret2)
{
  r->a = which;
  hm_jsl(ram, r, ret1, 0x82);
  hm_NameCursorPharse(ram, r);
  hm_rtl(ram, r);
  hm_bus_wr16(ram, db_addr(r, NAME_CELL), r->a);
  beep(ram, r, 3, ret2);
}
void hm_NameMenuKeyDown(uint8_t *ram, HmRegs *r)  { name_move(ram, r, 0, 0xC20C, 0xC223); }
void hm_NameMenuKeyUp(uint8_t *ram, HmRegs *r)    { name_move(ram, r, 1, 0xC22D, 0xC244); }
void hm_NameMenuKeyLeft(uint8_t *ram, HmRegs *r)  { name_move(ram, r, 2, 0xC24E, 0xC265); }
void hm_NameMenuKeyRight(uint8_t *ram, HmRegs *r) { name_move(ram, r, 3, 0xC26F, 0xC286); }

/* NameMenuKeyB ($84:C0AB): erase the last letter (with 4 letters there is
 * no cursor to clear first). */
void hm_NameMenuKeyB(uint8_t *ram, HmRegs *r)
{
  uint8_t n = rd8(ram, r, NAME_COUNT);
  set_a8(r, n);
  if (n) {
    r->c = n >= 4;
    if (n != 4) {
      r->a = 0x00A8;                                       /* the cursor's place: blank */
      hm_jsl(ram, r, 0xC0BE, 0x82);
      hm_Unk_OutOfMenu(ram, r);
      hm_rtl(ram, r);
    }
    r->a = (uint8_t)(rd8(ram, r, NAME_COUNT) - 1);         /* LDA #0; XBA; LDA; DEC */
    r->x = r->a;
    set_a8(r, 0xB1);
    wr8(ram, r, (uint16_t)(NAME + r->x), 0xB1);
    n = (uint8_t)(rd8(ram, r, NAME_COUNT) - 1);
    set_a8(r, n);
    wr8(ram, r, NAME_COUNT, n);
  }
  beep(ram, r, 2, 0xC0EC);
}

/* NameMenuKeyA ($84:C0EE): the cell's code: switch page (the BG1 scroll
 * shows it), "done" (only with a letter at least: $0993 = 3), or add the
 * letter (up to 4). */
void hm_NameMenuKeyA(uint8_t *ram, HmRegs *r)
{
  static const struct { uint16_t bg_x, bg_y, ret; } PAGE[3] = {
    { 0x0000, 0x0000, 0xC17C }, { 0x0100, 0x0000, 0xC1A6 }, { 0x0000, 0x0100, 0xC1D0 },
  };
  r->a = 5;
  hm_jsl(ram, r, 0xC0F6, 0x82);
  hm_NameCursorPharse(ram, r);
  hm_rtl(ram, r);
  uint16_t code = r->a;
  for (uint16_t k = 1; k <= 4; k++) {
    r->c = code >= k;
    if (code != k) continue;
    if (k == 4) {                                          /* done */
      uint8_t n = rd8(ram, r, NAME_COUNT);
      set_a8(r, n);
      if (!n) { beep(ram, r, 2, 0xC202); return; }
      set_a8(r, 3);
      wr8(ram, r, NAME_PAGE, 3);
      beep(ram, r, 1, 0xC1EF);
      return;
    }
    set_a8(r, (uint8_t)(k - 1));
    wr8(ram, r, NAME_PAGE, (uint8_t)(k - 1));
    r->a = PAGE[k - 1].bg_x;
    hm_bus_wr16(ram, db_addr(r, 0x013C), PAGE[k - 1].bg_x);
    hm_bus_wr16(ram, db_addr(r, 0x013E), PAGE[k - 1].bg_y);
    if (k == 3) r->a = 0x0100;
    hm_bus_wr16(ram, db_addr(r, NAME_CELL), 0);
    beep(ram, r, 1, PAGE[k - 1].ret);
    return;
  }
  uint8_t n = rd8(ram, r, NAME_COUNT);
  set_a8(r, n);
  r->c = n >= 4;
  if (n != 4) {                                            /* add the letter */
    r->a = 4;
    hm_jsl(ram, r, 0xC124, 0x82);
    hm_NameCursorPharse(ram, r);
    hm_rtl(ram, r);
    uint8_t letter = (uint8_t)r->a;
    hm_push8(ram, r, letter);                              /* PHA */
    r->x = rd8(ram, r, NAME_COUNT);                        /* LDA #0; XBA; LDA; TAX */
    r->a = (uint16_t)(hm_pull8(ram, r));                   /* PLA (A high = 0) */
    wr8(ram, r, (uint16_t)(NAME + r->x), letter);
    hm_jsl(ram, r, 0xC13A, 0x82);
    hm_Unk_OutOfMenu(ram, r);
    hm_rtl(ram, r);
    n = (uint8_t)(rd8(ram, r, NAME_COUNT) + 1);
    set_a8(r, n);
    wr8(ram, r, NAME_COUNT, n);
  }
  beep(ram, r, 1, 0xC155);
}

/* NameMenuInputs ($84:C066): the arrows with auto-repeat, then B, then A */
void hm_NameMenuInputs(uint8_t *ram, HmRegs *r)
{
  static const struct { uint16_t at, button; void (*fn)(uint8_t *, HmRegs *); } K[] = {
    { JOY_REPEAT, BTN_DOWN, hm_NameMenuKeyDown }, { JOY_REPEAT, BTN_UP, hm_NameMenuKeyUp },
    { JOY_REPEAT, BTN_RIGHT, hm_NameMenuKeyLeft }, { JOY_REPEAT, BTN_LEFT, hm_NameMenuKeyRight },
    { JOY_NEW, BTN_B, hm_NameMenuKeyB }, { JOY_NEW, BTN_A, hm_NameMenuKeyA },
  };
  for (unsigned i = 0; i < sizeof K / sizeof K[0]; i++)
    if (pressed(ram, r, K[i].at, K[i].button)) { K[i].fn(ram, r); return; }
}

/* ═════════════════════════════════════════════════════════════════════════
 * Menu 1 (CODE_84C2B4): the player moved by an event's pad (unless in the
 * air); with $7F1F5A bit $8000, Start skips: menu 9 (CODE_84C2D8, which
 * also sets $7F1F5C bit 2).
 * ═════════════════════════════════════════════════════════════════════════ */
void hm_CODE_84C2D8(uint8_t *ram, HmRegs *r)
{
  set_a8(r, 9);
  *hm_dp(ram, r, 0x95) = 9;
  beep(ram, r, 3, 0xC2EF);
  r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F5C) | 0x0004);
  hm_bus_wr16(ram, 0x7F1F5C, r->a);
}
void hm_CODE_84C2B4(uint8_t *ram, HmRegs *r)
{
  r->a = hm_dp_r16(ram, r, 0xD4);
  r->c = r->a >= 3;
  if (r->a != 3) {
    hm_jsl(ram, r, 0xC2C0, 0x84);
    hm_CODE_84C7D6(ram, r);
    hm_rtl(ram, r);
  }
  r->a = hm_bus_rd16(ram, 0x7F1F5A) & 0x8000;
  if (!r->a) return;
  if (pressed(ram, r, JOY_NEW, BTN_START)) hm_CODE_84C2D8(ram, r);
}

/* MenuInputs ($84:C288): by the menu number $95 */
void hm_MenuInputs(uint8_t *ram, HmRegs *r)
{
  static const struct { uint8_t menu; void (*fn)(uint8_t *, HmRegs *); } M[] = {
    { 1, hm_CODE_84C2B4 }, { 2, hm_StartMenuInputs }, { 3, hm_StartMenuInputs },
    { 5, hm_TitleMenuInputs }, { 7, hm_SaveSlotMenuInputs }, { 8, hm_LoadSlotMenuInputs },
  };
  uint8_t m = *hm_dp(ram, r, 0x95);
  set_a8(r, m);
  for (unsigned i = 0; i < sizeof M / sizeof M[0]; i++) {
    r->c = m >= M[i].menu;
    if (m == M[i].menu) { M[i].fn(ram, r); return; }
  }
}

/* CODE_84CED2 ($84:CED2, input mode 6): with $019B bit 2, after 120 frames
 * close the text box and go back to walking (and forget a pressed A). */
void hm_CODE_84CED2(uint8_t *ram, HmRegs *r)
{
  uint8_t box = rd8(ram, r, 0x019B);
  set_a8(r, box & 0x04);
  if (!(box & 0x04)) return;
  r->a = (uint16_t)(rd16(ram, r, 0x09B1) + 1);
  hm_bus_wr16(ram, db_addr(r, 0x09B1), r->a);
  r->c = r->a >= 0x78;
  if (r->a != 0x78) return;
  hm_bus_wr16(ram, db_addr(r, 0x09B1), 0);
  hm_jsl(ram, r, 0xCEF1, 0x83);
  hm_CloseTextBox(ram, r);
  hm_rtl(ram, r);
  set_a8(r, 1);
  wr8(ram, r, 0x019A, 1);
  r->a = rd16(ram, r, JOY_NEW) & 0xFF7F;
  hm_bus_wr16(ram, db_addr(r, JOY_NEW), r->a);
}

/* ─────────────────────────────────────────────────────────────────────────
 * InputTypeSelector ($84:C034): by the input mode $019A.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_InputTypeSelector(uint8_t *ram, HmRegs *r)
{
  static void (*const mode[])(uint8_t *, HmRegs *) = {
    hm_PlayerMovementInputs, hm_ImmobileInputs, hm_ListInputs, hm_MenuInputs,
    hm_NameMenuInputs, hm_CODE_84CED2,
  };
  uint8_t m = rd8(ram, r, 0x019A);
  set_a8(r, m);
  for (uint8_t k = 1; k <= 6; k++) {
    r->c = m >= k;
    if (m == k) { mode[k - 1](ram, r); return; }
  }
}
