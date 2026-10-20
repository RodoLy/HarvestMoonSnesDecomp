# HarvestMoonSnesDecomp — a decompilation of Harvest Moon (SNES)

**English** · [Español](README.es.md)

<!-- progress -->
![Decompiled: 2.49%](https://img.shields.io/badge/decompiled-2.49%25-2f855a)

**2.49%** of the game's code decompiled and verified: 755 of 30359 instructions, 15 of 372 routines (4.0%).
Computed with `python3 tools/progress.py --decomp ../HM-Decomp` over the HM-Decomp disassembly.
<!-- /progress -->

The game rewritten, routine by routine, as C a person can read. Every
rewritten routine is **checked against the original byte for byte**: the
whole memory (128 KB of WRAM, stack included) and the registers must come
out identical.

It is a sister project of the playable port
[HarvestMoonSNESRecomp](https://github.com/RodriLoy/HarvestMoonSNESRecomp)
and builds on the community disassembly
[HM-Decomp](https://github.com/ElisaS-Codes/HM-Decomp) (ElisaS-Codes and
DasK), where the routine names and the memory map come from.

| | HarvestMoonSNESRecomp | HarvestMoonSnesDecomp (this one) |
|---|---|---|
| Purpose | Play: the PC port and its improvements | Understand the game: readable code |
| Output | The game (`.exe`) | Routines in `src/` + a test executable |
| Needs | Your ROM | Your ROM and the recompilation project |

## Legal notice

This repository **contains no ROM and nothing generated from one** (no
generated code, no executables). To build it you need your own legal copy
of *Harvest Moon (USA)*:
SHA-256 `73a3aa87ddd5ce5df5ba51292316f42b6e128280179b0a1b11b4dcddc17d4163`.

## What is decompiled

| System | Routines | File |
|---|---|---|
| Random numbers | GetRNG, RNGReturn0toA ("roll an n-sided die") | `src/rng.c` |
| Arithmetic | DivisionUnsigned | `src/math.c` |
| Weather | SetWeatherFlags, WeatherTomorrow | `src/weather.c` |
| Farm | GetTileIndex, LoadsDamageProbabilities, ClimateFarmDamageCheck, ClimateFarmDamagePrep | `src/farm.c` |
| Stock | AddWood, AddGrass, AddPlayerHappiness | `src/stock.c` |
| Player | ChangeStamina | `src/stamina.c` |
| Animals | GetsCowPointer, GetChickenPointer | `src/animals.c` |

A few things found along the way (details in each file's comments):

- "1 chance in n" in the game is not exactly 1/n: RNGReturn0toA slightly
  favours 0.
- Rain 1/6 in spring and 1/10 in summer and fall; snow 1/3 in winter;
  hurricane 1/30 in summer (1/60 with the turtle shell).
- The first year has two fixed storms: winter day 8 and summer day 30.
- At night, rain breaks fences (1/96), a hurricane also rips out grass
  (1/16) and crops (1/4), and the new year wipes grass (1/64) and crops
  (1/32).

## Layout

- `src/` — the rewritten routines and the checker (`hm_decomp.c/.h`).
- `data/decomp_names.tsv` — names of 478 routines (from HM-Decomp).
- `tools/build.sh` — generates, renames and builds the test executable.
- `tools/check.sh` — runs the checks (random inputs, a new game, nights).
- `tools/progress.py` — computes the percentage above.
- `tools/import_decomp_names.py`, `tools/decomp_overrides.py` — helpers.
- `scripts/` — scripted playthroughs used by the checks.
- `DECOMPILACION.md` — the method, explained. `ESTADO.md` — status and
  notes. (Both in Spanish.)

## Usage

With the three projects side by side:

```
HarvestMoonSNESRecomp/   (git clone + bash tools/apply_patches.sh)
HarvestMoonSnesDecomp/
HM-Decomp/
```

```
cd HarvestMoonSnesDecomp
tools/build.sh --rom /path/to/your/rom.sfc            # HM_RECOMP=... if the recompilation lives elsewhere
HM_ROM=/path/to/your/rom.sfc tools/check.sh 2000 10   # 10 seeds x 2000 random calls + playthroughs
python3 tools/progress.py --decomp ../HM-Decomp --update-readme
```

The decompilation **does not modify** the recompilation project: it
generates its own copy of the code from your ROM, applies the names and the
checkers, and builds a test executable in `build/`. Without
`HM_DECOMP_CHECK` / `HM_DECOMP_FUZZ` that executable plays exactly like the
normal game.

## License

[PolyForm Noncommercial 1.0.0](LICENSE), the same as HarvestMoonSNESRecomp:
free to use for non-commercial purposes. It covers this repository's code
only, not the game or its data.

## Credits

- [HM-Decomp](https://github.com/ElisaS-Codes/HM-Decomp) by ElisaS-Codes and
  DasK: the disassembly, the names and the RAM map.
- [snesrecomp](https://github.com/RetroPortingToolKit/snesrecomp): the
  static recompiler everything runs on.
