/*
 * joypad.c — reading the controllers, once a frame.
 *
 * ReadJoypad ($80:872B, JSR, RTS; the game runs the copy in bank $00):
 *   - waits until the console has finished its automatic reading of the
 *     pads ($4212 bit 0);
 *   - keeps last frame's buttons ($0126, $0132) and reads the new ones from
 *     $4218 (pad 1) and $421A (pad 2) into $0124 and $0130;
 *   - works out what is useful from them, for pad 1:
 *       $0128  buttons pressed this frame (not held last frame),
 *       $012A  buttons let go this frame,
 *       $012C  "autorepeat": the new presses, plus every held button again
 *              while it is kept down (used by the menus, see below);
 *     and for pad 2 the same ($0134, $0136, $0138; nothing reads them).
 *   - $23 counts the frames some button has been held (0 when none). When
 *     it reaches 30 (half a second) the held buttons count as pressed again
 *     ($012C) and it goes back to 25: from then on every 5 frames, so a
 *     cursor held down keeps moving.
 * If $00 is not 0 another branch runs (it never does): it reads only the
 * low bytes, pad 1 into $012E and pad 2 into $0130, and leaves $0124 as it
 * was.
 * Leftovers: A = $0124's high byte : the counter $23; X, Y and the flags
 * (with the 8 / 16-bit widths) back as they were (PHP / PLP).
 *
 * Quirks: the "let go" word of pad 2 ($0136) is worked out from pad 1's
 * register, a typo that does not matter since pad 2 is never used. $012E
 * and $013A are ORed into the buttons read and then cleared: they are
 * always 0.
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */

/* `.W` addresses go through the data bank (DB): $4212 and $4218 are the
 * console's registers only when DB is a bank that has them. */
static uint32_t abs_(const HmRegs *r, uint16_t a) { return (uint32_t)r->db << 16 | a; }
static uint8_t rd8(uint8_t *ram, const HmRegs *r, uint16_t a) { return hm_bus_rd8(ram, abs_(r, a)); }
static uint16_t rd16(uint8_t *ram, const HmRegs *r, uint16_t a) { return hm_bus_rd16(ram, abs_(r, a)); }
static void wr8(uint8_t *ram, const HmRegs *r, uint16_t a, uint8_t v) { hm_bus_wr8(ram, abs_(r, a), v); }
static void wr16(uint8_t *ram, const HmRegs *r, uint16_t a, uint16_t v) { hm_bus_wr16(ram, abs_(r, a), v); }

enum { HVBJOY = 0x4212, JOY1 = 0x4218, JOY2 = 0x421A,
       PAD1 = 0x0124, PAD1_LAST = 0x0126, PAD1_NEW = 0x0128, PAD1_UP = 0x012A,
       PAD1_REPEAT = 0x012C, PAD1_EXTRA = 0x012E,
       PAD2 = 0x0130, PAD2_LAST = 0x0132, PAD2_NEW = 0x0134, PAD2_UP = 0x0136,
       PAD2_REPEAT = 0x0138, PAD2_EXTRA = 0x013A,
       HELD = 0x23 };

void hm_ReadJoypad(uint8_t *ram, HmRegs *r)
{
  hm_push8(ram, r, r->p);                                      /* PHP */
  while (rd8(ram, r, HVBJOY) & 0x01) { }                      /* still reading */

  wr16(ram, r, PAD1_LAST, rd16(ram, r, PAD1));
  wr16(ram, r, PAD2_LAST, rd16(ram, r, PAD2));
  if (*hm_dp(ram, r, 0x00)) {                                  /* never */
    wr8(ram, r, PAD1_EXTRA, rd8(ram, r, JOY1));
    wr8(ram, r, PAD2, rd8(ram, r, JOY2));
  } else {
    wr16(ram, r, PAD1, rd16(ram, r, JOY1) | rd16(ram, r, PAD1_EXTRA));
    wr16(ram, r, PAD2, rd16(ram, r, JOY2) | rd16(ram, r, PAD2_EXTRA));
    wr16(ram, r, PAD1_EXTRA, 0);
    wr16(ram, r, PAD2_EXTRA, 0);
  }

  uint16_t last1 = rd16(ram, r, PAD1_LAST), last2 = rd16(ram, r, PAD2_LAST);
  uint16_t pressed1 = (rd16(ram, r, JOY1) ^ last1) & rd16(ram, r, PAD1);
  wr16(ram, r, PAD1_NEW, pressed1);
  wr16(ram, r, PAD1_REPEAT, pressed1);
  wr16(ram, r, PAD1_UP, (rd16(ram, r, JOY1) ^ last1) & last1);
  uint16_t pressed2 = (rd16(ram, r, JOY2) ^ last2) & rd16(ram, r, PAD2);
  wr16(ram, r, PAD2_NEW, pressed2);
  wr16(ram, r, PAD2_REPEAT, pressed2);
  wr16(ram, r, PAD2_UP, (rd16(ram, r, JOY1) ^ last2) & last2);   /* pad 1's register */

  /* the hold counter (a word: INC / STZ run with 16-bit A) */
  uint16_t buttons = rd16(ram, r, PAD1);
  hm_dp_w16(ram, r, HELD, buttons ? (uint16_t)(hm_dp_r16(ram, r, HELD) + 1) : 0);
  if (*hm_dp(ram, r, HELD) == 30) {
    wr16(ram, r, PAD1_REPEAT, buttons);                        /* held: again */
    *hm_dp(ram, r, HELD) = 25;
  }
  r->a = (uint16_t)((buttons & 0xFF00) | *hm_dp(ram, r, HELD));
  hm_pull8(ram, r);                                            /* PLP */
}
