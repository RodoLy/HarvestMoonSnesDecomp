/*
 * toolused.c — what each tool does when it lands (the ToolUsed* routines,
 * bank $82; table Tool_Used_Table at $82:A5C3, indexed by the item in hand
 * $0921 and called by SUB_829260). All are JSR / RTS and start with an
 * 8-bit A.
 *
 * Most follow the same plan:
 *   1. PreCheckToolSuccess (or its Square / Line versions for seeds and the
 *      gold tools): does the tool work on the tile in front ($0985/$0987)?
 *      It leaves A = the answer and X = the tile's type.
 *   2. ChangeMapTile: put the new tile (tilled soil, a stump...).
 *   3. SpawnMapObject: an effect sprite on the tile ($0974-$0982 say which).
 *   4. Sometimes a find (1 chance in n, at most once a day or once ever,
 *      with flags in $7F1F5A-$7F1F6A) that starts an event (SUB_8480F8).
 *   5. The player goes back to idle ($D4 = 0) and loses stamina:
 *      ChangeStamina with A = $FE (2), $FF (1), $FC (4) or $F8 (8).
 *
 * The gold tools act on several tiles, one per call ($096B counts them):
 * the gold sickle and the seeds on the 9 tiles around the player, the gold
 * hoe on up to 6 tiles in a line (it stops at the first that refuses).
 * The stamina is paid once, after the last tile.
 *
 * $7F1F60 bit 3 also freezes the stamina (see stamina.c): while it is set
 * nothing is ever found either (except by the gold axe, which forgets to
 * check it).
 *
 * About the C flag: when the stamina is frozen ChangeStamina returns
 * without touching C, so what the routine leaves in C is whatever the last
 * comparison on the way set. The rewrites keep it (helpers `is`, `below`,
 * `add_c`, `sub_c`) so the checker can compare it.
 *
 * Every JSR / JSL return address below was read from the ROM
 * (tools/calls.py), never computed by hand.
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

#define HM_PLAYER_X        0xD6     /* direct page, 16b, pixels */
#define HM_PLAYER_Y        0xD8
#define HM_TILE_IN_FRONT_X 0x00985  /* 16b: where the tool lands */
#define HM_TILE_IN_FRONT_Y 0x00987
#define HM_SOW_STEP        0x0096B  /* 8b: which tile of a multi-tile use */
#define HM_HITS            0x0096D  /* 8b: hits on a big stone or a stump */
#define HM_GRASS_ON_FARM   0x0092E  /* 16b: one less each tuft cut */
#define HM_SEEDS_GRASS     0x00927  /* 8b bags: grass, corn, tomato, ... */
#define HM_SEEDS_CORN      0x00928
#define HM_SEEDS_TOMATO    0x00929
#define HM_SEEDS_POTATO    0x0092A
#define HM_SEEDS_TURNIP    0x0092B
#define HM_COW_FEED        0x0092C  /* 8b: portions in the bag */
#define HM_CHICKEN_FEED    0x0092D
#define HM_COWS_FED        0x00930  /* 8b: troughs filled today */
#define HM_CHICKENS_FED    0x00931
#define HM_COW_TROUGHS     0x00932  /* 16b: which troughs have food */
#define HM_CHICKEN_TROUGHS 0x00934
#define HM_HOUR            0x11F1C  /* $7F1F1C, 8b */
#define HM_PLANTED_GRASS   0x11F29  /* $7F1F29, 16b */
#define FLAGS_5A           0x11F5A  /* $7F1F5A... 16-bit flag words */
#define FLAGS_5C           0x11F5C
#define FLAGS_5E           0x11F5E
#define FLAGS_60           0x11F60
#define FLAGS_64           0x11F64
#define FLAGS_66           0x11F66
#define FLAGS_68           0x11F68
#define FLAGS_6A           0x11F6A
#define NO_FINDS           0x0008   /* FLAGS_60 */
#define TROUGH_BITS        0x82A571 /* ROM: dw 1 << n, n = 0..12 */

enum { SPRING, SUMMER, FALL, WINTER };
#define FARM_MAPS 0x04              /* maps 0-3 are the farm (per season) */
#define MAP_BARN  0x27
#define MAP_COOP  0x28

/* effect sprites */
#define FX_GRASS_CUT  0x00C1
#define FX_CUT        0x00B7
#define FX_STONE      0x00C8
#define FX_BIG_STONE  0x00C9
#define FX_HIT        0x00CB
#define FX_STUMP      0x00B8
#define FX_FEED       0x00C4

/* ── small helpers ───────────────────────────────────────────────────── */
static void push16(uint8_t *ram, HmRegs *r, uint16_t v)
{
  hm_push8(ram, r, (uint8_t)(v >> 8));
  hm_push8(ram, r, (uint8_t)v);
}
static void jsr(uint8_t *ram, HmRegs *r, uint16_t ret, void (*fn)(uint8_t *, HmRegs *))
{
  push16(ram, r, ret);
  fn(ram, r);
  hm_pull8(ram, r); hm_pull8(ram, r);                    /* RTS */
}
static void jsl(uint8_t *ram, HmRegs *r, uint16_t ret, uint8_t bank,
                void (*fn)(uint8_t *, HmRegs *))
{
  hm_jsl(ram, r, ret, bank);
  fn(ram, r);
  hm_rtl(ram, r);
}
/* CMP / CPX: C = v >= k */
static bool is(HmRegs *r, uint16_t v, uint16_t k) { r->c = v >= k; return v == k; }
static bool below(HmRegs *r, uint16_t v, uint16_t k) { r->c = v >= k; return v < k; }
static uint16_t add_c(HmRegs *r, uint16_t a, uint16_t b)
{
  r->c = (uint32_t)a + b > 0xFFFF;
  return r->a = (uint16_t)(a + b);
}
static uint16_t sub_c(HmRegs *r, uint16_t a, uint16_t b)
{
  r->c = a >= b;
  return r->a = (uint16_t)(a - b);
}
/* SEP #$20; LDA byte: the low byte of A is replaced */
static uint8_t load8(HmRegs *r, uint8_t v)
{
  r->a = (uint16_t)((r->a & 0xFF00) | v);
  return v;
}
/* LDA flags; AND #bit (A keeps the result) */
static bool test(uint8_t *ram, HmRegs *r, uint32_t at, uint16_t bit)
{
  r->a = hm_rd16(ram, at) & bit;
  return r->a != 0;
}
/* LDA flags; ORA #bit; STA flags */
static void set_flag(uint8_t *ram, HmRegs *r, uint32_t at, uint16_t bit)
{
  r->a = hm_rd16(ram, at) | bit;
  hm_wr16(ram, at, r->a);
}
static uint16_t tile_x(uint8_t *ram) { return hm_rd16(ram, HM_TILE_IN_FRONT_X); }
static uint16_t tile_y(uint8_t *ram) { return hm_rd16(ram, HM_TILE_IN_FRONT_Y); }
static uint8_t map_id(uint8_t *ram, HmRegs *r) { return *hm_dp(ram, r, HM_MAP_ID); }

/* SEP #$20; LDA #faces; JSL RNGReturn0toA: true on the 1-in-faces roll */
static bool one_in(uint8_t *ram, HmRegs *r, uint8_t faces, uint16_t ret)
{
  load8(r, faces);
  jsl(ram, r, ret, 0x80, hm_RNGReturn0toA);
  return (uint8_t)r->a == 0;                              /* TXA sets Z */
}
/* JSR PreCheckToolSuccess...: true if the tool works (A != 0); X = tile */
static bool tool_works(uint8_t *ram, HmRegs *r, uint16_t ret,
                       void (*check)(uint8_t *, HmRegs *))
{
  jsr(ram, r, ret, check);
  return r->a != 0;
}
/* the multi-tile tools first load the step: LDA #0; XBA; LDA $096B */
static bool tool_works_at_step(uint8_t *ram, HmRegs *r, uint16_t ret,
                               void (*check)(uint8_t *, HmRegs *))
{
  r->a = ram[HM_SOW_STEP];
  return tool_works(ram, r, ret, check);
}
static void change_tile(uint8_t *ram, HmRegs *r, uint16_t change, uint16_t x, uint16_t y,
                        uint16_t ret)
{
  r->a = change; r->x = x; r->y = y;
  jsl(ram, r, ret, 0x81, hm_ChangeMapTile);
}
static void change_tile_in_front(uint8_t *ram, HmRegs *r, uint16_t change, uint16_t ret)
{
  change_tile(ram, r, change, tile_x(ram), tile_y(ram), ret);
}
/* an effect sprite at x, y: flags 3 (in use + bit 1), routine 0 */
static void effect(uint8_t *ram, HmRegs *r, uint16_t x, uint16_t y, uint16_t sprite,
                   uint16_t ret)
{
  hm_wr16(ram, 0x0980, x);
  hm_wr16(ram, 0x0982, y);
  hm_wr16(ram, 0x097A, sprite);
  hm_wr16(ram, 0x097E, 0);
  ram[0x0974] = 3; ram[0x0975] = 0; ram[0x0976] = 0;
  r->a = 0;
  jsl(ram, r, ret, 0x81, hm_SpawnMapObject);
}
static void effect_in_front(uint8_t *ram, HmRegs *r, uint16_t sprite, uint16_t ret)
{
  effect(ram, r, tile_x(ram), tile_y(ram), sprite, ret);
}
/* SUB_8480F8: start event A (X, Y its arguments) */
static void event(uint8_t *ram, HmRegs *r, uint16_t a, uint16_t x, uint16_t y, uint16_t ret)
{
  r->a = a; r->x = x; r->y = y;
  jsl(ram, r, ret, 0x84, hm_SUB_8480F8);
}
static void add(uint8_t *ram, HmRegs *r, void (*fn)(uint8_t *, HmRegs *), uint16_t n,
                uint16_t ret)
{
  r->a = n;
  jsl(ram, r, ret, 0x83, fn);
}
static void add_money(uint8_t *ram, HmRegs *r, uint16_t tens_of_g, uint16_t ret)
{
  r->a = tens_of_g;
  hm_dp_w16(ram, r, 0x72, tens_of_g);
  r->a = 0;                                              /* SEP; LDA #0; STA $74 */
  *hm_dp(ram, r, 0x74) = 0;
  jsl(ram, r, ret, 0x83, hm_AddMoney);
}
static void idle(uint8_t *ram, HmRegs *r)
{
  r->a = 0;
  hm_dp_w16(ram, r, HM_PLAYER_ACTION, 0);
}
static void lose_stamina(uint8_t *ram, HmRegs *r, uint8_t stamina, uint16_t ret)
{
  load8(r, stamina);                                     /* SEP #$20; LDA #$Fx */
  r->n = 1;                                              /* ChangeStamina reads N */
  jsl(ram, r, ret, 0x81, hm_ChangeStamina);
}
/* Back to idle, then lose stamina (stamina = $FE: 2, $FF: 1, ...). */
static void tool_finished(uint8_t *ram, HmRegs *r, uint8_t stamina, uint16_t ret)
{
  idle(ram, r);
  lose_stamina(ram, r, stamina, ret);
}
/* The multi-tile tools: next tile; after the last one, step back to 0 and
 * 8 stamina. Returns at once (RTS) otherwise. */
static void gold_done(uint8_t *ram, HmRegs *r, uint16_t ret)
{
  ram[HM_SOW_STEP] = 0;
  tool_finished(ram, r, 0xF8, ret);
}
static void gold_step(uint8_t *ram, HmRegs *r, uint8_t tiles, uint16_t ret)
{
  if (is(r, load8(r, ++ram[HM_SOW_STEP]), tiles))
    gold_done(ram, r, ret);
}

/* ─────────────────────────────────────────────────────────────────────────
 * ToolUsedNoAction ($82:931E) and ToolUsedMilker ($82:9CC0): only back to
 * idle (milking happens elsewhere). ToolUsedUNK ($82:A570): nothing at
 * all (an RTS). Leftover A = 0.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_ToolUsedNoAction(uint8_t *ram, HmRegs *r) { idle(ram, r); }
void hm_ToolUsedMilker(uint8_t *ram, HmRegs *r)   { idle(ram, r); }
void hm_ToolUsedUNK(uint8_t *ram, HmRegs *r)      { (void)ram; (void)r; }

/* ToolUsedBell ($82:9B51), ToolUsedBrush ($82:9CC8): idle, 1 stamina. */
void hm_ToolUsedBell(uint8_t *ram, HmRegs *r)  { tool_finished(ram, r, 0xFF, 0x9B5F); }
void hm_ToolUsedBrush(uint8_t *ram, HmRegs *r) { tool_finished(ram, r, 0xFF, 0x9CD6); }

/* ToolUsedBlueFeather ($82:A3FD): idle and $7F1F5E bit $0040 (the proposal
 * code reads it). Leftover A = the flags. */
void hm_ToolUsedBlueFeather(uint8_t *ram, HmRegs *r)
{
  idle(ram, r);
  set_flag(ram, r, FLAGS_5E, 0x0040);
}

/* ─────────────────────────────────────────────────────────────────────────
 * ToolUsedCowMedicine ($82:9B11) / ToolUsedMiraclePotion ($82:9B31): used
 * up (the hand is emptied), 1 stamina, and a flag the cow code reads
 * ($7F1F5A bit $0080 medicine, $0100 potion). Leftover A = the flags.
 * ───────────────────────────────────────────────────────────────────────── */
static void give_to_cow(uint8_t *ram, HmRegs *r, uint16_t flag, uint16_t ret)
{
  idle(ram, r);
  ram[HM_TOOL_SELECTED] = 0;
  lose_stamina(ram, r, 0xFF, ret);
  set_flag(ram, r, FLAGS_5A, flag);
}
void hm_ToolUsedCowMedicine(uint8_t *ram, HmRegs *r)   { give_to_cow(ram, r, 0x0080, 0x9B22); }
void hm_ToolUsedMiraclePotion(uint8_t *ram, HmRegs *r) { give_to_cow(ram, r, 0x0100, 0x9B42); }

/* ─────────────────────────────────────────────────────────────────────────
 * Cutting (sickle and gold sickle). On the farm: a weed (tile 03) becomes
 * ground (change $0E), grass (79) is cut into fodder (+1 grass in the
 * silo, change $52, one tuft less in $092E), anything else becomes change
 * $17; off the farm, change $DD ($DE on tile 35). Then, in summer only and
 * once a day, 1 in 16 a frog jumps out (event $12, +2 happiness; $7F1F5A
 * bit $2000). Neither works in winter.
 * ───────────────────────────────────────────────────────────────────────── */
typedef struct CutRets {
  uint16_t grass, grass_tile, grass_fx, tile, fx, roll, frog, happy;
} CutRets;

static void cut(uint8_t *ram, HmRegs *r, const CutRets *ret)
{
  uint16_t tile = r->x;
  uint16_t change;
  if (!below(r, load8(r, map_id(ram, r)), FARM_MAPS)) {
    change = is(r, tile, 0x35) ? 0x00DE : 0x00DD;
  } else if (is(r, tile, 0x03)) {
    change = 0x000E;                                     /* weed */
  } else if (is(r, tile, 0x79)) {                        /* grass: fodder */
    add(ram, r, hm_AddGrass, 1, ret->grass);
    hm_wr16(ram, HM_GRASS_ON_FARM, r->a = (uint16_t)(hm_rd16(ram, HM_GRASS_ON_FARM) - 1));
    change_tile_in_front(ram, r, 0x0052, ret->grass_tile);
    effect_in_front(ram, r, FX_GRASS_CUT, ret->grass_fx);
    goto frog;
  } else {
    change = 0x0017;
  }
  change_tile_in_front(ram, r, change, ret->tile);
  effect_in_front(ram, r, FX_CUT, ret->fx);
frog:
  if (is(r, load8(r, ram[HM_SEASON]), SUMMER) && !test(ram, r, FLAGS_60, NO_FINDS)
      && !test(ram, r, FLAGS_5A, 0x2000) && one_in(ram, r, 16, ret->roll)) {
    event(ram, r, 0x0012, 0, 0x0032, ret->frog);
    add(ram, r, hm_AddPlayerHappiness, 2, ret->happy);
    set_flag(ram, r, FLAGS_5A, 0x2000);
  }
}

/* ToolUsedSickle ($82:9328) */
void hm_ToolUsedSickle(uint8_t *ram, HmRegs *r)
{
  static const CutRets ret = { 0x937C, 0x9394, 0x93C5, 0x93D1, 0x9402, 0x9428, 0x9439, 0x9442 };
  if (tool_works(ram, r, 0x932C, hm_PreCheckToolSuccess)
      && !is(r, load8(r, ram[HM_SEASON]), WINTER))
    cut(ram, r, &ret);
  tool_finished(ram, r, 0xFE, 0x945E);
}

/* ToolUsedGoldSickle ($82:9D42): the 9 tiles around the player, 8 stamina
 * at the end. Quirk: in winter, on a tile it could cut, it returns without
 * advancing the step (A = 3 from the season). */
void hm_ToolUsedGoldSickle(uint8_t *ram, HmRegs *r)
{
  static const CutRets ret = { 0x9D9A, 0x9DB2, 0x9DE3, 0x9DEF, 0x9E20, 0x9E46, 0x9E57, 0x9E60 };
  if (tool_works_at_step(ram, r, 0x9D4E, hm_PreCheckToolSuccessSquare)) {
    if (is(r, load8(r, ram[HM_SEASON]), WINTER)) return;
    cut(ram, r, &ret);
  }
  gold_step(ram, r, 9, 0x9E8C);
}

/* ─────────────────────────────────────────────────────────────────────────
 * Digging (hoe and gold hoe): the tile is tilled (change $17), then,
 * unless NO_FINDS:
 *   - spring / summer, once a day, 1 in 32: a mole (event $11; $7F1F5A
 *     bit $1000; the hoe also clears $093A) — and nothing else this time;
 *   - once a day ($7F1F5C bit $0200), 1 in 16 a coin (10 G, event $0F/$3A)
 *     or else 1 in 16 a money bag (50 G, event $0F/$3B);
 *   - 1 in 64, once a day ($7F1F5C bit $0100): a power berry (event $10),
 *     but only the first two times ever ($7F1F64 bits $0800, $1000).
 * ───────────────────────────────────────────────────────────────────────── */
typedef struct DigRets {
  uint16_t tile, mole_roll, mole, coin_roll, coin, coin_money, bag_roll, bag, bag_money,
           berry_roll, berry;
} DigRets;

static void dig(uint8_t *ram, HmRegs *r, const DigRets *ret, bool mole_clears_093a)
{
  change_tile_in_front(ram, r, 0x0017, ret->tile);

  if (below(r, load8(r, ram[HM_SEASON]), FALL) && !test(ram, r, FLAGS_60, NO_FINDS)
      && !test(ram, r, FLAGS_5A, 0x1000) && one_in(ram, r, 32, ret->mole_roll)) {
    event(ram, r, 0x0011, 0, 0x0030, ret->mole);         /* a mole */
    if (mole_clears_093a) ram[0x093A] = 0;
    set_flag(ram, r, FLAGS_5A, 0x1000);
    return;
  }

  if (!test(ram, r, FLAGS_60, NO_FINDS) && !test(ram, r, FLAGS_5C, 0x0200)) {
    if (one_in(ram, r, 16, ret->coin_roll)) {            /* a coin */
      set_flag(ram, r, FLAGS_5C, 0x0200);
      event(ram, r, 0x000F, 0, 0x003A, ret->coin);
      add_money(ram, r, 1, ret->coin_money);
      return;
    }
    if (one_in(ram, r, 16, ret->bag_roll)) {             /* a money bag */
      set_flag(ram, r, FLAGS_5C, 0x0200);
      event(ram, r, 0x000F, 0, 0x003B, ret->bag);
      add_money(ram, r, 5, ret->bag_money);
      return;
    }
  }

  if (one_in(ram, r, 64, ret->berry_roll) && !test(ram, r, FLAGS_60, NO_FINDS)
      && !test(ram, r, FLAGS_5C, 0x0100)) {
    set_flag(ram, r, FLAGS_5C, 0x0100);
    if (!test(ram, r, FLAGS_64, 0x0800)) {
      set_flag(ram, r, FLAGS_64, 0x0800);
    } else {
      if (test(ram, r, FLAGS_64, 0x1000)) return;
      set_flag(ram, r, FLAGS_64, 0x1000);
    }
    event(ram, r, 0x0010, 0, 0x001F, ret->berry);        /* a power berry */
  }
}

/* ToolUsedHoe ($82:9460) */
void hm_ToolUsedHoe(uint8_t *ram, HmRegs *r)
{
  static const DigRets ret = { 0x9478, 0x949E, 0x94AF, 0x94E0, 0x94FC, 0x950D, 0x9518,
                               0x9536, 0x9547, 0x9551, 0x95AD };
  if (tool_works(ram, r, 0x9464, hm_PreCheckToolSuccess))
    dig(ram, r, &ret, true);
  tool_finished(ram, r, 0xFE, 0x95BE);
}

/* ToolUsedGoldHoe ($82:9E8E): up to 6 tiles in a line, stopping at the
 * first one it can't till; 8 stamina at the end. */
void hm_ToolUsedGoldHoe(uint8_t *ram, HmRegs *r)
{
  static const DigRets ret = { 0x9EAE, 0x9ED6, 0x9EE7, 0x9F13, 0x9F2F, 0x9F40, 0x9F4B,
                               0x9F69, 0x9F7A, 0x9F84, 0x9FE0 };
  if (!tool_works_at_step(ram, r, 0x9E9A, hm_PreCheckToolSuccessLine)) {
    gold_done(ram, r, 0xA003);
    return;
  }
  dig(ram, r, &ret, false);
  gold_step(ram, r, 6, 0xA003);
}

/* The 2x2 big stones and stumps: X = which quarter was hit. The code walks
 * the tile in front to the top-left quarter ($10 pixels per tile): it
 * steps up-left, then right, then down-left, then right, stopping at the
 * first step whose quarter matches. Returns which of the two tile kinds
 * matched (0 / 1), or 2 if none. */
#define NONE 0xFFFF   /* no comparison at this step */
static bool step_matches(HmRegs *r, uint16_t tile, uint16_t a, uint16_t b, int *which)
{
  if (a != NONE && is(r, tile, a)) { *which = 0; return true; }
  if (b != NONE && is(r, tile, b)) { *which = 1; return true; }
  return false;
}
static int find_top_left(uint8_t *ram, HmRegs *r, uint16_t tile, const uint16_t q[4][2])
{
  int which = 2;
  hm_wr16(ram, HM_TILE_IN_FRONT_X, sub_c(r, tile_x(ram), 0x10));
  hm_wr16(ram, HM_TILE_IN_FRONT_Y, sub_c(r, tile_y(ram), 0x10));
  if (step_matches(r, tile, q[0][0], q[0][1], &which)) return which;
  hm_wr16(ram, HM_TILE_IN_FRONT_X, add_c(r, tile_x(ram), 0x10));
  if (step_matches(r, tile, q[1][0], q[1][1], &which)) return which;
  hm_wr16(ram, HM_TILE_IN_FRONT_X, sub_c(r, tile_x(ram), 0x10));
  hm_wr16(ram, HM_TILE_IN_FRONT_Y, add_c(r, tile_y(ram), 0x10));
  if (step_matches(r, tile, q[2][0], q[2][1], &which)) return which;
  hm_wr16(ram, HM_TILE_IN_FRONT_X, add_c(r, tile_x(ram), 0x10));
  if (step_matches(r, tile, q[3][0], q[3][1], &which)) return which;
  return 2;
}
/* a 2x2 thing broken: change on its top-left tile, effect in its middle */
static void break_2x2(uint8_t *ram, HmRegs *r, uint16_t change, uint16_t fx,
                      uint16_t ret_tile, uint16_t ret_fx)
{
  change_tile_in_front(ram, r, change, ret_tile);
  uint16_t x = add_c(r, tile_x(ram), 8), y = add_c(r, tile_y(ram), 8);
  effect(ram, r, x, y, fx, ret_fx);
}

/* ─────────────────────────────────────────────────────────────────────────
 * Hammering (hammer and gold hammer).
 *   - Small stone (tile 06): change $0E.
 *   - Rock (tile 04): change $0E on the farm, $E3 in the caves (maps $29+),
 *     $DD elsewhere; effect $C8.
 *   - Big stone (the other tiles): the hammer breaks it on the 6th hit
 *     ($096D counts; earlier hits just show effect $CB), the gold hammer
 *     at once: change $0D on the farm / $DF elsewhere on its top-left
 *     corner, effect $C9 in its middle.
 *   - Where the hammer does nothing: on maps $0C-$0F, tile F8 (a rock by
 *     the spring), event $15, and once a day 1 in 4 a power berry (only
 *     once ever: $7F1F64 bit $0200; daily $7F1F5C bit $0080).
 * The hammer costs 2 stamina, the gold hammer 4.
 * ───────────────────────────────────────────────────────────────────────── */
typedef struct HammerRets {
  uint16_t check, rock_tile, rock_fx, small_tile, big_tile, big_fx, hit_fx, stamina,
           spring, roll, berry;
} HammerRets;
static const uint16_t BIG_STONE[4][2] = {
  { 0x10, NONE }, { 0x0F, NONE }, { 0x0E, NONE }, { NONE, NONE } };

static void hammer(uint8_t *ram, HmRegs *r, const HammerRets *ret, bool gold)
{
  if (!tool_works(ram, r, ret->check, hm_PreCheckToolSuccess)) {
    uint8_t map = load8(r, map_id(ram, r));
    if (below(r, map, 0x0C) || !below(r, map, 0x10) || !is(r, r->x, 0x00F8)
        || test(ram, r, FLAGS_5C, 0x0080))
      goto done;
    r->a = 0x0015; r->x = 0; r->y = 0x0016;
    jsl(ram, r, ret->spring, 0x84, hm_UNK_LoadCCDataShort);
    if (!one_in(ram, r, 4, ret->roll) || test(ram, r, FLAGS_64, 0x0200)) goto done;
    set_flag(ram, r, FLAGS_64, 0x0200);
    hm_wr16(ram, HM_TILE_IN_FRONT_X, add_c(r, hm_dp_r16(ram, r, HM_PLAYER_X), 0x10));
    hm_wr16(ram, HM_TILE_IN_FRONT_Y, sub_c(r, hm_dp_r16(ram, r, HM_PLAYER_Y), 0x10));
    event(ram, r, 0x0010, 0, 0x001F, ret->berry);        /* a power berry */
    set_flag(ram, r, FLAGS_5C, 0x0080);
    goto done;
  }

  uint16_t tile = r->x;
  if (is(r, tile, 0x06)) {                               /* small stone */
    change_tile_in_front(ram, r, 0x000E, ret->small_tile);
    goto done;
  }
  if (is(r, tile, 0x04)) {                               /* rock */
    uint8_t map = load8(r, map_id(ram, r));
    uint16_t change = below(r, map, FARM_MAPS) ? 0x000E
                    : !below(r, load8(r, map), 0x29) ? 0x00E3 : 0x00DD;
    change_tile_in_front(ram, r, change, ret->rock_tile);
    effect_in_front(ram, r, FX_STONE, ret->rock_fx);
    goto done;
  }

  if (!gold) {                                           /* big stone */
    if (!is(r, load8(r, ++ram[HM_HITS]), 6)) {
      effect_in_front(ram, r, FX_HIT, ret->hit_fx);
      goto done;
    }
    ram[HM_HITS] = 0;
  }
  find_top_left(ram, r, tile, BIG_STONE);
  break_2x2(ram, r, below(r, load8(r, map_id(ram, r)), FARM_MAPS) ? 0x000D : 0x00DF,
            FX_BIG_STONE, ret->big_tile, ret->big_fx);
done:
  tool_finished(ram, r, gold ? 0xFC : 0xFE, ret->stamina);
}

/* ToolUsedHammer ($82:95C0), ToolUsedGoldHammer ($82:A005) */
void hm_ToolUsedHammer(uint8_t *ram, HmRegs *r)
{
  static const HammerRets ret = { 0x95C4, 0x95FE, 0x962F, 0x9641, 0x96C2, 0x96FB, 0x972E,
                                  0x973D, 0x976B, 0x9773, 0x97AC };
  hammer(ram, r, &ret, false);
}
void hm_ToolUsedGoldHammer(uint8_t *ram, HmRegs *r)
{
  static const HammerRets ret = { 0xA009, 0xA046, 0xA077, 0xA089, 0xA0F7, 0xA130, 0,
                                  0xA13F, 0xA16D, 0xA175, 0xA1AE };
  hammer(ram, r, &ret, true);
}

/* ─────────────────────────────────────────────────────────────────────────
 * Chopping (axe and gold axe). A stump breaks on the 6th hit of the axe
 * ($096D; earlier hits show effect $CB), at once with the gold axe: +6
 * wood, effect $B8 in its middle, and its top-left corner becomes change
 * $0D on the farm; off the farm $DF (stumps of quarters 09-0C, with once
 * ever 1 in 16 a power berry: $7F1F64 bit $0400) or $E0 (quarters 11-14).
 * Where the axe does nothing: on the mountain maps $10-$13, tile F4, an
 * event (once: $7F1F6A bit $0020; not while $0196 has bits $1A).
 * The axe costs 2 stamina, the gold axe 8.
 * ───────────────────────────────────────────────────────────────────────── */
typedef struct AxeRets { uint16_t roll, berry, tile, fx, wood; } AxeRets;
static const uint16_t STUMP[4][2] = { { 0x0C, 0x14 }, { 0x0B, 0x13 }, { 0x0A, 0x12 }, { 0x09, 0x11 } };

static void chop(uint8_t *ram, HmRegs *r, const AxeRets *ret, bool check_no_finds)
{
  uint16_t change;
  if (find_top_left(ram, r, r->x, STUMP) == 1) {
    change = 0x00E0;
  } else if (below(r, load8(r, map_id(ram, r)), FARM_MAPS)) {
    change = 0x000D;
  } else {
    if ((!check_no_finds || !test(ram, r, FLAGS_60, NO_FINDS))
        && one_in(ram, r, 16, ret->roll) && !test(ram, r, FLAGS_64, 0x0400)) {
      set_flag(ram, r, FLAGS_64, 0x0400);
      hm_wr16(ram, HM_TILE_IN_FRONT_X, add_c(r, tile_x(ram), 8));
      hm_wr16(ram, HM_TILE_IN_FRONT_Y, add_c(r, tile_y(ram), 8));
      event(ram, r, 0x0010, 0, 0x001F, ret->berry);      /* a power berry */
      hm_wr16(ram, HM_TILE_IN_FRONT_X, sub_c(r, tile_x(ram), 8));
      hm_wr16(ram, HM_TILE_IN_FRONT_Y, sub_c(r, tile_y(ram), 8));
    }
    change = 0x00DF;
  }
  break_2x2(ram, r, change, FX_STUMP, ret->tile, ret->fx);
  add(ram, r, hm_AddWood, 6, ret->wood);
}

/* ToolUsedAxe ($82:97BD) */
void hm_ToolUsedAxe(uint8_t *ram, HmRegs *r)
{
  static const AxeRets ret = { 0x9863, 0x989E, 0x98D3, 0x990C, 0x9915 };
  if (!tool_works(ram, r, 0x97C1, hm_PreCheckToolSuccess)) {
    uint8_t map = load8(r, map_id(ram, r));
    if (below(r, map, 0x10) || !below(r, map, 0x14) || !is(r, r->x, 0x00F4)
        || test(ram, r, 0x00196, 0x001A) || test(ram, r, FLAGS_6A, 0x0020))
      goto done;
    set_flag(ram, r, FLAGS_6A, 0x0020);
    r->a = 0; r->x = 0x0017; r->y = 0;
    jsl(ram, r, 0x9988, 0x84, hm_UNK_LoadCCDataLong);
    goto done;
  }
  if (!is(r, load8(r, ++ram[HM_HITS]), 6)) {
    effect_in_front(ram, r, FX_HIT, 0x9948);
    goto done;
  }
  ram[HM_HITS] = 0;
  chop(ram, r, &ret, true);
done:
  tool_finished(ram, r, 0xFE, 0x9997);
}

/* ToolUsedGoldAxe ($82:A1BF) */
void hm_ToolUsedGoldAxe(uint8_t *ram, HmRegs *r)
{
  static const AxeRets ret = { 0xA244, 0xA27F, 0xA2B4, 0xA2ED, 0xA2F6 };
  if (tool_works(ram, r, 0xA1C3, hm_PreCheckToolSuccess))
    chop(ram, r, &ret, false);
  tool_finished(ram, r, 0xF8, 0xA305);
}

/* ─────────────────────────────────────────────────────────────────────────
 * Seeds: each call sows one of the 9 tiles around the player ($096B =
 * which one, PreCheckToolSuccessSquare). The crop only takes in its
 * season; otherwise the tile gets change $EB. After the 9th tile, one bag
 * less (empty: the hand is emptied), back to idle and 1 stamina; before
 * that the routine returns right away (A = the step, high byte left over).
 *   corn $19 / tomato $1A: summer;  potato $1B / turnip $1C: spring;
 *   grass $1E: spring and summer (and +1 planted grass), else $ED.
 * ───────────────────────────────────────────────────────────────────────── */
typedef struct Sowing {
  uint16_t bag;                         /* its seed count */
  uint16_t ret_check, ret_tile, ret_stamina;
} Sowing;

static void sow_step_done(uint8_t *ram, HmRegs *r, const Sowing *s)
{
  if (!is(r, load8(r, ++ram[HM_SOW_STEP]), 9)) return;
  ram[HM_SOW_STEP] = 0;
  uint8_t left = load8(r, --ram[s->bag]);
  if (left == 0) ram[HM_TOOL_SELECTED] = 0;
  tool_finished(ram, r, 0xFF, s->ret_stamina);
}

static void sow_crop(uint8_t *ram, HmRegs *r, const Sowing *s, uint16_t crop, uint8_t season)
{
  if (tool_works_at_step(ram, r, s->ret_check, hm_PreCheckToolSuccessSquare)) {
    r->x = crop;
    /* corn / tomato: CMP #1; potato / turnip: just LDA (C unchanged) */
    uint8_t now = load8(r, ram[HM_SEASON]);
    bool in_season = season == SUMMER ? is(r, now, SUMMER) : now == SPRING;
    if (!in_season) r->x = 0x00EB;
    change_tile_in_front(ram, r, r->x, s->ret_tile);
  }
  sow_step_done(ram, r, s);
}

void hm_ToolUsedCornSeeds(uint8_t *ram, HmRegs *r)
{
  static const Sowing s = { HM_SEEDS_CORN, 0x99A5, 0x99C9, 0x99F6 };
  sow_crop(ram, r, &s, 0x0019, SUMMER);
}
void hm_ToolUsedTomatoSeeds(uint8_t *ram, HmRegs *r)
{
  static const Sowing s = { HM_SEEDS_TOMATO, 0x9A04, 0x9A28, 0x9A55 };
  sow_crop(ram, r, &s, 0x001A, SUMMER);
}
void hm_ToolUsedPotatoSeeds(uint8_t *ram, HmRegs *r)
{
  static const Sowing s = { HM_SEEDS_POTATO, 0x9A63, 0x9A85, 0x9AB2 };
  sow_crop(ram, r, &s, 0x001B, SPRING);
}
void hm_ToolUsedTurnipSeeds(uint8_t *ram, HmRegs *r)
{
  static const Sowing s = { HM_SEEDS_TURNIP, 0x9AC0, 0x9AE2, 0x9B0F };
  sow_crop(ram, r, &s, 0x001C, SPRING);
}

void hm_ToolUsedGrassSeeds(uint8_t *ram, HmRegs *r)
{
  static const Sowing s = { HM_SEEDS_GRASS, 0x9B6D, 0x9B9C, 0x9BC9 };
  if (tool_works_at_step(ram, r, s.ret_check, hm_PreCheckToolSuccessSquare)) {
    r->x = 0x00ED;
    if (below(r, load8(r, ram[HM_SEASON]), FALL)) {
      hm_wr16(ram, HM_PLANTED_GRASS, r->a = (uint16_t)(hm_rd16(ram, HM_PLANTED_GRASS) + 1));
      r->x = 0x001E;
    }
    change_tile_in_front(ram, r, r->x, s.ret_tile);
  }
  sow_step_done(ram, r, &s);
}

/* ─────────────────────────────────────────────────────────────────────────
 * ToolUsedPaint ($82:9BCB). Paints one of the four parts of the house wall
 * (tiles C3, E2, E3, E4 on the farm): the part at Y = $130 and X = $80,
 * $60, $90, $B0 gets change $59, $58, $5A, $5C; +10 happiness. Each part
 * only once ($7F1F66 bits $0200, $0400, $0800, $1000).
 * ───────────────────────────────────────────────────────────────────────── */
void hm_ToolUsedPaint(uint8_t *ram, HmRegs *r)
{
  static const struct { uint16_t flag, change, x, ret_happy, ret_tile; } PART[4] = {
    { 0x0200, 0x0059, 0x0080, 0x9BFD, 0x9C0C },   /* C3 (any other) */
    { 0x0400, 0x0058, 0x0060, 0x9C31, 0x9C40 },   /* E2 */
    { 0x0800, 0x005A, 0x0090, 0x9C61, 0x9C70 },   /* E3 */
    { 0x1000, 0x005C, 0x00B0, 0x9C91, 0x9CA0 },   /* E4 */
  };
  if (tool_works(ram, r, 0x9BCF, hm_PreCheckToolSuccess)) {
    uint16_t tile = r->x;
    int part = is(r, tile, 0x00E2) ? 1 : is(r, tile, 0x00E3) ? 2 : is(r, tile, 0x00E4) ? 3 : 0;
    if (!test(ram, r, FLAGS_66, PART[part].flag)) {
      add(ram, r, hm_AddPlayerHappiness, 10, PART[part].ret_happy);
      change_tile(ram, r, PART[part].change, PART[part].x, 0x0130, PART[part].ret_tile);
      set_flag(ram, r, FLAGS_66, PART[part].flag);
    }
  }
  tool_finished(ram, r, 0xFE, 0x9CBE);
}

/* ─────────────────────────────────────────────────────────────────────────
 * ToolUsedMagicBeans ($82:A377): before 17:00 and on map $31 (the
 * mountain top), the beans are planted: $7F1F68 bit $0200, map animation
 * $42 (StartMapAnimation), the hand is emptied. Anywhere else nothing.
 * ToolUsedGemSeed ($82:A3AA): before 17:00, on map $34, facing tile F6:
 * event $12 of group 0 (UNK_LoadCCDataLong) and the hand is emptied.
 * Both end idle, with no stamina cost.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_ToolUsedMagicBeans(uint8_t *ram, HmRegs *r)
{
  if (below(r, load8(r, ram[HM_HOUR]), 0x11) && is(r, load8(r, map_id(ram, r)), 0x31)) {
    set_flag(ram, r, FLAGS_68, 0x0200);
    r->a = 0x0042;
    jsl(ram, r, 0xA39C, 0x81, hm_StartMapAnimation);
    ram[HM_TOOL_SELECTED] = 0;
  }
  idle(ram, r);
}

void hm_ToolUsedGemSeed(uint8_t *ram, HmRegs *r)
{
  if (below(r, load8(r, ram[HM_HOUR]), 0x11) && is(r, load8(r, map_id(ram, r)), 0x34)) {
    r->a = 1; r->x = 0; r->y = 0;
    jsl(ram, r, 0xA3CA, 0x81, hm_CalculateTileinFront);
    r->x = tile_x(ram); r->y = tile_y(ram); r->a = 0;
    jsl(ram, r, 0xA3D9, 0x82, hm_UNK_CheckTileProperty);
    if (is(r, r->x, 0x00F6)) {
      r->a = 0; r->x = 0x0012; r->y = 0;
      jsl(ram, r, 0xA3EF, 0x84, hm_UNK_LoadCCDataLong);
      ram[HM_TOOL_SELECTED] = 0;
    }
  }
  idle(ram, r);
}

/* ─────────────────────────────────────────────────────────────────────────
 * Feeding (chicken feed in the coop, map $28; cow feed in the barn, map
 * $27). The trough is the tile 6 pixels ahead (CalculateTileinFront with
 * X = Y = 6); troughs are tiles E2-EE: trough n = tile - E2 gets its bit
 * in $0934 / $0932, one more fed today ($0931 / $0930) and change $99
 * (food in it). Anywhere else the feed is spilled (effect $C4). One
 * portion less either way (empty: the hand is emptied) and 2 stamina.
 * The chicken feed takes the stamina before going idle (A = 0 at the
 * end), the cow feed after.
 * ───────────────────────────────────────────────────────────────────────── */
typedef struct Feeding {
  uint8_t map;
  uint16_t troughs, fed, bag;
  uint16_t ret_front, ret_property, ret_tile, ret_fx, ret_stamina;
  bool stamina_first;
} Feeding;

static void feed(uint8_t *ram, HmRegs *r, const Feeding *f)
{
  if (is(r, load8(r, map_id(ram, r)), f->map)) {
    r->a = 1; r->x = 6; r->y = 6;
    jsl(ram, r, f->ret_front, 0x81, hm_CalculateTileinFront);
    r->x = tile_x(ram); r->y = tile_y(ram); r->a = 0;
    jsl(ram, r, f->ret_property, 0x82, hm_UNK_CheckTileProperty);
    if (!below(r, r->x, 0x00E2) && below(r, r->x, 0x00EF)) {
      uint16_t n = sub_c(r, r->x, 0x00E2);
      r->c = (n & 0x8000) != 0;                          /* ASL */
      r->x = (uint16_t)(n << 1);
      r->a = hm_bus_rd16(ram, TROUGH_BITS + r->x) | hm_rd16(ram, f->troughs);
      hm_wr16(ram, f->troughs, r->a);
      load8(r, ++ram[f->fed]);
      change_tile_in_front(ram, r, 0x0099, f->ret_tile);
      goto portion;
    }
  }
  effect_in_front(ram, r, FX_FEED, f->ret_fx);
portion:
  if (load8(r, --ram[f->bag]) == 0) ram[HM_TOOL_SELECTED] = 0;
  if (f->stamina_first) {
    lose_stamina(ram, r, 0xFE, f->ret_stamina);
    idle(ram, r);
  } else {
    tool_finished(ram, r, 0xFE, f->ret_stamina);
  }
}

void hm_ToolUsedChickenFeed(uint8_t *ram, HmRegs *r)
{
  static const Feeding f = { MAP_COOP, HM_CHICKEN_TROUGHS, HM_CHICKENS_FED, HM_CHICKEN_FEED,
                             0xA42C, 0xA43B, 0xA470, 0xA4A3, 0xA4B9, true };
  feed(ram, r, &f);
}
void hm_ToolUsedCowFeed(uint8_t *ram, HmRegs *r)
{
  static const Feeding f = { MAP_BARN, HM_COW_TROUGHS, HM_COWS_FED, HM_COW_FEED,
                             0xA4DA, 0xA4E9, 0xA51E, 0xA551, 0xA56E, false };
  feed(ram, r, &f);
}

/* ─────────────────────────────────────────────────────────────────────────
 * The watering can and the sprinkler. Both first make their sound (effect
 * 0, 7, $0A: UNK_Audio19). A tile that can be watered (PreCheckToolSuccess
 * leaves its type in X) becomes type + 1 in the map (EditTileonMap) and
 * gets the picture of entry 4 (type + 1) of the tile table [$0D] (the
 * byte there is the ChangeMapTile change).
 * ───────────────────────────────────────────────────────────────────────── */
#define HM_WATER           0x00926  /* 8b: water left in the can */

static void make_sound(uint8_t *ram, HmRegs *r, uint16_t ret)
{
  load8(r, 0x07);
  ram[0x0114] = 0x00;
  ram[0x0115] = 0x07;
  jsl(ram, r, ret, 0x83, hm_UNK_Audio19);
}
static void water_tile(uint8_t *ram, HmRegs *r, uint16_t ret_edit, uint16_t ret_change)
{
  uint16_t watered = (uint16_t)(r->x + 1);               /* TXA; INC A; PHA */
  push16(ram, r, watered);
  r->a = watered;
  r->x = tile_x(ram); r->y = tile_y(ram);
  jsl(ram, r, ret_edit, 0x82, hm_EditTileonMap);
  hm_pull8(ram, r); hm_pull8(ram, r);                    /* PLA */
  r->y = (uint16_t)(watered << 2);
  r->c = (watered << 1) & 0x8000 ? 1 : 0;                /* ASL; ASL */
  uint8_t change = hm_bus_rd8(ram, hm_dp_long(ram, r, 0x0D, r->y));
  change_tile_in_front(ram, r, change, ret_change);
}

/* ToolUsedWateringCan ($82:9CD8): nothing if PreCheckToolSuccess says 2
 * (it is asked twice: once for that, once for real) or the can is empty;
 * otherwise one water less and the tile in front is watered if it can be.
 * Idle, 2 stamina. */
void hm_ToolUsedWateringCan(uint8_t *ram, HmRegs *r)
{
  make_sound(ram, r, 0x9CED);
  jsr(ram, r, 0x9CF0, hm_PreCheckToolSuccess);
  if (!is(r, (uint8_t)r->a, 0x02) && load8(r, ram[HM_WATER]) != 0) {
    load8(r, --ram[HM_WATER]);
    if (tool_works(ram, r, 0x9D02, hm_PreCheckToolSuccess))
      water_tile(ram, r, 0x9D18, 0x9D31);
  }
  tool_finished(ram, r, 0xFE, 0x9D40);
}

/* ToolUsedSprinkler ($82:A307): one of the 9 tiles of the 3x3 square in
 * front per call (PreCheckToolSuccessSquare, step $096B); after the 9th,
 * idle and 8 stamina. It never runs out of water. */
void hm_ToolUsedSprinkler(uint8_t *ram, HmRegs *r)
{
  make_sound(ram, r, 0xA31C);
  if (tool_works_at_step(ram, r, 0xA327, hm_PreCheckToolSuccessSquare))
    water_tile(ram, r, 0xA33D, 0xA356);
  gold_step(ram, r, 9, 0xA375);
}

/* ─────────────────────────────────────────────────────────────────────────
 * SUB_829260 ($82:9260, JSL, RTL): uses the tool in hand. JSR through
 * Tool_Used_Table ($82:A5C3, one routine per item, in item order) with
 * A = X = 2 x item (ASL in 8 bits: C = bit 7 of the item), then sets
 * $7F1F5A bit $0040 (FLAG5A: "a tool was used").
 * Leftovers: A = the new $7F1F5A; X, Y, C those of the tool's routine.
 * ───────────────────────────────────────────────────────────────────────── */
#define RET_260_TOOL    0x926E

void hm_SUB_829260(uint8_t *ram, HmRegs *r)
{
  static void (*const used[])(uint8_t *, HmRegs *) = {
    hm_ToolUsedNoAction, hm_ToolUsedSickle, hm_ToolUsedHoe, hm_ToolUsedHammer,
    hm_ToolUsedAxe, hm_ToolUsedCornSeeds, hm_ToolUsedTomatoSeeds, hm_ToolUsedPotatoSeeds,
    hm_ToolUsedTurnipSeeds, hm_ToolUsedCowMedicine, hm_ToolUsedMiraclePotion, hm_ToolUsedBell,
    hm_ToolUsedGrassSeeds, hm_ToolUsedPaint, hm_ToolUsedMilker, hm_ToolUsedBrush,
    hm_ToolUsedWateringCan, hm_ToolUsedGoldSickle, hm_ToolUsedGoldHoe, hm_ToolUsedGoldHammer,
    hm_ToolUsedGoldAxe, hm_ToolUsedSprinkler, hm_ToolUsedMagicBeans, hm_ToolUsedGemSeed,
    hm_ToolUsedBlueFeather, hm_ToolUsedChickenFeed, hm_ToolUsedCowFeed, hm_ToolUsedUNK,
  };
  uint8_t item = ram[HM_TOOL_SELECTED];
  r->c = item >> 7;
  r->a = r->x = (uint8_t)(item << 1);
  if (item < sizeof used / sizeof used[0])               /* the game has no more */
    jsr(ram, r, RET_260_TOOL, used[item]);
  r->a = (uint16_t)(hm_rd16(ram, FLAGS_5A) | 0x0040);
  hm_wr16(ram, FLAGS_5A, r->a);
}
