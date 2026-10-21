# Estado de la descompilación (al 21-oct-2026)

Memoria del proyecto para retomarlo sin perder nada. El clima y el daño
del clima a la granja ya están reescritos y verificados. Porcentaje actual
en el README (`tools/progress.py`).

## Hecho

- **Proyecto separado** del juego. Se compila con `tools/build.sh` (usa la
  recompilación como base, sin modificarla) y se prueba con `tools/check.sh`.
- **Nombres**: 493 rutinas renombradas con los nombres de HM-Decomp
  (`data/decomp_names.tsv`).
- **Comprobador**: cada rutina reescrita se compara byte por byte con la
  original, en partidas reales (`HM_DECOMP_CHECK=1`) y con entradas al azar
  (`HM_DECOMP_FUZZ=<cuadro>:<veces>`).
- **51 rutinas reescritas y verificadas** (0 diferencias):

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

## Lo siguiente

Los ToolUsed* (ya tienen ChangeMapTile y SpawnMapObject); el
sistema de texto (StartTextBox) para ShippingScene y UpdateTime; el resto
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
