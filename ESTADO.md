# Estado de la descompilación (al 21-oct-2026)

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
- **430 rutinas reescritas y verificadas** (0 diferencias):

| Rutina | Dirección | Archivo |
|---|---|---|
| GetRNG | $83:8138 | src/rng.c |
| AddWood | $83:B224 | src/stock.c |
| AddGrass | $83:B253 | src/stock.c |
| AddPlayerHappiness | $83:B282 | src/stock.c |
| ChangeStamina | $81:D061 | src/stamina.c |
| GetsCowPointer | $83:C9A7 | src/animals.c |
| GetChickenPointer | $83:C995 | src/animals.c |
| DivisionUnsigned | $83:8082 | src/math.c |
| RNGReturn0toA | $80:89F9 | src/rng.c |
| SetWeatherFlags | $82:8C09 | src/weather.c |
| WeatherTomorrow | $82:8CF9 | src/weather.c |
| GetTileIndex | $82:B13C | src/farm.c |
| LoadsDamageProbabilities | $82:8270 | src/farm.c |
| ClimateFarmDamageCheck | $82:A713 | src/farm.c |
| ClimateFarmDamagePrep | $82:8209 | src/farm.c |
| MonthlyFarmTilesCheck | $82:A6A2 | src/farm.c |
| NightlyFarmTilesCheck | $82:A811 | src/farm.c |
| CalculateFarmDevelopment | $82:AA0C | src/farm.c |
| Sub_82A9A0 (pastar) | $82:A9A0 | src/farm.c |
| AddCowHappiness | $84:A5D4 | src/animals.c |
| CowFeedingandStatus | $83:BC5A | src/animals.c |
| SetCCPoiner | $84:887C | src/events.c |
| ResetsCurrentCCStructureLong | $84:88D4 | src/events.c |
| SUB_8480F8 | $84:80F8 | src/events.c |
| FindMostLovedName | $82:8EC6 | src/people.c |
| WifePregnanacyandChilds | $82:8790 | src/people.c |
| LoadsDateNames | $82:89D6 | src/calendar.c |
| AddMoney | $83:B1C9 | src/stock.c |
| SUB_8281C0 (pronóstico de la tele) | $82:81C0 | src/weather.c |
| HaveLunch | $82:80AA | src/stamina.c |
| GetTileContent | $82:B124 | src/tools.c |
| UNK_CheckTileProperty | $82:AC61 | src/tools.c |
| CheckToolSuccess | $82:AA71 | src/tools.c |
| CalculateTileinFront | $81:D14E | src/tools.c |
| TileAtOffsetFromPlayer (CODE_81D1A4) | $81:D1A4 | src/tools.c |
| PreCheckToolSuccess / Line / Square | $82:92A0, 92BC, 92D6 | src/tools.c |
| SUB_81A801, SUB_81A83A | $81:A801, $81:A83A | src/mapedit.c |
| SUB_82B060 | $82:B060 | src/mapedit.c |
| ResetsCurrentCCStructureShort | $84:8961 | src/events.c |
| UNK_LoadCCDataLong | $84:8097 | src/events.c |
| UNK_LoadCCDataShort | $84:803F | src/events.c |
| AddProgrammedDMA | $80:8A33 | src/dma.c |
| QueueTileGraphicsDMA (CODE_81A6C1) | $81:A6C1 | src/mapedit.c |
| ChangeMapTile (CODE_81A688) | $81:A688 | src/mapedit.c |
| SpawnMapObject (CODE_81A500) | $81:A500 | src/objects.c |
| CreateGameObject (CODE_858000) | $85:8000 | src/objects.c |
| LoadGameObjectComponents (CODE_858AE5) | $85:8AE5 | src/objects.c |
| LoadSpriteComponent (CODE_858C30) | $85:8C30 | src/objects.c |
| StartMapAnimation (CODE_81A5E1) | $81:A5E1 | src/mapedit.c |
| Las 28 ToolUsed*, SUB_829260 (las despacha), SUB_82927D | $82:931E-$82:A570 | src/toolused.c |
| RanchMasteryCalculator | $83:F26D | src/ending.c |
| AddNewChicken, AddNewCow | $83:C807, $83:C8DC | src/animals.c |
| PlaceAnimalsOnMap (CODE_83C296) | $83:C296 | src/animals.c |
| 57 comandos de evento (Event_*, con Event_UNKCowRelated) | $84:89BB-$84:BB4B | src/eventcmds.c |
| RemoveProgrammedDMA, UNK_SetDMA2 | $80:8AA0, $83:81B7 | src/dma.c |
| StartTextBox, SUB_83932D, CloseTextBox (CODE_8393F9) | $83:935F, $83:932D, $83:93F9 | src/text.c |
| DrawTextBoxArrow, BlinkTextBoxArrow, TextBoxArrow | $83:94D7, $83:9495, $83:9447 | src/text.c |
| UNK_SetDMA1 | $83:8166 | src/dma.c |
| EditTileonMap, EditTileonMapandsets0181 | $82:B03A, $82:B049 | src/mapedit.c |
| SaveSlotMenuInputs, LoadSlotMenuInputs, StartMenuInputs, TitleMenuInputs, ListInputs | $84:C2FE-$84:C6B6 | src/menus.c |
| ImmobileInputs | $84:CF05 | src/menus.c |
| SUB_80A18D, SUB_80A308, SUB_80A481, SUB_80A617 (fila/columna nueva) | $80:A18D-$80:A617 | src/scroll.c |
| UpdateBGOffset, SUB_80A0E1, UNK_StaticMapScroling, SUB_80A152 (la cámara) | $80:A0AB-$80:A152 | src/scroll.c |
| SetTimeofDayPalette, TransitionTimeofDayPalettes (+ ClearsTimeofDayPaletteIndexandSetsNext), ClearsTimeofDayPalette | $80:8FC7-$80:9166 | src/palette.c |
| SUB_80916F, SUB_80919D, LoadFirstHalfPaletteToWRAM, LoadSecondHalfPaletteToWRAM | $80:916F-$80:9208 | src/palette.c |
| UNK_CustomisePalette, ChangePalettebyTime, PrepareSpritePalette, CustomiseSpritePalette, SetPaletteToLoad | $80:9241-$80:9501 | src/palette.c |
| Event_TimeofDayPalette | $84:9356 | src/palette.c |
| CBBBB, CDDDD, CEEEE, CFFFF (choques) | $83:AD91-$83:B1C8 | src/collision.c |
| UNK_ToolUsed y las 28 ToolAnimation* | $82:90A8-$82:925F | src/toolanim.c |
| SUB_82AD0E (apretar A) | $82:AD0E | src/interact.c |
| PlayerMovementInputs (todo el bloque hasta CODE_84CED2) | $84:C6B7-$84:CED1 | src/playerinput.c |
| CODE_84C7D6 (el control de los eventos) | $84:C7D6 | src/playerinput.c |
| InputTypeSelector, MenuInputs, CODE_84C2B4, CODE_84C2D8, CODE_84CED2 | $84:C034-$84:CF04 | src/menus.c |
| NameMenuInputs y sus 6 teclas, NameCursorPharse, Unk_OutOfMenu | $84:C066-$84:C286, $82:EBAC, $82:EA60 | src/menus.c |
| UNK_PartialMap (dibujar el mapa, autotiling) | $82:AF00 | src/mapedit.c |
| Event_UNKChickenRelated2, Event_Cmd51-59 (comandos 50-59) | $84:BBDA-$84:C013 | src/eventcmds.c |
| Event_UNKPlayerPos (comando 37, el hijo) | $84:AB27 | src/eventcmds.c |
| DivisionSigned (sin uso; HM_INTERP) | $83:80D0 | src/math.c |
| DecompressTileMap (LZSS) | $83:81F8 | src/decompress.c |
| SUB_809EBC (la cámara al entrar), SUB_809F61 (BG2: nubes, paralaje) | $80:9EBC, $80:9F61 | src/scroll.c |
| Event_ResetFlag ($84:93C9), Event_EditTileonMap ($84:BA10), Event_StartTextBox/Copy, Event_UNKTools | bank $84 | src/eventcmds.c |
| UNK_SetPointer42, UNK_BigLoop, Zeroes*42Pointer(s), CallIndexedSubrutines y las 51 SUB_80C36C... de Subrutines_Table | $80:8E48-$80:95B2, $80:C36C- | src/coloranim.c |
| ReadJoypad (los mandos; lee $4212/$4218/$421A del emulador con hm_io_read) | $80:872B | src/joypad.c |
| UnusedBarnCoopCheck y UnusedBarnCheck (cuentan gallinas / vacas; sin usar) | $83:C9BA, $83:C9E5 | src/animals.c |
| ToolUsedSound1 (el ruido de la herramienta; Sound2 espera a UNK_Audio18, que llama a WaitForNMI) | $82:8FB1 | src/toolsound.c |
| CopyCurrentMaptoFarmMap, LoadDefaultFarmMap (MapPointerTable en $82:B174) | $82:A682, $82:A65A | src/farm.c |
| ClearsofAllCCStructs | $84:8000 | src/events.c |
| MultiplicationUnsigned | $83:8067 | src/math.c |
| UNK_ExecuteFromPointers, los ReplaceTiles* de cada mapa (y 4 que HM-Decomp toma por datos: gallinero, cueva, túnel, mapa $37), SUB_81A7CE, SUB_82B0A7, SUB_81B2BB | $81:A7CE, $81:A9E5-$81:B362, $82:B0A7 | src/maptiles.c |
| UNK_Audio3/4/6/9/10/11/12/13/14/21/22/25 (la música y las subidas al chip), Unk_Audio23 (multiplicación de 16 bits) | banco $83 | src/audio.c, src/math.c |
| ShippingScene, ShippingSceneDialogue | $82:8131, $82:8165 | src/shipping.c |
| CODE_8581A2/858B7B/858B41/858C9F (destruir un objeto), SUB_848895, SUB_848020, Event_UNK1, Event_UNKCCchanges7/11 | $85:81A2-, $84:8020, $84:8895, $84:9375, $84:9810, $84:B34B | src/objects.c, events.c, eventcmds.c |
| Event_UNKCCchanges3/8/15/17 (animaciones; 17 = girarse hacia el jugador) | $84:8F0A, $84:957D, $84:B8D9, $84:BA72 | src/eventcmds.c |
| CODE_8580B9 (mover un objeto), CODE_858100 (cambiarle el sprite), SUB_848331 (paso de un slot) | $85:80B9, $85:8100, $84:8331 | src/objects.c, events.c |
| CODE_8581CB, InvertIfFlipedX/Y, ClearSpriteDataTables | $85:81CB, $85:8BB0, $85:8BBC, $85:8ED7 | src/objects.c |
| CODE_81A58F (huellas en la nieve), CODE_81A600 (animaciones de mapa), CODE_81A4F1 | $81:A58F, $81:A600, $81:A4F1 | src/mapedit.c |
| SUB_8483CC (un slot que pasea solo: dirección, zona, mirar adelante) | $84:83CC | src/events.c |
| SUB_848286 (el sprite de un slot cada cuadro: crear, destruir, mover, cambiar) | $84:8286 | src/events.c |
| Event_UNKCCchanges5/9 (hablar con alguien), Event_UNKCFD1, Event_AudioRelated2 | $84:9079, $84:9633, $84:9553, $84:8CA8 | src/eventcmds.c |
| CODE_8582C7 (animar objetos), GetOBJOrderToDraw, PresetsWRAMCopyofOAM / OAMCopy, InitializeOBJs | $85:82C7, $85:8377, $85:8647, $85:8896, $85:820F | src/objects.c |
| LoadGameSlot, NewGameSetup | $83:B2B1, $83:A9BE | src/save.c |
| LoadMap, UNK_Audio5 (música de cada mapa) | $82:A5FB, $80:95DE | src/farm.c, audio.c |
| CODE_81CFA0 / 81CFE6 / 81D03D (el objeto del jugador), SUB_82EB57 (cursor del nombre) | $81:CFA0-D060, $82:EB57 | src/objects.c, menus.c |
| CODE_83F4D8 (comienzo del final: la vaca preferida, el % de desarrollo) | $83:F4D8 | src/animals.c |
| MapEvent_E586 ... E74C (13 rutinas de quién aparece en cada mapa) | $83:E586-$83:E74C | src/mapevents.c |
| MapEvent_D4D7 ... DE64 (16 rutinas: las casas) | $83:D4D7-$83:DE9A | src/mapevents.c |
| MapEvent_DE9B, E407, E4CB | $83:DE9B, $83:E407-$83:E585 | src/mapevents.c |
| MapEvent_D2E0 (el pueblo; error 24: STA.B $60) | $83:D2E0 | src/mapevents.c |
| MapEvent_E76D (la introducción, mapa $3C; el comienzo es "datos" en HM-Decomp) | $83:E76D | src/mapevents.c |
| MapEvent_EBA6 (la presentación del final) | $83:EBA6-$83:F26C | src/mapevents.c |
| GetFenceFromStack, CODE_81D6E2/D6F8, Unused01-11 (acciones), CODE_81D1C5 (corazones), GetBackgroundData, UNK_PresetsMemory3 | bancos $80/$81 | src/interact.c, people.c, maptiles.c, objects.c |
| SaveGameSlot, LoadGameSlotResume, CheckSRAMIntegrity | $83:B68E, $83:BA45, $83:BAD4 | src/save.c |
| UNK_Audio17, 7, 8, 15, 19, 16, 2 | $83:8EE4, 8EC9, 8E8B, 8DDF, 8332, 8E7F, 8E32 | src/audio.c |
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

Verificado con 10 semillas (200 noches al azar de CowFeedingandStatus por
semilla, con 12 vacas, 13 gallinas y todas las banderas al azar) y llamadas
reales de SetCCPoiner / ResetsCurrentCCStructureLong. 0 diferencias.

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

- La casilla donde cae la herramienta: CalculateTileinFront (N casillas
  hacia donde mira el jugador: 0 abajo, 1 arriba, 2 = +X, otro = -X) o
  TileAtOffsetFromPlayer (un desplazamiento fijo; las herramientas de oro
  usan la tabla de 9 casillas $82:92FA alrededor del jugador).
- Cambiar una casilla del mapa (mapedit.c): la tabla $81:B363 (6 bytes por
  cambio: gráfico en $A6:A096, filas, columnas, tipo de casilla) dice qué
  poner; SUB_81A83A copia el gráfico al mapa BG1 en $7E:2000 y SUB_82B060
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
  de las herramientas; BAAAA los mueve cada cuadro). SpawnMapObject toma
  uno libre; **con los 10 ocupados el juego se cuelga** ($81:A528). Si no
  hay objeto del juego libre, guarda el número del último creado ($A5 no
  cambia).
- En la partida real estas rutinas salen "omitidas": quien las llama corre
  en el intérprete de la recompilación y el cuerpo compilado le devuelve la
  llamada al instante. Las verifica la prueba al azar.

## Usar las herramientas (hecho el 21-oct-2026)

- Tabla `Tool_Used_Table` en $82:A5C3, una rutina por objeto en la mano
  ($0921); la llama SUB_829260. Todas JSR/RTS, entran con A de 8 bits.
- Plan común: PreCheckToolSuccess (Square / Line para semillas y oro) →
  ChangeMapTile → SpawnMapObject (efecto) → a veces un hallazgo (evento
  con SUB_8480F8) → $D4 = 0 y ChangeStamina ($FE = -2, $FF = -1, $FC = -4,
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
  Event_UNKTextrelated, Event_MapScrolling; Event_ChangeItemonHand empieza
  en $84:B4DC (dice B4DE); Event_StoreValue ($84:A90F),
  Event_UNKCCchanges16 ($84:BA51) y Event_UNKReset5C ($84:BB31) no tenían
  dirección. ResetFlagD / ResetFlagDD no están en la tabla (código
  muerto); Event_UNKTools ($84:BB65) tampoco y no tiene cuerpo compilado.
- La prueba al azar de los comandos con dirección evita la pila y la
  página directa ($7E:0000-1FFF) y el final de un banco.
- Faltan 34 comandos: los de texto (StartTextBox), pantalla (fades,
  paleta), sonido, la vaca (Event_UNKCowRelated, 1.567 instrucciones),
  Event_UNKChickenRelated2, Event_UNKPlayerPos, y los que usan
  SUB_848895.

## El chip de sonido (hecho el 21-oct-2026)

- El comprobador graba en orden lo que el original lee y escribe en los
  puertos $2140-$217F y se lo reproduce a la reescritura; cualquier
  diferencia (puerto, valor u orden) es un MISMATCH. Ver GUIDE.md 3.4.
- src/audio.c: UNK_Audio17, 7, 8, 15, 19, 16, 2. En vivo: 248 efectos de
  sonido del texto de una partida nueva, todos iguales.
- Con eso: ToolUsedWateringCan y ToolUsedSprinkler (src/toolused.c) y
  EditTileonMap / EditTileonMapandsets0181 (src/mapedit.c).

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

- `src/collision.c`: CEEEE (el mapa, dos esquinas), CFFFF (una esquina),
  CBBBB (objetos de evento), CDDDD (el jugador). Tablas de saltos leídas
  de la ROM (HM-Decomp las desensambla como código). Etiquetas indentadas:
  entradas en MANUAL y LOCAL.
- prep_collision: posiciones lejos del borde (con una esquina negativa el
  original lee bus abierto: $B1).
- Fuzz 3 × 2000 y en vivo (~2.900 llamadas): 0 diferencias; sabotajes
  detectados.

## Los botones al caminar (hecho el 22-oct-2026)

- `src/playerinput.c`: PlayerMovementInputs es una sola rutina con muchas
  salidas (HM-Decomp la parte en 15 etiquetas unidas con JMP). LOCAL de
  $84:C6B7 hasta CODE_84CED2. progress.py: una línea ya separada por
  split_called (CODE_84C7D6) se resta de esa rutina, no del bloque (si no,
  se contaba dos veces).
- HM_INTERP: no tiene cuerpo compilado propio (está dentro de
  InputTypeSelector); en vivo se probará cuando se reescriba
  InputTypeSelector (falta NameMenuInputs y MenuInputs).
- prep_player_input: $DA siempre 0-3 (CEEEE/CBBBB saltan por una tabla de
  4), jugador lejos del borde (bus abierto), a veces piso parejo con
  propiedad en el byte +2 (se lee con A = 2: bytes +2/+3) y sin objetos.
- Fuzz 4 × 2500: 0 diferencias. 23 sabotajes detectados (el de las
  esquinas $C0-$CF de "dejar algo" solo 1 vez en 6000: camino raro).

## Todos los modos de entrada (hecho el 22-oct-2026)

- InputTypeSelector reescrito (solo tiene cuerpo compilado en la copia
  $00, `_lo`): ahora todo el sistema de botones se prueba en vivo cada
  cuadro (newgame 3.321, actions 10.271, nights 7.474 llamadas: 0
  diferencias).
- CODE_84C7D6 es una rutina de verdad (MenuInputs la llama con JSL):
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

Más comandos de evento (`python3 tools/candidates.py | grep Event_`);
SUB_829260 (el despachador de herramientas: ya están todas); el
ShippingScene y UpdateTime (ya está todo el texto); el resto
de los eventos de animales (`Event_UNKCowRelated`, $84:9AD4).

Otras hojas candidatas (no llaman a nada): LoadsDamageProbabilities,
SetTimeofDayPalette, CalculateTileinFront, SUB_809EBC (cámara al cambiar de
pantalla), y los comandos de eventos `Event_*` del banco 84.

## Cómo retomarlo

```
HM_RECOMP=../HarvestMoonRecomp/codigo-fuente tools/build.sh --rom ruta/rom.sfc
HM_ROM=ruta/rom.sfc tools/check.sh 2000 10
```

Para cada rutina nueva: escribirla en `src/`, registrarla en
`src/hm_decomp.c` (con su rango de entradas para la prueba al azar),
compilar y comprobar. Ver `DECOMPILACION.md`.
