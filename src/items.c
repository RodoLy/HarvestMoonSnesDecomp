/*
 * items.c — the things the player puts down or throws (items as map
 * objects).
 *
 * An item that leaves the player's hands becomes a map object (see
 * objects.c, BAAAA): its record keeps the item ($0984 while it is worked
 * on), a state ($0976) and its sprite. SUB_818000 puts it down; then, each
 * frame, SUB_81809A runs the item's own routine (table DATA8_8197C0, one
 * per item), which acts by the state: lands, breaks, is eaten...
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

#define ITEM_SPRITES     0x8196AF   /* DATA8_8196AF: per item, a pointer to its sprites */
#define TILE_CHANGES     0x81A2AD   /* DATA8_81A2AD: per item, the tile it leaves (0: none) */
#define ITEM_SOUNDS      0x81A308   /* DATA8_81A308: per item, whether it makes a sound */
#define ITEM             0x0984     /* DB: the item being worked on */

static uint32_t db(const HmRegs *r, uint16_t a) { return (uint32_t)r->db << 16 | a; }

/* ─────────────────────────────────────────────────────────────────────────
 * SUB_8180B7 ($81:80B7, JSL, RTL; the start of HM-Decomp's SUB_8180B7
 * block): the sprite of item $0984 for the case Y (a facing, + 2 when put
 * down): DATA8_8196AF gives the item's list (3 bytes per item), and in it
 * 3 bytes per case: the sprite (to $097A) and a byte that x 64 is the flip
 * ($097E).
 * Leftovers: $72-$74 = the list, $7E = Y, A = the flip, Y = 3 Y + 2,
 * X = 3 item + 2, C = 0.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_SUB_8180B7(uint8_t *ram, HmRegs *r)
{
  uint16_t item = hm_bus_rd8(ram, db(r, ITEM));
  hm_dp_w16(ram, r, 0x7E, item);
  r->x = (uint16_t)(item * 3);                             /* ASL; CLC; ADC (no carry) */
  hm_dp_w16(ram, r, 0x72, hm_bus_rd16(ram, ITEM_SPRITES + r->x));
  r->x = (uint16_t)(r->x + 2);
  uint8_t bank = hm_bus_rd8(ram, ITEM_SPRITES + r->x);
  *hm_dp(ram, r, 0x74) = bank;
  uint16_t which = r->y;
  hm_dp_w16(ram, r, 0x7E, which);
  uint32_t y3 = (uint32_t)(uint16_t)(which << 1) + which;  /* ASL; CLC; ADC $7E */
  r->y = (uint16_t)y3;
  hm_bus_wr16(ram, db(r, 0x097A), hm_bus_rd16(ram, hm_dp_long(ram, r, 0x72, r->y)));
  r->y = (uint16_t)(r->y + 2);
  uint16_t flip = (uint16_t)(hm_bus_rd8(ram, hm_dp_long(ram, r, 0x72, r->y)) << 6);
  r->c = 0;                                                /* the last ASL */
  r->a = flip;
  hm_bus_wr16(ram, db(r, 0x097E), flip);
}

/* ─────────────────────────────────────────────────────────────────────────
 * SUB_818000 ($81:8000, JSL, RTL): the item in hand ($091D) is put down
 * in front of the player: a map object in state 0 ($0974 = $0975 = 1,
 * $0976 = 0) with the item's sprite for the player's facing (+ 2), two
 * pixels below the player (SpawnMapObject). Then the tile in front, and if
 * the item leaves something on the map (DATA8_81A2AD), that tile type
 * (from the map's property table) is put there; and if the item makes a
 * sound (DATA8_81A308), sound effect 8 of bank 6.
 * Leftovers: those of the last routine called.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_SUB_818000(uint8_t *ram, HmRegs *r)
{
  hm_bus_wr8(ram, db(r, 0x0974), 1);
  hm_bus_wr8(ram, db(r, 0x0975), 1);
  hm_bus_wr8(ram, db(r, 0x0976), 0);
  uint8_t item = hm_bus_rd8(ram, db(r, 0x091D));
  hm_bus_wr8(ram, db(r, ITEM), item);
  uint32_t sum = 2u + hm_dp_r16(ram, r, 0xDA);
  r->c = sum > 0xFFFF;
  r->a = (uint16_t)sum;
  r->y = r->a;
  hm_jsl(ram, r, 0x8025, 0x81);
  hm_SUB_8180B7(ram, r);
  hm_rtl(ram, r);
  r->a = hm_dp_r16(ram, r, 0xD6);
  hm_bus_wr16(ram, db(r, 0x0980), r->a);
  r->a = (uint16_t)(hm_dp_r16(ram, r, 0xD8) + 2);
  hm_bus_wr16(ram, db(r, 0x0982), r->a);
  hm_jsl(ram, r, 0x8037, 0x81);
  hm_SpawnMapObject(ram, r);
  hm_rtl(ram, r);
  r->a = 1; r->x = 0; r->y = 0;
  hm_jsl(ram, r, 0x8046, 0x81);
  hm_CalculateTileinFront(ram, r);
  hm_rtl(ram, r);

  uint16_t it = hm_bus_rd8(ram, db(r, ITEM));
  r->x = it;
  uint8_t change = hm_bus_rd8(ram, TILE_CHANGES + it);
  r->a = (uint16_t)((it & 0xFF00) | change);
  if (change) {
    r->c = (change >> 6) & 1;                              /* ASL; ASL (16 bits) */
    r->y = (uint16_t)(change << 2);
    r->a = hm_bus_rd8(ram, hm_dp_long(ram, r, 0x0D, r->y)); /* LDA #0; XBA; LDA [$0D],Y */
    r->x = hm_bus_rd16(ram, db(r, 0x0985));
    r->y = hm_bus_rd16(ram, db(r, 0x0987));
    hm_jsl(ram, r, 0x8071, 0x81);
    hm_ChangeMapTile(ram, r);
    hm_rtl(ram, r);
  }
  it = hm_bus_rd8(ram, db(r, ITEM));
  r->x = it;
  uint8_t sound = hm_bus_rd8(ram, ITEM_SOUNDS + it);
  r->a = (uint16_t)((it & 0xFF00) | sound);
  if (!sound) return;
  r->a = (uint16_t)((r->a & 0xFF00) | 0x06);
  hm_bus_wr8(ram, db(r, 0x0114), 0x08);
  hm_bus_wr8(ram, db(r, 0x0115), 0x06);
  hm_jsl(ram, r, 0x8098, 0x83);
  hm_UNK_Audio19(ram, r);
  hm_rtl(ram, r);
}
