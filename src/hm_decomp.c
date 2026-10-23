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
  ram[slot + 0x03] = (uint8_t)steps[rnd() % 8];
  ram[slot + 0x04] = (uint8_t)steps[rnd() % 8];
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
  { "HaveLunch", hm_HaveLunch, 0, "src/stamina.c", 0x8280AA, 1, 0, 0, 0xFF,
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
  { "LoadDefaultFarmMap", hm_LoadDefaultFarmMap, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/farm.c", 0x82A65A, 0, 0, 0, 0xFFFF, { { 0 } }, 100, prep_map_copy },
  { "ClearsofAllCCStructs", hm_ClearsofAllCCStructs, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
    "src/events.c", 0x848000, 0, 0, 0, 0xFFFF, { { 0 } }, 200, prep_map_copy },
  { "MultiplicationUnsigned", hm_MultiplicationUnsigned, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C | HM_JSR,
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
  { "Unk_Audio23", hm_Unk_Audio23, HM_OUT_A | HM_OUT_X | HM_OUT_Y | HM_OUT_C,
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

void *hm_decomp_check_begin(const CpuState *cpu)
{
  if (s_fuzzing && s_check_depth > 0) return NULL;
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
  if (!f) { free(s->sram); free(s); return; }
  Stats *st = &s_stats[index];
  st->calls++;
  /* A routine interrupted mid-way (the frame deadline yielded), handed to
   * the interpreter (its caller runs there: the compiled body returns at
   * once and the interpreter runs it, e.g. the sprite routines of bank $85)
   * or run in decimal mode is not comparable here; the rewrites assume
   * binary mode. The fuzzer still checks them. */
  if (!completed || s->cpu._flag_D) { skip_why(name, completed ? "decimal mode" : "interrupted"); st->skipped++; free(s->sram); free(s); return; }

  HmRegs in = { s->cpu.A, s->cpu.X, s->cpu.Y, s->cpu._flag_C, s->cpu._flag_N, s->cpu.DB, s->cpu.D,
                s->cpu.S, s->cpu.PB };
  { CpuState flags = s->cpu; cpu_mirrors_to_p(&flags); in.p = flags.P; }
  HmRegs out = in;
  uint8_t dma_after[0x80];
  dma_regs_save(dma_after);
  if (s_apu_overflow) { skip_why(name, "sound-port log full"); st->skipped++; free(s->sram); free(s); return; }
  g_hm_dma_regs = s->dma;
  g_hm_sram = s->sram;
  g_hm_sram_size = (uint32_t)g_sram_size;
  g_hm_apu = &apu;
  if (!setjmp(s_apu_abort)) f->fn(s->ram, &out);
  g_hm_apu = NULL;
  g_hm_sram = NULL;
  g_hm_dma_regs = NULL;
  if (!apu.bad && apu.pos != apu.n)
    snprintf(apu.why, sizeof apu.why, "APU: rewrite did %d port accesses, rom %d", apu.pos, apu.n), apu.bad = 1;

  int bad = 0;
  char why[256] = "";
  if (apu.bad) { bad = 1; snprintf(why, sizeof why, "%s", apu.why); }
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
  free(s->sram); free(s);
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
  for (int i = 0, k = 0; i < g_hm_decomp_count && only < 0; i++)
    if (g_hm_decomp_fns[i].out_mask & HM_APU) {
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
  uint16_t saved_pad[2] = { RtlGetPadState(0), RtlGetPadState(1) };  /* prep_joypad */
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
    }
    /* HM_DECOMP_FUZZ_CALL=<n>: run only call n of it (same inputs as in the
     * full run), e.g. to trace it. */
    const char *one_call = getenv("HM_DECOMP_FUZZ_CALL");
    int pick = one_call && *one_call ? atoi(one_call) : -1;
    /* A routine that talks to the sound chip changes the chip too, and the
     * chip only runs up to the CPU's time: for these the CPU clock never
     * goes back. Each call starts where the previous one ended (the chip
     * goes on as if the game called the routine again and again), and the
     * game resumes after the last one, like after a long frame. Before each
     * call, wait (outside the check) until the chip says "ready" (port 3 =
     * 1), as the game would before sending anything; if it does not (busy
     * loading music), the call is skipped. */
    int apu = (f->out_mask & HM_APU) != 0;
    uint64_t apu_clock = saved_cpu.master_cycles;
    for (int n = 0; n < n_calls; n++) {
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
      /* Every call starts with a fresh frame budget: long routines (the
       * 4096-tile farm loop) would otherwise hit the frame deadline, yield
       * half-way and not be comparable. The machine is restored after. */
      g_cpu.master_cycles = apu ? apu_clock : 0;
      if (apu) {
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
      if (f->out_mask & HM_JSR) cpu_push_jsr_return_frame(&g_cpu);
      else cpu_push_jsl_return_frame(&g_cpu);
      uint8_t frame_size = (f->out_mask & HM_JSR) ? 2 : 3;
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
      if (f->out_mask & HM_INTERP) {
        /* No wrapper around it: check this call here. */
        void *snap = hm_decomp_check_begin(&g_cpu);
        /* No compiled body: run the original in the recompiler's
         * interpreter, as the game itself does. */
        uint16_t s_in = g_cpu.S;
        RecompReturn rr = interp_tier_dispatch(&g_cpu, f->pc24);
        /* The interpreter gives up on very long runs and still answers
         * NORMAL: only a run that returned (its frame popped) is complete. */
        hm_decomp_check_end(f->name, snap, &g_cpu,
                            rr == RECOMP_RETURN_NORMAL && g_cpu.S == (uint16_t)(s_in + frame_size));
      } else {
        cpu_dispatch_pc_paired(&g_cpu, f->pc24, frame_size);
      }
      if (s_stats[i].bad != bad_before && s_stats[i].bad <= 5)
        fprintf(stderr, "[decomp-fuzz]   (that was %s call %d)\n", f->name, n);
      if (apu) apu_clock = g_cpu.master_cycles;
    }
    if (apu) saved_cpu.master_cycles = apu_clock;       /* time only goes forward */
  }
  g_cpu = saved_cpu;
  memcpy(g_cpu.ram, saved_ram, sizeof saved_ram);
  if (saved_sram) memcpy(g_sram, saved_sram, (size_t)g_sram_size);
  dma_regs_restore(saved_dma);
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
