/*
 * hm_decomp.c — registry and differential checker for the decompilation.
 * See hm_decomp.h.
 */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu_state.h"
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */
#include "mod_runtime.h"

void interp_bridge_set_master_deadline(uint64_t master_clock);  /* snes/interp_bridge.h */
typedef void (*InterpPreOpcodeHook)(CpuState *cpu, uint32_t pc24);           /* snes/interp_bridge.h */
bool interp_bridge_add_pre_opcode_hook(uint32_t pc24, InterpPreOpcodeHook hook);
void interp_bridge_pre_opcode_redirect(uint32_t pc24);
int interp_bridge_run_interrupt(CpuState *cpu, uint32_t entry_pc24);
size_t RtlSaveSnapshotToMemory(void *data, size_t capacity);      /* common_rtl.h */
bool RtlLoadSnapshotFromMemory(const void *data, size_t size);
void WatchdogFrameStart(void);   /* common_cpu_infra.h */
extern jmp_buf g_watchdog_jmp;
extern int g_recomp_stack_top;
/* HM_WAIT: the interpreter runs the compiled body of a routine it calls
 * when there is one; the compiled WaitForNMI would spin (the fuzzer has no
 * frame scheduler), so while one is fuzzed the waiting routines are kept in
 * the interpreter (at most 16). */
static const HmDecompFn *find(const char *name, int *index);
static void keep_waits_interpreted(int on, const HmDecompFn *fuzzed)
{
  /* first the ones other routines call (the fuzzed routine itself is
   * entered in the interpreter anyway), then the rest while there is room */
  static const char *const called[] = {
    "WaitForNMI", "WaitForNMIATimes", "UNK_Audio18", "ToolUsedSound2", "ScreenFadein",
    "ScreenFadeout", "UNK_Audio24", "UNK_Audio20", "CODE_8187C3", "UNK_Audio26" };
  uint32_t list[16];
  size_t n = 0;
  for (size_t k = 0; on && k < sizeof called / sizeof called[0]; k++) {
    int index = 0;
    const HmDecompFn *f = find(called[k], &index);
    if (f && f != fuzzed && n < 16) list[n++] = f->pc24;
  }
  for (int i = 0; on && i < g_hm_decomp_count && n < 16; i++) {
    const HmDecompFn *f = &g_hm_decomp_fns[i];
    int already = 0;
    for (size_t k = 0; k < n; k++) already |= list[k] == f->pc24;
    if ((f->out_mask & HM_WAIT) && f != fuzzed && !already) list[n++] = f->pc24;
  }
  interp_bridge_set_lle_bounce_exclusions(list, n);
}

/* HM_WAIT: while one is fuzzed, the original (in the interpreter) gets its
 * NMI when it reaches a wait loop's LDA $00 with $00 still 0: the CPU
 * pushes the bank, the address and the flags, sets I, clears decimal mode
 * and jumps to the vector ($00:867B, NMI_Interrupt), whose RTI comes back
 * to the LDA. Nothing else interrupts the run, so the C side (hm_nmi) can
 * do exactly the same. */
static int s_nmi_sim;
static void nmi_at_wait(CpuState *cpu, uint32_t pc24)
{
  if (!s_nmi_sim || cpu->emulation || cpu->D >= 0x2000 || cpu->ram[cpu->D]) return;
  cpu_mirrors_to_p(cpu);
  cpu_write8(cpu, 0x00, cpu->S, cpu->PB); cpu->S = (uint16_t)(cpu->S - 1);
  cpu_write8(cpu, 0x00, cpu->S, (uint8_t)(pc24 >> 8)); cpu->S = (uint16_t)(cpu->S - 1);
  cpu_write8(cpu, 0x00, cpu->S, (uint8_t)pc24); cpu->S = (uint16_t)(cpu->S - 1);
  cpu_write8(cpu, 0x00, cpu->S, cpu->P); cpu->S = (uint16_t)(cpu->S - 1);
  cpu->P = (uint8_t)((cpu->P | 0x04) & ~0x08);
  cpu_p_to_mirrors(cpu);
  cpu->PB = 0x00;
  interp_bridge_pre_opcode_redirect(0x00867B);
}
uint8_t ReadReg(uint16_t reg);                                    /* common_rtl.h */
void RtlSetPadState(int slot, uint16_t buttons);
uint16_t RtlGetPadState(int slot);
uint16_t joypad_auto_read_word(uint16_t state);                  /* snes/joypad.h */
/* The emulated DMA unit (snes/dma.h): reading $43xx has no side effects. */
typedef struct Dma Dma;
extern Dma *g_dma;
uint8_t dma_read(Dma *dma, uint16_t adr);
void dma_write(Dma *dma, uint16_t adr, uint8_t val);

uint8_t *g_hm_dma_regs;
uint8_t *g_hm_sram;
uint32_t g_hm_sram_size;
extern uint8_t *g_sram;                                           /* common_rtl.h */
extern int g_sram_size;

/* ── The sound chip's ports (see g_hm_apu in hm_decomp.h) ─────────────── */
typedef int (*ApuWriteObserver)(uint16_t reg, uint8_t value);
typedef int (*ApuReadObserver)(uint16_t reg, uint8_t *value);
int RtlAddApuPortObserver(ApuWriteObserver observer);        /* common_rtl.h */
int RtlAddApuPortReadObserver(ApuReadObserver observer);
enum { APU_LOG = 1 << 22 };   /* the uploads wait a long time: millions of reads */
static HmApuEvent s_apu_log[APU_LOG];
static int s_apu_n, s_apu_overflow, s_apu_recording;
HmApuReplay *g_hm_apu;
/* A rewrite that goes off the recorded traffic would wait forever for an
 * answer that is not there: the first difference ends it here. */
static jmp_buf s_apu_abort;

static void apu_log(uint16_t reg, uint8_t val, uint8_t write)
{
  if (!s_apu_recording) return;
  { static int tr = -1, k; if (tr < 0) tr = getenv("HM_APU_TRACE") != NULL;
    if (tr && k < 60) { k++; fprintf(stderr, "[apu] %s %04X %02X clk=%llu\n", write ? "W" : "R", reg, val, (unsigned long long)g_cpu.master_cycles); } }
  if (s_apu_n == APU_LOG) { s_apu_overflow = 1; return; }
  s_apu_log[s_apu_n++] = (HmApuEvent){ reg, val, write };
}
static int apu_wrote(uint16_t reg, uint8_t value) { apu_log(reg, value, 1); return 0; }
static int apu_read(uint16_t reg, uint8_t *value) { apu_log(reg, *value, 0); return 0; }

static void apu_mismatch(HmApuReplay *p, const char *what, uint16_t reg, uint8_t v)
{
  if (p->bad) return;
  p->bad = 1;
  if (p->pos < p->n) {
    const HmApuEvent *e = &p->ev[p->pos];
    snprintf(p->why, sizeof p->why, "APU #%d: %s $%04X=%02X, rom %s $%04X=%02X", p->pos, what, reg, v,
             e->write ? "wrote" : "read", e->reg, e->val);
  } else {
    snprintf(p->why, sizeof p->why, "APU #%d: %s $%04X=%02X, rom did nothing more", p->pos, what, reg, v);
  }
  longjmp(s_apu_abort, 1);
}
uint8_t hm_io_read(uint16_t reg) { return ReadReg(reg); }

uint8_t hm_apu_read(uint16_t reg)
{
  HmApuReplay *p = g_hm_apu;
  if (!p) return 0;
  if (p->bad) return 0;
  if (p->pos >= p->n || p->ev[p->pos].write || p->ev[p->pos].reg != reg) {
    apu_mismatch(p, "read", reg, 0);
    return 0;
  }
  return p->ev[p->pos++].val;
}
void hm_apu_write(uint16_t reg, uint8_t v)
{
  HmApuReplay *p = g_hm_apu;
  if (!p || p->bad) return;
  if (p->pos >= p->n || !p->ev[p->pos].write || p->ev[p->pos].reg != reg || p->ev[p->pos].val != v) {
    apu_mismatch(p, "wrote", reg, v);
    return;
  }
  p->pos++;
}
enum { DMA_REGS = 0x80 };
static void dma_regs_save(uint8_t *out)
{
  for (int i = 0; i < DMA_REGS; i++) out[i] = g_dma ? dma_read(g_dma, (uint16_t)(0x4300 + i)) : 0;
}
static void dma_regs_restore(const uint8_t *in)
{
  /* only the registers that exist: $43x0-$43xA and $43xB/$43xF */
  for (int i = 0; i < DMA_REGS; i++) {
    int r = i & 0xF;
    if (g_dma && (r <= 0xB || r == 0xF)) dma_write(g_dma, (uint16_t)(0x4300 + i), in[i]);
  }
}

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

/* CODE_858CB2: the loader's queue ($BB..$BD), its budget (below $2000: at
 * most 31 pieces, the trap is at 32), the slots: new ones left or not, and
 * at least one free (with none free the original spins) */
static void prep_loader(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  uint16_t bb = (uint16_t)(4 * (rnd() % 64));
  uint16_t n = (uint16_t)((rnd() % 4) ? rnd() % 12 : rnd() % 64);
  hm_wr16(ram, 0xBB, bb);
  hm_wr16(ram, 0xBD, (uint16_t)((bb + 4 * n) & 0xFF));
  hm_wr16(ram, 0xC7, (uint16_t)((rnd() % 4) ? 0x100 * (rnd() % 0x20) + rnd() % 0x100 : rnd() % 0x2000));
  for (unsigned q = 0; q < 64; q++) {
    hm_wr16(ram, 0x0A220 + 4 * q, (uint16_t)(rnd() % 0x800));
    hm_wr16(ram, 0x0A222 + 4 * q, (uint16_t)(0x019C + 0x24 * (rnd() % 40) + 0x14 + rnd() % 16));
  }
  uint32_t db = (uint32_t)r->db << 16;
  hm_bus_wr16(ram, db | 0x0846, (uint16_t)((rnd() % 2) ? 0x40 : rnd() % 0x41));
  hm_bus_wr16(ram, db | 0x0848, (uint16_t)((rnd() % 4) ? rnd() % 0x40 : rnd()));
  for (unsigned s = 0; s < 0x40; s++) {
    hm_wr16(ram, 0x0A320 + 4 * s, (uint16_t)((rnd() % 3) ? 1 + rnd() % 200 : 0));
    hm_wr16(ram, 0x0A322 + 4 * s, (uint16_t)((rnd() % 4) ? rnd() % 0x800 : 0xFFFF));
  }
  /* each piece takes a free slot: 32 free ones are enough for 31 pieces */
  for (unsigned k = 0, s = rnd() % 0x40; k < 32; k++, s = (s + 1 + rnd() % 2) % 0x40)
    hm_wr16(ram, 0x0A320 + 4 * s, 0);
  if (rnd() % 2) hm_wr16(ram, 0xC7, (uint16_t)(rnd() % 0x2000));
}

/* SUB_809D0B: the mountain, its find tiles and the water's edge */
static void prep_mountain(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  (void)r;
  ram[0x22] = (uint8_t)((rnd() % 8) ? 0x10 + rnd() % 4 : rnd());
  hm_wr16(ram, 0x00196, (uint16_t)((rnd() % 3) ? 0 : rnd()));
  static const uint16_t tiles[] = { 0xF8, 0xF8, 0xF9, 0xFA, 0xFB, 0xFB, 0x00, 0x1F9 };
  hm_wr16(ram, 0x00878, tiles[rnd() % 8]);
  hm_wr16(ram, 0x0087A, (rnd() % 3) ? hm_rd16(ram, 0x00878) : tiles[rnd() % 8]);
  /* mostly the season of the find tile under him (one roll in 17 finds) */
  static const uint8_t season_of[] = { 0, 0, 2, 3 };
  uint16_t t = hm_rd16(ram, 0x00878);
  if (t < 0xF8 || t > 0xFB) t = hm_rd16(ram, 0x0087A);
  ram[0x11F19] = (uint8_t)((rnd() % 4) ? ((t >= 0xF8 && t <= 0xFB) ? season_of[t - 0xF8] : rnd() % 4)
                                       : ((rnd() % 4) ? rnd() % 4 : rnd()));
  hm_wr16(ram, 0x11F5C, (uint16_t)((rnd() % 4) ? 0 : rnd()));
  hm_wr16(ram, 0x11F5A, (uint16_t)((rnd() % 4) ? 0x0200 : rnd()));
  static const uint16_t st[] = { 0, 0, 0x0002, 0x0010, 0x0800, 0x0001 };
  hm_wr16(ram, 0xD2, (uint16_t)((rnd() % 4) ? st[rnd() % 6] : rnd()));
  static const uint16_t act[] = { 0, 1, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14 };
  hm_wr16(ram, 0xD4, act[rnd() % 8]);
  for (unsigned i = 0; i < 3; i++) ram[HM_RNG_STATE + i] = (uint8_t)rnd();   /* other rolls */
}

/* SUB_809A64: the doors. Both tiles under the player an exit ($C0-$CF), a
 * real tile property table at $0D, every frame of the walk out ($098A
 * 0-$0D), dates and hours on the edges of the flags' tests, the maps that
 * shut ($0B), save the farm (below 4) or pass an hour ($0C-$0F). */
static void prep_map_copy(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void));
static void prep_doors(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_mountain(ram, r, rnd);
  prep_map_copy(ram, r, rnd);
  uint16_t t = (uint16_t)(0xC0 + rnd() % 16);
  static const uint16_t other[] = { 0xBF, 0xD0, 0xF8, 0xFB, 0x00 };
  hm_wr16(ram, 0x00878, (rnd() % 8) ? t : other[rnd() % 5]);
  hm_wr16(ram, 0x0087A, (rnd() % 8) ? t : other[rnd() % 5]);
  static const uint16_t tables[] = { 0xB3B4, 0xB7B4, 0xBBB4, 0xBFB4, 0xC3B4, 0xC7B4, 0xCBB4 };
  if (rnd() % 2) { hm_wr16(ram, 0x0D, tables[rnd() % 7]); ram[0x0F] = 0x82; }
  else {
    /* a made-up table at $7E:F800: any door of the 256 whose map animation
     * is a real one (the real tables never use flag 3) */
    hm_wr16(ram, 0x0D, 0xF800); ram[0x0F] = 0x7E;
    for (unsigned k = 0xC0; k < 0xD0; k++) {
      uint8_t idx;
      do idx = (uint8_t)rnd(); while (hm_bus_rd8(ram, 0x80B6F5u + 8u * idx + 2) >= 0x60);
      ram[0x0F800 + 4 * k] = idx;
    }
  }
  hm_wr16(ram, 0xD2, (uint16_t)((rnd() % 4) ? rnd() & ~0x0010 : rnd()));
  hm_wr16(ram, 0x11F60, (uint16_t)((rnd() % 4) ? rnd() & ~0x0006 : rnd()));
  hm_wr16(ram, 0x11F6C, (uint16_t)rnd());
  static const uint8_t steps[] = { 0, 0, 0, 0, 0, 1, 1, 2, 5, 0x0C, 0x0D, 0x0D, 0x0D };
  uint32_t db = (uint32_t)r->db << 16;
  hm_bus_wr8(ram, db | 0x098A, (uint8_t)((rnd() % 8) ? steps[rnd() % 13] : rnd()));
  hm_bus_wr16(ram, db | 0x00CF, (uint16_t)((rnd() % 2) ? 0 : rnd()));
  hm_bus_wr8(ram, db | 0x098B, (uint8_t)rnd());
  hm_wr16(ram, 0xDA, (uint16_t)((rnd() % 4) ? rnd() % 4 : rnd()));
  static const uint8_t maps[] = { 0, 2, 3, 4, 0x0B, 0x0C, 0x0D, 0x0F, 0x0F, 0x10, 0x20, 0x20 };
  uint8_t map = (uint8_t)((rnd() % 4) ? maps[rnd() % 12] : rnd() % 0x60);
  ram[0x22] = map;
  hm_bus_wr8(ram, db | 0x0022, (rnd() % 8) ? map : (uint8_t)(rnd() % 8));
  static const uint8_t hours[] = { 6, 0x10, 0x11, 0x11, 0x12, 0x12 };
  ram[0x11F1C] = (uint8_t)((rnd() % 8) ? hours[rnd() % 6] : rnd() % 0x18);
  static const uint8_t days[] = { 1, 1, 0x18, 0x18, 9, 0x0A, 0x0A, 0x0B, 0x0C, 0x0D, 0x14 };
  ram[0x11F1B] = days[rnd() % 11];
  ram[0x11F19] = (uint8_t)(rnd() % 4);
  static const uint8_t weekdays[] = { 0, 0, 1, 3, 5, 6, 6 };
  ram[0x11F1A] = weekdays[rnd() % 7];
  hm_wr16(ram, 0x00196, (uint16_t)rnd());
  hm_bus_wr8(ram, db | 0x0973, (uint8_t)rnd());
  /* scenarios: the first frame on a door with one flag, the date on its
   * edges; the last frame leaving the maps $0C-$0F at 17:00 / 18:00 */
  int scenario = (int)(rnd() % 3);
  if (scenario == 0) return;
  hm_wr16(ram, 0xD2, (uint16_t)(hm_rd16(ram, 0xD2) & ~0x0010));
  hm_wr16(ram, 0x11F60, (uint16_t)(hm_rd16(ram, 0x11F60) & ~0x0006));
  hm_wr16(ram, 0x11F6C, (uint16_t)(hm_rd16(ram, 0x11F6C) & ~0x0001));
  hm_wr16(ram, 0x00878, t);
  hm_wr16(ram, 0x0087A, t);
  if (scenario == 1) {
    static const uint8_t bits[] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 };
    uint8_t bit = bits[rnd() % 8], idx = 0;
    for (int tries = 0; tries < 1000; tries++) {
      idx = (uint8_t)rnd();
      uint32_t e = 0x80B6F5u + 8u * idx;
      if ((hm_bus_rd8(ram, e + 3) & bit) && hm_bus_rd8(ram, e + 2) < 0x60) break;
    }
    hm_wr16(ram, 0x0D, 0xF800); ram[0x0F] = 0x7E;
    ram[0x0F800 + 4 * t] = idx;
    hm_bus_wr8(ram, db | 0x098A, 0);
    if (ram[0x22] == 0x0B && rnd() % 4) ram[0x22] = 0x20;
    static const uint8_t edge_hours[] = { 0x10, 0x11 };
    ram[0x11F1C] = edge_hours[rnd() % 2];
    static const uint8_t edge_days[] = { 1, 0x18, 9, 0x0A, 0x0A, 0x0B, 0x0C, 0x0D };
    ram[0x11F1B] = edge_days[rnd() % 8];
    static const uint8_t edge_weekdays[] = { 0, 1, 5, 6 };
    ram[0x11F1A] = edge_weekdays[rnd() % 4];
    ram[0x11F19] = (uint8_t)((rnd() % 2) ? 3 : rnd() % 4);
    if (rnd() % 2) ram[0x11F19] = (ram[0x11F1B] == 1) ? 0 : (ram[0x11F1B] == 0x18) ? 3 : ram[0x11F19];
  } else {
    hm_bus_wr8(ram, db | 0x098A, 0x0D);
    static const uint8_t leave[] = { 0x0B, 0x0C, 0x0E, 0x0F, 0x10, 0x03 };
    ram[0x22] = leave[rnd() % 6];
    hm_bus_wr8(ram, db | 0x0022, ram[0x22]);
    static const uint8_t late[] = { 0x10, 0x11, 0x12 };
    ram[0x11F1C] = late[rnd() % 3];
  }
}

/* video.c: StartProgramedDMA. Channels waiting ($28), their VMAIN ($2A+)
 * and destinations ($32+), and the channels set up as the game does: to
 * VRAM ($18), CGRAM ($22), OAM ($04), now and then the other way (into
 * WRAM); short copies from WRAM or ROM. */
static void prep_prog_dma(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  hm_io_quiet();
  /* the WRAM port ($2181-$2183) away from the stack and the direct page */
  uint32_t port = 0x2000u + rnd() % 0x1C000u;
  hm_io_write_live(0x2181, (uint8_t)port);
  hm_io_write_live(0x2182, (uint8_t)(port >> 8));
  hm_io_write_live(0x2183, (uint8_t)(port >> 16));
  r->db = (rnd() % 2) ? 0x80 : 0x00;
  ram[0x28] = (uint8_t)rnd();
  for (unsigned ch = 0; ch < 8; ch++) {
    ram[0x2A + ch] = (uint8_t)((rnd() % 2) ? 0x80 : rnd());
    hm_wr16(ram, 0x32 + 2 * ch, (uint16_t)rnd());
    static const uint8_t bbad[] = { 0x18, 0x18, 0x22, 0x22, 0x04, 0x19, 0x80 };
    static const uint8_t banks[] = { 0x7E, 0x7F, 0x80, 0x9F, 0xA8 };
    uint16_t at = (uint16_t)(0x4300 + 16 * ch);
    uint8_t mode = (uint8_t)((rnd() % 4) ? rnd() % 2 : rnd() & 0x1F);
    if (rnd() % 16 == 0) mode |= 0x80;                       /* B to A */
    uint8_t bank = banks[rnd() % 5];
    uint16_t src = (uint16_t)rnd();
    if (bank == 0x7E && src < 0x2000) src = (uint16_t)(0x2000 + src);  /* not the stack or DP */
    if (bank >= 0x80) src |= 0x8000;
    uint16_t size = (uint16_t)(1 + rnd() % 0x400);
    dma_write(g_dma, at, mode);
    dma_write(g_dma, (uint16_t)(at + 1), bbad[rnd() % 7]);
    dma_write(g_dma, (uint16_t)(at + 2), (uint8_t)src);
    dma_write(g_dma, (uint16_t)(at + 3), (uint8_t)(src >> 8));
    dma_write(g_dma, (uint16_t)(at + 4), bank);
    dma_write(g_dma, (uint16_t)(at + 5), (uint8_t)size);
    dma_write(g_dma, (uint16_t)(at + 6), (uint8_t)(size >> 8));
  }
}

/* video.c: the INIDISP copy ($24), A (a brightness or a preset: 0-10, now
 * and then past the tables) */
static void prep_screen(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  hm_io_quiet();
  ram[0x24] = (uint8_t)rnd();
  r->a = (uint16_t)((rnd() % 4) ? (rnd() & 0xFF00) | rnd() % 11 : rnd());
  if (rnd() % 2) r->db = 0x80;
}

/* video.c: the clearing routines (A = a VRAM address, $AF = a size) */
static void prep_clear(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  hm_io_quiet();
  r->a = (uint16_t)rnd();
  hm_wr16(ram, 0xAF, (uint16_t)((rnd() % 2) ? 0x200 : rnd() % 0x400));
  if (rnd() % 2) r->db = 0x80;
}

/* video.c: one prepared channel ($27) */
static void prep_last_dma(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_prog_dma(ram, r, rnd);
  ram[0x27] = (uint8_t)(rnd() % 8);
}
/* video.c: LoadsFromVRAMwithOffset: X = the VRAM address, Y = the size,
 * $7E = the offset in $7E:2000 */
static void prep_vram_load(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_prog_dma(ram, r, rnd);
  r->x = (uint16_t)rnd();
  r->y = (uint16_t)(1 + rnd() % 0x800);
  hm_wr16(ram, 0x7E, (uint16_t)(rnd() % 0x8000));
}
/* video.c: the sprite pieces' copy list at $7E:A420 (ending with $FFFF) */
static void prep_piece_copies(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_prog_dma(ram, r, rnd);
  unsigned n = rnd() % 32;
  static const uint8_t banks[] = { 0x88, 0x89, 0x8A, 0x8B, 0x7E, 0x7F };
  for (unsigned i = 0; i < n; i++) {
    uint8_t bank = banks[rnd() % 6];
    uint16_t src = (uint16_t)rnd();
    if (bank >= 0x80) src |= 0x8000; else if (src < 0x2000) src = (uint16_t)(src + 0x2000);
    hm_wr16(ram, 0x0A420 + 6 * i, (uint16_t)((rnd() % 16 == 0) ? 0xFFFE : src));
    hm_wr16(ram, 0x0A422 + 6 * i, bank);
    hm_wr16(ram, 0x0A424 + 6 * i, (uint16_t)rnd());
  }
  hm_wr16(ram, 0x0A420 + 6 * n, 0xFFFF);
}
static void prep_joypad(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void));
/* video.c: UpdateGraphics: all of the above, $00 (done or not), $9A */
static void prep_update_graphics(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_piece_copies(ram, r, rnd);
  prep_joypad(ram, r, rnd);
  ram[0x00] = (uint8_t)((rnd() % 2) ? 0 : rnd());
  ram[0x9A] = (uint8_t)((rnd() % 2) ? 0x10 : 0);
  /* channel 4 as the game has it: the sprite table ($7E:A000, $220 bytes
   * to OAMDATA), not also among the prepared ones (started twice, its
   * second copy would be $10000 bytes long, through the sound ports) */
  ram[0x28] &= (uint8_t)~0x10;
  static const uint8_t oam_dma[] = { 0x00, 0x04, 0x00, 0xA0, 0x7E, 0x20, 0x02 };
  for (unsigned i = 0; i < sizeof oam_dma; i++) dma_write(g_dma, (uint16_t)(0x4340 + i), oam_dma[i]);
  for (unsigned i = 0; i < 3; i++) ram[HM_RNG_STATE + i] = (uint8_t)rnd();
  for (unsigned i = 0; i < 12; i++) ram[0x013C + i] = (uint8_t)rnd();
  ram[0x9B] = (uint8_t)rnd();
  /* Y's high byte: the interpreter (the fuzzer's original) clears it on
   * the way through 8-bit indexes, the compiled game keeps it; with 0 both
   * agree */
  r->y &= 0x00FF;
}

/* video.c: fixed copies (only the data bank and a quiet beam) */
static void prep_vram_fixed(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  hm_io_quiet();
  ram[0x28] = 0;                                           /* no other channel waiting */
  r->db = (rnd() % 2) ? 0x80 : 0x00;
}
/* video.c: SUB_82E5E7: A = the block, Y = the second set or not */
static void prep_title_block(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_vram_fixed(ram, r, rnd);
  r->a = (uint16_t)(rnd() % 4);
  r->y = (uint16_t)((rnd() % 2) ? 0 : 1 + rnd() % 0xFF);
}
static void prep_letter(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void));
/* video.c: SUB_82EA80: the letter's state (DrawLetter) and a quiet beam */
static void prep_name_letters(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_letter(ram, r, rnd);
  hm_io_quiet();
  ram[0x28] = 0;
}

/* items.c: CODE_81A94A: a tool and the bag, mostly in its shop (maps
 * $26-$28) with that map's real tile property table */
static void prep_put_down(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void));
static void prep_shelf(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_put_down(ram, r, rnd);
  hm_io_quiet();
  /* the tools that exist (0-$1B): past the table the tile to draw is
   * garbage, and its graphics would be read from the I/O area */
  uint8_t tool = (uint8_t)(rnd() % 0x1C);
  ram[0x0921] = tool;
  ram[0x0923] = (uint8_t)((rnd() % 4) ? 1 + rnd() % 0xFF : 0);
  uint8_t shop = tool < 0x19 ? 0x26 : tool == 0x19 ? 0x28 : 0x27;
  uint8_t map = (uint8_t)((rnd() % 4) ? shop : 0x26 + rnd() % 3);
  ram[0x22] = map;
  hm_wr16(ram, 0x0D, hm_bus_rd16(ram, 0x82B294u + 3u * map));
  ram[0x0F] = hm_bus_rd8(ram, 0x82B296u + 3u * map);
  /* only the channel ChangeMapTile prepares is started (random waiting
   * channels would be copies of $10000 bytes) */
  ram[0x28] = 0;
}

static void prep_sram(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void));
/* nmi.c: COP_Interrupt: the line IRQ (any data bank the game runs with) */
static void prep_irq(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  (void)ram;
  hm_io_quiet();
  r->db = (rnd() % 2) ? 0x80 : 0x00;
}
/* nmi.c: WaitForNMIATimes: as for the NMI, and A = 1-3 frames (keeping A's
 * range from the registry) */
static void prep_update_graphics(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void));
static void prep_wait_times(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  uint16_t a = r->a;
  prep_update_graphics(ram, r, rnd);
  ram[0x28] = 0;
  r->a = a;
}
/* nmi.c: WaitForNMI: the NMI with no other channel waiting (random
 * channels could copy from the I/O area; UpdateGraphics's own test has
 * them) */
static void prep_wait_frame(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_update_graphics(ram, r, rnd);
  ram[0x28] = 0;
}
/* audio.c: UNK_Audio18: the uploads' inputs (prep_upload) and a frame's
 * NMI (prep_update_graphics) */
static void prep_upload(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void));
static void prep_sound_wait(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_update_graphics(ram, r, rnd);
  prep_upload(ram, r, rnd);
  ram[0x28] = 0;
}
/* eventcmds.c: the fade commands: a script position and a frame's NMI */
static void prep_event_cmd(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void));
static void prep_event_fade(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_event_cmd(ram, r, rnd);
  prep_update_graphics(ram, r, rnd);
  hm_wr16(ram, 0x11F5A, (uint16_t)rnd());
  ram[0x28] = 0;
}
/* eventcmds.c: Event_AudioRelated3: a real noise (as for UNK_Audio24) and
 * 1-3 frames in the command's arguments, the item in hand 0-$1B */
static void prep_effect_wait(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void));
static void prep_event_sound_wait(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_event_cmd(ram, r, rnd);
  prep_effect_wait(ram, r, rnd);
  uint16_t at = hm_rd16(ram, 0xC9);
  ram[0x10000 + at + 1] = (uint8_t)r->a;
  hm_wr16(ram, 0x10000u + at + 2, r->x);
  hm_wr16(ram, 0x10000u + at + 4, r->y);
  ram[0x0921] = (uint8_t)(rnd() % 0x1C);
}
/* interact.c: CODE_81D670: its first frame ($096F != 1) or a later one
 * ($0970 often about to reach $20), an item in hand 0-$1B */
static void prep_action_sound(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  uint8_t keep[13];
  memcpy(keep, &ram[0x0103], sizeof keep);
  prep_sound_wait(ram, r, rnd);
  memcpy(&ram[0x0103], keep, sizeof keep);
  ram[0x096F] = (uint8_t)((rnd() % 2) ? 1 : (rnd() % 2) ? 0 : rnd());
  ram[0x0970] = (uint8_t)((rnd() % 2) ? 0x1F : rnd());
  ram[0x0921] = (uint8_t)(rnd() % 0x1C);
  hm_wr16(ram, 0xD2, (uint16_t)rnd());
  hm_wr16(ram, 0x11F5A, (uint16_t)rnd());
}
/* items.c: a tool taken off the shed's wall (item $2A-$43): a shelf, a
 * shop map (prep_shelf), the map's extras (prep_map_tiles), the game
 * objects (prep_destroy; the item's own in $0978), the sound chip and a
 * frame's NMI */
static void prep_map_tiles(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void));
static void prep_destroy(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void));
static void prep_tool_item(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_sound_wait(ram, r, rnd);
  prep_map_tiles(ram, r, rnd);
  prep_shelf(ram, r, rnd);
  prep_destroy(ram, r, rnd);
  ram[0x0984] = (uint8_t)(0x2A + rnd() % 26);
  hm_wr16(ram, 0x0978, (uint16_t)(rnd() % 40));
  ram[0x28] = 0;
}
/* items.c: SUB_81809A: any item 1-$5A, prepared as for its own routine */
static void prep_item(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void));
static void prep_item_food(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void));
static void prep_item_shop(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void));
static void prep_items_dispatch(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  switch (rnd() % 4) {
  case 0: prep_item(ram, r, rnd); break;
  case 1: prep_item_food(ram, r, rnd); break;
  case 2: prep_item_shop(ram, r, rnd); break;
  default: prep_tool_item(ram, r, rnd); break;
  }
  hm_io_quiet();
  hm_wr16(ram, 0x11F5E, (uint16_t)rnd());
}
/* items.c: BAAAA: the ten records of items left on the map, about three
 * in use, all with the item prepared (prep_items_dispatch) or no routine,
 * real sprites, positions near it */
static void prep_sprite(uint8_t *ram, uint16_t sprite, uint32_t (*rnd)(void));
static void prep_item_records(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_items_dispatch(ram, r, rnd);
  for (unsigned i = 0; i < 10; i++) {
    uint8_t *rec = &ram[0x0B4E6 + 16 * i];
    if (rnd() % 10 >= 3) { rec[0] = (uint8_t)(rnd() & 0xFE); continue; }
    rec[0] = (uint8_t)(1 | (rnd() & 0x06) | ((rnd() % 4) ? 0 : rnd() & 0xF8));
    rec[1] = (uint8_t)((rnd() % 4) ? 1 : 0);
    rec[2] = (uint8_t)((rnd() % 2) ? ram[0x0976] : rnd() % 9);
    rec[3] = ram[0x0984];
    uint16_t object = (uint16_t)(rnd() % 40);
    rec[4] = (uint8_t)object; rec[5] = 0;
    for (unsigned k = 0; k < 2; k++) {
      uint16_t sprite = (uint16_t)(rnd() % 0x450);
      prep_sprite(ram, sprite, rnd);
      rec[6 + 2 * k] = (uint8_t)sprite; rec[7 + 2 * k] = (uint8_t)(sprite >> 8);
    }
    if (rnd() % 2) { rec[8] = rec[6]; rec[9] = rec[7]; }
    rec[10] = (uint8_t)((rnd() % 2) ? 0 : 0x40); rec[11] = 0;
    uint16_t x = (uint16_t)(hm_rd16(ram, 0xDF) - 0x40 + rnd() % 0x80);
    uint16_t y = (uint16_t)(hm_rd16(ram, 0xE1) - 0x40 + rnd() % 0x80);
    rec[12] = (uint8_t)x; rec[13] = (uint8_t)(x >> 8); rec[14] = (uint8_t)y; rec[15] = (uint8_t)(y >> 8);
  }
  for (unsigned g = 0; g < 0x40; g++)
    if (hm_rd16(ram, 0x0A320 + 4 * g) < 40) hm_wr16(ram, 0x0A320 + 4 * g, (uint16_t)(40 + rnd() % 200));
}
/* events.c: EventInstructionInterpreter: a command 0-89 prepared as for
 * its own entry, its position also in the slot (+30, +32) */
static void prep_one_command(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  uint8_t cmd = (uint8_t)(rnd() % 90);
  uint32_t at = hm_bus_rd16(ram, 0x849419u + 2u * cmd) | 0x840000u;
  const HmDecompFn *f = NULL;
  for (int i = 0; i < g_hm_decomp_count && !f; i++)
    if (g_hm_decomp_fns[i].pc24 == at) f = &g_hm_decomp_fns[i];
  if (f && f->prep) f->prep(ram, r, rnd);
  else prep_event_cmd(ram, r, rnd);
  if (f)
    for (int v = 0; v < HM_FUZZ_MAX_VARS && f->vars[v].size; v++) {
      uint16_t x = (uint16_t)(f->vars[v].lo + rnd() % ((uint32_t)f->vars[v].hi - f->vars[v].lo + 1));
      ram[f->vars[v].addr] = (uint8_t)x;
      if (f->vars[v].size == 2) ram[f->vars[v].addr + 1] = (uint8_t)(x >> 8);
    }
  uint16_t pc = hm_rd16(ram, 0xC9);
  uint8_t bank = ram[0xCB];
  uint32_t cmd_at = ((uint32_t)bank << 16 | pc);
  if (bank == 0x7E || bank == 0x7F) ram[(cmd_at - 0x7E0000) & 0x1FFFF] = cmd;
  uint16_t slot = hm_rd16(ram, 0xCC);
  ram[slot + 0x30] = (uint8_t)pc; ram[slot + 0x31] = (uint8_t)(pc >> 8); ram[slot + 0x32] = bank;
}
/* events.c: IterateCCStructures: the game's own scripts as they are, the
 * waits about to end, the text box and the action sometimes stopping it */
static void prep_scripts(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_update_graphics(ram, r, rnd);
  ram[0x28] = 0;
  for (unsigned s = 0; s < 0x31; s++) {
    uint8_t *slot = &ram[0xB586 + 64 * s];
    if ((slot[0] & 1) && rnd() % 2) { slot[0x10] = (uint8_t)(rnd() % 3); slot[0x11] = 0; }
    if (rnd() % 4 == 0) { slot[0x03] = (uint8_t)rnd(); slot[0x04] = (uint8_t)rnd(); }
  }
  /* the one-frame bits it clears at the end */
  if (rnd() % 2) {
    ram[0x11F5B] |= 0x01; ram[0x11F5A] |= 0xD0; ram[0x11F5D] |= 0x04;
    ram[0x11F5E] |= 0x40; ram[0x11F60] |= 0x10;
  }
  ram[0x019A] = (uint8_t)((rnd() % 8) ? 0 : (rnd() % 2) ? 2 : 6);
  if (rnd() % 8 == 0) hm_wr16(ram, 0xD4, 3);
  for (unsigned i = 0; i < 3; i++) ram[HM_RNG_STATE + i] = (uint8_t)rnd();
}
/* audio.c: UNK_Audio20: a song as prep_music gives it */
static void prep_music(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void));
static void prep_song_wait(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_sound_wait(ram, r, rnd);
  prep_music(ram, r, rnd);
  r->a = (uint16_t)((r->a & 0xFF00) | ram[0x0110]);
  /* UNK_Audio20 called alone: the same song (a new one first waits until
   * the chip is not busy, which with the music playing it never is; the
   * other way is tested through UNK_Audio26, which fades the music first) */
  /* songs 0-$19 (or $FF: none): the table $80:B8CD of their instrument
   * counts has 26 entries, and a count past 10 would make UNK_Audio10 clear
   * memory round the whole bank */
  ram[0x0110] = (uint8_t)((rnd() % 8) ? rnd() % 26 : 0xFF);
  ram[0x0117] = ram[0x0110];
  ram[0x28] = 0;
}
/* audio.c: UNK_Audio26: song A, often a new one, and the map entered for
 * UNK_Audio5 */
static void prep_new_song_wait(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  /* the song playing ($0117) as the chip has it: if it said nothing plays
   * while something does, the old song would not fade and the chip would
   * stay busy for ever */
  uint8_t playing = ram[0x0117];
  prep_sound_wait(ram, r, rnd);
  prep_music(ram, r, rnd);
  ram[0x0117] = playing;
  ram[0x0110] = (uint8_t)((rnd() % 8) ? rnd() % 26 : (rnd() % 2) ? 0 : 0xFF);
  r->a = (uint16_t)((r->a & 0xFF00) | ram[0x0110]);
  ram[0x0110] = (uint8_t)rnd();
  ram[0x098B] = (uint8_t)(rnd() % 0x60);
  ram[0x11F1C] = (uint8_t)(rnd() % 24);
  ram[0x28] = 0;
}
/* audio.c: UNK_Audio24: effect A (0-$28) of bank X (6 or 7, the tools'
 * banks: with another one the sound program stops answering), then Y =
 * 1-3 frames */
static void prep_effect_wait(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_sound_wait(ram, r, rnd);
  /* a real noise: one of the tools' (Tool_Noise_Table: bank, two effects) */
  uint8_t bank = 6, effect = 0x14;
  for (int tries = 0; tries < 16; tries++) {
    uint32_t at = 0x829054u + 3u * (rnd() % 28);
    uint8_t b = hm_bus_rd8(ram, at), e = hm_bus_rd8(ram, at + 1 + rnd() % 2);
    if (b && e) { bank = b; effect = e; break; }
  }
  r->a = (uint16_t)((r->a & 0xFF00) | effect);
  r->x = (uint16_t)((rnd() & 0xFF00) | bank);
  r->y = (uint16_t)(1 + rnd() % 3);
}
/* toolsound.c: ToolUsedSound2: an item 0-$1B ($0921), the one noted
 * ($0119) often the same */
static void prep_tool_noise_wait(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_sound_wait(ram, r, rnd);
  ram[0x0921] = (uint8_t)(rnd() % 0x1C);
  ram[0x0119] = (uint8_t)((rnd() % 4) ? rnd() % 0x1C : ram[0x0921]);
}
/* nmi.c: the fades: from $92 (or $FF: from $25) to $94, 1-2 frames a
 * level ($93 = 0 would be 256), the target on the right side (else the
 * brightness byte goes round, 256 levels) */
static void prep_fade(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void), int up)
{
  prep_update_graphics(ram, r, rnd);
  ram[0x25] = (uint8_t)(rnd() % 16);
  ram[0x92] = (uint8_t)((rnd() % 8) ? rnd() % 16 : 0xFF);
  uint8_t from = ram[0x92] == 0xFF ? ram[0x25] : ram[0x92];
  ram[0x94] = up ? (uint8_t)(from + rnd() % (16 - from)) : (uint8_t)(rnd() % (from + 1));
  ram[0x93] = (uint8_t)(1 + rnd() % 2);
  hm_wr16(ram, 0x11F5A, (uint16_t)rnd());
  ram[0x28] = 0;
}
static void prep_fade_in(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void)) { prep_fade(ram, r, rnd, 1); }
static void prep_fade_out(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void)) { prep_fade(ram, r, rnd, 0); }
/* video.c: BackgroundsManager: any map (the title screens too); the
 * player's start a multiple of the step 8 (the word at $1E) or the walk
 * there never ends; bit 15 of $0196 (the picture stored unpacked) only
 * from the map's record: elsewhere the $8000 bytes copied as they are run
 * from the ROM into the I/O area */
static void prep_backgrounds(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  hm_io_quiet();
  ram[0x28] = 0;
  ram[0x22] = (uint8_t)(rnd() % 0x60);
  r->db = (rnd() % 2) ? 0x80 : 0x00;
  ram[0x1F] = 0;
  hm_wr16(ram, 0x017D, (uint16_t)(8 * (rnd() % 0x50)));
  hm_wr16(ram, 0x017F, (uint16_t)(8 * (rnd() % 0x50)));
  hm_wr16(ram, 0x0196, (uint16_t)((rnd() % 2) ? rnd() & 0x7FFF : rnd() & 0x001F));
  ram[0x11F5C] = (uint8_t)rnd();
  ram[0x11F5E] = (uint8_t)rnd();
  r->a = (uint16_t)rnd(); r->x = (uint16_t)rnd(); r->y = (uint16_t)rnd();
}
/* video.c: CODE_82E405: the saved games (some empty), the text state, a
 * line 0-2 */
static void prep_file_line(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_letter(ram, r, rnd);
  prep_sram(ram, r, rnd);
  /* the year picks its suffix ("st", "nd", "rd") from a table of three:
   * a garbage year would read letters, and DMA sources, from past it */
  if (g_sram && g_sram_size >= 0x2000)
    for (unsigned s = 0; s < 2; s++) {
      g_sram[0x1000 * s] = (uint8_t)(rnd() % 3);
      g_sram[0x1000 * s + 1] = (uint8_t)(rnd() % 4);     /* the season names, 4 */
    }
  hm_io_quiet();
  ram[0x28] = 0;
  r->a = (uint16_t)(rnd() % 3);
  r->y = (uint16_t)((rnd() % 2) ? 0 : 1);
  r->db = (rnd() % 2) ? 0x80 : 0x00;
}

static void prep_oam_copy(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void));
static void prep_draw_order(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void));
static uint32_t sprite_pieces(uint8_t *ram, uint16_t sprite);
/* objects.c: PrepareOAMData: up to 40 objects, in use or not, with real
 * sprites (their piece lists), flips, positions around the camera (some
 * pieces off screen), loaded slots or $FF; the preset ($0195) */
static void prep_prepare_oam(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_oam_copy(ram, r, rnd);
  prep_draw_order(ram, r, rnd);
  hm_io_quiet();
  r->db = (rnd() % 2) ? 0x80 : 0x00;
  uint16_t cam_x = (uint16_t)rnd(), cam_y = (uint16_t)rnd();
  hm_wr16(ram, 0xF5, cam_x);
  hm_wr16(ram, 0xF7, cam_y);
  for (unsigned i = 0; i < 40; i++) {
    uint16_t o = (uint16_t)(0x019C + 0x24 * i);
    hm_wr16(ram, o, (uint16_t)((rnd() % 4) ? 0x7777 : 0));
    uint16_t sprite = (uint16_t)(rnd() % 0x450);
    hm_wr16(ram, (uint16_t)(o + 0x02), sprite);
    static const uint16_t flips[] = { 0, 0x40, 0x80, 0xC0 };
    hm_wr16(ram, (uint16_t)(o + 0x04), (uint16_t)((rnd() % 4) ? flips[rnd() % 4] : rnd()));
    hm_wr16(ram, (uint16_t)(o + 0x08), (uint16_t)(cam_x - 32 + rnd() % 320));
    if (rnd() % 2) hm_wr16(ram, (uint16_t)(o + 0x0A), (uint16_t)(cam_y - 32 + rnd() % 288));
    uint32_t list = sprite_pieces(ram, sprite);
    hm_wr16(ram, (uint16_t)(o + 0x10), (uint16_t)list);
    uint8_t n = hm_bus_rd8(ram, list);
    if (n > 12) n = 12;
    ram[o + 0x13] = n;
    for (unsigned k = 0; k < 12; k++) ram[o + 0x14 + k] = (uint8_t)((rnd() % 6) ? rnd() % 0x40 : 0xFF);
  }
  ram[0x0195] = (uint8_t)((rnd() % 4) ? rnd() % 11 : rnd());
  ram[0x9A] = (uint8_t)rnd();
  hm_wr16(ram, 0xA1, (uint16_t)(rnd() % 0x450));
}

static void prep_animals(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_random_farm_and_weather(ram, r, rnd);      /* grass to graze, $0196 */
  static const uint8_t cow_status[] = { 0x00, 0x01, 0x03, 0x05, 0x09, 0x19, 0x29, 0x49,
                                        0x89, 0x39, 0x59, 0x08, 0x0B, 0xFF };
  static const uint8_t maps[] = { 0x27, 0x27, 0x28, 0x28, 0x00, 0x01, 0x02, 0x03, 0x04, 0x15 };
  for (int i = 0; i < 12; i++) {                   /* cows: 16 bytes at $7E:C1C6 */
    uint8_t *c = &ram[0x0C1C6 + 16 * i];
    c[0] = cow_status[rnd() % sizeof cow_status];
    c[1] = (uint8_t)rnd();
    c[2] = maps[rnd() % sizeof maps];
    static const uint8_t counters[] = { 0, 1, 2, 3, 6, 7, 13, 14, 20, 21, 255 };
    c[3] = counters[rnd() % sizeof counters];
    c[4] = (uint8_t)rnd();
    c[5] = (rnd() & 3) ? (uint8_t)(rnd() % 12) : 10;
  }
  static const uint8_t hen_status[] = { 0x00, 0x01, 0x03, 0x05, 0x09, 0x19, 0x11, 0x0F, 0xFF };
  for (int i = 0; i < 13; i++) {                   /* chickens: 8 bytes at $7E:C286 */
    uint8_t *h = &ram[0x0C286 + 8 * i];
    h[0] = hen_status[rnd() % sizeof hen_status];
    h[1] = maps[rnd() % sizeof maps];
    static const uint8_t counters[] = { 0, 1, 2, 3, 6, 7, 255 };
    h[2] = counters[rnd() % sizeof counters];
  }
  hm_wr16(ram, 0x11F60, (uint16_t)(rnd() & 0x0009));   /* bit 0: no feeding */
  hm_wr16(ram, 0x11F5A, (rnd() & 1) ? 0x1000 : 0);
  hm_wr16(ram, 0x11F68, (rnd() & 1) ? 0x0100 : 0);
  hm_wr16(ram, 0x00932, (uint16_t)rnd());               /* stalls with fodder */
  if (rnd() & 1) hm_wr16(ram, 0x00196, (uint16_t)(hm_rd16(ram, 0x00196) | 0x0400));
  /* Not both: with $7F1F5A bit $1000 and $0196 bit $0400 the original runs
   * into a BRK at $83:BDE4 (a game bug, see animals.c) and can't be compared. */
  if (hm_rd16(ram, 0x00196) & 0x0400) hm_wr16(ram, 0x11F5A, 0);
  ram[0x00931] = (uint8_t)(rnd() % 6);                  /* chicken feed */
  ram[0x0098C] = (uint8_t)(rnd() % 12);                 /* weather tomorrow */
  ram[0x11F0A] = (uint8_t)(rnd() % 13);                 /* cows */
  ram[0x11F0B] = (rnd() & 1) ? 12 : (uint8_t)(rnd() % 13); /* chickens */
  ram[0x11F12] = (uint8_t)(rnd() % 4);
  ram[0x11F32] = (rnd() & 1) ? 0x15 : (uint8_t)(rnd() % 0x15);
}

static void prep_family(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  (void)r;
  static const uint16_t hearts[] = { 0, 200, 449, 450, 451, 649, 650, 651, 999 };
  for (int i = 0; i < 5; i++) hm_wr16(ram, 0x11F1F + 2 * i, hearts[rnd() % 9]);
  static const uint16_t wives[] = { 0, 1, 2, 4, 8, 0x10, 0x20, 0x13 };
  hm_wr16(ram, 0x11F66, wives[rnd() % 8]);
  static const uint16_t days[] = { 0, 18, 19, 20, 119, 239, 359, 1000 };
  hm_wr16(ram, 0x11F3B, days[rnd() % 8]);
  static const uint16_t ages[] = { 0, 58, 59, 60, 89, 90, 179, 299, 419 };
  hm_wr16(ram, 0x11F37, ages[rnd() % 9]);
  hm_wr16(ram, 0x11F39, ages[rnd() % 9]);
  hm_wr16(ram, 0x11F6E, (uint16_t)((rnd() & 0x001C) | (rnd() & 0xFFE3)));
  hm_wr16(ram, 0x11F64, (uint16_t)rnd());
}
static void prep_money(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  (void)r;
  static const uint32_t money[] = { 0, 1, 500, 999998, 999999, 1000000, 0x7FFFFF, 0x0F423E };
  static const uint32_t change[] = { 1, 10, 0xFFFFFF, 0xFFFFF6, 0xF0BDC1, 999999, 0x800000, 0x0F423F };
  uint32_t m = (rnd() & 1) ? money[rnd() % 8] : rnd() % 1000000;
  uint32_t c = (rnd() & 1) ? change[rnd() % 8] : rnd() & 0xFFFFFF;
  hm_wr24(ram, 0x11F04, m);
  hm_wr16(ram, 0x72, (uint16_t)c); ram[0x74] = (uint8_t)(c >> 16);
}

static void prep_tile_in_front(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  r->x = (uint16_t)(rnd() & 0x3FF);                       /* a position on the map */
  r->y = (uint16_t)(rnd() & 0x3FF);
  static const uint8_t widths[] = { 1, 2, 4, 4 };
  ram[0x00181] = widths[rnd() % 4];
  for (int i = 0; i < 4096; i++) ram[0x009B6 + i] = (uint8_t)rnd();   /* tile types */
  /* property table: keep the game's one half of the time, but only if all
   * of it (4 bytes x 256 types) is real memory; whatever the game left
   * there can point below $8000 of a ROM bank, where the console reads
   * open bus (seen in the full regression, seed 51). */
  uint16_t at = hm_rd16(ram, 0x0D);
  uint8_t bank = ram[0x0F];
  bool real = bank == 0x7E || bank == 0x7F ||
              ((bank & 0x7F) < 0x7E && at >= 0x8000 && at <= 0xFBFF) ||
              ((bank & 0x7F) < 0x40 && at <= 0x1BFF);
  if (!real || (rnd() & 1)) {                             /* somewhere in ROM */
    hm_wr16(ram, 0x0D, (uint16_t)(0x8000 | (rnd() & 0x7C00)));
    ram[0x0F] = (uint8_t)(0xA0 + rnd() % 0x20);
  }
  /* facing: 0-3 in the game; larger values still read the flag table
   * inside the ROM (past $82:FFFF it would read hardware registers). */
  hm_wr16(ram, 0xDA, (uint16_t)((rnd() & 7) ? rnd() % 4 : rnd() % 0x2A00));
  hm_wr16(ram, 0x11F5C, (uint16_t)((rnd() & 3) ? 0 : 0x0008));
  static const uint8_t tools[] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
                                   0x0C, 0x0D, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x1A };
  ram[0x00921] = tools[rnd() % sizeof tools];
  static const uint8_t maps[] = { 0, 1, 2, 3, 4, 0x0F, 0x10, 0x13, 0x14, 0x3C, 0x3D, 0x50 };
  ram[0x00022] = maps[rnd() % sizeof maps];
}

static void prep_player_tool(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  uint16_t a = r->a;
  prep_tile_in_front(ram, r, rnd);
  r->a = a;
  hm_wr16(ram, 0xD6, (uint16_t)((rnd() & 3) ? 16 + rnd() % 0x3C0 : rnd()));  /* player X */
  hm_wr16(ram, 0xD8, (uint16_t)((rnd() & 3) ? 16 + rnd() % 0x3C0 : rnd()));  /* player Y */
  hm_wr16(ram, 0xDA, (uint16_t)(rnd() % 6));                                  /* facing */
  if (rnd() & 1) { r->x = (uint16_t)(rnd() % 32); r->y = (uint16_t)(rnd() % 32); }
}
static void prep_player_tool_near(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_player_tool(ram, r, rnd);
  /* the tile in front must stay on the map: player well inside it (the
   * gold hoe reaches 6 tiles ahead; off the map the original would read
   * hardware registers past the tile map), and a small A (the 8-bit
   * entries would leave a random high byte) */
  r->a &= 0x00FF;
  hm_wr16(ram, 0xD6, (uint16_t)(112 + rnd() % 0x320));
  hm_wr16(ram, 0xD8, (uint16_t)(112 + rnd() % 0x320));
}

static void prep_map_change(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  r->x = (uint16_t)(rnd() & 0x3FF);                       /* a position on the map */
  r->y = (uint16_t)(rnd() & 0x3FF);
  static const uint8_t widths[] = { 1, 2, 4, 4, 3 };
  ram[0x00181] = widths[rnd() % 5];
  /* a tile change from the table with a real size (entry 0 is 0 x 0) */
  for (int tries = 0; tries < 64; tries++) {
    uint16_t n = (uint16_t)(1 + rnd() % 48);
    uint32_t at = 0x81B363u + 6u * n;
    uint8_t rows = hm_bus_rd8(ram, at + 2), cols = hm_bus_rd8(ram, at + 3);
    if (rows && cols && rows <= 8 && cols <= 8) { r->a = n; break; }
  }
}
static void prep_map_block(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  r->x = (uint16_t)(rnd() & 0x3FF);
  r->y = (uint16_t)(rnd() & 0x3FF);
  static const uint8_t widths[] = { 1, 2, 4, 4 };
  ram[0x00181] = widths[rnd() % 4];
  hm_wr16(ram, 0x86, (uint16_t)(1 + rnd() % 4));         /* columns */
  hm_wr16(ram, 0x88, (uint16_t)(1 + rnd() % 4));         /* rows */
  ram[0x8F] = 0; ram[0x91] = 0;
}

static void prep_dma(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  (void)r;
  ram[0x27] = (uint8_t)(rnd() % 8);                         /* channel */
  static const uint8_t dest[] = { 0x18, 0x18, 0x22, 0x04 };   /* VRAM, CGRAM, OAM */
  ram[0x29] = dest[rnd() % 4];
  ram[0x28] = (uint8_t)rnd();
  hm_wr16(ram, 0xC7, (uint16_t)rnd());
  hm_wr16(ram, 0x72, (uint16_t)rnd()); ram[0x74] = (uint8_t)rnd();
}

/* QueueTileGraphicsDMA: never entry 0 (0 x 0), which ChangeMapTile filters
 * out first: with 0 rows its loop would run 32768 times. */
static void prep_tile_dma(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_map_change(ram, r, rnd);
  ram[0x28] = (uint8_t)rnd();                              /* channels queued */
  hm_wr16(ram, 0xC7, (uint16_t)rnd());                     /* DMA budget */
}
static void prep_map_change_dma(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_tile_dma(ram, r, rnd);
  if ((rnd() & 7) == 0) r->a = 0;                          /* "no change" */
}

/* objects.c: the graphics state of a sprite's pieces (slot loaded or $FF,
 * users of each slot) and the loader's queue, never full: at least 12
 * places free (a sprite has 12 pieces at most). */
static void prep_piece(uint8_t *ram, uint16_t piece, uint32_t (*rnd)(void))
{
  uint8_t slot = (rnd() % 3 == 0) ? 0xFF : (uint8_t)(rnd() & 0x3F);
  ram[0x10F00 + piece] = slot;                             /* $7F:0F00 */
  if (slot != 0xFF) hm_wr16(ram, 0xA320 + 4u * slot, (uint16_t)(rnd() % 50));
}
static void prep_queue(uint8_t *ram, unsigned max_used, uint32_t (*rnd)(void))
{
  uint16_t at = (uint16_t)((rnd() & 0x3F) << 2);
  uint16_t used = (uint16_t)(rnd() % (max_used + 1));
  hm_wr16(ram, 0xBD, at);
  hm_wr16(ram, 0xBB, (uint16_t)((at - 4 * used) & 0xFC));
}
static uint32_t sprite_pieces(uint8_t *ram, uint16_t sprite)   /* its piece list */
{
  uint8_t bank = sprite >= 0x262 ? 0x87 : 0x86;
  uint32_t table = bank == 0x87 ? 0x878080u + 2u * (sprite - 0x262) : 0x868080u + 2u * sprite;
  uint16_t entry = hm_bus_rd16(ram, table);
  return (uint32_t)bank << 16 | hm_bus_rd16(ram, (uint32_t)bank << 16 | entry);
}
static void prep_sprite(uint8_t *ram, uint16_t sprite, uint32_t (*rnd)(void))
{
  uint32_t list = sprite_pieces(ram, sprite);
  uint8_t n = hm_bus_rd8(ram, list);
  for (unsigned k = 0; k < n; k++)
    prep_piece(ram, hm_bus_rd16(ram, (list & 0xFF0000) | (uint16_t)(list + 1 + 5 * k)), rnd);
  prep_queue(ram, 50, rnd);
}
static void prep_sprite_piece(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  (void)r;
  uint16_t piece = (uint16_t)(rnd() % 0xA00);
  hm_wr16(ram, 0xB3, piece);
  hm_wr16(ram, 0xB5, (uint16_t)rnd());
  prep_piece(ram, piece, rnd);
  prep_queue(ram, 62, rnd);
}
static void prep_gobj_pieces(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  (void)r;
  uint16_t obj = (uint16_t)(0x24 * (rnd() % 40)), sprite = (uint16_t)(rnd() % 0x450);
  uint32_t list = sprite_pieces(ram, sprite);
  hm_wr16(ram, 0xA9, obj);
  hm_wr16(ram, 0x019C + obj + 0x10, (uint16_t)list);
  ram[0x7A] = (uint8_t)(list >> 16);
  prep_sprite(ram, sprite, rnd);
}
static void prep_create_gobj(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  (void)r;
  uint16_t sprite = (uint16_t)(rnd() % 0x450);
  hm_wr16(ram, 0xA1, sprite);
  hm_wr16(ram, 0x9B, (uint16_t)rnd()); hm_wr16(ram, 0x9D, (uint16_t)rnd());
  hm_wr16(ram, 0x9F, (uint16_t)rnd()); hm_wr16(ram, 0xA3, (uint16_t)rnd());
  hm_wr16(ram, 0xDC, (uint16_t)(rnd() % 41));
  int full = rnd() % 4 == 0;                               /* no room at all */
  for (unsigned i = 0; i < 40; i++)
    hm_wr16(ram, 0x019C + 0x24 * i, (full || rnd() % 2) ? (uint16_t)(rnd() | 1) : 0);
  prep_sprite(ram, sprite, rnd);
}
static void prep_spawn(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_create_gobj(ram, r, rnd);
  int any_free = 0;
  for (unsigned i = 0; i < 10; i++) {
    ram[0xB4E6 + 16 * i] = (uint8_t)rnd();
    any_free |= !(ram[0xB4E6 + 16 * i] & 1);
  }
  if (!any_free) ram[0xB4E6 + 16 * (rnd() % 10)] &= 0xFE;  /* 10 in use: hangs */
  ram[0x0974] = (uint8_t)rnd(); ram[0x0975] = (uint8_t)rnd();
  ram[0x0976] = (uint8_t)rnd(); ram[0x0984] = (uint8_t)rnd();
  hm_wr16(ram, 0x097A, hm_rd16(ram, 0xA1));
  hm_wr16(ram, 0x097E, (uint16_t)rnd());
  hm_wr16(ram, 0x0980, (uint16_t)rnd()); hm_wr16(ram, 0x0982, (uint16_t)rnd());
}

/* toolused.c: a whole tool use. The player well inside the map, the item
 * in hand, often the whole map made of one interesting tile (so the tile in
 * front is it), random finds flags, counters, and room for an effect
 * sprite (a free map object and space in the pieces queue: the original
 * would hang otherwise). */
static void prep_tool(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void), uint8_t tool,
                      const uint8_t *tiles, unsigned n_tiles)
{
  prep_player_tool_near(ram, r, rnd);
  ram[0x00921] = tool;
  static const uint8_t maps[] = { 0, 1, 2, 3, 0, 1, 2, 3, 0x04, 0x0C, 0x0F, 0x10, 0x13,
                                  0x14, 0x29, 0x2A, 0x3D };
  ram[0x00022] = maps[rnd() % sizeof maps];
  if (rnd() % 4) {
    uint8_t tile = (n_tiles && rnd() % 4) ? tiles[rnd() % n_tiles] : (uint8_t)rnd();
    memset(&ram[0x009B6], tile, 4096);
  }
  uint8_t table_bank = ram[0x0F];                          /* the game's table: sane? */
  bool game_table = (table_bank >= 0x80 && table_bank < 0xC0) || table_bank == 0x7E;
  if (rnd() % 4 || !game_table) {
    /* a tile property table of our own in WRAM ($7F:F000; restored after
     * the fuzz): most tools work on most tiles, from every side and in
     * every season, so the interesting paths are reached */
    for (unsigned i = 0; i < 256; i++) {
      uint8_t *e = &ram[0x1F000 + 4 * i];
      e[0] = (uint8_t)(rnd() % 0xFF);     /* (tile change $FF is 0 rows: a 65536-turn loop) */
      e[1] = (rnd() % 4) ? 0xFF : (uint8_t)rnd();
      e[2] = (uint8_t)((rnd() % 4) ? (rnd() & 0x7F) : rnd());
      e[3] = (rnd() % 4) ? 0xFF : (uint8_t)rnd();
    }
    hm_wr16(ram, 0x0D, 0xF000);
    ram[0x0F] = 0x7F;
  }
  ram[0x11F19] = (uint8_t)(rnd() % 4);                     /* season */
  for (unsigned i = 0; i < 3; i++) ram[HM_RNG_STATE + i] = (uint8_t)rnd();  /* the dice */
  /* the finds' flags: often all clear (nothing found yet today) */
  static const uint32_t flags[] = { 0x11F5A, 0x11F5C, 0x11F64, 0x11F66, 0x11F6A, 0x00196 };
  for (unsigned i = 0; i < 6; i++) hm_wr16(ram, flags[i], (uint16_t)((rnd() % 2) ? 0 : rnd()));
  hm_wr16(ram, 0x11F60, (uint16_t)((rnd() & ~8u) | (rnd() % 4 == 0 ? 8 : 0)));
  ram[0x0096D] = (uint8_t)(rnd() % 7);                     /* hits so far */
  ram[0x0096B] = (uint8_t)(rnd() % 9);                     /* sowing step */
  for (unsigned i = 0; i < 6; i++) ram[0x00926 + i] = (uint8_t)(rnd() % 3);  /* can, bags */
  hm_wr16(ram, 0x0092E, (uint16_t)rnd());
  ram[0x00917] = (uint8_t)(rnd() % 200); ram[0x00918] = (uint8_t)(rnd() % 200);  /* stamina */
  ram[0x28] = (uint8_t)rnd();                              /* DMA queue */
  hm_wr16(ram, 0xC7, (uint16_t)rnd());
  HmRegs dummy = *r;
  prep_spawn(ram, &dummy, rnd);                            /* game / map objects */
  static const uint16_t fx[] = { 0xB7, 0xB8, 0xC1, 0xC8, 0xC9, 0xCB };
  for (unsigned i = 0; i < 6; i++) prep_sprite(ram, fx[i], rnd);
}
#define TOOL_PREP(name, tool, ...)                                              \
  static void prep_##name(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))       \
  {                                                                             \
    static const uint8_t tiles[] = { 0, __VA_ARGS__ };                          \
    prep_tool(ram, r, rnd, tool, tiles + 1, sizeof tiles - 1);                  \
  }
TOOL_PREP(tool_none, 0x00)
TOOL_PREP(sickle, 0x01, 0x03, 0x79, 0x35, 0x05)
TOOL_PREP(hoe, 0x02, 0x01, 0x02)
TOOL_PREP(hammer, 0x03, 0x06, 0x04, 0x10, 0x0F, 0x0E, 0x0D, 0xF8)
TOOL_PREP(axe, 0x04, 0x09, 0x0A, 0x0B, 0x0C, 0x11, 0x12, 0x13, 0x14, 0xF4)
TOOL_PREP(corn, 0x05, 0x01, 0x02)
TOOL_PREP(tomato, 0x06, 0x01, 0x02)
TOOL_PREP(potato, 0x07, 0x01, 0x02)
TOOL_PREP(turnip, 0x08, 0x01, 0x02)
TOOL_PREP(medicine, 0x09)
TOOL_PREP(potion, 0x0A)
TOOL_PREP(bell, 0x0B)
TOOL_PREP(grass_seeds, 0x0C, 0x01, 0x02)
TOOL_PREP(paint, 0x0D, 0xC3, 0xE2, 0xE3, 0xE4)
TOOL_PREP(milker, 0x0E)
TOOL_PREP(brush, 0x0F)
TOOL_PREP(gold_sickle, 0x11, 0x03, 0x79, 0x35, 0x05)
TOOL_PREP(gold_hoe_any_step, 0x12, 0x01, 0x02)
static void prep_gold_hoe(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_gold_hoe_any_step(ram, r, rnd);
  ram[0x0096B] = (uint8_t)(rnd() % 6);                     /* its line: 6 tiles */
}
TOOL_PREP(gold_hammer, 0x13, 0x06, 0x04, 0x10, 0x0F, 0x0E, 0x0D, 0xF8)
TOOL_PREP(gold_axe, 0x14, 0x09, 0x0A, 0x0B, 0x0C, 0x11, 0x12, 0x13, 0x14, 0xF4)
TOOL_PREP(feather, 0x18)
/* watering: dry tilled soil (07) and some crops */
TOOL_PREP(watering_can, 0x10, 0x07, 0x1D, 0x21, 0x40)
TOOL_PREP(sprinkler, 0x15, 0x07, 0x1D, 0x21, 0x40)
/* mapedit.c: EditTileonMap. A position anywhere, any map width. */
static void prep_edit_tile(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  static const uint8_t widths[] = { 1, 2, 4, 3 };
  ram[0x0181] = widths[rnd() % 4];
  r->x = (uint16_t)(rnd() % 0x400); r->y = (uint16_t)(rnd() % 0x400);
  r->a = (uint16_t)rnd();
}
TOOL_PREP(tool_unk, 0x1B)
/* beans / gem seed: before 17:00 on their map (often) */
static void prep_beans_gem(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void), uint8_t tool,
                           uint8_t map, uint8_t tile)
{
  static const uint8_t t[1] = { 0 };
  prep_tool(ram, r, rnd, tool, t, 0);
  if (rnd() % 4) memset(&ram[0x009B6], tile, 4096);
  ram[0x11F1C] = (uint8_t)(rnd() % 24);                    /* hour */
  if (rnd() % 2) ram[0x00022] = map;
}
static void prep_beans(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void)) { prep_beans_gem(ram, r, rnd, 0x16, 0x31, 0xF6); }
static void prep_gem(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))   { prep_beans_gem(ram, r, rnd, 0x17, 0x34, 0xF6); }
/* feed: often in the barn / coop, facing a trough (E2-EE) */
static void prep_feed(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void), uint8_t tool, uint8_t map)
{
  static const uint8_t t[1] = { 0 };
  prep_tool(ram, r, rnd, tool, t, 0);
  if (rnd() % 4) memset(&ram[0x009B6], (uint8_t)(0xE0 + rnd() % 0x12), 4096);
  if (rnd() % 4) ram[0x00022] = map;
  ram[0x0092C] = (uint8_t)(rnd() % 3); ram[0x0092D] = (uint8_t)(rnd() % 3);
  ram[0x00930] = (uint8_t)rnd(); ram[0x00931] = (uint8_t)rnd();
  hm_wr16(ram, 0x00932, (uint16_t)rnd()); hm_wr16(ram, 0x00934, (uint16_t)rnd());
}
static void prep_chicken_feed(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void)) { prep_feed(ram, r, rnd, 0x19, 0x28); }
static void prep_cow_feed(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))     { prep_feed(ram, r, rnd, 0x1A, 0x27); }
/* SUB_829260: any of the 28 items, prepared as for its own routine */
static void prep_any_tool(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  static void (*const preps[])(uint8_t *, HmRegs *, uint32_t (*)(void)) = {
    prep_tool_none, prep_sickle, prep_hoe, prep_hammer, prep_axe, prep_corn, prep_tomato,
    prep_potato, prep_turnip, prep_medicine, prep_potion, prep_bell, prep_grass_seeds,
    prep_paint, prep_milker, prep_brush, prep_watering_can, prep_gold_sickle, prep_gold_hoe,
    prep_gold_hammer, prep_gold_axe, prep_sprinkler, prep_beans, prep_gem, prep_feather,
    prep_chicken_feed, prep_cow_feed, prep_tool_unk,
  };
  preps[rnd() % (sizeof preps / sizeof preps[0])](ram, r, rnd);
}
static void prep_map_animation(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  (void)ram;
  r->a = (uint16_t)(rnd() % 0x60);
}

/* ending.c: everything the ranch score adds up */
static void prep_ranch_score(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  (void)r;
  static const uint32_t words[] = { 0x11F04, 0x11F1F, 0x11F21, 0x11F23, 0x11F25, 0x11F27,
                                    0x11F33, 0x11F4A, 0x11F4C, 0x11F4E, 0x11F50, 0x11F56,
                                    0x11F64, 0x11F66, 0x11F6C, 0x11F6E, 0x11F5E };
  for (unsigned i = 0; i < sizeof words / sizeof words[0]; i++)
    hm_wr16(ram, words[i], (uint16_t)((rnd() % 4) ? rnd() % 1200 : rnd()));
  ram[0x11F06] = (uint8_t)rnd();                           /* money, high byte */
  ram[0x11F0A] = (uint8_t)((rnd() % 4) ? rnd() % 13 : rnd());   /* cows */
  ram[0x11F0B] = (uint8_t)((rnd() % 4) ? rnd() % 13 : rnd());   /* chickens */
  ram[0x00917] = (uint8_t)rnd();                           /* max stamina */
  for (unsigned i = 0; i < 12; i++) {                      /* cows: 16 bytes at $7E:C1C6 */
    ram[0x0C1C6 + 16 * i] = (uint8_t)rnd();
    ram[0x0C1C6 + 16 * i + 4] = (uint8_t)rnd();
  }
}

/* animals.c: AddNewChicken / AddNewCow: records in use or free (sometimes
 * all in use) and where the animal comes from */
static void prep_new_animal(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  int full = rnd() % 5 == 0;
  for (unsigned i = 0; i < 13; i++) ram[0x0C286 + 8 * i] = (uint8_t)(full ? (rnd() | 1) : rnd());
  for (unsigned i = 0; i < 12; i++) ram[0x0C1C6 + 16 * i] = (uint8_t)(full ? (rnd() | 1) : rnd());
  ram[0x11F19] = (uint8_t)(rnd() % 4);
  ram[0x11F2B] = (uint8_t)rnd();
  r->a = (uint16_t)((rnd() % 4) ? rnd() % 4 : rnd());
}

/* animals.c: PlaceAnimalsOnMap: animals spread over maps near the one
 * entered (often on it), the dog and the horse owned or not */
static void prep_place_animals(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  (void)r;
  static const uint8_t maps[] = { 0, 1, 2, 3, 4, 5, 7, 8, 0x0C, 0x0F, 0x10, 0x13, 0x14, 0x15,
                                  0x17, 0x18, 0x20, 0x27, 0x28, 0x31, 0x33 };
  uint8_t here = maps[rnd() % sizeof maps];
  ram[0x00022] = here;
  for (unsigned i = 0; i < 13; i++) {                      /* chickens */
    uint8_t *c = &ram[0x0C286 + 8 * i];
    for (unsigned k = 0; k < 8; k++) c[k] = (uint8_t)rnd();
    c[1] = (rnd() % 2) ? here : maps[rnd() % sizeof maps];
  }
  for (unsigned i = 0; i < 12; i++) {                      /* cows */
    uint8_t *c = &ram[0x0C1C6 + 16 * i];
    for (unsigned k = 0; k < 16; k++) c[k] = (uint8_t)rnd();
    c[2] = (rnd() % 2) ? here : maps[rnd() % sizeof maps];
  }
  hm_wr16(ram, 0x11F68, (uint16_t)rnd());
  hm_wr16(ram, 0x000D2, (uint16_t)((rnd() % 2) ? 0 : rnd()));   /* game state */
  hm_wr16(ram, 0x00196, (uint16_t)rnd());
  ram[0x11F30] = (rnd() % 2) ? here : maps[rnd() % sizeof maps];  /* dog */
  ram[0x11F31] = (rnd() % 2) ? here : maps[rnd() % sizeof maps];  /* horse */
  ram[0x11F32] = (uint8_t)((rnd() % 2) ? 0x15 : rnd() % 0x16);
  hm_wr16(ram, 0x11F2C, (uint16_t)rnd()); hm_wr16(ram, 0x11F2E, (uint16_t)rnd());
}

/* eventcmds.c: a chicken's slot running its script: the slot pointer, the
 * slot's fields, the chicken's record, the player's state and buttons */
static void prep_chicken_slot(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  (void)r;
  uint8_t n = (uint8_t)(rnd() % 13), slot = (uint8_t)(0x24 + n);
  uint16_t at = (uint16_t)(0xB586 + 64 * slot);
  hm_wr16(ram, 0xCC, at); ram[0xCE] = 0x7E;
  for (unsigned i = 0; i < 64; i++) ram[at + i] = (uint8_t)rnd();
  ram[at + 0x3F] = slot;
  if (rnd() % 2) ram[at + 0x0E] = 0xE0;
  if (rnd() % 2) ram[at + 0x0C] = 0;
  static const uint8_t status[] = { 0x01, 0x05, 0x09, 0x0B, 0x41, 0x09, 0x21, 0x0D };
  ram[0x0C286 + 8 * n] = status[rnd() % sizeof status];
  static const uint16_t actions[] = { 0, 0, 1, 2, 4, 0x0A, 0x0C, 0x0D, 0x17, 0x1B };
  hm_wr16(ram, 0xD4, actions[rnd() % (sizeof actions / sizeof actions[0])]);
  hm_wr16(ram, 0xD2, (uint16_t)((rnd() % 2) ? 0 : rnd()));            /* game state */
  hm_wr16(ram, 0x11F60, (uint16_t)((rnd() % 2) ? 0 : rnd()));
  hm_wr16(ram, 0x11F5A, (uint16_t)rnd());
  hm_wr16(ram, 0x00128, (uint16_t)((rnd() % 2) ? 0x0080 : rnd()));     /* buttons */
  ram[0x0091D] = (uint8_t)((rnd() % 2) ? 0 : rnd());                   /* in hands */
  hm_wr16(ram, 0x0093B + 2 * n, (uint16_t)((rnd() % 3) ? 0x0200 - rnd() % 3 : rnd()));
  ram[0xDE] = (uint8_t)rnd();
}

/* save.c: the saved games. Both SRAM slots random, often valid ("FARM",
 * the right checksum, a day), sometimes broken. */
static void prep_sram(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  r->a = (uint16_t)((rnd() % 4) ? rnd() % 2 : rnd());
  for (uint32_t a = 0x11F00; a < 0x11F80; a++) ram[a] = (uint8_t)rnd();
  for (uint32_t a = 0x0881; a < 0x08A1; a++) ram[a] = (uint8_t)rnd();
  if (!g_sram || g_sram_size < 0x2000) return;
  for (unsigned s = 0; s < 2; s++) {
    uint8_t *slot = g_sram + 0x1000 * s;
    if (rnd() % 2) for (unsigned i = 0; i < 0x1000; i++) slot[i] = (uint8_t)rnd();
    if (rnd() % 4) memcpy(slot + 0x3C, "FARM", 4);
    if (rnd() % 4 == 0) slot[0x3C + rnd() % 4] ^= (uint8_t)(1 + rnd() % 255);
    slot[0x03] = (uint8_t)((rnd() % 4) ? 1 + rnd() % 30 : 0);       /* the day */
    slot[0x2E] = (uint8_t)((rnd() % 4) ? rnd() % 2 : rnd());
    if (rnd() % 4) {                                                /* a right checksum */
      uint8_t keep = slot[0x2E];
      slot[0x2E] = slot[0x2F] = slot[0x30] = 0;
      uint16_t sum = 0;
      for (unsigned i = 0; i < 0x1000; i += 2) sum = (uint16_t)(sum + (slot[i] | slot[i + 1] << 8));
      slot[0x2E] = keep; slot[0x2F] = (uint8_t)sum; slot[0x30] = (uint8_t)(sum >> 8);
    }
  }
}
/* NewGameSetup: everything it sets holds something else first */
static void prep_new_game(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_sram(ram, r, rnd);
  for (uint32_t a = 0x08A1; a < 0x0900; a++) ram[a] = (uint8_t)rnd();
  static const uint16_t bytes[] = { 0x0917, 0x0921, 0x0923, 0x0926, 0x0927, 0x0928, 0x0929, 0x092A,
                                    0x092B, 0x092C, 0x092D, 0x0937, 0x098C, 0x09A3 };
  for (unsigned i = 0; i < sizeof bytes / sizeof bytes[0]; i++) ram[bytes[i]] = (uint8_t)rnd();
  hm_wr16(ram, 0x0196, (uint16_t)rnd());
}

/* eventcmds.c: a cow's slot ($18 + n) and its record ($7E:C1C6 + 16 n) */
static void prep_cow_slot(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  (void)r;
  uint8_t n = (uint8_t)(rnd() % 12), slot = (uint8_t)(0x18 + n);
  uint16_t at = (uint16_t)(0xB586 + 64 * slot);
  hm_wr16(ram, 0xCC, at); ram[0xCE] = 0x7E;
  for (unsigned i = 0; i < 64; i++) ram[at + i] = (uint8_t)rnd();
  ram[at + 0x3F] = slot;
  static const uint8_t states[] = { 0xC2, 0xE0, 0xE1, 0x00, 0x00, 0x00 };
  ram[at + 0x0E] = (rnd() % 4) ? states[rnd() % 6] : (uint8_t)rnd();
  if (rnd() % 3 == 0) ram[at + 0x0C] = 0;
  ram[at + 0x02] = (uint8_t)((rnd() % 4) ? rnd() % 4 : rnd());
  uint8_t *cow = &ram[0x0C1C6 + 16 * n];
  static const uint8_t status[] = { 0x02, 0x04, 0x08, 0x18, 0x28, 0x48, 0x0A, 0x0C, 0x01, 0x00 };
  cow[0] = (rnd() % 4) ? status[rnd() % 10] : (uint8_t)rnd();
  cow[1] = (uint8_t)((rnd() % 2) ? 0 : rnd());
  cow[3] = (uint8_t)((rnd() % 2) ? 0 : rnd());
  static const uint8_t edges[] = { 0x5F, 0x60, 0xBF, 0xC0 };       /* milk / price sizes */
  cow[4] = (rnd() % 4) ? (uint8_t)rnd() : edges[rnd() % 4];
  cow[5] = (uint8_t)(rnd() % 12);
  static const uint8_t maps[] = { 0, 1, 2, 3, 0x27, 0x27, 0x28, 0x10 };
  ram[0x22] = maps[rnd() % 8];
  static const uint8_t tools[] = { 0, 1, 2, 3, 4, 0x0E, 0x0E, 0x0E, 0x0F, 0x0F, 0x11, 0x14, 0x15, 0x0A };
  ram[0x00921] = (rnd() % 4) ? tools[rnd() % 14] : (uint8_t)rnd();
  static const uint16_t actions[] = { 0, 0, 0, 1, 2, 4, 0x0A, 0x0C, 0x0D, 0x1B };
  hm_wr16(ram, 0xD4, actions[rnd() % 10]);
  hm_wr16(ram, 0xD2, (uint16_t)((rnd() % 2) ? 0 : rnd()));
  hm_wr16(ram, 0x11F60, (uint16_t)((rnd() % 2) ? 0 : rnd()));
  static const uint16_t flags[] = { 0, 0x0004, 0x0010, 0x0040, 0x0040, 0x0040, 0x0080, 0x0100,
                                    0x0140, 0x00C0 };
  hm_wr16(ram, 0x11F5A, (rnd() % 4) ? flags[rnd() % 10] : (uint16_t)rnd());
  hm_wr16(ram, 0x11F64, (uint16_t)((rnd() % 2) ? 0 : rnd()));
  ram[0x11F0A] = (uint8_t)(rnd() % 13);                                /* cows owned */
  hm_wr16(ram, 0x00128, (uint16_t)((rnd() % 3) ? 0 : (rnd() % 2) ? 0x0080 : rnd()));
  hm_wr16(ram, 0x00953 + 2 * n, (uint16_t)((rnd() % 2) ? 0x0100 : rnd() % 0x102));
  ram[0xDE] = (uint8_t)rnd();
  hm_wr16(ram, 0xDA, (uint16_t)((rnd() % 4) ? rnd() % 5 : rnd()));     /* facing */
  hm_wr16(ram, 0xD6, (uint16_t)rnd()); hm_wr16(ram, 0xD8, (uint16_t)rnd());
  if (rnd() % 4 == 0) {          /* the player free, a tool or a potion on the cow */
    if (ram[at + 0x0E] == 0xC2 || ram[at + 0x0E] >= 0xE0) ram[at + 0x0E] = 0;
    ram[at + 0x0C] = 1;
    hm_wr16(ram, 0xD4, 0); hm_wr16(ram, 0xD2, 0); hm_wr16(ram, 0x11F60, 0);
    hm_wr16(ram, 0x00128, (rnd() % 4) ? 0 : 0x0080);
    static const uint16_t used[] = { 0x0040, 0x0040, 0x0040, 0x0080, 0x0100 };
    hm_wr16(ram, 0x11F5A, used[rnd() % 5]);
    static const uint8_t near_tools[] = { 0x0E, 0x0E, 0x0F, 0x01, 0x13 };
    ram[0x00921] = near_tools[rnd() % 5];
    static const uint8_t ok_status[] = { 0x08, 0x08, 0x04, 0x02, 0x48, 0x28 };
    cow[0] = ok_status[rnd() % 6];
    cow[1] = (rnd() % 2) ? 0 : (uint8_t)(rnd() & 0x0F);
  }
}

/* eventcmds.c: the dog's slot ($16) running its script */
static void prep_dog_slot(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  (void)r;
  uint16_t at = (uint16_t)(0xB586 + 64 * 0x16);
  hm_wr16(ram, 0xCC, at); ram[0xCE] = 0x7E;
  for (unsigned i = 0; i < 64; i++) ram[at + i] = (uint8_t)rnd();
  ram[at + 0x3F] = 0x16;
  if (rnd() % 2) ram[at + 0x0C] = 0;
  static const uint16_t moods[] = { 1, 2, 4, 8, 0x10, 0, 3, 0x12 };
  hm_wr16(ram, 0x11F58, moods[rnd() % 8]);
  ram[0x00938] = (uint8_t)((rnd() % 2) ? 0x78 : rnd() % 0x79);
  ram[0xDE] = (uint8_t)rnd();
  static const uint16_t actions[] = { 0, 0, 1, 2, 4, 0x0A, 0x0C, 0x0D, 0x17, 0x1B };
  hm_wr16(ram, 0xD4, actions[rnd() % (sizeof actions / sizeof actions[0])]);
  hm_wr16(ram, 0xD2, (uint16_t)((rnd() % 2) ? 0 : rnd()));
  hm_wr16(ram, 0x11F60, (uint16_t)((rnd() % 2) ? (rnd() & 0x10) : rnd()));
  hm_wr16(ram, 0x00128, (uint16_t)((rnd() % 2) ? 0x0080 : rnd()));
  ram[0x0091D] = (uint8_t)((rnd() % 2) ? 0 : rnd());
  hm_wr16(ram, 0xD6, (uint16_t)rnd()); hm_wr16(ram, 0xD8, (uint16_t)rnd());
}

/* eventcmds.c: a script command with random arguments. The script is put
 * in WRAM ($7F:F000-, restored after the fuzz); $CC points at a slot. */
static void prep_event_cmd(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_cc_pointer(ram, r, rnd);
  uint16_t at = (uint16_t)(0xF000 + (rnd() & 0x3FF));
  hm_wr16(ram, 0xC9, at); ram[0xCB] = 0x7F;
  for (unsigned i = 0; i < 8; i++) ram[0x10000 + at + i] = (uint8_t)rnd();
  uint16_t slot = hm_rd16(ram, 0xCC);
  for (unsigned i = 0; i < 64; i++) ram[slot + i] = (uint8_t)rnd();
  static const uint32_t words[] = { 0x11F5C, 0x11F7A, 0x00196, 0x000D2 };
  for (unsigned i = 0; i < 4; i++) hm_wr16(ram, words[i], (uint16_t)rnd());
  for (unsigned i = 0; i < 3; i++) ram[HM_RNG_STATE + i] = (uint8_t)rnd();
  if (rnd() % 4 == 0) ram[slot + 0x0D] = ram[0x10000 + at + 1];     /* "equals the roll" */
}
/* the Jumpif commands read the 24-bit address in their arguments: keep it
 * in WRAM (elsewhere the original would read hardware registers) */
static void prep_event_cmd_ptr(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_event_cmd(ram, r, rnd);
  uint16_t at = hm_rd16(ram, 0xC9);
  ram[0x10000 + at + 3] = (uint8_t)(0x7E + rnd() % 2);
  /* not into the stack or the direct page ($7E:0000-$1FFF): a write there
   * would change the command's own return address or its variables */
  if (ram[0x10000 + at + 3] == 0x7E && ram[0x10000 + at + 2] < 0x20) ram[0x10000 + at + 2] |= 0x20;
  /* nor at the very end of a bank (a 16 / 24-bit value would cross it) */
  if (ram[0x10000 + at + 2] == 0xFF) ram[0x10000 + at + 2] = 0xFE;
  if (rnd() % 4 == 0) {                                              /* "equals the value" */
    uint32_t a = (uint32_t)(ram[0x10000 + at + 3] - 0x7E) << 16 | hm_rd16(ram, 0x10000u + at + 1);
    for (unsigned k = 0; k < 3; k++) ram[(a + k) & 0x1FFFF] = ram[0x10000 + at + 4 + k];
  }
  ram[0x0018F] = (uint8_t)((rnd() % 4) ? rnd() : ram[0x10000 + at + 1]);
  hm_wr16(ram, 0x008FF, (uint16_t)((rnd() % 2) ? rnd() % 3 : rnd()));   /* waits */
  hm_wr16(ram, 0x11F5A, (uint16_t)rnd()); hm_wr16(ram, 0x11F60, (uint16_t)rnd());
  hm_wr16(ram, 0x11F04, (uint16_t)rnd()); ram[0x11F06] = (uint8_t)(rnd() % 0x10);
  static const uint16_t actions[] = { 0, 0, 1, 4, 0x0A, 0x0C, 0x0D, 0x17, 0x1B };
  hm_wr16(ram, 0xD4, actions[rnd() % (sizeof actions / sizeof actions[0])]);
  hm_wr16(ram, 0x00128, (uint16_t)((rnd() % 2) ? 0x0080 : rnd()));
  ram[0x0091D] = (uint8_t)((rnd() % 2) ? 0 : rnd());
  ram[0x0093A] = (uint8_t)((rnd() % 2) ? 0xB4 : rnd());
  if (rnd() % 2) hm_wr16(ram, 0xD2, (uint16_t)(rnd() & 0x1000));
}

/* dma.c: RemoveProgrammedDMA / UNK_SetDMA2. A's high byte is the table
 * index's high byte in RemoveProgrammedDMA (0 from every caller, usually). */
static void prep_dma_remove(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_dma(ram, r, rnd);
  r->a = (uint16_t)((rnd() % 4) ? (r->a & 0x00FF) : (r->a & 0x00FF) | (rnd() % 0x74) << 8);
  /* (beyond $73xx the table read leaves bank $80's ROM: open bus) */
}
static void prep_dma_block(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_dma(ram, r, rnd);
  r->x = (uint16_t)rnd();
}

/* text.c: the text box state */
static void prep_text_box(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_dma(ram, r, rnd);
  r->x = (uint16_t)((rnd() % 4) ? rnd() % 0x500 : rnd() % 0x2155);   /* text number: the
                                       * pointer must stay in bank $83 (ROM) */
  r->a = (uint16_t)rnd();
  static const uint8_t lines[] = { 0, 0, 1, 2, 3 };
  ram[0x0191] = (uint8_t)((rnd() % 8) ? lines[rnd() % 5] : rnd());
  hm_wr16(ram, 0x090D, (uint16_t)((rnd() % 2) ? 0x70 + rnd() % 0x20 : rnd()));
  hm_wr16(ram, 0x0146, (uint16_t)rnd());                              /* BG3 scroll Y */
  hm_wr16(ram, 0x0196, (uint16_t)rnd());
  hm_wr16(ram, 0x0973, (uint16_t)rnd());
  ram[0x019B] = (uint8_t)rnd();
  hm_wr16(ram, 0x11F5A, (uint16_t)rnd());                             /* $7F1F5A */
  hm_wr16(ram, 0x0189, (uint16_t)rnd());                              /* $018A: arrow place */
  if (rnd() % 2) ram[0x018A] = (uint8_t)(rnd() % 14);
  hm_wr16(ram, 0x018B, (uint16_t)rnd());                              /* arrow blink */
  if (rnd() % 2) ram[0x018B] = (uint8_t)((rnd() & 0x80) | (0x12 + rnd() % 4));
  if (rnd() % 4 == 0) r->a = (uint16_t)(rnd() % 3);                   /* arrow frame */
}

/* text.c: letters. Codes up to $27F (10 font groups) most of the time;
 * PrintDigit: a power 10^n (n < 8) and a number below 16 times it, as when
 * the game prints a number digit by digit (more would loop for ages). */
static void prep_letter(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_text_box(ram, r, rnd);
  r->a = (uint16_t)((rnd() % 4) ? rnd() % 0x280 : rnd());
  hm_wr16(ram, 0x0185, (uint16_t)((rnd() % 2) ? 0x5000 + 8 * (rnd() % 0x200) : rnd()));
  if (rnd() % 4 == 0) hm_wr16(ram, 0x0185, (uint16_t)((rnd() & 0xFF00) | 0x78));  /* end of line */
  static const uint32_t powers[] = { 1, 10, 100, 1000, 10000, 100000, 1000000, 10000000 };
  uint8_t n = (uint8_t)(rnd() % 8);
  ram[0x018C] = n;
  uint32_t v = (uint32_t)((((uint64_t)rnd() << 8) | (rnd() & 0xFF)) % (16ull * powers[n]));
  v &= 0xFFFFFF;
  hm_wr16(ram, 0x0192, (uint16_t)v); ram[0x0194] = (uint8_t)(v >> 16);
}
/* scroll.c: the camera and the map windows. Positions in the ranges the
 * windows use, a small speed, sometimes at a limit. */
static void prep_scroll(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_dma(ram, r, rnd);
  hm_wr16(ram, 0x10, (uint16_t)((rnd() % 4) ? rnd() % 0x440 : rnd()));
  hm_wr16(ram, 0x12, (uint16_t)((rnd() % 4) ? rnd() % 0xE0 : rnd()));
  hm_wr16(ram, 0x14, (uint16_t)((rnd() % 4) ? 0x20 * (rnd() % 0x60) : rnd()));
  hm_wr16(ram, 0x16, (uint16_t)(rnd() % 0x7000));
  hm_wr16(ram, 0x1A, (uint16_t)((rnd() % 2) ? 0x40 * (1 + rnd() % 4) : rnd()));
  hm_wr16(ram, 0x1C, (uint16_t)(rnd() % 0x400));
  hm_wr16(ram, 0x1E, (uint16_t)((rnd() % 4) ? 1 + rnd() % 8 : rnd()));
  ram[0x20] = (uint8_t)((rnd() % 4) ? rnd() % 8 : rnd());
  ram[0x21] = (uint8_t)((rnd() % 4) ? rnd() % 8 : rnd());
  for (unsigned a = 0xED; a < 0xF9; a += 2) hm_wr16(ram, a, (uint16_t)(rnd() % 0x400));
  if (rnd() % 4 == 0) hm_wr16(ram, 0xF5, hm_rd16(ram, (rnd() % 2) ? 0xED : 0xF1));
  if (rnd() % 4 == 0) hm_wr16(ram, 0xF7, hm_rd16(ram, (rnd() % 2) ? 0xEF : 0xF3));
  hm_wr16(ram, 0x0196, (uint16_t)rnd());
  /* the runs as a window left them (used as they are past the last one);
   * small, so no 16-bit read falls on the last byte of bank $7E: there the
   * recompilation takes the second byte from $7E:0000, the SNES from
   * $7F:0000 (GUIDE.md, 7) */
  for (unsigned a = 0x7E; a < 0x86; a += 2) hm_wr16(ram, a, (uint16_t)(2 * (rnd() % 0x20)));
  hm_wr16(ram, 0x12, (uint16_t)(hm_rd16(ram, 0x12) % 0xE0));
}

/* palette.c: the time-of-day palettes. A map (strict: one whose palettes
 * exist for this part of the day and for the night; an entry $FF would
 * point outside the pointer table), the target sometimes equal to the
 * shown colours (so the change ends) or one step away. */
static void palette_inputs(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void), bool strict)
{
  prep_dma(ram, r, rnd);
  static const uint8_t starts[4] = { 7, 15, 17, 18 };
  for (int tries = 0; tries < 200; tries++) {
    uint8_t map = (uint8_t)(rnd() % 0x60), hour = (uint8_t)(rnd() % 24);
    unsigned part = 0;
    while (part < 4 && hour >= starts[part]) part++;
    uint8_t now = hm_bus_rd8(ram, 0x80BB5C + map * 6 + part);
    uint8_t night = hm_bus_rd8(ram, 0x80BB5C + map * 6 + 4);
    uint8_t s0 = hm_bus_rd8(ram, 0x80BE44 + map * 2), s1 = hm_bus_rd8(ram, 0x80BE44 + map * 2 + 1);
    ram[0x22] = map; ram[0x11F1C] = hour;
    if (!strict || now < 0x75 && night < 0x75 && s0 < 0x75 && s1 < 0x75) break;
  }
  ram[0x00973] = (uint8_t)((rnd() % 4) ? 1 : rnd());
  ram[0x0017A] = (uint8_t)((rnd() % 2) ? 0x1F : rnd() % 0x20);
  ram[0x0017B] = (uint8_t)((rnd() % 2) ? hm_bus_rd8(ram, 0x80BB5C + ram[0x22] * 6 + rnd() % 5) : rnd());
  ram[0x0017C] = (uint8_t)rnd();
  hm_wr16(ram, 0x00196, (uint16_t)rnd());
  ram[0x11F19] = (uint8_t)(rnd() % 4);
  hm_wr16(ram, 0x11F3B, (uint16_t)((rnd() % 2) ? 0 : rnd()));
  hm_wr16(ram, 0x11F66, (uint16_t)((rnd() % 2) ? (1u << (rnd() % 6)) & 0x1F : rnd()));
  hm_wr16(ram, 0x11F5E, (uint16_t)((rnd() % 2) ? (0x40u << (rnd() % 4)) : rnd()));
  ram[0x92] = (uint8_t)((rnd() % 2) ? rnd() % 2 : rnd());
  /* the byte at $81 is read but never set by CustomiseSpritePalette: small,
   * so the colour table index stays in the ROM */
  ram[0x81] = (uint8_t)((rnd() % 4) ? 0 : rnd() % 0x20);
  /* the target: none, a palette of the table or the night one ($7F:0B00) */
  uint32_t target = 0;
  switch (rnd() % 4) {
  case 0: break;
  case 1: target = 0x7F0B00; break;
  default: {
    unsigned n = rnd() % 0x75;
    target = hm_bus_rd16(ram, 0x80B9FD + n * 3) | (uint32_t)hm_bus_rd8(ram, 0x80B9FD + n * 3 + 2) << 16;
  } }
  hm_wr16(ram, 0x04, (uint16_t)target); ram[0x06] = (uint8_t)(target >> 16);
  for (unsigned i = 0; i < 0x400; i++) ram[0x10900 + i] = (uint8_t)rnd();
  for (unsigned i = 0; i < 0x200; i += 2) {
    uint16_t c = (uint16_t)(rnd() & 0x7FFF);
    if (target) {
      uint16_t w = hm_bus_rd16(ram, (target + i) & 0xFFFFFF);
      unsigned how = rnd() % 4;
      if (how == 0) c = w;                                   /* already there */
      else if (how == 1) c = (uint16_t)(w ^ (1u << (rnd() % 15)));
    }
    hm_wr16(ram, 0x10D00 + i, c);
  }
  if (target && rnd() % 4 == 0)                              /* everything there */
    for (unsigned i = 0; i < 0x200; i += 2) hm_wr16(ram, 0x10D00 + i, hm_bus_rd16(ram, (target + i) & 0xFFFFFF));
  /* the arguments: a colour place (row, colour) */
  /* A (a palette, or a colour) comes from the registry's range */
  r->x = (uint16_t)((rnd() % 4) ? rnd() % 0x10 : rnd() % 0x100);
  r->y = (uint16_t)((rnd() % 4) ? rnd() % 0x10 : rnd() % 0x100);
  /* Event_TimeofDayPalette: a script with the palette byte */
  uint16_t at = (uint16_t)((rnd() % 16) ? 0xF000 + (rnd() & 0x3FF) : 0xFFFE);
  hm_wr16(ram, 0xC9, at); ram[0xCB] = 0x7F;
  ram[0x10000 + at + 1] = (uint8_t)(rnd() % 0x75);
}
/* the routines that load the map's palettes from the table need valid
 * entries; the others get any map (some have $FF for a part of the day) */
static void prep_palette(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  palette_inputs(ram, r, rnd, true);
}
static void prep_palette_any(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  palette_inputs(ram, r, rnd, false);
}

/* collision.c: who moves ($DF/$E1), how far ($E3) and the step ($E5/$E7),
 * near the player and near some event objects, on a map with random tile
 * types (prep_tile_in_front). */
static void prep_collision(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  uint16_t a = r->a;
  prep_tile_in_front(ram, r, rnd);
  r->a = a;
  /* far enough from the map's edges that no corner goes below 0 (the
   * original would read open bus outside the tile table) */
  uint16_t x = (uint16_t)(0x40 + rnd() % 0x380), y = (uint16_t)(0x40 + rnd() % 0x380);
  hm_wr16(ram, 0xDF, x); hm_wr16(ram, 0xE1, y);
  hm_wr16(ram, 0xE3, (uint16_t)((rnd() % 2) ? 0x10 * (rnd() % 3) : rnd() % 0x20));
  hm_wr16(ram, 0xE5, (uint16_t)((rnd() % 2) ? 0 : rnd() % 4));
  hm_wr16(ram, 0xE7, (uint16_t)((rnd() % 2) ? 0 : rnd() % 4));
  hm_wr16(ram, 0xD6, (uint16_t)((rnd() % 2) ? x - 0x20 + rnd() % 0x40 : rnd() % 0x400));
  hm_wr16(ram, 0xD8, (uint16_t)((rnd() % 2) ? y - 0x20 + rnd() % 0x40 : rnd() % 0x400));
  hm_wr16(ram, 0xD2, (uint16_t)((rnd() % 2) ? (rnd() & 0x0010) : rnd()));
  hm_wr16(ram, 0x11F60, (uint16_t)((rnd() % 4) ? rnd() & ~0x4000u : rnd()));
  hm_wr16(ram, 0xCC, (uint16_t)rnd()); ram[0xCE] = (uint8_t)rnd();
  for (unsigned i = 0; i < 0x31; i++) {
    uint32_t s = 0xB586 + i * 0x40;
    unsigned kind = rnd() % 4;
    ram[s] = (uint8_t)(kind == 0 ? 0 : kind == 1 ? 1 : kind == 2 ? 3 : rnd());
    ram[s + 0x0C] = (uint8_t)rnd();
    hm_wr16(ram, s + 0x1A, (uint16_t)((rnd() % 3) ? x - 0x20 + rnd() % 0x40 : rnd()));
    hm_wr16(ram, s + 0x1C, (uint16_t)((rnd() % 3) ? y - 0x20 + rnd() % 0x40 : rnd()));
  }
  /* a point of a tile for CFFFF */
  if (rnd() % 2) { r->x = (uint16_t)(x + rnd() % 0x20); r->y = (uint16_t)(y + rnd() % 0x20); }
}

/* toolanim.c: the tool's animation (UNK_ToolUsed and each tool's). Any
 * tool, prepared as for its own routine, sometimes without stamina. */
static void prep_tool_animation(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  /* half the time the watering can's (its tiles include water: "fill up") */
  if (rnd() % 2) {
    prep_watering_can(ram, r, rnd);
    if (rnd() % 2) {                                       /* water in front: the farm's */
      static const uint8_t water[] = { 0xF0, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD };
      memset(&ram[0x009B6], water[rnd() % 6], 4096);
      ram[0x00022] = (uint8_t)(rnd() % 4);
    }
  } else {
    prep_any_tool(ram, r, rnd);
  }
  hm_wr16(ram, 0xD2, (uint16_t)((rnd() % 4) ? hm_rd16(ram, 0xD2) & ~0x0008u : hm_rd16(ram, 0xD2) | 0x0008));
  ram[0x0091B] = (uint8_t)rnd();
  ram[0x00926] = (uint8_t)((rnd() % 2) ? 0 : rnd() % 0x15);
  /* the facing indexes a flag table in the ROM: past $2A00 it would reach
   * hardware registers (prep_tile_in_front) */
  hm_wr16(ram, 0xDA, (uint16_t)((rnd() % 8) ? rnd() % 4 : rnd() % 0x2A00));
  hm_wr16(ram, 0x11F5A, (uint16_t)rnd());
  if (ram[0x00921] >= 0x1C) ram[0x00921] = (uint8_t)(rnd() % 0x1C);
}

/* interact.c: SUB_82AD0E. The tile type in front (doors, signs, doors
 * with opening hours, things to pick up, the rest), a property table of our
 * own ($7F:F000; sign texts below $500 so StartTextBox reads its pointer
 * in the ROM), the date, and objects around (prep_collision). */
static void prep_interact(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  HmRegs t = *r;
  prep_text_box(ram, &t, rnd);
  prep_collision(ram, r, rnd);
  for (unsigned i = 0; i < 256; i++) {
    uint8_t *e = &ram[0x1F000 + 4 * i];
    e[0] = (uint8_t)rnd();
    e[1] = (uint8_t)((i >= 0xD0 && i < 0xE0) ? rnd() % 5 : (rnd() % 2) ? (rnd() & 0x81) : rnd());
    e[2] = (uint8_t)rnd();
    e[3] = (uint8_t)rnd();
  }
  /* the doors with opening hours: mostly rules that have some (Unk_Table13
   * byte 3: $81, $22, $02, $32, $24, $05, $03, $B9) */
  static const uint8_t rules[] = { 9, 18, 19, 20, 21, 59, 60, 62 };
  for (unsigned i = 0xC0; i < 0xD0; i++)
    if (rnd() % 4) {
      ram[0x1F000 + 4 * i] = rules[rnd() % 8];
      ram[0x1F000 + 4 * i + 1] = (uint8_t)(1 + rnd() % 0xFF);  /* a text */
    }
  hm_wr16(ram, 0x0D, 0xF000);
  ram[0x0F] = 0x7F;
  static const uint8_t bare[] = { 0x38, 0x39, 0x52, 0x53, 0x60, 0x61, 0x6E, 0x6F };
  switch (rnd() % 6) {
  case 0: r->x = (uint16_t)(0xC0 + rnd() % 0x40); break;   /* doors, signs, nothing */
  case 2: r->x = (uint16_t)(0xC0 + rnd() % 0x10); break;   /* opening hours */
  case 1: r->x = bare[rnd() % 8]; break;
  default: r->x = (uint16_t)(rnd() % 0x100); break;
  }
  ram[0x11F19] = (uint8_t)((rnd() % 8) ? rnd() % 4 : rnd());   /* season */
  ram[0x11F1A] = (uint8_t)((rnd() % 2) ? rnd() % 7 : 6 * (rnd() % 2));  /* weekday */
  ram[0x11F1C] = (uint8_t)((rnd() % 2) ? rnd() % 24 : 16 + rnd() % 3);  /* hour */
  hm_wr16(ram, 0x0196, (uint16_t)rnd());
  hm_wr16(ram, 0xDA, (uint16_t)(rnd() % 4));
}

/* playerinput.c: the buttons while walking. The pad (held, new, and the
 * events' $08FD) with one or two buttons, the game state's bits, the
 * action, and what A and Y need (prep_interact / prep_tool_animation). */
static void prep_player_input(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  HmRegs keep = *r;
  if (rnd() % 2) prep_tool_animation(ram, r, rnd); else prep_interact(ram, r, rnd);
  r->a = keep.a; r->x = keep.x; r->y = keep.y;
  static const uint16_t keys[] = { 0x8000, 0x4000, 0x2000, 0x1000, 0x0800, 0x0400, 0x0200,
                                   0x0100, 0x0080, 0x0040, 0x0020, 0x0010 };
  static const uint32_t pads[] = { 0x00124, 0x00128, 0x008FD };
  for (unsigned i = 0; i < 3; i++) {
    uint16_t v = 0;
    switch (rnd() % 4) {
    case 0: break;
    case 1: v = keys[rnd() % 12]; break;
    case 2: v = (uint16_t)(keys[rnd() % 12] | keys[rnd() % 12]); break;
    default: v = (uint16_t)rnd(); break;
    }
    hm_wr16(ram, pads[i], v);
  }
  uint16_t st = (uint16_t)((rnd() % 8) ? 0x4000 : 0);
  static const uint16_t bits[] = { 0x0002, 0x0010, 0x0020, 0x0200, 0x0800, 0x1000, 0x0004 };
  for (unsigned i = 0; i < 7; i++) if (rnd() % 6 == 0) st |= bits[i];
  if (rnd() % 8 == 0) st = (uint16_t)rnd();
  hm_wr16(ram, 0xD2, st);
  static const uint16_t actions[] = { 0, 0, 1, 1, 2, 3, 4, 9, 0x0F, 0x0F, 0x11, 0x11, 0x12, 0x12 };
  hm_wr16(ram, 0xD4, (uint16_t)((rnd() % 8) ? actions[rnd() % 14] : rnd() % 0x40));
  hm_wr16(ram, 0x11F5C, (uint16_t)((rnd() % 8) ? rnd() & ~0x20u : rnd()));
  hm_wr16(ram, 0x11F60, (uint16_t)((rnd() % 2) ? rnd() & 0x7FF9 : rnd()));
  hm_wr16(ram, 0x11F68, (uint16_t)rnd());
  hm_wr16(ram, 0x0911, (uint16_t)(rnd() % 4));
  /* the facing is always 0-3 in the game: CEEEE / CBBBB jump through a
   * 4-entry table with it */
  hm_wr16(ram, 0xDA, (uint16_t)(rnd() % 4));
  if (rnd() % 2) ram[0x00921] = (uint8_t)(rnd() % 0x1C);
  for (unsigned i = 0; i < 3; i++) ram[HM_RNG_STATE + i] = (uint8_t)rnd();
  /* sometimes an even floor of one tile type, with its own property (free,
   * jumpable $20 / $40) and facing every side (+3 = $FF), and no event
   * objects: so jumps land, things are put down and the dog is let go */
  if (rnd() % 2) {
    static const uint8_t types[] = { 0x10, 0x20, 0x40, 0xC4, 0xE1, 0xF5, 0x30, 0x60 };
    uint8_t ty = types[rnd() % 8];
    for (unsigned i = 0; i < 256; i++) {
      uint8_t *e = &ram[0x1F000 + 4 * i];
      e[0] = (uint8_t)rnd(); e[1] = (uint8_t)rnd(); e[2] = (uint8_t)rnd(); e[3] = (uint8_t)rnd();
    }
    static const uint8_t props[] = { 0x00, 0x20, 0x40, 0x60, 0x01, 0x02, 0x04, 0x08, 0x10 };
    ram[0x11F19] = (uint8_t)(rnd() % 4);
    ram[0x1F000 + 4 * ty + 2] = props[rnd() % 9];          /* (read with A = 2: bytes +2, +3) */
    ram[0x1F000 + 4 * ty + 3] = 0xFF;
    hm_wr16(ram, 0x0D, 0xF000); ram[0x0F] = 0x7F;
    memset(&ram[0x009B6], ty, 4096);
    hm_wr16(ram, 0x11F5C, (uint16_t)(hm_rd16(ram, 0x11F5C) & ~0x0008u));   /* (bit 3: no tile answers) */
    if (rnd() % 2) for (unsigned i = 0; i < 0x31; i++) ram[0xB586 + i * 0x40] = 0;
  }
  /* the player away from the map's edges: a jump's corner below 0 would
   * make the original read open bus (prep_collision) */
  for (unsigned a = 0xD6; a <= 0xD8; a += 2)
    if (hm_rd16(ram, a) < 0x40 || hm_rd16(ram, a) >= 0x3C0) hm_wr16(ram, a, (uint16_t)(0x40 + rnd() % 0x380));
}

/* mapedit.c: UNK_PartialMap. A map of random tile types with clusters of
 * the joining ones ($73-$79), a property table of our own ($7F:F000) whose
 * byte +0 (the drawing) is a tile change with a real size, or 0. */
static void prep_partial_map(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_map_change(ram, r, rnd);
  uint8_t good[48]; unsigned n = 0;
  for (uint16_t k = 1; k <= 48; k++) {
    uint32_t at = 0x81B363u + 6u * k;
    uint8_t rows = hm_bus_rd8(ram, at + 2), cols = hm_bus_rd8(ram, at + 3);
    if (rows && cols && rows <= 8 && cols <= 8) good[n++] = (uint8_t)k;
  }
  for (unsigned i = 0; i < 256; i++) {
    uint8_t *e = &ram[0x1F000 + 4 * i];
    e[0] = (uint8_t)((rnd() % 4 && n) ? good[rnd() % n] : 0);
    e[1] = (uint8_t)rnd(); e[2] = (uint8_t)rnd(); e[3] = (uint8_t)rnd();
  }
  /* the joining types: their drawing plus the join (up to 8) stays a real
   * tile change */
  for (unsigned i = 0x73; i < 0x7A; i++) ram[0x1F000 + 4 * i] = (uint8_t)(n ? good[rnd() % n] : 1);
  hm_wr16(ram, 0x0D, 0xF000); ram[0x0F] = 0x7F;
  for (unsigned i = 0; i < 4096; i++) {
    unsigned k = rnd() % 8;
    ram[0x009B6 + i] = (uint8_t)(k < 3 ? 0x73 + rnd() % 7 : k < 5 ? 0 : rnd());
  }
  ram[0x00181] = 4;                                        /* 64 tiles wide */
}

/* eventcmds.c: commands 50-59. The script at $7F:F000- (prep_event_cmd),
 * what is carried (a chicken $25 / $26 from slot $24 + n, item $29, the
 * dog), the busy states, and each command's arguments. */
static void prep_event_50s(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_event_cmd(ram, r, rnd);
  static const uint8_t items[] = { 0, 0, 0x25, 0x26, 0x29, 0x03 };
  ram[0x0091D] = (uint8_t)((rnd() % 8) ? items[rnd() % 6] : rnd());
  ram[0x00920] = (uint8_t)(0x24 + rnd() % 8);
  uint16_t st = (uint16_t)((rnd() % 2) ? 0 : rnd());
  if (rnd() % 3 == 0) st |= 0x0800;
  hm_wr16(ram, 0xD2, st);
  static const uint16_t actions[] = { 0, 1, 2, 0x0A, 0x0C, 0x0D, 0x1B };
  hm_wr16(ram, 0xD4, actions[rnd() % 7]);
  hm_wr16(ram, 0x0128, (uint16_t)((rnd() % 2) ? 0x0080 : rnd()));
  hm_wr16(ram, 0x11F60, (uint16_t)((rnd() % 2) ? rnd() & ~0x0006u : rnd()));
  uint16_t slot = hm_rd16(ram, 0xCC);
  ram[slot + 0x0C] = (uint8_t)((rnd() % 2) ? 1 : 0);
  hm_wr16(ram, 0xDA, (uint16_t)(rnd() % 4));
  ram[0x11F19] = (uint8_t)(rnd() % 4);
  uint16_t at = hm_rd16(ram, 0xC9);
  uint8_t *s = &ram[0x10000 + at];
  /* 54: <text word> <lines>: a text whose pointer is in the ROM */
  s[1] = (uint8_t)rnd(); s[2] = (uint8_t)(rnd() % 5); s[3] = (uint8_t)(rnd() % 4);
  ram[0x0191] = s[3];
}
/* 55: <address 24 in WRAM> <bit> <target word> */
static void prep_event_55(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_event_50s(ram, r, rnd);
  uint8_t *s = &ram[0x10000 + hm_rd16(ram, 0xC9)];
  s[1] = (uint8_t)rnd(); s[2] = (uint8_t)(0x20 + rnd() % 0xDF); s[3] = (uint8_t)(0x7E + rnd() % 2);
  s[4] = (uint8_t)((rnd() % 8) ? rnd() % 16 : rnd());
}
/* 58: <tile> <x word> <y word>, on the farm */
static void prep_event_58(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_event_50s(ram, r, rnd);
  uint8_t *s = &ram[0x10000 + hm_rd16(ram, 0xC9)];
  uint16_t x = (uint16_t)(rnd() % 0x400), y = (uint16_t)(rnd() % 0x400);
  s[2] = (uint8_t)x; s[3] = (uint8_t)(x >> 8); s[4] = (uint8_t)y; s[5] = (uint8_t)(y >> 8);
}
/* 56: a tool in hand, as for UNK_ToolUsed */
static void prep_event_tool(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_tool_animation(ram, r, rnd);
  HmRegs keep = *r;
  prep_event_cmd(ram, r, rnd);
  *r = keep;
  if (rnd() % 4 == 0) ram[0x00921] = 0;
}

/* eventcmds.c: Event_UNKPlayerPos. The script and slot (prep_event_cmd),
 * the requests in $7F1F5C, $7F1F32 = $15 or not, the idle timer near 120,
 * the random byte, and for "pressed A on it" what prep_collision gives
 * (room in front, objects around). */
static void prep_player_pos(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  HmRegs keep = *r;
  prep_collision(ram, r, rnd);
  *r = keep;
  prep_event_cmd(ram, r, rnd);
  uint16_t req = 0;
  static const uint16_t bits[] = { 0x0010, 0x0020, 0x0040, 0x0400, 0x0800 };
  for (unsigned i = 0; i < 5; i++) if (rnd() % 3 == 0) req |= bits[i];
  hm_wr16(ram, 0x11F5C, (uint16_t)((rnd() % 2) ? req : rnd()));
  ram[0x11F32] = (uint8_t)((rnd() % 4) ? 0x15 : rnd());
  ram[0x00939] = (uint8_t)((rnd() % 2) ? 0x78 : rnd() % 0x79);
  ram[0xDE] = (uint8_t)rnd();
  uint16_t slot = hm_rd16(ram, 0xCC);
  ram[slot + 0x02] = (uint8_t)((rnd() % 4) ? rnd() % 4 : rnd());
  ram[slot + 0x0C] = (uint8_t)((rnd() % 2) ? 1 : 0);
  ram[slot + 0x3F] = (uint8_t)(rnd() % 0x18);
  hm_wr16(ram, slot + 0x1A, (uint16_t)rnd()); hm_wr16(ram, slot + 0x1C, (uint16_t)rnd());
  static const uint16_t actions[] = { 0, 1, 2, 4, 0x0A, 0x0C, 0x0D, 0x1B };
  hm_wr16(ram, 0xD4, actions[rnd() % 8]);
  hm_wr16(ram, 0xD2, (uint16_t)((rnd() % 2) ? 0 : rnd()));
  hm_wr16(ram, 0x11F60, (uint16_t)((rnd() % 2) ? rnd() & ~0x0006u : rnd()));
  hm_wr16(ram, 0x0128, (uint16_t)((rnd() % 2) ? 0x0080 : rnd()));
  hm_wr16(ram, 0x0913, (uint16_t)(rnd() % 4));            /* a direction: CEEEE's table */
  if (rnd() % 3 == 0) {                 /* a free floor of type $C0-$CF, hands empty */
    uint8_t ty = (uint8_t)(0xC0 + rnd() % 0x10);
    for (unsigned i = 0; i < 1024; i++) ram[0x1F000 + i] = (uint8_t)rnd();
    ram[0x1F000 + 4 * ty + 2] = 0; ram[0x1F000 + 4 * ty + 3] = 0xFF;
    hm_wr16(ram, 0x0D, 0xF000); ram[0x0F] = 0x7F;
    memset(&ram[0x009B6], ty, 4096);
    hm_wr16(ram, 0x11F5C, (uint16_t)(hm_rd16(ram, 0x11F5C) & ~0x0808u));
    hm_wr16(ram, 0xD2, (uint16_t)(hm_rd16(ram, 0xD2) & ~0x0016u));
    ram[slot + 0x0C] = 1;
    hm_wr16(ram, 0x0128, 0x0080);
  }
}

/* math.c: DivisionSigned. Dividend and divisor of either sign; divisors
 * mostly small (both paths, 0, and the edges $00FE / $00FF, $8000). */
static void prep_division_signed(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  (void)r;
  hm_wr16(ram, 0x7E, (uint16_t)rnd());
  static const uint16_t edges[] = { 0, 0x00FE, 0x00FF, 0x8000, 0xFF01, 0xFF02, 1, 0xFFFF };
  uint16_t d;
  switch (rnd() % 4) {
  case 0: d = edges[rnd() % 8]; break;
  case 1: d = (uint16_t)rnd(); break;
  default: d = (uint16_t)(rnd() % 600); if (rnd() % 2) d = (uint16_t)(-d); break;
  }
  hm_wr16(ram, 0x80, d);
}

/* decompress.c: a compressed stream of our own at $7F:F000 (a size of
 * 1-$400, then random control bytes and items), written to $7F:C000;
 * size 0 would run 65536 bytes. */
static void prep_decompress(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  (void)r;
  uint16_t size = (uint16_t)(1 + rnd() % 0x400);
  ram[0x1F000] = (uint8_t)size; ram[0x1F001] = (uint8_t)(size >> 8);
  ram[0x1F002] = 0; ram[0x1F003] = 0;
  for (unsigned i = 4; i < 0xA00; i++) ram[0x1F000 + i] = (uint8_t)rnd();
  if (rnd() % 2)                                           /* mostly literals */
    for (unsigned i = 4; i < 0xA00; i += 9) ram[0x1F000 + i] |= (uint8_t)rnd();
  hm_wr16(ram, 0x72, 0xF000); ram[0x74] = 0x7F;
  hm_wr16(ram, 0x75, 0xC000); ram[0x77] = 0x7F;
  for (unsigned i = 0; i < 0x80; i++) ram[0x80 + i] = (uint8_t)rnd();   /* leftovers */
  ram[0x72] = 0x00; ram[0x73] = 0xF0; ram[0x74] = 0x7F;
  ram[0x75] = 0x00; ram[0x76] = 0xC0; ram[0x77] = 0x7F;
}

/* scroll.c: SUB_809EBC / SUB_809F61. The player and the camera, the
 * camera's limits (sometimes the camera at one), the map (the shed $26,
 * $31 and up), $0196's background bits, the facing, the cloud timer. */
static void prep_camera(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  (void)r;
  uint16_t lx = (uint16_t)(rnd() % 0x100), ly = (uint16_t)(rnd() % 0x100);
  hm_wr16(ram, 0xED, lx); hm_wr16(ram, 0xEF, ly);
  hm_wr16(ram, 0xF1, (uint16_t)(lx + rnd() % 0x300)); hm_wr16(ram, 0xF3, (uint16_t)(ly + rnd() % 0x300));
  hm_wr16(ram, 0xD6, (uint16_t)((rnd() % 8) ? rnd() % 0x400 : rnd()));
  hm_wr16(ram, 0xD8, (uint16_t)((rnd() % 8) ? rnd() % 0x400 : rnd()));
  hm_wr16(ram, 0xF5, (uint16_t)((rnd() % 4) ? lx + rnd() % 0x300 : hm_rd16(ram, (rnd() % 2) ? 0xED : 0xF1)));
  hm_wr16(ram, 0xF7, (uint16_t)((rnd() % 4) ? ly + rnd() % 0x300 : hm_rd16(ram, (rnd() % 2) ? 0xEF : 0xF3)));
  static const uint8_t maps[] = { 0, 1, 4, 0x10, 0x26, 0x31, 0x33, 0x3C };
  ram[0x22] = (uint8_t)((rnd() % 4) ? maps[rnd() % 8] : rnd());
  hm_wr16(ram, 0x0196, (uint16_t)((rnd() % 2) ? (2u << (rnd() % 3)) : rnd()));
  hm_wr16(ram, 0xDA, (uint16_t)((rnd() % 8) ? rnd() % 4 : rnd() % 8));
  ram[0x091C] = (uint8_t)((rnd() % 2) ? 9 : rnd() % 10);
  hm_wr16(ram, 0x1E, (uint16_t)((rnd() % 4) ? 1 + rnd() % 4 : rnd()));
  hm_wr16(ram, 0x0140, (uint16_t)rnd()); hm_wr16(ram, 0x0142, (uint16_t)rnd());
}

/* Event_EditTileonMap: positions on the map ($0181: 1, 2 or 4 screens) */
static void prep_event_tile(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_event_58(ram, r, rnd);
  static const uint8_t widths[] = { 1, 2, 4 };
  ram[0x00181] = widths[rnd() % 3];
}
/* Event_UNKTools: tools in the hand and the backpack */
static void prep_event_shed(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_event_cmd(ram, r, rnd);
  ram[0x00921] = (uint8_t)((rnd() % 4) ? rnd() % 0x1C : 0);
  ram[0x00923] = (uint8_t)((rnd() % 4) ? rnd() % 0x1C : 0);
  for (unsigned i = 0; i < 8; i++) ram[0x11F00 + i] = (uint8_t)rnd();
}

/* coloranim.c: the colour animations. 16 channels with lists of our own
 * in WRAM ($7F:E000: colours, delays, a jump back, sometimes the end), the
 * frames left, the colours and rows; rain or not; the map and the hour for
 * CallIndexedSubrutines; arguments for UNK_SetPointer42 (colour, row and a
 * channel below 16) and the Zeroes routines (a channel below 16). */
static void prep_colour_anim(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  for (unsigned c = 0; c < 16; c++) {
    uint16_t list = (uint16_t)(0xE000 + c * 0x40);
    unsigned steps = 1 + rnd() % 6;
    for (unsigned s = 0; s < steps; s++) {
      uint16_t col = (uint16_t)(rnd() & 0x7FFF);
      ram[0x10000 + list + 3 * s] = (uint8_t)col; ram[0x10000 + list + 3 * s + 1] = (uint8_t)(col >> 8);
      ram[0x10000 + list + 3 * s + 2] = (uint8_t)(rnd() % 8);
    }
    uint16_t end = (uint16_t)(list + 3 * steps);
    if (rnd() % 4) {                                        /* a loop back */
      ram[0x10000 + end] = 0xFE; ram[0x10000 + end + 1] = 0xFF;
      ram[0x10000 + end + 2] = (uint8_t)list; ram[0x10000 + end + 3] = (uint8_t)(list >> 8);
      ram[0x10000 + end + 4] = 0x7F;
    } else {
      ram[0x10000 + end] = 0xFF; ram[0x10000 + end + 1] = 0xFF;
    }
    bool used = c < 4 ? rnd() % 2 : (c < 4 + rnd() % 13);
    uint16_t at = (uint16_t)(list + 3 * (rnd() % (steps + 1)));
    hm_wr16(ram, 0x42 + 3 * c, used ? at : 0);
    ram[0x44 + 3 * c] = used ? 0x7F : 0;
    ram[0x014A + c] = (uint8_t)((rnd() % 2) ? 0 : rnd() % 4);
    ram[0x015A + c] = (uint8_t)(rnd() % 16);
    ram[0x016A + c] = (uint8_t)(rnd() % 16);
  }
  hm_wr16(ram, 0x0196, (uint16_t)((rnd() % 2) ? (rnd() & 0x000A) : rnd()));
  ram[0x22] = (uint8_t)(rnd() % 0x60);
  ram[0x11F1C] = (uint8_t)(rnd() % 24);
  ram[0x11F19] = (uint8_t)(rnd() % 4);
  ram[0x27] = (uint8_t)(rnd() % 8); ram[0x28] = (uint8_t)rnd();   /* the DMA queue */
  hm_wr16(ram, 0xC7, (uint16_t)rnd());
  hm_wr16(ram, 0x72, (uint16_t)(0xE000 + (rnd() % 16) * 0x40)); ram[0x74] = 0x7F;
  r->a = (uint16_t)((r->a & 0xFF00) | (rnd() % 16));
  r->x = (uint16_t)(rnd() % 16);
  r->y = (uint16_t)(rnd() % 16);
}

/* joypad.c: ReadJoypad. Random buttons on both pads (a held one, often the
 * same as last frame's), last frame's and the extra words, the hold counter
 * near 30, $00 sometimes not 0. */
static void prep_joypad(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  (void)r;
  for (int pad = 0; pad < 2; pad++) {
    uint16_t now = (uint16_t)(rnd() & 0x0FFF);
    RtlSetPadState(pad, now);
    now = RtlGetPadState(pad);
    unsigned base = pad ? 0x0130 : 0x0124;               /* the buttons kept */
    hm_wr16(ram, base, (uint16_t)((rnd() % 3) ? joypad_auto_read_word(now) : rnd()));
    hm_wr16(ram, base + 0x0A, (uint16_t)((rnd() % 4) ? 0 : rnd()));
  }
  ram[0x23] = (uint8_t)((rnd() % 2) ? 24 + rnd() % 8 : rnd());
  ram[0x24] = (uint8_t)((rnd() % 2) ? 0 : rnd());
  ram[0x00] = (uint8_t)((rnd() % 8) ? 0 : rnd());
}

/* text.c: PrintTextFrame. A text of our own at $7F:E000 (restored after
 * the fuzz): letters, spaces, waits, choices, names, 8-digit numbers (as
 * the game prints them: fewer digits than the number has would loop for a
 * long time) and the end. */
static void prep_printer(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_letter(ram, r, rnd);
  uint16_t *text = (uint16_t *)&ram[0x1E000];
  for (unsigned i = 0; i < 96; ) {
    unsigned k = rnd() % 16;
    if (k < 8) text[i++] = (uint16_t)(rnd() % 0x280);               /* a letter */
    else if (k < 10) text[i++] = 0x00B1;                           /* a space */
    else if (k == 10) text[i++] = (rnd() % 2) ? 0x00A2 : 0xFFFF;   /* wait / end */
    else if (k == 11) { text[i++] = 0xFFFE; text[i++] = (uint16_t)(1 + rnd() % 4); }
    else if (k == 12) { text[i++] = 0xFFFD; text[i++] = (uint16_t)(1 + rnd() % 8);
                        text[i++] = (uint16_t)(1 + rnd() % 8); }
    else if (k == 13) { text[i++] = 0xFFFC; text[i++] = 8; text[i++] = (uint16_t)(1 + rnd() % 12); }
    else text[i++] = (uint16_t)(0xBC + rnd() % 10);                /* a digit letter */
  }
  hm_wr16(ram, 0x01, 0xE000); ram[0x03] = 0x7F;
  hm_wr16(ram, 0x0187, (uint16_t)(rnd() % 80));
  ram[0x0189] = (uint8_t)((rnd() % 2) ? 4 : rnd() % 6);
  ram[0x018C] = 0;
  if (rnd() % 4 == 0) {                                            /* half-way */
    uint8_t n = (uint8_t)(1 + rnd() % 8);
    ram[0x018C] = n; ram[0x018D] = (uint8_t)(n + rnd() % 3);
    static const uint32_t powers[] = { 1, 10, 100, 1000, 10000, 100000, 1000000, 10000000 };
    uint32_t v = (uint32_t)(rnd() % (10u * powers[n - 1])) & 0xFFFFFF;
    hm_wr16(ram, 0x0192, (uint16_t)v); ram[0x0194] = (uint8_t)(v >> 16);
  }
  ram[0x018F] = (uint8_t)(rnd() % 5);
  ram[0x0190] = (uint8_t)(rnd() % 2);
  ram[0x019B] = (uint8_t)((rnd() % 4) ? (0x01 | (rnd() & 0x9E)) : rnd());
  /* the numbers' variables are 8 digits at most; keep them so */
  for (uint32_t a = 0x11F04; a < 0x11F80; a += 2) if (rnd() % 2) hm_wr16(ram, a, (uint16_t)rnd());
  ram[0x11F06] = (uint8_t)(rnd() % 0x98);
}
/* menus.c: the buttons (one at a time, mostly), the menu state, the
 * saved games (for the load menu) and a text box (for the list). */
static void prep_menu(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_text_box(ram, r, rnd);
  HmRegs keep = *r;
  prep_sram(ram, r, rnd);
  *r = keep;
  static const uint16_t buttons[] = { 0x8000, 0x1000, 0x0800, 0x0400, 0x0200, 0x0100, 0x0080, 0 };
  hm_wr16(ram, 0x0128, (rnd() % 4) ? buttons[rnd() % 8] : (uint16_t)rnd());
  hm_wr16(ram, 0x012C, (rnd() % 4) ? buttons[rnd() % 8] : (uint16_t)rnd());
  ram[0x098D] = (uint8_t)((rnd() % 4) ? rnd() % 3 : rnd());
  ram[0x098E] = (uint8_t)((rnd() % 4) ? rnd() % 2 : rnd());
  ram[0x018A] = (uint8_t)((rnd() % 4) ? rnd() % 22 : rnd());
  ram[0x94] = (uint8_t)rnd(); ram[0x95] = (uint8_t)rnd(); ram[0x97] = (uint8_t)rnd();
  hm_wr16(ram, 0x90, (uint16_t)rnd());
  if (rnd() % 2) hm_wr16(ram, 0x11F5A, (uint16_t)(hm_rd16(ram, 0x11F5A) | 0x8000));
  hm_wr16(ram, 0x0124, (rnd() % 2) ? buttons[rnd() % 8] : (uint16_t)rnd());     /* held */
  ram[0x018E] = (uint8_t)((rnd() % 4) ? rnd() % 6 : rnd());        /* a choice: options */
  ram[0x018F] = (uint8_t)((rnd() % 4) ? rnd() % 6 : rnd());
  static const uint8_t box[] = { 0x01, 0x03, 0x05, 0x09, 0x11, 0x21, 0x00, 0x13 };
  if (rnd() % 2) ram[0x019B] = box[rnd() % 8];
  if (rnd() % 3 == 0) {                                             /* a choice open */
    static const uint16_t moves[] = { 0x0400, 0x0800, 0x0100, 0x0200, 0x0080 };
    ram[0x019B] = 0x11;
    hm_wr16(ram, 0x0128, moves[rnd() % 5]);
  }
}

static void prep_letter_dma(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_dma_block(ram, r, rnd);
  r->a = (uint16_t)((rnd() % 4) ? rnd() % 2 : rnd());
}

/* audio.c: the commands the game really sends, and sound-effect numbers.
 * The original talks to the real (emulated) sound chip; its answers are
 * replayed to the rewrite. */
static void prep_audio(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  ram[0x92] = 0x0C;          /* UNK_Audio8 alone: $0A/$0B would leave the chip
                              * waiting for their arguments */
  /* only sound effects the game plays (other values can be orders that
   * leave the chip waiting for data the CPU never sends) */
  static const uint8_t effects[] = { 0x01, 0x03, 0x0F, 0x11, 0x14, 0x17, 0x1B, 0x28 };
  ram[0x0114] = effects[rnd() % sizeof effects];
  ram[0x0115] = (rnd() % 2) ? 6 : 7;
  ram[0x0116] = 0x0A;
  r->a = (uint16_t)(r->a & 0xFF00);                   /* UNK_Audio2: the game passes 0 */
}

/* toolsound.c: the item in hand (the 28 of Tool_Noise_Table) and $0922
 * (0 landed, 1 missed, 2 silent; sometimes 3, the next item's first). */
static void prep_tool_sound(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_audio(ram, r, rnd);
  ram[0x0921] = (uint8_t)(rnd() % 28);
  ram[0x0922] = (uint8_t)((rnd() % 8) ? rnd() % 3 : 3);
}

/* farm.c / events.c: a random current map and farm map; random first
 * bytes in the 49 script slots. */
static void prep_map_copy(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  (void)r;
  for (int i = 0; i < 0x1000; i++) { ram[0x09B6 + i] = (uint8_t)rnd(); ram[0x0A4E6 + i] = (uint8_t)rnd(); }
  for (int i = 0; i < 0x31; i++) ram[0x0B586 + 64 * i] = (uint8_t)rnd();
}

/* maptiles.c: the flags of what you built, bought and found, the
 * weather, the date, the tools in hand and bag, the troughs, the eggs, the
 * shed bytes, the map ($22, 0-$5F) and its width; the tile changes the map
 * routines use (others can have 0 rows: SUB_81A83A would then loop 65536
 * times), at positions inside a 64-tile map. */
static const uint8_t k_map_changes[] = { 0x60, 0x5F, 0x54, 0x53, 0x16, 0x59, 0x58, 0x5A, 0x5C,
  0xF5, 0xE1, 0xE2, 0xEE, 0xFD, 0xFE, 0x99, 0xF1, 0xE4, 0xEF, 0x9B, 0xF0 };
static const uint8_t k_typed_changes[] = { 0x60, 0x5F, 0x54, 0x53, 0x16, 0x59, 0x58, 0x5A, 0x5C,
  0xF5, 0xE1, 0xF0, 0xEF };
static void prep_map_tiles(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  (void)r;
  for (unsigned a = 0x11F5A; a < 0x11F70; a++) ram[a] = (uint8_t)rnd();
  hm_wr16(ram, 0x0196, (uint16_t)(rnd() & 0x001F));
  ram[0x11F36] = (uint8_t)(rnd() % 11);
  ram[0x11F18] = (uint8_t)((rnd() % 2) ? 0 : rnd() % 3);                /* year */
  ram[0x11F19] = (uint8_t)(rnd() % 4);                                   /* season */
  ram[0x11F1B] = (uint8_t)((rnd() % 2) ? 0x12 + rnd() % 4 : rnd() % 30); /* day, often 18-21 */
  ram[0x11F0A] = (uint8_t)((rnd() % 2) ? 10 + rnd() % 3 : rnd() % 13);  /* cows */
  static const uint8_t tools[] = { 0x00, 0x09, 0x0A, 0x0D, 0x0E, 0x0F, 0x11, 0x12, 0x13, 0x14, 0x15, 0x19, 0x1A };
  ram[0x0921] = tools[rnd() % sizeof tools];
  ram[0x0923] = tools[rnd() % sizeof tools];
  hm_wr16(ram, 0x0932, (uint16_t)rnd());
  hm_wr16(ram, 0x0934, (uint16_t)rnd());
  hm_wr16(ram, 0x11F45, (uint16_t)rnd());
  for (unsigned a = 0x11F00; a < 0x11F0A; a++) ram[a] = (uint8_t)rnd();
  static const uint8_t maps[] = { 0x00, 0x10, 0x15, 0x16, 0x17, 0x1C, 0x22, 0x24, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x37 };
  ram[0x22] = (uint8_t)((rnd() % 2) ? maps[rnd() % sizeof maps] : rnd() % 0x60);
  static const uint8_t widths[] = { 1, 2, 4 };
  ram[0x0181] = widths[rnd() % 3];
}
static void prep_stamp(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_map_tiles(ram, r, rnd);
  r->a = k_map_changes[rnd() % sizeof k_map_changes];
  r->x = (uint16_t)(rnd() % 0x3C0);
  r->y = (uint16_t)(rnd() % 0x3C0);
}
static void prep_stamp_typed(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_stamp(ram, r, rnd);
  r->a = k_typed_changes[rnd() % sizeof k_typed_changes];
}
/* the four lists have 26, 6, 8 and 4 items (item 0 and the shed's 27 are empty); X = 4 falls
 * back to list 0 */
static void prep_shop_item(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_map_tiles(ram, r, rnd);
  static const uint8_t items[5] = { 26, 6, 8, 4, 26 };
  r->x = (uint16_t)(rnd() % 5);
  r->a = (uint16_t)(1 + rnd() % items[r->x]);
  r->y = (uint16_t)(rnd() % 4);
}

/* audio.c: the music. The song wanted and the one playing ($0110 / $0117:
 * 0, the same, $FF or another), the arguments the game sends ($0111 /
 * $0112 = 1, $0113 = $10 or $40), A = a channel for UNK_Audio10. */
static void prep_music(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_audio(ram, r, rnd);
  uint8_t song = (uint8_t)(1 + rnd() % 0x1F);
  ram[0x0110] = (uint8_t)((rnd() % 5) ? song : (rnd() % 2) ? 0 : 0xFF);
  ram[0x0117] = (uint8_t)((rnd() % 3) == 0 ? ram[0x0110] : (rnd() % 3) == 0 ? 0 : 1 + rnd() % 0x1F);
  ram[0x0111] = 1;
  ram[0x0112] = 1;
  ram[0x0113] = (rnd() % 2) ? 0x10 : 0x40;
  /* a channel 1-10: with 0 UNK_Audio10 first waits until the chip is not
   * busy, which in the fuzzing frames it never is */
  r->a = (uint16_t)((r->a & 0xFF00) | (1 + rnd() % 10));
}
/* math.c: Unk_Audio23, factors with 0 and non-0 high bytes */
static void prep_multiply16(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  r->a = (uint16_t)((rnd() % 2) ? rnd() & 0xFF : rnd());
  hm_wr16(ram, 0x7E, (uint16_t)((rnd() % 2) ? rnd() & 0xFF : rnd()));
}

/* audio.c: the uploads. A song 0-25 ($0110), a sound effect 0-49 ($0114),
 * $0103 (instruments noted) 0-10. */
/* UNK_Audio1: what RESET gives it, the sound program at $AD:8000 */
static void prep_boot_upload(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  (void)r; (void)rnd;
  hm_wr16(ram, 0x0A, 0x8000);
  ram[0x0C] = 0xAD;
}

static void prep_upload(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_audio(ram, r, rnd);
  ram[0x0110] = (uint8_t)(rnd() % 26);
  ram[0x0114] = (uint8_t)(rnd() % 50);
  ram[0x0103] = (uint8_t)(rnd() % 11);
}

/* interact.c / people.c / maptiles.c / objects.c: wood in stock, the
 * action bits, affection points (the ranges reach $03E7), the map and a
 * byte of its graphics record. */
static void prep_actions(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  hm_wr16(ram, HM_STORED_WOOD, (uint16_t)((rnd() % 4) ? rnd() % 1000 : rnd() % 3));
  hm_wr16(ram, 0xD2, (uint16_t)rnd());
  ram[0x22] = (uint8_t)(rnd() % 0x60);
  r->a = (uint16_t)((rnd() % 2) ? rnd() % 0x400 : rnd());
  r->y = (uint16_t)(rnd() % 0x18);
}

/* shipping.c: the text box's inputs (prep_text_box), the hour (often 17),
 * the map (0-5), the scene's bits of $7F1F5A, $D2 bit 6, the money
 * shipped (often 0). */
static void prep_shipping(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_text_box(ram, r, rnd);
  ram[0x11F1C] = (uint8_t)((rnd() % 2) ? 17 : 15 + rnd() % 4);
  ram[0x22] = (uint8_t)(rnd() % 6);
  hm_wr16(ram, 0x11F5A, (uint16_t)rnd());
  hm_wr16(ram, 0xD2, (uint16_t)((rnd() % 2) ? rnd() & ~0x0040 : rnd()));
  hm_wr16(ram, 0x11F07, (uint16_t)((rnd() % 2) ? 0 : rnd()));
  ram[0x11F09] = (uint8_t)((rnd() % 2) ? 0 : rnd());
}

/* objects.c / events.c: destroying game objects. 40 objects with 0-5
 * pieces, each in a graphics slot 0-$3F or still queued ($FF, 1 in 6);
 * every slot's count well above 0 (at 0 CODE_858C9F would wait forever); the
 * object $A5 / $A9 and slot $B3 to work on; every event slot's +12 an
 * object number, and the commands' arguments. */
static void prep_destroy(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  for (unsigned i = 0; i < 40; i++) {
    uint8_t *o = &ram[0x019C + 0x24 * i];
    uint8_t pieces = (uint8_t)(rnd() % 6);
    o[0x13] = pieces;
    for (unsigned p = 0; p < pieces; p++) o[0x14 + p] = (uint8_t)((rnd() % 6) ? rnd() % 0x40 : 0xFF);
  }
  /* high enough for every piece that may share the slot */
  for (unsigned s = 0; s < 0x40; s++) hm_wr16(ram, 0x0A320 + 4 * s, (uint16_t)(40 + rnd() % 200));
  hm_wr16(ram, 0xA5, (uint16_t)(rnd() % 40));
  hm_wr16(ram, 0xA9, (uint16_t)(0x24 * (rnd() % 40)));
  hm_wr16(ram, 0xB3, (uint16_t)(rnd() % 0x40));
  hm_wr16(ram, 0x0740, (uint16_t)rnd());
  for (unsigned s = 0; s < 0x31; s++) hm_wr16(ram, 0x0B586 + 64 * s + 0x12, (uint16_t)(rnd() % 40));
  r->a = (uint16_t)(rnd() % 0x31);                          /* SUB_848020: a slot */
}
static void prep_destroy_cmd(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_event_cmd(ram, r, rnd);
  prep_destroy(ram, r, rnd);
  uint16_t slot = hm_rd16(ram, 0xCC);
  hm_wr16(ram, slot + 0x12, (uint16_t)(rnd() % 40));
  uint16_t at = hm_rd16(ram, 0xC9);
  ram[0x10000 + at + 1] = (uint8_t)(rnd() % 0x31);          /* Event_UNKCCchanges7 */
  ram[0x10000 + at + 2] |= 0x80;     /* Event_UNKCCchanges11: <word> is a table in the ROM ($B3:8000-) */
  hm_wr16(ram, 0x72, (uint16_t)(0x8000 + (rnd() & 0x7FFF))); ram[0x74] = 0xB3;   /* SUB_848895 */
}

/* eventcmds.c: the animation commands. A table in the ROM, the player's
 * action ($0A sometimes) and direction, $7F1F5E bit 6, whether the player
 * talked to the slot (+0C). */
static void prep_anim_cmd(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_destroy_cmd(ram, r, rnd);
  hm_wr16(ram, 0xD4, (uint16_t)((rnd() % 3) ? 0x0A : rnd() % 0x20));
  hm_wr16(ram, 0xDA, (uint16_t)((rnd() % 2) ? rnd() % 4 : rnd()));
  hm_wr16(ram, 0x11F5E, (uint16_t)rnd());
  uint16_t slot = hm_rd16(ram, 0xCC);
  ram[slot + 0x0C] = (uint8_t)((rnd() % 2) ? 0 : rnd());
  uint16_t at = hm_rd16(ram, 0xC9);
  ram[0x10000 + at + 6] |= 0x80;                            /* command 3's table */
  ram[slot + 0x34] |= 0x80;            /* command 17 uses the slot's own table, +33 */
}

/* objects.c: moving a game object or changing its sprite: a sprite to
 * create (prep_create_gobj) and objects with pieces (prep_destroy). */
static void prep_change_gobj(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_create_gobj(ram, r, rnd);
  prep_destroy(ram, r, rnd);
}

/* events.c: SUB_848331, a slot walking (+01 bit 5) whose step timer +06
 * is often about to run out, with steps both ways. */
static void prep_slot_walk(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_event_cmd(ram, r, rnd);
  uint16_t slot = hm_rd16(ram, 0xCC);
  if (rnd() % 4) ram[slot + 0x01] |= 0x20;
  ram[slot + 0x06] = (uint8_t)((rnd() % 4) ? 1 : rnd());
}

/* events.c: SUB_8483CC, a slot wandering (+01 bit 3, with or without
 * bit 7) right after a step (+05 == +06), with or without steps left, a
 * forced direction or the random one ($DE), its home and range around its
 * position, the animation tables in the ROM, on a map of random tiles
 * with the player nearby (prep_collision), the tiles ahead from the last
 * look ($E9/$EB). */
static void prep_slot_wander(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_collision(ram, r, rnd);
  uint16_t x = hm_rd16(ram, 0xDF), y = hm_rd16(ram, 0xE1);
  prep_cc_pointer(ram, r, rnd);
  uint16_t slot = hm_rd16(ram, 0xCC);
  for (unsigned i = 0; i < 64; i++) ram[slot + i] = (uint8_t)rnd();
  ram[slot + 0x01] = (uint8_t)((rnd() % 8) ? ram[slot + 0x01] | 0x08 : ram[slot + 0x01]);
  ram[slot + 0x02] = (uint8_t)((rnd() % 8) ? rnd() % 4 : rnd());
  if (ram[slot + 0x01] & 0x80) ram[slot + 0x02] &= 3;      /* CEEEE: 0-3 */
  static const int8_t steps[] = { 0, 0, 1, -1, 2, -2, 0x7F, -0x80 };
  /* the big steps only away from the top and left edges: a corner
   * looked at below 0 would read the tile table's open bus in the
   * original (prep_collision) */
  unsigned kinds = (x >= 0xC0 && y >= 0xC0) ? 8 : 6;
  ram[slot + 0x03] = (uint8_t)steps[rnd() % kinds];
  ram[slot + 0x04] = (uint8_t)steps[rnd() % kinds];
  /* a direction past 3 (the area check takes it as "down") only without a
   * step: CDDDD would get it too, and it was checked for 0-3 */
  if (ram[slot + 0x02] >= 4) ram[slot + 0x03] = ram[slot + 0x04] = 0;
  if (rnd() % 4) ram[slot + 0x05] = ram[slot + 0x06];
  ram[slot + 0x07] = (uint8_t)((rnd() % 2) ? rnd() % 8 : rnd());
  ram[slot + 0x08] = (uint8_t)((rnd() % 2) ? 0 : (rnd() % 2) ? 1 + rnd() % 3 : rnd());
  static const uint8_t forced[] = { 0, 0, 0, 1, 2, 4, 8, 3, 0x10 };
  ram[slot + 0x0F] = forced[rnd() % 9];
  hm_wr16(ram, 0xDE, (uint16_t)((rnd() % 4) ? rnd() % 5 : rnd()));
  ram[slot + 0x09] = (uint8_t)((rnd() % 2) ? rnd() % 0x30 : rnd());
  ram[slot + 0x0A] = (uint8_t)((rnd() % 2) ? rnd() % 0x30 : rnd());
  hm_wr16(ram, slot + 0x1A, x); hm_wr16(ram, slot + 0x1C, y);
  hm_wr16(ram, slot + 0x1E, (uint16_t)((rnd() % 4) ? x - 0x20 + rnd() % 0x40 : rnd()));
  hm_wr16(ram, slot + 0x20, (uint16_t)((rnd() % 4) ? y - 0x20 + rnd() % 0x40 : rnd()));
  hm_wr16(ram, slot + 0x33, (uint16_t)(0x8000 + (rnd() & 0x7BFF)));   /* $B3:8000- */
  hm_wr16(ram, slot + 0x36, (uint16_t)(0x8000 + (rnd() & 0x7BFF)));
  ram[slot + 0x3F] = (uint8_t)((rnd() % 2) ? 0x14 + rnd() % 0x14 : rnd());
  static const uint16_t tiles[] = { 0x00FF, 0x00E1, 0x00C0, 0x00C7, 0x00CF, 0x00D0, 0x00BF, 0x0001, 0x01E1 };
  hm_wr16(ram, 0xE9, (rnd() % 2) ? tiles[rnd() % 9] : (uint16_t)rnd());
  hm_wr16(ram, 0xEB, (rnd() % 2) ? tiles[rnd() % 9] : (uint16_t)rnd());
  /* tile $E1 often in front, for the slots $18-$23 */
  if (rnd() % 4 == 0) memset(&ram[0x009B6], 0xE1, 4096);
}

/* events.c: SUB_848286, a slot's sprite each frame: the wandering above
 * (prep_slot_wander), then its game object (prep_change_gobj) with the
 * slot's sprite. The walking and standing animations write the sprite
 * (+16) from their tables, so these are tables of the ROM's bank $B3
 * whose four entries are all real sprites, each one prepared. */
static uint16_t s_sprite_tables[256];
static int s_n_sprite_tables = -1;
static void prep_slot_sprite(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  if (s_n_sprite_tables < 0) {
    s_n_sprite_tables = 0;
    for (uint32_t at = 0x8000; at < 0xFFF0 && s_n_sprite_tables < 256; at += 0x41) {
      int ok = 1;
      for (int e = 0; e < 4 && ok; e++) ok = hm_bus_rd16(ram, 0xB30000 + at + 3 * e) < 0x450;
      if (ok) s_sprite_tables[s_n_sprite_tables++] = (uint16_t)at;
    }
  }
  prep_slot_wander(ram, r, rnd);
  uint16_t slot = hm_rd16(ram, 0xCC);
  /* the animations take entry +02 of their table, and only entries 0-3
   * (the directions) are checked to hold real sprites */
  ram[slot + 0x02] &= 3;
  prep_change_gobj(ram, r, rnd);
  uint16_t sprite = hm_rd16(ram, 0xA1);
  if (s_n_sprite_tables > 0)
    for (int t = 0; t < 2; t++) {
      uint16_t table = s_sprite_tables[rnd() % (unsigned)s_n_sprite_tables];
      hm_wr16(ram, slot + (t ? 0x36 : 0x33), table);
      for (int e = 0; e < 4; e++) prep_sprite(ram, hm_bus_rd16(ram, 0xB30000u + table + 3 * e), rnd);
    }
  prep_sprite(ram, sprite, rnd);
  /* prep_sprite gives the graphics slots low use counts; destroying an
   * object hangs on a count of 0 (prep_destroy) */
  for (unsigned g = 0; g < 0x40; g++)
    if (hm_rd16(ram, 0x0A320 + 4 * g) < 40) hm_wr16(ram, 0x0A320 + 4 * g, (uint16_t)(40 + rnd() % 200));
  hm_wr16(ram, slot + 0x12, hm_rd16(ram, 0xA5));
  hm_wr16(ram, slot + 0x14, hm_rd16(ram, 0x9F));
  hm_wr16(ram, slot + 0x16, sprite);
  hm_wr16(ram, slot + 0x18, (uint16_t)((rnd() % 2) ? sprite : rnd() % 0x450));
  static const uint8_t does[] = { 0x40, 0x02, 0x04, 0x00, 0x00, 0x04, 0x02 };
  ram[slot + 0x01] = (uint8_t)((ram[slot + 0x01] & ~0x46) | does[rnd() % 7]);
}

/* eventcmds.c: the talking commands (5, 9): as the animation commands,
 * with the player's action (often 5, holding something up, or a busy
 * one), carrying ($D2 bit 2), $7F1F60, A just pressed ($0128 bit 7) and
 * an item in hand ($091D) sometimes; StartMapAnimation's number for
 * Event_UNKCFD1; the music for Event_AudioRelated2. */
static void prep_talk_cmd(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_anim_cmd(ram, r, rnd);
  uint16_t slot = hm_rd16(ram, 0xCC);
  ram[slot + 0x0C] = (uint8_t)((rnd() % 4) ? 1 + rnd() % 0xFF : 0);
  static const uint16_t actions[] = { 0, 1, 5, 5, 5, 0x0A, 0x0C, 0x0D, 0x1B, 0x0E };
  hm_wr16(ram, 0xD4, actions[rnd() % 10]);
  uint16_t d2 = hm_rd16(ram, 0xD2), f60 = (uint16_t)rnd(), pad = (uint16_t)rnd();
  hm_wr16(ram, 0xD2, (uint16_t)((rnd() % 4) ? d2 & ~0x0004u : d2 | 0x0004));
  hm_wr16(ram, 0x11F60, (uint16_t)((rnd() % 4) ? f60 & ~0x0006u : f60));
  hm_wr16(ram, 0x00128, (uint16_t)((rnd() % 4) ? pad | 0x0080 : pad & ~0x0080u));
  ram[0x0091D] = (uint8_t)((rnd() % 2) ? 0 : rnd());
}
static void prep_map_animation_cmd(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_event_cmd(ram, r, rnd);
  uint16_t at = hm_rd16(ram, 0xC9);
  hm_wr16(ram, 0x10000u + at + 1, (uint16_t)(rnd() % 0x60));
}
static void prep_music_cmd(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_event_cmd(ram, r, rnd);
  prep_music(ram, r, rnd);
}

/* objects.c: CODE_8582C7, animating the game objects. Each one in use
 * gets a real sprite and is somewhere in its list of frames (sometimes at
 * the end, to loop back), with a timer about to run out, $FE (over) or
 * $FF (for ever); every piece of every frame is prepared, and the slots
 * have high use counts (CODE_858C9F hangs on 0). */
static void prep_animate_gobjs(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  (void)r;
  for (unsigned i = 0; i < 40; i++) {
    uint32_t o = 0x019C + 0x24 * i;
    uint16_t sprite = (uint16_t)(rnd() % 0x450);
    uint8_t bank = sprite >= 0x262 ? 0x87 : 0x86;
    uint32_t table = bank == 0x87 ? 0x878080u + 2u * (sprite - 0x262) : 0x868080u + 2u * sprite;
    uint16_t entry = hm_bus_rd16(ram, table);
    unsigned frames = 0;
    while (frames < 32 && hm_bus_rd16(ram, (uint32_t)bank << 16 | (uint16_t)(entry + 3 * frames))) {
      uint16_t list = hm_bus_rd16(ram, (uint32_t)bank << 16 | (uint16_t)(entry + 3 * frames));
      uint8_t n = hm_bus_rd8(ram, (uint32_t)bank << 16 | list);
      for (unsigned k = 0; k < n && k < 16; k++)
        prep_piece(ram, hm_bus_rd16(ram, (uint32_t)bank << 16 | (uint16_t)(list + 1 + 5 * k)), rnd);
      frames++;
    }
    unsigned at = frames ? rnd() % (frames + 1) : 0;
    hm_wr16(ram, o + 0x00, (uint16_t)((rnd() % 4) ? 0x7777 : 0));
    hm_wr16(ram, o + 0x02, sprite);
    hm_wr16(ram, o + 0x0C, entry);
    hm_wr16(ram, o + 0x0E, (uint16_t)(entry + 3 * at));
    hm_wr16(ram, o + 0x10, hm_bus_rd16(ram, (uint32_t)bank << 16 | (uint16_t)(entry + 3 * (at ? at - 1 : 0))));
    static const uint8_t timers[] = { 0, 0, 0, 1, 2, 0xFE, 0xFF, 0x10 };
    ram[o + 0x12] = (uint8_t)((rnd() % 8) ? timers[rnd() % 8] : rnd());
    uint8_t pieces = (uint8_t)(rnd() % 6);
    ram[o + 0x13] = pieces;
    for (unsigned k = 0; k < pieces; k++) ram[o + 0x14 + k] = (uint8_t)((rnd() % 6) ? rnd() % 0x40 : 0xFF);
  }
  for (unsigned g = 0; g < 0x40; g++) hm_wr16(ram, 0x0A320 + 4 * g, (uint16_t)(40 + rnd() % 200));
  prep_queue(ram, 40, rnd);
  hm_wr16(ram, 0xDC, (uint16_t)(1 + rnd() % 40));
  hm_wr16(ram, 0x0740, (uint16_t)rnd());
}

/* objects.c: GetOBJOrderToDraw: how many objects ($DC, at least 1), the
 * player's one ($0905, sometimes past $DC) and their Y (+0A), close to the
 * player's and often equal. */
static void prep_draw_order(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  (void)r;
  uint16_t n = (uint16_t)(1 + rnd() % 40);
  hm_wr16(ram, 0xDC, n);
  uint16_t player = (uint16_t)((rnd() % 8) ? rnd() % n : rnd() % 40);
  hm_wr16(ram, 0x0905, player);
  uint16_t y = (uint16_t)rnd();
  for (unsigned i = 0; i < 40; i++)
    hm_wr16(ram, 0x019C + 0x24 * i + 0x0A, (uint16_t)((rnd() % 4) ? y - 8 + rnd() % 17 : rnd()));
}
/* PresetsWRAMCopyofOAM / InitializeOBJs: what they clear holds anything */
static void prep_oam_copy(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  (void)r;
  for (unsigned i = 0; i < 0x220; i++) ram[0xA000 + i] = (uint8_t)rnd();
  hm_wr16(ram, 0x00B7, (uint16_t)rnd());
}
static void prep_init_objs(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_oam_copy(ram, r, rnd);
  for (unsigned i = 0; i < 40 * 0x24; i++) ram[0x019C + i] = (uint8_t)rnd();
  hm_wr16(ram, 0xA1, (uint16_t)((rnd() % 2) ? 0x250 + rnd() % 0x20 : rnd()));
}

/* farm.c: LoadMap, any map with tiles (the farm's four, and those whose
 * MapPointerTable entry is not $FFFFFF), sometimes a farm copy of noise. */
static void prep_load_map(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  (void)r;
  uint8_t map;
  do map = (uint8_t)((rnd() % 4) ? rnd() % 0x56 : rnd() % 5);
  while (map >= 4 && hm_bus_rd8(ram, 0x82B174u + 3u * map + 2) == 0xFF);
  ram[0x22] = map;
  for (unsigned i = 0; i < 3; i++) ram[0x0D + i] = (uint8_t)rnd();
  if (rnd() % 2) for (unsigned i = 0; i < 0x1000; i++) ram[0xA4E6 + i] = (uint8_t)rnd();
}

/* audio.c: UNK_Audio5, the music of a map: any map (often $1E and the
 * edges of the ranges), the hour (often 18), the season, $0196 bits 1/4. */
static void prep_map_music(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  (void)r;
  static const uint8_t maps[] = { 0x1E, 0x14, 0x15, 0x30, 0x31, 0x56, 0x57, 0x5A, 0x5B, 0 };
  ram[0x22] = (uint8_t)((rnd() % 3) ? rnd() % 0x60 : maps[rnd() % 10]);
  ram[0x11F1C] = (uint8_t)((rnd() % 4) ? 6 + rnd() % 13 : (rnd() % 2) ? 18 : 17);
  ram[0x11F19] = (uint8_t)(rnd() % 4);
  hm_wr16(ram, 0x0196, (uint16_t)rnd());
  ram[0x0110] = (uint8_t)rnd();
}

/* menus.c: SUB_82EB57, the name screen's cursor (prep_inputs: a page and
 * a cell), its old place anything. */
static void prep_inputs(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void));
static void prep_name_cursor(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_inputs(ram, r, rnd);
  for (unsigned i = 0; i < 4; i++) ram[0x099B + i] = (uint8_t)rnd();
}

/* objects.c: the player's object: as changing a game object
 * (prep_change_gobj), with the player's object $0905 = $A5, an animation
 * $0901 (often the one shown, $0903) whose sprite from DATA8_81D210 is
 * prepared, the player's position, the state. */
static void prep_player_object(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_change_gobj(ram, r, rnd);
  hm_wr16(ram, 0x0905, hm_rd16(ram, 0xA5));
  uint16_t anim = (uint16_t)(rnd() % 0x80);
  hm_wr16(ram, 0x0901, anim);
  hm_wr16(ram, 0x0903, (uint16_t)((rnd() % 2) ? anim : rnd() % 0x80));
  uint16_t sprite = hm_bus_rd16(ram, 0x81D210u + 3u * anim);
  if (sprite < 0x450) prep_sprite(ram, sprite, rnd);
  for (unsigned g = 0; g < 0x40; g++)
    if (hm_rd16(ram, 0x0A320 + 4 * g) < 40) hm_wr16(ram, 0x0A320 + 4 * g, (uint16_t)(40 + rnd() % 200));
  hm_wr16(ram, 0x090F, (uint16_t)rnd());
  hm_wr16(ram, 0x0915, (uint16_t)rnd());
  hm_wr16(ram, 0xD6, (uint16_t)rnd()); hm_wr16(ram, 0xD8, (uint16_t)rnd());
  hm_wr16(ram, 0xD2, (uint16_t)rnd());
  /* the timer byte +12 of the player's object: $FF sometimes (read back as $FFFF) */
  uint16_t at = hm_bus_rd16(ram, 0x858BE0u + 2u * hm_rd16(ram, 0xA5));
  if (rnd() % 3 == 0) ram[0x019C + at + 0x12] = 0xFF;
}

/* animals.c: CODE_83F4D8, the start of the ending: a random farm, 12 cows in use
 * or not with their affection (often equal), names, the development. */
static void prep_cow_festival(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_random_farm(ram, r, rnd);
  uint8_t love = (uint8_t)rnd();
  for (unsigned c = 0; c < 12; c++) {
    uint8_t *cow = &ram[0x0C1C6 + 16 * c];
    for (unsigned i = 0; i < 16; i++) cow[i] = (uint8_t)rnd();
    if (rnd() % 3 == 0) cow[0x04] = love;
  }
  for (unsigned i = 0; i < 8; i++) ram[0x0889 + i] = (uint8_t)rnd();
  ram[0x09B5] = (uint8_t)rnd();
  hm_wr16(ram, 0x11F56, (uint16_t)rnd());
  ram[0x11F47] = (uint8_t)rnd();
  /* a well developed farm sometimes: past 2,950 tiles the percentage is
   * capped at 100 */
  if (rnd() % 3 == 0) {
    unsigned keep = 70 + rnd() % 30;
    for (unsigned i = 0; i < 0x1000; i++)
      ram[0xA4E6 + i] = (uint8_t)((rnd() % 100 < keep) ? 0x1D + rnd() % 0x83 : rnd() % 5);
  }
}

/* mapevents.c: who appears on a map: the story flags ($7F1F64-$7F1F75),
 * the date (often the days the routines test), the time, the children's
 * ages (around 60), the weather. */
static void prep_map_event(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  (void)r;
  for (uint32_t a = 0x11F5A; a < 0x11F76; a++) ram[a] = (uint8_t)rnd();
  static const uint8_t seasons[] = { 0, 1, 2, 3, 3, 1 };
  ram[0x11F19] = seasons[rnd() % 6];
  static const uint8_t days[] = { 1, 0x18, 0x18, 0x0A, 0x0A, 0x07, 0x08, 0x0C, 0x0D, 0x11, 0x12,
                                  0x19, 0x1A, 0x1C, 0x1D, 0x1E, 0x14, 0x17, 0x0C, 0x14 };
  ram[0x11F1B] = (uint8_t)((rnd() % 2) ? days[rnd() % 20] : 1 + rnd() % 30);
  ram[0x11F1A] = (uint8_t)((rnd() % 2) ? rnd() % 7 : (rnd() % 2) * 6);   /* weekday: Sunday, Saturday */
  ram[0x11F18] = (uint8_t)((rnd() % 2) ? 0 : rnd() % 4);           /* year */
  ram[0x11F1C] = (uint8_t)(6 + rnd() % 13);                         /* hour */
  hm_wr16(ram, 0x11F37, (uint16_t)((rnd() % 2) ? 0x3B + rnd() % 3 : rnd() % 0x100));
  hm_wr16(ram, 0x11F39, (uint16_t)((rnd() % 2) ? 0x3B + rnd() % 3 : rnd() % 0x100));
  static const uint8_t weathers[] = { 0, 1, 2, 3, 3, 4, 5, 6, 7 };
  ram[0x0098C] = weathers[rnd() % 9];                               /* tomorrow's weather */
  if (rnd() % 4 == 0) ram[0x11F1C] = (uint8_t)(14 + rnd() % 4);     /* around 3 and 5 pm */
  hm_wr16(ram, 0x0196, (uint16_t)rnd());
  /* the usual day: past the first event, no special day, no rain */
  if (rnd() % 4) ram[0x11F68] |= 0x01;
  static const uint16_t special[] = { 0x0002, 0x0008, 0x0200 };
  for (unsigned i = 0; i < 3; i++)
    if (rnd() % 4) hm_wr16(ram, 0x0196, (uint16_t)(hm_rd16(ram, 0x0196) & ~special[i]));
  if (rnd() % 4) ram[0x11F64] &= (uint8_t)~0x20;
  /* the other people's first visits, mostly seen */
  if (rnd() % 4) ram[0x11F68] |= (uint8_t)((rnd() % 4) ? 0x0E : 0x06);
  if (rnd() % 2) ram[0x11F69] |= 0x20;                              /* $7F1F68 bit 13 */
  if (rnd() % 2) { ram[0x11F5E] &= (uint8_t)~0x30; ram[0x11F6C] &= (uint8_t)~0x14; }
  /* coming into the map from below or not; the state, the facing */
  hm_wr16(ram, 0xD8, (uint16_t)((rnd() % 2) ? 0x1F8 + rnd() % 0x10 : rnd() % 0x400));
  hm_wr16(ram, 0xD2, (uint16_t)rnd());
  hm_wr16(ram, 0xDA, (uint16_t)(rnd() % 4));
  for (unsigned i = 0; i < 3; i++) ram[HM_RNG_STATE + i] = (uint8_t)rnd();
  if (rnd() % 2) ram[0x11F61] &= (uint8_t)~0x04;                    /* $7F1F60 bit 10 */
  ram[0x11F49] = (uint8_t)((rnd() % 8) ? rnd() % 0x1F : rnd());     /* the introduction's step */
  for (unsigned i = 0; i < 4; i++) ram[0x11F00 + i] = (uint8_t)rnd();
  ram[0x0917] = (uint8_t)rnd();
}

/* MapEvent_EBA6, the ending: its step, and every number its parts test,
 * often around the bounds */
static void prep_map_event_ending(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_map_event(ram, r, rnd);
  ram[0x11F47] = (uint8_t)((rnd() % 8) ? rnd() % 0x26 : rnd());
  static const uint8_t herd[] = { 0, 0, 1, 6, 7, 12 };
  ram[0x11F0A] = herd[rnd() % 6];
  ram[0x11F0B] = herd[rnd() % 6];
  static const uint16_t happy[] = { 99, 100, 199, 200, 799, 800, 899, 900, 0, 999 };
  hm_wr16(ram, 0x11F33, happy[rnd() % 10]);
  for (unsigned i = 0; i < 4; i++) hm_wr16(ram, 0x11F4A + 2 * i, (uint16_t)((rnd() % 2) ? 199 + rnd() % 2 : rnd() % 400));
  for (unsigned i = 0; i < 5; i++) hm_wr16(ram, 0x11F1F + 2 * i, (uint16_t)((rnd() % 2) ? 280 + rnd() % 40 : rnd()));
  ram[0x11F36] = (uint8_t)((rnd() % 2) ? 10 : rnd() % 11);
  hm_wr16(ram, 0x11F04, (uint16_t)((rnd() % 2) ? 999 + rnd() % 2 : rnd()));
  ram[0x11F06] = (uint8_t)((rnd() % 2) ? 0 : rnd());
  hm_wr16(ram, 0x11F52, (uint16_t)((rnd() % 2) ? 99 + rnd() % 2 : rnd() % 200));
  ram[0x09B5] = (uint8_t)((rnd() % 2) ? 0xBF + rnd() % 2 : rnd());
  if (rnd() % 2) ram[0x11F66] &= (uint8_t)~0x1F;                    /* no wife */
  if (rnd() % 3 == 0) ram[0x11F66] = (uint8_t)(1 << (rnd() % 5));
  for (unsigned c = 0; c < 12; c++) for (unsigned i = 0; i < 16; i++) ram[0x0C1C6 + 16 * c + i] = (uint8_t)rnd();
  ram[0x0917] = (uint8_t)(100 + rnd() % 156);                       /* max stamina */
  /* a quarter of the time, right at one of the parts with many conditions */
  switch (rnd() % 12) {
  case 0:                                                           /* the bad ending */
    ram[0x11F47] = 0; ram[0x11F66] &= (uint8_t)~0x1F;
    hm_wr16(ram, 0x11F33, (uint16_t)((rnd() % 2) ? 99 : 100));
    ram[0x11F0A] = (uint8_t)(rnd() % 4 ? 0 : 1); ram[0x11F0B] = (uint8_t)(rnd() % 4 ? 0 : 1);
    break;
  case 1:                                                           /* the girls' hearts */
    ram[0x11F47] = 0x10; ram[0x11F66] &= (uint8_t)~0x1F;
    hm_wr16(ram, 0x11F33, 200);
    for (unsigned i = 0; i < 5; i++) hm_wr16(ram, 0x11F1F + 2 * i, (uint16_t)(299 + rnd() % 3));
    break;
  case 2:                                                           /* everything */
    ram[0x11F47] = 0x13; ram[0x11F6E] |= 0x08;
    hm_wr16(ram, 0x11F33, 900); ram[0x11F0A] = 1; ram[0x11F0B] = 1; ram[0x11F36] = 10;
    hm_wr16(ram, 0x11F52, 100); ram[0x11F06] = 0;
    break;
  default: break;
  }
}

/* MapEvent_CB69, the farm: often 6:00:00 sharp (or a second off), the
 * girls' hearts around 200, money around 30,000 G, 5-6 chickens, single */
static void prep_map_event_farm(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_map_event(ram, r, rnd);
  if (rnd() % 2) {
    ram[0x11F1C] = 6; ram[0x11F1D] = 0;
    ram[0x11F1E] = (uint8_t)((rnd() % 4) ? 0 : 1 + rnd() % 59);
  } else {
    ram[0x11F1D] = (uint8_t)(rnd() % 15); ram[0x11F1E] = (uint8_t)(rnd() % 60);
  }
  if (rnd() % 2) ram[0x11F66] &= (uint8_t)~0x1F;
  if (rnd() % 2) { ram[0x11F6B] &= (uint8_t)~0xF0; ram[0x11F6C] &= (uint8_t)~0x3F; }
  for (unsigned i = 0; i < 5; i++) hm_wr16(ram, 0x11F1F + 2 * i, (uint16_t)((rnd() % 2) ? 199 + rnd() % 2 : rnd() % 1000));
  hm_wr16(ram, 0x11F04, (uint16_t)((rnd() % 2) ? 299 + rnd() % 2 : rnd()));
  ram[0x11F06] = (uint8_t)((rnd() % 2) ? 0 : rnd());
  ram[0x11F0B] = (uint8_t)((rnd() % 2) ? 5 + rnd() % 2 : rnd() % 13);
  ram[0x11F32] = (uint8_t)((rnd() % 2) ? 0x15 : rnd());
  ram[0x11F35] = (uint8_t)((rnd() % 2) ? 0 : rnd());
  if (rnd() % 2) hm_wr16(ram, 0x0196, (uint16_t)(hm_rd16(ram, 0x0196) & ~0x001Eu));
  if (rnd() % 2) ram[0x11F5A] &= (uint8_t)~0x02;
  if (rnd() % 2) ram[0x11F67] &= (uint8_t)~0x01;                      /* $7F1F66 bit 8 */
  if (rnd() % 2) ram[0x11F66] &= (uint8_t)~0xC0;
  /* half the time, past the scenes and first days, to reach the end */
  if (rnd() % 2) {
    ram[0x11F5A] &= (uint8_t)~0x02;
    ram[0x11F68] |= 0xA1;                                           /* bits 0, 5, 7 */
    ram[0x11F66] &= (uint8_t)~0xC0; ram[0x11F67] &= (uint8_t)~0x01;
    if (rnd() % 4) { ram[0x11F70] = (uint8_t)((ram[0x11F70] & ~0x02) | 0x01); ram[0x11F64] &= (uint8_t)~0x10; }
    if (rnd() % 4) ram[0x0098C] = (uint8_t)(rnd() % 6);
    if (rnd() % 4) ram[0x11F65] |= 0x01;                              /* $7F1F64 bit 8 */
    if (rnd() % 2) ram[0x11F69] &= (uint8_t)~0x04;                    /* $7F1F68 bit 10 */
    if (rnd() % 2) ram[0x11F69] &= (uint8_t)~0x40;                    /* bit 14 */
    if (rnd() % 2) ram[0x11F64] |= 0xC0;
    if (rnd() % 2) { ram[0x11F6B] &= (uint8_t)~0x0D; }               /* $7F1F6A bits 8, 10, 11 */
    if (rnd() % 2) ram[0x11F6F] |= 0x04;                              /* $7F1F6E bit 10 */
    ram[0x11F18] = (uint8_t)(rnd() % 3);
    /* and then right at one of the late cases */
    static const uint8_t visitor_days[] = { 2, 7, 13, 17, 25, 28 };
    switch (rnd() % 8) {
    case 0:                                                         /* the first summer */
      ram[0x11F70] &= (uint8_t)~0x03; ram[0x11F64] &= (uint8_t)~0x10; ram[0x0098C] = 0;
      ram[0x11F18] = 0; ram[0x11F19] = 1; ram[0x11F1B] = (uint8_t)(0x13 + rnd() % 2);
      ram[0x11F1C] = 6; ram[0x11F1D] = 0; ram[0x11F1E] = 0;
      break;
    case 1:                                                         /* 30,000 G */
      ram[0x11F18] = 1; ram[0x11F1C] = 6; ram[0x11F1D] = 0; ram[0x11F1E] = 0;
      hm_wr16(ram, 0x0196, (uint16_t)(hm_rd16(ram, 0x0196) & ~0x001Au));
      ram[0x11F69] &= (uint8_t)~0x04; ram[0x11F65] |= 0x01;
      hm_wr16(ram, 0x11F04, (uint16_t)(2999 + rnd() % 2)); ram[0x11F06] = 0;
      break;
    case 2:                                                         /* a Saturday */
      ram[0x11F18] = 1; ram[0x11F1A] = (uint8_t)(5 + rnd() % 2); ram[0x11F1C] = 6; ram[0x11F1D] = 0; ram[0x11F1E] = 0;
      hm_wr16(ram, 0x0196, (uint16_t)(hm_rd16(ram, 0x0196) & ~0x001Au));
      ram[0x11F64] |= 0x40; ram[0x11F69] &= (uint8_t)~0x44; ram[0x11F65] |= 0x01;
      ram[0x11F06] = 0x80;                                          /* no 30,000 G */
      break;
    case 3:                                                         /* a fall visitor day */
      ram[0x11F19] = 2; ram[0x11F1B] = visitor_days[rnd() % 6]; ram[0x11F18] = 0;
      hm_wr16(ram, 0x0196, (uint16_t)(hm_rd16(ram, 0x0196) & ~0x001Au));
      ram[0x11F64] |= 0x80; ram[0x11F69] |= 0x20; ram[0x11F6B] &= (uint8_t)~0x01; ram[0x11F65] |= 0x01;
      if (rnd() % 2) ram[0x11F1C] = 7;
      break;
    case 4:                                                         /* the chickens */
      ram[0x11F6B] &= (uint8_t)~0x0C; ram[0x11F6F] |= 0x04; ram[0x11F0B] = (uint8_t)(5 + rnd() % 2);
      ram[0x11F1C] = 6; ram[0x11F1D] = 0; ram[0x11F1E] = 0; ram[0x11F65] |= 0x01;
      hm_wr16(ram, 0x0196, (uint16_t)(hm_rd16(ram, 0x0196) & ~0x001Eu));
      ram[0x11F64] &= (uint8_t)~0xC0; ram[0x11F18] = 0; ram[0x11F19] = (uint8_t)(rnd() % 2);
      break;
    case 5:                                                         /* the girls */
      ram[0x11F66] &= (uint8_t)~0x9F; ram[0x11F1C] = 6; ram[0x11F1D] = 0; ram[0x11F1E] = 0;
      ram[0x11F1B] = visitor_days[rnd() % 6];
      hm_wr16(ram, 0x0196, (uint16_t)(hm_rd16(ram, 0x0196) & ~0x001Au));
      ram[0x11F6B] &= (uint8_t)~0xF0; ram[0x11F6C] &= (uint8_t)~0x3F;
      for (unsigned i = 0; i < 5; i++) hm_wr16(ram, 0x11F1F + 2 * i, (uint16_t)((rnd() % 3) ? 150 : 199 + rnd() % 2));
      break;
    default: break;
    }
  }
}

/* items.c: putting an item down: an item with a sprite list, the facing,
 * the tile in front and its property table (prep_tile_dma), room for the
 * map object and the item's sprite prepared (prep_spawn). */
static void prep_put_down(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  HmRegs t = *r;
  prep_map_change_dma(ram, &t, rnd);
  prep_spawn(ram, r, rnd);
  uint8_t item;
  do item = (uint8_t)(1 + rnd() % 0x5A);
  while (hm_bus_rd8(ram, 0x8196AFu + 3u * item + 2) == 0xFF);
  ram[0x091D] = item;
  ram[0x0984] = item;                                               /* SUB_8180B7's item */
  uint16_t dir = (uint16_t)(rnd() % 4);
  hm_wr16(ram, 0xDA, dir);
  uint32_t list = (uint32_t)hm_bus_rd8(ram, 0x8196AFu + 3u * item + 2) << 16 | hm_bus_rd16(ram, 0x8196AFu + 3u * item);
  uint16_t sprite = hm_bus_rd16(ram, (list + 3u * (2 + dir)) & 0xFFFFFF);
  if (sprite < 0x450) prep_sprite(ram, sprite, rnd);
  for (unsigned g = 0; g < 0x40; g++)
    if (hm_rd16(ram, 0x0A320 + 4 * g) < 40) hm_wr16(ram, 0x0A320 + 4 * g, (uint16_t)(40 + rnd() % 200));
  hm_wr16(ram, 0xD6, (uint16_t)(0x40 + rnd() % 0x380));
  hm_wr16(ram, 0xD8, (uint16_t)(0x40 + rnd() % 0x380));
  r->y = (uint16_t)(rnd() % 6);                                     /* SUB_8180B7: a case */
  /* the farm's real tile property table (ChangeMapTile draws the tile it
   * gives; a random one can make it loop for ever) */
  hm_wr16(ram, 0x0D, hm_bus_rd16(ram, 0x82B294)); ram[0x0F] = hm_bus_rd8(ram, 0x82B296);
}

/* items.c: an item map object at any step of its routine. Everything its
 * steps call is made safe (text boxes, game objects, the collision check,
 * the tile in front, the events) and the inputs they branch on are biased:
 * the state, the answer, the player's action, the map, the day and hour,
 * and, half of the time, a whole map of one interesting tile type. */
static uint8_t item_pick(uint32_t (*rnd)(void), const uint8_t *list, unsigned n) { return list[rnd() % n]; }
static void prep_item_any(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void), const uint8_t *items, unsigned n,
                          bool keep_item)
{
  prep_text_box(ram, r, rnd);
  prep_collision(ram, r, rnd);
  prep_destroy(ram, r, rnd);
  prep_new_animal(ram, r, rnd);
  uint16_t near_x = hm_rd16(ram, 0xDF), near_y = hm_rd16(ram, 0xE1);  /* where the objects are */
  prep_put_down(ram, r, rnd);
  if (rnd() % 2) { hm_wr16(ram, 0xD6, near_x); hm_wr16(ram, 0xD8, near_y); }
  uint8_t item = item_pick(rnd, items, n);
  ram[0x0984] = item;
  ram[0x091D] = (uint8_t)((rnd() % 4) ? item : item_pick(rnd, items, n));
  static const uint8_t states[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 0xFF };
  ram[0x0976] = (uint8_t)((rnd() % 8) ? states[rnd() % sizeof states] : rnd());
  hm_wr16(ram, 0x0978, (uint16_t)(rnd() % 40));
  unsigned slot = rnd() % 10;
  hm_wr16(ram, 0x07, (uint16_t)(0xB4E6 + 16 * slot)); ram[0x09] = 0x7E;
  ram[0x019A] = (uint8_t)((rnd() % 4) ? ((rnd() % 2) ? 2 : rnd() % 2) : rnd());
  ram[0x018F] = (uint8_t)((rnd() % 4) ? rnd() % 2 : rnd());
  static const uint16_t actions[] = { 0, 3, 5, 5, 8 };
  hm_wr16(ram, 0xD4, (uint16_t)((rnd() % 6) ? actions[rnd() % 5] : rnd()));
  hm_wr16(ram, 0xD2, (uint16_t)rnd());
  ram[0x0974] = (uint8_t)rnd();
  static const uint8_t maps[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 0x0F, 0x10, 0x12, 0x14, 0x15, 0x27, 0x28 };
  ram[0x22] = (uint8_t)((rnd() % 6) ? maps[rnd() % sizeof maps] : rnd() % 0x60);
  ram[0x11F1A] = (uint8_t)((rnd() % 2) ? 0 : rnd() % 7);              /* weekday */
  ram[0x11F1C] = (uint8_t)((rnd() % 2) ? 6 + rnd() % 11 : rnd() % 24); /* hour */
  hm_wr16(ram, 0x0196, (uint16_t)((rnd() % 2) ? rnd() & ~0x0002u : rnd()));
  for (unsigned a = 0x11F5C; a < 0x11F76; a++) ram[a] = (uint8_t)rnd();
  if (rnd() % 2) ram[0x11F5C] |= 0x10;
  if (rnd() % 2) ram[0x11F6F] &= (uint8_t)~0x20;                       /* no egg hatching */
  if (rnd() % 2) ram[0x11F67] &= (uint8_t)~0x20;                       /* no spring event yet */
  if (rnd() % 2) ram[0x11F74] &= (uint8_t)~0x20;
  ram[0x09AC] = (uint8_t)((rnd() % 4) ? 1 + rnd() % 5 : rnd());
  hm_wr16(ram, 0x09AD, (uint16_t)(rnd() % 0x400)); hm_wr16(ram, 0x09AF, (uint16_t)(rnd() % 0x400));
  ram[0x0920] = (uint8_t)(0x24 + rnd() % 13);
  if (rnd() % 3 == 0) ram[0x11F61] &= (uint8_t)~0x40;                 /* objects block */
  if (rnd() % 2) { hm_wr16(ram, 0xCF, 0); ram[0xD1] = (uint8_t)((rnd() % 2) ? 0 : rnd()); }
  hm_wr16(ram, 0x090B, (uint16_t)rnd()); hm_wr16(ram, 0x090D, (uint16_t)rnd());
  static const uint8_t tiles[] = { 0x01, 0x02, 0x07, 0x08, 0xF0, 0xF2, 0xF4, 0xF7, 0xF9, 0xFA, 0xFB,
                                   0xFC, 0xFD, 0xFF, 0xE1, 0xC5, 0xE2, 0xE8, 0xEE, 0xEF, 0x00 };
  if (rnd() % 2) memset(&ram[0x09B6], tiles[rnd() % sizeof tiles], 4096);
  /* the rare places: the coop's incubator with an egg, the mountain's
   * spring and pond, an animal set down near tiles $C0-$D0 */
  switch (rnd() % 6) {
  case 0:
    if (!keep_item) ram[0x0984] = ram[0x091D] = 0x14;
    ram[0x22] = 0x28;
    memset(&ram[0x09B6], (rnd() % 4) ? 0xF7 : 0xF2, 4096);
    break;
  case 1:
    ram[0x22] = (uint8_t)(0x10 + rnd() % 5);
    if (!keep_item && rnd() % 2) ram[0x0984] = ram[0x091D] = 7;
    memset(&ram[0x09B6], (rnd() % 2) ? 0xF0 : 0xF4, 4096);
    break;
  case 2: {
    static const uint8_t edge[] = { 0xBF, 0xC0, 0xCF, 0xD0, 0xE1 };
    memset(&ram[0x09B6], edge[rnd() % sizeof edge], 4096);
    break;
  }
  default: break;
  }
  /* SUB_81809A calls them with A's high byte 0 (LDA #0; XBA; LDA $0984;
   * ASL), which the shop goods keep for their article number */
  r->a = (uint16_t)(rnd() & 0xFF);
  r->x = (uint16_t)rnd(); r->y = (uint16_t)rnd();
}
static const uint8_t k_item_general[] = {
  0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
  0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1E, 0x1F, 0x20, 0x21,
  0x22, 0x24, 0x25, 0x26, 0x29, 0x57, 0x58, 0x59, 0x5A };
static const uint8_t k_item_shop[] = {
  0x1B, 0x1C, 0x1D, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F,
  0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56 };
static void prep_item(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_item_any(ram, r, rnd, k_item_general, sizeof k_item_general, false);
}
static void prep_item_food(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  static const uint8_t food[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
  prep_item_any(ram, r, rnd, food, sizeof food, true);
}
static void prep_item_shop(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_item_any(ram, r, rnd, k_item_shop, sizeof k_item_shop, true);
  if (rnd() % 2) ram[0x0976] = (uint8_t)(rnd() % 4);
}

/* actions.c: an action of the player at any of its steps: text boxes,
 * map animations, map tiles and the player's game object made safe; the
 * step, the frame counter (near the ends it waits for), the answer, the TV
 * channel, the bits the actions read, the pieces of fence. A = the action
 * x 2, as PlayerInteractionSelector leaves it. */
static void prep_player_action(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_text_box(ram, r, rnd);
  prep_player_object(ram, r, rnd);
  prep_put_down(ram, r, rnd);
  ram[0x096F] = (uint8_t)((rnd() % 8) ? rnd() % 6 : rnd());
  static const uint8_t near_end[] = { 0x15, 0x16, 0x1D, 0x1E, 0x1F, 0x20, 0x01, 0x00, 0x07,
                                      0x3F, 0x41, 0x63, 0xC5 };
  ram[0x0970] = (uint8_t)((rnd() % 4) ? near_end[rnd() % sizeof near_end] : rnd());
  ram[0x019A] = (uint8_t)((rnd() % 4) ? rnd() % 4 : rnd());
  ram[0x018F] = (uint8_t)((rnd() % 4) ? rnd() % 2 : rnd());
  ram[0x0990] = (uint8_t)((rnd() % 4) ? rnd() % 6 : rnd() % 0x20);
  ram[0x11F19] = (uint8_t)(rnd() % 4);
  if (rnd() % 2) { hm_wr16(ram, 0xCF, 0); ram[0xD1] = 0; }
  hm_wr16(ram, 0x0196, (uint16_t)rnd());
  hm_wr16(ram, 0xD2, (uint16_t)rnd());
  uint16_t room = (uint16_t)rnd();
  hm_wr16(ram, 0x0878, room);
  hm_wr16(ram, 0x087A, (uint16_t)((rnd() % 2) ? room : rnd()));
  for (unsigned a = 0x11F5A; a < 0x11F80; a++) ram[a] = (uint8_t)rnd();
  if (rnd() % 2) ram[0x11F6E] &= (uint8_t)~0x10;
  if (rnd() % 2) hm_wr16(ram, 0x11F66, (uint16_t)(rnd() & ~0x001Fu));
  for (unsigned i = 0; i < 8; i++) ram[0x09A4 + i] = (uint8_t)((rnd() % 3) ? rnd() % 6 : 0);
  if (rnd() % 2) hm_wr16(ram, 0x11F78, (uint16_t)(rnd() & rnd()));
  for (unsigned g = 0; g < 5; g++)                                    /* affections */
    hm_wr16(ram, 0x11F1F + 2 * g, (uint16_t)((rnd() % 4) ? rnd() % 0x400 : rnd()));
  hm_wr16(ram, HM_STORED_GRASS, (uint16_t)((rnd() % 4) ? rnd() % 1000 : rnd() % 2));
  /* the coop's nests are in columns $10-$DF: keep the tile in front in
   * them (PlayerAction_53 would search for ever otherwise) */
  hm_wr16(ram, 0xD6, (uint16_t)(0x20 + rnd() % 0xA0));
  hm_wr16(ram, 0xDA, (uint16_t)(rnd() % 4));
  hm_wr16(ram, 0x01, (uint16_t)(0x1000 + rnd() % 0x800));             /* PlayerAction_46's stray store */
  ram[0x09A0] = (uint8_t)(0x18 + rnd() % 12);                         /* the dealer's cow, chicken */
  ram[0x09A1] = (uint8_t)(0x24 + rnd() % 13);
  ram[0x11F18] = (uint8_t)(rnd() % 3);                                /* year */
  ram[0x11F0A] = (uint8_t)((rnd() % 3) ? rnd() % 13 : 12);            /* cows, chickens */
  ram[0x11F0B] = (uint8_t)((rnd() % 3) ? rnd() % 13 : 12);
  hm_wr16(ram, 0x11F29, (uint16_t)(rnd() % 0x200));                   /* room */
  hm_wr16(ram, 0x11F0E, (uint16_t)((rnd() % 2) ? 0x00FA : 0x01F4));   /* lumber needed */
  static const uint16_t wood[] = { 0x00F9, 0x00FA, 0x01F3, 0x01F4 };
  hm_wr16(ram, HM_STORED_WOOD, (uint16_t)((rnd() % 2) ? wood[rnd() % 4] : rnd() % 1000));
  ram[0x0921] = (uint8_t)((rnd() % 2) ? 0x0A + rnd() % 2 : rnd() % 0x20);   /* the tool in hand */
  for (unsigned i = 0; i < 12; i++) {                                 /* the cows' records, byte +0 */
    static const uint8_t cow[] = { 0x01, 0x03, 0x05, 0x11, 0x21, 0x41, 0x71, 0x07 };
    ram[0x0C1C6 + 16 * i] = (uint8_t)((rnd() % 4) ? cow[rnd() % sizeof cow] : rnd());
  }
  ram[0x11F04] = (uint8_t)rnd(); ram[0x11F05] = (uint8_t)rnd();       /* money */
  ram[0x11F06] = (uint8_t)((rnd() % 2) ? 0 : rnd() % 0x10);
  hm_wr16(ram, 0x11F15, (uint16_t)(rnd() % 0x400)); ram[0x11F17] = 0; /* the cow's price */
  if (rnd() % 2) { hm_wr16(ram, 0x11F07, 0); ram[0x11F09] = (uint8_t)((rnd() % 2) ? 0 : rnd()); }
  else { hm_wr16(ram, 0x11F07, (uint16_t)(1 + rnd() % 0xFFFF)); ram[0x11F09] = (uint8_t)rnd(); }
  hm_wr16(ram, 0x0124, (uint16_t)((rnd() % 4) ? rnd() & 0x7FFF : rnd()));
  hm_wr16(ram, 0x0128, (uint16_t)rnd());
  r->a = (uint16_t)(2 * (0x0F + rnd() % 0x49));
  r->x = (uint16_t)rnd(); r->y = (uint16_t)(rnd() % 0x400);
}

/* playerstate.c: the player at any of his steps: the tool he may be using,
 * text boxes, the camera and the scroll, the collision checks and the
 * tile map, his game object, putting an item down, the sound chip; the
 * action, the state bits and the counters each step looks at. A = the
 * action x 2, as SUB_81BFB7 leaves it. */
static void prep_player_state_as(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void), int action)
{
  prep_text_box(ram, r, rnd);
  prep_scroll(ram, r, rnd);
  prep_camera(ram, r, rnd);
  prep_collision(ram, r, rnd);
  prep_put_down(ram, r, rnd);
  prep_player_object(ram, r, rnd);
  /* every animation a step may set: their sprites' pieces ready (a sprite
   * the fuzz did not prepare can send CODE_81CFE6 off the rails) */
  static uint8_t done_sprite[0x450 / 8];
  memset(done_sprite, 0, sizeof done_sprite);
  for (unsigned anim = 0; anim < 0x100; anim++) {
    uint16_t sprite = hm_bus_rd16(ram, 0x81D210u + 3u * anim);
    if (sprite >= 0x450 || (done_sprite[sprite >> 3] & (1 << (sprite & 7)))) continue;
    done_sprite[sprite >> 3] |= (uint8_t)(1 << (sprite & 7));
    prep_sprite(ram, sprite, rnd);
  }
  for (unsigned g = 0; g < 0x40; g++)
    if (hm_rd16(ram, 0x0A320 + 4 * g) < 40) hm_wr16(ram, 0x0A320 + 4 * g, (uint16_t)(40 + rnd() % 200));
  prep_audio(ram, r, rnd);
  /* inside the map, as prep_collision wants it (CEEEE reads tiles around) */
  hm_wr16(ram, 0xD6, (uint16_t)(0x40 + rnd() % 0x380));
  hm_wr16(ram, 0xD8, (uint16_t)(0x40 + rnd() % 0x380));
  static const uint16_t actions[] = { 0, 0, 1, 2, 3, 9, 0x0E, 0x17, 0x18, 0x0A, 0x08 };
  uint16_t act = (uint16_t)((rnd() % 4) ? actions[rnd() % sizeof actions] : rnd() % 0x1C);
  static const uint16_t on_foot[] = { 0, 1, 2, 6, 7, 0x0E };
  if (action == -2) act = on_foot[rnd() % 6];               /* PlayerState_00, CODE_81CB14 */
  else if (action >= 0) act = (uint16_t)action;             /* the step's own action */
  hm_wr16(ram, 0xD4, act);
  static const uint16_t bits[] = { 0x0002, 0x0004, 0x0010, 0x0020, 0x0080, 0x0100, 0x0200, 0x0800,
                                   0x1000, 0x2000, 0x4000, 0x8000 };
  uint16_t st = (uint16_t)((rnd() % 2) ? rnd() : 0);
  if (rnd() % 2) st = (uint16_t)(st | bits[rnd() % 12]);
  if (rnd() % 2) st = (uint16_t)(st & ~(bits[rnd() % 12] | bits[rnd() % 12]));
  hm_wr16(ram, 0xD2, st);
  static const uint8_t idle[] = { 0xEF, 0xF0, 0x00, 0x77 };
  ram[0x0925] = (uint8_t)((rnd() % 2) ? idle[rnd() % 4] : rnd());
  ram[0x091A] = (uint8_t)((rnd() % 2) ? 0x0F : rnd());
  ram[0x091B] = (uint8_t)((rnd() % 2) ? 0x17 : rnd());
  ram[0x0919] = (uint8_t)((rnd() % 2) ? 0x07 : rnd() % 8);
  ram[0x0971] = (uint8_t)rnd();
  static const uint8_t items[] = { 2, 8, 1, 5 };
  ram[0x0984] = (uint8_t)((rnd() % 2) ? items[rnd() % 4] : rnd() % 0x5B);
  static const uint8_t maps[] = { 0x15, 0x16, 0x17, 0x29, 0x00, 0x10 };
  if (rnd() % 2) ram[0x22] = maps[rnd() % sizeof maps];
  hm_wr16(ram, 0x0911, (uint16_t)(rnd() % 4));
  hm_wr16(ram, 0x0913, (uint16_t)(rnd() % 4));
  hm_wr16(ram, 0xDA, (uint16_t)(rnd() % 4));
  hm_wr16(ram, 0x1E, (uint16_t)(1 + rnd() % 3));
  for (unsigned a = 0x11F5A; a < 0x11F70; a++) ram[a] = (uint8_t)rnd();
  ram[0x11F1C] = (uint8_t)(rnd() % 24);
  ram[0x0921] = (uint8_t)(rnd() % 0x1C);
  ram[0x0924] = (uint8_t)rnd();
  hm_wr16(ram, 0x11F2C, (uint16_t)(rnd() % 0x400)); hm_wr16(ram, 0x11F2E, (uint16_t)(rnd() % 0x400));
  if (action == -2 && rnd() % 2) hm_wr16(ram, 0xD2, (uint16_t)(hm_rd16(ram, 0xD2) | 0x0004));
  /* the steps that move him call CODE_81CB77 compiled: on a map of mixed
   * tiles its corner slides can turn him back and forth for ever (the
   * original does too). A map of one tile type, solid or free as a whole
   * (no half bits), cannot; CODE_81CB77 itself is fuzzed on mixed maps in
   * the interpreter, which gives up on such a run. */
  if (action == -3) {                                      /* CODE_81CB77: tile types at the slide / door limits */
    static const uint8_t edge[] = { 0x00, 0x9F, 0xA0, 0xAF, 0xB0, 0xBF, 0xC0, 0xCF, 0xD0 };
    for (unsigned i = 0; i < 4096; i++)
      if (rnd() % 2) ram[0x09B6 + i] = edge[rnd() % sizeof edge];
  }
  if (action == 0x13 && rnd() % 2) {                       /* the animation it ends with */
    static const uint16_t last[] = { 0x008B, 0x008C, 0x008A };
    hm_wr16(ram, 0x0901, last[rnd() % 3]);
  }
  if (action == 0x11 && rnd() % 2) {                       /* a state whose next two rolls are 0 */
    for (int tries = 0; tries < 100000; tries++) {
      uint8_t st[3] = { (uint8_t)rnd(), (uint8_t)rnd(), (uint8_t)rnd() }, roll[2];
      uint8_t first[3] = { st[0], st[1], st[2] };
      for (int k = 0; k < 2; k++) {                       /* GetRNG */
        uint8_t c = ((st[1] ^ st[0]) & 0x02) != 0, n;
        n = (uint8_t)((st[1] >> 1) | (c << 7)); c = st[1] & 1; st[1] = n;
        n = (uint8_t)((st[0] >> 1) | (c << 7)); c = st[0] & 1; st[0] = n;
        n = (uint8_t)((st[2] >> 1) | (c << 7)); c = st[2] & 1; st[2] = n;
        unsigned sum = st[1] + 0x47u;
        uint8_t a = (uint8_t)sum; c = sum > 0xFF;
        n = (uint8_t)((a >> 1) | (c << 7)); c = a & 1; a = n;
        n = (uint8_t)((a >> 1) | (c << 7)); c = a & 1; a = n;
        a ^= st[0];
        sum = a + st[2] + c;
        st[0] = (uint8_t)sum;
        roll[k] = (uint8_t)sum;
      }
      if (roll[0] <= 1 && roll[1] <= 63) {
        ram[HM_RNG_STATE] = first[0]; ram[HM_RNG_STATE + 1] = first[1]; ram[HM_RNG_STATE + 2] = first[2];
        break;
      }
    }
  }
  if (action != -3) {
    uint32_t table = (uint32_t)ram[0x0F] << 16 | hm_rd16(ram, 0x0D);
    uint8_t type = 0;
    for (int tries = 0; tries < 64; tries++) {
      type = (uint8_t)rnd();
      if (!(hm_bus_rd8(ram, table + 4u * type + 2) & 0x1E)) break;
    }
    memset(&ram[0x09B6], type, 4096);
  }
  r->a = (uint16_t)(2 * act);
  r->x = (uint16_t)rnd(); r->y = (uint16_t)rnd();
}
#define PREP_PS(name, action) \
  static void prep_ps_##name(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void)) { prep_player_state_as(ram, r, rnd, action); }
PREP_PS(any, -1) PREP_PS(move, -3) PREP_PS(foot, -2) PREP_PS(1A, 0x1A) PREP_PS(17, 0x17) PREP_PS(18, 0x18) PREP_PS(16, 0x16)
PREP_PS(15, 0x15) PREP_PS(14, 0x14) PREP_PS(0F, 0x0F) PREP_PS(10, 0x10) PREP_PS(11, 0x11) PREP_PS(12, 0x12)
PREP_PS(13, 0x13) PREP_PS(1B, 0x1B) PREP_PS(0D, 0x0D) PREP_PS(0C, 0x0C) PREP_PS(0B, 0x0B) PREP_PS(0A, 0x0A)
PREP_PS(00, 0x00) PREP_PS(01, 0x01) PREP_PS(02, 0x02) PREP_PS(0E, 0x0E) PREP_PS(03, 0x03) PREP_PS(04, 0x04)
PREP_PS(05, 0x05) PREP_PS(08, 0x08) PREP_PS(09, 0x09)
/* playerstate.c: $1C, swapping the tools: as the other steps, and the
 * sound chip with a frame's NMI (ToolUsedSound2); tools 0-$1B */
static void prep_sound_wait(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void));
/* playerstate.c: SUB_81BFB7 / PlayerInteractionSelector: any step (as
 * prep_ps_any) or a pending action (as prep_player_action), the sound chip
 * and a frame's NMI */
static void prep_player_action(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void));
static void prep_player_frame(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  int step = rnd() % 2;
  if (step) prep_player_state_as(ram, r, rnd, (int)(rnd() % 29));
  else prep_player_action(ram, r, rnd);
  prep_sound_wait(ram, r, rnd);
  ram[0x0921] = (uint8_t)(rnd() % 0x1C);
  ram[0x0923] = (uint8_t)(rnd() % 0x1C);
  /* the step of the action, or the pending action; sometimes neither */
  if (step) { ram[0xD2] |= 0x01; ram[0xD2] &= (uint8_t)~0x40; }
  else ram[0xD2] |= 0x41;
  if (rnd() % 8 == 0) ram[0xD2] &= (uint8_t)~0x01;
  if (hm_rd16(ram, 0xD4) > 0x1C) hm_wr16(ram, 0xD4, 0);
  if (ram[0x096E] >= 0x58) ram[0x096E] = (uint8_t)(rnd() % 0x58);
}
static void prep_ps_swap(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_player_state_as(ram, r, rnd, 0x1C);
  prep_sound_wait(ram, r, rnd);
  ram[0x0921] = (uint8_t)(rnd() % 0x1C);
  ram[0x0923] = (uint8_t)(rnd() % 0x1C);
}

/* MapEvent_DC4A: mostly on the way to its visitor's days (year 2 on, fall
 * or winter, an ordinary weekday) */
static void prep_map_event_dc4a(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_map_event(ram, r, rnd);
  if (rnd() % 4 == 0) return;
  ram[0x11F68] |= 0x04;
  ram[0x11F5E] &= (uint8_t)~0x20;
  ram[0x11F6C] &= (uint8_t)~0x14;
  ram[0x11F18] = (uint8_t)(1 + rnd() % 3);
  ram[0x11F19] = (uint8_t)(2 + rnd() % 2);
  ram[0x11F1B] = (uint8_t)(1 + rnd() % 30);
  hm_wr16(ram, 0x0196, (uint16_t)(hm_rd16(ram, 0x0196) & ~0x001Au));
  ram[0x11F6B] &= (uint8_t)~0x02;
  ram[0x11F1A] = (uint8_t)(1 + rnd() % 5);
}

/* objects.c: InvertIfFliped: an offset and the flip bits. */
static void prep_flip(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  r->a = (uint16_t)((rnd() % 2) ? rnd() % 0x40 : rnd());
  hm_wr16(ram, 0x9F, (uint16_t)rnd());
}

/* mapedit.c: footprints (winter often, the farm or the mountain, a
 * position, the tiles) and the map animation player (a script of steps in
 * WRAM at $7F:E000 with real tile changes, ends and jumps, the wait). */
static void prep_footprints(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_tile_in_front(ram, r, rnd);
  prep_tile_dma(ram, r, rnd);
  ram[0x11F19] = (uint8_t)((rnd() % 4) ? 3 : rnd() % 4);
  static const uint8_t maps[] = { 0, 1, 2, 3, 0x10, 0x12, 0x14, 0x15, 0x08, 0x26 };
  ram[0x22] = maps[rnd() % sizeof maps];
  hm_wr16(ram, 0x0907, (uint16_t)(rnd() & 0x3FF));
  hm_wr16(ram, 0x0909, (uint16_t)(rnd() & 0x3FF));
}
static void prep_animation_player(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  prep_tile_dma(ram, r, rnd);
  for (unsigned i = 0; i < 16; i++) {
    uint8_t *s = &ram[0x1E000 + 7 * i];
    unsigned kind = i < 4 ? 2 : rnd() % 8;     /* the first 4: plain steps, the jumps' targets
                                                * (a jump's target is done unchecked) */
    HmRegs t = *r;
    prep_map_change(ram, &t, rnd);
    hm_wr16(ram, 0x1E000 + 7 * i, kind == 0 ? 0xFFFF : kind == 1 ? 0xFFFE : t.a);
    if (kind == 1) { s[2] = (uint8_t)(rnd() % 4 * 7); s[3] = 0xE0; s[4] = 0x7F; }
    else { hm_wr16(ram, 0x1E002 + 7 * i, t.x); hm_wr16(ram, 0x1E004 + 7 * i, t.y); }
    s[6] = (uint8_t)(rnd() % 4);
  }
  uint16_t at = (uint16_t)(0xE000 + 7 * (rnd() % 12));
  int none = rnd() % 8 == 0;
  hm_wr16(ram, 0xCF, none ? 0 : at); ram[0xD1] = none ? 0 : 0x7F;
  ram[0x0989] = (uint8_t)((rnd() % 2) ? 0 : rnd() % 4);
}

/* menus.c: the name screen, menu 1 and mode 6, and InputTypeSelector
 * (every input mode). The page and cell of the name grid, the letters
 * typed, the pad, the mode and the menu. */
static void prep_inputs(uint8_t *ram, HmRegs *r, uint32_t (*rnd)(void))
{
  HmRegs keep = *r;
  if (rnd() % 2) prep_player_input(ram, r, rnd); else prep_menu(ram, r, rnd);
  *r = keep;
  prep_audio(ram, r, rnd);
  *r = keep;
  ram[0x00993] = (uint8_t)((rnd() % 8) ? rnd() % 3 : rnd() % 4);   /* page */
  ram[0x00994] = (uint8_t)(rnd() % 5);                                /* letters */
  /* the cell: the pages have 71, 84 and 49 cells (the grids at $82:EBF8,
   * $82:EE30, $82:F0D0) */
  static const uint16_t grids[] = { 0xEBF8, 0xEE30, 0xF0D0, 0xF0D0 };
  static const uint8_t cells_in[] = { 71, 84, 49, 49 };
  unsigned pg = ram[0x00993] & 3;
  hm_wr16(ram, 0x00991, (uint16_t)(rnd() % cells_in[pg]));
  if (rnd() % 2) {                       /* a cell with a code: a page, "done" */
    uint32_t g = 0x820000u | grids[pg];
    uint16_t cells[84]; unsigned n = 0;
    for (uint16_t c = 0; c < cells_in[pg]; c++) if (hm_bus_rd8(ram, g + c * 8u + 7)) cells[n++] = c;
    if (n) hm_wr16(ram, 0x00991, cells[rnd() % n]);
    if (rnd() % 4 == 0) ram[0x00994] = 0;                             /* no letter yet */
  }
  for (unsigned i = 0; i < 4; i++) ram[0x00885 + i] = (uint8_t)rnd();
  static const uint16_t keys[] = { 0x8000, 0x1000, 0x0800, 0x0400, 0x0200, 0x0100, 0x0080, 0 };
  if (rnd() % 2) hm_wr16(ram, 0x0128, keys[rnd() % 8]);
  if (rnd() % 2) hm_wr16(ram, 0x012C, keys[rnd() % 8]);
  ram[0x0019A] = (uint8_t)((rnd() % 8) ? 1 + rnd() % 6 : rnd());       /* input mode */
  static const uint8_t menus[] = { 1, 1, 2, 3, 5, 7, 8, 9, 0 };
  ram[0x95] = (uint8_t)((rnd() % 8) ? menus[rnd() % 9] : rnd());
  hm_wr16(ram, 0x009B1, (uint16_t)((rnd() % 2) ? 0x77 : rnd() % 0x78));  /* mode 6 timer */
  if (rnd() % 2) ram[0x0019B] |= 0x04;
  if (rnd() % 2) hm_wr16(ram, 0x11F5A, (uint16_t)(hm_rd16(ram, 0x11F5A) | 0x8000));
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
  { "ChangeStamina", hm_ChangeStamina, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C, "src/stamina.c",
    0x81D061, 1, 0, 0, 0xFF,
    { B8(HM_MAX_STAMINA, 0, 255), B8(HM_CURRENT_STAMINA, 0, 255), B8(HM_EXHAUSTION, 0, 3),
      W16(HM_GAME_STATE, 0, 0xFFFF), B8(0x11F60, 0, 255) } },
  { "GetsCowPointer", hm_GetsCowPointer, HM_OUT_A, "src/animals.c",
    0x83C9A7, 0, 0, 0, 0xFFFF, { { 0 } } },
  { "GetChickenPointer", hm_GetChickenPointer, HM_OUT_A, "src/animals.c",
    0x83C995, 0, 0, 0, 0xFFFF, { { 0 } } },
  { "DivisionUnsigned", hm_DivisionUnsigned, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_IO, "src/math.c",
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
  { "MapEvent_E586", hm_MapEvent_E586, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/mapevents.c", 0x83E586, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_map_event },
  { "MapEvent_E5A2", hm_MapEvent_E5A2, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/mapevents.c", 0x83E5A2, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_map_event },
  { "MapEvent_E5B4", hm_MapEvent_E5B4, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/mapevents.c", 0x83E5B4, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_map_event },
  { "MapEvent_E5CD", hm_MapEvent_E5CD, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/mapevents.c", 0x83E5CD, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_map_event },
  { "MapEvent_E5DF", hm_MapEvent_E5DF, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/mapevents.c", 0x83E5DF, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_map_event },
  { "MapEvent_E5F1", hm_MapEvent_E5F1, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/mapevents.c", 0x83E5F1, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_map_event },
  { "MapEvent_E603", hm_MapEvent_E603, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/mapevents.c", 0x83E603, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_map_event },
  { "MapEvent_E632", hm_MapEvent_E632, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/mapevents.c", 0x83E632, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_map_event },
  { "MapEvent_E661", hm_MapEvent_E661, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/mapevents.c", 0x83E661, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_map_event },
  { "MapEvent_E690", hm_MapEvent_E690, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/mapevents.c", 0x83E690, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_map_event },
  { "MapEvent_E6B2", hm_MapEvent_E6B2, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/mapevents.c", 0x83E6B2, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_map_event },
  { "MapEvent_E6C4", hm_MapEvent_E6C4, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/mapevents.c", 0x83E6C4, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_map_event },
  { "MapEvent_E74C", hm_MapEvent_E74C, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/mapevents.c", 0x83E74C, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_map_event },
  { "MapEvent_D4D7", hm_MapEvent_D4D7, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/mapevents.c", 0x83D4D7, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_map_event },
  { "MapEvent_D5E7", hm_MapEvent_D5E7, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/mapevents.c", 0x83D5E7, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_map_event },
  { "MapEvent_D613", hm_MapEvent_D613, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/mapevents.c", 0x83D613, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_map_event },
  { "MapEvent_D74E", hm_MapEvent_D74E, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/mapevents.c", 0x83D74E, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_map_event },
  { "MapEvent_D818", hm_MapEvent_D818, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/mapevents.c", 0x83D818, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_map_event },
  { "MapEvent_D844", hm_MapEvent_D844, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/mapevents.c", 0x83D844, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_map_event },
  { "MapEvent_D929", hm_MapEvent_D929, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/mapevents.c", 0x83D929, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_map_event },
  { "MapEvent_D955", hm_MapEvent_D955, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/mapevents.c", 0x83D955, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_map_event },
  { "MapEvent_DA1F", hm_MapEvent_DA1F, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/mapevents.c", 0x83DA1F, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_map_event },
  { "MapEvent_DA4B", hm_MapEvent_DA4B, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/mapevents.c", 0x83DA4B, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_map_event },
  { "MapEvent_DAED", hm_MapEvent_DAED, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/mapevents.c", 0x83DAED, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_map_event },
  { "MapEvent_DB8F", hm_MapEvent_DB8F, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/mapevents.c", 0x83DB8F, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_map_event },
  { "MapEvent_DC1E", hm_MapEvent_DC1E, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/mapevents.c", 0x83DC1E, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_map_event },
  { "MapEvent_DC4A", hm_MapEvent_DC4A, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/mapevents.c", 0x83DC4A, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_map_event_dc4a },
  { "MapEvent_DD95", hm_MapEvent_DD95, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/mapevents.c", 0x83DD95, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_map_event },
  { "MapEvent_DE64", hm_MapEvent_DE64, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/mapevents.c", 0x83DE64, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_map_event },
  { "MapEvent_DE9B", hm_MapEvent_DE9B, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/mapevents.c", 0x83DE9B, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_map_event },
  { "MapEvent_E407", hm_MapEvent_E407, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/mapevents.c", 0x83E407, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_map_event },
  { "MapEvent_E4CB", hm_MapEvent_E4CB, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/mapevents.c", 0x83E4CB, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_map_event },
  { "MapEvent_D2E0", hm_MapEvent_D2E0, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/mapevents.c", 0x83D2E0, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_map_event },
  { "MapEvent_E76D", hm_MapEvent_E76D, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/mapevents.c", 0x83E76D, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_map_event },
  { "MapEvent_EBA6", hm_MapEvent_EBA6, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/mapevents.c", 0x83EBA6, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_map_event_ending },
  { "MapEvent_CB69", hm_MapEvent_CB69, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/mapevents.c", 0x83CB69, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_map_event_farm },
  { "CODE_83F4D8", hm_CODE_83F4D8, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/animals.c", 0x83F4D8, 0, 0, 0, 0xFFFF, { { 0 } }, 40, prep_cow_festival },
  { "Sub_82A9A0", hm_Sub_82A9A0, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/farm.c", 0x82A9A0, 0, 0, 0, 0xFFFF, { W16(0x0092E, 0, 0xFFFF) }, 40, prep_random_farm },
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
  { "CowFeedingandStatus", hm_CowFeedingandStatus, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/animals.c", 0x83BC5A, 0, 0, 0, 0xFFFF,
    { B8(HM_RNG_STATE, 0, 255), B8(HM_RNG_STATE + 1, 0, 255), W16(0x11F6E, 0, 0xFFFF),
      W16(0x11F64, 0, 0xFFFF), W16(0x11F45, 0, 0xFFFF), W16(0x11F33, 0, 1100) },
    200, prep_animals },
  { "FindMostLovedName", hm_FindMostLovedName, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/people.c", 0x828EC6, 0, 0, 0, 0xFFFF,
    { W16(0x11F1F, 0, 999), W16(0x11F21, 0, 999), W16(0x11F23, 0, 999), W16(0x11F25, 0, 999),
      W16(0x11F27, 0, 999) } },
  { "WifePregnanacyandChilds", hm_WifePregnanacyandChilds,
    HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP, "src/people.c", 0x828790, 0, 0, 0, 0xFFFF,
    { W16(0x11F6C, 0, 0xFFFF), W16(0x11F33, 0, 1100) }, 0, prep_family },
  { "LoadsDateNames", hm_LoadsDateNames, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C, "src/calendar.c",
    0x8289D6, 1, 0, 0, 0xFF, { B8(HM_SEASON, 0, 3), B8(HM_WEEKDAY, 0, 6), B8(HM_DAY, 0, 31) } },
  { "AddMoney", hm_AddMoney, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C, "src/stock.c",
    0x83B1C9, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_money },
  { "SUB_8281C0", hm_SUB_8281C0, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C, "src/weather.c",
    0x8281C0, 0, 0, 0, 0xFFFF, { B8(HM_WEATHER_TOMORROW, 0, 40), W16(0x00196, 0, 0x03FF) } },
  { "HaveLunch", hm_HaveLunch, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C, "src/stamina.c", 0x8280AA, 1, 0, 0, 0xFF,
    { B8(0x11F1C, 11, 13), W16(HM_GAME_STATE, 0, 0xFFFF), W16(HM_PLAYER_ACTION, 0x0E, 0x19),
      B8(HM_MAX_STAMINA, 0, 255), B8(HM_CURRENT_STAMINA, 0, 255), B8(HM_RNG_STATE, 0, 255) } },
  { "GetTileContent", hm_GetTileContent, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C, "src/tools.c",
    0x82B124, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_tile_in_front },
  { "UNK_CheckTileProperty", hm_UNK_CheckTileProperty, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/tools.c", 0x82AC61, 0, 0, 0, 3, { B8(HM_SEASON, 0, 3) }, 0, prep_tile_in_front },
  { "CheckToolSuccess", hm_CheckToolSuccess, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/tools.c", 0x82AA71, 0, 0, 0, 0xFFFF, { B8(HM_SEASON, 0, 3) }, 0, prep_tile_in_front },
  { "ResetsCurrentCCStructureShort", hm_ResetsCurrentCCStructureShort,
    HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR, "src/events.c",
    0x848961, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_cc_pointer },
  { "UNK_LoadCCDataLong", hm_UNK_LoadCCDataLong, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/events.c", 0x848097, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_start },
  { "UNK_LoadCCDataShort", hm_UNK_LoadCCDataShort, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/events.c", 0x84803F, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_start },
  { "CalculateTileinFront", hm_CalculateTileinFront, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/tools.c", 0x81D14E, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_tool },
  { "TileAtOffsetFromPlayer", hm_TileAtOffsetFromPlayer, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/tools.c", 0x81D1A4, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_tool },
  { "PreCheckToolSuccess", hm_PreCheckToolSuccess, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/tools.c", 0x8292A0, 0, 0, 0, 0xFFFF, { B8(HM_SEASON, 0, 3) }, 0, prep_player_tool_near },
  { "PreCheckToolSuccessLine", hm_PreCheckToolSuccessLine,
    HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR, "src/tools.c", 0x8292BC, 1, 0, 0, 5,
    { B8(HM_SEASON, 0, 3) }, 0, prep_player_tool_near },
  { "PreCheckToolSuccessSquare", hm_PreCheckToolSuccessSquare,
    HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR, "src/tools.c", 0x8292D6, 1, 0, 0, 8,
    { B8(HM_SEASON, 0, 3) }, 0, prep_player_tool_near },
  /* A up to $CC0: past that the table read leaves the ROM ($82:71D7...) and
   * the original gets open-bus values. The game uses small entries. */
  { "SUB_81A801", hm_SUB_81A801, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C, "src/mapedit.c",
    0x81A801, 0, 0, 0, 0x0CC0, { { 0 } } },
  { "SUB_81A83A", hm_SUB_81A83A, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C, "src/mapedit.c",
    0x81A83A, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_map_change },
  { "SUB_82B060", hm_SUB_82B060, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C, "src/mapedit.c",
    0x82B060, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_map_block },
  { "AddProgrammedDMA", hm_AddProgrammedDMA, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C, "src/dma.c",
    0x808A33, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_dma },
  { "QueueTileGraphicsDMA", hm_QueueTileGraphicsDMA, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/mapedit.c", 0x81A6C1, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_tile_dma },
  { "ChangeMapTile", hm_ChangeMapTile, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/mapedit.c", 0x81A688, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_map_change_dma },
  { "LoadSpriteComponent", hm_LoadSpriteComponent, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/objects.c", 0x858C30, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_sprite_piece },
  { "LoadGameObjectComponents", hm_LoadGameObjectComponents,
    HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/objects.c", 0x858AE5, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_gobj_pieces },
  { "CreateGameObject", hm_CreateGameObject, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/objects.c", 0x858000, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_create_gobj },
  { "SUB_8180B7", hm_SUB_8180B7, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/items.c", 0x8180B7, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_put_down },
  { "SUB_818000", hm_SUB_818000, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU | HM_INTERP,
    "src/items.c", 0x818000, 0, 0, 0, 0xFFFF, { { 0 } }, 100, prep_put_down },
  { "CODE_81883A", hm_CODE_81883A, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/items.c", 0x81883A, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "CODE_818855", hm_CODE_818855, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/items.c", 0x818855, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item_food },
  { "CODE_81889A", hm_CODE_81889A, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/items.c", 0x81889A, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "CODE_8188BC", hm_CODE_8188BC, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/items.c", 0x8188BC, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "CODE_8188E5", hm_CODE_8188E5, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/items.c", 0x8188E5, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "CODE_818923", hm_CODE_818923, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/items.c", 0x818923, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item_shop },
  { "CODE_818B6B", hm_CODE_818B6B, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/items.c", 0x818B6B, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item_food },
  { "CODE_818BA5", hm_CODE_818BA5, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/items.c", 0x818BA5, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item_food },
  { "CODE_818C24", hm_CODE_818C24, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/items.c", 0x818C24, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "CODE_818D38", hm_CODE_818D38, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/items.c", 0x818D38, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "CODE_818E16", hm_CODE_818E16, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/items.c", 0x818E16, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "CODE_818E1B", hm_CODE_818E1B, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/items.c", 0x818E1B, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "CODE_818E98", hm_CODE_818E98, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/items.c", 0x818E98, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "CODE_818F2A", hm_CODE_818F2A, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/items.c", 0x818F2A, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "CODE_818F5A", hm_CODE_818F5A, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/items.c", 0x818F5A, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "CODE_819010", hm_CODE_819010, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/items.c", 0x819010, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "CODE_819021", hm_CODE_819021, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/items.c", 0x819021, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "CODE_819049", hm_CODE_819049, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/items.c", 0x819049, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "CODE_819228", hm_CODE_819228, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/items.c", 0x819228, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "CODE_819379", hm_CODE_819379, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/items.c", 0x819379, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "CODE_81968F", hm_CODE_81968F, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/items.c", 0x81968F, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "CODE_819695", hm_CODE_819695, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/items.c", 0x819695, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "ItemObject_01", hm_ItemObject_01, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x8180FE, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item_food },
  { "ItemObject_02", hm_ItemObject_02, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x818102, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item_food },
  { "ItemObject_03", hm_ItemObject_03, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x818106, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item_food },
  { "ItemObject_04", hm_ItemObject_04, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x81810A, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item_food },
  { "ItemObject_05", hm_ItemObject_05, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x81810E, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item_food },
  { "ItemObject_06", hm_ItemObject_06, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x818112, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item_food },
  { "ItemObject_07", hm_ItemObject_07, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x818116, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item_food },
  { "ItemObject_08", hm_ItemObject_08, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x81811A, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item_food },
  { "ItemObject_09", hm_ItemObject_09, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x81811E, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "ItemObject_0A", hm_ItemObject_0A, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x818122, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "ItemObject_0D", hm_ItemObject_0D, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x818126, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "ItemObject_0E", hm_ItemObject_0E, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x81812A, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "ItemObject_0F", hm_ItemObject_0F, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x81812E, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "ItemObject_10", hm_ItemObject_10, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x818132, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "ItemObject_11", hm_ItemObject_11, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x81814A, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "ItemObject_12", hm_ItemObject_12, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x818162, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "ItemObject_13", hm_ItemObject_13, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x81817A, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "ItemObject_14", hm_ItemObject_14, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x818192, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "ItemObject_15", hm_ItemObject_15, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x818196, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "ItemObject_16", hm_ItemObject_16, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x81819A, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "ItemObject_17", hm_ItemObject_17, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x81819E, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "ItemObject_18", hm_ItemObject_18, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x8181A2, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "ItemObject_19", hm_ItemObject_19, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x8181A6, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "ItemObject_1A", hm_ItemObject_1A, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x8181AA, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "ItemObject_1B", hm_ItemObject_1B, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x8181AE, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item_shop },
  { "ItemObject_1C", hm_ItemObject_1C, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x8181C8, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item_shop },
  { "ItemObject_1D", hm_ItemObject_1D, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x8181E2, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item_shop },
  { "ItemObject_1E", hm_ItemObject_1E, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x8181FC, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "ItemObject_1F", hm_ItemObject_1F, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x81823E, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "ItemObject_20", hm_ItemObject_20, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x818280, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "ItemObject_21", hm_ItemObject_21, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x8182C2, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "ItemObject_22", hm_ItemObject_22, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x8182FE, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "ItemObject_23", hm_ItemObject_23, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x8183B6, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "ItemObject_24", hm_ItemObject_24, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x8183B7, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "ItemObject_25", hm_ItemObject_25, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x8183BB, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "ItemObject_26", hm_ItemObject_26, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x8183BF, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "ItemObject_27", hm_ItemObject_27, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x8183C3, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "ItemObject_28", hm_ItemObject_28, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x8183C4, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "ItemObject_29", hm_ItemObject_29, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x8183C5, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "ItemObject_44", hm_ItemObject_44, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x818499, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item_shop },
  { "ItemObject_45", hm_ItemObject_45, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x8184D5, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item_shop },
  { "ItemObject_46", hm_ItemObject_46, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x818511, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item_shop },
  { "ItemObject_47", hm_ItemObject_47, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x81854D, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item_shop },
  { "ItemObject_48", hm_ItemObject_48, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x818589, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item_shop },
  { "ItemObject_49", hm_ItemObject_49, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x8185C5, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item_shop },
  { "ItemObject_4A", hm_ItemObject_4A, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x8185DF, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item_shop },
  { "ItemObject_4B", hm_ItemObject_4B, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x8185F6, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item_shop },
  { "ItemObject_4C", hm_ItemObject_4C, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x81860D, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item_shop },
  { "ItemObject_4D", hm_ItemObject_4D, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x818624, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item_shop },
  { "ItemObject_4E", hm_ItemObject_4E, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x81865B, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item_shop },
  { "ItemObject_4F", hm_ItemObject_4F, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x818692, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item_shop },
  { "ItemObject_50", hm_ItemObject_50, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x8186C9, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item_shop },
  { "ItemObject_51", hm_ItemObject_51, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x818700, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item_shop },
  { "ItemObject_52", hm_ItemObject_52, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x818737, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item_shop },
  { "ItemObject_53", hm_ItemObject_53, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x81874E, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item_shop },
  { "ItemObject_54", hm_ItemObject_54, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x818765, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item_shop },
  { "ItemObject_55", hm_ItemObject_55, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x818781, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item_shop },
  { "ItemObject_56", hm_ItemObject_56, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x81879D, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item_shop },
  { "ItemObject_58", hm_ItemObject_58, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x8187B7, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "ItemObject_59", hm_ItemObject_59, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x8187BB, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "ItemObject_5A", hm_ItemObject_5A, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/items.c", 0x8187BF, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_item },
  { "CODE_81D716", hm_CODE_81D716, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/actions.c", 0x81D716, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_10", hm_PlayerAction_10, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81D7C5, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_11", hm_PlayerAction_11, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81D979, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_12", hm_PlayerAction_12, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81D9CC, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_13", hm_PlayerAction_13, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81DA82, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_14", hm_PlayerAction_14, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81DB38, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_15", hm_PlayerAction_15, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81DBA1, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_16", hm_ActionNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81DBCC, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_17", hm_ActionNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81DBCD, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_18", hm_ActionNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81DBCE, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_19", hm_ActionNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81DBCF, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_1A", hm_ActionNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81DBD0, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_1B", hm_ActionNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81DBD1, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_1C", hm_ActionNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81DBD2, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_1D", hm_PlayerAction_1D, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81DBD3, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_1E", hm_PlayerAction_1E, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81DBF0, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_1F", hm_ActionNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81DC57, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_20", hm_ActionNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81DC58, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_21", hm_ActionNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81DC59, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_22", hm_ActionNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81DC5A, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_23", hm_ActionNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81DC5B, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_24", hm_ActionNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81DC5C, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_25", hm_ActionNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81DC5D, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_26", hm_PlayerAction_26, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81DC5E, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_27", hm_PlayerAction_27, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81DCCE, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_28", hm_PlayerAction_28, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81DCF3, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_29", hm_PlayerAction_29, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81DD18, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_2A", hm_PlayerAction_2A, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81DD3D, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_2B", hm_PlayerAction_2B, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81DD62, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_2C", hm_PlayerAction_2C, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81DD87, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_2D", hm_PlayerAction_2D, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81DDAC, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_2F", hm_PlayerAction_2F, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81E245, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_30", hm_PlayerAction_30, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81E26A, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_31", hm_PlayerAction_31, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81E28F, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_32", hm_PlayerAction_32, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81E2B4, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_33", hm_PlayerAction_33, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81E2D9, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_34", hm_PlayerAction_34, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81E349, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_35", hm_PlayerAction_35, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP | HM_APU | HM_IO | HM_WAIT,
    "src/actions.c", 0x81E36E, 0, 0, 0, 0xFFFF, { { 0 } }, 4, prep_action_sound },
  { "PlayerAction_36", hm_PlayerAction_36, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81E394, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_37", hm_PlayerAction_37, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81E404, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_38", hm_PlayerAction_38, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81E429, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_39", hm_PlayerAction_39, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81E44E, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_3A", hm_PlayerAction_3A, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81E473, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_3B", hm_PlayerAction_3B, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81E498, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_3C", hm_PlayerAction_3C, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81E4BD, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_3D", hm_PlayerAction_3D, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81E4E2, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_3E", hm_PlayerAction_3E, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81E507, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_3F", hm_PlayerAction_3F, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81E52C, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_4A", hm_PlayerAction_4A, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81F490, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_4B", hm_PlayerAction_4B, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81F4AD, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_4C", hm_PlayerAction_4C, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81F4CA, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_4D", hm_PlayerAction_4D, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81F4E6, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_4E", hm_PlayerAction_4E, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81F502, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_4F", hm_PlayerAction_4F, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81F51E, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_50", hm_PlayerAction_50, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81F53A, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_2E", hm_PlayerAction_2E, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81DE1C, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_40", hm_PlayerAction_40, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81E59C, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_41", hm_PlayerAction_41, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81E6C3, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_42", hm_PlayerAction_42, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81E6EA, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_43", hm_PlayerAction_43, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81E79E, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_44", hm_PlayerAction_44, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81EBD5, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_45", hm_PlayerAction_45, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81EFAD, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_46", hm_PlayerAction_46, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81F061, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_47", hm_PlayerAction_47, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81F121, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_48", hm_PlayerAction_48, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81F2DA, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_49", hm_PlayerAction_49, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81F40E, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_51", hm_PlayerAction_51, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81F5B0, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_52", hm_PlayerAction_52, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81F6D7, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_53", hm_PlayerAction_53, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81F7FF, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_54", hm_PlayerAction_54, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81F86A, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_55", hm_PlayerAction_55, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81F942, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_56", hm_PlayerAction_56, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81FA69, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "PlayerAction_57", hm_PlayerAction_57, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/actions.c", 0x81FA84, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_action },
  { "CODE_858CB2", hm_CODE_858CB2, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/objects.c", 0x858CB2, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_loader },
  { "SUB_809A64", hm_SUB_809A64, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/playerstate.c", 0x809A64, 0, 0, 0, 0xFFFF, { { 0 } }, 400, prep_doors },
  { "SUB_809D0B", hm_SUB_809D0B, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/playerstate.c", 0x809D0B, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_mountain },
  { "PlayerState_00", hm_PlayerState_00, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP | HM_APU,
    "src/playerstate.c", 0x81BFDC, 0, 0, 0, 0xFFFF, { { 0 } }, 150, prep_ps_foot },
  { "ResetIdleAnimatioTimer", hm_ResetIdleAnimatioTimer, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/playerstate.c", 0x81C002, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_ps_any },
  { "SUB_81BFB7", hm_SUB_81BFB7, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU | HM_IO | HM_WAIT,
    "src/playerstate.c", 0x81BFB7, 0, 0, 0, 0xFFFF, { { 0 } }, 200, prep_player_frame },
  { "PlayerInteractionSelector", hm_PlayerInteractionSelector, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP | HM_APU | HM_IO | HM_WAIT,
    "src/playerstate.c", 0x81D570, 0, 0, 0, 0xFFFF, { { 0 } }, 200, prep_player_frame },
  { "PlayerState_1C", hm_PlayerState_1C, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP | HM_APU | HM_IO | HM_WAIT,
    "src/playerstate.c", 0x81C061, 0, 0, 0, 0xFFFF, { { 0 } }, 30, prep_ps_swap },
  { "PlayerState_1A", hm_PlayerState_1A, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/playerstate.c", 0x81C0B5, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_ps_1A },
  { "PlayerState_17", hm_PlayerState_17, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/playerstate.c", 0x81C111, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_ps_17 },
  { "PlayerState_18", hm_PlayerState_18, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/playerstate.c", 0x81C153, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_ps_18 },
  { "PlayerState_16", hm_PlayerState_16, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/playerstate.c", 0x81C1A1, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_ps_16 },
  { "PlayerState_15", hm_PlayerState_15, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/playerstate.c", 0x81C1F0, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_ps_15 },
  { "PlayerState_14", hm_PlayerState_14, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/playerstate.c", 0x81C211, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_ps_14 },
  { "PlayerState_0F", hm_PlayerState_0F, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/playerstate.c", 0x81C232, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_ps_0F },
  { "PlayerState_10", hm_PlayerState_10, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/playerstate.c", 0x81C242, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_ps_10 },
  { "PlayerState_11", hm_PlayerState_11, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/playerstate.c", 0x81C263, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_ps_11 },
  { "PlayerState_12", hm_PlayerState_12, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/playerstate.c", 0x81C28E, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_ps_12 },
  { "PlayerState_13", hm_PlayerState_13, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/playerstate.c", 0x81C2AF, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_ps_13 },
  { "PlayerState_1B", hm_PlayerState_1B, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/playerstate.c", 0x81C333, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_ps_1B },
  { "PlayerState_0D", hm_PlayerState_0D, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/playerstate.c", 0x81C364, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_ps_0D },
  { "PlayerState_0C", hm_PlayerState_0C, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/playerstate.c", 0x81C395, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_ps_0C },
  { "PlayerState_0B", hm_PlayerState_0B, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/playerstate.c", 0x81C4EC, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_ps_0B },
  { "PlayerState_0A", hm_PlayerState_0A, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP | HM_APU,
    "src/playerstate.c", 0x81C51A, 0, 0, 0, 0xFFFF, { { 0 } }, 150, prep_ps_0A },
  { "CODE_81C556", hm_CODE_81C556, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/playerstate.c", 0x81C556, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_ps_00 },
  { "CODE_81C66E", hm_CODE_81C66E, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP | HM_APU,
    "src/playerstate.c", 0x81C66E, 0, 0, 0, 0xFFFF, { { 0 } }, 150, prep_ps_01 },
  { "CODE_81C736", hm_CODE_81C736, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP | HM_APU,
    "src/playerstate.c", 0x81C736, 0, 0, 0, 0xFFFF, { { 0 } }, 150, prep_ps_02 },
  { "CODE_81C83B", hm_CODE_81C83B, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/playerstate.c", 0x81C83B, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_ps_0E },
  { "PlayerState_03", hm_PlayerState_03, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP | HM_APU,
    "src/playerstate.c", 0x81C86B, 0, 0, 0, 0xFFFF, { { 0 } }, 150, prep_ps_03 },
  { "PlayerState_04", hm_PlayerState_04, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP | HM_APU,
    "src/playerstate.c", 0x81C991, 0, 0, 0, 0xFFFF, { { 0 } }, 150, prep_ps_04 },
  { "PlayerState_05", hm_PlayerState_05, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP | HM_APU,
    "src/playerstate.c", 0x81C9DF, 0, 0, 0, 0xFFFF, { { 0 } }, 150, prep_ps_05 },
  { "PlayerState_08", hm_PlayerState_08, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/playerstate.c", 0x81CA36, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_ps_08 },
  { "CODE_81CB14", hm_CODE_81CB14, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/playerstate.c", 0x81CB14, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_ps_foot },
  { "CODE_81CB5E", hm_CODE_81CB5E, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/playerstate.c", 0x81CB5E, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_ps_09 },
  { "CODE_81CB77", hm_CODE_81CB77, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP | HM_JSR,
    "src/playerstate.c", 0x81CB77, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_ps_move },
  { "SpawnMapObject", hm_SpawnMapObject, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/objects.c", 0x81A500, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_spawn },
  { "ToolUsedNoAction", hm_ToolUsedNoAction, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolused.c", 0x82931E, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_tool_none },
  { "ToolUsedSickle", hm_ToolUsedSickle, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolused.c", 0x829328, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_sickle },
  { "ToolUsedHoe", hm_ToolUsedHoe, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolused.c", 0x829460, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_hoe },
  { "ToolUsedHammer", hm_ToolUsedHammer, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolused.c", 0x8295C0, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_hammer },
  { "ToolUsedAxe", hm_ToolUsedAxe, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolused.c", 0x8297BD, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_axe },
  { "ToolUsedCornSeeds", hm_ToolUsedCornSeeds, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolused.c", 0x829999, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_corn },
  { "ToolUsedTomatoSeeds", hm_ToolUsedTomatoSeeds, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolused.c", 0x8299F8, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_tomato },
  { "ToolUsedPotatoSeeds", hm_ToolUsedPotatoSeeds, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolused.c", 0x829A57, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_potato },
  { "ToolUsedTurnipSeeds", hm_ToolUsedTurnipSeeds, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolused.c", 0x829AB4, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_turnip },
  { "ToolUsedCowMedicine", hm_ToolUsedCowMedicine, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolused.c", 0x829B11, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_medicine },
  { "ToolUsedMiraclePotion", hm_ToolUsedMiraclePotion, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolused.c", 0x829B31, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_potion },
  { "ToolUsedBell", hm_ToolUsedBell, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolused.c", 0x829B51, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_bell },
  { "ToolUsedGrassSeeds", hm_ToolUsedGrassSeeds, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolused.c", 0x829B61, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_grass_seeds },
  { "ToolUsedPaint", hm_ToolUsedPaint, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolused.c", 0x829BCB, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_paint },
  { "ToolUsedMilker", hm_ToolUsedMilker, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolused.c", 0x829CC0, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_milker },
  { "ToolUsedBrush", hm_ToolUsedBrush, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolused.c", 0x829CC8, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_brush },
  { "ToolUsedGoldSickle", hm_ToolUsedGoldSickle, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolused.c", 0x829D42, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_gold_sickle },
  { "ToolUsedGoldHoe", hm_ToolUsedGoldHoe, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolused.c", 0x829E8E, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_gold_hoe },
  { "ToolUsedGoldHammer", hm_ToolUsedGoldHammer, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolused.c", 0x82A005, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_gold_hammer },
  { "ToolUsedGoldAxe", hm_ToolUsedGoldAxe, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolused.c", 0x82A1BF, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_gold_axe },
  { "ToolUsedMagicBeans", hm_ToolUsedMagicBeans, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolused.c", 0x82A377, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_beans },
  { "ToolUsedGemSeed", hm_ToolUsedGemSeed, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolused.c", 0x82A3AA, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_gem },
  { "ToolUsedBlueFeather", hm_ToolUsedBlueFeather, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolused.c", 0x82A3FD, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_feather },
  { "ToolUsedChickenFeed", hm_ToolUsedChickenFeed, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolused.c", 0x82A414, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_chicken_feed },
  { "ToolUsedCowFeed", hm_ToolUsedCowFeed, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolused.c", 0x82A4C2, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_cow_feed },
  { "ToolUsedUNK", hm_ToolUsedUNK, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolused.c", 0x82A570, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_tool_unk },
  { "StartMapAnimation", hm_StartMapAnimation, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/mapedit.c", 0x81A5E1, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_map_animation },
  { "RanchMasteryCalculator", hm_RanchMasteryCalculator,
    HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP | HM_JSR,
    "src/ending.c", 0x83F26D, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_ranch_score },
  { "AddNewChicken", hm_AddNewChicken, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/animals.c", 0x83C807, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_new_animal },
  { "AddNewCow", hm_AddNewCow, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/animals.c", 0x83C8DC, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_new_animal },
  { "PlaceAnimalsOnMap", hm_PlaceAnimalsOnMap, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/animals.c", 0x83C296, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_place_animals },
  { "Event_UNKChickenRelated", hm_Event_UNKChickenRelated,
    HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x849857, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_chicken_slot },
  { "Event_UNKDogRelated", hm_Event_UNKDogRelated,
    HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x84AF06, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_dog_slot },
  { "Event_AudioRelated", hm_Event_AudioRelated, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x8489BB, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd },
  { "Event_TimeRunning", hm_Event_TimeRunning, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x8489E3, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd },
  { "Event_TimeStopped", hm_Event_TimeStopped, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x848A0D, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd },
  { "Event_SetHour", hm_Event_SetHour, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x848A39, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd },
  { "Event_Empty", hm_Event_Empty, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x848A58, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd },
  { "Event_SetPlayerPosition", hm_Event_SetPlayerPosition, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x848A65, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd },
  { "Event_SetTransitionDest", hm_Event_SetTransitionDest, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x848A94, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd },
  { "Event_SetPlayerDirection", hm_Event_SetPlayerDirection, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x848ABF, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd },
  { "Event_UNKChangesGameState", hm_Event_UNKChangesGameState, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x848AE7, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd },
  { "Event_NewCCStructure", hm_Event_NewCCStructure, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x848B08, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd },
  { "Event_UNKset7Aflag", hm_Event_UNKset7Aflag, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x848BAC, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd },
  { "Event_UNKCompareset7Aflag", hm_Event_UNKCompareset7Aflag, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x848BDC, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd },
  { "Event_UNKCCchanges2", hm_Event_UNKCCchanges2, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x848C22, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd },
  { "Event_UNKDisableCCstruct", hm_Event_UNKDisableCCstruct, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x848CE2, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd },
  { "Event_UNKChangesGameState2", hm_Event_UNKChangesGameState2, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x848CFD, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd },
  { "Event_JumptoInstruction", hm_Event_JumptoInstruction, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x848D13, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd },
  { "Event_UNKSetsCC10", hm_Event_UNKSetsCC10, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x848D24, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd },
  { "Event_JumpifFlagSet", hm_Event_JumpifFlagSet, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x848D44, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd_ptr },
  { "Event_JumpifEquals", hm_Event_JumpifEquals, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x848DA5, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd_ptr },
  { "Event_JumpifBetween", hm_Event_JumpifBetween, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x848DFB, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd_ptr },
  { "Event_GetRNG", hm_Event_GetRNG, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x848E68, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd },
  { "Event_JumpIfEqualsRNG", hm_Event_JumpIfEqualsRNG, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x848E90, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd },
  { "Event_UNKSetAnimation", hm_Event_UNKSetAnimation, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x848EC9, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd },
  { "Event_UNKPickupMole", hm_Event_UNKPickupMole, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x84A94C, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd_ptr },
  { "Event_UNKCCchanges6", hm_Event_UNKCCchanges6, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x84923F, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd_ptr },
  { "Event_UNK3", hm_Event_UNK3, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x84B93C, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd_ptr },
  { "Event_JumpifBetweenValueLong", hm_Event_JumpifBetweenValueLong, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x84B79A, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd_ptr },
  { "Event_UNKFishing", hm_Event_UNKFishing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x84AA5D, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd_ptr },
  { "Event_UNKCCchanges13", hm_Event_UNKCCchanges13, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x84B427, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd_ptr },
  { "Event_UNKCCchanges12", hm_Event_UNKCCchanges12, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x84B389, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd_ptr },
  { "Event_UNKCCchanges10", hm_Event_UNKCCchanges10, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x849777, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd_ptr },
  { "Event_UNKTextrelated", hm_Event_UNKTextrelated, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x8491D2, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd_ptr },
  { "Event_JumpifEqualsValueLong", hm_Event_JumpifEqualsValueLong, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x84B6BC, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd_ptr },
  { "Event_JumpifBetweenValue", hm_Event_JumpifBetweenValue, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x84B72D, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd_ptr },
  { "Event_ChangeMoney", hm_Event_ChangeMoney, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x84B602, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd_ptr },
  { "Event_UNKCCchanges4", hm_Event_UNKCCchanges4, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x848F9B, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd_ptr },
  { "Event_UNKCCchanges14", hm_Event_UNKCCchanges14, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x84B5A9, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd_ptr },
  { "Event_JumpifEqualsValue", hm_Event_JumpifEqualsValue, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x84B666, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd_ptr },
  { "Event_SetFlag", hm_Event_SetFlag, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x849306, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd_ptr },
  { "Event_SetValueLong", hm_Event_SetValueLong, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x84B889, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd_ptr },
  { "Event_MapScrolling", hm_Event_MapScrolling, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x849513, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd_ptr },
  { "Event_StoreValue", hm_Event_StoreValue, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x84A90F, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd_ptr },
  { "Event_SetValueSort", hm_Event_SetValueSort, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x84B84C, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd_ptr },
  { "Event_UNKJumpif018F", hm_Event_UNKJumpif018F, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x84919D, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd_ptr },
  { "Event_TransitiontoX", hm_Event_TransitiontoX, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x84B52D, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd_ptr },
  { "Event_ChangeItemonHand", hm_Event_ChangeItemonHand, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x84B4DC, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd_ptr },
  { "Event_SetItemonHand", hm_Event_SetItemonHand, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x84B556, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd_ptr },
  { "Event_UNK2", hm_Event_UNK2, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x84B365, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd_ptr },
  { "Event_TransitiontoHouse", hm_Event_TransitiontoHouse, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x84B503, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd_ptr },
  { "Event_UNKCCchanges16", hm_Event_UNKCCchanges16, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x84BA51, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd_ptr },
  { "Event_UNKSet5C", hm_Event_UNKSet5C, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x84B58F, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd_ptr },
  { "Event_UNKReset5C", hm_Event_UNKReset5C, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x84BB31, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd_ptr },
  { "Event_UNKSet60", hm_Event_UNKSet60, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x84BB4B, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd_ptr },
  { "Event_DropItemAnimation", hm_Event_DropItemAnimation, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x84B57B, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_cmd_ptr },
  { "RemoveProgrammedDMA", hm_RemoveProgrammedDMA, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/dma.c", 0x808AA0, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_dma_remove },
  { "UNK_SetDMA2", hm_UNK_SetDMA2, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/dma.c", 0x8381B7, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_dma_block },
  { "StartTextBox", hm_StartTextBox, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/text.c", 0x83935F, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_text_box },
  { "SUB_83932D", hm_SUB_83932D, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/text.c", 0x83932D, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_text_box },
  { "CloseTextBox", hm_CloseTextBox, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/text.c", 0x8393F9, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_text_box },
  { "DrawTextBoxArrow", hm_DrawTextBoxArrow, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/text.c", 0x8394D7, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_text_box },
  { "BlinkTextBoxArrow", hm_BlinkTextBoxArrow,
    HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/text.c", 0x839495, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_text_box },
  { "TextBoxArrow", hm_TextBoxArrow, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/text.c", 0x839447, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_text_box },
  { "UNK_SetDMA1", hm_UNK_SetDMA1, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/dma.c", 0x838166, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_letter_dma },
  { "FindLetterGraphics", hm_FindLetterGraphics, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/text.c", 0x839862, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_letter },
  { "DrawLetter", hm_DrawLetter, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/text.c", 0x839823, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_letter },
  { "AdvanceLetterCursor", hm_AdvanceLetterCursor,
    HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/text.c", 0x839838, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_letter },
  { "BlinkTextCursor", hm_BlinkTextCursor, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/text.c", 0x83975F, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_letter },
  { "PrintDigit", hm_PrintDigit, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/text.c", 0x8397A6, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_letter },
  { "UNK_Audio17", hm_UNK_Audio17, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU | HM_JSR,
    "src/audio.c", 0x838EE4, 1, 0, 0, 0xFFFF, { { 0 } }, 200, prep_audio },
  { "UNK_Audio7", hm_UNK_Audio7, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU | HM_JSR,
    "src/audio.c", 0x838EC9, 1, 0, 0, 0xFFFF, { { 0 } }, 200, prep_audio },
  { "UNK_Audio8", hm_UNK_Audio8, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU | HM_JSR,
    "src/audio.c", 0x838E8B, 1, 0, 0, 0xFFFF, { { 0 } }, 200, prep_audio },
  { "UNK_Audio15", hm_UNK_Audio15, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU,
    "src/audio.c", 0x838DDF, 1, 0, 0, 0xFFFF, { { 0 } }, 200, prep_audio },
  { "UNK_Audio19", hm_UNK_Audio19, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU,
    "src/audio.c", 0x838332, 1, 0, 0, 0xFFFF, { { 0 } }, 200, prep_audio },
  { "UNK_Audio16", hm_UNK_Audio16, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU | HM_INTERP,
    "src/audio.c", 0x838E7F, 1, 0, 0, 0xFFFF, { { 0 } }, 200, prep_audio },
  { "UNK_Audio2", hm_UNK_Audio2, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU | HM_INTERP,
    "src/audio.c", 0x838E32, 1, 0, 0, 0xFFFF, { { 0 } }, 200, prep_audio },
  { "EditTileonMap", hm_EditTileonMap, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/mapedit.c", 0x82B03A, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_edit_tile },
  { "EditTileonMapandsets0181", hm_EditTileonMapandsets0181, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/mapedit.c", 0x82B049, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_edit_tile },
  { "ToolUsedWateringCan", hm_ToolUsedWateringCan,
    HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU | HM_JSR,
    "src/toolused.c", 0x829CD8, 1, 0, 0, 0xFF, { { 0 } }, 200, prep_watering_can },
  { "ToolUsedSprinkler", hm_ToolUsedSprinkler,
    HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU | HM_JSR,
    "src/toolused.c", 0x82A307, 1, 0, 0, 0xFF, { { 0 } }, 120, prep_sprinkler },
  { "SUB_829260", hm_SUB_829260, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU,
    "src/toolused.c", 0x829260, 0, 0, 0, 0xFFFF, { { 0 } }, 150, prep_any_tool },
  { "SUB_82927D", hm_SUB_82927D, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/tools.c", 0x82927D, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_tool_none },
  { "PrintTextFrame", hm_PrintTextFrame,
    HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU | HM_INTERP,
    "src/text.c", 0x83951C, 1, 0, 0, 0xFFFF, { { 0 } }, 150, prep_printer },
  { "Event_UNKCowRelated", hm_Event_UNKCowRelated,
    HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x849AD4, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_cow_slot },
  { "SaveGameSlot", hm_SaveGameSlot, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/save.c", 0x83B68E, 0, 0, 0, 0xFFFF, { { 0 } }, 300, prep_sram },
  { "NewGameSetup", hm_NewGameSetup, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/save.c", 0x83A9BE, 0, 0, 0, 0xFFFF, { { 0 } }, 300, prep_new_game },
  { "LoadGameSlot", hm_LoadGameSlot, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/save.c", 0x83B2B1, 0, 0, 0, 0xFFFF, { { 0 } }, 300, prep_sram },
  { "LoadGameSlotResume", hm_LoadGameSlotResume, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/save.c", 0x83BA45, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_sram },
  { "CheckSRAMIntegrity", hm_CheckSRAMIntegrity, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/save.c", 0x83BAD4, 0, 0, 0, 0xFFFF, { { 0 } }, 300, prep_sram },
  { "SaveSlotMenuInputs", hm_SaveSlotMenuInputs, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU | HM_INTERP,
    "src/menus.c", 0x84C2FE, 1, 0, 0, 0xFFFF, { { 0 } }, 150, prep_menu },
  { "LoadSlotMenuInputs", hm_LoadSlotMenuInputs, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU | HM_INTERP,
    "src/menus.c", 0x84C380, 1, 0, 0, 0xFFFF, { { 0 } }, 150, prep_menu },
  { "StartMenuInputs", hm_StartMenuInputs, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU | HM_INTERP,
    "src/menus.c", 0x84C428, 1, 0, 0, 0xFFFF, { { 0 } }, 150, prep_menu },
  { "TitleMenuInputs", hm_TitleMenuInputs, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU | HM_INTERP,
    "src/menus.c", 0x84C458, 1, 0, 0, 0xFFFF, { { 0 } }, 150, prep_menu },
  { "ListInputs", hm_ListInputs, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU | HM_INTERP,
    "src/menus.c", 0x84C540, 1, 0, 0, 0xFFFF, { { 0 } }, 150, prep_menu },
  { "ImmobileInputs", hm_ImmobileInputs, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU | HM_INTERP,
    "src/menus.c", 0x84CF05, 1, 0, 0, 0xFFFF, { { 0 } }, 400, prep_menu },
  { "SUB_80A18D", hm_SUB_80A18D, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/scroll.c", 0x80A18D, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_scroll },
  { "SUB_80A308", hm_SUB_80A308, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/scroll.c", 0x80A308, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_scroll },
  { "SUB_80A481", hm_SUB_80A481, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/scroll.c", 0x80A481, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_scroll },
  { "SUB_80A617", hm_SUB_80A617, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/scroll.c", 0x80A617, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_scroll },
  { "UpdateBGOffset", hm_UpdateBGOffset, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/scroll.c", 0x80A0AB, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_scroll },
  { "SUB_80A0E1", hm_SUB_80A0E1, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/scroll.c", 0x80A0E1, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_scroll },
  { "UNK_StaticMapScroling", hm_UNK_StaticMapScroling, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/scroll.c", 0x80A11C, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_scroll },
  { "SUB_80A152", hm_SUB_80A152, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/scroll.c", 0x80A152, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_scroll },
  { "SetTimeofDayPalette", hm_SetTimeofDayPalette, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/palette.c", 0x808FC7, 1, 0, 0, 0x0074, { { 0 } }, 0, prep_palette_any },
  { "TransitionTimeofDayPalettes", hm_TransitionTimeofDayPalettes, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/palette.c", 0x80900C, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_palette_any },
  { "ClearsTimeofDayPaletteIndexandSetsNext", hm_ClearsTimeofDayPaletteIndexandSetsNext, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/palette.c", 0x809157, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_palette_any },
  { "ClearsTimeofDayPalette", hm_ClearsTimeofDayPalette, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/palette.c", 0x809166, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_palette_any },
  { "SUB_80916F", hm_SUB_80916F, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/palette.c", 0x80916F, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_palette_any },
  { "SUB_80919D", hm_SUB_80919D, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/palette.c", 0x80919D, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_palette_any },
  { "LoadFirstHalfPaletteToWRAM", hm_LoadFirstHalfPaletteToWRAM, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/palette.c", 0x8091CF, 0, 0, 0, 0x0074, { { 0 } }, 0, prep_palette_any },
  { "LoadSecondHalfPaletteToWRAM", hm_LoadSecondHalfPaletteToWRAM, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/palette.c", 0x809208, 0, 0, 0, 0x0074, { { 0 } }, 0, prep_palette_any },
  { "CustomiseSpritePalette", hm_CustomiseSpritePalette, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/palette.c", 0x8093A4, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_palette_any },
  { "UNK_CustomisePalette", hm_UNK_CustomisePalette, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/palette.c", 0x809241, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_palette },
  { "ChangePalettebyTime", hm_ChangePalettebyTime, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/palette.c", 0x8092E2, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_palette },
  { "PrepareSpritePalette", hm_PrepareSpritePalette, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/palette.c", 0x809329, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_palette },
  { "SetPaletteToLoad", hm_SetPaletteToLoad, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/palette.c", 0x809501, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_palette_any },
  { "Event_TimeofDayPalette", hm_Event_TimeofDayPalette, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/palette.c", 0x849356, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_palette_any },
  { "CFFFF", hm_CFFFF, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/collision.c", 0x83B0F6, 0, 0, 0, 1, { { 0 } }, 0, prep_collision },
  { "CEEEE", hm_CEEEE, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/collision.c", 0x83AF37, 0, 0, 0, 3, { { 0 } }, 0, prep_collision },
  { "CBBBB", hm_CBBBB, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/collision.c", 0x83AD91, 0, 0, 0, 3, { { 0 } }, 0, prep_collision },
  { "CDDDD", hm_CDDDD, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/collision.c", 0x83AEC3, 0, 0, 0, 3, { { 0 } }, 0, prep_collision },
  { "UNK_ToolUsed", hm_UNK_ToolUsed, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/toolanim.c", 0x8290A8, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_tool_animation },
  { "ToolAnimationNoAction", hm_ToolAnimationNoAction, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolanim.c", 0x8290DD, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_tool_animation },
  { "ToolAnimationSickle", hm_ToolAnimationSickle, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolanim.c", 0x8290DE, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_tool_animation },
  { "ToolAnimationHoe", hm_ToolAnimationHoe, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolanim.c", 0x8290EC, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_tool_animation },
  { "ToolAnimationHammer", hm_ToolAnimationHammer, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolanim.c", 0x8290FA, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_tool_animation },
  { "ToolAnimationAxe", hm_ToolAnimationAxe, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolanim.c", 0x829108, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_tool_animation },
  { "ToolAnimationCornSeeds", hm_ToolAnimationCornSeeds, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolanim.c", 0x829116, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_tool_animation },
  { "ToolAnimationTomatoSeeds", hm_ToolAnimationTomatoSeeds, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolanim.c", 0x829121, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_tool_animation },
  { "ToolAnimationPotatoSeeds", hm_ToolAnimationPotatoSeeds, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolanim.c", 0x82912C, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_tool_animation },
  { "ToolAnimationTurnipSeeds", hm_ToolAnimationTurnipSeeds, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolanim.c", 0x829137, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_tool_animation },
  { "ToolAnimationCowMedicine", hm_ToolAnimationCowMedicine, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolanim.c", 0x829142, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_tool_animation },
  { "ToolAnimationMiraclePotion", hm_ToolAnimationMiraclePotion, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolanim.c", 0x829150, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_tool_animation },
  { "ToolAnimationBell", hm_ToolAnimationBell, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolanim.c", 0x82915E, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_tool_animation },
  { "ToolAnimationGrassSeeds", hm_ToolAnimationGrassSeeds, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolanim.c", 0x829179, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_tool_animation },
  { "ToolAnimationPaint", hm_ToolAnimationPaint, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolanim.c", 0x829184, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_tool_animation },
  { "ToolAnimationMilker", hm_ToolAnimationMilker, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolanim.c", 0x82918D, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_tool_animation },
  { "ToolAnimationBrush", hm_ToolAnimationBrush, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolanim.c", 0x82919B, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_tool_animation },
  { "ToolAnimationWateringCan", hm_ToolAnimationWateringCan, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolanim.c", 0x8291A9, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_tool_animation },
  { "ToolAnimationGoldSickle", hm_ToolAnimationGoldSickle, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolanim.c", 0x8291E8, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_tool_animation },
  { "ToolAnimationGoldHoe", hm_ToolAnimationGoldHoe, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolanim.c", 0x8291F1, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_tool_animation },
  { "ToolAnimationGoldHammer", hm_ToolAnimationGoldHammer, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolanim.c", 0x8291FD, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_tool_animation },
  { "ToolAnimationGoldAxe", hm_ToolAnimationGoldAxe, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolanim.c", 0x829209, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_tool_animation },
  { "ToolAnimationSprinkler", hm_ToolAnimationSprinkler, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolanim.c", 0x829215, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_tool_animation },
  { "ToolAnimationBeans", hm_ToolAnimationBeans, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolanim.c", 0x82921E, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_tool_animation },
  { "ToolAnimationGem", hm_ToolAnimationGem, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolanim.c", 0x82922A, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_tool_animation },
  { "ToolAnimationBlueFeather", hm_ToolAnimationBlueFeather, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolanim.c", 0x829236, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_tool_animation },
  { "ToolAnimationChickenFeed", hm_ToolAnimationChickenFeed, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolanim.c", 0x82923F, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_tool_animation },
  { "ToolAnimationCowFeed", hm_ToolAnimationCowFeed, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolanim.c", 0x82924B, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_tool_animation },
  { "ToolAnimationUNK", hm_ToolAnimationUNK, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/toolanim.c", 0x829257, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_tool_animation },
  { "SUB_82AD0E", hm_SUB_82AD0E, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/interact.c", 0x82AD0E, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_interact },
  { "PlayerMovementInputs", hm_PlayerMovementInputs, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/playerinput.c", 0x84C6B7, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_input },
  { "CODE_84C7D6", hm_CODE_84C7D6, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/playerinput.c", 0x84C7D6, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_input },
  { "NameCursorPharse", hm_NameCursorPharse, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/menus.c", 0x82EBAC, 0, 0, 0, 5, { { 0 } }, 0, prep_inputs },
  { "SUB_82EB57", hm_SUB_82EB57, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/menus.c", 0x82EB57, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_name_cursor },
  { "Unk_OutOfMenu", hm_Unk_OutOfMenu, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/menus.c", 0x82EA60, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_inputs },
  { "NameMenuKeyDown", hm_NameMenuKeyDown, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU | HM_INTERP,
    "src/menus.c", 0x84C204, 0, 0, 0, 0xFFFF, { { 0 } }, 150, prep_inputs },
  { "NameMenuKeyUp", hm_NameMenuKeyUp, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU | HM_INTERP,
    "src/menus.c", 0x84C225, 0, 0, 0, 0xFFFF, { { 0 } }, 150, prep_inputs },
  { "NameMenuKeyLeft", hm_NameMenuKeyLeft, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU | HM_INTERP,
    "src/menus.c", 0x84C246, 0, 0, 0, 0xFFFF, { { 0 } }, 150, prep_inputs },
  { "NameMenuKeyRight", hm_NameMenuKeyRight, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU | HM_INTERP,
    "src/menus.c", 0x84C267, 0, 0, 0, 0xFFFF, { { 0 } }, 150, prep_inputs },
  { "NameMenuKeyB", hm_NameMenuKeyB, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU | HM_INTERP,
    "src/menus.c", 0x84C0AB, 0, 0, 0, 0xFFFF, { { 0 } }, 150, prep_inputs },
  { "NameMenuKeyA", hm_NameMenuKeyA, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU | HM_INTERP,
    "src/menus.c", 0x84C0EE, 0, 0, 0, 0xFFFF, { { 0 } }, 150, prep_inputs },
  { "NameMenuInputs", hm_NameMenuInputs, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU | HM_INTERP,
    "src/menus.c", 0x84C066, 0, 0, 0, 0xFFFF, { { 0 } }, 150, prep_inputs },
  { "CODE_84C2D8", hm_CODE_84C2D8, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU | HM_INTERP,
    "src/menus.c", 0x84C2D8, 1, 0, 0, 0xFFFF, { { 0 } }, 150, prep_inputs },
  { "CODE_84C2B4", hm_CODE_84C2B4, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU | HM_INTERP,
    "src/menus.c", 0x84C2B4, 1, 0, 0, 0xFFFF, { { 0 } }, 150, prep_inputs },
  { "MenuInputs", hm_MenuInputs, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU | HM_INTERP,
    "src/menus.c", 0x84C288, 1, 0, 0, 0xFFFF, { { 0 } }, 150, prep_inputs },
  { "CODE_84CED2", hm_CODE_84CED2, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/menus.c", 0x84CED2, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_inputs },
  { "InputTypeSelector", hm_InputTypeSelector, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU | HM_INTERP,
    "src/menus.c", 0x84C034, 1, 0, 0, 0xFFFF, { { 0 } }, 150, prep_inputs },
  { "UNK_PartialMap", hm_UNK_PartialMap, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/mapedit.c", 0x82AF00, 1, 0, 0, 0xFFFF, { { 0 } }, 20, prep_partial_map },
  { "Event_UNKChickenRelated2", hm_Event_UNKChickenRelated2, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x84BBDA, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_50s },
  { "Event_Cmd51", hm_Event_Cmd51, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x84BC9C, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_50s },
  { "Event_Cmd52", hm_Event_Cmd52, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x84BD44, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_50s },
  { "Event_Cmd53", hm_Event_Cmd53, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x84BE0D, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_50s },
  { "Event_Cmd54", hm_Event_Cmd54, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x84BED6, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_50s },
  { "Event_Cmd55", hm_Event_Cmd55, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x84BF20, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_55 },
  { "Event_Cmd56", hm_Event_Cmd56, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x84BF81, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_tool },
  { "Event_Cmd57", hm_Event_Cmd57, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x84BFA0, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_50s },
  { "Event_Cmd58", hm_Event_Cmd58, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x84BFBF, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_58 },
  { "Event_Cmd59", hm_Event_Cmd59, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x84C000, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_50s },
  { "Event_UNKPlayerPos", hm_Event_UNKPlayerPos, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x84AB27, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_pos },
  { "DivisionSigned", hm_DivisionSigned, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/math.c", 0x8380D0, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_division_signed },
  { "DecompressTileMap", hm_DecompressTileMap, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/decompress.c", 0x8381F8, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_decompress },
  { "SUB_809EBC", hm_SUB_809EBC, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/scroll.c", 0x809EBC, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_camera },
  { "SUB_809F61", hm_SUB_809F61, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/scroll.c", 0x809F61, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_camera },
  { "Event_ResetFlag", hm_Event_ResetFlag, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x8493C9, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_55 },
  { "Event_EditTileonMap", hm_Event_EditTileonMap, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x84BA10, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_tile },
  { "Event_StartTextBox", hm_Event_StartTextBox, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x848FEF, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_50s },
  { "Event_StartTextBoxCopy", hm_Event_StartTextBoxCopy, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x849034, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_50s },
  { "Event_UNKTools", hm_Event_UNKTools, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/eventcmds.c", 0x84BB65, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_event_shed },
  { "UNK_SetPointer42", hm_UNK_SetPointer42, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/coloranim.c", 0x808E48, 1, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "UNK_BigLoop", hm_UNK_BigLoop, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/coloranim.c", 0x808E69, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "Zeroes42Pointers", hm_Zeroes42Pointers, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/coloranim.c", 0x808FAB, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "ZeroesPartial42Pointers", hm_ZeroesPartial42Pointers, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/coloranim.c", 0x808F92, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "ZeroesOne42Pointer", hm_ZeroesOne42Pointer, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/coloranim.c", 0x808F82, 0, 0, 0, 0x000F, { { 0 } }, 0, prep_colour_anim },
  { "CallIndexedSubrutines", hm_CallIndexedSubrutines, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/coloranim.c", 0x809553, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80C36C", hm_SUB_80C36C, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/coloranim.c", 0x80C36C, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80C3D9", hm_SUB_80C3D9, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80C3D9, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80C446", hm_SUB_80C446, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80C446, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80C4B3", hm_SUB_80C4B3, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80C4B3, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80C520", hm_SUB_80C520, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80C520, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80C58D", hm_SUB_80C58D, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80C58D, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80C5FA", hm_SUB_80C5FA, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80C5FA, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80C667", hm_SUB_80C667, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80C667, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80C6D4", hm_SUB_80C6D4, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80C6D4, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80C792", hm_SUB_80C792, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80C792, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80C7FF", hm_SUB_80C7FF, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80C7FF, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80C86C", hm_SUB_80C86C, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80C86C, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80C945", hm_SUB_80C945, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80C945, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80C9B2", hm_SUB_80C9B2, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80C9B2, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80CA1F", hm_SUB_80CA1F, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80CA1F, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80CA8C", hm_SUB_80CA8C, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80CA8C, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80CADE", hm_SUB_80CADE, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80CADE, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80CB30", hm_SUB_80CB30, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80CB30, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80CB82", hm_SUB_80CB82, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80CB82, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80CBD4", hm_SUB_80CBD4, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80CBD4, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80CC92", hm_SUB_80CC92, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80CC92, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80CCE4", hm_SUB_80CCE4, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80CCE4, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80CD36", hm_SUB_80CD36, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80CD36, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80CD88", hm_SUB_80CD88, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80CD88, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80CE46", hm_SUB_80CE46, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80CE46, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80CE98", hm_SUB_80CE98, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80CE98, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80CEEA", hm_SUB_80CEEA, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80CEEA, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80CF3C", hm_SUB_80CF3C, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80CF3C, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80CFFA", hm_SUB_80CFFA, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80CFFA, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80D067", hm_SUB_80D067, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80D067, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80D125", hm_SUB_80D125, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80D125, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80D1E3", hm_SUB_80D1E3, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80D1E3, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80D2A1", hm_SUB_80D2A1, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80D2A1, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80D344", hm_SUB_80D344, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80D344, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80D402", hm_SUB_80D402, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80D402, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80D4C0", hm_SUB_80D4C0, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80D4C0, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80D563", hm_SUB_80D563, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80D563, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80D606", hm_SUB_80D606, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80D606, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80D6FA", hm_SUB_80D6FA, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80D6FA, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80D7EE", hm_SUB_80D7EE, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80D7EE, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80D891", hm_SUB_80D891, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80D891, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80D934", hm_SUB_80D934, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80D934, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80D96B", hm_SUB_80D96B, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80D96B, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80D9A2", hm_SUB_80D9A2, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80D9A2, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80D9D9", hm_SUB_80D9D9, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80D9D9, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80DA10", hm_SUB_80DA10, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80DA10, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80DA47", hm_SUB_80DA47, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80DA47, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80DAB4", hm_SUB_80DAB4, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80DAB4, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80DB06", hm_SUB_80DB06, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80DB06, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80DC30", hm_SUB_80DC30, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80DC30, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "SUB_80DD09", hm_SUB_80DD09, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/coloranim.c", 0x80DD09, 0, 1, 0, 0xFFFF, { { 0 } }, 0, prep_colour_anim },
  { "ReadJoypad", hm_ReadJoypad, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/joypad.c", 0x80872B, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_joypad },
  { "UnusedBarnCoopCheck", hm_UnusedBarnCoopCheck, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/animals.c", 0x83C9BA, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_animals },
  { "UnusedBarnCheck", hm_UnusedBarnCheck, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/animals.c", 0x83C9E5, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_animals },
  { "ToolUsedSound1", hm_ToolUsedSound1, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU,
    "src/toolsound.c", 0x828FB1, 1, 0, 0, 0xFFFF, { { 0 } }, 200, prep_tool_sound },
  { "CopyCurrentMaptoFarmMap", hm_CopyCurrentMaptoFarmMap, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/farm.c", 0x82A682, 1, 0, 0, 0xFFFF, { { 0 } }, 100, prep_map_copy },
  { "LoadMap", hm_LoadMap, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/farm.c", 0x82A5FB, 1, 1, 0, 0xFFFF, { { 0 } }, 0, prep_load_map },
  { "LoadDefaultFarmMap", hm_LoadDefaultFarmMap, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/farm.c", 0x82A65A, 0, 0, 0, 0xFFFF, { { 0 } }, 100, prep_map_copy },
  { "ClearsofAllCCStructs", hm_ClearsofAllCCStructs, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/events.c", 0x848000, 0, 0, 0, 0xFFFF, { { 0 } }, 200, prep_map_copy },
  { "MultiplicationUnsigned", hm_MultiplicationUnsigned, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_IO,
    "src/math.c", 0x838067, 1, 0, 0, 0xFFFF, { B8(0x011A, 0, 255), B8(0x011B, 0, 255) }, 0, NULL },
  { "SUB_81A7CE", hm_SUB_81A7CE, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/maptiles.c", 0x81A7CE, 0, 0, 0, 0xFFFF, { { 0 } }, 200, prep_stamp },
  { "SUB_82B0A7", hm_SUB_82B0A7, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/maptiles.c", 0x82B0A7, 0, 0, 0, 0xFFFF, { { 0 } }, 200, prep_stamp_typed },
  { "SUB_81B2BB", hm_SUB_81B2BB, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/maptiles.c", 0x81B2BB, 0, 0, 0, 0xFFFF, { { 0 } }, 300, prep_shop_item },
  { "UNK_ExecuteFromPointers", hm_UNK_ExecuteFromPointers, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/maptiles.c", 0x81A9E5, 0, 0, 0, 0xFFFF, { { 0 } }, 300, prep_map_tiles },
  { "ReplaceTilesFarm", hm_ReplaceTilesFarm, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/maptiles.c", 0x81AAB6, 0, 0, 0, 0xFFFF, { { 0 } }, 200, prep_map_tiles },
  { "ReplaceTilesMoutain", hm_ReplaceTilesMoutain, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/maptiles.c", 0x81AC80, 0, 0, 0, 0xFFFF, { { 0 } }, 200, prep_map_tiles },
  { "ReplaceTilesHouse1", hm_ReplaceTilesHouse1, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/maptiles.c", 0x81ACCC, 0, 0, 0, 0xFFFF, { { 0 } }, 200, prep_map_tiles },
  { "ReplaceTilesHouse2", hm_ReplaceTilesHouse2, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/maptiles.c", 0x81AD23, 0, 0, 0, 0xFFFF, { { 0 } }, 200, prep_map_tiles },
  { "ReplaceTilesHouse3", hm_ReplaceTilesHouse3, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/maptiles.c", 0x81AD7A, 0, 0, 0, 0xFFFF, { { 0 } }, 200, prep_map_tiles },
  { "ReplaceTilesFlowerShop", hm_ReplaceTilesFlowerShop, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/maptiles.c", 0x81ADD5, 0, 0, 0, 0xFFFF, { { 0 } }, 200, prep_map_tiles },
  { "ReplaceTilesGeneralStore", hm_ReplaceTilesGeneralStore, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/maptiles.c", 0x81AE6F, 0, 0, 0, 0xFFFF, { { 0 } }, 200, prep_map_tiles },
  { "ReplaceTilesAnimalShop", hm_ReplaceTilesAnimalShop, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/maptiles.c", 0x81AFC0, 0, 0, 0, 0xFFFF, { { 0 } }, 200, prep_map_tiles },
  { "ReplaceTilesToolshed", hm_ReplaceTilesToolshed, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/maptiles.c", 0x81B064, 0, 0, 0, 0xFFFF, { { 0 } }, 100, prep_map_tiles },
  { "ReplaceTilesBarn", hm_ReplaceTilesBarn, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/maptiles.c", 0x81B0A8, 0, 0, 0, 0xFFFF, { { 0 } }, 200, prep_map_tiles },
  { "ReplaceTilesCoop", hm_ReplaceTilesCoop, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/maptiles.c", 0x81B121, 0, 0, 0, 0xFFFF, { { 0 } }, 200, prep_map_tiles },
  { "ReplaceTilesMountainCave", hm_ReplaceTilesMountainCave, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/maptiles.c", 0x81B1CF, 0, 0, 0, 0xFFFF, { { 0 } }, 200, prep_map_tiles },
  { "ReplaceTilesElfTunnel", hm_ReplaceTilesElfTunnel, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/maptiles.c", 0x81B21E, 0, 0, 0, 0xFFFF, { { 0 } }, 200, prep_map_tiles },
  { "ReplaceTilesMap37", hm_ReplaceTilesMap37, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/maptiles.c", 0x81B26D, 0, 0, 0, 0xFFFF, { { 0 } }, 200, prep_map_tiles },
  { "ReplaceTilesTown", hm_ReplaceTilesNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/maptiles.c", 0x81AC71, 0, 0, 0, 0xFFFF, { { 0 } }, 50, prep_map_tiles },
  { "ReplaceTilesFlowerFestival", hm_ReplaceTilesNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/maptiles.c", 0x81AC78, 0, 0, 0, 0xFFFF, { { 0 } }, 50, prep_map_tiles },
  { "ReplaceTilesHarvestFestival", hm_ReplaceTilesNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/maptiles.c", 0x81AC79, 0, 0, 0, 0xFFFF, { { 0 } }, 50, prep_map_tiles },
  { "DATA8_00AC7A", hm_ReplaceTilesNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/maptiles.c", 0x81AC7A, 0, 0, 0, 0xFFFF, { { 0 } }, 50, prep_map_tiles },
  { "DATA8_00AC7B", hm_ReplaceTilesNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/maptiles.c", 0x81AC7B, 0, 0, 0, 0xFFFF, { { 0 } }, 50, prep_map_tiles },
  { "ReplaceTilesForkSpring", hm_ReplaceTilesNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/maptiles.c", 0x81AC7C, 0, 0, 0, 0xFFFF, { { 0 } }, 50, prep_map_tiles },
  { "ReplaceTilesForkSummer", hm_ReplaceTilesNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/maptiles.c", 0x81AC7D, 0, 0, 0, 0xFFFF, { { 0 } }, 50, prep_map_tiles },
  { "ReplaceTilesForkFall", hm_ReplaceTilesNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/maptiles.c", 0x81AC7E, 0, 0, 0, 0xFFFF, { { 0 } }, 50, prep_map_tiles },
  { "ReplaceTilesForkWinter", hm_ReplaceTilesNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/maptiles.c", 0x81AC7F, 0, 0, 0, 0xFFFF, { { 0 } }, 50, prep_map_tiles },
  { "DATA8_00ACCB", hm_ReplaceTilesNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/maptiles.c", 0x81ACCB, 0, 0, 0, 0xFFFF, { { 0 } }, 50, prep_map_tiles },
  { "ReplaceTilesMayorHouse", hm_ReplaceTilesNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/maptiles.c", 0x81ADD1, 0, 0, 0, 0xFFFF, { { 0 } }, 50, prep_map_tiles },
  { "ReplaceTilesMayorHouseHall", hm_ReplaceTilesNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/maptiles.c", 0x81ADD2, 0, 0, 0, 0xFFFF, { { 0 } }, 50, prep_map_tiles },
  { "ReplaceTilesMariasRoom", hm_ReplaceTilesNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/maptiles.c", 0x81ADD3, 0, 0, 0, 0xFFFF, { { 0 } }, 50, prep_map_tiles },
  { "ReplaceTilesChurch", hm_ReplaceTilesNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/maptiles.c", 0x81ADD4, 0, 0, 0, 0xFFFF, { { 0 } }, 50, prep_map_tiles },
  { "ReplaceTilesFlowerShopRooms", hm_ReplaceTilesNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/maptiles.c", 0x81AE6A, 0, 0, 0, 0xFFFF, { { 0 } }, 50, prep_map_tiles },
  { "ReplaceTilesBar", hm_ReplaceTilesNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/maptiles.c", 0x81AE6B, 0, 0, 0, 0xFFFF, { { 0 } }, 50, prep_map_tiles },
  { "ReplaceTilesBarRooms", hm_ReplaceTilesNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/maptiles.c", 0x81AE6C, 0, 0, 0, 0xFFFF, { { 0 } }, 50, prep_map_tiles },
  { "ReplaceTilesRestaurant", hm_ReplaceTilesNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/maptiles.c", 0x81AE6D, 0, 0, 0, 0xFFFF, { { 0 } }, 50, prep_map_tiles },
  { "ReplaceTilesRestaurantRooms", hm_ReplaceTilesNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/maptiles.c", 0x81AE6E, 0, 0, 0, 0xFFFF, { { 0 } }, 50, prep_map_tiles },
  { "ReplaceTilesGeneralStoreRooms", hm_ReplaceTilesNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/maptiles.c", 0x81AFBF, 0, 0, 0, 0xFFFF, { { 0 } }, 50, prep_map_tiles },
  { "ReplaceTilesWitchHouse", hm_ReplaceTilesNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/maptiles.c", 0x81B063, 0, 0, 0, 0xFFFF, { { 0 } }, 50, prep_map_tiles },
  { "Unk_Audio23", hm_Unk_Audio23, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_IO,
    "src/math.c", 0x838000, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_multiply16 },
  { "UNK_Audio6", hm_UNK_Audio6, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU,
    "src/audio.c", 0x838D8B, 1, 0, 0, 0xFFFF, { { 0 } }, 100, prep_music },
  { "UNK_Audio13", hm_UNK_Audio13, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU | HM_INTERP,
    "src/audio.c", 0x838CF3, 1, 0, 0, 0xFFFF, { { 0 } }, 100, prep_music },
  { "UNK_Audio14", hm_UNK_Audio14, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU,
    "src/audio.c", 0x838D39, 1, 0, 0, 0xFFFF, { { 0 } }, 100, prep_music },
  { "UNK_Audio10", hm_UNK_Audio10, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU,
    "src/audio.c", 0x8389C7, 1, 0, 0, 0xFFFF, { { 0 } }, 100, prep_music },
  { "UNK_Audio21", hm_UNK_Audio21, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU,
    "src/audio.c", 0x83841F, 1, 0, 0, 0xFFFF, { { 0 } }, 100, prep_music },
  { "UNK_Audio22", hm_UNK_Audio22, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU,
    "src/audio.c", 0x838380, 1, 0, 0, 0xFFFF, { { 0 } }, 100, prep_music },
  { "UNK_Audio5", hm_UNK_Audio5, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/audio.c", 0x8095DE, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_map_music },
  { "UNK_Audio25", hm_UNK_Audio25, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU,
    "src/audio.c", 0x838401, 1, 0, 0, 0xFFFF, { { 0 } }, 100, prep_music },
  { "UNK_Audio12", hm_UNK_Audio12, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU,
    "src/audio.c", 0x838AFF, 1, 0, 0, 0xFFFF, { { 0 } }, 40, prep_upload },
  { "UNK_Audio11", hm_UNK_Audio11, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU,
    "src/audio.c", 0x838A26, 1, 0, 0, 0xFFFF, { { 0 } }, 20, prep_upload },
  { "UNK_Audio9", hm_UNK_Audio9, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU,
    "src/audio.c", 0x83878C, 1, 0, 0, 0xFFFF, { { 0 } }, 10, prep_upload },
  { "UNK_Audio3", hm_UNK_Audio3, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU | HM_INTERP,
    "src/audio.c", 0x838598, 1, 0, 0, 0xFFFF, { { 0 } }, 10, prep_upload },
  /* the routines that are only a return (other routines jump or branch
   * to them, or nothing calls them) */
  { "StartProgramedDMA", hm_StartProgramedDMA, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_IO,
    "src/video.c", 0x808AF0, 0, 0, 0, 0xFFFF, { { 0 } }, 300, prep_prog_dma },
  { "InitialzieScreenStatusVar", hm_InitialzieScreenStatusVar, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_IO | HM_INTERP,
    "src/video.c", 0x808C4C, 1, 0, 0, 0xFFFF, { { 0 } }, 300, prep_screen },
  { "ManageGraphicPresets", hm_ManageGraphicPresets, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_IO,
    "src/video.c", 0x808C59, 1, 0, 0, 0xFFFF, { { 0 } }, 300, prep_screen },
  { "ForceBlank", hm_ForceBlank, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_IO,
    "src/video.c", 0x808E0F, 1, 0, 0, 0xFFFF, { { 0 } }, 300, prep_screen },
  { "ResetForceBlank", hm_ResetForceBlank, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_IO | HM_INTERP,
    "src/video.c", 0x808E1E, 1, 0, 0, 0xFFFF, { { 0 } }, 300, prep_screen },
  { "SetsBrightness", hm_SetsBrightness, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_IO,
    "src/video.c", 0x808E2D, 1, 1, 0, 0xFFFF, { { 0 } }, 300, prep_screen },
  { "ZeroesVRAM", hm_ZeroesVRAM, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_IO | HM_INTERP,
    "src/video.c", 0x808846, 1, 0, 0, 0xFFFF, { { 0 } }, 100, prep_clear },
  { "ZeroesOAM", hm_ZeroesOAM, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_IO | HM_INTERP,
    "src/video.c", 0x808887, 1, 0, 0, 0xFFFF, { { 0 } }, 100, prep_clear },
  { "UNUSED1", hm_UNUSED1, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_IO | HM_INTERP,
    "src/video.c", 0x8088C3, 1, 0, 0, 0xFFFF, { { 0 } }, 100, prep_clear },
  { "ZeroesCGRAM", hm_ZeroesCGRAM, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_IO | HM_INTERP,
    "src/video.c", 0x808980, 1, 0, 0, 0xFFFF, { { 0 } }, 100, prep_clear },
  { "ZeroesPartialVRAM", hm_ZeroesPartialVRAM, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_IO,
    "src/video.c", 0x8089BC, 0, 0, 0, 0xFFFF, { { 0 } }, 100, prep_clear },
  { "StartLastPreparedDMA", hm_StartLastPreparedDMA, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_IO,
    "src/video.c", 0x808AB2, 1, 0, 0, 0xFFFF, { { 0 } }, 300, prep_last_dma },
  { "LoadsFromVRAMwithOffset", hm_LoadsFromVRAMwithOffset, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_IO | HM_JSR,
    "src/video.c", 0x80AA38, 0, 0, 0, 0xFFFF, { { 0 } }, 300, prep_vram_load },
  { "CopiesWRAMtoOBJVGRAM", hm_CopiesWRAMtoOBJVGRAM, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_IO | HM_INTERP,
    "src/video.c", 0x858E57, 1, 0, 0, 0xFFFF, { { 0 } }, 200, prep_piece_copies },
  { "UpdateGraphics", hm_UpdateGraphics, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_IO | HM_INTERP | HM_JSR,
    "src/video.c", 0x8086B1, 1, 0, 0, 0xFFFF, { { 0 } }, 200, prep_update_graphics },
  { "AutoMapScrolling", hm_AutoMapScrolling, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP, "src/scroll.c",
    0x8095B3, 1, 0, 0, 0xFFFF,
    { B8(0x0880, 0, 3), W16(0x087C, 0, 0xFFFF), W16(0x087E, 0, 0xFFFF), W16(0xF5, 0, 0xFFFF), W16(0xF7, 0, 0xFFFF) } },
  { "LoadDialogueBox", hm_LoadDialogueBox, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_IO,
    "src/video.c", 0x8392BB, 0, 0, 0, 0xFFFF, { { 0 } }, 30, prep_vram_fixed },
  { "SUB_82E5E7", hm_SUB_82E5E7, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_IO | HM_INTERP | HM_JSR,
    "src/video.c", 0x82E5E7, 0, 0, 0, 0xFFFF, { { 0 } }, 300, prep_title_block },
  { "SUB_82EA80", hm_SUB_82EA80, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_IO | HM_INTERP,
    "src/video.c", 0x82EA80, 0, 0, 0, 0xFFFF, { { 0 } }, 300, prep_name_letters },
  { "CODE_81A94A", hm_CODE_81A94A, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_IO | HM_INTERP,
    "src/items.c", 0x81A94A, 1, 0, 0, 0xFFFF, { { 0 } }, 200, prep_shelf },
  { "SUB_82EAB4", hm_SUB_82EAB4, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_IO | HM_JSR | HM_INTERP,
    "src/video.c", 0x82EAB4, 0, 0, 0, 0xFFFF, { { 0 } }, 50, prep_vram_fixed },
  { "SUB_82E742", hm_SUB_82E742, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_IO | HM_JSR | HM_INTERP,
    "src/video.c", 0x82E742, 0, 0, 0, 0xFFFF, { { 0 } }, 30, prep_vram_fixed },
  { "NMI_Interrupt", hm_NMI_Interrupt, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_IO | HM_INTERP | HM_RTI,
    "src/nmi.c", 0x80867B, 1, 0, 0, 0xFFFF, { { 0 } }, 300, prep_update_graphics },
  { "COP_Interrupt", hm_COP_Interrupt, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_IO | HM_INTERP | HM_RTI,
    "src/nmi.c", 0x808699, 1, 0, 0, 0xFFFF, { { 0 } }, 300, prep_irq },
  { "WaitForNMI", hm_WaitForNMI, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_IO | HM_WAIT,
    "src/nmi.c", 0x808645, 1, 0, 0, 0xFFFF, { { 0 } }, 300, prep_wait_frame },
  { "WaitForNMIATimes", hm_WaitForNMIATimes, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_IO | HM_WAIT,
    "src/nmi.c", 0x80865D, 0, 0, 1, 3, { { 0 } }, 200, prep_wait_times },
  { "ScreenFadein", hm_ScreenFadein, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_IO | HM_WAIT,
    "src/nmi.c", 0x8087CE, 0, 0, 0, 0xFFFF, { { 0 } }, 100, prep_fade_in },
  { "ScreenFadeout", hm_ScreenFadeout, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_IO | HM_WAIT,
    "src/nmi.c", 0x80880A, 0, 0, 0, 0xFFFF, { { 0 } }, 100, prep_fade_out },
  { "UNK_Audio18", hm_UNK_Audio18, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU | HM_IO | HM_WAIT,
    "src/audio.c", 0x83833E, 1, 0, 0, 0xFFFF, { { 0 } }, 40, prep_sound_wait },
  { "UNK_Audio20", hm_UNK_Audio20, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU | HM_IO | HM_WAIT,
    "src/audio.c", 0x8383A4, 1, 0, 0, 0xFFFF, { { 0 } }, 30, prep_song_wait },
  { "UNK_Audio24", hm_UNK_Audio24, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU | HM_IO | HM_WAIT,
    "src/audio.c", 0x8382FE, 1, 0, 0, 0xFFFF, { { 0 } }, 30, prep_effect_wait },
  { "UNK_Audio26", hm_UNK_Audio26, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU | HM_IO | HM_WAIT,
    "src/audio.c", 0x8382C6, 1, 0, 0, 0xFFFF, { { 0 } }, 10, prep_new_song_wait },
  { "ToolUsedSound2", hm_ToolUsedSound2, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU | HM_IO | HM_WAIT,
    "src/toolsound.c", 0x828FF3, 1, 0, 0, 0xFFFF, { { 0 } }, 40, prep_tool_noise_wait },
  { "Event_ScreenFadein", hm_Event_ScreenFadein, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_IO | HM_WAIT,
    "src/eventcmds.c", 0x848B83, 0, 0, 0, 0xFFFF, { { 0 } }, 30, prep_event_fade },
  { "Event_ScreenFadeout", hm_Event_ScreenFadeout, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_IO | HM_WAIT,
    "src/eventcmds.c", 0x848CB9, 0, 0, 0, 0xFFFF, { { 0 } }, 30, prep_event_fade },
  { "Event_AudioRelated3", hm_Event_AudioRelated3, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP | HM_APU | HM_IO | HM_WAIT,
    "src/eventcmds.c", 0x8495E8, 0, 0, 0, 0xFFFF, { { 0 } }, 30, prep_event_sound_wait },
  { "CODE_81D670", hm_CODE_81D670, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP | HM_APU | HM_IO | HM_WAIT,
    "src/interact.c", 0x81D670, 0, 0, 0, 0xFFFF, { { 0 } }, 40, prep_action_sound },
  { "EventInstructionInterpreter", hm_EventInstructionInterpreter, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_APU | HM_IO | HM_WAIT,
    "src/events.c", 0x848249, 0, 0, 0, 0xFFFF, { { 0 } }, 60, prep_one_command },
  { "IterateCCStructures", hm_IterateCCStructures, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU | HM_IO | HM_WAIT,
    "src/events.c", 0x84816F, 0, 0, 0, 0xFFFF, { { 0 } }, 30, prep_scripts },
  { "BAAAA", hm_BAAAA, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP | HM_APU | HM_IO | HM_WAIT,
    "src/items.c", 0x81A383, 1, 0, 0, 0xFFFF, { { 0 } }, 30, prep_item_records },
  { "SUB_81809A", hm_SUB_81809A, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP | HM_APU | HM_IO | HM_WAIT,
    "src/items.c", 0x81809A, 1, 0, 0, 0xFFFF, { { 0 } }, 40, prep_items_dispatch },
  { "CODE_8187C3", hm_CODE_8187C3, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP | HM_APU | HM_IO | HM_WAIT,
    "src/items.c", 0x8187C3, 1, 0, 0, 0xFF, { { 0 } }, 30, prep_tool_item },
  { "ItemObject_2A", hm_ItemObject_2A, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP | HM_APU | HM_IO | HM_WAIT,
    "src/items.c", 0x8183C9, 1, 0, 0, 0xFF, { { 0 } }, 6, prep_tool_item },
  { "ItemObject_2B", hm_ItemObject_2B, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP | HM_APU | HM_IO | HM_WAIT,
    "src/items.c", 0x8183D1, 1, 0, 0, 0xFF, { { 0 } }, 6, prep_tool_item },
  { "ItemObject_2C", hm_ItemObject_2C, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP | HM_APU | HM_IO | HM_WAIT,
    "src/items.c", 0x8183D9, 1, 0, 0, 0xFF, { { 0 } }, 6, prep_tool_item },
  { "ItemObject_2D", hm_ItemObject_2D, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP | HM_APU | HM_IO | HM_WAIT,
    "src/items.c", 0x8183E1, 1, 0, 0, 0xFF, { { 0 } }, 6, prep_tool_item },
  { "ItemObject_2E", hm_ItemObject_2E, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP | HM_APU | HM_IO | HM_WAIT,
    "src/items.c", 0x8183E9, 1, 0, 0, 0xFF, { { 0 } }, 6, prep_tool_item },
  { "ItemObject_2F", hm_ItemObject_2F, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP | HM_APU | HM_IO | HM_WAIT,
    "src/items.c", 0x8183F1, 1, 0, 0, 0xFF, { { 0 } }, 6, prep_tool_item },
  { "ItemObject_30", hm_ItemObject_30, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP | HM_APU | HM_IO | HM_WAIT,
    "src/items.c", 0x8183F9, 1, 0, 0, 0xFF, { { 0 } }, 6, prep_tool_item },
  { "ItemObject_31", hm_ItemObject_31, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP | HM_APU | HM_IO | HM_WAIT,
    "src/items.c", 0x818401, 1, 0, 0, 0xFF, { { 0 } }, 6, prep_tool_item },
  { "ItemObject_32", hm_ItemObject_32, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP | HM_APU | HM_IO | HM_WAIT,
    "src/items.c", 0x818409, 1, 0, 0, 0xFF, { { 0 } }, 6, prep_tool_item },
  { "ItemObject_33", hm_ItemObject_33, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP | HM_APU | HM_IO | HM_WAIT,
    "src/items.c", 0x818411, 1, 0, 0, 0xFF, { { 0 } }, 6, prep_tool_item },
  { "ItemObject_34", hm_ItemObject_34, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP | HM_APU | HM_IO | HM_WAIT,
    "src/items.c", 0x818419, 1, 0, 0, 0xFF, { { 0 } }, 6, prep_tool_item },
  { "ItemObject_35", hm_ItemObject_35, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP | HM_APU | HM_IO | HM_WAIT,
    "src/items.c", 0x818421, 1, 0, 0, 0xFF, { { 0 } }, 6, prep_tool_item },
  { "ItemObject_36", hm_ItemObject_36, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP | HM_APU | HM_IO | HM_WAIT,
    "src/items.c", 0x818429, 1, 0, 0, 0xFF, { { 0 } }, 6, prep_tool_item },
  { "ItemObject_37", hm_ItemObject_37, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP | HM_APU | HM_IO | HM_WAIT,
    "src/items.c", 0x818431, 1, 0, 0, 0xFF, { { 0 } }, 6, prep_tool_item },
  { "ItemObject_38", hm_ItemObject_38, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP | HM_APU | HM_IO | HM_WAIT,
    "src/items.c", 0x818439, 1, 0, 0, 0xFF, { { 0 } }, 6, prep_tool_item },
  { "ItemObject_39", hm_ItemObject_39, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP | HM_APU | HM_IO | HM_WAIT,
    "src/items.c", 0x818441, 1, 0, 0, 0xFF, { { 0 } }, 6, prep_tool_item },
  { "ItemObject_3A", hm_ItemObject_3A, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP | HM_APU | HM_IO | HM_WAIT,
    "src/items.c", 0x818449, 1, 0, 0, 0xFF, { { 0 } }, 6, prep_tool_item },
  { "ItemObject_3B", hm_ItemObject_3B, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP | HM_APU | HM_IO | HM_WAIT,
    "src/items.c", 0x818451, 1, 0, 0, 0xFF, { { 0 } }, 6, prep_tool_item },
  { "ItemObject_3C", hm_ItemObject_3C, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP | HM_APU | HM_IO | HM_WAIT,
    "src/items.c", 0x818459, 1, 0, 0, 0xFF, { { 0 } }, 6, prep_tool_item },
  { "ItemObject_3D", hm_ItemObject_3D, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP | HM_APU | HM_IO | HM_WAIT,
    "src/items.c", 0x818461, 1, 0, 0, 0xFF, { { 0 } }, 6, prep_tool_item },
  { "ItemObject_3E", hm_ItemObject_3E, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP | HM_APU | HM_IO | HM_WAIT,
    "src/items.c", 0x818469, 1, 0, 0, 0xFF, { { 0 } }, 6, prep_tool_item },
  { "ItemObject_3F", hm_ItemObject_3F, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP | HM_APU | HM_IO | HM_WAIT,
    "src/items.c", 0x818471, 1, 0, 0, 0xFF, { { 0 } }, 6, prep_tool_item },
  { "ItemObject_40", hm_ItemObject_40, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP | HM_APU | HM_IO | HM_WAIT,
    "src/items.c", 0x818479, 1, 0, 0, 0xFF, { { 0 } }, 6, prep_tool_item },
  { "ItemObject_41", hm_ItemObject_41, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP | HM_APU | HM_IO | HM_WAIT,
    "src/items.c", 0x818481, 1, 0, 0, 0xFF, { { 0 } }, 6, prep_tool_item },
  { "ItemObject_42", hm_ItemObject_42, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP | HM_APU | HM_IO | HM_WAIT,
    "src/items.c", 0x818489, 1, 0, 0, 0xFF, { { 0 } }, 6, prep_tool_item },
  { "ItemObject_43", hm_ItemObject_43, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP | HM_APU | HM_IO | HM_WAIT,
    "src/items.c", 0x818491, 1, 0, 0, 0xFF, { { 0 } }, 6, prep_tool_item },
  { "BackgroundsManager", hm_BackgroundsManager, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_IO | HM_INTERP,
    "src/video.c", 0x80A7C6, 1, 0, 0, 0xFFFF, { { 0 } }, 100, prep_backgrounds },
  { "CODE_82E405", hm_CODE_82E405, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_IO | HM_JSR | HM_INTERP,
    "src/video.c", 0x82E405, 0, 0, 0, 0xFFFF, { { 0 } }, 100, prep_file_line },
  { "PrepareOAMData", hm_PrepareOAMData, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_IO | HM_INTERP,
    "src/objects.c", 0x8583E0, 0, 0, 0, 0xFFFF, { { 0 } }, 300, prep_prepare_oam },
  { "COP_Return", hm_ActionNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/interact.c", 0x80872A, 0, 0, 0, 0xFFFF, { { 0 } }, 50, NULL },
  { "ScreenTransitionReturn", hm_ActionNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/interact.c", 0x80972B, 0, 0, 0, 0xFFFF, { { 0 } }, 50, NULL },
  { "ShippingReturn", hm_ActionNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/interact.c", 0x8281BF, 0, 0, 0, 0xFFFF, { { 0 } }, 50, NULL },
  { "CODE_83F4D7", hm_ActionNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/interact.c", 0x83F4D7, 0, 0, 0, 0xFFFF, { { 0 } }, 50, NULL },
  { "UNUSED2", hm_UNUSED2, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/audio.c", 0x838376, 1, 0, 0, 0xFFFF, { W16(0x0103, 0, 0xFFFF) }, 0, NULL },
  { "UNK_Audio1", hm_UNK_Audio1, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU | HM_INTERP | HM_LIVE,
    "src/audio.c", 0x83843D, 0, 0, 0, 0xFFFF, { { 0 } }, 1, prep_boot_upload },
  { "UNK_Audio4", hm_UNK_Audio4, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_APU | HM_INTERP,
    "src/audio.c", 0x8384D3, 1, 0, 0, 0xFFFF, { { 0 } }, 20, prep_upload },
  { "GetFenceFromStack", hm_GetFenceFromStack, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/interact.c", 0x81D644, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_actions },
  { "CODE_81D6E2", hm_CODE_81D6E2, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/interact.c", 0x81D6E2, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_actions },
  { "CODE_81D6F8", hm_CODE_81D6F8, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/interact.c", 0x81D6F8, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_actions },
  { "CODE_81D1C5", hm_CODE_81D1C5, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/people.c", 0x81D1C5, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_actions },
  { "GetBackgroundData", hm_GetBackgroundData, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/maptiles.c", 0x80A7AE, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_actions },
  { "UNK_PresetsMemory3", hm_UNK_PresetsMemory3, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/objects.c", 0x81A4C7, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_actions },
  { "Unused01", hm_ActionNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/interact.c", 0x81D643, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_actions },
  { "Unused02", hm_ActionNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/interact.c", 0x81D66F, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_actions },
  { "Unused03", hm_ActionNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/interact.c", 0x81D6E1, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_actions },
  { "Unused04", hm_ActionNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/interact.c", 0x81D70E, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_actions },
  { "Unused05", hm_ActionNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/interact.c", 0x81D70F, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_actions },
  { "Unused06", hm_ActionNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/interact.c", 0x81D710, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_actions },
  { "Unused07", hm_ActionNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/interact.c", 0x81D711, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_actions },
  { "Unused08", hm_ActionNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/interact.c", 0x81D712, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_actions },
  { "Unused09", hm_ActionNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/interact.c", 0x81D713, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_actions },
  { "Unused10", hm_ActionNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/interact.c", 0x81D714, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_actions },
  { "Unused11", hm_ActionNothing, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/interact.c", 0x81D715, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_actions },
  { "ShippingScene", hm_ShippingScene, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/shipping.c", 0x828131, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_shipping },
  { "ShippingSceneDialogue", hm_ShippingSceneDialogue, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/shipping.c", 0x828165, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_shipping },
  { "CODE_858C9F", hm_CODE_858C9F, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/objects.c", 0x858C9F, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_destroy },
  { "CODE_858B7B", hm_CODE_858B7B, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/objects.c", 0x858B7B, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_destroy },
  { "CODE_858B41", hm_CODE_858B41, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/objects.c", 0x858B41, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_destroy },
  { "CODE_8581A2", hm_CODE_8581A2, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/objects.c", 0x8581A2, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_destroy },
  { "SUB_848895", hm_SUB_848895, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/events.c", 0x848895, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_destroy_cmd },
  { "SUB_848020", hm_SUB_848020, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/events.c", 0x848020, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_destroy },
  { "Event_UNK1", hm_Event_UNK1, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x84B34B, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_destroy_cmd },
  { "Event_UNKCCchanges7", hm_Event_UNKCCchanges7, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x849375, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_destroy_cmd },
  { "Event_UNKCCchanges11", hm_Event_UNKCCchanges11, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x849810, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_destroy_cmd },
  { "Event_UNKCCchanges8", hm_Event_UNKCCchanges8, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x84957D, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_anim_cmd },
  { "Event_UNKCCchanges15", hm_Event_UNKCCchanges15, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x84B8D9, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_anim_cmd },
  { "Event_UNKCCchanges3", hm_Event_UNKCCchanges3, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x848F0A, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_anim_cmd },
  { "Event_UNKCCchanges17", hm_Event_UNKCCchanges17, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x84BA72, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_anim_cmd },
  { "Event_UNKCCchanges5", hm_Event_UNKCCchanges5, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x849079, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_talk_cmd },
  { "Event_UNKCCchanges9", hm_Event_UNKCCchanges9, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x849633, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_talk_cmd },
  { "Event_UNKCFD1", hm_Event_UNKCFD1, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/eventcmds.c", 0x849553, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_map_animation_cmd },
  { "Event_AudioRelated2", hm_Event_AudioRelated2, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_APU,
    "src/eventcmds.c", 0x848CA8, 0, 0, 0, 0xFFFF, { { 0 } }, 100, prep_music_cmd },
  { "CODE_8580B9", hm_CODE_8580B9, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/objects.c", 0x8580B9, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_change_gobj },
  { "CODE_858100", hm_CODE_858100, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/objects.c", 0x858100, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_change_gobj },
  { "SUB_848331", hm_SUB_848331, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/events.c", 0x848331, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_slot_walk },
  { "SUB_8483CC", hm_SUB_8483CC, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/events.c", 0x8483CC, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_slot_wander },
  { "SUB_848286", hm_SUB_848286, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/events.c", 0x848286, 1, 0, 0, 0xFF, { { 0 } }, 0, prep_slot_sprite },
  { "CODE_81CFA0", hm_CODE_81CFA0, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/objects.c", 0x81CFA0, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_object },
  { "CODE_81CFE6", hm_CODE_81CFE6, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/objects.c", 0x81CFE6, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_object },
  { "CODE_81D03D", hm_CODE_81D03D, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/objects.c", 0x81D03D, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_player_object },
  { "GetOBJOrderToDraw", hm_GetOBJOrderToDraw, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/objects.c", 0x858377, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_draw_order },
  { "PresetsWRAMCopyofOAM", hm_PresetsWRAMCopyofOAM, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
    "src/objects.c", 0x858647, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_oam_copy },
  { "PresetsWRAMCopyOAMCopy", hm_PresetsWRAMCopyOAMCopy, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/objects.c", 0x858896, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_oam_copy },
  { "InitializeOBJs", hm_InitializeOBJs, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/objects.c", 0x85820F, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_init_objs },
  { "CODE_8582C7", hm_CODE_8582C7, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/objects.c", 0x8582C7, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_animate_gobjs },
  { "CODE_8581CB", hm_CODE_8581CB, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/objects.c", 0x8581CB, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_change_gobj },
  { "InvertIfFlipedX", hm_InvertIfFlipedX, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/objects.c", 0x858BB0, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_flip },
  { "InvertIfFlipedY", hm_InvertIfFlipedY, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR | HM_INTERP,
    "src/objects.c", 0x858BBC, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_flip },
  { "ClearSpriteDataTables", hm_ClearSpriteDataTables, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/objects.c", 0x858ED7, 1, 0, 0, 0xFFFF, { { 0 } }, 200, NULL },
  { "CODE_81A58F", hm_CODE_81A58F, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/mapedit.c", 0x81A58F, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_footprints },
  { "CODE_81A600", hm_CODE_81A600, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_INTERP,
    "src/mapedit.c", 0x81A600, 0, 0, 0, 0xFFFF, { { 0 } }, 0, prep_animation_player },
  { "CODE_81A4F1", hm_CODE_81A4F1, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/mapedit.c", 0x81A4F1, 1, 0, 0, 0xFFFF, { { 0 } }, 0, prep_map_tiles },
};
const int g_hm_decomp_count = (int)(sizeof g_hm_decomp_fns / sizeof g_hm_decomp_fns[0]);

int g_hm_decomp_check;

typedef struct Snapshot {
  CpuState cpu;
  uint8_t ram[0x20000];
  uint8_t dma[0x80];       /* $4300-$437F before the call */
  uint8_t *sram;           /* the battery RAM before the call (or NULL) */
  int apu_from;            /* where its sound-port traffic starts in s_apu_log */
  HmIoState *io;           /* HM_IO: the chips before the call (hm_io.c) */
} Snapshot;
static void snapshot_free(Snapshot *s) { hm_io_free(s->io); free(s->sram); free(s); }

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
  RtlAddApuPortObserver(apu_wrote);
  RtlAddApuPortReadObserver(apu_read);
  /* The interpreter gives up on a routine after 2 million steps; the sound
   * uploads run by it (UNK_Audio3) take far more while the chip answers.
   * While fuzzing, allow them (a constructor runs before anything is
   * interpreted, and an explicit setting wins). */
  const char *fz = getenv("HM_DECOMP_FUZZ");
  if (fz && *fz) setenv("SNESRECOMP_INTERP_STEP_CAP", "400000000", 0);
}

/* While fuzzing, only the routine being fuzzed is compared: the calls it
 * makes have their own fuzz entries, and checking thousands of nested calls
 * (each one copies 128 KB) is too slow for the 4096-tile farm loops. */
static int s_fuzzing, s_check_depth;

/* HM_DECOMP_SKIP_WHY=1: say why a call was skipped (debugging). */
static void skip_why(const char *name, const char *why)
{
  static int on = -1;
  if (on < 0) on = getenv("HM_DECOMP_SKIP_WHY") != NULL;
  if (on) fprintf(stderr, "[decomp-check] skipped %s: %s\n", name, why);
}

void *hm_decomp_check_begin(const CpuState *cpu, const char *name)
{
  if (s_fuzzing && s_check_depth > 0) return NULL;
  int index = 0;
  const HmDecompFn *f = find(name, &index);
  s_check_depth++;
  Snapshot *s = malloc(sizeof *s);
  if (!s) { s_check_depth--; return NULL; }
  if (s_check_depth == 1) { s_apu_n = 0; s_apu_overflow = 0; }
  s_apu_recording = 1;
  s->apu_from = s_apu_n;
  s->cpu = *cpu;
  memcpy(s->ram, cpu->ram, sizeof s->ram);
  dma_regs_save(s->dma);
  s->sram = NULL;
  if (g_sram && g_sram_size > 0 && (s->sram = malloc((size_t)g_sram_size)) != NULL)
    memcpy(s->sram, g_sram, (size_t)g_sram_size);
  s->io = (f && (f->out_mask & HM_IO)) ? hm_io_save() : NULL;
  return s;
}

void hm_decomp_check_end(const char *name, void *snapshot, const CpuState *cpu, int completed)
{
  Snapshot *s = snapshot;
  int index = 0;
  if (!s) return;                       /* nested call not checked (fuzzing) */
  s_check_depth--;
  if (s_check_depth == 0) s_apu_recording = 0;
  HmApuReplay apu = { s_apu_log + s->apu_from, s_apu_n - s->apu_from, 0, 0, "" };
  const HmDecompFn *f = find(name, &index);
  if (!f) { snapshot_free(s); return; }
  Stats *st = &s_stats[index];
  st->calls++;
  /* A routine interrupted mid-way (the frame deadline yielded), handed to
   * the interpreter (its caller runs there: the compiled body returns at
   * once and the interpreter runs it, e.g. the sprite routines of bank $85)
   * or run in decimal mode is not comparable here; the rewrites assume
   * binary mode. The fuzzer still checks them. */
  if (!completed || s->cpu._flag_D) { skip_why(name, completed ? "decimal mode" : "interrupted"); st->skipped++; snapshot_free(s); return; }

  HmRegs in = { s->cpu.A, s->cpu.X, s->cpu.Y, s->cpu._flag_C, s->cpu._flag_N, s->cpu.DB, s->cpu.D,
                s->cpu.S, s->cpu.PB };
  { CpuState flags = s->cpu; cpu_mirrors_to_p(&flags); in.p = flags.P; }
  HmRegs out = in;
  uint8_t dma_after[0x80];
  dma_regs_save(dma_after);
  if (s_apu_overflow) { skip_why(name, "sound-port log full"); st->skipped++; snapshot_free(s); return; }
  /* HM_IO: the rewrite runs on the real machine (WRAM and chips) put back
   * as they were before the call; afterwards what it left is taken (WRAM to
   * s->ram, the DMA registers to s->dma, the chips to `io_rw`) and the
   * original's result is put back. */
  int io = (f->out_mask & HM_IO) != 0;
  HmIoState *io_rom = NULL, *io_rw = NULL;
  uint8_t *ram_rom = NULL;
  uint64_t clock = g_cpu.master_cycles;
  if (io) {
    if (!s->io || !(io_rom = hm_io_save()) || !(ram_rom = malloc(sizeof s->ram))) {
      hm_io_free(io_rom); skip_why(name, "chip state not available"); st->skipped++; snapshot_free(s); return;
    }
    memcpy(ram_rom, cpu->ram, sizeof s->ram);
    memcpy(cpu->ram, s->ram, sizeof s->ram);
    hm_io_load(s->io);
  }
  g_hm_dma_regs = io ? NULL : s->dma;
  g_hm_io_live = io;
  g_hm_sram = s->sram;
  g_hm_sram_size = (uint32_t)g_sram_size;
  g_hm_apu = &apu;
  if (!setjmp(s_apu_abort)) f->fn(io ? cpu->ram : s->ram, &out);
  g_hm_apu = NULL;
  g_hm_sram = NULL;
  g_hm_dma_regs = NULL;
  g_hm_io_live = 0;
  if (io) {
    io_rw = hm_io_save();
    dma_regs_save(s->dma);
    memcpy(s->ram, cpu->ram, sizeof s->ram);
    memcpy(cpu->ram, ram_rom, sizeof s->ram);
    hm_io_load(io_rom);
    g_cpu.master_cycles = clock;
    free(ram_rom);
  }
  if (!apu.bad && apu.pos != apu.n)
    snprintf(apu.why, sizeof apu.why, "APU: rewrite did %d port accesses, rom %d", apu.pos, apu.n), apu.bad = 1;

  int bad = 0;
  char why[256] = "";
  if (apu.bad) { bad = 1; snprintf(why, sizeof why, "%s", apu.why); }
  if (!bad && io && io_rw && hm_io_diff(io_rw, io_rom, why, sizeof why)) bad = 1;
  hm_io_free(io_rw);
  hm_io_free(io_rom);
  if (!bad && (f->out_mask & HM_OUT_A) && out.a != cpu->A) { bad = 1; snprintf(why, sizeof why, "A %04X != rom %04X", out.a, cpu->A); }
  if (!bad && (f->out_mask & HM_OUT_X) && out.x != cpu->X) { bad = 1; snprintf(why, sizeof why, "X %04X != rom %04X", out.x, cpu->X); }
  if (!bad && (f->out_mask & HM_OUT_Y) && out.y != cpu->Y) { bad = 1; snprintf(why, sizeof why, "Y %04X != rom %04X", out.y, cpu->Y); }
  if (!bad && (f->out_mask & HM_OUT_C) && out.c != cpu->_flag_C) { bad = 1; snprintf(why, sizeof why, "C %u != rom %u", out.c, cpu->_flag_C); }
  if (!bad) {
    for (int i = 0; i < 0x80; i++) {
      int reg = i & 0xF;                 /* $43xC-$43xE don't exist (read 0) */
      if ((reg <= 0xB || reg == 0xF) && s->dma[i] != dma_after[i]) {
        bad = 1;
        snprintf(why, sizeof why, "DMA $43%02X = %02X, rom wrote %02X", i, s->dma[i], dma_after[i]);
        break;
      }
    }
  }
  if (!bad && s->sram) {
    for (int i = 0; i < g_sram_size; i++)
      if (s->sram[i] != g_sram[i]) {
        bad = 1;
        snprintf(why, sizeof why, "SRAM %04X = %02X, rom wrote %02X", i, s->sram[i], g_sram[i]);
        break;
      }
  }
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
  snapshot_free(s);
}

/* ── Fuzzing (HM_DECOMP_FUZZ=<frame>:<iterations>) ─────────────────── */
static uint32_t s_rng = 0x12345678u, s_seed;
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
    s_seed = s_rng;
  }
  /* One routine per frame, starting at frame `at`: the frame watchdog
   * (5 s of real time per frame) then applies to each routine on its own. */
  /* The routines that talk to the sound chip get a window of their own
   * (HM_DECOMP_FUZZ_APU=<frame>, by default 900 frames earlier): in some
   * moments of the game the chip is busy for a long time (loading music)
   * and never says "ready" (in the demo, around frames 1210 and 1650). */
  static int apu_at = -2;
  if (apu_at == -2) {
    const char *e = getenv("HM_DECOMP_FUZZ_APU");
    apu_at = e && *e ? atoi(e) : (at >= 900 ? at - 900 : at + g_hm_decomp_count);
  }
  if (at < 0) return;
  int only = -1;
  if ((int)frame >= at && (int)frame < at + g_hm_decomp_count &&
      !(g_hm_decomp_fns[(int)frame - at].out_mask & HM_APU))
    only = (int)frame - at;
  /* HM_LIVE (the upload to the chip's boot ROM): frame 1, before the
   * game's own upload is done; only when named (below). */
  for (int i = 0; i < g_hm_decomp_count && only < 0; i++)
    if ((g_hm_decomp_fns[i].out_mask & HM_LIVE) && frame == 1) only = i;
  for (int i = 0, k = 0; i < g_hm_decomp_count && only < 0; i++)
    if ((g_hm_decomp_fns[i].out_mask & HM_APU) && !(g_hm_decomp_fns[i].out_mask & HM_LIVE)) {
      if ((int)frame == apu_at + k) only = i;
      k++;
    }
  if (only < 0) return;

  static CpuState saved_cpu;
  static uint8_t saved_ram[0x20000], saved_dma[0x80];
  saved_cpu = g_cpu;
  memcpy(saved_ram, g_cpu.ram, sizeof saved_ram);
  static uint8_t *saved_sram;                    /* the saved games: put back too */
  if (g_sram && g_sram_size > 0 && !saved_sram) saved_sram = malloc((size_t)g_sram_size);
  if (saved_sram) memcpy(saved_sram, g_sram, (size_t)g_sram_size);
  dma_regs_save(saved_dma);
  HmIoState *saved_io = hm_io_save();           /* the chips (HM_IO routines change them) */
  HmBeam *saved_beam = hm_beam_save();
  uint16_t saved_pad[2] = { RtlGetPadState(0), RtlGetPadState(1) };  /* prep_joypad */
  int prev = g_hm_decomp_check;
  if (!s_stats) { s_stats = calloc((size_t)g_hm_decomp_count, sizeof *s_stats); atexit(report); }
  g_hm_decomp_check = 1;
  s_fuzzing = 1;
  /* Long interpreted routines (CowFeedingandStatus) would otherwise pause at
   * the frame's deadline half-way, as they do in the game, and not be
   * comparable. The frame driver sets the deadline again every frame. */
  interp_bridge_set_master_deadline(0);
  interp_bridge_add_pre_opcode_hook(0x808654, nmi_at_wait);   /* WaitForNMI */
  interp_bridge_add_pre_opcode_hook(0x80866C, nmi_at_wait);   /* WaitForNMIATimes */
  if (only == 0)
    fprintf(stderr, "[decomp-fuzz] frames %u-%u: %d calls per routine\n", frame,
            frame + g_hm_decomp_count - 1, iters);
  for (int i = only; i == only; i++) {
    const HmDecompFn *f = &g_hm_decomp_fns[i];
    int n_calls = (f->fuzz_max && f->fuzz_max < iters) ? f->fuzz_max : iters;
    /* Each routine gets its own stream of random inputs (from the seed and
     * its place in the list), so a call can be replayed on its own with
     * HM_DECOMP_FUZZ_ONLY / HM_DECOMP_FUZZ_CALL. */
    s_rng = s_seed ^ (0x9E3779B9u * (uint32_t)(i + 1));
    if (!s_rng) s_rng = 1;
    /* HM_DECOMP_FUZZ_ONLY=<name>: fuzz just that routine (debugging). */
    const char *only_name = getenv("HM_DECOMP_FUZZ_ONLY");
    if (only_name && *only_name) {                /* a name, or several: a,b,c */
      size_t len = strlen(f->name);
      const char *s = only_name;
      int found = 0;
      while (!found && (s = strstr(s, f->name)) != NULL) {
        found = (s == only_name || s[-1] == ',') && (s[len] == '\0' || s[len] == ',');
        s += len;
      }
      if (!found) n_calls = 0;
      else if (f->out_mask & HM_LIVE) n_calls = 1;   /* named: one call */
    } else if (f->out_mask & HM_LIVE) n_calls = 0;
    /* HM_DECOMP_FUZZ_CALL=<n>: run only call n of it (same inputs as in the
     * full run), e.g. to trace it. */
    const char *one_call = getenv("HM_DECOMP_FUZZ_CALL");
    int pick = one_call && *one_call ? atoi(one_call) : -1;
    /* A routine that talks to the sound chip changes the chip too, and the
     * chip only runs up to the CPU's time. Before each call, wait (outside
     * the check) until the chip says "ready" (port 3 = 1), as the game
     * would before sending anything; if it does not (busy loading music),
     * the call is skipped. */
    int apu = (f->out_mask & HM_APU) != 0;
    uint64_t apu_clock = saved_cpu.master_cycles;
    /* The sound routines: with the chip going on from call to call while
     * the WRAM that keeps count is put back every time, what each call
     * sends piles up in the chip (instruments in its memory, a song
     * changing) until the sound program stops answering or stays busy.
     * So for them the whole machine is saved once and put back before
     * every call (and after the last), the chip included: every call
     * starts from the same moment. */
    static uint8_t *machine;
    size_t machine_size = 0;
    if (apu && !(f->out_mask & HM_LIVE)) {
      if (!machine) machine = malloc(4u << 20);
      if (machine) machine_size = RtlSaveSnapshotToMemory(machine, 4u << 20);
    }
    volatile int hangs = 0;
    for (int n = 0; n < n_calls; n++) {
      if (machine_size) {
        RtlLoadSnapshotFromMemory(machine, machine_size);
        apu_clock = saved_cpu.master_cycles;
      }
      /* Between frames nothing is being checked: a check left open by a
       * call that never came back (a sound routine waiting for the chip
       * when the frame was cut) would make every later call look nested,
       * and unchecked. */
      s_check_depth = 0;
      s_apu_recording = 0;
      g_cpu = saved_cpu;
      memcpy(g_cpu.ram, saved_ram, sizeof saved_ram);
      if (saved_sram) memcpy(g_sram, saved_sram, (size_t)g_sram_size);
      dma_regs_restore(saved_dma);
      if (f->out_mask & HM_IO) { hm_io_load(saved_io); hm_beam_load(saved_beam); }
      /* Every call starts with a fresh frame budget: long routines (the
       * 4096-tile farm loop) would otherwise hit the frame deadline, yield
       * half-way and not be comparable. The machine is restored after. */
      g_cpu.master_cycles = apu ? apu_clock : (f->out_mask & HM_IO) ? saved_cpu.master_cycles : 0;
      if (apu && !(f->out_mask & HM_LIVE)) {
        int ready = 0;
        for (long k = 0; k < 200000 && !ready; k++) {
          g_cpu.master_cycles += 64;
          ready = ReadReg(0x2143) == 1;
        }
        apu_clock = g_cpu.master_cycles;
        if (!ready) { skip_why(f->name, "sound chip not ready"); s_stats[i].skipped++; continue; }
      }
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
      /* JSL enters the routine's bank; a JSR runs in its caller's bank,
       * which is the routine's own bank too */
      g_cpu.PB = (uint8_t)(f->pc24 >> 16);
      uint8_t frame_size = 3;
      if (f->out_mask & HM_RTI) {
        /* an interrupt in the middle of the game's code: its bank, an
         * address and the flags; the handler starts with I set, decimal
         * clear and PB = 0 */
        cpu_push_interrupt_frame_at(&g_cpu, 0x808654);
        g_cpu.P = (uint8_t)((g_cpu.P | 0x04) & ~0x08);
        cpu_p_to_mirrors(&g_cpu);
        g_cpu.PB = 0x00;
        frame_size = 4;
      } else if (f->out_mask & HM_JSR) {
        cpu_push_jsr_return_frame(&g_cpu);
        frame_size = 2;
      } else {
        cpu_push_jsl_return_frame(&g_cpu);
      }
      if (pick >= 0 && n != pick) continue;
      /* HM_DECOMP_FUZZ_DUMP=<file>: with HM_DECOMP_FUZZ_CALL, save that call's
       * WRAM (128 KB) and registers before it runs (debugging). */
      const char *dump = getenv("HM_DECOMP_FUZZ_DUMP");
      if (dump && *dump && pick == n) {
        FILE *fd = fopen(dump, "wb");
        if (fd) {
          fwrite(g_cpu.ram, 1, 0x20000, fd);
          uint16_t regs[4] = { g_cpu.A, g_cpu.X, g_cpu.Y, g_cpu.DB };
          fwrite(regs, sizeof regs, 1, fd);
          fclose(fd);
        }
      }
      unsigned bad_before = s_stats[i].bad;
      /* the frame watchdog (5 s of real time per frame): the waiting
       * routines run many frames' worth of work in the interpreter, so each
       * call gets its own budget (the frame's guest code is over here: the
       * fuzzer runs after it, from the frame tick) */
      if ((f->out_mask & HM_WAIT) && g_recomp_stack_top == 0) WatchdogFrameStart();
      /* A sound routine can wait for ever for a chip that stays busy (with
       * the music playing it never gets free): the watchdog then cuts the
       * call, which counts as skipped, and the machine is put back. */
      jmp_buf keep;
      int guard = machine_size && g_recomp_stack_top == 0;
      if (guard) {
        memcpy(keep, g_watchdog_jmp, sizeof keep);
        WatchdogFrameStart();
        if (setjmp(g_watchdog_jmp)) {
          memcpy(g_watchdog_jmp, keep, sizeof keep);
          WatchdogFrameStart();
          g_hm_io_live = 0; g_hm_apu = NULL; g_hm_dma_regs = NULL; g_hm_sram = NULL;
          s_check_depth = 0; s_apu_recording = 0;
          if (s_nmi_sim) keep_waits_interpreted(0, NULL);
          s_nmi_sim = 0;
          skip_why(f->name, "hung: the chip never answered");
          s_stats[i].calls++; s_stats[i].skipped++;
          if (++hangs == 3) break;                   /* it stays busy: give up */
          continue;
        }
      }
      s_nmi_sim = (f->out_mask & HM_WAIT) != 0;
      keep_waits_interpreted(s_nmi_sim, f);
      if (f->out_mask & (HM_INTERP | HM_WAIT | HM_RTI)) {
        /* No wrapper around it (or, waiting for the NMI, never a complete
         * call in the compiled code): check this call here. */
        void *snap = hm_decomp_check_begin(&g_cpu, f->name);
        /* No compiled body: run the original in the recompiler's
         * interpreter, as the game itself does. */
        uint16_t s_in = g_cpu.S;
        RecompReturn rr = (f->out_mask & HM_RTI)
          ? (interp_bridge_run_interrupt(&g_cpu, f->pc24 & 0x7FFFFF) ? RECOMP_RETURN_NORMAL : RECOMP_RETURN_SKIP_1)
          : interp_tier_dispatch(&g_cpu, f->pc24);
        /* The interpreter gives up on very long runs and still answers
         * NORMAL: only a run that returned (its frame popped) is complete. */
        hm_decomp_check_end(f->name, snap, &g_cpu,
                            rr == RECOMP_RETURN_NORMAL && g_cpu.S == (uint16_t)(s_in + frame_size));
      } else {
        cpu_dispatch_pc_paired(&g_cpu, f->pc24, frame_size);
      }
      if (s_nmi_sim) keep_waits_interpreted(0, NULL);
      s_nmi_sim = 0;
      if (guard) memcpy(g_watchdog_jmp, keep, sizeof keep);
      if (s_stats[i].bad != bad_before && s_stats[i].bad <= 5)
        fprintf(stderr, "[decomp-fuzz]   (that was %s call %d)\n", f->name, n);
      if (apu) apu_clock = g_cpu.master_cycles;
    }
    if (machine_size) {
      RtlLoadSnapshotFromMemory(machine, machine_size);
      apu_clock = saved_cpu.master_cycles;
    }
    if (apu) saved_cpu.master_cycles = apu_clock;       /* time only goes forward */
  }
  g_cpu = saved_cpu;
  memcpy(g_cpu.ram, saved_ram, sizeof saved_ram);
  if (saved_sram) memcpy(g_sram, saved_sram, (size_t)g_sram_size);
  dma_regs_restore(saved_dma);
  hm_io_load(saved_io);
  hm_io_free(saved_io);
  hm_beam_load(saved_beam);
  free(saved_beam);
  RtlSetPadState(0, saved_pad[0]);
  RtlSetPadState(1, saved_pad[1]);
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
