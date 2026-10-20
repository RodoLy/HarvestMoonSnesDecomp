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

#define W16(a, lo, hi) { a, 2, lo, hi }
#define B8(a, lo, hi)  { a, 1, lo, hi }

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

void *hm_decomp_check_begin(const CpuState *cpu)
{
  Snapshot *s = malloc(sizeof *s);
  if (!s) return NULL;
  s->cpu = *cpu;
  memcpy(s->ram, cpu->ram, sizeof s->ram);
  return s;
}

void hm_decomp_check_end(const char *name, void *snapshot, const CpuState *cpu, int completed)
{
  Snapshot *s = snapshot;
  int index = 0;
  const HmDecompFn *f = find(name, &index);
  if (!s || !f) { free(s); return; }
  Stats *st = &s_stats[index];
  st->calls++;
  /* A routine interrupted mid-way (the frame deadline yielded) or run in
   * decimal mode is not comparable; the rewrites assume binary mode. */
  if (!completed || s->cpu._flag_D) { st->skipped++; free(s); return; }

  HmRegs in = { s->cpu.A, s->cpu.X, s->cpu.Y, s->cpu._flag_C, s->cpu._flag_N, s->cpu.DB, s->cpu.D };
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
    if (st->bad <= 5)
      fprintf(stderr, "[decomp-check] MISMATCH %s: %s (in A=%04X X=%04X Y=%04X C=%u DB=%02X)\n",
              name, why, in.a, in.x, in.y, in.c, in.db);
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
  }
  if (at < 0 || (int)frame != at) return;
  at = -1;

  static CpuState saved_cpu;
  static uint8_t saved_ram[0x20000];
  saved_cpu = g_cpu;
  memcpy(saved_ram, g_cpu.ram, sizeof saved_ram);
  int prev = g_hm_decomp_check;
  if (!s_stats) { s_stats = calloc((size_t)g_hm_decomp_count, sizeof *s_stats); atexit(report); }
  g_hm_decomp_check = 1;
  fprintf(stderr, "[decomp-fuzz] frame %u: %d calls per routine\n", frame, iters);
  for (int i = 0; i < g_hm_decomp_count; i++) {
    const HmDecompFn *f = &g_hm_decomp_fns[i];
    for (int n = 0; n < iters; n++) {
      g_cpu = saved_cpu;
      memcpy(g_cpu.ram, saved_ram, sizeof saved_ram);
      for (int v = 0; v < HM_FUZZ_MAX_VARS && f->vars[v].size; v++) {
        uint16_t x = rnd_in(f->vars[v].lo, f->vars[v].hi);
        g_cpu.ram[f->vars[v].addr] = (uint8_t)x;
        if (f->vars[v].size == 2) g_cpu.ram[f->vars[v].addr + 1] = (uint8_t)(x >> 8);
      }
      uint16_t a = rnd_in(f->a_lo, f->a_hi);
      g_cpu.A = f->entry_m ? (uint16_t)((rnd() & 0xFF00) | (a & 0xFF)) : a;
      g_cpu.X = (uint16_t)rnd(); g_cpu.Y = (uint16_t)rnd();
      g_cpu.emulation = 0;
      g_cpu.D = 0;
      g_cpu._flag_D = 0;
      g_cpu._flag_C = rnd() & 1;
      g_cpu._flag_N = rnd() & 1;
      cpu_mirrors_to_p(&g_cpu);
      g_cpu.P = (uint8_t)((g_cpu.P & ~0x30) | (f->entry_m ? 0x20 : 0) | (f->entry_x ? 0x10 : 0));
      cpu_p_to_mirrors(&g_cpu);
      if (f->entry_x) { g_cpu.X &= 0xFF; g_cpu.Y &= 0xFF; }
      cpu_push_jsl_return_frame(&g_cpu);
      cpu_dispatch_pc_paired(&g_cpu, f->pc24, 3);
    }
  }
  g_cpu = saved_cpu;
  memcpy(g_cpu.ram, saved_ram, sizeof saved_ram);
  g_hm_decomp_check = prev;
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
