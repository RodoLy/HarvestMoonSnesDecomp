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

/* ── Names of the memory locations used here ──────────────────────────────
 * Also used, from hm_ram.h: HM_MAP_ID ($22), HM_GAME_STATE ($D2) and
 * HM_INPUT_STATE ($019A). */

/* Direct page */
#define NMI_STATUS        0x00   /* 8b: set by the NMI (UpdateGraphics), cleared by the loop */
#define FADE_FROM         0x92   /* 8b: ScreenFadeOut's arguments: the starting brightness, */
#define FADE_FRAMES       0x93   /* 8b: frames per brightness level, */
#define FADE_TO           0x94   /* 8b: the brightness it ends at */
#define LOAD_BUDGET       0xC7   /* 16b: bytes the sprite piece loader may still send
                                    this frame (objects.c) */

/* WRAM of the data bank */
#define BG3_Y             0x0146 /* 16b: BG3's scroll, up-down (the text box's layer) */
#define TRANSITION_X      0x017D /* 16b: where the player appears after a transition */
#define TRANSITION_Y      0x017F /* 16b */
#define WEATHER_FLAGS     0x0196 /* 16b: weather and day flags; bit $2000 = a name is to
                                    be written (the names screen) */
#define HM_LUNCH_CHOICE   0x0924 /* 8b: the breakfast (0-3; stamina.c) */
#define TRANSITION_MAP    0x098B /* 8b: the map a transition goes to */

/* WRAM $7F:1Fxx (bus address) */
#define FLAGS_68          0x7F1F68 /* 16b flags: bit 0 = the first day is over */

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
 * of the house (map $15: PickMapMusic, FadeOutSongSlowly), the screen faded out
 * (from 15, 3 frames a level, to 1), blanked, VRAM and CGRAM cleared; the
 * day's state from zero (FirstNightReset) and the music chosen again; BG3
 * back ($0146 = $100), the controls to the player ($019A = 1); the player
 * at (136, 120) in the house (FindMostLovedName, EnterMap);
 * after the first day ($7F1F68 bit 0), the breakfast ($0924 = 0-3,
 * RNGReturn0toA, and $D2 bit 2). Then the game loop.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_SpawnAfterLoad(uint8_t *ram, HmRegs *r)
{
  set_a8(r, 0x15);                                         /* REP #$30; SEP #$20 */
  *hm_dp(ram, r, HM_MAP_ID) = 0x15;
  jsl(ram, r, 0x8008, 0x80, hm_PickMapMusic);
  jsl(ram, r, 0x800C, 0x83, hm_FadeOutSongSlowly);
  set_a8(r, 0x0F); *hm_dp(ram, r, FADE_FROM) = 0x0F;       /* SEP #$20 */
  set_a8(r, 0x03); *hm_dp(ram, r, FADE_FRAMES) = 0x03;
  set_a8(r, 0x01); *hm_dp(ram, r, FADE_TO) = 0x01;
  jsl(ram, r, 0x801E, 0x80, hm_ScreenFadeOut);
  jsl(ram, r, 0x8022, 0x80, hm_ForceBlank);
  jsl(ram, r, 0x8026, 0x80, hm_ZeroesVRAM);
  jsl(ram, r, 0x802A, 0x80, hm_ZeroesCGRAM);
  jsl(ram, r, 0x802E, 0x83, hm_FirstNightReset);
  jsl(ram, r, 0x8032, 0x80, hm_PickMapMusic);
  r->a = 0x0100;                                           /* REP #$20 */
  wr16(ram, r, BG3_Y, 0x0100);
  set_a8(r, 0x01);                                         /* SEP #$20 */
  wr8(ram, r, HM_INPUT_STATE, 0x01);
  r->a = 0x0088;                                           /* REP #$30 */
  wr16(ram, r, TRANSITION_X, 0x0088);
  r->a = 0x0078;
  wr16(ram, r, TRANSITION_Y, 0x0078);
  set_a8(r, 0x15);                                         /* SEP #$20 */
  wr8(ram, r, TRANSITION_MAP, 0x15);
  jsl(ram, r, 0x805A, 0x82, hm_FindMostLovedName);
  jsl(ram, r, 0x805E, 0x80, hm_EnterMap);
  r->a = (uint16_t)(hm_bus_rd16(ram, FLAGS_68) & 0x0001);  /* REP #$20 */
  g_hm_next = 0x808083;
  if (!r->a) return;                                       /* BEQ GameLoop */
  set_a8(r, 0x03);                                         /* SEP #$20 */
  jsl(ram, r, 0x8071, 0x80, hm_RNGReturn0toA);
  wr8(ram, r, HM_LUNCH_CHOICE, (uint8_t)r->a);             /* SEP #$20 */
  r->a = (uint16_t)(hm_dp_r16(ram, r, HM_GAME_STATE) | 0x0004);   /* REP #$30 */
  hm_dp_w16(ram, r, HM_GAME_STATE, r->a);
  /* on into GameLoop: the check stops here (HM_TOLOOP) */
}

/* The frame's work, as GameLoop (and the demos, with the same calls) do
 * it: the fifteen JSLs, one every 4 bytes from `at` (hm_decomp.h) */
void hm_game_frame(uint8_t *ram, HmRegs *r, uint16_t at)
{
  static const struct { uint8_t bank; Fn *fn; } WORK[15] = {
    { 0x80, hm_SetScreenTransition }, { 0x80, hm_CheckDoors }, { 0x82, hm_UpdateTime },
    { 0x83, hm_PrintTextFrame }, { 0x80, hm_TransitionTimeOfDayPalettes }, { 0x80, hm_RunColourAnimations },
    { 0x84, hm_InputTypeSelector }, { 0x81, hm_RunItemsOnMap }, { 0x81, hm_PlayerFrame },
    { 0x80, hm_AutoMapScrolling }, { 0x84, hm_RunEventSlots }, { 0x81, hm_RunMapAnimation },
    { 0x85, hm_AnimateGameObjects }, { 0x85, hm_LoadQueuedSpritePieces }, { 0x85, hm_PrepareOAMData },
  };
  for (int i = 0; i < 15; i++) jsl(ram, r, (uint16_t)(at + 4 * i), WORK[i].bank, WORK[i].fn);
}

/* ─────────────────────────────────────────────────────────────────────────
 * GameLoop ($80:8083): one frame of the game. It waits for the NMI ($00
 * set), then $C7 = $1800 (the sprites' first tile) and, unless a name is
 * to be written ($0196 bit 13: the bit off, and on to the names screen,
 * NamesScreen, JML $82:E80C), runs the frame's work:
 *   SetScreenTransition (a map to go to), CheckDoors, UpdateTime (the
 *   clock), PrintTextFrame (the text box), TransitionTimeOfDayPalettes,
 *   RunColourAnimations,
 *   InputTypeSelector (the joypad and what it does), RunItemsOnMap (the items),
 *   PlayerFrame (the player's frame), AutoMapScrolling,
 *   RunEventSlots (the event scripts), RunMapAnimation, AnimateGameObjects,
 *   LoadQueuedSpritePieces, PrepareOAMData (the sprites),
 * clears $00 and goes round (JMP $8083). The check stops there, after one
 * frame, or at the JML (HM_TOLOOP).
 * ───────────────────────────────────────────────────────────────────────── */
void hm_GameLoop(uint8_t *ram, HmRegs *r)
{
  uint8_t entry = r->p;
  for (;;) {                                               /* SEP #$20; LDA $00; BEQ */
    uint8_t ready = *hm_dp(ram, r, NMI_STATUS);
    set_a8(r, ready);
    if (ready) break;
    hm_nmi(ram, r, 0x8085, (uint8_t)(entry | 0x20));       /* at the LDA, after the SEP */
  }
  r->a = 0x1800;                                           /* REP #$20 */
  hm_dp_w16(ram, r, LOAD_BUDGET, 0x1800);
  r->a = (uint16_t)(rd16(ram, r, WEATHER_FLAGS) & 0x2000);
  if (r->a) {
    r->a = (uint16_t)(rd16(ram, r, WEATHER_FLAGS) & 0xDFFF);   /* REP #$30 */
    wr16(ram, r, WEATHER_FLAGS, r->a);
    g_hm_next = 0x82E80C;
    return;                                                /* JML NamesScreen: the check stops */
  }
  hm_game_frame(ram, r, 0x809B);
  *hm_dp(ram, r, NMI_STATUS) = 0;                          /* SEP #$20; STZ $00 */
  g_hm_next = 0x808083;
  /* JMP GameLoop: one frame done, the check stops here (HM_TOLOOP) */
}
