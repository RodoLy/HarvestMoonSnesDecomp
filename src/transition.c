/*
 * transition.c — going from one map to another: loading the new one.
 *
 * These routines are made of many others and wait frames (WaitForNMI,
 * the fades). The flags those waits save (PHP) and the NMI pushes depend
 * on N, Z and V left by the routines before, which the rewrites do not
 * follow; the carry and the index width, which the waits give back with
 * their PLP, are followed. So they are checked with HM_LOOSE: the stack
 * below the final stack pointer is not compared (hm_decomp.h).
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
static uint16_t dp16(uint8_t *ram, const HmRegs *r, unsigned a) { return hm_dp_r16(ram, r, a); }
static void set_dp16(uint8_t *ram, const HmRegs *r, unsigned a, uint16_t v) { hm_dp_w16(ram, r, a, v); }

/* JSL at `at` to a routine of `bank`. A routine that saves the flags
 * (PHP) gives back with its PLP the carry it got: r->p carries the carry
 * as it is now, 8-bit A, 16-bit indexes, and D and I of the entry. */
static void jsl(uint8_t *ram, HmRegs *r, uint16_t at, uint8_t bank, Fn *fn)
{
  uint8_t keep = r->p;
  r->p = (uint8_t)((keep & 0x0C) | 0x20 | (r->c & 1));
  hm_jsl(ram, r, (uint16_t)(at + 3), bank);
  fn(ram, r);
  hm_rtl(ram, r);
  r->p = keep;
}
/* JSL to a routine that saves the flags (PHP): the carry as it is now and
 * the index width `x8`, with 8-bit A and D and I of `entry` (N, Z and V
 * are not followed: HM_LOOSE) */
static void jsl_flags(uint8_t *ram, HmRegs *r, uint16_t at, uint8_t bank, Fn *fn, uint8_t entry, int x8)
{
  r->p = (uint8_t)((entry & 0x0C) | 0x20 | (x8 ? 0x10 : 0) | (r->c & 1));
  hm_jsl(ram, r, (uint16_t)(at + 3), bank);
  fn(ram, r);
  hm_rtl(ram, r);
  r->p = entry;
}
static void state_off(uint8_t *ram, HmRegs *r, uint16_t bits)  /* LDA #b; EOR #$FFFF; AND $D2 */
{
  r->a = (uint16_t)(dp16(ram, r, 0xD2) & ~bits);
  set_dp16(ram, r, 0xD2, r->a);
}

/* ─────────────────────────────────────────────────────────────────────────
 * SUB_80972C ($80:972C, JSL, RTL): loads map A and fades it in.
 *  - The time runs ($0973 = 1) on maps whose graphics record has bit 5
 *    in its byte 1 (GetBackgroundData), unless $7F1F5C bit 0.
 *  - With bit 7 or 6 of that byte (outdoors), and unless $7F1F5C bit 1
 *    (or bit 6 alone with $0196 bit 2): the weather's own layer first.
 *    Rain ($0196 bit 1): picture $57 and its four colour cycles
 *    (UNK_SetPointer42, $80:B9D7-$80:B9E2); snow (bit 2) before 17:00:
 *    picture $58; bit 3: picture $59 and its cycles ($80:B9EA-$80:B9F5).
 *  - Then the text box's graphics, the map itself (LoadMap,
 *    BackgroundsManager), its colours (ChangePalettebyTime,
 *    PrepareSpritePalette, CallIndexedSubrutines, UNK_CustomisePalette,
 *    UNK_BigLoop) sent at once to CGRAM ($7F:0900, $200 bytes).
 *  - BG2 starts at the camera ($0140 / $0142; the tool shed at $0100
 *    down), and a long list of things is reset: the BG2 drift, the step
 *    counter, the player's last position, $D2 bits 7 and 12...
 *  - What the player holds comes along: an item ($091D, not the tools
 *    $0D-$0F nor $57) becomes an item object again (SUB_8180B7,
 *    SpawnMapObject) and he holds it (animation $14 + facing); else his
 *    hands are empty.
 *  - The player's object (CODE_81CFA0) and frame (SUB_81BFB7), the
 *    animals (PlaceAnimalsOnMap, unless $7F1F5E bit 1), the music
 *    (UNK_Audio21, 20, 22: the new song becomes the one playing), the
 *    item's noises; then two frames of objects and scripts are prepared
 *    (BAAAA, IterateCCStructures, the sprites), each sent by the NMI, and
 *    the screen is lit and faded in from 3 (ScreenFadein).
 * Leftovers: those of ScreenFadein.
 * ───────────────────────────────────────────────────────────────────────── */
static void colour_cycle(uint8_t *ram, HmRegs *r, uint16_t at, uint16_t table, uint8_t n, uint8_t slot)
{
  r->a = table;                                            /* REP #$20; SEP #$10 */
  hm_dp_w16(ram, r, 0x72, table);
  *hm_dp(ram, r, 0x74) = 0x80;
  r->a = (uint16_t)((table & 0xFF00) | n);
  r->x = slot;
  r->y = 0;
  jsl_flags(ram, r, at, 0x80, hm_UNK_SetPointer42, r->p, 1);
}
static void weather_picture(uint8_t *ram, HmRegs *r, uint8_t picture, uint16_t at)
{
  set_a8(r, picture);
  *hm_dp(ram, r, 0x22) = picture;
  jsl(ram, r, at, 0x80, hm_BackgroundsManager);
}
static void objects_frame(uint8_t *ram, HmRegs *r, uint16_t at, uint8_t entry)
{
  r->a = 0x1800;                                           /* REP #$20 */
  hm_dp_w16(ram, r, 0xC7, 0x1800);
  jsl(ram, r, at, 0x81, hm_BAAAA);
  jsl(ram, r, (uint16_t)(at + 4), 0x84, hm_IterateCCStructures);
  jsl(ram, r, (uint16_t)(at + 8), 0x85, hm_CODE_8582C7);
  jsl(ram, r, (uint16_t)(at + 12), 0x85, hm_CODE_858CB2);
  jsl(ram, r, (uint16_t)(at + 16), 0x85, hm_PrepareOAMData);
  *hm_dp(ram, r, 0x00) = 0;                                /* SEP #$20; STZ $00 */
  jsl_flags(ram, r, (uint16_t)(at + 24), 0x80, hm_WaitForNMI, entry, 0);
}

void hm_SUB_80972C(uint8_t *ram, HmRegs *r)
{
  uint8_t entry = r->p;
  uint8_t map = (uint8_t)r->a;
  *hm_dp(ram, r, 0x22) = map;
  hm_push8(ram, r, map);                                   /* PHA */
  wr8(ram, r, 0x0973, 0);
  r->y = 1;
  jsl(ram, r, 0x973D, 0x80, hm_GetBackgroundData);
  uint8_t kind = (uint8_t)r->a;
  hm_push8(ram, r, kind);
  set_a8(r, kind & 0x20);
  if (kind & 0x20) {
    r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F5C) & 0x0001);
    if (!r->a) { set_a8(r, 1); wr8(ram, r, 0x0973, 1); }
  }
  kind = hm_pull8(ram, r);                                 /* PLA */
  set_a8(r, kind & 0xC0);
  bool weather = (kind & 0xC0) != 0;
  if (weather) {
    set_a8(r, kind & 0x80);
    if (!(kind & 0x80)) {
      r->a = (uint16_t)(rd16(ram, r, 0x0196) & 0x0004);
      if (r->a) weather = false;
    }
  }
  if (weather) {
    r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F5C) & 0x0002);
    if (r->a) weather = false;
  }
  if (weather) {
    r->a = (uint16_t)(rd16(ram, r, 0x0196) & 0x0002);
    if (r->a) {                                            /* rain */
      weather_picture(ram, r, 0x57, 0x9793);
      colour_cycle(ram, r, 0x97AE, 0xB9D7, 0x0C, 0);
      colour_cycle(ram, r, 0x97C9, 0xB9DC, 0x0D, 1);
      colour_cycle(ram, r, 0x97E4, 0xB9E2, 0x0E, 2);
      colour_cycle(ram, r, 0x97FF, 0xB9DF, 0x0F, 3);
    } else {
      r->a = (uint16_t)(rd16(ram, r, 0x0196) & 0x0004);
      if (r->a) {                                          /* snow, before 17:00 */
        uint8_t hour = hm_bus_rd8(ram, 0x7F1F1C);
        set_a8(r, hour);
        r->c = hour >= 0x11;
        if (hour < 0x11) weather_picture(ram, r, 0x58, 0x9821);
      } else {
        r->a = (uint16_t)(rd16(ram, r, 0x0196) & 0x0008);
        if (r->a) {
          weather_picture(ram, r, 0x59, 0x9838);
          colour_cycle(ram, r, 0x9853, 0xB9EA, 0x0C, 0);
          colour_cycle(ram, r, 0x986E, 0xB9EF, 0x0D, 1);
          colour_cycle(ram, r, 0x9889, 0xB9F2, 0x0E, 2);
          colour_cycle(ram, r, 0x98A4, 0xB9F5, 0x0F, 3);
        }
      }
    }
  }

  jsl(ram, r, 0x98A8, 0x83, hm_LoadDialogueBox);
  map = hm_pull8(ram, r);                                  /* SEP #$20; PLA */
  set_a8(r, map);
  *hm_dp(ram, r, 0x22) = map;
  jsl(ram, r, 0x98B1, 0x82, hm_LoadMap);
  jsl(ram, r, 0x98B5, 0x80, hm_BackgroundsManager);
  jsl(ram, r, 0x98B9, 0x80, hm_ChangePalettebyTime);
  jsl(ram, r, 0x98BD, 0x80, hm_PrepareSpritePalette);
  jsl(ram, r, 0x98C1, 0x80, hm_CallIndexedSubrutines);
  jsl(ram, r, 0x98C5, 0x80, hm_UNK_CustomisePalette);
  jsl(ram, r, 0x98C9, 0x80, hm_UNK_BigLoop);
  set_a8(r, 0);                                            /* the colours, to CGRAM */
  *hm_dp(ram, r, 0x27) = 0;
  set_a8(r, 0x22);
  *hm_dp(ram, r, 0x29) = 0x22;
  r->y = 0x0200;
  r->x = 0;
  r->a = 0x0900;
  hm_dp_w16(ram, r, 0x72, 0x0900);
  set_a8(r, 0x7F);
  *hm_dp(ram, r, 0x74) = 0x7F;
  jsl(ram, r, 0x98EC, 0x80, hm_AddProgrammedDMA);
  jsl(ram, r, 0x98F0, 0x80, hm_StartLastPreparedDMA);

  hm_dp_w16(ram, r, 0x1E, 0);                              /* REP #$30 */
  r->a = dp16(ram, r, 0xF5);
  wr16(ram, r, 0x0140, r->a);
  r->a = dp16(ram, r, 0xF7);
  wr16(ram, r, 0x0142, r->a);
  set_a8(r, *hm_dp(ram, r, 0x22));
  r->c = *hm_dp(ram, r, 0x22) >= 0x26;
  if (*hm_dp(ram, r, 0x22) == 0x26) {                      /* the tool shed */
    r->a = 0x0100;
    wr16(ram, r, 0x0142, 0x0100);
  }
  wr8(ram, r, 0x091C, 0);
  r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F5A) & 0xFDFF);
  hm_bus_wr16(ram, 0x7F1F5A, r->a);
  r->a = 0;
  hm_bus_wr16(ram, 0x7F1F7A, 0);
  wr16(ram, r, 0x0878, 0);
  r->a = dp16(ram, r, 0xD6);
  wr16(ram, r, 0x0907, r->a);
  r->a = dp16(ram, r, 0xD8);
  wr16(ram, r, 0x0909, r->a);
  wr8(ram, r, 0x098A, 0);
  wr8(ram, r, 0x0919, 0);
  state_off(ram, r, 0x0080);
  wr16(ram, r, 0x08FD, 0);
  wr16(ram, r, 0x08FF, 0);
  state_off(ram, r, 0x1000);

  uint8_t held = rd8(ram, r, 0x091D);                      /* what the player holds */
  set_a8(r, held);
  bool empty = true;
  if (held) {
    r->c = held >= 0x0D;
    bool tool = held == 0x0D || held == 0x0E || held == 0x0F;
    if (!tool) { r->c = held >= 0x57; tool = held == 0x57; }
    if (!tool) {
      wr8(ram, r, 0x0984, held);
      r->y = 1;                                            /* REP #$20; LDY #1 */
      jsl(ram, r, 0x997F, 0x81, hm_SUB_8180B7);
      r->a = rd16(ram, r, 0x090B);
      wr16(ram, r, 0x0980, r->a);
      r->a = rd16(ram, r, 0x090D);
      wr16(ram, r, 0x0982, r->a);
      set_a8(r, 1);
      wr8(ram, r, 0x0974, 1);
      wr8(ram, r, 0x0975, 1);
      set_a8(r, 2);
      wr8(ram, r, 0x0976, 2);
      jsl(ram, r, 0x99A0, 0x81, hm_SpawnMapObject);
      uint32_t s = 0x14u + dp16(ram, r, 0xDA);             /* REP #$30; CLC; ADC $DA */
      r->c = s > 0xFFFF;
      r->a = (uint16_t)s;
      wr16(ram, r, 0x0901, r->a);
      empty = false;
    } else {
      uint32_t s = dp16(ram, r, 0xDA);                     /* LDA #0; CLC; ADC $DA */
      r->c = 0;
      r->a = (uint16_t)s;
      wr16(ram, r, 0x0901, r->a);
    }
  }
  if (empty) {
    wr8(ram, r, 0x091D, 0);
    state_off(ram, r, 0x0002);
  }

  jsl(ram, r, 0x99CD, 0x81, hm_CODE_81CFA0);
  jsl(ram, r, 0x99D1, 0x81, hm_SUB_81BFB7);
  r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F5E) & 0x0002);
  if (!r->a) jsl(ram, r, 0x99E0, 0x83, hm_PlaceAnimalsOnMap);
  r->a = 0;
  set_dp16(ram, r, 0xD4, 0);
  jsl(ram, r, 0x99EB, 0x83, hm_UNK_Audio21);
  jsl(ram, r, 0x99EF, 0x83, hm_UNK_Audio20);
  jsl(ram, r, 0x99F3, 0x83, hm_UNK_Audio22);
  jsl(ram, r, 0x99F7, 0x82, hm_ToolUsedSound2);
  set_a8(r, rd8(ram, r, 0x0110));
  wr8(ram, r, 0x0117, (uint8_t)r->a);
  jsl_flags(ram, r, 0x9A03, 0x80, hm_WaitForNMI, entry, 0);
  objects_frame(ram, r, 0x9A0E, entry);
  objects_frame(ram, r, 0x9A31, entry);
  jsl(ram, r, 0x9A4D, 0x80, hm_ResetForceBlank);
  *hm_dp(ram, r, 0x92) = 3;
  *hm_dp(ram, r, 0x93) = 3;
  *hm_dp(ram, r, 0x94) = 0x0F;
  r->a = (uint16_t)((r->a & 0xFF00) | 0x0F);
  jsl_flags(ram, r, 0x9A5F, 0x80, hm_ScreenFadein, entry, 0);
}

/* ─────────────────────────────────────────────────────────────────────────
 * UNK_ScreenTransition ($80:96D3, JSL, RTL): enters map $098B: $0196
 * bits 0, 5, 14 and 15 and $7F1F5C bits 0-3 cleared, $D2 bit 14 set; the
 * old map's things cleared (ZeroesPartialVRAM from $7000,
 * Zeroes42Pointers, ClearsTimeofDayPalette, ClearSpriteDataTables,
 * InitializeOBJs, UNK_PresetsMemory3, ClearsofAllCCStructs); then the
 * new map's people (RunsFunctionbyIndex, IterateCCStructures) and the map
 * itself (SUB_80972C).
 * Leftovers: those of SUB_80972C.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_UNK_ScreenTransition(uint8_t *ram, HmRegs *r)
{
  r->a = (uint16_t)(rd16(ram, r, 0x0196) & 0x3FDE);        /* REP #$30 */
  wr16(ram, r, 0x0196, r->a);
  r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F5C) & 0xFFF0);
  hm_bus_wr16(ram, 0x7F1F5C, r->a);
  r->a = (uint16_t)(dp16(ram, r, 0xD2) | 0x4000);
  set_dp16(ram, r, 0xD2, r->a);
  r->a = 0x7000;
  jsl(ram, r, 0x96F7, 0x80, hm_ZeroesPartialVRAM);
  jsl(ram, r, 0x96FB, 0x80, hm_Zeroes42Pointers);
  jsl(ram, r, 0x96FF, 0x80, hm_ClearsTimeofDayPalette);
  jsl(ram, r, 0x9703, 0x85, hm_ClearSpriteDataTables);
  jsl(ram, r, 0x9707, 0x85, hm_InitializeOBJs);
  jsl(ram, r, 0x970B, 0x81, hm_UNK_PresetsMemory3);
  jsl(ram, r, 0x970F, 0x84, hm_ClearsofAllCCStructs);
  uint8_t map = rd8(ram, r, 0x098B);                       /* SEP #$20 */
  set_a8(r, map);
  *hm_dp(ram, r, 0x22) = map;
  jsl(ram, r, 0x971A, 0x83, hm_RunsFunctionbyIndex);
  jsl(ram, r, 0x971E, 0x84, hm_IterateCCStructures);
  set_a8(r, rd8(ram, r, 0x098B));                          /* SEP #$20 */
  jsl(ram, r, 0x9727, 0x80, hm_SUB_80972C);
}

/* ─────────────────────────────────────────────────────────────────────────
 * SetScreenTransition ($80:9671, JSL, RTL): with $0196 bit 14 (a map to
 * go to), unless $7F1F60 bit 6 alone (without bit 8) holds it: the music
 * of map $098B is chosen and faded in (UNK_Audio5, UNK_Audio25; not with
 * $7F1F5E bit 15); with $7F1F60 bit 3 the map becomes $3C; the screen
 * fades out (from 15, 3 frames a level, to 1), the screen is blanked
 * (ForceBlank) and UNK_ScreenTransition follows.
 * Leftovers: those of UNK_ScreenTransition, or A = the bits tested.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_SetScreenTransition(uint8_t *ram, HmRegs *r)
{
  r->a = (uint16_t)(rd16(ram, r, 0x0196) & 0x4000);        /* REP #$30 */
  if (!r->a) return;
  r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F5E) & 0x8000);
  if (!r->a) {
    r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F60) & 0x0100);
    if (!r->a) {
      r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F60) & 0x0040);
      if (r->a) return;
    }
    uint8_t map = rd8(ram, r, 0x098B);                     /* SEP #$20 */
    set_a8(r, map);
    *hm_dp(ram, r, 0x22) = map;
    jsl(ram, r, 0x96A3, 0x80, hm_UNK_Audio5);
    jsl(ram, r, 0x96A7, 0x83, hm_UNK_Audio25);
  }
  r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F60) & 0x0008);  /* REP #$20 */
  if (r->a) { set_a8(r, 0x3C); wr8(ram, r, 0x098B, 0x3C); }
  set_a8(r, 0x0F); *hm_dp(ram, r, 0x92) = 0x0F;            /* SEP #$20 */
  set_a8(r, 0x03); *hm_dp(ram, r, 0x93) = 0x03;
  set_a8(r, 0x01); *hm_dp(ram, r, 0x94) = 0x01;
  jsl(ram, r, 0x96CB, 0x80, hm_ScreenFadeout);
  jsl(ram, r, 0x96CF, 0x80, hm_ForceBlank);
  hm_UNK_ScreenTransition(ram, r);                         /* runs on into it */
}
