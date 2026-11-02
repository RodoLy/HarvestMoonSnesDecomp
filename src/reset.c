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

/* ── Names of the memory locations used here ────────────────────────────── */

/* Direct page */
#define SOUND_PROGRAM     0x0A   /* 24b $0A-$0C: UploadSoundProgram's argument, where
                                    the sound program is */
#define SOUND_PROGRAM_BANK 0x0C  /* 16b: its bank (and a 0 byte) */

/* WRAM of the data bank */
#define STACK_TOP         0x1F00 /* the stack starts here (S), the WRAM's first bank
                                    ends at $2000 */
#define PAD1              0x0124 /* 16b: pad 1's buttons held (joypad.c) */
#define PAD1_LAST         0x0126 /* 16b: last frame's */
#define PAD1_NEW          0x0128 /* 16b: pressed this frame (HM_JOY_NEW elsewhere) */
#define PAD1_UP           0x012A /* 16b: let go this frame */
#define PAD2              0x0130 /* 16b: the same for pad 2 */
#define PAD2_LAST         0x0132
#define PAD2_NEW          0x0134
#define PAD2_UP           0x0136
#define UNKNOWN_0148      0x0148 /* 8b: cleared here and by FirstNightReset (day.c);
                                    nothing found reads it */
#define WRAM_LOW_END      0x2000 /* the first $2000 bytes of WRAM, seen in every bank */
#define WRAM_REST         0x7E2000u /* bus address: the rest of bank $7E */
#define WRAM_REST_SIZE    0xE000
#define WRAM_BANK_7F      0x7F0000u /* bus address: the bank $7F, all of it */

/* The CPU's registers ($42xx) */
#define NMITIMEN          0x4200 /* NMI, line IRQ and automatic pad reading on / off */
#define WRIO              0x4201 /* the I/O port */
#define WRMPYA            0x4202 /* multiplier: first number */
#define WRMPYB            0x4203 /* multiplier: second number (starts it) */
#define WRDIVL            0x4204 /* divider: the number divided (16b) */
#define WRDIVH            0x4205
#define WRDIVB            0x4206 /* divider: the divisor (starts it) */
#define HTIMEL            0x4207 /* the IRQ's dot (16b) */
#define HTIMEH            0x4208
#define VTIMEL            0x4209 /* the IRQ's line (16b) */
#define VTIMEH            0x420A
#define MDMAEN            0x420B /* starts the DMA channels */
#define HDMAEN            0x420C /* the HDMA channels on */
#define MEMSEL            0x420D /* bit 0: FastROM */
#define RDNMI             0x4210 /* read-only from here to $421F */
#define TIMEUP            0x4211
#define HVBJOY            0x4212
#define RDIO              0x4213
#define RDDIVL            0x4214
#define RDDIVH            0x4215
#define RDMPYL            0x4216
#define RDMPYH            0x4217
#define JOY1L             0x4218
#define JOY1H             0x4219
#define JOY2L             0x421A
#define JOY2H             0x421B
#define JOY3L             0x421C
#define JOY3H             0x421D
#define JOY4L             0x421E
#define JOY4H             0x421F

/* The picture chip's registers ($21xx) */
#define INIDISP           0x2100 /* forced blank (bit 7) and brightness */
#define OBSEL             0x2101 /* sprite sizes and graphics address */
#define OAMADDL           0x2102 /* OAM address (16b) */
#define OAMADDH           0x2103
#define OAMDATA           0x2104 /* OAM data (written twice) */
#define BGMODE            0x2105
#define MOSAIC            0x2106
#define BG1SC             0x2107 /* the backgrounds' tile maps */
#define BG2SC             0x2108
#define BG3SC             0x2109
#define BG4SC             0x210A
#define BG12NBA           0x210B /* the backgrounds' graphics */
#define BG34NBA           0x210C
#define BG1HOFS           0x210D /* the scrolls (written twice) */
#define BG1VOFS           0x210E
#define BG2HOFS           0x210F
#define BG2VOFS           0x2110
#define BG3HOFS           0x2111
#define BG3VOFS           0x2112
#define BG4HOFS           0x2113
#define BG4VOFS           0x2114
#define VMAIN             0x2115 /* VRAM increment */
#define VMADDL            0x2116 /* VRAM address (16b) */
#define VMADDH            0x2117
#define VMDATAL           0x2118 /* VRAM data (16b) */
#define VMDATAH           0x2119
#define M7SEL             0x211A /* mode 7 (the matrix and centre written twice) */
#define M7A               0x211B
#define M7B               0x211C
#define M7C               0x211D
#define M7D               0x211E
#define M7X               0x211F
#define M7Y               0x2120
#define CGADD             0x2121 /* CGRAM address */
#define CGDATA            0x2122 /* CGRAM data (written twice) */
#define W12SEL            0x2123 /* the windows */
#define W34SEL            0x2124
#define WOBJSEL           0x2125
#define WH0               0x2126
#define WH1               0x2127
#define WH2               0x2128
#define WH3               0x2129
#define WBGLOG            0x212A
#define WOBJLOG           0x212B
#define TM                0x212C /* main screen layers */
#define TS                0x212D /* sub screen layers */
#define TMW               0x212E
#define TSW               0x212F
#define CGWSEL            0x2130 /* colour math */
#define CGADSUB           0x2131
#define COLDATA           0x2132 /* the fixed colour */
#define SETINI            0x2133
#define MPYL              0x2134 /* read-only from here to $213F */
#define MPYM              0x2135
#define MPYH              0x2136
#define SLHV              0x2137
#define RDOAM             0x2138
#define RDVRAML           0x2139
#define RDVRAMH           0x213A
#define RDCGRAM           0x213B
#define OPHCT             0x213C
#define OPVCT             0x213D
#define STAT77            0x213E
#define STAT78            0x213F
#define WMDATA            0x2180 /* the WRAM port: data */
#define WMADDL            0x2181 /* its address (24b) */
#define WMADDM            0x2182
#define WMADDH            0x2183

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
  static const uint16_t PADS[8] = { PAD1, PAD1_NEW, PAD1_LAST, PAD1_UP, PAD2, PAD2_NEW, PAD2_LAST, PAD2_UP };
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
  wr8(ram, r, UNKNOWN_0148, 0);
  wr8(ram, r, NMITIMEN, 0x80);                             /* the NMI on */
  wr8(ram, r, INIDISP, 0);
  /* CLI; JML IntroScreen: the check stops here and puts its frame back */
  for (int i = 0; i < 3; i++) ram[(uint16_t)(s_in + 1 + i)] = frame[i];
  r->s = s_in;
  g_hm_next = 0x82D75E;
}
