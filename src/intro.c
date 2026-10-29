/*
 * intro.c — from the power-on to the game: the title screen and its
 * menus, the new game and the saved one.
 *
 * IntroScreen ($82:D75E, from RESET) sets up the Natsume logo and then
 * runs a frame at a time ($82:D80D): it waits for the NMI, runs the
 * colour cycles (UNK_BigLoop) and the joypad (InputTypeSelector), then the
 * part of the intro of $95 (the stage):
 *   0  the logo, 2 seconds (120 frames), then it fades out: stage 1;
 *   1  the parents' scene: a small game (script 0 of group 9) played on its
 *      own frames ($82:D93F, as GameLoop does) until the script ends
 *      ($7F1F5C bit 2), then the title's pictures: stage 2;
 *   2  the title slides in (BG2 right one pixel every other frame, BG1 to
 *      0), then a second's pause ($96 to $3C): stage 3;
 *   3  BG1 / BG2 up to 0: stage 6;
 *   6, 9  the title screen's background (map $5C; 9 also clears the
 *      screen first), faded in: stage 4;
 *   4  a second (BG2 drifting), then the menu's marks (colour cycles,
 *      UNK_SetPointer42) with the choice $098D: stage 5;
 *   5  the menu: BG2 drifting, the marks again when the choice changed
 *      ($97, set by the menu's inputs), and the demo's count $90 + 1;
 *   7  the new game's file screen (map $5E, the two files' frames and
 *      names), on its own frames ($82:E0EB);
 *   8  the saved game's file screen: SUB_82E1F1;
 *   10 the first demo: SUB_82D1C0;
 * and at the end of each frame ($82:D871, "democheck"), after 600 frames
 * of the menu (10 s), the other demos (DemoPresentation).
 * The file screens go back to the title ($94 = 1, $82:DEC5), or start the
 * game ($94 = 2): StartFromNewGame, StartFromLoad.
 *
 * Nothing here returns: each check stops at the end of one frame (the
 * jump back to the frame's start) or where it goes on to another of these
 * routines (HM_TOLOOP). IntroScreen_D80D, _D93F, _E0EB and SUB_82E1F1_E337
 * check one frame of each loop on its own.
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

typedef void Fn(uint8_t *ram, HmRegs *r);

static uint32_t db(const HmRegs *r, uint16_t a) { return (uint32_t)r->db << 16 | a; }
static uint8_t rd8(uint8_t *ram, const HmRegs *r, uint16_t a) { return hm_bus_rd8(ram, db(r, a)); }
static uint16_t rd16(uint8_t *ram, const HmRegs *r, uint16_t a) { return hm_bus_rd16(ram, db(r, a)); }
static void wr8(uint8_t *ram, const HmRegs *r, uint16_t a, uint8_t v) { hm_bus_wr8(ram, db(r, a), v); }
static void wr16(uint8_t *ram, const HmRegs *r, uint16_t a, uint16_t v) { hm_bus_wr16(ram, db(r, a), v); }
static void set_a8(HmRegs *r, uint8_t v) { r->a = (uint16_t)((r->a & 0xFF00) | v); }
static uint8_t dp8(uint8_t *ram, HmRegs *r, unsigned a) { return *hm_dp(ram, r, a); }
static void set_dp8(uint8_t *ram, HmRegs *r, unsigned a, uint8_t v) { *hm_dp(ram, r, a) = v; }

#define STAGE   0x95   /* direct page: the intro's stage */
#define COUNT   0x94   /* direct page: the stage's frames */
#define DEMO    0x90   /* direct page: the menu's frames, to the demo */

/* JSL at `at` to a routine of `bank`. Those that wait frames save the
 * flags (PHP): r->p carries the carry, 8-bit A, the index width `x8`, D
 * and I of the entry (N, Z and V are not followed: HM_LOOSE). */
static void call(uint8_t *ram, HmRegs *r, uint16_t at, uint8_t bank, Fn *fn, int x8)
{
  uint8_t keep = r->p;
  r->p = (uint8_t)((keep & 0x0C) | 0x20 | (x8 ? 0x10 : 0) | (r->c & 1));
  hm_jsl(ram, r, (uint16_t)(at + 3), bank);
  fn(ram, r);
  hm_rtl(ram, r);
  r->p = keep;
}
static void jsl(uint8_t *ram, HmRegs *r, uint16_t at, uint8_t bank, Fn *fn) { call(ram, r, at, bank, fn, 0); }
/* JSR to a routine of this bank, returning to `ret` */
static void jsr(uint8_t *ram, HmRegs *r, uint16_t ret, Fn *fn)
{
  hm_push8(ram, r, (uint8_t)(ret >> 8));
  hm_push8(ram, r, (uint8_t)ret);
  fn(ram, r);
  hm_pull8(ram, r); hm_pull8(ram, r);
}
/* the fades: $92 from, $93 frames a level, $94 to */
static void fade(uint8_t *ram, HmRegs *r, uint8_t from, uint8_t frames, uint8_t to, uint16_t at, Fn *fn)
{
  set_a8(r, from); set_dp8(ram, r, 0x92, from);            /* SEP #$20 */
  set_a8(r, frames); set_dp8(ram, r, 0x93, frames);
  set_a8(r, to); set_dp8(ram, r, 0x94, to);
  jsl(ram, r, at, 0x80, fn);
}
static void fade_out(uint8_t *ram, HmRegs *r, uint8_t frames, uint16_t at) { fade(ram, r, 0x0F, frames, 0x01, at, hm_ScreenFadeout); }
static void fade_in(uint8_t *ram, HmRegs *r, uint8_t from, uint8_t frames, uint16_t at) { fade(ram, r, from, frames, 0x0F, at, hm_ScreenFadein); }
/* SEP #$20; LDA #map; STA $22 (and the routine at `at`) */
static void map_is(uint8_t *ram, HmRegs *r, uint8_t map)
{
  set_a8(r, map);
  set_dp8(ram, r, 0x22, map);
}
/* a picture's colours to CGRAM: channel 0, $200 bytes from $A9:`from` */
static void colours(uint8_t *ram, HmRegs *r, uint16_t from, uint16_t at)
{
  set_a8(r, 0); set_dp8(ram, r, 0x27, 0);                  /* SEP #$20; REP #$10 */
  set_a8(r, 0x22); set_dp8(ram, r, 0x29, 0x22);
  r->y = 0x0200; r->x = 0;                                 /* REP #$20 */
  r->a = from;
  hm_dp_w16(ram, r, 0x72, from);
  set_a8(r, 0xA9); set_dp8(ram, r, 0x74, 0xA9);            /* SEP #$20 */
  jsl(ram, r, at, 0x80, hm_AddProgrammedDMA);
  jsl(ram, r, (uint16_t)(at + 4), 0x80, hm_StartLastPreparedDMA);
}
/* the music of map $22 changes (UNK_Audio21, 20, 22) and is noted as the
 * one playing ($0117) */
static void new_music(uint8_t *ram, HmRegs *r, uint16_t at)
{
  jsl(ram, r, at, 0x83, hm_UNK_Audio21);
  jsl(ram, r, (uint16_t)(at + 4), 0x83, hm_UNK_Audio20);
  jsl(ram, r, (uint16_t)(at + 8), 0x83, hm_UNK_Audio22);
  uint8_t song = rd8(ram, r, 0x0110);                      /* SEP #$20 */
  set_a8(r, song);
  wr8(ram, r, 0x0117, song);
}
/* the screen lit, one frame */
static void lit(uint8_t *ram, HmRegs *r, uint16_t at)
{
  jsl(ram, r, at, 0x80, hm_ResetForceBlank);
  jsl(ram, r, (uint16_t)(at + 4), 0x80, hm_WaitForNMI);
}
/* everything of the old picture cleared, from `at` (JSLs 4 bytes apart):
 * ZeroesVRAM, ZeroesCGRAM, Zeroes42Pointers, ClearSpriteDataTables,
 * InitializeOBJs, UNK_PresetsMemory3, ClearsofAllCCStructs */
static void clear_all(uint8_t *ram, HmRegs *r, uint16_t at)
{
  static const struct { uint8_t bank; Fn *fn; } ALL[7] = {
    { 0x80, hm_ZeroesVRAM }, { 0x80, hm_ZeroesCGRAM }, { 0x80, hm_Zeroes42Pointers },
    { 0x85, hm_ClearSpriteDataTables }, { 0x85, hm_InitializeOBJs }, { 0x81, hm_UNK_PresetsMemory3 },
    { 0x84, hm_ClearsofAllCCStructs },
  };
  for (int i = 0; i < 7; i++) jsl(ram, r, (uint16_t)(at + 4 * i), ALL[i].bank, ALL[i].fn);
}
/* STZ of words (REP #$20) */
static void zero_words(uint8_t *ram, HmRegs *r, const uint16_t *a, unsigned n)
{
  for (unsigned i = 0; i < n; i++) wr16(ram, r, a[i], 0);
}
static const uint16_t SCROLLS[4] = { 0x013C, 0x013E, 0x0140, 0x0142 };
static const uint16_t ALL_SCROLLS[6] = { 0x013C, 0x013E, 0x0140, 0x0142, 0x0144, 0x0146 };
/* $D6, $D8 (the player), $017D, $017F (where to), $0196 */
static void no_place(uint8_t *ram, HmRegs *r)
{
  hm_dp_w16(ram, r, 0xD6, 0);
  hm_dp_w16(ram, r, 0xD8, 0);
  wr16(ram, r, 0x017D, 0);
  wr16(ram, r, 0x017F, 0);
  wr16(ram, r, 0x0196, 0);
}
/* a colour cycle (UNK_SetPointer42) from $82:`table`: A = which, X / Y
 * the cycle's numbers, with 8-bit indexes */
static void cycle(uint8_t *ram, HmRegs *r, uint16_t table, uint8_t a, uint8_t x, uint8_t y, uint16_t at)
{
  r->a = table;                                            /* REP #$20; SEP #$10 */
  hm_dp_w16(ram, r, 0x72, table);
  set_a8(r, 0x82); set_dp8(ram, r, 0x74, 0x82);            /* SEP #$20 */
  set_a8(r, a);
  r->x = x; r->y = y;
  call(ram, r, at, 0x80, hm_UNK_SetPointer42, 1);
}
#define MARK_ON  0xF2C9   /* the menu's chosen line */
#define MARK_OFF 0xF2DA
/* the menu's three lines' marks, the one of $098D (0-2) lit; the calls
 * at `at`, + $1B, + $36 */
static void menu_marks(uint8_t *ram, HmRegs *r, uint8_t choice, uint16_t at)
{
  static const struct { uint8_t a, x, y; } LINE[3] = { { 0x0D, 6, 7 }, { 0x0F, 7, 7 }, { 0x09, 8, 2 } };
  for (int i = 0; i < 3; i++)
    cycle(ram, r, i == choice ? MARK_ON : MARK_OFF, LINE[i].a, LINE[i].x, LINE[i].y, (uint16_t)(at + 0x1B * i));
}
/* SEP #$20; LDA $098D; CMP #1; BNE; CMP #2; BNE: the menu's choice */
static uint8_t choice_of(uint8_t *ram, HmRegs *r)
{
  uint8_t c = rd8(ram, r, 0x098D);
  set_a8(r, c);
  r->c = c >= 1;
  if (c == 1) return 1;
  r->c = c >= 2;
  return c == 2 ? 2 : 0;
}
#define FILE_ON  0xF2E2   /* the chosen file's frame */
#define FILE_OFF 0xF2F9
/* the two files' frames, the one of slot $098E lit: the calls at `at`
 * and `at2` */
static void file_marks(uint8_t *ram, HmRegs *r, bool second, uint16_t at, uint16_t at2)
{
  cycle(ram, r, second ? FILE_OFF : FILE_ON, 6, 4, 0, at);
  cycle(ram, r, second ? FILE_ON : FILE_OFF, 6, 5, 1, at2);
}
/* the background drifts one pixel every other frame ($96) */
static void drift(uint8_t *ram, HmRegs *r)
{
  uint8_t d = dp8(ram, r, 0x96);                           /* SEP #$20; LDA $96; CMP #1 */
  set_a8(r, d);
  r->c = d >= 1;
  if (d == 1) {
    set_dp8(ram, r, 0x96, 0);
    wr16(ram, r, 0x0140, (uint16_t)(rd16(ram, r, 0x0140) + 1));   /* REP #$20; INC; DEC */
    wr16(ram, r, 0x0142, (uint16_t)(rd16(ram, r, 0x0142) - 1));
  } else {
    set_dp8(ram, r, 0x96, (uint8_t)(d + 1));
  }
}
/* LDA $0140; INC; STA (REP #$20) */
static void bg2_right(uint8_t *ram, HmRegs *r)
{
  r->a = (uint16_t)(rd16(ram, r, 0x0140) + 1);
  wr16(ram, r, 0x0140, r->a);
}
/* SEP #$20; INC $94 */
static void count_up(uint8_t *ram, HmRegs *r) { set_dp8(ram, r, COUNT, (uint8_t)(dp8(ram, r, COUNT) + 1)); }
/* SEP #$20; LDA #s; STA $95; STZ $94 */
static void next_stage(uint8_t *ram, HmRegs *r, uint8_t s)
{
  set_a8(r, s);
  set_dp8(ram, r, STAGE, s);
  set_dp8(ram, r, COUNT, 0);
}
/* the NMI's wait loop (LDA $00 at `at`; the NMI comes back to it) */
static void wait_nmi(uint8_t *ram, HmRegs *r, uint16_t at, uint8_t entry)
{
  for (;;) {
    uint8_t ready = dp8(ram, r, 0x00);                     /* SEP #$20; LDA $00; BEQ */
    set_a8(r, ready);
    if (ready) break;
    hm_nmi(ram, r, at, (uint8_t)(entry | 0x20));
  }
}

/* $82:D871, the end of each frame: $00 cleared; after 600 frames of the
 * menu (JMP DemoPresentation) or round again (BRA $82:D80D): the check
 * stops at either */
static void democheck(uint8_t *ram, HmRegs *r)
{
  set_dp8(ram, r, 0x00, 0);                                /* SEP #$20; STZ $00 */
  r->a = hm_dp_r16(ram, r, DEMO);                          /* REP #$20; CMP #600 */
  r->c = r->a >= 0x0258;
  g_hm_next = r->a == 0x0258 ? 0x82D3C7 : 0x82D80D;        /* BNE; JMP DemoPresentation / BRA */
}

/* stage 0: the logo for 120 frames, then the fade */
static void logo(uint8_t *ram, HmRegs *r)
{
  r->y = 0;
  do {
    hm_push8(ram, r, (uint8_t)(r->y >> 8)); hm_push8(ram, r, (uint8_t)r->y);   /* PHY */
    jsl(ram, r, 0xD887, 0x80, hm_WaitForNMI);
    uint8_t lo = hm_pull8(ram, r);                         /* REP #$30; PLY; INY */
    r->y = (uint16_t)((lo | hm_pull8(ram, r) << 8) + 1);
    r->c = r->y >= 0x0078;
  } while (r->y != 0x0078);
  fade_out(ram, r, 3, 0xD8A2);
  next_stage(ram, r, 1);
  democheck(ram, r);
}

/* stage 1's frames ($82:D93F): as GameLoop's; false when the scene goes on
 * (the check stops: JMP back) */
static bool parents_frame(uint8_t *ram, HmRegs *r, uint8_t entry)
{
  wait_nmi(ram, r, 0xD941, entry);
  r->a = 0x1800;                                           /* REP #$20 */
  hm_dp_w16(ram, r, 0xC7, 0x1800);
  hm_game_frame(ram, r, 0xD94C);
  r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F5C) & 0x0004);  /* REP #$20 */
  if (!r->a) { set_dp8(ram, r, 0x00, 0); g_hm_next = 0x82D93F; return false; }   /* SEP #$20; STZ $00; JMP $D93F */
  return true;
}
/* the scene is over ($82:D9B3): unless it is the demo's end ($95 = 9), the
 * title's pictures (map $5B, palette $6D, colours from $A9:D800), BG1 and
 * BG2 at ($100, $100), its music, faded in: stage 2 */
static void parents_end(uint8_t *ram, HmRegs *r)
{
  uint8_t s = dp8(ram, r, STAGE);                          /* SEP #$20; CMP #9 */
  set_a8(r, s);
  r->c = s >= 9;
  if (s == 9) { democheck(ram, r); return; }               /* JMP $D871 */
  fade_out(ram, r, 3, 0xD9CC);
  jsl(ram, r, 0xD9D0, 0x80, hm_ForceBlank);
  set_a8(r, 0x03);
  jsl(ram, r, 0xD9D8, 0x80, hm_ManageGraphicPresets);
  clear_all(ram, r, 0xD9DC);
  map_is(ram, r, 0x5B);
  jsl(ram, r, 0xD9FE, 0x80, hm_BackgroundsManager);
  r->a = 0x006D;                                           /* REP #$20 */
  jsl(ram, r, 0xDA07, 0x80, hm_LoadFirstHalfPaletteToWRAM);
  colours(ram, r, 0xD800, 0xDA2A);
  r->a = 0x0100;                                           /* REP #$20 */
  static const uint16_t AT[4] = { 0x013C, 0x0140, 0x013E, 0x0142 };
  for (int i = 0; i < 4; i++) wr16(ram, r, AT[i], 0x0100);
  jsl(ram, r, 0xDA4C, 0x80, hm_UNK_Audio5);
  new_music(ram, r, 0xDA50);
  lit(ram, r, 0xDA64);
  fade_in(ram, r, 0x01, 6, 0xDA7A);
  next_stage(ram, r, 2);
  set_dp8(ram, r, 0x96, 0);
  democheck(ram, r);
}
/* stage 1: the parents' scene: the game set up (the player standing,
 * BG3 off screen), script 0 of group 9, map $098B entered (its music
 * changed), and its frames */
static void parents(uint8_t *ram, HmRegs *r, uint8_t entry)
{
  clear_all(ram, r, 0xD8B2);                               /* REP #$30 */
  r->a = (uint16_t)(hm_dp_r16(ram, r, 0xD2) | 0x0001);
  hm_dp_w16(ram, r, 0xD2, r->a);
  r->a = 0;
  hm_dp_w16(ram, r, 0xD4, 0);
  hm_dp_w16(ram, r, 0xDA, 0);
  wr16(ram, r, 0x0911, 0);
  wr16(ram, r, 0x0901, 0);
  r->a = 0x0100;                                           /* REP #$20 */
  wr16(ram, r, 0x0146, 0x0100);
  r->a = 0; r->x = 0x0009; r->y = 0;                       /* REP #$30 */
  jsl(ram, r, 0xD90A, 0x84, hm_UNK_LoadCCDataLong);
  jsl(ram, r, 0xD90E, 0x84, hm_IterateCCStructures);
  map_is(ram, r, rd8(ram, r, 0x098B));                     /* SEP #$20 */
  jsl(ram, r, 0xD919, 0x80, hm_UNK_Audio5);
  new_music(ram, r, 0xD91D);
  set_a8(r, rd8(ram, r, 0x098B));                          /* SEP #$20 */
  jsl(ram, r, 0xD936, 0x80, hm_SUB_80972C);
  r->y = 0;                                                /* REP #$30 */
  if (parents_frame(ram, r, entry)) parents_end(ram, r);
}

/* stage 2: the title slides in */
static void title_in(uint8_t *ram, HmRegs *r)
{
  uint8_t c = (uint8_t)(dp8(ram, r, COUNT) & 0x01);        /* SEP #$20; LDA $94; AND #1 */
  set_a8(r, c);
  if (!c) {
    bg2_right(ram, r);                                     /* REP #$20 */
    r->a = rd16(ram, r, 0x013C);
    if (!r->a) {                                           /* in place: a second's pause */
      uint8_t p = (uint8_t)(dp8(ram, r, 0x96) + 1);        /* SEP #$20 */
      set_a8(r, p);
      set_dp8(ram, r, 0x96, p);
      r->c = p >= 0x3C;
      if (p == 0x3C) { next_stage(ram, r, 3); democheck(ram, r); return; }
    } else {
      r->a = (uint16_t)(r->a - 1);
      wr16(ram, r, 0x013C, r->a);
    }
  }
  count_up(ram, r);
  democheck(ram, r);
}
/* stage 6 / 9 / the way back from a file screen ($82:DBB2 on): the title
 * screen's background (map $5C, its music), faded in, and stage 4 */
static void title_screen(uint8_t *ram, HmRegs *r)
{
  static const uint16_t W[4] = { 0x013C, 0x013E, 0x0140, 0x0142 };
  hm_dp_w16(ram, r, DEMO, 0);                              /* REP #$20 */
  zero_words(ram, r, W, 4);
  no_place(ram, r);
  jsl(ram, r, 0xDBCF, 0x80, hm_BackgroundsManager);
  new_music(ram, r, 0xDBD3);
  lit(ram, r, 0xDBE7);
  fade_in(ram, r, 0x01, 1, 0xDBFD);
  next_stage(ram, r, 4);
  democheck(ram, r);
}
/* the music of map $5C (UNK_Audio5, UNK_Audio25) and the fast fade out */
static void title_music_out(uint8_t *ram, HmRegs *r, uint16_t at)
{
  map_is(ram, r, 0x5C);
  jsl(ram, r, at, 0x80, hm_UNK_Audio5);
  jsl(ram, r, (uint16_t)(at + 4), 0x83, hm_UNK_Audio25);
  fade_out(ram, r, 1, (uint16_t)(at + 0x16));
  jsl(ram, r, (uint16_t)(at + 0x1A), 0x80, hm_ForceBlank);
}
static void stage6(uint8_t *ram, HmRegs *r)
{
  title_music_out(ram, r, 0xDB94);
  title_screen(ram, r);
}
/* stage 3: BG1 and BG2 up to 0 (then stage 6's code) */
static void title_up(uint8_t *ram, HmRegs *r)
{
  uint8_t c = (uint8_t)(dp8(ram, r, COUNT) & 0x01);        /* SEP #$20 */
  set_a8(r, c);
  if (!c) {
    bg2_right(ram, r);                                     /* REP #$20 */
    r->a = rd16(ram, r, 0x013E);
    if (!r->a) { stage6(ram, r); return; }                 /* JMP $DB8E */
    r->a = (uint16_t)(r->a - 1);
    wr16(ram, r, 0x013E, r->a);
    r->a = (uint16_t)(rd16(ram, r, 0x0142) - 1);
    wr16(ram, r, 0x0142, r->a);
  }
  count_up(ram, r);
  democheck(ram, r);
}
/* stage 9: the whole screen cleared first, then as stage 6 */
static void stage9(uint8_t *ram, HmRegs *r)
{
  title_music_out(ram, r, 0xDAFB);
  set_a8(r, 0x03);                                         /* SEP #$20 */
  jsl(ram, r, 0xDB1D, 0x80, hm_ManageGraphicPresets);
  clear_all(ram, r, 0xDB21);
  no_place(ram, r);                                        /* REP #$20 */
  map_is(ram, r, 0x5B);
  jsl(ram, r, 0xDB52, 0x80, hm_BackgroundsManager);
  r->a = 0x006D;                                           /* REP #$20 */
  jsl(ram, r, 0xDB5B, 0x80, hm_LoadFirstHalfPaletteToWRAM);
  colours(ram, r, 0xD800, 0xDB7E);
  map_is(ram, r, 0x5C);
  title_screen(ram, r);                                    /* BRA $DBB2 */
}
/* stage 4: a second, then the menu's marks: stage 5 */
static void menu_in(uint8_t *ram, HmRegs *r)
{
  uint8_t c = (uint8_t)(dp8(ram, r, COUNT) & 0x01);        /* SEP #$20 */
  set_a8(r, c);
  if (!c) bg2_right(ram, r);                               /* REP #$20 */
  uint8_t n = dp8(ram, r, COUNT);                          /* SEP #$20; CMP #$3C */
  set_a8(r, n);
  r->c = n >= 0x3C;
  if (n != 0x3C) { count_up(ram, r); democheck(ram, r); return; }
  jsl(ram, r, 0xDC32, 0x80, hm_Zeroes42Pointers);
  cycle(ram, r, 0xF2A4, 0x0A, 4, 7, 0xDC4D);
  cycle(ram, r, 0xF2B5, 0x0B, 5, 7, 0xDC68);
  static const uint16_t AT[3] = { 0xDC98, 0xDCED, 0xDD42 };
  uint8_t choice = choice_of(ram, r);
  menu_marks(ram, r, choice, AT[choice]);
  next_stage(ram, r, 5);
  democheck(ram, r);
}
/* stage 5: the menu */
static void menu(uint8_t *ram, HmRegs *r)
{
  uint8_t c = (uint8_t)(dp8(ram, r, COUNT) & 0x01);        /* SEP #$20 */
  set_a8(r, c);
  if (!c) bg2_right(ram, r);                               /* REP #$20 */
  count_up(ram, r);
  uint8_t changed = dp8(ram, r, 0x97);                     /* LDA $97 */
  set_a8(r, changed);
  if (changed) {
    set_dp8(ram, r, 0x97, 0);
    static const uint16_t AT[3] = { 0xDDD5, 0xDE2A, 0xDE7F };
    uint8_t choice = choice_of(ram, r);
    menu_marks(ram, r, choice, AT[choice]);
  }
  hm_dp_w16(ram, r, DEMO, (uint16_t)(hm_dp_r16(ram, r, DEMO) + 1));   /* REP #$30; INC $90 */
  democheck(ram, r);
}

/* $82:DEC5: from a file screen back to the title: as stage 9, without
 * clearing the objects, both pictures loaded, $90 = 0 */
static void back_to_title(uint8_t *ram, HmRegs *r)
{
  title_music_out(ram, r, 0xDECB);
  jsl(ram, r, 0xDEE9, 0x80, hm_ZeroesVRAM);
  set_a8(r, 0x03);                                         /* SEP #$20 */
  jsl(ram, r, 0xDEF1, 0x80, hm_ManageGraphicPresets);
  zero_words(ram, r, SCROLLS, 4);                          /* REP #$20 */
  no_place(ram, r);
  map_is(ram, r, 0x5B);
  jsl(ram, r, 0xDF16, 0x80, hm_BackgroundsManager);
  map_is(ram, r, 0x5C);
  jsl(ram, r, 0xDF20, 0x80, hm_BackgroundsManager);
  r->a = 0x006D;                                           /* REP #$20 */
  jsl(ram, r, 0xDF29, 0x80, hm_LoadFirstHalfPaletteToWRAM);
  colours(ram, r, 0xD800, 0xDF4C);
  new_music(ram, r, 0xDF54);
  lit(ram, r, 0xDF68);
  fade_in(ram, r, 0x01, 1, 0xDF7E);
  next_stage(ram, r, 4);
  hm_dp_w16(ram, r, DEMO, 0);                              /* REP #$20 */
  democheck(ram, r);
}

/* a file screen's start: map $5E's music, the screen (preset 4, palette
 * $6F, colours from $A9:8C00), the two files' frames (SUB_82E742) and
 * contents (CODE_82E405, Y = `kind`: 0 the new game's, 1 the saved
 * game's); the calls from `at` on (the routine's own address) */
static void file_screen_start(uint8_t *ram, HmRegs *r, uint16_t at, uint16_t kind)
{
  map_is(ram, r, 0x5E);
  jsl(ram, r, (uint16_t)(at + 0x06), 0x80, hm_UNK_Audio5);
  jsl(ram, r, (uint16_t)(at + 0x0A), 0x83, hm_UNK_Audio25);
  fade_out(ram, r, 3, (uint16_t)(at + 0x1C));
  jsl(ram, r, (uint16_t)(at + 0x20), 0x80, hm_ForceBlank);
  jsl(ram, r, (uint16_t)(at + 0x24), 0x80, hm_ZeroesVRAM);
  set_a8(r, 0x04);                                         /* SEP #$20 */
  jsl(ram, r, (uint16_t)(at + 0x2C), 0x80, hm_ManageGraphicPresets);
  jsl(ram, r, (uint16_t)(at + 0x30), 0x80, hm_BackgroundsManager);
  r->a = 0x006F;                                           /* REP #$20 */
  jsl(ram, r, (uint16_t)(at + 0x39), 0x80, hm_LoadFirstHalfPaletteToWRAM);
  colours(ram, r, 0x8C00, (uint16_t)(at + 0x5C));
  jsr(ram, r, (uint16_t)(at + 0x66), hm_SUB_82E742);
  for (uint16_t f = 0; f < 2; f++) {                       /* REP #$30 */
    r->a = f; r->y = kind;
    jsr(ram, r, (uint16_t)(at + 0x71 + 0x0B * f), hm_CODE_82E405);
  }
  jsl(ram, r, (uint16_t)(at + 0x7D), 0x80, hm_Zeroes42Pointers);
}
/* a file screen's end of start: the scrolls at 0 (`scrolls` first, the
 * new game's), the music, the screen lit and faded in, $94 / $96 / $97 = 0 */
static void file_screen_lit(uint8_t *ram, HmRegs *r, uint16_t music)
{
  new_music(ram, r, music);
  lit(ram, r, (uint16_t)(music + 0x14));
  fade_in(ram, r, 0x03, 3, (uint16_t)(music + 0x2A));
  set_dp8(ram, r, COUNT, 0);                               /* SEP #$20 */
  set_dp8(ram, r, 0x96, 0);
  set_dp8(ram, r, 0x97, 0);
}
/* a file screen's frame's end: the drift; $94 = 1 back to the title, 2
 * the game (the check stops at the JML), else round again (stops) */
static void file_screen_end(uint8_t *ram, HmRegs *r, uint32_t back, uint32_t start)
{
  drift(ram, r);
  uint8_t c = dp8(ram, r, COUNT);                          /* SEP #$20; CMP #1 */
  set_a8(r, c);
  r->c = c >= 1;
  if (c == 1) { back_to_title(ram, r); return; }
  r->c = c >= 2;
  if (c == 2) { g_hm_next = start; return; }               /* JML StartFromNewGame / StartFromLoad */
  set_dp8(ram, r, 0x00, 0);                                /* STZ $00; JML back */
  g_hm_next = back;
}
/* LoadGameSlotResume of slot A; with no saved game there (X = 0), BG1's
 * x = 0 */
static void slot_resume(uint8_t *ram, HmRegs *r, uint16_t slot, uint16_t at)
{
  r->a = slot;                                             /* REP #$20 */
  jsl(ram, r, at, 0x83, hm_LoadGameSlotResume);
  r->c = 1;                                                /* REP #$30; CPX #0 */
  if (r->x == 0) { r->a = 0; wr16(ram, r, 0x013C, 0); }
}

/* stage 7's frames ($82:E0EB): the new game's file screen */
static void new_file_frame(uint8_t *ram, HmRegs *r, uint8_t entry)
{
  wait_nmi(ram, r, 0xE0ED, entry);
  r->a = 0x1800;                                           /* REP #$20 */
  hm_dp_w16(ram, r, 0xC7, 0x1800);
  jsl(ram, r, 0xE0F8, 0x80, hm_UNK_BigLoop);
  jsl(ram, r, 0xE0FC, 0x84, hm_InputTypeSelector);
  uint8_t changed = dp8(ram, r, 0x97);                     /* SEP #$20 */
  set_a8(r, changed);
  if (changed) {
    set_dp8(ram, r, 0x97, 0);
    uint8_t slot = rd8(ram, r, 0x098E);
    set_a8(r, slot);
    if (!slot) { slot_resume(ram, r, 0, 0xE11A); file_marks(ram, r, false, 0xE148, 0xE163); }
    else       { slot_resume(ram, r, 1, 0xE170); file_marks(ram, r, true, 0xE19E, 0xE1B9); }
  }
  file_screen_end(ram, r, 0x82E0EB, 0x82E7F9);
}
/* stage 7: the new game's file screen */
static void new_file(uint8_t *ram, HmRegs *r, uint8_t entry)
{
  file_screen_start(ram, r, 0xDF92, 0);
  zero_words(ram, r, ALL_SCROLLS, 6);                      /* REP #$20 */
  r->a = rd8(ram, r, 0x098E);                              /* SEP #$20; LDA #0; XBA; LDA */
  jsl(ram, r, 0xE02F, 0x83, hm_LoadGameSlotResume);
  r->c = 1;                                                /* REP #$30; CPX #0 */
  uint8_t slot = rd8(ram, r, 0x098E);                      /* SEP #$20 */
  set_a8(r, slot);
  file_marks(ram, r, slot != 0, slot ? 0xE096 : 0xE05C, slot ? 0xE0B1 : 0xE077);
  file_screen_lit(ram, r, 0xE0B5);
  new_file_frame(ram, r, entry);
}

/* one frame of the intro ($82:D80D) */
static void intro_frame(uint8_t *ram, HmRegs *r, uint8_t entry)
{
  wait_nmi(ram, r, 0xD80F, entry);
  jsl(ram, r, 0xD813, 0x80, hm_UNK_BigLoop);
  jsl(ram, r, 0xD817, 0x84, hm_InputTypeSelector);
  uint8_t s = dp8(ram, r, STAGE);                          /* SEP #$20; LDA $95 */
  set_a8(r, s);
  if (!s) { logo(ram, r); return; }
  for (uint8_t k = 1; k <= 0x0A; k++) {                    /* CMP #k; BNE */
    r->c = s >= k;
    if (s == k) break;
  }
  switch (s) {
  case 1: parents(ram, r, entry); return;
  case 2: title_in(ram, r); return;
  case 3: title_up(ram, r); return;
  case 4: menu_in(ram, r); return;
  case 5: menu(ram, r); return;
  case 6: stage6(ram, r); return;
  case 7: new_file(ram, r, entry); return;
  case 8: g_hm_next = 0x82E1F1; return;                    /* JML SUB_82E1F1: the check stops */
  case 9: stage9(ram, r); return;
  case 10: g_hm_next = 0x82D1C0; return;                   /* JML SUB_82D1C0: the check stops */
  default: democheck(ram, r); return;
  }
}

/* ─────────────────────────────────────────────────────────────────────────
 * IntroScreen ($82:D75E, JML from RESET): the controls to the title
 * ($019A = 4), the menu's choice $098D = 2 ("continue") when the saved
 * games failed their check ($7F1F60 bit 11) and 0 otherwise; the demos
 * from the first ($7F1F48 / $7F1F49 = 0); the farm's map from the ROM;
 * the logo (preset 3, map $5D, palette $6E, colours from $A9:DA00), the
 * scrolls at 0, the screen lit and faded in; $97 = 0, $90 = 0, $AF = 1.
 * Then its frames.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_IntroScreen(uint8_t *ram, HmRegs *r)
{
  uint8_t entry = r->p;
  set_a8(r, 0x04);                                         /* REP #$30; SEP #$20 */
  wr8(ram, r, 0x019A, 0x04);
  wr8(ram, r, 0x098D, 0);
  r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F60) & 0x0800);  /* REP #$20 */
  if (r->a) { set_a8(r, 0x02); wr8(ram, r, 0x098D, 0x02); }
  set_a8(r, 0x00);                                         /* SEP #$20 */
  hm_bus_wr8(ram, 0x7F1F48, 0);
  hm_bus_wr8(ram, 0x7F1F49, 0);
  jsl(ram, r, 0xD78A, 0x82, hm_LoadDefaultFarmMap);
  set_a8(r, 0x03);                                         /* SEP #$20 */
  jsl(ram, r, 0xD792, 0x80, hm_ManageGraphicPresets);
  jsl(ram, r, 0xD796, 0x80, hm_ForceBlank);
  jsl(ram, r, 0xD79A, 0x80, hm_ZeroesVRAM);
  map_is(ram, r, 0x5D);
  jsl(ram, r, 0xD7A4, 0x80, hm_BackgroundsManager);
  r->a = 0x006E;                                           /* REP #$20 */
  jsl(ram, r, 0xD7AD, 0x80, hm_LoadFirstHalfPaletteToWRAM);
  colours(ram, r, 0xDA00, 0xD7D0);
  zero_words(ram, r, SCROLLS, 4);                          /* REP #$20 */
  lit(ram, r, 0xD7E6);
  fade_in(ram, r, 0x03, 3, 0xD7FC);
  set_dp8(ram, r, 0x97, 0);                                /* SEP #$20 */
  hm_dp_w16(ram, r, DEMO, 0);                              /* REP #$20 */
  r->a = 0x0001;
  hm_dp_w16(ram, r, 0xAF, 0x0001);
  intro_frame(ram, r, entry);                              /* the check stops after it */
}
/* the frames on their own: $82:D80D (the intro's), $82:D93F (the parents'
 * scene's), $82:E0EB (the new game's file screen's) */
void hm_IntroScreen_D80D(uint8_t *ram, HmRegs *r) { intro_frame(ram, r, r->p); }
void hm_IntroScreen_D93F(uint8_t *ram, HmRegs *r)
{
  if (parents_frame(ram, r, r->p)) parents_end(ram, r);
}
void hm_IntroScreen_E0EB(uint8_t *ram, HmRegs *r) { new_file_frame(ram, r, r->p); }
void hm_IntroScreen_D871(uint8_t *ram, HmRegs *r) { democheck(ram, r); }

/* ─────────────────────────────────────────────────────────────────────────
 * SUB_82E1F1 ($82:E1F1, stage 8, JML): the saved game's file screen: as
 * the new game's, with the files' contents of the saved games, and its
 * frames ($82:E337): a changed choice ($97) lights the other file's frame;
 * $94 = 1 back to the title, 2 the saved game (StartFromLoad).
 * ───────────────────────────────────────────────────────────────────────── */
static void load_file_frame(uint8_t *ram, HmRegs *r, uint8_t entry)
{
  wait_nmi(ram, r, 0xE339, entry);
  r->a = 0x1800;                                           /* REP #$20 */
  hm_dp_w16(ram, r, 0xC7, 0x1800);
  jsl(ram, r, 0xE344, 0x80, hm_UNK_BigLoop);
  jsl(ram, r, 0xE348, 0x84, hm_InputTypeSelector);
  uint8_t changed = dp8(ram, r, 0x97);                     /* SEP #$20 */
  set_a8(r, changed);
  if (changed) {
    set_dp8(ram, r, 0x97, 0);
    uint8_t slot = rd8(ram, r, 0x098E);
    set_a8(r, slot);
    file_marks(ram, r, slot != 0, slot ? 0xE3B2 : 0xE378, slot ? 0xE3CD : 0xE393);
  }
  file_screen_end(ram, r, 0x82E337, 0x82E7E9);             /* SUB_82E3D1 */
}
void hm_SUB_82E1F1(uint8_t *ram, HmRegs *r)
{
  uint8_t entry = r->p;
  file_screen_start(ram, r, 0xE1F1, 1);
  uint8_t slot = rd8(ram, r, 0x098E);                      /* SEP #$20 */
  set_a8(r, slot);
  file_marks(ram, r, slot != 0, slot ? 0xE2CE : 0xE294, slot ? 0xE2E9 : 0xE2AF);
  zero_words(ram, r, ALL_SCROLLS, 6);                      /* REP #$20 */
  file_screen_lit(ram, r, 0xE301);
  load_file_frame(ram, r, entry);
}
void hm_SUB_82E1F1_E337(uint8_t *ram, HmRegs *r) { load_file_frame(ram, r, r->p); }

/* ─────────────────────────────────────────────────────────────────────────
 * StartFromLoad ($82:E7E9, JML): the saved game of slot $098E
 * (LoadGameSlot) and its first day (SpawnAfterLoad, JML).
 * StartFromNewGame ($82:E7F9, JML): a new game (NewGameSetup) and its
 * first day.
 * SUB_UNUSED_1 ($82:E801, nothing reaches it): the player's name written
 * again ($099F = 0, Unk_NamesInput).
 * The checks stop where SpawnAfterLoad goes into GameLoop, or after the
 * names screen's first frame.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_StartFromLoad(uint8_t *ram, HmRegs *r)
{
  r->a = rd8(ram, r, 0x098E);                              /* SEP #$20; LDA #0; XBA; LDA */
  jsl(ram, r, 0xE7F1, 0x83, hm_LoadGameSlot);
  hm_SpawnAfterLoad(ram, r);                               /* JML */
}
void hm_StartFromNewGame(uint8_t *ram, HmRegs *r)
{
  jsl(ram, r, 0xE7F9, 0x83, hm_NewGameSetup);
  hm_SpawnAfterLoad(ram, r);                               /* JML */
}
void hm_SUB_UNUSED_1(uint8_t *ram, HmRegs *r)
{
  set_a8(r, 0x00);                                         /* SEP #$20 */
  wr8(ram, r, 0x099F, 0);
  hm_Unk_NamesInput(ram, r);                               /* JML */
}
