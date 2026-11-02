/*
 * modpack.c — the community's mods.
 *
 * A mod is a folder in mods/ (next to the program; on Android in the app's
 * folder on the shared storage, beside config.ini):
 *
 *   mods/
 *     My mod/
 *       mod.ini        [Mod] Name, Author, Version, Description (optional)
 *       *.ips, *.bps   patches to the game's ROM, applied in name order
 *
 * Patches are the ROM hackers' usual ones (Lunar IPS, Floating IPS, beat...)
 * and are applied to your ROM in memory when the game starts (and on
 * Restart): the file on disk is never changed. Only the ROM's data is read
 * by this program (the game's code is the decompiled C), so what a patch
 * changes in the pictures, the text, the maps, the music and the game's
 * tables shows; what it changes in the 65816 code does nothing.
 *
 * IPS patches are for the ROM without a copier's header (.sfc); a mod made
 * for a headered one (.smc) says Header = yes in mod.ini. BPS patches carry
 * the CRC of the ROM they were made for and are checked against it.
 *
 * The mods turned on are kept in config.ini ([Mods] On, their folders in
 * the order they are applied).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "config.h"
#include "modpack.h"

enum { MAX_ROM = 8 << 20 };

static ModPack s_packs[MAX_PACKS];
static int s_n;
static char s_applied[sizeof g_cfg.mods_on];     /* the list the ROM in use was made with */

const char *modpack_dir(void)
{
  static char dir[1100];
  snprintf(dir, sizeof dir, "%smods", config_dir());
  return dir;
}

static void trim(char *s)
{
  char *p = s;
  while (*p == ' ' || *p == '\t') p++;
  memmove(s, p, strlen(p) + 1);
  size_t n = strlen(s);
  while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) s[--n] = 0;
}

static void read_ini(ModPack *m)
{
  char path[1400];
  snprintf(path, sizeof path, "%s/%s/mod.ini", modpack_dir(), m->folder);
  FILE *f = fopen(path, "r");
  if (!f) return;
  char line[600];
  bool first = true;
  while (fgets(line, sizeof line, f)) {
    if (first && (uint8_t)line[0] == 0xEF && (uint8_t)line[1] == 0xBB && (uint8_t)line[2] == 0xBF) memmove(line, line + 3, strlen(line + 3) + 1);
    first = false;
    trim(line);
    if (!line[0] || line[0] == '#' || line[0] == ';' || line[0] == '[') continue;
    char *eq = strchr(line, '=');
    if (!eq) continue;
    *eq = 0;
    char *k = line, *v = eq + 1;
    trim(k); trim(v);
    if (!SDL_strcasecmp(k, "name") && *v) snprintf(m->name, sizeof m->name, "%s", v);
    else if (!SDL_strcasecmp(k, "author")) snprintf(m->author, sizeof m->author, "%s", v);
    else if (!SDL_strcasecmp(k, "version")) snprintf(m->version, sizeof m->version, "%s", v);
    else if (!SDL_strcasecmp(k, "description")) snprintf(m->desc, sizeof m->desc, "%s", v);
    else if (!SDL_strcasecmp(k, "header")) m->header = !SDL_strcasecmp(v, "yes") || !strcmp(v, "1") || !SDL_strcasecmp(v, "true");
  }
  fclose(f);
}

/* is `folder` in the ';' list? its place (0...), or -1 */
static int place_in(const char *list, const char *folder)
{
  int i = 0;
  const char *p = list;
  size_t n = strlen(folder);
  while (*p) {
    const char *e = strchr(p, ';');
    size_t len = e ? (size_t)(e - p) : strlen(p);
    if (len == n && !strncmp(p, folder, n)) return i;
    i++;
    if (!e) break;
    p = e + 1;
  }
  return -1;
}

static int by_name(const void *a, const void *b) { return SDL_strcasecmp(((const ModPack *)a)->folder, ((const ModPack *)b)->folder); }

int modpack_scan(void)
{
  s_n = 0;
  int count = 0;
  char **names = SDL_GlobDirectory(modpack_dir(), "*", 0, &count);
  for (int i = 0; names && i < count && s_n < MAX_PACKS; i++) {
    char path[1400];
    snprintf(path, sizeof path, "%s/%s", modpack_dir(), names[i]);
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path, &info) || info.type != SDL_PATHTYPE_DIRECTORY) continue;
    if (strchr(names[i], '/') || strchr(names[i], '\\') || strchr(names[i], ';')) continue;
    ModPack *m = &s_packs[s_n];
    memset(m, 0, sizeof *m);
    snprintf(m->folder, sizeof m->folder, "%s", names[i]);
    snprintf(m->name, sizeof m->name, "%s", names[i]);
    read_ini(m);
    int np = 0;
    char **ips = SDL_GlobDirectory(path, "*.ips", SDL_GLOB_CASEINSENSITIVE, &np);
    m->patches = np;
    SDL_free(ips);
    char **bps = SDL_GlobDirectory(path, "*.bps", SDL_GLOB_CASEINSENSITIVE, &np);
    m->patches += np;
    SDL_free(bps);
    s_n++;
  }
  SDL_free(names);
  qsort(s_packs, (size_t)s_n, sizeof s_packs[0], by_name);
  for (int i = 0; i < s_n; i++) s_packs[i].on = place_in(g_cfg.mods_on, s_packs[i].folder) >= 0;
  return s_n;
}
int modpack_count(void) { return s_n; }
const ModPack *modpack_get(int i) { return i >= 0 && i < s_n ? &s_packs[i] : NULL; }

void modpack_set(int i, bool on)
{
  if (i < 0 || i >= s_n) return;
  ModPack *m = &s_packs[i];
  char out[sizeof g_cfg.mods_on] = "";
  /* the list without it, then (when on) it at the end */
  const char *p = g_cfg.mods_on;
  while (*p) {
    const char *e = strchr(p, ';');
    size_t len = e ? (size_t)(e - p) : strlen(p);
    if (len && !(len == strlen(m->folder) && !strncmp(p, m->folder, len))) {
      size_t o = strlen(out);
      if (o + len + 2 < sizeof out) { if (o) out[o++] = ';'; memcpy(out + o, p, len); out[o + len] = 0; }
    }
    if (!e) break;
    p = e + 1;
  }
  if (on) {
    size_t o = strlen(out);
    if (o + strlen(m->folder) + 2 < sizeof out) { if (o) strcat(out, ";"); strcat(out, m->folder); }
  }
  snprintf(g_cfg.mods_on, sizeof g_cfg.mods_on, "%s", out);
  m->on = on;
  config_save();
}

bool modpack_changed(void) { return strcmp(s_applied, g_cfg.mods_on) != 0; }

/* ── The patches ───────────────────────────────────────────────────────── */
static uint8_t *read_file(const char *path, size_t *n)
{
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
  if (len <= 0 || len > MAX_ROM * 2) { fclose(f); return NULL; }
  uint8_t *d = malloc((size_t)len);
  if (d && fread(d, 1, (size_t)len, f) != (size_t)len) { free(d); d = NULL; }
  fclose(f);
  *n = (size_t)len;
  return d;
}
static uint32_t crc32(const uint8_t *d, size_t n)
{
  static uint32_t t[256];
  if (!t[1]) for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; t[i] = c; }
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++) c = t[(c ^ d[i]) & 255] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}
/* the ROM grown to `need` bytes (new bytes 0) */
static bool grow(uint8_t **rom, uint32_t *size, size_t need)
{
  if (need <= *size) return true;
  if (need > MAX_ROM) return false;
  uint8_t *r = realloc(*rom, need);
  if (!r) return false;
  memset(r + *size, 0, need - *size);
  *rom = r; *size = (uint32_t)need;
  return true;
}

static const char *apply_ips(uint8_t **rom, uint32_t *size, const uint8_t *p, size_t n, long shift)
{
  if (n < 8 || memcmp(p, "PATCH", 5)) return "not an IPS patch";
  size_t i = 5;
  for (;;) {
    if (i + 3 > n) return "the patch ends too soon";
    uint32_t off = (uint32_t)p[i] << 16 | (uint32_t)p[i + 1] << 8 | p[i + 2];
    i += 3;
    if (off == 0x454F46) {                       /* "EOF", then perhaps the new size */
      if (i + 3 <= n) {
        uint32_t cut = (uint32_t)p[i] << 16 | (uint32_t)p[i + 1] << 8 | p[i + 2];
        long c = (long)cut + shift;
        if (c > 0 && (uint32_t)c < *size) *size = (uint32_t)c;
      }
      return NULL;
    }
    if (i + 2 > n) return "the patch ends too soon";
    uint32_t len = (uint32_t)p[i] << 8 | p[i + 1];
    i += 2;
    long at = (long)off + shift;
    if (len) {
      if (i + len > n) return "the patch ends too soon";
      if (at < 0) { i += len; continue; }       /* into the copier's header: nothing there */
      if (!grow(rom, size, (size_t)at + len)) return "the ROM would be too big";
      memcpy(*rom + at, p + i, len);
      i += len;
    } else {                                      /* a run of one byte */
      if (i + 3 > n) return "the patch ends too soon";
      uint32_t count = (uint32_t)p[i] << 8 | p[i + 1];
      uint8_t v = p[i + 2];
      i += 3;
      if (at < 0) continue;
      if (!grow(rom, size, (size_t)at + count)) return "the ROM would be too big";
      memset(*rom + at, v, count);
    }
  }
}

static uint64_t bps_number(const uint8_t *p, size_t n, size_t *i, bool *bad)
{
  uint64_t data = 0, shift = 1;
  for (;;) {
    if (*i >= n || shift > (1ull << 56)) { *bad = true; return 0; }
    uint8_t x = p[(*i)++];
    data += (x & 0x7F) * shift;
    if (x & 0x80) break;
    shift <<= 7;
    data += shift;
  }
  return data;
}
static const char *apply_bps(uint8_t **rom, uint32_t *size, const uint8_t *p, size_t n)
{
  if (n < 4 + 12 || memcmp(p, "BPS1", 4)) return "not a BPS patch";
  uint32_t want_src = (uint32_t)p[n - 12] | (uint32_t)p[n - 11] << 8 | (uint32_t)p[n - 10] << 16 | (uint32_t)p[n - 9] << 24;
  uint32_t want_dst = (uint32_t)p[n - 8] | (uint32_t)p[n - 7] << 8 | (uint32_t)p[n - 6] << 16 | (uint32_t)p[n - 5] << 24;
  uint32_t want_self = (uint32_t)p[n - 4] | (uint32_t)p[n - 3] << 8 | (uint32_t)p[n - 2] << 16 | (uint32_t)p[n - 1] << 24;
  if (crc32(p, n - 4) != want_self) return "the patch file is damaged";
  if (crc32(*rom, *size) != want_src) return "it was made for another ROM (or another mod changed it first)";
  size_t i = 4;
  bool bad = false;
  uint64_t src_size = bps_number(p, n, &i, &bad), dst_size = bps_number(p, n, &i, &bad), meta = bps_number(p, n, &i, &bad);
  if (bad || src_size != *size || dst_size > MAX_ROM || i + meta > n - 12) return "the patch is damaged";
  i += (size_t)meta;
  uint8_t *dst = calloc(1, dst_size ? (size_t)dst_size : 1);
  if (!dst) return "out of memory";
  const uint8_t *src = *rom;
  size_t out = 0;
  int64_t src_rel = 0, dst_rel = 0;
  while (i < n - 12) {
    uint64_t d = bps_number(p, n, &i, &bad);
    uint64_t len = (d >> 2) + 1;
    if (bad || out + len > dst_size) { free(dst); return "the patch is damaged"; }
    switch (d & 3) {
    case 0:                                       /* SourceRead */
      if (out + len > src_size) { free(dst); return "the patch is damaged"; }
      memcpy(dst + out, src + out, (size_t)len); out += (size_t)len; break;
    case 1:                                       /* TargetRead */
      if (i + len > n - 12) { free(dst); return "the patch is damaged"; }
      memcpy(dst + out, p + i, (size_t)len); i += (size_t)len; out += (size_t)len; break;
    case 2: case 3: {                             /* SourceCopy, TargetCopy */
      uint64_t o = bps_number(p, n, &i, &bad);
      int64_t delta = (int64_t)(o >> 1) * ((o & 1) ? -1 : 1);
      if ((d & 3) == 2) {
        src_rel += delta;
        if (bad || src_rel < 0 || (uint64_t)src_rel + len > src_size) { free(dst); return "the patch is damaged"; }
        memcpy(dst + out, src + src_rel, (size_t)len); src_rel += (int64_t)len; out += (size_t)len;
      } else {
        dst_rel += delta;
        if (bad || dst_rel < 0 || (uint64_t)dst_rel >= out) { free(dst); return "the patch is damaged"; }
        while (len--) dst[out++] = dst[dst_rel++];   /* byte by byte: it may overlap */
      }
      break;
    }
    }
  }
  if (out != dst_size || crc32(dst, (size_t)dst_size) != want_dst) { free(dst); return "the result is not what the patch expects"; }
  free(*rom);
  *rom = dst; *size = (uint32_t)dst_size;
  return NULL;
}

static int by_file(const void *a, const void *b) { return SDL_strcasecmp(*(char *const *)a, *(char *const *)b); }

int modpack_apply(uint8_t **rom, uint32_t *size, char *err, size_t n)
{
  if (err && n) err[0] = 0;
  snprintf(s_applied, sizeof s_applied, "%s", g_cfg.mods_on);
  if (!g_cfg.mods_on[0]) return 0;
  modpack_scan();
  int done = 0;
  const char *p = g_cfg.mods_on;
  while (*p) {
    const char *e = strchr(p, ';');
    char folder[128];
    size_t len = e ? (size_t)(e - p) : strlen(p);
    if (len >= sizeof folder) len = sizeof folder - 1;
    memcpy(folder, p, len); folder[len] = 0;
    p = e ? e + 1 : p + strlen(p);
    const ModPack *m = NULL;
    for (int i = 0; i < s_n; i++) if (!strcmp(s_packs[i].folder, folder)) m = &s_packs[i];
    if (!m) continue;                             /* the folder is gone */
    char dir[1400];
    snprintf(dir, sizeof dir, "%s/%s", modpack_dir(), folder);
    int count = 0;
    char **files = SDL_GlobDirectory(dir, "*", 0, &count);
    if (files) qsort(files, (size_t)count, sizeof *files, by_file);
    const char *why = NULL;
    /* the mod on a copy: when a patch of it fails the ROM stays as it was */
    uint32_t sz = *size;
    uint8_t *copy = malloc(sz ? sz : 1);
    if (!copy) why = "out of memory";
    else memcpy(copy, *rom, sz);
    for (int k = 0; files && k < count && !why; k++) {
      const char *dot = strrchr(files[k], '.');
      bool ips = dot && !SDL_strcasecmp(dot, ".ips"), bps = dot && !SDL_strcasecmp(dot, ".bps");
      if (!ips && !bps) continue;
      char path[1600];
      snprintf(path, sizeof path, "%s/%s", dir, files[k]);
      size_t pn = 0;
      uint8_t *patch = read_file(path, &pn);
      if (!patch) { why = "a patch could not be read"; break; }
      why = ips ? apply_ips(&copy, &sz, patch, pn, m->header ? -512 : 0) : apply_bps(&copy, &sz, patch, pn);
      free(patch);
    }
    if (copy && !why) { free(*rom); *rom = copy; *size = sz; }
    else free(copy);
    SDL_free(files);
    if (why) {
      if (err && n && !err[0]) snprintf(err, n, "%s: %s", m->name, why);
      SDL_Log("mod %s: %s", m->name, why);
      continue;
    }
    SDL_Log("mod %s: on", m->name);
    done++;
  }
  return done;
}
