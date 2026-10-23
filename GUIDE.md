# Guide: Harvest Moon (SNES) from the inside

**English** · [Español](GUIA.md)

This guide explains, assuming nothing, **how the game works on the inside**,
from what we find while decompiling it. It is updated with every new
finding.

- The first part (1-4) is the minimum needed for the rest: what
  decompiling is, what the console is like inside, and how we check that
  what we write is right.
- The second part (5) is the game, system by system: the weather, the
  farm, the tools, the animals, the people, the events, the ending...
- At the end: the original game's bugs (6), a glossary (8) and the
  progress (9).

> How to read the numbers: `$1F` is hexadecimal (base 16): `$0A` = 10,
> `$10` = 16, `$64` = 100, `$FF` = 255. An address like `$7F:1F19` is
> "bank $7F, offset $1F19" (see 2.3).

---

## 1. What we are doing

### 1.1 Recompilation and decompilation

The original game is a cartridge: a **ROM**, millions of bytes with the
SNES processor's instructions and the data (graphics, text, maps).

- **The recompilation** (the HarvestMoonSNESRecomp project) translates
  those instructions into C automatically, so the game runs on a PC. The
  result works, but it is unreadable: thousands of lines like "load this
  byte, add this other one, if the result is zero jump".
- **The decompilation** (this project) rewrites those routines **by
  hand**, as C a person can read: with names (`ChangeStamina`,
  `ToolUsedHoe`) and comments that say what each thing does and why.

A **routine** is a piece of code with a beginning and an end, like a
function: "add money", "decide tomorrow's weather", "use the hoe".

### 1.2 HM-Decomp

We start from the community's disassembly,
[HM-Decomp](https://github.com/ElisaS-Codes/HM-Decomp): the cartridge's
code written as 65816 assembly, with many names already given. The routine
and variable names come from there. When a routine has no name there
(`CODE_81A688`, that is "code at $81:A688"), we give it one
(`ChangeMapTile`).

---

## 2. The minimum about the SNES

### 2.1 The processor (65816) and its registers

The processor works with a few internal "boxes" called **registers**:

| Register | What for |
|---|---|
| **A** | the accumulator: where arithmetic happens |
| **X**, **Y** | indexes: "item number X of a table" |
| **S** | the stack pointer (see 2.4) |
| **D** | the direct page (see 2.5) |
| **DB** | the data bank: which bank variables are read from |
| **PB** | the program bank: which bank the running code is in |
| **P** | the flags (see below) |

**Flags** are bits left behind by operations:

- **C** (carry): "went over" when adding, "didn't run short" when
  subtracting, and "greater or equal" when comparing. Many routines return
  information in C.
- **Z** (zero): the last result was 0.
- **N** (negative): the last result has its top bit set.

### 2.2 8-bit and 16-bit modes

A, X and Y can work with 8 bits (0-255) or 16 bits (0-65,535). The
instructions `SEP` (to 8 bits) and `REP` (to 16 bits) switch them. In
HM-Decomp they show as `%Set8bit(!M)` and `%Set16bit(!MX)`.

A detail that matters a lot when decompiling: in 8 bits, A **still keeps
its high byte**. An 8-bit instruction only changes the low byte, so what
was left on top stays there and can show up again later.

### 2.3 Memory

| Area | What it is |
|---|---|
| **WRAM** `$7E:0000-$7F:FFFF` | working memory (128 KB): all the game's variables |
| **ROM** banks `$80-$FF`, `$8000-$FFFF` | the cartridge: code and data, read-only |
| `$0000-$1FFF` of banks `$00-$3F` and `$80-$BF` | a "mirror" of the first 8 KB of WRAM |
| `$2100-$21FF`, `$4200-$43FF` | hardware registers: video, sound, DMA |
| SRAM (bank `$70`) | the cartridge's battery-backed memory, where the game is saved |

So `$7F:1F19` (the season) is byte `$1F19` of the second WRAM bank. And
`$0921` (the item in hand) falls in the first 8 KB, which can be seen from
almost any bank.

### 2.4 The stack, JSL/RTL and JSR/RTS

To call a routine, the processor saves on the **stack** the address it has
to come back to, then jumps:

- `JSL` (long call) saves 3 bytes: the bank and the return address. The
  routine ends with `RTL`.
- `JSR` (short call, within the same bank) saves 2 bytes. The routine
  ends with `RTS`.
- `PHA`/`PLA`, `PHX`/`PLX` push and pull values on the stack.

The stack lives in WRAM, so those bytes stay written in memory. That is
why the rewrites also write exactly the same bytes on the stack: return
addresses are read from the ROM with `tools/calls.py`, never worked out by
hand.

### 2.5 The direct page and long pointers

The **direct page** is 256 bytes of fast access. In this game it starts at
`$0000`, so `$72` is simply the variable `$0072`. The game uses them as
working variables:

- `$72-$74`: a 3-byte long pointer (bank + address) to an animal's
  record.
- `$CC-$CE`: the current event slot.
- `$C9-$CB`: the position in an event script.

`LDA [$72],Y` means "read from where `$72-$74` points, plus Y".

### 2.6 Frames, NMI and DMA

- The game moves in **frames**: 60 per second.
- At the end of each frame the hardware signals with an interrupt, the
  **NMI**. That is when the game copies graphics to video memory.
- Those copies are done with **DMA**: the hardware moves blocks of bytes
  by itself, without the processor going byte by byte. During the frame
  the game writes down the copies it wants (a "queue"), and the NMI starts
  them all together.

### 2.7 The sound chip

Music and effects are made by another processor (the SPC700, or "APU").
The game talks to it through 4 ports (`$2140-$2143`): it writes a request
and **waits** for the chip to answer (see 5.15 for the conversation, and
3.4 for how the checker deals with it).

---

## 3. How we know the rewrite is right

### 3.1 The checker

Every rewritten routine is compared against the original, **byte for
byte**:

1. All of memory (128 KB) and the registers are copied.
2. The original routine runs.
3. The rewrite runs on the copy.
4. The whole memory (stack included), the registers A, X, Y, the C flag
   and the DMA registers are compared.

If a single byte differs, the checker reports it with the exact input that
produced it.

### 3.2 Two ways of testing

- **Live**: the game plays itself from a script (a new game and 10 forced
  nights) and every real call is checked.
- **Random** ("fuzzing"): each routine is called 2,000 times with random
  inputs, with 10 different seeds. Each routine has its **preparation**
  (`prep_*`), which builds inputs that make sense: a random farm, random
  animals, an event script of random bytes...

### 3.3 Making the test really test

Some tricks that were needed:

- **Getting the rare cases to happen.** If a find is 1 in 64, the random
  generator's seed has to be randomized too. Otherwise every call gets the
  same roll and the find never shows up.
- **Getting the tools to work.** There is a tile table of our own where
  almost everything can be used from every side.
- **Getting the equality to happen.** In "jump if equal", a random
  equality comes up 1 time in 256, so the preparation forces it now and
  then.
- **Sabotage.** To confirm a branch is tested, a number in the rewrite is
  broken on purpose and we check that the checker complains. It always
  did.

### 3.4 What the checker cannot see

- **The saved games (SRAM)** are compared too: the checker copies the
  cartridge's battery RAM before the call and checks that the rewrite
  leaves it exactly as the original did.
- **The video hardware**: its registers are not memory. What the game
  writes there is not compared, except the DMA registers.
- **The sound chip, solved by recording.** Its answers come from another
  processor and cannot be computed. So, while the original runs, the
  checker writes down every read and write on the sound ports, in order.
  Then, when the rewrite runs, each port read gets the value the original
  got at that same point, and each write must be exactly the one the
  original made (same port, same value, same order). One difference and
  the check fails. In the random test, these routines are tested in a
  calmer moment of the game (900 frames earlier), because sometimes the
  chip is busy loading music for a long time and never says "ready". And
  since the chip's clock cannot go back, for these routines the CPU's clock
  never goes back either: each call starts where the previous one ended,
  as if the game called the routine again and again.
- **What runs in the interpreter.** Some routines have no compiled
  version: the recompilation runs them in an interpreter. The live
  checker does not see those calls, so the random test is the
  verification (it calls the original through the interpreter).
- **Reads outside the cartridge.** If a random test makes a routine read
  a table so far away that it leaves the ROM, it lands on addresses with
  nothing there (the "open bus": you get whatever was last on the wires).
  That never happens in the game, so the preparations keep the inputs
  inside the ROM. Beware of what a preparation does *not* set: the full
  regression of October 23 found a difference in `UNK_CheckTileProperty`
  because half of the time it kept the property table pointer ($0D-$0F)
  the game had at that moment, and at that frame it pointed below $8000 of
  a ROM bank. Now it is only kept if the whole table (4 bytes for 256
  types) is real memory.

### 3.5 The percentage

`tools/progress.py` counts the instructions of all of HM-Decomp's code
(45,176) and how many belong to routines already rewritten and verified. A
routine that walks the 4,096 farm tiles weighs more than a three-line one.
On October 21 the measurement was corrected: code without an address was
not being counted and the figure came out too high (10.49 % → 7.29 %).
A second correction came later: HM-Decomp sometimes writes several
routines one after the other under a single name. Now a label that is
called from outside its block counts as a routine of its own, so the
instructions are credited to the right one (20.45 % → 20.33 %).
A third one on October 23: four script commands have an address comment
in HM-Decomp with the wrong bank ($80 instead of $84), and one of them
landed on a real routine of bank $80 (`CallIndexedSubrutines`, $80:9553),
whose count included it (56.14 % → 56.10 %).

---

## 4. Where each thing is in the project

| File | What is there |
|---|---|
| `src/rng.c` | random numbers |
| `src/math.c` | division |
| `src/weather.c` | the weather |
| `src/farm.c` | the farm field |
| `src/tools.c` | which tile a tool hits and whether it works |
| `src/toolused.c` | what each tool does |
| `src/stamina.c` | stamina and lunch |
| `src/stock.c` | wood, fodder, money, happiness |
| `src/animals.c` | cows and chickens |
| `src/people.c` | the favourite girl, wife and children |
| `src/calendar.c` | the names of the date |
| `src/events.c` | the event slots |
| `src/eventcmds.c` | the script commands |
| `src/mapedit.c` | changing map tiles |
| `src/dma.c` | the DMA queue |
| `src/objects.c` | objects with a sprite |
| `src/ending.c` | the ending's score |
| `src/text.c` | the dialogue box |
| `src/audio.c` | talking to the sound chip |
| `src/save.c` | the saved games |
| `src/menus.c` | buttons in menus |
| `src/scroll.c` | scrolling the map |
| `src/palette.c` | the colours of the day |
| `src/collision.c` | bumping into the map, objects and the player |
| `src/toolanim.c` | the tool's animation when pressing Y |
| `src/interact.c` | pressing A in front of something |
| `src/playerinput.c` | the buttons while walking |
| `src/decompress.c` | the compressed data (LZSS) |
| `src/coloranim.c` | colours that move (palette cycling) |
| `src/joypad.c` | reading the controller |
| `src/toolsound.c` | the noise of each tool |
| `src/maptiles.c` | what a map gets when it loads |
| `src/shipping.c` | the shipping at 5 pm |
| `src/hm_decomp.c` | the checker and the preparations |

---

## 5. The game from the inside

### 5.1 Random numbers

- `GetRNG` ($83:8138) is a 24-bit generator: from the current number it
  computes the next. The seed lives at `$0100-$0102`.
- `RNGReturn0toA` ($80:89F9) is "roll an n-sided die" and gives 0 to n-1.
  The game uses it as "1 in n": **it rolled 0**.
- **It is not fair.** With a 6-sided die, 0 comes out for 43 of the 256
  possible values instead of 42.7. 0 comes out a little more than it
  should.
- `$DE` is another random byte the animals use (the dog, the chickens).

### 5.2 Date and time

- **Calendar.** `$7F:1F18` year (0 = year 1), `1F19` season (0 spring, 1
  summer, 2 fall, 3 winter), `1F1A` weekday, `1F1B` day (1-30).
- **Clock.** `1F1C` hour, `1F1D` minutes (in steps of 4).
- `$0973`: 0 = the clock is stopped, 1 = running, 2 = skip to the next
  day. At 18:00 the clock stops.
- **Festivals:** Flower (spring 23), Harvest (fall 12), Egg (fall 20),
  Thanksgiving (winter 10), Star Night (winter 24), New Year (winter 30).
- **The date suffix** is "st/nd/rd" only for days 1-3, so the game writes
  "21th", "22th", "23th".

**The shipping at 5 pm** (`src/shipping.c`). Every in-game hour
`UpdateTime` calls `ShippingScene`: at 17:00 and on the farm it switches
the input off, marks the scene ($7F1F5A bit 10) and starts its script ($26,
in event slot 6). When the script is done (it sets bit 11), and no change
of map is under way, `ShippingSceneDialogue` shows the day's text: what was
shipped (text $031A), or "nothing" ($031B) when the money shipped
($7F1F07-$7F1F09) is 0; then script $27 and the scene mark is cleared.

### 5.3 The weather

Every night tomorrow's weather is decided (`WeatherTomorrow`) and a code is
stored in `$098C`:

| Code | Weather |
|---|---|
| 0 | sunny |
| 1 | rain |
| 2 | snow |
| 3 | hurricane |
| 4, 5 | the two first-year storms |
| 6-11 | festival days |

- **Chances:** rain 1/6 in spring and 1/10 in summer and fall. Snow 1/3,
  winter only. Hurricane 1/30, summer only; with the turtle shell, 1/60.
- **The first-year storms:** in winter it is certain on day 8, in summer
  on day 30, and each happens only once.
- **The next morning:** `SetWeatherFlags` turns the code into the flags in
  `$0196`, which the maps and the TV read.

### 5.4 The farm

- **The field** is a 64 × 64 grid of tiles (`$7E:A4E6`, one byte per
  tile). Each tile is 16 × 16 pixels.
- **Tile codes:**

  | Code | Tile |
  |---|---|
  | 02 | soil |
  | 03 | weed or stone |
  | 05 | fence |
  | 06 | broken fence |
  | 07, 08 | tilled soil |
  | 1E-6F | crops at every stage |
  | 70-79 | growing grass (79 = grown) |
  | A0 and up | not field anymore |

- **Dry and watered go in pairs:** even code = dry, the next odd one =
  watered.
- **Every night** (`NightlyFarmTilesCheck`):
  - Rain waters everything dry.
  - Watered tiles dry up if it doesn't rain.
  - A watered crop grows one stage.
  - In fall watered crops only dry up, and in winter nothing grows.
  - On the first day of winter every crop goes back to tilled soil.
- **Night damage:** rain breaks fences (1/96); a hurricane also rips out
  grass (1/16) and crops (1/4); the new year wipes grass (1/64) and crops
  (1/32).
- **The "ranch development"** (`CalculateFarmDevelopment`) is counting
  the tiles with fences, crops or grass.
- **The farm is kept apart.** On the farm the game works on the current
  map (`$7E:09B6`); `CopyCurrentMaptoFarmMap` saves it into the farm's own
  copy (`$7E:A4E6`), so it survives going elsewhere. A new game starts
  from the field in the cartridge (`LoadDefaultFarmMap`, from `$A7:8000`,
  the first pointer of `MapPointerTable`, which is at $82:B174, not B173 as
  HM-Decomp says).

### 5.5 The tools

**Does it work here?** Each map has a property table: 4 bytes per tile
type, which say which tools work and from which sides and in which
seasons. Bits: sickle 01, hoe 02, hammer 04, axe 08, water 10, seeds 20.
`CheckToolSuccess` answers: 1 works, 0 doesn't, 2 the watering can gets
refilled.

**Quirk:** any item that is not on the list "works".

**First the animation** (`src/toolanim.c`). Pressing Y runs
`UNK_ToolUsed`: with no stamina left ($D2 bit 3) the player only shows the
"tired" animation ($4D). Otherwise it checks the tile in front, which also
chooses the tool's sound, and calls the tool's animation routine
(`Tool_Animation_Table`, again one per item). Each only puts the player's
animation in $0901: a number per tool, plus the direction for the tools
that look different when facing each way. The watering can has three:
pouring, empty, and filling up (in front of water it also fills the can,
20 uses). The bell also raises $7F1F5A bit $10, which calls the cows. What
the tool does to the tile comes later, when the animation reaches that
moment (`SUB_829260`).

**Who calls them:** `SUB_829260` takes the item in hand ($0921), looks up
its routine in `Tool_Used_Table` (one per item, in item order: 28 in
all) and runs it; then it raises the flag "a tool was used" ($7F1F5A bit
$0040).

**What each one does** (`src/toolused.c`; all cost stamina at the end):

| Tool | What it does |
|---|---|
| Sickle | Cuts weeds; grass becomes fodder (+1 in the silo). Doesn't work in winter. |
| Hoe | Tills the soil. |
| Hammer | Breaks small stones at once; the big 2×2 rocks on the 6th hit. |
| Axe | Cuts stumps on the 6th hit: +6 wood. |
| Seeds | Each use sows the 9 tiles around, one per call. |
| Paint | Paints one of the 4 parts of the house wall: +10 happiness each. |
| Feed | In the coop or the barn it fills the trough in front; otherwise it spills on the floor. |
| Medicine, potion | Used up, they leave a flag the cow code reads. |
| Watering can | Waters the tile in front: its type becomes the next code (dry → watered). One water less each use; with the can empty, or facing water (answer 2), nothing. It asks `PreCheckToolSuccess` twice. Sound effect 0, 7. |

The hammer's and the axe's hit counter is the same one, `$096D`.

**Seeds only take in their season:**

| Seed | Season |
|---|---|
| Corn, tomato | summer |
| Potato, turnip | spring |
| Grass | spring and summer |

Out of season the tile ends up as a dead sprout.

**The gold tools:**

| Gold tool | Difference |
|---|---|
| Sickle | cuts the 9 tiles around |
| Hoe | tills up to 6 tiles in a line and stops at the first it can't |
| Hammer | breaks the big rock in one hit (costs 4 stamina) |
| Axe | cuts the stump in one hit (costs 8) |
| Sprinkler | waters the 9 tiles around, one per call, and never runs out of water (costs 8) |

**Finds.** None happens while `$7F:1F60` bit 3 is set:

| Tool | Find | Chance | When |
|---|---|---|---|
| Hoe | coin (10 G) | 1/16 | once a day |
| Hoe | money bag (50 G), if no coin | 1/16 | once a day |
| Hoe | mole | 1/32 | spring and summer, once a day |
| Hoe | power berry | 1/64 | only twice in the whole game |
| Sickle | a frog (+2 happiness) | 1/16 | summer, once a day |
| Hammer | berry in the rock by the spring (maps 0C-0F) | 1/4 | once in the whole game |
| Axe | berry in a stump off the farm | 1/16 | once |
| Axe on the mountain (tile F4) | the gold axe event | — | once |

**The noise of each tool** (`src/toolsound.c`). `Tool_Noise_Table`
($82:9054) has 3 bytes per item in hand: a sound bank and two effects, one
for when the tool lands on something and one for when it does not ($0922 =
0 or 1; 2 means silent). `ToolUsedSound1` ($82:8FB1) picks the effect and
plays it on the sound chip (`UNK_Audio19`); an effect of 0 is no sound.

### 5.6 Stamina

- **Current stamina** `$0918` and **max** `$0917`; it starts at 100.
- **At 0** the player collapses.
- **Tiredness** (`$096C`, 0 to 3): goes up in stages as stamina drops
  below a half, a quarter, etc.
- **Quirk:** `ChangeStamina` decides whether the change is positive or
  negative from the N flag left by **its caller**, not from the value in
  A.

### 5.7 The animals

**Cow record** (16 bytes, from `$7E:C1C6`, up to 12 cows):

| Byte | Content |
|---|---|
| +0 | status: 01 exists, 02 newborn, 04 calf, 08 adult, 10 cranky, 20 sick, 40 pregnant, 80 just gave birth |
| +2 | where it is (`$27` = barn) |
| +3 | a day counter |
| +4 | happiness (0-255) |
| +5 | days out of the barn |

**Chicken record** (8 bytes, from `$7E:C286`, up to 13):

| Byte | Content |
|---|---|
| +0 | status: 01 exists, 02 egg, 04 chick, 08 hen, 10 hungry |
| +1 | where it is (`$28` = coop) |

**The animals' night:**

- **Unfed cows** lose happiness and may fall sick: 1/4, or 1/2 with snow
  or a hurricane.
- **Sick to the end, it dies**, unless tomorrow is a festival.
- **A cow 10 days out** turns cranky.
- **The birth:** the calf starts with happiness 20, 50 or 100 depending on
  the mother's.

**New animals.** `AddNewCow` and `AddNewChicken` look for the first free
record. A cow born on the farm gives you +70 happiness.

**Counting them, unused.** Right after the slot pointers there are two
routines that count the animals: `UnusedBarnCoopCheck` ($83:C9BA) the
chickens in use (bit 0 of the record, of 13) and, right after its RTL with
no label of its own, `UnusedBarnCheck` ($83:C9E5) the cows (of 12). Nothing
calls them: they look like a first version, and the game counts elsewhere.

**On entering a map** (`PlaceAnimalsOnMap`), every chicken, cow, the dog
and the horse that is there starts its script. "Is there" groups maps:

| The animal is on… | It shows on… |
|---|---|
| 00-03 (the farm in any season) | 00-03 |
| 04-07 | 04-07 |
| 08-0F | 0C-0F |
| 10-13 | 10-13 |
| 14-17 | 15-17 |
| 31 and up | 31 and up |
| any other | only that very map |

The horse has no 14-17 group. With rain, cows out of the barn use other
scripts.

**The chicken, every frame:**

- If you press A in front of it with free hands, **you pick it up**: as a
  chick (item $26) or as a hen ($25).
- If it is marked for sale, it is sold (price $32).
- If you leave it, a counter reaches $200 and starts another script.

**The dog, every frame:**

- Each **hug** is counted in `$7F:1F52`.
- Its **mood** (`$7F:1F58`) moves between 5 states every 2 seconds, and a
  random roll decides what it does.
- When it is **called**, it runs towards you along the axis where it is
  farther away.

**The cow, every frame** (`Event_UNKCowRelated`, the biggest routine
decompiled so far: 1,567 instructions). Each cow has a 16-byte record:
status (calf, young, adult, sick, pregnant, "runs away from you"),
today's flags (brushed, milked, talked to, moving, bred), map, days
pregnant, **happiness**, times hit and position.

- **Talking** (A in front of it): a text that depends on its status. The
  first time each day: **+1 happiness**.
- **Brush:** once a day, **+3 happiness**.
- **Milker:** once a day, adults only, not sick or pregnant. The size of the
  milk depends on happiness: below 96 small, below 192 medium, otherwise
  large.
- **Hitting it** with the sickle, hoe, hammer or axe (or the gold ones):
  it complains, and the "times hit" counter goes up (up to 10).
- **Miracle potion** on an adult: it gets pregnant (**+10 happiness**),
  unless there are already 12 cows. **Medicine:** cured.
- **Selling it** (from the farm): the price depends on happiness: 500 G
  below 96, 800 G below 192, 1000 G above.
- **Wandering:** every 256 frames (about 4 seconds) a random roll picks a
  walking script, different for calves, young and adult cows.
- **The bell:** the cows turn towards you (or away, if they are of the
  "runs away" kind) and come.
- Every script has four versions, one per kind of cow (slot byte +02).

### 5.8 People

- **Hearts** of the five girls: `$7F:1F1F + 2·n` (0-999), in the order
  Maria, Ann, Nina, Ellen, Eve. Talking to them adds +8.
- **The favourite** (`FindMostLovedName`): on a tie the last one wins.
- **Points into hearts** (`CODE_81D1C5`): the points (0-999) become a
  heart level, 0-9, by these ranges (table $81:D1E8): 0-49, 50-119, 120-199, 200-249,
  250-299, 300-399, 400-499, 500-599, 600-799, 800-999. The steps are not
  even: the first hearts come slowly, and the last one takes 200 points.
- **Children.** The first needs 450 hearts with the wife; the second, 650
  and the first child 90 days old. Both need the house at level 2 and 20
  days of marriage. A child is born 60 days after it is conceived.

### 5.9 Money, wood, fodder

- **Money** (`$7F:1F04`, 3 bytes) is kept in **tens of G**: the last 0 you
  see on screen is decoration. It is capped at 999,999.
- **Wood, fodder and happiness** are capped at 999.
- **When a script charges or pays** (the `ChangeMoney` command) and the
  money would go negative, the money becomes **0** instead of "not
  enough". See 6.

### 5.10 Events

Everything scripted (scenes, villagers walking, animals, festivals) runs as
an **event script** in one of the **slots** ("CC"). They are 64-byte
structures from `$7E:B586`:

| Byte | Content |
|---|---|
| +00 | in use |
| +01 | flags |
| +10 | frame counter |
| +1A, +1C | position |
| +30, +32 | where its script is |
| +3F | its own number |

**Slot numbers:** dog $16, horse $17, cows $18 + n, chickens $24 + n.
There are 49 ($31); `ClearsofAllCCStructs` ($84:8000) frees them all
(first byte 0), for example on entering a map.

**The script is a small language.** Each byte is a **command** (there are
90, in the table `$84:9419`), followed by its arguments. `$C9-$CB` points
at where it is. Some commands:

| Command | What it does |
|---|---|
| `JumptoInstruction` | jumps to another part of the script |
| `JumpifFlagSet`, `JumpifEquals`, `JumpifBetween` | jump if something holds |
| `UNKCompareset7Aflag` | waits until a flag is set; meanwhile it repeats the command every frame |
| `TimeRunning`, `TimeStopped`, `SetHour` | move the clock |
| `SetPlayerPosition`, `SetPlayerDirection` | move the player |
| `NewCCStructure` | starts another slot on another script |
| `JumpifEqualsValue` / `...Long`, `JumpifBetweenValue` / `...Long` | jump comparing a 16- or 24-bit value at any address |
| `SetFlag`, `StoreValue`, `SetValueSort`, `SetValueLong` | write a flag or a value at any address |
| `UNKTextrelated` | adds (signed) to a byte, kept in 0..255; despite the name, it has nothing to do with text |
| `UNKCCchanges14` | adds (signed) to a value, kept in 0..999 (hearts, happiness...) |
| `ChangeMoney` | adds or takes money |
| `UNKCCchanges12` / `13` | waits a number of frames while allowing certain things (`$08FD` / `$08FF`, state bit `$1000`) |
| `UNK2` | waits until `$7F:1F5A` bit `$8000` is set |
| `MapScrolling` | moves the camera by itself: X speed, Y speed and frames |
| `TransitiontoX`, `TransitiontoHouse` | change map (`$098B`; `$0196` bit `$4000` starts the transition) |
| `SetItemonHand`, `ChangeItemonHand`, `DropItemAnimation` | put an item in the hands or take it away |
| `UNKCCchanges4`, `6`, `10` | set up a walk for the slot: speed, destination, starting point |
| `UNKCCchanges8`, `15`, `3` | choose the slot's animation: a table in bank $B3 and an entry (given, or the player's direction); 3 also places the slot |
| `UNKCCchanges17` | if the player talked to the slot, it turns to face the player, stops, and the script jumps |
| `UNKCCchanges5` (1E, 1F), `UNKCCchanges9` (2D) | **talking to someone**: if the player talked to the slot (+0C), is not using or showing a tool nor whistling, carries nothing, `$7F1F60` bits 1-2 are clear and A was just pressed: the slot faces the player, the player stops (action 0, `$D2` bit `$40`, `$096E` = `$56`) and the script jumps. 2D has a second address, for when the player holds something up (action 5 with an item: that item also goes to `$091E`) |
| `UNKCFD1` (2A) | starts a map animation (`StartMapAnimation`, the number in the argument) |
| `AudioRelated2` (0E) | changes the music to the one asked for in `$0110` (`UNK_Audio25`) |

Of the 90 commands only three are left in the original: the two screen
fades (0A, 0F) and `AudioRelated3` (2C, a sound effect). All three wait
whole frames (`WaitForNMI`), and a routine that waits for the next frame
cannot be checked call by call. So the command interpreter and the slot
loop (`IterateCCStructures`) stay in the original too.
| `UNK1`, `UNKCCchanges7` | destroy the slot's own object, or another slot and its object |

**Things you pick up.** Several commands use the same "free hands" test:
the slot allows it (+0C), you press A, your hands are empty and you are not
using a tool, in a menu or in a scene. Each checks the conditions in a
slightly different order:

| Command | What you pick up |
|---|---|
| `UNKChickenRelated` | a hen ($25) or a chick ($26) |
| `UNKPickupMole` | the mole ($29); otherwise every 3 seconds it may move |
| `UNKFishing` | a fish ($07) |
| `UNK3` | item $08 |

**Commands 50-59** ($84:BBDA-$84:C013). HM-Decomp names only the first;
here they are `Event_Cmd51`...`Event_Cmd59`:

| Command | What it does |
|---|---|
| 50 `UNKChickenRelated2` | puts down what is carried when a scene starts: a hen or chick goes back to its place in the coop (table $83:CA10), any other item disappears, the carried dog goes back to the farm (the season's map, at 120, 424); the player stands |
| 51 | the same, but without the animation, and of the other items only the mole ($29) is dropped |
| 52 / 53 | if A was pressed on the slot's object with free hands, the object goes away and $7F1F60 bit 1 / bit 2 is set (with those bits, A in front of tile $F5 gives action $1A) |
| 54 | `<text> <lines>`: a text box that closes by itself (input mode 6, 120 frames) |
| 55 | `<address> <bit> <target>`: jump if that bit of the word there is clear |
| 56 | use the tool in hand |
| 57 | `<n>`: change the stamina by n (`ChangeStamina`: the sign comes from bit 7, bug 11) |
| 58 | `<tile> <x> <y>`: put that tile type on the farm |
| 59 | the player's action $1C |

**More commands:** `StartTextBox` and `StartTextBoxCopy` (28 and 29, the
same code) open a text box with `<text> <lines>`; `ResetFlag` (38; 39 and 40
point to the same code, see bug 13) clears a bit of a word anywhere;
`EditTileonMap` (75, $84:BA10) sets a tile type on the current map, as 58
does on the farm. `Event_UNKTools` puts the tools in the hand and in the
backpack back in the shed (one bit per tool in $7F1F00), but no command
number reaches it.

**Command 37, `UNKPlayerPos`** ($84:AB27), every frame for an object of
group $17 the player can carry (probably the child: when the player puts it
down, script $17/$14 starts). Its requests are bits of $7F1F5C:

- every 120 frames ($0939) an idle animation chosen by the random byte $DE
  (scripts $4B-$50; away from place $15 of $7F1F32, script $80 instead);
- bit 6: face the way written in its byte +2 (scripts $28-$2B);
- bit 10: walk towards the player along the longer axis (like the dog);
- bit 5: it is taken in the arms ($D2 bit 4: carrying; the object goes
  away);
- A pressed on it with free hands and room in front: the player picks it
  up (action $17); bit 4 says "touched this frame".

### 5.11 Maps

- `$22` is the **current map**:

  | Maps | Place |
  |---|---|
  | 00-03 | farm (one per season) |
  | 04-07 | town |
  | 08-0B | festivals |
  | 0C-0F | crossroads |
  | 10-13 | mountain |
  | 15-17 | house (levels 1-3) |
  | 1B | church |
  | 27 | barn |
  | 28 | coop |
  | 29 | cave |
  | 31-34 | mountain top |

- **Map $30 hangs the game.**
- **The map's picture** is kept whole in WRAM (`$7E:2000`); video memory
  only holds a 64 × 64 window that is refilled from there.
- **Changing a tile** (`ChangeMapTile`) does three things: it writes the
  new picture into that map in memory, queues the DMA copy to video
  memory and, if needed, changes the tile's type. What to put comes from
  the table `$81:B363`.
- **Drawing the whole map** (`UNK_PartialMap`): for each of the 64 × 64
  tiles it takes the drawing in byte +0 of the tile type's property entry
  (0: nothing). The types $73-$79 join their neighbours of the same kind
  (fences and the like, "autotiling"): the four neighbours make a mask
  (left 8, right 2, up 4, down 1) and the table `$82:B02A` says which of
  the drawings to add. It is long: it does not finish in one frame.
- **Compressed data** (`DecompressTileMap`, LZSS). The data begins with its
  size and then comes in groups: one control byte and 8 items. Each bit of
  the control byte (lowest first) says what the next item is: 1 a byte to
  copy as it is; 0 two bytes meaning "copy 3 to 34 bytes from this place of
  what was already written" (an 11-bit place and a 5-bit length + 3). What
  was written is remembered in a 2 KB window at $7F:0000, which starts
  cleared and is written from $07DE (2048 - 34, the usual LZSS layout).
  Repeated runs (a row of the same tile, the same pattern again) take two
  bytes instead of many.

**Footprints in the snow** (`CODE_81A58F`). Every 8 steps the game
remembers where the player is ($0907 / $0909); in winter, on the farm or
the mountain, the place remembered 8 steps before gets a footprint if its
tile is snow (types 1, 2, $A8, $F6). Only the picture in video memory
changes, not the map: the prints vanish when the map is loaded again.

**Map animations** (`CODE_81A600`, every frame, after `StartMapAnimation`).
A script of 7-byte steps: a tile change, its X and Y, and how many frames
to wait; `$FFFF` ends it and `$FFFE` jumps to another script. The magic
beans growing are one.

### 5.12 Objects with a sprite

- **Game objects** (`$019C`, 40 of 36 bytes): everything with a sprite on
  screen. `CreateGameObject` takes a free one.
- **Sprites:** each is built from 16 × 16 pieces (up to 12). Numbers
  0-$261 are described in bank $86 and $262-$44F in bank $87.
- **Piece graphics:** if a piece's picture is already in video memory it
  is reused; otherwise it goes to a queue the NMI empties.
- **Map objects** (`$7E:B4E6`, 10): the effects tools leave, like cut grass
  or the hit on a rock.
- **Destroying one** (`CODE_8581A2`, `src/objects.c`): each graphics slot
  counts how many pieces use it ($7E:A320); destroying an object gives its
  pieces' slots back and frees the record. But if one of its pieces is
  still waiting in the queue, nothing is freed: $0740 counts one more
  refusal and the object simply stays. Scripts destroy objects with
  commands `Event_UNK1` (their own) and `Event_UNKCCchanges7` (another
  slot's).
- **Moving and changing one:** `CODE_8580B9` moves an object (flip, X, Y);
  `CODE_858100` gives it another sprite: the old pieces are freed (with
  the same refusal as above) and the new ones loaded. `CODE_8581CB` reads
  an object back into those variables.
- **Flipped sprites:** a piece's offset becomes -offset - 16 when the
  object is flipped that way (`InvertIfFlipedX` / `Y`): the mirrored
  place of a 16-pixel piece. `ClearSpriteDataTables` empties the queue
  and frees every graphics slot (when a map loads).
- **Animating the objects** (`CODE_8582C7`, every frame): a sprite's
  entry (+0C) is a list of frames of 3 bytes: the frame's piece list and
  how many frames it lasts; a piece list of 0 goes back to the first frame
  (a loop). Each object counts down its timer (+12); at 0 it moves to the
  next frame (+0E): its old pieces are freed and the new ones loaded,
  unless one of the old ones is still waiting in the queue (then it tries
  again next frame). A duration of $FF stays for ever. A timer of $FE
  frees the object (pieces and record), but it can only be found there
  when the object was just created or given a new sprite (they copy the
  first frame's duration): a later frame of $FE is counted down right
  after it is loaded, so it just lasts 254 frames.
- **Drawing order** (`GetOBJOrderToDraw`): a kind of depth sort around
  the player's object (`$0905`) into `$084C`: the objects whose Y is not
  smaller than the player's go first, in their own order, then the
  player, and the ones above it (smaller Y) fill the list from the end
  backwards. Only the side of the player counts; it is not a full sort.
- **Emptying the sprite table** (`PresetsWRAMCopyofOAM` and an exact copy,
  `PresetsWRAMCopyOAMCopy`): the WRAM copy of the 128 hardware sprites
  ($7E:A000) all at X = Y = $F0, off the screen, and the high table
  ($7E:A200) to 0. `InitializeOBJs` frees the 40 game objects (and $DC =
  0) and empties the table; the graphics slots are left as they are.
- **A slot walking** (`SUB_848331`): with +01 bit 5 set, every +05 frames
  the slot moves by (+03, +04), signed bytes, added to its position.
- **A slot wandering on its own** (`SUB_8483CC`, 531 instructions; the
  town's people, the animals in the barn): with +01 bit 3 set, right
  after each step (+05 = +06):
  - with no steps left (+08 = 0) it picks a new walk of +07 steps: the
    direction +0F if the script forced one (1 down, 2 up, 4 right,
    8 left), else the random one in $DE (0 down, 1 up, 2 right, 3 left;
    any other value: stand still);
  - otherwise it goes on, one step less.
  - Without +01 bit 7 the slot never leaves its area: its home (+1E, +20)
    plus or minus the range (+09 across, +0A down). A step that would go
    past it makes it stand still (no step, +08 = 0, the standing animation
    from the table at +33).
  - Starting a walk: direction +02, step (+03, +04) = (0, 1), (0, -1),
    (1, 0) or (-1, 0), the pause +0B into +05/+06 and the walking
    animation from the table at +36; either way +01 |= 4 (the animation
    changed).
  - Then it looks ahead (CEEEE against the map, only with bit 7; the first
    corner's tile type goes to +0E): it stops if blocked, if the tile ahead
    is $FF or $C0-$CF, or if the player is in the way (CDDDD). Tile $E1
    only lets the slots $18-$23 (+3F) through. Stopping: no step, +05/+06
    = +0B and the forced direction is cleared.
  - Quirk: without bit 7 it still looks at the second corner's tile
    ($EB), which is whatever the last check of anyone left there.
  - The "random" $DE: the slot loop adds $33 to it before each active
    slot, so each one gets a different value in the same frame.
- **A slot's sprite, each frame** (`SUB_848286`): unless frozen (+01
  bit 4), the slot wanders and takes its step (the two above). Then its
  game object (+12) gets the position (+1A, +1C), the animation (+14) and
  the sprite (+16), and by +01: bit 6 destroys it, bit 1 creates it (the
  new object goes to +12), bit 2 changes its sprite if +16 is not the
  last one (+18), and otherwise it just moves. Finally +18 = +16 and the
  one-time bits 1, 2 and 4 are cleared.

### 5.13 The ending

At the end of year 3 (summer, day 30), the evaluation computes the **ranch
score** (`RanchMasteryCalculator`, 0 to 999). It adds up:

| Item | Points |
|---|---|
| Money | /128, at most 78 (= 99,840 G) |
| Each cow and chicken | 3 |
| Max stamina | (max − 100) / 2 |
| Each girl's hearts | /16 |
| Tomatoes, corn, potatoes and turnips shipped | /16 each |
| Happiness | /32 |
| Each house extension | 16 |
| Each child | 16 |
| Painted house | 32 |
| Clock | 22 |
| Turtle shell | 21 |
| Farm development | /2 |
| Each cow's happiness | /8, at most 25 |

### 5.14 The text box

Dialogues are shown on **BG3**, the background layer used for text.

- **Opening** (`StartTextBox`, X = number of the text): it looks up the
  text in `Text_Pointer_Table` ($83:9BF6, 3 bytes per text) and leaves
  its address in $01-$03. It resets the "printer" (the next letter goes to
  VRAM $5010), picks the box height once (2 lines if $090D is below $81,
  otherwise 1), scrolls BG3 up so the box appears, **stops the clock** and
  raises the flag "a box is open" ($7F:1F5A bit 14) so other systems wait.
- **Closing** (`CloseTextBox`): the reverse. It scrolls BG3 back, wipes the
  box by copying an empty tilemap ($9C:BBA3) over it, and lets the clock
  run again, unless it was already stopped before ($0196 bit 5).
- **The "more text" arrow** (`BlinkTextBoxArrow`): while the box waits for
  a button, every 20 frames the arrow switches between its two drawings.
  It is a 2×2 block of tiles that `UNK_SetDMA2` copies to VRAM in two
  small DMAs (top row and bottom row). There are 12 possible places for it.

**How a letter gets on the screen.** The box does not have a slot per
letter in its tilemap. Instead, the box's tilemap points at a row of tiles
in order, and the printer *draws* each letter into the next tile:

- The **font** is in the ROM in groups of 64 letters (8 per row). Each
  letter is two 8×8 tiles, one above the other. `FindLetterGraphics`
  turns a letter code into the address of its graphics.
- `DrawLetter` copies those two tiles to VRAM at the cursor ($0185) with
  `UNK_SetDMA1` (two small DMAs: top half and bottom half).
- `AdvanceLetterCursor` moves the cursor one tile forward; at the end of a
  line it jumps over the bottom halves.
- **Numbers** (money, days...) are printed digit by digit by `PrintDigit`:
  it subtracts the power of ten (1, 10, 100...) until the number goes
  negative, which gives the digit, and skips the zeros at the start.
  Digit codes are $BC-$C5.
- While the text waits for a button, `BlinkTextCursor` alternates the
  letter at the cursor with a mark every 20 frames.

**The printer** (`PrintTextFrame`, `ADDDDFFFF` in HM-Decomp) runs once
per frame. A text is a list of 16-bit codes. A normal code is a letter:
one is printed **every 5 frames**, each with a little sound effect
(`UNK_Audio19`, see 5.15). Some codes are orders:

| Code | Meaning |
|---|---|
| `$00A2` | wait for a button (the cursor blinks at the bottom) |
| `$00B1` | a space: the cursor moves and the next code is read at once |
| `$FFFC n k` | the number in variable k, with n digits, one digit per frame (money, day...) |
| `$FFFD n k` | name k (the player, the farm, the dog...), n letters, without sound |
| `$FFFE n` | a choice between n options (the cursor blinks on the chosen one) |
| `$FFFF` | the end of the text |

The variables for numbers are in `Text_Number_Pointer_Table` ($83:98EE:
address and size in bytes) and the names in `Text_Name_Pointer_Table`
($83:9AAE).

In a new game the printer runs 2,424 times and the checker compared every
one of them, together with its 248 sound effects (see 3.4).

### 5.15 Talking to the sound chip

The sound chip is a second processor. The game and the chip share four
one-byte "ports". The conversation always goes the same way:

1. The game reads port 3 twice in a row until both reads agree (the chip
   may be changing it at that very moment), and waits until it says
   **1: "ready"**.
2. It writes the command in ports 0-2 and **2 in port 3: "here it is"**.
3. It waits until port 3 says **4** (simple command) or **8** (command with
   arguments): **"done"**.
4. It writes **0** in port 3.

Commands seen so far: `$0A` plays a sound effect (with three arguments:
$0114, $0115, $0116; each letter of text uses 3, 6, $0A), `$0B` takes one
argument, `$0C` none. Before a sound effect the game asks whether the chip
is busy (port 0, bit 4) and, if it is, simply skips the sound.

In a new game, the text alone sends 248 sound effects: all of them were
checked live, with the recording of 3.4.

**The music and the uploads** (`src/audio.c`). The chip has its own 64 KB
of memory, so instruments (sound samples) and songs must be sent to it. All
the commands:

| Command | Routine | What it is |
|---|---|---|
| $01 | `UNK_Audio4` | the sound program's data ($3F2 bytes from $B2:D120), at power-on |
| $02 | `UNK_Audio3` | the 10 instruments that are always there |
| $03 | `UNK_Audio9`, `UNK_Audio12` | the instruments of a song (up to 11), or of a sound effect |
| $04 | `UNK_Audio10` | stop a channel (and forget the instruments noted from it on) |
| $05 | `UNK_Audio11` | a song's notes |
| $07 | `UNK_Audio13` | (no argument; the one it is given is kept and never sent) |
| $08, $09 | `UNK_Audio14`, `UNK_Audio6` | two-argument commands used when the song changes (1 and $10 or $40: maybe a fade) |
| $0A-$0C | see above | sound effects and two small ones |

Big data travels **3 bytes at a time**: the game first sends how many
groups of 3 (the size divided by 3, rounded up), then each group, waiting
for "0", clearing port 3 and waiting for "1" again after every one. The
tables: 26 songs (`$83:8F83`, 14 bytes each: where the notes are and the 11
instruments), 40 instruments (`$83:90EF`, 10 bytes each, and their sizes
at `$83:8EFF`), and which instrument each of the 50 sound effects needs
(`$83:927F`).

$0110 is the song wanted and $0117 the one playing; when they differ,
`UNK_Audio21`, `UNK_Audio22` and `UNK_Audio25` send the change.
`Unk_Audio23` is not about sound at all: it multiplies two 16-bit numbers
with the console's 8 x 8 multiplier.

*Quirks:* `UNK_Audio6` only sends its command when the chip says it is
busy (the opposite of the others). After a song's instruments, `UNK_Audio9`
always sends instrument 6 as well: it does so when the list ends in empty
places, and every one of the 26 songs' lists does. And again an
address of HM-Decomp is one byte early: `UNK_Audio14` starts at $83:8D39,
not 8D38.

### 5.16 The saved games

The cartridge has a battery RAM (SRAM) with **two slots of 4 KB**. What
is saved (`SaveGameSlot`), at the start of the slot:

| Offset | What |
|---|---|
| +00-+03 | year, season, weekday, day |
| +04-+0A | seed bags and feed |
| +0B, +0C | number of cows and chickens |
| +0D-+1A | tomorrow's weather, max stamina, item in hand, water, more flags |
| +2E | 1 = this slot was saved last |
| +2F | checksum (16 bits) |
| +31-+38 | crops shipped (corn, tomatoes, turnips, potatoes) |
| +39-+3B | money (24 bits) |
| +3C-+3F | the letters "FARM": the slot is valid |
| +40-+7E | wood, fodder, the girls' hearts, event flags, the dog, happiness, the family |
| +80-+97 | names: player, dog, horse, children (and the shed's items) |
| +98 | the 13 chickens (8 bytes each) |
| +100 | the 12 cows (16 bytes each) |
| +1C0 | the farm's tiles, to the end of the slot |

- The **checksum** is the sum of the slot's 2,048 words (16 bits),
  counted with +2E and +2F-+30 at zero.
- At power-on, `CheckSRAMIntegrity` checks both slots: "FARM" and the
  checksum. A slot that fails is **cleared** (its first 2 KB) and gets
  "FARM" back, so it shows as empty.
- The title screen reads only the date and the name of each slot
  (`LoadGameSlotResume`); a slot with day 0 is empty.
- **Loading** (`LoadGameSlot`) is the exact inverse of saving, in the same
  order, and checks nothing (neither "FARM" nor the checksum: that was
  done at power-on). Each letter of the five names also goes to a second,
  16-bit copy ($08D5-$08FC, two bytes per letter). The farm is first
  reset to the default one (`LoadDefaultFarmMap`) and then the saved part
  is copied over it (tiles $C0 on).
- **A new game** (`NewGameSetup`) gives the same variables their
  starting values: year 1, spring, Monday the 1st, one bag of grass seeds,
  100 of stamina, 300 G (money is kept in tens: 30), the shed with the
  basic tools ($0F) plus the watering can and grass seeds ($88), every
  name blank (character $B1), the rest 0, and the default farm. The cows,
  the chickens and the children's 16-bit name copies are not touched.
  Quirk: the horse's name is blanked twice (the code was surely meant to
  do something else the second time).

### 5.17 Buttons and menus

Each frame `InputTypeSelector` looks at the **input mode** ($019A): 1
walking (5.22), 2 no movement, 3 a list inside a text box, 4 menus, 5 the
name screen, 6 a text box that closes by itself (with $019B bit 2, after
120 frames; then back to walking). In menus, the **menu number** ($95) says
which one is open:

| Menu | What the buttons do |
|---|---|
| 1 | the player is moved by an event's pad ($08FD); with $7F1F5A bit $8000, Start skips (menu 9, $7F1F5C bit 2) |
| 2, 3 | Start goes to menu 6 (when $7F1F5A bit $8000 allows it) |
| 5 | the title menu: three options, up and down wrap around; A opens menu 8, 7 or $0A |
| 7 | choose a slot to save: up / down switch the slot, B cancels, A accepts |
| 8 | choose a slot to load: the same, but A only works on a slot with a game (if not, a "refused" sound) |

The answer goes to $94 (1 = cancel, 2 = accept). Every move makes a sound
effect: 3 for moving, 1 for accepting, 2 for refusing.

**The list in a text box** (mode 3): 22 places in two columns of 11 and an
arrow. Up and down move inside the first column and wrap around (they do
nothing in the second); right and left jump to the other column. They use
the button's **auto-repeat**, so holding it keeps moving. A closes the list.

**The name screen** (mode 5). The letters are laid out in a grid with
three pages (the grids at $82:EBF8, $82:EE30 and $82:F0D0, with 71, 84
and 49 cells). Each cell has 8 bytes: its x and y, the cell each arrow
leads to, the letter, and a code (1-3: show page 0-2, 4: done). The arrows
(with auto-repeat) move the cursor $0991 to the cell the grid says, so the
moves are "precooked". A adds the letter (up to 4, at $0885) or obeys the
code; "done" needs at least one letter. B erases the last letter (an empty
letter is $B1). Switching page scrolls BG1: the three pages are in the
same picture, page 1 to the right of page 0 and page 2 below it.

**While a text box is open** (mode 2, `ImmobileInputs`):

- While letters are being printed, **holding A** makes the next letter
  come at once: that is how you speed up the text.
- When the text waits for a button, A wipes the box and goes on.
- At the end of the text, A closes the box and you can walk again.
- In a **choice**, the options are laid out in two columns of three; up
  and down move inside a column, left and right jump to the other one,
  and A closes the box with the chosen option in $018F.

### 5.18 Scrolling the map

A map can be bigger than the screen, and bigger than what video memory
holds. The whole map is in WRAM (`$7E:2000`, see 5.11); video memory keeps
only a **window** of 64 × 32 tiles around the camera. When the camera
moves, the game brings in the row or column that is about to come into
view, a little before you see it.

**Every frame, for each direction** (down `UpdateBGOffset`, up
`SUB_80A0E1`, right `UNK_StaticMapScroling`, left `SUB_80A152`):

1. The camera position ($F5 across, $F7 down) is copied to the BG1
   offset ($013C / $013E): that is what makes the picture move.
2. If the camera is at one of its limits ($ED left, $EF up, $F1 right,
   $F3 down), nothing else happens.
3. The speed $1E is capped so it does not go past the end.
4. If the map scrolls (bit 0 of $0196), the speed is added to (or taken
   from) a **counter of pixels inside a tile** ($20 across, $21 down,
   0 to 7). When it reaches 8 (or goes below 0) it wraps, and a new line of
   tiles is needed.

**The new line** (`SUB_80A18D` the row below, `SUB_80A308` the row above,
`SUB_80A481` the column at the right, `SUB_80A617` the column at the left):

- The map in WRAM is stored in blocks of 32 × 32 tiles (one "screen"). A
  row or a column of the window usually crosses from one screen to the
  next, so it is copied in **two runs** that each jump to the next screen
  when they reach its edge.
- The 32 + 32 tiles go into two buffers ($0746 and $07C6) and two DMAs are
  queued (channels 0 and 1) to send them to video memory at $6000 + the
  position. For columns the DMA uses the "step 32" mode, so the tiles
  land one under the other.
- Then the position in video memory ($14 rows, $10 columns) and in WRAM
  ($16 rows, $12 columns) moves on, wrapping around the window.

| Variable | What it holds |
|---|---|
| $10 / $14 | where the next column / row goes in video memory |
| $12 / $16 | where it comes from in the WRAM map |
| $1A | the length of a map row |
| $1C | the camera's offset inside the row |
| $1E | the camera speed (pixels per frame) |
| $20 / $21 | pixels inside the tile (0-7), across / down |

**Entering a map** (`SUB_809EBC`): the camera is placed so the player is
in the middle of the screen (128 pixels), unless that would show past a
limit; then it stays at the limit and the player is off centre. $090B /
$090D say where the player is on the screen.

**The second background, BG2** (`SUB_809F61`, every frame):

- in the tool shed (map $26), nothing;
- on the maps from $31 (the mountain top and others) a parallax: across,
  BG2 follows the camera; up and down it moves at half speed. It also
  doubles the camera speed;
- on the other maps it depends on $0196: bit 1 or bit 3, BG2 is fixed to
  the camera; bit 2, **the clouds**: they move with the camera and every
  10 frames drift one pixel to the right and one up.

### 5.19 The colours of the day

The SNES has 256 colours on screen at a time (the **palette**, kept in the
console's CGRAM): the first 128 for the backgrounds, the other 128 for the
sprites. A colour is 15 bits: 5 of red, 5 of green, 5 of blue (0-31 each).
The game keeps three copies in WRAM:

| Where | What |
|---|---|
| `$7F:0900` | what is sent to the console |
| `$7F:0B00` | a palette being prepared (the night one) |
| `$7F:0D00` | the colours shown now |

**Each map has a palette for each part of the day** (the table `$80:BB5C`,
6 bytes per map): before 7:00, 7:00-14:59, 15:00-16:59, 17:00-17:59 and
18:00 or later. $FF means "keep the one there is" (indoors, for example,
the palette does not change).

**The change is slow**:

1. When the hour enters another part of the day, `SetPaletteToLoad` sees
   the map has another palette and calls `SetTimeofDayPalette`, which
   points `$04-$06` at it. From 18:00 the target is the night palette
   already prepared in `$7F:0B00`.
2. `TransitionTimeofDayPalettes` runs every frame while time runs. Every
   **32 frames** it moves each colour **one step** in red, in green and in
   blue towards the target, and sends the palette to the console by DMA.
   Since a component can be 31 steps away, a full change can take
   31 × 32 frames, about 16 seconds.
3. When no colour changed, the change is over: the target is dropped and
   the new palette is noted as loaded (`$017B`).

Before 18:00 only the backgrounds' half changes; from 18:00 the sprites'
half too. Colours 1-11 (1-15 when bit 2 of $0196 is clear; HM-Decomp calls
that bit "sunny") are never touched: they are probably the ones of the text
box and the window, which must not darken.

**The night palette** is prepared by `UNK_CustomisePalette` when entering a
map: the backgrounds from the map's "18:00" palette and the sprites from its
night sprite palette (table `UNK_Table11`, $80:BE44, two per map: day and
night).

**Some sprite colours are made to measure** (`CustomiseSpritePalette`):

- Three colours of the last sprite palette (row 15, colours 10-12) change
  with the **season** (on the mountain maps $10-$13, with the map instead)
  and with the night.
- If the wife is pregnant or there is a child, three colours of row 11
  (8-10) change with the **stage** (the first bit set of $7F1F66, bits 0-4)
  and with the night: the baby's sprite.
- "Night" for these colours means: from 18:00 on an outdoor map (below $15,
  or $31 and above). Indoors the sprites keep their day colours.

*Note:* HM-Decomp puts `CustomiseSpritePalette` at $80:93A3, but that byte
is the `RTL` of the routine before it; the game calls $80:93A4.
`CustomiseSpritePalette` also reads the byte $81 without setting it; in the
game it is always 0, so it changes nothing.

### 5.20 Bumping into things

Before a step, the game asks three questions. The inputs are in the direct
page: $DF / $E1 the position (x, y) of who moves, $E3 how far ahead to look,
$E5 / $E7 the step, and A the direction (0 down, 1 up, 2 right, 3 left).
HM-Decomp's button names swap left and right: on the SNES pad bit $0100 is
Right and $0200 Left, so its `PlayerActionLeft` is really "right".

- **The map** (`CEEEE`): it tests the two corners of the side that moves,
  6 pixels to each side of the middle and $E3 + the step + 6 pixels ahead.
  Each corner (`CFFFF`) reads the tile's property:

  | Bit | What is solid |
  |---|---|
  | 0 | the whole tile |
  | 1 / 2 | the left / right half (x & 8) |
  | 3 / 4 | the top / bottom half (y & 8) |
  | none | nothing; but carrying something, tiles $C0-$CF block |

  The answer is (first corner) × 2 + (second corner). When something
  blocks, the routine also says how far into the tile the corner is, so
  the caller can stop right at the edge or slide along it.
- **The objects** (`CBBBB`): the 49 event slots ($7E:B586). One is in the
  way if its position is within 12 pixels of the point looked at, on both
  axes. The first ones found get a mark (byte +$0C = 1). Objects with
  bit 1 of their byte +0 do not block. With $7F1F60 bit $4000 nothing
  blocks.
- **The player** (`CDDDD`): the same 12-pixel test against the player's
  position; it is what the animals and people use.

### 5.21 Pressing A

When the player presses A facing something, `SUB_82AD0E` looks at the type
of the tile in front (0 if the tile does not face the player's side) and at
its 4 bytes in the map's property table:

| Tile type | What A does |
|---|---|
| $F0-$FF | a door: if byte +1 bit 0, go to place [+0] ($096E; $D2 bit 6 starts the change of map) |
| $E0-$EF | nothing |
| $D0-$DF | a sign: shows text [+0] (a word) |
| $C0-$CF | a door with opening hours: shows text [+1] when the place is closed (see below) |
| the rest | picks the thing up if byte +1 bit 7; otherwise talks to whoever is in front |

**Opening hours.** Byte +0 picks a rule in the entrances table
(`Unk_Table13`, $80:B6F5, 8 bytes each; byte 3 is the rule). Each bit
closes the place at one time:

| Bit | Closed |
|---|---|
| $02 | from 17:00 |
| $04 | before 17:00 |
| $08 | Monday to Friday |
| $10 | Saturday |
| $20 | Sunday |
| $80 | when $0196 bit 4 is set |

For example the shops have $32: closed on Saturday, on Sunday and from
17:00.

**Picking things up:** the item in hand becomes
`DATA8_82CFB4[type × 4 + season]` (the same tile gives a different thing
each season), the tile's byte +0 goes to $091F and the player's action
becomes 4 (holding it). In autumn and winter the tiles $38, $39, $52, $53,
$60, $61, $6E and $6F give nothing.

**Talking:** with none of that, the routine asks `CBBBB` (5.20) whether an
event object is 16 pixels ahead; the object found gets its byte +$0C = 1,
and its own script (a person, an animal) reacts to it.

**Actions** ($096E). A door-like tile can carry an action instead of a
place; `PlayerInteractionSelector` runs it from the table $81:D593. Of its
14 entries only four do something: 2 takes lumber from the wood stack
(`GetFenceFromStack`: 2 pieces of wood, and you carry item $57), 4 is
`CODE_81D670`, and 6 / 7 start map animations $11 / $12. The rest are a
lone RTS (`Unused01`-`Unused11`). Each clears $D2 bit 6 when done.

### 5.22 The buttons while walking

In input mode 1 (5.17), `PlayerMovementInputs` ($84:C6B7) reads the pad
every frame. HM-Decomp writes it as many labels (`PlayerActionKeysPressed`,
`PlayerActionDown`, `PlayerKeyA`...) joined by jumps, but it is one routine
with many exits.

**First, can the player act?** Not while $D2 has bit 2, 6 or 7, not in the
air (action 3), not with $7F1F5C bit 5, and only with $D2 bit $4000. While
fishing (actions $11, $12) only Y counts. And many actions (holding up an
item, eating...) do not let the player move: the table
`PlayerActionCanInteract` says which (1 = busy).

**The arrows** set the action "walk" ($D4 = 1) and the direction ($DA:
0 down, 1 up, 2 right, 3 left). Then one button is looked at, in this
order:

| Button | What it does |
|---|---|
| A | talk, read, open, pick up (`SUB_82AD0E`, 5.21); with the dog in the arms, let it go in front (action $16; the dog's place and map are saved); with something in the arms, put it down on a free side (action $18); holding an item, drop it (action 5) |
| B (held) | run (action 2); in front of a tile with property bit $20 or $40, jump over it (action 3, 28 or 33 pixels) if there is room to land |
| Select | show the item in hand (action $0C) |
| X | action $1C |
| R | whistle for the horse, if you have one ($7F1F68 bit $100; action $0D) |
| L | whistle for the dog, if you have one (bit $80; action $1B) |
| Y | use the tool (action $0A, `UNK_ToolUsed`); with the rod: cast ($0F → $10), reel in empty ($11 → $0F), or with a bite ($12 → $13) one of four catch animations at random |

R, L, Select, X and Y do nothing while holding something ($D2 bits 1, 4,
11).

**During events** ($D2 bit $1000) the pad is not read: the event's script
"presses" the buttons in $08FD instead, with the same bits.

**The pad's bits** ($0124 held, $0128 new): B $8000, Y $4000, Select
$2000, Start $1000, up $0800, down $0400, left $0200, right $0100, A $0080,
X $0040, L $0020, R $0010. (HM-Decomp swaps left and right, see 5.20.)

**Dead code:** after each arrow's jump there are a few instructions nothing
reaches, and the block at $84:C852-$84:C934 (it would turn $0911 into
actions 6 and 7) is never called. They count as part of the routine.

### 5.23 Colours that move

Water that shimmers, lights that blink: the game changes single colours of
the palette, step by step (palette cycling, `src/coloranim.c`).

- There are **16 channels**. Each has a pointer to a list ($42 + 3 n), the
  colour it changes ($015A + n: colour, $016A + n: palette row) and how
  many frames are left of the current step ($014A + n).
- A list is made of steps of 3 bytes: the new colour (a word) and how many
  frames it stays (a byte). $FFFE followed by a pointer jumps (so the list
  loops); $FFFF ends it.
- `UNK_BigLoop` runs every frame: it advances every channel and, if one
  ran, sends the palette to the console by DMA. Channels 0-3 only run
  while it rains or snows. From channel 4 on, the first empty channel ends
  the loop, so they are always used in order.
- `CallIndexedSubrutines` starts the animations of the map for the part of
  the day, from `Subrutines_Table` ($80:BEEC): 6 words per map (5 parts of
  the day, then a word that, when it is not $FFFF, means "only in autumn
  and winter"). Each word is one of 51 small routines, each starting a few
  channels (`UNK_SetPointer42`: list, colour, row, channel).

*Quirk:* after the end mark $FFFF the channel's pointer is cleared, but the
same frame goes on and still writes colour $FFFF and moves the cleared
pointer to $00:0003. Since all the lists end in a loop ($FFFE), it never
happens.

### 5.24 Reading the controller

Every frame `ReadJoypad` ($80:872B, `src/joypad.c`) reads the two pads and
leaves in WRAM everything the rest of the game asks about the buttons:

| Address | What it holds (pad 1) |
|---|---|
| $0124 | the buttons held now |
| $0126 | the ones held last frame |
| $0128 | the ones pressed this frame (held now, not before) |
| $012A | the ones let go this frame |
| $012C | "autorepeat": the new presses, plus every held button again once in a while |
| $23 | how many frames some button has been held |

The autorepeat is what makes a menu cursor keep moving when you hold the
D-pad: after 30 frames (half a second) held, the held buttons count as
pressed again, and the counter goes back to 25, so from then on it repeats
every 5 frames. Pad 2 gets the same words ($0130-$0138), but nothing reads
them: Harvest Moon is for one player.

Before reading, it waits until the console has finished reading the pads
by itself ($4212 bit 0). For the check, the rewrite asks the emulator for
those registers ($4212, $4218, $421A); the fuzzer also presses random
buttons on both pads for each call and puts the real ones back after.

### 5.25 What a map gets when it loads

Each map's layout is stored once in the cartridge. What changes during the
game (your house extensions, the tools you bought, the fodder you put
out...) is added on top every time the map loads: `UNK_ExecuteFromPointers`
($81:A9E5, `src/maptiles.c`) calls that map's routine from a table (one
word per map, $00-$5F). Most maps have nothing to add and point at a lone
RTS.

| Map | What it adds |
|---|---|
| Farm ($00-$03) | the house's first or second extension, one power berry flower for each berry eaten (10 places), the painted house (4 parts, $7F1F66 bits 9-12) and three more things ($7F1F64 bits 0-1, $7F1F68 bit 12) |
| Mountain ($10-$13) | one thing with rain or snow, and one more when $7F1F64 bit 1 is set |
| House ($15-$17) | three things: $0196 bit 4, $7F1F6C bit 12 (HM-Decomp calls it the turtle shell) and the clock ($7F1F6E bit 14) |
| Flower shop ($1C) | the seeds on the shelves: summer seeds in summer, spring seeds in spring, and two more in both |
| General store ($22) | every tool for sale you have not bought and are not carrying: the paint (once the house has its second extension), the milker, the brush, the four gold tools (once they are on sale) and the sprinkler (from summer day 20 of the first year) |
| Animal shop ($24) | the miracle potion (only with fewer than 12 cows and none pregnant), the medicine and the two fodders |
| Tool shed ($26) | every tool you own, at its place on the wall (24 places) |
| Barn ($27), coop ($28) | fodder in every trough you filled; in the coop also an egg by each hen that laid one |
| Mountain cave ($29), elf tunnel ($2A), map $37 | small things behind flags; map $37's only the first time |

The same flag, $7F1F64 bit 1, changes the farm, the mountain and the elf
tunnel: something you do that shows in three places.

Three pieces do the work:

- `SUB_81A7CE` stamps a tile change (picture and tile type) at a position.
  It is `ChangeMapTile` without sending anything to the VRAM: while a map
  loads, the whole picture is sent afterwards.
- `SUB_82B0A7` then writes the tile type of every tile of the change, from
  a second sheet at $A7:F500 laid out like the graphics (for changes made
  of different tiles, like an extension with its door).
- `SUB_81B2BB` shows an item of a list (the shed, the flower shop, the
  general store, the animal shop; 8 bytes each: position, change, and a
  bit of $7F1F00-) always, only if its bit is set (you own it), or only if
  it is not (still for sale).

*About HM-Decomp:* four of these routines appear there as data (`dw`
lines): the coop's ($81:B121), the cave's ($81:B1CF), the tunnel's
($81:B21E) and, as an indented label, map $37's ($81:B26D). They were
disassembled from the ROM. The first three are verified but do not count
in the percentage, which measures HM-Decomp's own code. Also,
`ReplaceTilesMoutain` starts at $81:AC80, not $81:AC7F (that byte is the
RTS of `ReplaceTilesForkWinter`), and `MapPointerTable` is at $82:B174.

---

## 6. Bugs and quirks of the original game

Things the game does wrong or oddly, found while decompiling:

1. **A hidden `BRK`** in the cows' night ($83:BDE1). If a cow was left
   outside and there was also a broken-fence night, the X register is left
   in 8 bits. Then an instruction is read one byte short and the processor
   executes a `BRK` (an error interrupt).
2. **The ending's score counts wrong**:
   - Hearts and shipped crops are **cut off** instead of capped: 512
     turnips shipped are worth 0 points and 600 hearts count as 88.
   - Only the low 16 bits of the money count: with 655,360 G your money
     is worth 0.
3. **Hang with 10 effects.** If the 10 effect places are in use and
   another is needed, the game gets stuck in a jump to itself ($81:A528).
4. **The die is not fair** (see 5.1).
5. **"21th", "22th", "23th"** in the date.
6. **Any unknown item "works"** as a tool.
7. **The gold axe** skips the "no finds today" check.
8. **The gold sickle in winter** does not advance its step when it lands on
   a tile it could cut.
9. **The horse** uses a different "is it on this map?" rule from the other
   animals.
10. **The grown-grass counter** only goes down in the first 4 rows of the
    field.
11. **`ChangeStamina`** decides the sign from the caller's N flag.
12. **`ChangeMoney` reads the amount twice:** it keeps the second, and the
    argument's first 3 bytes are ignored. And if the result would be
    negative, it sets the money to 0 instead of refusing.
13. **Unused commands:** the command table points three times at
    `ResetFlag`, so `ResetFlagD` and `ResetFlagDD` are never reached;
    `Event_UNKTools` (puts the tools back in the shed) is not in the table
    either.
14. **`RemoveProgrammedDMA` uses the whole A as an index.** In 8-bit mode,
    `TAX` copies the hidden high byte of A too, so the channel table is
    read at (high byte : channel). It works only because every caller has
    0 there.
15. **The width of the letters is ignored.** The printer passes "wide
    letter: yes / no" to `DrawLetter` and `AdvanceLetterCursor`, but both
    start by setting it to 0. Every letter is 8 pixels wide. (Probably a
    leftover of the Japanese version, whose characters were 16 wide.)
16. **After a number or a name the printer skips two codes in 8 bits**
    (only the low byte of the position), so in a text of more than 256
    codes it would jump backwards (no text seen so far is that long).
17. **The cow that goes from the farm to the barn gets a wrong position.**
    The table of barn places has x and y for each cow, but this code reads
    the same word twice (it forgets to move to the next one), so the
    position written for the cow is (x, x).
18. **The flag "a save was reset"** ($7F1F60 bit $0800) is only set when
    *both* slots fail the check, not when one does (the code looks at slot
    1's result only on slot 2's failure path).
19. **The camera's speed cap looks at the wrong side.** Going down (or
    right), the speed is capped by the camera's position, that is, the
    distance from the *top*, not what is left to the bottom; going up (or
    left) it is capped by the distance to the *bottom* limit. Since the
    camera moves one or two pixels per frame, it only shows in the first
    pixel or two of a map.
20. **A signed division nobody uses.** `DivisionSigned` ($83:80D0) divides
    with signs (the quotient negative when one of the two is; the remainder
    always positive), but nothing in the game calls it. It is rewritten all
    the same.
21. **Pad 2, read with pad 1.** `ReadJoypad` works out pad 2's "let go"
    buttons ($0136) from pad 1's register ($4218) instead of pad 2's: a typo.
    It does not matter, pad 2 is never used. The same routine also has a
    branch that never runs (when $00 is not 0) that would read only the low
    bytes of the pads.

22. **A sum thrown away.** The flower shop's shelf loops do `TYA; CLC;
    ADC #5` and then `TYA` again, so the sum is lost and the item is just
    Y + 1. Maybe the shelves were meant to start at item 5 at first.
23. **A loop that never ends.** When a piece gives its graphics slot back,
    the game waits while the slot's counter is 0 before lowering it (`LDA;
    BEQ` to itself, `CODE_858C9F`). At 0 it would hang forever; it never
    happens because a slot being given back is always in use.

## 7. Differences found in the recompilation

- **`SEP #$10` does not clear the high byte of X and Y.** On the real
  SNES, switching the indexes to 8 bits clears their high byte; in the
  recompilation it is kept. It does not seem to change anything visible
  in the game, but the checker notices it. The rewrites imitate the
  recompilation, which is the reference.
- **A 16-bit read at the last byte of bank $7E.** On the SNES, reading two
  bytes at `$7E:FFFF` takes the second from `$7F:0000`; in the
  recompilation it comes from `$7E:0000`. The game never does it (it was
  found only by random tests of the scroll routines, which then avoid it).
- **The interpreter gives up quietly.** Routines with no compiled body run
  in the recompiler's interpreter, which stops a routine after 2 million
  steps (to escape endless loops) and still reports it as finished. The
  sound uploads take far more while the chip answers, so the checker now
  counts a run as finished only if the routine really returned, and the
  fuzzer raises that limit. The log of the sound chip's traffic was also
  made 64 times bigger (4 million reads and writes): every wait reads the
  port thousands of times.

## 8. Glossary

| Word | Meaning |
|---|---|
| ROM | the contents of the cartridge |
| WRAM | the console's working memory |
| SRAM | the cartridge's memory where the game is saved |
| register | internal box of the processor (A, X, Y...) |
| flag | bit left by operations (C, Z, N) |
| bank | 64 KB block of memory; `$7F:1F19` = bank $7F |
| stack | memory area where return addresses are kept |
| JSL / JSR | call a routine (long / short) |
| direct page | 256 bytes of fast variables (`$72`, `$CC`...) |
| long pointer | 3-byte address kept in memory |
| frame | each picture; the game runs at 60 per second |
| NMI | the hardware's signal at the end of each frame |
| DMA | a memory copy done by the hardware |
| sprite | a figure that moves over the background |
| slot (CC) | structure where an event script runs |
| fuzzing | testing a routine with random inputs |
| interpreter | the part of the recompilation that runs, instruction by instruction, what is not compiled |
| open bus | reading an address where nothing answers: you get the last value seen on the bus |
| BG3 | the third background layer; the game uses it for text |
| port | a byte shared by two processors (the CPU and the sound chip) |
| handshake | the "ready / here it is / done" exchange over the ports |
| palette | the 256 colours the screen can use at a time |
| CGRAM | the console's memory where the palette is kept |
| LZSS | a way to compress: "copy this byte" or "copy that many bytes from what you already wrote" |

## 9. Progress

| Date | Progress | What was added |
|---|---|---|
| 19-Oct-2026 | — | weather, farm damage |
| 20-Oct-2026 | 7.94 % (old measure) | growth, animals, the whole night |
| 21-Oct-2026 | 7.29 % | corrected measure |
| 21-Oct-2026 | 7.69 % | DMA, changing tiles |
| 21-Oct-2026 | 8.26 % | objects with a sprite |
| 21-Oct-2026 | 12.10 % | the tools |
| 21-Oct-2026 | 14.41 % | animals on the map, the ending |
| 21-Oct-2026 | 17.35 % | event commands, dog, chicken |
| 21-Oct-2026 | 20.45 % | 31 more event commands (values, waits, picking things up) |
| 21-Oct-2026 | 20.33 % | corrected measure (routines written together are split) |
| 21-Oct-2026 | 20.85 % | the text box: open, close, the arrow; two DMA helpers |
| 21-Oct-2026 | 21.31 % | the letters: font, drawing, cursor, numbers |
| 21-Oct-2026 | 21.59 % | the checker records the sound chip; 7 sound routines |
| 21-Oct-2026 | 21.86 % | the watering can and the sprinkler: all 28 tools done |
| 21-Oct-2026 | 21.91 % | the routine that picks the tool's routine |
| 22-Oct-2026 | 22.49 % | the text printer (checked live: 2,424 frames of text in a new game) |
| 22-Oct-2026 | 25.96 % | the cow's every frame (1,567 instructions) |
| 22-Oct-2026 | 27.28 % | the saved games (the checker now compares the SRAM) |
| 22-Oct-2026 | 28.16 % | the buttons in the menus |
| 22-Oct-2026 | 28.72 % | the buttons while a text box is open |
| 22-Oct-2026 | 30.61 % | scrolling the map (checked live: about 1,200 calls in a new game) |
| 22-Oct-2026 | 32.17 % | the colours of the day (checked live: about 13,000 calls) |
| 22-Oct-2026 | 33.34 % | bumping into things (checked live: about 2,900 calls) |
| 22-Oct-2026 | 33.80 % | the tools' animations (29 routines) |
| 22-Oct-2026 | 34.31 % | pressing A; a new script (`scripts/actions.txt`) checks the tools live |
| 22-Oct-2026 | 36.10 % | the buttons while walking (the 15 labels of that block now count as one routine: 519 routines in all) |
| 22-Oct-2026 | 36.92 % | `InputTypeSelector`, the name screen, menu 1, mode 6: every input mode is rewritten; checked live every frame (about 21,000 calls) |
| 22-Oct-2026 | 37.26 % | drawing the whole map, with fences joining their neighbours |
| 22-Oct-2026 | 38.35 % | event commands 50-59 (the 9 without a label in HM-Decomp now count by their address) |
| 22-Oct-2026 | 39.25 % | event command 37: the carried object of group $17 (probably the child) |
| 22-Oct-2026 | 39.37 % | the signed division (unused) |
| 22-Oct-2026 | 39.59 % | the decompressor (LZSS); the checker now also knows all the flags at the entry, for `PHP` |
| 22-Oct-2026 | 40.09 % | placing the camera on entering a map, and the second background (clouds, parallax) |
| 22-Oct-2026 | 40.54 % | five more event commands (texts, clearing a bit, a tile, the shed); more than half of the routines are done |
| 22-Oct-2026 | 47.73 % | the colour animations: 57 routines (51 of them, small, read from a table) |
| 22-Oct-2026 | 47.87 % | reading the controller (the buttons pressed, let go, and the menus' autorepeat) |
| 22-Oct-2026 | 47.97 % | two unused routines that count the chickens and the cows |
| 22-Oct-2026 | 48.04 % | the noise of each tool |
| 22-Oct-2026 | 48.18 % | the farm's copy of the field, freeing all the script slots, the multiplication |
| 22-Oct-2026 | 50.23 % | what each map gets when it loads (39 routines); past half of the code |
| 22-Oct-2026 | 52.99 % | the music and the uploads to the sound chip (13 routines), the shipping at 5 pm, the player's actions, points into hearts |
| 23-Oct-2026 | 53.48 % | destroying a game object, and the script commands that do it |
| 23-Oct-2026 | 54.03 % | four script commands that choose animations and make a person face you |
| 23-Oct-2026 | 54.42 % | moving a game object, changing its sprite, a slot's walking step |
| 23-Oct-2026 | 54.60 % | flipped sprites, emptying the sprite tables, reading an object back |
| 23-Oct-2026 | 54.80 % | footprints in the snow, the map animation player |
| 23-Oct-2026 | 55.98 % | the slots that wander on their own (the town's people, the animals) |
| 23-Oct-2026 | 56.14 % | each slot's sprite, every frame |
| 23-Oct-2026 | 56.76 % | talking to people (commands 1E, 1F, 2D), a map animation and the music from a script; count corrected (−0.04) |
| 23-Oct-2026 | 57.86 % | animating the game objects, the drawing order, emptying the sprite table |
| 23-Oct-2026 | 59.10 % | loading a saved game and starting a new one |
