/*
 * toolsound.c — the noise each tool makes.
 *
 * Tool_Noise_Table ($82:9054): 3 bytes per item in hand ($0921): the
 * sound bank ($0115) and two sound effects ($0114), the first when the
 * tool lands on something ($0922 = 0), the second when it does not
 * ($0922 = 1). An effect of 0 is no sound.
 *
 * ToolUsedSound1 ($82:8FB1, JSL, RTL): plays the tool's noise, unless
 * $0922 is 2 (silent). It sets the bank, picks the effect by $0922 and
 * plays it with UNK_Audio19.
 * Leftovers: A, X = where it read the effect, C = 0; with $0922 = 2,
 * A = 2 and C = 1. When the game calls it with an 8-bit X the index wraps
 * at 256 (TAX / INX keep only the low byte).
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

#define TOOL_NOISE_TABLE 0x829054
#define RET_SOUND1_PLAY  0x8FF1

static uint32_t db_(const HmRegs *r, uint16_t a) { return (uint32_t)r->db << 16 | a; }
static bool x8(const HmRegs *r) { return (r->p & 0x10) != 0; }
static uint16_t idx(const HmRegs *r, unsigned v) { return (uint16_t)(x8(r) ? v & 0xFF : v); }
static uint8_t noise(uint8_t *ram, uint16_t x) { return hm_bus_rd8(ram, (TOOL_NOISE_TABLE + x) & 0xFFFFFF); }

void hm_ToolUsedSound1(uint8_t *ram, HmRegs *r)
{
  uint16_t sound = hm_bus_rd8(ram, db_(r, 0x0922));
  r->a = sound;
  r->c = sound >= 2;                                       /* CMP #2 */
  if (sound == 2) return;
  hm_dp_w16(ram, r, 0x80, sound);
  uint16_t tool = hm_bus_rd8(ram, db_(r, 0x0921));
  hm_dp_w16(ram, r, 0x7E, tool);
  r->c = 0;                                                /* ASL, CLC; ADC */
  r->x = idx(r, (uint16_t)(tool * 3));                     /* TAX */
  hm_bus_wr8(ram, db_(r, 0x0115), noise(ram, r->x));       /* the bank */
  r->x = idx(r, r->x + 1u);                                /* INX */
  uint16_t sum = (uint16_t)(r->x + sound);                 /* TXA; CLC; ADC $80 */
  r->x = idx(r, sum);                                      /* TAX */
  uint8_t effect = noise(ram, r->x);
  r->a = (uint16_t)((sum & 0xFF00) | effect);
  if (!effect) return;
  hm_bus_wr8(ram, db_(r, 0x0114), effect);
  hm_jsl(ram, r, RET_SOUND1_PLAY, 0x83);
  hm_UNK_Audio19(ram, r);
  hm_rtl(ram, r);
}
