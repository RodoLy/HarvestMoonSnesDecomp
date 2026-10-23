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

/* ─────────────────────────────────────────────────────────────────────────
 * UNK_PresetsMemory3 ($81:A4C7, JSL, RTL): clears the first byte of the
 * 10 records of 16 bytes at $7E:B4E6 (right before the event slots).
 * Leftovers: $07-$09 = $7E:B586, A = $B586, X = 10, Y = 0, C = 1.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_UNK_PresetsMemory3(uint8_t *ram, HmRegs *r)
{
  hm_dp_w16(ram, r, 0x07, 0xB4E6);
  *hm_dp(ram, r, 0x09) = 0x7E;
  for (uint16_t x = 0; x != 10; x++) {
    hm_bus_wr8(ram, hm_dp_long(ram, r, 0x07, 0), 0);
    hm_dp_w16(ram, r, 0x07, (uint16_t)(hm_dp_r16(ram, r, 0x07) + 0x10));
  }
  r->a = 0xB586;
  r->x = 10;
  r->y = 0;
  r->c = 1;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Destroying a game object.
 *
 * CODE_8581A2 ($85:81A2, JSL, RTL): object $A5 ($A9 = its offset, from
 * Table_GameOBJIndexes at $85:8BE0). If one of its pieces is still waiting
 * in the loader's queue (slot $FF), nothing can be freed yet: $0740 counts
 * one more postponed destruction, $AF = A = $FFFF and it returns. Otherwise
 * every piece gives its graphics slot back, the record is freed (+0 = 0)
 * and A = its +0E. The callers do not retry: the object simply stays.
 *
 * CODE_858B7B ($85:8B7B, JSR, RTS): the check, $AF = 0 (all loaded) or
 * $FFFF. CODE_858B41 ($85:8B41, JSR, RTS): the freeing; each piece's slot
 * byte becomes $FF. CODE_858C9F ($85:8C9F, JSR, RTS): one user less for
 * slot $B3 ($7E:A320 + 4 slot).
 * Bug: CODE_858C9F waits while the slot's count is 0 ("LDA; BEQ" to
 * itself), which would hang the game; a slot in use is never at 0, so it
 * does not happen. The rewrite does not wait (the fuzz never gives 0).
 * Leftovers: see each one; C = 1 at the end of the loops (CPY / CMP).
 * ───────────────────────────────────────────────────────────────────────── */
#define GOBJ_OFFSETS    0x858BE0
#define POSTPONED_FREES 0x0740     /* DB: */

void hm_CODE_858C9F(uint8_t *ram, HmRegs *r)
{
  uint16_t slot = hm_dp_r16(ram, r, 0xB3);
  r->c = (slot >> 14) & 1;                                 /* the second ASL */
  r->x = (uint16_t)(slot << 2);
  uint32_t users = (SLOT_USERS + r->x) & 0xFFFFFF;
  r->a = (uint16_t)(hm_bus_rd16(ram, users) - 1);
  hm_bus_wr16(ram, users, r->a);
}

/* LDA #0; XBA; LDA +13,X (8-bit), TAY: the number of pieces */
static uint16_t pieces_of(uint8_t *ram, HmRegs *r, uint16_t x)
{
  r->a = hm_bus_rd8(ram, abs_x(r, GOBJ + 0x13, x));
  return r->a;
}

void hm_CODE_858B7B(uint8_t *ram, HmRegs *r)
{
  uint16_t x = hm_dp_r16(ram, r, 0xA9);
  uint16_t y = pieces_of(ram, r, x);
  for (;;) {
    r->c = 1;                                              /* CPY #0 */
    if (y == 0) {
      r->a = 0;
      hm_dp_w16(ram, r, 0xAF, 0);
      break;
    }
    uint8_t slot = hm_bus_rd8(ram, abs_x(r, GOBJ + 0x14, x));
    r->a = (uint16_t)((r->a & 0xFF00) | slot);
    r->c = slot == 0xFF;                                   /* CMP #$FF */
    if (slot == 0xFF) {                                    /* still queued */
      uint32_t waits = abs_db(r, POSTPONED_FREES);
      hm_bus_wr16(ram, waits, (uint16_t)(hm_bus_rd16(ram, waits) + 1));
      r->a = 0xFFFF;
      hm_dp_w16(ram, r, 0xAF, 0xFFFF);
      break;
    }
    x++; y--;
  }
  r->x = x; r->y = y;
}

#define RET_B41_SLOT 0x8B6B   /* JSR CODE_858C9F at $85:8B69 */

void hm_CODE_858B41(uint8_t *ram, HmRegs *r)
{
  uint16_t x = hm_dp_r16(ram, r, 0xA9);
  uint16_t y = pieces_of(ram, r, x);
  while (y != 0) {
    uint8_t slot = hm_bus_rd8(ram, abs_x(r, GOBJ + 0x14, x));
    r->a = (uint16_t)((r->a & 0xFF00) | slot);
    if (slot != 0xFF) {
      *hm_dp(ram, r, 0xB3) = slot;
      *hm_dp(ram, r, 0xB4) = 0;
      r->a = (uint16_t)((r->a & 0xFF00) | 0xFF);
      hm_bus_wr8(ram, abs_x(r, GOBJ + 0x14, x), 0xFF);
      push16(ram, r, y); push16(ram, r, x);                /* PHY; PHX */
      push16(ram, r, RET_B41_SLOT);
      hm_CODE_858C9F(ram, r);
      pull16(ram, r);
      x = pull16(ram, r); y = pull16(ram, r);              /* PLX; PLY */
    }
    x++; y--;
  }
  r->c = 1;                                                /* CPY #0 */
  r->x = hm_dp_r16(ram, r, 0xA9);
  r->y = 0;
}

#define RET_1A2_CHECK 0x81B0
#define RET_1A2_FREE  0x81BA

void hm_CODE_8581A2(uint8_t *ram, HmRegs *r)
{
  uint16_t n = hm_dp_r16(ram, r, 0xA5);
  r->c = (n >> 15) & 1;                                    /* ASL */
  r->x = (uint16_t)(n << 1);
  uint16_t obj = hm_bus_rd16(ram, (GOBJ_OFFSETS + r->x) & 0xFFFFFF);
  hm_dp_w16(ram, r, 0xA9, obj);
  push16(ram, r, RET_1A2_CHECK);
  hm_CODE_858B7B(ram, r);
  pull16(ram, r);
  r->a = hm_dp_r16(ram, r, 0xAF);
  if (r->a) return;                                        /* JMP to an RTL */
  push16(ram, r, RET_1A2_FREE);
  hm_CODE_858B41(ram, r);
  pull16(ram, r);
  r->x = hm_dp_r16(ram, r, 0xA9);
  hm_bus_wr16(ram, abs_x(r, GOBJ, r->x), 0);
  r->a = hm_bus_rd16(ram, abs_x(r, GOBJ + 0x0E, r->x));
}

/* ─────────────────────────────────────────────────────────────────────────
 * Changing a game object (both JSL, RTL; $A5 = the object, $A9 = its
 * offset from Table_GameOBJIndexes; $77 / $7A = the bank of sprite $A1's
 * table, $86 or $87).
 *
 * CODE_8580B9 ($85:80B9): moves it: flip $9F, X $9B, Y $9D. $A7 = 0 when
 * done, 1 when the record is not in use (then nothing changes).
 * CODE_858100 ($85:8100): gives it another sprite ($A1, with $9F, $A3,
 * $9B, $9D as CreateGameObject): its pieces are freed (unless one is still
 * queued: then nothing, as CODE_8581A2) and the new sprite's are loaded.
 * Leftovers: A = 0 (or $FFFF / the record's first word), X = the offset,
 * C from the last arithmetic.
 * ───────────────────────────────────────────────────────────────────────── */
static void sprite_bank(uint8_t *ram, HmRegs *r)
{
  uint16_t sprite = hm_dp_r16(ram, r, 0xA1);
  uint8_t bank = sprite >= FIRST_SPRITE_87 ? 0x87 : 0x86;
  r->c = sprite >= FIRST_SPRITE_87;                        /* CMP #$0262 */
  r->a = (uint16_t)((sprite & 0xFF00) | bank);
  *hm_dp(ram, r, 0x77) = bank;
  *hm_dp(ram, r, 0x7A) = bank;
}
static uint16_t object_offset(uint8_t *ram, HmRegs *r)
{
  uint16_t n = hm_dp_r16(ram, r, 0xA5);
  r->c = (n >> 15) & 1;                                    /* ASL */
  r->x = (uint16_t)(n << 1);
  r->a = hm_bus_rd16(ram, (GOBJ_OFFSETS + r->x) & 0xFFFFFF);
  hm_dp_w16(ram, r, 0xA9, r->a);
  return r->a;
}

void hm_CODE_8580B9(uint8_t *ram, HmRegs *r)
{
  sprite_bank(ram, r);
  uint16_t x = object_offset(ram, r);
  r->x = x;
  r->a = 1;
  hm_dp_w16(ram, r, 0xA7, 1);
  r->a = hm_bus_rd16(ram, abs_x(r, GOBJ, x));
  if (!r->a) return;                                       /* not in use */
  hm_bus_wr16(ram, abs_x(r, GOBJ + 0x04, x), hm_dp_r16(ram, r, 0x9F));
  hm_bus_wr16(ram, abs_x(r, GOBJ + 0x08, x), hm_dp_r16(ram, r, 0x9B));
  hm_bus_wr16(ram, abs_x(r, GOBJ + 0x0A, x), hm_dp_r16(ram, r, 0x9D));
  r->a = 0;
  hm_dp_w16(ram, r, 0xA7, 0);
}

#define RET_100_CHECK  0x812B
#define RET_100_FREE   0x8132
#define RET_100_PIECES 0x819B

void hm_CODE_858100(uint8_t *ram, HmRegs *r)
{
  sprite_bank(ram, r);
  object_offset(ram, r);
  push16(ram, r, RET_100_CHECK);
  hm_CODE_858B7B(ram, r);
  pull16(ram, r);
  r->a = hm_dp_r16(ram, r, 0xAF);
  if (r->a) return;                                        /* a piece still queued */
  push16(ram, r, RET_100_FREE);
  hm_CODE_858B41(ram, r);
  pull16(ram, r);
  r->y = hm_dp_r16(ram, r, 0xA5);
  uint16_t x = r->x = hm_dp_r16(ram, r, 0xA9);
  uint16_t sprite = hm_dp_r16(ram, r, 0xA1);
  hm_bus_wr16(ram, abs_x(r, GOBJ + 0x02, x), sprite);
  hm_bus_wr16(ram, abs_x(r, GOBJ + 0x04, x), hm_dp_r16(ram, r, 0x9F));
  hm_bus_wr16(ram, abs_x(r, GOBJ + 0x06, x), hm_dp_r16(ram, r, 0xA3));
  hm_bus_wr16(ram, abs_x(r, GOBJ + 0x08, x), hm_dp_r16(ram, r, 0x9B));
  hm_bus_wr16(ram, abs_x(r, GOBJ + 0x0A, x), hm_dp_r16(ram, r, 0x9D));
  uint16_t number = hm_bus_rd16(ram, abs_x(r, GOBJ + 0x02, x));
  uint32_t table = SPRITES_86;
  if (sprite >= FIRST_SPRITE_87) { number = (uint16_t)(number - FIRST_SPRITE_87); table = SPRITES_87; }
  uint16_t entry = hm_bus_rd16(ram, (table + (uint16_t)(number << 1)) & 0xFFFFFF);
  hm_dp_w16(ram, r, 0x75, entry);
  hm_bus_wr16(ram, abs_x(r, GOBJ + 0x0C, x), entry);
  hm_bus_wr16(ram, abs_x(r, GOBJ + 0x0E, x), (uint16_t)(entry + 3));
  hm_bus_wr16(ram, abs_x(r, GOBJ + 0x10, x), hm_bus_rd16(ram, hm_dp_long(ram, r, 0x75, 0)));
  hm_bus_wr8(ram, abs_x(r, GOBJ + 0x12, x), hm_bus_rd8(ram, hm_dp_long(ram, r, 0x75, 2)));
  r->x = x; r->y = 2;
  push16(ram, r, RET_100_PIECES);
  hm_LoadGameObjectComponents(ram, r);
  pull16(ram, r);
  r->a = 0;
  hm_dp_w16(ram, r, 0xA7, 0);
}

/* ─────────────────────────────────────────────────────────────────────────
 * CODE_8581CB ($85:81CB, JSL, RTL): reads object $A5 back into the
 * variables CreateGameObject takes ($A1 sprite, $9F flip, $A3, $9B / $9D
 * position; only if the record is in use), and $A7 = its byte +12 (from
 * the sprite's entry) sign-extended, in use or not.
 * Leftovers: $A9 / X = the offset, A = $A7, C = 1 (CMP #0).
 * ───────────────────────────────────────────────────────────────────────── */
void hm_CODE_8581CB(uint8_t *ram, HmRegs *r)
{
  uint16_t x = object_offset(ram, r);
  r->x = x;
  if (hm_bus_rd16(ram, abs_x(r, GOBJ, x))) {
    hm_dp_w16(ram, r, 0xA1, hm_bus_rd16(ram, abs_x(r, GOBJ + 0x02, x)));
    hm_dp_w16(ram, r, 0x9F, hm_bus_rd16(ram, abs_x(r, GOBJ + 0x04, x)));
    hm_dp_w16(ram, r, 0xA3, hm_bus_rd16(ram, abs_x(r, GOBJ + 0x06, x)));
    hm_dp_w16(ram, r, 0x9B, hm_bus_rd16(ram, abs_x(r, GOBJ + 0x08, x)));
    hm_dp_w16(ram, r, 0x9D, hm_bus_rd16(ram, abs_x(r, GOBJ + 0x0A, x)));
  }
  int8_t b = (int8_t)hm_bus_rd8(ram, abs_x(r, GOBJ + 0x12, x));
  r->a = (uint16_t)(int16_t)b;
  hm_dp_w16(ram, r, 0xA7, r->a);
  r->c = 1;
}

/* ─────────────────────────────────────────────────────────────────────────
 * InvertIfFlipedX / InvertIfFlipedY ($85:8BB0 / $85:8BBC, JSR, RTS): A = a
 * piece's offset inside the sprite; if the object is flipped that way ($9F
 * bit 6 / bit 7) it becomes -A - 16, the mirrored place of a 16-pixel
 * piece. Leftovers: C from that add, or untouched.
 * ───────────────────────────────────────────────────────────────────────── */
static void invert_if(uint8_t *ram, HmRegs *r, uint16_t bit)
{
  uint16_t a = r->a;
  push16(ram, r, a);                                       /* PHA */
  if (!(hm_dp_r16(ram, r, 0x9F) & bit)) { r->a = pull16(ram, r); return; }
  pull16(ram, r);
  uint32_t sum = (uint32_t)(uint16_t)(-a) + 0xFFF0;        /* EOR #$FFFF; INC; CLC; ADC #$FFF0 */
  r->c = sum > 0xFFFF;
  r->a = (uint16_t)sum;
}
void hm_InvertIfFlipedX(uint8_t *ram, HmRegs *r) { invert_if(ram, r, 0x0040); }
void hm_InvertIfFlipedY(uint8_t *ram, HmRegs *r) { invert_if(ram, r, 0x0080); }

/* ─────────────────────────────────────────────────────────────────────────
 * ClearSpriteDataTables ($85:8ED7, JSL, RTL): empties everything about the
 * piece graphics: no slot in use ($7E:A320 counts 0, their +2 words
 * $FFFF), an empty queue ($7E:A220, $BB = $BD = 0, $7E:A420 = $FFFF) and
 * no piece loaded ($7F:0F00-$7F:1EFF = $FF), then $0846 = $0848 = 0.
 * Leftovers: A = $FFFF, X = $1000, Y = 64, C = 1.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_ClearSpriteDataTables(uint8_t *ram, HmRegs *r)
{
  hm_bus_wr16(ram, 0x7EA420, 0xFFFF);
  hm_dp_w16(ram, r, 0xBB, 0);
  hm_dp_w16(ram, r, 0xBD, 0);
  for (uint16_t y = 0; y != 64; y++) {
    hm_bus_wr16(ram, SLOT_USERS + 4 * y, 0);
    hm_bus_wr16(ram, SLOT_USERS + 2 + 4 * y, 0xFFFF);
    hm_bus_wr16(ram, PIECE_QUEUE + 4 * y, 0xFFFF);
  }
  for (uint16_t x = 0; x != 0x1000; x += 2) hm_bus_wr16(ram, PIECE_SLOT + x, 0xFFFF);
  hm_bus_wr16(ram, abs_db(r, 0x0846), 0);
  hm_bus_wr16(ram, abs_db(r, 0x0848), 0);
  r->a = 0xFFFF; r->x = 0x1000; r->y = 64; r->c = 1;
}

/* ─────────────────────────────────────────────────────────────────────────
 * CODE_8582C7 ($85:82C7, JSL, RTL): animates the game objects, every
 * frame. A sprite's entry (+0C) is a list of frames, 3 bytes each: the
 * piece list of the frame and how many frames it lasts; a piece list of 0
 * goes back to the first frame (a loop). For each of the $DC objects in
 * use, by its timer +12:
 *   0     the next frame: unless a piece of the current one is still
 *         queued (CODE_858B7B; then it waits), its pieces are freed and
 *         those of the frame at +0E are loaded; +10 = the frame's piece
 *         list, +12 = its duration, +0E = the frame after it;
 *   $FE   the object is freed (its pieces and +00 = 0). Only found right
 *         after the object was created or given a sprite (they copy the
 *         first frame's duration): a later frame of $FE is counted down
 *         as soon as it is loaded, and simply lasts 254 frames;
 *   $FF   stays on this frame for ever;
 *   other one frame less (also right after loading a frame).
 * Leftovers: $A9 = $24 x $DC, $AB = $DC, A = $AB, C = 1; X, Y and $75 /
 * $77 / $7A of the last object worked on.
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_2C7_CHECK  0x830E   /* JSR CODE_858B7B at $85:830C */
#define RET_2C7_FREE   0x8315   /* JSR CODE_858B41 at $85:8313 */
#define RET_2C7_LOAD   0x8344   /* JSR LoadGameObjectComponents at $85:8342 */
#define RET_2C7_END    0x8356   /* JSR CODE_858B41 at $85:8354 */

static void jsr85(uint8_t *ram, HmRegs *r, uint16_t ret, void (*fn)(uint8_t *, HmRegs *))
{
  push16(ram, r, ret);
  fn(ram, r);
  pull16(ram, r);
}
static uint8_t gobj8(uint8_t *ram, HmRegs *r, uint16_t field, uint16_t x)   /* 8-bit LDA */
{
  uint8_t v = hm_bus_rd8(ram, abs_x(r, (uint16_t)(GOBJ + field), x));
  r->a = (uint16_t)((r->a & 0xFF00) | v);
  return v;
}

/* the next frame (.CODE_858303 with a timer of 0); false if it must wait */
static bool next_frame(uint8_t *ram, HmRegs *r)
{
  jsr85(ram, r, RET_2C7_CHECK, hm_CODE_858B7B);
  r->a = hm_dp_r16(ram, r, 0xAF);
  if (r->a) return false;
  jsr85(ram, r, RET_2C7_FREE, hm_CODE_858B41);
  uint16_t x = r->x = hm_dp_r16(ram, r, 0xA9);
  hm_dp_w16(ram, r, 0x75, r->a = hm_bus_rd16(ram, abs_x(r, GOBJ + 0x0E, x)));
  while ((r->a = hm_bus_rd16(ram, hm_dp_long(ram, r, 0x75, 0))) == 0)    /* the end: loop */
    hm_dp_w16(ram, r, 0x75, r->a = hm_bus_rd16(ram, abs_x(r, GOBJ + 0x0C, x)));
  hm_bus_wr16(ram, abs_x(r, GOBJ + 0x10, x), r->a);
  r->y = 2;
  uint8_t lasts = hm_bus_rd8(ram, hm_dp_long(ram, r, 0x75, 2));
  r->a = (uint16_t)((r->a & 0xFF00) | lasts);
  hm_bus_wr8(ram, abs_x(r, GOBJ + 0x12, x), lasts);
  uint16_t at = hm_dp_r16(ram, r, 0x75);
  r->c = (uint32_t)at + 3 > 0xFFFF;
  r->a = (uint16_t)(at + 3);
  hm_bus_wr16(ram, abs_x(r, GOBJ + 0x0E, x), r->a);
  jsr85(ram, r, RET_2C7_LOAD, hm_LoadGameObjectComponents);
  return true;
}

void hm_CODE_8582C7(uint8_t *ram, HmRegs *r)
{
  hm_dp_w16(ram, r, 0xA9, 0);
  hm_dp_w16(ram, r, 0xAB, 0);
  do {
    uint16_t x = r->x = hm_dp_r16(ram, r, 0xA9);
    r->a = hm_bus_rd16(ram, abs_x(r, GOBJ, x));
    if (r->a) {
      uint16_t sprite = hm_bus_rd16(ram, abs_x(r, GOBJ + 0x02, x));
      uint8_t bank = sprite >= FIRST_SPRITE_87 ? 0x87 : 0x86;
      r->c = sprite >= FIRST_SPRITE_87;                    /* CMP #$0262 */
      r->a = (uint16_t)((sprite & 0xFF00) | bank);
      *hm_dp(ram, r, 0x77) = bank;
      *hm_dp(ram, r, 0x7A) = bank;
      uint8_t timer = gobj8(ram, r, 0x12, x);
      bool count_down = true;
      if (timer == 0) {
        count_down = next_frame(ram, r);
      } else {
        r->c = timer >= 0xFE;                              /* CMP #$FE */
        if (timer == 0xFE) {
          jsr85(ram, r, RET_2C7_END, hm_CODE_858B41);
          r->a = 0;
          hm_bus_wr16(ram, abs_x(r, GOBJ, r->x), 0);
          count_down = false;
        }
      }
      if (count_down) {                                    /* .CODE_858362 */
        x = r->x = hm_dp_r16(ram, r, 0xA9);
        timer = gobj8(ram, r, 0x12, x);
        r->c = timer >= 0xFF;                              /* CMP #$FF */
        if (timer != 0xFF) {
          r->a = (uint16_t)((r->a & 0xFF00) | (uint8_t)(timer - 1));
          hm_bus_wr8(ram, abs_x(r, GOBJ + 0x12, x), (uint8_t)(timer - 1));
        }
      }
    }
    uint16_t at = hm_dp_r16(ram, r, 0xA9);                 /* .CODE_8582D4 */
    r->c = (uint32_t)at + GOBJ_SIZE > 0xFFFF;
    hm_dp_w16(ram, r, 0xA9, (uint16_t)(at + GOBJ_SIZE));
    hm_dp_w16(ram, r, 0xAB, (uint16_t)(hm_dp_r16(ram, r, 0xAB) + 1));
    r->a = hm_dp_r16(ram, r, 0xAB);
    r->c = r->a >= hm_dp_r16(ram, r, 0xDC);                /* CMP $DC */
  } while (r->a != hm_dp_r16(ram, r, 0xDC));
}

/* ─────────────────────────────────────────────────────────────────────────
 * GetOBJOrderToDraw ($85:8377, JSR, RTS): the order the $DC game objects
 * are drawn in ($084C, one byte each), a kind of depth sort around the
 * player's object ($0905): the objects at or below the player (Y, +0A, not
 * smaller than the player's) go first, in order, and the ones above go at
 * the end, from the last place backwards; the player's object goes in the
 * middle, last of the first group. So the player is drawn over everything
 * above it on the screen and under what is in front of it. (The order is
 * only by the side of the player; among themselves objects keep their
 * numbers.)
 * Leftovers: $A9 = the player's Y, $AD = the player's place, $AF = the
 * place before it, $7E = $FFFF; X = $AD, Y = $DC, A = the player's object
 * (8-bit), C = 1.
 * ───────────────────────────────────────────────────────────────────────── */
#define DRAW_ORDER   0x084C   /* DB: */
#define PLAYER_GOBJ  0x0905   /* DB: 16b */

void hm_GetOBJOrderToDraw(uint8_t *ram, HmRegs *r)
{
  uint16_t player = hm_bus_rd16(ram, abs_db(r, PLAYER_GOBJ));
  r->c = (player >> 15) & 1;                               /* ASL */
  uint16_t at = hm_bus_rd16(ram, (GOBJ_OFFSETS + (uint16_t)(player << 1)) & 0xFFFFFF);
  uint16_t player_y = hm_bus_rd16(ram, abs_x(r, GOBJ + 0x0A, at));
  hm_dp_w16(ram, r, 0xA9, player_y);
  hm_dp_w16(ram, r, 0xAD, 0);
  hm_dp_w16(ram, r, 0xAF, (uint16_t)(hm_dp_r16(ram, r, 0xDC) - 1));
  hm_dp_w16(ram, r, 0x7E, 0xFFFF);
  uint16_t x = 0, y = 0;
  do {
    push16(ram, r, x);                                     /* PHX */
    if (y != hm_bus_rd16(ram, abs_db(r, PLAYER_GOBJ))) {
      uint16_t oy = hm_bus_rd16(ram, abs_x(r, GOBJ + 0x0A, x));
      r->c = oy >= hm_dp_r16(ram, r, 0xA9);                /* CMP $A9 */
      uint16_t place;
      if (r->c) {
        place = hm_dp_r16(ram, r, 0xAD);
        hm_dp_w16(ram, r, 0xAD, (uint16_t)(place + 1));
      } else {
        place = hm_dp_r16(ram, r, 0xAF);
        hm_dp_w16(ram, r, 0xAF, (uint16_t)(place - 1));
      }
      hm_bus_wr8(ram, abs_x(r, DRAW_ORDER, place), (uint8_t)y);
    }
    uint16_t back = pull16(ram, r);                        /* PLA */
    r->c = (uint32_t)back + GOBJ_SIZE > 0xFFFF;
    x = (uint16_t)(back + GOBJ_SIZE);
    y++;
  } while (y != hm_dp_r16(ram, r, 0xDC));                  /* CPY $DC */
  r->x = hm_dp_r16(ram, r, 0xAD);
  r->y = y;
  r->c = 1;                                                /* CPY $DC: equal */
  uint16_t p = hm_bus_rd16(ram, abs_db(r, PLAYER_GOBJ));
  r->a = p;
  hm_bus_wr8(ram, abs_x(r, DRAW_ORDER, r->x), (uint8_t)p);
}

/* ─────────────────────────────────────────────────────────────────────────
 * PresetsWRAMCopyofOAM ($85:8647, JSR, RTS) and PresetsWRAMCopyOAMCopy
 * ($85:8896, the same code twice): empties the WRAM copy of the sprite
 * table (OAM) before it is filled: the 128 sprites ($7E:A000, 4 bytes
 * each) at X = Y = $F0, off the screen, and the high table ($7E:A200, 32
 * bytes) to 0. $B7 = 1 (absolute, data bank).
 * Leftovers: A = 1.
 * ───────────────────────────────────────────────────────────────────────── */
#define OAM_COPY       0x7EA000
#define OAM_COPY_HIGH  0x7EA200

void hm_PresetsWRAMCopyofOAM(uint8_t *ram, HmRegs *r)
{
  for (uint32_t i = 0; i < 0x200; i += 4) hm_bus_wr16(ram, OAM_COPY + i, 0xF0F0);
  for (uint32_t i = 0; i < 0x20; i += 2) hm_bus_wr16(ram, OAM_COPY_HIGH + i, 0);
  r->a = 1;
  hm_bus_wr16(ram, abs_db(r, 0x00B7), 1);
}
void hm_PresetsWRAMCopyOAMCopy(uint8_t *ram, HmRegs *r) { hm_PresetsWRAMCopyofOAM(ram, r); }

/* ─────────────────────────────────────────────────────────────────────────
 * InitializeOBJs ($85:820F, JSL, RTL): frees all 40 game objects (their
 * first $12 bytes to 0, the piece bytes +14..+23 to $FF), no object handed
 * out ($DC = 0) and the sprite table emptied (PresetsWRAMCopyofOAM). The
 * pieces' graphics slots are not touched. $77 / $7A = the bank of sprite
 * $A1's table, as usual.
 * Leftovers: $0086 (absolute) = $5A0, Y = 40, A = 1 and the rest of
 * PresetsWRAMCopyofOAM; X = $57C, C = 1.
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_20F_PRESETS 0x82C5   /* JSR PresetsWRAMCopyofOAM at $85:82C3 */

void hm_InitializeOBJs(uint8_t *ram, HmRegs *r)
{
  uint16_t sprite = hm_dp_r16(ram, r, 0xA1);
  uint8_t bank = sprite >= FIRST_SPRITE_87 ? 0x87 : 0x86;
  *hm_dp(ram, r, 0x77) = bank;
  *hm_dp(ram, r, 0x7A) = bank;
  uint32_t counter = abs_db(r, 0x0086);
  hm_bus_wr16(ram, counter, 0);
  uint16_t y = 0;
  do {
    uint16_t x = r->x = hm_bus_rd16(ram, counter);
    for (uint16_t f = 0; f < 0x14; f += 2) hm_bus_wr16(ram, abs_x(r, (uint16_t)(GOBJ + f), x), 0);
    for (uint16_t f = 0x14; f < 0x24; f++) hm_bus_wr8(ram, abs_x(r, (uint16_t)(GOBJ + f), x), 0xFF);
    uint16_t next = hm_bus_rd16(ram, counter);
    hm_bus_wr16(ram, counter, (uint16_t)(next + GOBJ_SIZE));
    y++;
  } while (y != GOBJ_MAX);
  r->y = y;
  r->c = 1;
  hm_dp_w16(ram, r, 0xDC, 0);
  push16(ram, r, RET_20F_PRESETS);
  hm_PresetsWRAMCopyofOAM(ram, r);
  pull16(ram, r);
}
