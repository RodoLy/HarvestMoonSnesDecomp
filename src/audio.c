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
