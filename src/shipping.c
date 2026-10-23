/*
 * shipping.c — the shipping at 5 pm.
 *
 * ShippingScene ($82:8131, JSL, RTL; called every in-game hour by
 * UpdateTime): at 17:00, on the farm (maps 0-3), the shipping scene starts:
 * the input is switched off ($019A = 0), $7F1F5A bit 10 marks the scene as
 * running and script $26 is started in event slot 6 (UNK_LoadCCDataLong).
 * It then falls into ShippingSceneDialogue.
 *
 * ShippingSceneDialogue ($82:8165, JSL, RTL; also called by UpdateTime
 * every frame while bit 10 is set): once the scene's script has set
 * $7F1F5A bit 11, and no change of map is under way ($D2 bit 6), the
 * shipping text is shown: $031A (the day's shipping), or $031B when the
 * money shipped ($7F1F07-$7F1F09) is 0. Input mode 2 (text), the box
 * height is picked again ($0191 = 0), slot 6 gets script $27 and bit 10 is
 * cleared.
 * Both end at the same RTL (ShippingReturn, $82:81BF).
 * Leftovers: those of the last thing done: a compare (A = its value, C)
 * or the scripts / text routines.
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

#define FLAGS_5A        0x7F1F5A
#define HOUR            0x7F1F1C
#define SHIPPED_LOW     0x7F1F07
#define SHIPPED_HIGH    0x7F1F09
#define INPUT_STATE     0x019A    /* DB: */
#define TEXT_BOX_LINES  0x0191    /* DB: */
#define SCENE_RUNNING   0x0400
#define SCENE_DONE      0x0800

static uint32_t db_(const HmRegs *r, uint16_t a) { return (uint32_t)r->db << 16 | a; }
static void jsl(uint8_t *ram, HmRegs *r, uint16_t ret, uint8_t bank, void (*fn)(uint8_t *, HmRegs *))
{
  hm_jsl(ram, r, ret, bank);
  fn(ram, r);
  hm_rtl(ram, r);
}
static void set_a8(HmRegs *r, uint8_t v) { r->a = (uint16_t)((r->a & 0xFF00) | v); }

void hm_ShippingSceneDialogue(uint8_t *ram, HmRegs *r)
{
  r->a = (uint16_t)(hm_bus_rd16(ram, FLAGS_5A) & SCENE_DONE);
  if (!r->a) return;
  r->a = (uint16_t)(hm_dp_r16(ram, r, 0xD2) & 0x0040);
  if (r->a) return;
  r->a = 0x0002;
  hm_bus_wr16(ram, db_(r, INPUT_STATE), 0x0002);
  r->x = 0x031A;                                           /* the day's shipping */
  r->a = hm_bus_rd16(ram, SHIPPED_LOW);
  if (!r->a) {
    set_a8(r, hm_bus_rd8(ram, SHIPPED_HIGH));
    if (!(r->a & 0xFF)) r->x = 0x031B;                     /* nothing shipped */
  }
  set_a8(r, 0);
  hm_bus_wr8(ram, db_(r, TEXT_BOX_LINES), 0);
  jsl(ram, r, 0x81A2, 0x83, hm_StartTextBox);
  r->a = 0x0006; r->x = 0x0000; r->y = 0x0027;
  jsl(ram, r, 0x81B1, 0x84, hm_UNK_LoadCCDataShort);
  r->a = (uint16_t)(hm_bus_rd16(ram, FLAGS_5A) & ~SCENE_RUNNING);
  hm_bus_wr16(ram, FLAGS_5A, r->a);
}

void hm_ShippingScene(uint8_t *ram, HmRegs *r)
{
  uint8_t hour = hm_bus_rd8(ram, HOUR);
  set_a8(r, hour);
  r->c = hour >= 17;
  if (hour != 17) return;
  uint8_t map = *hm_dp(ram, r, 0x22);
  set_a8(r, map);
  r->c = map >= 4;
  if (map >= 4) return;                                    /* not on the farm */
  set_a8(r, 0);
  hm_bus_wr8(ram, db_(r, INPUT_STATE), 0);
  r->a = (uint16_t)(hm_bus_rd16(ram, FLAGS_5A) | SCENE_RUNNING);
  hm_bus_wr16(ram, FLAGS_5A, r->a);
  r->a = 0x0006; r->x = 0x0000; r->y = 0x0026;
  jsl(ram, r, 0x8164, 0x84, hm_UNK_LoadCCDataLong);
  hm_ShippingSceneDialogue(ram, r);                        /* falls into it */
}
