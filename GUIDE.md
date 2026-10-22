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

- **The video hardware**: its registers are not memory. What the game
  writes there is not compared, except the DMA registers.
- **The sound chip, solved by recording.** Its answers come from another
  processor and cannot be computed. So, while the original runs, the
  checker writes down every read and write on the sound ports, in order.
  Then, when the rewrite runs, each port read gets the value the original
  got at that same point, and each write must be exactly the one the
  original made (same port, same value, same order). One difference and
  the check fails. In the random test, these routines are tested in a
  calmer moment of the game (300 frames earlier), because sometimes the
  chip is busy loading music for a long time and never says "ready".
- **What runs in the interpreter.** Some routines have no compiled
  version: the recompilation runs them in an interpreter. The live
  checker does not see those calls, so the random test is the
  verification (it calls the original through the interpreter).
- **Reads outside the cartridge.** If a random test makes a routine read
  a table so far away that it leaves the ROM, it lands on addresses with
  nothing there (the "open bus": you get whatever was last on the wires).
  That never happens in the game, so the preparations keep the inputs
  inside the ROM.

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

### 5.5 The tools

**Does it work here?** Each map has a property table: 4 bytes per tile
type, which say which tools work and from which sides and in which
seasons. Bits: sickle 01, hoe 02, hammer 04, axe 08, water 10, seeds 20.
`CheckToolSuccess` answers: 1 works, 0 doesn't, 2 the watering can gets
refilled.

**Quirk:** any item that is not on the list "works".

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

### 5.8 People

- **Hearts** of the five girls: `$7F:1F1F + 2·n` (0-999), in the order
  Maria, Ann, Nina, Ellen, Eve. Talking to them adds +8.
- **The favourite** (`FindMostLovedName`): on a tie the last one wins.
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

### 5.12 Objects with a sprite

- **Game objects** (`$019C`, 40 of 36 bytes): everything with a sprite on
  screen. `CreateGameObject` takes a free one.
- **Sprites:** each is built from 16 × 16 pieces (up to 12). Numbers
  0-$261 are described in bank $86 and $262-$44F in bank $87.
- **Piece graphics:** if a piece's picture is already in video memory it
  is reused; otherwise it goes to a queue the NMI empties.
- **Map objects** (`$7E:B4E6`, 10): the effects tools leave, like cut grass
  or the hit on a rock.

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

The main printer, which reads the text code by code (`ADDDDFFFF` in
HM-Decomp, 261 instructions), also sends a sound effect for each letter
(`UNK_Audio19`, see 5.15).

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

## 7. Differences found in the recompilation

- **`SEP #$10` does not clear the high byte of X and Y.** On the real
  SNES, switching the indexes to 8 bits clears their high byte; in the
  recompilation it is kept. It does not seem to change anything visible
  in the game, but the checker notices it. The rewrites imitate the
  recompilation, which is the reference.

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
