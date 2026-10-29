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

/* ─────────────────────────────────────────────────────────────────────────
 * ToolUsedSound2 ($82:8FF3, JSL, RTL): when the item in hand ($0921) is
 * not the last one noted ($0119), its noises' instruments are loaded: the
 * bank ($0115) and, for each of the two effects that is not 0, the effect
 * ($0114) with UNK_Audio18 (the first also with the instrument of the song
 * playing, $0103 = table $80:B8CD + 1). Then $0119 = the item.
 * The game calls it with 16-bit indexes (PHX / PLX around the first
 * effect would not match otherwise). The ADC that makes 3 x item clears V,
 * which UNK_Audio18 keeps.
 * Leftovers: A = the item (8-bit), X where it last read the table, C = 0
 * from the ADC or that of the compare / UNK_Audio18.
 * ───────────────────────────────────────────────────────────────────────── */
#define SONG_INSTRUMENTS 0x80B8CD

void hm_ToolUsedSound2(uint8_t *ram, HmRegs *r)
{
  uint8_t entry = r->p;
  uint8_t tool = hm_bus_rd8(ram, db_(r, 0x0921));
  r->a = tool;                                             /* LDA #0; XBA; LDA $0921 */
  uint8_t noted = hm_bus_rd8(ram, db_(r, 0x0119));
  r->c = tool >= noted;
  if (tool != noted) {
    hm_dp_w16(ram, r, 0x7E, tool);                         /* REP #$20; STA $7E */
    r->x = (uint16_t)(tool * 3);                           /* ASL; CLC; ADC $7E; TAX */
    r->c = 0;
    r->a = r->x;
    uint8_t bank = noise(ram, r->x);
    r->a = (uint16_t)((r->a & 0xFF00) | bank);
    if (bank) {
      hm_bus_wr8(ram, db_(r, 0x0115), bank);
      r->x++;
      hm_push8(ram, r, (uint8_t)(r->x >> 8));              /* PHX */
      hm_push8(ram, r, (uint8_t)r->x);
      uint8_t first = noise(ram, r->x);
      r->a = (uint16_t)((r->a & 0xFF00) | first);
      if (first) {
        hm_bus_wr8(ram, db_(r, 0x0114), first);
        r->a = hm_bus_rd8(ram, db_(r, 0x0118));
        r->x = r->a;
        uint8_t ins = (uint8_t)(hm_bus_rd8(ram, (SONG_INSTRUMENTS + r->x) & 0xFFFFFF) + 1);
        r->a = (uint16_t)((r->a & 0xFF00) | ins);
        hm_bus_wr8(ram, db_(r, 0x0103), ins);
        r->p = (uint8_t)(entry & ~0x40);
        hm_jsl(ram, r, 0x9037, 0x83);
        hm_UNK_Audio18(ram, r);
        hm_rtl(ram, r);
      }
      uint8_t lo = hm_pull8(ram, r);                       /* SEP #$20; REP #$10; PLX; INX */
      r->x = (uint16_t)((lo | hm_pull8(ram, r) << 8) + 1);
      uint8_t second = noise(ram, r->x);
      r->a = (uint16_t)((r->a & 0xFF00) | second);
      if (second) {
        hm_bus_wr8(ram, db_(r, 0x0114), second);
        r->p = (uint8_t)(entry & ~0x40);
        hm_jsl(ram, r, 0x904A, 0x83);
        hm_UNK_Audio18(ram, r);
        hm_rtl(ram, r);
      }
      r->p = entry;
    }
  }
  r->a = (uint16_t)((r->a & 0xFF00) | hm_bus_rd8(ram, db_(r, 0x0921)));
  hm_bus_wr8(ram, db_(r, 0x0119), (uint8_t)r->a);
}
