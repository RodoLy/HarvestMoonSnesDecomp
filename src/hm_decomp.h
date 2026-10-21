/*
 * hm_decomp.h — the hand-written decompilation of Harvest Moon.
 *
 * The recompiler already turns every routine into C, but that C is a literal
 * translation of 65816 instructions. The decompilation rewrites routines as
 * C a person can read, named after what they do, using the names in
 * hm_ram.h (from the recompilation project). Each routine lives in
 * src/<system>.c.
 *
 * How we know a rewrite is right (differential checking): every decompiled
 * routine has a generated wrapper (tools/decomp_overrides.py) around the
 * recompiled original. Run the game with HM_DECOMP_CHECK=1 and, on every
 * call, the wrapper snapshots RAM and registers, lets the original run, then
 * runs the rewrite on the snapshot and compares the results byte for byte.
 * Any difference is logged with the inputs. Without HM_DECOMP_CHECK the
 * original runs alone, so the game is unchanged.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The CPU state a routine sees and returns. Rewrites read the inputs they
 * use and write the outputs they produce; `out_mask` says which outputs the
 * checker compares. */
typedef struct HmRegs {
  uint16_t a, x, y;
  uint8_t c;          /* carry flag */
  uint8_t n;          /* negative flag (some routines branch on the caller's) */
  uint8_t db;         /* data bank */
  uint16_t d;         /* direct page */
  uint16_t s;         /* stack pointer (bank $00) */
  uint8_t pb;         /* program bank: $80-$87, or $00-$07 when the game runs
                       * the same code from the slow mirror */
} HmRegs;

/* The stack lives in WRAM, so what a routine pushes stays there after it
 * returns, and the checker compares it too. PHA/PHY/JSL write downwards. */
static inline void hm_push8(uint8_t *ram, HmRegs *r, uint8_t v)
{
  ram[r->s & 0x1FFF] = v;
  r->s = (uint16_t)(r->s - 1);
}
static inline uint8_t hm_pull8(uint8_t *ram, HmRegs *r)
{
  r->s = (uint16_t)(r->s + 1);
  return ram[r->s & 0x1FFF];
}
/* JSL to a routine in `bank`: pushes the current bank (PB) and the address
 * of the JSL's own last byte, then runs in the new bank. RTL pops the 3
 * bytes and goes back to the caller's bank. */
static inline void hm_jsl(uint8_t *ram, HmRegs *r, uint16_t return_pc, uint8_t bank)
{
  hm_push8(ram, r, r->pb);
  hm_push8(ram, r, (uint8_t)(return_pc >> 8));
  hm_push8(ram, r, (uint8_t)return_pc);
  r->pb = bank;
}
static inline void hm_rtl(uint8_t *ram, HmRegs *r)
{
  hm_pull8(ram, r);
  hm_pull8(ram, r);
  r->pb = hm_pull8(ram, r);
}

/* Direct-page variables ($00-$FF relative to D, in bank $00 = low WRAM). */
static inline uint8_t *hm_dp(uint8_t *ram, const HmRegs *r, unsigned off)
{
  return &ram[(uint16_t)(r->d + off) & 0x1FFF];
}
static inline void hm_dp_w16(uint8_t *ram, const HmRegs *r, unsigned off, uint16_t v)
{
  *hm_dp(ram, r, off) = (uint8_t)v;
  *hm_dp(ram, r, off + 1) = (uint8_t)(v >> 8);
}
static inline uint16_t hm_dp_r16(uint8_t *ram, const HmRegs *r, unsigned off)
{
  return (uint16_t)(*hm_dp(ram, r, off) | *hm_dp(ram, r, off + 1) << 8);
}

/* ── The whole address space, for long pointers ([$72],Y and friends) ──────
 * The ROM never changes, so rewrites may read it directly: g_rom is the
 * cartridge image the runtime loaded (LoROM: bank & $3F selects a 32 KB
 * chunk, addresses $8000-$FFFF). WRAM is `ram`: banks $7E/$7F, and the
 * first 8 KB mirrored in banks $00-$3F / $80-$BF. Hardware registers are
 * not modelled (reads give 0, writes are dropped), except the DMA
 * registers $4300-$437F (g_hm_dma_regs below). */
extern const uint8_t *g_rom;

/* The DMA registers $4300-$437F, while the checker runs a rewrite: a copy
 * of them taken before the call (the checker compares it with the real
 * ones afterwards). NULL otherwise. */
extern uint8_t *g_hm_dma_regs;

static inline uint8_t *hm_bus_ram(uint8_t *ram, uint32_t addr)
{
  uint8_t bank = (uint8_t)(addr >> 16);
  uint16_t a = (uint16_t)addr;
  if (bank == 0x7E) return &ram[a];
  if (bank == 0x7F) return &ram[0x10000 + a];
  if ((bank & 0x7F) < 0x40 && a < 0x2000) return &ram[a];
  if ((bank & 0x7F) < 0x40 && a >= 0x4300 && a < 0x4380 && g_hm_dma_regs)
    return &g_hm_dma_regs[a - 0x4300];
  return NULL;
}
static inline uint8_t hm_bus_rd8(uint8_t *ram, uint32_t addr)
{
  uint8_t *p = hm_bus_ram(ram, addr);
  if (p) return *p;
  uint8_t bank = (uint8_t)(addr >> 16);
  uint16_t a = (uint16_t)addr;
  if (a >= 0x8000 && (bank & 0x7F) < 0x7E)
    return g_rom[(uint32_t)(bank & 0x3F) << 15 | (a & 0x7FFF)];
  return 0;
}
static inline uint16_t hm_bus_rd16(uint8_t *ram, uint32_t addr)
{
  return (uint16_t)(hm_bus_rd8(ram, addr) | hm_bus_rd8(ram, (addr + 1) & 0xFFFFFF) << 8);
}
static inline void hm_bus_wr8(uint8_t *ram, uint32_t addr, uint8_t v)
{
  uint8_t *p = hm_bus_ram(ram, addr);
  if (p) *p = v;
}
static inline void hm_bus_wr16(uint8_t *ram, uint32_t addr, uint16_t v)
{
  hm_bus_wr8(ram, addr, (uint8_t)v);
  hm_bus_wr8(ram, (addr + 1) & 0xFFFFFF, (uint8_t)(v >> 8));
}
/* A 24-bit pointer stored in the direct page ($72-$74, $CC-$CE...), plus Y:
 * the address `[dp],Y` uses. */
static inline uint32_t hm_dp_long(uint8_t *ram, const HmRegs *r, unsigned off, uint16_t y)
{
  uint32_t p = hm_dp_r16(ram, r, off) | (uint32_t)*hm_dp(ram, r, off + 2) << 16;
  return (p + y) & 0xFFFFFF;
}

enum { HM_OUT_A = 1, HM_OUT_X = 2, HM_OUT_Y = 4, HM_OUT_C = 8,
       /* The recompiler has no compiled body for this routine (it runs in
        * its interpreter), so there is no wrapper to check live calls: the
        * fuzzer checks it by calling the original directly. */
       HM_INTERP = 16,
       /* Called with JSR / returns with RTS (2-byte frame), not JSL / RTL. */
       HM_JSR = 32 };

/* Fuzzing: which inputs to randomize when testing a routine on its own
 * (HM_DECOMP_FUZZ). Ranges are inclusive; size 1 or 2 bytes. */
typedef struct HmFuzzVar { uint32_t addr; uint8_t size; uint16_t lo, hi; } HmFuzzVar;
enum { HM_FUZZ_MAX_VARS = 6 };

typedef struct HmDecompFn {
  const char *name;                       /* HM-Decomp label */
  void (*fn)(uint8_t *ram, HmRegs *r);    /* the rewrite: ram is WRAM (128 KB) */
  unsigned out_mask;                      /* outputs to compare */
  const char *source;                     /* where it is written */
  /* Fuzz spec: entry point, entry widths, A's range and RAM inputs. */
  uint32_t pc24;
  uint8_t entry_m, entry_x;
  uint16_t a_lo, a_hi;
  HmFuzzVar vars[HM_FUZZ_MAX_VARS];
  /* Optional: at most this many fuzz calls (0 = all) for slow routines, and
   * a function that prepares more inputs (e.g. a random farm). */
  uint16_t fuzz_max;
  void (*prep)(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void));
} HmDecompFn;

/* Registry, defined in hm_decomp.c. */
extern const HmDecompFn g_hm_decomp_fns[];
extern const int g_hm_decomp_count;

/* Check mode (HM_DECOMP_CHECK=1); read by the generated wrappers. */
extern int g_hm_decomp_check;

struct CpuState;
/* HM_DECOMP_FUZZ=<frame>:<iterations>: at that frame boundary, call every
 * registered routine <iterations> times with random inputs (checking each
 * call), then restore the machine exactly as it was. Driven by a mod-runtime
 * frame callback registered in hm_decomp.c. */
void *hm_decomp_check_begin(const struct CpuState *cpu);
void hm_decomp_check_end(const char *name, void *snapshot, const struct CpuState *cpu,
                         int completed);

/* ── The decompiled routines (one line each; see the .c files) ───────── */
void hm_GetRNG(uint8_t *ram, HmRegs *r);              /* rng.c */
void hm_AddWood(uint8_t *ram, HmRegs *r);             /* stock.c */
void hm_AddGrass(uint8_t *ram, HmRegs *r);
void hm_AddPlayerHappiness(uint8_t *ram, HmRegs *r);
void hm_ChangeStamina(uint8_t *ram, HmRegs *r);       /* stamina.c */
void hm_GetsCowPointer(uint8_t *ram, HmRegs *r);      /* animals.c */
void hm_GetChickenPointer(uint8_t *ram, HmRegs *r);
void hm_DivisionUnsigned(uint8_t *ram, HmRegs *r);    /* math.c */
void hm_RNGReturn0toA(uint8_t *ram, HmRegs *r);       /* rng.c */
void hm_SetWeatherFlags(uint8_t *ram, HmRegs *r);     /* weather.c */
void hm_WeatherTomorrow(uint8_t *ram, HmRegs *r);
void hm_GetTileIndex(uint8_t *ram, HmRegs *r);        /* farm.c */
void hm_LoadsDamageProbabilities(uint8_t *ram, HmRegs *r);
void hm_ClimateFarmDamageCheck(uint8_t *ram, HmRegs *r);
void hm_ClimateFarmDamagePrep(uint8_t *ram, HmRegs *r);
void hm_MonthlyFarmTilesCheck(uint8_t *ram, HmRegs *r);
void hm_NightlyFarmTilesCheck(uint8_t *ram, HmRegs *r);
void hm_CalculateFarmDevelopment(uint8_t *ram, HmRegs *r);
void hm_Sub_82A9A0(uint8_t *ram, HmRegs *r);
void hm_AddCowHappiness(uint8_t *ram, HmRegs *r);     /* animals.c */
void hm_CowFeedingandStatus(uint8_t *ram, HmRegs *r);
void hm_FindMostLovedName(uint8_t *ram, HmRegs *r);   /* people.c */
void hm_WifePregnanacyandChilds(uint8_t *ram, HmRegs *r);
void hm_LoadsDateNames(uint8_t *ram, HmRegs *r);      /* calendar.c */
void hm_AddMoney(uint8_t *ram, HmRegs *r);            /* stock.c */
void hm_SUB_8281C0(uint8_t *ram, HmRegs *r);          /* weather.c */
void hm_HaveLunch(uint8_t *ram, HmRegs *r);           /* stamina.c */
void hm_GetTileContent(uint8_t *ram, HmRegs *r);      /* tools.c */
void hm_UNK_CheckTileProperty(uint8_t *ram, HmRegs *r);
void hm_CheckToolSuccess(uint8_t *ram, HmRegs *r);
void hm_CalculateTileinFront(uint8_t *ram, HmRegs *r);
void hm_TileAtOffsetFromPlayer(uint8_t *ram, HmRegs *r);
void hm_PreCheckToolSuccess(uint8_t *ram, HmRegs *r);
void hm_PreCheckToolSuccessLine(uint8_t *ram, HmRegs *r);
void hm_PreCheckToolSuccessSquare(uint8_t *ram, HmRegs *r);
void hm_SUB_81A801(uint8_t *ram, HmRegs *r);          /* mapedit.c */
void hm_SUB_81A83A(uint8_t *ram, HmRegs *r);
void hm_SUB_82B060(uint8_t *ram, HmRegs *r);
void hm_AddProgrammedDMA(uint8_t *ram, HmRegs *r);    /* dma.c */
void hm_SetCCPoiner(uint8_t *ram, HmRegs *r);         /* events.c */
void hm_ResetsCurrentCCStructureLong(uint8_t *ram, HmRegs *r);
void hm_SUB_8480F8(uint8_t *ram, HmRegs *r);
void hm_ResetsCurrentCCStructureShort(uint8_t *ram, HmRegs *r);
void hm_UNK_LoadCCDataLong(uint8_t *ram, HmRegs *r);
void hm_UNK_LoadCCDataShort(uint8_t *ram, HmRegs *r);
