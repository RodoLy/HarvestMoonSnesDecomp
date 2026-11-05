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
| Eve's fan | $269-$274 (young man, short hair) | the bar, map $1E (slot 4) | text $120: "Good evening. I'm a fan of Eve's, you know." |
| The carpenters (brothers) | $25A-$264 (also carrying wood, with the hammer) | the mountain, map $10; their house, map $2B | texts $128/$129: "We are carpenters"; live together, no wife mentioned (the game never says) |
| The shipping trainee | not confirmed yet; most likely $28F-$299 (young man, blue shirt, the most poses) | — | text $117/$118: "I'm a trainee of shipping too... With grit!" |

The first step agreed: **Eve's fan and one of the carpenters**, then the
trainee once his sprite is confirmed.

### Ruled out

- The bartender ($285): Eve's uncle (text $11E: "she is my niece"), not young.
- Ann's father (tool shop): a widower (texts $1DC, $246).
- Ellen's father ($2A4, the drunk: "hic").
- The mayor (Maria's father): married.
- The angler ($265): an old man with a cane (text $12B).
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

