/*
 * reset.c — the power-on (the RESET vector, $80:8428).
 *
 * The CPU starts in emulation mode with interrupts off; RESET switches to
 * native mode, puts the stack at $1F00 and the direct page at 0, writes
 * every register of the CPU and the picture chip with its start value,
 * clears all the WRAM, uploads the sound program to the sound chip and
 * starts it, clears VRAM, OAM and CGRAM, checks the saved games, turns
 * the NMI on and goes to the title (IntroScreen, JML).
 *
 * It never returns and throws the stack away. Its check (HM_TOLOOP, at
 * frame 1 like UNK_Audio1, whose upload needs the chip's boot ROM:
 * HM_LIVE) stops at the JML and puts the checker's own frame back on the
 * stack (the bytes RESET cleared, and S), so the call can end; the
 * rewrite does the same at its end.
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

typedef void Fn(uint8_t *ram, HmRegs *r);

static uint32_t db(const HmRegs *r, uint16_t a) { return (uint32_t)r->db << 16 | a; }
static void wr8(uint8_t *ram, const HmRegs *r, uint16_t a, uint8_t v) { hm_bus_wr8(ram, db(r, a), v); }
static void wr16(uint8_t *ram, const HmRegs *r, uint16_t a, uint16_t v) { hm_bus_wr16(ram, db(r, a), v); }
static void set_a8(HmRegs *r, uint8_t v) { r->a = (uint16_t)((r->a & 0xFF00) | v); }

static void jsl(uint8_t *ram, HmRegs *r, uint16_t at, uint8_t bank, Fn *fn)
{
  hm_jsl(ram, r, (uint16_t)(at + 3), bank);
  fn(ram, r);
  hm_rtl(ram, r);
}

/* The registers, in the order RESET writes them (STZ, or the value):
 * the CPU's ($4200-$421F: the NMI and the DMA off, the I/O port $FF, the
 * multiplier and divider, the timers, FastROM on, and stores to the
 * read-only ones), then the picture chip's ($2100-$2133: the scroll and
 * mode 7 registers twice, VRAM increment after the high byte ($80),
 * colour math $30 / $E0), then stores to its read-only ones and the WRAM
 * port ($2134-$2183). */
typedef struct { uint16_t reg; uint8_t v; } Write;
static const Write REGS[] = {
  { 0x4200, 0 }, { 0x420B, 0 }, { 0x420C, 0 }, { 0x4201, 0xFF },
  { 0x4202, 0 }, { 0x4203, 0 }, { 0x4204, 0 }, { 0x4205, 0 }, { 0x4206, 0 },
  { 0x4207, 0 }, { 0x4208, 0 }, { 0x4209, 0 }, { 0x420A, 0 }, { 0x420B, 0 }, { 0x420C, 0 },
  { 0x420D, 0x01 },
  { 0x4210, 0 }, { 0x4211, 0 }, { 0x4212, 0 }, { 0x4213, 0 }, { 0x4214, 0 }, { 0x4215, 0 },
  { 0x4216, 0 }, { 0x4217, 0 }, { 0x4218, 0 }, { 0x4219, 0 }, { 0x421A, 0 }, { 0x421B, 0 },
  { 0x421C, 0 }, { 0x421D, 0 }, { 0x421E, 0 }, { 0x421F, 0 },
  { 0x2100, 0 }, { 0x2101, 0 }, { 0x2102, 0 }, { 0x2103, 0 }, { 0x2104, 0 }, { 0x2104, 0 },
  { 0x2105, 0 }, { 0x2106, 0 }, { 0x2107, 0 }, { 0x2108, 0 }, { 0x2109, 0 }, { 0x210A, 0 },
  { 0x210B, 0 }, { 0x210C, 0 },
  { 0x210D, 0 }, { 0x210D, 0 }, { 0x210E, 0 }, { 0x210E, 0 }, { 0x210F, 0 }, { 0x210F, 0 },
  { 0x2110, 0 }, { 0x2110, 0 }, { 0x2111, 0 }, { 0x2111, 0 }, { 0x2112, 0 }, { 0x2112, 0 },
  { 0x2113, 0 }, { 0x2113, 0 }, { 0x2114, 0 }, { 0x2114, 0 },
  { 0x2115, 0x80 }, { 0x2116, 0 }, { 0x2117, 0 }, { 0x2118, 0 }, { 0x2119, 0 }, { 0x211A, 0 },
  { 0x211B, 0 }, { 0x211B, 0 }, { 0x211C, 0 }, { 0x211C, 0 }, { 0x211D, 0 }, { 0x211D, 0 },
  { 0x211E, 0 }, { 0x211E, 0 }, { 0x211F, 0 }, { 0x211F, 0 }, { 0x2120, 0 }, { 0x2120, 0 },
  { 0x2121, 0 }, { 0x2122, 0 }, { 0x2122, 0 },
  { 0x2123, 0 }, { 0x2124, 0 }, { 0x2125, 0 }, { 0x2126, 0 }, { 0x2127, 0 }, { 0x2128, 0 },
  { 0x2129, 0 }, { 0x212A, 0 }, { 0x212B, 0 }, { 0x212C, 0 }, { 0x212D, 0 }, { 0x212E, 0 },
  { 0x212F, 0 }, { 0x2130, 0x30 }, { 0x2131, 0 }, { 0x2132, 0xE0 }, { 0x2133, 0 },
  { 0x2134, 0 }, { 0x2135, 0 }, { 0x2136, 0 }, { 0x2137, 0 }, { 0x2138, 0 }, { 0x2139, 0 },
  { 0x213A, 0 }, { 0x213B, 0 }, { 0x213B, 0 }, { 0x213C, 0 }, { 0x213D, 0 }, { 0x213E, 0 },
  { 0x213F, 0 }, { 0x2180, 0 }, { 0x2181, 0 }, { 0x2182, 0 }, { 0x2183, 0 },
};

void hm_RESET(uint8_t *ram, HmRegs *r)
{
  /* the checker's frame (not the original's: see above) */
  uint16_t s_in = r->s;
  uint8_t frame[3];
  for (int i = 0; i < 3; i++) frame[i] = ram[(uint16_t)(s_in + 1 + i)];

  r->c = 0;                                                /* SEI; CLC; XCE (native already) */
  r->x = 0x1F00;                                           /* REP #$30; LDX; TXS */
  r->s = 0x1F00;
  r->a = 0;                                                /* LDA #0; TCD */
  r->d = 0;
  for (unsigned i = 0; i < sizeof REGS / sizeof REGS[0]; i++) {   /* SEP #$30 */
    if (REGS[i].v) set_a8(r, REGS[i].v);
    wr8(ram, r, REGS[i].reg, REGS[i].v);
  }
  r->a = 0;                                                /* REP #$30: all the WRAM cleared */
  for (r->x = 0; r->x != 0x2000; r->x += 2) wr16(ram, r, r->x, 0);
  for (r->x = 0; r->x != 0xE000; r->x += 2) hm_bus_wr16(ram, 0x7E2000u + r->x, 0);
  r->x = 0;
  do { hm_bus_wr16(ram, 0x7F0000u + r->x, 0); r->x += 2; } while (r->x != 0);
  r->c = 1;                                                /* CPX #0 */
  static const uint16_t PADS[8] = { 0x0124, 0x0128, 0x0126, 0x012A, 0x0130, 0x0134, 0x0132, 0x0136 };
  for (int i = 0; i < 8; i++) wr16(ram, r, PADS[i], 0);
  r->a = 0x8000;                                           /* the sound program at $AD:8000 */
  hm_dp_w16(ram, r, 0x0A, 0x8000);
  r->a = 0x00AD;
  hm_dp_w16(ram, r, 0x0C, 0x00AD);
  jsl(ram, r, 0x85FC, 0x83, hm_UNK_Audio1);
  set_a8(r, 0x00);                                         /* SEP #$20 */
  jsl(ram, r, 0x8604, 0x83, hm_UNK_Audio2);
  jsl(ram, r, 0x8608, 0x83, hm_UNK_Audio3);
  jsl(ram, r, 0x860C, 0x83, hm_UNK_Audio4);
  jsl(ram, r, 0x8610, 0x80, hm_InitialzieScreenStatusVar);
  jsl(ram, r, 0x8614, 0x80, hm_ZeroesVRAM);
  jsl(ram, r, 0x8618, 0x80, hm_ZeroesOAM);
  jsl(ram, r, 0x861C, 0x80, hm_ZeroesCGRAM);
  jsl(ram, r, 0x8620, 0x85, hm_ClearSpriteDataTables);
  jsl(ram, r, 0x8624, 0x85, hm_InitializeOBJs);
  jsl(ram, r, 0x8628, 0x83, hm_CheckSRAMIntegrity);
  set_a8(r, 0x80);                                         /* SEP #$20; REP #$10 */
  wr8(ram, r, 0x2100, 0x80);                               /* the screen off */
  wr8(ram, r, 0x0148, 0);
  wr8(ram, r, 0x4200, 0x80);                               /* the NMI on */
  wr8(ram, r, 0x2100, 0);
  /* CLI; JML IntroScreen: the check stops here and puts its frame back */
  for (int i = 0; i < 3; i++) ram[(uint16_t)(s_in + 1 + i)] = frame[i];
  r->s = s_in;
  g_hm_next = 0x82D75E;
}
