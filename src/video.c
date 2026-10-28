/*
 * video.c — the routines that program the picture chip (PPU) and the DMA
 * unit. They are checked on the real machine (HM_IO, see hm_io.c): their
 * writes go to the emulated chips, and the chips' state is compared.
 *
 * The prepared DMAs: a routine that wants something copied to video memory
 * sets up a DMA channel ($43x0-$43x6) and notes it for later:
 *   $28        one bit per channel that waits to be started;
 *   $2A + ch   for a copy to VRAM, the VMAIN value (how the address moves);
 *   $32 + 2ch  the destination: the VRAM word address, or the CGRAM colour.
 * StartProgramedDMA starts them all (during the NMI, when video memory can
 * be written).
 */
#include "hm_decomp.h"

#define DMA_WAITING     0x28   /* DP: the channels to start */
#define DMA_VMAIN       0x2A   /* DP: + channel */
#define DMA_DESTINATION 0x32   /* DP: + 2 channel */
#define BBAD_VRAM       0x18   /* $2118: a copy to VRAM */

static uint32_t db(const HmRegs *r, uint16_t a) { return (uint32_t)r->db << 16 | a; }
static void set_a8(HmRegs *r, uint8_t v) { r->a = (uint16_t)((r->a & 0xFF00) | v); }

/* ─────────────────────────────────────────────────────────────────────────
 * StartProgramedDMA ($80:8AF0, JSL, RTL): for each channel 0-7 waiting in
 * $28: when its B address ($43x1) is $18 (VRAM) VMAIN = $2A + ch and the
 * VRAM address = $32 + 2ch (16 bits); otherwise the CGRAM address = the
 * low byte of $32 + 2ch. Then the channel is started ($420B = its bit: the
 * copy happens there). At the end $28 = 0 and $420B = 0.
 * Leftovers: X = 8, C = 1 (CPX #8); A's low byte = the last channel's bit
 * when channel 7 was started, else 0; its high byte = the high byte of the
 * last VRAM address used (16-bit load), or as it was. The stack keeps the
 * last channel PHX'd.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_StartProgramedDMA(uint8_t *ram, HmRegs *r)
{
  for (uint8_t ch = 0; ch < 8; ch++) {
    /* $28 is read again for each channel: a copy into WRAM ($2180) can
     * change it */
    uint8_t bit = (uint8_t)(1 << ch), waiting = *hm_dp(ram, r, DMA_WAITING);
    set_a8(r, (uint8_t)(bit & waiting));
    if (!(bit & waiting)) continue;
    hm_push8(ram, r, ch);                                          /* PHX */
    uint8_t bbad = hm_bus_rd8(ram, db(r, (uint16_t)(0x4301 + 16 * ch)));
    hm_pull8(ram, r);                                              /* PLX */
    if (bbad == BBAD_VRAM) {
      hm_bus_wr8(ram, db(r, 0x2115), *hm_dp(ram, r, DMA_VMAIN + ch));
      r->a = hm_dp_r16(ram, r, DMA_DESTINATION + 2u * ch);
      hm_bus_wr16(ram, db(r, 0x2116), r->a);
    } else {
      set_a8(r, *hm_dp(ram, r, DMA_DESTINATION + 2u * ch));
      hm_bus_wr8(ram, db(r, 0x2121), (uint8_t)r->a);
    }
    set_a8(r, bit);
    hm_bus_wr8(ram, db(r, 0x420B), bit);                           /* the copy */
  }
  r->x = 8;
  r->c = 1;
  *hm_dp(ram, r, DMA_WAITING) = 0;
  hm_bus_wr8(ram, db(r, 0x420B), 0);
}

/* ─────────────────────────────────────────────────────────────────────────
 * The screen's brightness and blanking. $24 is the game's copy of INIDISP
 * ($2100: bit 7 = forced blank, bits 0-3 = brightness); each routine
 * changes the copy and writes it to the chip.
 *   ForceBlank ($80:8E0F, JSL, RTL): sets bit 7.
 *   ResetForceBlank ($80:8E1E): keeps only the brightness.
 *   SetsBrightness ($80:8E2D): brightness = A & $0F, keeping the blank
 *     bit; brightness 0 means blank instead ($80; $7E keeps the value).
 *   InitialzieScreenStatusVar ($80:8C4C): $24 = 0, then ForceBlank.
 * Leftovers: A's low byte = the value written (high byte untouched); the
 * flags as they were (PHP / PLP); the stack keeps the PHP'd byte.
 * ───────────────────────────────────────────────────────────────────────── */
#define INIDISP_COPY 0x24   /* DP */
#define INIDISP      0x002100

static void php(uint8_t *ram, HmRegs *r) { hm_push8(ram, r, r->p); }
static void plp(uint8_t *ram, HmRegs *r) { r->c = hm_pull8(ram, r) & 1; }
static void set_inidisp(uint8_t *ram, HmRegs *r, uint8_t v)
{
  set_a8(r, v);
  *hm_dp(ram, r, INIDISP_COPY) = v;
  hm_bus_wr8(ram, INIDISP, v);
}

void hm_ForceBlank(uint8_t *ram, HmRegs *r)
{
  php(ram, r);
  set_inidisp(ram, r, (uint8_t)(*hm_dp(ram, r, INIDISP_COPY) | 0x80));
  plp(ram, r);
}
void hm_ResetForceBlank(uint8_t *ram, HmRegs *r)
{
  php(ram, r);
  set_inidisp(ram, r, (uint8_t)(*hm_dp(ram, r, INIDISP_COPY) & 0x0F));
  plp(ram, r);
}
void hm_SetsBrightness(uint8_t *ram, HmRegs *r)
{
  php(ram, r);
  uint8_t bright = (uint8_t)(r->a & 0x0F);
  if (!bright) bright = 0x80;
  hm_bus_wr8(ram, 0x80007E, bright);                       /* STA.L $80:007E */
  set_inidisp(ram, r, (uint8_t)((*hm_dp(ram, r, INIDISP_COPY) & 0x80) | *hm_dp(ram, r, 0x7E)));
  plp(ram, r);
}
void hm_InitialzieScreenStatusVar(uint8_t *ram, HmRegs *r)
{
  php(ram, r);
  *hm_dp(ram, r, INIDISP_COPY) = 0;
  plp(ram, r);
  hm_ForceBlank(ram, r);                                   /* JMP */
}

/* ─────────────────────────────────────────────────────────────────────────
 * ManageGraphicPresets ($80:8C59, JSL, RTL): A = one of the 11 "graphic
 * presets" ($19B6). Each PPU register of the table below gets the preset's
 * value from its table ($80:8B44-$80:8C4B), and the value is also kept in
 * WRAM ($19B7-$19E1). On top:
 *   $19EA = (OBSEL & 7) << 13 (the sprites' graphics in VRAM, in words);
 *   $19E3/$19E5/$19E7/$19E9 = the BGnSC values without the size bits;
 *   $19E2/$19E4/$19E6/$19E8 (bytes) and the words $19C0-$19CE = 0;
 *   in forced blank ($24 bit 7) the eight scroll registers get 0;
 *   M7SEL = 0 and COLDATA = $E0 always.
 * Leftovers: A = the SETINI value (high byte 0), X = the preset; the flags
 * as they were (PHP / PLP); the stack keeps the PHP'd byte and X.
 * ───────────────────────────────────────────────────────────────────────── */
typedef struct Preset { uint16_t table, copy; uint8_t reg; } Preset;

static void preset(uint8_t *ram, HmRegs *r, const Preset *p)
{
  uint8_t v = hm_bus_rd8(ram, 0x800000u | (uint16_t)(p->table + r->x));
  set_a8(r, v);
  hm_bus_wr8(ram, 0x800000u | p->copy, v);
  hm_bus_wr8(ram, 0x002100u | p->reg, v);
}

void hm_ManageGraphicPresets(uint8_t *ram, HmRegs *r)
{
  static const Preset bg_sc[] = {
    { 0x8B5A, 0x19BA, 0x07 }, { 0x8B65, 0x19BB, 0x08 }, { 0x8B70, 0x19BC, 0x09 }, { 0x8B7B, 0x19BD, 0x0A } };
  static const Preset nba[] = { { 0x8B86, 0x19BE, 0x0B }, { 0x8B91, 0x19BF, 0x0C } };
  static const Preset rest[] = {
    { 0x8BE9, 0x19D1, 0x23 }, { 0x8BF4, 0x19D2, 0x24 }, { 0x8BFF, 0x19D3, 0x25 },   /* windows */
    { 0x8C36, 0x19D8, 0x2A }, { 0x8C41, 0x19D9, 0x2B },
    { 0x8C0A, 0x19D4, 0x26 }, { 0x8C15, 0x19D5, 0x27 }, { 0x8C20, 0x19D6, 0x28 }, { 0x8C2B, 0x19D7, 0x29 },
    { 0x8B9C, 0x19DA, 0x2C }, { 0x8BA7, 0x19DB, 0x2D }, { 0x8BB2, 0x19DC, 0x2E }, { 0x8BBD, 0x19DD, 0x2F },
    { 0x8BC8, 0x19DE, 0x30 }, { 0x8BD3, 0x19DF, 0x31 } };

  php(ram, r);
  uint8_t which = (uint8_t)r->a;
  hm_bus_wr8(ram, 0x8019B6, which);
  r->x = which;                                            /* TAX (8-bit) */
  static const Preset obsel = { 0x8B44, 0x19B7, 0x01 };
  preset(ram, r, &obsel);
  r->a = (uint16_t)((r->a & 0x0007) << 13);                /* XBA; AND #$0700; ASL x5 */
  hm_bus_wr16(ram, 0x8019EA, r->a);
  static const Preset bgmode = { 0x8B4F, 0x19B8, 0x05 };
  preset(ram, r, &bgmode);
  for (uint16_t a = 0x19E2; a <= 0x19E8; a += 2) hm_bus_wr8(ram, db(r, a), 0);
  for (unsigned i = 0; i < 4; i++) {
    preset(ram, r, &bg_sc[i]);
    set_a8(r, (uint8_t)(r->a & 0xFC));                     /* without the size bits */
    hm_bus_wr8(ram, 0x8019E3u + 2u * i, (uint8_t)r->a);
  }
  for (unsigned i = 0; i < 2; i++) preset(ram, r, &nba[i]);
  r->a = 0;
  for (uint16_t a = 0x19C0; a <= 0x19CE; a += 2) hm_bus_wr16(ram, 0x800000u | a, 0);
  if (*hm_dp(ram, r, INIDISP_COPY) & 0x80) {               /* forced blank: scroll = 0 */
    hm_push8(ram, r, (uint8_t)r->x);                       /* PHX */
    for (uint8_t x = 0; x < 8; x += 2) {
      hm_bus_wr8(ram, 0x00210Du + x, 0); hm_bus_wr8(ram, 0x00210Du + x, 0);
      hm_bus_wr8(ram, 0x00210Eu + x, 0); hm_bus_wr8(ram, 0x00210Eu + x, 0);
    }
    hm_pull8(ram, r);                                      /* PLX */
  }
  hm_bus_wr8(ram, 0x8019D0, 0);                            /* M7SEL */
  hm_bus_wr8(ram, 0x00211A, 0);
  for (unsigned i = 0; i < sizeof rest / sizeof rest[0]; i++) preset(ram, r, &rest[i]);
  set_a8(r, 0xE0);                                         /* COLDATA */
  hm_bus_wr8(ram, 0x8019E0, 0xE0);
  hm_bus_wr8(ram, 0x002132, 0xE0);
  static const Preset setini = { 0x8BDE, 0x19E1, 0x33 };
  preset(ram, r, &setini);
  plp(ram, r);
}

/* ─────────────────────────────────────────────────────────────────────────
 * Clearing video memory, each with one DMA on channel 0 from a word of 0
 * in the ROM ($80:8424, the source fixed). All but UNUSED1 first force
 * blank (INIDISP = $80) and switch the interrupts off (NMITIMEN = 0).
 *   ZeroesVRAM ($80:8846, JSL, RTL): VMAIN $80, address 0, $10000 bytes
 *     (size 0): all of VRAM.
 *   ZeroesOAM ($80:8887): OAM address 0, $43F bytes, twice the OAM.
 *   ZeroesCGRAM ($80:8980): CGRAM address 0, $3FF bytes, twice the CGRAM
 *     (less one byte: the last colour keeps its high byte).
 *   ZeroesPartialVRAM ($80:89BC): from the VRAM address in A, $FFF bytes.
 *   UNUSED1 ($80:88C3; nothing calls it): the sprites off screen: OAM from
 *     the word $00F0 at $80:8426 ($200 bytes, 8-bit mode) and the high
 *     table from 0 ($20 bytes); then on channel 4 the game's WRAM copy of
 *     the OAM ($7E:A000, $AF bytes) and of the high table ($7E:A200).
 * Leftovers: A = the last 16-bit load with the low byte of the last MDMAEN
 * value; UNUSED1 also X = $20, C = 0; the rest untouched.
 * ───────────────────────────────────────────────────────────────────────── */
static void wr16(uint8_t *ram, const HmRegs *r, uint16_t a, uint16_t v) { hm_bus_wr16(ram, db(r, a), v); }
static void wr8(uint8_t *ram, const HmRegs *r, uint16_t a, uint8_t v) { hm_bus_wr8(ram, db(r, a), v); }

/* channel `ch` (its registers at $43n0): mode, B address, source, size; then
 * MDMAEN. A as the original's loads leave it (the size, then the bit). */
static void dma_go(uint8_t *ram, HmRegs *r, unsigned ch, uint8_t mode, uint8_t bbad,
                   uint32_t src, uint16_t size)
{
  uint16_t at = (uint16_t)(0x4300 + 16 * ch);
  wr8(ram, r, at, mode);
  wr8(ram, r, (uint16_t)(at + 1), bbad);
  wr16(ram, r, (uint16_t)(at + 2), (uint16_t)src);
  wr8(ram, r, (uint16_t)(at + 4), (uint8_t)(src >> 16));
  wr16(ram, r, (uint16_t)(at + 5), size);
  r->a = (uint16_t)((size & 0xFF00) | (1u << ch));
  wr8(ram, r, 0x420B, (uint8_t)(1u << ch));
}
static void blank_no_interrupts(uint8_t *ram, HmRegs *r)
{
  wr8(ram, r, 0x2100, 0x80);
  wr8(ram, r, 0x4200, 0);
}

#define ZERO_WORD 0x808424   /* ROM: $0000 */
#define OFF_SCREEN 0x808426  /* ROM: $00F0 */

void hm_ZeroesVRAM(uint8_t *ram, HmRegs *r)
{
  blank_no_interrupts(ram, r);
  wr8(ram, r, 0x2115, 0x80);
  wr16(ram, r, 0x2116, 0);
  dma_go(ram, r, 0, 0x09, 0x18, ZERO_WORD, 0x0000);
}
void hm_ZeroesOAM(uint8_t *ram, HmRegs *r)
{
  blank_no_interrupts(ram, r);
  wr16(ram, r, 0x2102, 0);
  dma_go(ram, r, 0, 0x09, 0x04, ZERO_WORD, 0x043F);
}
void hm_ZeroesCGRAM(uint8_t *ram, HmRegs *r)
{
  blank_no_interrupts(ram, r);
  wr16(ram, r, 0x2121, 0);
  dma_go(ram, r, 0, 0x09, 0x22, ZERO_WORD, 0x03FF);
}
void hm_ZeroesPartialVRAM(uint8_t *ram, HmRegs *r)
{
  wr16(ram, r, 0x2116, r->a);
  blank_no_interrupts(ram, r);
  wr8(ram, r, 0x2115, 0x80);
  dma_go(ram, r, 0, 0x09, 0x18, ZERO_WORD, 0x0FFF);
}
void hm_UNUSED1(uint8_t *ram, HmRegs *r)
{
  wr16(ram, r, 0x2102, 0);
  dma_go(ram, r, 0, 0x08, 0x04, OFF_SCREEN, 0x0200);
  wr16(ram, r, 0x2102, 0x0100);
  dma_go(ram, r, 0, 0x08, 0x04, ZERO_WORD, 0x0020);
  wr16(ram, r, 0x2102, 0);
  /* channel 4, written register by register (8-bit) */
  r->x = 0;
  wr16(ram, r, 0x2102, 0);
  wr8(ram, r, 0x4340, 0); wr8(ram, r, 0x4341, 0x04);
  wr8(ram, r, 0x4342, 0x00); wr8(ram, r, 0x4343, 0xA0); wr8(ram, r, 0x4344, 0x7E);
  r->x = hm_dp_r16(ram, r, 0xAF);
  wr16(ram, r, 0x4345, r->x);
  wr8(ram, r, 0x420B, 0x10);
  r->x = 0x0100;
  wr16(ram, r, 0x2102, 0x0100);
  wr8(ram, r, 0x4340, 0); wr8(ram, r, 0x4341, 0x04);
  wr8(ram, r, 0x4342, 0x00); wr8(ram, r, 0x4343, 0xA2); wr8(ram, r, 0x4344, 0x7E);
  r->c = 0;                                                /* CLC; ADC #2 */
  r->x = 0x0020;
  wr16(ram, r, 0x4345, 0x0020);
  r->a = 0x0010;
  wr8(ram, r, 0x420B, 0x10);
}
