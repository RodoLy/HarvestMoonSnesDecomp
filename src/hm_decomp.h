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
        * done): UploadSoundProgram, which talks to the chip's boot ROM; after the
        * upload the chip runs the game's sound program and never answers
        * that way again. E.g. HM_DECOMP_FUZZ=100000:1
        * HM_DECOMP_FUZZ_ONLY=UploadSoundProgram. */
       HM_LIVE = 128,
       /* Writes the console's chips (PPU, DMA, WRAM port, $42xx): checked
        * by running the rewrite on the real machine from the state before
        * and comparing the chips' state (hm_io.c). */
       HM_IO = 256,
       /* An interrupt handler (RTI): fuzzed with an interrupt's 4-byte frame
        * (bank, address, flags) on the stack. */
       HM_RTI = 512,
       /* Waits for the NMI (WaitForNMI and what calls it). In the game the
        * compiled code yields to the frame scheduler there, so live calls
        * are never complete; the fuzzer runs the original in the
        * interpreter and, when it reaches the wait loop with $00 = 0, makes
        * the NMI happen right there (hm_nmi below does the same in C). */
       HM_WAIT = 1024,
       /* A long routine made of others that waits frames: the flags its
        * waits save (PHP) and the NMI pushes depend on N, Z and V left by
        * the routines before, which the rewrites do not follow, so the
        * stack below the final stack pointer (what is left there, never
        * read again) is not compared. Everything else is. */
       HM_LOOSE = 2048,
       /* Ends by going into the game loop (GameLoop, $80:8083), which never
        * returns: the original is stopped when it gets there (an RTL takes
        * it back as if it returned), and the rewrite returns there. For
        * GameLoop itself the stop is its JMP back ($80:80DB, one frame
        * done) or its JML into the names screen ($80:80E9); for the names
        * screen (NamesScreen) its JML back to the frame's start or to
        * the name's routine; for RESET, which throws the stack away, its
        * JML to the title, where the checker's frame is put back; for the
        * loop of one BRA (UnusedEndlessLoop), its second turn. */
       HM_TOLOOP = 4096 };

/* video.c: the NMI arriving at `pc` (the wait loop) with the flags `p`:
 * pushes bank, address and flags as the CPU does, runs NMI_Interrupt with
 * PB = 0, I set and decimal clear, and returns through its RTI. */
void hm_nmi(uint8_t *ram, HmRegs *r, uint16_t pc, uint8_t p);
/* game.c: the fifteen JSLs of a game frame (GameLoop's, and the demos'),
 * one every 4 bytes from `at` in the caller's bank */
void hm_game_frame(uint8_t *ram, HmRegs *r, uint16_t at);

/* ── Running the game on the rewrites alone (native/) ──────────────────────
 * The routines that never return (RESET, the title screen, the demos, the
 * names screen, GameLoop) end where the original jumps on (JML / JMP) to
 * the next one; g_hm_next is that address, so a host can call the next
 * routine itself. It changes nothing the checker compares.
 * g_hm_frame_hook, when set, runs at every NMI before its handler: the end
 * of a frame (draw it, read the pads, run the sound chip). NULL in the
 * checking build. */
extern uint32_t g_hm_next;
extern void (*g_hm_frame_hook)(uint8_t *ram);
/* The native build's improvements (native/mods.c) reach into three places,
 * through these; their values here are the ROM's own, so the checking
 * build (and a native build with the improvements off) is untouched:
 *   g_hm_oam_right / g_hm_oam_left: the screen X a sprite piece must be
 *     under / from to be drawn (PrepareOAMData, $85:853B / $85:8540;
 *     widescreen widens them);
 *   g_hm_no_ending: the house's date check for the ending (Summer 30,
 *     Year 3, $83:DEE7) fails while it is set (the endless game);
 *   g_hm_year_suffix: the diary's year ordinal past the game's three
 *     ("4th", "5th"... $82:F298);
 *   g_hm_crops_passable: the player walks through the crops of the farm
 *     (CollideTileCorner, the map's collision: a crop tile no longer blocks; and
 *     PutItemDown changes the tile A looked at, 7 pixels ahead);
 *   g_hm_single_seed: a seed bag sows one tile at a time, 9 per bag
 *     (the seeds' ToolUsed; g_hm_seeds_used, grass, corn, tomato, potato,
 *     turnip: the seeds gone from each open bag, kept in the saved game's
 *     unused bytes +20 to +25 by SaveGameSlot / LoadGameSlot);
 *   g_hm_bag_take: the crop bag gives back an item (PutItemDown leaves
 *     nothing on the map for it). */
extern uint16_t g_hm_oam_right, g_hm_oam_left;
extern int g_hm_no_ending;
extern int g_hm_crops_passable;
extern int g_hm_single_seed;
/* g_hm_bag_on: the crop bag (native/mods.c) is on: its items (g_hm_bag,
 * item ids, 0 = empty) are kept in the saved game's unused bytes +52 to
 * +5B by SaveGameSlot / LoadGameSlot; g_hm_bag_take: the next PutItemDown
 * (picking up) takes the item out of the bag: nothing is left on the map
 * and no sound (it clears it). */
extern int g_hm_bag_on, g_hm_bag_take;
extern uint8_t g_hm_bag[9];
extern uint8_t g_hm_seeds_used[5];
/* g_hm_wife_on: the wife's help (native/wife.c) is on; g_hm_wife_until:
 * the last day she helps (year x 120 + season x 30 + day; 0 none), kept in
 * the saved game's +5C-+5E by SaveGameSlot / LoadGameSlot */
extern int g_hm_wife_on;
extern uint16_t g_hm_wife_until;
/* g_hm_goddess_on: the Harvest Goddess's hearts (native/goddess.c) are on;
 * her hearts (0-1000), the day of the last offering that counted and her
 * flags are kept in the saved game's unused +26 to +2B by SaveGameSlot /
 * LoadGameSlot. g_hm_spring_offer: called with each item thrown into the
 * mountain's small pond, tile $F4 (items.c, the goddess's: event $17), before the game's own handling. */
extern int g_hm_goddess_on;
extern uint16_t g_hm_goddess_hearts, g_hm_goddess_day;
extern uint8_t g_hm_goddess_flags;
extern void (*g_hm_spring_offer)(uint8_t *ram, uint8_t item);
/* g_hm_feather_used: called when the Blue Feather is used (shown), by
 * ToolUsedBlueFeather (toolused.c), after the game's own work: the native
 * build's goddess takes it as a proposal at her pond. */
extern void (*g_hm_feather_used)(void);
/* g_hm_script_text: when set, called by the scripts' StartTextBox commands
 * (eventcmds.c) with the text's number before the game's box opens; if it
 * returns true, the game's box is not opened (the host shows its own) and
 * the script goes on. Unset (the default), the game runs as before. */
extern bool (*g_hm_script_text)(uint8_t *ram, uint16_t text);
/* g_hm_slot_picture: when set, called by UpdateSlotSprite (events.c) with
 * each event slot's address in ram[] before the slot's picture goes to its
 * game object, so the host can dress a slot as someone else (the
 * goddess's wedding, native/goddess.c). It may change the slot's sprite
 * (+16) and flip (+14) and set flag $04 (+01). Off: the game as it is. */
extern void (*g_hm_slot_picture)(uint8_t *ram, uint32_t slot);
/* g_hm_night_wife: when set, called by NightReset (day.c) just before
 * WifePregnancyAndChildren (done = false) and just after it (done = true),
 * so the host can let the night's wife routine count a wife the game does
 * not know about (the goddess's children, native/goddess.c). Off: the game
 * as it is. */
extern void (*g_hm_night_wife)(uint8_t *ram, bool done);
extern uint16_t (*g_hm_year_suffix)(uint16_t x, uint16_t rom_value);
/* the loops' own entries the host needs (not checked on their own) */
void hm_IntroDemoCheck(uint8_t *ram, HmRegs *r);    /* intro.c: the demo check */
void hm_DemoFrame(uint8_t *ram, HmRegs *r);     /* demo.c: a frame of each demo */
void hm_DemoPresentationFrame(uint8_t *ram, HmRegs *r);

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
void hm_GetCowPointer(uint8_t *ram, HmRegs *r);      /* animals.c */
void hm_GetChickenPointer(uint8_t *ram, HmRegs *r);
void hm_DivisionUnsigned(uint8_t *ram, HmRegs *r);    /* math.c */
void hm_RNGReturn0toA(uint8_t *ram, HmRegs *r);       /* rng.c */
void hm_SetWeatherFlags(uint8_t *ram, HmRegs *r);     /* weather.c */
void hm_WeatherTomorrow(uint8_t *ram, HmRegs *r);
void hm_GetTileIndex(uint8_t *ram, HmRegs *r);        /* farm.c */
void hm_EditTileOnMap(uint8_t *ram, HmRegs *r);       /* mapedit.c */
void hm_EditFarmTile(uint8_t *ram, HmRegs *r);
void hm_LoadDamageProbabilities(uint8_t *ram, HmRegs *r);
void hm_ClimateFarmDamageCheck(uint8_t *ram, HmRegs *r);
void hm_ClimateFarmDamagePrep(uint8_t *ram, HmRegs *r);
void hm_MonthlyFarmTilesCheck(uint8_t *ram, HmRegs *r);
void hm_NightlyFarmTilesCheck(uint8_t *ram, HmRegs *r);
void hm_CalculateFarmDevelopment(uint8_t *ram, HmRegs *r);
void hm_StartEnding(uint8_t *ram, HmRegs *r);
void hm_MapEvent_Farm(uint8_t *ram, HmRegs *r);
void hm_MapEvent_EndingSlideShow(uint8_t *ram, HmRegs *r);
void hm_MapEvent_PlayerHouse(uint8_t *ram, HmRegs *r);
void hm_StartMapEvents(uint8_t *ram, HmRegs *r);
void hm_MapEvent_IntroDays(uint8_t *ram, HmRegs *r);
void hm_MapEvent_Town(uint8_t *ram, HmRegs *r);
void hm_MapEvent_Barn(uint8_t *ram, HmRegs *r);
void hm_MapEvent_Crossroads(uint8_t *ram, HmRegs *r);
void hm_MapEvent_MountainCave(uint8_t *ram, HmRegs *r);
void hm_MapEvent_AnnGeneralStore(uint8_t *ram, HmRegs *r);
void hm_MapEvent_AnnHouse(uint8_t *ram, HmRegs *r);
void hm_MapEvent_EllenHouse1(uint8_t *ram, HmRegs *r);
void hm_MapEvent_EllenHouse2(uint8_t *ram, HmRegs *r);
void hm_MapEvent_DA4B_Map25Person(uint8_t *ram, HmRegs *r);
void hm_MapEvent_AnimalShop(uint8_t *ram, HmRegs *r);
void hm_MapEvent_EveHouse1(uint8_t *ram, HmRegs *r);
void hm_MapEvent_EveHouse2(uint8_t *ram, HmRegs *r);
void hm_MapEvent_Mountain(uint8_t *ram, HmRegs *r);
void hm_MapEvent_DD95_Map2BPerson(uint8_t *ram, HmRegs *r);
void hm_MapEvent_ElfTunnel(uint8_t *ram, HmRegs *r);
void hm_MapEvent_MariaHouse1(uint8_t *ram, HmRegs *r);
void hm_MapEvent_MariaHouse2(uint8_t *ram, HmRegs *r);
void hm_MapEvent_Church(uint8_t *ram, HmRegs *r);
void hm_MapEvent_NinaFlowerShop(uint8_t *ram, HmRegs *r);
void hm_MapEvent_NinaHouse(uint8_t *ram, HmRegs *r);
void hm_MapEvent_Beanstalk(uint8_t *ram, HmRegs *r);
void hm_MapEvent_E5A2_Map2C(uint8_t *ram, HmRegs *r);
void hm_MapEvent_Coop(uint8_t *ram, HmRegs *r);
void hm_MapEvent_E5CD_Maps2Dto30(uint8_t *ram, HmRegs *r);
void hm_MapEvent_FlowerFestival(uint8_t *ram, HmRegs *r);
void hm_MapEvent_HarvestFestival(uint8_t *ram, HmRegs *r);
void hm_MapEvent_StarryNightMap0A(uint8_t *ram, HmRegs *r);
void hm_MapEvent_StarryNightMap39(uint8_t *ram, HmRegs *r);
void hm_MapEvent_StarryNightMap14(uint8_t *ram, HmRegs *r);
void hm_MapEvent_NewYear(uint8_t *ram, HmRegs *r);
void hm_MapEvent_E6B2_Festival0B(uint8_t *ram, HmRegs *r);
void hm_MapEvent_ToolShedFamily(uint8_t *ram, HmRegs *r);
void hm_MapEvent_Nothing(uint8_t *ram, HmRegs *r);

void hm_CowEatsGrass(uint8_t *ram, HmRegs *r);
void hm_AddCowHappiness(uint8_t *ram, HmRegs *r);     /* animals.c */
void hm_CowFeedingAndStatus(uint8_t *ram, HmRegs *r);
void hm_AddNewChicken(uint8_t *ram, HmRegs *r);
void hm_AddNewCow(uint8_t *ram, HmRegs *r);
void hm_PlaceAnimalsOnMap(uint8_t *ram, HmRegs *r);
void hm_FindMostLovedName(uint8_t *ram, HmRegs *r);   /* people.c */
void hm_WifePregnancyAndChildren(uint8_t *ram, HmRegs *r);
void hm_LoadDateNames(uint8_t *ram, HmRegs *r);      /* calendar.c */
void hm_AddMoney(uint8_t *ram, HmRegs *r);            /* stock.c */
void hm_TvWeatherForecast(uint8_t *ram, HmRegs *r);          /* weather.c */
void hm_HaveLunch(uint8_t *ram, HmRegs *r);           /* stamina.c */
void hm_GetTileContent(uint8_t *ram, HmRegs *r);      /* tools.c */
void hm_GetTileProperty(uint8_t *ram, HmRegs *r);
void hm_CheckToolSuccess(uint8_t *ram, HmRegs *r);
void hm_CalculateTileInFront(uint8_t *ram, HmRegs *r);
void hm_TileAtOffsetFromPlayer(uint8_t *ram, HmRegs *r);
void hm_PreCheckToolSuccess(uint8_t *ram, HmRegs *r);
void hm_PreCheckToolSuccessLine(uint8_t *ram, HmRegs *r);
void hm_PreCheckToolSuccessSquare(uint8_t *ram, HmRegs *r);
void hm_ReadTileChange(uint8_t *ram, HmRegs *r);          /* mapedit.c */
void hm_DrawTileChange(uint8_t *ram, HmRegs *r);
void hm_SetTileTypeBlock(uint8_t *ram, HmRegs *r);
void hm_QueueTileGraphicsDMA(uint8_t *ram, HmRegs *r);
void hm_ChangeMapTile(uint8_t *ram, HmRegs *r);
void hm_StartMapAnimation(uint8_t *ram, HmRegs *r);
void hm_RanchMasteryCalculator(uint8_t *ram, HmRegs *r); /* ending.c */
void hm_AddProgrammedDMA(uint8_t *ram, HmRegs *r);    /* dma.c */
void hm_RemoveProgrammedDMA(uint8_t *ram, HmRegs *r);
void hm_DrawTilemapBlock2x2(uint8_t *ram, HmRegs *r);
void hm_CopyLetterToVram(uint8_t *ram, HmRegs *r);
void hm_LoadSpriteComponent(uint8_t *ram, HmRegs *r); /* objects.c */
void hm_LoadGameObjectComponents(uint8_t *ram, HmRegs *r);
void hm_CreateGameObject(uint8_t *ram, HmRegs *r);
void hm_SpawnMapObject(uint8_t *ram, HmRegs *r);
void hm_GetItemSprite(uint8_t *ram, HmRegs *r);   /* items.c */
void hm_PlayerState_00_StandWalkRun(uint8_t *ram, HmRegs *r);   /* playerstate.c */
void hm_ResetIdleAnimationTimer(uint8_t *ram, HmRegs *r);
void hm_PlayerState_1A_PutChildBack(uint8_t *ram, HmRegs *r);
void hm_PlayerState_17_PickUpFoal(uint8_t *ram, HmRegs *r);
void hm_PlayerState_18_PutDownFoal(uint8_t *ram, HmRegs *r);
void hm_PlayerState_16_ReleaseDog(uint8_t *ram, HmRegs *r);
void hm_PlayerState_15_BeforeFeelSick(uint8_t *ram, HmRegs *r);
void hm_PlayerState_14_FeelSick(uint8_t *ram, HmRegs *r);
void hm_PlayerState_0F_HoldRod(uint8_t *ram, HmRegs *r);
void hm_PlayerState_10_CastLine(uint8_t *ram, HmRegs *r);
void hm_PlayerState_11_WaitForBite(uint8_t *ram, HmRegs *r);
void hm_PlayerState_12_FishBites(uint8_t *ram, HmRegs *r);
void hm_PlayerState_13_ReelInCatch(uint8_t *ram, HmRegs *r);
void hm_PlayerState_1B_WhistleDog(uint8_t *ram, HmRegs *r);
void hm_PlayerState_0D_WhistleHorse(uint8_t *ram, HmRegs *r);
void hm_PlayerState_0C_ShowToolAndTime(uint8_t *ram, HmRegs *r);
void hm_PlayerState_0B_Tired(uint8_t *ram, HmRegs *r);
void hm_PlayerState_0A_UseTool(uint8_t *ram, HmRegs *r);
void hm_PlayerStandStill(uint8_t *ram, HmRegs *r);
void hm_PlayerWalk(uint8_t *ram, HmRegs *r);
void hm_PlayerRun(uint8_t *ram, HmRegs *r);
void hm_PlayerPushed(uint8_t *ram, HmRegs *r);
void hm_PlayerState_03_Jump(uint8_t *ram, HmRegs *r);
void hm_PlayerState_04_PickUp(uint8_t *ram, HmRegs *r);
void hm_PlayerState_05_Throw(uint8_t *ram, HmRegs *r);
void hm_PlayerState_08_Eat(uint8_t *ram, HmRegs *r);
void hm_PlayerEatAnimation(uint8_t *ram, HmRegs *r);
void hm_PlayerIdleAnimation(uint8_t *ram, HmRegs *r);
void hm_CheckMountainFinds(uint8_t *ram, HmRegs *r);
void hm_CheckDoors(uint8_t *ram, HmRegs *r);
void hm_LoadQueuedSpritePieces(uint8_t *ram, HmRegs *r);
void hm_UploadSoundProgram(uint8_t *ram, HmRegs *r);
void hm_UnusedCountInstrument(uint8_t *ram, HmRegs *r);
void hm_StartProgrammedDMA(uint8_t *ram, HmRegs *r);
void hm_InitializeScreenStatusVars(uint8_t *ram, HmRegs *r);
void hm_ManageGraphicPresets(uint8_t *ram, HmRegs *r);
void hm_ForceBlank(uint8_t *ram, HmRegs *r);
void hm_ResetForceBlank(uint8_t *ram, HmRegs *r);
void hm_SetBrightness(uint8_t *ram, HmRegs *r);
void hm_ZeroesVRAM(uint8_t *ram, HmRegs *r);
void hm_ZeroesOAM(uint8_t *ram, HmRegs *r);
void hm_UnusedHideSprites(uint8_t *ram, HmRegs *r);
void hm_ZeroesCGRAM(uint8_t *ram, HmRegs *r);
void hm_ZeroesPartialVRAM(uint8_t *ram, HmRegs *r);
void hm_StartLastPreparedDMA(uint8_t *ram, HmRegs *r);
void hm_CopyMapPictureToVRAM(uint8_t *ram, HmRegs *r);
void hm_BackgroundsManager(uint8_t *ram, HmRegs *r);
void hm_NMI_Interrupt(uint8_t *ram, HmRegs *r);   /* nmi.c */
void hm_COP_Interrupt(uint8_t *ram, HmRegs *r);
void hm_WaitForNMI(uint8_t *ram, HmRegs *r);
void hm_WaitForNMIATimes(uint8_t *ram, HmRegs *r);
void hm_ScreenFadeIn(uint8_t *ram, HmRegs *r);
void hm_ScreenFadeOut(uint8_t *ram, HmRegs *r);
void hm_PrepareSoundEffect(uint8_t *ram, HmRegs *r);   /* audio.c */
void hm_SetSong(uint8_t *ram, HmRegs *r);
void hm_PlayEffectWithSongInstrument(uint8_t *ram, HmRegs *r);
void hm_ChangeMusic(uint8_t *ram, HmRegs *r);
void hm_ToolUsedSound2(uint8_t *ram, HmRegs *r);   /* toolsound.c */
void hm_Event_ScreenFadeIn(uint8_t *ram, HmRegs *r);   /* eventcmds.c */
void hm_Event_ScreenFadeOut(uint8_t *ram, HmRegs *r);
void hm_Event_PlayEffectAndWait(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_04_Jingle(uint8_t *ram, HmRegs *r);   /* interact.c */
void hm_TakeToolFromShelf(uint8_t *ram, HmRegs *r);   /* items.c */
void hm_RunItemRoutine(uint8_t *ram, HmRegs *r);
void hm_RunItemsOnMap(uint8_t *ram, HmRegs *r);
void hm_EventInstructionInterpreter(uint8_t *ram, HmRegs *r);   /* events.c */
void hm_RunEventSlots(uint8_t *ram, HmRegs *r);
void hm_PlayerState_1C_SwapTools(uint8_t *ram, HmRegs *r);   /* playerstate.c */
void hm_PlayerAction_35(uint8_t *ram, HmRegs *r);   /* actions.c */
void hm_PlayerFrame(uint8_t *ram, HmRegs *r);   /* playerstate.c */
void hm_LoadAndShowMap(uint8_t *ram, HmRegs *r);
void hm_EnterMap(uint8_t *ram, HmRegs *r);
void hm_SetScreenTransition(uint8_t *ram, HmRegs *r);
void hm_NightReset(uint8_t *ram, HmRegs *r);      /* day.c */
void hm_UpdateTime(uint8_t *ram, HmRegs *r);
void hm_FirstNightReset(uint8_t *ram, HmRegs *r);
void hm_SetPlayerName(uint8_t *ram, HmRegs *r);   /* names.c */
void hm_SetCowNameBought(uint8_t *ram, HmRegs *r);
void hm_SetCowNameBorn(uint8_t *ram, HmRegs *r);
void hm_SetDogName(uint8_t *ram, HmRegs *r);
void hm_SetHorseName(uint8_t *ram, HmRegs *r);
void hm_NamesScreen(uint8_t *ram, HmRegs *r);
void hm_NamesScreenFrame(uint8_t *ram, HmRegs *r);
void hm_SetKid1Name(uint8_t *ram, HmRegs *r);
void hm_SetKid2Name(uint8_t *ram, HmRegs *r);
void hm_SpawnAfterLoad(uint8_t *ram, HmRegs *r);  /* game.c */
void hm_GameLoop(uint8_t *ram, HmRegs *r);
void hm_FirstDemo(uint8_t *ram, HmRegs *r);    /* demo.c */
void hm_DemoPresentation(uint8_t *ram, HmRegs *r);
void hm_IntroScreen(uint8_t *ram, HmRegs *r);     /* intro.c */
void hm_IntroFrame(uint8_t *ram, HmRegs *r);
void hm_ParentsSceneFrame(uint8_t *ram, HmRegs *r);
void hm_NewFileScreenFrame(uint8_t *ram, HmRegs *r);
void hm_ContinueFileScreen(uint8_t *ram, HmRegs *r);
void hm_ContinueFileScreenFrame(uint8_t *ram, HmRegs *r);
void hm_StartFromLoad(uint8_t *ram, HmRegs *r);
void hm_StartFromNewGame(uint8_t *ram, HmRegs *r);
void hm_UnusedRewritePlayerName(uint8_t *ram, HmRegs *r);
void hm_RESET(uint8_t *ram, HmRegs *r);          /* reset.c */
void hm_UnusedEndlessLoop(uint8_t *ram, HmRegs *r);     /* day.c */   /* transition.c */
void hm_PlayerInteractionSelector(uint8_t *ram, HmRegs *r);
void hm_ItemObject_2A_Sickle(uint8_t *ram, HmRegs *r);
void hm_ItemObject_2B_Hoe(uint8_t *ram, HmRegs *r);
void hm_ItemObject_2C_Hammer(uint8_t *ram, HmRegs *r);
void hm_ItemObject_2D_Axe(uint8_t *ram, HmRegs *r);
void hm_ItemObject_2E_CornSeeds(uint8_t *ram, HmRegs *r);
void hm_ItemObject_2F_TomatoSeeds(uint8_t *ram, HmRegs *r);
void hm_ItemObject_30_PotatoSeeds(uint8_t *ram, HmRegs *r);
void hm_ItemObject_31_TurnipSeeds(uint8_t *ram, HmRegs *r);
void hm_ItemObject_32_CowMedicine(uint8_t *ram, HmRegs *r);
void hm_ItemObject_33_MiraclePotion(uint8_t *ram, HmRegs *r);
void hm_ItemObject_34_Bell(uint8_t *ram, HmRegs *r);
void hm_ItemObject_35_GrassSeeds(uint8_t *ram, HmRegs *r);
void hm_ItemObject_36_Paint(uint8_t *ram, HmRegs *r);
void hm_ItemObject_37_Milker(uint8_t *ram, HmRegs *r);
void hm_ItemObject_38_Brush(uint8_t *ram, HmRegs *r);
void hm_ItemObject_39_WateringCan(uint8_t *ram, HmRegs *r);
void hm_ItemObject_3A_GoldSickle(uint8_t *ram, HmRegs *r);
void hm_ItemObject_3B_GoldHoe(uint8_t *ram, HmRegs *r);
void hm_ItemObject_3C_GoldHammer(uint8_t *ram, HmRegs *r);
void hm_ItemObject_3D_GoldAxe(uint8_t *ram, HmRegs *r);
void hm_ItemObject_3E_Sprinkler(uint8_t *ram, HmRegs *r);
void hm_ItemObject_3F_MagicBeans(uint8_t *ram, HmRegs *r);
void hm_ItemObject_40_GemSeed(uint8_t *ram, HmRegs *r);
void hm_ItemObject_41_BlueFeather(uint8_t *ram, HmRegs *r);
void hm_ItemObject_42_ChickenFeed(uint8_t *ram, HmRegs *r);
void hm_ItemObject_43_CowFeed(uint8_t *ram, HmRegs *r);
void hm_CopySpritePiecesToVRAM(uint8_t *ram, HmRegs *r);
void hm_UpdateGraphics(uint8_t *ram, HmRegs *r);
void hm_AutoMapScrolling(uint8_t *ram, HmRegs *r);
void hm_LoadDialogueBox(uint8_t *ram, HmRegs *r);
void hm_DrawTileBlock3x2(uint8_t *ram, HmRegs *r);
void hm_DrawNameEntryBlanks(uint8_t *ram, HmRegs *r);
void hm_PutToolOnShelf(uint8_t *ram, HmRegs *r);
void hm_DrawScreenBar(uint8_t *ram, HmRegs *r);
void hm_DrawScreenBars(uint8_t *ram, HmRegs *r);
void hm_DrawSaveFileLine(uint8_t *ram, HmRegs *r);
void hm_PrepareOAMData(uint8_t *ram, HmRegs *r);
void hm_MovePlayer(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_0F_Tv(uint8_t *ram, HmRegs *r);   /* actions.c */
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
void hm_PutItemDown(uint8_t *ram, HmRegs *r);
void hm_ItemAnimalSteps(uint8_t *ram, HmRegs *r);
void hm_ItemFoodSteps(uint8_t *ram, HmRegs *r);
void hm_ItemPlaceableSteps(uint8_t *ram, HmRegs *r);
void hm_ItemFodderSteps(uint8_t *ram, HmRegs *r);
void hm_ItemCommonSteps(uint8_t *ram, HmRegs *r);
void hm_ItemShopGoodsSteps(uint8_t *ram, HmRegs *r);
void hm_FoodAskToEat(uint8_t *ram, HmRegs *r);
void hm_FoodEatAnswer(uint8_t *ram, HmRegs *r);
void hm_PlaceableCarried(uint8_t *ram, HmRegs *r);
void hm_PlaceableLands(uint8_t *ram, HmRegs *r);
void hm_PlaceableGone(uint8_t *ram, HmRegs *r);
void hm_AnimalSetDown(uint8_t *ram, HmRegs *r);
void hm_FodderIntoFeedBox(uint8_t *ram, HmRegs *r);
void hm_ItemAskToSell(uint8_t *ram, HmRegs *r);
void hm_ItemSellAnswer(uint8_t *ram, HmRegs *r);
void hm_ItemTakeInHand(uint8_t *ram, HmRegs *r);
void hm_ItemFinished(uint8_t *ram, HmRegs *r);
void hm_ItemCarried(uint8_t *ram, HmRegs *r);
void hm_AnimalCarried(uint8_t *ram, HmRegs *r);
void hm_ItemLands(uint8_t *ram, HmRegs *r);
void hm_ItemRemoveMapObject(uint8_t *ram, HmRegs *r);
void hm_ItemWaitMapAnimation(uint8_t *ram, HmRegs *r);
void hm_ItemObject_01_Mushroom(uint8_t *ram, HmRegs *r);
void hm_ItemObject_02_PoisonMushroom(uint8_t *ram, HmRegs *r);
void hm_ItemObject_03_WildGrapes(uint8_t *ram, HmRegs *r);
void hm_ItemObject_04_TropicalFruit(uint8_t *ram, HmRegs *r);
void hm_ItemObject_05_FullmoonBerry(uint8_t *ram, HmRegs *r);
void hm_ItemObject_06_Cake(uint8_t *ram, HmRegs *r);
void hm_ItemObject_07_Fish(uint8_t *ram, HmRegs *r);
void hm_ItemObject_08_PowerBerry(uint8_t *ram, HmRegs *r);
void hm_ItemObject_09_Weed(uint8_t *ram, HmRegs *r);
void hm_ItemObject_0A_FallWeed(uint8_t *ram, HmRegs *r);
void hm_ItemObject_0D_Stone(uint8_t *ram, HmRegs *r);
void hm_ItemObject_0E_SnowyStone(uint8_t *ram, HmRegs *r);
void hm_ItemObject_0F_Fence(uint8_t *ram, HmRegs *r);
void hm_ItemObject_10_Corn(uint8_t *ram, HmRegs *r);
void hm_ItemObject_11_Tomato(uint8_t *ram, HmRegs *r);
void hm_ItemObject_12_Potato(uint8_t *ram, HmRegs *r);
void hm_ItemObject_13_Turnip(uint8_t *ram, HmRegs *r);
void hm_ItemObject_14_Egg(uint8_t *ram, HmRegs *r);
void hm_ItemObject_15_SmallMilk(uint8_t *ram, HmRegs *r);
void hm_ItemObject_16_MediumMilk(uint8_t *ram, HmRegs *r);
void hm_ItemObject_17_LargeMilk(uint8_t *ram, HmRegs *r);
void hm_ItemObject_18_Herb(uint8_t *ram, HmRegs *r);
void hm_ItemObject_19_Flower(uint8_t *ram, HmRegs *r);
void hm_ItemObject_1A_Fodder(uint8_t *ram, HmRegs *r);
void hm_ItemObject_1B_ShopLilacPerfume(uint8_t *ram, HmRegs *r);
void hm_ItemObject_1C_ShopRosePerfume(uint8_t *ram, HmRegs *r);
void hm_ItemObject_1D_ShopVioletPerfume(uint8_t *ram, HmRegs *r);
void hm_ItemObject_1E_FestivalEgg1(uint8_t *ram, HmRegs *r);
void hm_ItemObject_1F_FestivalEgg2(uint8_t *ram, HmRegs *r);
void hm_ItemObject_20_FestivalEgg3(uint8_t *ram, HmRegs *r);
void hm_ItemObject_21_FestivalEgg4(uint8_t *ram, HmRegs *r);
void hm_ItemObject_22_FestivalEgg5(uint8_t *ram, HmRegs *r);
void hm_ItemObject_23_Nothing(uint8_t *ram, HmRegs *r);
void hm_ItemObject_24_WeatherVane(uint8_t *ram, HmRegs *r);
void hm_ItemObject_25_Hen(uint8_t *ram, HmRegs *r);
void hm_ItemObject_26_Chick(uint8_t *ram, HmRegs *r);
void hm_ItemObject_27_Nothing(uint8_t *ram, HmRegs *r);
void hm_ItemObject_28_Nothing(uint8_t *ram, HmRegs *r);
void hm_ItemObject_29_Mole(uint8_t *ram, HmRegs *r);
void hm_ItemObject_44_ShopGrassSeeds(uint8_t *ram, HmRegs *r);
void hm_ItemObject_45_ShopCornSeeds(uint8_t *ram, HmRegs *r);
void hm_ItemObject_46_ShopTomatoSeeds(uint8_t *ram, HmRegs *r);
void hm_ItemObject_47_ShopPotatoSeeds(uint8_t *ram, HmRegs *r);
void hm_ItemObject_48_ShopTurnipSeeds(uint8_t *ram, HmRegs *r);
void hm_ItemObject_49_ShopFlower(uint8_t *ram, HmRegs *r);
void hm_ItemObject_4A_ShopPaint(uint8_t *ram, HmRegs *r);
void hm_ItemObject_4B_ShopMilker(uint8_t *ram, HmRegs *r);
void hm_ItemObject_4C_ShopBrush(uint8_t *ram, HmRegs *r);
void hm_ItemObject_4D_ShopGoldSickle(uint8_t *ram, HmRegs *r);
void hm_ItemObject_4E_ShopGoldHoe(uint8_t *ram, HmRegs *r);
void hm_ItemObject_4F_ShopGoldHammer(uint8_t *ram, HmRegs *r);
void hm_ItemObject_50_ShopGoldAxe(uint8_t *ram, HmRegs *r);
void hm_ItemObject_51_ShopSprinkler(uint8_t *ram, HmRegs *r);
void hm_ItemObject_52_ShopCowMedicine(uint8_t *ram, HmRegs *r);
void hm_ItemObject_53_ShopMiraclePotion(uint8_t *ram, HmRegs *r);
void hm_ItemObject_54_ShopChickenFeed(uint8_t *ram, HmRegs *r);
void hm_ItemObject_55_ShopCowFeed(uint8_t *ram, HmRegs *r);
void hm_ItemObject_56_ShopCake(uint8_t *ram, HmRegs *r);
void hm_ItemObject_58_LilacPerfume(uint8_t *ram, HmRegs *r);
void hm_ItemObject_59_RosePerfume(uint8_t *ram, HmRegs *r);
void hm_ItemObject_5A_VioletPerfume(uint8_t *ram, HmRegs *r);
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
void hm_ToolUsedUnusedTool(uint8_t *ram, HmRegs *r);
void hm_SetEventSlotPointer(uint8_t *ram, HmRegs *r);         /* events.c */
void hm_ResetCurrentEventSlotLong(uint8_t *ram, HmRegs *r);
void hm_StartScriptAtTileInFront(uint8_t *ram, HmRegs *r);
void hm_ResetCurrentEventSlotShort(uint8_t *ram, HmRegs *r);
void hm_StartEventScriptLong(uint8_t *ram, HmRegs *r);
void hm_StartEventScript(uint8_t *ram, HmRegs *r);
void hm_Event_ChickenFrame(uint8_t *ram, HmRegs *r);  /* eventcmds.c */
void hm_Event_DogFrame(uint8_t *ram, HmRegs *r);
void hm_Event_CowFrame(uint8_t *ram, HmRegs *r);
void hm_Event_SetWantedSong(uint8_t *ram, HmRegs *r);
void hm_Event_TimeRunning(uint8_t *ram, HmRegs *r);
void hm_Event_TimeStopped(uint8_t *ram, HmRegs *r);
void hm_Event_SetHour(uint8_t *ram, HmRegs *r);
void hm_Event_DoNothing(uint8_t *ram, HmRegs *r);
void hm_Event_SetPlayerPosition(uint8_t *ram, HmRegs *r);
void hm_Event_SetTransitionDest(uint8_t *ram, HmRegs *r);
void hm_Event_SetPlayerDirection(uint8_t *ram, HmRegs *r);
void hm_Event_DisablePlayerControl(uint8_t *ram, HmRegs *r);
void hm_Event_StartOtherSlot(uint8_t *ram, HmRegs *r);
void hm_Event_SetSceneFlag(uint8_t *ram, HmRegs *r);
void hm_Event_WaitForSceneFlag(uint8_t *ram, HmRegs *r);
void hm_Event_MoveSlot(uint8_t *ram, HmRegs *r);
void hm_Event_FreeOwnSlot(uint8_t *ram, HmRegs *r);
void hm_Event_EnablePlayerControl(uint8_t *ram, HmRegs *r);
void hm_Event_JumpToInstruction(uint8_t *ram, HmRegs *r);
void hm_Event_WaitFrames(uint8_t *ram, HmRegs *r);
void hm_Event_JumpIfFlagSet(uint8_t *ram, HmRegs *r);
void hm_Event_JumpIfEquals(uint8_t *ram, HmRegs *r);
void hm_Event_JumpIfBetween(uint8_t *ram, HmRegs *r);
void hm_Event_GetRNG(uint8_t *ram, HmRegs *r);
void hm_Event_JumpIfEqualsRNG(uint8_t *ram, HmRegs *r);
void hm_Event_SetPlayerAnimation(uint8_t *ram, HmRegs *r);
void hm_Event_PickUpMole(uint8_t *ram, HmRegs *r);
void hm_Event_WanderInArea(uint8_t *ram, HmRegs *r);
void hm_Event_PickUpPowerBerry(uint8_t *ram, HmRegs *r);
void hm_Event_JumpIfBetweenValueLong(uint8_t *ram, HmRegs *r);
void hm_Event_PickUpFish(uint8_t *ram, HmRegs *r);
void hm_Event_RunPlayer(uint8_t *ram, HmRegs *r);
void hm_Event_WalkPlayer(uint8_t *ram, HmRegs *r);
void hm_Event_WanderFreely(uint8_t *ram, HmRegs *r);
void hm_Event_AddToByteClamped(uint8_t *ram, HmRegs *r);
void hm_Event_JumpIfEqualsValueLong(uint8_t *ram, HmRegs *r);
void hm_Event_JumpIfBetweenValue(uint8_t *ram, HmRegs *r);
void hm_Event_ChangeMoney(uint8_t *ram, HmRegs *r);
void hm_Event_SetSlotSprite(uint8_t *ram, HmRegs *r);
void hm_Event_AddToWordMax999(uint8_t *ram, HmRegs *r);
void hm_Event_JumpIfEqualsValue(uint8_t *ram, HmRegs *r);
void hm_Event_SetFlag(uint8_t *ram, HmRegs *r);
void hm_Event_SetValueLong(uint8_t *ram, HmRegs *r);
void hm_Event_MapScrolling(uint8_t *ram, HmRegs *r);
void hm_Event_StoreValue(uint8_t *ram, HmRegs *r);
void hm_Event_SetValueByte(uint8_t *ram, HmRegs *r);
void hm_Event_JumpIfAnswer(uint8_t *ram, HmRegs *r);
void hm_Event_TransitionToMap(uint8_t *ram, HmRegs *r);
void hm_Event_ChangeItemOnHand(uint8_t *ram, HmRegs *r);
void hm_Event_SetItemOnHand(uint8_t *ram, HmRegs *r);
void hm_Event_WaitUntilSkippable(uint8_t *ram, HmRegs *r);
void hm_Event_TransitionToHouse(uint8_t *ram, HmRegs *r);
void hm_Event_MakeSlotPassable(uint8_t *ram, HmRegs *r);
void hm_Event_IgnoreTileProperties(uint8_t *ram, HmRegs *r);
void hm_Event_UseTileProperties(uint8_t *ram, HmRegs *r);
void hm_Event_DisableTools(uint8_t *ram, HmRegs *r);
void hm_Event_DropItemAnimation(uint8_t *ram, HmRegs *r);
void hm_StartTextBox(uint8_t *ram, HmRegs *r);       /* text.c */
void hm_ClearTextBox(uint8_t *ram, HmRegs *r);
void hm_CloseTextBox(uint8_t *ram, HmRegs *r);
void hm_DrawTextBoxArrow(uint8_t *ram, HmRegs *r);
void hm_BlinkTextBoxArrow(uint8_t *ram, HmRegs *r);
void hm_TextBoxArrow(uint8_t *ram, HmRegs *r);
void hm_FindLetterGraphics(uint8_t *ram, HmRegs *r);
void hm_DrawLetter(uint8_t *ram, HmRegs *r);
void hm_AdvanceLetterCursor(uint8_t *ram, HmRegs *r);
void hm_BlinkTextCursor(uint8_t *ram, HmRegs *r);
void hm_PrintDigit(uint8_t *ram, HmRegs *r);
void hm_SoundChipBusy(uint8_t *ram, HmRegs *r);       /* audio.c */
void hm_SoundChipBusyOrPlaying(uint8_t *ram, HmRegs *r);
void hm_SoundStartCommand(uint8_t *ram, HmRegs *r);
void hm_PlaySoundEffect(uint8_t *ram, HmRegs *r);
void hm_PlaySoundEffect0A(uint8_t *ram, HmRegs *r);
void hm_SoundCommand0C(uint8_t *ram, HmRegs *r);
void hm_SoundCommand0B(uint8_t *ram, HmRegs *r);
void hm_ToolUsedWateringCan(uint8_t *ram, HmRegs *r); /* toolused.c */
void hm_ToolUsedSprinkler(uint8_t *ram, HmRegs *r);
void hm_UseToolInHand(uint8_t *ram, HmRegs *r);
void hm_CheckToolSuccessForSound(uint8_t *ram, HmRegs *r);         /* tools.c */
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
void hm_LoadMapRowBelow(uint8_t *ram, HmRegs *r);         /* scroll.c */
void hm_LoadMapRowAbove(uint8_t *ram, HmRegs *r);
void hm_LoadMapColumnRight(uint8_t *ram, HmRegs *r);
void hm_LoadMapColumnLeft(uint8_t *ram, HmRegs *r);
void hm_UpdateBGOffset(uint8_t *ram, HmRegs *r);
void hm_ScrollCameraUp(uint8_t *ram, HmRegs *r);
void hm_ScrollCameraRight(uint8_t *ram, HmRegs *r);
void hm_ScrollCameraLeft(uint8_t *ram, HmRegs *r);
void hm_PlaceCameraOnMapEntry(uint8_t *ram, HmRegs *r);
void hm_MoveSecondBackground(uint8_t *ram, HmRegs *r);
void hm_SetTimeOfDayPalette(uint8_t *ram, HmRegs *r);   /* palette.c */
void hm_TransitionTimeOfDayPalettes(uint8_t *ram, HmRegs *r);
void hm_FinishTimeOfDayPalette(uint8_t *ram, HmRegs *r);
void hm_ClearTimeOfDayPalette(uint8_t *ram, HmRegs *r);
void hm_SetPaletteColour(uint8_t *ram, HmRegs *r);
void hm_SetPaletteColourNoFade(uint8_t *ram, HmRegs *r);
void hm_LoadFirstHalfPaletteToWRAM(uint8_t *ram, HmRegs *r);
void hm_LoadSecondHalfPaletteToWRAM(uint8_t *ram, HmRegs *r);
void hm_CustomiseSpritePalette(uint8_t *ram, HmRegs *r);
void hm_PrepareNightPalette(uint8_t *ram, HmRegs *r);
void hm_ChangePaletteByTime(uint8_t *ram, HmRegs *r);
void hm_PrepareSpritePalette(uint8_t *ram, HmRegs *r);
void hm_SetPaletteToLoad(uint8_t *ram, HmRegs *r);
void hm_Event_TimeOfDayPalette(uint8_t *ram, HmRegs *r);
void hm_CollideTileCorner(uint8_t *ram, HmRegs *r);          /* collision.c */
void hm_CollideMap(uint8_t *ram, HmRegs *r);
void hm_CollideEventObjects(uint8_t *ram, HmRegs *r);
void hm_CollidePlayer(uint8_t *ram, HmRegs *r);
void hm_StartToolAnimation(uint8_t *ram, HmRegs *r);     /* toolanim.c */
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
void hm_ToolAnimationUnusedTool(uint8_t *ram, HmRegs *r);
void hm_PressAOnTile(uint8_t *ram, HmRegs *r);       /* interact.c */
void hm_PlayerMovementInputs(uint8_t *ram, HmRegs *r); /* playerinput.c */
void hm_ReadEventPad(uint8_t *ram, HmRegs *r);
void hm_ReadNameCursorCell(uint8_t *ram, HmRegs *r); /* menus.c */
void hm_NameCursorPosition(uint8_t *ram, HmRegs *r);
void hm_DrawNameLetter(uint8_t *ram, HmRegs *r);
void hm_NameMenuKeyDown(uint8_t *ram, HmRegs *r);
void hm_NameMenuKeyUp(uint8_t *ram, HmRegs *r);
void hm_NameMenuKeyLeft(uint8_t *ram, HmRegs *r);
void hm_NameMenuKeyRight(uint8_t *ram, HmRegs *r);
void hm_NameMenuKeyB(uint8_t *ram, HmRegs *r);
void hm_NameMenuKeyA(uint8_t *ram, HmRegs *r);
void hm_NameMenuInputs(uint8_t *ram, HmRegs *r);
void hm_SkipEventScene(uint8_t *ram, HmRegs *r);
void hm_MenuEventPad(uint8_t *ram, HmRegs *r);
void hm_MenuInputs(uint8_t *ram, HmRegs *r);
void hm_TextBoxAutoClose(uint8_t *ram, HmRegs *r);
void hm_InputTypeSelector(uint8_t *ram, HmRegs *r);
void hm_DrawWholeMap(uint8_t *ram, HmRegs *r);   /* mapedit.c */
void hm_Event_PutDownCarried(uint8_t *ram, HmRegs *r); /* eventcmds.c */
void hm_Event_Cmd51_PutDownCarriedQuietly(uint8_t *ram, HmRegs *r);
void hm_Event_Cmd52_PickUpChild1(uint8_t *ram, HmRegs *r);
void hm_Event_Cmd53_PickUpChild2(uint8_t *ram, HmRegs *r);
void hm_Event_Cmd54_AutoCloseTextBox(uint8_t *ram, HmRegs *r);
void hm_Event_Cmd55_JumpIfBitClear(uint8_t *ram, HmRegs *r);
void hm_Event_Cmd56_UseToolInHand(uint8_t *ram, HmRegs *r);
void hm_Event_Cmd57_ChangeStamina(uint8_t *ram, HmRegs *r);
void hm_Event_Cmd58_SetFarmTile(uint8_t *ram, HmRegs *r);
void hm_Event_Cmd59_SwapTools(uint8_t *ram, HmRegs *r);
void hm_Event_HorseFrame(uint8_t *ram, HmRegs *r);
void hm_DivisionSigned(uint8_t *ram, HmRegs *r);   /* math.c */
void hm_DecompressTileMap(uint8_t *ram, HmRegs *r); /* decompress.c */
void hm_Event_ResetFlag(uint8_t *ram, HmRegs *r);
void hm_Event_EditTileOnMap(uint8_t *ram, HmRegs *r);
void hm_Event_StartTextBox(uint8_t *ram, HmRegs *r);
void hm_Event_StartTextBoxCopy(uint8_t *ram, HmRegs *r);
void hm_Event_ReturnToolsToShed(uint8_t *ram, HmRegs *r);
void hm_ReadJoypad(uint8_t *ram, HmRegs *r);         /* joypad.c */
void hm_UnusedBarnCoopCheck(uint8_t *ram, HmRegs *r); /* animals.c */
void hm_UnusedBarnCheck(uint8_t *ram, HmRegs *r);
void hm_ToolUsedSound1(uint8_t *ram, HmRegs *r);     /* toolsound.c */
void hm_CopyCurrentMapToFarmMap(uint8_t *ram, HmRegs *r);  /* farm.c */
void hm_LoadDefaultFarmMap(uint8_t *ram, HmRegs *r);
void hm_LoadMap(uint8_t *ram, HmRegs *r);
void hm_ClearAllEventSlots(uint8_t *ram, HmRegs *r);     /* events.c */
void hm_MultiplicationUnsigned(uint8_t *ram, HmRegs *r);   /* math.c */
void hm_ChangeMapTileNoVram(uint8_t *ram, HmRegs *r); /* maptiles.c */
void hm_SetTileTypesFromChange(uint8_t *ram, HmRegs *r);
void hm_DrawShelfItem(uint8_t *ram, HmRegs *r);
void hm_RunMapExtras(uint8_t *ram, HmRegs *r);
void hm_ReplaceTilesFarm(uint8_t *ram, HmRegs *r);
void hm_ReplaceTilesMountain(uint8_t *ram, HmRegs *r);
void hm_ReplaceTilesHouse1(uint8_t *ram, HmRegs *r);
void hm_ReplaceTilesHouse2(uint8_t *ram, HmRegs *r);
void hm_ReplaceTilesHouse3(uint8_t *ram, HmRegs *r);
void hm_ReplaceTilesFlowerShop(uint8_t *ram, HmRegs *r);
void hm_ReplaceTilesGeneralStore(uint8_t *ram, HmRegs *r);
void hm_ReplaceTilesAnimalShop(uint8_t *ram, HmRegs *r);
void hm_ReplaceTilesToolShed(uint8_t *ram, HmRegs *r);
void hm_ReplaceTilesBarn(uint8_t *ram, HmRegs *r);
void hm_ReplaceTilesCoop(uint8_t *ram, HmRegs *r);
void hm_ReplaceTilesMountainCave(uint8_t *ram, HmRegs *r);
void hm_ReplaceTilesElfTunnel(uint8_t *ram, HmRegs *r);
void hm_ReplaceTilesMap37(uint8_t *ram, HmRegs *r);
void hm_ReplaceTilesNothing(uint8_t *ram, HmRegs *r);
void hm_Multiply16(uint8_t *ram, HmRegs *r);
void hm_SoundCommand09IfBusy(uint8_t *ram, HmRegs *r);
void hm_SoundCommand07(uint8_t *ram, HmRegs *r);
void hm_SoundCommand08(uint8_t *ram, HmRegs *r);
void hm_DropInstrumentsFrom(uint8_t *ram, HmRegs *r);
void hm_FadeOutSong(uint8_t *ram, HmRegs *r);
void hm_StartSong(uint8_t *ram, HmRegs *r);
void hm_FadeOutSongSlowly(uint8_t *ram, HmRegs *r);
void hm_PickMapMusic(uint8_t *ram, HmRegs *r);
void hm_PickMapMusicAnyHour(uint8_t *ram, HmRegs *r);
void hm_LoadEffectInstrument(uint8_t *ram, HmRegs *r);
void hm_SendSongData(uint8_t *ram, HmRegs *r);
void hm_LoadSongInstruments(uint8_t *ram, HmRegs *r);
void hm_LoadDefaultInstruments(uint8_t *ram, HmRegs *r);
void hm_SendSoundData(uint8_t *ram, HmRegs *r);
void hm_GetFenceFromStack(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_06_MapAnimation11(uint8_t *ram, HmRegs *r);
void hm_PlayerAction_07_MapAnimation12(uint8_t *ram, HmRegs *r);
void hm_AffectionToHearts(uint8_t *ram, HmRegs *r);
void hm_GetBackgroundData(uint8_t *ram, HmRegs *r);
void hm_ClearItemRecords(uint8_t *ram, HmRegs *r);
void hm_ActionNothing(uint8_t *ram, HmRegs *r);
void hm_ShippingScene(uint8_t *ram, HmRegs *r);       /* shipping.c */
void hm_ShippingSceneDialogue(uint8_t *ram, HmRegs *r);
void hm_ReleaseSpriteSlot(uint8_t *ram, HmRegs *r);
void hm_CheckSpritePiecesLoaded(uint8_t *ram, HmRegs *r);
void hm_FreeSpritePieces(uint8_t *ram, HmRegs *r);
void hm_AnimateGameObjects(uint8_t *ram, HmRegs *r);
void hm_GetOBJOrderToDraw(uint8_t *ram, HmRegs *r);
void hm_ClearOAMCopy(uint8_t *ram, HmRegs *r);
void hm_ClearOAMCopyAgain(uint8_t *ram, HmRegs *r);
void hm_InitializeOBJs(uint8_t *ram, HmRegs *r);
void hm_CreatePlayerObject(uint8_t *ram, HmRegs *r);
void hm_UpdatePlayerObject(uint8_t *ram, HmRegs *r);
void hm_ReadPlayerObjectTimer(uint8_t *ram, HmRegs *r);
void hm_DestroyGameObject(uint8_t *ram, HmRegs *r);
void hm_SetSlotAnimation(uint8_t *ram, HmRegs *r);
void hm_FreeEventSlot(uint8_t *ram, HmRegs *r);
void hm_Event_DestroyOwnObject(uint8_t *ram, HmRegs *r);
void hm_Event_FreeOtherSlot(uint8_t *ram, HmRegs *r);
void hm_Event_SetStandingAnimation(uint8_t *ram, HmRegs *r);
void hm_Event_SetSlotAnimation(uint8_t *ram, HmRegs *r);
void hm_Event_SetSlotAnimationPlayerDir(uint8_t *ram, HmRegs *r);
void hm_Event_PlaceSlot(uint8_t *ram, HmRegs *r);
void hm_Event_FacePlayerIfSpokenTo(uint8_t *ram, HmRegs *r);
void hm_MoveGameObject(uint8_t *ram, HmRegs *r);
void hm_ChangeGameObjectSprite(uint8_t *ram, HmRegs *r);
void hm_StepSlot(uint8_t *ram, HmRegs *r);
void hm_WanderSlot(uint8_t *ram, HmRegs *r);
void hm_UpdateSlotSprite(uint8_t *ram, HmRegs *r);
void hm_Event_ChangeToWantedSong(uint8_t *ram, HmRegs *r);
void hm_Event_StartMapAnimation(uint8_t *ram, HmRegs *r);
void hm_Event_TalkIfSpokenTo(uint8_t *ram, HmRegs *r);
void hm_Event_TalkOrTakeShownItem(uint8_t *ram, HmRegs *r);
void hm_ReadGameObject(uint8_t *ram, HmRegs *r);
void hm_InvertIfFlippedX(uint8_t *ram, HmRegs *r);
void hm_InvertIfFlippedY(uint8_t *ram, HmRegs *r);
void hm_ClearSpriteDataTables(uint8_t *ram, HmRegs *r);
void hm_SnowFootprints(uint8_t *ram, HmRegs *r);
void hm_RunMapAnimation(uint8_t *ram, HmRegs *r);
void hm_FreeMapObject(uint8_t *ram, HmRegs *r);
void hm_StartColourAnimation(uint8_t *ram, HmRegs *r); /* coloranim.c */
void hm_RunColourAnimations(uint8_t *ram, HmRegs *r);
void hm_StopAllColourAnimations(uint8_t *ram, HmRegs *r);
void hm_StopColourAnimationsFrom(uint8_t *ram, HmRegs *r);
void hm_StopColourAnimation(uint8_t *ram, HmRegs *r);
void hm_StartMapColourAnimations(uint8_t *ram, HmRegs *r);
void hm_Colours_FarmSpringSummer_Day(uint8_t *ram, HmRegs *r);
void hm_Colours_FarmSpringSummer_Afternoon(uint8_t *ram, HmRegs *r);
void hm_Colours_FarmSpringSummer_Evening(uint8_t *ram, HmRegs *r);
void hm_Colours_FarmSpringSummer_Night(uint8_t *ram, HmRegs *r);
void hm_Colours_FarmAutumn_Day(uint8_t *ram, HmRegs *r);
void hm_Colours_FarmAutumn_Afternoon(uint8_t *ram, HmRegs *r);
void hm_Colours_FarmAutumn_Evening(uint8_t *ram, HmRegs *r);
void hm_Colours_FarmAutumn_Night(uint8_t *ram, HmRegs *r);
void hm_Colours_Map2A(uint8_t *ram, HmRegs *r);
void hm_Colours_House(uint8_t *ram, HmRegs *r);
void hm_Colours_Map18(uint8_t *ram, HmRegs *r);
void hm_Colours_Church(uint8_t *ram, HmRegs *r);
void hm_Colours_Map1D(uint8_t *ram, HmRegs *r);
void hm_Colours_Map1F(uint8_t *ram, HmRegs *r);
void hm_Colours_Map23(uint8_t *ram, HmRegs *r);
void hm_Colours_Map24(uint8_t *ram, HmRegs *r);
void hm_Colours_TownSpring_Day(uint8_t *ram, HmRegs *r);
void hm_Colours_TownSpring_Afternoon(uint8_t *ram, HmRegs *r);
void hm_Colours_TownSpring_Evening(uint8_t *ram, HmRegs *r);
void hm_Colours_TownSpring_Night(uint8_t *ram, HmRegs *r);
void hm_Colours_TownSummer_Day(uint8_t *ram, HmRegs *r);
void hm_Colours_TownSummer_Afternoon(uint8_t *ram, HmRegs *r);
void hm_Colours_TownSummer_Evening(uint8_t *ram, HmRegs *r);
void hm_Colours_TownSummer_Night(uint8_t *ram, HmRegs *r);
void hm_Colours_TownAutumn_Day(uint8_t *ram, HmRegs *r);
void hm_Colours_TownAutumn_Afternoon(uint8_t *ram, HmRegs *r);
void hm_Colours_TownAutumn_Evening(uint8_t *ram, HmRegs *r);
void hm_Colours_TownAutumn_Night(uint8_t *ram, HmRegs *r);
void hm_Colours_TownWinter_Night(uint8_t *ram, HmRegs *r);
void hm_Colours_MountainSpring_Day(uint8_t *ram, HmRegs *r);
void hm_Colours_MountainSpring_Afternoon(uint8_t *ram, HmRegs *r);
void hm_Colours_MountainSpring_Evening(uint8_t *ram, HmRegs *r);
void hm_Colours_MountainSpring_Night(uint8_t *ram, HmRegs *r);
void hm_Colours_MountainSummer_Day(uint8_t *ram, HmRegs *r);
void hm_Colours_MountainSummer_Afternoon(uint8_t *ram, HmRegs *r);
void hm_Colours_MountainSummer_Evening(uint8_t *ram, HmRegs *r);
void hm_Colours_MountainSummer_Night(uint8_t *ram, HmRegs *r);
void hm_Colours_MountainAutumn_Day(uint8_t *ram, HmRegs *r);
void hm_Colours_MountainAutumn_Afternoon(uint8_t *ram, HmRegs *r);
void hm_Colours_MountainAutumn_Evening(uint8_t *ram, HmRegs *r);
void hm_Colours_MountainAutumn_Night(uint8_t *ram, HmRegs *r);
void hm_Colours_MountainWinter_Day(uint8_t *ram, HmRegs *r);
void hm_Colours_MountainWinter_Afternoon(uint8_t *ram, HmRegs *r);
void hm_Colours_MountainWinter_Evening(uint8_t *ram, HmRegs *r);
void hm_Colours_MountainWinter_Night(uint8_t *ram, HmRegs *r);
void hm_Colours_MountainTop_Night(uint8_t *ram, HmRegs *r);
void hm_Colours_Map2C(uint8_t *ram, HmRegs *r);
void hm_Colours_Festival0A_Night(uint8_t *ram, HmRegs *r);
void hm_Colours_Map25(uint8_t *ram, HmRegs *r);
void hm_Colours_Festival09_Day(uint8_t *ram, HmRegs *r);
void hm_Colours_Map21(uint8_t *ram, HmRegs *r);
