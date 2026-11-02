/* modpack.h — the community's mods: folders in mods/ with patches (IPS,
 * BPS) for the game's ROM, applied in memory when the game starts.
 * modpack.c. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { MAX_PACKS = 32 };

typedef struct {
  char folder[128];          /* its folder in mods/ */
  char name[64], author[64], version[24], desc[240];
  int patches;               /* .ips and .bps files in it */
  bool header;               /* IPS made for a ROM with a copier's header */
  bool on;
} ModPack;

const char *modpack_dir(void);                  /* where the mods are (no slash at the end) */
int modpack_scan(void);                         /* look at the folder again: how many */
int modpack_count(void);
const ModPack *modpack_get(int i);
void modpack_set(int i, bool on);               /* turned on or off (config.ini) */
bool modpack_changed(void);                     /* the mods on differ from the ROM's in use */
/* the mods on, applied to the ROM in memory (it may grow and move): how
 * many were; the first problem in `err` (that mod is left out) */
int modpack_apply(uint8_t **rom, uint32_t *size, char *err, size_t n);
