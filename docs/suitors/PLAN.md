# Male suitors — research and plan

*(Español: [PLAN.es.md](PLAN.es.md))*

A planned improvement for the native build, after the Harvest Goddess
(`native/goddess.c`): single men of the town the player can court and
marry, the same way as the girls.

**Done so far:** step 1 (hearts and gifts) for four of them: Eve's fan
(the Visitor), a carpenter, Ann's father and the shipping worker in the
vest, in `native/suitors.c`, with its own option (`MaleSuitors` in
`config.ini`, "Male suitors" in the launcher). The diary keeps all four
(their love / 4, one byte each; see `src/save.c`). It builds, but it **has
not been tried in the game yet** (see "To try it" at the end).

| Who | Loves | Dislikes |
|---|---|---|
| Eve's fan (the Visitor) | Wild Grapes, Fish, Cake | the weeds, Poison Mushroom |
| A carpenter | Large Milk, Corn, Mushroom | Flower, the perfumes, Poison Mushroom |
| Ann's father | Potato, Egg, Herb | the weeds, Poison Mushroom |
| The shipping worker in the vest | Tomato, Tropical Fruit, Large Milk | the weeds, Poison Mushroom |

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
| Eve's fan: **the Visitor** (also called the Reporter or the "Lonely Bachelor") | $269-$26F (light jacket, red tie): all the poses | the bar at night, map $1E | called the "Lonely Bachelor"; text $120, at night in the bar: "Good evening. I'm a fan of Eve's, you know." |
| The carpenters (brothers) | $25A-$264 (also carrying wood, with the hammer) | the mountain, map $10; their house, map $2B | texts $128/$129: "We are carpenters"; live together, no wife mentioned (the game never says) |
| Ann's father | $296-$29C (brown hair, blue bib): front, back, side, walking and talking, all of them | the tool shop | a widower (texts $1DC, $246) |
| The shipping workers (two, both single) | one is $270-$274 (blond, white shirt, dark vest: all the poses; at night in the bar he gives tips: "You can't use the tool when you lose your stamina..."); the other (blond, white shirt, cap; at night in the bar: "Even if you put shipping goods in the box at night...") is not among $250-$2E1: his sprite and poses are still to be found. One of them is the trainee (text $117/$118: "I'm a trainee of shipping too... With grit!") | the farm (they collect what is shipped); at night, the bar | both single |

**Who is who** was checked against the sheet names on The Spriters Resource
(Harvest Moon, SNES: "Reporter", "Shipping Workers", "Tool Shop Owner",
"Mayor", "Parents", "Bartender", "Fisherman"), only by looking at them:
nothing was copied from them. That corrected what was here before: $28F-$295
(moustache) is the **mayor**, not the shipper; $2D9-$2DE and $2DF-$2E1 are
**the hero's parents**, not Ann's father; and $269-$274 is not one person: $269-$26F is the Visitor (Eve's fan) and $270-$274 a shipping worker.

`native/suitors.c` now uses only $269-$26F for Eve's fan.

The first step agreed: **Eve's fan and one of the carpenters**; then Ann's
father and both shipping workers (the second once his sprite is found).

### Ruled out

- The bartender ($285-$287): only drawn from the front, behind the counter (wiping a glass); no back, side or walking poses, so he could not walk, marry or live on the farm without new drawings.
- Ellen's father ($2A4, the drunk: "hic").
- The mayor ($28F-$295, the one with the moustache; Maria's father): married.
- The fisherman ($265-$268): only drawn sitting and fishing, from the front (what he holds is a rod, not a walking stick); no back, side or walking poses.
- The hunter ($27D): Eve's grandfather.
- The priest ($2A5); the hawker and the peddler (they do not live in town).

## How it is built (same pieces as the Goddess) — all done

1. Hearts and gifts: his favourite things, his lines by hearts, in the
   game's own text box (`native/textbox.c`).
2. His heart event (approved), like the girls': at a certain love, on a day
   of good weather and no festival, **someone comes to your farm to tell
   you**; you go and help, and solving it ends the day (the game's are like
   that: Maria runs away, Ellen loses P-Chan, Eve's grandfather is hurt in
   the cave, Ann loses her weather vane, Nina does not come back from the
   cave). Once each:
   - **Eve's fan:** Eve comes, worried: "My regular fan didn't come last
     night, and he never misses a night." He is on the mountain, by the lake,
     with a notebook: he was writing a song, not for Eve, for you.
   - **The carpenter:** his brother comes running: "My brother went up to
     cut wood and hasn't come down!" He is in the mountain's woods, a twisted
     ankle under some logs; you help him down. The next day he leaves you an
     oak stool.
   - **Ann's father:** Ann comes: "My dad went out this morning and hasn't
     come back." He is at his wife's grave, by the church: he tells you
     about her, and if you bring a Flower you leave it beside his (the most
     love); "staying a while" also raises it; "leaving" neither raises nor
     lowers it. The next day Ann: "My dad came home happy yesterday... do
     you know why?" If the game has no graves, a small headstone drawn by us
     by the church.
   - **The shipping worker in the vest:** the one with the cap comes: "My
     partner lost the bag with the day's payments, and if it doesn't turn
     up they'll fire him!" It is somewhere on his route (in town, behind a
     fence); he thanks you watching the sunset.

   **One carpenter only:** both brothers share the pictures; only one is a
   suitor (he says the new lines) and the others keep their own. He is the
   one whose script is his (bank $B3, $EE81-$EEA5: the one who says text
   $128), in `kSuitors` as `script_from` / `script_to`.
3. Dancing at the festivals: at the festivals where a girl can be asked to
   dance, asking him instead, with the hearts that brings.
4. Proposal: the Blue Feather (`g_hm_feather_used`), with the game's
   conditions (hearts, the house extended).
5. Wedding: the game's scene (event group $1E, map $2D) with him dressed
   in the bride's slot through `g_hm_slot_picture`, as the Goddess.
6. Married life: a husband's day (home in the morning, out from 9:00,
   home at 18:00), walking with his own poses (not floating).
7. Children: the game only has a birth; for two men, an **adoption**
   scene of its own (for example, the Harvest Sprites bring a baby);
   the children then grow with the game's system (`hm_WifePregnancyAndChildren`).
8. The ending, with him in the girl's place: lend the wife's bit for the last day, as the Goddess
   (`lend_for_ending`), and dress the wife's figures as him.

Off by default, with its own option in `config.ini`, like `HarvestGoddess`.

## The fifth: the trainee

The shipping worker with the cap ($276-$27B, all poses) is the vest one's
trainee, single as well. His heart event: the vest one comes to the farm
("my trainee went to deliver to the mountain and never came back"); he is
lost on the mountain path, and you walk him back (or tease him).

## Where each heart event happens

| Suitor | Who comes | Where he is |
|---|---|---|
| Eve's fan | Eve | the mountain lake, by the fence, looking at the water |
| Carpenter | his brother | the mountain woods, by the logs |
| Ann's father | Ann | the graves beside the church, looking at them |
| Shipper (vest) | the trainee | the town, by the fence on the road |
| Trainee (cap) | the shipper | the mountain path, past the big tree |

## To continue in another session

Open the `male-suitors` branch (started from `harvest-goddess`) of
RodoLy/HarvestMoonSnesDecomp and ask to read `docs/suitors/PLAN.md` first.

## To try it

1. Turn on "Male suitors" in the launcher's improvements.
2. **Hearts and gifts.** Press A in front of each: Eve's fan (the bar, at
   night), the carpenter (the mountain, by his house), Ann's father (the
   tool shop), the two shipping workers (the bar, at night). Hold one of his
   favourites and press A: it leaves your hands. The pause menu's Hearts
   page shows theirs with Up / Down.
3. **Heart event.** At five hearts (500), on a morning of good weather that
   is not a festival, someone waits by your door. Talk to them, go where
   they say, walk up to him: his scene, a choice, and the day ends. The
   next morning there is something on the farm. For Ann's father, carry a
   Flower.
4. **Dance.** At the Flower or Harvest Festival, press A in front of him.
5. **Proposal.** Ten hearts (1000), the house extended once, nobody else:
   use the Blue Feather in front of him. The wedding is a week later, the
   next time you step onto the farm.
6. **Married life.** He is at home in the morning and at night, on the farm
   from 9:00 to 18:00 (at home when it rains), walking with his own poses.
7. **The baby.** With the game's own counting (his love, days married, the
   house), the night a child would be born, the Harvest Sprites bring one
   to the house the next time you are home.
8. **The ending** (summer 30 of the third year): he walks it with you.
