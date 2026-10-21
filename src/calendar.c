/*
 * calendar.c — the date shown on screen.
 *
 * LoadsDateNames ($82:89D6, JSL, RTL). Copies the names for today into the
 * text buffers the diary and the calendar print (16-bit text codes, see
 * people.c): the season ($08B3, 6 codes, from the ROM table $82:8AD9, 16
 * bytes per season), the weekday ($08BF, 9 codes, $82:8B19, 32 bytes per
 * day) and the day's ordinal suffix ($08D1, 2 codes, $82:8BF9).
 *
 * Quirk: the suffix is "st", "nd", "rd" only for days 1, 2 and 3; every
 * other day gets "th" — so the game says 21th, 22th and 23th.
 *
 * Leftovers: X = the last table offset read, A = the last code, C from the
 * day comparisons.
 * ───────────────────────────────────────────────────────────────────────── */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

#define SEASON_NAMES    0x828AD9
#define WEEKDAY_NAMES   0x828B19
#define ORDINAL_NAMES   0x828BF9
#define SEASON_TEXT     0x008B3   /* 6 x 16b */
#define WEEKDAY_TEXT    0x008BF   /* 9 x 16b */
#define ORDINAL_TEXT    0x008D1   /* 2 x 16b */

/* Copies `n` words from table + x; leaves X/A as the original's INX pairs. */
static void copy_text(uint8_t *ram, HmRegs *r, uint32_t table, uint16_t x, uint32_t to, int n)
{
  for (int k = 0; k < n; k++) {
    if (k) x = (uint16_t)(x + 2);
    r->a = hm_bus_rd16(ram, table + x);
    hm_wr16(ram, to + 2u * k, r->a);
  }
  r->x = x;
}

void hm_LoadsDateNames(uint8_t *ram, HmRegs *r)
{
  copy_text(ram, r, SEASON_NAMES, (uint16_t)(ram[HM_SEASON] << 4), SEASON_TEXT, 6);
  copy_text(ram, r, WEEKDAY_NAMES, (uint16_t)(ram[HM_WEEKDAY] << 5), WEEKDAY_TEXT, 9);

  uint8_t day = ram[HM_DAY];
  uint16_t x;
  if (day == 1)      { x = 0; r->c = 1; }
  else if (day == 2) { x = 4; r->c = 1; }
  else if (day == 3) { x = 8; r->c = 1; }
  else               { x = 12; r->c = day >= 3; }
  copy_text(ram, r, ORDINAL_NAMES, x, ORDINAL_TEXT, 2);
}
