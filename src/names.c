/*
 * names.c — after a name was written in the names screen (Unk_NamesInput):
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

#define TYPED 0x0885   /* DB: the four letters typed */

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
  set_a8(r, 0x0F); *hm_dp(ram, r, 0x92) = 0x0F;           /* SEP #$20; REP #$10 */
  set_a8(r, 0x03); *hm_dp(ram, r, 0x93) = 0x03;
  set_a8(r, 0x01); *hm_dp(ram, r, 0x94) = 0x01;
  jsl(ram, r, at, 0x80, hm_ScreenFadeout);
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
    r->y = (uint16_t)(0x0C + i);
    set_a8(r, rd8(ram, r, (uint16_t)(TYPED + i)));
    hm_bus_wr8(ram, hm_dp_long(ram, r, 0x72, r->y), (uint8_t)r->a);
  }
}
/* $0196 bit 14 is already set by the flag lines; BG3 and the controls */
static void back_to_game(uint8_t *ram, HmRegs *r)
{
  r->a = 0x0100;                                           /* REP #$20 */
  wr16(ram, r, 0x0146, 0x0100);
  set_a8(r, 0x01);                                         /* SEP #$20 */
  wr8(ram, r, 0x019A, 0x01);
  g_hm_next = 0x808083;
  /* JMP GameLoop: the check stops here (HM_TOLOOP) */
}
static void go_to_map(uint8_t *ram, HmRegs *r)
{
  r->a = (uint16_t)(rd16(ram, r, 0x0196) | 0x4000);        /* REP #$20 */
  wr16(ram, r, 0x0196, r->a);
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
  keep_name(ram, r, db(r, 0x0881), 0x08D5);
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
  flag_off(ram, r, 0x7F1F5A, 0x0002);
  back_to_game(ram, r);
}
/* SetCowNameBorn ($80:81D2): a calf; $7F1F64 bits 2 and 3 (the birth)
 * off */
void hm_SetCowNameBorn(uint8_t *ram, HmRegs *r)
{
  dark(ram, r, 0x81E2);
  new_cow(ram, r, 1, 0x81F7);
  flag_off(ram, r, 0x7F1F64, 0x0004);
  flag_off(ram, r, 0x7F1F64, 0x0008);
  go_to_map(ram, r);
  back_to_game(ram, r);
}
/* SetDogName ($80:8254): $0899-$089C and $08DD-$08E3 */
void hm_SetDogName(uint8_t *ram, HmRegs *r)
{
  dark(ram, r, 0x8264);
  keep_name(ram, r, db(r, 0x0899), 0x08DD);
  go_to_map(ram, r);
  back_to_game(ram, r);
}
/* SetHorseName ($80:82C6): $089D-$08A0 and $08E5-$08EB */
void hm_SetHorseName(uint8_t *ram, HmRegs *r)
{
  dark(ram, r, 0x82D6);
  keep_name(ram, r, db(r, 0x089D), 0x08E5);
  go_to_map(ram, r);
  back_to_game(ram, r);
}
/* SetKid1Name ($80:8338): $7F1F3D-$7F1F40 (the save's) and $08ED-$08F3 */
void hm_SetKid1Name(uint8_t *ram, HmRegs *r)
{
  dark(ram, r, 0x8348);
  keep_name(ram, r, 0x7F1F3D, 0x08ED);
  go_to_map(ram, r);
  back_to_game(ram, r);
}
/* SetKid2Name ($80:83AE): $7F1F41-$7F1F44 and $08F5-$08FB */
void hm_SetKid2Name(uint8_t *ram, HmRegs *r)
{
  dark(ram, r, 0x83BE);
  keep_name(ram, r, 0x7F1F41, 0x08F5);
  go_to_map(ram, r);
  back_to_game(ram, r);
}

/* ─────────────────────────────────────────────────────────────────────────
 * Unk_NamesInput ($82:E80C; GameLoop jumps here, JML, with $0196 bit 13):
 * the names screen.
 *  - The setup: the music of map $5F (UNK_Audio5, UNK_Audio25), the
 *    screen faded out (from 15, 3 frames a level, to 1) and blanked, VRAM
 *    cleared, the colour cycles, sprites and objects stopped
 *    (Zeroes42Pointers, ClearSpriteDataTables, InitializeOBJs); $0196 kept
 *    in $0198 and cleared, no player object ($0905 = 0), the controls to
 *    the names screen ($019A = 5); its graphics (preset 4,
 *    BackgroundsManager), its colours (palette $6F), two blocks of $100
 *    bytes from $A9:8C00 and $A9:DE00 to CGRAM; UNK_PresetsMemory3; the
 *    cursor's sprite (CreateGameObject at (40, 68), animation $219, kept
 *    in $0995-$099D); the frames of the name (SUB_82EAB4, SUB_82EA80); the
 *    scrolls at 0; the music changes (UNK_Audio21, 20, 22); the screen lit
 *    and faded in (from 3, 3 frames a level, to 15); page 0, cell 0, no
 *    letters ($0885-$0888 = $B1, blank).
 *  - Then a frame at a time (from $82:E95F): wait for the NMI; the cursor
 *    of the name blinks (every $14 frames, local $82:EA15); the joypad
 *    (InputTypeSelector); unless the name is done ($0993 = 3), the cursor
 *    moves (SUB_82EB57), the background drifts one pixel every other frame
 *    (BG2 x + 1, y - 1), the cursor's sprite is drawn (CODE_8580B9,
 *    CODE_8582C7, CODE_858CB2, PrepareOAMData); and round again.
 *  - Done: $0196 back, and on to where the name goes ($099F: 0 the player,
 *    1 a cow bought, 2 a calf, 3 the dog, 4 the horse, 5 / 6 the
 *    children: SetPlayerName... SetKid2Name, JML; any other value would go
 *    round for ever).
 * Bug: SUB_82EAB4 ends with RTS but is called with JSL, so a byte (the
 * bank, $82) stays on the stack from there on. Nothing returns from here,
 * so it does no harm; the check's stop takes it off before its RTL.
 * The check stops after the first frame (the JML back to $82:E95F), or at
 * the JML to the name's routine (HM_TOLOOP); Unk_NamesInput_E95F checks a
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
  *hm_dp(ram, r, 0x27) = channel;
  set_a8(r, 0x22);
  *hm_dp(ram, r, 0x29) = 0x22;
  r->y = 0x0100;                                           /* REP #$20 */
  r->x = to;
  r->a = from;
  hm_dp_w16(ram, r, 0x72, from);
  set_a8(r, 0xA9);                                         /* SEP #$20 */
  *hm_dp(ram, r, 0x74) = 0xA9;
  jsl(ram, r, at, 0x80, hm_AddProgrammedDMA);
}
/* the four numbers of the cursor's sprite: to the direct page and kept */
static const struct { uint8_t dp; uint16_t keep; } CURSOR[4] = {
  { 0xA1, 0x0997 }, { 0x9F, 0x0999 }, { 0x9B, 0x099B }, { 0x9D, 0x099D },
};

/* $82:EA15 (JSR): the name's cursor blinks: unless the name is full, every
 * $14 counts ($018B, bit 7 the phase) the phase turns; the cursor is drawn
 * ($A8) or blanked ($B1) at the name's place (Unk_OutOfMenu) */
static void name_cursor(uint8_t *ram, HmRegs *r)
{
  uint8_t n = rd8(ram, r, 0x0994);                         /* SEP #$20; REP #$10 */
  set_a8(r, n);
  r->c = n >= 4;
  if (n == 4) return;
  r->a = (uint16_t)(rd16(ram, r, 0x018B) & 0x007F);        /* REP #$20 */
  r->c = r->a >= 0x0014;
  if (r->a == 0x0014) {
    uint8_t v = (uint8_t)((rd8(ram, r, 0x018B) & 0x80) ^ 0x80);   /* SEP #$20 */
    set_a8(r, v);
    wr8(ram, r, 0x018B, v);
  }
  uint8_t phase = (uint8_t)(rd8(ram, r, 0x018B) & 0x80);
  set_a8(r, phase);
  if (!phase) { r->a = 0x00A8; jsl(ram, r, 0xEA47, 0x82, hm_Unk_OutOfMenu); }
  else        { r->a = 0x00B1; jsl(ram, r, 0xEA52, 0x82, hm_Unk_OutOfMenu); }
  uint8_t c = (uint8_t)(rd8(ram, r, 0x018B) + 1);          /* SEP #$20 */
  set_a8(r, c);
  wr8(ram, r, 0x018B, c);
}

/* the done name: $0196 back, and the JML to its routine (the check stops
 * there: not followed) */
static void name_done(uint8_t *ram, HmRegs *r)
{
  r->a = rd16(ram, r, 0x0198);                             /* REP #$20 */
  wr16(ram, r, 0x0196, r->a);
  uint8_t to = rd8(ram, r, 0x099F);                        /* SEP #$20 */
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
    uint8_t ready = *hm_dp(ram, r, 0x00);
    set_a8(r, ready);
    if (ready) break;
    hm_nmi(ram, r, 0xE961, (uint8_t)(entry | 0x20));
  }
  r->a = 0x1800;                                           /* REP #$20 */
  hm_dp_w16(ram, r, 0xC7, 0x1800);
  hm_push8(ram, r, 0xE9); hm_push8(ram, r, 0x6E);          /* JSR $EA15 */
  name_cursor(ram, r);
  hm_pull8(ram, r); hm_pull8(ram, r);
  jsl(ram, r, 0xE96F, 0x84, hm_InputTypeSelector);
  uint8_t page = rd8(ram, r, 0x0993);                      /* SEP #$20 */
  set_a8(r, page);
  r->c = page >= 3;
  if (page == 3) { name_done(ram, r); return false; }
  jsl(ram, r, 0xE97F, 0x82, hm_SUB_82EB57);
  uint8_t drift = *hm_dp(ram, r, 0x96);                    /* SEP #$20 */
  set_a8(r, drift);
  r->c = drift >= 1;
  if (drift == 1) {
    *hm_dp(ram, r, 0x96) = 0;
    wr16(ram, r, 0x0140, (uint16_t)(rd16(ram, r, 0x0140) + 1));   /* REP #$20 */
    wr16(ram, r, 0x0142, (uint16_t)(rd16(ram, r, 0x0142) - 1));
  } else {
    *hm_dp(ram, r, 0x96) = (uint8_t)(drift + 1);
  }
  for (int i = 0; i < 4; i++) {                            /* REP #$20 */
    r->a = rd16(ram, r, CURSOR[i].keep);
    hm_dp_w16(ram, r, CURSOR[i].dp, r->a);
  }
  r->a = rd16(ram, r, 0x0995);
  hm_dp_w16(ram, r, 0xA5, r->a);
  jsl(ram, r, 0xE9B6, 0x85, hm_CODE_8580B9);
  jsl(ram, r, 0xE9BA, 0x85, hm_CODE_8582C7);
  jsl(ram, r, 0xE9BE, 0x85, hm_CODE_858CB2);
  jsl(ram, r, 0xE9C2, 0x85, hm_PrepareOAMData);
  *hm_dp(ram, r, 0x00) = 0;                                /* SEP #$20; STZ $00 */
  g_hm_next = 0x82E95F;
  return true;                                             /* JML $82:E95F */
}

void hm_Unk_NamesInput(uint8_t *ram, HmRegs *r)
{
  uint8_t entry = r->p;
  set_a8(r, 0x5F);                                         /* SEP #$20 */
  *hm_dp(ram, r, 0x22) = 0x5F;
  jsl(ram, r, 0xE812, 0x80, hm_UNK_Audio5);
  jsl(ram, r, 0xE816, 0x83, hm_UNK_Audio25);
  set_a8(r, 0x0F); *hm_dp(ram, r, 0x92) = 0x0F;            /* SEP #$20 */
  set_a8(r, 0x03); *hm_dp(ram, r, 0x93) = 0x03;
  set_a8(r, 0x01); *hm_dp(ram, r, 0x94) = 0x01;
  jsl(ram, r, 0xE828, 0x80, hm_ScreenFadeout);
  jsl(ram, r, 0xE82C, 0x80, hm_ForceBlank);
  jsl(ram, r, 0xE830, 0x80, hm_ZeroesVRAM);
  jsl(ram, r, 0xE834, 0x80, hm_Zeroes42Pointers);
  jsl(ram, r, 0xE838, 0x85, hm_ClearSpriteDataTables);
  jsl(ram, r, 0xE83C, 0x85, hm_InitializeOBJs);
  r->a = rd16(ram, r, 0x0196);                             /* REP #$20 */
  wr16(ram, r, 0x0198, r->a);
  wr16(ram, r, 0x0196, 0);
  wr16(ram, r, 0x0905, 0);
  set_a8(r, 0x05);                                         /* SEP #$20 */
  wr8(ram, r, 0x019A, 0x05);
  set_a8(r, 0x04);
  jsl(ram, r, 0xE859, 0x80, hm_ManageGraphicPresets);
  jsl(ram, r, 0xE85D, 0x80, hm_BackgroundsManager);
  r->a = 0x006F;                                           /* REP #$20 */
  jsl(ram, r, 0xE866, 0x80, hm_LoadFirstHalfPaletteToWRAM);
  colours(ram, r, 0, 0x0000, 0x8C00, 0xE889);
  colours(ram, r, 1, 0x0080, 0xDE00, 0xE8AC);
  jsl(ram, r, 0xE8B0, 0x80, hm_StartProgramedDMA);
  jsl(ram, r, 0xE8B4, 0x81, hm_UNK_PresetsMemory3);
  static const uint16_t start[4] = { 0x0219, 0x0000, 0x0028, 0x0044 };
  for (int i = 0; i < 4; i++) {                            /* REP #$20 */
    r->a = start[i];
    hm_dp_w16(ram, r, CURSOR[i].dp, start[i]);
    wr16(ram, r, CURSOR[i].keep, start[i]);
  }
  hm_dp_w16(ram, r, 0xA3, 0);
  jsl(ram, r, 0xE8DC, 0x85, hm_CreateGameObject);
  r->a = hm_dp_r16(ram, r, 0xA5);                          /* REP #$20 */
  wr16(ram, r, 0x0995, r->a);
  hm_jsl(ram, r, 0xE8EA, 0x82);                            /* JSL ... */
  hm_SUB_82EAB4(ram, r);
  hm_pull8(ram, r); hm_pull8(ram, r);                      /* ... RTS: the bank stays */
  jsl(ram, r, 0xE8EB, 0x82, hm_SUB_82EA80);
  zero16_all(ram, r, 0x013C, 0x0146);                      /* REP #$20: the scrolls */
  jsl(ram, r, 0xE903, 0x83, hm_UNK_Audio21);
  jsl(ram, r, 0xE907, 0x83, hm_UNK_Audio20);
  jsl(ram, r, 0xE90B, 0x83, hm_UNK_Audio22);
  uint8_t song = rd8(ram, r, 0x0110);                      /* SEP #$20 */
  set_a8(r, song);
  wr8(ram, r, 0x0117, song);
  jsl(ram, r, 0xE917, 0x80, hm_ResetForceBlank);
  jsl(ram, r, 0xE91B, 0x80, hm_WaitForNMI);
  set_a8(r, 0x03); *hm_dp(ram, r, 0x92) = 0x03;            /* SEP #$20 */
  set_a8(r, 0x03); *hm_dp(ram, r, 0x93) = 0x03;
  set_a8(r, 0x0F); *hm_dp(ram, r, 0x94) = 0x0F;
  jsl(ram, r, 0xE92D, 0x80, hm_ScreenFadein);
  wr16(ram, r, 0x0991, 0);                                 /* REP #$20 */
  set_a8(r, 0x00);                                         /* SEP #$20 */
  wr8(ram, r, 0x0993, 0);
  wr8(ram, r, 0x0994, 0);
  wr8(ram, r, 0x018B, 0);
  set_a8(r, 0xB1);
  for (uint16_t i = 0; i < 4; i++) wr8(ram, r, (uint16_t)(TYPED + i), 0xB1);
  *hm_dp(ram, r, 0x94) = 0;
  *hm_dp(ram, r, 0x96) = 0;
  *hm_dp(ram, r, 0x97) = 0;
  name_frame(ram, r, entry);
  hm_pull8(ram, r);   /* the check stops after the first frame, and takes the stray byte off */
}

/* $82:E95F: one frame of the names screen on its own (entered as the
 * JML back to it does) */
void hm_Unk_NamesInput_E95F(uint8_t *ram, HmRegs *r)
{
  name_frame(ram, r, r->p);
}
