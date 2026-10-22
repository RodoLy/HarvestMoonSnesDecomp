/*
 * toolanim.c — pressing Y: the tool's animation.
 *
 * UNK_ToolUsed ($82:90A8) runs when the player presses Y with a tool in
 * hand. Without stamina ($D2 bit 3) the player only shows the "tired"
 * animation. Otherwise it checks the tile in front (SUB_82927D, which also
 * chooses the tool's sound) and calls the tool's animation routine through
 * Tool_Animation_Table ($82:A58B, one per item, the same order as the tools
 * in toolused.c). Each puts the player's animation number in $0901: a base
 * per tool, plus the direction ($DA, 0-3) for the ones that look different
 * when facing each way. What the tool does to the tile comes later, when
 * the animation reaches its moment (SUB_829260, toolused.c).
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

#define ANIMATION      0x0901   /* the player's animation to play */
#define TOOL_SOUND     0x091B
#define TOOL           0x0921
#define CAN_WATER      0x0926
#define FLAGS_5A       0x7F1F5A

static uint32_t db_addr(const HmRegs *r, uint16_t a) { return (uint32_t)r->db << 16 | a; }
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
static void animation(uint8_t *ram, HmRegs *r, uint16_t n)
{
  r->a = n;
  hm_bus_wr16(ram, db_addr(r, ANIMATION), n);
}
/* LDA #base; CLC; ADC $DA: one animation per direction */
static void facing(uint8_t *ram, HmRegs *r, uint16_t base)
{
  uint32_t s = (uint32_t)base + hm_dp_r16(ram, r, 0xDA);
  r->c = s > 0xFFFF;
  animation(ram, r, (uint16_t)s);
}

void hm_ToolAnimationNoAction(uint8_t *ram, HmRegs *r) { (void)ram; (void)r; }
void hm_ToolAnimationSickle(uint8_t *ram, HmRegs *r)        { facing(ram, r, 0x50); }
void hm_ToolAnimationHoe(uint8_t *ram, HmRegs *r)           { facing(ram, r, 0x54); }
void hm_ToolAnimationHammer(uint8_t *ram, HmRegs *r)        { facing(ram, r, 0x58); }
void hm_ToolAnimationAxe(uint8_t *ram, HmRegs *r)           { facing(ram, r, 0x5C); }
void hm_ToolAnimationCornSeeds(uint8_t *ram, HmRegs *r)     { animation(ram, r, 0x46); }
void hm_ToolAnimationTomatoSeeds(uint8_t *ram, HmRegs *r)   { animation(ram, r, 0x46); }
void hm_ToolAnimationPotatoSeeds(uint8_t *ram, HmRegs *r)   { animation(ram, r, 0x46); }
void hm_ToolAnimationTurnipSeeds(uint8_t *ram, HmRegs *r)   { animation(ram, r, 0x46); }
void hm_ToolAnimationCowMedicine(uint8_t *ram, HmRegs *r)   { facing(ram, r, 0xAC); }
void hm_ToolAnimationMiraclePotion(uint8_t *ram, HmRegs *r) { facing(ram, r, 0xAC); }
/* the bell also calls the cows: $7F1F5A bit $10 */
void hm_ToolAnimationBell(uint8_t *ram, HmRegs *r)
{
  facing(ram, r, 0x60);
  r->a = (uint16_t)(hm_bus_rd16(ram, FLAGS_5A) | 0x0010);
  hm_bus_wr16(ram, FLAGS_5A, r->a);
}
void hm_ToolAnimationGrassSeeds(uint8_t *ram, HmRegs *r)    { animation(ram, r, 0x46); }
void hm_ToolAnimationPaint(uint8_t *ram, HmRegs *r)         { animation(ram, r, 0x44); }
void hm_ToolAnimationMilker(uint8_t *ram, HmRegs *r)        { facing(ram, r, 0x78); }
void hm_ToolAnimationBrush(uint8_t *ram, HmRegs *r)         { facing(ram, r, 0x64); }
/* the watering can: in front of water it fills up (20 uses); empty, a
 * different animation (nothing comes out) */
void hm_ToolAnimationWateringCan(uint8_t *ram, HmRegs *r)
{
  jsr(ram, r, 0x91AF, hm_PreCheckToolSuccess);
  uint8_t answer = (uint8_t)r->a;
  r->c = answer >= 2;                                      /* CMP #2 */
  if (answer == 2) {
    facing(ram, r, 0x70);
    r->a = (uint16_t)((r->a & 0xFF00) | 0x14);
    hm_bus_wr8(ram, db_addr(r, CAN_WATER), 0x14);
    return;
  }
  uint8_t water = hm_bus_rd8(ram, db_addr(r, CAN_WATER));
  r->a = (uint16_t)((r->a & 0xFF00) | water);
  facing(ram, r, water ? 0x68 : 0x6C);
}
void hm_ToolAnimationGoldSickle(uint8_t *ram, HmRegs *r)    { animation(ram, r, 0x48); }
void hm_ToolAnimationGoldHoe(uint8_t *ram, HmRegs *r)       { facing(ram, r, 0x7C); }
void hm_ToolAnimationGoldHammer(uint8_t *ram, HmRegs *r)    { facing(ram, r, 0x84); }
void hm_ToolAnimationGoldAxe(uint8_t *ram, HmRegs *r)       { facing(ram, r, 0x80); }
void hm_ToolAnimationSprinkler(uint8_t *ram, HmRegs *r)     { animation(ram, r, 0x47); }
void hm_ToolAnimationBeans(uint8_t *ram, HmRegs *r)         { facing(ram, r, 0xAC); }
void hm_ToolAnimationGem(uint8_t *ram, HmRegs *r)           { facing(ram, r, 0xAC); }
void hm_ToolAnimationBlueFeather(uint8_t *ram, HmRegs *r)   { animation(ram, r, 0xA8); }
void hm_ToolAnimationChickenFeed(uint8_t *ram, HmRegs *r)   { facing(ram, r, 0x74); }
void hm_ToolAnimationCowFeed(uint8_t *ram, HmRegs *r)       { facing(ram, r, 0x74); }
void hm_ToolAnimationUNK(uint8_t *ram, HmRegs *r)           { animation(ram, r, 0x88); }

/* ─────────────────────────────────────────────────────────────────────────
 * UNK_ToolUsed ($82:90A8, JSL, RTL).
 * ───────────────────────────────────────────────────────────────────────── */
void hm_UNK_ToolUsed(uint8_t *ram, HmRegs *r)
{
  static void (*const animate[])(uint8_t *, HmRegs *) = {
    hm_ToolAnimationNoAction, hm_ToolAnimationSickle, hm_ToolAnimationHoe,
    hm_ToolAnimationHammer, hm_ToolAnimationAxe, hm_ToolAnimationCornSeeds,
    hm_ToolAnimationTomatoSeeds, hm_ToolAnimationPotatoSeeds, hm_ToolAnimationTurnipSeeds,
    hm_ToolAnimationCowMedicine, hm_ToolAnimationMiraclePotion, hm_ToolAnimationBell,
    hm_ToolAnimationGrassSeeds, hm_ToolAnimationPaint, hm_ToolAnimationMilker,
    hm_ToolAnimationBrush, hm_ToolAnimationWateringCan, hm_ToolAnimationGoldSickle,
    hm_ToolAnimationGoldHoe, hm_ToolAnimationGoldHammer, hm_ToolAnimationGoldAxe,
    hm_ToolAnimationSprinkler, hm_ToolAnimationBeans, hm_ToolAnimationGem,
    hm_ToolAnimationBlueFeather, hm_ToolAnimationChickenFeed, hm_ToolAnimationCowFeed,
    hm_ToolAnimationUNK,
  };
  r->a = hm_dp_r16(ram, r, 0xD2) & 0x0008;
  if (r->a) {                                              /* no stamina */
    animation(ram, r, 0x4D);
    r->a = 0x000B;
    hm_dp_w16(ram, r, 0xD4, 0x000B);                       /* action: tired */
    return;
  }
  jsr(ram, r, 0x90BA, hm_SUB_82927D);
  hm_bus_wr8(ram, db_addr(r, TOOL_SOUND), 0);
  uint8_t item = hm_bus_rd8(ram, db_addr(r, TOOL));
  r->c = item >> 7;                                        /* ASL (8 bits) */
  r->a = r->x = (uint8_t)(item << 1);
  if (item < sizeof animate / sizeof animate[0])           /* the game has no more */
    jsr(ram, r, 0x90CA, animate[item]);
}
