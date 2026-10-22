/*
 * scroll.c — scrolling a big map.
 *
 * A map can be bigger than what the SNES keeps in video memory. The whole
 * map's BG1 tilemap is in WRAM at $7E:2000 (see mapedit.c); VRAM holds a
 * window of it (at $6000, 64 x 32 entries in two halves). When the camera
 * crosses 8 pixels, the row or column of tiles that comes into view is
 * copied from WRAM into a buffer ($0746 / $07C6, two halves of 32 entries)
 * and queued as two DMAs to VRAM:
 *
 *   SUB_80A18D  a new row at the bottom   SUB_80A308  a new row at the top
 *   SUB_80A481  a new column at the right SUB_80A617  a new column at the left
 *
 * Direct page: $10 / $14 the VRAM position of the next column / row,
 * $12 / $16 the WRAM position, $1A the row length, $1C the camera's offset
 * inside the row, $1E the camera speed, $20 / $21 the sub-tile offset in
 * pixels (0-7), $ED / $EF / $F1 / $F3 the camera's limits (left, up, right,
 * down), $F5 / $F7 the camera position.
 *
 * Because the WRAM map is laid out in 32-entry screens, each copy splits
 * into two runs ($7E/$80 for the first half, $82/$84 for the second) that
 * wrap at the end of a screen ($40 bytes).
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

#define ROW_BUFFER_1   0x0746
#define ROW_BUFFER_2   0x07C6

static uint32_t db_addr(const HmRegs *r, uint16_t a) { return (uint32_t)r->db << 16 | a; }
static uint16_t dp16(uint8_t *ram, const HmRegs *r, unsigned a) { return hm_dp_r16(ram, r, a); }
static void set16(uint8_t *ram, const HmRegs *r, unsigned a, uint16_t v) { hm_dp_w16(ram, r, a, v); }
static void push16(uint8_t *ram, HmRegs *r, uint16_t v)
{
  hm_push8(ram, r, (uint8_t)(v >> 8));
  hm_push8(ram, r, (uint8_t)v);
}
static void pull16(uint8_t *ram, HmRegs *r) { hm_pull8(ram, r); hm_pull8(ram, r); }

/* the two runs: where they start in the map ($7E, $82) and in the buffer
 * ($80, $84) */
static void runs(uint8_t *ram, HmRegs *r, uint16_t a, uint16_t b, uint16_t c, uint16_t d)
{
  set16(ram, r, 0x7E, a); set16(ram, r, 0x80, b); set16(ram, r, 0x82, c); set16(ram, r, 0x84, d);
}

/* 32 entries from [$72] (+$7E) and [$75] (+$82) into the buffers; each run
 * wraps at $40 bytes by moving `wrap` in the map; `step` per entry. */
static void copy_entries(uint8_t *ram, HmRegs *r, uint16_t step, uint16_t wrap)
{
  for (uint16_t x = 0; x < 0x40; x += 2) {
    push16(ram, r, x);                                  /* PHX */
    if (dp16(ram, r, 0x80) == 0x40) {
      set16(ram, r, 0x80, 0);
      set16(ram, r, 0x7E, (uint16_t)(dp16(ram, r, 0x7E) + wrap));
    }
    if (dp16(ram, r, 0x84) == 0x40) {
      set16(ram, r, 0x84, 0);
      set16(ram, r, 0x82, (uint16_t)(dp16(ram, r, 0x82) + wrap));
    }
    uint16_t v = hm_bus_rd16(ram, hm_dp_long(ram, r, 0x72, dp16(ram, r, 0x7E)));
    hm_bus_wr16(ram, (db_addr(r, ROW_BUFFER_1) + dp16(ram, r, 0x80)) & 0xFFFFFF, v);
    v = hm_bus_rd16(ram, hm_dp_long(ram, r, 0x75, dp16(ram, r, 0x82)));
    hm_bus_wr16(ram, (db_addr(r, ROW_BUFFER_2) + dp16(ram, r, 0x84)) & 0xFFFFFF, v);
    set16(ram, r, 0x7E, (uint16_t)(dp16(ram, r, 0x7E) + step));
    set16(ram, r, 0x80, (uint16_t)(dp16(ram, r, 0x80) + 2));
    set16(ram, r, 0x82, (uint16_t)(dp16(ram, r, 0x82) + step));
    set16(ram, r, 0x84, (uint16_t)(dp16(ram, r, 0x84) + 2));
    pull16(ram, r);                                     /* PLX */
  }
}

/* the two DMAs: channel 0 the first buffer to VRAM $6000 + at, channel 1
 * the second to $6000 + at + half; mode $80 (rows) or $81 (columns:
 * VRAM steps 32 words) */
static void queue(uint8_t *ram, HmRegs *r, uint16_t at, uint16_t half, uint16_t mode,
                  uint16_t ret1, uint16_t ret2)
{
  static const uint16_t buffer[2] = { ROW_BUFFER_1, ROW_BUFFER_2 };
  for (unsigned ch = 0; ch < 2; ch++) {
    *hm_dp(ram, r, 0x27) = (uint8_t)ch;
    *hm_dp(ram, r, 0x29) = 0x18;
    r->y = 0x0040;
    uint32_t to = (uint32_t)at + 0x6000;                /* CLC; ADC #$6000 (; ADC half) */
    if (ch) to = (to & 0xFFFF) + half + (to > 0xFFFF);
    r->x = (uint16_t)to;
    set16(ram, r, 0x72, buffer[ch]);
    *hm_dp(ram, r, 0x74) = 0x80;
    r->a = mode;
    hm_jsl(ram, r, ch ? ret2 : ret1, 0x80);
    hm_AddProgrammedDMA(ram, r);
    hm_rtl(ram, r);
  }
}

/* the row windows from $12 (SUB_80A18D / SUB_80A308) */
static void row_runs(uint8_t *ram, HmRegs *r)
{
  uint16_t v = dp16(ram, r, 0x12);
  if (v < 0x20) runs(ram, r, 0, 0, 0, 0);
  else if (v < 0x60) runs(ram, r, (uint16_t)(v - 0x20), (uint16_t)(v - 0x20), 0, 0);
  else if (v < 0xA0) runs(ram, r, 0x80, 0, (uint16_t)(v - 0x60), (uint16_t)(v - 0x60));
  else if (v < 0xC0) runs(ram, r, (uint16_t)(v - 0x20), (uint16_t)(v - 0xA0), 0x80, 0);
  /* $C0 and more: the runs are left as they are */
}
/* the column windows from $16 (SUB_80A481 / SUB_80A617): the buffer
 * position is the screen row, (offset >> 8) * 2 */
static void column_runs(uint8_t *ram, HmRegs *r, uint16_t last_mask)
{
  uint16_t v = dp16(ram, r, 0x16);
  if (v < 0x1000) runs(ram, r, 0, 0, 0, 0);
  else if (v < 0x3000) {
    uint16_t o = (uint16_t)(v - 0x1000);
    runs(ram, r, o, (uint16_t)(((o >> 8) & 0x1F) << 1), 0, 0);
  } else if (v < 0x5000) {
    uint16_t o = (uint16_t)(v - 0x3000);
    runs(ram, r, 0x4000, 0, o, (uint16_t)(((o >> 8) & 0x1F) << 1));
  } else if (v < 0x6000) {
    uint16_t o = (uint16_t)(v - 0x5000);
    runs(ram, r, (uint16_t)(v - 0x1000), (uint16_t)(((o >> 8) & last_mask) << 1), 0x4000, 0);
  }
}
static void sources(uint8_t *ram, HmRegs *r, uint16_t first, uint16_t second_offset)
{
  set16(ram, r, 0x72, first);
  set16(ram, r, 0x75, (uint16_t)(first + second_offset));
  *hm_dp(ram, r, 0x74) = 0x7E;
  *hm_dp(ram, r, 0x77) = 0x7E;
}
/* the next VRAM position: `next`, wrapped from `a` to `a2` and `b` to `b2` */
static void next_position(HmRegs *r, uint8_t *ram, unsigned at, uint16_t next,
                          uint16_t a, uint16_t a2, uint16_t b, uint16_t b2)
{
  r->c = next >= a;
  if (next == a) next = a2;
  else {
    r->c = next >= b;
    if (next == b) next = b2;
  }
  r->a = next;
  set16(ram, r, at, next);
}

/* ─────────────────────────────────────────────────────────────────────────
 * SUB_80A18D ($80:A18D, JSL, RTL): the row below comes into view.
 * Leftovers: A = the new $14, X = 1 (AddProgrammedDMA), Y = $40, C from
 * the wrap compares.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_SUB_80A18D(uint8_t *ram, HmRegs *r)
{
  row_runs(ram, r);
  uint16_t inner = (uint16_t)(dp16(ram, r, 0x1C) >> 2);
  set16(ram, r, 0x86, inner);
  uint32_t s = 0x2000u + dp16(ram, r, 0x16);           /* CLC; ADC $16; ADC $1C */
  s = (s & 0xFFFF) + dp16(ram, r, 0x1C) + (s > 0xFFFF);
  uint16_t first = (uint16_t)((uint16_t)s - inner);    /* SEC; SBC $86 */
  sources(ram, r, first, 0x40);
  copy_entries(ram, r, 2, 0x40);
  queue(ram, r, dp16(ram, r, 0x14), 0x0400, 0x0080, 0xA2B6, 0xA2E3);
  set16(ram, r, 0x16, (uint16_t)(dp16(ram, r, 0x16) + dp16(ram, r, 0x1A)));
  next_position(r, ram, 0x14, (uint16_t)(dp16(ram, r, 0x14) + 0x20), 0x0400, 0x0800, 0x0C00, 0x0000);
}

/* SUB_80A308 ($80:A308): the row above. */
void hm_SUB_80A308(uint8_t *ram, HmRegs *r)
{
  row_runs(ram, r);
  uint16_t inner = (uint16_t)(dp16(ram, r, 0x1C) >> 2);
  set16(ram, r, 0x86, inner);
  uint16_t first = (uint16_t)(0x2000 + dp16(ram, r, 0x16) - inner);
  sources(ram, r, first, 0x40);
  copy_entries(ram, r, 2, 0x40);
  queue(ram, r, dp16(ram, r, 0x14), 0x0400, 0x0080, 0xA42F, 0xA45C);
  set16(ram, r, 0x16, (uint16_t)(dp16(ram, r, 0x16) - dp16(ram, r, 0x1A)));
  next_position(r, ram, 0x14, (uint16_t)(dp16(ram, r, 0x14) - 0x20), 0xFFE0, 0x0BE0, 0x07E0, 0x03E0);
}

/* SUB_80A481 ($80:A481): the column at the right. The second run's source
 * is $72 + $2000 with the carry of the first sum (no CLC). The last window
 * masks the screen row with $0F where SUB_80A617 uses $1F (the same here,
 * the row is below $10). */
void hm_SUB_80A481(uint8_t *ram, HmRegs *r)
{
  column_runs(ram, r, 0x0F);
  uint32_t s = 0x2000u + dp16(ram, r, 0x12);           /* CLC; ADC $12; ADC #$60 */
  s = (s & 0xFFFF) + 0x60 + (s > 0xFFFF);
  uint16_t first = (uint16_t)s;
  sources(ram, r, first, (uint16_t)(0x2000 + (s > 0xFFFF)));
  copy_entries(ram, r, 0x100, 0x2000);
  queue(ram, r, dp16(ram, r, 0x10), 0x0800, 0x0081, 0xA5C4, 0xA5F1);
  set16(ram, r, 0x12, (uint16_t)(dp16(ram, r, 0x12) + 2));
  next_position(r, ram, 0x10, (uint16_t)(dp16(ram, r, 0x10) + 1), 0x0020, 0x0400, 0x0420, 0x0000);
}

/* SUB_80A617 ($80:A617): the column at the left. */
void hm_SUB_80A617(uint8_t *ram, HmRegs *r)
{
  column_runs(ram, r, 0x1F);
  uint16_t first = (uint16_t)(0x2000 + dp16(ram, r, 0x12) - 0x20);
  sources(ram, r, first, 0x2000);
  copy_entries(ram, r, 0x100, 0x2000);
  queue(ram, r, dp16(ram, r, 0x10), 0x0800, 0x0081, 0xA75B, 0xA788);
  set16(ram, r, 0x12, (uint16_t)(dp16(ram, r, 0x12) - 2));
  uint16_t next = (uint16_t)(dp16(ram, r, 0x10) - 1);
  r->c = next >= 0xFFFF;                              /* CMP #$FFFF */
  if (next == 0xFFFF) next = 0x041F;
  else { r->c = next >= 0x03FF; if (next == 0x03FF) next = 0x001F; }
  r->a = next;
  set16(ram, r, 0x10, next);
}

/* ═════════════════════════════════════════════════════════════════════════
 * The camera moves: BG1 follows the camera ($F5 / $F7 -> $013C / $013E);
 * unless it is at a limit, the speed $1E is capped to what is left, and
 * (with $0196 bit 0, "the map scrolls") the sub-tile offset advances;
 * every 8 pixels a new row / column is brought in.
 * ═════════════════════════════════════════════════════════════════════════ */
static bool follow(uint8_t *ram, HmRegs *r, unsigned pos, uint16_t bg, unsigned low, unsigned high,
                   bool towards_high)
{
  uint16_t p = dp16(ram, r, pos);
  r->a = p;
  hm_bus_wr16(ram, db_addr(r, bg), p);
  r->c = p >= dp16(ram, r, low);
  if (p == dp16(ram, r, low)) return false;
  r->c = p >= dp16(ram, r, high);
  if (p == dp16(ram, r, high)) return false;
  uint16_t room = p;
  if (!towards_high) {                                 /* LDA limit; SEC; SBC pos */
    room = (uint16_t)(dp16(ram, r, high) - p);
    r->a = room;
  }
  r->c = room >= dp16(ram, r, 0x1E);
  if (room < dp16(ram, r, 0x1E)) set16(ram, r, 0x1E, room);
  r->a = hm_bus_rd16(ram, db_addr(r, 0x0196)) & 0x0001;
  return r->a != 0;
}
/* forwards: offset += speed; at 8 or more, -8 and a new line */
static bool forwards(uint8_t *ram, HmRegs *r, unsigned sub)
{
  uint16_t s = (uint16_t)(*hm_dp(ram, r, sub) + *hm_dp(ram, r, 0x1E));
  r->c = s > 0xFF;
  uint8_t v = (uint8_t)s;
  *hm_dp(ram, r, sub) = v;
  r->a = v;
  r->c = v >= 8;
  if (v < 8) return false;
  v = (uint8_t)(v - 8);
  r->a = v;
  *hm_dp(ram, r, sub) = v;
  return true;
}
/* backwards: offset -= speed; below 0, +8 and a new line */
static bool backwards(uint8_t *ram, HmRegs *r, unsigned sub)
{
  uint8_t o = *hm_dp(ram, r, sub), sp = *hm_dp(ram, r, 0x1E);
  r->c = o >= sp;
  uint8_t v = (uint8_t)(o - sp);
  *hm_dp(ram, r, sub) = v;
  r->a = v;
  if (!(v & 0x80)) return false;
  uint16_t s = (uint16_t)(8 + v);
  r->c = s > 0xFF;
  v = (uint8_t)s;
  r->a = v;
  *hm_dp(ram, r, sub) = v;
  return true;
}

/* UpdateBGOffset ($80:A0AB): the camera goes down. */
void hm_UpdateBGOffset(uint8_t *ram, HmRegs *r)
{
  if (follow(ram, r, 0xF7, 0x013E, 0xEF, 0xF3, true) && forwards(ram, r, 0x21)) {
    hm_jsl(ram, r, 0xA0DF, 0x80);
    hm_SUB_80A18D(ram, r);
    hm_rtl(ram, r);
  }
}
/* SUB_80A0E1 ($80:A0E1): the camera goes up. */
void hm_SUB_80A0E1(uint8_t *ram, HmRegs *r)
{
  if (follow(ram, r, 0xF7, 0x013E, 0xEF, 0xF3, false) && backwards(ram, r, 0x21)) {
    hm_jsl(ram, r, 0xA11A, 0x80);
    hm_SUB_80A308(ram, r);
    hm_rtl(ram, r);
  }
}
/* UNK_StaticMapScroling ($80:A11C): the camera goes right. */
void hm_UNK_StaticMapScroling(uint8_t *ram, HmRegs *r)
{
  if (follow(ram, r, 0xF5, 0x013C, 0xED, 0xF1, true) && forwards(ram, r, 0x20)) {
    hm_jsl(ram, r, 0xA150, 0x80);
    hm_SUB_80A481(ram, r);
    hm_rtl(ram, r);
  }
}
/* SUB_80A152 ($80:A152): the camera goes left. */
void hm_SUB_80A152(uint8_t *ram, HmRegs *r)
{
  if (follow(ram, r, 0xF5, 0x013C, 0xED, 0xF1, false) && backwards(ram, r, 0x20)) {
    hm_jsl(ram, r, 0xA18B, 0x80);
    hm_SUB_80A617(ram, r);
    hm_rtl(ram, r);
  }
}
