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
