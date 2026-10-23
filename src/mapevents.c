/*
 * mapevents.c — who appears when a map is entered.
 *
 * RunsFunctionbyIndex ($83:CA98) calls, for the map being entered ($22),
 * its routine from UNK_TableFunctionSelect ($83:CAA9, one word per map).
 * Each routine starts the event scripts of the people and things of that
 * map, depending on the date, the time and the story flags, with
 * UNK_LoadCCDataLong (A = the slot, X = the script group, Y = the script).
 *
 * Flags seen so far (16-bit words in $7F:1F64-$7F:1F74):
 *   $7F1F66  bits 0-4: one per girl (Maria, Ann, Nina, Ellen, Eve: which
 *            one the player married, see the houses)
 *   $7F1F6E  bit 1: a story flag about the wife (it moves her between her
 *            parents' house and the farm); bits 2 / 3: first / second
 *            child
 *   $7F1F68, $7F1F6A, $7F1F74: events seen
 *
 * All the routines here are JSR / RTS, entered with 16-bit A, X and Y.
 * Leftovers: those of the last UNK_LoadCCDataLong, or of the last test.
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

#define SEASON   0x7F1F19
#define DAY      0x7F1F1B
#define KID1_AGE 0x7F1F37
#define KID2_AGE 0x7F1F39

/* LDA #a; LDX #x; LDY #y; JSL UNK_LoadCCDataLong at $83:`at` */
static void spawn(uint8_t *ram, HmRegs *r, uint16_t a, uint16_t x, uint16_t y, uint16_t at)
{
  r->a = a; r->x = x; r->y = y;
  hm_jsl(ram, r, (uint16_t)(at + 3), 0x84);
  hm_UNK_LoadCCDataLong(ram, r);
  hm_rtl(ram, r);
}
/* LDA.L addr; AND #mask (16 bits): true if any of the bits is set */
static bool flag(uint8_t *ram, HmRegs *r, uint32_t addr, uint16_t mask)
{
  r->a = (uint16_t)(hm_bus_rd16(ram, addr) & mask);
  return r->a != 0;
}
/* SEP #$20; LDA.L addr; CMP #v: true if equal */
static bool byte_is(uint8_t *ram, HmRegs *r, uint32_t addr, uint8_t v)
{
  uint8_t b = hm_bus_rd8(ram, addr);
  r->a = (uint16_t)((r->a & 0xFF00) | b);
  r->c = b >= v;
  return b == v;
}
/* LDA.L addr; CMP #v (16 bits): true if at least v */
static bool word_at_least(uint8_t *ram, HmRegs *r, uint32_t addr, uint16_t v)
{
  r->a = hm_bus_rd16(ram, addr);
  r->c = r->a >= v;
  return r->c;
}

/* ── one script, always ─────────────────────────────────────────────── */
void hm_MapEvent_E5A2(uint8_t *ram, HmRegs *r) { spawn(ram, r, 0x0000, 0x0014, 0x0002, 0xE5AF); }
void hm_MapEvent_E5CD(uint8_t *ram, HmRegs *r) { spawn(ram, r, 0x0000, 0x001E, 0x0000, 0xE5DA); }
void hm_MapEvent_E5DF(uint8_t *ram, HmRegs *r) { spawn(ram, r, 0x0000, 0x000E, 0x0001, 0xE5EC); }
void hm_MapEvent_E5F1(uint8_t *ram, HmRegs *r) { spawn(ram, r, 0x0000, 0x0026, 0x0001, 0xE5FE); }
void hm_MapEvent_E6B2(uint8_t *ram, HmRegs *r) { spawn(ram, r, 0x0000, 0x0028, 0x0003, 0xE6BF); }
/* E74C: nothing (most of the maps from $3B on) */
void hm_MapEvent_E74C(uint8_t *ram, HmRegs *r) { (void)ram; (void)r; }

/* ── one script, if a flag is set ───────────────────────────────────── */
void hm_MapEvent_E586(uint8_t *ram, HmRegs *r)
{
  if (flag(ram, r, 0x7F1F68, 0x0200)) spawn(ram, r, 0x0000, 0x0014, 0x0001, 0xE59C);
}
void hm_MapEvent_E5B4(uint8_t *ram, HmRegs *r)
{
  if (flag(ram, r, 0x7F1F6A, 0x4000)) spawn(ram, r, 0x0015, 0x0000, 0x007F, 0xE5C8);
}

/* ── winter 24 (the Starry Night), if the flag of $7F1F74 is set ───── */
static void winter_24(uint8_t *ram, HmRegs *r, uint16_t mask, uint16_t y, uint16_t at)
{
  if (!byte_is(ram, r, SEASON, 3)) return;
  if (!byte_is(ram, r, DAY, 0x18)) return;
  if (flag(ram, r, 0x7F1F74, mask)) spawn(ram, r, 0x0000, 0x000F, y, at);
}
void hm_MapEvent_E603(uint8_t *ram, HmRegs *r) { winter_24(ram, r, 0x0001, 0x0001, 0xE62D); }
void hm_MapEvent_E632(uint8_t *ram, HmRegs *r) { winter_24(ram, r, 0x0008, 0x0004, 0xE65C); }
void hm_MapEvent_E661(uint8_t *ram, HmRegs *r) { winter_24(ram, r, 0x0010, 0x0005, 0xE68B); }

/* E690: spring 1 (the new year) */
void hm_MapEvent_E690(uint8_t *ram, HmRegs *r)
{
  uint8_t season = hm_bus_rd8(ram, SEASON);
  r->a = (uint16_t)((r->a & 0xFF00) | season);
  if (season) return;
  if (byte_is(ram, r, DAY, 1)) spawn(ram, r, 0x0000, 0x0028, 0x0001, 0xE6AD);
}

/* ── the family at home: the wife (by who she is) and the children ──── */
void hm_MapEvent_E6C4(uint8_t *ram, HmRegs *r)
{
  if (!flag(ram, r, 0x7F1F6E, 0x0002)) {
    if (flag(ram, r, 0x7F1F66, 0x0002))      spawn(ram, r, 0x0013, 0x0044, 0x0001, 0xE6EE);
    else if (flag(ram, r, 0x7F1F66, 0x0010)) spawn(ram, r, 0x0013, 0x0044, 0x0004, 0xE6FF);
  }
  if (flag(ram, r, 0x7F1F6E, 0x0004) && word_at_least(ram, r, KID1_AGE, 0x003C))
    spawn(ram, r, 0x0014, 0x0045, 0x0002, 0xE724);
  if (flag(ram, r, 0x7F1F6E, 0x0008) && word_at_least(ram, r, KID2_AGE, 0x003C))
    spawn(ram, r, 0x0015, 0x0045, 0x0005, 0xE747);
}
