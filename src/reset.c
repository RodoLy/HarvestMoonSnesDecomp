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
 * frame 1 like UploadSoundProgram, whose upload needs the chip's boot ROM:
 * HM_LIVE) stops at the JML and puts the checker's own frame back on the
 * stack (the bytes RESET cleared, and S), so the call can end; the
 * rewrite does the same at its end.
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */
#include "hm_vars.h"
#include "hm_regs.h"

/* ── Names of the memory locations used here ────────────────────────────── */

/* Direct page */
#define SOUND_PROGRAM     0x0A   /* 24b $0A-$0C: UploadSoundProgram's argument, where
                                    the sound program is */
#define SOUND_PROGRAM_BANK 0x0C  /* 16b: its bank (and a 0 byte) */

/* WRAM of the data bank */
#define STACK_TOP         0x1F00 /* the stack starts here (S), the WRAM's first bank
                                    ends at $2000 */
#define WRAM_LOW_END      0x2000 /* the first $2000 bytes of WRAM, seen in every bank */
#define WRAM_REST         0x7E2000u /* bus address: the rest of bank $7E */
#define WRAM_REST_SIZE    0xE000
#define WRAM_BANK_7F      0x7F0000u /* bus address: the bank $7F, all of it */

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
  { NMITIMEN, 0 }, { MDMAEN, 0 }, { HDMAEN, 0 }, { WRIO, 0xFF },
  { WRMPYA, 0 }, { WRMPYB, 0 }, { WRDIVL, 0 }, { WRDIVH, 0 }, { WRDIVB, 0 },
  { HTIMEL, 0 }, { HTIMEH, 0 }, { VTIMEL, 0 }, { VTIMEH, 0 }, { MDMAEN, 0 }, { HDMAEN, 0 },
  { MEMSEL, 0x01 },
  { RDNMI, 0 }, { TIMEUP, 0 }, { HVBJOY, 0 }, { RDIO, 0 }, { RDDIVL, 0 }, { RDDIVH, 0 },
  { RDMPYL, 0 }, { RDMPYH, 0 }, { JOY1L, 0 }, { JOY1H, 0 }, { JOY2L, 0 }, { JOY2H, 0 },
  { JOY3L, 0 }, { JOY3H, 0 }, { JOY4L, 0 }, { JOY4H, 0 },
  { INIDISP, 0 }, { OBSEL, 0 }, { OAMADDL, 0 }, { OAMADDH, 0 }, { OAMDATA, 0 }, { OAMDATA, 0 },
  { BGMODE, 0 }, { MOSAIC, 0 }, { BG1SC, 0 }, { BG2SC, 0 }, { BG3SC, 0 }, { BG4SC, 0 },
  { BG12NBA, 0 }, { BG34NBA, 0 },
  { BG1HOFS, 0 }, { BG1HOFS, 0 }, { BG1VOFS, 0 }, { BG1VOFS, 0 }, { BG2HOFS, 0 }, { BG2HOFS, 0 },
  { BG2VOFS, 0 }, { BG2VOFS, 0 }, { BG3HOFS, 0 }, { BG3HOFS, 0 }, { BG3VOFS, 0 }, { BG3VOFS, 0 },
  { BG4HOFS, 0 }, { BG4HOFS, 0 }, { BG4VOFS, 0 }, { BG4VOFS, 0 },
  { VMAIN, 0x80 }, { VMADDL, 0 }, { VMADDH, 0 }, { VMDATAL, 0 }, { VMDATAH, 0 }, { M7SEL, 0 },
  { M7A, 0 }, { M7A, 0 }, { M7B, 0 }, { M7B, 0 }, { M7C, 0 }, { M7C, 0 },
  { M7D, 0 }, { M7D, 0 }, { M7X, 0 }, { M7X, 0 }, { M7Y, 0 }, { M7Y, 0 },
  { CGADD, 0 }, { CGDATA, 0 }, { CGDATA, 0 },
  { W12SEL, 0 }, { W34SEL, 0 }, { WOBJSEL, 0 }, { WH0, 0 }, { WH1, 0 }, { WH2, 0 },
  { WH3, 0 }, { WBGLOG, 0 }, { WOBJLOG, 0 }, { TM, 0 }, { TS, 0 }, { TMW, 0 },
  { TSW, 0 }, { CGWSEL, 0x30 }, { CGADSUB, 0 }, { COLDATA, 0xE0 }, { SETINI, 0 },
  { MPYL, 0 }, { MPYM, 0 }, { MPYH, 0 }, { SLHV, 0 }, { RDOAM, 0 }, { RDVRAML, 0 },
  { RDVRAMH, 0 }, { RDCGRAM, 0 }, { RDCGRAM, 0 }, { OPHCT, 0 }, { OPVCT, 0 }, { STAT77, 0 },
  { STAT78, 0 }, { WMDATA, 0 }, { WMADDL, 0 }, { WMADDM, 0 }, { WMADDH, 0 },
};

void hm_RESET(uint8_t *ram, HmRegs *r)
{
  /* the checker's frame (not the original's: see above) */
  uint16_t s_in = r->s;
  uint8_t frame[3];
  for (int i = 0; i < 3; i++) frame[i] = ram[(uint16_t)(s_in + 1 + i)];

  r->c = 0;                                                /* SEI; CLC; XCE (native already) */
  r->x = STACK_TOP;                                        /* REP #$30; LDX; TXS */
  r->s = STACK_TOP;
  r->a = 0;                                                /* LDA #0; TCD */
  r->d = 0;
  for (unsigned i = 0; i < sizeof REGS / sizeof REGS[0]; i++) {   /* SEP #$30 */
    if (REGS[i].v) set_a8(r, REGS[i].v);
    wr8(ram, r, REGS[i].reg, REGS[i].v);
  }
  r->a = 0;                                                /* REP #$30: all the WRAM cleared */
  for (r->x = 0; r->x != WRAM_LOW_END; r->x += 2) wr16(ram, r, r->x, 0);
  for (r->x = 0; r->x != WRAM_REST_SIZE; r->x += 2) hm_bus_wr16(ram, WRAM_REST + r->x, 0);
  r->x = 0;
  do { hm_bus_wr16(ram, WRAM_BANK_7F + r->x, 0); r->x += 2; } while (r->x != 0);
  r->c = 1;                                                /* CPX #0 */
  static const uint16_t PADS[8] = { HM_PAD1_HELD, HM_PAD1_NEW, HM_PAD1_LAST, HM_PAD1_UP, HM_PAD2_HELD, HM_PAD2_NEW, HM_PAD2_LAST, HM_PAD2_UP };
  for (int i = 0; i < 8; i++) wr16(ram, r, PADS[i], 0);
  r->a = 0x8000;                                           /* the sound program at $AD:8000 */
  hm_dp_w16(ram, r, SOUND_PROGRAM, 0x8000);
  r->a = 0x00AD;
  hm_dp_w16(ram, r, SOUND_PROGRAM_BANK, 0x00AD);
  jsl(ram, r, 0x85FC, 0x83, hm_UploadSoundProgram);
  set_a8(r, 0x00);                                         /* SEP #$20 */
  jsl(ram, r, 0x8604, 0x83, hm_SoundCommand0B);
  jsl(ram, r, 0x8608, 0x83, hm_LoadDefaultInstruments);
  jsl(ram, r, 0x860C, 0x83, hm_SendSoundData);
  jsl(ram, r, 0x8610, 0x80, hm_InitializeScreenStatusVars);
  jsl(ram, r, 0x8614, 0x80, hm_ZeroesVRAM);
  jsl(ram, r, 0x8618, 0x80, hm_ZeroesOAM);
  jsl(ram, r, 0x861C, 0x80, hm_ZeroesCGRAM);
  jsl(ram, r, 0x8620, 0x85, hm_ClearSpriteDataTables);
  jsl(ram, r, 0x8624, 0x85, hm_InitializeOBJs);
  jsl(ram, r, 0x8628, 0x83, hm_CheckSRAMIntegrity);
  set_a8(r, 0x80);                                         /* SEP #$20; REP #$10 */
  wr8(ram, r, INIDISP, 0x80);                              /* the screen off */
  wr8(ram, r, HM_UNKNOWN_0148, 0);
  wr8(ram, r, NMITIMEN, 0x80);                             /* the NMI on */
  wr8(ram, r, INIDISP, 0);
  /* CLI; JML IntroScreen: the check stops here and puts its frame back */
  for (int i = 0; i < 3; i++) ram[(uint16_t)(s_in + 1 + i)] = frame[i];
  r->s = s_in;
  g_hm_next = 0x82D75E;
}
