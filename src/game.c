/*
 * game.c — the game proper: the start of a day after the game is loaded,
 * and the main loop, one frame of the game each time round.
 *
 * GameLoop ($80:8083) never returns: SpawnAfterLoad and the names routines
 * end by going into it, and it goes round for ever (or jumps into the
 * names screen). The checks stop where they go into it, and for GameLoop
 * itself at the end of one frame (HM_TOLOOP).
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

typedef void Fn(uint8_t *ram, HmRegs *r);

static uint32_t db(const HmRegs *r, uint16_t a) { return (uint32_t)r->db << 16 | a; }
static uint16_t rd16(uint8_t *ram, const HmRegs *r, uint16_t a) { return hm_bus_rd16(ram, db(r, a)); }
static void wr8(uint8_t *ram, const HmRegs *r, uint16_t a, uint8_t v) { hm_bus_wr8(ram, db(r, a), v); }
static void wr16(uint8_t *ram, const HmRegs *r, uint16_t a, uint16_t v) { hm_bus_wr16(ram, db(r, a), v); }
static void set_a8(HmRegs *r, uint8_t v) { r->a = (uint16_t)((r->a & 0xFF00) | v); }

/* JSL at `at` to a routine of `bank`. Those that wait frames save the
 * flags (PHP): r->p carries the carry, 8-bit A, 16-bit indexes, D and I
 * of the entry (N, Z and V are not followed: HM_LOOSE). */
static void jsl(uint8_t *ram, HmRegs *r, uint16_t at, uint8_t bank, Fn *fn)
{
  uint8_t keep = r->p;
  r->p = (uint8_t)((keep & 0x0C) | 0x20 | (r->c & 1));
  hm_jsl(ram, r, (uint16_t)(at + 3), bank);
  fn(ram, r);
  hm_rtl(ram, r);
  r->p = keep;
}

/* ─────────────────────────────────────────────────────────────────────────
 * SpawnAfterLoad ($80:8000, JSL; once a saved game was loaded): the music
 * of the house (map $15: UNK_Audio5, UNK_Audio25), the screen faded out
 * (from 15, 3 frames a level, to 1), blanked, VRAM and CGRAM cleared; the
 * day's state from zero (FirstNightReset) and the music chosen again; BG3
 * back ($0146 = $100), the controls to the player ($019A = 1); the player
 * at (136, 120) in the house (FindMostLovedName, UNK_ScreenTransition);
 * after the first day ($7F1F68 bit 0), the breakfast ($0924 = 0-3,
 * RNGReturn0toA, and $D2 bit 2). Then the game loop.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_SpawnAfterLoad(uint8_t *ram, HmRegs *r)
{
  set_a8(r, 0x15);                                         /* REP #$30; SEP #$20 */
  *hm_dp(ram, r, 0x22) = 0x15;
  jsl(ram, r, 0x8008, 0x80, hm_UNK_Audio5);
  jsl(ram, r, 0x800C, 0x83, hm_UNK_Audio25);
  set_a8(r, 0x0F); *hm_dp(ram, r, 0x92) = 0x0F;            /* SEP #$20 */
  set_a8(r, 0x03); *hm_dp(ram, r, 0x93) = 0x03;
  set_a8(r, 0x01); *hm_dp(ram, r, 0x94) = 0x01;
  jsl(ram, r, 0x801E, 0x80, hm_ScreenFadeout);
  jsl(ram, r, 0x8022, 0x80, hm_ForceBlank);
  jsl(ram, r, 0x8026, 0x80, hm_ZeroesVRAM);
  jsl(ram, r, 0x802A, 0x80, hm_ZeroesCGRAM);
  jsl(ram, r, 0x802E, 0x83, hm_FirstNightReset);
  jsl(ram, r, 0x8032, 0x80, hm_UNK_Audio5);
  r->a = 0x0100;                                           /* REP #$20 */
  wr16(ram, r, 0x0146, 0x0100);
  set_a8(r, 0x01);                                         /* SEP #$20 */
  wr8(ram, r, 0x019A, 0x01);
  r->a = 0x0088;                                           /* REP #$30 */
  wr16(ram, r, 0x017D, 0x0088);
  r->a = 0x0078;
  wr16(ram, r, 0x017F, 0x0078);
  set_a8(r, 0x15);                                         /* SEP #$20 */
  wr8(ram, r, 0x098B, 0x15);
  jsl(ram, r, 0x805A, 0x82, hm_FindMostLovedName);
  jsl(ram, r, 0x805E, 0x80, hm_UNK_ScreenTransition);
  r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F68) & 0x0001);  /* REP #$20 */
  g_hm_next = 0x808083;
  if (!r->a) return;                                       /* BEQ GameLoop */
  set_a8(r, 0x03);                                         /* SEP #$20 */
  jsl(ram, r, 0x8071, 0x80, hm_RNGReturn0toA);
  wr8(ram, r, 0x0924, (uint8_t)r->a);                      /* SEP #$20 */
  r->a = (uint16_t)(hm_dp_r16(ram, r, 0xD2) | 0x0004);     /* REP #$30 */
  hm_dp_w16(ram, r, 0xD2, r->a);
  /* on into GameLoop: the check stops here (HM_TOLOOP) */
}

/* The frame's work, as GameLoop (and the demos, with the same calls) do
 * it: the fifteen JSLs, one every 4 bytes from `at` (hm_decomp.h) */
void hm_game_frame(uint8_t *ram, HmRegs *r, uint16_t at)
{
  static const struct { uint8_t bank; Fn *fn; } WORK[15] = {
    { 0x80, hm_SetScreenTransition }, { 0x80, hm_SUB_809A64 }, { 0x82, hm_UpdateTime },
    { 0x83, hm_PrintTextFrame }, { 0x80, hm_TransitionTimeofDayPalettes }, { 0x80, hm_UNK_BigLoop },
    { 0x84, hm_InputTypeSelector }, { 0x81, hm_BAAAA }, { 0x81, hm_SUB_81BFB7 },
    { 0x80, hm_AutoMapScrolling }, { 0x84, hm_IterateCCStructures }, { 0x81, hm_CODE_81A600 },
    { 0x85, hm_CODE_8582C7 }, { 0x85, hm_CODE_858CB2 }, { 0x85, hm_PrepareOAMData },
  };
  for (int i = 0; i < 15; i++) jsl(ram, r, (uint16_t)(at + 4 * i), WORK[i].bank, WORK[i].fn);
}

/* ─────────────────────────────────────────────────────────────────────────
 * GameLoop ($80:8083): one frame of the game. It waits for the NMI ($00
 * set), then $C7 = $1800 (the sprites' first tile) and, unless a name is
 * to be written ($0196 bit 13: the bit off, and on to the names screen,
 * Unk_NamesInput, JML $82:E80C), runs the frame's work:
 *   SetScreenTransition (a map to go to), SUB_809A64, UpdateTime (the
 *   clock), PrintTextFrame (the text box), TransitionTimeofDayPalettes,
 *   UNK_BigLoop,
 *   InputTypeSelector (the joypad and what it does), BAAAA (the items),
 *   SUB_81BFB7 (the player's frame), AutoMapScrolling,
 *   IterateCCStructures (the event scripts), CODE_81A600, CODE_8582C7,
 *   CODE_858CB2, PrepareOAMData (the sprites),
 * clears $00 and goes round (JMP $8083). The check stops there, after one
 * frame, or at the JML (HM_TOLOOP).
 * ───────────────────────────────────────────────────────────────────────── */
void hm_GameLoop(uint8_t *ram, HmRegs *r)
{
  uint8_t entry = r->p;
  for (;;) {                                               /* SEP #$20; LDA $00; BEQ */
    uint8_t ready = *hm_dp(ram, r, 0x00);
    set_a8(r, ready);
    if (ready) break;
    hm_nmi(ram, r, 0x8085, (uint8_t)(entry | 0x20));       /* at the LDA, after the SEP */
  }
  r->a = 0x1800;                                           /* REP #$20 */
  hm_dp_w16(ram, r, 0xC7, 0x1800);
  r->a = (uint16_t)(rd16(ram, r, 0x0196) & 0x2000);
  if (r->a) {
    r->a = (uint16_t)(rd16(ram, r, 0x0196) & 0xDFFF);      /* REP #$30 */
    wr16(ram, r, 0x0196, r->a);
    g_hm_next = 0x82E80C;
    return;                                                /* JML Unk_NamesInput: the check stops */
  }
  hm_game_frame(ram, r, 0x809B);
  *hm_dp(ram, r, 0x00) = 0;                                /* SEP #$20; STZ $00 */
  g_hm_next = 0x808083;
  /* JMP GameLoop: one frame done, the check stops here (HM_TOLOOP) */
}
