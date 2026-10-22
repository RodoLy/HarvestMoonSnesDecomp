/*
 * palette.c — the colours: the palette of each hour and its slow change.
 *
 * The game keeps three copies of the 256 colours (CGRAM) in WRAM:
 *
 *   $7F:0900  what is sent to the console (by DMA, after a change)
 *   $7F:0B00  a palette being prepared (the target of a change, or the
 *             sprite colours of the next map)
 *   $7F:0D00  the colours shown now, used to compute the next step
 *
 * Every map has five palettes, one per part of the day (Time_Palette_Table,
 * $80:BB5C, 6 bytes per map: before 7:00, 7-14, 15-16, 17, 18 and later;
 * $FF = keep the one there is). When the hour changes part,
 * SetTimeofDayPalette points $04-$06 at the new palette and
 * TransitionTimeofDayPalettes moves each colour one step (of 32) towards it
 * every 32 frames, until all are equal.
 *
 * A colour is 15 bits: red in bits 0-4, green 5-9, blue 10-14.
 * PalettePointerTable ($80:B9FD) has a 24-bit pointer per palette (3 bytes
 * each).
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

#define PALETTE_POINTERS   0x80B9FD
#define TIME_PALETTES      0x80BB5C   /* 6 bytes per map */
#define SPRITE_COLOURS     0x80BD9C   /* UNK_Table9: 3 colours per season / time */
#define CHILD_COLOURS      0x80BDFC   /* UNK_Table10: 3 colours per baby stage */
#define SPRITE_PALETTES    0xBE44     /* UNK_Table11 (read with the data bank) */

#define SENT       0x7F0900
#define PREPARED   0x7F0B00
#define SHOWN      0x7F0D00

#define HOUR       0x7F1F1C
#define SEASON     0x7F1F19
#define PREGNANCY  0x7F1F3B
#define CHILDREN   0x7F1F66
#define FLAGS_5E   0x7F1F5E

#define COUNTDOWN  0x017A   /* frames until the next step (to 32) */
#define LOADED     0x017B   /* the palette in use */
#define NEXT       0x017C   /* the palette being moved to */

static uint32_t db_addr(const HmRegs *r, uint16_t a) { return (uint32_t)r->db << 16 | a; }
static uint16_t dp16(uint8_t *ram, const HmRegs *r, unsigned a) { return hm_dp_r16(ram, r, a); }
static void set16(uint8_t *ram, const HmRegs *r, unsigned a, uint16_t v) { hm_dp_w16(ram, r, a, v); }
static uint8_t lda8(HmRegs *r, uint8_t v)
{
  r->a = (uint16_t)((r->a & 0xFF00) | v);
  return v;
}
static void push16(uint8_t *ram, HmRegs *r, uint16_t v)
{
  hm_push8(ram, r, (uint8_t)(v >> 8));
  hm_push8(ram, r, (uint8_t)v);
}
static uint16_t pull16(uint8_t *ram, HmRegs *r)
{
  uint16_t lo = hm_pull8(ram, r);
  return (uint16_t)(lo | hm_pull8(ram, r) << 8);
}

/* palette n -> $72-$74 (or `at`): LDA n; ASL; CLC; ADC n; TAX; ... */
static void palette_pointer(uint8_t *ram, HmRegs *r, uint16_t n, unsigned at)
{
  uint16_t x = (uint16_t)(n * 3);
  r->c = 0;                                   /* n * 3 never carries here */
  r->x = x;
  r->a = hm_bus_rd16(ram, PALETTE_POINTERS + x);
  set16(ram, r, at, r->a);
  r->x = x = (uint16_t)(x + 2);
  lda8(r, hm_bus_rd8(ram, PALETTE_POINTERS + x));
  *hm_dp(ram, r, at + 2) = (uint8_t)r->a;
}

/* the part of the day: 0 before 7:00, 1 7-14, 2 15-16, 3 17, 4 from 18:00 */
static uint16_t part_of_day(uint8_t *ram, HmRegs *r)
{
  static const uint8_t starts[4] = { 7, 15, 17, 18 };
  uint8_t h = lda8(r, hm_bus_rd8(ram, HOUR));
  uint16_t x = 0;
  for (; x < 4; x++) {
    r->c = h >= starts[x];
    if (!r->c) break;
  }
  return r->x = x;
}
/* the map's palette for the part of the day: X = $22 * 6 + part */
static uint8_t time_palette(uint8_t *ram, HmRegs *r, uint16_t part)
{
  set16(ram, r, 0x7E, part);                              /* STX $7E */
  uint16_t map = *hm_dp(ram, r, 0x22);                    /* LDA #0; XBA; LDA $22 */
  set16(ram, r, 0x80, map);
  uint32_t s = (uint32_t)(uint16_t)(map << 2) + map;       /* ASL; ASL; CLC; ADC $80 */
  s = (s & 0xFFFF) + map + (s > 0xFFFF);                   /* ADC $80 */
  s = (s & 0xFFFF) + part + (s > 0xFFFF);                  /* ADC $7E */
  r->c = s > 0xFFFF;
  r->x = (uint16_t)s;
  r->a = hm_bus_rd8(ram, TIME_PALETTES + r->x);            /* LDA #0; XBA; LDA.L */
  return (uint8_t)r->a;
}

/* ─────────────────────────────────────────────────────────────────────────
 * SetTimeofDayPalette ($80:8FC7): start moving to palette A (8 bits).
 * From 18:00 the target is not a palette of the table but $7F:0B00 (the
 * night colours prepared there).
 * ───────────────────────────────────────────────────────────────────────── */
void hm_SetTimeofDayPalette(uint8_t *ram, HmRegs *r)
{
  uint8_t n = (uint8_t)r->a;
  hm_bus_wr8(ram, db_addr(r, NEXT), n);
  hm_bus_wr8(ram, db_addr(r, COUNTDOWN), 0);
  r->a = n;                                               /* XBA; LDA #0; XBA */
  set16(ram, r, 0x7E, n);
  uint8_t h = lda8(r, hm_bus_rd8(ram, HOUR));
  r->c = h >= 0x12;
  if (r->c) {
    set16(ram, r, 0x04, 0x0B00);
    *hm_dp(ram, r, 0x06) = 0x7F;
    r->a = 0x0B7F;
    return;
  }
  palette_pointer(ram, r, n, 0x04);
}

/* ─────────────────────────────────────────────────────────────────────────
 * TransitionTimeofDayPalettes ($80:900C), every frame while time runs.
 * Every 32 frames each colour moves one step in red, green and blue towards
 * the target palette [$04]. Only colour 0 and the colours from 12 on (from
 * 16 on when bit 2 of $0196 is clear) are moved: colours 1-11 are left
 * alone. Before 18:00 the first 128 colours (the backgrounds) change, from
 * 18:00 all 256 (the sprites too). When nothing changed any more, the
 * target is dropped and the new palette is noted as loaded.
 * ───────────────────────────────────────────────────────────────────────── */
static uint16_t step(uint8_t *ram, HmRegs *r, uint16_t now, uint16_t target, unsigned at)
{
  set16(ram, r, at, now);
  r->a = target;
  r->c = target >= now;
  if (target == now) return now;
  now = (uint16_t)(target > now ? now + 1 : now - 1);
  set16(ram, r, at, now);
  lda8(r, 1);
  *hm_dp(ram, r, 0x92) = 1;
  return now;
}

void hm_ClearsTimeofDayPaletteIndexandSetsNext(uint8_t *ram, HmRegs *r)
{
  set16(ram, r, 0x04, 0);
  *hm_dp(ram, r, 0x06) = 0;
  uint8_t next = lda8(r, hm_bus_rd8(ram, db_addr(r, NEXT)));
  hm_bus_wr8(ram, db_addr(r, LOADED), next);
}

void hm_TransitionTimeofDayPalettes(uint8_t *ram, HmRegs *r)
{
  if (!(lda8(r, hm_bus_rd8(ram, db_addr(r, 0x0973))) & 1)) {   /* time stopped */
    lda8(r, 0);
    return;
  }
  r->a = dp16(ram, r, 0x04);
  if (r->a == 0 && lda8(r, *hm_dp(ram, r, 0x06)) == 0) return; /* nothing to do */
  uint8_t count = lda8(r, (uint8_t)(hm_bus_rd8(ram, db_addr(r, COUNTDOWN)) + 1));
  hm_bus_wr8(ram, db_addr(r, COUNTDOWN), count);
  r->c = count >= 0x20;
  if (count != 0x20) return;
  hm_bus_wr8(ram, db_addr(r, COUNTDOWN), 0);

  set16(ram, r, 0x84, r->a = 0x0100);
  uint8_t h = lda8(r, hm_bus_rd8(ram, HOUR));
  r->c = h >= 18;
  if (r->c) set16(ram, r, 0x84, r->a = 0x0200);
  *hm_dp(ram, r, 0x92) = 0;

  uint16_t y = 0;
  do {
    r->c = y >= 2;
    if (y == 2) {
      y = 0x18;
      r->a = hm_bus_rd16(ram, db_addr(r, 0x0196)) & 0x0004;
      if (r->a == 0) y = 0x20;
    }
    r->x = y;
    uint32_t from = hm_dp_long(ram, r, 0x04, y);
    uint16_t now = hm_bus_rd16(ram, SHOWN + y);
    uint16_t want = hm_bus_rd16(ram, from);
    step(ram, r, now & 0x001F, want & 0x001F, 0x7E);
    step(ram, r, (now & 0x03E0) >> 5, (want & 0x03E0) >> 5, 0x80);
    step(ram, r, (now & 0x7C00) >> 10, (want & 0x7C00) >> 10, 0x82);
    /* ASL $80 x5, ASL $82 x10, then red | green | blue */
    set16(ram, r, 0x80, (uint16_t)(dp16(ram, r, 0x80) << 5));
    set16(ram, r, 0x82, (uint16_t)(dp16(ram, r, 0x82) << 10));
    r->a = (uint16_t)(dp16(ram, r, 0x7E) | dp16(ram, r, 0x80) | dp16(ram, r, 0x82));
    hm_bus_wr16(ram, SENT + y, r->a);
    hm_bus_wr16(ram, SHOWN + y, r->a);
    y = (uint16_t)(y + 2);
    r->c = y >= dp16(ram, r, 0x84);
  } while (y != dp16(ram, r, 0x84));
  r->y = y;

  if (lda8(r, *hm_dp(ram, r, 0x92)) == 0) {               /* all equal: done */
    hm_ClearsTimeofDayPaletteIndexandSetsNext(ram, r);
    return;
  }
  /* send the colours: channel 6, to $2122 (CGRAM), from $7F:0900 */
  *hm_dp(ram, r, 0x27) = 6;
  *hm_dp(ram, r, 0x29) = 0x22;
  r->y = dp16(ram, r, 0x84);
  r->x = 0;
  set16(ram, r, 0x72, 0x0900);
  *hm_dp(ram, r, 0x74) = 0x7F;
  r->a = 0x097F;
  hm_jsl(ram, r, 0x9155, 0x80);
  hm_AddProgrammedDMA(ram, r);
  hm_rtl(ram, r);
}

/* ClearsTimeofDayPalette ($80:9166): drop the target palette. */
void hm_ClearsTimeofDayPalette(uint8_t *ram, HmRegs *r)
{
  set16(ram, r, 0x04, 0);
  *hm_dp(ram, r, 0x06) = 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * SUB_80916F / SUB_80919D ($80:916F / $80:919D): put colour A as colour X
 * of palette row Y (16 rows of 16). With $92 = 0 it goes to $7F:0900
 * (SUB_80919D: and to $7F:0D00, so it is not faded away); otherwise only to
 * the prepared palette $7F:0B00.
 * ───────────────────────────────────────────────────────────────────────── */
static uint16_t colour_index(uint8_t *ram, HmRegs *r)
{
  set16(ram, r, 0x82, r->a);
  set16(ram, r, 0x7E, r->y);
  set16(ram, r, 0x80, (uint16_t)(r->x << 1));
  uint32_t s = (uint32_t)(uint16_t)(r->y << 5) + dp16(ram, r, 0x80);
  r->c = s > 0xFFFF;
  r->a = r->x = (uint16_t)s;                              /* TAX */
  lda8(r, *hm_dp(ram, r, 0x92));
  return r->x;
}
void hm_SUB_80916F(uint8_t *ram, HmRegs *r)
{
  uint16_t x = colour_index(ram, r);
  bool prepared = (uint8_t)r->a != 0;
  r->a = dp16(ram, r, 0x82);
  hm_bus_wr16(ram, ((prepared ? PREPARED : SENT) + x) & 0xFFFFFF, r->a);
}
void hm_SUB_80919D(uint8_t *ram, HmRegs *r)
{
  uint16_t x = colour_index(ram, r);
  bool prepared = (uint8_t)r->a != 0;
  r->a = dp16(ram, r, 0x82);
  if (prepared) {
    hm_bus_wr16(ram, (PREPARED + x) & 0xFFFFFF, r->a);
  } else {
    hm_bus_wr16(ram, (SENT + x) & 0xFFFFFF, r->a);
    hm_bus_wr16(ram, (SHOWN + x) & 0xFFFFFF, r->a);
  }
}

/* ─────────────────────────────────────────────────────────────────────────
 * Loading a whole half (128 colours) of palette A: the first half
 * (backgrounds) or the second (sprites), into $7F:0900 and $7F:0D00.
 * ───────────────────────────────────────────────────────────────────────── */
static void copy_half(uint8_t *ram, HmRegs *r, uint32_t to, uint16_t x, bool also_shown)
{
  set16(ram, r, 0x7E, 0x0100);
  uint16_t y = 0;
  do {
    r->a = hm_bus_rd16(ram, hm_dp_long(ram, r, 0x72, y));
    hm_bus_wr16(ram, to + x, r->a);
    if (also_shown) hm_bus_wr16(ram, SHOWN + x, r->a);
    y = (uint16_t)(y + 2);
    x = (uint16_t)(x + 2);
  } while (y != 0x0100);
  r->c = 1;                                               /* CPY $7E: equal */
  r->x = x;
  r->y = y;
}
void hm_LoadFirstHalfPaletteToWRAM(uint8_t *ram, HmRegs *r)
{
  set16(ram, r, 0x7E, r->a);
  palette_pointer(ram, r, r->a, 0x72);
  copy_half(ram, r, SENT, 0x0000, true);
}
void hm_LoadSecondHalfPaletteToWRAM(uint8_t *ram, HmRegs *r)
{
  set16(ram, r, 0x7E, r->a);
  palette_pointer(ram, r, r->a, 0x72);
  copy_half(ram, r, SENT, 0x0100, true);
}

/* ─────────────────────────────────────────────────────────────────────────
 * CustomiseSpritePalette ($80:93A4; HM-Decomp's comment says 93A3, which is
 * the RTL before it): three colours of sprite row 15 (10-12) depend on the
 * season (or, on the mountain maps $10-$13, on the map) and on the night;
 * if the wife is pregnant or there is a child, three colours of row 11
 * (8-10) depend on the stage (bits 0-4 of $7F1F66) and on the night.
 * "Night" here is: $92 set, or from 18:00 on a map below $15 or from $31.
 * ───────────────────────────────────────────────────────────────────────── */
static bool night_colours(uint8_t *ram, HmRegs *r)
{
  if (lda8(r, *hm_dp(ram, r, 0x92)) != 0) return true;
  uint8_t h = lda8(r, hm_bus_rd8(ram, HOUR));
  r->c = h >= 18;
  if (!r->c) return false;
  uint8_t map = lda8(r, *hm_dp(ram, r, 0x22));
  r->c = map >= 0x31;
  if (r->c) return true;
  r->c = map >= 0x15;
  return !r->c;
}
static void three_colours(uint8_t *ram, HmRegs *r, uint32_t table, uint16_t x, uint16_t first,
                          uint16_t row, const uint16_t ret[3])
{
  for (unsigned i = 0; i < 3; i++) {
    if (i) x = (uint16_t)(pull16(ram, r) + 2);
    r->x = x;
    if (i < 2) push16(ram, r, x);
    r->a = hm_bus_rd16(ram, table + x);
    r->x = (uint16_t)(first + i);
    r->y = row;
    hm_jsl(ram, r, ret[i], 0x80);
    hm_SUB_80919D(ram, r);
    hm_rtl(ram, r);
  }
}
void hm_CustomiseSpritePalette(uint8_t *ram, HmRegs *r)
{
  static const uint16_t RET_SEASON[3] = { 0x9413, 0x9427, 0x943A };
  static const uint16_t RET_CHILD[3] = { 0x94D8, 0x94EC, 0x94FF };
  set16(ram, r, 0x7E, 0);
  if (night_colours(ram, r)) set16(ram, r, 0x7E, r->a = 0x0004);

  /* the season, or the mountain map */
  r->a = 0;                                               /* LDA #0; XBA */
  uint8_t map = lda8(r, hm_bus_rd8(ram, db_addr(r, 0x0022)));
  r->c = map >= 4;
  bool mountain = false;
  if (r->c) {
    r->c = map >= 0x10;
    if (r->c) { r->c = map >= 0x14; mountain = !r->c; }
  }
  if (mountain) {
    r->a = (uint16_t)(map - 8);                           /* SEC; SBC #8 */
    r->c = 1;
  } else {
    lda8(r, hm_bus_rd8(ram, SEASON));
  }
  set16(ram, r, 0x82, r->a);

  /* X = ((v * 2) + the word at $80) * 2, v = $82 + $7E (8 bits) */
  uint16_t s8 = (uint16_t)(*hm_dp(ram, r, 0x82) + *hm_dp(ram, r, 0x7E));
  uint8_t v = (uint8_t)s8;
  *hm_dp(ram, r, 0x80) = v;
  uint32_t s = (uint32_t)(uint16_t)(v << 1) + dp16(ram, r, 0x80);
  three_colours(ram, r, SPRITE_COLOURS, (uint16_t)(s << 1), 0x0A, 0x0F, RET_SEASON);

  /* the baby / child */
  set16(ram, r, 0x7E, 0);
  if (night_colours(ram, r)) set16(ram, r, 0x7E, r->a = 0x0006);
  r->a = hm_bus_rd16(ram, PREGNANCY);
  if (r->a == 0) return;
  uint16_t stage = 0;
  for (unsigned bit = 0; bit < 5 && !stage; bit++) {
    r->a = hm_bus_rd16(ram, CHILDREN) & (uint16_t)(1u << bit);
    if (r->a) stage = (uint16_t)(bit + 1);
  }
  if (!stage) return;
  uint16_t n = (uint16_t)(stage + dp16(ram, r, 0x7E));    /* CLC; ADC $7E */
  set16(ram, r, 0x80, n);
  uint32_t t = (uint32_t)(uint16_t)(n << 1) + n;          /* ASL; CLC; ADC $80 */
  three_colours(ram, r, CHILD_COLOURS, (uint16_t)((uint16_t)t << 1), 0x08, 0x0B, RET_CHILD);
}

/* ─────────────────────────────────────────────────────────────────────────
 * UNK_CustomisePalette ($80:9241): prepares the night palette of the map
 * in $7F:0B00: the backgrounds from the map's "18:00 and later" palette and
 * the sprites from its night sprite palette (UNK_Table11), then the
 * customised sprite colours (with $92 = 1, into $7F:0B00 too).
 * ───────────────────────────────────────────────────────────────────────── */
void hm_UNK_CustomisePalette(uint8_t *ram, HmRegs *r)
{
  uint16_t map = *hm_dp(ram, r, 0x22);
  set16(ram, r, 0x80, map);
  uint32_t s = (uint32_t)(uint16_t)(map << 2) + map;
  s = (s & 0xFFFF) + map + (s > 0xFFFF);
  s = (s & 0xFFFF) + 4 + (s > 0xFFFF);
  r->x = (uint16_t)s;
  r->a = hm_bus_rd8(ram, TIME_PALETTES + r->x);
  set16(ram, r, 0x7E, r->a);
  palette_pointer(ram, r, r->a, 0x72);
  copy_half(ram, r, PREPARED, 0x0000, false);

  r->a = 0;
  uint16_t m2 = (uint16_t)(*hm_dp(ram, r, 0x22) << 1);     /* ASL (8 bits) */
  r->c = m2 > 0xFF;
  r->x = (uint16_t)((m2 & 0xFF) + 1);
  r->a = hm_bus_rd8(ram, db_addr(r, (uint16_t)(SPRITE_PALETTES + r->x)));
  set16(ram, r, 0x7E, r->a);
  palette_pointer(ram, r, r->a, 0x72);
  copy_half(ram, r, PREPARED, 0x0100, false);

  lda8(r, 1);
  *hm_dp(ram, r, 0x92) = 1;
  hm_jsl(ram, r, 0x92E0, 0x80);
  hm_CustomiseSpritePalette(ram, r);
  hm_rtl(ram, r);
}

/* ChangePalettebyTime ($80:92E2): load the map's palette for this part of
 * the day (backgrounds) and note it as loaded. */
void hm_ChangePalettebyTime(uint8_t *ram, HmRegs *r)
{
  uint8_t n = time_palette(ram, r, part_of_day(ram, r));
  hm_bus_wr8(ram, db_addr(r, LOADED), n);
  hm_jsl(ram, r, 0x9327, 0x80);
  hm_LoadFirstHalfPaletteToWRAM(ram, r);
  hm_rtl(ram, r);
}

/* PrepareSpritePalette ($80:9329): the sprites' half of the palette: the
 * map's (day or night) or, with bits $80 / $100 / $200 of $7F1F5E, palette
 * $71 / $72 / $73; then the customised colours. */
void hm_PrepareSpritePalette(uint8_t *ram, HmRegs *r)
{
  static const uint16_t special[3] = { 0x0080, 0x0100, 0x0200 };
  static const uint16_t ret_load[3] = { 0x9376, 0x9388, 0x939A };
  static const uint16_t ret_custom[3] = { 0x937E, 0x9390, 0x93A2 };
  uint16_t ret1 = 0x9364, ret2 = 0x936C;
  int which = -1;
  for (int i = 0; i < 3 && which < 0; i++) {
    r->a = hm_bus_rd16(ram, FLAGS_5E) & special[i];
    if (r->a) which = i;
  }
  if (which >= 0) {
    r->a = (uint16_t)(0x71 + which);
    ret1 = ret_load[which];
    ret2 = ret_custom[which];
  } else {
    r->a = 0;
    uint16_t m2 = (uint16_t)(*hm_dp(ram, r, 0x22) << 1);  /* ASL (8 bits) */
    r->x = (uint16_t)(m2 & 0xFF);
    uint8_t h = lda8(r, hm_bus_rd8(ram, HOUR));
    r->c = h >= 0x12;
    if (r->c) r->x++;
    r->a = hm_bus_rd8(ram, db_addr(r, (uint16_t)(SPRITE_PALETTES + r->x)));
  }
  hm_jsl(ram, r, ret1, 0x80);
  hm_LoadSecondHalfPaletteToWRAM(ram, r);
  hm_rtl(ram, r);
  *hm_dp(ram, r, 0x92) = 0;
  hm_jsl(ram, r, ret2, 0x80);
  hm_CustomiseSpritePalette(ram, r);
  hm_rtl(ram, r);
}

/* SetPaletteToLoad ($80:9501): when the part of the day changes the map's
 * palette (and it is not $FF), start moving to it. */
void hm_SetPaletteToLoad(uint8_t *ram, HmRegs *r)
{
  uint8_t n = time_palette(ram, r, part_of_day(ram, r));
  uint8_t loaded = hm_bus_rd8(ram, db_addr(r, LOADED));
  r->c = n >= loaded;
  if (n == loaded) return;
  r->c = n >= 0xFF;
  if (n == 0xFF) return;
  hm_push8(ram, r, n);                                    /* PHA */
  hm_jsl(ram, r, 0x954B, 0x80);
  hm_SetTimeofDayPalette(ram, r);
  hm_rtl(ram, r);
  lda8(r, hm_pull8(ram, r));
  hm_bus_wr8(ram, db_addr(r, LOADED), (uint8_t)r->a);
}

/* Event_TimeofDayPalette ($84:9356, JSR): script command "move to palette
 * <byte>". */
void hm_Event_TimeofDayPalette(uint8_t *ram, HmRegs *r)
{
  uint16_t pc = (uint16_t)(dp16(ram, r, 0xC9) + 1);
  set16(ram, r, 0xC9, pc);
  r->a = pc;
  lda8(r, hm_bus_rd8(ram, hm_dp_long(ram, r, 0xC9, 0)));
  hm_jsl(ram, r, 0x9369, 0x80);
  hm_SetTimeofDayPalette(ram, r);
  hm_rtl(ram, r);
  uint16_t after = dp16(ram, r, 0xC9);
  r->c = after == 0xFFFF;
  r->a = (uint16_t)(after + 1);
  set16(ram, r, 0xC9, r->a);
}
