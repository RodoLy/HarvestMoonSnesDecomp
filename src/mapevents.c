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

#define HOUR     0x7F1F1C
#define WEEKDAY  0x7F1F1A
#define FLAGS_0196        0x0196   /* DB: */
#define WEATHER_TOMORROW  0x098C   /* DB: */

static uint32_t db(const HmRegs *r, uint16_t a) { return (uint32_t)r->db << 16 | a; }
/* SEP #$20; LDA addr; CMP #v: true if below v */
static bool byte_below(uint8_t *ram, HmRegs *r, uint32_t addr, uint8_t v)
{
  uint8_t b = hm_bus_rd8(ram, addr);
  r->a = (uint16_t)((r->a & 0xFF00) | b);
  r->c = b >= v;
  return !r->c;
}
/* SEP #$20; LDA addr (no compare): the byte */
static uint8_t byte_of(uint8_t *ram, HmRegs *r, uint32_t addr)
{
  uint8_t b = hm_bus_rd8(ram, addr);
  r->a = (uint16_t)((r->a & 0xFF00) | b);
  return b;
}
/* the married girl's own house: if the player married her ($7F1F66 bit)
 * and $7F1F6E bit 1 is set, only her script (group $22) */
static bool married_here(uint8_t *ram, HmRegs *r, uint16_t girl)
{
  return flag(ram, r, 0x7F1F66, girl) && flag(ram, r, 0x7F1F6E, 0x0002);
}
/* winter 10 before 5 pm (season 3, day 10, hour < 17) */
static bool winter_10_morning(uint8_t *ram, HmRegs *r)
{
  return byte_is(ram, r, SEASON, 3) && byte_is(ram, r, DAY, 0x0A) && byte_below(ram, r, HOUR, 0x11);
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

/* ── the girls' houses ──────────────────────────────────────────────────
 * Each person has a script in several groups, one per kind of day:
 *   $0B before the first event of the story ($7F1F68 bit 0)
 *   $07 with $0196 bit 3      $06 rain (tomorrow's weather 3) or
 *   $08 with $0196 bit 9          $7F1F64 bit 5
 *   $04 with $0196 bit 1      $02 / $03 Saturday / Sunday (weekday 6 / 0)
 *   $01 any other day (also a weather of 6 or more, the special days)
 *   $27 winter 10 before 5 pm $0F winter 24 (with the $7F1F74 bit)
 * and the married girl ($22) when she is at her parents' house.
 * ───────────────────────────────────────────────────────────────────── */

/* D4FF, the first house (the script of each group is 1, or 0 / 3) */
static void house_D4FF(uint8_t *ram, HmRegs *r)
{
  if (winter_10_morning(ram, r)) { spawn(ram, r, 0, 0x27, 3, 0xD526); return; }
  if (!flag(ram, r, 0x7F1F68, 0x0001)) { spawn(ram, r, 0, 0x0B, 1, 0xD541); return; }
  if (flag(ram, r, db(r, FLAGS_0196), 0x0008)) { spawn(ram, r, 0, 7, 0, 0xD5C2); return; }
  if (!byte_below(ram, r, db(r, WEATHER_TOMORROW), 6)) { spawn(ram, r, 0, 1, 1, 0xD592); return; }
  if (byte_is(ram, r, db(r, WEATHER_TOMORROW), 3)) { spawn(ram, r, 0, 6, 0, 0xD5D2); return; }
  if (flag(ram, r, 0x7F1F64, 0x0020)) { spawn(ram, r, 0, 6, 0, 0xD5D2); return; }
  if (flag(ram, r, db(r, FLAGS_0196), 0x0200)) { spawn(ram, r, 0, 8, 0, 0xD5E2); return; }
  if (flag(ram, r, db(r, FLAGS_0196), 0x0002)) { spawn(ram, r, 0, 4, 0, 0xD5B2); return; }
  if (byte_is(ram, r, WEEKDAY, 6)) { spawn(ram, r, 0, 2, 1, 0xD5A2); return; }
  spawn(ram, r, 0, 1, 1, 0xD592);
}
void hm_MapEvent_D4D7(uint8_t *ram, HmRegs *r)
{
  if (married_here(ram, r, 0x0001)) spawn(ram, r, 0, 0x22, 3, 0xD4FA);
  else house_D4FF(ram, r);
}
void hm_MapEvent_D5E7(uint8_t *ram, HmRegs *r)
{
  if (married_here(ram, r, 0x0001)) spawn(ram, r, 0, 0x22, 4, 0xD60E);
  else house_D4FF(ram, r);
}

/* D613: winter 10, winter 24, then as the others (script 2, or 1 / 12),
 * Sunday too (group 3). It tests $7F1F68 bit 0 a second time (nobody if
 * clear), which can never fail: without it the first test returned. */
void hm_MapEvent_D613(uint8_t *ram, HmRegs *r)
{
  if (winter_10_morning(ram, r)) { spawn(ram, r, 0, 0x27, 2, 0xD63A); return; }
  if (byte_is(ram, r, SEASON, 3) && byte_is(ram, r, DAY, 0x18) && flag(ram, r, 0x7F1F74, 0x0002)) {
    spawn(ram, r, 0, 0x0F, 2, 0xD669);
    return;
  }
  if (!flag(ram, r, 0x7F1F68, 0x0001)) { spawn(ram, r, 0, 0x0B, 0x0C, 0xD684); return; }
  if (flag(ram, r, db(r, FLAGS_0196), 0x0008)) { spawn(ram, r, 0, 7, 1, 0xD729); return; }
  if (!byte_below(ram, r, db(r, WEATHER_TOMORROW), 6)) { spawn(ram, r, 0, 1, 2, 0xD6E9); return; }
  if (byte_is(ram, r, db(r, WEATHER_TOMORROW), 3)) { spawn(ram, r, 0, 6, 1, 0xD739); return; }
  if (flag(ram, r, 0x7F1F64, 0x0020)) { spawn(ram, r, 0, 6, 1, 0xD739); return; }
  if (flag(ram, r, db(r, FLAGS_0196), 0x0200)) { spawn(ram, r, 0, 8, 1, 0xD749); return; }
  if (!flag(ram, r, 0x7F1F68, 0x0001)) return;
  if (flag(ram, r, db(r, FLAGS_0196), 0x0002)) { spawn(ram, r, 0, 4, 1, 0xD719); return; }
  uint8_t day = byte_of(ram, r, WEEKDAY);
  if (day == 0) { spawn(ram, r, 0, 3, 1, 0xD6F9); return; }
  r->c = day >= 6;
  if (day == 6) { spawn(ram, r, 0, 2, 2, 0xD709); return; }
  spawn(ram, r, 0, 1, 2, 0xD6E9);
}

/* The usual day of a person without other cases: the script and the JSL
 * address of each group: $0B (before the first event), 7, 1 (also a
 * weather of 6 or more), 6 (rain), 8, 4. */
typedef struct { uint8_t s0B, s07, s01, s06, s08, s04; uint16_t at0B, at07, at01, at06, at08, at04; } Day;

static void usual_day(uint8_t *ram, HmRegs *r, const Day *d)
{
  if (!flag(ram, r, 0x7F1F68, 0x0001)) { spawn(ram, r, 0, 0x0B, d->s0B, d->at0B); return; }
  if (flag(ram, r, db(r, FLAGS_0196), 0x0008)) { spawn(ram, r, 0, 7, d->s07, d->at07); return; }
  if (!byte_below(ram, r, db(r, WEATHER_TOMORROW), 6)) { spawn(ram, r, 0, 1, d->s01, d->at01); return; }
  if (byte_is(ram, r, db(r, WEATHER_TOMORROW), 3) || flag(ram, r, 0x7F1F64, 0x0020)) {
    spawn(ram, r, 0, 6, d->s06, d->at06);
    return;
  }
  if (flag(ram, r, db(r, FLAGS_0196), 0x0200)) { spawn(ram, r, 0, 8, d->s08, d->at08); return; }
  if (flag(ram, r, db(r, FLAGS_0196), 0x0002)) { spawn(ram, r, 0, 4, d->s04, d->at04); return; }
  spawn(ram, r, 0, 1, d->s01, d->at01);
}

/* D776, the second girl's house (script 3, or 2) */
static void house_D776(uint8_t *ram, HmRegs *r)
{
  static const Day d = { 3, 2, 3, 2, 2, 2, 0xD78C, 0xD7F3, 0xD7D3, 0xD803, 0xD813, 0xD7E3 };
  usual_day(ram, r, &d);
}
void hm_MapEvent_D74E(uint8_t *ram, HmRegs *r)
{
  if (married_here(ram, r, 0x0004)) spawn(ram, r, 0, 0x22, 7, 0xD771);
  else house_D776(ram, r);
}
void hm_MapEvent_D818(uint8_t *ram, HmRegs *r)
{
  if (married_here(ram, r, 0x0004)) spawn(ram, r, 0, 0x22, 8, 0xD83F);
  else house_D776(ram, r);
}

/* D86C, the third girl's house: $7F1F5E bit 4 first (group $1A) */
static void house_D86C(uint8_t *ram, HmRegs *r)
{
  static const Day d = { 2, 3, 4, 3, 3, 3, 0xD89D, 0xD904, 0xD8E4, 0xD914, 0xD924, 0xD8F4 };
  if (flag(ram, r, 0x7F1F5E, 0x0010)) { spawn(ram, r, 0, 0x1A, 1, 0xD882); return; }
  usual_day(ram, r, &d);
}
void hm_MapEvent_D844(uint8_t *ram, HmRegs *r)
{
  if (married_here(ram, r, 0x0002)) spawn(ram, r, 0, 0x22, 5, 0xD867);
  else house_D86C(ram, r);
}
void hm_MapEvent_D929(uint8_t *ram, HmRegs *r)
{
  if (married_here(ram, r, 0x0002)) spawn(ram, r, 0, 0x22, 6, 0xD950);
  else house_D86C(ram, r);
}

/* D97D, the fourth girl's house */
static void house_D97D(uint8_t *ram, HmRegs *r)
{
  static const Day d = { 4, 4, 5, 4, 4, 4, 0xD993, 0xD9FA, 0xD9DA, 0xDA0A, 0xDA1A, 0xD9EA };
  usual_day(ram, r, &d);
}
void hm_MapEvent_D955(uint8_t *ram, HmRegs *r)
{
  if (married_here(ram, r, 0x0008)) spawn(ram, r, 0, 0x22, 9, 0xD978);
  else house_D97D(ram, r);
}
void hm_MapEvent_DA1F(uint8_t *ram, HmRegs *r)
{
  if (married_here(ram, r, 0x0008)) spawn(ram, r, 0, 0x22, 0x0A, 0xDA46);
  else house_D97D(ram, r);
}

/* DA4B, DAED: two more people with a usual day */
void hm_MapEvent_DA4B(uint8_t *ram, HmRegs *r)
{
  static const Day d = { 5, 5, 6, 5, 5, 5, 0xDA61, 0xDAC8, 0xDAA8, 0xDAD8, 0xDAE8, 0xDAB8 };
  usual_day(ram, r, &d);
}
void hm_MapEvent_DAED(uint8_t *ram, HmRegs *r)
{
  static const Day d = { 6, 6, 7, 6, 6, 6, 0xDB03, 0xDB6A, 0xDB4A, 0xDB7A, 0xDB8A, 0xDB5A };
  usual_day(ram, r, &d);
}

/* LDA.L $7F1F68; ORA #bit; STA.L: an event seen */
static void seen(uint8_t *ram, HmRegs *r, uint16_t bit)
{
  r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F68) | bit);
  hm_bus_wr16(ram, 0x7F1F68, r->a);
}

/* DBB7, the fifth girl's house: winter 24; the first visit (group $0B,
 * then $7F1F68 bit 1); otherwise group 5 */
static void house_DBB7(uint8_t *ram, HmRegs *r)
{
  if (byte_is(ram, r, SEASON, 3) && byte_is(ram, r, DAY, 0x18) && flag(ram, r, 0x7F1F74, 0x0004)) {
    spawn(ram, r, 0, 0x0F, 3, 0xDBE1);
    return;
  }
  if (!flag(ram, r, 0x7F1F68, 0x0002)) {
    spawn(ram, r, 0, 0x0B, 7, 0xDBFC);
    seen(ram, r, 0x0002);
    return;
  }
  spawn(ram, r, 0, 5, 0, 0xDC19);
}
void hm_MapEvent_DB8F(uint8_t *ram, HmRegs *r)
{
  if (married_here(ram, r, 0x0010)) spawn(ram, r, 0, 0x22, 0x0B, 0xDBB2);
  else house_DBB7(ram, r);
}
void hm_MapEvent_DC1E(uint8_t *ram, HmRegs *r)
{
  if (married_here(ram, r, 0x0010)) spawn(ram, r, 0, 0x22, 0x0C, 0xDC45);
  else house_DBB7(ram, r);
}

/* DC4A: the first visit (group $0B, $7F1F68 bit 2); $7F1F5E bit 5 or
 * $7F1F6C bit 4 (group $1D); with $7F1F6C bit 2 a second person in slot
 * 9 (group $1C, script 1, or 2 with $7F1F5E bit 3, and then nobody
 * else); from year 2, in fall and winter, on some days (2-7, 13-17,
 * 25-28) of an ordinary weekday the slot-9 person of group $21; then the
 * usual one (none on the special days of $0196 bits 1 / 3 / 9; Sunday
 * group 3, Saturday group 2, else group 1). */
void hm_MapEvent_DC4A(uint8_t *ram, HmRegs *r)
{
  if (!flag(ram, r, 0x7F1F68, 0x0004)) {
    spawn(ram, r, 0, 0x0B, 9, 0xDC60);
    seen(ram, r, 0x0004);
    return;
  }
  if (flag(ram, r, 0x7F1F5E, 0x0020)) { spawn(ram, r, 0, 0x1D, 3, 0xDC88); return; }
  if (flag(ram, r, 0x7F1F6C, 0x0010)) { spawn(ram, r, 0, 0x1D, 1, 0xDCA3); return; }
  if (flag(ram, r, 0x7F1F6C, 0x0004)) {
    if (flag(ram, r, 0x7F1F5E, 0x0008)) { spawn(ram, r, 9, 0x1C, 2, 0xDCD8); return; }
    spawn(ram, r, 9, 0x1C, 1, 0xDCC7);
  } else {
    bool visit = false;
    if (byte_of(ram, r, 0x7F1F18) && !byte_below(ram, r, SEASON, 2)) {
      uint8_t day = byte_of(ram, r, DAY);
      r->c = day >= 1;
      if (day != 1) {
        static const uint8_t bounds[] = { 0x08, 0x0D, 0x12, 0x19, 0x1D };
        for (int i = 0; i < 5; i++) {
          r->c = day >= bounds[i];
          if (!r->c) { visit = !(i & 1); break; }
        }
      }
    }
    if (visit && !flag(ram, r, db(r, FLAGS_0196), 0x001A) && !flag(ram, r, 0x7F1F6A, 0x0200)) {
      uint8_t wd = byte_of(ram, r, WEEKDAY);
      if (wd != 0) {
        r->c = wd >= 6;
        if (wd != 6) spawn(ram, r, 9, 0x21, 0, 0xDD39);
      }
    }
  }
  if (flag(ram, r, db(r, FLAGS_0196), 0x0008)) return;
  if (flag(ram, r, db(r, FLAGS_0196), 0x0200)) return;
  if (flag(ram, r, db(r, FLAGS_0196), 0x0002)) return;
  uint8_t wd = byte_of(ram, r, WEEKDAY);
  if (wd == 0) { spawn(ram, r, 0, 3, 2, 0xDD80); return; }
  r->c = wd >= 6;
  if (wd == 6) { spawn(ram, r, 0, 2, 3, 0xDD90); return; }
  spawn(ram, r, 0, 1, 8, 0xDD70);
}

/* DD95: with $7F1F6A bit 12 but not 13, only group $19; the first visit
 * (group $0B, only in the first spring and summer before the 26th; the
 * visit is marked as seen anyway); then the usual day of script 9 / 7 */
void hm_MapEvent_DD95(uint8_t *ram, HmRegs *r)
{
  if (!flag(ram, r, 0x7F1F6A, 0x2000) && flag(ram, r, 0x7F1F6A, 0x1000)) {
    spawn(ram, r, 0, 0x19, 1, 0xDDB4);
    return;
  }
  if (!flag(ram, r, 0x7F1F68, 0x0008)) {
    bool first = false;
    if (!byte_of(ram, r, 0x7F1F18)) {
      uint8_t season = byte_of(ram, r, SEASON);
      r->c = season >= 2;
      if (!r->c) {
        r->c = season >= 0;
        if (season == 0) first = true;
        else first = byte_below(ram, r, DAY, 0x1A);
      }
    }
    if (first) {
      spawn(ram, r, 0, 0x0B, 0x0A, 0xDDEB);
      seen(ram, r, 0x0008);
      return;
    }
    seen(ram, r, 0x0008);
  }
  if (flag(ram, r, db(r, FLAGS_0196), 0x0008)) { spawn(ram, r, 0, 7, 7, 0xDE4F); return; }
  if (flag(ram, r, db(r, FLAGS_0196), 0x0200)) { spawn(ram, r, 0, 8, 7, 0xDE5F); return; }
  if (flag(ram, r, db(r, FLAGS_0196), 0x0002)) { spawn(ram, r, 0, 4, 7, 0xDE3F); return; }
  spawn(ram, r, 0, 1, 9, 0xDE2F);
}

/* DE64: group $15 until $7F1F68 bit 13; then with $7F1F64 bit 1 group 1 */
void hm_MapEvent_DE64(uint8_t *ram, HmRegs *r)
{
  if (!flag(ram, r, 0x7F1F68, 0x2000)) { spawn(ram, r, 0, 0x15, 0, 0xDE7A); return; }
  if (flag(ram, r, 0x7F1F64, 0x0002)) spawn(ram, r, 0, 1, 0x0A, 0xDE95);
}

/* DE9B: with $7F1F64 bit 3, a scene (slot $15, script $25 of group 0)
 * that stops the player: $096E = $45, $D2 |= $40, action 0. */
void hm_MapEvent_DE9B(uint8_t *ram, HmRegs *r)
{
  if (!flag(ram, r, 0x7F1F64, 0x0008)) return;
  spawn(ram, r, 0x15, 0, 0x25, 0xDEB1);
  r->a = (uint16_t)((r->a & 0xFF00) | 0x45);
  hm_bus_wr8(ram, db(r, 0x096E), 0x45);
  hm_bus_wr8(ram, db(r, 0x096F), 0);
  hm_bus_wr8(ram, db(r, 0x0970), 0);
  r->a = (uint16_t)(hm_dp_r16(ram, r, 0xD2) | 0x0040);
  hm_dp_w16(ram, r, 0xD2, r->a);
  r->a = 0;
  hm_dp_w16(ram, r, 0xD4, 0);
}

/* E407, maps $0C-$0F (the crossroads): $7F1F5C bit 7 off and slot $15's script $15;
 * then the first time (group $0A, $7F1F68 bit 4; while $7F1F68 bit 0 is
 * clear, group $0B after that); from the first winter (or year 2) on, a
 * visit of group $0D (first script 0, $7F1F68 bit 6 and $099F = 4; then
 * script 1, until $7F1F68 bit 8, which also clears $7F1F31). */
void hm_MapEvent_E407(uint8_t *ram, HmRegs *r)
{
  r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F5C) & 0xFF7F);
  hm_bus_wr16(ram, 0x7F1F5C, r->a);
  spawn(ram, r, 0x15, 0, 0x15, 0xE41F);
  if (!flag(ram, r, 0x7F1F68, 0x0001)) {
    if (flag(ram, r, 0x7F1F68, 0x0010)) { spawn(ram, r, 0, 0x0B, 0x0B, 0xE45F); return; }
    spawn(ram, r, 0, 0x0A, 1, 0xE442);
    seen(ram, r, 0x0010);
    return;
  }
  if (!byte_of(ram, r, 0x7F1F18) && !byte_is(ram, r, SEASON, 3)) return;
  if (!flag(ram, r, 0x7F1F68, 0x0040)) {
    spawn(ram, r, 0, 0x0D, 0, 0xE48E);
    seen(ram, r, 0x0040);
    r->a = (uint16_t)((r->a & 0xFF00) | 0x04);
    hm_bus_wr8(ram, db(r, 0x099F), 0x04);
    return;
  }
  if (flag(ram, r, 0x7F1F68, 0x0100)) return;
  spawn(ram, r, 0, 0x0D, 1, 0xE4BD);
  r->a &= 0xFF00;
  hm_bus_wr8(ram, 0x7F1F31, 0);
}

/* E4CB, map $29: until $7F1F6A bit 6, a script at the tile (224, 384)
 * (SUB_8480F8: slot $10, script $1F). Coming from below ($D8 >= $200)
 * with $D2 bit 11: the bit off and the
 * dog placed on this season's farm at (120, 424), the player's animation
 * = his direction. Then group $1D with $7F1F6C bit 4, group $1B with bit
 * 0, or on Saturday group 2. */
void hm_MapEvent_E4CB(uint8_t *ram, HmRegs *r)
{
  if (!flag(ram, r, 0x7F1F6A, 0x0040)) {
    hm_bus_wr16(ram, db(r, 0x0985), 0x00E0);
    hm_bus_wr16(ram, db(r, 0x0987), 0x0180);
    r->a = 0x0010; r->x = 0; r->y = 0x001F;
    hm_jsl(ram, r, 0xE4EE, 0x84);
    hm_SUB_8480F8(ram, r);
    hm_rtl(ram, r);
  }
  r->a = hm_dp_r16(ram, r, 0xD8);
  r->c = r->a >= 0x0200;
  if (r->c) {
    uint16_t state = hm_dp_r16(ram, r, 0xD2);
    r->a = state & 0x0800;
    if (r->a) {
      r->a = (uint16_t)(state & 0xF7FF);
      hm_dp_w16(ram, r, 0xD2, r->a);
      uint8_t season = byte_of(ram, r, SEASON);
      hm_bus_wr8(ram, 0x7F1F30, season);
      hm_bus_wr16(ram, 0x7F1F2C, 0x0078);
      r->a = 0x01A8;
      hm_bus_wr16(ram, 0x7F1F2E, 0x01A8);
      r->a = hm_dp_r16(ram, r, 0xDA);                      /* LDA #0; CLC; ADC */
      r->c = 0;
      hm_bus_wr16(ram, db(r, 0x0901), r->a);
    }
  }
  if (flag(ram, r, 0x7F1F6C, 0x0010)) { spawn(ram, r, 0, 0x1D, 2, 0xE54B); return; }
  if (flag(ram, r, 0x7F1F6C, 0x0001)) { spawn(ram, r, 0, 0x1B, 1, 0xE566); return; }
  if (byte_is(ram, r, WEEKDAY, 6)) spawn(ram, r, 0, 2, 4, 0xE580);
}

/* ── D2E0, the town (maps 4-7) ──────────────────────────────────────────
 *   - with $7F1F60 bit 10, group $14 script 3 (and the bit was meant to be
 *     cleared: see the bug);
 *   - fall 20 before 3 pm: slot $15's script $7E, the numbers 0-5 (and two
 *     0s) at $09A4-$09AB shuffled, $09A2 = 0, and group $28 script 2;
 *   - spring 23 before 3 pm group $0E; fall 12 before 3 pm group $26;
 *     winter 10 before 5 pm group $27;
 *   - otherwise slot $15's script $7E until $7F1F6A bit 14, and the usual
 *     day of script 0 (nobody on the special days of $0196 bits 1 / 3 / 9
 *     and on rain).
 * Bug: the code clears bit 10 of $7F1F60 into the direct page ($60, STA.B)
 * instead of $7F1F60 (STA.L), so the flag stays set and this happens
 * every time the town is entered.
 * The shuffle: for each of the first 7 places, swap it with a random one
 * of 9 (RNGReturn0toA(8): 0-7, so places $09AB too).
 * ───────────────────────────────────────────────────────────────────── */
#define SHUFFLE 0x09A4   /* DB: */

static void festival_order(uint8_t *ram, HmRegs *r)
{
  hm_dp_w16(ram, r, 0x7E, 0);
  static const uint8_t start[8] = { 0, 1, 2, 3, 4, 5, 0, 0 };
  for (int i = 0; i < 8; i++) hm_bus_wr8(ram, db(r, (uint16_t)(SHUFFLE + i)), start[i]);
  r->a = (uint16_t)((r->a & 0xFF00) | 0);
  for (uint16_t y = 0; y != 7; y++) {
    hm_push8(ram, r, (uint8_t)(y >> 8)); hm_push8(ram, r, (uint8_t)y);   /* PHY */
    hm_dp_w16(ram, r, 0x84, y);
    uint8_t here = hm_bus_rd8(ram, db(r, (uint16_t)(SHUFFLE + y)));
    *hm_dp(ram, r, 0x95) = here;
    r->x = y;
    r->a = (uint16_t)((r->a & 0xFF00) | 8);
    hm_jsl(ram, r, 0xD37D, 0x80);
    hm_RNGReturn0toA(ram, r);
    hm_rtl(ram, r);
    uint16_t other = (uint8_t)r->a;                       /* XBA; LDA #0; XBA */
    r->x = other;
    hm_dp_w16(ram, r, 0x86, other);
    uint8_t there = hm_bus_rd8(ram, db(r, (uint16_t)(SHUFFLE + other)));
    hm_bus_wr8(ram, db(r, (uint16_t)(SHUFFLE + y)), there);
    hm_bus_wr8(ram, db(r, (uint16_t)(SHUFFLE + other)), here);
    r->a = (uint16_t)((other & 0xFF00) | here);
    uint8_t lo = hm_pull8(ram, r);                       /* PLY */
    r->y = (uint16_t)(lo | hm_pull8(ram, r) << 8);
    r->c = (uint16_t)(y + 1) >= 7;
  }
  r->y = 7;
  hm_bus_wr8(ram, db(r, 0x09A2), 0);
}

/* SEP #$20; season, day, hour below: a festival morning */
static bool date_before(uint8_t *ram, HmRegs *r, uint8_t season, uint8_t day, uint8_t hour)
{
  if (season == 0) {
    uint8_t s = byte_of(ram, r, SEASON);                 /* LDA; BNE */
    if (s) return false;
  } else if (!byte_is(ram, r, SEASON, season)) return false;
  return byte_is(ram, r, DAY, day) && byte_below(ram, r, HOUR, hour);
}

void hm_MapEvent_D2E0(uint8_t *ram, HmRegs *r)
{
  if (flag(ram, r, 0x7F1F60, 0x0400)) {
    r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F60) & 0xFBFF);
    hm_dp_w16(ram, r, 0x60, r->a);                       /* STA.B $60: the bug */
    spawn(ram, r, 0, 0x14, 3, 0xD2FF);
    return;
  }
  if (date_before(ram, r, 2, 0x14, 0x0F)) {
    spawn(ram, r, 0x15, 0, 0x7E, 0xD334);
    festival_order(ram, r);
    spawn(ram, r, 0, 0x28, 2, 0xD3B3);
    return;
  }
  if (date_before(ram, r, 0, 0x17, 0x0F)) { spawn(ram, r, 0, 0x0E, 0, 0xD3DD); return; }
  if (date_before(ram, r, 2, 0x0C, 0x0F)) { spawn(ram, r, 0, 0x26, 0, 0xD409); return; }
  if (date_before(ram, r, 3, 0x0A, 0x11)) { spawn(ram, r, 0, 0x27, 0, 0xD435); return; }
  if (!flag(ram, r, 0x7F1F6A, 0x4000)) spawn(ram, r, 0x15, 0, 0x7E, 0xD44E);
  if (!flag(ram, r, 0x7F1F68, 0x0001)) { spawn(ram, r, 0, 0x0B, 0, 0xD468); return; }
  if (flag(ram, r, db(r, FLAGS_0196), 0x0008)) return;
  if (byte_is(ram, r, db(r, WEATHER_TOMORROW), 3)) return;
  if (flag(ram, r, 0x7F1F64, 0x0020)) return;
  if (flag(ram, r, db(r, FLAGS_0196), 0x0200)) return;
  if (flag(ram, r, db(r, FLAGS_0196), 0x0002)) return;
  uint8_t wd = byte_of(ram, r, WEEKDAY);
  if (wd == 0) { spawn(ram, r, 0, 3, 0, 0xD4C2); return; }
  r->c = wd >= 6;
  if (wd == 6) { spawn(ram, r, 0, 2, 0, 0xD4D2); return; }
  spawn(ram, r, 0, 1, 0, 0xD4B2);
}

/* ── E76D, map $3C: the days of the introduction ────────────────────────
 * (HM-Decomp has its first $140 bytes as data; disassembled from the ROM.)
 * The player wakes up: 7:00 ($7F1F1C-$7F1F1E = 7, 0, 0), $0915 = 0, the
 * state $D2 = 1 (bit 1 off), action 0, facing 0, $0911 = $0901 = 0, the
 * clock stopped ($0973 = 0), nothing in hand ($091D), stamina full ($0918
 * = $0917), $0925 = 0. Then the step $7F1F49 (0-$1C) says which script of
 * group $47 runs (the step + 1), and the step goes one up; from $1D on,
 * nothing. Steps 7 and $0D also fill the shed ($3D, 0, $12, 0 and $0D, $64,
 * $12, 0) and empty the hands of tools.
 * ───────────────────────────────────────────────────────────────────── */
#define INTRO_STEP 0x7F1F49

/* waking up at `hour`:00 with full stamina, standing, facing down, the
 * clock stopped, nothing in hand ($D2 = 1) */
static void wake_up(uint8_t *ram, HmRegs *r, uint8_t hour)
{
  const uint8_t clock[3] = { hour, 0, 0 };
  for (int i = 0; i < 3; i++) hm_bus_wr8(ram, 0x7F1F1C + i, clock[i]);
  hm_bus_wr16(ram, db(r, 0x0915), 0);
  hm_dp_w16(ram, r, 0xD2, 0);
  hm_dp_w16(ram, r, 0xD4, 0);
  uint8_t max = hm_bus_rd8(ram, db(r, 0x0917));
  hm_bus_wr8(ram, db(r, 0x0918), max);
  hm_bus_wr8(ram, db(r, 0x0925), 0);
  hm_dp_w16(ram, r, 0xD2, 0x0001);                         /* LDA $D2; ORA #1 */
  hm_dp_w16(ram, r, 0xD4, 0);
  hm_dp_w16(ram, r, 0xDA, 0);
  hm_bus_wr16(ram, db(r, 0x0911), 0);
  hm_bus_wr16(ram, db(r, 0x0901), 0);
  hm_bus_wr8(ram, db(r, 0x0973), 0);
  hm_bus_wr8(ram, db(r, 0x091D), 0);
  r->a = (uint16_t)(0xFFFD & hm_dp_r16(ram, r, 0xD2));
  hm_dp_w16(ram, r, 0xD2, r->a);
}

void hm_MapEvent_E76D(uint8_t *ram, HmRegs *r)
{
  wake_up(ram, r, 7);

  uint8_t step = byte_of(ram, r, INTRO_STEP);
  if (step) r->c = 1;                         /* the CMP chain ends on an equal or above */
  if (step >= 0x1D) return;
  /* each step's code: 24 bytes from $E8AD, 56 for steps 7 and $0D */
  uint16_t at = (uint16_t)(0xE8AD + 0x18 * step);
  if (step > 7) at = (uint16_t)(at + 0x20);
  if (step > 0x0D) at = (uint16_t)(at + 0x20);
  if (step == 7 || step == 0x0D) {
    static const uint8_t shed[2][4] = { { 0x3D, 0x00, 0x12, 0x00 }, { 0x0D, 0x64, 0x12, 0x00 } };
    const uint8_t *s = shed[step == 0x0D];
    for (int i = 0; i < 4; i++) hm_bus_wr8(ram, 0x7F1F00 + i, s[i]);
    r->a = (uint16_t)((r->a & 0xFF00) | s[3]);
    hm_bus_wr8(ram, db(r, 0x0921), 0);
    hm_bus_wr8(ram, db(r, 0x0923), 0);
    at = (uint16_t)(at + 0x20);
  }
  spawn(ram, r, 0, 0x47, (uint16_t)(step + 1), (uint16_t)(at + 0x0B));
  r->a = (uint16_t)((r->a & 0xFF00) | (uint8_t)(step + 1));
  hm_bus_wr8(ram, INTRO_STEP, (uint8_t)(step + 1));
}

/* ── EBA6, maps $3D and $4B: the ending ─────────────────────────────────
 * The player wakes at 6:00 (as in the introduction) and the step
 * $7F1F47 says which part of the ending's slide show comes next. Each part
 * is shown only if it applies; if not, the next one is tried:
 *   0  no wife, happiness below 100, no cows and no chickens: the bad
 *      ending (group $2F; next step $14)
 *   1  $7F1F5E bit 15 on; 7 cows or more ($30)   2  1-6 cows ($31)
 *   3  chickens ($32)   4  the festival cow's affection ($09B5) $C0+ ($33)
 *   5-8  200 or more corn, tomatoes, turnips, potatoes shipped ($34, $35,
 *      $37, $36)
 *   9 / $0A  no wife and happiness 100+ / 800+ ($38, $39)
 *   $0B-$0F  married to Maria, Ann, Nina, Ellen, Eve (groups $3A-$3E: two
 *      scripts for the first four, steps $21-$24 show the second)
 *   $10  no wife, happiness 200+ and the five girls' hearts adding up to
 *      1500+ ($3F)
 *   $11 a child ($40)   $12 two children ($41)
 *   $13  everything: two children, happiness 900+, cows and chickens, the
 *      10 power berries, 10,000 G (money is kept in tens) and 100 hugs of
 *      the dog ($42; $7F1F5E bit 15 off)
 *   $14  the girl with the most hearts ($09B3, the first on a tie) and the
 *      credits (group $46, scripts 0-7; $7F1F5E bit 15 on, off at the end)
 *   $20  the ranch score (RanchMasteryCalculator, which shows script $0C)
 * Steps $1C-$1F (scripts 8-$0B) follow each other, but nothing here leads
 * to them (step $1B goes on to $20).
 * ───────────────────────────────────────────────────────────────────── */
#define ENDING_STEP 0x7F1F47
#define COWS        0x7F1F0A
#define CHICKENS    0x7F1F0B
#define HAPPINESS   0x7F1F33
#define HEARTS      0x7F1F1F   /* 5 words: Maria, Ann, Nina, Ellen, Eve */
#define FLAGS_5E    0x7F1F5E

/* spawn, then SEP #$20; LDA #next; STA.L $7F1F47 */
static void part(uint8_t *ram, HmRegs *r, uint16_t x, uint16_t y, uint16_t at, uint8_t next)
{
  spawn(ram, r, 0, x, y, at);
  r->a = (uint16_t)((r->a & 0xFF00) | next);
  hm_bus_wr8(ram, ENDING_STEP, next);
}
static void flags_5e(uint8_t *ram, HmRegs *r, bool on)
{
  uint16_t v = hm_bus_rd16(ram, FLAGS_5E);
  r->a = on ? (uint16_t)(v | 0x8000) : (uint16_t)(v & 0x7FFF);
  hm_bus_wr16(ram, FLAGS_5E, r->a);
}
static bool no_wife(uint8_t *ram, HmRegs *r) { return !flag(ram, r, 0x7F1F66, 0x001F); }
static bool shipped(uint8_t *ram, HmRegs *r, uint32_t addr) { return word_at_least(ram, r, addr, 200); }

void hm_MapEvent_EBA6(uint8_t *ram, HmRegs *r)
{
  wake_up(ram, r, 6);
  uint8_t step = byte_of(ram, r, ENDING_STEP);
  if (step) r->c = 1;                         /* the CMP chain ends on an equal or above */
  switch (step) {
  case 0x00:
    if (no_wife(ram, r) && !word_at_least(ram, r, HAPPINESS, 100) &&
        !byte_of(ram, r, COWS) && !byte_of(ram, r, CHICKENS)) {
      part(ram, r, 0x2F, 0, 0xED4D, 0x14);
      return;
    }
    /* fall through */
  case 0x01:
    flags_5e(ram, r, true);
    if (!byte_below(ram, r, COWS, 7)) { part(ram, r, 0x30, 0, 0xED7E, 0x02); return; }
    /* fall through */
  case 0x02: {
    uint8_t cows = byte_of(ram, r, COWS);
    if (cows && byte_below(ram, r, COWS, 7)) { part(ram, r, 0x31, 0, 0xEDA4, 0x03); return; }
  } /* fall through */
  case 0x03:
    if (byte_of(ram, r, CHICKENS)) { part(ram, r, 0x32, 0, 0xEDC6, 0x04); return; }
    /* fall through */
  case 0x04:
    if (!byte_below(ram, r, db(r, 0x09B5), 0xC0)) { part(ram, r, 0x33, 0, 0xEDE9, 0x05); return; }
    /* fall through */
  case 0x05:
    if (shipped(ram, r, 0x7F1F4A)) { part(ram, r, 0x34, 0, 0xEE0C, 0x06); return; }
    /* fall through */
  case 0x06:
    if (shipped(ram, r, 0x7F1F4C)) { part(ram, r, 0x35, 0, 0xEE2F, 0x07); return; }
    /* fall through */
  case 0x07:
    if (shipped(ram, r, 0x7F1F4E)) { part(ram, r, 0x37, 0, 0xEE52, 0x08); return; }
    /* fall through */
  case 0x08:
    if (shipped(ram, r, 0x7F1F50)) { part(ram, r, 0x36, 0, 0xEE75, 0x09); return; }
    /* fall through */
  case 0x09:
    if (no_wife(ram, r) && word_at_least(ram, r, HAPPINESS, 100)) { part(ram, r, 0x38, 0, 0xEEA1, 0x0A); return; }
    /* fall through */
  case 0x0A:
    if (no_wife(ram, r) && word_at_least(ram, r, HAPPINESS, 800)) { part(ram, r, 0x39, 0, 0xEECD, 0x0B); return; }
    /* fall through */
  case 0x0B:
    if (flag(ram, r, 0x7F1F66, 0x0001)) { part(ram, r, 0x3A, 0, 0xEEF0, 0x21); return; }
    goto girl_2;
  case 0x21:
    part(ram, r, 0x3A, 1, 0xEF08, 0x0C);
    return;
  case 0x0C:
  girl_2:
    if (flag(ram, r, 0x7F1F66, 0x0002)) { part(ram, r, 0x3B, 0, 0xEF2B, 0x22); return; }
    goto girl_3;
  case 0x22:
    part(ram, r, 0x3B, 1, 0xEF43, 0x0D);
    return;
  case 0x0D:
  girl_3:
    if (flag(ram, r, 0x7F1F66, 0x0004)) { part(ram, r, 0x3C, 0, 0xEF66, 0x23); return; }
    goto girl_4;
  case 0x23:
    part(ram, r, 0x3C, 1, 0xEF7E, 0x0E);
    return;
  case 0x0E:
  girl_4:
    if (flag(ram, r, 0x7F1F66, 0x0008)) { part(ram, r, 0x3D, 0, 0xEFA1, 0x24); return; }
    goto girl_5;
  case 0x24:
    part(ram, r, 0x3D, 1, 0xEFB9, 0x0F);
    return;
  case 0x0F:
  girl_5:
    if (flag(ram, r, 0x7F1F66, 0x0010)) { part(ram, r, 0x3E, 0, 0xEFDC, 0x10); return; }
    /* fall through */
  case 0x10:
    if (no_wife(ram, r) && word_at_least(ram, r, HAPPINESS, 200)) {
      uint32_t sum = 0;
      unsigned carry = 0;
      for (int i = 0; i < 5; i++) {                        /* CLC; ADC x 4 */
        sum = (sum & 0xFFFF) + hm_bus_rd16(ram, HEARTS + 2 * i) + carry;
        carry = sum > 0xFFFF;
      }
      r->a = (uint16_t)sum;
      r->c = r->a >= 0x05DC;
      if (r->c) { part(ram, r, 0x3F, 0, 0xF022, 0x11); return; }
    }
    /* fall through */
  case 0x11:
    if (flag(ram, r, 0x7F1F6E, 0x000C)) { part(ram, r, 0x40, 0, 0xF045, 0x12); return; }
    /* fall through */
  case 0x12:
    if (flag(ram, r, 0x7F1F6E, 0x0008)) { part(ram, r, 0x41, 0, 0xF068, 0x13); return; }
    /* fall through */
  case 0x13:
    if (flag(ram, r, 0x7F1F6E, 0x0008) && word_at_least(ram, r, HAPPINESS, 900) &&
        byte_of(ram, r, COWS) && byte_of(ram, r, CHICKENS) && byte_is(ram, r, 0x7F1F36, 10)) {
      uint32_t low = (uint32_t)hm_bus_rd16(ram, 0x7F1F04) + 0xFC18;   /* money - 1000 */
      r->c = low > 0xFFFF;
      uint32_t high = (uint32_t)hm_bus_rd8(ram, 0x7F1F06) + 0xFF + r->c;
      r->c = high > 0xFF;
      r->a = (uint16_t)((low & 0xFF00) | (uint8_t)high);
      if (!(high & 0x80) && word_at_least(ram, r, 0x7F1F52, 100)) {
        part(ram, r, 0x42, 0, 0xF0C9, 0x14);
        flags_5e(ram, r, false);
        return;
      }
    }
    /* fall through */
  case 0x14: {
    uint16_t best = hm_bus_rd16(ram, HEARTS);
    r->a = best;
    hm_bus_wr16(ram, db(r, 0x09B3), best);
    for (int i = 1; i < 5; i++) {
      uint16_t h = hm_bus_rd16(ram, HEARTS + 2 * i);
      r->c = r->a >= h;                                      /* CMP.L; BCS */
      if (!r->c) { r->a = h; hm_bus_wr16(ram, db(r, 0x09B3), h); }
    }
    part(ram, r, 0x46, 0, 0xF131, 0x15);
    flags_5e(ram, r, true);
    return;
  }
  case 0x15: part(ram, r, 0x46, 1, 0xF156, 0x16); return;
  case 0x16: part(ram, r, 0x46, 2, 0xF16E, 0x17); return;
  case 0x17: part(ram, r, 0x46, 3, 0xF186, 0x18); return;
  case 0x18: part(ram, r, 0x46, 4, 0xF19E, 0x19); return;
  case 0x19: part(ram, r, 0x46, 5, 0xF1B6, 0x1A); return;
  case 0x1A: part(ram, r, 0x46, 6, 0xF1CE, 0x1B); return;
  case 0x1B: part(ram, r, 0x46, 7, 0xF1E6, 0x20); flags_5e(ram, r, false); return;
  case 0x1C: part(ram, r, 0x46, 8, 0xF20B, 0x1D); return;
  case 0x1D: part(ram, r, 0x46, 9, 0xF223, 0x1E); return;
  case 0x1E: part(ram, r, 0x46, 0x0A, 0xF23B, 0x1F); return;
  case 0x1F: part(ram, r, 0x46, 0x0B, 0xF253, 0x20); flags_5e(ram, r, false); return;
  case 0x20: hm_RanchMasteryCalculator(ram, r); return;   /* JMP */
  default: return;                                        /* $25 (CODE_83F4D7) and up */
  }
}

/* ── CB69, the farm (maps 0-3) ──────────────────────────────────────────
 * In this order:
 *   - the wife at home (Maria, Nina or Ellen: group $44, unless $7F1F6E
 *     bit 1) and the children once 60 days old (as E6C4);
 *   - at 6:00:00 on a visitor day (2-7, 13-17, 25-28; not on the special
 *     days of $0196 bits 1 / 3 / 4) while still single, the first girl
 *     with 200+ hearts whose visit was not seen comes (groups $19-$1D;
 *     each marks her flag and blocks the later ones: Maria $7F1F6A bit
 *     12, Ann bit 14, Nina $7F1F6C bit 0, Ellen bit 2, Eve bit 4);
 *   - $7F1F5A bit 1: a scene (slot 7, script $20) that stops the player;
 *   - the first days: group $0A until the first event ($7F1F68 bit 0),
 *     then group $0C twice ($7F1F68 bits 5 and 7; the second also puts
 *     the dog at (120, 424) on map 0);
 *   - at 6:00:00 the morning visitors of group $24: rain or snow (weather
 *     6+; at 6:00 with seconds not 0 the rest of these is skipped),
 *     $7F1F70 bit 1, $7F1F64 bit 4, and in the first summer before the
 *     20th the first time ($7F1F70 bit 0);
 *   - $7F1F66 bit 6: a scene (slot 9, script $2C; the bit off);
 *   - $7F1F66 bit 7 with some development: slot 0 script $2E;
 *   - $7F1F66 bit 8: two scenes (slots 9 and $0A, scripts $36 / $37);
 *   - $7F1F32 = $15 the first time ($7F1F64 bit 8): a scene (slot $0B,
 *     script $1D);
 *   - from year 2 at 6:00:00 on an ordinary day with 30,000 G or more,
 *     once ($7F1F68 bit 10): group $13;
 *   - from the first winter at 6:00:00 on a Saturday, with $7F1F64 bit 6
 *     or 7, once ($7F1F68 bit 14): group $16;
 *   - in fall on a visitor day with $7F1F64 bit 6 or 7 and $7F1F68 bit
 *     13, until $7F1F6A bit 8: group $18 (and $7F1F6A bit 7 is set: it is
 *     not the bit tested, so this happens every such day);
 *   - $7F1F6E bit 10 with 6+ chickens at 6:00:00 on an ordinary day: group
 *     $29, and from then on ($7F1F6A bit 10) every time the farm is
 *     entered, until $7F1F6A bit 11;
 *   - at 6:00:00 on an ordinary day, one time in nine: group $23, and 2 of
 *     happiness.
 * ───────────────────────────────────────────────────────────────────── */
static bool six_sharp(uint8_t *ram, HmRegs *r)        /* hour 6, minutes 0, seconds 0 */
{
  return byte_is(ram, r, HOUR, 6) && !byte_of(ram, r, 0x7F1F1D) && !byte_of(ram, r, 0x7F1F1E);
}
/* CMP #1; BEQ no; CMP #8; BCC yes; CMP #$0D; BCC no; ... CMP #$1D */
static bool visitor_day(uint8_t *ram, HmRegs *r)
{
  uint8_t day = byte_of(ram, r, DAY);
  r->c = day >= 1;
  if (day == 1) return false;
  static const uint8_t bounds[] = { 0x08, 0x0D, 0x12, 0x19, 0x1D };
  for (int i = 0; i < 5; i++) {
    r->c = day >= bounds[i];
    if (!r->c) return !(i & 1);
  }
  return false;
}
static void flag_on(uint8_t *ram, HmRegs *r, uint32_t addr, uint16_t bit)
{
  r->a = (uint16_t)(hm_bus_rd16(ram, addr) | bit);
  hm_bus_wr16(ram, addr, r->a);
}
/* a scene that stops the player: $096E = code, $D2 |= $40, action 0 */
static void stop_player(uint8_t *ram, HmRegs *r, uint8_t code)
{
  r->a = (uint16_t)((r->a & 0xFF00) | code);
  hm_bus_wr8(ram, db(r, 0x096E), code);
  hm_bus_wr8(ram, db(r, 0x096F), 0);
  hm_bus_wr8(ram, db(r, 0x0970), 0);
  r->a = (uint16_t)(hm_dp_r16(ram, r, 0xD2) | 0x0040);
  hm_dp_w16(ram, r, 0xD2, r->a);
  r->a = 0;
  hm_dp_w16(ram, r, 0xD4, 0);
}
/* LDA.L moneyL; CLC; ADC #-n; SEP; LDA.L moneyH; ADC #$FF: true if not negative */
static bool money_at_least(uint8_t *ram, HmRegs *r, uint16_t minus_n)
{
  uint32_t low = (uint32_t)hm_bus_rd16(ram, 0x7F1F04) + minus_n;
  r->c = low > 0xFFFF;
  uint32_t high = (uint32_t)hm_bus_rd8(ram, 0x7F1F06) + 0xFF + r->c;
  r->c = high > 0xFF;
  r->a = (uint16_t)((low & 0xFF00) | (uint8_t)high);
  return !(high & 0x80);
}
/* a girl's visit: her flag, her group, $09A3 = 0 */
static void girl_visit(uint8_t *ram, HmRegs *r, uint32_t addr, uint16_t bit, uint16_t x, uint16_t at)
{
  flag_on(ram, r, addr, bit);
  spawn(ram, r, 0, x, 0, at);
  r->a &= 0xFF00;
  hm_bus_wr8(ram, db(r, 0x09A3), 0);
}

static bool girls_visit(uint8_t *ram, HmRegs *r)
{
  if (flag(ram, r, db(r, FLAGS_0196), 0x001A)) return false;
  for (uint16_t g = 1; g <= 0x10; g <<= 1) if (flag(ram, r, 0x7F1F66, g)) return false;
  if (!six_sharp(ram, r)) return false;
  if (!visitor_day(ram, r)) return false;
  const uint32_t A6 = 0x7F1F6A, C6 = 0x7F1F6C, W = 0x7F1F66;
  if (!flag(ram, r, W, 0x0001) && !flag(ram, r, W, 0x0080) && !flag(ram, r, A6, 0x2000) &&
      !flag(ram, r, A6, 0x1000) && word_at_least(ram, r, HEARTS, 200)) {
    girl_visit(ram, r, A6, 0x1000, 0x19, 0xCCDF); return true;
  }
  if (!flag(ram, r, A6, 0x1000) && !flag(ram, r, W, 0x0002) && !flag(ram, r, A6, 0x8000) &&
      !flag(ram, r, A6, 0x4000) && word_at_least(ram, r, HEARTS + 2, 200)) {
    girl_visit(ram, r, A6, 0x4000, 0x1A, 0xCD2E); return true;
  }
  if (!flag(ram, r, A6, 0x1000) && !flag(ram, r, A6, 0x4000) && !flag(ram, r, W, 0x0004) &&
      !flag(ram, r, C6, 0x0002) && !flag(ram, r, C6, 0x0001) && word_at_least(ram, r, HEARTS + 4, 200)) {
    girl_visit(ram, r, C6, 0x0001, 0x1B, 0xCD86); return true;
  }
  if (!flag(ram, r, A6, 0x1000) && !flag(ram, r, A6, 0x4000) && !flag(ram, r, C6, 0x0001) &&
      !flag(ram, r, W, 0x0008) && !flag(ram, r, C6, 0x0008) && !flag(ram, r, C6, 0x0004) &&
      word_at_least(ram, r, HEARTS + 6, 200)) {
    girl_visit(ram, r, C6, 0x0004, 0x1C, 0xCDE7); return true;
  }
  if (!flag(ram, r, A6, 0x1000) && !flag(ram, r, A6, 0x4000) && !flag(ram, r, C6, 0x0001) &&
      !flag(ram, r, C6, 0x0004) && !flag(ram, r, W, 0x0010) && !flag(ram, r, C6, 0x0020) &&
      !flag(ram, r, C6, 0x0010) && word_at_least(ram, r, HEARTS + 8, 200)) {
    girl_visit(ram, r, C6, 0x0010, 0x1D, 0xCE51); return true;
  }
  return false;
}

void hm_MapEvent_CB69(uint8_t *ram, HmRegs *r)
{
  if (!flag(ram, r, 0x7F1F6E, 0x0002)) {
    if (flag(ram, r, 0x7F1F66, 0x0001))      spawn(ram, r, 0x13, 0x44, 0, 0xCB9C);
    else if (flag(ram, r, 0x7F1F66, 0x0004)) spawn(ram, r, 0x13, 0x44, 2, 0xCBAD);
    else if (flag(ram, r, 0x7F1F66, 0x0008)) spawn(ram, r, 0x13, 0x44, 3, 0xCBBE);
  }
  if (flag(ram, r, 0x7F1F6E, 0x0004) && word_at_least(ram, r, KID1_AGE, 0x003C))
    spawn(ram, r, 0x14, 0x45, 1, 0xCBE3);
  if (flag(ram, r, 0x7F1F6E, 0x0008) && word_at_least(ram, r, KID2_AGE, 0x003C))
    spawn(ram, r, 0x15, 0x45, 4, 0xCC06);
  if (girls_visit(ram, r)) return;

  if (flag(ram, r, 0x7F1F5A, 0x0002)) { spawn(ram, r, 7, 0, 0x20, 0xCE71); stop_player(ram, r, 0x42); return; }
  if (!flag(ram, r, 0x7F1F68, 0x0001)) { spawn(ram, r, 0, 0x0A, 2, 0xCEA9); return; }
  if (!flag(ram, r, 0x7F1F68, 0x0020)) {
    spawn(ram, r, 0, 0x0C, 0, 0xCEC4);
    seen(ram, r, 0x0020);
    r->a = (uint16_t)((r->a & 0xFF00) | 3);
    hm_bus_wr8(ram, db(r, 0x099F), 3);
    return;
  }
  if (!flag(ram, r, 0x7F1F68, 0x0080)) {
    spawn(ram, r, 0, 0x0C, 1, 0xCEF3);
    hm_bus_wr16(ram, 0x7F1F2C, 0x0078);
    r->a = 0x01A8;
    hm_bus_wr16(ram, 0x7F1F2E, 0x01A8);
    r->a = 0x0100;                                          /* SEP; LDA #0 */
    hm_bus_wr8(ram, 0x7F1F30, 0);
    return;
  }
  bool skip_mornings = false;
  if (!byte_below(ram, r, db(r, WEATHER_TOMORROW), 6) && byte_is(ram, r, HOUR, 6) &&
      !byte_of(ram, r, 0x7F1F1D)) {
    if (!byte_of(ram, r, 0x7F1F1E)) { spawn(ram, r, 6, 0x24, 0, 0xCF3D); return; }
    skip_mornings = true;
  }
  if (!skip_mornings) {
    if (flag(ram, r, 0x7F1F70, 0x0002) && six_sharp(ram, r)) { spawn(ram, r, 6, 0x24, 2, 0xCF6E); return; }
    if (flag(ram, r, 0x7F1F64, 0x0010) && six_sharp(ram, r)) { spawn(ram, r, 6, 0x24, 3, 0xCF9F); return; }
    if (!flag(ram, r, 0x7F1F70, 0x0001) && !byte_of(ram, r, 0x7F1F18) && byte_is(ram, r, SEASON, 1) &&
        byte_below(ram, r, DAY, 0x14) && six_sharp(ram, r)) {
      flag_on(ram, r, 0x7F1F70, 0x0001);
      spawn(ram, r, 0x0B, 0x24, 1, 0xCFF3);
      return;
    }
  }
  if (flag(ram, r, 0x7F1F66, 0x0040)) {
    spawn(ram, r, 9, 0, 0x2C, 0xD00E);
    stop_player(ram, r, 0x47);
    r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F66) & 0xFFBF);
    hm_bus_wr16(ram, 0x7F1F66, r->a);
    return;
  }
  if (flag(ram, r, 0x7F1F66, 0x0080) && byte_of(ram, r, 0x7F1F35)) { spawn(ram, r, 0, 0, 0x2E, 0xD05B); return; }
  if (flag(ram, r, 0x7F1F66, 0x0100)) {
    r->a = (uint16_t)(hm_bus_rd16(ram, 0x7F1F66) & 0xFEFF);
    hm_bus_wr16(ram, 0x7F1F66, r->a);
    spawn(ram, r, 9, 0, 0x36, 0xD081);
    spawn(ram, r, 0x0A, 0, 0x37, 0xD090);
    stop_player(ram, r, 0x48);
    return;
  }
  if (!flag(ram, r, 0x7F1F64, 0x0100) && byte_is(ram, r, 0x7F1F32, 0x15)) {
    flag_on(ram, r, 0x7F1F64, 0x0100);
    spawn(ram, r, 0x0B, 0, 0x1D, 0xD0DD);
    stop_player(ram, r, 0x49);
    return;
  }
  if (byte_of(ram, r, 0x7F1F18) && six_sharp(ram, r) && !flag(ram, r, db(r, FLAGS_0196), 0x001A) &&
      money_at_least(ram, r, 0xF448) && !flag(ram, r, 0x7F1F68, 0x0400)) {
    spawn(ram, r, 0, 0x13, 0, 0xD151);
    seen(ram, r, 0x0400);
    return;
  }
  if ((byte_of(ram, r, 0x7F1F18) || byte_is(ram, r, SEASON, 3)) && six_sharp(ram, r) &&
      byte_is(ram, r, WEEKDAY, 6) && !flag(ram, r, db(r, FLAGS_0196), 0x001A) &&
      flag(ram, r, 0x7F1F64, 0x00C0) && !flag(ram, r, 0x7F1F68, 0x4000)) {
    seen(ram, r, 0x4000);
    spawn(ram, r, 0, 0x16, 0, 0xD1C9);
    return;
  }
  if (byte_is(ram, r, SEASON, 2) && visitor_day(ram, r) && !flag(ram, r, db(r, FLAGS_0196), 0x001A) &&
      flag(ram, r, 0x7F1F64, 0x00C0) && flag(ram, r, 0x7F1F68, 0x2000) && !flag(ram, r, 0x7F1F6A, 0x0100)) {
    flag_on(ram, r, 0x7F1F6A, 0x0080);
    spawn(ram, r, 0, 0x18, 0, 0xD239);
    return;
  }
  if (!flag(ram, r, 0x7F1F6A, 0x0800)) {
    if (flag(ram, r, 0x7F1F6A, 0x0400)) { spawn(ram, r, 0, 0x29, 0, 0xD299); return; }
    if (!flag(ram, r, db(r, FLAGS_0196), 0x001E) && flag(ram, r, 0x7F1F6E, 0x0400) &&
        !byte_below(ram, r, CHICKENS, 6) && six_sharp(ram, r)) {
      flag_on(ram, r, 0x7F1F6A, 0x0400);
      spawn(ram, r, 0, 0x29, 0, 0xD299);
      return;
    }
  }
  if (flag(ram, r, db(r, FLAGS_0196), 0x001A) || !six_sharp(ram, r)) return;
  r->a = (uint16_t)((r->a & 0xFF00) | 8);
  hm_jsl(ram, r, 0xD2C3, 0x80);
  hm_RNGReturn0toA(ram, r);
  hm_rtl(ram, r);
  if ((uint8_t)r->a) return;                                /* BNE */
  spawn(ram, r, 0, 0x23, 0, 0xD2D1);
  r->a = 0x0002;
  hm_jsl(ram, r, 0xD2DD, 0x83);
  hm_AddPlayerHappiness(ram, r);
  hm_rtl(ram, r);
}

/* ── DED3, the player's house (maps $15-$17, its three sizes) ───────────
 *   - summer 30 of the third year (year 2): the ending (JMP CODE_83F4D8);
 *   - the music of the house after the extensions ($098B = $17 with
 *     $7F1F64 bit 7, $16 with bit 6);
 *   - with $7F1F6C bit 13 (and not bit 14), a script at the tile (156,
 *     144) (SUB_8480F8: slot $10, script $1F);
 *   - things the night left to do, each one flag of $7F1F6E cleared:
 *     bit 7, song $19 (UNK_Audio26) and the nine tiles from (576, 704)
 *     to (608, 736) set to 2 (EditTileonMapandsets0181); bit 8, song $15;
 *     bit 9, noise $22 of bank 7 twice (after 20 and 30 frames,
 *     UNK_Audio24); bit 5, noise $20 and 60 frames;
 *   - then one scene, the first that fits:
 *       bit 4: group $1F (script 2 with $7F1F5E bit 14; else script 1
 *              with bit 13, and $099F = 5 if the first child is 60 days
 *              old, 6 if not; else script 0);
 *       bit 6 (cleared): a cow's scene: the name of cow $0937 (bytes
 *              $0C-$0F of its record, GetsCowPointer) at $0889-$088F,
 *              one letter per word, and group $20;
 *       the wife (first bit of $7F1F66): with her heart below 200 at
 *              6:00:00, unless a child or bit 0 of $7F1F6E, group $22
 *              ($7F1F6E bits 0-1 and $7F1F5E bit 12 set); otherwise
 *              $7F1F5E bit 12 follows $7F1F6E bit 1, or her group $43
 *              (slot $13, script = her number);
 *   - and after the wife, every time: the children 60 days old or more
 *     (group $45: slot $14 script 0, slot $15 script 3) and the man of
 *     group $0A: script 3 with $7F1F60 bit 5, script 0 until $7F1F68 bit
 *     0; script 4 on summer 1 of the second year at 6:00:00, 5 on
 *     summer 29 of the third year at 6:00:00; then group $28 on spring 1,
 *     group $0F on winter 24.
 * The noises and songs wait frames (WaitForNMI): r->p as transition.c
 * gives it (HM_LOOSE).
 * ───────────────────────────────────────────────────────────────────── */
#define YEAR 0x7F1F18

typedef void HmFn(uint8_t *ram, HmRegs *r);
/* a JSL to a routine that may wait frames: the carry as it is, 8-bit A,
 * 16-bit indexes, D and I of the entry */
static void call_waits(uint8_t *ram, HmRegs *r, uint16_t at, uint8_t bank, HmFn *fn)
{
  uint8_t keep = r->p;
  r->p = (uint8_t)((keep & 0x0C) | 0x20 | (r->c & 1));
  hm_jsl(ram, r, (uint16_t)(at + 3), bank);
  fn(ram, r);
  hm_rtl(ram, r);
  r->p = keep;
}
/* SEP #$20; LDA #song; JSL UNK_Audio26 */
static void song(uint8_t *ram, HmRegs *r, uint8_t s, uint16_t at)
{
  r->a = (uint16_t)((r->a & 0xFF00) | s);
  call_waits(ram, r, at, 0x83, hm_UNK_Audio26);
}
/* SEP #$20; REP #$10; LDA #effect; LDX #7; LDY #frames; JSL UNK_Audio24 */
static void noise(uint8_t *ram, HmRegs *r, uint8_t effect, uint16_t frames, uint16_t at)
{
  r->a = (uint16_t)((r->a & 0xFF00) | effect);
  r->x = 7; r->y = frames;
  call_waits(ram, r, at, 0x83, hm_UNK_Audio24);
}
/* LDA $7F1F6E; AND #~bit; STA $7F1F6E */
static void flag_off(uint8_t *ram, HmRegs *r, uint32_t addr, uint16_t bit)
{
  r->a = (uint16_t)(hm_bus_rd16(ram, addr) & ~bit);
  hm_bus_wr16(ram, addr, r->a);
}
static void house_tiles(uint8_t *ram, HmRegs *r)
{
  uint16_t at = 0xDF74;
  for (uint16_t y = 0x02C0; y <= 0x02E0; y += 0x10)
    for (uint16_t x = 0x0240; x <= 0x0260; x += 0x10, at += 0x10) {
      r->a = (uint16_t)((r->a & 0xFF00) | 0x02);
      r->x = x; r->y = y;
      hm_jsl(ram, r, (uint16_t)(at + 3), 0x82);
      hm_EditTileonMapandsets0181(ram, r);
      hm_rtl(ram, r);
    }
}
/* cow $0937's name: bytes $0C-$0F of its record to $0889-$088F */
static void cow_scene(uint8_t *ram, HmRegs *r)
{
  r->a = hm_bus_rd8(ram, db(r, 0x0937));                     /* LDA #0; XBA; LDA */
  hm_jsl(ram, r, 0xE100, 0x83);
  hm_GetsCowPointer(ram, r);
  hm_rtl(ram, r);
  for (uint16_t i = 0; i < 4; i++) {
    r->y = (uint16_t)(0x0C + i);
    r->a = hm_bus_rd8(ram, hm_dp_long(ram, r, 0x72, r->y));  /* LDA #0; XBA; LDA [$72],Y */
    if (i < 3) hm_bus_wr16(ram, db(r, (uint16_t)(0x0889 + 2 * i)), r->a);
    else hm_bus_wr8(ram, db(r, 0x088F), (uint8_t)r->a);
  }
  spawn(ram, r, 0, 0x20, 0, 0xE13F);
}
/* the wife's scene; returns when it goes on to the children */
static void wife(uint8_t *ram, HmRegs *r)
{
  static const uint32_t hearts[5] = { 0x7F1F1F, 0x7F1F21, 0x7F1F23, 0x7F1F25, 0x7F1F27 };
  int girl = -1;
  for (int i = 0; i < 5 && girl < 0; i++)
    if (flag(ram, r, 0x7F1F66, (uint16_t)(1 << i))) girl = i;
  if (girl < 0) return;
  if (!word_at_least(ram, r, hearts[girl], 0x00C8) && six_sharp(ram, r) &&
      !flag(ram, r, 0x7F1F6E, 0x000C) && !flag(ram, r, 0x7F1F6E, 0x0001)) {
    spawn(ram, r, 0, 0x22, 0, 0xE1F1);
    flag_on(ram, r, 0x7F1F6E, 0x0003);
    flag_on(ram, r, 0x7F1F5E, 0x1000);
    return;
  }
  flag_off(ram, r, 0x7F1F5E, 0x1000);
  if (flag(ram, r, 0x7F1F6E, 0x0002)) { flag_on(ram, r, 0x7F1F5E, 0x1000); return; }
  for (uint16_t i = 0; i < 5; i++)
    if (flag(ram, r, 0x7F1F66, (uint16_t)(1 << i))) {
      spawn(ram, r, 0x13, 0x43, i, (uint16_t)(0xE27C + 0x11 * i));
      return;
    }
}
/* SEP #$20; year, season, day; 6:00:00 */
static bool morning_of(uint8_t *ram, HmRegs *r, uint8_t year, uint8_t season, uint8_t day)
{
  return byte_is(ram, r, YEAR, year) && byte_is(ram, r, SEASON, season) && byte_is(ram, r, DAY, day) &&
         six_sharp(ram, r);
}

void hm_MapEvent_DED3(uint8_t *ram, HmRegs *r)
{
  if (byte_is(ram, r, YEAR, 2) && byte_is(ram, r, SEASON, 1) && byte_is(ram, r, DAY, 0x1E) && !g_hm_no_ending) {
    hm_CODE_83F4D8(ram, r);                                  /* JMP */
    return;
  }
  if (flag(ram, r, 0x7F1F64, 0x0080)) {
    r->a = (uint16_t)((r->a & 0xFF00) | 0x17);
    hm_bus_wr8(ram, db(r, 0x098B), 0x17);
  } else if (flag(ram, r, 0x7F1F64, 0x0040)) {
    r->a = (uint16_t)((r->a & 0xFF00) | 0x16);
    hm_bus_wr8(ram, db(r, 0x098B), 0x16);
  }
  if (flag(ram, r, 0x7F1F6C, 0x2000) && !flag(ram, r, 0x7F1F6C, 0x4000)) {
    hm_bus_wr16(ram, db(r, 0x0985), 0x009C);
    hm_bus_wr16(ram, db(r, 0x0987), 0x0090);
    r->a = 0x0010; r->x = 0; r->y = 0x001F;
    hm_jsl(ram, r, 0xDF46, 0x84);
    hm_SUB_8480F8(ram, r);
    hm_rtl(ram, r);
  }
  if (flag(ram, r, 0x7F1F6E, 0x0080)) {
    flag_off(ram, r, 0x7F1F6E, 0x0080);
    song(ram, r, 0x19, 0xDF64);
    house_tiles(ram, r);
  }
  if (flag(ram, r, 0x7F1F6E, 0x0100)) {
    flag_off(ram, r, 0x7F1F6E, 0x0100);
    song(ram, r, 0x15, 0xE012);
  }
  if (flag(ram, r, 0x7F1F6E, 0x0200)) {
    flag_off(ram, r, 0x7F1F6E, 0x0200);
    noise(ram, r, 0x22, 0x0014, 0xE038);
    noise(ram, r, 0x22, 0x001E, 0xE048);
  }
  if (flag(ram, r, 0x7F1F6E, 0x0020)) {
    noise(ram, r, 0x20, 0x003C, 0xE063);
    flag_off(ram, r, 0x7F1F6E, 0x0020);
  }
  if (flag(ram, r, 0x7F1F6E, 0x0010)) {
    if (flag(ram, r, 0x7F1F5E, 0x4000)) { spawn(ram, r, 0, 0x1F, 2, 0xE0D6); return; }
    if (!flag(ram, r, 0x7F1F5E, 0x2000)) { spawn(ram, r, 0, 0x1F, 0, 0xE09C); return; }
    spawn(ram, r, 0, 0x1F, 1, 0xE0AC);
    r->a = hm_bus_rd16(ram, KID1_AGE);
    r->c = r->a >= 0x003C;
    uint8_t v = r->a == 0x003C ? 5 : 6;
    r->a = (uint16_t)((r->a & 0xFF00) | v);
    hm_bus_wr8(ram, db(r, 0x099F), v);
    return;
  }
  if (flag(ram, r, 0x7F1F6E, 0x0040)) {
    flag_off(ram, r, 0x7F1F6E, 0x0040);
    cow_scene(ram, r);
    return;
  }
  wife(ram, r);
  if (flag(ram, r, 0x7F1F6E, 0x0004) && word_at_least(ram, r, KID1_AGE, 0x003C))
    spawn(ram, r, 0x14, 0x45, 0, 0xE2E5);
  if (flag(ram, r, 0x7F1F6E, 0x0008) && word_at_least(ram, r, KID2_AGE, 0x003C))
    spawn(ram, r, 0x15, 0x45, 3, 0xE308);
  if (flag(ram, r, 0x7F1F60, 0x0020)) { spawn(ram, r, 0, 0x0A, 3, 0xE33B); return; }
  if (!flag(ram, r, 0x7F1F68, 0x0001)) { spawn(ram, r, 0, 0x0A, 0, 0xE32B); return; }
  if (morning_of(ram, r, 1, 1, 0x01)) { spawn(ram, r, 0, 0x0A, 4, 0xE37B); return; }
  if (morning_of(ram, r, 2, 1, 0x1D)) { spawn(ram, r, 0, 0x0A, 5, 0xE3BB); return; }
  if (!byte_of(ram, r, SEASON) && byte_is(ram, r, DAY, 0x01)) { spawn(ram, r, 0, 0x28, 0, 0xE3DD); return; }
  if (byte_is(ram, r, SEASON, 3) && byte_is(ram, r, DAY, 0x18)) spawn(ram, r, 0, 0x0F, 0, 0xE401);
}

/* ─────────────────────────────────────────────────────────────────────────
 * RunsFunctionbyIndex ($83:CA98, JSL, RTL): the routine of map $22 from
 * UNK_TableFunctionSelect ($83:CAA9), with A = X = $22 x 2. The table has
 * $60 words (the maps go to $56; the rest, and $3B, $3E-$4A, $4C-$5F, are
 * the empty E74C).
 * ───────────────────────────────────────────────────────────────────────── */
static void (*const MAP_EVENTS[0x60])(uint8_t *, HmRegs *) = {
  hm_MapEvent_CB69, hm_MapEvent_CB69, hm_MapEvent_CB69, hm_MapEvent_CB69,     /* $00 the farm */
  hm_MapEvent_D2E0, hm_MapEvent_D2E0, hm_MapEvent_D2E0, hm_MapEvent_D2E0,     /* $04 the town */
  hm_MapEvent_E5DF, hm_MapEvent_E5F1, hm_MapEvent_E603, hm_MapEvent_E6B2,
  hm_MapEvent_E407, hm_MapEvent_E407, hm_MapEvent_E407, hm_MapEvent_E407,     /* $0C the crossroads */
  hm_MapEvent_DC4A, hm_MapEvent_DC4A, hm_MapEvent_DC4A, hm_MapEvent_DC4A,
  hm_MapEvent_E661, hm_MapEvent_DED3, hm_MapEvent_DED3, hm_MapEvent_DED3,     /* $15 the house */
  hm_MapEvent_D4D7, hm_MapEvent_D4D7, hm_MapEvent_D5E7, hm_MapEvent_D613,
  hm_MapEvent_D74E, hm_MapEvent_D818, hm_MapEvent_DB8F, hm_MapEvent_DC1E,
  hm_MapEvent_D955, hm_MapEvent_DA1F, hm_MapEvent_D844, hm_MapEvent_D929,
  hm_MapEvent_DAED, hm_MapEvent_DA4B, hm_MapEvent_E6C4, hm_MapEvent_DE9B,
  hm_MapEvent_E5B4, hm_MapEvent_E4CB, hm_MapEvent_DE64, hm_MapEvent_DD95,
  hm_MapEvent_E5A2, hm_MapEvent_E5CD, hm_MapEvent_E5CD, hm_MapEvent_E5CD,
  hm_MapEvent_E5CD, hm_MapEvent_E586, hm_MapEvent_E586, hm_MapEvent_E586,
  hm_MapEvent_E586, hm_MapEvent_E586, hm_MapEvent_E586, hm_MapEvent_E586,
  hm_MapEvent_E586, hm_MapEvent_E632, hm_MapEvent_E690, hm_MapEvent_E74C,
  hm_MapEvent_E76D, hm_MapEvent_EBA6, hm_MapEvent_E74C, hm_MapEvent_E74C,
  hm_MapEvent_E74C, hm_MapEvent_E74C, hm_MapEvent_E74C, hm_MapEvent_E74C,
  hm_MapEvent_E74C, hm_MapEvent_E74C, hm_MapEvent_E74C, hm_MapEvent_E74C,
  hm_MapEvent_E74C, hm_MapEvent_E74C, hm_MapEvent_E74C, hm_MapEvent_EBA6,
  hm_MapEvent_E74C, hm_MapEvent_E74C, hm_MapEvent_E74C, hm_MapEvent_E74C,
  hm_MapEvent_E74C, hm_MapEvent_E74C, hm_MapEvent_E74C, hm_MapEvent_E74C,
  hm_MapEvent_E74C, hm_MapEvent_E74C, hm_MapEvent_E74C, hm_MapEvent_E74C,
  hm_MapEvent_E74C, hm_MapEvent_E74C, hm_MapEvent_E74C, hm_MapEvent_E74C,
};

void hm_RunsFunctionbyIndex(uint8_t *ram, HmRegs *r)
{
  uint8_t map = *hm_dp(ram, r, 0x22);                        /* SEP #$20; LDA #0; XBA; LDA $22 */
  r->a = (uint16_t)(map << 1);                               /* REP #$20; ASL; TAX */
  r->c = 0;
  r->x = r->a;
  if (map < 0x60) {
    hm_push8(ram, r, 0xCA); hm_push8(ram, r, 0xA7);          /* JSR ($CAA9,X) */
    MAP_EVENTS[map](ram, r);
    hm_pull8(ram, r); hm_pull8(ram, r);
  }
}
