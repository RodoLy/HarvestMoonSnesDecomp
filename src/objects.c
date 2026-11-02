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
 * the loader (LoadQueuedSpritePieces, run at the NMI) takes them out.
 *
 * Map objects: 10 records of 16 bytes at $7E:B4E6:
 *   +0 flags (bit 0 = in use)   +1 its routine (table $81:A58B)   +2, +3
 *   +4 its game object number   +6 sprite   +8 sprite shown now
 *   +A flip   +C X   +E Y
 * RunItemsOnMap ($81:A383) moves them every frame.
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

/* A game object's record (offsets from GOBJ + 36 x the object number) */
#define GOBJ_STATUS       0x00   /* GOBJ_IN_USE, or 0 = free */
#define GOBJ_SPRITE       0x02   /* its sprite number */
#define GOBJ_FLIP         0x04   /* its flip: bit 6 across, bit 7 up-down */
#define GOBJ_PARAM        0x06   /* copied from OBJ_PARAM */
#define GOBJ_X            0x08
#define GOBJ_Y            0x0A
#define GOBJ_FIRST_FRAME  0x0C   /* the sprite's entry: its first frame */
#define GOBJ_NEXT_FRAME   0x0E   /* the frame to show after this one */
#define GOBJ_PIECE_LIST   0x10   /* address of the frame's piece list */
#define GOBJ_TIMER        0x12   /* frames left on this one ($FE free, $FF for ever) */
#define GOBJ_PIECE_COUNT  0x13   /* how many pieces */
#define GOBJ_PIECE_SLOTS  0x14   /* one byte per piece: its graphics slot ($FF = queued) */

/* A sprite's entry: a list of frames */
#define FRAME_PIECES      0      /* word: the frame's piece list (0 = back to the first) */
#define FRAME_LASTS       2      /* byte: how many frames it lasts */
#define FRAME_SIZE        3
/* A piece in a piece list (after the count byte) */
#define PIECE_DX          2      /* signed byte: its offset across in the sprite */
#define PIECE_DY          3      /* signed byte: its offset down */
#define PIECE_ATTR        4      /* palette (bits 0-2) and flip (bits 4-5) */
#define PIECE_ENTRY_SIZE  5

/* A map object's record (offsets from MAP_OBJECTS + 16 x n) */
#define MOBJ_FLAGS        0x0    /* bit 0 = in use */
#define MOBJ_ROUTINE      0x1    /* its routine (table $81:A58B) */
#define MOBJ_STATE        0x2
#define MOBJ_ITEM         0x3
#define MOBJ_GOBJ         0x4    /* word: its game object number */
#define MOBJ_SPRITE       0x6
#define MOBJ_SPRITE_SHOWN 0x8    /* the sprite shown now */
#define MOBJ_FLIP         0xA
#define MOBJ_X            0xC
#define MOBJ_Y            0xE

/* Other tables */
#define GOBJ_OFFSETS     0x858BE0   /* ROM: Table_GameOBJIndexes, object -> record offset */
#define HIGH_X_BITS      0x858BD0   /* ROM: words, the X high bit of each sprite of a high-table word */
#define PIECE_GRAPHICS   0x868000   /* ROM: words, 2 x the index (same table as SLOT_TILES) */
#define SLOT_TILES       0x868000   /* 2 x slot: the slot's first tile */
#define PLAYER_ANIMS     0x81D210   /* ROM: DATA8_81D210, per animation 3 bytes:
                                     * the sprite and the flip / 64 */
#define QUEUE_SLOT_AT    2          /* in a PIECE_QUEUE entry: where to write the slot */
#define SLOT_PIECE       2          /* in a SLOT_USERS entry: the piece loaded in it
                                     * ($FFFF = none) */
#define LOAD_COPIES      0x7EA420   /* 6 bytes each: source, bank, VRAM */
#define COPY_BANK        2          /* in a LOAD_COPIES entry: the source's bank */
#define COPY_VRAM        4          /* in a LOAD_COPIES entry: the VRAM address */
#define COPY_SIZE        6
#define OAM_COPY         0x7EA000   /* the WRAM copy of the sprite table: 128 x 4 bytes */
#define OAM_COPY_HIGH    0x7EA200   /* its high table: 32 bytes */
#define OAM_Y_BYTE       1          /* in a sprite's 4 bytes (X is byte 0) */
#define OAM_TILE_BYTE    2
#define OAM_ATTR_BYTE    3

/* Data-bank variables */
#define GOBJ_CLEAR_AT    0x0086     /* DB (absolute, not direct page): the record
                                     * InitializeOBJs is clearing */
#define UNKNOWN_00B7     0x00B7     /* DB (absolute): ClearOAMCopy sets it to 1 */
#define GRAPHIC_PRESET   0x0195     /* DB: the graphics preset (its x 16 goes in the
                                     * sprites' attributes) */
#define POSTPONED_FREES  0x0740     /* DB: destructions refused (a piece still queued) */
#define QUEUE_LEFT       0x0742     /* DB: entries still in the piece queue */
#define NEW_SLOTS        0x0846     /* DB: slots handed out so far (to $40) */
#define SEARCH_FROM      0x0848     /* DB: where the search for a free slot starts (1-$40) */
#define OAM_SIZE_USED    0x084A     /* DB: bytes of OAM_COPY filled this frame */
#define DRAW_ORDER       0x084C     /* DB: the drawing order, one byte per object */
/* $0901 is HM_ANIM_REQUEST (hm_ram.h): the player's animation */
#define PLAYER_ANIM_SHOWN 0x0903    /* DB: the animation the player's object shows */
#define PLAYER_GOBJ      0x0905     /* DB: 16b, the player's game object */
#define PLAYER_FLIP      0x090F     /* DB: the flip of the player's animation */
#define PLAYER_TIMER     0x0915     /* DB: the player's object's timer, sign-extended */
#define ITEM_FLAGS       0x0974     /* DB: the map object to spawn: its flags, */
#define ITEM_ROUTINE     0x0975     /*     its routine, */
#define ITEM_STATE       0x0976     /*     its state, */
#define ITEM_SPRITE      0x097A     /*     its sprite, */
#define ITEM_FLIP        0x097E     /*     its flip, */
#define ITEM_X           0x0980     /*     its position */
#define ITEM_Y           0x0982
#define ITEM             0x0984     /* DB: the item being worked on */

/* Direct page. The game object routines' arguments and results: */
#define OBJ_X            0x9B   /* position */
#define OBJ_Y            0x9D
#define OBJ_FLIP         0x9F   /* bit 6 across, bit 7 up-down */
#define OBJ_SPRITE       0xA1
#define OBJ_PARAM        0xA3   /* goes to the record's +06 */
#define OBJ_NUMBER       0xA5   /* the object's number */
#define OBJ_RESULT       0xA7   /* 0 = done / $FFFF no room / 1 not in use; ReadGameObject:
                                 * the timer, sign-extended */
#define OBJ_OFFSET       0xA9   /* the record's offset (36 x the number) */
#define OBJ_INDEX        0xAB   /* loops: the number of the object worked on
                                 * (PrepareOAMData: its place in DRAW_ORDER) */
#define GOBJ_COUNT       0xDC   /* game objects handed out so far (only grows, to 40) */
/* The sprite and its pieces: */
#define MAP_OBJ_PTR      0x07   /* long $07-$09: the map object worked on */
#define MAP_OBJ_PTR_BANK 0x09
#define SPRITE_ENTRY     0x75   /* long $75-$77: the sprite's entry / the frame */
#define SPRITE_ENTRY_BANK 0x77
#define PIECE_LIST       0x78   /* long $78-$7A: the piece list / the piece */
#define PIECE_LIST_BANK  0x7A
#define PIECES_LEFT      0xAD   /* pieces still to do */
#define PIECE_NUM        0xB3   /* in: a piece's graphics number; out: its slot
                                 * ($FFFF = queued). ReleaseSpriteSlot: the slot */
#define PIECE_SLOT_AT    0xB5   /* where the piece's slot is to be written */
#define QUEUE_OUT        0xBB   /* PIECE_QUEUE: where the loader takes the next one */
#define QUEUE_IN         0xBD   /* PIECE_QUEUE: where the next one goes */
#define LOAD_BUDGET      0xC7   /* bytes the loader may still send this frame */
/* $AF means a different thing in each routine: */
#define SLOT_BYTE_AT     0xAF   /* LoadGameObjectComponents: where the next slot goes */
#define PIECES_QUEUED    0xAF   /* CheckSpritePiecesLoaded: 0 all loaded, $FFFF not */
#define ORDER_BACK       0xAF   /* GetOBJOrderToDraw: the next place from the end */
#define OAM_USED         0xAF   /* PrepareOAMData: bytes of OAM_COPY filled so far */
/* GetOBJOrderToDraw's own uses: */
#define ORDER_PLAYER_Y   0xA9   /* the player's Y (OBJ_OFFSET elsewhere) */
#define ORDER_FRONT      0xAD   /* the next place from the start (PIECES_LEFT elsewhere) */
/* LoadQueuedSpritePieces's own uses: */
#define LOAD_VRAM        0x75   /* the slot's VRAM address */
#define LOAD_SLOT_AT     0x78   /* long $78-$7A: where the slot is written (bank $80) */
#define LOAD_SLOT_AT_BANK 0x7A
#define COPY_AT          0xBF   /* the next free LOAD_COPIES entry (offset) */
#define LOAD_PIECE       0xC1   /* the piece, then its graphics' offset */
#define LOAD_SLOT4       0xC3   /* 4 x the slot, then the graphics' address */
#define LOAD_SLOT        0xC5   /* the slot, then the graphics' bank */
/* PrepareOAMData's own uses: */
#define OAM_PIECE_AT     0x75   /* the piece's byte in GOBJ_PIECE_SLOTS (offset) */
#define SCREEN_X         0xBF   /* the piece's place on the screen */
#define SCREEN_Y         0xC1
#define OAM_HIGH_BITS    0xC3   /* the word of OAM_COPY_HIGH being changed */
#define TEMP_C5          0xC5   /* set to 3, not read here */
#define OAM_PALETTE      0xB1   /* the piece's palette x 2 */
#define OAM_FLIP_BITS    0xB2   /* the piece's flip bits */
/* Scratch: */
#define TEMP_7E          0x7E   /* a temporary (a multiplier, a part of a number...) */
#define TEMP_80          0x80   /* another one (PrepareOAMData) */
/* Others: */
#define NMI_DMA_CHANNELS 0x9A   /* the DMA channels the NMI starts (bit 4 = the OAM) */
/* $D2 is HM_GAME_STATE (hm_ram.h) */
#define PLAYER_X         0xD6   /* the player's position */
#define PLAYER_Y         0xD8
#define CAMERA_X         0xF5   /* the camera's position */
#define CAMERA_Y         0xF7

/* The console's registers (with the data bank) */
#define OAMADDL          0x2102   /* OAM address (16b) */
#define WRMPYA           0x4202   /* multiplier: first number */
#define WRMPYB           0x4203   /* multiplier: second number (starts it) */
#define DMAP4            0x4340   /* DMA channel 4: mode */
#define BBAD4            0x4341   /*   its destination register ($21xx) */
#define A1T4L            0x4342   /*   its source address */
#define A1T4H            0x4343
#define A1B4             0x4344   /*   its source bank */
#define DAS4L            0x4345   /*   its size (16b) */

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
  uint16_t piece = hm_dp_r16(ram, r, PIECE_NUM);
  uint8_t slot = hm_bus_rd8(ram, (PIECE_SLOT + piece) & 0xFFFFFF);
  r->y = slot;
  if (slot != 0xFF) {
    r->x = (uint16_t)(slot << 2);
    uint32_t users = (SLOT_USERS + r->x) & 0xFFFFFF;
    hm_bus_wr16(ram, users, (uint16_t)(hm_bus_rd16(ram, users) + 1));
    r->a = slot;
    hm_dp_w16(ram, r, PIECE_NUM, slot);
    r->c = 0;                                            /* CPY #$FF: Y < $FF */
    return;
  }
  uint16_t at = hm_dp_r16(ram, r, QUEUE_IN);
  uint16_t next = (uint16_t)(at + 4);
  if (next == 0x0100) next = 0;                          /* 64 entries, then wrap */
  r->c = next >= hm_dp_r16(ram, r, QUEUE_OUT);           /* the CMP $BB of the wait */
  hm_dp_w16(ram, r, QUEUE_IN, next);
  r->x = at;
  hm_bus_wr16(ram, (PIECE_QUEUE + at) & 0xFFFFFF, piece);
  hm_bus_wr16(ram, (PIECE_QUEUE + QUEUE_SLOT_AT + at) & 0xFFFFFF, hm_dp_r16(ram, r, PIECE_SLOT_AT));
  r->a = 0xFFFF;
  hm_dp_w16(ram, r, PIECE_NUM, 0xFFFF);
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
  uint16_t obj = hm_dp_r16(ram, r, OBJ_OFFSET);
  hm_dp_w16(ram, r, PIECE_LIST, hm_bus_rd16(ram, abs_x(r, GOBJ + GOBJ_PIECE_LIST, obj)));
  uint8_t pieces = hm_bus_rd8(ram, hm_dp_long(ram, r, PIECE_LIST, 0));
  *hm_dp(ram, r, PIECES_LEFT) = pieces;
  hm_bus_wr8(ram, abs_x(r, GOBJ + GOBJ_PIECE_COUNT, obj), pieces);
  *hm_dp(ram, r, PIECES_LEFT + 1) = 0;
  uint16_t first = (uint16_t)(obj + GOBJ);               /* CLC; ADC #$019C */
  r->c = (uint32_t)first + GOBJ_PIECE_SLOTS > 0xFFFF;    /* CLC; ADC #$0014 */
  hm_dp_w16(ram, r, SLOT_BYTE_AT, (uint16_t)(first + GOBJ_PIECE_SLOTS));

  uint16_t x = obj, y = 1;
  while (hm_dp_r16(ram, r, PIECES_LEFT) != 0) {
    hm_dp_w16(ram, r, PIECE_NUM, hm_bus_rd16(ram, hm_dp_long(ram, r, PIECE_LIST, y)));
    hm_dp_w16(ram, r, PIECE_SLOT_AT, hm_dp_r16(ram, r, SLOT_BYTE_AT));
    push16(ram, r, x);                                   /* PHX; PHY */
    push16(ram, r, y);
    push16(ram, r, RET_AE5_PIECE);                       /* JSR */
    hm_LoadSpriteComponent(ram, r);
    pull16(ram, r);                                      /* RTS */
    y = pull16(ram, r);                                  /* PLY; PLX */
    x = pull16(ram, r);
    hm_bus_wr8(ram, abs_x(r, GOBJ + GOBJ_PIECE_SLOTS, x), (uint8_t)hm_dp_r16(ram, r, PIECE_NUM));
    x++;
    hm_dp_w16(ram, r, SLOT_BYTE_AT, (uint16_t)(hm_dp_r16(ram, r, SLOT_BYTE_AT) + 1));
    r->c = (uint32_t)y + PIECE_ENTRY_SIZE > 0xFFFF;
    y = (uint16_t)(y + PIECE_ENTRY_SIZE);
    hm_dp_w16(ram, r, PIECES_LEFT, (uint16_t)(hm_dp_r16(ram, r, PIECES_LEFT) - 1));
  }
  r->a = 0;
  r->x = hm_dp_r16(ram, r, OBJ_OFFSET);
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
  uint16_t sprite = hm_dp_r16(ram, r, OBJ_SPRITE);
  uint8_t bank = sprite >= FIRST_SPRITE_87 ? 0x87 : 0x86;
  *hm_dp(ram, r, SPRITE_ENTRY_BANK) = bank;
  *hm_dp(ram, r, PIECE_LIST_BANK) = bank;

  uint16_t handed_out = hm_dp_r16(ram, r, GOBJ_COUNT);
  if (handed_out != GOBJ_MAX) {
    handed_out++;
    hm_dp_w16(ram, r, GOBJ_COUNT, handed_out);
  }
  uint16_t x = 0, y = 0;
  while (hm_bus_rd16(ram, abs_x(r, GOBJ, x)) != 0) {
    x = (uint16_t)(x + GOBJ_SIZE);
    y++;
    if (y == hm_dp_r16(ram, r, GOBJ_COUNT)) {            /* none free */
      r->c = 1;                                          /* CPY: equal */
      r->a = 0xFFFF;
      hm_dp_w16(ram, r, OBJ_RESULT, 0xFFFF);
      r->x = x; r->y = y;
      return;
    }
  }

  hm_dp_w16(ram, r, OBJ_NUMBER, y);
  hm_bus_wr16(ram, abs_x(r, GOBJ + GOBJ_STATUS, x), GOBJ_IN_USE);
  hm_bus_wr16(ram, abs_x(r, GOBJ + GOBJ_SPRITE, x), sprite);
  hm_bus_wr16(ram, abs_x(r, GOBJ + GOBJ_FLIP, x), hm_dp_r16(ram, r, OBJ_FLIP));
  hm_bus_wr16(ram, abs_x(r, GOBJ + GOBJ_PARAM, x), hm_dp_r16(ram, r, OBJ_PARAM));
  hm_bus_wr16(ram, abs_x(r, GOBJ + GOBJ_X, x), hm_dp_r16(ram, r, OBJ_X));
  hm_bus_wr16(ram, abs_x(r, GOBJ + GOBJ_Y, x), hm_dp_r16(ram, r, OBJ_Y));
  hm_dp_w16(ram, r, OBJ_OFFSET, x);

  uint16_t number = hm_bus_rd16(ram, abs_x(r, GOBJ + GOBJ_SPRITE, x));
  uint32_t table = SPRITES_86;
  if (hm_dp_r16(ram, r, OBJ_SPRITE) >= FIRST_SPRITE_87) {
    number = (uint16_t)(number - FIRST_SPRITE_87);
    table = SPRITES_87;
  }
  uint16_t entry = hm_bus_rd16(ram, (table + (uint16_t)(number << 1)) & 0xFFFFFF);
  hm_dp_w16(ram, r, SPRITE_ENTRY, entry);
  hm_bus_wr16(ram, abs_x(r, GOBJ + GOBJ_FIRST_FRAME, x), entry);
  hm_bus_wr16(ram, abs_x(r, GOBJ + GOBJ_NEXT_FRAME, x), (uint16_t)(entry + FRAME_SIZE));
  hm_bus_wr16(ram, abs_x(r, GOBJ + GOBJ_PIECE_LIST, x), hm_bus_rd16(ram, hm_dp_long(ram, r, SPRITE_ENTRY, FRAME_PIECES)));
  hm_bus_wr8(ram, abs_x(r, GOBJ + GOBJ_TIMER, x), hm_bus_rd8(ram, hm_dp_long(ram, r, SPRITE_ENTRY, FRAME_LASTS)));

  r->x = x; r->y = 2;
  push16(ram, r, RET_8000_PIECES);                       /* JSR */
  hm_LoadGameObjectComponents(ram, r);
  pull16(ram, r);                                        /* RTS */
  r->a = 0;
  hm_dp_w16(ram, r, OBJ_RESULT, 0);
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
  hm_dp_w16(ram, r, MAP_OBJ_PTR, (uint16_t)MAP_OBJECTS);
  *hm_dp(ram, r, MAP_OBJ_PTR_BANK) = (uint8_t)(MAP_OBJECTS >> 16);
  uint16_t n = 0;
  while (hm_bus_rd8(ram, hm_dp_long(ram, r, MAP_OBJ_PTR, MOBJ_FLAGS)) & 1) {
    hm_dp_w16(ram, r, MAP_OBJ_PTR, (uint16_t)(hm_dp_r16(ram, r, MAP_OBJ_PTR) + MAP_OBJECT_SIZE));
    if (++n == MAP_OBJECTS_MAX) return;                  /* the original hangs */
  }

  hm_bus_wr8(ram, hm_dp_long(ram, r, MAP_OBJ_PTR, MOBJ_FLAGS), hm_bus_rd8(ram, abs_db(r, ITEM_FLAGS)));
  hm_bus_wr8(ram, hm_dp_long(ram, r, MAP_OBJ_PTR, MOBJ_ROUTINE), hm_bus_rd8(ram, abs_db(r, ITEM_ROUTINE)));
  hm_bus_wr8(ram, hm_dp_long(ram, r, MAP_OBJ_PTR, MOBJ_STATE), hm_bus_rd8(ram, abs_db(r, ITEM_STATE)));
  hm_bus_wr8(ram, hm_dp_long(ram, r, MAP_OBJ_PTR, MOBJ_ITEM), hm_bus_rd8(ram, abs_db(r, ITEM)));
  uint16_t sprite = hm_bus_rd16(ram, abs_db(r, ITEM_SPRITE));
  hm_bus_wr16(ram, hm_dp_long(ram, r, MAP_OBJ_PTR, MOBJ_SPRITE), sprite);
  hm_bus_wr16(ram, hm_dp_long(ram, r, MAP_OBJ_PTR, MOBJ_SPRITE_SHOWN), sprite);
  hm_dp_w16(ram, r, OBJ_SPRITE, sprite);
  uint16_t flip = hm_bus_rd16(ram, abs_db(r, ITEM_FLIP));
  hm_bus_wr16(ram, hm_dp_long(ram, r, MAP_OBJ_PTR, MOBJ_FLIP), flip);
  hm_dp_w16(ram, r, OBJ_FLIP, flip);
  uint16_t px = hm_bus_rd16(ram, abs_db(r, ITEM_X));
  hm_bus_wr16(ram, hm_dp_long(ram, r, MAP_OBJ_PTR, MOBJ_X), px);
  hm_dp_w16(ram, r, OBJ_X, px);
  uint16_t py = hm_bus_rd16(ram, abs_db(r, ITEM_Y));
  hm_bus_wr16(ram, hm_dp_long(ram, r, MAP_OBJ_PTR, MOBJ_Y), py);
  hm_dp_w16(ram, r, OBJ_Y, py);
  hm_dp_w16(ram, r, OBJ_PARAM, 0);

  r->a = py; r->x = n; r->y = MOBJ_Y;
  hm_jsl(ram, r, RET_A500_CREATE, 0x85);
  hm_CreateGameObject(ram, r);
  hm_rtl(ram, r);
  r->y = MOBJ_GOBJ;
  r->a = hm_dp_r16(ram, r, OBJ_NUMBER);
  hm_bus_wr16(ram, hm_dp_long(ram, r, MAP_OBJ_PTR, MOBJ_GOBJ), r->a);
}

/* ─────────────────────────────────────────────────────────────────────────
 * ClearItemRecords ($81:A4C7, JSL, RTL): clears the first byte of the
 * 10 records of 16 bytes at $7E:B4E6 (right before the event slots).
 * Leftovers: $07-$09 = $7E:B586, A = $B586, X = 10, Y = 0, C = 1.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_ClearItemRecords(uint8_t *ram, HmRegs *r)
{
  hm_dp_w16(ram, r, MAP_OBJ_PTR, (uint16_t)MAP_OBJECTS);
  *hm_dp(ram, r, MAP_OBJ_PTR_BANK) = (uint8_t)(MAP_OBJECTS >> 16);
  for (uint16_t x = 0; x != MAP_OBJECTS_MAX; x++) {
    hm_bus_wr8(ram, hm_dp_long(ram, r, MAP_OBJ_PTR, MOBJ_FLAGS), 0);
    hm_dp_w16(ram, r, MAP_OBJ_PTR, (uint16_t)(hm_dp_r16(ram, r, MAP_OBJ_PTR) + MAP_OBJECT_SIZE));
  }
  r->a = (uint16_t)(MAP_OBJECTS + MAP_OBJECTS_MAX * MAP_OBJECT_SIZE);
  r->x = MAP_OBJECTS_MAX;
  r->y = 0;
  r->c = 1;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Destroying a game object.
 *
 * DestroyGameObject ($85:81A2, JSL, RTL): object $A5 ($A9 = its offset, from
 * Table_GameOBJIndexes at $85:8BE0). If one of its pieces is still waiting
 * in the loader's queue (slot $FF), nothing can be freed yet: $0740 counts
 * one more postponed destruction, $AF = A = $FFFF and it returns. Otherwise
 * every piece gives its graphics slot back, the record is freed (+0 = 0)
 * and A = its +0E. The callers do not retry: the object simply stays.
 *
 * CheckSpritePiecesLoaded ($85:8B7B, JSR, RTS): the check, $AF = 0 (all loaded) or
 * $FFFF. FreeSpritePieces ($85:8B41, JSR, RTS): the freeing; each piece's slot
 * byte becomes $FF. ReleaseSpriteSlot ($85:8C9F, JSR, RTS): one user less for
 * slot $B3 ($7E:A320 + 4 slot).
 * Bug: ReleaseSpriteSlot waits while the slot's count is 0 ("LDA; BEQ" to
 * itself), which would hang the game; a slot in use is never at 0, so it
 * does not happen. The rewrite does not wait (the fuzz never gives 0).
 * Leftovers: see each one; C = 1 at the end of the loops (CPY / CMP).
 * ───────────────────────────────────────────────────────────────────────── */
void hm_ReleaseSpriteSlot(uint8_t *ram, HmRegs *r)
{
  uint16_t slot = hm_dp_r16(ram, r, PIECE_NUM);
  r->c = (slot >> 14) & 1;                                 /* the second ASL */
  r->x = (uint16_t)(slot << 2);
  uint32_t users = (SLOT_USERS + r->x) & 0xFFFFFF;
  r->a = (uint16_t)(hm_bus_rd16(ram, users) - 1);
  hm_bus_wr16(ram, users, r->a);
}

/* LDA #0; XBA; LDA +13,X (8-bit), TAY: the number of pieces */
static uint16_t pieces_of(uint8_t *ram, HmRegs *r, uint16_t x)
{
  r->a = hm_bus_rd8(ram, abs_x(r, GOBJ + GOBJ_PIECE_COUNT, x));
  return r->a;
}

void hm_CheckSpritePiecesLoaded(uint8_t *ram, HmRegs *r)
{
  uint16_t x = hm_dp_r16(ram, r, OBJ_OFFSET);
  uint16_t y = pieces_of(ram, r, x);
  for (;;) {
    r->c = 1;                                              /* CPY #0 */
    if (y == 0) {
      r->a = 0;
      hm_dp_w16(ram, r, PIECES_QUEUED, 0);
      break;
    }
    uint8_t slot = hm_bus_rd8(ram, abs_x(r, GOBJ + GOBJ_PIECE_SLOTS, x));
    r->a = (uint16_t)((r->a & 0xFF00) | slot);
    r->c = slot == 0xFF;                                   /* CMP #$FF */
    if (slot == 0xFF) {                                    /* still queued */
      uint32_t waits = abs_db(r, POSTPONED_FREES);
      hm_bus_wr16(ram, waits, (uint16_t)(hm_bus_rd16(ram, waits) + 1));
      r->a = 0xFFFF;
      hm_dp_w16(ram, r, PIECES_QUEUED, 0xFFFF);
      break;
    }
    x++; y--;
  }
  r->x = x; r->y = y;
}

#define RET_B41_SLOT 0x8B6B   /* JSR ReleaseSpriteSlot at $85:8B69 */

void hm_FreeSpritePieces(uint8_t *ram, HmRegs *r)
{
  uint16_t x = hm_dp_r16(ram, r, OBJ_OFFSET);
  uint16_t y = pieces_of(ram, r, x);
  while (y != 0) {
    uint8_t slot = hm_bus_rd8(ram, abs_x(r, GOBJ + GOBJ_PIECE_SLOTS, x));
    r->a = (uint16_t)((r->a & 0xFF00) | slot);
    if (slot != 0xFF) {
      *hm_dp(ram, r, PIECE_NUM) = slot;
      *hm_dp(ram, r, PIECE_NUM + 1) = 0;
      r->a = (uint16_t)((r->a & 0xFF00) | 0xFF);
      hm_bus_wr8(ram, abs_x(r, GOBJ + GOBJ_PIECE_SLOTS, x), 0xFF);
      push16(ram, r, y); push16(ram, r, x);                /* PHY; PHX */
      push16(ram, r, RET_B41_SLOT);
      hm_ReleaseSpriteSlot(ram, r);
      pull16(ram, r);
      x = pull16(ram, r); y = pull16(ram, r);              /* PLX; PLY */
    }
    x++; y--;
  }
  r->c = 1;                                                /* CPY #0 */
  r->x = hm_dp_r16(ram, r, OBJ_OFFSET);
  r->y = 0;
}

#define RET_1A2_CHECK 0x81B0
#define RET_1A2_FREE  0x81BA

void hm_DestroyGameObject(uint8_t *ram, HmRegs *r)
{
  uint16_t n = hm_dp_r16(ram, r, OBJ_NUMBER);
  r->c = (n >> 15) & 1;                                    /* ASL */
  r->x = (uint16_t)(n << 1);
  uint16_t obj = hm_bus_rd16(ram, (GOBJ_OFFSETS + r->x) & 0xFFFFFF);
  hm_dp_w16(ram, r, OBJ_OFFSET, obj);
  push16(ram, r, RET_1A2_CHECK);
  hm_CheckSpritePiecesLoaded(ram, r);
  pull16(ram, r);
  r->a = hm_dp_r16(ram, r, PIECES_QUEUED);
  if (r->a) return;                                        /* JMP to an RTL */
  push16(ram, r, RET_1A2_FREE);
  hm_FreeSpritePieces(ram, r);
  pull16(ram, r);
  r->x = hm_dp_r16(ram, r, OBJ_OFFSET);
  hm_bus_wr16(ram, abs_x(r, GOBJ, r->x), 0);
  r->a = hm_bus_rd16(ram, abs_x(r, GOBJ + GOBJ_NEXT_FRAME, r->x));
}

/* ─────────────────────────────────────────────────────────────────────────
 * Changing a game object (both JSL, RTL; $A5 = the object, $A9 = its
 * offset from Table_GameOBJIndexes; $77 / $7A = the bank of sprite $A1's
 * table, $86 or $87).
 *
 * MoveGameObject ($85:80B9): moves it: flip $9F, X $9B, Y $9D. $A7 = 0 when
 * done, 1 when the record is not in use (then nothing changes).
 * ChangeGameObjectSprite ($85:8100): gives it another sprite ($A1, with $9F, $A3,
 * $9B, $9D as CreateGameObject): its pieces are freed (unless one is still
 * queued: then nothing, as DestroyGameObject) and the new sprite's are loaded.
 * Leftovers: A = 0 (or $FFFF / the record's first word), X = the offset,
 * C from the last arithmetic.
 * ───────────────────────────────────────────────────────────────────────── */
static void sprite_bank(uint8_t *ram, HmRegs *r)
{
  uint16_t sprite = hm_dp_r16(ram, r, OBJ_SPRITE);
  uint8_t bank = sprite >= FIRST_SPRITE_87 ? 0x87 : 0x86;
  r->c = sprite >= FIRST_SPRITE_87;                        /* CMP #$0262 */
  r->a = (uint16_t)((sprite & 0xFF00) | bank);
  *hm_dp(ram, r, SPRITE_ENTRY_BANK) = bank;
  *hm_dp(ram, r, PIECE_LIST_BANK) = bank;
}
static uint16_t object_offset(uint8_t *ram, HmRegs *r)
{
  uint16_t n = hm_dp_r16(ram, r, OBJ_NUMBER);
  r->c = (n >> 15) & 1;                                    /* ASL */
  r->x = (uint16_t)(n << 1);
  r->a = hm_bus_rd16(ram, (GOBJ_OFFSETS + r->x) & 0xFFFFFF);
  hm_dp_w16(ram, r, OBJ_OFFSET, r->a);
  return r->a;
}

void hm_MoveGameObject(uint8_t *ram, HmRegs *r)
{
  sprite_bank(ram, r);
  uint16_t x = object_offset(ram, r);
  r->x = x;
  r->a = 1;
  hm_dp_w16(ram, r, OBJ_RESULT, 1);
  r->a = hm_bus_rd16(ram, abs_x(r, GOBJ, x));
  if (!r->a) return;                                       /* not in use */
  hm_bus_wr16(ram, abs_x(r, GOBJ + GOBJ_FLIP, x), hm_dp_r16(ram, r, OBJ_FLIP));
  hm_bus_wr16(ram, abs_x(r, GOBJ + GOBJ_X, x), hm_dp_r16(ram, r, OBJ_X));
  hm_bus_wr16(ram, abs_x(r, GOBJ + GOBJ_Y, x), hm_dp_r16(ram, r, OBJ_Y));
  r->a = 0;
  hm_dp_w16(ram, r, OBJ_RESULT, 0);
}

#define RET_100_CHECK  0x812B
#define RET_100_FREE   0x8132
#define RET_100_PIECES 0x819B

void hm_ChangeGameObjectSprite(uint8_t *ram, HmRegs *r)
{
  sprite_bank(ram, r);
  object_offset(ram, r);
  push16(ram, r, RET_100_CHECK);
  hm_CheckSpritePiecesLoaded(ram, r);
  pull16(ram, r);
  r->a = hm_dp_r16(ram, r, PIECES_QUEUED);
  if (r->a) return;                                        /* a piece still queued */
  push16(ram, r, RET_100_FREE);
  hm_FreeSpritePieces(ram, r);
  pull16(ram, r);
  r->y = hm_dp_r16(ram, r, OBJ_NUMBER);
  uint16_t x = r->x = hm_dp_r16(ram, r, OBJ_OFFSET);
  uint16_t sprite = hm_dp_r16(ram, r, OBJ_SPRITE);
  hm_bus_wr16(ram, abs_x(r, GOBJ + GOBJ_SPRITE, x), sprite);
  hm_bus_wr16(ram, abs_x(r, GOBJ + GOBJ_FLIP, x), hm_dp_r16(ram, r, OBJ_FLIP));
  hm_bus_wr16(ram, abs_x(r, GOBJ + GOBJ_PARAM, x), hm_dp_r16(ram, r, OBJ_PARAM));
  hm_bus_wr16(ram, abs_x(r, GOBJ + GOBJ_X, x), hm_dp_r16(ram, r, OBJ_X));
  hm_bus_wr16(ram, abs_x(r, GOBJ + GOBJ_Y, x), hm_dp_r16(ram, r, OBJ_Y));
  uint16_t number = hm_bus_rd16(ram, abs_x(r, GOBJ + GOBJ_SPRITE, x));
  uint32_t table = SPRITES_86;
  if (sprite >= FIRST_SPRITE_87) { number = (uint16_t)(number - FIRST_SPRITE_87); table = SPRITES_87; }
  uint16_t entry = hm_bus_rd16(ram, (table + (uint16_t)(number << 1)) & 0xFFFFFF);
  hm_dp_w16(ram, r, SPRITE_ENTRY, entry);
  hm_bus_wr16(ram, abs_x(r, GOBJ + GOBJ_FIRST_FRAME, x), entry);
  hm_bus_wr16(ram, abs_x(r, GOBJ + GOBJ_NEXT_FRAME, x), (uint16_t)(entry + FRAME_SIZE));
  hm_bus_wr16(ram, abs_x(r, GOBJ + GOBJ_PIECE_LIST, x), hm_bus_rd16(ram, hm_dp_long(ram, r, SPRITE_ENTRY, FRAME_PIECES)));
  hm_bus_wr8(ram, abs_x(r, GOBJ + GOBJ_TIMER, x), hm_bus_rd8(ram, hm_dp_long(ram, r, SPRITE_ENTRY, FRAME_LASTS)));
  r->x = x; r->y = 2;
  push16(ram, r, RET_100_PIECES);
  hm_LoadGameObjectComponents(ram, r);
  pull16(ram, r);
  r->a = 0;
  hm_dp_w16(ram, r, OBJ_RESULT, 0);
}

/* ─────────────────────────────────────────────────────────────────────────
 * ReadGameObject ($85:81CB, JSL, RTL): reads object $A5 back into the
 * variables CreateGameObject takes ($A1 sprite, $9F flip, $A3, $9B / $9D
 * position; only if the record is in use), and $A7 = its byte +12 (from
 * the sprite's entry) sign-extended, in use or not.
 * Leftovers: $A9 / X = the offset, A = $A7, C = 1 (CMP #0).
 * ───────────────────────────────────────────────────────────────────────── */
void hm_ReadGameObject(uint8_t *ram, HmRegs *r)
{
  uint16_t x = object_offset(ram, r);
  r->x = x;
  if (hm_bus_rd16(ram, abs_x(r, GOBJ, x))) {
    hm_dp_w16(ram, r, OBJ_SPRITE, hm_bus_rd16(ram, abs_x(r, GOBJ + GOBJ_SPRITE, x)));
    hm_dp_w16(ram, r, OBJ_FLIP, hm_bus_rd16(ram, abs_x(r, GOBJ + GOBJ_FLIP, x)));
    hm_dp_w16(ram, r, OBJ_PARAM, hm_bus_rd16(ram, abs_x(r, GOBJ + GOBJ_PARAM, x)));
    hm_dp_w16(ram, r, OBJ_X, hm_bus_rd16(ram, abs_x(r, GOBJ + GOBJ_X, x)));
    hm_dp_w16(ram, r, OBJ_Y, hm_bus_rd16(ram, abs_x(r, GOBJ + GOBJ_Y, x)));
  }
  int8_t b = (int8_t)hm_bus_rd8(ram, abs_x(r, GOBJ + GOBJ_TIMER, x));
  r->a = (uint16_t)(int16_t)b;
  hm_dp_w16(ram, r, OBJ_RESULT, r->a);
  r->c = 1;
}

/* ─────────────────────────────────────────────────────────────────────────
 * InvertIfFlippedX / InvertIfFlippedY ($85:8BB0 / $85:8BBC, JSR, RTS): A = a
 * piece's offset inside the sprite; if the object is flipped that way ($9F
 * bit 6 / bit 7) it becomes -A - 16, the mirrored place of a 16-pixel
 * piece. Leftovers: C from that add, or untouched.
 * ───────────────────────────────────────────────────────────────────────── */
static void invert_if(uint8_t *ram, HmRegs *r, uint16_t bit)
{
  uint16_t a = r->a;
  push16(ram, r, a);                                       /* PHA */
  if (!(hm_dp_r16(ram, r, OBJ_FLIP) & bit)) { r->a = pull16(ram, r); return; }
  pull16(ram, r);
  uint32_t sum = (uint32_t)(uint16_t)(-a) + 0xFFF0;        /* EOR #$FFFF; INC; CLC; ADC #$FFF0 */
  r->c = sum > 0xFFFF;
  r->a = (uint16_t)sum;
}
void hm_InvertIfFlippedX(uint8_t *ram, HmRegs *r) { invert_if(ram, r, 0x0040); }
void hm_InvertIfFlippedY(uint8_t *ram, HmRegs *r) { invert_if(ram, r, 0x0080); }

/* ─────────────────────────────────────────────────────────────────────────
 * ClearSpriteDataTables ($85:8ED7, JSL, RTL): empties everything about the
 * piece graphics: no slot in use ($7E:A320 counts 0, their +2 words
 * $FFFF), an empty queue ($7E:A220, $BB = $BD = 0, $7E:A420 = $FFFF) and
 * no piece loaded ($7F:0F00-$7F:1EFF = $FF), then $0846 = $0848 = 0.
 * Leftovers: A = $FFFF, X = $1000, Y = 64, C = 1.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_ClearSpriteDataTables(uint8_t *ram, HmRegs *r)
{
  hm_bus_wr16(ram, LOAD_COPIES, 0xFFFF);
  hm_dp_w16(ram, r, QUEUE_OUT, 0);
  hm_dp_w16(ram, r, QUEUE_IN, 0);
  for (uint16_t y = 0; y != 64; y++) {
    hm_bus_wr16(ram, SLOT_USERS + 4 * y, 0);
    hm_bus_wr16(ram, SLOT_USERS + SLOT_PIECE + 4 * y, 0xFFFF);
    hm_bus_wr16(ram, PIECE_QUEUE + 4 * y, 0xFFFF);
  }
  for (uint16_t x = 0; x != 0x1000; x += 2) hm_bus_wr16(ram, PIECE_SLOT + x, 0xFFFF);
  hm_bus_wr16(ram, abs_db(r, NEW_SLOTS), 0);
  hm_bus_wr16(ram, abs_db(r, SEARCH_FROM), 0);
  r->a = 0xFFFF; r->x = 0x1000; r->y = 64; r->c = 1;
}

/* ─────────────────────────────────────────────────────────────────────────
 * AnimateGameObjects ($85:82C7, JSL, RTL): animates the game objects, every
 * frame. A sprite's entry (+0C) is a list of frames, 3 bytes each: the
 * piece list of the frame and how many frames it lasts; a piece list of 0
 * goes back to the first frame (a loop). For each of the $DC objects in
 * use, by its timer +12:
 *   0     the next frame: unless a piece of the current one is still
 *         queued (CheckSpritePiecesLoaded; then it waits), its pieces are freed and
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
#define RET_2C7_CHECK  0x830E   /* JSR CheckSpritePiecesLoaded at $85:830C */
#define RET_2C7_FREE   0x8315   /* JSR FreeSpritePieces at $85:8313 */
#define RET_2C7_LOAD   0x8344   /* JSR LoadGameObjectComponents at $85:8342 */
#define RET_2C7_END    0x8356   /* JSR FreeSpritePieces at $85:8354 */

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
  jsr85(ram, r, RET_2C7_CHECK, hm_CheckSpritePiecesLoaded);
  r->a = hm_dp_r16(ram, r, PIECES_QUEUED);
  if (r->a) return false;
  jsr85(ram, r, RET_2C7_FREE, hm_FreeSpritePieces);
  uint16_t x = r->x = hm_dp_r16(ram, r, OBJ_OFFSET);
  hm_dp_w16(ram, r, SPRITE_ENTRY, r->a = hm_bus_rd16(ram, abs_x(r, GOBJ + GOBJ_NEXT_FRAME, x)));
  while ((r->a = hm_bus_rd16(ram, hm_dp_long(ram, r, SPRITE_ENTRY, FRAME_PIECES))) == 0) /* the end: loop */
    hm_dp_w16(ram, r, SPRITE_ENTRY, r->a = hm_bus_rd16(ram, abs_x(r, GOBJ + GOBJ_FIRST_FRAME, x)));
  hm_bus_wr16(ram, abs_x(r, GOBJ + GOBJ_PIECE_LIST, x), r->a);
  r->y = 2;
  uint8_t lasts = hm_bus_rd8(ram, hm_dp_long(ram, r, SPRITE_ENTRY, FRAME_LASTS));
  r->a = (uint16_t)((r->a & 0xFF00) | lasts);
  hm_bus_wr8(ram, abs_x(r, GOBJ + GOBJ_TIMER, x), lasts);
  uint16_t at = hm_dp_r16(ram, r, SPRITE_ENTRY);
  r->c = (uint32_t)at + FRAME_SIZE > 0xFFFF;
  r->a = (uint16_t)(at + FRAME_SIZE);
  hm_bus_wr16(ram, abs_x(r, GOBJ + GOBJ_NEXT_FRAME, x), r->a);
  jsr85(ram, r, RET_2C7_LOAD, hm_LoadGameObjectComponents);
  return true;
}

void hm_AnimateGameObjects(uint8_t *ram, HmRegs *r)
{
  hm_dp_w16(ram, r, OBJ_OFFSET, 0);
  hm_dp_w16(ram, r, OBJ_INDEX, 0);
  do {
    uint16_t x = r->x = hm_dp_r16(ram, r, OBJ_OFFSET);
    r->a = hm_bus_rd16(ram, abs_x(r, GOBJ, x));
    if (r->a) {
      uint16_t sprite = hm_bus_rd16(ram, abs_x(r, GOBJ + GOBJ_SPRITE, x));
      uint8_t bank = sprite >= FIRST_SPRITE_87 ? 0x87 : 0x86;
      r->c = sprite >= FIRST_SPRITE_87;                    /* CMP #$0262 */
      r->a = (uint16_t)((sprite & 0xFF00) | bank);
      *hm_dp(ram, r, SPRITE_ENTRY_BANK) = bank;
      *hm_dp(ram, r, PIECE_LIST_BANK) = bank;
      uint8_t timer = gobj8(ram, r, GOBJ_TIMER, x);
      bool count_down = true;
      if (timer == 0) {
        count_down = next_frame(ram, r);
      } else {
        r->c = timer >= 0xFE;                              /* CMP #$FE */
        if (timer == 0xFE) {
          jsr85(ram, r, RET_2C7_END, hm_FreeSpritePieces);
          r->a = 0;
          hm_bus_wr16(ram, abs_x(r, GOBJ, r->x), 0);
          count_down = false;
        }
      }
      if (count_down) {                                    /* .CODE_858362 */
        x = r->x = hm_dp_r16(ram, r, OBJ_OFFSET);
        timer = gobj8(ram, r, GOBJ_TIMER, x);
        r->c = timer >= 0xFF;                              /* CMP #$FF */
        if (timer != 0xFF) {
          r->a = (uint16_t)((r->a & 0xFF00) | (uint8_t)(timer - 1));
          hm_bus_wr8(ram, abs_x(r, GOBJ + GOBJ_TIMER, x), (uint8_t)(timer - 1));
        }
      }
    }
    uint16_t at = hm_dp_r16(ram, r, OBJ_OFFSET);           /* .CODE_8582D4 */
    r->c = (uint32_t)at + GOBJ_SIZE > 0xFFFF;
    hm_dp_w16(ram, r, OBJ_OFFSET, (uint16_t)(at + GOBJ_SIZE));
    hm_dp_w16(ram, r, OBJ_INDEX, (uint16_t)(hm_dp_r16(ram, r, OBJ_INDEX) + 1));
    r->a = hm_dp_r16(ram, r, OBJ_INDEX);
    r->c = r->a >= hm_dp_r16(ram, r, GOBJ_COUNT);          /* CMP $DC */
  } while (r->a != hm_dp_r16(ram, r, GOBJ_COUNT));
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
void hm_GetOBJOrderToDraw(uint8_t *ram, HmRegs *r)
{
  uint16_t player = hm_bus_rd16(ram, abs_db(r, PLAYER_GOBJ));
  r->c = (player >> 15) & 1;                               /* ASL */
  uint16_t at = hm_bus_rd16(ram, (GOBJ_OFFSETS + (uint16_t)(player << 1)) & 0xFFFFFF);
  uint16_t player_y = hm_bus_rd16(ram, abs_x(r, GOBJ + GOBJ_Y, at));
  hm_dp_w16(ram, r, ORDER_PLAYER_Y, player_y);
  hm_dp_w16(ram, r, ORDER_FRONT, 0);
  hm_dp_w16(ram, r, ORDER_BACK, (uint16_t)(hm_dp_r16(ram, r, GOBJ_COUNT) - 1));
  hm_dp_w16(ram, r, TEMP_7E, 0xFFFF);
  uint16_t x = 0, y = 0;
  do {
    push16(ram, r, x);                                     /* PHX */
    if (y != hm_bus_rd16(ram, abs_db(r, PLAYER_GOBJ))) {
      uint16_t oy = hm_bus_rd16(ram, abs_x(r, GOBJ + GOBJ_Y, x));
      r->c = oy >= hm_dp_r16(ram, r, ORDER_PLAYER_Y);      /* CMP $A9 */
      uint16_t place;
      if (r->c) {
        place = hm_dp_r16(ram, r, ORDER_FRONT);
        hm_dp_w16(ram, r, ORDER_FRONT, (uint16_t)(place + 1));
      } else {
        place = hm_dp_r16(ram, r, ORDER_BACK);
        hm_dp_w16(ram, r, ORDER_BACK, (uint16_t)(place - 1));
      }
      hm_bus_wr8(ram, abs_x(r, DRAW_ORDER, place), (uint8_t)y);
    }
    uint16_t back = pull16(ram, r);                        /* PLA */
    r->c = (uint32_t)back + GOBJ_SIZE > 0xFFFF;
    x = (uint16_t)(back + GOBJ_SIZE);
    y++;
  } while (y != hm_dp_r16(ram, r, GOBJ_COUNT));            /* CPY $DC */
  r->x = hm_dp_r16(ram, r, ORDER_FRONT);
  r->y = y;
  r->c = 1;                                                /* CPY $DC: equal */
  uint16_t p = hm_bus_rd16(ram, abs_db(r, PLAYER_GOBJ));
  r->a = p;
  hm_bus_wr8(ram, abs_x(r, DRAW_ORDER, r->x), (uint8_t)p);
}

/* ─────────────────────────────────────────────────────────────────────────
 * ClearOAMCopy ($85:8647, JSR, RTS) and ClearOAMCopyAgain
 * ($85:8896, the same code twice): empties the WRAM copy of the sprite
 * table (OAM) before it is filled: the 128 sprites ($7E:A000, 4 bytes
 * each) at X = Y = $F0, off the screen, and the high table ($7E:A200, 32
 * bytes) to 0. $B7 = 1 (absolute, data bank).
 * Leftovers: A = 1.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_ClearOAMCopy(uint8_t *ram, HmRegs *r)
{
  for (uint32_t i = 0; i < 0x200; i += 4) hm_bus_wr16(ram, OAM_COPY + i, 0xF0F0);
  for (uint32_t i = 0; i < 0x20; i += 2) hm_bus_wr16(ram, OAM_COPY_HIGH + i, 0);
  r->a = 1;
  hm_bus_wr16(ram, abs_db(r, UNKNOWN_00B7), 1);
}
void hm_ClearOAMCopyAgain(uint8_t *ram, HmRegs *r) { hm_ClearOAMCopy(ram, r); }

/* ─────────────────────────────────────────────────────────────────────────
 * InitializeOBJs ($85:820F, JSL, RTL): frees all 40 game objects (their
 * first $12 bytes to 0, the piece bytes +14..+23 to $FF), no object handed
 * out ($DC = 0) and the sprite table emptied (ClearOAMCopy). The
 * pieces' graphics slots are not touched. $77 / $7A = the bank of sprite
 * $A1's table, as usual.
 * Leftovers: $0086 (absolute) = $5A0, Y = 40, A = 1 and the rest of
 * ClearOAMCopy; X = $57C, C = 1.
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_20F_PRESETS 0x82C5   /* JSR ClearOAMCopy at $85:82C3 */

void hm_InitializeOBJs(uint8_t *ram, HmRegs *r)
{
  uint16_t sprite = hm_dp_r16(ram, r, OBJ_SPRITE);
  uint8_t bank = sprite >= FIRST_SPRITE_87 ? 0x87 : 0x86;
  *hm_dp(ram, r, SPRITE_ENTRY_BANK) = bank;
  *hm_dp(ram, r, PIECE_LIST_BANK) = bank;
  uint32_t counter = abs_db(r, GOBJ_CLEAR_AT);
  hm_bus_wr16(ram, counter, 0);
  uint16_t y = 0;
  do {
    uint16_t x = r->x = hm_bus_rd16(ram, counter);
    for (uint16_t f = 0; f < GOBJ_PIECE_SLOTS; f += 2) hm_bus_wr16(ram, abs_x(r, (uint16_t)(GOBJ + f), x), 0);
    for (uint16_t f = GOBJ_PIECE_SLOTS; f < GOBJ_SIZE; f++) hm_bus_wr8(ram, abs_x(r, (uint16_t)(GOBJ + f), x), 0xFF);
    uint16_t next = hm_bus_rd16(ram, counter);
    hm_bus_wr16(ram, counter, (uint16_t)(next + GOBJ_SIZE));
    y++;
  } while (y != GOBJ_MAX);
  r->y = y;
  r->c = 1;
  hm_dp_w16(ram, r, GOBJ_COUNT, 0);
  push16(ram, r, RET_20F_PRESETS);
  hm_ClearOAMCopy(ram, r);
  pull16(ram, r);
}

/* ─────────────────────────────────────────────────────────────────────────
 * The player's own game object ($0905). Its animation number ($0901, the
 * one shown last in $0903) picks a sprite and a flip from DATA8_81D210
 * ($81:D210, 3 bytes each: the sprite, and a byte that x 64 is the flip,
 * kept in $090F).
 *
 * CreatePlayerObject ($81:CFA0, JSL, RTL): creates the object at the player's
 * position (CreateGameObject) and keeps its number in $0905.
 * UpdatePlayerObject ($81:CFE6, JSR, RTS), every frame: moves it to the player
 * (MoveGameObject); if the animation changed, gives it the new sprite
 * (ChangeGameObjectSprite); then ReadPlayerObjectTimer.
 * ReadPlayerObjectTimer ($81:D03D, JSR, RTS): reads the object back (ReadGameObject);
 * $0915 = its timer byte, sign-extended ($A7); at $FFFF (the frame lasts
 * for ever: the animation is over) the game state bit $2000 is cleared.
 * Leftovers: those of the object routines; A = $A5 / $0905 (CFA0), the
 * cleared state or $A7 (D03D), C from the last compare.
 * ───────────────────────────────────────────────────────────────────────── */
/* $0903 = $0901; $A1 = its sprite, $090F = $9F = its flip; returns n x 3 */
static void player_sprite(uint8_t *ram, HmRegs *r, uint16_t anim)
{
  hm_bus_wr16(ram, abs_db(r, PLAYER_ANIM_SHOWN), anim);
  hm_dp_w16(ram, r, TEMP_7E, anim);
  uint32_t idx = (uint32_t)(uint16_t)(anim << 1) + anim;  /* ASL; CLC; ADC $7E */
  r->x = (uint16_t)idx;
  hm_dp_w16(ram, r, OBJ_SPRITE, hm_bus_rd16(ram, PLAYER_ANIMS + r->x));
  r->x = (uint16_t)(r->x + 2);
  uint16_t flip = (uint16_t)(hm_bus_rd8(ram, PLAYER_ANIMS + r->x) << 6);
  r->c = 0;                                                /* the last ASL */
  r->a = flip;
  hm_bus_wr16(ram, abs_db(r, PLAYER_FLIP), flip);
  hm_dp_w16(ram, r, OBJ_FLIP, flip);
}

void hm_CreatePlayerObject(uint8_t *ram, HmRegs *r)
{
  uint16_t anim = hm_bus_rd16(ram, abs_db(r, HM_ANIM_REQUEST));
  player_sprite(ram, r, anim);
  r->a = 0;
  hm_dp_w16(ram, r, OBJ_PARAM, 0);
  hm_dp_w16(ram, r, OBJ_X, hm_dp_r16(ram, r, PLAYER_X));
  hm_dp_w16(ram, r, OBJ_Y, hm_dp_r16(ram, r, PLAYER_Y));
  r->a = hm_dp_r16(ram, r, PLAYER_Y);
  hm_jsl(ram, r, 0xCFDD, 0x85);
  hm_CreateGameObject(ram, r);
  hm_rtl(ram, r);
  r->a = hm_dp_r16(ram, r, OBJ_NUMBER);
  hm_bus_wr16(ram, abs_db(r, PLAYER_GOBJ), r->a);
}

void hm_ReadPlayerObjectTimer(uint8_t *ram, HmRegs *r)
{
  hm_dp_w16(ram, r, OBJ_NUMBER, hm_bus_rd16(ram, abs_db(r, PLAYER_GOBJ)));
  hm_jsl(ram, r, 0xD047, 0x85);
  hm_ReadGameObject(ram, r);
  hm_rtl(ram, r);
  r->a = hm_dp_r16(ram, r, OBJ_RESULT);
  hm_bus_wr16(ram, abs_db(r, PLAYER_TIMER), r->a);
  r->c = 1;                                                /* CMP #$FFFF: >= */
  if (r->a != 0xFFFF) { r->c = r->a >= 0xFFFF; return; }
  r->a = (uint16_t)(0xDFFF & hm_dp_r16(ram, r, HM_GAME_STATE)); /* the state's bit $2000 off */
  hm_dp_w16(ram, r, HM_GAME_STATE, r->a);
}

void hm_UpdatePlayerObject(uint8_t *ram, HmRegs *r)
{
  hm_dp_w16(ram, r, OBJ_NUMBER, hm_bus_rd16(ram, abs_db(r, PLAYER_GOBJ)));
  hm_dp_w16(ram, r, OBJ_X, hm_dp_r16(ram, r, PLAYER_X));
  hm_dp_w16(ram, r, OBJ_Y, hm_dp_r16(ram, r, PLAYER_Y));
  r->a = hm_bus_rd16(ram, abs_db(r, PLAYER_FLIP));
  hm_dp_w16(ram, r, OBJ_FLIP, r->a);
  hm_jsl(ram, r, 0xCFFD, 0x85);
  hm_MoveGameObject(ram, r);
  hm_rtl(ram, r);
  uint16_t anim = hm_bus_rd16(ram, abs_db(r, HM_ANIM_REQUEST));
  uint16_t shown = hm_bus_rd16(ram, abs_db(r, PLAYER_ANIM_SHOWN));
  r->a = anim;
  r->c = anim >= shown;                                    /* CMP $0903 */
  if (anim != shown) {
    player_sprite(ram, r, anim);
    r->a = hm_bus_rd16(ram, abs_db(r, PLAYER_GOBJ));
    hm_dp_w16(ram, r, OBJ_NUMBER, r->a);
    hm_jsl(ram, r, 0xD038, 0x85);
    hm_ChangeGameObjectSprite(ram, r);
    hm_rtl(ram, r);
  }
  push16(ram, r, 0xD03B);                                  /* JSR ReadPlayerObjectTimer */
  hm_ReadPlayerObjectTimer(ram, r);
  pull16(ram, r);
}

/* ─────────────────────────────────────────────────────────────────────────
 * LoadQueuedSpritePieces ($85:8CB2, JSL, RTL): the loader of the queued pieces (run
 * at the NMI). While the queue ($BB != $BD) has pieces and the budget $C7
 * has $100 bytes left for one more, takes the piece at $BB:
 *   - its slot: the next new one while $0846 < $40 (then $0846 + 1), or
 *     else the first free one ($7E:A320 + 4 slot = 0) from slot $0848
 *     (which turns 1-$40 each piece);
 *   - the slot gets one user and the piece; the piece that was in it, if
 *     any, is no longer loaded ($7F:0F00 + it = $FF); the new one is
 *     ($7F:0F00 + piece = slot), and the slot is written where the queue
 *     said (bank $80);
 *   - a copy for the NMI at $7E:A420 + $BF (6 bytes each, $FFFF after the
 *     last): the piece's graphics (ROM $86:8000 + 2 x the piece's low 6
 *     bits, x $20, + $2000 x bits 6-7, + $8000), its bank ($88 + the
 *     piece's high byte), and the slot's VRAM address ($86:8000 + 2 x
 *     slot, x $10).
 * At the end $0742 = what is left in the queue (entries).
 * Traps (the original spins forever): no free slot; a slot >= $2000; 32
 * pieces in one go. None can happen with the budgets the game gives; the
 * rewrite spins too.
 * Leftovers: $7E, $C1, $C3, $C5, $75 of the last piece, $7A = $80; X the
 * last X; Y the count of the search; C from the last LSR; A's high byte
 * from the last 16-bit load of the loop's test.
 * ───────────────────────────────────────────────────────────────────────── */
static void trap(void) { for (;;) { } }

/* the console's multiplier: 8 x 8 bits (STA $4202; STA $4203, with the
 * data bank; the product read back from $4216) */
static uint16_t graphics_times(uint8_t *ram, HmRegs *r, uint16_t index, uint8_t by)
{
  uint8_t a = (uint8_t)hm_bus_rd16(ram, PIECE_GRAPHICS + 2u * index);
  hm_bus_wr8(ram, abs_db(r, WRMPYA), a);
  hm_bus_wr8(ram, abs_db(r, WRMPYB), by);
  return (uint16_t)(a * by);
}

void hm_LoadQueuedSpritePieces(uint8_t *ram, HmRegs *r)
{
  *hm_dp(ram, r, LOAD_SLOT_AT_BANK) = 0x80;
  hm_dp_w16(ram, r, COPY_AT, 0);
  for (;;) {
    r->a = hm_dp_r16(ram, r, QUEUE_IN);
    if (r->a == hm_dp_r16(ram, r, QUEUE_OUT)) break;
    r->a = hm_dp_r16(ram, r, LOAD_BUDGET);
    if (r->a < 0x0100) break;

    uint16_t at = hm_dp_r16(ram, r, QUEUE_OUT);
    r->x = at;
    uint16_t piece = hm_bus_rd16(ram, (PIECE_QUEUE + at) & 0xFFFFFF);
    hm_dp_w16(ram, r, LOAD_PIECE, piece);
    hm_dp_w16(ram, r, LOAD_SLOT_AT, hm_bus_rd16(ram, (PIECE_QUEUE + QUEUE_SLOT_AT + at) & 0xFFFFFF));
    hm_dp_w16(ram, r, LOAD_BUDGET, (uint16_t)(hm_dp_r16(ram, r, LOAD_BUDGET) - 0x0100));
    uint16_t next = (uint16_t)(at + 4);
    if (next == 0x0100) next = 0;
    hm_dp_w16(ram, r, QUEUE_OUT, next);
    uint16_t from = (uint16_t)((hm_bus_rd16(ram, abs_db(r, SEARCH_FROM)) & 0x3F) + 1);
    hm_bus_wr16(ram, abs_db(r, SEARCH_FROM), from);

    uint16_t handed = hm_bus_rd16(ram, abs_db(r, NEW_SLOTS));
    if (handed != 0x0040) {
      hm_dp_w16(ram, r, LOAD_SLOT, handed);
      hm_dp_w16(ram, r, LOAD_SLOT4, (uint16_t)(handed << 2));
      hm_bus_wr16(ram, abs_db(r, NEW_SLOTS), (uint16_t)(handed + 1));
    } else {
      uint16_t x = (uint16_t)(from << 2);
      for (r->y = 0; hm_bus_rd16(ram, (SLOT_USERS + x) & 0xFFFFFF) != 0; ) {
        x = (uint16_t)((x + 4) & 0xFF);
        if (++r->y == 0x40) trap();
      }
      hm_dp_w16(ram, r, LOAD_SLOT4, x);
      hm_dp_w16(ram, r, LOAD_SLOT, (uint16_t)(x >> 2));
    }
    uint16_t slot4 = hm_dp_r16(ram, r, LOAD_SLOT4);
    if (slot4 & 0x8000) trap();
    uint16_t slot = hm_dp_r16(ram, r, LOAD_SLOT);
    hm_bus_wr8(ram, hm_dp_long(ram, r, LOAD_SLOT_AT, 0), (uint8_t)slot);
    *hm_dp(ram, r, LOAD_VRAM) = (uint8_t)slot;
    *hm_dp(ram, r, LOAD_VRAM + 1) = 0;

    r->x = slot4;
    hm_bus_wr16(ram, (SLOT_USERS + slot4) & 0xFFFFFF, 0x0001);
    uint16_t old = hm_bus_rd16(ram, (SLOT_USERS + SLOT_PIECE + slot4) & 0xFFFFFF);
    if (old != 0xFFFF) {
      push16(ram, r, slot4);                               /* PHX ... PLX */
      hm_bus_wr8(ram, (PIECE_SLOT + old) & 0xFFFFFF, 0xFF);
      pull16(ram, r);
    }
    hm_bus_wr16(ram, (SLOT_USERS + SLOT_PIECE + slot4) & 0xFFFFFF, piece);
    hm_bus_wr8(ram, (PIECE_SLOT + piece) & 0xFFFFFF, (uint8_t)(slot4 >> 2));

    /* the piece's number: bits 0-5 the graphics, 6-7 a $2000 part, 8-9 the bank */
    uint16_t high = (uint16_t)(piece & 0xFFC0);
    uint16_t part = (uint16_t)(high >> 6);
    hm_dp_w16(ram, r, TEMP_7E, (uint16_t)((part >> 2) << 2));
    hm_dp_w16(ram, r, LOAD_SLOT, (uint16_t)((part >> 2) + 0x88));
    uint16_t low = (uint16_t)(piece - high);
    uint16_t source = graphics_times(ram, r, low, 0x20);
    hm_dp_w16(ram, r, LOAD_PIECE, source);
    uint16_t base = (uint16_t)((part & 3) << 13);       /* Table_of_Multof2000 */
    uint16_t sum = (uint16_t)(source + base + 0x8000);
    hm_dp_w16(ram, r, LOAD_SLOT4, sum);
    hm_dp_w16(ram, r, LOAD_VRAM, graphics_times(ram, r, slot & 0xFF, 0x10));

    uint16_t bf = hm_dp_r16(ram, r, COPY_AT);
    r->x = bf;
    hm_bus_wr16(ram, (LOAD_COPIES + bf) & 0xFFFFFF, sum);
    hm_bus_wr16(ram, (LOAD_COPIES + COPY_BANK + bf) & 0xFFFFFF, hm_dp_r16(ram, r, LOAD_SLOT));
    hm_bus_wr16(ram, (LOAD_COPIES + COPY_VRAM + bf) & 0xFFFFFF, hm_dp_r16(ram, r, LOAD_VRAM));
    hm_bus_wr16(ram, (LOAD_COPIES + COPY_SIZE + bf) & 0xFFFFFF, 0xFFFF);
    bf = (uint16_t)(bf + COPY_SIZE);
    if (bf == 0x00C0) trap();
    hm_dp_w16(ram, r, COPY_AT, bf);
  }
  uint8_t bb = *hm_dp(ram, r, QUEUE_OUT), bd = *hm_dp(ram, r, QUEUE_IN);
  uint8_t left = (uint8_t)(bb >= bd ? bb - bd : bd - bb);
  r->c = (left >> 1) & 1;
  left = (uint8_t)(left >> 2);
  r->a = (uint16_t)((r->a & 0xFF00) | left);
  hm_bus_wr8(ram, abs_db(r, QUEUE_LEFT), left);
  hm_bus_wr8(ram, abs_db(r, QUEUE_LEFT + 1), 0);
}

/* ─────────────────────────────────────────────────────────────────────────
 * PrepareOAMData ($85:83E0, JSL, RTL): the sprite table for this frame.
 * The WRAM copy of the OAM is emptied (ClearOAMCopyAgain) and the
 * objects put in drawing order ($084C, GetOBJOrderToDraw). Then, object by
 * object in that order (only the records in use), piece by piece from the
 * last to the first (pieces not loaded, $FF, are skipped):
 *   - the screen position: the object's ($9B, $9D) + the piece's offset in
 *     the sprite's piece list (signed bytes +2 / +3, mirrored by the flip
 *     $9F: InvertIfFlippedX / Y) - the camera ($F5, $F7);
 *   - a piece outside the screen (X not in -16..255, Y not in -16..239) is
 *     left out;
 *   - its 4 bytes at $7E:A000 + $AF: X, Y, the graphics slot's tile number
 *     ($86:8000 + 2 x slot) and the attributes: the palette (byte +4: bits
 *     0-2 x 2), the flip (bits 4-5 to 6-7), priority 2, xor the object's
 *     flip, | the graphic preset ($0195) x 16; the high table at
 *     $7E:A200 + $AF / 16 gets the X high bit ($85:8BD0).
 * Last, the DMA of the table to OAM is set up on channel 4 ($7E:A000, $220
 * bytes, OAM address 0) and flagged in $9A for the NMI.
 * Leftovers: A = $9A's new value with $AF's high byte, X = $220, Y = the
 * object count ($DC), C = 1 (CPY $DC); $AF = $084A = the table's size used.
 * ───────────────────────────────────────────────────────────────────────── */
static void set_piece_bank(uint8_t *ram, HmRegs *r, uint16_t sprite)
{
  uint8_t bank = sprite >= FIRST_SPRITE_87 ? 0x87 : 0x86;
  r->a = (uint16_t)((sprite & 0xFF00) | bank);
  *hm_dp(ram, r, SPRITE_ENTRY_BANK) = bank;
  *hm_dp(ram, r, PIECE_LIST_BANK) = bank;
}
/* the gobj record offset of the object drawn n-th */
static uint16_t ordered(uint8_t *ram, HmRegs *r, uint16_t n)
{
  r->x = (uint16_t)(hm_bus_rd8(ram, abs_x(r, DRAW_ORDER, n)) << 1);
  r->a = hm_bus_rd16(ram, GOBJ_OFFSETS + r->x);
  hm_dp_w16(ram, r, OBJ_OFFSET, r->a);
  return r->a;
}
/* a piece's offset byte, sign-extended, through InvertIfFliped X / Y */
static uint16_t piece_offset(uint8_t *ram, HmRegs *r, uint16_t y, uint16_t ret, void (*flip)(uint8_t *, HmRegs *))
{
  int8_t off = (int8_t)hm_bus_rd8(ram, hm_dp_long(ram, r, PIECE_LIST, y));
  r->a = (uint16_t)(int16_t)off;
  push16(ram, r, ret);                                     /* JSR */
  flip(ram, r);
  pull16(ram, r);
  return r->a;
}

void hm_PrepareOAMData(uint8_t *ram, HmRegs *r)
{
  set_piece_bank(ram, r, hm_dp_r16(ram, r, OBJ_SPRITE));
  push16(ram, r, 0x8401); hm_ClearOAMCopyAgain(ram, r); pull16(ram, r);
  push16(ram, r, 0x8404); hm_GetOBJOrderToDraw(ram, r); pull16(ram, r);

  ordered(ram, r, 0);
  hm_dp_w16(ram, r, OBJ_INDEX, 0);
  hm_dp_w16(ram, r, OAM_USED, 0);
  uint16_t y = 0;
  for (;;) {
    uint16_t obj = hm_dp_r16(ram, r, OBJ_OFFSET);
    r->x = obj;
    r->a = hm_bus_rd16(ram, abs_x(r, GOBJ, obj));
    if (r->a == 0) {                                       /* not in use: the next one */
      for (;;) {
        y++;
        ordered(ram, r, y);
        r->c = y >= hm_dp_r16(ram, r, GOBJ_COUNT);
        if (y == hm_dp_r16(ram, r, GOBJ_COUNT)) goto done;
        r->x = hm_dp_r16(ram, r, OBJ_OFFSET);
        r->a = hm_bus_rd16(ram, abs_x(r, GOBJ, r->x));
        if (r->a) break;
      }
      obj = hm_dp_r16(ram, r, OBJ_OFFSET);
    }
    /* an object */
    hm_dp_w16(ram, r, OBJ_INDEX, y);
    r->x = obj;
    set_piece_bank(ram, r, hm_bus_rd16(ram, abs_x(r, GOBJ + GOBJ_SPRITE, obj)));
    hm_dp_w16(ram, r, PIECE_LIST, hm_bus_rd16(ram, abs_x(r, GOBJ + GOBJ_PIECE_LIST, obj)));
    hm_dp_w16(ram, r, OBJ_FLIP, hm_bus_rd16(ram, abs_x(r, GOBJ + GOBJ_FLIP, obj)));
    hm_dp_w16(ram, r, OBJ_X, hm_bus_rd16(ram, abs_x(r, GOBJ + GOBJ_X, obj)));
    hm_dp_w16(ram, r, OBJ_Y, hm_bus_rd16(ram, abs_x(r, GOBJ + GOBJ_Y, obj)));
    *hm_dp(ram, r, PIECES_LEFT) = hm_bus_rd8(ram, abs_x(r, GOBJ + GOBJ_PIECE_COUNT, obj));
    *hm_dp(ram, r, PIECES_LEFT + 1) = 0;
    /* the last piece's entry: list + 1 + 5 (n - 1) */
    hm_dp_w16(ram, r, PIECE_LIST, (uint16_t)(hm_dp_r16(ram, r, PIECE_LIST) + 1));
    uint16_t n1 = (uint16_t)(hm_dp_r16(ram, r, PIECES_LEFT) - 1);
    hm_dp_w16(ram, r, TEMP_7E, n1);
    hm_dp_w16(ram, r, TEMP_80, (uint16_t)(n1 << 1));
    uint32_t s = (uint32_t)(uint16_t)(n1 << 1) + n1;                 /* CLC; ADC $7E */
    s = (uint32_t)(uint16_t)s + (uint16_t)(n1 << 1) + (s > 0xFFFF);  /* ADC $80 */
    s = (uint32_t)(uint16_t)s + hm_dp_r16(ram, r, PIECE_LIST) + (s > 0xFFFF); /* ADC $78 */
    hm_dp_w16(ram, r, PIECE_LIST, (uint16_t)s);
    hm_dp_w16(ram, r, OAM_PIECE_AT, (uint16_t)(obj + hm_dp_r16(ram, r, PIECES_LEFT) - 1)); /* the last piece */
    r->c = 1;                                              /* SBC #1: no borrow */

    for (;;) {                                             /* the pieces, last to first */
      y = hm_dp_r16(ram, r, OBJ_INDEX);
      r->y = y;
      r->a = hm_dp_r16(ram, r, PIECES_LEFT);
      if (r->a == 0) break;
      r->x = hm_dp_r16(ram, r, OAM_PIECE_AT);
      uint8_t slot = hm_bus_rd8(ram, abs_x(r, GOBJ + GOBJ_PIECE_SLOTS, r->x));
      if (slot != 0xFF) {
        uint16_t dx = piece_offset(ram, r, PIECE_DX, 0x84D6, hm_InvertIfFlippedX);
        hm_dp_w16(ram, r, SCREEN_X, (uint16_t)(dx + hm_dp_r16(ram, r, OBJ_X) - hm_dp_r16(ram, r, CAMERA_X)));
        uint16_t dy = piece_offset(ram, r, PIECE_DY, 0x84F7, hm_InvertIfFlippedY);
        uint32_t fy = (uint32_t)(uint16_t)(dy + hm_dp_r16(ram, r, OBJ_Y)) - hm_dp_r16(ram, r, CAMERA_Y);
        hm_dp_w16(ram, r, SCREEN_Y, (uint16_t)fy);
        hm_dp_w16(ram, r, TEMP_C5, 3);
        /* the high table: bits for 4 sprites a byte, 16 bytes a word */
        uint16_t at = hm_dp_r16(ram, r, OAM_USED);
        hm_dp_w16(ram, r, TEMP_7E, (uint16_t)(at & 0xFFE0));
        uint16_t word = (uint16_t)((at & 0xFFE0) >> 4);
        hm_dp_w16(ram, r, TEMP_80, word);
        hm_dp_w16(ram, r, TEMP_7E, (uint16_t)(at - (at & 0xFFE0)));
        uint16_t high = hm_bus_rd16(ram, (OAM_COPY_HIGH + word) & 0xFFFFFF);
        hm_dp_w16(ram, r, OAM_HIGH_BITS, high);
        uint16_t fx = hm_dp_r16(ram, r, SCREEN_X);
        r->x = word;
        if (fx >= 0x0100) {
          r->x = (uint16_t)((hm_dp_r16(ram, r, TEMP_7E) & 0xFFFC) >> 1);
          high |= hm_bus_rd16(ram, HIGH_X_BITS + r->x);
          hm_dp_w16(ram, r, OAM_HIGH_BITS, high);
        }
        uint16_t fyv = hm_dp_r16(ram, r, SCREEN_Y);
        bool on_screen = (fx < g_hm_oam_right || fx >= g_hm_oam_left) && (fyv < 0x00F0 || fyv >= 0xFFF0);
        if (on_screen) {
          r->x = at;
          hm_bus_wr8(ram, (OAM_COPY + at) & 0xFFFFFF, (uint8_t)fx);
          hm_bus_wr8(ram, (OAM_COPY + OAM_Y_BYTE + at) & 0xFFFFFF, (uint8_t)fyv);
          hm_bus_wr16(ram, (OAM_COPY_HIGH + word) & 0xFFFFFF, hm_dp_r16(ram, r, OAM_HIGH_BITS));
          uint8_t b = hm_bus_rd8(ram, hm_dp_long(ram, r, PIECE_LIST, PIECE_ATTR));
          *hm_dp(ram, r, OAM_FLIP_BITS) = b;
          *hm_dp(ram, r, OAM_PALETTE) = (uint8_t)((b << 1) & 0x0E); /* the palette */
          uint8_t f = (uint8_t)(((uint8_t)(b << 2) & 0xC0) >> 1);
          *hm_dp(ram, r, OAM_FLIP_BITS) = f;
          uint8_t attr = (uint8_t)(((uint8_t)(f << 2) | f) & 0xC0);
          attr = (uint8_t)((attr | *hm_dp(ram, r, OAM_PALETTE) | 0x20) ^ *hm_dp(ram, r, OBJ_FLIP));
          hm_bus_wr8(ram, (OAM_COPY + OAM_ATTR_BYTE + at) & 0xFFFFFF, attr);
          uint8_t preset = (uint8_t)(hm_bus_rd8(ram, abs_db(r, GRAPHIC_PRESET)) << 4);
          attr = (uint8_t)(preset | hm_bus_rd8(ram, (OAM_COPY + OAM_ATTR_BYTE + at) & 0xFFFFFF));
          hm_bus_wr8(ram, (OAM_COPY + OAM_ATTR_BYTE + at) & 0xFFFFFF, attr);
          r->y = 4;
          r->x = (uint16_t)(slot << 1);
          r->a = hm_bus_rd16(ram, SLOT_TILES + r->x);
          r->x = at;
          hm_bus_wr8(ram, (OAM_COPY + OAM_TILE_BYTE + at) & 0xFFFFFF, (uint8_t)r->a);
          hm_dp_w16(ram, r, OAM_USED, (uint16_t)(at + 4));
        }
      }
      r->a = (uint16_t)(hm_dp_r16(ram, r, PIECE_LIST) - PIECE_ENTRY_SIZE); /* the piece before */
      hm_dp_w16(ram, r, PIECE_LIST, r->a);
      hm_dp_w16(ram, r, OAM_PIECE_AT, (uint16_t)(hm_dp_r16(ram, r, OAM_PIECE_AT) - 1));
      hm_dp_w16(ram, r, PIECES_LEFT, (uint16_t)(hm_dp_r16(ram, r, PIECES_LEFT) - 1));
    }
    /* no pieces left: the next object (Y = $AB) */
    for (;;) {
      y++;
      ordered(ram, r, y);
      r->c = y >= hm_dp_r16(ram, r, GOBJ_COUNT);
      if (y == hm_dp_r16(ram, r, GOBJ_COUNT)) goto done;
      r->x = hm_dp_r16(ram, r, OBJ_OFFSET);
      r->a = hm_bus_rd16(ram, abs_x(r, GOBJ, r->x));
      if (r->a) break;
    }
  }
done:
  r->y = y;
  r->a = hm_dp_r16(ram, r, OAM_USED);
  hm_bus_wr16(ram, abs_db(r, OAM_SIZE_USED), r->a);
  r->x = 0;
  hm_bus_wr16(ram, abs_db(r, OAMADDL), 0);                /* OAM address 0 */
  hm_bus_wr8(ram, abs_db(r, DMAP4), 0x00);
  hm_bus_wr8(ram, abs_db(r, BBAD4), 0x04);                 /* OAMDATA */
  hm_bus_wr8(ram, abs_db(r, A1T4L), (uint8_t)OAM_COPY);
  hm_bus_wr8(ram, abs_db(r, A1T4H), (uint8_t)(OAM_COPY >> 8));
  hm_bus_wr8(ram, abs_db(r, A1B4), (uint8_t)(OAM_COPY >> 16));
  r->x = 0x0220;
  hm_bus_wr16(ram, abs_db(r, DAS4L), 0x0220);
  uint8_t flag = (uint8_t)(*hm_dp(ram, r, NMI_DMA_CHANNELS) | 0x10);
  r->a = (uint16_t)((r->a & 0xFF00) | flag);
  *hm_dp(ram, r, NMI_DMA_CHANNELS) = flag;
}
