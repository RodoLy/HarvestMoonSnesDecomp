/*
 * maptiles.c — what a map gets on top of its stored layout when it loads:
 * the house extensions and painted walls, the power berry flowers, the
 * tools on the shed wall, the goods on the shop shelves, the fodder in the
 * troughs, the eggs in the coop...
 *
 * UNK_ExecuteFromPointers ($81:A9E5, JSL, RTL) calls, through the table
 * $81:A9F6 (one word per map, $22 = the map being loaded), that map's
 * routine (JSR). Maps with nothing to add point at a lone RTS.
 *
 * Every change is a "tile change" of the table $81:B363 (see mapedit.c)
 * stamped at a position in pixels:
 *   SUB_81A7CE   the picture (BG1 tilemap in WRAM) and, if the change has
 *                one, its tile type in the map ($09B6); unlike ChangeMapTile
 *                no VRAM transfer is queued: the whole tilemap is uploaded
 *                after loading.
 *   SUB_82B0A7   the tile types of the block one by one, from a second
 *                sheet at $A7:F500 that matches the graphics sheet (16
 *                types per row): for changes made of different tiles (a
 *                house extension with its door).
 *   SUB_81B2BB   an item of a shop or of the shed (8-byte records: x, y,
 *                tile change, a byte of $7F1F00-... and its bit), shown
 *                always, only if that bit is set, or only if it is not.
 *
 * Four of the map routines are shown by HM-Decomp as data (`dw` lines):
 * the coop ($81:B121), the mountain cave ($81:B1CF), the elf tunnel
 * ($81:B21E) and map $37 ($81:B26D, an indented label inside the barn's
 * block). They are disassembled here from the ROM.
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

static void push16(uint8_t *ram, HmRegs *r, uint16_t v)
{
  hm_push8(ram, r, (uint8_t)(v >> 8));
  hm_push8(ram, r, (uint8_t)v);
}
static uint16_t pull16(uint8_t *ram, HmRegs *r)
{
  uint8_t lo = hm_pull8(ram, r);
  return (uint16_t)(lo | hm_pull8(ram, r) << 8);
}
static uint32_t db_(const HmRegs *r, uint16_t a) { return (uint32_t)r->db << 16 | a; }
static uint16_t rd16(uint8_t *ram, uint32_t a) { return hm_bus_rd16(ram, a & 0xFFFFFF); }
static uint8_t rd8(uint8_t *ram, uint32_t a) { return hm_bus_rd8(ram, a & 0xFFFFFF); }
static void jsl(uint8_t *ram, HmRegs *r, uint16_t ret, uint8_t bank, void (*fn)(uint8_t *, HmRegs *))
{
  hm_jsl(ram, r, ret, bank);
  fn(ram, r);
  hm_rtl(ram, r);
}
static void jsr(uint8_t *ram, HmRegs *r, uint16_t ret, void (*fn)(uint8_t *, HmRegs *))
{
  push16(ram, r, ret);
  fn(ram, r);
  pull16(ram, r);
}

/* ─────────────────────────────────────────────────────────────────────────
 * SUB_81A7CE ($81:A7CE, JSL, RTL). A = tile change, X / Y = position.
 * ChangeMapTile without the VRAM part. It starts by reading the change's
 * entry (SUB_81A801) for nothing: SUB_81A83A reads it again, and so does
 * the next call.
 * Leftovers: $86 / $88 = columns / rows; registers from SUB_82B060 when the
 * change has a tile type, otherwise A = 0, X / Y = the position and C from
 * SUB_81A801.
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_7CE_FIRST   0xA7D6
#define RET_7CE_PICTURE 0xA7E2
#define RET_7CE_ENTRY   0xA7E9
#define RET_7CE_TYPES   0xA7FF

void hm_SUB_81A7CE(uint8_t *ram, HmRegs *r)
{
  uint16_t change = r->a, x = r->x, y = r->y;
  push16(ram, r, y); push16(ram, r, x); push16(ram, r, change);
  jsl(ram, r, RET_7CE_FIRST, 0x81, hm_SUB_81A801);
  pull16(ram, r); pull16(ram, r); pull16(ram, r);
  push16(ram, r, y); push16(ram, r, x); push16(ram, r, change);
  r->a = change; r->x = x; r->y = y;
  jsl(ram, r, RET_7CE_PICTURE, 0x81, hm_SUB_81A83A);
  r->a = pull16(ram, r);                                  /* PLA */
  jsl(ram, r, RET_7CE_ENTRY, 0x81, hm_SUB_81A801);
  r->x = pull16(ram, r);                                  /* PLX; PLY */
  r->y = pull16(ram, r);
  hm_dp_w16(ram, r, 0x86, hm_dp_r16(ram, r, 0x80));
  hm_dp_w16(ram, r, 0x88, hm_dp_r16(ram, r, 0x82));
  r->a = hm_dp_r16(ram, r, 0x84);
  if (r->a == 0) return;
  jsl(ram, r, RET_7CE_TYPES, 0x82, hm_SUB_82B060);
}

/* ─────────────────────────────────────────────────────────────────────────
 * SUB_82B0A7 ($82:B0A7, JSL, RTL). A = tile change, X / Y = position. For
 * every tile of the block, its type from the sheet at $A7:F500 into the map
 * (EditTileonMap). The graphics offset of the change ($7E, 4 bytes per
 * tile, $80 per row of the graphics sheet) gives where it starts in the
 * type sheet: (offset mod $80) / 4 + (offset / $80) * 16.
 * Leftovers: $8E / $90 = the position, $72-$74 = $A7:F500, $86 = one past
 * the last type read, $82 / $84 = columns / rows; X / Y = the same, C = 1,
 * A from the last EditTileonMap.
 * ───────────────────────────────────────────────────────────────────────── */
#define TILE_TYPE_SHEET 0xA7F500
#define RET_0A7_ENTRY   0xB0B0
#define RET_0A7_EDIT    0xB113

void hm_SUB_82B0A7(uint8_t *ram, HmRegs *r)
{
  hm_dp_w16(ram, r, 0x8E, r->x);
  hm_dp_w16(ram, r, 0x90, r->y);
  jsl(ram, r, RET_0A7_ENTRY, 0x81, hm_SUB_81A801);
  hm_dp_w16(ram, r, 0x72, (uint16_t)TILE_TYPE_SHEET);
  *hm_dp(ram, r, 0x74) = (uint8_t)(TILE_TYPE_SHEET >> 16);
  uint16_t graphics = hm_dp_r16(ram, r, 0x7E);
  hm_dp_w16(ram, r, 0x86, (uint16_t)(((graphics & 0x7F) >> 2) + ((graphics >> 7) << 4)));
  uint16_t rows = hm_dp_r16(ram, r, 0x82), cols = hm_dp_r16(ram, r, 0x80);
  hm_dp_w16(ram, r, 0x84, rows);
  hm_dp_w16(ram, r, 0x82, cols);
  uint16_t row = 0;
  do {
    push16(ram, r, row);                                  /* PHY */
    uint16_t col = 0;
    do {
      push16(ram, r, col); push16(ram, r, row);           /* PHX; PHY */
      uint16_t at = hm_dp_r16(ram, r, 0x86);
      hm_dp_w16(ram, r, 0x86, (uint16_t)(at + 1));
      uint8_t type = hm_bus_rd8(ram, hm_dp_long(ram, r, 0x72, at));
      hm_push8(ram, r, type);                             /* PLY; PHY; PHA */
      uint32_t px = (uint32_t)(uint16_t)(col << 4) + hm_dp_r16(ram, r, 0x8E);
      uint32_t py = (uint32_t)(uint16_t)(row << 4) + hm_dp_r16(ram, r, 0x90);
      r->x = (uint16_t)px;
      r->y = (uint16_t)py;
      r->c = py > 0xFFFF;
      r->a = (uint16_t)((py & 0xFF00) | hm_pull8(ram, r));  /* SEP; PLA */
      jsl(ram, r, RET_0A7_EDIT, 0x82, hm_EditTileonMap);
      pull16(ram, r);                                     /* PLY; PLX */
      col = (uint16_t)(pull16(ram, r) + 1);
    } while (col != hm_dp_r16(ram, r, 0x82));
    row = (uint16_t)(pull16(ram, r) + 1);
  } while (row != hm_dp_r16(ram, r, 0x84));
  r->x = hm_dp_r16(ram, r, 0x82);
  r->y = row;
  r->c = 1;
}

/* ─────────────────────────────────────────────────────────────────────────
 * SUB_81B2BB ($81:B2BB, JSR, RTS). A = item, X = list, Y = how:
 *   lists ($81, 8 bytes per item): 0 the shed $BE0F, 1 the flower shop
 *   $BEEF, 2 the general store $BF27, 3 the animal shop $BF6F (any other X
 *   is the shed too);
 *   Y = 2 always; 1 only if the item's bit in $7F1F00 + its byte is set
 *   (a tool you own, hanging in the shed); 0 only if it is not (still for
 *   sale).
 * The record: +0 x, +2 y, +4 tile change, +6 which byte, +7 its bit.
 * Leftovers when nothing is shown: A = Y's high byte : (byte AND bit), X =
 * the byte number, Y = 8 A, C from comparing Y with 2 and 1; otherwise
 * SUB_81A7CE's.
 * ───────────────────────────────────────────────────────────────────────── */
#define SHED_ITEMS      0x7F1F00
#define RET_2BB_STAMP   0xB361

void hm_SUB_81B2BB(uint8_t *ram, HmRegs *r)
{
  uint16_t item = r->a, list = r->x, how = r->y;
  push16(ram, r, how); push16(ram, r, item);              /* PHY; PHA */
  static const uint16_t lists[4] = { 0xBE0F, 0xBEEF, 0xBF27, 0xBF6F };
  uint16_t at = lists[list < 4 ? list : 0];
  hm_dp_w16(ram, r, 0x72, at);
  *hm_dp(ram, r, 0x74) = 0x81;
  item = pull16(ram, r);                                  /* PLA */
  r->c = (item >> 13) & 1;                                /* the third ASL */
  uint16_t y = (uint16_t)(item << 3);
  push16(ram, r, y);                                      /* PHY */
  uint8_t byte = hm_bus_rd8(ram, hm_dp_long(ram, r, 0x72, (uint16_t)(y + 6)));
  uint8_t bit = hm_bus_rd8(ram, hm_dp_long(ram, r, 0x72, (uint16_t)(y + 7)));
  r->x = byte;
  *hm_dp(ram, r, 0x92) = bit;
  r->y = y = pull16(ram, r);                              /* PLY */
  how = pull16(ram, r);                                   /* PLA */
  r->a = how;
  r->c = how >= 2;
  if (how != 2) {
    bool owned = (rd8(ram, SHED_ITEMS + byte) & bit) != 0;
    r->c = how >= 1;
    r->a = (uint16_t)((how & 0xFF00) | (rd8(ram, SHED_ITEMS + byte) & bit));
    if (how == 1 ? !owned : owned) return;
  }
  uint16_t px = hm_bus_rd16(ram, hm_dp_long(ram, r, 0x72, y));
  push16(ram, r, px);
  uint16_t py = hm_bus_rd16(ram, hm_dp_long(ram, r, 0x72, (uint16_t)(y + 2)));
  push16(ram, r, py);
  r->a = hm_bus_rd16(ram, hm_dp_long(ram, r, 0x72, (uint16_t)(y + 4)));
  r->y = pull16(ram, r);
  r->x = pull16(ram, r);
  jsl(ram, r, RET_2BB_STAMP, 0x81, hm_SUB_81A7CE);
}

/* ── The pieces the map routines are made of ──────────────────────────── */
/* LDA $flags; AND #mask: A is left with the result */
static bool flag(uint8_t *ram, HmRegs *r, uint32_t addr, uint16_t mask)
{
  r->a = (uint16_t)(rd16(ram, addr) & mask);
  return r->a != 0;
}
/* the picture (and the type, if the change has one) */
static void stamp(uint8_t *ram, HmRegs *r, uint16_t change, uint16_t x, uint16_t y, uint16_t ret)
{
  r->a = change; r->x = x; r->y = y;
  jsl(ram, r, ret, 0x81, hm_SUB_81A7CE);
}
/* the same, keeping A / X / Y on the stack around it (PHA; PHX; PHY) */
static void stamp_kept(uint8_t *ram, HmRegs *r, uint16_t change, uint16_t x, uint16_t y, uint16_t ret)
{
  push16(ram, r, change); push16(ram, r, x); push16(ram, r, y);
  stamp(ram, r, change, x, y, ret);
  r->y = pull16(ram, r); r->x = pull16(ram, r); r->a = pull16(ram, r);
}
/* the picture, then every tile's type from the type sheet */
static void stamp_typed(uint8_t *ram, HmRegs *r, uint16_t change, uint16_t x, uint16_t y,
                        uint16_t ret_picture, uint16_t ret_types)
{
  stamp_kept(ram, r, change, x, y, ret_picture);
  jsl(ram, r, ret_types, 0x82, hm_SUB_82B0A7);
}
/* an item of a shop or of the shed */
static void item(uint8_t *ram, HmRegs *r, uint16_t n, uint16_t list, uint16_t how, uint16_t ret)
{
  r->a = n; r->x = list; r->y = how;
  jsr(ram, r, ret, hm_SUB_81B2BB);
}
/* LDA !tool_selected; CMP #t; BEQ ...; LDA !tool_backpack; CMP #t; BEQ ...
 * (8-bit A): true when you have it in your hand or in the bag */
static bool carrying(uint8_t *ram, HmRegs *r, uint8_t tool)
{
  uint8_t hand = hm_bus_rd8(ram, db_(r, 0x0921));
  r->a = (uint16_t)((r->a & 0xFF00) | hand);
  r->c = hand >= tool;
  if (hand == tool) return true;
  uint8_t bag = hm_bus_rd8(ram, db_(r, 0x0923));
  r->a = (uint16_t)((r->a & 0xFF00) | bag);
  r->c = bag >= tool;
  return bag == tool;
}
/* an 8-bit LDA.L / CMP #k */
static uint8_t get8(uint8_t *ram, HmRegs *r, uint32_t addr)
{
  uint8_t v = rd8(ram, addr);
  r->a = (uint16_t)((r->a & 0xFF00) | v);
  return v;
}
static bool cmp8(HmRegs *r, uint8_t v, uint8_t k) { r->c = v >= k; return v == k; }

#define WEATHER   0x0196       /* DB: */
#define FLAGS_5A  0x7F1F5A
#define FLAGS_64  0x7F1F64
#define FLAGS_66  0x7F1F66
#define FLAGS_68  0x7F1F68
#define FLAGS_6A  0x7F1F6A
#define FLAGS_6C  0x7F1F6C
#define FLAGS_6E  0x7F1F6E
#define POWER_BERRIES 0x7F1F36
#define YEAR      0x7F1F18
#define SEASON    0x7F1F19
#define DAY       0x7F1F1B
#define COWS      0x7F1F0A

/* ─────────────────────────────────────────────────────────────────────────
 * ReplaceTilesFarm ($81:AAB6, maps 0-3): what you built or found.
 *   $7F1F64 bit 1 / bit 0: two things by the farm (changes $60, $5F);
 *   bit 7 the house's second extension ($54), else bit 6 the first ($53);
 *   one power berry flower ($16) for each berry eaten ($7F1F36), at the
 *   places of $81:AC49 (x, y words; 10 places);
 *   $7F1F66 bits 9-12: the house painted ($59, $58, $5A, $5C, four parts);
 *   $7F1F68 bit 12: $F5.
 * ───────────────────────────────────────────────────────────────────────── */
#define POWER_FLOWER_PLACES 0x81AC49

void hm_ReplaceTilesFarm(uint8_t *ram, HmRegs *r)
{
  if (flag(ram, r, FLAGS_64, 0x0002)) stamp_typed(ram, r, 0x60, 0x0310, 0x0360, 0xAAD0, 0xAAD9);
  if (flag(ram, r, FLAGS_64, 0x0001)) stamp_typed(ram, r, 0x5F, 0x0240, 0x02F0, 0xAAF4, 0xAAFD);
  if (flag(ram, r, FLAGS_64, 0x0080))      stamp_typed(ram, r, 0x54, 0x0060, 0x0100, 0xAB1A, 0xAB23);
  else if (flag(ram, r, FLAGS_64, 0x0040)) stamp_typed(ram, r, 0x53, 0x0060, 0x0100, 0xAB42, 0xAB4B);

  if (get8(ram, r, POWER_BERRIES)) {                    /* 8-bit A from here */
    uint8_t n = 0;
    do {
      hm_push8(ram, r, n);                                /* PHA */
      uint16_t at = (uint16_t)(n << 2);                   /* XBA; LDA #0; XBA; ASL; ASL */
      uint16_t x = rd16(ram, POWER_FLOWER_PLACES + at);
      uint16_t y = rd16(ram, POWER_FLOWER_PLACES + at + 2);
      stamp_typed(ram, r, 0x16, x, y, 0xAB7C, 0xAB85);
      n = hm_pull8(ram, r);                               /* PLA */
      r->c = n == 0xFF;                                   /* CLC; ADC #1 */
      n++;
      r->a = (uint16_t)((r->a & 0xFF00) | n);
    } while (!cmp8(r, n, rd8(ram, POWER_BERRIES)));
  }

  if (flag(ram, r, FLAGS_66, 0x0200)) stamp_typed(ram, r, 0x59, 0x0080, 0x0130, 0xABAE, 0xABB7);
  if (flag(ram, r, FLAGS_66, 0x0400)) stamp_typed(ram, r, 0x58, 0x0060, 0x0130, 0xABD2, 0xABDB);
  if (flag(ram, r, FLAGS_66, 0x0800)) stamp_typed(ram, r, 0x5A, 0x0090, 0x0130, 0xABF6, 0xABFF);
  if (flag(ram, r, FLAGS_66, 0x1000)) stamp_typed(ram, r, 0x5C, 0x00B0, 0x0130, 0xAC1A, 0xAC23);
  if (flag(ram, r, FLAGS_68, 0x1000)) stamp_typed(ram, r, 0xF5, 0x0120, 0x0140, 0xAC3E, 0xAC47);
}

/* ─────────────────────────────────────────────────────────────────────────
 * ReplaceTilesMoutain ($81:AC80, maps $10-$13; HM-Decomp says $81:AC7F,
 * which is the RTS of ReplaceTilesForkWinter): with rain or snow, $E2 at
 * ($180, $20); then, if $7F1F64 bit 1, $E1 at ($170, $270) with its types.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_ReplaceTilesMoutain(uint8_t *ram, HmRegs *r)
{
  if (flag(ram, r, db_(r, WEATHER), 0x0002) || flag(ram, r, db_(r, WEATHER), 0x0008))
    stamp(ram, r, 0xE2, 0x0180, 0x0020, 0xACC5);
  if (flag(ram, r, FLAGS_64, 0x0002)) stamp_typed(ram, r, 0xE1, 0x0170, 0x0270, 0xACAC, 0xACB5);
}

/* ─────────────────────────────────────────────────────────────────────────
 * ReplaceTilesHouse1 / 2 / 3 ($81:ACCC / AD23 / AD7A, maps $15-$17, the
 * house at each size): $0196 bit 4 → $EE at ($70, $C0); $7F1F6C bit 12 →
 * $FD and the tile type $D7 under it; $7F1F6E bit 14 (the clock) → $FE.
 * Houses 1 and 2 put them at the same places, house 3 elsewhere.
 * ───────────────────────────────────────────────────────────────────────── */
typedef struct { uint16_t shell_x, shell_y, clock_x, clock_y, ret[4]; } House;

static void house(uint8_t *ram, HmRegs *r, const House *h)
{
  if (flag(ram, r, db_(r, WEATHER), 0x0010)) stamp(ram, r, 0xEE, 0x0070, 0x00C0, h->ret[0]);
  if (flag(ram, r, FLAGS_6C, 0x1000)) {
    stamp(ram, r, 0xFD, h->shell_x, h->shell_y, h->ret[1]);
    r->a = 0xD7; r->x = h->shell_x; r->y = (uint16_t)(h->shell_y + 0x10);
    jsl(ram, r, h->ret[2], 0x82, hm_EditTileonMap);
  }
  if (flag(ram, r, FLAGS_6E, 0x4000)) stamp(ram, r, 0xFE, h->clock_x, h->clock_y, h->ret[3]);
}
void hm_ReplaceTilesHouse1(uint8_t *ram, HmRegs *r)
{
  static const House h = { 0x0090, 0x0040, 0x00A0, 0x0030, { 0xACE2, 0xACFA, 0xAD09, 0xAD21 } };
  house(ram, r, &h);
}
void hm_ReplaceTilesHouse2(uint8_t *ram, HmRegs *r)
{
  static const House h = { 0x0090, 0x0040, 0x00A0, 0x0030, { 0xAD39, 0xAD51, 0xAD60, 0xAD78 } };
  house(ram, r, &h);
}
void hm_ReplaceTilesHouse3(uint8_t *ram, HmRegs *r)
{
  static const House h = { 0x0140, 0x0050, 0x0150, 0x0040, { 0xAD90, 0xADA8, 0xADB7, 0xADCF } };
  house(ram, r, &h);
}

/* ─────────────────────────────────────────────────────────────────────────
 * ReplaceTilesFlowerShop ($81:ADD5, map $1C): only if $7F1F68 bit 0 (the
 * shop is open?). The seeds on the shelves, list 1: in summer items 1-2,
 * in spring 3-4, and in both item 5 always and item 6 if not bought.
 * Quirk: the loops do TYA; CLC; ADC #5 and then TYA again, so the sum is
 * thrown away (item = Y + 1).
 * ───────────────────────────────────────────────────────────────────────── */
static void seed_shelf(uint8_t *ram, HmRegs *r, uint16_t from, uint16_t to, uint16_t ret)
{
  for (uint16_t y = from; y != to; y++) {
    push16(ram, r, y);                                    /* PHY */
    r->c = 0;                                             /* (Y + 5 < $10000) */
    item(ram, r, (uint16_t)(y + 1), 1, 2, ret);
    pull16(ram, r);                                       /* PLY; INY */
  }
  r->y = to;
  r->c = 1;                                               /* CPY */
}
void hm_ReplaceTilesFlowerShop(uint8_t *ram, HmRegs *r)
{
  if (!flag(ram, r, FLAGS_68, 0x0001)) return;
  uint8_t season = get8(ram, r, SEASON);
  if (cmp8(r, season, 1)) seed_shelf(ram, r, 0, 2, 0xAE02);
  season = get8(ram, r, SEASON);
  if (season == 0) seed_shelf(ram, r, 2, 4, 0xAE2B);
  season = get8(ram, r, SEASON);
  r->c = season >= 2;
  if (season < 2) item(ram, r, 5, 1, 2, 0xAE4E);
  season = get8(ram, r, SEASON);
  r->c = season >= 2;
  if (season < 2) item(ram, r, 6, 1, 0, 0xAE68);
}

/* ─────────────────────────────────────────────────────────────────────────
 * ReplaceTilesGeneralStore ($81:AE6F, map $22): only if $7F1F68 bit 0.
 * The tools for sale (list 2), each only if not bought yet and not in your
 * hand or bag: 1 the paint ($0D, once the house has its second extension),
 * 2 the milker ($0E), 3 the brush ($0F), 4-7 the gold sickle, hoe, hammer
 * and axe ($11-$14, once $7F1F6A bits 0-3 are set), 8 the sprinkler ($15:
 * from summer day 20 of the first year on).
 * ───────────────────────────────────────────────────────────────────────── */
void hm_ReplaceTilesGeneralStore(uint8_t *ram, HmRegs *r)
{
  if (!flag(ram, r, FLAGS_68, 0x0001)) return;
  if (!carrying(ram, r, 0x0D) && flag(ram, r, FLAGS_64, 0x0080)) item(ram, r, 1, 2, 0, 0xAEA5);
  if (!carrying(ram, r, 0x0E)) item(ram, r, 2, 2, 0, 0xAEC3);
  if (!carrying(ram, r, 0x0F)) item(ram, r, 3, 2, 0, 0xAEE1);
  if (!carrying(ram, r, 0x11) && flag(ram, r, FLAGS_6A, 0x0001)) item(ram, r, 4, 2, 0, 0xAF0A);
  if (!carrying(ram, r, 0x12) && flag(ram, r, FLAGS_6A, 0x0002)) item(ram, r, 5, 2, 0, 0xAF33);
  if (!carrying(ram, r, 0x13) && flag(ram, r, FLAGS_6A, 0x0004)) item(ram, r, 6, 2, 0, 0xAF5C);
  if (!carrying(ram, r, 0x14) && flag(ram, r, FLAGS_6A, 0x0008)) item(ram, r, 7, 2, 0, 0xAF85);
  if (carrying(ram, r, 0x15)) return;
  cmp8(r, get8(ram, r, YEAR), 1);
  if (!r->c) {                                            /* the first year */
    uint8_t season = get8(ram, r, SEASON);
    if (season == 0) return;                              /* spring: not yet */
    cmp8(r, season, 2);
    if (!r->c) {                                          /* summer: from day 20 */
      cmp8(r, get8(ram, r, DAY), 0x14);
      if (!r->c) return;
    }                                                     /* fall, winter: yes */
  }
  item(ram, r, 8, 2, 0, 0xAFBD);
}

/* ─────────────────────────────────────────────────────────────────────────
 * ReplaceTilesAnimalShop ($81:AFC0, map $24): only if $7F1F68 bit 0.
 * List 3, each if not bought and not carried: 1 the miracle potion ($0A:
 * only if no cow is pregnant, $7F1F64 bit 2 clear, $7F1F6E bit 12 set and
 * fewer than 12 cows), 2 the medicine ($09), 3 / 4 the chicken / cow
 * fodder ($19 / $1A).
 * ───────────────────────────────────────────────────────────────────────── */
void hm_ReplaceTilesAnimalShop(uint8_t *ram, HmRegs *r)
{
  if (!flag(ram, r, FLAGS_68, 0x0001)) return;
  if (!flag(ram, r, FLAGS_64, 0x0004) && flag(ram, r, FLAGS_6E, 0x1000) &&
      !cmp8(r, get8(ram, r, COWS), 0x0C) && !carrying(ram, r, 0x0A))
    item(ram, r, 1, 3, 0, 0xB007);
  if (!carrying(ram, r, 0x09)) item(ram, r, 2, 3, 0, 0xB025);
  if (!carrying(ram, r, 0x19)) item(ram, r, 3, 3, 0, 0xB043);
  if (!carrying(ram, r, 0x1A)) item(ram, r, 4, 3, 0, 0xB061);
}

/* ─────────────────────────────────────────────────────────────────────────
 * ReplaceTilesToolshed ($81:B064, map $26): the 24 items of list 0 you
 * own, on the wall; and, if $7F1F68 bit 13, $F0 at ($70, $20) with its
 * types.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_ReplaceTilesToolshed(uint8_t *ram, HmRegs *r)
{
  for (uint16_t y = 0; y != 0x18; y++) {
    push16(ram, r, y);                                    /* PHY */
    item(ram, r, (uint16_t)(y + 1), 0, 1, 0xB076);
    pull16(ram, r);
  }
  r->y = 0x18;
  r->c = 1;
  if (flag(ram, r, FLAGS_68, 0x2000)) stamp_typed(ram, r, 0xF0, 0x0070, 0x0020, 0xB09D, 0xB0A6);
}

/* ─────────────────────────────────────────────────────────────────────────
 * ReplaceTilesBarn ($81:B0A8, map $27) and the coop ($81:B121, map $28):
 * item $1A / $19 of list 0 if owned, then fodder ($99) in every trough with
 * food ($0932 / $0934, one bit per trough, the bits of $82:A571), at the
 * places $81:B0ED / $81:B19F (13 / 12 troughs). The coop also shows an egg
 * ($F1) by each hen that laid one ($7F1F45, the bits of $81:BF97), at the
 * hens' places in the coop ($83:CA10).
 * ───────────────────────────────────────────────────────────────────────── */
#define TROUGH_BITS   0x82A571
#define HEN_BITS      0x81BF97
#define HEN_PLACES    0x83CA10

static void by_bits(uint8_t *ram, HmRegs *r, uint16_t n, uint32_t bits, uint32_t food,
                    uint32_t places, uint16_t change, uint16_t ret)
{
  for (uint16_t y = 0; y != n; y++) {
    push16(ram, r, y);                                    /* PHY */
    r->x = (uint16_t)(y << 1);
    if (flag(ram, r, food, rd16(ram, bits + r->x))) {
      r->x = (uint16_t)(y << 2);
      uint16_t px = rd16(ram, places + r->x);
      push16(ram, r, px);
      r->x = (uint16_t)(r->x + 2);
      uint16_t py = rd16(ram, places + r->x);
      push16(ram, r, py);
      r->a = change;
      r->y = pull16(ram, r); r->x = pull16(ram, r);
      jsl(ram, r, ret, 0x81, hm_SUB_81A7CE);
    }
    pull16(ram, r);                                       /* PLY; INY */
  }
  r->y = n;
  r->c = 1;
}
void hm_ReplaceTilesBarn(uint8_t *ram, HmRegs *r)
{
  item(ram, r, 0x1A, 0, 1, 0xB0B5);
  by_bits(ram, r, 13, TROUGH_BITS, db_(r, 0x0932), 0x81B0ED, 0x99, 0xB0E2);
}
void hm_ReplaceTilesCoop(uint8_t *ram, HmRegs *r)
{
  item(ram, r, 0x19, 0, 1, 0xB12E);
  by_bits(ram, r, 12, TROUGH_BITS, db_(r, 0x0934), 0x81B19F, 0x99, 0xB15D);
  by_bits(ram, r, 13, HEN_BITS, 0x7F1F45, HEN_PLACES, 0xF1, 0xB194);
}

/* ─────────────────────────────────────────────────────────────────────────
 * The mountain cave ($81:B1CF, map $29): if $7F1F6C bit 0 or 4, $E4 at
 * three places. The elf tunnel ($81:B21E, map $2A): if $7F1F64 bit 1, $EF
 * twice with its types. Map $37 ($81:B26D): the first time ($7F1F5A bit 0,
 * then set), $9B at ($80, $110).
 * ───────────────────────────────────────────────────────────────────────── */
void hm_ReplaceTilesMountainCave(uint8_t *ram, HmRegs *r)
{
  if (!flag(ram, r, FLAGS_6C, 0x0011)) return;
  stamp_kept(ram, r, 0xE4, 0x0070, 0x0160, 0xB1E9);
  stamp_kept(ram, r, 0xE4, 0x0070, 0x0130, 0xB200);
  stamp_kept(ram, r, 0xE4, 0x0060, 0x0170, 0xB217);
}
void hm_ReplaceTilesElfTunnel(uint8_t *ram, HmRegs *r)
{
  if (!flag(ram, r, FLAGS_64, 0x0002)) return;
  stamp_typed(ram, r, 0xEF, 0x0030, 0x0180, 0xB23B, 0xB244);
  stamp_typed(ram, r, 0xEF, 0x0020, 0x01B0, 0xB256, 0xB25F);
}
void hm_ReplaceTilesMap37(uint8_t *ram, HmRegs *r)
{
  if (flag(ram, r, FLAGS_5A, 0x0001)) return;
  r->a = (uint16_t)(rd16(ram, FLAGS_5A) | 0x0001);
  hm_bus_wr16(ram, FLAGS_5A, r->a);
  stamp(ram, r, 0x9B, 0x0080, 0x0110, 0xB291);
}

/* The maps with nothing to add: a lone RTS each. */
void hm_ReplaceTilesNothing(uint8_t *ram, HmRegs *r) { (void)ram; (void)r; }

/* ─────────────────────────────────────────────────────────────────────────
 * UNK_ExecuteFromPointers ($81:A9E5, JSL, RTL): the routine of map $22
 * (maps $00-$5F), called with JSR (A9F6,X).
 * ───────────────────────────────────────────────────────────────────────── */
#define MAP_ROUTINES  0x81A9F6
#define RET_9E5_CALL  0xA9F4

void hm_UNK_ExecuteFromPointers(uint8_t *ram, HmRegs *r)
{
  r->a = *hm_dp(ram, r, 0x22);
  r->c = (r->a >> 15) & 1;                                /* ASL */
  r->a = (uint16_t)(r->a << 1);
  r->x = r->a;
  uint16_t target = rd16(ram, MAP_ROUTINES + r->x);
  void (*fn)(uint8_t *, HmRegs *);
  switch (target) {
    case 0xAAB6: fn = hm_ReplaceTilesFarm; break;
    case 0xAC80: fn = hm_ReplaceTilesMoutain; break;
    case 0xACCC: fn = hm_ReplaceTilesHouse1; break;
    case 0xAD23: fn = hm_ReplaceTilesHouse2; break;
    case 0xAD7A: fn = hm_ReplaceTilesHouse3; break;
    case 0xADD5: fn = hm_ReplaceTilesFlowerShop; break;
    case 0xAE6F: fn = hm_ReplaceTilesGeneralStore; break;
    case 0xAFC0: fn = hm_ReplaceTilesAnimalShop; break;
    case 0xB064: fn = hm_ReplaceTilesToolshed; break;
    case 0xB0A8: fn = hm_ReplaceTilesBarn; break;
    case 0xB121: fn = hm_ReplaceTilesCoop; break;
    case 0xB1CF: fn = hm_ReplaceTilesMountainCave; break;
    case 0xB21E: fn = hm_ReplaceTilesElfTunnel; break;
    case 0xB26D: fn = hm_ReplaceTilesMap37; break;
    default:     fn = hm_ReplaceTilesNothing; break;  /* a lone RTS */
  }
  jsr(ram, r, RET_9E5_CALL, fn);
}
