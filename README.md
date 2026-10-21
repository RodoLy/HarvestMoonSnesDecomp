# HarvestMoonSnesDecomp — a decompilation of Harvest Moon (SNES)

**English** · [Español](README.es.md)

<!-- progress -->
![Decompiled: 10.49%](https://img.shields.io/badge/decompiled-10.49%25-2f855a)

**10.49%** of the game's code decompiled and verified: 3204 of 30543 instructions, 44 of 376 routines (11.7%).
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
| Weather | SetWeatherFlags, WeatherTomorrow, SUB_8281C0 (the TV forecast) | `src/weather.c` |
| Farm | GetTileIndex, LoadsDamageProbabilities, ClimateFarmDamageCheck, ClimateFarmDamagePrep, NightlyFarmTilesCheck, MonthlyFarmTilesCheck, CalculateFarmDevelopment, Sub_82A9A0 (grazing) | `src/farm.c` |
| Stock and money | AddWood, AddGrass, AddPlayerHappiness, AddMoney | `src/stock.c` |
| Player | ChangeStamina, HaveLunch | `src/stamina.c` |
| People | FindMostLovedName, WifePregnanacyandChilds | `src/people.c` |
| Calendar | LoadsDateNames | `src/calendar.c` |
| Animals | GetsCowPointer, GetChickenPointer, AddCowHappiness, CowFeedingandStatus (the animals' night) | `src/animals.c` |
| Events | SetCCPoiner, ResetsCurrentCCStructureLong / Short, SUB_8480F8, UNK_LoadCCDataLong / Short (start an event script) | `src/events.c` |
| Map edits | SUB_81A801, SUB_81A83A, SUB_82B060 (change tiles of the current map) | `src/mapedit.c` |
| Tools | GetTileContent, UNK_CheckTileProperty, CheckToolSuccess, CalculateTileinFront, TileAtOffsetFromPlayer, PreCheckToolSuccess / Line / Square | `src/tools.c` |

A few things found along the way (details in each file's comments):

- "1 chance in n" in the game is not exactly 1/n: RNGReturn0toA slightly
  favours 0.
- Rain 1/6 in spring and 1/10 in summer and fall; snow 1/3 in winter;
  hurricane 1/30 in summer (1/60 with the turtle shell).
- The first year has two fixed storms: winter day 8 and summer day 30.
- At night, rain breaks fences (1/96), a hurricane also rips out grass
  (1/16) and crops (1/4), and the new year wipes grass (1/64) and crops
  (1/32).
- Crops grow one stage per night only if they were watered; rain waters
  everything. In fall watered crops just dry out, and on the first day of
  winter every crop dies back to tilled soil.
- The ending's "ranch development" score is simply the number of farm tiles
  with fences, crops or grass.
- A hungry cow may fall sick (1/4); a cow sick to the end dies unless
  tomorrow is a festival. Pregnancy counts down with a global counter, and
  the calf's starting happiness (20 / 50 / 100) depends on the mother's.
- Children: the first needs 450 hearts with the wife, the second 650 and
  the first child 90 days old; both need the house at level 2. A child is
  born 60 days after it is conceived.
- Whether a tool works is a bit in the tile's property table (sickle 01,
  hoe 02, hammer 04, axe 08, water 10, seeds 20), and only from the sides
  and in the seasons the tile allows. Any item not on that list "works".
- The date suffix is "st/nd/rd" only for days 1-3, so the game writes
  "21th", "22th" and "23th".
- **A bug in the original game:** in CowFeedingandStatus, after a 1-in-16
  roll for a cow left outside, the X register stays 8-bit; if a broken-fence
  night flag ($0196 bit $0400) is also set, `LDY.W #$0002` at $83:BDE1 is
  decoded as a 2-byte instruction and the leftover `00` executes as `BRK`.

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
