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
  uint8_t p;          /* all the flags at the entry (for PHP: its byte goes on
                       * the stack); c and n above are the live ones */
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
 * registers $4300-$437F (g_hm_dma_regs below), the sound chip's ports
 * $2140-$217F (g_hm_apu below) and the cartridge's battery RAM, where the
 * game is saved (g_hm_sram below). */
extern const uint8_t *g_rom;

/* The DMA registers $4300-$437F, while the checker runs a rewrite: a copy
 * of them taken before the call (the checker compares it with the real
 * ones afterwards). NULL otherwise. */
extern uint8_t *g_hm_dma_regs;

/* The battery RAM (SRAM, the saved games): banks $70-$7D / $F0-$FD,
 * addresses $0000-$7FFF (LoROM), mirrored every g_hm_sram_size bytes.
 * While the checker runs a rewrite: a copy taken before the call, compared
 * with the real one afterwards. NULL otherwise. */
extern uint8_t *g_hm_sram;
extern uint32_t g_hm_sram_size;

/* The sound chip's ports $2140-$217F. Their answers come from another
 * processor and cannot be computed, so the checker records every port
 * read and write the original makes, in order; while the rewrite runs on
 * the snapshot, each read it makes gets the value the original read at
 * that point, and each write must be the one the original made. Any
 * difference in the order, the port or the value is a mismatch. NULL when
 * no rewrite is being checked. */
typedef struct HmApuEvent { uint16_t reg; uint8_t val, write; } HmApuEvent;
typedef struct HmApuReplay {
  const HmApuEvent *ev;
  int n, pos, bad;
  char why[96];
} HmApuReplay;
extern HmApuReplay *g_hm_apu;
uint8_t hm_apu_read(uint16_t reg);
/* The console's own registers that a rewrite reads: $4212 (the beam and the
 * automatic pad reading) and $4214-$421F (the results of the multiplier and
 * divider, the pads). Asked to the emulator; reading them changes nothing in
 * WRAM ($4212 only moves the emulated beam a little). */
uint8_t hm_io_read(uint16_t reg);
static inline int hm_is_io_reg(uint32_t addr)
{
  uint16_t a = (uint16_t)addr;
  return ((addr >> 16) & 0x7F) < 0x40 && (a == 0x4212 || (a >= 0x4214 && a < 0x4220));
}
void hm_apu_write(uint16_t reg, uint8_t v);
static inline int hm_is_apu_port(uint32_t addr)
{
  uint16_t a = (uint16_t)addr;
  return ((addr >> 16) & 0x7F) < 0x40 && a >= 0x2140 && a < 0x2180;
}

static inline uint8_t *hm_bus_ram(uint8_t *ram, uint32_t addr)
{
  uint8_t bank = (uint8_t)(addr >> 16);
  uint16_t a = (uint16_t)addr;
  if (bank == 0x7E) return &ram[a];
  if (bank == 0x7F) return &ram[0x10000 + a];
  if ((bank & 0x7F) < 0x40 && a < 0x2000) return &ram[a];
  if ((bank & 0x7F) < 0x40 && a >= 0x4300 && a < 0x4380 && g_hm_dma_regs)
    return &g_hm_dma_regs[a - 0x4300];
  if ((bank & 0x7F) >= 0x70 && (bank & 0x7F) < 0x7E && a < 0x8000 && g_hm_sram)
    return &g_hm_sram[(((uint32_t)(bank & 0xF) << 15) | a) & (g_hm_sram_size - 1)];
  return NULL;
}
/* HM_IO routines: while the checker runs the rewrite on the real machine
 * (hm_io.c), the chips' registers are really read and written. */
extern int g_hm_io_live;
uint8_t hm_io_read_live(uint16_t reg);
void hm_io_write_live(uint16_t reg, uint8_t v);
static inline int hm_is_chip_reg(uint32_t addr)
{
  uint16_t a = (uint16_t)addr;
  return ((addr >> 16) & 0x7F) < 0x40 &&
         ((a >= 0x2100 && a < 0x2140) || (a >= 0x2180 && a < 0x2184) || (a >= 0x4200 && a < 0x4380));
}
static inline uint8_t hm_bus_rd8(uint8_t *ram, uint32_t addr)
{
  uint8_t *p = hm_bus_ram(ram, addr);
  if (p) return *p;
  uint8_t bank = (uint8_t)(addr >> 16);
  uint16_t a = (uint16_t)addr;
  if (a >= 0x8000 && (bank & 0x7F) < 0x7E)
    return g_rom[(uint32_t)(bank & 0x3F) << 15 | (a & 0x7FFF)];
  if (hm_is_apu_port(addr)) return hm_apu_read(a);
  if (g_hm_io_live && hm_is_chip_reg(addr)) return hm_io_read_live(a);
  if (hm_is_io_reg(addr)) return hm_io_read(a);
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
  else if (hm_is_apu_port(addr)) hm_apu_write((uint16_t)addr, v);
  else if (g_hm_io_live && hm_is_chip_reg(addr)) hm_io_write_live((uint16_t)addr, v);
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
       HM_JSR = 32,
       /* Talks to the sound chip: while fuzzing, the CPU clock keeps going
        * from call to call (the chip only runs up to the CPU's time, so a
        * clock set back to 0 would leave it stopped and the routine
        * waiting for its answer forever). */
       HM_APU = 64,
       /* Fuzzed only when named in HM_DECOMP_FUZZ_ONLY, once, without
        * waiting for "ready", at frame 1 (before the game's own upload is
        * done): UNK_Audio1, which talks to the chip's boot ROM; after the
        * upload the chip runs the game's sound program and never answers
        * that way again. E.g. HM_DECOMP_FUZZ=100000:1
        * HM_DECOMP_FUZZ_ONLY=UNK_Audio1. */
       HM_LIVE = 128,
       /* Writes the console's chips (PPU, DMA, WRAM port, $42xx): checked
        * by running the rewrite on the real machine from the state before
        * and comparing the chips' state (hm_io.c). */
       HM_IO = 256 };

/* hm_io.c: the chips' state for HM_IO checks. */
typedef struct HmIoState HmIoState;
int hm_io_available(void);
HmIoState *hm_io_save(void);
void hm_io_load(const HmIoState *s);
void hm_io_free(HmIoState *s);
int hm_io_diff(const HmIoState *rewrite, const HmIoState *rom, char *why, size_t n);
void hm_io_quiet(void);
typedef struct HmBeam HmBeam;
HmBeam *hm_beam_save(void);
void hm_beam_load(const HmBeam *b);

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
void *hm_decomp_check_begin(const struct CpuState *cpu, const char *name);
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
void hm_EditTileonMap(uint8_t *ram, HmRegs *r);       /* mapedit.c */
void hm_EditTileonMapandsets0181(uint8_t *ram, HmRegs *r);
void hm_LoadsDamageProbabilities(uint8_t *ram, HmRegs *r);
void hm_ClimateFarmDamageCheck(uint8_t *ram, HmRegs *r);
void hm_ClimateFarmDamagePrep(uint8_t *ram, HmRegs *r);
void hm_MonthlyFarmTilesCheck(uint8_t *ram, HmRegs *r);
void hm_NightlyFarmTilesCheck(uint8_t *ram, HmRegs *r);
void hm_CalculateFarmDevelopment(uint8_t *ram, HmRegs *r);
void hm_CODE_83F4D8(uint8_t *ram, HmRegs *r);
void hm_MapEvent_CB69(uint8_t *ram, HmRegs *r);
void hm_MapEvent_EBA6(uint8_t *ram, HmRegs *r);
void hm_MapEvent_E76D(uint8_t *ram, HmRegs *r);
void hm_MapEvent_D2E0(uint8_t *ram, HmRegs *r);
void hm_MapEvent_DE9B(uint8_t *ram, HmRegs *r);
void hm_MapEvent_E407(uint8_t *ram, HmRegs *r);
void hm_MapEvent_E4CB(uint8_t *ram, HmRegs *r);
void hm_MapEvent_D844(uint8_t *ram, HmRegs *r);
void hm_MapEvent_D929(uint8_t *ram, HmRegs *r);
void hm_MapEvent_D955(uint8_t *ram, HmRegs *r);
void hm_MapEvent_DA1F(uint8_t *ram, HmRegs *r);
void hm_MapEvent_DA4B(uint8_t *ram, HmRegs *r);
void hm_MapEvent_DAED(uint8_t *ram, HmRegs *r);
void hm_MapEvent_DB8F(uint8_t *ram, HmRegs *r);
void hm_MapEvent_DC1E(uint8_t *ram, HmRegs *r);
void hm_MapEvent_DC4A(uint8_t *ram, HmRegs *r);
void hm_MapEvent_DD95(uint8_t *ram, HmRegs *r);
void hm_MapEvent_DE64(uint8_t *ram, HmRegs *r);
void hm_MapEvent_D4D7(uint8_t *ram, HmRegs *r);
void hm_MapEvent_D5E7(uint8_t *ram, HmRegs *r);
void hm_MapEvent_D613(uint8_t *ram, HmRegs *r);
void hm_MapEvent_D74E(uint8_t *ram, HmRegs *r);
void hm_MapEvent_D818(uint8_t *ram, HmRegs *r);
void hm_MapEvent_E586(uint8_t *ram, HmRegs *r);
void hm_MapEvent_E5A2(uint8_t *ram, HmRegs *r);
void hm_MapEvent_E5B4(uint8_t *ram, HmRegs *r);
void hm_MapEvent_E5CD(uint8_t *ram, HmRegs *r);
void hm_MapEvent_E5DF(uint8_t *ram, HmRegs *r);
void hm_MapEvent_E5F1(uint8_t *ram, HmRegs *r);
void hm_MapEvent_E603(uint8_t *ram, HmRegs *r);
void hm_MapEvent_E632(uint8_t *ram, HmRegs *r);
void hm_MapEvent_E661(uint8_t *ram, HmRegs *r);
void hm_MapEvent_E690(uint8_t *ram, HmRegs *r);
void hm_MapEvent_E6B2(uint8_t *ram, HmRegs *r);
void hm_MapEvent_E6C4(uint8_t *ram, HmRegs *r);
void hm_MapEvent_E74C(uint8_t *ram, HmRegs *r);

void hm_Sub_82A9A0(uint8_t *ram, HmRegs *r);
void hm_AddCowHappiness(uint8_t *ram, HmRegs *r);     /* animals.c */
void hm_CowFeedingandStatus(uint8_t *ram, HmRegs *r);
void hm_AddNewChicken(uint8_t *ram, HmRegs *r);
void hm_AddNewCow(uint8_t *ram, HmRegs *r);
void hm_PlaceAnimalsOnMap(uint8_t *ram, HmRegs *r);
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
void hm_QueueTileGraphicsDMA(uint8_t *ram, HmRegs *r);
void hm_ChangeMapTile(uint8_t *ram, HmRegs *r);
void hm_StartMapAnimation(uint8_t *ram, HmRegs *r);
void hm_RanchMasteryCalculator(uint8_t *ram, HmRegs *r); /* ending.c */
void hm_AddProgrammedDMA(uint8_t *ram, HmRegs *r);    /* dma.c */
void hm_RemoveProgrammedDMA(uint8_t *ram, HmRegs *r);
void hm_UNK_SetDMA2(uint8_t *ram, HmRegs *r);
void hm_UNK_SetDMA1(uint8_t *ram, HmRegs *r);
void hm_LoadSpriteComponent(uint8_t *ram, HmRegs *r); /* objects.c */
void hm_LoadGameObjectComponents(uint8_t *ram, HmRegs *r);
void hm_CreateGameObject(uint8_t *ram, HmRegs *r);
void hm_SpawnMapObject(uint8_t *ram, HmRegs *r);
void hm_SUB_8180B7(uint8_t *ram, HmRegs *r);   /* items.c */
void hm_PlayerState_00(uint8_t *ram, HmRegs *r);   /* playerstate.c */
void hm_ResetIdleAnimatioTimer(uint8_t *ram, HmRegs *r);
void hm_PlayerState_1A(uint8_t *ram, HmRegs *r);
void hm_PlayerState_17(uint8_t *ram, HmRegs *r);
void hm_PlayerState_18(uint8_t *ram, HmRegs *r);
void hm_PlayerState_16(uint8_t *ram, HmRegs *r);
void hm_PlayerState_15(uint8_t *ram, HmRegs *r);
void hm_PlayerState_14(uint8_t *ram, HmRegs *r);
void hm_PlayerState_0F(uint8_t *ram, HmRegs *r);
void hm_PlayerState_10(uint8_t *ram, HmRegs *r);
void hm_PlayerState_11(uint8_t *ram, HmRegs *r);
void hm_PlayerState_12(uint8_t *ram, HmRegs *r);
void hm_PlayerState_13(uint8_t *ram, HmRegs *r);
void hm_PlayerState_1B(uint8_t *ram, HmRegs *r);
void hm_PlayerState_0D(uint8_t *ram, HmRegs *r);
void hm_PlayerState_0C(uint8_t *ram, HmRegs *r);
void hm_PlayerState_0B(uint8_t *ram, HmRegs *r);
void hm_PlayerState_0A(uint8_t *ram, HmRegs *r);
void hm_CODE_81C556(uint8_t *ram, HmRegs *r);
void hm_CODE_81C66E(uint8_t *ram, HmRegs *r);
void hm_CODE_81C736(uint8_t *ram, HmRegs *r);
void hm_CODE_81C83B(uint8_t *ram, HmRegs *r);
void hm_PlayerState_03(uint8_t *ram, HmRegs *r);
void hm_PlayerState_04(uint8_t *ram, HmRegs *r);
void hm_PlayerState_05(uint8_t *ram, HmRegs *r);
void hm_PlayerState_08(uint8_t *ram, HmRegs *r);
void hm_CODE_81CB14(uint8_t *ram, HmRegs *r);
void hm_CODE_81CB5E(uint8_t *ram, HmRegs *r);
void hm_SUB_809D0B(uint8_t *ram, HmRegs *r);
void hm_SUB_809A64(uint8_t *ram, HmRegs *r);
void hm_CODE_858CB2(uint8_t *ram, HmRegs *r);
void hm_UNK_Audio1(uint8_t *ram, HmRegs *r);
void hm_UNUSED2(uint8_t *ram, HmRegs *r);
void hm_StartProgramedDMA(uint8_t *ram, HmRegs *r);
void hm_InitialzieScreenStatusVar(uint8_t *ram, HmRegs *r);
void hm_ManageGraphicPresets(uint8_t *ram, HmRegs *r);
void hm_ForceBlank(uint8_t *ram, HmRegs *r);
void hm_ResetForceBlank(uint8_t *ram, HmRegs *r);
void hm_SetsBrightness(uint8_t *ram, HmRegs *r);
void hm_ZeroesVRAM(uint8_t *ram, HmRegs *r);
void hm_ZeroesOAM(uint8_t *ram, HmRegs *r);
void hm_UNUSED1(uint8_t *ram, HmRegs *r);
void hm_ZeroesCGRAM(uint8_t *ram, HmRegs *r);
void hm_ZeroesPartialVRAM(uint8_t *ram, HmRegs *r);
void hm_StartLastPreparedDMA(uint8_t *ram, HmRegs *r);
void hm_LoadsFromVRAMwithOffset(uint8_t *ram, HmRegs *r);
void hm_CopiesWRAMtoOBJVGRAM(uint8_t *ram, HmRegs *r);
void hm_UpdateGraphics(uint8_t *ram, HmRegs *r);
void hm_AutoMapScrolling(uint8_t *ram, HmRegs *r);
void hm_LoadDialogueBox(uint8_t *ram, HmRegs *r);
void hm_SUB_82E5E7(uint8_t *ram, HmRegs *r);
void hm_SUB_82EA80(uint8_t *ram, HmRegs *r);
void hm_CODE_81CB77(uint8_t *ram, HmRegs *r);
void hm_CODE_81D716(uint8_t *ram, HmRegs *r);   /* actions.c */
void hm_PlayerAction_2E(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_40(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_41(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_42(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_43(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_44(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_45(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_46(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_47(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_48(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_49(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_51(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_52(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_53(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_54(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_55(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_56(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_57(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_10(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_11(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_12(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_13(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_14(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_15(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_1D(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_1E(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_26(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_27(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_28(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_29(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_2A(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_2B(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_2C(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_2D(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_2F(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_30(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_31(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_32(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_33(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_34(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_36(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_37(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_38(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_39(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_3A(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_3B(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_3C(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_3D(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_3E(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_3F(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_4A(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_4B(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_4C(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_4D(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_4E(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_4F(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_50(uint8_t *ram, HmRegs *r);
void hm_SUB_818000(uint8_t *ram, HmRegs *r);
void hm_CODE_81883A(uint8_t *ram, HmRegs *r);
void hm_CODE_818855(uint8_t *ram, HmRegs *r);
void hm_CODE_81889A(uint8_t *ram, HmRegs *r);
void hm_CODE_8188BC(uint8_t *ram, HmRegs *r);
void hm_CODE_8188E5(uint8_t *ram, HmRegs *r);
void hm_CODE_818923(uint8_t *ram, HmRegs *r);
void hm_CODE_818B6B(uint8_t *ram, HmRegs *r);
void hm_CODE_818BA5(uint8_t *ram, HmRegs *r);
void hm_CODE_818C24(uint8_t *ram, HmRegs *r);
void hm_CODE_818D38(uint8_t *ram, HmRegs *r);
void hm_CODE_818E16(uint8_t *ram, HmRegs *r);
void hm_CODE_818E1B(uint8_t *ram, HmRegs *r);
void hm_CODE_818E98(uint8_t *ram, HmRegs *r);
void hm_CODE_818F2A(uint8_t *ram, HmRegs *r);
void hm_CODE_818F5A(uint8_t *ram, HmRegs *r);
void hm_CODE_819010(uint8_t *ram, HmRegs *r);
void hm_CODE_819021(uint8_t *ram, HmRegs *r);
void hm_CODE_819049(uint8_t *ram, HmRegs *r);
void hm_CODE_819228(uint8_t *ram, HmRegs *r);
void hm_CODE_819379(uint8_t *ram, HmRegs *r);
void hm_CODE_81968F(uint8_t *ram, HmRegs *r);
void hm_CODE_819695(uint8_t *ram, HmRegs *r);
void hm_ItemObject_01(uint8_t *ram, HmRegs *r);
void hm_ItemObject_02(uint8_t *ram, HmRegs *r);
void hm_ItemObject_03(uint8_t *ram, HmRegs *r);
void hm_ItemObject_04(uint8_t *ram, HmRegs *r);
void hm_ItemObject_05(uint8_t *ram, HmRegs *r);
void hm_ItemObject_06(uint8_t *ram, HmRegs *r);
void hm_ItemObject_07(uint8_t *ram, HmRegs *r);
void hm_ItemObject_08(uint8_t *ram, HmRegs *r);
void hm_ItemObject_09(uint8_t *ram, HmRegs *r);
void hm_ItemObject_0A(uint8_t *ram, HmRegs *r);
void hm_ItemObject_0D(uint8_t *ram, HmRegs *r);
void hm_ItemObject_0E(uint8_t *ram, HmRegs *r);
void hm_ItemObject_0F(uint8_t *ram, HmRegs *r);
void hm_ItemObject_10(uint8_t *ram, HmRegs *r);
void hm_ItemObject_11(uint8_t *ram, HmRegs *r);
void hm_ItemObject_12(uint8_t *ram, HmRegs *r);
void hm_ItemObject_13(uint8_t *ram, HmRegs *r);
void hm_ItemObject_14(uint8_t *ram, HmRegs *r);
void hm_ItemObject_15(uint8_t *ram, HmRegs *r);
void hm_ItemObject_16(uint8_t *ram, HmRegs *r);
void hm_ItemObject_17(uint8_t *ram, HmRegs *r);
void hm_ItemObject_18(uint8_t *ram, HmRegs *r);
void hm_ItemObject_19(uint8_t *ram, HmRegs *r);
void hm_ItemObject_1A(uint8_t *ram, HmRegs *r);
void hm_ItemObject_1B(uint8_t *ram, HmRegs *r);
void hm_ItemObject_1C(uint8_t *ram, HmRegs *r);
void hm_ItemObject_1D(uint8_t *ram, HmRegs *r);
void hm_ItemObject_1E(uint8_t *ram, HmRegs *r);
void hm_ItemObject_1F(uint8_t *ram, HmRegs *r);
void hm_ItemObject_20(uint8_t *ram, HmRegs *r);
void hm_ItemObject_21(uint8_t *ram, HmRegs *r);
void hm_ItemObject_22(uint8_t *ram, HmRegs *r);
void hm_ItemObject_23(uint8_t *ram, HmRegs *r);
void hm_ItemObject_24(uint8_t *ram, HmRegs *r);
void hm_ItemObject_25(uint8_t *ram, HmRegs *r);
void hm_ItemObject_26(uint8_t *ram, HmRegs *r);
void hm_ItemObject_27(uint8_t *ram, HmRegs *r);
void hm_ItemObject_28(uint8_t *ram, HmRegs *r);
void hm_ItemObject_29(uint8_t *ram, HmRegs *r);
void hm_ItemObject_44(uint8_t *ram, HmRegs *r);
void hm_ItemObject_45(uint8_t *ram, HmRegs *r);
void hm_ItemObject_46(uint8_t *ram, HmRegs *r);
void hm_ItemObject_47(uint8_t *ram, HmRegs *r);
void hm_ItemObject_48(uint8_t *ram, HmRegs *r);
void hm_ItemObject_49(uint8_t *ram, HmRegs *r);
void hm_ItemObject_4A(uint8_t *ram, HmRegs *r);
void hm_ItemObject_4B(uint8_t *ram, HmRegs *r);
void hm_ItemObject_4C(uint8_t *ram, HmRegs *r);
void hm_ItemObject_4D(uint8_t *ram, HmRegs *r);
void hm_ItemObject_4E(uint8_t *ram, HmRegs *r);
void hm_ItemObject_4F(uint8_t *ram, HmRegs *r);
void hm_ItemObject_50(uint8_t *ram, HmRegs *r);
void hm_ItemObject_51(uint8_t *ram, HmRegs *r);
void hm_ItemObject_52(uint8_t *ram, HmRegs *r);
void hm_ItemObject_53(uint8_t *ram, HmRegs *r);
void hm_ItemObject_54(uint8_t *ram, HmRegs *r);
void hm_ItemObject_55(uint8_t *ram, HmRegs *r);
void hm_ItemObject_56(uint8_t *ram, HmRegs *r);
void hm_ItemObject_58(uint8_t *ram, HmRegs *r);
void hm_ItemObject_59(uint8_t *ram, HmRegs *r);
void hm_ItemObject_5A(uint8_t *ram, HmRegs *r);
void hm_ToolUsedNoAction(uint8_t *ram, HmRegs *r);  /* toolused.c */
void hm_ToolUsedSickle(uint8_t *ram, HmRegs *r);
void hm_ToolUsedHoe(uint8_t *ram, HmRegs *r);
void hm_ToolUsedHammer(uint8_t *ram, HmRegs *r);
void hm_ToolUsedAxe(uint8_t *ram, HmRegs *r);
void hm_ToolUsedCornSeeds(uint8_t *ram, HmRegs *r);
void hm_ToolUsedTomatoSeeds(uint8_t *ram, HmRegs *r);
void hm_ToolUsedPotatoSeeds(uint8_t *ram, HmRegs *r);
void hm_ToolUsedTurnipSeeds(uint8_t *ram, HmRegs *r);
void hm_ToolUsedCowMedicine(uint8_t *ram, HmRegs *r);
void hm_ToolUsedMiraclePotion(uint8_t *ram, HmRegs *r);
void hm_ToolUsedBell(uint8_t *ram, HmRegs *r);
void hm_ToolUsedGrassSeeds(uint8_t *ram, HmRegs *r);
void hm_ToolUsedPaint(uint8_t *ram, HmRegs *r);
void hm_ToolUsedMilker(uint8_t *ram, HmRegs *r);
void hm_ToolUsedBrush(uint8_t *ram, HmRegs *r);
void hm_ToolUsedGoldSickle(uint8_t *ram, HmRegs *r);
void hm_ToolUsedGoldHoe(uint8_t *ram, HmRegs *r);
void hm_ToolUsedGoldHammer(uint8_t *ram, HmRegs *r);
void hm_ToolUsedGoldAxe(uint8_t *ram, HmRegs *r);
void hm_ToolUsedMagicBeans(uint8_t *ram, HmRegs *r);
void hm_ToolUsedGemSeed(uint8_t *ram, HmRegs *r);
void hm_ToolUsedBlueFeather(uint8_t *ram, HmRegs *r);
void hm_ToolUsedChickenFeed(uint8_t *ram, HmRegs *r);
void hm_ToolUsedCowFeed(uint8_t *ram, HmRegs *r);
void hm_ToolUsedUNK(uint8_t *ram, HmRegs *r);
void hm_SetCCPoiner(uint8_t *ram, HmRegs *r);         /* events.c */
void hm_ResetsCurrentCCStructureLong(uint8_t *ram, HmRegs *r);
void hm_SUB_8480F8(uint8_t *ram, HmRegs *r);
void hm_ResetsCurrentCCStructureShort(uint8_t *ram, HmRegs *r);
void hm_UNK_LoadCCDataLong(uint8_t *ram, HmRegs *r);
void hm_UNK_LoadCCDataShort(uint8_t *ram, HmRegs *r);
void hm_Event_UNKChickenRelated(uint8_t *ram, HmRegs *r);  /* eventcmds.c */
void hm_Event_UNKDogRelated(uint8_t *ram, HmRegs *r);
void hm_Event_UNKCowRelated(uint8_t *ram, HmRegs *r);
void hm_Event_AudioRelated(uint8_t *ram, HmRegs *r);
void hm_Event_TimeRunning(uint8_t *ram, HmRegs *r);
void hm_Event_TimeStopped(uint8_t *ram, HmRegs *r);
void hm_Event_SetHour(uint8_t *ram, HmRegs *r);
void hm_Event_Empty(uint8_t *ram, HmRegs *r);
void hm_Event_SetPlayerPosition(uint8_t *ram, HmRegs *r);
void hm_Event_SetTransitionDest(uint8_t *ram, HmRegs *r);
void hm_Event_SetPlayerDirection(uint8_t *ram, HmRegs *r);
void hm_Event_UNKChangesGameState(uint8_t *ram, HmRegs *r);
void hm_Event_NewCCStructure(uint8_t *ram, HmRegs *r);
void hm_Event_UNKset7Aflag(uint8_t *ram, HmRegs *r);
void hm_Event_UNKCompareset7Aflag(uint8_t *ram, HmRegs *r);
void hm_Event_UNKCCchanges2(uint8_t *ram, HmRegs *r);
void hm_Event_UNKDisableCCstruct(uint8_t *ram, HmRegs *r);
void hm_Event_UNKChangesGameState2(uint8_t *ram, HmRegs *r);
void hm_Event_JumptoInstruction(uint8_t *ram, HmRegs *r);
void hm_Event_UNKSetsCC10(uint8_t *ram, HmRegs *r);
void hm_Event_JumpifFlagSet(uint8_t *ram, HmRegs *r);
void hm_Event_JumpifEquals(uint8_t *ram, HmRegs *r);
void hm_Event_JumpifBetween(uint8_t *ram, HmRegs *r);
void hm_Event_GetRNG(uint8_t *ram, HmRegs *r);
void hm_Event_JumpIfEqualsRNG(uint8_t *ram, HmRegs *r);
void hm_Event_UNKSetAnimation(uint8_t *ram, HmRegs *r);
void hm_Event_UNKPickupMole(uint8_t *ram, HmRegs *r);
void hm_Event_UNKCCchanges6(uint8_t *ram, HmRegs *r);
void hm_Event_UNK3(uint8_t *ram, HmRegs *r);
void hm_Event_JumpifBetweenValueLong(uint8_t *ram, HmRegs *r);
void hm_Event_UNKFishing(uint8_t *ram, HmRegs *r);
void hm_Event_UNKCCchanges13(uint8_t *ram, HmRegs *r);
void hm_Event_UNKCCchanges12(uint8_t *ram, HmRegs *r);
void hm_Event_UNKCCchanges10(uint8_t *ram, HmRegs *r);
void hm_Event_UNKTextrelated(uint8_t *ram, HmRegs *r);
void hm_Event_JumpifEqualsValueLong(uint8_t *ram, HmRegs *r);
void hm_Event_JumpifBetweenValue(uint8_t *ram, HmRegs *r);
void hm_Event_ChangeMoney(uint8_t *ram, HmRegs *r);
void hm_Event_UNKCCchanges4(uint8_t *ram, HmRegs *r);
void hm_Event_UNKCCchanges14(uint8_t *ram, HmRegs *r);
void hm_Event_JumpifEqualsValue(uint8_t *ram, HmRegs *r);
void hm_Event_SetFlag(uint8_t *ram, HmRegs *r);
void hm_Event_SetValueLong(uint8_t *ram, HmRegs *r);
void hm_Event_MapScrolling(uint8_t *ram, HmRegs *r);
void hm_Event_StoreValue(uint8_t *ram, HmRegs *r);
void hm_Event_SetValueSort(uint8_t *ram, HmRegs *r);
void hm_Event_UNKJumpif018F(uint8_t *ram, HmRegs *r);
void hm_Event_TransitiontoX(uint8_t *ram, HmRegs *r);
void hm_Event_ChangeItemonHand(uint8_t *ram, HmRegs *r);
void hm_Event_SetItemonHand(uint8_t *ram, HmRegs *r);
void hm_Event_UNK2(uint8_t *ram, HmRegs *r);
void hm_Event_TransitiontoHouse(uint8_t *ram, HmRegs *r);
void hm_Event_UNKCCchanges16(uint8_t *ram, HmRegs *r);
void hm_Event_UNKSet5C(uint8_t *ram, HmRegs *r);
void hm_Event_UNKReset5C(uint8_t *ram, HmRegs *r);
void hm_Event_UNKSet60(uint8_t *ram, HmRegs *r);
void hm_Event_DropItemAnimation(uint8_t *ram, HmRegs *r);
void hm_StartTextBox(uint8_t *ram, HmRegs *r);       /* text.c */
void hm_SUB_83932D(uint8_t *ram, HmRegs *r);
void hm_CloseTextBox(uint8_t *ram, HmRegs *r);
void hm_DrawTextBoxArrow(uint8_t *ram, HmRegs *r);
void hm_BlinkTextBoxArrow(uint8_t *ram, HmRegs *r);
void hm_TextBoxArrow(uint8_t *ram, HmRegs *r);
void hm_FindLetterGraphics(uint8_t *ram, HmRegs *r);
void hm_DrawLetter(uint8_t *ram, HmRegs *r);
void hm_AdvanceLetterCursor(uint8_t *ram, HmRegs *r);
void hm_BlinkTextCursor(uint8_t *ram, HmRegs *r);
void hm_PrintDigit(uint8_t *ram, HmRegs *r);
void hm_UNK_Audio17(uint8_t *ram, HmRegs *r);       /* audio.c */
void hm_UNK_Audio7(uint8_t *ram, HmRegs *r);
void hm_UNK_Audio8(uint8_t *ram, HmRegs *r);
void hm_UNK_Audio15(uint8_t *ram, HmRegs *r);
void hm_UNK_Audio19(uint8_t *ram, HmRegs *r);
void hm_UNK_Audio16(uint8_t *ram, HmRegs *r);
void hm_UNK_Audio2(uint8_t *ram, HmRegs *r);
void hm_ToolUsedWateringCan(uint8_t *ram, HmRegs *r); /* toolused.c */
void hm_ToolUsedSprinkler(uint8_t *ram, HmRegs *r);
void hm_SUB_829260(uint8_t *ram, HmRegs *r);
void hm_SUB_82927D(uint8_t *ram, HmRegs *r);         /* tools.c */
void hm_PrintTextFrame(uint8_t *ram, HmRegs *r);    /* text.c */
void hm_SaveGameSlot(uint8_t *ram, HmRegs *r);       /* save.c */
void hm_LoadGameSlot(uint8_t *ram, HmRegs *r);
void hm_NewGameSetup(uint8_t *ram, HmRegs *r);
void hm_LoadGameSlotResume(uint8_t *ram, HmRegs *r);
void hm_CheckSRAMIntegrity(uint8_t *ram, HmRegs *r);
void hm_SaveSlotMenuInputs(uint8_t *ram, HmRegs *r); /* menus.c */
void hm_LoadSlotMenuInputs(uint8_t *ram, HmRegs *r);
void hm_StartMenuInputs(uint8_t *ram, HmRegs *r);
void hm_TitleMenuInputs(uint8_t *ram, HmRegs *r);
void hm_ListInputs(uint8_t *ram, HmRegs *r);
void hm_ImmobileInputs(uint8_t *ram, HmRegs *r);
void hm_SUB_80A18D(uint8_t *ram, HmRegs *r);         /* scroll.c */
void hm_SUB_80A308(uint8_t *ram, HmRegs *r);
void hm_SUB_80A481(uint8_t *ram, HmRegs *r);
void hm_SUB_80A617(uint8_t *ram, HmRegs *r);
void hm_UpdateBGOffset(uint8_t *ram, HmRegs *r);
void hm_SUB_80A0E1(uint8_t *ram, HmRegs *r);
void hm_UNK_StaticMapScroling(uint8_t *ram, HmRegs *r);
void hm_SUB_80A152(uint8_t *ram, HmRegs *r);
void hm_SUB_809EBC(uint8_t *ram, HmRegs *r);
void hm_SUB_809F61(uint8_t *ram, HmRegs *r);
void hm_SetTimeofDayPalette(uint8_t *ram, HmRegs *r);   /* palette.c */
void hm_TransitionTimeofDayPalettes(uint8_t *ram, HmRegs *r);
void hm_ClearsTimeofDayPaletteIndexandSetsNext(uint8_t *ram, HmRegs *r);
void hm_ClearsTimeofDayPalette(uint8_t *ram, HmRegs *r);
void hm_SUB_80916F(uint8_t *ram, HmRegs *r);
void hm_SUB_80919D(uint8_t *ram, HmRegs *r);
void hm_LoadFirstHalfPaletteToWRAM(uint8_t *ram, HmRegs *r);
void hm_LoadSecondHalfPaletteToWRAM(uint8_t *ram, HmRegs *r);
void hm_CustomiseSpritePalette(uint8_t *ram, HmRegs *r);
void hm_UNK_CustomisePalette(uint8_t *ram, HmRegs *r);
void hm_ChangePalettebyTime(uint8_t *ram, HmRegs *r);
void hm_PrepareSpritePalette(uint8_t *ram, HmRegs *r);
void hm_SetPaletteToLoad(uint8_t *ram, HmRegs *r);
void hm_Event_TimeofDayPalette(uint8_t *ram, HmRegs *r);
void hm_CFFFF(uint8_t *ram, HmRegs *r);          /* collision.c */
void hm_CEEEE(uint8_t *ram, HmRegs *r);
void hm_CBBBB(uint8_t *ram, HmRegs *r);
void hm_CDDDD(uint8_t *ram, HmRegs *r);
void hm_UNK_ToolUsed(uint8_t *ram, HmRegs *r);     /* toolanim.c */
void hm_ToolAnimationNoAction(uint8_t *ram, HmRegs *r);
void hm_ToolAnimationSickle(uint8_t *ram, HmRegs *r);
void hm_ToolAnimationHoe(uint8_t *ram, HmRegs *r);
void hm_ToolAnimationHammer(uint8_t *ram, HmRegs *r);
void hm_ToolAnimationAxe(uint8_t *ram, HmRegs *r);
void hm_ToolAnimationCornSeeds(uint8_t *ram, HmRegs *r);
void hm_ToolAnimationTomatoSeeds(uint8_t *ram, HmRegs *r);
void hm_ToolAnimationPotatoSeeds(uint8_t *ram, HmRegs *r);
void hm_ToolAnimationTurnipSeeds(uint8_t *ram, HmRegs *r);
void hm_ToolAnimationCowMedicine(uint8_t *ram, HmRegs *r);
void hm_ToolAnimationMiraclePotion(uint8_t *ram, HmRegs *r);
void hm_ToolAnimationBell(uint8_t *ram, HmRegs *r);
void hm_ToolAnimationGrassSeeds(uint8_t *ram, HmRegs *r);
void hm_ToolAnimationPaint(uint8_t *ram, HmRegs *r);
void hm_ToolAnimationMilker(uint8_t *ram, HmRegs *r);
void hm_ToolAnimationBrush(uint8_t *ram, HmRegs *r);
void hm_ToolAnimationWateringCan(uint8_t *ram, HmRegs *r);
void hm_ToolAnimationGoldSickle(uint8_t *ram, HmRegs *r);
void hm_ToolAnimationGoldHoe(uint8_t *ram, HmRegs *r);
void hm_ToolAnimationGoldHammer(uint8_t *ram, HmRegs *r);
void hm_ToolAnimationGoldAxe(uint8_t *ram, HmRegs *r);
void hm_ToolAnimationSprinkler(uint8_t *ram, HmRegs *r);
void hm_ToolAnimationBeans(uint8_t *ram, HmRegs *r);
void hm_ToolAnimationGem(uint8_t *ram, HmRegs *r);
void hm_ToolAnimationBlueFeather(uint8_t *ram, HmRegs *r);
void hm_ToolAnimationChickenFeed(uint8_t *ram, HmRegs *r);
void hm_ToolAnimationCowFeed(uint8_t *ram, HmRegs *r);
void hm_ToolAnimationUNK(uint8_t *ram, HmRegs *r);
void hm_SUB_82AD0E(uint8_t *ram, HmRegs *r);       /* interact.c */
void hm_PlayerMovementInputs(uint8_t *ram, HmRegs *r); /* playerinput.c */
void hm_CODE_84C7D6(uint8_t *ram, HmRegs *r);
void hm_NameCursorPharse(uint8_t *ram, HmRegs *r); /* menus.c */
void hm_SUB_82EB57(uint8_t *ram, HmRegs *r);
void hm_Unk_OutOfMenu(uint8_t *ram, HmRegs *r);
void hm_NameMenuKeyDown(uint8_t *ram, HmRegs *r);
void hm_NameMenuKeyUp(uint8_t *ram, HmRegs *r);
void hm_NameMenuKeyLeft(uint8_t *ram, HmRegs *r);
void hm_NameMenuKeyRight(uint8_t *ram, HmRegs *r);
void hm_NameMenuKeyB(uint8_t *ram, HmRegs *r);
void hm_NameMenuKeyA(uint8_t *ram, HmRegs *r);
void hm_NameMenuInputs(uint8_t *ram, HmRegs *r);
void hm_CODE_84C2D8(uint8_t *ram, HmRegs *r);
void hm_CODE_84C2B4(uint8_t *ram, HmRegs *r);
void hm_MenuInputs(uint8_t *ram, HmRegs *r);
void hm_CODE_84CED2(uint8_t *ram, HmRegs *r);
void hm_InputTypeSelector(uint8_t *ram, HmRegs *r);
void hm_UNK_PartialMap(uint8_t *ram, HmRegs *r);   /* mapedit.c */
void hm_Event_UNKChickenRelated2(uint8_t *ram, HmRegs *r); /* eventcmds.c */
void hm_Event_Cmd51(uint8_t *ram, HmRegs *r);
void hm_Event_Cmd52(uint8_t *ram, HmRegs *r);
void hm_Event_Cmd53(uint8_t *ram, HmRegs *r);
void hm_Event_Cmd54(uint8_t *ram, HmRegs *r);
void hm_Event_Cmd55(uint8_t *ram, HmRegs *r);
void hm_Event_Cmd56(uint8_t *ram, HmRegs *r);
void hm_Event_Cmd57(uint8_t *ram, HmRegs *r);
void hm_Event_Cmd58(uint8_t *ram, HmRegs *r);
void hm_Event_Cmd59(uint8_t *ram, HmRegs *r);
void hm_Event_UNKPlayerPos(uint8_t *ram, HmRegs *r);
void hm_DivisionSigned(uint8_t *ram, HmRegs *r);   /* math.c */
void hm_DecompressTileMap(uint8_t *ram, HmRegs *r); /* decompress.c */
void hm_Event_ResetFlag(uint8_t *ram, HmRegs *r);
void hm_Event_EditTileonMap(uint8_t *ram, HmRegs *r);
void hm_Event_StartTextBox(uint8_t *ram, HmRegs *r);
void hm_Event_StartTextBoxCopy(uint8_t *ram, HmRegs *r);
void hm_Event_UNKTools(uint8_t *ram, HmRegs *r);
void hm_ReadJoypad(uint8_t *ram, HmRegs *r);         /* joypad.c */
void hm_UnusedBarnCoopCheck(uint8_t *ram, HmRegs *r); /* animals.c */
void hm_UnusedBarnCheck(uint8_t *ram, HmRegs *r);
void hm_ToolUsedSound1(uint8_t *ram, HmRegs *r);     /* toolsound.c */
void hm_CopyCurrentMaptoFarmMap(uint8_t *ram, HmRegs *r);  /* farm.c */
void hm_LoadDefaultFarmMap(uint8_t *ram, HmRegs *r);
void hm_LoadMap(uint8_t *ram, HmRegs *r);
void hm_ClearsofAllCCStructs(uint8_t *ram, HmRegs *r);     /* events.c */
void hm_MultiplicationUnsigned(uint8_t *ram, HmRegs *r);   /* math.c */
void hm_SUB_81A7CE(uint8_t *ram, HmRegs *r); /* maptiles.c */
void hm_SUB_82B0A7(uint8_t *ram, HmRegs *r);
void hm_SUB_81B2BB(uint8_t *ram, HmRegs *r);
void hm_UNK_ExecuteFromPointers(uint8_t *ram, HmRegs *r);
void hm_ReplaceTilesFarm(uint8_t *ram, HmRegs *r);
void hm_ReplaceTilesMoutain(uint8_t *ram, HmRegs *r);
void hm_ReplaceTilesHouse1(uint8_t *ram, HmRegs *r);
void hm_ReplaceTilesHouse2(uint8_t *ram, HmRegs *r);
void hm_ReplaceTilesHouse3(uint8_t *ram, HmRegs *r);
void hm_ReplaceTilesFlowerShop(uint8_t *ram, HmRegs *r);
void hm_ReplaceTilesGeneralStore(uint8_t *ram, HmRegs *r);
void hm_ReplaceTilesAnimalShop(uint8_t *ram, HmRegs *r);
void hm_ReplaceTilesToolshed(uint8_t *ram, HmRegs *r);
void hm_ReplaceTilesBarn(uint8_t *ram, HmRegs *r);
void hm_ReplaceTilesCoop(uint8_t *ram, HmRegs *r);
void hm_ReplaceTilesMountainCave(uint8_t *ram, HmRegs *r);
void hm_ReplaceTilesElfTunnel(uint8_t *ram, HmRegs *r);
void hm_ReplaceTilesMap37(uint8_t *ram, HmRegs *r);
void hm_ReplaceTilesNothing(uint8_t *ram, HmRegs *r);
void hm_Unk_Audio23(uint8_t *ram, HmRegs *r);
void hm_UNK_Audio6(uint8_t *ram, HmRegs *r);
void hm_UNK_Audio13(uint8_t *ram, HmRegs *r);
void hm_UNK_Audio14(uint8_t *ram, HmRegs *r);
void hm_UNK_Audio10(uint8_t *ram, HmRegs *r);
void hm_UNK_Audio21(uint8_t *ram, HmRegs *r);
void hm_UNK_Audio22(uint8_t *ram, HmRegs *r);
void hm_UNK_Audio25(uint8_t *ram, HmRegs *r);
void hm_UNK_Audio5(uint8_t *ram, HmRegs *r);
void hm_UNK_Audio12(uint8_t *ram, HmRegs *r);
void hm_UNK_Audio11(uint8_t *ram, HmRegs *r);
void hm_UNK_Audio9(uint8_t *ram, HmRegs *r);
void hm_UNK_Audio3(uint8_t *ram, HmRegs *r);
void hm_UNK_Audio4(uint8_t *ram, HmRegs *r);
void hm_GetFenceFromStack(uint8_t *ram, HmRegs *r);
void hm_CODE_81D6E2(uint8_t *ram, HmRegs *r);
void hm_CODE_81D6F8(uint8_t *ram, HmRegs *r);
void hm_CODE_81D1C5(uint8_t *ram, HmRegs *r);
void hm_GetBackgroundData(uint8_t *ram, HmRegs *r);
void hm_UNK_PresetsMemory3(uint8_t *ram, HmRegs *r);
void hm_ActionNothing(uint8_t *ram, HmRegs *r);
void hm_ShippingScene(uint8_t *ram, HmRegs *r);       /* shipping.c */
void hm_ShippingSceneDialogue(uint8_t *ram, HmRegs *r);
void hm_CODE_858C9F(uint8_t *ram, HmRegs *r);
void hm_CODE_858B7B(uint8_t *ram, HmRegs *r);
void hm_CODE_858B41(uint8_t *ram, HmRegs *r);
void hm_CODE_8582C7(uint8_t *ram, HmRegs *r);
void hm_GetOBJOrderToDraw(uint8_t *ram, HmRegs *r);
void hm_PresetsWRAMCopyofOAM(uint8_t *ram, HmRegs *r);
void hm_PresetsWRAMCopyOAMCopy(uint8_t *ram, HmRegs *r);
void hm_InitializeOBJs(uint8_t *ram, HmRegs *r);
void hm_CODE_81CFA0(uint8_t *ram, HmRegs *r);
void hm_CODE_81CFE6(uint8_t *ram, HmRegs *r);
void hm_CODE_81D03D(uint8_t *ram, HmRegs *r);
void hm_CODE_8581A2(uint8_t *ram, HmRegs *r);
void hm_SUB_848895(uint8_t *ram, HmRegs *r);
void hm_SUB_848020(uint8_t *ram, HmRegs *r);
void hm_Event_UNK1(uint8_t *ram, HmRegs *r);
void hm_Event_UNKCCchanges7(uint8_t *ram, HmRegs *r);
void hm_Event_UNKCCchanges11(uint8_t *ram, HmRegs *r);
void hm_Event_UNKCCchanges8(uint8_t *ram, HmRegs *r);
void hm_Event_UNKCCchanges15(uint8_t *ram, HmRegs *r);
void hm_Event_UNKCCchanges3(uint8_t *ram, HmRegs *r);
void hm_Event_UNKCCchanges17(uint8_t *ram, HmRegs *r);
void hm_CODE_8580B9(uint8_t *ram, HmRegs *r);
void hm_CODE_858100(uint8_t *ram, HmRegs *r);
void hm_SUB_848331(uint8_t *ram, HmRegs *r);
void hm_SUB_8483CC(uint8_t *ram, HmRegs *r);
void hm_SUB_848286(uint8_t *ram, HmRegs *r);
void hm_Event_AudioRelated2(uint8_t *ram, HmRegs *r);
void hm_Event_UNKCFD1(uint8_t *ram, HmRegs *r);
void hm_Event_UNKCCchanges5(uint8_t *ram, HmRegs *r);
void hm_Event_UNKCCchanges9(uint8_t *ram, HmRegs *r);
void hm_CODE_8581CB(uint8_t *ram, HmRegs *r);
void hm_InvertIfFlipedX(uint8_t *ram, HmRegs *r);
void hm_InvertIfFlipedY(uint8_t *ram, HmRegs *r);
void hm_ClearSpriteDataTables(uint8_t *ram, HmRegs *r);
void hm_CODE_81A58F(uint8_t *ram, HmRegs *r);
void hm_CODE_81A600(uint8_t *ram, HmRegs *r);
void hm_CODE_81A4F1(uint8_t *ram, HmRegs *r);
void hm_UNK_SetPointer42(uint8_t *ram, HmRegs *r); /* coloranim.c */
void hm_UNK_BigLoop(uint8_t *ram, HmRegs *r);
void hm_Zeroes42Pointers(uint8_t *ram, HmRegs *r);
void hm_ZeroesPartial42Pointers(uint8_t *ram, HmRegs *r);
void hm_ZeroesOne42Pointer(uint8_t *ram, HmRegs *r);
void hm_CallIndexedSubrutines(uint8_t *ram, HmRegs *r);
void hm_SUB_80C36C(uint8_t *ram, HmRegs *r);
void hm_SUB_80C3D9(uint8_t *ram, HmRegs *r);
void hm_SUB_80C446(uint8_t *ram, HmRegs *r);
void hm_SUB_80C4B3(uint8_t *ram, HmRegs *r);
void hm_SUB_80C520(uint8_t *ram, HmRegs *r);
void hm_SUB_80C58D(uint8_t *ram, HmRegs *r);
void hm_SUB_80C5FA(uint8_t *ram, HmRegs *r);
void hm_SUB_80C667(uint8_t *ram, HmRegs *r);
void hm_SUB_80C6D4(uint8_t *ram, HmRegs *r);
void hm_SUB_80C792(uint8_t *ram, HmRegs *r);
void hm_SUB_80C7FF(uint8_t *ram, HmRegs *r);
void hm_SUB_80C86C(uint8_t *ram, HmRegs *r);
void hm_SUB_80C945(uint8_t *ram, HmRegs *r);
void hm_SUB_80C9B2(uint8_t *ram, HmRegs *r);
void hm_SUB_80CA1F(uint8_t *ram, HmRegs *r);
void hm_SUB_80CA8C(uint8_t *ram, HmRegs *r);
void hm_SUB_80CADE(uint8_t *ram, HmRegs *r);
void hm_SUB_80CB30(uint8_t *ram, HmRegs *r);
void hm_SUB_80CB82(uint8_t *ram, HmRegs *r);
void hm_SUB_80CBD4(uint8_t *ram, HmRegs *r);
void hm_SUB_80CC92(uint8_t *ram, HmRegs *r);
void hm_SUB_80CCE4(uint8_t *ram, HmRegs *r);
void hm_SUB_80CD36(uint8_t *ram, HmRegs *r);
void hm_SUB_80CD88(uint8_t *ram, HmRegs *r);
void hm_SUB_80CE46(uint8_t *ram, HmRegs *r);
void hm_SUB_80CE98(uint8_t *ram, HmRegs *r);
void hm_SUB_80CEEA(uint8_t *ram, HmRegs *r);
void hm_SUB_80CF3C(uint8_t *ram, HmRegs *r);
void hm_SUB_80CFFA(uint8_t *ram, HmRegs *r);
void hm_SUB_80D067(uint8_t *ram, HmRegs *r);
void hm_SUB_80D125(uint8_t *ram, HmRegs *r);
void hm_SUB_80D1E3(uint8_t *ram, HmRegs *r);
void hm_SUB_80D2A1(uint8_t *ram, HmRegs *r);
void hm_SUB_80D344(uint8_t *ram, HmRegs *r);
void hm_SUB_80D402(uint8_t *ram, HmRegs *r);
void hm_SUB_80D4C0(uint8_t *ram, HmRegs *r);
void hm_SUB_80D563(uint8_t *ram, HmRegs *r);
void hm_SUB_80D606(uint8_t *ram, HmRegs *r);
void hm_SUB_80D6FA(uint8_t *ram, HmRegs *r);
void hm_SUB_80D7EE(uint8_t *ram, HmRegs *r);
void hm_SUB_80D891(uint8_t *ram, HmRegs *r);
void hm_SUB_80D934(uint8_t *ram, HmRegs *r);
void hm_SUB_80D96B(uint8_t *ram, HmRegs *r);
void hm_SUB_80D9A2(uint8_t *ram, HmRegs *r);
void hm_SUB_80D9D9(uint8_t *ram, HmRegs *r);
void hm_SUB_80DA10(uint8_t *ram, HmRegs *r);
void hm_SUB_80DA47(uint8_t *ram, HmRegs *r);
void hm_SUB_80DAB4(uint8_t *ram, HmRegs *r);
void hm_SUB_80DB06(uint8_t *ram, HmRegs *r);
void hm_SUB_80DC30(uint8_t *ram, HmRegs *r);
void hm_SUB_80DD09(uint8_t *ram, HmRegs *r);
