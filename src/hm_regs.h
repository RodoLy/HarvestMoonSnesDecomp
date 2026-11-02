/*
 * hm_regs.h — the console's registers, by their names in the SNES manuals.
 *
 * The routines write them through the bus (hm_bus_wr8(ram, INIDISP, v) ...);
 * the values are the registers' addresses in bank $00 (and its mirrors).
 * Only the registers this code touches are here.
 */
#pragma once

/* ── The picture unit (PPU), $2100-$213F ─────────────────────────────────────────────── */
#define INIDISP      0x2100 /* forced blank (bit 7) and brightness */
#define OBSEL        0x2101 /* sprite sizes and graphics address */
#define OAMADDL      0x2102 /* OAM address (16b) */
#define OAMADDH      0x2103
#define OAMDATA      0x2104 /* OAM data (written twice) */
#define BGMODE       0x2105
#define MOSAIC       0x2106
#define BG1SC        0x2107 /* the backgrounds' tile maps */
#define BG2SC        0x2108
#define BG3SC        0x2109
#define BG4SC        0x210A
#define BG12NBA      0x210B /* the backgrounds' graphics */
#define BG34NBA      0x210C
#define BG1HOFS      0x210D /* BG1 scroll, across (+ 2 per layer) */
#define BG1VOFS      0x210E /* and down */
#define BG2HOFS      0x210F
#define BG2VOFS      0x2110
#define BG3HOFS      0x2111
#define BG3VOFS      0x2112
#define BG4HOFS      0x2113
#define BG4VOFS      0x2114
#define VMAIN        0x2115 /* how the VRAM address moves */
#define VMADDL       0x2116 /* VRAM address (16b) */
#define VMADDH       0x2117
#define VMDATAL      0x2118 /* VRAM data (16b) */
#define VMDATAH      0x2119
#define M7SEL        0x211A /* mode 7 (the matrix and centre written twice) */
#define M7A          0x211B
#define M7B          0x211C
#define M7C          0x211D
#define M7D          0x211E
#define M7X          0x211F
#define M7Y          0x2120
#define CGADD        0x2121 /* CGRAM address */
#define CGDATA       0x2122 /* CGRAM data (written twice) */
#define W12SEL       0x2123 /* the windows */
#define W34SEL       0x2124
#define WOBJSEL      0x2125
#define WH0          0x2126
#define WH1          0x2127
#define WH2          0x2128
#define WH3          0x2129
#define WBGLOG       0x212A
#define WOBJLOG      0x212B
#define TM           0x212C /* main screen layers */
#define TS           0x212D /* sub screen layers */
#define TMW          0x212E
#define TSW          0x212F
#define CGWSEL       0x2130 /* colour math */
#define CGADSUB      0x2131
#define COLDATA      0x2132 /* the fixed colour */
#define SETINI       0x2133
#define MPYL         0x2134 /* read-only from here to $213F */
#define MPYM         0x2135
#define MPYH         0x2136
#define SLHV         0x2137
#define RDOAM        0x2138
#define RDVRAML      0x2139
#define RDVRAMH      0x213A
#define RDCGRAM      0x213B
#define OPHCT        0x213C
#define OPVCT        0x213D
#define STAT77       0x213E
#define STAT78       0x213F

/* ── The sound chip's ports, $2140-$2143 (audio.c) ───────────────────────────────────── */
#define APUIO0       0x2140 /* the sound chip's port 0; ports 1-3 follow */

/* ── The WRAM port, $2180-$2183 ──────────────────────────────────────────────────────── */
#define WMDATA       0x2180 /* the WRAM port: data */
#define WMADDL       0x2181 /* its address (24b) */
#define WMADDM       0x2182
#define WMADDH       0x2183

/* ── The CPU's registers, $4200-$421F ────────────────────────────────────────────────── */
#define NMITIMEN     0x4200 /* NMI, line IRQ and automatic pad reading on / off */
#define WRIO         0x4201 /* the I/O port */
#define WRMPYA       0x4202 /* 8b: the multiplier's first factor */
#define WRMPYB       0x4203 /* 8b: its second factor; writing it starts it */
#define WRDIVL       0x4204 /* 16b: the divider's dividend (WRDIVL/H) */
#define WRDIVH       0x4205
#define WRDIVB       0x4206 /* 8b: the divisor; writing it starts it */
#define HTIMEL       0x4207 /* the IRQ's dot (16b) */
#define HTIMEH       0x4208
#define VTIMEL       0x4209 /* the IRQ's line (16b) */
#define VTIMEH       0x420A
#define MDMAEN       0x420B /* starts the DMA channels (bit = channel) */
#define HDMAEN       0x420C /* the HDMA channels on */
#define MEMSEL       0x420D /* bit 0: FastROM */
#define RDNMI        0x4210 /* read-only from here to $421F */
#define TIMEUP       0x4211 /* the line IRQ's flag (reading it lets the IRQ go) */
#define HVBJOY       0x4212 /* bit 0 = the automatic pad reading is running */
#define RDIO         0x4213
#define RDDIVL       0x4214
#define RDDIVH       0x4215
#define RDMPYL       0x4216
#define RDMPYH       0x4217
#define JOY1L        0x4218 /* 16b: pad 1's buttons */
#define JOY1H        0x4219
#define JOY2L        0x421A /* 16b: pad 2's buttons */
#define JOY2H        0x421B
#define JOY3L        0x421C
#define JOY3H        0x421D
#define JOY4L        0x421E
#define JOY4H        0x421F

/* ── DMA channels: $43n0 + the register, n = the channel (dma.c) ─────────────────────── */
#define DMAP0        0x4300 /* DMA channel 0: mode (+ $10 per channel) */
#define BBAD0        0x4301 /* its destination register ($21xx) */
#define A1T0L        0x4302 /* its source address */
#define A1B0         0x4304 /* its source bank */
#define DAS0L        0x4305 /* its size (16b) */
#define DMAP4        0x4340 /* DMA channel 4: mode */
#define BBAD4        0x4341 /* its destination register ($21xx) */
#define A1T4L        0x4342 /* its source address */
#define A1T4H        0x4343
#define A1B4         0x4344 /* its source bank */
#define DAS4L        0x4345 /* its size (16b) */

/* ── The same registers as a DMA channel's destination (BBADn holds $21xx's low byte) ── */
#define BBAD_OAM     0x04   /* $2104: a copy to OAM */
#define BBAD_VRAM    0x18   /* $2118, the VRAM data register (as in dma.c) */
#define BBAD_CGDATA  0x22   /* $2122: a copy to CGRAM */
