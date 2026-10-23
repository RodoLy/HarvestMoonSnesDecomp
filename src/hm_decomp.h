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
static inline uint8_t hm_bus_rd8(uint8_t *ram, uint32_t addr)
{
  uint8_t *p = hm_bus_ram(ram, addr);
  if (p) return *p;
  uint8_t bank = (uint8_t)(addr >> 16);
  uint16_t a = (uint16_t)addr;
  if (a >= 0x8000 && (bank & 0x7F) < 0x7E)
    return g_rom[(uint32_t)(bank & 0x3F) << 15 | (a & 0x7FFF)];
  if (hm_is_apu_port(addr)) return hm_apu_read(a);
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
       HM_APU = 64 };

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
void hm_EditTileonMap(uint8_t *ram, HmRegs *r);       /* mapedit.c */
void hm_EditTileonMapandsets0181(uint8_t *ram, HmRegs *r);
void hm_LoadsDamageProbabilities(uint8_t *ram, HmRegs *r);
void hm_ClimateFarmDamageCheck(uint8_t *ram, HmRegs *r);
void hm_ClimateFarmDamagePrep(uint8_t *ram, HmRegs *r);
void hm_MonthlyFarmTilesCheck(uint8_t *ram, HmRegs *r);
void hm_NightlyFarmTilesCheck(uint8_t *ram, HmRegs *r);
void hm_CalculateFarmDevelopment(uint8_t *ram, HmRegs *r);
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
