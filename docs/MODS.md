# Making a mod

*[Español](MODS.es.md)*

The native build loads **mods**: changes to the game's pictures, text,
maps, music or tables, made by anyone and turned on in the launcher
(**Mods**, on its first page). Your ROM file is never changed: the mods
you turn on are applied to a copy in memory when the game starts (and on
**Restart**).

## What a mod is

A folder inside `mods/`, next to the program (on Android, in the app's
folder on the shared storage, `Android/data/io.github.rodriloy.harvestmoon/files/mods/`,
beside `config.ini`):

```
mods/
  Spanish text/
    mod.ini
    spanish.ips
  Winter outfit/
    mod.ini
    outfit.bps
```

- **`mod.ini`** (optional) — what the launcher shows:

  ```ini
  [Mod]
  Name = Winter outfit
  Author = Your name
  Version = 1.0
  Description = The farmer wears a scarf and a coat all year.
  ```

  Without it the mod is listed by its folder's name.
- **`*.ips` and `*.bps`** — the patches, applied in the order of their
  file names. These are the usual ROM-hacking patches, so any patch made
  for **Harvest Moon (USA)** can be dropped in.

## Making the patch

1. Copy your ROM and change the copy with the usual tools: a tile editor
   (YY-CHR, Tile Molester) for pictures, a hex editor with the game's
   text table for text, a level editor for maps...
2. Make a patch from the original and the changed ROM with
   [Floating IPS (Flips)](https://github.com/Alcaro/Flips): **BPS** is the
   better choice (it checks that it is applied to the right ROM); **IPS**
   works too.
3. Put the patch and a `mod.ini` in a folder, the folder in `mods/`, and
   turn it on in the launcher.

The patch is all you share: never share the ROM itself.

## What works

This program is the game rewritten in C: it reads the ROM's **data** (the
graphics, the text, the maps, the palettes, the music, the game's tables)
but not its 65816 **code**. So a patch that changes data shows in the
game; whatever a patch changes in the code does nothing. Most graphics
and text hacks are data; hacks that add new code (a variable-width font,
new events) only half-work.

## Details

- IPS patches are for the ROM **without** a copier's header (`.sfc`, 2 MB).
  For a patch made for a headered ROM (`.smc`), add `Header = yes` to
  `mod.ini`.
- Mods are applied in the order you turned them on. A BPS patch checks
  the ROM it gets: when another mod changed the same ROM first, it is not
  applied and the launcher says why. IPS patches are applied as they are
  (two that change the same bytes: the later one wins).
- A mod that fails is left out; the others still apply.
- Turning mods on or off in the game takes effect at the next **Restart**
  (or the next time the program starts). Save states belong to the mods
  they were made with.
