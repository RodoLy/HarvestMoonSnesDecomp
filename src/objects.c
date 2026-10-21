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

/* ─────────────────────────────────────────────────────────────────────────
 * LoadGameObjectComponents ($85:8AE5, JSR, RTS; CODE_858AE5 in HM-Decomp).
 * For the game object at offset $A9: reads its piece list ($78-$7A = the
 * list, bank $86/$87 already in $7A), stores the count at +13, and loads
 * every piece (LoadSpriteComponent), writing its slot at +14 + n.
 * Leftovers: $AD = 0, $AF = the byte after the last piece, $B3/$B5 of the
 * last piece; A = 0, X = $A9, Y = 1 + 5 pieces, C from the last ADC.
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_AE5_PIECE 0x8B1E   /* JSR LoadSpriteComponent at $85:8B1C */

void hm_LoadGameObjectComponents(uint8_t *ram, HmRegs *r)
{
  uint16_t obj = hm_dp_r16(ram, r, 0xA9);
  hm_dp_w16(ram, r, 0x78, hm_bus_rd16(ram, abs_x(r, GOBJ + 0x10, obj)));
  uint8_t pieces = hm_bus_rd8(ram, hm_dp_long(ram, r, 0x78, 0));
  *hm_dp(ram, r, 0xAD) = pieces;
  hm_bus_wr8(ram, abs_x(r, GOBJ + 0x13, obj), pieces);
  *hm_dp(ram, r, 0xAE) = 0;
  uint16_t first = (uint16_t)(obj + GOBJ);               /* CLC; ADC #$019C */
  r->c = (uint32_t)first + 0x14 > 0xFFFF;                /* CLC; ADC #$0014 */
  hm_dp_w16(ram, r, 0xAF, (uint16_t)(first + 0x14));

  uint16_t x = obj, y = 1;
  while (hm_dp_r16(ram, r, 0xAD) != 0) {
    hm_dp_w16(ram, r, 0xB3, hm_bus_rd16(ram, hm_dp_long(ram, r, 0x78, y)));
    hm_dp_w16(ram, r, 0xB5, hm_dp_r16(ram, r, 0xAF));
    push16(ram, r, x);                                   /* PHX; PHY */
    push16(ram, r, y);
    push16(ram, r, RET_AE5_PIECE);                       /* JSR */
    hm_LoadSpriteComponent(ram, r);
    pull16(ram, r);                                      /* RTS */
    y = pull16(ram, r);                                  /* PLY; PLX */
    x = pull16(ram, r);
    hm_bus_wr8(ram, abs_x(r, GOBJ + 0x14, x), (uint8_t)hm_dp_r16(ram, r, 0xB3));
    x++;
    hm_dp_w16(ram, r, 0xAF, (uint16_t)(hm_dp_r16(ram, r, 0xAF) + 1));
    r->c = (uint32_t)y + 5 > 0xFFFF;
    y = (uint16_t)(y + 5);
    hm_dp_w16(ram, r, 0xAD, (uint16_t)(hm_dp_r16(ram, r, 0xAD) - 1));
  }
  r->a = 0;
  r->x = hm_dp_r16(ram, r, 0xA9);
  r->y = y;
}

/* ─────────────────────────────────────────────────────────────────────────
 * CreateGameObject ($85:8000, JSL, RTL; CODE_858000 in HM-Decomp). Takes a
 * free game object (one more of the 40 if not all are handed out yet, then
 * the first free one) and fills it from $A1 (sprite), $9F, $A3, $9B/$9D;
 * looks up the sprite's entry and loads its pieces.
 * $A7 = 0 and $A5 / $A9 = the object; or $A7 = $FFFF if none is free.
 * Leftovers: $75 the sprite's entry, $77 / $7A its bank. Created: as
 * LoadGameObjectComponents, then A = 0. No room: A = $FFFF, X = $24 n,
 * Y = n (n = $DC), C = 1.
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_8000_PIECES 0x80B0   /* JSR LoadGameObjectComponents at $85:80AE */

void hm_CreateGameObject(uint8_t *ram, HmRegs *r)
{
  uint16_t sprite = hm_dp_r16(ram, r, 0xA1);
  uint8_t bank = sprite >= FIRST_SPRITE_87 ? 0x87 : 0x86;
  *hm_dp(ram, r, 0x77) = bank;
  *hm_dp(ram, r, 0x7A) = bank;

  uint16_t handed_out = hm_dp_r16(ram, r, 0xDC);
  if (handed_out != GOBJ_MAX) {
    handed_out++;
    hm_dp_w16(ram, r, 0xDC, handed_out);
  }
  uint16_t x = 0, y = 0;
  while (hm_bus_rd16(ram, abs_x(r, GOBJ, x)) != 0) {
    x = (uint16_t)(x + GOBJ_SIZE);
    y++;
    if (y == hm_dp_r16(ram, r, 0xDC)) {                  /* none free */
      r->c = 1;                                          /* CPY: equal */
      r->a = 0xFFFF;
      hm_dp_w16(ram, r, 0xA7, 0xFFFF);
      r->x = x; r->y = y;
      return;
    }
  }

  hm_dp_w16(ram, r, 0xA5, y);
  hm_bus_wr16(ram, abs_x(r, GOBJ + 0x00, x), GOBJ_IN_USE);
  hm_bus_wr16(ram, abs_x(r, GOBJ + 0x02, x), sprite);
  hm_bus_wr16(ram, abs_x(r, GOBJ + 0x04, x), hm_dp_r16(ram, r, 0x9F));
  hm_bus_wr16(ram, abs_x(r, GOBJ + 0x06, x), hm_dp_r16(ram, r, 0xA3));
  hm_bus_wr16(ram, abs_x(r, GOBJ + 0x08, x), hm_dp_r16(ram, r, 0x9B));
  hm_bus_wr16(ram, abs_x(r, GOBJ + 0x0A, x), hm_dp_r16(ram, r, 0x9D));
  hm_dp_w16(ram, r, 0xA9, x);

  uint16_t number = hm_bus_rd16(ram, abs_x(r, GOBJ + 0x02, x));
  uint32_t table = SPRITES_86;
  if (hm_dp_r16(ram, r, 0xA1) >= FIRST_SPRITE_87) {
    number = (uint16_t)(number - FIRST_SPRITE_87);
    table = SPRITES_87;
  }
  uint16_t entry = hm_bus_rd16(ram, (table + (uint16_t)(number << 1)) & 0xFFFFFF);
  hm_dp_w16(ram, r, 0x75, entry);
  hm_bus_wr16(ram, abs_x(r, GOBJ + 0x0C, x), entry);
  hm_bus_wr16(ram, abs_x(r, GOBJ + 0x0E, x), (uint16_t)(entry + 3));
  hm_bus_wr16(ram, abs_x(r, GOBJ + 0x10, x), hm_bus_rd16(ram, hm_dp_long(ram, r, 0x75, 0)));
  hm_bus_wr8(ram, abs_x(r, GOBJ + 0x12, x), hm_bus_rd8(ram, hm_dp_long(ram, r, 0x75, 2)));

  r->x = x; r->y = 2;
  push16(ram, r, RET_8000_PIECES);                       /* JSR */
  hm_LoadGameObjectComponents(ram, r);
  pull16(ram, r);                                        /* RTS */
  r->a = 0;
  hm_dp_w16(ram, r, 0xA7, 0);
}

/* ─────────────────────────────────────────────────────────────────────────
 * SpawnMapObject ($81:A500, JSL, RTL; CODE_81A500 in HM-Decomp). Takes the
 * first free map object ($07-$09 = it) and fills it from $0974-$0976,
 * $0984 (flags, routine, +2, +3) and $097A (sprite), $097E (flip),
 * $0980/$0982 (X/Y), then creates its game object and keeps its number.
 * Notes:
 *   - With all 10 map objects in use the game hangs: `BRA` to itself at
 *     $81:A528 (the rewrite just returns; the fuzz always leaves one free).
 *   - If there is no free game object, +4 keeps whatever $A5 held (the
 *     last object created), since CreateGameObject leaves $A5 alone.
 * Leftovers: A = $A5, Y = 4, X and C from CreateGameObject.
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_A500_CREATE 0xA580   /* JSL CreateGameObject at $81:A57D */

void hm_SpawnMapObject(uint8_t *ram, HmRegs *r)
{
  hm_dp_w16(ram, r, 0x07, (uint16_t)MAP_OBJECTS);
  *hm_dp(ram, r, 0x09) = (uint8_t)(MAP_OBJECTS >> 16);
  uint16_t n = 0;
  while (hm_bus_rd8(ram, hm_dp_long(ram, r, 0x07, 0)) & 1) {
    hm_dp_w16(ram, r, 0x07, (uint16_t)(hm_dp_r16(ram, r, 0x07) + MAP_OBJECT_SIZE));
    if (++n == MAP_OBJECTS_MAX) return;                  /* the original hangs */
  }

  hm_bus_wr8(ram, hm_dp_long(ram, r, 0x07, 0), hm_bus_rd8(ram, abs_db(r, 0x0974)));
  hm_bus_wr8(ram, hm_dp_long(ram, r, 0x07, 1), hm_bus_rd8(ram, abs_db(r, 0x0975)));
  hm_bus_wr8(ram, hm_dp_long(ram, r, 0x07, 2), hm_bus_rd8(ram, abs_db(r, 0x0976)));
  hm_bus_wr8(ram, hm_dp_long(ram, r, 0x07, 3), hm_bus_rd8(ram, abs_db(r, 0x0984)));
  uint16_t sprite = hm_bus_rd16(ram, abs_db(r, 0x097A));
  hm_bus_wr16(ram, hm_dp_long(ram, r, 0x07, 0x6), sprite);
  hm_bus_wr16(ram, hm_dp_long(ram, r, 0x07, 0x8), sprite);
  hm_dp_w16(ram, r, 0xA1, sprite);
  uint16_t flip = hm_bus_rd16(ram, abs_db(r, 0x097E));
  hm_bus_wr16(ram, hm_dp_long(ram, r, 0x07, 0xA), flip);
  hm_dp_w16(ram, r, 0x9F, flip);
  uint16_t px = hm_bus_rd16(ram, abs_db(r, 0x0980));
  hm_bus_wr16(ram, hm_dp_long(ram, r, 0x07, 0xC), px);
  hm_dp_w16(ram, r, 0x9B, px);
  uint16_t py = hm_bus_rd16(ram, abs_db(r, 0x0982));
  hm_bus_wr16(ram, hm_dp_long(ram, r, 0x07, 0xE), py);
  hm_dp_w16(ram, r, 0x9D, py);
  hm_dp_w16(ram, r, 0xA3, 0);

  r->a = py; r->x = n; r->y = 0x0E;
  hm_jsl(ram, r, RET_A500_CREATE, 0x85);
  hm_CreateGameObject(ram, r);
  hm_rtl(ram, r);
  r->y = 4;
  r->a = hm_dp_r16(ram, r, 0xA5);
  hm_bus_wr16(ram, hm_dp_long(ram, r, 0x07, 4), r->a);
}
