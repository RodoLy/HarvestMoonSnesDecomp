/*
 * hw.c — the console around the decompiled game, for the native build.
 *
 * The game's own code is the C in src/: it runs directly, there is no
 * 65816 anywhere. What the C talks to is the rest of the console, which
 * the game's data needs as it is (the pictures are in the picture chip's
 * formats, the music is a program for the sound chip's processor):
 *   - the picture chip (PPU) and the DMA unit, drawn a line at a time;
 *   - the sound chip (APU: its SPC700 processor runs the game's music
 *     program, its DSP makes the samples);
 *   - the CPU's own registers ($42xx: NMI, the multiplier and divider, the
 *     pads) and the WRAM port ($2180).
 * These come from the recompilation's runner (snesrecomp/runner/src/snes),
 * compiled as they are; this file is the glue: the memory map, the
 * registers, and the functions the decompilation calls for the chips
 * (hm_io_read_live / hm_io_write_live / hm_apu_read / hm_apu_write).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "snes.h"
#include "ppu.h"
#include "dma.h"
#include "apu.h"
#include "dsp.h"

#include "hm_decomp.h"
#include "native.h"

/* ── What the decompilation expects (hm_decomp.h) ─────────────────────── */
uint8_t g_ram[0x20000];
const uint8_t *g_rom;
uint8_t *g_hm_dma_regs;               /* NULL: the DMA registers are the chip's */
uint8_t *g_hm_sram;
uint32_t g_hm_sram_size;
int g_hm_io_live = 1;                 /* the chips' registers are really written */
HmApuReplay *g_hm_apu;

/* ── What the runner's chips expect ───────────────────────────────────── */
Snes *g_snes;
Ppu *g_ppu;
int snes_frame_counter;
unsigned char g_snesrecomp_last_hdmaen;
uint64_t g_apu_timer0_total_ticks;
void RtlApuLock(void) {}
void RtlApuUnlock(void) {}

static uint32_t s_rom_size;
static uint16_t s_pad[2];             /* the pads as the auto-read latched them */
static bool s_nmi_flag;               /* RDNMI bit 7 */
static uint64_t s_apu_cycles;         /* run so far */

/* ── The memory map (LoROM) ───────────────────────────────────────────── */
static uint8_t hm_cart_read(uint8_t bank, uint16_t a)
{
  uint8_t b = bank & 0x7F;
  if (a >= 0x8000 && b < 0x7E)
    return g_rom[(((uint32_t)(b & 0x3F) << 15) | (a & 0x7FFF)) % s_rom_size];
  if (b >= 0x70 && b < 0x7E && a < 0x8000 && g_hm_sram)
    return g_hm_sram[(((uint32_t)(b & 0xF) << 15) | a) & (g_hm_sram_size - 1)];
  return 0;
}
static void hm_cart_write(uint8_t bank, uint16_t a, uint8_t v)
{
  uint8_t b = bank & 0x7F;
  if (b >= 0x70 && b < 0x7E && a < 0x8000 && g_hm_sram)
    g_hm_sram[(((uint32_t)(b & 0xF) << 15) | a) & (g_hm_sram_size - 1)] = v;
}

/* ── The sound chip ───────────────────────────────────────────────────── */
static void apu_run(uint32_t cycles)
{
  Apu *apu = g_snes->apu;
  for (uint32_t i = 0; i < cycles; i++) apu_cycle(apu);
  s_apu_cycles += cycles;
}
uint64_t native_apu_cycles(void) { return s_apu_cycles; }
void native_apu_run_to(uint64_t target)
{
  if (target > s_apu_cycles) apu_run((uint32_t)(target - s_apu_cycles));
}
uint32_t native_audio_samples(int16_t *out, int frames)
{
  return dsp_getSamples(g_snes->apu->dsp, out, frames);
}
/* A loop waiting for the chip's answer: the chip runs meanwhile, about
 * 4 of its cycles a turn of such a loop. When the waiting adds up to a
 * frame, the frame ends there as on the console (native_lag_frame: the
 * picture, the pads, and the NMI interrupting the wait), so the screen,
 * the sound and the NMI's own work (the pads, the random numbers) keep
 * their pace through long waits. */
void (*g_native_lag_frame)(void);
static uint32_t s_wait;
enum { SPC_PER_FRAME = 17039, SPC_PER_POLL = 4 };
uint8_t hm_apu_read(uint16_t reg)
{
  apu_run(SPC_PER_POLL);
  s_wait += SPC_PER_POLL;
  if (s_wait >= SPC_PER_FRAME && g_native_lag_frame) {
    s_wait = 0;
    g_native_lag_frame();
  }
  return g_snes->apu->outPorts[reg & 3];
}
bool native_nmi_enabled(void) { return g_snes->nmiEnabled; }
void hm_apu_write(uint16_t reg, uint8_t v)
{
  apu_writePortNow(g_snes->apu, (uint8_t)(reg & 3), v);
}

/* ── The CPU's registers ──────────────────────────────────────────────── */
static uint8_t cpu_reg_read(uint16_t reg)
{
  Snes *s = g_snes;
  switch (reg) {
  case 0x4210: { uint8_t v = (uint8_t)(0x02 | (s_nmi_flag ? 0x80 : 0)); s_nmi_flag = false; return v; }
  case 0x4211: return 0;
  case 0x4212: return 0x80;            /* in the vertical blank, the pads read */
  case 0x4213: return 0;
  case 0x4214: return (uint8_t)s->divideResult;
  case 0x4215: return (uint8_t)(s->divideResult >> 8);
  case 0x4216: return (uint8_t)s->multiplyResult;
  case 0x4217: return (uint8_t)(s->multiplyResult >> 8);
  case 0x4218: return (uint8_t)s_pad[0];
  case 0x4219: return (uint8_t)(s_pad[0] >> 8);
  case 0x421A: return (uint8_t)s_pad[1];
  case 0x421B: return (uint8_t)(s_pad[1] >> 8);
  default: return 0;
  }
}
static void cpu_reg_write(uint16_t reg, uint8_t v)
{
  Snes *s = g_snes;
  switch (reg) {
  case 0x4200:
    s->nmiEnabled = v & 0x80; s->vIrqEnabled = v & 0x20; s->hIrqEnabled = v & 0x10;
    s->autoJoyRead = v & 1;
    break;
  case 0x4202: s->multiplyA = v; break;
  case 0x4203: s->multiplyResult = (uint16_t)(s->multiplyA * v); break;
  case 0x4204: s->divideA = (uint16_t)((s->divideA & 0xFF00) | v); break;
  case 0x4205: s->divideA = (uint16_t)((s->divideA & 0x00FF) | v << 8); break;
  case 0x4206:
    if (v == 0) { s->divideResult = 0xFFFF; s->multiplyResult = s->divideA; }
    else { s->divideResult = (uint16_t)(s->divideA / v); s->multiplyResult = (uint16_t)(s->divideA % v); }
    break;
  case 0x4207: s->hTimer = (uint16_t)((s->hTimer & 0x100) | v); break;
  case 0x4208: s->hTimer = (uint16_t)((s->hTimer & 0x0FF) | (v & 1) << 8); break;
  case 0x4209: s->vTimer = (uint16_t)((s->vTimer & 0x100) | v); break;
  case 0x420A: s->vTimer = (uint16_t)((s->vTimer & 0x0FF) | (v & 1) << 8); break;
  case 0x420B:                         /* a DMA: done at once */
    dma_startDma(s->dma, v, false);
    while (dma_cycle(s->dma)) {}
    break;
  case 0x420C:
    g_snesrecomp_last_hdmaen = v;
    dma_startDma(s->dma, v, true);
    break;
  default: break;
  }
}

/* ── The buses the DMA unit uses (the runner's snes.h) ────────────────── */
uint8_t snes_readBBus(Snes *s, uint8_t adr)
{
  if (adr < 0x40) return ppu_read(g_ppu, adr);
  if (adr < 0x80) return s->apu->outPorts[adr & 3];
  if (adr == 0x80) { uint8_t v = s->ram[s->ramAdr++]; s->ramAdr &= 0x1FFFF; return v; }
  return 0;
}
void snes_writeBBus(Snes *s, uint8_t adr, uint8_t val)
{
  if (adr < 0x40) { ppu_write(g_ppu, adr, val); return; }
  if (adr < 0x80) { apu_writePortNow(s->apu, adr & 3, val); return; }
  switch (adr) {
  case 0x80: s->ram[s->ramAdr] = val; s->ramAdr = (s->ramAdr + 1) & 0x1FFFF; break;
  case 0x81: s->ramAdr = (s->ramAdr & 0x1FF00) | val; break;
  case 0x82: s->ramAdr = (s->ramAdr & 0x100FF) | (uint32_t)val << 8; break;
  case 0x83: s->ramAdr = (s->ramAdr & 0x0FFFF) | (uint32_t)(val & 1) << 16; break;
  }
}
uint8_t snes_read(Snes *s, uint32_t adr)
{
  uint8_t bank = (uint8_t)(adr >> 16);
  uint16_t a = (uint16_t)adr;
  if (bank == 0x7E || bank == 0x7F) return s->ram[((uint32_t)(bank & 1) << 16) | a];
  if ((bank & 0x7F) < 0x40) {
    if (a < 0x2000) return s->ram[a];
    if (a >= 0x2100 && a < 0x2200) return snes_readBBus(s, (uint8_t)a);
    if (a >= 0x4200 && a < 0x4220) return cpu_reg_read(a);
    if (a >= 0x4300 && a < 0x4380) return dma_read(s->dma, a);
  }
  return hm_cart_read(bank, a);
}
void snes_write(Snes *s, uint32_t adr, uint8_t val)
{
  uint8_t bank = (uint8_t)(adr >> 16);
  uint16_t a = (uint16_t)adr;
  if (bank == 0x7E || bank == 0x7F) { s->ram[((uint32_t)(bank & 1) << 16) | a] = val; return; }
  if ((bank & 0x7F) < 0x40) {
    if (a < 0x2000) { s->ram[a] = val; return; }
    if (a >= 0x2100 && a < 0x2200) { snes_writeBBus(s, (uint8_t)a, val); return; }
    if (a >= 0x4200 && a < 0x4220) { cpu_reg_write(a, val); return; }
    if (a >= 0x4300 && a < 0x4380) { dma_write(s->dma, a, val); return; }
  }
  hm_cart_write(bank, a, val);
}

/* ── The decompilation's chip calls (hm_decomp.h) ─────────────────────── */
uint8_t hm_io_read_live(uint16_t reg)
{
  if (reg >= 0x2100 && reg < 0x2200) return snes_readBBus(g_snes, (uint8_t)reg);
  if (reg >= 0x4200 && reg < 0x4220) return cpu_reg_read(reg);
  if (reg >= 0x4300 && reg < 0x4380) return dma_read(g_snes->dma, reg);
  return 0;
}
void hm_io_write_live(uint16_t reg, uint8_t v)
{
  if (reg >= 0x2100 && reg < 0x2200) snes_writeBBus(g_snes, (uint8_t)reg, v);
  else if (reg >= 0x4200 && reg < 0x4220) cpu_reg_write(reg, v);
  else if (reg >= 0x4300 && reg < 0x4380) dma_write(g_snes->dma, reg, v);
}
uint8_t hm_io_read(uint16_t reg) { return cpu_reg_read(reg); }

/* ── Start, frames ────────────────────────────────────────────────────── */
bool native_hw_init(const uint8_t *rom, uint32_t rom_size, uint8_t *sram, uint32_t sram_size)
{
  g_rom = rom; s_rom_size = rom_size;
  g_hm_sram = sram; g_hm_sram_size = sram_size;
  g_snes = calloc(1, sizeof(Snes));
  if (!g_snes) return false;
  g_snes->ram = g_ram;
  g_snes->ppu = g_ppu = ppu_init();
  g_snes->apu = apu_init();
  g_snes->dma = dma_init(g_snes);
  if (!g_ppu || !g_snes->apu || !g_snes->dma) return false;
  ppu_reset(g_ppu);
  apu_reset(g_snes->apu);
  dma_reset(g_snes->dma);
  return true;
}

/* kPpuRenderFlags_*: 1 the fast renderer, 8 no limit of sprites per line */
uint32_t g_native_ppu_flags = 1 | 8;
void native_set_pads(uint16_t pad1, uint16_t pad2) { s_pad[0] = pad1; s_pad[1] = pad2; }

/* The field: from the PPU's state as the frame's code left it, HDMA
 * started from $420C, lines 1-224 (the visible ones) into `pixels`
 * (256 x 224, 32-bit BGRX). Then the vertical blank's own work. */
void native_draw_frame(uint8_t *pixels)
{
  PpuBeginDrawing(g_ppu, pixels, 256 * 4, g_native_ppu_flags);
  PpuSetExtraSpace(g_ppu, 0);
  Dma *dma = g_snes->dma;
  dma_startDma(dma, g_snesrecomp_last_hdmaen, true);
  dma_initHdma(dma);
  dma_primeHdmaFirstLine(dma);
  for (int line = 1; line <= 224; line++) {
    g_snes->vPos = (uint16_t)line;
    ppu_runLine(g_ppu, line);
    dma_doHdma(dma);
  }
  g_snes->vPos = 225;
  ppu_checkOverscan(g_ppu);
  ppu_handleVblank(g_ppu);
  s_nmi_flag = true;
  s_wait = 0;
  snes_frame_counter++;
}
