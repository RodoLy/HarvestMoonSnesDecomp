/*
 * objects.c — objects with a sprite: the "game objects" the screen is made
 * of (people, animals, the tools' effects...) and the "map objects" the
 * tools leave on the map (cut grass, a splash...).
 *
 * Game objects ("gobj" in HM-Decomp): up to 40 records of $24 bytes at
 * $019C (data bank):
 *   +00 $7777 = in use, 0 = free     +02 sprite number    +04 flip
 *   +06 (copied from $A3)            +08 X                +0A Y
 *   +0C entry of the sprite table    +0E the same + 3
 *   +10 address of the sprite's piece list   +12 a byte of the sprite entry
 *   +13 number of pieces             +14... one byte per piece: the
 *                                           graphics slot it is in ($FF =
 *                                           queued, not loaded yet)
 * Sprites 0-$261 are described in bank $86 (table $86:8080), $262-$44F in
 * bank $87 (table $87:8080). A sprite's piece list is a count and then 5
 * bytes per piece (the first word is the piece's graphics number).
 *
 * Direct page: inputs $9B/$9D position, $9F flip, $A1 sprite, $A3; outputs
 * $A5 record number, $A7 0 = created / $FFFF = no room, $A9 record offset.
 * $DC counts the records handed out so far (it only grows, up to 40).
 *
 * Graphics of the pieces: $7F:0F00 + piece says in which slot (of the
 * sprite graphics in VRAM) it already is, $FF = not loaded; $7E:A320 +
 * 4 slot counts the pieces using the slot. Pieces not loaded go into a ring
 * queue of 64 entries of 4 bytes at $7E:A220 (piece, address of the byte
 * that will receive the slot): $BD is where the next one goes, $BB where
 * the loader (CODE_858CB2, run at the NMI) takes them out.
 *
 * Map objects: 10 records of 16 bytes at $7E:B4E6:
 *   +0 flags (bit 0 = in use)   +1 its routine (table $81:A58B)   +2, +3
 *   +4 its game object number   +6 sprite   +8 sprite shown now
 *   +A flip   +C X   +E Y
 * BAAAA ($81:A383) moves them every frame.
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

#define GOBJ             0x019C     /* DB: the 40 game objects */
#define GOBJ_SIZE        0x24
#define GOBJ_IN_USE      0x7777
#define GOBJ_MAX         0x28
#define SPRITES_86       0x868080   /* ROM: sprite -> its entry, bank $86 */
#define SPRITES_87       0x878080   /* ROM: the same for sprites $262+ */
#define FIRST_SPRITE_87  0x0262
#define PIECE_SLOT       0x7F0F00   /* piece -> graphics slot ($FF = none) */
#define PIECE_QUEUE      0x7EA220   /* 64 x (piece, where to put the slot) */
#define SLOT_USERS       0x7EA320   /* slot * 4 -> how many pieces use it */
#define MAP_OBJECTS      0x7EB4E6
#define MAP_OBJECT_SIZE  0x10
#define MAP_OBJECTS_MAX  10

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
/* `addr,X` with the data bank: may run on into the next bank */
static uint32_t abs_x(const HmRegs *r, uint16_t addr, uint16_t x)
{
  return (((uint32_t)r->db << 16 | addr) + x) & 0xFFFFFF;
}
static uint32_t abs_db(const HmRegs *r, uint16_t addr)
{
  return (uint32_t)r->db << 16 | addr;
}

/* ─────────────────────────────────────────────────────────────────────────
 * LoadSpriteComponent ($85:8C30, JSR, RTS; CODE_858C30 in HM-Decomp).
 * $B3 = a piece's graphics number, $B5 = where its slot will be written.
 * Already loaded: one more user of its slot; $B3 = A = Y = the slot.
 * Not loaded: queued for the loader; $B3 = A = $FFFF, X = the queue entry.
 * If the queue is full the original waits ("CMP $BB; BEQ" to itself) for
 * the NMI to empty a place; the rewrite cannot wait (the fuzz never fills
 * the queue).
 * C: in the queue case, the new $BD >= $BB; otherwise 0.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_LoadSpriteComponent(uint8_t *ram, HmRegs *r)
{
  uint16_t piece = hm_dp_r16(ram, r, 0xB3);
  uint8_t slot = hm_bus_rd8(ram, (PIECE_SLOT + piece) & 0xFFFFFF);
  r->y = slot;
  if (slot != 0xFF) {
    r->x = (uint16_t)(slot << 2);
    uint32_t users = (SLOT_USERS + r->x) & 0xFFFFFF;
    hm_bus_wr16(ram, users, (uint16_t)(hm_bus_rd16(ram, users) + 1));
    r->a = slot;
    hm_dp_w16(ram, r, 0xB3, slot);
    r->c = 0;                                            /* CPY #$FF: Y < $FF */
    return;
  }
  uint16_t at = hm_dp_r16(ram, r, 0xBD);
  uint16_t next = (uint16_t)(at + 4);
  if (next == 0x0100) next = 0;                          /* 64 entries, then wrap */
  r->c = next >= hm_dp_r16(ram, r, 0xBB);                /* the CMP $BB of the wait */
  hm_dp_w16(ram, r, 0xBD, next);
  r->x = at;
  hm_bus_wr16(ram, (PIECE_QUEUE + at) & 0xFFFFFF, piece);
  hm_bus_wr16(ram, (PIECE_QUEUE + 2 + at) & 0xFFFFFF, hm_dp_r16(ram, r, 0xB5));
  r->a = 0xFFFF;
  hm_dp_w16(ram, r, 0xB3, 0xFFFF);
}

