/*
 * audio.c — talking to the sound chip (the SPC700, see GUIDE.md 2.7).
 *
 * The SNES's sound runs on its own processor; the main CPU only sees four
 * one-byte "ports" ($2140-$2143, APUIO0-3) shared with it. The game's
 * protocol, as these routines use it:
 *
 *   1. wait until port 3 reads the same twice in a row (the other
 *      processor may be changing it) and then reads 1: "ready";
 *   2. write the command in ports 0-2 and 2 in port 3: "here it is";
 *   3. wait until port 3 reads 4 (simple commands) or 8 (commands with
 *      arguments): "done";
 *   4. write 0 in port 3.
 *
 * Command $0A plays a sound effect: $0114-$0116 = its arguments (a letter
 * of text sets them to 3, 6, $0A). $0B with an argument in A, $0C alone.
 * Before sending, UNK_Audio15 asks (port 0 bit 4) whether the chip is busy
 * and, if so, drops the sound.
 *
 * The ports are addressed with the data bank (STA.W $2140), as the original.
 * The checker replays what the chip answered (see g_hm_apu in hm_decomp.h).
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

static uint32_t db_addr(const HmRegs *r, uint16_t addr) { return (uint32_t)r->db << 16 | addr; }
static uint8_t port(uint8_t *ram, const HmRegs *r, int n) { return hm_bus_rd8(ram, db_addr(r, (uint16_t)(0x2140 + n))); }
static void set_port(uint8_t *ram, const HmRegs *r, int n, uint8_t v) { hm_bus_wr8(ram, db_addr(r, (uint16_t)(0x2140 + n)), v); }
static uint8_t rd8(uint8_t *ram, const HmRegs *r, uint16_t a) { return hm_bus_rd8(ram, db_addr(r, a)); }

static void jsr(uint8_t *ram, HmRegs *r, uint16_t ret, void (*fn)(uint8_t *, HmRegs *))
{
  hm_push8(ram, r, (uint8_t)(ret >> 8));
  hm_push8(ram, r, (uint8_t)ret);
  fn(ram, r);
  hm_pull8(ram, r); hm_pull8(ram, r);
}

/* "LDA $2143; STA $93; LDA $2143; CMP $93; BNE": until port 3 reads the
 * same twice in a row. */
static void wait_steady(uint8_t *ram, HmRegs *r)
{
  uint8_t second;
  do {
    *hm_dp(ram, r, 0x93) = port(ram, r, 3);
    second = port(ram, r, 3);
  } while (second != *hm_dp(ram, r, 0x93));
}
/* Steady, then a third read must be `v`; if not, start over. */
static void wait_steady_then(uint8_t *ram, HmRegs *r, uint8_t v)
{
  do wait_steady(ram, r); while (port(ram, r, 3) != v);
}
/* Write a command: ports 0-2, then 2 in port 3. */
static void send(uint8_t *ram, HmRegs *r, uint8_t p0, uint8_t p1, uint8_t p2)
{
  set_port(ram, r, 0, p0);
  set_port(ram, r, 1, p1);
  set_port(ram, r, 2, p2);
  set_port(ram, r, 3, 2);
}
/* The 8-bit A a routine leaves: its low byte, the high byte untouched. */
static void set_a8(HmRegs *r, uint8_t v) { r->a = (uint16_t)((r->a & 0xFF00) | v); }

/* ─────────────────────────────────────────────────────────────────────────
 * UNK_Audio17 ($83:8EE4, JSR, RTS): waits for "ready" and returns
 * A = port 0 & $10 (nonzero: the chip is busy). C = 1 (from CMP).
 * ───────────────────────────────────────────────────────────────────────── */
void hm_UNK_Audio17(uint8_t *ram, HmRegs *r)
{
  wait_steady_then(ram, r, 1);
  set_a8(r, port(ram, r, 0) & 0x10);
  r->c = 1;
}

/* ─────────────────────────────────────────────────────────────────────────
 * UNK_Audio7 ($83:8EC9, JSR, RTS): the same with A = port 0 & $11.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_UNK_Audio7(uint8_t *ram, HmRegs *r)
{
  wait_steady_then(ram, r, 1);
  set_a8(r, port(ram, r, 0) & 0x11);
  r->c = 1;
}

/* ─────────────────────────────────────────────────────────────────────────
 * UNK_Audio8 ($83:8E8B, JSR, RTS): sends the simple command $92 (ports
 * 1-2 = 0) and waits for "done" (4). Unlike the others, after the first
 * steady read it only waits for 1 (it does not check steadiness again).
 * Leftovers: A = 4 (the last read), C = 1.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_UNK_Audio8(uint8_t *ram, HmRegs *r)
{
  wait_steady(ram, r);
  while (port(ram, r, 3) != 1) {}
  send(ram, r, *hm_dp(ram, r, 0x92), 0, 0);
  wait_steady(ram, r);
  while (port(ram, r, 3) != 4) {}
  set_port(ram, r, 3, 0);
  set_a8(r, 4);
  r->c = 1;
}

/* ─────────────────────────────────────────────────────────────────────────
 * UNK_Audio15 ($83:8DDF, JSL, RTL): play sound effect $0114-$0116. If the
 * chip is busy (UNK_Audio17), nothing. Otherwise it sends command $0A
 * (UNK_Audio8), then the three arguments, and waits for "done" (8).
 * Leftovers: A = 8, C = 1; or those of UNK_Audio17.
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_DDF_READY   0x8DE1
#define RET_DDF_COMMAND 0x8DEC

void hm_UNK_Audio15(uint8_t *ram, HmRegs *r)
{
  jsr(ram, r, RET_DDF_READY, hm_UNK_Audio17);
  if (r->a & 0xFF) return;                            /* busy */
  *hm_dp(ram, r, 0x92) = 0x0A;
  jsr(ram, r, RET_DDF_COMMAND, hm_UNK_Audio8);
  wait_steady_then(ram, r, 1);
  send(ram, r, rd8(ram, r, 0x0114), rd8(ram, r, 0x0115), rd8(ram, r, 0x0116));
  wait_steady_then(ram, r, 8);
  set_port(ram, r, 3, 0);
  set_a8(r, 8);
  r->c = 1;
}

/* ─────────────────────────────────────────────────────────────────────────
 * UNK_Audio19 ($83:8332, JSL, RTL): UNK_Audio15 with $0116 = $0A.
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_332_SOUND   0x833C

void hm_UNK_Audio19(uint8_t *ram, HmRegs *r)
{
  set_a8(r, 0x0A);
  hm_bus_wr8(ram, db_addr(r, 0x0116), 0x0A);
  hm_jsl(ram, r, RET_332_SOUND, 0x83);
  hm_UNK_Audio15(ram, r);
  hm_rtl(ram, r);
}

/* ─────────────────────────────────────────────────────────────────────────
 * UNK_Audio16 ($83:8E7F, JSL, RTL): sends command $0C.
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_E7F_COMMAND 0x8E89

void hm_UNK_Audio16(uint8_t *ram, HmRegs *r)
{
  set_a8(r, 0x0C);
  *hm_dp(ram, r, 0x92) = 0x0C;
  jsr(ram, r, RET_E7F_COMMAND, hm_UNK_Audio8);
}

/* ─────────────────────────────────────────────────────────────────────────
 * UNK_Audio2 ($83:8E32, JSL, RTL): A (low byte) = argument. Sends command
 * $0B, then the argument in port 0, and waits for "done" (8).
 * $94 = the argument. Leftovers: A = 8, C = 1.
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_E32_COMMAND 0x8E3E

void hm_UNK_Audio2(uint8_t *ram, HmRegs *r)
{
  *hm_dp(ram, r, 0x94) = (uint8_t)r->a;
  set_a8(r, 0x0B);
  *hm_dp(ram, r, 0x92) = 0x0B;
  jsr(ram, r, RET_E32_COMMAND, hm_UNK_Audio8);
  wait_steady_then(ram, r, 1);
  send(ram, r, *hm_dp(ram, r, 0x94), 0, 0);
  wait_steady_then(ram, r, 8);
  set_port(ram, r, 3, 0);
  set_a8(r, 8);
  r->c = 1;
}

/* ─────────────────────────────────────────────────────────────────────────
 * The music. $0110 = the song wanted, $0117 = the one playing. Command $09
 * (UNK_Audio6) and $08 (UNK_Audio14) take two arguments, $07
 * (UNK_Audio13) and $04 (UNK_Audio10) one.
 *
 * UNK_Audio6 ($83:8D8B, JSL, RTL): command $09 with $0112 / $0113. Only
 * when UNK_Audio7 says the chip is busy (port 0 & $11 not 0); otherwise it
 * returns at once.
 * Leftovers: A = 8 and C = 1 when sent; UNK_Audio7's otherwise.
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_D8B_BUSY    0x8D8F
#define RET_D8B_COMMAND 0x8D9A

void hm_UNK_Audio6(uint8_t *ram, HmRegs *r)
{
  jsr(ram, r, RET_D8B_BUSY, hm_UNK_Audio7);
  if ((r->a & 0xFF) == 0) return;
  *hm_dp(ram, r, 0x92) = 0x09;
  jsr(ram, r, RET_D8B_COMMAND, hm_UNK_Audio8);
  wait_steady_then(ram, r, 1);
  send(ram, r, rd8(ram, r, 0x0112), rd8(ram, r, 0x0113), 0);
  wait_steady_then(ram, r, 8);
  set_port(ram, r, 3, 0);
  set_a8(r, 8);
  r->c = 1;
}

/* ─────────────────────────────────────────────────────────────────────────
 * UNK_Audio13 ($83:8CF3, JSL, RTL): sends command $07 by itself (without
 * UNK_Audio8) and waits for 4. A is kept in $94 but never sent.
 * Leftovers: A = 4, C = 1.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_UNK_Audio13(uint8_t *ram, HmRegs *r)
{
  *hm_dp(ram, r, 0x94) = (uint8_t)r->a;
  *hm_dp(ram, r, 0x92) = 0x07;
  wait_steady(ram, r);
  while (port(ram, r, 3) != 1) {}
  send(ram, r, 0x07, 0, 0);
  wait_steady_then(ram, r, 4);
  set_port(ram, r, 3, 0);
  set_a8(r, 4);
  r->c = 1;
}

/* ─────────────────────────────────────────────────────────────────────────
 * UNK_Audio14 ($83:8D39, JSL, RTL; HM-Decomp says $83:8D38, the RTL of
 * UNK_Audio13): waits while the chip is busy, then command $08 with
 * $0111 / $0113. Leftovers: A = 8, C = 1.
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_D39_BUSY    0x8D3B
#define RET_D39_COMMAND 0x8D46

void hm_UNK_Audio14(uint8_t *ram, HmRegs *r)
{
  do jsr(ram, r, RET_D39_BUSY, hm_UNK_Audio7); while (r->a & 0xFF);
  *hm_dp(ram, r, 0x92) = 0x08;
  jsr(ram, r, RET_D39_COMMAND, hm_UNK_Audio8);
  wait_steady_then(ram, r, 1);
  send(ram, r, rd8(ram, r, 0x0111), rd8(ram, r, 0x0113), 0);
  wait_steady_then(ram, r, 8);
  set_port(ram, r, 3, 0);
  set_a8(r, 8);
  r->c = 1;
}

/* ─────────────────────────────────────────────────────────────────────────
 * UNK_Audio10 ($83:89C7, JSL, RTL): A = n. Command $04 with n (when n is
 * 0 it first waits until the chip is not busy), then clears $0104 + n up to
 * $010E. Unlike the others, the final wait for 8 is a plain loop (no
 * "same twice" check).
 * Leftovers: $94 = n, A = 0, X = $0B, C = 1.
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_9C7_BUSY    0x89D2
#define RET_9C7_COMMAND 0x89DD

void hm_UNK_Audio10(uint8_t *ram, HmRegs *r)
{
  uint8_t n = (uint8_t)r->a;
  hm_push8(ram, r, n);                                       /* PHA */
  *hm_dp(ram, r, 0x94) = n;
  if (n == 0)
    do jsr(ram, r, RET_9C7_BUSY, hm_UNK_Audio7); while (r->a & 0xFF);
  *hm_dp(ram, r, 0x92) = 0x04;
  jsr(ram, r, RET_9C7_COMMAND, hm_UNK_Audio8);
  wait_steady_then(ram, r, 1);
  send(ram, r, n, 0, 0);
  while (port(ram, r, 3) != 8) {}
  set_port(ram, r, 3, 0);
  hm_pull8(ram, r);                                          /* PLA */
  for (uint16_t x = n; x != 0x0B; x++) hm_bus_wr8(ram, db_addr(r, (uint16_t)(0x0104 + x)), 0);
  r->a = 0;
  r->x = 0x0B;
  r->c = 1;
}

/* ─────────────────────────────────────────────────────────────────────────
 * When the song changes ($0110 != $0117 and something is playing):
 * UNK_Audio21 ($83:841F) sends command $09 with 1, $10; UNK_Audio25
 * ($83:8401) with 1, $40 (its "nothing playing" branch jumps to the RTL
 * of the routine before it, $83:8400). UNK_Audio22 ($83:8380): if the new
 * song is not 0, $FF nor the one playing, command $08 with 1, $40; then
 * $0119 = 0. All JSL, RTL.
 * Leftovers: A's low byte = the last value compared, C from the compare,
 * or those of UNK_Audio6 / UNK_Audio14.
 * ───────────────────────────────────────────────────────────────────────── */
static void change_song(uint8_t *ram, HmRegs *r, uint8_t speed, uint16_t ret)
{
  uint8_t want = rd8(ram, r, 0x0110), playing = rd8(ram, r, 0x0117);
  set_a8(r, want);
  r->c = want >= playing;
  if (want == playing) return;
  set_a8(r, playing);
  if (playing == 0) return;
  hm_bus_wr8(ram, db_addr(r, 0x0112), 0x01);
  hm_bus_wr8(ram, db_addr(r, 0x0113), speed);
  set_a8(r, speed);
  hm_jsl(ram, r, ret, 0x83);
  hm_UNK_Audio6(ram, r);
  hm_rtl(ram, r);
}
void hm_UNK_Audio21(uint8_t *ram, HmRegs *r) { change_song(ram, r, 0x10, 0x843B); }
void hm_UNK_Audio25(uint8_t *ram, HmRegs *r) { change_song(ram, r, 0x40, 0x841D); }

void hm_UNK_Audio22(uint8_t *ram, HmRegs *r)
{
  uint8_t want = rd8(ram, r, 0x0110), playing = rd8(ram, r, 0x0117);
  set_a8(r, want);
  if (want != 0) {
    r->c = want >= playing;
    if (want != playing) {
      r->c = want == 0xFF;                                   /* CMP #$FF */
      if (want != 0xFF) {
        hm_bus_wr8(ram, db_addr(r, 0x0111), 0x01);
        hm_bus_wr8(ram, db_addr(r, 0x0113), 0x40);
        set_a8(r, 0x40);
        hm_jsl(ram, r, 0x839D, 0x83);
        hm_UNK_Audio14(ram, r);
        hm_rtl(ram, r);
      }
    }
  }
  hm_bus_wr8(ram, db_addr(r, 0x0119), 0);
}

/* ─────────────────────────────────────────────────────────────────────────
 * Uploads: the instruments (samples) and the songs travel to the chip 3
 * bytes at a time; after each group the CPU waits for port 3 = 0, clears
 * it and waits for 1 again.
 *
 *   UNK_Audio_Table3 ($83:90EF)  10 bytes per instrument (40): a word for
 *                                the first command, 3 + 2 bytes for the
 *                                next two, a long pointer to the samples
 *   UNK_Audio_Table5 ($83:8EFF)  the size of each instrument's samples
 *   UNK_Audio_Table6 ($83:8F83)  14 bytes per song (26): a long pointer to
 *                                its data, then its 11 instruments (0 =
 *                                none)
 *   UNK_Audio_Table7 ($83:8F4F)  the size of each song's data
 *   UNK_Audio_Table2 ($83:927F)  the instrument of each sound effect
 *   UNK_Audio_Table4 ($83:92B1)  the 10 instruments always loaded
 * Sizes are in bytes; they are sent as a count of 3-byte groups (rounded
 * up, with DivisionUnsigned).
 * ───────────────────────────────────────────────────────────────────────── */
#define AUDIO_TABLE2 0x83927F
#define AUDIO_TABLE3 0x8390EF
#define AUDIO_TABLE4 0x8392B1
#define AUDIO_TABLE5 0x838EFF
#define AUDIO_TABLE6 0x838F83
#define AUDIO_TABLE7 0x838F4F

static uint8_t rom8(uint8_t *ram, uint32_t a) { return hm_bus_rd8(ram, a & 0xFFFFFF); }
static uint16_t rom16(uint8_t *ram, uint32_t a) { return hm_bus_rd16(ram, a & 0xFFFFFF); }
static void jsl83(uint8_t *ram, HmRegs *r, uint16_t ret, void (*fn)(uint8_t *, HmRegs *))
{
  hm_jsl(ram, r, ret, 0x83);
  fn(ram, r);
  hm_rtl(ram, r);
}
/* A x $7E with Unk_Audio23 (JSL), from 16-bit code right after LDA #k:
 * its PHP saves the flags of that moment: C = 1 (every caller has just
 * compared equal), N = Z = 0, 16-bit A / X, and V, D, I as they came in. */
static void times(uint8_t *ram, HmRegs *r, uint16_t ret)
{
  uint8_t entry = r->p;
  r->c = 1;
  r->p = (uint8_t)((entry & 0x4C) | 0x01);
  jsl83(ram, r, ret, hm_Unk_Audio23);
  r->p = entry;
}
/* bytes -> groups of 3, rounded up: $80 (DivisionUnsigned leaves Y = 0) */
static uint16_t groups_of_3(uint8_t *ram, HmRegs *r, uint16_t bytes, uint16_t ret)
{
  hm_dp_w16(ram, r, 0x7E, bytes);
  hm_dp_w16(ram, r, 0x80, 3);
  jsl83(ram, r, ret, hm_DivisionUnsigned);
  hm_dp_w16(ram, r, 0x80, r->a);
  if (hm_dp_r16(ram, r, 0x7E)) hm_dp_w16(ram, r, 0x80, (uint16_t)(hm_dp_r16(ram, r, 0x80) + 1));
  return hm_dp_r16(ram, r, 0x80);
}
/* after a command's group: "done" (4), clear, then "ready" again */
static void next_group(uint8_t *ram, HmRegs *r)
{
  wait_steady_then(ram, r, 4);
  set_port(ram, r, 3, 0);
  wait_steady(ram, r);
  while (port(ram, r, 3) != 1) {}
}
/* 3 bytes from [$0A],Y */
static void send_3(uint8_t *ram, HmRegs *r)
{
  uint8_t b[3];
  for (int i = 0; i < 3; i++) b[i] = hm_bus_rd8(ram, hm_dp_long(ram, r, 0x0A, (uint16_t)(r->y + i)));
  r->y = (uint16_t)(r->y + 3);
  r->x = (uint16_t)(r->x - 1);
  send(ram, r, b[0], b[1], b[2]);
}

/* Instrument $94, its command already sent and answered: the three
 * header groups from Table3, the count, then the samples. Leftovers: $0A-$0C
 * = the samples, $80 = the count; A = count's low byte : 1, X = 0, Y =
 * 3 count, C = 1. */
static void send_instrument(uint8_t *ram, HmRegs *r, uint16_t ret_times, uint16_t ret_divide)
{
  uint8_t ins = *hm_dp(ram, r, 0x94);
  hm_dp_w16(ram, r, 0x7E, ins);
  r->a = 0x000A;
  times(ram, r, ret_times);                                /* X = 10 ins */
  uint16_t x = r->a;
  uint16_t w = rom16(ram, AUDIO_TABLE3 + x);
  send(ram, r, ins, (uint8_t)w, (uint8_t)(w >> 8));
  x = (uint16_t)(x + 2);
  next_group(ram, r);
  send(ram, r, rom8(ram, AUDIO_TABLE3 + x), rom8(ram, AUDIO_TABLE3 + x + 1), rom8(ram, AUDIO_TABLE3 + x + 2));
  x = (uint16_t)(x + 3);
  next_group(ram, r);
  send(ram, r, rom8(ram, AUDIO_TABLE3 + x), rom8(ram, AUDIO_TABLE3 + x + 1), 0);
  x = (uint16_t)(x + 2);
  next_group(ram, r);
  hm_dp_w16(ram, r, 0x0A, rom16(ram, AUDIO_TABLE3 + x));
  *hm_dp(ram, r, 0x0C) = rom8(ram, AUDIO_TABLE3 + x + 2);
  uint16_t count = groups_of_3(ram, r, rom16(ram, AUDIO_TABLE5 + (uint8_t)(ins << 1)), ret_divide);
  r->x = count;
  send(ram, r, (uint8_t)count, (uint8_t)(count >> 8), 0);
  r->y = 0;
  next_group(ram, r);
  do {
    send_3(ram, r);
    wait_steady_then(ram, r, 0);
    set_port(ram, r, 3, 0);
    wait_steady(ram, r);
    while (port(ram, r, 3) != 1) {}
  } while (r->x != 0);
  r->a = (uint16_t)((count & 0xFF) << 8 | 1);
  r->c = 1;
}

/* ─────────────────────────────────────────────────────────────────────────
 * UNK_Audio12 ($83:8AFF, JSL, RTL): command $03, the instrument of sound
 * effect $0114 (Table2), noted in $0104 + $0103 ($0103 counts them).
 * ───────────────────────────────────────────────────────────────────────── */
void hm_UNK_Audio12(uint8_t *ram, HmRegs *r)
{
  r->x = 0;
  *hm_dp(ram, r, 0x92) = 0x03;
  jsr(ram, r, 0x8B0C, hm_UNK_Audio8);
  wait_steady_then(ram, r, 1);
  uint8_t ins = rom8(ram, AUDIO_TABLE2 + rd8(ram, r, 0x0114));
  *hm_dp(ram, r, 0x94) = ins;
  uint8_t n = rd8(ram, r, 0x0103);
  hm_bus_wr8(ram, db_addr(r, (uint16_t)(0x0104 + n)), ins);
  hm_bus_wr8(ram, db_addr(r, 0x0103), (uint8_t)(n + 1));
  send_instrument(ram, r, 0x8B4A, 0x8C54);
  set_port(ram, r, 3, 0);
}

/* ─────────────────────────────────────────────────────────────────────────
 * UNK_Audio9 ($83:878C, JSL, RTL): the instruments of song $0110 (its 11 in
 * Table6, skipping the 0s), each with command $03, noted in $0104-$010E;
 * $0103 and $010F count them. Quirk: when the list ends with 0s,
 * instrument 6 is sent as well (and not noted).
 * Leftovers: $84 = where the list ends, X = $84, Y = how many were sent,
 * A from the last instrument, C = 1.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_UNK_Audio9(uint8_t *ram, HmRegs *r)
{
  hm_bus_wr8(ram, db_addr(r, 0x0103), 0);
  hm_bus_wr8(ram, db_addr(r, 0x010F), 0);
  for (uint16_t x = 0; x != 0x0B; x++) hm_bus_wr8(ram, db_addr(r, (uint16_t)(0x0104 + x)), 0);
  r->y = 0;
  hm_dp_w16(ram, r, 0x7E, rd8(ram, r, 0x0110));
  r->a = 0x000E;
  times(ram, r, 0x87B5);
  uint16_t x = (uint16_t)(r->a + 3);
  uint16_t end = (uint16_t)(r->a + 0x0E);
  hm_dp_w16(ram, r, 0x84, end);
  for (;;) {
    uint8_t ins = rom8(ram, AUDIO_TABLE6 + x);
    *hm_dp(ram, r, 0x94) = ins;
    if (ins) {
      hm_bus_wr8(ram, db_addr(r, (uint16_t)(0x0104 + r->y)), ins);
      hm_bus_wr8(ram, db_addr(r, 0x0103), (uint8_t)(rd8(ram, r, 0x0103) + 1));
      hm_bus_wr8(ram, db_addr(r, 0x010F), (uint8_t)(rd8(ram, r, 0x010F) + 1));
    } else {
      do x++; while (x != end && rom8(ram, AUDIO_TABLE6 + x) == 0);
      if (x != end) continue;                              /* found one: back to the top */
      *hm_dp(ram, r, 0x94) = 0x06;
      x--;
    }
    r->x = x;
    *hm_dp(ram, r, 0x92) = 0x03;
    jsr(ram, r, 0x87EF, hm_UNK_Audio8);
    wait_steady_then(ram, r, 1);
    uint16_t y = r->y;
    hm_push8(ram, r, (uint8_t)(x >> 8)); hm_push8(ram, r, (uint8_t)x);   /* PHX; PHY */
    hm_push8(ram, r, (uint8_t)(y >> 8)); hm_push8(ram, r, (uint8_t)y);
    send_instrument(ram, r, 0x8816, 0x8920);
    hm_pull8(ram, r); hm_pull8(ram, r); hm_pull8(ram, r); hm_pull8(ram, r);
    x = (uint16_t)(x + 1);
    r->y = (uint16_t)(y + 1);
    r->x = x;
    if (x == end) break;
  }
  r->c = 1;
}

/* ─────────────────────────────────────────────────────────────────────────
 * UNK_Audio3 ($83:8598, JSL, RTL): the 10 instruments of Table4 (0 =
 * skip), each with command $02. Leftovers: X = 10, A from the last one
 * sent (or Table4's last 0), C = 1.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_UNK_Audio3(uint8_t *ram, HmRegs *r)
{
  for (uint16_t x = 0; x != 0x0A; x++) {
    uint8_t ins = rom8(ram, AUDIO_TABLE4 + x);
    *hm_dp(ram, r, 0x94) = ins;
    set_a8(r, ins);
    r->x = x;
    if (!ins) continue;
    *hm_dp(ram, r, 0x92) = 0x02;
    jsr(ram, r, 0x85B8, hm_UNK_Audio8);
    wait_steady_then(ram, r, 1);
    hm_push8(ram, r, (uint8_t)(x >> 8)); hm_push8(ram, r, (uint8_t)x);   /* PHX */
    send_instrument(ram, r, 0x85DE, 0x86E8);
    hm_pull8(ram, r); hm_pull8(ram, r);
  }
  r->x = 0x0A;
  r->c = 1;
}

/* ─────────────────────────────────────────────────────────────────────────
 * UNK_Audio11 ($83:8A26, JSL, RTL): command $05, the data of song $0110
 * (Table6's pointer, Table7's size). The wait for the count's "done" is a
 * plain loop, and each group starts by clearing port 3.
 * Leftovers: A = the count's high byte : 0, X = 0, Y = 3 count, C = 1.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_UNK_Audio11(uint8_t *ram, HmRegs *r)
{
  *hm_dp(ram, r, 0x92) = 0x05;
  jsr(ram, r, 0x8A30, hm_UNK_Audio8);
  wait_steady_then(ram, r, 1);
  uint8_t song = rd8(ram, r, 0x0110);
  uint16_t count = groups_of_3(ram, r, rom16(ram, AUDIO_TABLE7 + (uint8_t)(song << 1)), 0x8A5E);
  send(ram, r, (uint8_t)count, (uint8_t)(count >> 8), 0);
  while (port(ram, r, 3) != 4) {}
  r->y = 0;
  hm_dp_w16(ram, r, 0x7E, song);
  r->a = 0x000E;
  times(ram, r, 0x8A98);
  hm_dp_w16(ram, r, 0x0A, rom16(ram, AUDIO_TABLE6 + r->a));
  *hm_dp(ram, r, 0x0C) = rom8(ram, AUDIO_TABLE6 + (uint16_t)(r->a + 2));
  r->x = count;
  do {
    set_port(ram, r, 3, 0);
    wait_steady_then(ram, r, 1);
    send_3(ram, r);
    wait_steady_then(ram, r, 0);
    set_port(ram, r, 3, 0);
  } while (r->x != 0);
  r->a = (uint16_t)(count & 0xFF00);
  r->c = 1;
}

/* ─────────────────────────────────────────────────────────────────────────
 * UNK_Audio4 ($83:84D3, JSL, RTL): command $01, then $3F2 bytes from
 * $B2:D120 (the sound program's data). Here each group waits for "ready"
 * with the full check and nothing waits after the last one.
 * Leftovers: A = the count's high byte : 0, X = 0, Y = 3 count, C = 1.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_UNK_Audio4(uint8_t *ram, HmRegs *r)
{
  *hm_dp(ram, r, 0x92) = 0x01;
  jsr(ram, r, 0x84DD, hm_UNK_Audio8);
  wait_steady_then(ram, r, 1);
  uint16_t count = groups_of_3(ram, r, 0x03F2, 0x8502);
  send(ram, r, (uint8_t)count, (uint8_t)(count >> 8), 0);
  wait_steady_then(ram, r, 4);
  set_port(ram, r, 3, 0);
  r->y = 0;
  hm_dp_w16(ram, r, 0x0A, 0xD120);
  *hm_dp(ram, r, 0x0C) = 0xB2;
  r->x = count;
  do {
    wait_steady_then(ram, r, 1);
    send_3(ram, r);
    wait_steady_then(ram, r, 0);
    set_port(ram, r, 3, 0);
  } while (r->x != 0);
  r->a = (uint16_t)(count & 0xFF00);
  r->c = 1;
}

/* ─────────────────────────────────────────────────────────────────────────
 * UNK_Audio5 ($80:95DE, JSL, RTL): which music the map being entered ($22)
 * asks for, into $0110:
 *   - from 6 pm (hour 18) on, none ($FF), except on map $1E;
 *   - $0196 bit 4: song $16; else $0196 bit 1: maps $57-$5A none (0),
 *     $31-$56 song $13, $15-$30 song $14, below $15 song $13, and from $5B
 *     on, as normal;
 *   - normally the map's entry in Mayby_Table_AudioTracksbySeason
 *     ($80:B8E7, one word per map: $FFFF = none, 0 stays in $0110), a list
 *     of four songs, one per season (read with the data bank).
 * Leftovers: A, X, Y and C as each way out leaves them (see the code).
 * ───────────────────────────────────────────────────────────────────────── */
#define TRACKS_BY_MAP 0x80B8E7

void hm_UNK_Audio5(uint8_t *ram, HmRegs *r)
{
  uint8_t map = *hm_dp(ram, r, 0x22);
  set_a8(r, map);
  r->c = map >= 0x1E;
  if (map != 0x1E) {
    uint8_t hour = hm_bus_rd8(ram, 0x7F1F1C);
    set_a8(r, hour);
    r->c = hour >= 18;
    if (r->c) {                                       /* Max7E0110 */
      set_a8(r, 0xFF);
      hm_bus_wr8(ram, db_addr(r, 0x0110), 0xFF);
      return;
    }
  }
  hm_bus_wr8(ram, db_addr(r, 0x0110), 0);
  uint16_t flags = hm_bus_rd16(ram, db_addr(r, 0x0196));
  if (flags & 0x0010) {
    r->a = 0x0016;
    hm_bus_wr8(ram, db_addr(r, 0x0110), 0x16);
    return;
  }
  r->a = flags & 0x0002;
  if (r->a) {
    r->a = map;
    r->c = map >= 0x5B;
    if (!r->c) {
      r->c = map >= 0x57;
      if (r->c) return;
      uint8_t song = 0x13;
      r->c = map >= 0x31;
      if (!r->c) { r->c = map >= 0x15; if (r->c) song = 0x14; }
      r->a = song;
      hm_bus_wr8(ram, db_addr(r, 0x0110), song);
      return;
    }
  }
  r->a = (uint16_t)(map << 1);                        /* LDA #0; XBA; LDA; ASL */
  r->c = 0;
  r->x = r->a;
  uint16_t list = hm_bus_rd16(ram, TRACKS_BY_MAP + r->x);
  r->a = list;
  r->c = list == 0xFFFF;                              /* CMP #$FFFF */
  if (list == 0xFFFF) return;
  hm_dp_w16(ram, r, 0x7E, list);
  uint8_t season = hm_bus_rd8(ram, 0x7F1F19);
  r->y = season;
  uint8_t song = hm_bus_rd8(ram, (db_addr(r, list) + season) & 0xFFFFFF);   /* LDA ($7E),Y */
  r->a = song;
  hm_bus_wr8(ram, db_addr(r, 0x0110), song);
}

/* ─────────────────────────────────────────────────────────────────────────
 * UNK_Audio1 ($83:843D, JSL, RTL; at power-on, from RESET): the sound
 * program goes to the chip with the protocol of its boot ROM (the "IPL"):
 *   1. wait until ports 0-1 read $BBAA (the chip is waiting);
 *   2. the block's address ($0500) in ports 2-3, 1 in port 1 ("data
 *      follows"), $CC in port 0; wait for the $CC back;
 *   3. $FD6 bytes from [$0A] (the caller gives $AD:8000): each byte in
 *      port 1 and its index (0, 1, 2... wrapping at $FF) in port 0, with
 *      one 16-bit store (which writes port 1 first), each once the chip
 *      echoed the previous index;
 *   4. the address $0500 again, 0 in port 1 ("run it") and in port 0 the
 *      last index + 4 (4 if that wraps to 0); wait for its echo.
 * The original loops back to 3 while an overflow (V) says another block
 * follows, but its second header is built with X = 0, which never sets
 * it: there is only one block.
 * Leftovers: A = the last value of port 0 (high byte 0), X = 0, Y = $FD6,
 * the flags as they were (PHP / PLP); the stack below keeps the PHA'd
 * values.
 * ───────────────────────────────────────────────────────────────────────── */
#define IPL_BLOCK_SIZE 0x0FD6
#define IPL_ADDRESS    0x0500

/* CMP $2140 (8-bit) until port 0 reads v */
static void wait_echo(uint8_t *ram, HmRegs *r, uint8_t v)
{
  while (port(ram, r, 0) != v) {}
}

void hm_UNK_Audio1(uint8_t *ram, HmRegs *r)
{
  uint8_t flags = r->p;
  hm_push8(ram, r, flags);                                    /* PHP */
  r->y = 0;
  for (;;) {                                                  /* CMP $2140 (16-bit) */
    uint8_t lo = port(ram, r, 0), hi = port(ram, r, 1);
    if (lo == 0xAA && hi == 0xBB) break;
  }
  hm_push8(ram, r, 0xCC);                                     /* PHA */
  set_port(ram, r, 2, (uint8_t)IPL_ADDRESS);
  set_port(ram, r, 3, (uint8_t)(IPL_ADDRESS >> 8));
  set_port(ram, r, 1, 1);                                     /* CPX #1; ROL: data follows */
  hm_pull8(ram, r);                                           /* PLA */
  set_port(ram, r, 0, 0xCC);
  wait_echo(ram, r, 0xCC);

  uint8_t index = 0;
  for (uint16_t left = IPL_BLOCK_SIZE; left; left--) {
    uint8_t byte = hm_bus_rd8(ram, hm_dp_long(ram, r, 0x0A, r->y));
    r->y++;
    if (left != IPL_BLOCK_SIZE) {
      wait_echo(ram, r, index);
      index++;
    }
    set_port(ram, r, 1, byte);                                /* STA $2140 (16-bit): */
    set_port(ram, r, 0, index);                               /* $2141 goes first */
  }
  wait_echo(ram, r, index);
  uint8_t start = (uint8_t)(index + 4);                       /* ADC #3 after CMP (C = 1) */
  if (start == 0) start = 4;                                  /* again: 0 + 3 + the carry */
  hm_push8(ram, r, start);                                    /* PHA */
  r->x = 0;
  set_port(ram, r, 2, (uint8_t)IPL_ADDRESS);
  set_port(ram, r, 3, (uint8_t)(IPL_ADDRESS >> 8));
  set_port(ram, r, 1, 0);                                     /* run it */
  hm_pull8(ram, r);                                           /* PLA */
  set_port(ram, r, 0, start);
  wait_echo(ram, r, start);
  r->a = start;
  hm_pull8(ram, r);                                           /* PLP */
  r->c = flags & 1;
}
