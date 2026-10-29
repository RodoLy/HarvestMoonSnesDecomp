/* native.h — the native build's console glue (hw.c) and host (main.c). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

extern uint8_t g_ram[0x20000];

bool native_hw_init(const uint8_t *rom, uint32_t rom_size, uint8_t *sram, uint32_t sram_size);
void native_set_pads(uint16_t pad1, uint16_t pad2);
void native_draw_frame(uint8_t *pixels);
uint64_t native_apu_cycles(void);
void native_apu_run_to(uint64_t target);
uint32_t native_audio_samples(int16_t *out, int frames);
bool native_nmi_enabled(void);
extern void (*g_native_lag_frame)(void);
extern uint32_t g_native_ppu_flags;
