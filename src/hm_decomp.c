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
/* The emulated DMA unit (snes/dma.h): reading $43xx has no side effects. */
typedef struct Dma Dma;
extern Dma *g_dma;
uint8_t dma_read(Dma *dma, uint16_t adr);
void dma_write(Dma *dma, uint16_t adr, uint8_t val);

uint8_t *g_hm_dma_regs;
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
  if (rnd() & 1) {                                        /* property table: somewhere in ROM */
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
  if (rnd() % 4) {
    /* a tile property table of our own in WRAM ($7F:F000; restored after
     * the fuzz): most tools work on most tiles, from every side and in
     * every season, so the interesting paths are reached */
    for (unsigned i = 0; i < 256; i++) {
      uint8_t *e = &ram[0x1F000 + 4 * i];
      e[0] = (uint8_t)rnd();
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
};
const int g_hm_decomp_count = (int)(sizeof g_hm_decomp_fns / sizeof g_hm_decomp_fns[0]);

int g_hm_decomp_check;

typedef struct Snapshot {
  CpuState cpu;
  uint8_t ram[0x20000];
  uint8_t dma[0x80];       /* $4300-$437F before the call */
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
  dma_regs_save(s->dma);
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
  /* A routine interrupted mid-way (the frame deadline yielded), handed to
   * the interpreter (its caller runs there: the compiled body returns at
   * once and the interpreter runs it, e.g. the sprite routines of bank $85)
   * or run in decimal mode is not comparable here; the rewrites assume
   * binary mode. The fuzzer still checks them. */
  if (!completed || s->cpu._flag_D) { st->skipped++; free(s); return; }

  HmRegs in = { s->cpu.A, s->cpu.X, s->cpu.Y, s->cpu._flag_C, s->cpu._flag_N, s->cpu.DB, s->cpu.D,
                s->cpu.S, s->cpu.PB };
  HmRegs out = in;
  uint8_t dma_after[0x80];
  dma_regs_save(dma_after);
  g_hm_dma_regs = s->dma;
  f->fn(s->ram, &out);
  g_hm_dma_regs = NULL;

  int bad = 0;
  char why[256] = "";
  if ((f->out_mask & HM_OUT_A) && out.a != cpu->A) { bad = 1; snprintf(why, sizeof why, "A %04X != rom %04X", out.a, cpu->A); }
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
  if (at < 0 || (int)frame < at || (int)frame >= at + g_hm_decomp_count) return;
  int only = (int)frame - at;

  static CpuState saved_cpu;
  static uint8_t saved_ram[0x20000], saved_dma[0x80];
  saved_cpu = g_cpu;
  memcpy(saved_ram, g_cpu.ram, sizeof saved_ram);
  dma_regs_save(saved_dma);
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
    if (only_name && *only_name && strcmp(only_name, f->name)) n_calls = 0;
    /* HM_DECOMP_FUZZ_CALL=<n>: run only call n of it (same inputs as in the
     * full run), e.g. to trace it. */
    const char *one_call = getenv("HM_DECOMP_FUZZ_CALL");
    int pick = one_call && *one_call ? atoi(one_call) : -1;
    for (int n = 0; n < n_calls; n++) {
      g_cpu = saved_cpu;
      memcpy(g_cpu.ram, saved_ram, sizeof saved_ram);
      dma_regs_restore(saved_dma);
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
      /* JSL enters the routine's bank; a JSR runs in its caller's bank,
       * which is the routine's own bank too */
      g_cpu.PB = (uint8_t)(f->pc24 >> 16);
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
  dma_regs_restore(saved_dma);
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
