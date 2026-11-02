/*
 * names.c — after a name was written in the names screen (NamesScreen):
 * the name goes where it belongs, and the game goes on.
 *
 * The four letters typed are at $0885-$0888. Each routine fades the
 * screen out (from 15, 3 frames a level, to 1), blanks it, clears VRAM
 * and CGRAM, keeps the name, asks for a map to be entered ($0196 bit 14,
 * which SetScreenTransition sees), puts BG3 back ($0146 = $100), the
 * controls back to the player ($019A = 1), and jumps into the game loop
 * (GameLoop, $80:8083), which never comes back: the check stops there
 * (HM_TOLOOP).
 * Each name is kept twice: one byte a letter, and one word a letter (the
 * text routines use the words).
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

typedef void Fn(uint8_t *ram, HmRegs *r);

/* ── Names of the memory locations used here ──────────────────────────────
 * Also used, from hm_ram.h: HM_MAP_ID ($22), HM_CAM_X ($013C, the first of
 * the scrolls) and HM_INPUT_STATE ($019A). */

/* Direct page */
#define NMI_STATUS      0x00   /* 8b: set by the NMI, cleared by the loop */
#define DMA_CHANNEL     0x27   /* 8b: AddProgrammedDMA's channel (dma.c) */
#define DMA_B_ADDRESS   0x29   /* 8b: its destination register ($22 = CGRAM data) */
#define DMA_SOURCE      0x72   /* 24b $72-$74: its source */
#define DMA_SOURCE_BANK 0x74   /* 8b: the source's bank */
#define COW_POINTER     0x72   /* 24b: new_cow: the cow's record (AddNewCow); the
                                  same bytes as DMA_SOURCE */
#define FADE_FROM       0x92   /* 8b: the fades' arguments: the starting brightness, */
#define FADE_FRAMES     0x93   /* 8b: frames per brightness level, */
#define FADE_TO         0x94   /* 8b: the brightness it ends at */
#define DRIFT_FRAME     0x96   /* 8b: the background drifts when it is 1 (0 / 1) */
#define MENU_REDRAW     0x97   /* 8b: 1 = draw the menu again (menus.c); cleared */
#define OBJ_X           0x9B   /* 16b: the game object routines' arguments (objects.c) */
#define OBJ_Y           0x9D   /* 16b */
#define OBJ_FLIP        0x9F   /* 16b */
#define OBJ_SPRITE      0xA1   /* 16b */
#define OBJ_PARAM       0xA3   /* 16b */
#define OBJ_NUMBER      0xA5   /* 16b: the object's number */
#define LOAD_BUDGET     0xC7   /* 16b: bytes the loader may still send this frame */

/* WRAM of the data bank */
#define WANTED_SONG     0x0110 /* 8b: the song asked for */
#define PLAYING_SONG    0x0117 /* 8b: the song playing */
#define BG2_X           0x0140 /* 16b: BG2's scroll, across */
#define BG2_Y           0x0142 /* 16b: up-down */
#define BG3_SCROLL_Y    0x0146 /* 16b: BG3's scroll down (the text box's layer) */
#define ARROW_BLINK     0x018B /* 8b: the blink (here the name cursor's): bit 7 the
                                  phase, 0-6 a counter */
#define WEATHER_FLAGS   0x0196 /* 16b: weather and day flags; bit 13 the names
                                  screen, bit 14 a map to enter */
#define WEATHER_FLAGS_KEPT 0x0198 /* 16b: WEATHER_FLAGS kept during the names screen */
#define PLAYER_NAME     0x0881 /* 4 bytes: the player's name */
#define TYPED           0x0885 /* 4 bytes: the four letters typed ($B1 = empty) */
#define DOG_NAME        0x0899 /* 4 bytes */
#define HORSE_NAME      0x089D /* 4 bytes */
#define PLAYER_NAME_WORDS 0x08D5 /* 4 x 16b: the 16-bit copies of the names */
#define DOG_NAME_WORDS  0x08DD
#define HORSE_NAME_WORDS 0x08E5
#define KID1_NAME_WORDS 0x08ED
#define KID2_NAME_WORDS 0x08F5
#define PLAYER_GOBJ     0x0905 /* 16b: the player's game object */
#define NAME_CELL       0x0991 /* 16b: the cell under the cursor */
#define NAME_PAGE       0x0993 /* 8b: the page shown (0-2; 3 = done) */
#define NAME_COUNT      0x0994 /* 8b: how many letters are typed */
#define CURSOR_GOBJ     0x0995 /* 16b: the cursor's game object */
#define CURSOR_SPRITE   0x0997 /* 16b: the cursor's sprite, kept */
#define CURSOR_FLIP     0x0999 /* 16b: its flip */
#define NAME_CURSOR_X   0x099B /* 16b: its position, across */
#define NAME_CURSOR_Y   0x099D /* 16b: down */
#define NAME_FOR        0x099F /* 8b: where the name goes: 0 the player, 1 a cow
                                  bought, 2 a calf, 3 the dog, 4 the horse, 5 / 6
                                  the children */

/* The game's saved state, $7F:1Fxx (bus addresses) */
#define KID1_NAME       0x7F1F3D /* 4 bytes */
#define KID2_NAME       0x7F1F41 /* 4 bytes */
#define FLAGS_5A        0x7F1F5A /* 16b flags: bit 1 a cow is being bought */
#define FLAGS_64        0x7F1F64 /* 16b story flags: bits 2 and 3 a calf's birth */

/* A cow's record */
#define COW_NAME        0x0C     /* 4 bytes: +0C-+0F */

/* ROM: the names screen's colours, in bank $A9 */
#define NAME_COLOURS_1  0x8C00   /* $A9:8C00, $100 bytes */
#define NAME_COLOURS_2  0xDE00   /* $A9:DE00, $100 bytes */

static uint32_t db(const HmRegs *r, uint16_t a) { return (uint32_t)r->db << 16 | a; }
static uint8_t rd8(uint8_t *ram, const HmRegs *r, uint16_t a) { return hm_bus_rd8(ram, db(r, a)); }
static uint16_t rd16(uint8_t *ram, const HmRegs *r, uint16_t a) { return hm_bus_rd16(ram, db(r, a)); }
static void wr8(uint8_t *ram, const HmRegs *r, uint16_t a, uint8_t v) { hm_bus_wr8(ram, db(r, a), v); }
static void wr16(uint8_t *ram, const HmRegs *r, uint16_t a, uint16_t v) { hm_bus_wr16(ram, db(r, a), v); }
static void set_a8(HmRegs *r, uint8_t v) { r->a = (uint16_t)((r->a & 0xFF00) | v); }

/* JSL at `at` (bank 80 unless said): the fade saves the flags (PHP):
 * r->p carries the carry, 8-bit A, 16-bit indexes, D and I of the entry
 * (N, Z and V are not followed: HM_LOOSE) */
static void jsl(uint8_t *ram, HmRegs *r, uint16_t at, uint8_t bank, Fn *fn)
{
  uint8_t keep = r->p;
  r->p = (uint8_t)((keep & 0x0C) | 0x20 | (r->c & 1));
  hm_jsl(ram, r, (uint16_t)(at + 3), bank);
  fn(ram, r);
  hm_rtl(ram, r);
  r->p = keep;
}
/* the fade out and the cleared screen; the JSLs from `at` */
static void dark(uint8_t *ram, HmRegs *r, uint16_t at)
{
  set_a8(r, 0x0F); *hm_dp(ram, r, FADE_FROM) = 0x0F;       /* SEP #$20; REP #$10 */
  set_a8(r, 0x03); *hm_dp(ram, r, FADE_FRAMES) = 0x03;
  set_a8(r, 0x01); *hm_dp(ram, r, FADE_TO) = 0x01;
  jsl(ram, r, at, 0x80, hm_ScreenFadeOut);
  jsl(ram, r, (uint16_t)(at + 4), 0x80, hm_ForceBlank);
  jsl(ram, r, (uint16_t)(at + 8), 0x80, hm_ZeroesVRAM);
  jsl(ram, r, (uint16_t)(at + 12), 0x80, hm_ZeroesCGRAM);
}
/* the four letters to `bytes` (one byte each, a full address) and `words`
 * (one word each) */
static void keep_name(uint8_t *ram, HmRegs *r, uint32_t bytes, uint16_t words)
{
  for (uint16_t i = 0; i < 4; i++) {                       /* LDA #0; XBA: A's top 0 */
    uint8_t letter = rd8(ram, r, (uint16_t)(TYPED + i));
    r->a = letter;
    hm_bus_wr8(ram, bytes + i, letter);
    wr16(ram, r, (uint16_t)(words + 2 * i), letter);
  }
}
/* a new cow (AddNewCow: A = 0 bought, 1 born) and its name at +0C-+0F */
static void new_cow(uint8_t *ram, HmRegs *r, uint16_t born, uint16_t at)
{
  r->a = born;                                             /* REP #$20 */
  jsl(ram, r, at, 0x83, hm_AddNewCow);
  for (uint16_t i = 0; i < 4; i++) {                       /* SEP #$20; REP #$10 */
    r->y = (uint16_t)(COW_NAME + i);
    set_a8(r, rd8(ram, r, (uint16_t)(TYPED + i)));
    hm_bus_wr8(ram, hm_dp_long(ram, r, COW_POINTER, r->y), (uint8_t)r->a);
  }
}
/* $0196 bit 14 is already set by the flag lines; BG3 and the controls */
static void back_to_game(uint8_t *ram, HmRegs *r)
{
  r->a = 0x0100;                                           /* REP #$20 */
  wr16(ram, r, BG3_SCROLL_Y, 0x0100);
  set_a8(r, 0x01);                                         /* SEP #$20 */
  wr8(ram, r, HM_INPUT_STATE, 0x01);
  g_hm_next = 0x808083;
  /* JMP GameLoop: the check stops here (HM_TOLOOP) */
}
static void go_to_map(uint8_t *ram, HmRegs *r)
{
  r->a = (uint16_t)(rd16(ram, r, WEATHER_FLAGS) | 0x4000); /* REP #$20 */
  wr16(ram, r, WEATHER_FLAGS, r->a);
}
static void flag_off(uint8_t *ram, HmRegs *r, uint32_t addr, uint16_t bits)
{
  r->a = (uint16_t)(hm_bus_rd16(ram, addr) & ~bits);
  hm_bus_wr16(ram, addr, r->a);
}

/* SetPlayerName ($80:80ED): $0881-$0884 and $08D5-$08DB */
void hm_SetPlayerName(uint8_t *ram, HmRegs *r)
{
  dark(ram, r, 0x80FD);
  keep_name(ram, r, db(r, PLAYER_NAME), PLAYER_NAME_WORDS);
  go_to_map(ram, r);
  back_to_game(ram, r);
}
/* SetCowNameBought ($80:815F): a bought cow; $7F1F5A bit 1 (buying it)
 * off */
void hm_SetCowNameBought(uint8_t *ram, HmRegs *r)
{
  dark(ram, r, 0x816F);
  new_cow(ram, r, 0, 0x8184);
  go_to_map(ram, r);
  flag_off(ram, r, FLAGS_5A, 0x0002);
  back_to_game(ram, r);
}
/* SetCowNameBorn ($80:81D2): a calf; $7F1F64 bits 2 and 3 (the birth)
 * off */
void hm_SetCowNameBorn(uint8_t *ram, HmRegs *r)
{
  dark(ram, r, 0x81E2);
  new_cow(ram, r, 1, 0x81F7);
  flag_off(ram, r, FLAGS_64, 0x0004);
  flag_off(ram, r, FLAGS_64, 0x0008);
  go_to_map(ram, r);
  back_to_game(ram, r);
}
/* SetDogName ($80:8254): $0899-$089C and $08DD-$08E3 */
void hm_SetDogName(uint8_t *ram, HmRegs *r)
{
  dark(ram, r, 0x8264);
  keep_name(ram, r, db(r, DOG_NAME), DOG_NAME_WORDS);
  go_to_map(ram, r);
  back_to_game(ram, r);
}
/* SetHorseName ($80:82C6): $089D-$08A0 and $08E5-$08EB */
void hm_SetHorseName(uint8_t *ram, HmRegs *r)
{
  dark(ram, r, 0x82D6);
  keep_name(ram, r, db(r, HORSE_NAME), HORSE_NAME_WORDS);
  go_to_map(ram, r);
  back_to_game(ram, r);
}
/* SetKid1Name ($80:8338): $7F1F3D-$7F1F40 (the save's) and $08ED-$08F3 */
void hm_SetKid1Name(uint8_t *ram, HmRegs *r)
{
  dark(ram, r, 0x8348);
  keep_name(ram, r, KID1_NAME, KID1_NAME_WORDS);
  go_to_map(ram, r);
  back_to_game(ram, r);
}
/* SetKid2Name ($80:83AE): $7F1F41-$7F1F44 and $08F5-$08FB */
void hm_SetKid2Name(uint8_t *ram, HmRegs *r)
{
  dark(ram, r, 0x83BE);
  keep_name(ram, r, KID2_NAME, KID2_NAME_WORDS);
  go_to_map(ram, r);
  back_to_game(ram, r);
}

/* ─────────────────────────────────────────────────────────────────────────
 * NamesScreen ($82:E80C; GameLoop jumps here, JML, with $0196 bit 13):
 * the names screen.
 *  - The setup: the music of map $5F (PickMapMusic, FadeOutSongSlowly), the
 *    screen faded out (from 15, 3 frames a level, to 1) and blanked, VRAM
 *    cleared, the colour cycles, sprites and objects stopped
 *    (StopAllColourAnimations, ClearSpriteDataTables, InitializeOBJs); $0196 kept
 *    in $0198 and cleared, no player object ($0905 = 0), the controls to
 *    the names screen ($019A = 5); its graphics (preset 4,
 *    BackgroundsManager), its colours (palette $6F), two blocks of $100
 *    bytes from $A9:8C00 and $A9:DE00 to CGRAM; ClearItemRecords; the
 *    cursor's sprite (CreateGameObject at (40, 68), animation $219, kept
 *    in $0995-$099D); the frames of the name (DrawScreenBar, DrawNameEntryBlanks); the
 *    scrolls at 0; the music changes (FadeOutSong, 20, 22); the screen lit
 *    and faded in (from 3, 3 frames a level, to 15); page 0, cell 0, no
 *    letters ($0885-$0888 = $B1, blank).
 *  - Then a frame at a time (from $82:E95F): wait for the NMI; the cursor
 *    of the name blinks (every $14 frames, local $82:EA15); the joypad
 *    (InputTypeSelector); unless the name is done ($0993 = 3), the cursor
 *    moves (NameCursorPosition), the background drifts one pixel every other frame
 *    (BG2 x + 1, y - 1), the cursor's sprite is drawn (MoveGameObject,
 *    AnimateGameObjects, LoadQueuedSpritePieces, PrepareOAMData); and round again.
 *  - Done: $0196 back, and on to where the name goes ($099F: 0 the player,
 *    1 a cow bought, 2 a calf, 3 the dog, 4 the horse, 5 / 6 the
 *    children: SetPlayerName... SetKid2Name, JML; any other value would go
 *    round for ever).
 * Bug: DrawScreenBar ends with RTS but is called with JSL, so a byte (the
 * bank, $82) stays on the stack from there on. Nothing returns from here,
 * so it does no harm; the check's stop takes it off before its RTL.
 * The check stops after the first frame (the JML back to $82:E95F), or at
 * the JML to the name's routine (HM_TOLOOP); NamesScreenFrame checks a
 * frame on its own.
 * ───────────────────────────────────────────────────────────────────────── */
static void zero16_all(uint8_t *ram, HmRegs *r, uint16_t from, uint16_t to)
{
  for (uint16_t a = from; a <= to; a += 2) wr16(ram, r, a, 0);
}
/* two blocks of colours to CGRAM ($27 the channel, $29 = $22) */
static void colours(uint8_t *ram, HmRegs *r, uint8_t channel, uint16_t to, uint16_t from, uint16_t at)
{
  set_a8(r, channel);                                      /* SEP #$20; REP #$10 */
  *hm_dp(ram, r, DMA_CHANNEL) = channel;
  set_a8(r, 0x22);
  *hm_dp(ram, r, DMA_B_ADDRESS) = 0x22;
  r->y = 0x0100;                                           /* REP #$20 */
  r->x = to;
  r->a = from;
  hm_dp_w16(ram, r, DMA_SOURCE, from);
  set_a8(r, 0xA9);                                         /* SEP #$20 */
  *hm_dp(ram, r, DMA_SOURCE_BANK) = 0xA9;
  jsl(ram, r, at, 0x80, hm_AddProgrammedDMA);
}
/* the four numbers of the cursor's sprite: to the direct page and kept */
static const struct { uint8_t dp; uint16_t keep; } CURSOR[4] = {
  { OBJ_SPRITE, CURSOR_SPRITE }, { OBJ_FLIP, CURSOR_FLIP },
  { OBJ_X, NAME_CURSOR_X }, { OBJ_Y, NAME_CURSOR_Y },
};

/* $82:EA15 (JSR): the name's cursor blinks: unless the name is full, every
 * $14 counts ($018B, bit 7 the phase) the phase turns; the cursor is drawn
 * ($A8) or blanked ($B1) at the name's place (DrawNameLetter) */
static void name_cursor(uint8_t *ram, HmRegs *r)
{
  uint8_t n = rd8(ram, r, NAME_COUNT);                     /* SEP #$20; REP #$10 */
  set_a8(r, n);
  r->c = n >= 4;
  if (n == 4) return;
  r->a = (uint16_t)(rd16(ram, r, ARROW_BLINK) & 0x007F);   /* REP #$20 */
  r->c = r->a >= 0x0014;
  if (r->a == 0x0014) {
    uint8_t v = (uint8_t)((rd8(ram, r, ARROW_BLINK) & 0x80) ^ 0x80);   /* SEP #$20 */
    set_a8(r, v);
    wr8(ram, r, ARROW_BLINK, v);
  }
  uint8_t phase = (uint8_t)(rd8(ram, r, ARROW_BLINK) & 0x80);
  set_a8(r, phase);
  if (!phase) { r->a = 0x00A8; jsl(ram, r, 0xEA47, 0x82, hm_DrawNameLetter); }
  else        { r->a = 0x00B1; jsl(ram, r, 0xEA52, 0x82, hm_DrawNameLetter); }
  uint8_t c = (uint8_t)(rd8(ram, r, ARROW_BLINK) + 1);     /* SEP #$20 */
  set_a8(r, c);
  wr8(ram, r, ARROW_BLINK, c);
}

/* the done name: $0196 back, and the JML to its routine (the check stops
 * there: not followed) */
static void name_done(uint8_t *ram, HmRegs *r)
{
  r->a = rd16(ram, r, WEATHER_FLAGS_KEPT);                 /* REP #$20 */
  wr16(ram, r, WEATHER_FLAGS, r->a);
  uint8_t to = rd8(ram, r, NAME_FOR);                      /* SEP #$20 */
  set_a8(r, to);
  static const uint32_t SET_NAME[7] = { 0x8080ED, 0x80815F, 0x8081D2, 0x808254, 0x8082C6, 0x808338, 0x8083AE };
  for (uint8_t k = 0; k <= 6; k++) {
    r->c = to >= k;
    if (to == k) { g_hm_next = SET_NAME[k]; return; }     /* JML Set...Name */
  }
  for (;;) { }                                             /* BRA: round for ever */
}

/* one frame of the names screen ($82:E95F); false once the name is done */
static bool name_frame(uint8_t *ram, HmRegs *r, uint8_t entry)
{
  for (;;) {                                               /* SEP #$20; LDA $00; BEQ */
    uint8_t ready = *hm_dp(ram, r, NMI_STATUS);
    set_a8(r, ready);
    if (ready) break;
    hm_nmi(ram, r, 0xE961, (uint8_t)(entry | 0x20));
  }
  r->a = 0x1800;                                           /* REP #$20 */
  hm_dp_w16(ram, r, LOAD_BUDGET, 0x1800);
  hm_push8(ram, r, 0xE9); hm_push8(ram, r, 0x6E);          /* JSR $EA15 */
  name_cursor(ram, r);
  hm_pull8(ram, r); hm_pull8(ram, r);
  jsl(ram, r, 0xE96F, 0x84, hm_InputTypeSelector);
  uint8_t page = rd8(ram, r, NAME_PAGE);                   /* SEP #$20 */
  set_a8(r, page);
  r->c = page >= 3;
  if (page == 3) { name_done(ram, r); return false; }
  jsl(ram, r, 0xE97F, 0x82, hm_NameCursorPosition);
  uint8_t drift = *hm_dp(ram, r, DRIFT_FRAME);             /* SEP #$20 */
  set_a8(r, drift);
  r->c = drift >= 1;
  if (drift == 1) {
    *hm_dp(ram, r, DRIFT_FRAME) = 0;
    wr16(ram, r, BG2_X, (uint16_t)(rd16(ram, r, BG2_X) + 1));   /* REP #$20 */
    wr16(ram, r, BG2_Y, (uint16_t)(rd16(ram, r, BG2_Y) - 1));
  } else {
    *hm_dp(ram, r, DRIFT_FRAME) = (uint8_t)(drift + 1);
  }
  for (int i = 0; i < 4; i++) {                            /* REP #$20 */
    r->a = rd16(ram, r, CURSOR[i].keep);
    hm_dp_w16(ram, r, CURSOR[i].dp, r->a);
  }
  r->a = rd16(ram, r, CURSOR_GOBJ);
  hm_dp_w16(ram, r, OBJ_NUMBER, r->a);
  jsl(ram, r, 0xE9B6, 0x85, hm_MoveGameObject);
  jsl(ram, r, 0xE9BA, 0x85, hm_AnimateGameObjects);
  jsl(ram, r, 0xE9BE, 0x85, hm_LoadQueuedSpritePieces);
  jsl(ram, r, 0xE9C2, 0x85, hm_PrepareOAMData);
  *hm_dp(ram, r, NMI_STATUS) = 0;                          /* SEP #$20; STZ $00 */
  g_hm_next = 0x82E95F;
  return true;                                             /* JML $82:E95F */
}

void hm_NamesScreen(uint8_t *ram, HmRegs *r)
{
  uint8_t entry = r->p;
  set_a8(r, 0x5F);                                         /* SEP #$20 */
  *hm_dp(ram, r, HM_MAP_ID) = 0x5F;
  jsl(ram, r, 0xE812, 0x80, hm_PickMapMusic);
  jsl(ram, r, 0xE816, 0x83, hm_FadeOutSongSlowly);
  set_a8(r, 0x0F); *hm_dp(ram, r, FADE_FROM) = 0x0F;       /* SEP #$20 */
  set_a8(r, 0x03); *hm_dp(ram, r, FADE_FRAMES) = 0x03;
  set_a8(r, 0x01); *hm_dp(ram, r, FADE_TO) = 0x01;
  jsl(ram, r, 0xE828, 0x80, hm_ScreenFadeOut);
  jsl(ram, r, 0xE82C, 0x80, hm_ForceBlank);
  jsl(ram, r, 0xE830, 0x80, hm_ZeroesVRAM);
  jsl(ram, r, 0xE834, 0x80, hm_StopAllColourAnimations);
  jsl(ram, r, 0xE838, 0x85, hm_ClearSpriteDataTables);
  jsl(ram, r, 0xE83C, 0x85, hm_InitializeOBJs);
  r->a = rd16(ram, r, WEATHER_FLAGS);                      /* REP #$20 */
  wr16(ram, r, WEATHER_FLAGS_KEPT, r->a);
  wr16(ram, r, WEATHER_FLAGS, 0);
  wr16(ram, r, PLAYER_GOBJ, 0);
  set_a8(r, 0x05);                                         /* SEP #$20 */
  wr8(ram, r, HM_INPUT_STATE, 0x05);
  set_a8(r, 0x04);
  jsl(ram, r, 0xE859, 0x80, hm_ManageGraphicPresets);
  jsl(ram, r, 0xE85D, 0x80, hm_BackgroundsManager);
  r->a = 0x006F;                                           /* REP #$20 */
  jsl(ram, r, 0xE866, 0x80, hm_LoadFirstHalfPaletteToWRAM);
  colours(ram, r, 0, 0x0000, NAME_COLOURS_1, 0xE889);
  colours(ram, r, 1, 0x0080, NAME_COLOURS_2, 0xE8AC);
  jsl(ram, r, 0xE8B0, 0x80, hm_StartProgrammedDMA);
  jsl(ram, r, 0xE8B4, 0x81, hm_ClearItemRecords);
  static const uint16_t start[4] = { 0x0219, 0x0000, 0x0028, 0x0044 };
  for (int i = 0; i < 4; i++) {                            /* REP #$20 */
    r->a = start[i];
    hm_dp_w16(ram, r, CURSOR[i].dp, start[i]);
    wr16(ram, r, CURSOR[i].keep, start[i]);
  }
  hm_dp_w16(ram, r, OBJ_PARAM, 0);
  jsl(ram, r, 0xE8DC, 0x85, hm_CreateGameObject);
  r->a = hm_dp_r16(ram, r, OBJ_NUMBER);                    /* REP #$20 */
  wr16(ram, r, CURSOR_GOBJ, r->a);
  hm_jsl(ram, r, 0xE8EA, 0x82);                            /* JSL ... */
  hm_DrawScreenBar(ram, r);
  hm_pull8(ram, r); hm_pull8(ram, r);                      /* ... RTS: the bank stays */
  jsl(ram, r, 0xE8EB, 0x82, hm_DrawNameEntryBlanks);
  zero16_all(ram, r, HM_CAM_X, BG3_SCROLL_Y);              /* REP #$20: the scrolls */
  jsl(ram, r, 0xE903, 0x83, hm_FadeOutSong);
  jsl(ram, r, 0xE907, 0x83, hm_SetSong);
  jsl(ram, r, 0xE90B, 0x83, hm_StartSong);
  uint8_t song = rd8(ram, r, WANTED_SONG);                 /* SEP #$20 */
  set_a8(r, song);
  wr8(ram, r, PLAYING_SONG, song);
  jsl(ram, r, 0xE917, 0x80, hm_ResetForceBlank);
  jsl(ram, r, 0xE91B, 0x80, hm_WaitForNMI);
  set_a8(r, 0x03); *hm_dp(ram, r, FADE_FROM) = 0x03;       /* SEP #$20 */
  set_a8(r, 0x03); *hm_dp(ram, r, FADE_FRAMES) = 0x03;
  set_a8(r, 0x0F); *hm_dp(ram, r, FADE_TO) = 0x0F;
  jsl(ram, r, 0xE92D, 0x80, hm_ScreenFadeIn);
  wr16(ram, r, NAME_CELL, 0);                              /* REP #$20 */
  set_a8(r, 0x00);                                         /* SEP #$20 */
  wr8(ram, r, NAME_PAGE, 0);
  wr8(ram, r, NAME_COUNT, 0);
  wr8(ram, r, ARROW_BLINK, 0);
  set_a8(r, 0xB1);
  for (uint16_t i = 0; i < 4; i++) wr8(ram, r, (uint16_t)(TYPED + i), 0xB1);
  *hm_dp(ram, r, FADE_TO) = 0;
  *hm_dp(ram, r, DRIFT_FRAME) = 0;
  *hm_dp(ram, r, MENU_REDRAW) = 0;
  name_frame(ram, r, entry);
  hm_pull8(ram, r);   /* the check stops after the first frame, and takes the stray byte off */
}

/* $82:E95F: one frame of the names screen on its own (entered as the
 * JML back to it does) */
void hm_NamesScreenFrame(uint8_t *ram, HmRegs *r)
{
  name_frame(ram, r, r->p);
}
