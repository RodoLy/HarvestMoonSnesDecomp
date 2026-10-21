/*
 * people.c — the girls, the wife and the children.
 *
 * Hearts: 16-bit, 0..999, at $7F1F1F + 2 i for i = Maria, Ann, Nina,
 * Ellen, Eve (HM_HEARTS_BASE). The wife: $7F1F66 bits 0..4 in the same
 * order (only one is ever set).
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

enum { MARIA, ANN, NINA, ELLEN, EVE, GIRLS };

/* ─────────────────────────────────────────────────────────────────────────
 * FindMostLovedName ($82:8EC6, JSL, RTL; no compiled body). Writes the name
 * of the girl with the most hearts into $08A1-$08AA (five 16-bit text
 * codes; see the text encoding in hm_ram.h's notes: a-z = 00-19, A-Z =
 * 1A-33, B1 = space). On a tie the later girl in the list wins (BCS).
 * HM-Decomp's branch labels are swapped: index 1 writes "Ann", 2 "Nina".
 * Leftovers: $7E = the most hearts, X = $0A, Y = the girl, A = the last
 * code written, C = 1.
 * ───────────────────────────────────────────────────────────────────────── */
#define MOST_LOVED_NAME 0x008A1   /* 5 x 16b */

static const uint16_t kGirlNames[GIRLS][5] = {
  { 0x26, 0x00, 0x11, 0x08, 0x00 },   /* "Maria" */
  { 0x1A, 0x0D, 0x0D, 0xB1, 0xB1 },   /* "Ann  " */
  { 0x27, 0x08, 0x0D, 0x00, 0xB1 },   /* "Nina " */
  { 0x1E, 0x0B, 0x0B, 0x04, 0x0D },   /* "Ellen" */
  { 0x1E, 0x15, 0x04, 0xB1, 0xB1 },   /* "Eve  " */
};

void hm_FindMostLovedName(uint8_t *ram, HmRegs *r)
{
  uint16_t best = hm_rd16(ram, HM_HEARTS_BASE);
  unsigned girl = MARIA;
  for (unsigned i = 1; i < GIRLS; i++) {
    uint16_t hearts = hm_rd16(ram, HM_HEARTS_BASE + 2 * i);
    if (hearts >= best) { best = hearts; girl = i; }
  }
  hm_dp_w16(ram, r, 0x7E, best);
  for (unsigned k = 0; k < 5; k++)
    hm_wr16(ram, MOST_LOVED_NAME + 2 * k, kGirlNames[girl][k]);
  r->a = kGirlNames[girl][4];
  r->x = 0x0A;
  r->y = (uint16_t)girl;
  r->c = 1;
}
