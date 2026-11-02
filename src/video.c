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
 * StartProgrammedDMA starts them all (during the NMI, when video memory can
 * be written).
 */
#include "hm_decomp.h"
#include "hm_ram.h"   /* from the recompilation project */
#include "hm_vars.h"
#include "hm_regs.h"

/* Direct page. The prepared DMAs (AddProgrammedDMA's arguments and notes): */
#define DMA_CHANNEL     0x27   /* the channel prepared (the last one) */
#define DMA_WAITING     0x28   /* the channels to start */
#define DMA_BBAD        0x29   /* the B address (destination register, $21xx) */
#define DMA_VMAIN       0x2A   /* + channel */
#define DMA_DESTINATION 0x32   /* + 2 channel */
#define SOURCE          0x72   /* long $72-$74: the source of a DMA (AddProgrammedDMA)
                                * or of packed data (DecompressTileMap) */
#define SOURCE_BANK     0x74
#define UNPACK_TO       0x75   /* long $75-$77: where DecompressTileMap unpacks */
#define UNPACK_TO_BANK  0x77
/* The map scroll (see GUIDE.md, scroll.c): */
#define VRAM_COLUMN     0x10   /* where the next column goes in video memory */
#define WRAM_COLUMN     0x12   /* where it comes from in the WRAM map */
#define VRAM_ROW        0x14   /* where the next row goes in video memory */
#define WRAM_ROW        0x16   /* where it comes from in the WRAM map */
#define MAP_ROW_LENGTH  0x1A   /* the length of a map row */
#define CAMERA_IN_ROW   0x1C   /* the camera's offset inside the row */
#define CAMERA_SPEED    0x1E   /* pixels per frame */
#define PIXELS_IN_TILE_X 0x20  /* pixels inside the tile (0-7), across */
#define PIXELS_IN_TILE_Y 0x21  /*   and down */
/* Others: */
#define NMI_STATUS      0x00   /* the frame's work is done (set by UpdateGraphics) */
#define MAP_RECORD      0x18   /* BackgroundsManager: the map's record (data bank) */
#define INIDISP_COPY    0x24   /* the game's copy of INIDISP */
#define OAM_CHANNEL     0x9A   /* the MDMAEN bit for the sprite table */
#define OAM_USED        0xAF   /* bytes of the WRAM sprite table filled (PrepareOAMData) */
#define COPY_AT         0xBF   /* CopySpritePiecesToVRAM: the HM_LOAD_COPIES entry (offset) */
/* $7E-$88 mean a different thing in each routine: */
#define BRIGHTNESS      0x7E   /* SetBrightness: the new brightness (written as $80:007E) */
#define PICTURE_AT      0x7E   /* CopyMapPictureToVRAM: the offset in DECOMP_BUFFER */
#define ROW_STEP        0x80   /* BackgroundsManager: the step between the row pairs */
#define BLOCK_SOURCE    0x7E   /* DrawTileBlock3x2: the offset in BLOCK_TILES */
#define BLOCK_VRAM      0x80   /*   the row's VRAM address */
#define BLOCK_ROW       0x82   /*   the row (0, 1) */
#define BLOCK_SECOND    0x84   /*   not 0: the second set of tiles */
#define BLANK_AT        0x82   /* DrawNameEntryBlanks: the next blank's position */
#define NAME_AT         0x82   /* DrawSaveFileLine: the next letter's position */
#define BAR_TILE        0x7E   /* the bars: the column's tiles: tile, */
#define BAR_TILE_1      0x80   /*   tile + 1 (the top row, copied from $80:007E), */
#define BAR_TILE_10     0x82   /*   tile + $10, */
#define BAR_TILE_11     0x84   /*   tile + $11 (the bottom row, from $80:0082) */
#define BAR_VRAM        0x86   /*   the column's VRAM address */
#define BAR_COLUMNS     0x88   /*   the bar's columns */
/* BackgroundsManager's own uses: */
#define PICTURE_VRAM    0x8A   /* the layer picture's VRAM address */
#define TILEMAPS_LEFT   0x92   /* tilemaps still to load */
#define PICTURES_LEFT   0x93   /* layer pictures still to load */

/* Data-bank variables (or long, in bank $80) */
#define CAMERA_BG       HM_CAM_X  /* $013C-$0147: BG1-BG3's scroll, H then V, 16b each */
#define LAYER_KIND      0x0182    /* DB: 3 or more sets HM_WEATHER_FLAGS bit 0 (the map scrolls) */
#define TEXT_AT         (0x800000 | HM_TEXT_X)   /* the text position (long) */
#define PRESET_NUMBER   0x8019B6  /* $19B6: the preset ManageGraphicPresets was given */
#define SCROLL_COPIES   0x19C0    /* $19C0-$19CE: words, cleared */
#define SCROLL_COPIES_LAST 0x19CE
#define M7SEL_COPY      0x8019D0  /* $19D0: the copy of M7SEL */
#define COLDATA_COPY    0x8019E0  /* $19E0: the copy of COLDATA */
#define BGSC_BYTES      0x19E2    /* DB: bytes $19E2/$19E4/$19E6/$19E8, cleared */
#define BGSC_BYTES_LAST 0x19E8
#define BGSC_NO_SIZE    0x8019E3  /* $19E3/$19E5/$19E7/$19E9: BGnSC without the size bits */
#define OBJ_GRAPHICS    0x8019EA  /* $19EA: the sprites' graphics in VRAM (words) */
/* Long addresses */
#define DECOMP_BUFFER   0x7E2000  /* where pictures and tilemaps are unpacked */
#define COPY_BANK       2         /* in a HM_LOAD_COPIES entry: the source's bank */
#define COPY_VRAM       4         /* in a HM_LOAD_COPIES entry: the VRAM address */
#define COPY_SIZE       6

/* ROM */
#define ZERO_WORD       0x808424  /* $0000 */
#define OFF_SCREEN      0x808426  /* $00F0 */
#define LAYER_SIZES     0x80AA68  /* by HM_MAP_WIDTH: MAP_ROW_LENGTH */
#define LAYER_SIZES_2   0x80AA72  /* by HM_MAP_WIDTH: CAMERA_IN_ROW */
#define BLOCK_TILES     0x82E666  /* DrawTileBlock3x2's tiles */
#define BLOCK_PLACES    0x82E73E  /* DrawTileBlock3x2: per block, its VRAM address */
#define BAR_VRAM_AT     0x82EB49  /* DrawScreenBar: the address */
#define BAR_TILE_AT     0x82EB4B  /*   the first tile */
#define BAR_COLUMNS_AT  0x82EB55  /* DrawScreenBar: the columns */
#define BARS_VRAM       0x82F258  /* DrawScreenBars: per bar, the address, */
#define BARS_TILE       0x82F268  /*   the first tile, */
#define SAVE_LINES      0x82F278  /* DrawSaveFileLine: per line, 4 positions */
#define BARS_COLUMNS    0x82F288  /* DrawScreenBars: per bar, the columns */
#define YEAR_SUFFIXES   0x82F298  /* 4 bytes per year: two letters */
#define BOX_TILEMAP     0xA5CB05  /* the text box's tilemap (packed) */
#define BLANK_LETTERS   0x9CBBA3  /* the box's letter tiles, blank */

/* VRAM (word addresses) */
#define VRAM_LETTERS    0x5000    /* the text box's letters */
#define VRAM_SHORT      0x7000    /* a layer picture here (maps below $5B) skips rows */
#define VRAM_BOX_MAP    0x7800    /* the text box's tilemap */

static uint32_t db(const HmRegs *r, uint16_t a) { return (uint32_t)r->db << 16 | a; }
static void set_a8(HmRegs *r, uint8_t v) { r->a = (uint16_t)((r->a & 0xFF00) | v); }

/* ─────────────────────────────────────────────────────────────────────────
 * StartProgrammedDMA ($80:8AF0, JSL, RTL): for each channel 0-7 waiting in
 * $28: when its B address ($43x1) is $18 (VRAM) VMAIN = $2A + ch and the
 * VRAM address = $32 + 2ch (16 bits); otherwise the CGRAM address = the
 * low byte of $32 + 2ch. Then the channel is started ($420B = its bit: the
 * copy happens there). At the end $28 = 0 and $420B = 0.
 * Leftovers: X = 8, C = 1 (CPX #8); A's low byte = the last channel's bit
 * when channel 7 was started, else 0; its high byte = the high byte of the
 * last VRAM address used (16-bit load), or as it was. The stack keeps the
 * last channel PHX'd.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_StartProgrammedDMA(uint8_t *ram, HmRegs *r)
{
  for (uint8_t ch = 0; ch < 8; ch++) {
    /* $28 is read again for each channel: a copy into WRAM ($2180) can
     * change it */
    uint8_t bit = (uint8_t)(1 << ch), waiting = *hm_dp(ram, r, DMA_WAITING);
    set_a8(r, (uint8_t)(bit & waiting));
    if (!(bit & waiting)) continue;
    hm_push8(ram, r, ch);                                          /* PHX */
    uint8_t bbad = hm_bus_rd8(ram, db(r, (uint16_t)(BBAD0 + 16 * ch)));
    hm_pull8(ram, r);                                              /* PLX */
    if (bbad == BBAD_VRAM) {
      hm_bus_wr8(ram, db(r, VMAIN), *hm_dp(ram, r, DMA_VMAIN + ch));
      r->a = hm_dp_r16(ram, r, DMA_DESTINATION + 2u * ch);
      hm_bus_wr16(ram, db(r, VMADDL), r->a);
    } else {
      set_a8(r, *hm_dp(ram, r, DMA_DESTINATION + 2u * ch));
      hm_bus_wr8(ram, db(r, CGADD), (uint8_t)r->a);
    }
    set_a8(r, bit);
    hm_bus_wr8(ram, db(r, MDMAEN), bit);                           /* the copy */
  }
  r->x = 8;
  r->c = 1;
  *hm_dp(ram, r, DMA_WAITING) = 0;
  hm_bus_wr8(ram, db(r, MDMAEN), 0);
}

/* ─────────────────────────────────────────────────────────────────────────
 * The screen's brightness and blanking. $24 is the game's copy of INIDISP
 * ($2100: bit 7 = forced blank, bits 0-3 = brightness); each routine
 * changes the copy and writes it to the chip.
 *   ForceBlank ($80:8E0F, JSL, RTL): sets bit 7.
 *   ResetForceBlank ($80:8E1E): keeps only the brightness.
 *   SetBrightness ($80:8E2D): brightness = A & $0F, keeping the blank
 *     bit; brightness 0 means blank instead ($80; $7E keeps the value).
 *   InitializeScreenStatusVars ($80:8C4C): $24 = 0, then ForceBlank.
 * Leftovers: A's low byte = the value written (high byte untouched); the
 * flags as they were (PHP / PLP); the stack keeps the PHP'd byte.
 * ───────────────────────────────────────────────────────────────────────── */
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
void hm_SetBrightness(uint8_t *ram, HmRegs *r)
{
  php(ram, r);
  uint8_t bright = (uint8_t)(r->a & 0x0F);
  if (!bright) bright = 0x80;
  hm_bus_wr8(ram, 0x800000 | BRIGHTNESS, bright);          /* STA.L $80:007E */
  set_inidisp(ram, r, (uint8_t)((*hm_dp(ram, r, INIDISP_COPY) & 0x80) | *hm_dp(ram, r, BRIGHTNESS)));
  plp(ram, r);
}
void hm_InitializeScreenStatusVars(uint8_t *ram, HmRegs *r)
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
/* A register's entry: its table in bank $80 (11 bytes, one per preset),
 * its copy in WRAM ($19xx) and the register ($21xx, low byte) */
typedef struct Preset { uint16_t table, copy; uint8_t reg; } Preset;

static void preset(uint8_t *ram, HmRegs *r, const Preset *p)
{
  uint8_t v = hm_bus_rd8(ram, 0x800000u | (uint16_t)(p->table + r->x));
  set_a8(r, v);
  hm_bus_wr8(ram, 0x800000u | p->copy, v);
  hm_bus_wr8(ram, INIDISP | p->reg, v);                    /* $21xx */
}

void hm_ManageGraphicPresets(uint8_t *ram, HmRegs *r)
{
  static const Preset bg_sc[] = {                          /* BG1SC-BG4SC */
    { 0x8B5A, 0x19BA, 0x07 }, { 0x8B65, 0x19BB, 0x08 }, { 0x8B70, 0x19BC, 0x09 }, { 0x8B7B, 0x19BD, 0x0A } };
  static const Preset nba[] = { { 0x8B86, 0x19BE, 0x0B }, { 0x8B91, 0x19BF, 0x0C } };   /* BG12NBA, BG34NBA */
  static const Preset rest[] = {
    { 0x8BE9, 0x19D1, 0x23 }, { 0x8BF4, 0x19D2, 0x24 }, { 0x8BFF, 0x19D3, 0x25 },   /* windows: W12SEL-WOBJSEL */
    { 0x8C36, 0x19D8, 0x2A }, { 0x8C41, 0x19D9, 0x2B },                             /* WBGLOG, WOBJLOG */
    { 0x8C0A, 0x19D4, 0x26 }, { 0x8C15, 0x19D5, 0x27 }, { 0x8C20, 0x19D6, 0x28 }, { 0x8C2B, 0x19D7, 0x29 },  /* WH0-WH3 */
    { 0x8B9C, 0x19DA, 0x2C }, { 0x8BA7, 0x19DB, 0x2D }, { 0x8BB2, 0x19DC, 0x2E }, { 0x8BBD, 0x19DD, 0x2F },  /* TM, TS, TMW, TSW */
    { 0x8BC8, 0x19DE, 0x30 }, { 0x8BD3, 0x19DF, 0x31 } };                           /* CGWSEL, CGADSUB */

  php(ram, r);
  uint8_t which = (uint8_t)r->a;
  hm_bus_wr8(ram, PRESET_NUMBER, which);
  r->x = which;                                            /* TAX (8-bit) */
  static const Preset obsel = { 0x8B44, 0x19B7, 0x01 };    /* OBSEL */
  preset(ram, r, &obsel);
  r->a = (uint16_t)((r->a & 0x0007) << 13);                /* XBA; AND #$0700; ASL x5 */
  hm_bus_wr16(ram, OBJ_GRAPHICS, r->a);
  static const Preset bgmode = { 0x8B4F, 0x19B8, 0x05 };   /* BGMODE */
  preset(ram, r, &bgmode);
  for (uint16_t a = BGSC_BYTES; a <= BGSC_BYTES_LAST; a += 2) hm_bus_wr8(ram, db(r, a), 0);
  for (unsigned i = 0; i < 4; i++) {
    preset(ram, r, &bg_sc[i]);
    set_a8(r, (uint8_t)(r->a & 0xFC));                     /* without the size bits */
    hm_bus_wr8(ram, BGSC_NO_SIZE + 2u * i, (uint8_t)r->a);
  }
  for (unsigned i = 0; i < 2; i++) preset(ram, r, &nba[i]);
  r->a = 0;
  for (uint16_t a = SCROLL_COPIES; a <= SCROLL_COPIES_LAST; a += 2) hm_bus_wr16(ram, 0x800000u | a, 0);
  if (*hm_dp(ram, r, INIDISP_COPY) & 0x80) {               /* forced blank: scroll = 0 */
    hm_push8(ram, r, (uint8_t)r->x);                       /* PHX */
    for (uint8_t x = 0; x < 8; x += 2) {
      hm_bus_wr8(ram, BG1HOFS + x, 0); hm_bus_wr8(ram, BG1HOFS + x, 0);
      hm_bus_wr8(ram, BG1VOFS + x, 0); hm_bus_wr8(ram, BG1VOFS + x, 0);
    }
    hm_pull8(ram, r);                                      /* PLX */
  }
  hm_bus_wr8(ram, M7SEL_COPY, 0);                          /* M7SEL */
  hm_bus_wr8(ram, M7SEL, 0);
  for (unsigned i = 0; i < sizeof rest / sizeof rest[0]; i++) preset(ram, r, &rest[i]);
  set_a8(r, 0xE0);                                         /* COLDATA */
  hm_bus_wr8(ram, COLDATA_COPY, 0xE0);
  hm_bus_wr8(ram, COLDATA, 0xE0);
  static const Preset setini = { 0x8BDE, 0x19E1, 0x33 };   /* SETINI */
  preset(ram, r, &setini);
  plp(ram, r);
}

/* ─────────────────────────────────────────────────────────────────────────
 * Clearing video memory, each with one DMA on channel 0 from a word of 0
 * in the ROM ($80:8424, the source fixed). All but UnusedHideSprites first force
 * blank (INIDISP = $80) and switch the interrupts off (NMITIMEN = 0).
 *   ZeroesVRAM ($80:8846, JSL, RTL): VMAIN $80, address 0, $10000 bytes
 *     (size 0): all of VRAM.
 *   ZeroesOAM ($80:8887): OAM address 0, $43F bytes, twice the OAM.
 *   ZeroesCGRAM ($80:8980): CGRAM address 0, $3FF bytes, twice the CGRAM
 *     (less one byte: the last colour keeps its high byte).
 *   ZeroesPartialVRAM ($80:89BC): from the VRAM address in A, $FFF bytes.
 *   UnusedHideSprites ($80:88C3; nothing calls it): the sprites off screen: OAM from
 *     the word $00F0 at $80:8426 ($200 bytes, 8-bit mode) and the high
 *     table from 0 ($20 bytes); then on channel 4 the game's WRAM copy of
 *     the OAM ($7E:A000, $AF bytes) and of the high table ($7E:A200).
 * Leftovers: A = the last 16-bit load with the low byte of the last MDMAEN
 * value; UnusedHideSprites also X = $20, C = 0; the rest untouched.
 * ───────────────────────────────────────────────────────────────────────── */
static void wr16(uint8_t *ram, const HmRegs *r, uint16_t a, uint16_t v) { hm_bus_wr16(ram, db(r, a), v); }
static void wr8(uint8_t *ram, const HmRegs *r, uint16_t a, uint8_t v) { hm_bus_wr8(ram, db(r, a), v); }

/* channel `ch` (its registers at $43n0): mode, B address, source, size; then
 * MDMAEN. A as the original's loads leave it (the size, then the bit). */
static void dma_go(uint8_t *ram, HmRegs *r, unsigned ch, uint8_t mode, uint8_t bbad,
                   uint32_t src, uint16_t size)
{
  uint16_t at = (uint16_t)(DMAP0 + 16 * ch);
  wr8(ram, r, at, mode);
  wr8(ram, r, (uint16_t)(at + 1), bbad);                   /* $43n1 */
  wr16(ram, r, (uint16_t)(at + 2), (uint16_t)src);         /* $43n2 */
  wr8(ram, r, (uint16_t)(at + 4), (uint8_t)(src >> 16));   /* $43n4 */
  wr16(ram, r, (uint16_t)(at + 5), size);                  /* $43n5 */
  r->a = (uint16_t)((size & 0xFF00) | (1u << ch));
  wr8(ram, r, MDMAEN, (uint8_t)(1u << ch));
}
static void blank_no_interrupts(uint8_t *ram, HmRegs *r)
{
  wr8(ram, r, INIDISP, 0x80);
  wr8(ram, r, NMITIMEN, 0);
}

void hm_ZeroesVRAM(uint8_t *ram, HmRegs *r)
{
  blank_no_interrupts(ram, r);
  wr8(ram, r, VMAIN, 0x80);
  wr16(ram, r, VMADDL, 0);
  dma_go(ram, r, 0, 0x09, BBAD_VRAM, ZERO_WORD, 0x0000);
}
void hm_ZeroesOAM(uint8_t *ram, HmRegs *r)
{
  blank_no_interrupts(ram, r);
  wr16(ram, r, OAMADDL, 0);
  dma_go(ram, r, 0, 0x09, BBAD_OAM, ZERO_WORD, 0x043F);
}
void hm_ZeroesCGRAM(uint8_t *ram, HmRegs *r)
{
  blank_no_interrupts(ram, r);
  wr16(ram, r, CGADD, 0);
  dma_go(ram, r, 0, 0x09, BBAD_CGDATA, ZERO_WORD, 0x03FF);
}
void hm_ZeroesPartialVRAM(uint8_t *ram, HmRegs *r)
{
  wr16(ram, r, VMADDL, r->a);
  blank_no_interrupts(ram, r);
  wr8(ram, r, VMAIN, 0x80);
  dma_go(ram, r, 0, 0x09, BBAD_VRAM, ZERO_WORD, 0x0FFF);
}
void hm_UnusedHideSprites(uint8_t *ram, HmRegs *r)
{
  wr16(ram, r, OAMADDL, 0);
  dma_go(ram, r, 0, 0x08, BBAD_OAM, OFF_SCREEN, 0x0200);
  wr16(ram, r, OAMADDL, 0x0100);
  dma_go(ram, r, 0, 0x08, BBAD_OAM, ZERO_WORD, 0x0020);
  wr16(ram, r, OAMADDL, 0);
  /* channel 4, written register by register (8-bit) */
  r->x = 0;
  wr16(ram, r, OAMADDL, 0);
  wr8(ram, r, DMAP4, 0); wr8(ram, r, BBAD4, BBAD_OAM);
  wr8(ram, r, A1T4L, (uint8_t)WRAM_LONG(HM_OAM_COPY)); wr8(ram, r, A1T4H, (uint8_t)(WRAM_LONG(HM_OAM_COPY) >> 8));
  wr8(ram, r, A1B4, (uint8_t)(WRAM_LONG(HM_OAM_COPY) >> 16));
  r->x = hm_dp_r16(ram, r, OAM_USED);
  wr16(ram, r, DAS4L, r->x);
  wr8(ram, r, MDMAEN, 0x10);
  r->x = 0x0100;
  wr16(ram, r, OAMADDL, 0x0100);
  wr8(ram, r, DMAP4, 0); wr8(ram, r, BBAD4, BBAD_OAM);
  wr8(ram, r, A1T4L, (uint8_t)WRAM_LONG(HM_OAM_COPY_HIGH)); wr8(ram, r, A1T4H, (uint8_t)(WRAM_LONG(HM_OAM_COPY_HIGH) >> 8));
  wr8(ram, r, A1B4, (uint8_t)(WRAM_LONG(HM_OAM_COPY_HIGH) >> 16));
  r->c = 0;                                                /* CLC; ADC #2 */
  r->x = 0x0020;
  wr16(ram, r, DAS4L, 0x0020);
  r->a = 0x0010;
  wr8(ram, r, MDMAEN, 0x10);
}

/* ─────────────────────────────────────────────────────────────────────────
 * StartLastPreparedDMA ($80:8AB2, JSL, RTL): StartProgrammedDMA for one
 * channel only, the one in $27 (the last one prepared), right away; then
 * $28 = 0, forgetting any other waiting channel.
 * Leftovers: X = the channel, C = 0 (the last LSR); A's low byte = the
 * channel's bit, its high byte the VRAM address's (VRAM) or as it was;
 * the stack keeps the channel PHA'd.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_StartLastPreparedDMA(uint8_t *ram, HmRegs *r)
{
  uint8_t ch = *hm_dp(ram, r, DMA_CHANNEL);
  set_a8(r, ch);
  hm_push8(ram, r, ch);                                            /* PHA */
  uint8_t bbad = hm_bus_rd8(ram, db(r, (uint16_t)(BBAD0 + (uint8_t)(ch << 4))));
  hm_pull8(ram, r);                                                /* PLX */
  if (bbad == BBAD_VRAM) {
    wr8(ram, r, VMAIN, *hm_dp(ram, r, (unsigned)(DMA_VMAIN + ch)));
    r->a = hm_dp_r16(ram, r, (unsigned)(DMA_DESTINATION + 2u * ch));
    wr16(ram, r, VMADDL, r->a);
  } else {
    set_a8(r, *hm_dp(ram, r, (uint8_t)(DMA_DESTINATION + (uint8_t)(2 * ch))));
    wr8(ram, r, CGADD, (uint8_t)r->a);
  }
  r->x = ch;
  r->c = 0;
  uint8_t bit = hm_bus_rd8(ram, DMA_CHANNEL_BITS + ch);
  set_a8(r, bit);
  wr8(ram, r, MDMAEN, bit);
  *hm_dp(ram, r, DMA_WAITING) = 0;
  wr8(ram, r, MDMAEN, 0);
}

/* ─────────────────────────────────────────────────────────────────────────
 * CopyMapPictureToVRAM ($80:AA38, JSR, RTS; despite its name it writes
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

void hm_CopyMapPictureToVRAM(uint8_t *ram, HmRegs *r)
{
  uint16_t x = r->x, y = r->y;
  push16(ram, r, x);
  push16(ram, r, y);
  *hm_dp(ram, r, DMA_CHANNEL) = 0;
  *hm_dp(ram, r, DMA_BBAD) = BBAD_VRAM;                    /* the B address */
  uint32_t sum = (uint32_t)(uint16_t)DECOMP_BUFFER + hm_dp_r16(ram, r, PICTURE_AT);
  r->c = sum > 0xFFFF;
  hm_dp_w16(ram, r, SOURCE, (uint16_t)sum);
  *hm_dp(ram, r, SOURCE_BANK) = (uint8_t)(DECOMP_BUFFER >> 16);
  r->a = 0x0080;                                           /* VMAIN */
  jsl(ram, r, 0xAA5B, 0x80, hm_AddProgrammedDMA);
  jsl(ram, r, 0xAA5F, 0x80, hm_StartLastPreparedDMA);
  r->y = pull16(ram, r);
  r->x = pull16(ram, r);
}

/* ─────────────────────────────────────────────────────────────────────────
 * CopySpritePiecesToVRAM ($85:8E57, JSL, RTL): the sprite pieces the loader
 * (LoadQueuedSpritePieces) queued at $7E:A420 (6 bytes each: source, bank, VRAM
 * address; $FFFF ends the list) go to VRAM, each as two DMAs of $40 bytes
 * on channel 0: the top half of the 16 x 16 piece from the source to the
 * address, the bottom half from source + $200 to address + $100.
 * Leftovers: A = $FFFF, C = 1 (CMP), X = $BF = the end of the list.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_CopySpritePiecesToVRAM(uint8_t *ram, HmRegs *r)
{
  wr8(ram, r, VMAIN, 0x80);
  wr8(ram, r, DMAP0, 0x01);
  wr8(ram, r, BBAD0, BBAD_VRAM);
  hm_dp_w16(ram, r, COPY_AT, 0);
  for (;;) {
    uint16_t x = hm_dp_r16(ram, r, COPY_AT);
    r->x = x;
    r->a = hm_bus_rd16(ram, WRAM_LONG(HM_LOAD_COPIES) + x);
    if (r->a == 0xFFFF) break;
    for (int half = 0; half < 2; half++) {
      uint32_t vram = hm_bus_rd16(ram, WRAM_LONG(HM_LOAD_COPIES) + COPY_VRAM + x) + 0x100u * half;
      uint32_t src = hm_bus_rd16(ram, WRAM_LONG(HM_LOAD_COPIES) + x) + 0x200u * half;
      wr16(ram, r, VMADDL, (uint16_t)vram);
      wr16(ram, r, A1T0L, (uint16_t)src);
      wr16(ram, r, DAS0L, 0x0040);
      wr8(ram, r, A1B0, hm_bus_rd8(ram, WRAM_LONG(HM_LOAD_COPIES) + COPY_BANK + x));
      wr8(ram, r, MDMAEN, 0x01);
      r->x = hm_dp_r16(ram, r, COPY_AT);
    }
    hm_dp_w16(ram, r, COPY_AT, (uint16_t)(x + COPY_SIZE));
  }
  r->c = 1;
}

/* ─────────────────────────────────────────────────────────────────────────
 * UpdateGraphics ($80:86B1, JSR, RTS; from the NMI). Unless the frame's
 * work is done already ($00 != 0): the prepared DMAs (StartProgrammedDMA),
 * the sprite table (channel 4, if $9A says so), the sprite pieces
 * (CopySpritePiecesToVRAM). Then always: the pads (ReadJoypad), one random
 * number (GetRNG), the scroll of BG1-BG3 from $013C-$0147, and $00 = 1.
 * Quirk: after the sprite pieces the A register is 16-bit (left so by
 * CopySpritePiecesToVRAM), so the last "STZ $9A; STZ $420B" also clear $9B
 * and HDMAEN.
 * Leftovers: A = $0001 (GetRNG's high byte 0), X and Y those of the calls,
 * the flags as they were (PHP / PLP). (StartProgrammedDMA makes X and Y
 * 8-bit and CopySpritePiecesToVRAM 16-bit again: the console would clear
 * Y's high byte there, the recompiled game, our reference, keeps it.)
 * ───────────────────────────────────────────────────────────────────────── */
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
    jsl(ram, r, 0x86B8, 0x80, hm_StartProgrammedDMA);
    set_a8(r, *hm_dp(ram, r, OAM_CHANNEL));
    wr8(ram, r, MDMAEN, (uint8_t)r->a);
    *hm_dp(ram, r, OAM_CHANNEL) = 0;
    wr8(ram, r, MDMAEN, 0);
    jsl(ram, r, 0x86C8, 0x85, hm_CopySpritePiecesToVRAM);
    /* CopySpritePiecesToVRAM returns with a 16-bit A, so these two STZ are
     * 16-bit: $9B goes too, and $420C (HDMAEN): HDMA is switched off */
    hm_dp_w16(ram, r, OAM_CHANNEL, 0);
    wr16(ram, r, MDMAEN, 0);
  }
  jsr(ram, r, 0x86D1, hm_ReadJoypad);
  jsl(ram, r, 0x86D4, 0x83, hm_GetRNG);
  for (uint16_t i = 0; i < 12; i++) {                      /* BG1HOFS .. BG3VOFS, low then high */
    set_a8(r, hm_bus_rd8(ram, db(r, (uint16_t)(CAMERA_BG + i))));
    wr8(ram, r, (uint16_t)(BG1HOFS + i / 2), (uint8_t)r->a);
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
  *hm_dp(ram, r, DMA_BBAD) = BBAD_VRAM;
  r->y = size;
  r->x = vram;
  hm_dp_w16(ram, r, SOURCE, (uint16_t)src);
  *hm_dp(ram, r, SOURCE_BANK) = (uint8_t)(src >> 16);
  r->a = 0x0080;                                           /* VMAIN */
  jsl(ram, r, at_add, 0x80, hm_AddProgrammedDMA);
  jsl(ram, r, at_start, 0x80, hm_StartLastPreparedDMA);
}

void hm_LoadDialogueBox(uint8_t *ram, HmRegs *r)
{
  hm_dp_w16(ram, r, SOURCE, (uint16_t)BOX_TILEMAP);
  hm_dp_w16(ram, r, UNPACK_TO, (uint16_t)DECOMP_BUFFER);
  *hm_dp(ram, r, SOURCE_BANK) = (uint8_t)(BOX_TILEMAP >> 16);
  *hm_dp(ram, r, UNPACK_TO_BANK) = (uint8_t)(DECOMP_BUFFER >> 16);
  r->a = 0x207E;                                           /* LDA #$2000; ...; LDA #$7E */
  jsl(ram, r, 0x92D3, 0x83, hm_DecompressTileMap);
  vram_copy(ram, r, DECOMP_BUFFER, VRAM_BOX_MAP, 0x0400, 0x92F9, 0x92FD);
  vram_copy(ram, r, BLANK_LETTERS, VRAM_LETTERS, 0x1000, 0x9324, 0x9328);
}

/* ─────────────────────────────────────────────────────────────────────────
 * DrawTileBlock3x2 ($82:E5E7, JSR, RTS; the title and name screens): draws a
 * 3 x 2 block of tiles. A = which block: its VRAM address from the table at
 * $82:E73E ($80); Y = 0 or not ($84): the second set of tiles. For each of
 * the two rows ($82 = 0, 1; the row's address + $400 each time) three DMAs
 * of $24 bytes from $82:E666 + $7E ($7E = 0 or $6C, + $24 each) to VRAM
 * address, + $20 each.
 * Leftovers: A = 2, C = 1 (CMP #2), X = the last address + $60, Y = $24;
 * $7E, $80, $82, $84 as they ended.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_DrawTileBlock3x2(uint8_t *ram, HmRegs *r)
{
  r->x = (uint16_t)(r->a << 1);
  hm_dp_w16(ram, r, BLOCK_VRAM, hm_bus_rd16(ram, BLOCK_PLACES + r->x));
  hm_dp_w16(ram, r, BLOCK_ROW, 0);
  hm_dp_w16(ram, r, BLOCK_SECOND, r->y);
  for (;;) {
    r->x = hm_dp_r16(ram, r, BLOCK_VRAM);
    r->y = 0x0024;
    hm_dp_w16(ram, r, BLOCK_SOURCE, hm_dp_r16(ram, r, BLOCK_SECOND) ? 0x006C : 0);
    for (uint16_t i = 0; i < 3; i++) {
      push16(ram, r, i);                                   /* PHA */
      *hm_dp(ram, r, DMA_CHANNEL) = 0;
      *hm_dp(ram, r, DMA_BBAD) = BBAD_VRAM;
      uint16_t src = (uint16_t)((uint16_t)BLOCK_TILES + hm_dp_r16(ram, r, BLOCK_SOURCE));
      hm_dp_w16(ram, r, SOURCE, src);
      *hm_dp(ram, r, SOURCE_BANK) = (uint8_t)(BLOCK_TILES >> 16);
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
      hm_dp_w16(ram, r, BLOCK_SOURCE, (uint16_t)(hm_dp_r16(ram, r, BLOCK_SOURCE) + 0x24));
      r->a = (uint16_t)(pull16(ram, r) + 1);               /* PLA; INC */
      r->c = r->a >= 3;
    }
    hm_dp_w16(ram, r, BLOCK_VRAM, (uint16_t)(hm_dp_r16(ram, r, BLOCK_VRAM) + 0x400));
    r->a = (uint16_t)(hm_dp_r16(ram, r, BLOCK_ROW) + 1);
    hm_dp_w16(ram, r, BLOCK_ROW, r->a);
    r->c = r->a >= 2;
    if (r->a == 2) break;
  }
}

/* ─────────────────────────────────────────────────────────────────────────
 * DrawNameEntryBlanks ($82:EA80, JSL, RTL; the name screen): four letters $A8 drawn
 * (DrawLetter, X = 1) at the positions $82:EB4D, + $10 each ($0185 = the
 * position), each sent at once (StartProgrammedDMA).
 * Leftovers: X = 4, C = 1 (CPX #4), A = $82 = the last position + $10, Y
 * that of the calls.
 * ───────────────────────────────────────────────────────────────────────── */
void hm_DrawNameEntryBlanks(uint8_t *ram, HmRegs *r)
{
  hm_dp_w16(ram, r, BLANK_AT, hm_bus_rd16(ram, NAME_LETTER_AT));
  for (uint16_t i = 0; i < 4; i++) {
    push16(ram, r, i);                                     /* PHX */
    hm_bus_wr16(ram, TEXT_AT, hm_dp_r16(ram, r, BLANK_AT));
    r->a = 0x00A8;
    r->x = 0x0001;
    jsl(ram, r, 0xEA9A, 0x83, hm_DrawLetter);
    jsl(ram, r, 0xEA9E, 0x80, hm_StartProgrammedDMA);
    r->a = (uint16_t)(hm_dp_r16(ram, r, BLANK_AT) + 0x10);
    hm_dp_w16(ram, r, BLANK_AT, r->a);
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
 *   DrawScreenBar ($82:EAB4, JSR, RTS): one bar: the first tile at
 *     $82:EB4B, the address at $82:EB49, the columns at $82:EB55.
 *   DrawScreenBars ($82:E742, JSR, RTS): eight bars, from the tables at
 *     $82:F268 (tiles), $82:F258 (addresses), $82:F288 (columns).
 * Leftovers: A = Y = $88 = the bar's columns, C = 1 (CPY); X = 8 (from
 * StartProgrammedDMA) or, for the eight bars, $10.
 * ───────────────────────────────────────────────────────────────────────── */
static void bar_start(uint8_t *ram, HmRegs *r, uint16_t tile, uint16_t vram)
{
  hm_dp_w16(ram, r, BAR_TILE, tile);
  hm_dp_w16(ram, r, BAR_TILE_1, (uint16_t)(tile + 1));
  hm_dp_w16(ram, r, BAR_TILE_10, (uint16_t)(tile + 0x10));
  hm_dp_w16(ram, r, BAR_TILE_11, (uint16_t)(tile + 0x11));
  hm_dp_w16(ram, r, BAR_VRAM, vram);
}
/* one column; the AddProgrammedDMA / StartProgrammedDMA calls at `at` */
static void bar_column(uint8_t *ram, HmRegs *r, const uint16_t at[3])
{
  for (int half = 0; half < 2; half++) {
    *hm_dp(ram, r, DMA_CHANNEL) = (uint8_t)half;
    *hm_dp(ram, r, DMA_BBAD) = BBAD_VRAM;
    r->y = 0x0004;
    r->x = (uint16_t)(hm_dp_r16(ram, r, BAR_VRAM) + 0x20 * half);
    hm_dp_w16(ram, r, SOURCE, (uint16_t)(BAR_TILE + 4 * half));   /* $80:007E / $80:0082 */
    *hm_dp(ram, r, SOURCE_BANK) = 0x80;
    r->a = 0x0080;
    jsl(ram, r, at[half], 0x80, hm_AddProgrammedDMA);
  }
  jsl(ram, r, at[2], 0x80, hm_StartProgrammedDMA);
  for (unsigned v = BAR_TILE; v <= BAR_VRAM; v += 2)
    hm_dp_w16(ram, r, v, (uint16_t)(hm_dp_r16(ram, r, v) + 2));
}

void hm_DrawScreenBar(uint8_t *ram, HmRegs *r)
{
  static const uint16_t at[3] = { 0xEAF3, 0xEB1E, 0xEB22 };
  bar_start(ram, r, hm_bus_rd16(ram, BAR_TILE_AT), hm_bus_rd16(ram, BAR_VRAM_AT));
  uint16_t columns = hm_bus_rd16(ram, BAR_COLUMNS_AT);
  uint16_t y = 0;
  do {
    push16(ram, r, y);                                     /* PHY */
    bar_column(ram, r, at);
    y = (uint16_t)(pull16(ram, r) + 1);                    /* PLY; INY */
    r->y = y;
    r->a = columns;
    hm_dp_w16(ram, r, BAR_COLUMNS, columns);
  } while (y != columns);
  r->c = 1;
}

void hm_DrawScreenBars(uint8_t *ram, HmRegs *r)
{
  static const uint16_t at[3] = { 0xE787, 0xE7B2, 0xE7B6 };
  for (uint16_t x = 0; x != 0x10; x += 2) {
    bar_start(ram, r, hm_bus_rd16(ram, BARS_TILE + x), hm_bus_rd16(ram, BARS_VRAM + x));
    uint16_t columns = hm_bus_rd16(ram, BARS_COLUMNS + x);
    uint16_t y = 0;
    do {
      push16(ram, r, y);                                   /* PHY; PHX */
      push16(ram, r, x);
      bar_column(ram, r, at);
      pull16(ram, r);                                      /* PLX; PLY; INY */
      y = (uint16_t)(pull16(ram, r) + 1);
      r->y = y;
      r->a = columns;
      hm_dp_w16(ram, r, BAR_COLUMNS, columns);
    } while (y != columns);
    r->x = (uint16_t)(x + 2);
  }
  r->c = 1;
}

/* ─────────────────────────────────────────────────────────────────────────
 * DrawSaveFileLine ($82:E405, JSR, RTS): one saved game's line on the file
 * screen; A = the line (slot), Y as DrawTileBlock3x2 wants it. The slot's summary
 * is read (LoadGameSlotResume); an empty one (X = 0) is drawn as a blank
 * block (JML DrawTileBlock3x2). Otherwise, at the positions of the line's 8-byte
 * entry at $82:F278 (the text position goes in $0185, + 8 a letter):
 *   +0 the player's name, 4 letters ($0881-$0884);
 *   +2 the year (+ 1, PrintDigit) and its two-letter suffix ($82:F298);
 *   +4 the day, two digits (PrintDigit with $018C = 1, then 0, + 8);
 *   +6 the season's name (LoadDateNames) and its 6 letters ($08B3-$08BD).
 * Every letter or digit is sent at once (StartProgrammedDMA).
 * Leftovers: those of the last StartProgrammedDMA; the stack keeps the
 * pushed entry offsets.
 * ───────────────────────────────────────────────────────────────────────── */
static void letter_now(uint8_t *ram, HmRegs *r, uint16_t letter, uint16_t at)
{
  r->a = letter;
  r->x = 0;
  jsl(ram, r, at, 0x83, hm_DrawLetter);
  jsl(ram, r, (uint16_t)(at + 4), 0x80, hm_StartProgrammedDMA);
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
  wr8(ram, r, HM_PRINT_LEFT, which);
  uint8_t flags = (uint8_t)(hm_bus_rd8(ram, db(r, HM_TEXT_BOX_STATE)) & 0x7F);
  wr8(ram, r, HM_TEXT_BOX_STATE, flags);
  r->a = (uint16_t)((hm_bus_rd16(ram, TEXT_AT) & 0xFF00) | flags);
  jsl(ram, r, at, 0x83, hm_PrintDigit);
  jsl(ram, r, (uint16_t)(at + 4), 0x80, hm_StartProgrammedDMA);
}

void hm_DrawSaveFileLine(uint8_t *ram, HmRegs *r)
{
  uint16_t a = r->a, y = r->y;
  push16(ram, r, a);                                       /* PHA; PHY */
  push16(ram, r, y);
  jsl(ram, r, 0xE409, 0x83, hm_LoadGameSlotResume);
  r->y = pull16(ram, r);                                   /* PLY; PLA */
  r->a = pull16(ram, r);
  r->c = r->x >= 0;                                        /* CPX #0 */
  if (r->x == 0) { hm_DrawTileBlock3x2(ram, r); return; }        /* JML: an empty slot */

  uint16_t entry = (uint16_t)(r->a << 3);
  push16(ram, r, entry);                                   /* PHX */
  hm_dp_w16(ram, r, NAME_AT, hm_bus_rd16(ram, SAVE_LINES + entry));
  for (uint16_t i = 0; i < 4; i++) {                       /* the name */
    push16(ram, r, i);
    hm_bus_wr16(ram, TEXT_AT, hm_dp_r16(ram, r, NAME_AT));
    letter_now(ram, r, hm_bus_rd8(ram, db(r, (uint16_t)(HM_PLAYER_NAME + i))), 0xE43C);
    hm_dp_w16(ram, r, NAME_AT, (uint16_t)(hm_dp_r16(ram, r, NAME_AT) + 8));
    pull16(ram, r);
  }
  pull16(ram, r);                                          /* PLX; INX; INX */
  entry = (uint16_t)(entry + 2);
  push16(ram, r, entry);
  hm_bus_wr16(ram, TEXT_AT, hm_bus_rd16(ram, SAVE_LINES + entry));
  uint8_t year = hm_bus_rd8(ram, WRAM_LONG(HM_YEAR));
  wr8(ram, r, HM_PRINT_NUMBER, (uint8_t)(year + 1));
  wr16(ram, r, HM_PRINT_NUMBER + 1, 0);
  digit_now(ram, r, 0, 0xE481);
  next_letter(ram);
  uint16_t suffix[2] = { hm_bus_rd16(ram, YEAR_SUFFIXES + 4u * year), hm_bus_rd16(ram, YEAR_SUFFIXES + 4u * year + 2) };
  if (g_hm_year_suffix) {                                  /* past "3rd" (native: the endless game) */
    suffix[0] = g_hm_year_suffix((uint16_t)(4u * year), suffix[0]);
    suffix[1] = g_hm_year_suffix((uint16_t)(4u * year + 2), suffix[1]);
  }
  letter_now(ram, r, suffix[0], 0xE4AE);
  next_letter(ram);
  letter_now(ram, r, suffix[1], 0xE4DF);
  pull16(ram, r);
  entry = (uint16_t)(entry + 2);
  push16(ram, r, entry);
  hm_bus_wr16(ram, TEXT_AT, hm_bus_rd16(ram, SAVE_LINES + entry));
  wr8(ram, r, HM_PRINT_NUMBER, hm_bus_rd8(ram, WRAM_LONG(HM_DAY)));
  wr16(ram, r, HM_PRINT_NUMBER + 1, 0);
  digit_now(ram, r, 1, 0xE512);
  pull16(ram, r);                                          /* PLX; PHX */
  push16(ram, r, entry);
  uint32_t at8 = (uint32_t)hm_bus_rd16(ram, SAVE_LINES + entry) + 8;
  r->c = at8 > 0xFFFF;
  hm_bus_wr16(ram, TEXT_AT, (uint16_t)at8);
  r->a = (uint16_t)(at8 & 0xFF00);                         /* SEP; LDA #0 */
  wr8(ram, r, HM_PRINT_LEFT, 0);
  jsl(ram, r, 0xE531, 0x83, hm_PrintDigit);
  jsl(ram, r, 0xE535, 0x80, hm_StartProgrammedDMA);
  jsl(ram, r, 0xE53B, 0x82, hm_LoadDateNames);
  pull16(ram, r);
  entry = (uint16_t)(entry + 2);
  hm_bus_wr16(ram, TEXT_AT, hm_bus_rd16(ram, SAVE_LINES + entry));
  for (uint16_t i = 0; i < 6; i++) {                       /* the season's name */
    if (i) next_letter(ram);
    letter_now(ram, r, rd16_db(ram, r, (uint16_t)(HM_SEASON_TEXT + 2 * i)), (uint16_t)(0xE552 + 0x1C * i));
  }
}

/* ─────────────────────────────────────────────────────────────────────────
 * BackgroundsManager ($80:A7C6, JSL, RTL): loads the picture of map $22.
 * Maps_Graphics_Table ($80:AA7C) points, through the data bank, at the
 * map's record ($18 = the pointer); it reads, in order:
 *   - maps below $57 (the rest are the title and splash screens):
 *       the graphics preset (to $0195, then ManageGraphicPresets),
 *       a word ORed into the flags $0196 (bit 5 dropped again if bit 0 of
 *       $7F1F5C is set);
 *   - $0181 (the size of the layers, an index for the tables below) and
 *     $0182 (3 or more sets bit 0 of $0196);
 *   - $92 = how many tilemaps, $93 = how many layer pictures;
 *   - maps below $57: the edges the sprites are kept inside ($ED, $F1,
 *     $EF, $F3);
 *   - for each tilemap: the VRAM address and the long address of the
 *     packed data; it is unpacked (DecompressTileMap) to $7E:2000 and
 *     copied whole ($2000 bytes) to VRAM on channel 0;
 *   - for each layer picture: its VRAM address ($8A) and long address. It
 *     is unpacked to $7E:2000 (or, if bit 15 of $0196 is set, copied as it
 *     is, $8000 bytes); the farms (maps 0-3) get their fields and houses
 *     drawn on it (DrawWholeMap); a map with one picture also gets its
 *     extras (RunMapExtras) unless bit 1 of $7F1F5E is set.
 *     Then, with the position, the scroll and the subpixels cleared, it
 *     goes to VRAM as 2 x 32 x 2 rows of $40 bytes (CopyMapPictureToVRAM);
 *     $1A / $1C come from the tables $80:AA68 / $80:AA72 (by $0181), and
 *     $80 = $1A - $40 is the step between the row pairs. For the maps below
 *     $5B whose picture goes to $7000 the second row of each pair, and the
 *     gap between the halves, are skipped.
 * Last, for maps below $57, the camera is walked to where it should be
 * ($017D, $017F) 8 pixels at a time (by the word at $1E, its low byte just
 * set to 8), each step drawing the new column (ScrollCameraRight) or
 * row (UpdateBGOffset) and sending it (StartProgrammedDMA).
 * Leftovers: C = 1; A, X and Y those of the last step.
 * ───────────────────────────────────────────────────────────────────────── */
static uint32_t record_at(uint8_t *ram, const HmRegs *r)    /* ($18),Y */
{
  return ((uint32_t)r->db << 16) + hm_dp_r16(ram, r, MAP_RECORD) + r->y;
}
static uint8_t rec8(uint8_t *ram, HmRegs *r) { return hm_bus_rd8(ram, record_at(ram, r) & 0xFFFFFF); }
static uint16_t rec16(uint8_t *ram, HmRegs *r) { return hm_bus_rd16(ram, record_at(ram, r) & 0xFFFFFF); }

/* $72-$74 = the long address in the record, $75-$77 = $7E:2000; A = $207E */
static void packed_source(uint8_t *ram, HmRegs *r)
{
  hm_dp_w16(ram, r, SOURCE, rec16(ram, r));
  r->y = (uint16_t)(r->y + 2);
  *hm_dp(ram, r, SOURCE_BANK) = rec8(ram, r);
  r->y++;
  push16(ram, r, r->y);                                    /* PHY */
  hm_dp_w16(ram, r, UNPACK_TO, (uint16_t)DECOMP_BUFFER);
  *hm_dp(ram, r, UNPACK_TO_BANK) = (uint8_t)(DECOMP_BUFFER >> 16);
  r->a = 0x207E;
}

/* the camera walk: + the word at $1E until $at equals the target */
static void walk_camera(uint8_t *ram, HmRegs *r, uint8_t at, uint16_t target, uint16_t call,
                        void (*draw)(uint8_t *, HmRegs *))
{
  for (;;) {
    uint16_t now = hm_dp_r16(ram, r, at), to = rd16_db(ram, r, target);
    r->a = now;
    r->c = now >= to;                                      /* CMP */
    if (now == to) return;
    uint32_t sum = (uint32_t)now + hm_dp_r16(ram, r, CAMERA_SPEED);
    r->c = sum > 0xFFFF;
    r->a = (uint16_t)sum;
    hm_dp_w16(ram, r, at, r->a);
    jsl(ram, r, call, 0x80, hm_PlaceCameraOnMapEntry);
    jsl(ram, r, (uint16_t)(call + 4), 0x80, draw);
    jsl(ram, r, (uint16_t)(call + 8), 0x80, hm_StartProgrammedDMA);
  }
}

void hm_BackgroundsManager(uint8_t *ram, HmRegs *r)
{
  uint8_t map = *hm_dp(ram, r, HM_MAP_ID);
  r->x = (uint16_t)(map << 1);                             /* ASL; TAX */
  r->c = 0;
  r->a = hm_bus_rd16(ram, MAPS_GRAPHICS_TABLE + r->x);
  hm_dp_w16(ram, r, MAP_RECORD, r->a);
  r->y = 0;
  set_a8(r, map);
  r->c = map >= HM_MAP_FIRST_PICTURE;                      /* CMP #$57 */
  if (map < HM_MAP_FIRST_PICTURE) {
    set_a8(r, rec8(ram, r));
    wr8(ram, r, HM_GRAPHIC_PRESET, (uint8_t)r->a);         /* the graphics preset */
    jsl(ram, r, 0xA7E9, 0x80, hm_ManageGraphicPresets);
    r->y++;
    r->a = (uint16_t)(rec16(ram, r) | rd16_db(ram, r, HM_WEATHER_FLAGS));
    wr16(ram, r, HM_WEATHER_FLAGS, r->a);
    r->a = hm_bus_rd16(ram, WRAM_LONG(HM_FLAGS_5C)) & 0x0001;
    if (r->a) {
      r->a = rd16_db(ram, r, HM_WEATHER_FLAGS) & 0xFFDF;
      wr16(ram, r, HM_WEATHER_FLAGS, r->a);
    }
    r->y = (uint16_t)(r->y + 2);
  }
  set_a8(r, rec8(ram, r));
  wr8(ram, r, HM_MAP_WIDTH, (uint8_t)r->a);                /* the layer size */
  r->y++;
  set_a8(r, rec8(ram, r));
  wr8(ram, r, LAYER_KIND, (uint8_t)r->a);
  r->c = (uint8_t)r->a >= 3;
  if (r->c) {
    r->a = rd16_db(ram, r, HM_WEATHER_FLAGS) | 0x0001;
    wr16(ram, r, HM_WEATHER_FLAGS, r->a);
  }
  r->y++;
  set_a8(r, rec8(ram, r));
  *hm_dp(ram, r, TILEMAPS_LEFT) = (uint8_t)r->a;           /* tilemaps */
  r->y++;
  set_a8(r, rec8(ram, r));
  *hm_dp(ram, r, PICTURES_LEFT) = (uint8_t)r->a;           /* layer pictures */
  r->y++;
  set_a8(r, map);
  r->c = map >= HM_MAP_FIRST_PICTURE;
  if (map < HM_MAP_FIRST_PICTURE) {                        /* the sprite edges */
    static const uint8_t edges[4] = { HM_CAM_MIN_X, HM_CAM_MAX_X, HM_CAM_MIN_Y, HM_CAM_MAX_Y };
    for (int i = 0; i < 4; i++) {
      r->a = rec16(ram, r);
      hm_dp_w16(ram, r, edges[i], r->a);
      r->y = (uint16_t)(r->y + 2);
    }
  }

  set_a8(r, *hm_dp(ram, r, TILEMAPS_LEFT));
  if (*hm_dp(ram, r, TILEMAPS_LEFT)) {
    do {                                                   /* the tilemaps */
      push16(ram, r, rec16(ram, r));                       /* the VRAM address */
      r->y = (uint16_t)(r->y + 2);
      packed_source(ram, r);
      jsl(ram, r, 0xA883, 0x83, hm_DecompressTileMap);
      set_a8(r, 0x00);
      *hm_dp(ram, r, DMA_CHANNEL) = 0;
      set_a8(r, BBAD_VRAM);
      *hm_dp(ram, r, DMA_BBAD) = BBAD_VRAM;
      r->y = pull16(ram, r);                               /* PLY; PLA; PHY */
      r->a = pull16(ram, r);
      push16(ram, r, r->y);
      r->x = r->a;
      r->y = 0x2000;
      hm_dp_w16(ram, r, SOURCE, (uint16_t)DECOMP_BUFFER);
      *hm_dp(ram, r, SOURCE_BANK) = (uint8_t)(DECOMP_BUFFER >> 16);
      r->a = 0x0080;
      jsl(ram, r, 0xA8AA, 0x80, hm_AddProgrammedDMA);
      jsl(ram, r, 0xA8AE, 0x80, hm_StartLastPreparedDMA);
      r->y = pull16(ram, r);
      uint8_t left = (uint8_t)(*hm_dp(ram, r, TILEMAPS_LEFT) - 1);
      set_a8(r, left);
      *hm_dp(ram, r, TILEMAPS_LEFT) = left;
    } while (*hm_dp(ram, r, TILEMAPS_LEFT));
  }

  for (;;) {                                               /* the layer pictures */
    r->a = rec16(ram, r);
    hm_dp_w16(ram, r, PICTURE_VRAM, r->a);                 /* its VRAM address */
    r->y = (uint16_t)(r->y + 2);
    packed_source(ram, r);
    r->a = rd16_db(ram, r, HM_WEATHER_FLAGS) & 0x8000;
    if (!r->a) {
      jsl(ram, r, 0xA8EB, 0x83, hm_DecompressTileMap);
    } else {                                               /* stored as it is */
      uint32_t from = hm_dp_r16(ram, r, SOURCE) | (uint32_t)*hm_dp(ram, r, SOURCE_BANK) << 16;
      uint32_t to = hm_dp_r16(ram, r, UNPACK_TO) | (uint32_t)*hm_dp(ram, r, UNPACK_TO_BANK) << 16;
      for (r->y = 0; r->y != 0x8000; r->y = (uint16_t)(r->y + 2)) {
        r->a = hm_bus_rd16(ram, (from + r->y) & 0xFFFFFF);
        hm_bus_wr16(ram, (to + r->y) & 0xFFFFFF, r->a);
      }
      r->c = 1;
    }
    set_a8(r, map);
    r->c = map >= 0x04;
    if (map < 0x04) jsl(ram, r, 0xA909, 0x82, hm_DrawWholeMap);  /* a farm */
    set_a8(r, *hm_dp(ram, r, PICTURES_LEFT));
    r->c = *hm_dp(ram, r, PICTURES_LEFT) >= 1;
    if (*hm_dp(ram, r, PICTURES_LEFT) == 1) {
      r->a = hm_bus_rd16(ram, WRAM_LONG(HM_FLAGS_5E)) & 0x0002;
      if (!r->a) jsl(ram, r, 0xA920, 0x81, hm_RunMapExtras);
    }
    hm_dp_w16(ram, r, HM_CAMERA_X, 0);
    hm_dp_w16(ram, r, HM_CAMERA_Y, 0);
    hm_dp_w16(ram, r, VRAM_COLUMN, 0x0410);
    hm_dp_w16(ram, r, VRAM_ROW, 0x0A00);
    hm_dp_w16(ram, r, WRAM_COLUMN, 0);
    hm_dp_w16(ram, r, WRAM_ROW, 0);
    *hm_dp(ram, r, PIXELS_IN_TILE_X) = 0;
    *hm_dp(ram, r, PIXELS_IN_TILE_Y) = 0;
    hm_dp_w16(ram, r, HM_PLAYER_X, 0);
    hm_dp_w16(ram, r, HM_PLAYER_Y, 0);
    r->x = (uint16_t)(hm_bus_rd8(ram, db(r, HM_MAP_WIDTH)) << 1);
    r->a = hm_bus_rd16(ram, LAYER_SIZES + r->x);
    hm_dp_w16(ram, r, MAP_ROW_LENGTH, r->a);
    r->c = r->a >= 0x0040;                                 /* SEC; SBC */
    r->a = (uint16_t)(r->a - 0x0040);
    hm_dp_w16(ram, r, ROW_STEP, r->a);
    r->a = hm_bus_rd16(ram, LAYER_SIZES_2 + r->x);
    hm_dp_w16(ram, r, CAMERA_IN_ROW, r->a);
    hm_dp_w16(ram, r, PICTURE_AT, 0);
    r->x = hm_dp_r16(ram, r, PICTURE_VRAM);
    r->y = 0x0040;
    /* skip the second row of each pair (and the gap) */
    bool short_rows = map < 0x5B;
    for (uint16_t half = 0; half < 2; half++) {
      r->a = half;
      push16(ram, r, half);
      for (uint16_t row = 0; row < 0x20; row++) {
        r->a = row;
        push16(ram, r, row);
        jsr(ram, r, 0xA97A, hm_CopyMapPictureToVRAM);
        uint32_t s = (uint32_t)hm_dp_r16(ram, r, PICTURE_AT) + 0x40;
        hm_dp_w16(ram, r, PICTURE_AT, (uint16_t)s);
        s = (uint32_t)r->x + 0x400;
        r->a = r->x = (uint16_t)s;
        r->c = s > 0xFFFF;
        set_a8(r, map);
        r->c = map >= 0x5B;
        bool skip = false;
        if (short_rows) {
          r->y = hm_dp_r16(ram, r, PICTURE_VRAM);
          r->c = r->y >= VRAM_SHORT;
          skip = r->y == VRAM_SHORT;
        }
        if (!skip) {
          r->y = 0x0040;
          jsr(ram, r, 0xA99F, hm_CopyMapPictureToVRAM);
        }
        s = (uint32_t)hm_dp_r16(ram, r, PICTURE_AT) + hm_dp_r16(ram, r, ROW_STEP);
        hm_dp_w16(ram, r, PICTURE_AT, (uint16_t)s);
        r->c = r->x >= 0x03E0;
        r->a = r->x = (uint16_t)(r->x - 0x03E0);
        r->y = 0x0040;
        r->a = (uint16_t)(pull16(ram, r) + 1);
        r->c = r->a >= 0x0020;
      }
      set_a8(r, map);
      r->c = map >= 0x5B;
      bool skip = false;
      if (short_rows) {
        r->y = hm_dp_r16(ram, r, PICTURE_VRAM);
        r->c = r->y >= VRAM_SHORT;
        skip = r->y == VRAM_SHORT;
      }
      if (!skip) {
        uint32_t s = (uint32_t)r->x + 0x400;
        r->a = r->x = (uint16_t)s;
        r->c = s > 0xFFFF;
      }
      r->y = 0x0040;
      r->a = (uint16_t)(pull16(ram, r) + 1);
      r->c = r->a >= 0x0002;
    }
    r->y = pull16(ram, r);                                 /* PLY */
    uint8_t left = (uint8_t)(*hm_dp(ram, r, PICTURES_LEFT) - 1);
    set_a8(r, left);
    *hm_dp(ram, r, PICTURES_LEFT) = left;
    if (!left) break;
  }

  set_a8(r, map);
  r->c = map >= HM_MAP_FIRST_PICTURE;
  if (map >= HM_MAP_FIRST_PICTURE) return;
  set_a8(r, 0x08);
  *hm_dp(ram, r, CAMERA_SPEED) = 0x08;
  walk_camera(ram, r, HM_PLAYER_X, HM_TRANSITION_X, 0xAA0B, hm_ScrollCameraRight);
  walk_camera(ram, r, HM_PLAYER_Y, HM_TRANSITION_Y, 0xAA29, hm_UpdateBGOffset);
}
