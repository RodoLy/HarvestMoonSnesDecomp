# Estado de la descompilación (al 29-oct-2026)

Memoria del proyecto para retomarlo sin perder nada. El clima y el daño
del clima a la granja ya están reescritos y verificados. Porcentaje actual
en el README (`tools/progress.py`).

## Hecho

- **Proyecto separado** del juego. Se compila con `tools/build.sh` (usa la
  recompilación como base, sin modificarla) y se prueba con `tools/check.sh`.
- **Nombres**: 522 rutinas renombradas con los nombres de HM-Decomp
  (`data/decomp_names.tsv`).
- **Comprobador**: cada rutina reescrita se compara byte por byte con la
  original, en partidas reales (`HM_DECOMP_CHECK=1`) y con entradas al azar
  (`HM_DECOMP_FUZZ=<cuadro>:<veces>`).
- **Todas las rutinas reescritas y verificadas (778)** (0 diferencias):

| Rutina | Dirección | Archivo |
|---|---|---|
| GetRNG | $83:8138 | src/rng.c |
| AddWood | $83:B224 | src/stock.c |
| AddGrass | $83:B253 | src/stock.c |
| AddPlayerHappiness | $83:B282 | src/stock.c |
| ChangeStamina | $81:D061 | src/stamina.c |
| GetCowPointer | $83:C9A7 | src/animals.c |
| GetChickenPointer | $83:C995 | src/animals.c |
| DivisionUnsigned | $83:8082 | src/math.c |
| RNGReturn0toA | $80:89F9 | src/rng.c |
| SetWeatherFlags | $82:8C09 | src/weather.c |
| WeatherTomorrow | $82:8CF9 | src/weather.c |
| GetTileIndex | $82:B13C | src/farm.c |
| LoadDamageProbabilities | $82:8270 | src/farm.c |
| ClimateFarmDamageCheck | $82:A713 | src/farm.c |
| ClimateFarmDamagePrep | $82:8209 | src/farm.c |
| MonthlyFarmTilesCheck | $82:A6A2 | src/farm.c |
| NightlyFarmTilesCheck | $82:A811 | src/farm.c |
| CalculateFarmDevelopment | $82:AA0C | src/farm.c |
| CowEatsGrass (pastar) | $82:A9A0 | src/farm.c |
| AddCowHappiness | $84:A5D4 | src/animals.c |
| CowFeedingAndStatus | $83:BC5A | src/animals.c |
| SetEventSlotPointer | $84:887C | src/events.c |
| ResetCurrentEventSlotLong | $84:88D4 | src/events.c |
| StartScriptAtTileInFront | $84:80F8 | src/events.c |
| FindMostLovedName | $82:8EC6 | src/people.c |
| WifePregnancyAndChildren | $82:8790 | src/people.c |
| LoadDateNames | $82:89D6 | src/calendar.c |
| AddMoney | $83:B1C9 | src/stock.c |
| TvWeatherForecast (pronóstico de la tele) | $82:81C0 | src/weather.c |
| HaveLunch | $82:80AA | src/stamina.c |
| GetTileContent | $82:B124 | src/tools.c |
| GetTileProperty | $82:AC61 | src/tools.c |
| CheckToolSuccess | $82:AA71 | src/tools.c |
| CalculateTileInFront | $81:D14E | src/tools.c |
| TileAtOffsetFromPlayer (CODE_81D1A4) | $81:D1A4 | src/tools.c |
| PreCheckToolSuccess / Line / Square | $82:92A0, 92BC, 92D6 | src/tools.c |
| ReadTileChange, DrawTileChange | $81:A801, $81:A83A | src/mapedit.c |
| SetTileTypeBlock | $82:B060 | src/mapedit.c |
| ResetCurrentEventSlotShort | $84:8961 | src/events.c |
| StartEventScriptLong | $84:8097 | src/events.c |
| StartEventScript | $84:803F | src/events.c |
| AddProgrammedDMA | $80:8A33 | src/dma.c |
| QueueTileGraphicsDMA (CODE_81A6C1) | $81:A6C1 | src/mapedit.c |
| ChangeMapTile (CODE_81A688) | $81:A688 | src/mapedit.c |
| SpawnMapObject (CODE_81A500) | $81:A500 | src/objects.c |
| CreateGameObject (CODE_858000) | $85:8000 | src/objects.c |
| LoadGameObjectComponents (CODE_858AE5) | $85:8AE5 | src/objects.c |
| LoadSpriteComponent (CODE_858C30) | $85:8C30 | src/objects.c |
| StartMapAnimation (CODE_81A5E1) | $81:A5E1 | src/mapedit.c |
| Las 28 ToolUsed*, UseToolInHand (las despacha), CheckToolSuccessForSound | $82:931E-$82:A570 | src/toolused.c |
| RanchMasteryCalculator | $83:F26D | src/ending.c |
| AddNewChicken, AddNewCow | $83:C807, $83:C8DC | src/animals.c |
| PlaceAnimalsOnMap (CODE_83C296) | $83:C296 | src/animals.c |
| 57 comandos de evento (Event_*, con Event_CowFrame) | $84:89BB-$84:BB4B | src/eventcmds.c |
| RemoveProgrammedDMA, DrawTilemapBlock2x2 | $80:8AA0, $83:81B7 | src/dma.c |
| StartTextBox, ClearTextBox, CloseTextBox (CODE_8393F9) | $83:935F, $83:932D, $83:93F9 | src/text.c |
| DrawTextBoxArrow, BlinkTextBoxArrow, TextBoxArrow | $83:94D7, $83:9495, $83:9447 | src/text.c |
| CopyLetterToVram | $83:8166 | src/dma.c |
| EditTileOnMap, EditFarmTile | $82:B03A, $82:B049 | src/mapedit.c |
| SaveSlotMenuInputs, LoadSlotMenuInputs, StartMenuInputs, TitleMenuInputs, ListInputs | $84:C2FE-$84:C6B6 | src/menus.c |
| ImmobileInputs | $84:CF05 | src/menus.c |
| LoadMapRowBelow, LoadMapRowAbove, LoadMapColumnRight, LoadMapColumnLeft (fila/columna nueva) | $80:A18D-$80:A617 | src/scroll.c |
| UpdateBGOffset, ScrollCameraUp, ScrollCameraRight, ScrollCameraLeft (la cámara) | $80:A0AB-$80:A152 | src/scroll.c |
| SetTimeOfDayPalette, TransitionTimeOfDayPalettes (+ FinishTimeOfDayPalette), ClearTimeOfDayPalette | $80:8FC7-$80:9166 | src/palette.c |
| SetPaletteColour, SetPaletteColourNoFade, LoadFirstHalfPaletteToWRAM, LoadSecondHalfPaletteToWRAM | $80:916F-$80:9208 | src/palette.c |
| PrepareNightPalette, ChangePaletteByTime, PrepareSpritePalette, CustomiseSpritePalette, SetPaletteToLoad | $80:9241-$80:9501 | src/palette.c |
| Event_TimeOfDayPalette | $84:9356 | src/palette.c |
| CollideEventObjects, CollidePlayer, CollideMap, CollideTileCorner (choques) | $83:AD91-$83:B1C8 | src/collision.c |
| StartToolAnimation y las 28 ToolAnimation* | $82:90A8-$82:925F | src/toolanim.c |
| PressAOnTile (apretar A) | $82:AD0E | src/interact.c |
| PlayerMovementInputs (todo el bloque hasta TextBoxAutoClose) | $84:C6B7-$84:CED1 | src/playerinput.c |
| ReadEventPad (el control de los eventos) | $84:C7D6 | src/playerinput.c |
| InputTypeSelector, MenuInputs, MenuEventPad, SkipEventScene, TextBoxAutoClose | $84:C034-$84:CF04 | src/menus.c |
| NameMenuInputs y sus 6 teclas, ReadNameCursorCell, DrawNameLetter | $84:C066-$84:C286, $82:EBAC, $82:EA60 | src/menus.c |
| DrawWholeMap (dibujar el mapa, autotiling) | $82:AF00 | src/mapedit.c |
| Event_PutDownCarried, Event_Cmd51_PutDownCarriedQuietly-59 (comandos 50-59) | $84:BBDA-$84:C013 | src/eventcmds.c |
| Event_HorseFrame (comando 37, el hijo) | $84:AB27 | src/eventcmds.c |
| DivisionSigned (sin uso; HM_INTERP) | $83:80D0 | src/math.c |
| DecompressTileMap (LZSS) | $83:81F8 | src/decompress.c |
| PlaceCameraOnMapEntry (la cámara al entrar), MoveSecondBackground (BG2: nubes, paralaje) | $80:9EBC, $80:9F61 | src/scroll.c |
| Event_ResetFlag ($84:93C9), Event_EditTileOnMap ($84:BA10), Event_StartTextBox/Copy, Event_ReturnToolsToShed | bank $84 | src/eventcmds.c |
| StartColourAnimation, RunColourAnimations, Zeroes*42Pointer(s), StartMapColourAnimations y las 51 Colours_FarmSpringSummer_Day... de Subrutines_Table | $80:8E48-$80:95B2, $80:C36C- | src/coloranim.c |
| ReadJoypad (los mandos; lee $4212/$4218/$421A del emulador con hm_io_read) | $80:872B | src/joypad.c |
| UnusedBarnCoopCheck y UnusedBarnCheck (cuentan gallinas / vacas; sin usar) | $83:C9BA, $83:C9E5 | src/animals.c |
| ToolUsedSound1 (el ruido de la herramienta; Sound2 espera a PrepareSoundEffect, que llama a WaitForNMI) | $82:8FB1 | src/toolsound.c |
| CopyCurrentMapToFarmMap, LoadDefaultFarmMap (MapPointerTable en $82:B174) | $82:A682, $82:A65A | src/farm.c |
| ClearAllEventSlots | $84:8000 | src/events.c |
| MultiplicationUnsigned | $83:8067 | src/math.c |
| RunMapExtras, los ReplaceTiles* de cada mapa (y 4 que HM-Decomp toma por datos: gallinero, cueva, túnel, mapa $37), ChangeMapTileNoVram, SetTileTypesFromChange, DrawShelfItem | $81:A7CE, $81:A9E5-$81:B362, $82:B0A7 | src/maptiles.c |
| LoadDefaultInstruments/4/6/9/10/11/12/13/14/21/22/25 (la música y las subidas al chip), Multiply16 (multiplicación de 16 bits) | banco $83 | src/audio.c, src/math.c |
| ShippingScene, ShippingSceneDialogue | $82:8131, $82:8165 | src/shipping.c |
| DestroyGameObject/858B7B/858B41/858C9F (destruir un objeto), SetSlotAnimation, FreeEventSlot, Event_DestroyOwnObject, Event_FreeOtherSlot/11 | $85:81A2-, $84:8020, $84:8895, $84:9375, $84:9810, $84:B34B | src/objects.c, events.c, eventcmds.c |
| Event_PlaceSlot/8/15/17 (animaciones; 17 = girarse hacia el jugador) | $84:8F0A, $84:957D, $84:B8D9, $84:BA72 | src/eventcmds.c |
| MoveGameObject (mover un objeto), ChangeGameObjectSprite (cambiarle el sprite), StepSlot (paso de un slot) | $85:80B9, $85:8100, $84:8331 | src/objects.c, events.c |
| ReadGameObject, InvertIfFlippedX/Y, ClearSpriteDataTables | $85:81CB, $85:8BB0, $85:8BBC, $85:8ED7 | src/objects.c |
| SnowFootprints (huellas en la nieve), RunMapAnimation (animaciones de mapa), FreeMapObject | $81:A58F, $81:A600, $81:A4F1 | src/mapedit.c |
| WanderSlot (un slot que pasea solo: dirección, zona, mirar adelante) | $84:83CC | src/events.c |
| UpdateSlotSprite (el sprite de un slot cada cuadro: crear, destruir, mover, cambiar) | $84:8286 | src/events.c |
| Event_TalkIfSpokenTo/9 (hablar con alguien), Event_StartMapAnimation, Event_ChangeToWantedSong | $84:9079, $84:9633, $84:9553, $84:8CA8 | src/eventcmds.c |
| AnimateGameObjects (animar objetos), GetOBJOrderToDraw, ClearOAMCopy / OAMCopy, InitializeOBJs | $85:82C7, $85:8377, $85:8647, $85:8896, $85:820F | src/objects.c |
| LoadGameSlot, NewGameSetup | $83:B2B1, $83:A9BE | src/save.c |
| LoadMap, PickMapMusic (música de cada mapa) | $82:A5FB, $80:95DE | src/farm.c, audio.c |
| CreatePlayerObject / 81CFE6 / 81D03D (el objeto del jugador), NameCursorPosition (cursor del nombre) | $81:CFA0-D060, $82:EB57 | src/objects.c, menus.c |
| StartEnding (comienzo del final: la vaca preferida, el % de desarrollo) | $83:F4D8 | src/animals.c |
| MapEvent_Beanstalk ... E74C (13 rutinas de quién aparece en cada mapa) | $83:E586-$83:E74C | src/mapevents.c |
| MapEvent_MariaHouse1 ... DE64 (16 rutinas: las casas) | $83:D4D7-$83:DE9A | src/mapevents.c |
| MapEvent_Barn, E407, E4CB | $83:DE9B, $83:E407-$83:E585 | src/mapevents.c |
| MapEvent_Town (el pueblo; error 24: STA.B $60) | $83:D2E0 | src/mapevents.c |
| MapEvent_IntroDays (la introducción, mapa $3C; el comienzo es "datos" en HM-Decomp) | $83:E76D | src/mapevents.c |
| MapEvent_EndingSlideShow (la presentación del final) | $83:EBA6-$83:F26C | src/mapevents.c |
| MapEvent_Farm (la granja) | $83:CB69-$83:D2DF | src/mapevents.c |
| PutItemDown (dejar el objeto), GetItemSprite (su sprite) | $81:8000, $81:80B7 | src/items.c |
| Las rutinas de los objetos: ItemObject_01_Mushroom-5A (61, sin las 26 herramientas), las 6 máquinas de estados (ItemAnimalSteps, 818855, 81889A, 8188BC, 8188E5, 818923) y sus 16 pasos (FoodAskToEat-ItemWaitMapAnimation) | $81:80FE-$81:96AE | src/items.c |
| Las acciones del jugador $0F-$57 (PlayerAction_0F_Tv y PlayerAction_10-57, 71 rutinas; falta la $35, espera cuadros) | $81:D716-$81:FAB3 | src/actions.c |
| El jugador cada cuadro: los 26 pasos por acción de PlayerFrame (PlayerState_XX, PlayerStandStill/C66E/C736/C83B/CB14/CB5E), el fin del cuadro (ResetIdleAnimationTimer) y el movimiento (MovePlayer); faltan el despachador y la acción $1C (esperan cuadros) | $81:BFDC-$81:CF9F | src/playerstate.c |
| Las puertas (CheckDoors: salir por las casillas $C0-$CF, banderas de horario y festivales) y la montaña (CheckMountainFinds: hallazgos por estación, empezar a pescar); HaveLunch ahora exacta también en registros | $80:9A64-$80:9EBB, $82:80AA | src/playerstate.c, stamina.c |
| El cargador de piezas de sprite del NMI (LoadQueuedSpritePieces; error 28: tres bucles sin fin) | $85:8CB2 | src/objects.c |
| UploadSoundProgram (la subida del programa de sonido con el protocolo de arranque del chip; se prueba una vez en el cuadro 1 al nombrarla en HM_DECOMP_FUZZ_ONLY, flag HM_LIVE) | $83:843D | src/audio.c |
| Las rutinas que son solo un retorno (COP_Return, ScreenTransitionReturn, ShippingReturn, CODE_83F4D7) y UnusedCountInstrument ($0103 + 1, nadie la llama) | $80:872A, $80:972B, $82:81BF, $83:F4D7, $83:8376 | src/interact.c, audio.c |
| El chip de imagen (comprobación nueva HM_IO sobre la máquina real, src/hm_io.c): StartProgrammedDMA, ForceBlank, ResetForceBlank, SetBrightness, InitializeScreenStatusVars, ManageGraphicPresets, ZeroesVRAM/OAM/CGRAM/PartialVRAM, UnusedHideSprites | $80:8846-$80:8E47 | src/video.c |
| StartLastPreparedDMA, CopyMapPictureToVRAM, CopySpritePiecesToVRAM, UpdateGraphics (error 29: apaga el HDMA), AutoMapScrolling | $80:8AB2, $80:AA38, $85:8E57, $80:86B1, $80:95B3 | src/video.c, scroll.c |
| LoadDialogueBox, DrawTileBlock3x2, DrawNameEntryBlanks | $83:92BB, $82:E5E7, $82:EA80 | src/video.c |
| PutToolOnShelf (herramienta al estante), DrawScreenBar, DrawScreenBars (barras de casillas), DrawSaveFileLine (línea de una partida guardada) | $81:A94A, $82:EAB4, $82:E742, $82:E405 | src/items.c, video.c |
| PrepareOAMData (la tabla de sprites de cada cuadro; comprobada con el intérprete: la recompilación amplía a propósito la prueba de pantalla para la pantalla ancha) | $85:83E0 | src/objects.c |
| BackgroundsManager (el dibujo de un mapa al entrar: tilemaps, dibujos de capa, la cámara hasta la entrada; en vivo siempre la corta el NMI, se comprueba con el fuzz) | $80:A7C6 | src/video.c |
| NMI_Interrupt, COP_Interrupt (las interrupciones), WaitForNMI, WaitForNMIATimes (esperar cuadros; solo con el fuzz: en vivo el código compilado cede el cuadro en el bucle) | $80:867B, $80:8699, $80:8645, $80:865D | src/nmi.c |
| ScreenFadeIn, ScreenFadeOut, Event_ScreenFadeIn, Event_ScreenFadeOut (fundidos) | $80:87CE, $80:880A, $84:8B83, $84:8CB9 | src/nmi.c, eventcmds.c |
| PrepareSoundEffect, SetSong, PlayEffectWithSongInstrument, ChangeMusic, ToolUsedSound2, Event_PlayEffectAndWait, PlayerAction_04_Jingle (sonidos que esperan cuadros) | $83:833E, $83:83A4, $83:82FE, $83:82C6, $82:8FF3, $84:95E8, $81:D670 | src/audio.c, toolsound.c, eventcmds.c, interact.c |
| RunItemsOnMap, RunItemRoutine, TakeToolFromShelf, ItemObject_2A_Sickle-43 (los objetos en el mapa; las herramientas del cobertizo) | $81:A383, $81:809A, $81:87C3, $81:83C9-$81:8491 | src/items.c |
| EventInstructionInterpreter, RunEventSlots (el intérprete de guiones y el bucle de los 49 slots) | $84:8249, $84:816F | src/events.c |
| PlayerState_1C_SwapTools (cambiar la herramienta de la mano y la de la bolsa) | $81:C061 | src/playerstate.c |
| PlayerFrame, PlayerInteractionSelector (el cuadro del jugador: paso de la acción o acción pendiente), PlayerAction_35 | $81:BFB7, $81:D570, $81:E36E | src/playerstate.c, actions.c |
| LoadAndShowMap, EnterMap, SetScreenTransition (ir a otro mapa y cargarlo) | $80:972C, $80:96D3, $80:9671 | src/transition.c |
| MapEvent_PlayerHouse (quién aparece en la casa), StartMapEvents (la rutina de cada mapa) | $83:DED3, $83:CA98 | src/mapevents.c |
| UpdateTime, NightReset, FirstNightReset, UnusedEndlessLoop (el reloj, la noche, el primer día; error 31: un BRA a sí mismo) | $82:8000, $82:82AC, $83:ABF0, $82:878E | src/day.c |
| SpawnAfterLoad, GameLoop (la primera mañana y el bucle del juego) | $80:8000, $80:8083 | src/game.c |
| NamesScreen, SetPlayerName, SetCowNameBought, SetCowNameBorn, SetDogName, SetHorseName, SetKid1Name, SetKid2Name (la pantalla de los nombres; error 30) | $82:E80C, $80:80ED-$80:83AE | src/names.c |
| FirstDemo, DemoPresentation (las demos) | $82:D1C0, $82:D3C7 | src/demo.c |
| IntroScreen, ContinueFileScreen, StartFromLoad, StartFromNewGame, UnusedRewritePlayerName (la pantalla de título, su menú y las pantallas de partidas) | $82:D75E, $82:E1F1, $82:E7E9, $82:E7F9, $82:E801 | src/intro.c |
| RESET (el encendido) | $80:8428 | src/reset.c |
| GetFenceFromStack, PlayerAction_06_MapAnimation11/D6F8, Unused01-11 (acciones), AffectionToHearts (corazones), GetBackgroundData, ClearItemRecords | bancos $80/$81 | src/interact.c, people.c, maptiles.c, objects.c |
| SaveGameSlot, LoadGameSlotResume, CheckSRAMIntegrity | $83:B68E, $83:BA45, $83:BAD4 | src/save.c |
| SoundChipBusy, 7, 8, 15, 19, 16, 2 | $83:8EE4, 8EC9, 8E8B, 8DDF, 8332, 8E7F, 8E32 | src/audio.c |
| FindLetterGraphics, DrawLetter, AdvanceLetterCursor (CODE_839862/23/38) | $83:9862, $83:9823, $83:9838 | src/text.c |
| BlinkTextCursor, PrintDigit (CODE_83975F, CODE_8397A6) | $83:975F, $83:97A6 | src/text.c |
| PrintTextFrame (ADDDDFFFF) | $83:951C | src/text.c |

Rareza encontrada: ChangeStamina decide el signo del cambio con la bandera N
que deja quien la llama, no con el valor de A.

## El clima (hecho el 19-oct-2026)

Verificado con 10 semillas × 2.000 llamadas al azar por rutina, más una
partida con 10 noches forzadas en fechas elegidas (`scripts/nights.txt`):
0 diferencias. Las entradas al azar producen todos los resultados posibles
del clima (soleado, lluvia, nieve, huracán, las dos tormentas del año 1 y
los seis festivales).

Lo aprendido:
- **RNGReturn0toA(n)** no es del todo justo: con n = 6, el 0 sale con 43
  de los 256 valores del RNG. "1 de cada n" en el juego es "salió 0".
- **Tormentas del año 1** (códigos 4 y 5): invierno, segura el día 8 (se
  decide el 7) o 1/8 antes; verano, segura el día 30 (se decide el 29) o
  1/30 antes. Cada una se marca vista en $7F1F64 (bit 1 y bit 0).
- **Huracán**: solo verano, 1/30; con el caparazón de tortuga ($7F1F6C bit
  $1000) 1/60. Nunca se decide el día 30.
- Lluvia 1/6 primavera, 1/10 verano y otoño. Nieve 1/3 solo en invierno.
- **WeatherTomorrow no tiene cuerpo compilado**: la recompilación la corre
  en su intérprete (igual que NightReset). Por eso no lleva envoltorio; está
  marcada `HM_INTERP` y la prueba al azar la compara llamando al original
  con `interp_tier_dispatch`. En vivo solo se comprueban las rutinas
  compiladas que llama.
- Las reescrituras ahora imitan también la **pila**: PHA/PHY y los marcos de
  JSL quedan en la WRAM y el comprobador los compara. Un JSL guarda el banco
  actual (PB) y cambia al de la rutina llamada (`hm_jsl` / `hm_rtl`).

## Daño del clima a la granja (hecho el 19-oct-2026)

Verificado con 10 semillas: 2.000 llamadas al azar por rutina, y 40 granjas
enteras al azar por semilla para las dos rutinas que recorren las 4.096
casillas. 0 diferencias.

- La granja es una cuadrícula de 64 × 64 casillas en $7E:A4E6 (una casilla =
  16 × 16 píxeles). Códigos: 02 tierra, 05 cerca, 06 cerca rota, 07/08
  tierra arada, 1D pasto recién sembrado, 1E-6F cultivos, 70-79 pasto
  creciendo (79 = crecido), A0+ no es campo.
- Probabilidades por noche (tabla $82:8298; fila = clima):
  lluvia cercas 1/96; nieve cercas 1/64; huracán cercas 1/8, pasto 1/16,
  cultivos 1/4; tormenta de invierno del año 1 cercas 1/8, cultivos 1/4;
  la tormenta de verano del año 1 no hace daño; año nuevo pasto 1/64,
  cultivos 1/32.
- Rareza: el contador de pasto crecido ($092E) solo baja en las primeras
  4 filas (compara 16 bits con el byte alto del índice de casilla).
- ClimateFarmDamageCheck no cabe en un cuadro: en el juego real se pausa y
  sigue en el siguiente, así que su llamada real sale "omitida". Se verifica
  con la prueba al azar (que pone el reloj del cuadro en 0 y no comprueba
  las llamadas anidadas, que tienen su propia prueba).

## Crecimiento de los cultivos (hecho el 20-oct-2026)

Verificado con 10 semillas × 40 granjas enteras al azar (con fechas,
lluvia y el primer día de invierno). 0 diferencias.

- Las casillas van en pares: código par = seco, el impar siguiente = regado.
  07/08 tierra arada, 1E/1F semillas, 20-6F etapas de cultivo; 39, 53, 61 y
  6F son tomate, maíz, papa y nabo maduros (regados).
- De noche (NightlyFarmTilesCheck): la lluvia riega lo seco (+1); lo regado
  sin lluvia se seca (-1); un cultivo regado crece una etapa (+1) y, si
  llueve, queda regado otra vez (+1). En otoño los regados solo se secan; en
  invierno nada crece. El pasto (70-9F) crece cada noche salvo en invierno;
  7C (pasto cortado) vuelve a 73.
- Tierra vacía (01/02), primavera y verano, cada 4 días: 1/256 de que salga
  hierba o piedra (03). Cerca rota: 1/8 de poner $0196 bit $0400.
- Primer día de invierno (MonthlyFarmTilesCheck): cultivos → 07, pasto → 7A.
- CalculateFarmDevelopment (evaluación final) cuenta las casillas con cercas,
  cultivos o pasto en $7F1F56.
- `farm.c` tiene un recorrido de casillas común: `tile_begin` / `tile_end` y
  `FOR_EACH_FARM_TILE`.

## Los animales y los eventos (hecho el 20-oct-2026)

Verificado con 10 semillas (200 noches al azar de CowFeedingAndStatus por
semilla, con 12 vacas, 13 gallinas y todas las banderas al azar) y llamadas
reales de SetEventSlotPointer / ResetCurrentEventSlotLong. 0 diferencias.

- Registro de vaca (16 bytes en $7E:C1C6): +0 estado (01 existe, 02 recién
  nacida, 04 cría, 08 adulta, 10 de mal humor, 20 enferma, 40 preñada, 80
  acaba de parir), +1 banderas del día (80 = poción milagrosa), +2 mapa ($27
  establo), +3 contador, +4 felicidad, +5 días fuera.
- Registro de gallina (8 bytes en $7E:C286): +0 estado (01, 02 huevo, 04
  pollito, 08 gallina, 10 con hambre), +1 mapa ($28 gallinero), +2 contador.
- Eventos: "CC structs" de 64 bytes desde $7E:B586; los guiones están en la
  ROM ($B3:8000: tabla de grupos de 24 bits).
- **Bug del juego** en $83:BDE1 (BRK): ver `cow_after_meal` en animals.c.
- Infraestructura nueva: lectura de ROM y punteros largos (`hm_bus_rd8`,
  `hm_dp_long`), nombres manuales para etiquetas locales
  (`import_decomp_names.py` MANUAL, `progress.py` LOCAL), una rutina por
  cuadro en la prueba al azar (el vigilante de 5 s es por cuadro),
  `HM_DECOMP_DIFF_ALL`, `HM_DECOMP_FUZZ_ONLY`, `HM_DECOMP_FUZZ_CALL`.
  Para trazar el original: `SNESRECOMP_INTERP_PCTRACE=1` (así se encontró
  el BRK).

## La noche, parte lógica (hecho el 20-oct-2026)

Las subrutinas de NightReset que son lógica del juego ya están todas:
clima, granja, animales, nombres de la fecha, la chica favorita, esposa e
hijos, dinero. NightReset misma toca pantalla, sonido y guardado (registros
del hardware y SRAM), que el comprobador no modela, así que queda para
después.

- Hijos: primero 450 corazones, segundo 650 y el primer hijo con 90 días;
  casa nivel 2 ($7F1F64 bit $0080) y 20 días de casados ($7F1F3B). Nace a
  los 60 días de concebido ($7F1F37 / $7F1F39); cada 120 días, cumpleaños
  ($7F1F6C $0100 / $0200). El aniversario: cada 120 días de casados.
- FindMostLovedName: en empate gana la última; las etiquetas de HM-Decomp
  están cruzadas (Ann / Nina).
- HaveLunch y ChangeStamina solo comparan la WRAM (sus registros de salida
  no están modelados).

## Herramientas (hecho el 21-oct-2026)

- Cada mapa tiene una tabla de propiedades de casilla en `[$0D]` (puntero
  largo en la página directa): 4 bytes por tipo de casilla. +1..+2 = qué
  hace la casilla (los bits bajos: qué herramientas funcionan), +2 bit 7 =
  "sin valor por omisión", +3 = desde qué lados (01-08) y en qué estaciones
  (10-80) aplica.
- La regadera se rellena en casillas F0/F9-FD (granja y mapas $3D+) o
  F0/F4 (montaña $10-$13); en los mapas 04-0F y 14-3C no se riega.
- La pintura solo funciona en la granja, en C3 o E2-E4.
- Prueba al azar: cada rutina tiene ahora su propia semilla, así que
  `HM_DECOMP_FUZZ_ONLY` + `HM_DECOMP_FUZZ_CALL` reproducen la llamada exacta.
  Cuidado con leer tablas con índices grandes: si la dirección sale de la
  ROM ($83:40FA...) la original lee registros del hardware y no se puede
  comparar.

- La casilla donde cae la herramienta: CalculateTileInFront (N casillas
  hacia donde mira el jugador: 0 abajo, 1 arriba, 2 = +X, otro = -X) o
  TileAtOffsetFromPlayer (un desplazamiento fijo; las herramientas de oro
  usan la tabla de 9 casillas $82:92FA alrededor del jugador).
- Cambiar una casilla del mapa (mapedit.c): la tabla $81:B363 (6 bytes por
  cambio: gráfico en $A6:A096, filas, columnas, tipo de casilla) dice qué
  poner; DrawTileChange copia el gráfico al mapa BG1 en $7E:2000 y SetTileTypeBlock
  escribe el tipo en $09B6. ChangeMapTile (CODE_81A688) hace todo eso y
  además encola la copia del gráfico a la VRAM con QueueTileGraphicsDMA
  (CODE_81A6C1) → AddProgrammedDMA.
- Siguiente en herramientas: los ToolUsed* (ya tienen ChangeMapTile, que
  usan 27 veces) necesitan CODE_81A500 (crea un objeto; ojo: si los 10
  lugares están ocupados se queda en un bucle infinito, `BRA` a sí mismo en
  $81:A528).

## DMA de video (hecho el 21-oct-2026)

- La cola de "DMA programado" ($80:8A33 AddProgrammedDMA): el juego prepara
  durante el cuadro copias a la VRAM / CGRAM y el NMI las arranca todas
  juntas. Página directa: $27 próximo canal, $28 canales a arrancar (un bit
  cada uno), $29 registro destino ($18 = datos de VRAM), $2A+canal modo de
  dirección de VRAM, $32+2·canal dirección destino, $72-$74 origen, $C7
  bytes que quedan este cuadro (se descuenta leyendo de vuelta el registro
  de tamaño).
- El comprobador ahora guarda, restaura y compara también los registros de
  DMA $4300-$437F (`g_hm_dma_regs`; `hm_bus_rd8/wr8` los alcanzan).
- QueueTileGraphicsDMA: la dirección VRAM del mapa BG1 es $6000 + 2·columna
  + 64·fila, cada pantalla de 32×32; la segunda pantalla horizontal suma
  $3E0 (= $400 − 32) y la vertical $400. Encola dos medias filas por fila
  de 16 px, por los canales 2/3 y luego 0/1.
- Con 0 filas (cambio 0) su bucle daría 32768 vueltas; ChangeMapTile lo
  evita saliendo antes si A = 0.

## El porcentaje se corrigió (21-oct-2026)

`progress.py` no contaba las etiquetas sin dirección de HM-Decomp
(`CODE_81A500:` sin comentario `;81A500`) ni las líneas `ETIQUETA: INSTR`.
Eran 13.814 instrucciones fuera de la cuenta: el total pasó de 30.543 a
**45.176** y el porcentaje bajó (10,49% → 7,29% con lo mismo hecho). Ahora
con estas tres rutinas: 7,69%.

ShippingScene queda para después: sigue de largo hacia StartTextBox (el
sistema de texto).

## Objetos con sprite (hecho el 21-oct-2026)

- **Objetos del juego** ("gobj"): 40 registros de $24 bytes en $019C
  (en uso = $7777, sprite, volteo, X, Y, pieza por pieza el lugar de su
  gráfico). $DC cuenta cuántos se repartieron (solo crece, hasta 40).
  CreateGameObject toma uno libre y carga sus piezas.
- **Sprites**: 0-$261 se describen en el banco $86 (tabla $86:8080) y
  $262-$44F en el $87 ($87:8080). Cada sprite es una lista de piezas (como
  mucho 12), 5 bytes cada una.
- **Gráficos de las piezas**: $7F:0F00 + pieza = en qué lugar de la VRAM
  está ($FF = no cargada); $7E:A320 + 4·lugar = cuántas piezas lo usan. Las
  no cargadas van a una cola circular de 64 en $7E:A220 ($BD escribe, $BB
  lee el cargador del NMI). Si la cola está llena, LoadSpriteComponent
  espera a que el NMI la vacíe (`CMP $BB; BEQ` a sí mismo).
- **Objetos del mapa**: 10 registros de 16 bytes en $7E:B4E6 (los efectos
  de las herramientas; RunItemsOnMap los mueve cada cuadro). SpawnMapObject toma
  uno libre; **con los 10 ocupados el juego se cuelga** ($81:A528). Si no
  hay objeto del juego libre, guarda el número del último creado ($A5 no
  cambia).
- En la partida real estas rutinas salen "omitidas": quien las llama corre
  en el intérprete de la recompilación y el cuerpo compilado le devuelve la
  llamada al instante. Las verifica la prueba al azar.

## Usar las herramientas (hecho el 21-oct-2026)

- Tabla `Tool_Used_Table` en $82:A5C3, una rutina por objeto en la mano
  ($0921); la llama UseToolInHand. Todas JSR/RTS, entran con A de 8 bits.
- Plan común: PreCheckToolSuccess (Square / Line para semillas y oro) →
  ChangeMapTile → SpawnMapObject (efecto) → a veces un hallazgo (evento
  con StartScriptAtTileInFront) → $D4 = 0 y ChangeStamina ($FE = -2, $FF = -1, $FC = -4,
  $F8 = -8).
- Banderas de hallazgos: $7F1F5A ($1000 topo, $2000 rana), $7F1F5C ($0200
  moneda/bolsa del día, $0100 baya del día, $0080 baya del martillo del
  día), $7F1F64 ($0800/$1000 bayas de la azada, $0200 del martillo, $0400
  del hacha), $7F1F66 (las 4 partes de la casa pintadas), $7F1F6A ($0020
  evento del hacha en la montaña). $7F1F60 bit 3: "sin hallazgos" (y
  energía congelada).
- La bandera C importa: con la energía congelada ChangeStamina no la toca,
  así que la deja la última comparación del camino. Las reescrituras la
  llevan (`is`, `below`, `add_c`, `sub_c`).
- ChangeStamina ahora modela sus registros de salida. **Diferencia de la
  recompilación con el SNES real:** su `SEP #$10` no borra el byte alto de
  X/Y (el hardware sí); la reescritura imita a la recompilación.
- `tools/calls.py`: lista los JSR/JSL de una rutina con su dirección de
  retorno leída de la ROM.
- Prueba al azar: la semilla del dado ($0100-$0102) se sortea en cada
  llamada (si no, todas sacan la misma tirada y los hallazgos nunca
  salen); una tabla de propiedades propia en $7F:F000 para que las
  herramientas funcionen; el jugador a 7 casillas del borde (la azada de
  oro mira 6 adelante; fuera del mapa el original lee hardware).
- En vivo no se ven: todo el uso de herramientas corre en el intérprete
  de la recompilación (se pulsa X en el guion, pero el envoltorio no ve la
  llamada). Las verifica la prueba al azar.
- La regadera y el aspersor se hicieron después, cuando el comprobador
  aprendió a grabar el chip de sonido (ver "El chip de sonido").

## Animales en el mapa y el final (hecho el 21-oct-2026)

- PlaceAnimalsOnMap: al entrar a un mapa arranca el guion de cada
  gallina (evento $24+n), vaca ($18+n), perro ($16) y caballo ($17) que
  esté ahí. "Está ahí" agrupa mapas: 00-03, 04-07, 08-0F con 0C-0F,
  10-13, 14-17 con 15-17, 31+; el resto solo el mismo mapa. El caballo no
  tiene el grupo 14-17. Con lluvia ($0196 bit 1) las vacas que no están en
  el establo usan otros guiones ($17 / $2F / 9). Verificada también en
  vivo (al entrar a mapas en la partida y las noches).
- AddNewChicken / AddNewCow: buscan el primer registro libre; no cambian
  los contadores $7F1F0A/0B. Gallina: 1 comprada, 2 nacida (+1 =
  estación + 4), otro valor = estado $43. Vaca nacida: estado 3, felicidad
  de $7F1F2B, +70 de felicidad al jugador.
- RanchMasteryCalculator ($83:F26D, se llega con JMP; sin cuerpo
  compilado: `HM_INTERP | HM_JSR`). Errores del original documentados en
  src/ending.c (AND #$1FF en corazones y envíos; dinero de 16 bits).
- El fuzz ahora pone PB en el banco de la rutina también para las JSR
  (hacía falta para las que corren por el intérprete).
- `tools/candidates.py`: lista las rutinas que faltan, su tamaño, qué
  llaman que aún no está hecho y si tocan hardware.
- (La regadera y el aspersor ya están: ver "El chip de sonido".)

## Comandos de los guiones de evento (hecho el 21-oct-2026)

- Tabla `EventInstructionPointers` ($84:9419): 90 comandos, uno por byte
  de guion. $C9-$CB = posición en el guion (cada comando la avanza con
  CLC; ADC, y eso deja en A y C); $CC-$CE = el slot. Los argumentos van
  después del byte del comando.
- Hechos 25: la gallina y el perro de cada cuadro, y 23 chicos (saltos,
  pruebas, reloj, jugador, slots, banderas $7F1F7A).
- Perro: ánimo en $7F1F58 (bits 1, 2, 4, 8, $10), cambia cada $78
  cuadros con el byte al azar $DE; $7F1F60 bit $10 lo llama.
- Gallina: levantarla con A (objeto $26 pollito, $25 gallina); venta
  ($7F1F5A bit 3, precio $32); contador $093B+2n hasta $200.
- Verificados también en vivo: los guiones reales de la partida y de las
  noches ejecutan muchos de estos comandos (miles de saltos).
- Prueba al azar: el guion se arma en $7F:F000 con bytes al azar; para
  los Jumpif con dirección, el banco se fuerza a $7E/$7F, y 1 de cada 4
  veces se fuerza la igualdad para que la rama del salto se pruebe.
- HM-Decomp anota Event_TimeStopped en ;808A0D; es $84:8A0D (nombre
  manual). `progress.py` ahora guarda el texto de las etiquetas con
  dirección para poder encontrarlas en `LOCAL`.

## Más comandos de evento (hecho el 21-oct-2026)

- 31 más: valores en cualquier dirección (Jumpif*Value*, SetFlag,
  StoreValue, SetValue*), sumas con tope (UNKTextrelated 0-255,
  UNKCCchanges14 0-999), ChangeMoney, esperas (UNKCCchanges12/13 con
  $08FD/$08FF, UNK2), levantar cosas (topo $29, pescado $07, objeto $08),
  caminatas (UNKCCchanges4/6/10), transiciones, objetos en la mano,
  banderas.
- Nombres corregidos: HM-Decomp anota $80 en vez de $84 en
  Event_AddToByteClamped, Event_MapScrolling; Event_ChangeItemOnHand empieza
  en $84:B4DC (dice B4DE); Event_StoreValue ($84:A90F),
  Event_MakeSlotPassable ($84:BA51) y Event_UseTileProperties ($84:BB31) no tenían
  dirección. ResetFlagD / ResetFlagDD no están en la tabla (código
  muerto); Event_ReturnToolsToShed ($84:BB65) tampoco y no tiene cuerpo compilado.
- La prueba al azar de los comandos con dirección evita la pila y la
  página directa ($7E:0000-1FFF) y el final de un banco.
- Faltan 34 comandos: los de texto (StartTextBox), pantalla (fades,
  paleta), sonido, la vaca (Event_CowFrame, 1.567 instrucciones),
  Event_PutDownCarried, Event_HorseFrame, y los que usan
  SetSlotAnimation.

## El chip de sonido (hecho el 21-oct-2026)

- El comprobador graba en orden lo que el original lee y escribe en los
  puertos $2140-$217F y se lo reproduce a la reescritura; cualquier
  diferencia (puerto, valor u orden) es un MISMATCH. Ver GUIDE.md 3.4.
- src/audio.c: SoundChipBusy, 7, 8, 15, 19, 16, 2. En vivo: 248 efectos de
  sonido del texto de una partida nueva, todos iguales.
- Con eso: ToolUsedWateringCan y ToolUsedSprinkler (src/toolused.c) y
  EditTileOnMap / EditFarmTile (src/mapedit.c).

## Desplazar el mapa (hecho el 22-oct-2026)

- `src/scroll.c`: las 4 rutinas de la cámara (abajo, arriba, derecha,
  izquierda) y las 4 que copian la fila o columna nueva de la WRAM a dos
  búferes y programan dos DMA a la VRAM.
- prep_scroll limita $16 < $7000, $1C < $400, $12 < $E0 y los tramos
  $7E-$84 pares < $40: con valores más grandes una lectura de 16 bits cae
  en $7E:FFFF y la recompilación toma el segundo byte de $7E:0000 (el SNES
  de $7F:0000). Anotado como diferencia 2 en la guía.
- Fuzz 3 semillas × 2000 y en vivo (newgame.txt, ~1.200 llamadas): 0
  diferencias. Sabotajes detectados (el de `<` por `<=` en el tope de
  velocidad es equivalente: no cambia nada).

## Los colores del día (hecho el 22-oct-2026)

- `src/palette.c`: las 14 rutinas de paletas ($80:8FC7-$80:9501 y el
  comando de evento $84:9356).
- `CustomiseSpritePalette` está en $80:93A4 (HM-Decomp dice 93A3, que es el
  RTL anterior): entrada en MANUAL (import_decomp_names.py) y en LOCAL
  (progress.py).
- Dos preparaciones: prep_palette (mapas con paletas válidas en la tabla,
  para las que cargan de la tabla) y prep_palette_any (cualquier mapa).
- Fuzz 3 semillas × 2000 y en vivo (newgame + nights, unas 13.000
  llamadas): 0 diferencias. 15 sabotajes detectados.

## Choques (hecho el 22-oct-2026)

- `src/collision.c`: CollideMap (el mapa, dos esquinas), CollideTileCorner (una esquina),
  CollideEventObjects (objetos de evento), CollidePlayer (el jugador). Tablas de saltos leídas
  de la ROM (HM-Decomp las desensambla como código). Etiquetas indentadas:
  entradas en MANUAL y LOCAL.
- prep_collision: posiciones lejos del borde (con una esquina negativa el
  original lee bus abierto: $B1).
- Fuzz 3 × 2000 y en vivo (~2.900 llamadas): 0 diferencias; sabotajes
  detectados.

## Los botones al caminar (hecho el 22-oct-2026)

- `src/playerinput.c`: PlayerMovementInputs es una sola rutina con muchas
  salidas (HM-Decomp la parte en 15 etiquetas unidas con JMP). LOCAL de
  $84:C6B7 hasta TextBoxAutoClose. progress.py: una línea ya separada por
  split_called (ReadEventPad) se resta de esa rutina, no del bloque (si no,
  se contaba dos veces).
- HM_INTERP: no tiene cuerpo compilado propio (está dentro de
  InputTypeSelector); en vivo se probará cuando se reescriba
  InputTypeSelector (falta NameMenuInputs y MenuInputs).
- prep_player_input: $DA siempre 0-3 (CollideMap/CollideEventObjects saltan por una tabla de
  4), jugador lejos del borde (bus abierto), a veces piso parejo con
  propiedad en el byte +2 (se lee con A = 2: bytes +2/+3) y sin objetos.
- Fuzz 4 × 2500: 0 diferencias. 23 sabotajes detectados (el de las
  esquinas $C0-$CF de "dejar algo" solo 1 vez en 6000: camino raro).

## Todos los modos de entrada (hecho el 22-oct-2026)

- InputTypeSelector reescrito (solo tiene cuerpo compilado en la copia
  $00, `_lo`): ahora todo el sistema de botones se prueba en vivo cada
  cuadro (newgame 3.321, actions 10.271, nights 7.474 llamadas: 0
  diferencias).
- ReadEventPad es una rutina de verdad (MenuInputs la llama con JSL):
  registrada aparte; progress.py deja sus líneas fuera del rango LOCAL de
  PlayerMovementInputs.
- La pantalla del nombre: cuadrículas de 71, 84 y 49 casillas; la
  preparación elige a veces casillas con código (cambio de página, listo).
- Regresión completa (248 rutinas, 2 semillas): 0 diferencias.

## HmRegs.p (22-oct-2026)

- HmRegs tiene `p`: todas las banderas de la entrada (el comprobador la
  llena con cpu_mirrors_to_p). Sirve para `PHP` (su byte queda en la pila,
  que se compara). Va al final de la estructura: los inicializadores
  viejos siguen valiendo.

## Lo siguiente

Todo el código del juego está reescrito (100 %, `tools/progress.py`). Lo
que queda es mejorar el texto: poner nombres claros a las rutinas que aún
tienen el de HM-Decomp (`UNK_*`, `SUB_*`, `CODE_*`) y a las variables de
la WRAM, y seguir usando la descompilación para las ideas de la sección 7
de MEMORIA.md (minimapa, objetos y animales nuevos).

## Cómo retomarlo

```
HM_RECOMP=../HarvestMoonRecomp/codigo-fuente tools/build.sh --rom ruta/rom.sfc
HM_ROM=ruta/rom.sfc tools/check.sh 2000 10
```

Para cada rutina nueva: escribirla en `src/`, registrarla en
`src/hm_decomp.c` (con su rango de entradas para la prueba al azar),
compilar y comprobar. Ver `DECOMPILATION.md`.
