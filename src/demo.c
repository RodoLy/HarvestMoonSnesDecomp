/*
 * demo.c — the demos the title screen shows when nobody presses a button:
 * the game plays itself on a farm of its own, driven by an event script.
 *
 * Both routines set up a fresh game (the farm's map from the ROM, a day's
 * flags from zero, a few tools and seeds), start the demo's script (group
 * $47), load its map, and then run the game a frame at a time, as GameLoop
 * does, until the script says it is over ($7F1F5C bit 2). Then the
 * chickens go, $95 = 9 and $90 = 0, and back to the title screen's demo
 * check (IntroScreen, $82:D871, JML).
 * The checks stop after the first frame (the JMP back to the wait) or at
 * that JML (HM_TOLOOP).
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

typedef void Fn(uint8_t *ram, HmRegs *r);

static uint32_t db(const HmRegs *r, uint16_t a) { return (uint32_t)r->db << 16 | a; }
static uint8_t rd8(uint8_t *ram, const HmRegs *r, uint16_t a) { return hm_bus_rd8(ram, db(r, a)); }
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

/* What a demo starts from. The two differ in a few bytes: */
typedef struct {
  uint16_t base;        /* the routine's address: the code is laid out the same from there */
  uint16_t flags60;     /* $7F1F60 */
  uint8_t shed2;        /* $7F1F01: the shed's second row of tools */
  bool demo;            /* DemoPresentation: its own seeds, $08FD/$08FF, and $7F1F49 kept */
} Demo;

/* the fade out (from 15, 3 frames a level, to 1) and the fresh day */
static void fresh_game(uint8_t *ram, HmRegs *r, const Demo *d)
{
  set_a8(r, 0x0F); *hm_dp(ram, r, 0x92) = 0x0F;            /* SEP #$20; REP #$10 */
  set_a8(r, 0x03); *hm_dp(ram, r, 0x93) = 0x03;
  set_a8(r, 0x01); *hm_dp(ram, r, 0x94) = 0x01;
  jsl(ram, r, (uint16_t)(d->base + 0x10), 0x80, hm_ScreenFadeout);
  set_a8(r, 0x06);                                         /* SEP #$20: 6:00:00 */
  hm_bus_wr8(ram, 0x7F1F1C, 6);
  set_a8(r, 0x00);
  hm_bus_wr8(ram, 0x7F1F1D, 0);
  hm_bus_wr8(ram, 0x7F1F1E, 0);
  set_a8(r, 0x01);
  *hm_dp(ram, r, 0x95) = 0x01;
  *hm_dp(ram, r, 0x94) = 0;
  r->a = 0;                                                /* REP #$20: the day's flags */
  static const uint32_t day[] = { 0x7F1F5A, 0x7F1F5C, 0x7F1F5E, 0x7F1F60, 0x7F1F74, 0x7F1F76, 0x7F1F78 };
  for (unsigned i = 0; i < 7; i++) hm_bus_wr16(ram, day[i], 0);
  r->a = d->flags60;
  hm_bus_wr16(ram, 0x7F1F60, d->flags60);
  r->a = 0;                                                /* the story's */
  for (uint32_t f = 0x7F1F64; f <= 0x7F1F72; f += 2) hm_bus_wr16(ram, f, 0);
  if (!d->demo) hm_bus_wr8(ram, 0x7F1F49, 0);              /* SEP #$20 */
  set_a8(r, 0x8F); hm_bus_wr8(ram, 0x7F1F00, 0x8F);        /* the shed */
  set_a8(r, d->shed2); hm_bus_wr8(ram, 0x7F1F01, d->shed2);
  set_a8(r, 0x00); hm_bus_wr8(ram, 0x7F1F02, 0); hm_bus_wr8(ram, 0x7F1F03, 0);
  wr8(ram, r, 0x0921, 0);                                  /* nothing in the hands */
  wr8(ram, r, 0x0923, 0);
  set_a8(r, 0x01);                                         /* seeds: one bag each */
  wr8(ram, r, 0x0927, 1);
  wr8(ram, r, 0x092B, 1);
  if (!d->demo) { wr8(ram, r, 0x0928, 1); wr8(ram, r, 0x0929, 1); }
  else { wr16(ram, r, 0x08FD, 0); wr16(ram, r, 0x08FF, 0); }   /* REP #$20 */
  wr16(ram, r, 0x0915, 0);                                 /* REP #$20: the player */
  hm_dp_w16(ram, r, 0xD2, 0);
  hm_dp_w16(ram, r, 0xD4, 0);
  r->a = (uint16_t)(hm_dp_r16(ram, r, 0xD2) | 0x0001);     /* REP #$30 */
  hm_dp_w16(ram, r, 0xD2, r->a);
  r->a = 0;
  hm_dp_w16(ram, r, 0xD4, 0);
  hm_dp_w16(ram, r, 0xDA, 0);
  wr16(ram, r, 0x0911, 0);
  wr16(ram, r, 0x0901, 0);
  wr8(ram, r, 0x0973, 0);                                  /* SEP #$20: the clock stopped */
  hm_bus_wr8(ram, 0x7F1F19, 0);                            /* spring */
  wr8(ram, r, 0x091D, 0);
  r->a = (uint16_t)(0xFFFD & hm_dp_r16(ram, r, 0xD2));     /* REP #$30 */
  hm_dp_w16(ram, r, 0xD2, r->a);
  wr16(ram, r, 0x0932, 0);                                 /* the animals fed */
  wr16(ram, r, 0x0934, 0);
  r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F5C) & 0xFFFB);
  hm_bus_wr16(ram, 0x7F1F5C, r->a);
}
/* the screen and objects cleared, the farm's map from the ROM, BG3 */
static void fresh_screen(uint8_t *ram, HmRegs *r, uint16_t at)
{
  jsl(ram, r, at, 0x80, hm_ZeroesVRAM);
  jsl(ram, r, (uint16_t)(at + 4), 0x80, hm_ZeroesCGRAM);
  jsl(ram, r, (uint16_t)(at + 8), 0x80, hm_Zeroes42Pointers);
  jsl(ram, r, (uint16_t)(at + 12), 0x85, hm_ClearSpriteDataTables);
  jsl(ram, r, (uint16_t)(at + 16), 0x85, hm_InitializeOBJs);
  jsl(ram, r, (uint16_t)(at + 20), 0x81, hm_UNK_PresetsMemory3);
  jsl(ram, r, (uint16_t)(at + 24), 0x84, hm_ClearsofAllCCStructs);
  jsl(ram, r, (uint16_t)(at + 28), 0x82, hm_LoadDefaultFarmMap);
  r->a = 0x0100;                                           /* REP #$20 */
  wr16(ram, r, 0x0146, 0x0100);
}
/* LDA #0; LDX #$47; LDY #script; JSL UNK_LoadCCDataLong */
static void demo_script(uint8_t *ram, HmRegs *r, uint16_t script, uint16_t at)
{
  r->a = 0; r->x = 0x0047; r->y = script;                  /* REP #$30 */
  jsl(ram, r, at, 0x84, hm_UNK_LoadCCDataLong);
}
/* the scripts run once, and map $098B is entered (its music, its load) */
static void demo_map(uint8_t *ram, HmRegs *r, uint16_t at)
{
  jsl(ram, r, at, 0x84, hm_IterateCCStructures);
  uint8_t map = rd8(ram, r, 0x098B);                       /* SEP #$20 */
  set_a8(r, map);
  *hm_dp(ram, r, 0x22) = map;
  jsl(ram, r, (uint16_t)(at + 0x0B), 0x80, hm_UNK_Audio5);
  set_a8(r, rd8(ram, r, 0x098B));                          /* SEP #$20 */
  jsl(ram, r, (uint16_t)(at + 0x14), 0x80, hm_SUB_80972C);
  r->y = 0;                                                /* REP #$30 */
}
/* one frame: the wait ($90 = 0; the NMI comes back to the LDA $00 at
 * `wait`), the game's work from `work` */
static void demo_frame(uint8_t *ram, HmRegs *r, uint16_t wait, uint16_t work, uint8_t entry)
{
  hm_dp_w16(ram, r, 0x90, 0);                              /* REP #$20; STZ $90 */
  for (;;) {
    uint8_t ready = *hm_dp(ram, r, 0x00);                  /* SEP #$20; LDA $00; BEQ */
    set_a8(r, ready);
    if (ready) break;
    hm_nmi(ram, r, wait, (uint8_t)(entry | 0x20));
  }
  r->a = 0x1800;                                           /* REP #$20 */
  hm_dp_w16(ram, r, 0xC7, 0x1800);
  hm_game_frame(ram, r, work);
}
/* the end: the chickens' first 8 bytes cleared, $95 = 9, $90 = 0 */
static void demo_end(uint8_t *ram, HmRegs *r)
{
  for (r->x = 0; r->x != 8; r->x++) {                      /* REP #$30; SEP #$20 */
    set_a8(r, 0);
    hm_bus_wr8(ram, 0x7EC286 + r->x, 0);
  }
  r->c = 1;                                                /* CPX #8 */
}
static void demo_leave(uint8_t *ram, HmRegs *r)
{
  set_a8(r, 0x09);                                         /* SEP #$20 */
  *hm_dp(ram, r, 0x95) = 0x09;
  hm_dp_w16(ram, r, 0x90, 0);                              /* REP #$20 */
  g_hm_next = 0x82D871;
  /* JML IntroScreen's demo check: the check stops here (HM_TOLOOP) */
}

/* ─────────────────────────────────────────────────────────────────────────
 * SUB_82D1C0 ($82:D1C0, JML from the title screen): the first demo:
 * script 0 of group $47; $7F1F60 = $0108, $7F1F49 = 0, the shed's second
 * row $88, a bag of grass, turnip, corn and tomato seeds.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_SUB_82D1C0(uint8_t *ram, HmRegs *r)
{
  static const Demo d = { 0xD1C0, 0x0108, 0x88, false };
  uint8_t entry = r->p;
  fresh_game(ram, r, &d);
  fresh_screen(ram, r, 0xD2F3);
  demo_script(ram, r, 0, 0xD326);
  demo_map(ram, r, 0xD32A);
  hm_SUB_82D1C0_D347(ram, r);
  (void)entry;
}
/* its frames ($82:D347) */
void hm_SUB_82D1C0_D347(uint8_t *ram, HmRegs *r)
{
  demo_frame(ram, r, 0xD34D, 0xD358, r->p);
  r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F5C) & 0x0004);  /* REP #$20 */
  if (!r->a) {
    *hm_dp(ram, r, 0x00) = 0;                              /* SEP #$20; STZ $00 */
    g_hm_next = 0x82D347;
    return;                                                /* JMP back to the wait: the check stops */
  }
  demo_end(ram, r);
  demo_leave(ram, r);
}

/* ─────────────────────────────────────────────────────────────────────────
 * DemoPresentation ($82:D3C7, JML from the title screen): the other
 * demos, one after the other: $7F1F48 (0-$0B) picks the script of group
 * $47 (2, 4, 6, 7, 9, $0B, $0D, $11, $15, $17, $19, $1B; any other value
 * as 0) and becomes the next one (after $0B, 0 again). $7F1F60 = 8, the
 * shed's second row $EC, a bag of grass and turnip seeds, $08FD/$08FF =
 * 0. It also ends with $7F1F60 bit 7, and, if it ended on the farm (map
 * below 4), the farm's map is kept (CopyCurrentMaptoFarmMap).
 * ───────────────────────────────────────────────────────────────────────── */
void hm_DemoPresentation(uint8_t *ram, HmRegs *r)
{
  static const Demo d = { 0xD3C7, 0x0008, 0xEC, true };
  static const uint8_t SCRIPTS[12] = { 0x02, 0x04, 0x06, 0x07, 0x09, 0x0B, 0x0D, 0x11, 0x15, 0x17, 0x19, 0x1B };
  uint8_t entry = r->p;
  fresh_game(ram, r, &d);
  fresh_screen(ram, r, 0xD4F4);
  uint8_t step = hm_bus_rd8(ram, 0x7F1F48);                /* SEP #$20 */
  set_a8(r, step);
  for (uint8_t k = 1; k <= 0x0B && step; k++) {            /* CMP #k; BNE */
    r->c = step >= k;
    if (step == k) break;
  }
  uint8_t which = step <= 0x0B ? step : 0;
  demo_script(ram, r, SCRIPTS[which], (uint16_t)(0xD57F + 0x1A * which));
  uint8_t next = which == 0x0B ? 0 : (uint8_t)(which + 1);
  set_a8(r, next);                                         /* SEP #$20 */
  hm_bus_wr8(ram, 0x7F1F48, next);
  demo_map(ram, r, 0xD6AC);
  hm_DemoPresentation_D6C9(ram, r);
  (void)entry;
}
/* its frames ($82:D6C9) */
void hm_DemoPresentation_D6C9(uint8_t *ram, HmRegs *r)
{
  demo_frame(ram, r, 0xD6CF, 0xD6DA, r->p);
  r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F5C) & 0x0004);  /* REP #$20 */
  if (!r->a) {
    r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F60) & 0x0080);
    if (!r->a) {
      *hm_dp(ram, r, 0x00) = 0;                            /* SEP #$20; STZ $00 */
      g_hm_next = 0x82D6C9;
      return;                                              /* JMP back to the wait: the check stops */
    }
  }
  demo_end(ram, r);
  uint8_t map = *hm_dp(ram, r, 0x22);                      /* SEP #$20 */
  set_a8(r, map);
  r->c = map >= 4;
  if (map < 4) jsl(ram, r, 0xD74C, 0x82, hm_CopyCurrentMaptoFarmMap);
  demo_leave(ram, r);
}
