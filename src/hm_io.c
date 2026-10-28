/*
 * hm_io.c — checking the routines that talk to the console's own chips:
 * the picture unit (PPU, $2100-$213F), the DMA unit ($4300-$437F, $420B,
 * $420C), the WRAM port ($2180-$2183) and the CPU's registers ($4200-$421F:
 * NMITIMEN, the timers, the multiplier and divider).
 *
 * The sound chip is checked by replaying what it answered (hm_decomp.c);
 * these chips keep state instead, so a routine flagged HM_IO is checked by
 * comparing that state:
 *   1. before the original runs: hm_io_save() (registers, the VRAM, CGRAM
 *      and OAM pointers and latches, the three memories, the DMA channels,
 *      HDMAEN, the CPU's registers and the RDNMI latch);
 *   2. the original runs as always, changing the real machine;
 *   3. its result is saved, the state before is put back, and the rewrite
 *      runs on the real machine: its writes go to the emulated chips
 *      (hm_io_write_live), so a DMA it starts really copies;
 *   4. what it left is compared with what the original left, and the
 *      original's result is put back for the game to go on.
 * The writes here do not move the emulated beam (WriteReg brings it up to
 * the CPU's clock first): the beam stays where the original left it, so
 * nothing is drawn, no NMI is raised and both runs see the same beam.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu_state.h"
#include "snes/snes.h"
#include "snes/ppu.h"
#include "snes/dma.h"
#include "hm_decomp.h"

extern Snes *g_snes;
extern Ppu *g_ppu;
extern Dma *g_dma;
extern uint8_t g_snesrecomp_last_hdmaen;

int g_hm_io_live;

typedef struct HmPpuAccess {
  uint16_t vramPointer, vramReadBuffer;
  bool vramIncrementOnHigh;
  uint8_t vramRemapMode, vramIncrement;
  uint8_t cgramPointer, cgramBuffer;
  bool cgramSecondWrite;
  uint8_t oamAdr, oamBuffer;
  bool oamInHigh, oamSecondWrite;
  uint8_t scrollPrev, scrollPrev2, m7prev;
} HmPpuAccess;

typedef struct HmCpuRegs {
  bool nmiEnabled, hIrqEnabled, vIrqEnabled, inIrq, rdnmiPending, autoJoyRead, ppuLatch;
  uint16_t hTimer, vTimer;
  uint8_t multiplyA;
  uint16_t multiplyResult, divideA, divideResult;
  uint32_t ramAdr;
} HmCpuRegs;

struct HmIoState {
  uint8_t regs[PPU_SAVESTATE_REGS_SIZE];          /* inidisp .. cgwsel */
  HmPpuAccess access;
  uint8_t mem[PPU_SAVESTATE_MEM_SIZE];            /* cgram, oam, highOam, vram */
  DmaChannel channel[8];
  uint8_t hdmaen;
  HmCpuRegs cpu;
};

int hm_io_available(void) { return g_snes && g_ppu && g_dma && g_snes->ram == g_cpu.ram; }

HmIoState *hm_io_save(void)
{
  if (!hm_io_available()) return NULL;
  HmIoState *s = calloc(1, sizeof *s);
  if (!s) return NULL;
  memcpy(s->regs, &g_ppu->inidisp, sizeof s->regs);
  memcpy(s->mem, &g_ppu->cgram, sizeof s->mem);
  HmPpuAccess *a = &s->access;
  a->vramPointer = g_ppu->vramPointer;       a->vramReadBuffer = g_ppu->vramReadBuffer;
  a->vramIncrementOnHigh = g_ppu->vramIncrementOnHigh;
  a->vramRemapMode = g_ppu->vramRemapMode;   a->vramIncrement = g_ppu->vramIncrement;
  a->cgramPointer = g_ppu->cgramPointer;     a->cgramBuffer = g_ppu->cgramBuffer;
  a->cgramSecondWrite = g_ppu->cgramSecondWrite;
  a->oamAdr = g_ppu->oamAdr;                 a->oamBuffer = g_ppu->oamBuffer;
  a->oamInHigh = g_ppu->oamInHigh;           a->oamSecondWrite = g_ppu->oamSecondWrite;
  a->scrollPrev = g_ppu->scrollPrev;         a->scrollPrev2 = g_ppu->scrollPrev2;
  a->m7prev = g_ppu->m7prev;
  memcpy(s->channel, g_dma->channel, sizeof s->channel);
  s->hdmaen = g_snesrecomp_last_hdmaen;
  HmCpuRegs *c = &s->cpu;
  c->nmiEnabled = g_snes->nmiEnabled;   c->hIrqEnabled = g_snes->hIrqEnabled;
  c->vIrqEnabled = g_snes->vIrqEnabled; c->inIrq = g_snes->inIrq;
  c->rdnmiPending = g_snes->rdnmiPending;
  c->autoJoyRead = g_snes->autoJoyRead; c->ppuLatch = g_snes->ppuLatch;
  c->hTimer = g_snes->hTimer;           c->vTimer = g_snes->vTimer;
  c->multiplyA = g_snes->multiplyA;     c->multiplyResult = g_snes->multiplyResult;
  c->divideA = g_snes->divideA;         c->divideResult = g_snes->divideResult;
  c->ramAdr = g_snes->ramAdr;
  return s;
}

void hm_io_load(const HmIoState *s)
{
  if (!s || !hm_io_available()) return;
  memcpy(&g_ppu->inidisp, s->regs, sizeof s->regs);
  memcpy(&g_ppu->cgram, s->mem, sizeof s->mem);
  const HmPpuAccess *a = &s->access;
  g_ppu->vramPointer = a->vramPointer;       g_ppu->vramReadBuffer = a->vramReadBuffer;
  g_ppu->vramIncrementOnHigh = a->vramIncrementOnHigh;
  g_ppu->vramRemapMode = a->vramRemapMode;   g_ppu->vramIncrement = a->vramIncrement;
  g_ppu->cgramPointer = a->cgramPointer;     g_ppu->cgramBuffer = a->cgramBuffer;
  g_ppu->cgramSecondWrite = a->cgramSecondWrite;
  g_ppu->oamAdr = a->oamAdr;                 g_ppu->oamBuffer = a->oamBuffer;
  g_ppu->oamInHigh = a->oamInHigh;           g_ppu->oamSecondWrite = a->oamSecondWrite;
  g_ppu->scrollPrev = a->scrollPrev;         g_ppu->scrollPrev2 = a->scrollPrev2;
  g_ppu->m7prev = a->m7prev;
  memcpy(g_dma->channel, s->channel, sizeof s->channel);
  g_snesrecomp_last_hdmaen = s->hdmaen;
  const HmCpuRegs *c = &s->cpu;
  g_snes->nmiEnabled = c->nmiEnabled;   g_snes->hIrqEnabled = c->hIrqEnabled;
  g_snes->vIrqEnabled = c->vIrqEnabled; g_snes->inIrq = c->inIrq;
  g_snes->rdnmiPending = c->rdnmiPending;
  g_snes->autoJoyRead = c->autoJoyRead; g_snes->ppuLatch = c->ppuLatch;
  g_snes->hTimer = c->hTimer;           g_snes->vTimer = c->vTimer;
  g_snes->multiplyA = c->multiplyA;     g_snes->multiplyResult = c->multiplyResult;
  g_snes->divideA = c->divideA;         g_snes->divideResult = c->divideResult;
  g_snes->ramAdr = c->ramAdr;
}

void hm_io_free(HmIoState *s) { free(s); }

/* The first difference, as "what: rewrite value, rom value". 0 = same. */
int hm_io_diff(const HmIoState *rw, const HmIoState *rom, char *why, size_t n)
{
  static const char *const mem_name[] = { "CGRAM", "OAM", "high OAM", "VRAM" };
  static const unsigned mem_at[] = { 0, 0x200, 0x400, 0x420, 0x10420 };
  for (unsigned i = 0; i < sizeof rw->regs; i++)
    if (rw->regs[i] != rom->regs[i]) {
      snprintf(why, n, "PPU register byte +%02X = %02X, rom %02X", i, rw->regs[i], rom->regs[i]);
      return 1;
    }
  for (unsigned m = 0; m < 4; m++)
    for (unsigned i = mem_at[m]; i < mem_at[m + 1]; i++)
      if (rw->mem[i] != rom->mem[i]) {
        snprintf(why, n, "%s byte %04X = %02X, rom %02X", mem_name[m], i - mem_at[m], rw->mem[i], rom->mem[i]);
        return 1;
      }
#define FIELD(st, f, what) \
  if ((st)->f != (rom_##st)->f) { \
    snprintf(why, n, "%s %s = %X, rom %X", what, #f, (unsigned)(st)->f, (unsigned)(rom_##st)->f); return 1; }
  const HmPpuAccess *a = &rw->access, *rom_a = &rom->access;
  FIELD(a, vramPointer, "PPU") FIELD(a, vramReadBuffer, "PPU") FIELD(a, vramIncrementOnHigh, "PPU")
  FIELD(a, vramRemapMode, "PPU") FIELD(a, vramIncrement, "PPU") FIELD(a, cgramPointer, "PPU")
  FIELD(a, cgramBuffer, "PPU") FIELD(a, cgramSecondWrite, "PPU") FIELD(a, oamAdr, "PPU")
  FIELD(a, oamBuffer, "PPU") FIELD(a, oamInHigh, "PPU") FIELD(a, oamSecondWrite, "PPU")
  FIELD(a, scrollPrev, "PPU") FIELD(a, scrollPrev2, "PPU") FIELD(a, m7prev, "PPU")
  for (int ch = 0; ch < 8; ch++) {
    const DmaChannel *d = &rw->channel[ch], *rom_d = &rom->channel[ch];
    char what[16];
    snprintf(what, sizeof what, "DMA %d", ch);
    FIELD(d, bAdr, what) FIELD(d, aAdr, what) FIELD(d, aBank, what) FIELD(d, size, what)
    FIELD(d, indBank, what) FIELD(d, tableAdr, what) FIELD(d, repCount, what)
    FIELD(d, unusedByte, what) FIELD(d, dmaActive, what) FIELD(d, hdmaActive, what)
    FIELD(d, mode, what) FIELD(d, fixed, what) FIELD(d, decrement, what) FIELD(d, indirect, what)
    FIELD(d, fromB, what) FIELD(d, unusedBit, what) FIELD(d, doTransfer, what)
    FIELD(d, terminated, what) FIELD(d, offIndex, what)
  }
  if (rw->hdmaen != rom->hdmaen) {
    snprintf(why, n, "HDMAEN = %02X, rom %02X", rw->hdmaen, rom->hdmaen);
    return 1;
  }
  const HmCpuRegs *c = &rw->cpu, *rom_c = &rom->cpu;
  FIELD(c, nmiEnabled, "CPU") FIELD(c, hIrqEnabled, "CPU") FIELD(c, vIrqEnabled, "CPU")
  /* not inIrq / rdnmiPending: the beam sets them, and the original's run
   * moves the beam (WriteReg syncs it, a DMA charges its time) */
  FIELD(c, autoJoyRead, "CPU")
  FIELD(c, ppuLatch, "CPU") FIELD(c, hTimer, "CPU") FIELD(c, vTimer, "CPU") FIELD(c, multiplyA, "CPU")
  FIELD(c, multiplyResult, "CPU") FIELD(c, divideA, "CPU") FIELD(c, divideResult, "CPU")
  FIELD(c, ramAdr, "CPU")
#undef FIELD
  return 0;
}

/* WriteReg's routing, without its clock sync. */
void hm_io_write_live(uint16_t reg, uint8_t v)
{
  if (reg >= 0x2100 && reg < 0x2140) ppu_write(g_ppu, (uint8_t)reg, v);
  else if (reg >= 0x2180 && reg < 0x2184) snes_writeBBus(g_snes, (uint8_t)reg, v);
  else if (reg >= 0x4200 && reg < 0x4220) snes_writeReg(g_snes, reg, v);
  else if (reg >= 0x4300 && reg < 0x4380) dma_write(g_dma, reg, v);
}
uint8_t hm_io_read_live(uint16_t reg)
{
  if (reg >= 0x2100 && reg < 0x2140) return ppu_read(g_ppu, (uint8_t)reg);
  if (reg == 0x2180) return snes_readBBus(g_snes, (uint8_t)reg);
  if (reg >= 0x4200 && reg < 0x4220) return snes_readReg(g_snes, reg);
  if (reg >= 0x4300 && reg < 0x4380) return dma_read(g_dma, reg);
  return 0;
}

/* For the fuzzer: nothing that the beam would do on its own while the
 * original runs (HDMA writing registers on each line, raster IRQs). */
void hm_io_quiet(void)
{
  if (!hm_io_available()) return;
  snes_writeReg(g_snes, 0x420C, 0);
  g_snes->hIrqEnabled = g_snes->vIrqEnabled = false;
  g_snes->inIrq = false;
}

/* For the fuzzer: the beam and its clock anchor, so that every call of an
 * HM_IO routine starts at the same place of the frame (a CPU clock set
 * back behind the anchor makes the next sync jump the beam about). */
struct HmBeam {
  uint8_t tail[sizeof(Snes) - offsetof(Snes, hPos)];
  uint64_t beamMasterLast;
  bool rdnmiPending;
};
HmBeam *hm_beam_save(void)
{
  if (!hm_io_available()) return NULL;
  HmBeam *b = malloc(sizeof *b);
  if (!b) return NULL;
  memcpy(b->tail, &g_snes->hPos, sizeof b->tail);
  b->beamMasterLast = g_snes->beamMasterLast;
  b->rdnmiPending = g_snes->rdnmiPending;
  return b;
}
void hm_beam_load(const HmBeam *b)
{
  if (!b || !hm_io_available()) return;
  memcpy(&g_snes->hPos, b->tail, sizeof b->tail);
  g_snes->beamMasterLast = b->beamMasterLast;
  g_snes->rdnmiPending = b->rdnmiPending;
}
