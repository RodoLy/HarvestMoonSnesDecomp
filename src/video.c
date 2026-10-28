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

/* ─────────────────────────────────────────────────────────────────────────
 * StartLastPreparedDMA ($80:8AB2, JSL, RTL): StartProgramedDMA for one
 * channel only, the one in $27 (the last one prepared), right away; then
 * $28 = 0, forgetting any other waiting channel.
 * Leftovers: X = the channel, C = 0 (the last LSR); A's low byte = the
 * channel's bit, its high byte the VRAM address's (VRAM) or as it was;
 * the stack keeps the channel PHA'd.
 * ───────────────────────────────────────────────────────────────────────── */
#define DMA_CHANNEL 0x27   /* DP */
#define CHANNEL_BITS 0x808B3C

void hm_StartLastPreparedDMA(uint8_t *ram, HmRegs *r)
{
  uint8_t ch = *hm_dp(ram, r, DMA_CHANNEL);
  set_a8(r, ch);
  hm_push8(ram, r, ch);                                            /* PHA */
  uint8_t bbad = hm_bus_rd8(ram, db(r, (uint16_t)(0x4301 + (uint8_t)(ch << 4))));
  hm_pull8(ram, r);                                                /* PLX */
  if (bbad == BBAD_VRAM) {
    wr8(ram, r, 0x2115, *hm_dp(ram, r, (unsigned)(DMA_VMAIN + ch)));
    r->a = hm_dp_r16(ram, r, (unsigned)(DMA_DESTINATION + 2u * ch));
    wr16(ram, r, 0x2116, r->a);
  } else {
    set_a8(r, *hm_dp(ram, r, (uint8_t)(DMA_DESTINATION + (uint8_t)(2 * ch))));
    wr8(ram, r, 0x2121, (uint8_t)r->a);
  }
  r->x = ch;
  r->c = 0;
  uint8_t bit = hm_bus_rd8(ram, CHANNEL_BITS + ch);
  set_a8(r, bit);
  wr8(ram, r, 0x420B, bit);
  *hm_dp(ram, r, DMA_WAITING) = 0;
  wr8(ram, r, 0x420B, 0);
}

/* ─────────────────────────────────────────────────────────────────────────
 * LoadsFromVRAMwithOffset ($80:AA38, JSR, RTS; despite its name it writes
 * VRAM): Y bytes from $7E:2000 + $7E to the VRAM address X (VMAIN $80),
 * prepared on channel 0 with AddProgrammedDMA and started at once
 * (StartLastPreparedDMA).
 * Leftovers: those of StartLastPreparedDMA, X and Y as they were (PHX /
 * PHY / PLY / PLX, 16-bit).
 * ───────────────────────────────────────────────────────────────────────── */
static void jsl(uint8_t *ram, HmRegs *r, uint16_t at, uint8_t bank, void (*fn)(uint8_t *, HmRegs *))
{
  hm_jsl(ram, r, (uint16_t)(at + 3), bank);
  fn(ram, r);
  hm_rtl(ram, r);
}
static void push16(uint8_t *ram, HmRegs *r, uint16_t v) { hm_push8(ram, r, (uint8_t)(v >> 8)); hm_push8(ram, r, (uint8_t)v); }
static uint16_t pull16(uint8_t *ram, HmRegs *r) { uint8_t lo = hm_pull8(ram, r); return (uint16_t)(lo | hm_pull8(ram, r) << 8); }

void hm_LoadsFromVRAMwithOffset(uint8_t *ram, HmRegs *r)
{
  uint16_t x = r->x, y = r->y;
  push16(ram, r, x);
  push16(ram, r, y);
  *hm_dp(ram, r, DMA_CHANNEL) = 0;
  *hm_dp(ram, r, 0x29) = BBAD_VRAM;                        /* the B address */
  uint32_t sum = 0x2000u + hm_dp_r16(ram, r, 0x7E);
  r->c = sum > 0xFFFF;
  hm_dp_w16(ram, r, 0x72, (uint16_t)sum);
  *hm_dp(ram, r, 0x74) = 0x7E;
  r->a = 0x0080;                                           /* VMAIN */
  jsl(ram, r, 0xAA5B, 0x80, hm_AddProgrammedDMA);
  jsl(ram, r, 0xAA5F, 0x80, hm_StartLastPreparedDMA);
  r->y = pull16(ram, r);
  r->x = pull16(ram, r);
}

/* ─────────────────────────────────────────────────────────────────────────
 * CopiesWRAMtoOBJVGRAM ($85:8E57, JSL, RTL): the sprite pieces the loader
 * (CODE_858CB2) queued at $7E:A420 (6 bytes each: source, bank, VRAM
 * address; $FFFF ends the list) go to VRAM, each as two DMAs of $40 bytes
 * on channel 0: the top half of the 16 x 16 piece from the source to the
 * address, the bottom half from source + $200 to address + $100.
 * Leftovers: A = $FFFF, C = 1 (CMP), X = $BF = the end of the list.
 * ───────────────────────────────────────────────────────────────────────── */
#define LOAD_COPIES 0x7EA420

void hm_CopiesWRAMtoOBJVGRAM(uint8_t *ram, HmRegs *r)
{
  wr8(ram, r, 0x2115, 0x80);
  wr8(ram, r, 0x4300, 0x01);
  wr8(ram, r, 0x4301, BBAD_VRAM);
  hm_dp_w16(ram, r, 0xBF, 0);
  for (;;) {
    uint16_t x = hm_dp_r16(ram, r, 0xBF);
    r->x = x;
    r->a = hm_bus_rd16(ram, LOAD_COPIES + x);
    if (r->a == 0xFFFF) break;
    for (int half = 0; half < 2; half++) {
      uint32_t vram = hm_bus_rd16(ram, LOAD_COPIES + 4u + x) + 0x100u * half;
      uint32_t src = hm_bus_rd16(ram, LOAD_COPIES + x) + 0x200u * half;
      wr16(ram, r, 0x2116, (uint16_t)vram);
      wr16(ram, r, 0x4302, (uint16_t)src);
      wr16(ram, r, 0x4305, 0x0040);
      wr8(ram, r, 0x4304, hm_bus_rd8(ram, LOAD_COPIES + 2u + x));
      wr8(ram, r, 0x420B, 0x01);
      r->x = hm_dp_r16(ram, r, 0xBF);
    }
    hm_dp_w16(ram, r, 0xBF, (uint16_t)(x + 6));
  }
  r->c = 1;
}

/* ─────────────────────────────────────────────────────────────────────────
 * UpdateGraphics ($80:86B1, JSR, RTS; from the NMI). Unless the frame's
 * work is done already ($00 != 0): the prepared DMAs (StartProgramedDMA),
 * the sprite table (channel 4, if $9A says so), the sprite pieces
 * (CopiesWRAMtoOBJVGRAM). Then always: the pads (ReadJoypad), one random
 * number (GetRNG), the scroll of BG1-BG3 from $013C-$0147, and $00 = 1.
 * Quirk: after the sprite pieces the A register is 16-bit (left so by
 * CopiesWRAMtoOBJVGRAM), so the last "STZ $9A; STZ $420B" also clear $9B
 * and HDMAEN.
 * Leftovers: A = $0001 (GetRNG's high byte 0), X and Y those of the calls,
 * the flags as they were (PHP / PLP). (StartProgramedDMA makes X and Y
 * 8-bit and CopiesWRAMtoOBJVGRAM 16-bit again: the console would clear
 * Y's high byte there, the recompiled game, our reference, keeps it.)
 * ───────────────────────────────────────────────────────────────────────── */
#define NMI_STATUS  0x00   /* DP */
#define OAM_CHANNEL 0x9A   /* DP: the MDMAEN bit for the sprite table */

static void jsr(uint8_t *ram, HmRegs *r, uint16_t at, void (*fn)(uint8_t *, HmRegs *))
{
  uint16_t ret = (uint16_t)(at + 2);
  hm_push8(ram, r, (uint8_t)(ret >> 8));
  hm_push8(ram, r, (uint8_t)ret);
  fn(ram, r);
  hm_pull8(ram, r); hm_pull8(ram, r);
}

void hm_UpdateGraphics(uint8_t *ram, HmRegs *r)
{
  php(ram, r);
  set_a8(r, *hm_dp(ram, r, NMI_STATUS));
  if (!*hm_dp(ram, r, NMI_STATUS)) {
    jsl(ram, r, 0x86B8, 0x80, hm_StartProgramedDMA);
    set_a8(r, *hm_dp(ram, r, OAM_CHANNEL));
    wr8(ram, r, 0x420B, (uint8_t)r->a);
    *hm_dp(ram, r, OAM_CHANNEL) = 0;
    wr8(ram, r, 0x420B, 0);
    jsl(ram, r, 0x86C8, 0x85, hm_CopiesWRAMtoOBJVGRAM);
    /* CopiesWRAMtoOBJVGRAM returns with a 16-bit A, so these two STZ are
     * 16-bit: $9B goes too, and $420C (HDMAEN): HDMA is switched off */
    hm_dp_w16(ram, r, OAM_CHANNEL, 0);
    wr16(ram, r, 0x420B, 0);
  }
  jsr(ram, r, 0x86D1, hm_ReadJoypad);
  jsl(ram, r, 0x86D4, 0x83, hm_GetRNG);
  for (uint16_t i = 0; i < 12; i++) {                      /* BG1HOFS .. BG3VOFS, low then high */
    set_a8(r, hm_bus_rd8(ram, db(r, (uint16_t)(0x013C + i))));
    wr8(ram, r, (uint16_t)(0x210D + i / 2), (uint8_t)r->a);
  }
  set_a8(r, 0x01);
  *hm_dp(ram, r, NMI_STATUS) = 1;
  plp(ram, r);
}

/* ─────────────────────────────────────────────────────────────────────────
 * LoadDialogueBox ($83:92BB, JSL, RTL): the text box's graphics. The
 * tilemap at $A5:CB05 is decompressed to $7E:2000 (DecompressTileMap) and
 * copied to VRAM $7800 ($400 bytes); then the letters' graphics at
 * $9C:BBA3 to VRAM $5000 ($1000 bytes). Both with AddProgrammedDMA on
 * channel 0 and StartLastPreparedDMA.
 * Leftovers: those of the last StartLastPreparedDMA; X = $5000, Y = $1000
 * (AddProgrammedDMA keeps them).
 * ───────────────────────────────────────────────────────────────────────── */
static void vram_copy(uint8_t *ram, HmRegs *r, uint32_t src, uint16_t vram, uint16_t size,
                      uint16_t at_add, uint16_t at_start)
{
  *hm_dp(ram, r, DMA_CHANNEL) = 0;
  *hm_dp(ram, r, 0x29) = BBAD_VRAM;
  r->y = size;
  r->x = vram;
  hm_dp_w16(ram, r, 0x72, (uint16_t)src);
  *hm_dp(ram, r, 0x74) = (uint8_t)(src >> 16);
  r->a = 0x0080;                                           /* VMAIN */
  jsl(ram, r, at_add, 0x80, hm_AddProgrammedDMA);
  jsl(ram, r, at_start, 0x80, hm_StartLastPreparedDMA);
}

void hm_LoadDialogueBox(uint8_t *ram, HmRegs *r)
{
  hm_dp_w16(ram, r, 0x72, 0xCB05);
  hm_dp_w16(ram, r, 0x75, 0x2000);
  *hm_dp(ram, r, 0x74) = 0xA5;
  *hm_dp(ram, r, 0x77) = 0x7E;
  r->a = 0x207E;                                           /* LDA #$2000; ...; LDA #$7E */
  jsl(ram, r, 0x92D3, 0x83, hm_DecompressTileMap);
  vram_copy(ram, r, 0x7E2000, 0x7800, 0x0400, 0x92F9, 0x92FD);
  vram_copy(ram, r, 0x9CBBA3, 0x5000, 0x1000, 0x9324, 0x9328);
}

/* ─────────────────────────────────────────────────────────────────────────
 * SUB_82E5E7 ($82:E5E7, JSR, RTS; the title and name screens): draws a
 * 3 x 2 block of tiles. A = which block: its VRAM address from the table at
 * $82:E73E ($80); Y = 0 or not ($84): the second set of tiles. For each of
 * the two rows ($82 = 0, 1; the row's address + $400 each time) three DMAs
 * of $24 bytes from $82:E666 + $7E ($7E = 0 or $6C, + $24 each) to VRAM
 * address, + $20 each.
 * Leftovers: A = 2, C = 1 (CMP #2), X = the last address + $60, Y = $24;
 * $7E, $80, $82, $84 as they ended.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_SUB_82E5E7(uint8_t *ram, HmRegs *r)
{
  r->x = (uint16_t)(r->a << 1);
  hm_dp_w16(ram, r, 0x80, hm_bus_rd16(ram, 0x82E73Eu + r->x));
  hm_dp_w16(ram, r, 0x82, 0);
  hm_dp_w16(ram, r, 0x84, r->y);
  for (;;) {
    r->x = hm_dp_r16(ram, r, 0x80);
    r->y = 0x0024;
    hm_dp_w16(ram, r, 0x7E, hm_dp_r16(ram, r, 0x84) ? 0x006C : 0);
    for (uint16_t i = 0; i < 3; i++) {
      push16(ram, r, i);                                   /* PHA */
      *hm_dp(ram, r, DMA_CHANNEL) = 0;
      *hm_dp(ram, r, 0x29) = BBAD_VRAM;
      uint16_t src = (uint16_t)(0xE666 + hm_dp_r16(ram, r, 0x7E));
      hm_dp_w16(ram, r, 0x72, src);
      *hm_dp(ram, r, 0x74) = 0x82;
      r->a = (uint16_t)((src & 0xFF00) | 0x80);            /* 8-bit LDA #$80: VMAIN */
      uint16_t x = r->x, y = r->y;
      push16(ram, r, y);                                   /* PHY; PHX */
      push16(ram, r, x);
      jsl(ram, r, 0xE630, 0x80, hm_AddProgrammedDMA);
      jsl(ram, r, 0xE634, 0x80, hm_StartLastPreparedDMA);
      x = pull16(ram, r);                                  /* PLX; PLY */
      y = pull16(ram, r);
      r->y = y;
      r->x = (uint16_t)(x + 0x20);
      hm_dp_w16(ram, r, 0x7E, (uint16_t)(hm_dp_r16(ram, r, 0x7E) + 0x24));
      r->a = (uint16_t)(pull16(ram, r) + 1);               /* PLA; INC */
      r->c = r->a >= 3;
    }
    hm_dp_w16(ram, r, 0x80, (uint16_t)(hm_dp_r16(ram, r, 0x80) + 0x400));
    r->a = (uint16_t)(hm_dp_r16(ram, r, 0x82) + 1);
    hm_dp_w16(ram, r, 0x82, r->a);
    r->c = r->a >= 2;
    if (r->a == 2) break;
  }
}

/* ─────────────────────────────────────────────────────────────────────────
 * SUB_82EA80 ($82:EA80, JSL, RTL; the name screen): four letters $A8 drawn
 * (DrawLetter, X = 1) at the positions $82:EB4D, + $10 each ($0185 = the
 * position), each sent at once (StartProgramedDMA).
 * Leftovers: X = 4, C = 1 (CPX #4), A = $82 = the last position + $10, Y
 * that of the calls.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_SUB_82EA80(uint8_t *ram, HmRegs *r)
{
  hm_dp_w16(ram, r, 0x82, hm_bus_rd16(ram, 0x82EB4D));
  for (uint16_t i = 0; i < 4; i++) {
    push16(ram, r, i);                                     /* PHX */
    hm_bus_wr16(ram, 0x800185, hm_dp_r16(ram, r, 0x82));
    r->a = 0x00A8;
    r->x = 0x0001;
    jsl(ram, r, 0xEA9A, 0x83, hm_DrawLetter);
    jsl(ram, r, 0xEA9E, 0x80, hm_StartProgramedDMA);
    r->a = (uint16_t)(hm_dp_r16(ram, r, 0x82) + 0x10);
    hm_dp_w16(ram, r, 0x82, r->a);
    r->x = (uint16_t)(pull16(ram, r) + 1);                 /* PLX; INX */
  }
  r->c = 1;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Bars of tiles (the title screen's and the file screen's frames). A bar is
 * drawn one column at a time: the column's two tile pairs live in the
 * direct page ($7E/$80 the top row, $82/$84 the bottom one: tile, tile + 1,
 * tile + $10, tile + $11) and are copied from there ($80:007E, $80:0082, 4
 * bytes each, channels 0 and 1) to VRAM $86 and $86 + $20; then the four
 * tiles and the address move on by 2.
 *   SUB_82EAB4 ($82:EAB4, JSR, RTS): one bar: the first tile at
 *     $82:EB4B, the address at $82:EB49, the columns at $82:EB55.
 *   SUB_82E742 ($82:E742, JSR, RTS): eight bars, from the tables at
 *     $82:F268 (tiles), $82:F258 (addresses), $82:F288 (columns).
 * Leftovers: A = Y = $88 = the bar's columns, C = 1 (CPY); X = 8 (from
 * StartProgramedDMA) or, for the eight bars, $10.
 * ───────────────────────────────────────────────────────────────────────── */
static void bar_start(uint8_t *ram, HmRegs *r, uint16_t tile, uint16_t vram)
{
  hm_dp_w16(ram, r, 0x7E, tile);
  hm_dp_w16(ram, r, 0x80, (uint16_t)(tile + 1));
  hm_dp_w16(ram, r, 0x82, (uint16_t)(tile + 0x10));
  hm_dp_w16(ram, r, 0x84, (uint16_t)(tile + 0x11));
  hm_dp_w16(ram, r, 0x86, vram);
}
/* one column; the AddProgrammedDMA / StartProgramedDMA calls at `at` */
static void bar_column(uint8_t *ram, HmRegs *r, const uint16_t at[3])
{
  for (int half = 0; half < 2; half++) {
    *hm_dp(ram, r, DMA_CHANNEL) = (uint8_t)half;
    *hm_dp(ram, r, 0x29) = BBAD_VRAM;
    r->y = 0x0004;
    r->x = (uint16_t)(hm_dp_r16(ram, r, 0x86) + 0x20 * half);
    hm_dp_w16(ram, r, 0x72, (uint16_t)(0x007E + 4 * half));
    *hm_dp(ram, r, 0x74) = 0x80;
    r->a = 0x0080;
    jsl(ram, r, at[half], 0x80, hm_AddProgrammedDMA);
  }
  jsl(ram, r, at[2], 0x80, hm_StartProgramedDMA);
  for (unsigned v = 0x7E; v <= 0x86; v += 2)
    hm_dp_w16(ram, r, v, (uint16_t)(hm_dp_r16(ram, r, v) + 2));
}

void hm_SUB_82EAB4(uint8_t *ram, HmRegs *r)
{
  static const uint16_t at[3] = { 0xEAF3, 0xEB1E, 0xEB22 };
  bar_start(ram, r, hm_bus_rd16(ram, 0x82EB4B), hm_bus_rd16(ram, 0x82EB49));
  uint16_t columns = hm_bus_rd16(ram, 0x82EB55);
  uint16_t y = 0;
  do {
    push16(ram, r, y);                                     /* PHY */
    bar_column(ram, r, at);
    y = (uint16_t)(pull16(ram, r) + 1);                    /* PLY; INY */
    r->y = y;
    r->a = columns;
    hm_dp_w16(ram, r, 0x88, columns);
  } while (y != columns);
  r->c = 1;
}

void hm_SUB_82E742(uint8_t *ram, HmRegs *r)
{
  static const uint16_t at[3] = { 0xE787, 0xE7B2, 0xE7B6 };
  for (uint16_t x = 0; x != 0x10; x += 2) {
    bar_start(ram, r, hm_bus_rd16(ram, 0x82F268u + x), hm_bus_rd16(ram, 0x82F258u + x));
    uint16_t columns = hm_bus_rd16(ram, 0x82F288u + x);
    uint16_t y = 0;
    do {
      push16(ram, r, y);                                   /* PHY; PHX */
      push16(ram, r, x);
      bar_column(ram, r, at);
      pull16(ram, r);                                      /* PLX; PLY; INY */
      y = (uint16_t)(pull16(ram, r) + 1);
      r->y = y;
      r->a = columns;
      hm_dp_w16(ram, r, 0x88, columns);
    } while (y != columns);
    r->x = (uint16_t)(x + 2);
  }
  r->c = 1;
}

/* ─────────────────────────────────────────────────────────────────────────
 * CODE_82E405 ($82:E405, JSR, RTS): one saved game's line on the file
 * screen; A = the line (slot), Y as SUB_82E5E7 wants it. The slot's summary
 * is read (LoadGameSlotResume); an empty one (X = 0) is drawn as a blank
 * block (JML SUB_82E5E7). Otherwise, at the positions of the line's 8-byte
 * entry at $82:F278 (the text position goes in $0185, + 8 a letter):
 *   +0 the player's name, 4 letters ($0881-$0884);
 *   +2 the year (+ 1, PrintDigit) and its two-letter suffix ($82:F298);
 *   +4 the day, two digits (PrintDigit with $018C = 1, then 0, + 8);
 *   +6 the season's name (LoadsDateNames) and its 6 letters ($08B3-$08BD).
 * Every letter or digit is sent at once (StartProgramedDMA).
 * Leftovers: those of the last StartProgramedDMA; the stack keeps the
 * pushed entry offsets.
 * ───────────────────────────────────────────────────────────────────────── */
#define TEXT_AT 0x800185   /* the text position (long) */

static void letter_now(uint8_t *ram, HmRegs *r, uint16_t letter, uint16_t at)
{
  r->a = letter;
  r->x = 0;
  jsl(ram, r, at, 0x83, hm_DrawLetter);
  jsl(ram, r, (uint16_t)(at + 4), 0x80, hm_StartProgramedDMA);
}
static void next_letter(uint8_t *ram)
{
  hm_bus_wr16(ram, TEXT_AT, (uint16_t)(hm_bus_rd16(ram, TEXT_AT) + 8));
}
static uint16_t rd16_db(uint8_t *ram, const HmRegs *r, uint16_t a) { return hm_bus_rd16(ram, db(r, a)); }
/* PrintDigit of $0192 with $018C = `which`, sent at once; A's high byte
 * is the position's (its 16-bit load) */
static void digit_now(uint8_t *ram, HmRegs *r, uint8_t which, uint16_t at)
{
  wr8(ram, r, 0x018C, which);
  uint8_t flags = (uint8_t)(hm_bus_rd8(ram, db(r, 0x019B)) & 0x7F);
  wr8(ram, r, 0x019B, flags);
  r->a = (uint16_t)((hm_bus_rd16(ram, TEXT_AT) & 0xFF00) | flags);
  jsl(ram, r, at, 0x83, hm_PrintDigit);
  jsl(ram, r, (uint16_t)(at + 4), 0x80, hm_StartProgramedDMA);
}

void hm_CODE_82E405(uint8_t *ram, HmRegs *r)
{
  uint16_t a = r->a, y = r->y;
  push16(ram, r, a);                                       /* PHA; PHY */
  push16(ram, r, y);
  jsl(ram, r, 0xE409, 0x83, hm_LoadGameSlotResume);
  r->y = pull16(ram, r);                                   /* PLY; PLA */
  r->a = pull16(ram, r);
  r->c = r->x >= 0;                                        /* CPX #0 */
  if (r->x == 0) { hm_SUB_82E5E7(ram, r); return; }        /* JML: an empty slot */

  uint16_t entry = (uint16_t)(r->a << 3);
  push16(ram, r, entry);                                   /* PHX */
  hm_dp_w16(ram, r, 0x82, hm_bus_rd16(ram, 0x82F278u + entry));
  for (uint16_t i = 0; i < 4; i++) {                       /* the name */
    push16(ram, r, i);
    hm_bus_wr16(ram, TEXT_AT, hm_dp_r16(ram, r, 0x82));
    letter_now(ram, r, hm_bus_rd8(ram, db(r, (uint16_t)(0x0881 + i))), 0xE43C);
    hm_dp_w16(ram, r, 0x82, (uint16_t)(hm_dp_r16(ram, r, 0x82) + 8));
    pull16(ram, r);
  }
  pull16(ram, r);                                          /* PLX; INX; INX */
  entry = (uint16_t)(entry + 2);
  push16(ram, r, entry);
  hm_bus_wr16(ram, TEXT_AT, hm_bus_rd16(ram, 0x82F278u + entry));
  uint8_t year = hm_bus_rd8(ram, 0x7F1F18);
  wr8(ram, r, 0x0192, (uint8_t)(year + 1));
  wr16(ram, r, 0x0193, 0);
  digit_now(ram, r, 0, 0xE481);
  next_letter(ram);
  letter_now(ram, r, hm_bus_rd16(ram, 0x82F298u + 4u * year), 0xE4AE);
  next_letter(ram);
  letter_now(ram, r, hm_bus_rd16(ram, 0x82F298u + 4u * year + 2), 0xE4DF);
  pull16(ram, r);
  entry = (uint16_t)(entry + 2);
  push16(ram, r, entry);
  hm_bus_wr16(ram, TEXT_AT, hm_bus_rd16(ram, 0x82F278u + entry));
  wr8(ram, r, 0x0192, hm_bus_rd8(ram, 0x7F1F1B));
  wr16(ram, r, 0x0193, 0);
  digit_now(ram, r, 1, 0xE512);
  pull16(ram, r);                                          /* PLX; PHX */
  push16(ram, r, entry);
  uint32_t at8 = (uint32_t)hm_bus_rd16(ram, 0x82F278u + entry) + 8;
  r->c = at8 > 0xFFFF;
  hm_bus_wr16(ram, TEXT_AT, (uint16_t)at8);
  r->a = (uint16_t)(at8 & 0xFF00);                         /* SEP; LDA #0 */
  wr8(ram, r, 0x018C, 0);
  jsl(ram, r, 0xE531, 0x83, hm_PrintDigit);
  jsl(ram, r, 0xE535, 0x80, hm_StartProgramedDMA);
  jsl(ram, r, 0xE53B, 0x82, hm_LoadsDateNames);
  pull16(ram, r);
  entry = (uint16_t)(entry + 2);
  hm_bus_wr16(ram, TEXT_AT, hm_bus_rd16(ram, 0x82F278u + entry));
  for (uint16_t i = 0; i < 6; i++) {                       /* the season's name */
    if (i) next_letter(ram);
    letter_now(ram, r, rd16_db(ram, r, (uint16_t)(0x08B3 + 2 * i)), (uint16_t)(0xE552 + 0x1C * i));
  }
}
