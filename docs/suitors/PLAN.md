# Male suitors — research and plan

*(Español: [PLAN.es.md](PLAN.es.md))*

A planned improvement for the native build, after the Harvest Goddess
(`native/goddess.c`): single men of the town the player can court and
marry, the same way as the girls.

**Done so far:** step 1 (hearts and gifts) for Eve's fan, in
`native/suitors.c`, with its own option (`MaleSuitors` in `config.ini`,
"Male suitors" in the launcher). It builds, but it **has not been tried in
the game yet**: the first test is to go to the bar at night, talk to him
and give him something (see "To try it" at the end).

## What was found

Sources: the game's 1,280 texts (`tools/dump_texts.py rom.sfc out.txt`
writes them out; text $nnn is the index of Text_Pointer_Table, $83:9BF6)
and the people each map starts (the event slots, $7E:B586 + 64 n, read
from RAM dumps on every map at 11:00 and 19:00).

`townsfolk_sprites.png` shows sprites $250-$2E1. **Its colours are wrong**
(drawn with the Goddess's palette); the shapes and poses are the real ones.

### Candidates (single, with full poses: front, back, side)

| Who | Sprites | Where | Why single |
|---|---|---|---|
| Eve's fan | $269-$26F **or** $270-$274: the range $269-$274 is **two people** (see below); which one is in the bar is still to be seen | the bar, map $1E (slot 4) | text $120: "Good evening. I'm a fan of Eve's, you know." |
| The carpenters (brothers) | $25A-$264 (also carrying wood, with the hammer) | the mountain, map $10; their house, map $2B | texts $128/$129: "We are carpenters"; live together, no wife mentioned (the game never says) |
| Ann's father | $296-$29C (brown hair, blue bib): front, back, side, walking and talking, all of them | the tool shop | a widower (texts $1DC, $246) |
| The reporter | $269-$26F (light jacket, red tie): all the poses | — | the game gives him no wife |
| A shipping worker | $270-$274 (blond, white shirt, dark vest): all the poses. One of the two shippers: whether he is the trainee (text $117/$118: "I'm a trainee of shipping too... With grit!") or the boss is still to be seen; the other (blond, with a cap) is not among $250-$2E1 | the farm (collects what is shipped) | — |

**Who is who** was checked against the sheet names on The Spriters Resource
(Harvest Moon, SNES: "Reporter", "Shipping Workers", "Tool Shop Owner",
"Mayor", "Parents", "Bartender", "Fisherman"), only by looking at them:
nothing was copied from them. That corrected what was here before: $28F-$295
(moustache) is the **mayor**, not the shipper; $2D9-$2DE and $2DF-$2E1 are
**the hero's parents**, not Ann's father; and $269-$274 is not one person.

**Mind `native/suitors.c`:** Eve's fan is set as $269-$274, which covers the
reporter and the shipping worker too. It must keep only his own range once
it is known which one he is.

The first step agreed: **Eve's fan and one of the carpenters**; then Ann's
father, and the reporter and the shipping worker if they turn out single.

### Ruled out

- The bartender ($285-$287): only drawn from the front, behind the counter (wiping a glass); no back, side or walking poses, so he could not walk, marry or live on the farm without new drawings.
- Ellen's father ($2A4, the drunk: "hic").
- The mayor ($28F-$295, the one with the moustache; Maria's father): married.
- The fisherman ($265-$268): only drawn sitting and fishing, from the front (what he holds is a rod, not a walking stick); no back, side or walking poses.
- The hunter ($27D): Eve's grandfather.
- The priest ($2A5); the hawker and the peddler (they do not live in town).

## How it would be built (same pieces as the Goddess)

1. Hearts and gifts: his favourite things, his lines by hearts, in the
   game's own text box (`native/textbox.c`).
2. Proposal: the Blue Feather (`g_hm_feather_used`), with the game's
   conditions (hearts, the house extended).
3. Wedding: the game's scene (event group $1E, map $2D) with him dressed
   in the bride's slot through `g_hm_slot_picture`, as the Goddess.
4. Married life: a husband's day (home in the morning, out from 9:00,
   home at 18:00), walking with his own poses (not floating).
5. Children: the game only has a birth; for two men, an **adoption**
   scene of its own (for example, the Harvest Sprites bring a baby);
   the children then grow with the game's system (`hm_WifePregnancyAndChildren`).
6. The ending: lend the wife's bit for the last day, as the Goddess
   (`lend_for_ending`), and dress the wife's figures as him.

Off by default, with its own option in `config.ini`, like `HarvestGoddess`.

## To continue in another session

Open the `male-suitors` branch (started from `harvest-goddess`) of
RodoLy/HarvestMoonSnesDecomp and ask to read `docs/suitors/PLAN.md` first.

## To try it

1. Turn on "Male suitors" in the launcher's improvements.
2. Go to the bar at night. Press A in front of Eve's fan: his line comes
   in the game's text box (not his own "I'm a fan of Eve's" any more).
3. Pick up Wild Grapes, a Fish or a Cake and press A in front of him: it
   leaves your hands and he is very happy. A second gift the same day stays
   in your hands.
4. Save, load: his love is kept (there is no hearts row for him yet in
   the pause menu; it can be added next, with his face).

If A in front of him does nothing different, his sprite numbers or the
bar's map number ($1E) are not the ones read from the RAM dumps: they are
at the top of `native/suitors.c` (`kSuitors`).

