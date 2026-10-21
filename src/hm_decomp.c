/*
 * hm_decomp.c — registry and differential checker for the decompilation.
 * See hm_decomp.h.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu_state.h"
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */
#include "mod_runtime.h"

void interp_bridge_set_master_deadline(uint64_t master_clock);  /* snes/interp_bridge.h */

#define W16(a, lo, hi) { a, 2, lo, hi }
#define B8(a, lo, hi)  { a, 1, lo, hi }

/* Fuzz input makers for the farm routines (farm.c). */
static void prep_random_farm(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  /* A field with every kind of tile: mostly field codes, some "not field". */
  static const uint8_t kinds[] = { 0x00, 0x02, 0x05, 0x06, 0x07, 0x08, 0x1D, 0x1E, 0x30,
                                   0x6F, 0x70, 0x75, 0x79, 0x9F, 0xA0, 0xFF };
  for (int i = 0; i < 64 * 64; i++)
    ram[0x0A4E6 + i] = (rnd() & 3) ? kinds[rnd() % sizeof kinds] : (uint8_t)rnd();
  /* Chances: usually the game's, sometimes 0, 1, 255 or odd 16-bit ones. */
  static const uint16_t chances[] = { 0, 1, 4, 8, 0x10, 0x20, 0x40, 0x60, 0xFF, 0x0100, 0x1234 };
  r->a = chances[rnd() % 11]; r->x = chances[rnd() % 11]; r->y = chances[rnd() % 11];
}
static void prep_random_farm_and_weather(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_random_farm(ram, r, rnd);
  static const uint16_t flags[] = { 0x0000, 0x0002, 0x0004, 0x0008, 0x0010, 0x0100, 0x0200,
                                    0x0102, 0x0300, 0x001A };
  hm_wr16(ram, 0x00196, flags[rnd() % 10]);
}

static void prep_random_farm_and_date(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_random_farm_and_weather(ram, r, rnd);
  ram[0x11F19] = (uint8_t)(rnd() % 4);                       /* season */
  ram[0x11F1B] = (uint8_t)((rnd() & 3) ? 1 + rnd() % 30 : 1); /* day; often 1 */
}

static void prep_cow_pointer(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  /* $72-$74 -> one of the 16 cow records ($7E:C1C6 + 16 n) */
  hm_wr16(ram, 0x72, (uint16_t)(0xC1C6 + 16 * (rnd() % 16)));
  ram[0x74] = 0x7E;
  static const uint16_t changes[] = { 0xFFF8, 0xFFF0, 0xFFE8, 0xFFF4, 0xFFE2, 1, 8, 0x80, 0x7FFF, 0x8000 };
  if (rnd() & 1) r->a = changes[rnd() % 10];
}
static void prep_cc_pointer(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  (void)r;
  hm_wr16(ram, 0xCC, (uint16_t)(0xB586 + 64 * (rnd() % 0x31)));
  ram[0xCE] = 0x7E;
}
static void prep_event_start(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  (void)ram;
  r->a = (uint16_t)(rnd() % 0x31);   /* CC slot */
  r->x = (uint16_t)(rnd() % 12);     /* script group */
  r->y = (uint16_t)(rnd() % 0x60);   /* script number */
}

const HmDecompFn g_hm_decomp_fns[] = {
  { "GetRNG", hm_GetRNG, HM_OUT_A | HM_OUT_C, "src/rng.c",
    0x838138, 1, 0, 0, 0xFFFF,
    { B8(HM_RNG_STATE, 0, 255), B8(HM_RNG_STATE + 1, 0, 255), B8(HM_RNG_STATE + 2, 0, 255) } },
  { "AddWood", hm_AddWood, HM_OUT_A | HM_OUT_C, "src/stock.c",
    0x83B224, 0, 0, 0, 0xFFFF, { W16(HM_STORED_WOOD, 0, 1100) } },
  { "AddGrass", hm_AddGrass, HM_OUT_A | HM_OUT_C, "src/stock.c",
    0x83B253, 0, 0, 0, 0xFFFF, { W16(HM_STORED_GRASS, 0, 1100) } },
  { "AddPlayerHappiness", hm_AddPlayerHappiness, HM_OUT_A | HM_OUT_C, "src/stock.c",
    0x83B282, 0, 0, 0, 0xFFFF, { W16(HM_HAPPINESS, 0, 1100) } },
  { "ChangeStamina", hm_ChangeStamina, 0, "src/stamina.c",
    0x81D061, 1, 0, 0, 0xFF,
    { B8(HM_MAX_STAMINA, 0, 255), B8(HM_CURRENT_STAMINA, 0, 255), B8(HM_EXHAUSTION, 0, 3),
      W16(HM_GAME_STATE, 0, 0xFFFF), B8(0x11F60, 0, 255) } },
  { "GetsCowPointer", hm_GetsCowPointer, HM_OUT_A, "src/animals.c",
    0x83C9A7, 0, 0, 0, 0xFFFF, { { 0 } } },
  { "GetChickenPointer", hm_GetChickenPointer, HM_OUT_A, "src/animals.c",
    0x83C995, 0, 0, 0, 0xFFFF, { { 0 } } },
  { "DivisionUnsigned", hm_DivisionUnsigned, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C, "src/math.c",
    0x838082, 0, 0, 0, 0xFFFF,
    /* small and large divisors: both paths, and divisor 0 */
    { W16(0x7E, 0, 0xFFFF), W16(0x80, 0, 600) } },
  { "RNGReturn0toA", hm_RNGReturn0toA, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C, "src/rng.c",
    0x8089F9, 1, 1, 0, 0xFF,
    { B8(HM_RNG_STATE, 0, 255), B8(HM_RNG_STATE + 1, 0, 255), B8(HM_RNG_STATE + 2, 0, 255) } },
  { "SetWeatherFlags", hm_SetWeatherFlags, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C, "src/weather.c",
    0x828C09, 0, 0, 0, 0xFFFF,
    { B8(HM_WEATHER_TOMORROW, 0, 12), B8(HM_SEASON, 0, 3),
      W16(0x11F64, 0, 0xFFFF), W16(0x11F6E, 0, 0xFFFF) } },
  { "WeatherTomorrow", hm_WeatherTomorrow, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/weather.c", 0x828CF9, 0, 0, 0, 0xFFFF,
    { B8(HM_SEASON, 0, 3), B8(HM_DAY, 1, 30), B8(HM_YEAR, 0, 2),
      W16(0x11F64, 0, 3), W16(0x11F6C, 0x0FFF, 0x1000), B8(HM_RNG_STATE, 0, 255) } },
  { "GetTileIndex", hm_GetTileIndex, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR, "src/farm.c",
    0x82B13C, 0, 0, 0, 0xFFFF, { B8(0x00181, 0, 5) } },
  { "LoadsDamageProbabilities", hm_LoadsDamageProbabilities,
    HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR, "src/farm.c",
    0x828270, 0, 0, 0, 4, { { 0 } } },
  { "ClimateFarmDamageCheck", hm_ClimateFarmDamageCheck, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/farm.c", 0x82A713, 0, 0, 0, 0xFFFF,
    { B8(HM_RNG_STATE, 0, 255), B8(HM_RNG_STATE + 1, 0, 255), W16(0x0092E, 0, 0xFFFF),
      W16(0x11F29, 0, 3) },
    40, prep_random_farm },
  { "ClimateFarmDamagePrep", hm_ClimateFarmDamagePrep, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/farm.c", 0x828209, 0, 0, 0, 0xFFFF,
    { B8(HM_RNG_STATE, 0, 255), B8(HM_RNG_STATE + 1, 0, 255), W16(0x0092E, 0, 0xFFFF),
      W16(0x11F29, 0, 3) },
    40, prep_random_farm_and_weather },
  { "MonthlyFarmTilesCheck", hm_MonthlyFarmTilesCheck, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/farm.c", 0x82A6A2, 1, 0, 0, 0xFF, { { 0 } }, 40, prep_random_farm_and_date },
  { "NightlyFarmTilesCheck", hm_NightlyFarmTilesCheck, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/farm.c", 0x82A811, 0, 0, 0, 0xFFFF,
    { B8(HM_RNG_STATE, 0, 255), B8(HM_RNG_STATE + 1, 0, 255), W16(0x11F6E, 0, 0xFFFF) },
    40, prep_random_farm_and_date },
  { "CalculateFarmDevelopment", hm_CalculateFarmDevelopment, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/farm.c", 0x82AA0C, 0, 0, 0, 0xFFFF, { W16(0x11F56, 0, 0xFFFF) }, 40, prep_random_farm },
  { "AddCowHappiness", hm_AddCowHappiness, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/animals.c", 0x84A5D4, 0, 0, 0, 0xFFFF,
    { B8(0x0C1C6 + 4, 0, 255), B8(0x0C1D6 + 4, 0, 255), B8(0x0C1E6 + 4, 0, 255) },
    0, prep_cow_pointer },
  { "SetCCPoiner", hm_SetCCPoiner, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C, "src/events.c",
    0x84887C, 0, 0, 0, 0xFFFF, { { 0 } } },
  { "ResetsCurrentCCStructureLong", hm_ResetsCurrentCCStructureLong,
    HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR, "src/events.c",
    0x8488D4, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_cc_pointer },
  { "SUB_8480F8", hm_SUB_8480F8, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C, "src/events.c",
    0x8480F8, 0, 0, 0, 0xFFFF, { W16(0x00985, 0, 0xFFFF), W16(0x00987, 0, 0xFFFF) },
    0, prep_event_start },
};
const int g_hm_decomp_count = (int)(sizeof g_hm_decomp_fns / sizeof g_hm_decomp_fns[0]);

int g_hm_decomp_check;

typedef struct Snapshot {
  CpuState cpu;
  uint8_t ram[0x20000];
} Snapshot;

typedef struct Stats { unsigned calls, ok, bad, skipped; } Stats;
static Stats *s_stats;

static const HmDecompFn *find(const char *name, int *index)
{
  for (int i = 0; i < g_hm_decomp_count; i++)
    if (!strcmp(g_hm_decomp_fns[i].name, name)) { *index = i; return &g_hm_decomp_fns[i]; }
  return NULL;
}

static void report(void)
{
  if (!s_stats) return;
  fprintf(stderr, "[decomp-check] summary:\n");
  for (int i = 0; i < g_hm_decomp_count; i++)
    fprintf(stderr, "[decomp-check]   %-24s calls=%u ok=%u MISMATCH=%u skipped=%u\n",
            g_hm_decomp_fns[i].name, s_stats[i].calls, s_stats[i].ok, s_stats[i].bad,
            s_stats[i].skipped);
}

__attribute__((constructor)) static void hm_decomp_init(void)
{
  const char *e = getenv("HM_DECOMP_CHECK");
  g_hm_decomp_check = e && *e && *e != '0';
  if (g_hm_decomp_check) {
    s_stats = calloc((size_t)g_hm_decomp_count, sizeof *s_stats);
    atexit(report);
    fprintf(stderr, "[decomp-check] checking %d decompiled routines\n", g_hm_decomp_count);
  }
}

/* While fuzzing, only the routine being fuzzed is compared: the calls it
 * makes have their own fuzz entries, and checking thousands of nested calls
 * (each one copies 128 KB) is too slow for the 4096-tile farm loops. */
static int s_fuzzing, s_check_depth;

void *hm_decomp_check_begin(const CpuState *cpu)
{
  if (s_fuzzing && s_check_depth > 0) return NULL;
  s_check_depth++;
  Snapshot *s = malloc(sizeof *s);
  if (!s) { s_check_depth--; return NULL; }
  s->cpu = *cpu;
  memcpy(s->ram, cpu->ram, sizeof s->ram);
  return s;
}

void hm_decomp_check_end(const char *name, void *snapshot, const CpuState *cpu, int completed)
{
  Snapshot *s = snapshot;
  int index = 0;
  if (!s) return;                       /* nested call not checked (fuzzing) */
  s_check_depth--;
  const HmDecompFn *f = find(name, &index);
  if (!f) { free(s); return; }
  Stats *st = &s_stats[index];
  st->calls++;
  /* A routine interrupted mid-way (the frame deadline yielded) or run in
   * decimal mode is not comparable; the rewrites assume binary mode. */
  if (!completed || s->cpu._flag_D) { st->skipped++; free(s); return; }

  HmRegs in = { s->cpu.A, s->cpu.X, s->cpu.Y, s->cpu._flag_C, s->cpu._flag_N, s->cpu.DB, s->cpu.D,
                s->cpu.S, s->cpu.PB };
  HmRegs out = in;
  f->fn(s->ram, &out);

  int bad = 0;
  char why[256] = "";
  if ((f->out_mask & HM_OUT_A) && out.a != cpu->A) { bad = 1; snprintf(why, sizeof why, "A %04X != rom %04X", out.a, cpu->A); }
  if (!bad && (f->out_mask & HM_OUT_X) && out.x != cpu->X) { bad = 1; snprintf(why, sizeof why, "X %04X != rom %04X", out.x, cpu->X); }
  if (!bad && (f->out_mask & HM_OUT_Y) && out.y != cpu->Y) { bad = 1; snprintf(why, sizeof why, "Y %04X != rom %04X", out.y, cpu->Y); }
  if (!bad && (f->out_mask & HM_OUT_C) && out.c != cpu->_flag_C) { bad = 1; snprintf(why, sizeof why, "C %u != rom %u", out.c, cpu->_flag_C); }
  if (!bad) {
    for (size_t i = 0; i < sizeof s->ram; i++)
      if (s->ram[i] != cpu->ram[i]) {
        bad = 1;
        snprintf(why, sizeof why, "WRAM %05zX = %02X, rom wrote %02X", i, s->ram[i], cpu->ram[i]);
        break;
      }
  }
  if (bad) {
    st->bad++;
    if (st->bad <= 5) {
      fprintf(stderr, "[decomp-check] MISMATCH %s: %s (in A=%04X X=%04X Y=%04X C=%u DB=%02X)\n",
              name, why, in.a, in.x, in.y, in.c, in.db);
      /* HM_DECOMP_DIFF_ALL=1: list every differing byte (rewrite / rom). */
      static int all = -1;
      if (all < 0) { const char *e = getenv("HM_DECOMP_DIFF_ALL"); all = e && *e && *e != '0'; }
      for (size_t i = 0, n = 0; all && i < sizeof s->ram && n < 24; i++)
        if (s->ram[i] != cpu->ram[i]) {
          fprintf(stderr, "[decomp-check]     %05zX: %02X / rom %02X\n", i, s->ram[i], cpu->ram[i]);
          n++;
        }
    }
  } else {
    st->ok++;
  }
  free(s);
}

/* ── Fuzzing (HM_DECOMP_FUZZ=<frame>:<iterations>) ─────────────────── */
static uint32_t s_rng = 0x12345678u;
static uint32_t rnd(void) { s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5; return s_rng; }
static uint16_t rnd_in(uint16_t lo, uint16_t hi)
{
  /* Mostly uniform in range, sometimes the edges, which is where bugs hide. */
  uint32_t k = rnd() % 8;
  if (k == 0) return lo;
  if (k == 1) return hi;
  return (uint16_t)(lo + rnd() % ((uint32_t)hi - lo + 1));
}

static void fuzz_at(unsigned frame)
{
  static int at = -2, iters;
  if (at == -2) {
    const char *e = getenv("HM_DECOMP_FUZZ");
    at = -1;
    if (e && *e) { at = atoi(e); const char *c = strchr(e, ':'); iters = c ? atoi(c + 1) : 1000; }
    /* HM_DECOMP_FUZZ_SEED=<n>: other random inputs (a frame only fits a few
     * thousand calls per routine, so run several seeds for more). */
    const char *seed = getenv("HM_DECOMP_FUZZ_SEED");
    if (seed && *seed) s_rng ^= (uint32_t)strtoul(seed, NULL, 0) * 0x9E3779B9u;
    if (!s_rng) s_rng = 1;
  }
  /* One routine per frame, starting at frame `at`: the frame watchdog
   * (5 s of real time per frame) then applies to each routine on its own. */
  if (at < 0 || (int)frame < at || (int)frame >= at + g_hm_decomp_count) return;
  int only = (int)frame - at;

  static CpuState saved_cpu;
  static uint8_t saved_ram[0x20000];
  saved_cpu = g_cpu;
  memcpy(saved_ram, g_cpu.ram, sizeof saved_ram);
  int prev = g_hm_decomp_check;
  if (!s_stats) { s_stats = calloc((size_t)g_hm_decomp_count, sizeof *s_stats); atexit(report); }
  g_hm_decomp_check = 1;
  s_fuzzing = 1;
  /* Long interpreted routines (CowFeedingandStatus) would otherwise pause at
   * the frame's deadline half-way, as they do in the game, and not be
   * comparable. The frame driver sets the deadline again every frame. */
  interp_bridge_set_master_deadline(0);
  if (only == 0)
    fprintf(stderr, "[decomp-fuzz] frames %u-%u: %d calls per routine\n", frame,
            frame + g_hm_decomp_count - 1, iters);
  for (int i = only; i == only; i++) {
    const HmDecompFn *f = &g_hm_decomp_fns[i];
    int n_calls = (f->fuzz_max && f->fuzz_max < iters) ? f->fuzz_max : iters;
    /* HM_DECOMP_FUZZ_ONLY=<name>: fuzz just that routine (debugging). */
    const char *only_name = getenv("HM_DECOMP_FUZZ_ONLY");
    if (only_name && *only_name && strcmp(only_name, f->name)) n_calls = 0;
    /* HM_DECOMP_FUZZ_CALL=<n>: run only call n of it (same inputs as in the
     * full run), e.g. to trace it. */
    const char *one_call = getenv("HM_DECOMP_FUZZ_CALL");
    int pick = one_call && *one_call ? atoi(one_call) : -1;
    for (int n = 0; n < n_calls; n++) {
      g_cpu = saved_cpu;
      memcpy(g_cpu.ram, saved_ram, sizeof saved_ram);
      /* Every call starts with a fresh frame budget: long routines (the
       * 4096-tile farm loop) would otherwise hit the frame deadline, yield
       * half-way and not be comparable. The machine is restored after. */
      g_cpu.master_cycles = 0;
      for (int v = 0; v < HM_FUZZ_MAX_VARS && f->vars[v].size; v++) {
        uint16_t x = rnd_in(f->vars[v].lo, f->vars[v].hi);
        g_cpu.ram[f->vars[v].addr] = (uint8_t)x;
        if (f->vars[v].size == 2) g_cpu.ram[f->vars[v].addr + 1] = (uint8_t)(x >> 8);
      }
      uint16_t a = rnd_in(f->a_lo, f->a_hi);
      g_cpu.A = f->entry_m ? (uint16_t)((rnd() & 0xFF00) | (a & 0xFF)) : a;
      g_cpu.X = (uint16_t)rnd(); g_cpu.Y = (uint16_t)rnd();
      if (f->prep) {
        HmRegs pr = { g_cpu.A, g_cpu.X, g_cpu.Y };
        f->prep(g_cpu.ram, &pr, rnd);
        g_cpu.A = pr.a; g_cpu.X = pr.x; g_cpu.Y = pr.y;
      }
      g_cpu.emulation = 0;
      g_cpu.D = 0;
      g_cpu._flag_D = 0;
      g_cpu._flag_C = rnd() & 1;
      g_cpu._flag_N = rnd() & 1;
      cpu_mirrors_to_p(&g_cpu);
      /* I set (IRQs masked): the interpreter otherwise hands control back to
       * the scheduler half-way through a long routine when an IRQ is pending,
       * as the real CPU would to run the handler. No rewritten routine
       * touches I. */
      g_cpu.P = (uint8_t)((g_cpu.P & ~0x30) | 0x04 | (f->entry_m ? 0x20 : 0) | (f->entry_x ? 0x10 : 0));
      cpu_p_to_mirrors(&g_cpu);
      if (f->entry_x) { g_cpu.X &= 0xFF; g_cpu.Y &= 0xFF; }
      if (!(f->out_mask & HM_JSR)) g_cpu.PB = (uint8_t)(f->pc24 >> 16);  /* JSL enters its bank */
      if (f->out_mask & HM_JSR) cpu_push_jsr_return_frame(&g_cpu);
      else cpu_push_jsl_return_frame(&g_cpu);
      uint8_t frame_size = (f->out_mask & HM_JSR) ? 2 : 3;
      if (pick >= 0 && n != pick) continue;
      unsigned bad_before = s_stats[i].bad;
      if (f->out_mask & HM_INTERP) {
        /* No wrapper around it: check this call here. */
        void *snap = hm_decomp_check_begin(&g_cpu);
        /* No compiled body: run the original in the recompiler's
         * interpreter, as the game itself does. */
        RecompReturn rr = interp_tier_dispatch(&g_cpu, f->pc24);
        hm_decomp_check_end(f->name, snap, &g_cpu, rr == RECOMP_RETURN_NORMAL);
      } else {
        cpu_dispatch_pc_paired(&g_cpu, f->pc24, frame_size);
      }
      if (s_stats[i].bad != bad_before && s_stats[i].bad <= 5)
        fprintf(stderr, "[decomp-fuzz]   (that was %s call %d)\n", f->name, n);
    }
  }
  g_cpu = saved_cpu;
  memcpy(g_cpu.ram, saved_ram, sizeof saved_ram);
  g_hm_decomp_check = prev;
  s_fuzzing = 0;
  if (only == g_hm_decomp_count - 1)
    fprintf(stderr, "[decomp-fuzz] done; machine restored\n");
}

/* Hooked in through the mod runtime, so the game project needs no code for
 * the decompilation: frame callbacks are cleared whenever mods (re)activate,
 * and reset callbacks run right before, so re-register there. */
static void on_frame(void)
{
  static unsigned frame;
  fuzz_at(++frame);
}
static void on_reset(void) { snes_mod_register_frame_callback(on_frame); }

SNES_MOD_CONSTRUCTOR(register_hm_decomp)
{
  snes_mod_register_reset_callback(on_reset);
}
