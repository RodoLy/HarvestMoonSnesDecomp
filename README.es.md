# HarvestMoonSnesDecomp — descompilación de Harvest Moon (SNES)

[English](README.md) · **Español**

<!-- progress -->
![Descompilado: 56.14%](https://img.shields.io/badge/descompilado-56.14%25-2f855a)

**56.14%** del código del juego descompilado y verificado: 25362 de 45176 instrucciones, 427 de 531 rutinas (80.4%).
Se calcula con `python3 tools/progress.py --decomp ../HM-Decomp` sobre el desensamblado de HM-Decomp.
<!-- /progress -->

El juego reescrito, rutina por rutina, como C que una persona puede leer.
Cada rutina reescrita se **comprueba contra la original byte por byte**: la
memoria completa (128 KB de WRAM, pila incluida), los registros del CPU y
los registros de DMA tienen que quedar idénticos.

Es un proyecto hermano del port jugable
[HarvestMoonSNESRecomp](https://github.com/RodriLoy/HarvestMoonSNESRecomp)
y se apoya en el desensamblado de la comunidad
[HM-Decomp](https://github.com/ElisaS-Codes/HM-Decomp) (ElisaS-Codes y DasK),
de donde vienen los nombres de las rutinas y el mapa de memoria.

| | HarvestMoonSNESRecomp | HarvestMoonSnesDecomp (este) |
|---|---|---|
| Para qué | Jugar: el port de PC con sus mejoras | Entender el juego: código legible |
| Resultado | El juego (`.exe`) | Rutinas en `src/` + un ejecutable de prueba |
| Depende de | Tu ROM | Tu ROM y el proyecto de recompilación |

## Aviso legal

Este repositorio **no contiene la ROM ni nada generado a partir de ella**
(ni código generado, ni ejecutables). Para compilar necesitas tu propia
copia legal de *Harvest Moon (USA)*:
SHA-256 `73a3aa87ddd5ce5df5ba51292316f42b6e128280179b0a1b11b4dcddc17d4163`.

## Qué está descompilado

| Sistema | Rutinas | Archivo |
|---|---|---|
| Números aleatorios | GetRNG, RNGReturn0toA ("dado de n caras") | `src/rng.c` |
| Aritmética | DivisionUnsigned | `src/math.c` |
| Clima | SetWeatherFlags, WeatherTomorrow, SUB_8281C0 (el pronóstico de la tele) | `src/weather.c` |
| Granja | GetTileIndex, LoadsDamageProbabilities, ClimateFarmDamageCheck, ClimateFarmDamagePrep, NightlyFarmTilesCheck, MonthlyFarmTilesCheck, CalculateFarmDevelopment, Sub_82A9A0 (pastar) | `src/farm.c` |
| Existencias y dinero | AddWood, AddGrass, AddPlayerHappiness, AddMoney | `src/stock.c` |
| Jugador | ChangeStamina, HaveLunch | `src/stamina.c` |
| Personas | FindMostLovedName, WifePregnanacyandChilds | `src/people.c` |
| Calendario | LoadsDateNames | `src/calendar.c` |
| Animales | GetsCowPointer, GetChickenPointer, AddCowHappiness, CowFeedingandStatus (la noche de los animales), AddNewCow, AddNewChicken, PlaceAnimalsOnMap (CODE_83C296: gallinas, vacas, perro y caballo en el mapa al que entras) | `src/animals.c` |
| Final | RanchMasteryCalculator (el puntaje final del rancho) | `src/ending.c` |
| Comandos de los guiones de evento | 57 de los 90: cada cuadro de la vaca, la gallina y el perro (Event_UNKCowRelated, Event_UNKChickenRelated, Event_UNKDogRelated), saltos y pruebas (JumptoInstruction, JumpifFlagSet / Equals / Between, JumpIfEqualsRNG, GetRNG), reloj (TimeRunning / Stopped, SetHour), jugador (SetPlayerPosition / Direction, SetTransitionDest, UNKSetAnimation), slots (NewCCStructure, UNKDisableCCstruct, UNKSetsCC10, UNKCCchanges2) y banderas | `src/eventcmds.c` |
| Eventos | SetCCPoiner, ResetsCurrentCCStructureLong / Short, SUB_8480F8, UNK_LoadCCDataLong / Short (arrancar un guion de evento) | `src/events.c` |
| Edición del mapa | EditTileonMap / EditTileonMapandsets0181, ChangeMapTile (CODE_81A688), QueueTileGraphicsDMA (CODE_81A6C1), SUB_81A801, SUB_81A83A, SUB_82B060, StartMapAnimation (CODE_81A5E1) | `src/mapedit.c` |
| DMA de video | AddProgrammedDMA (encolar una copia a VRAM / CGRAM para el próximo NMI), RemoveProgrammedDMA, UNK_SetDMA2 (un bloque de 2x2 casillas de BG3), UNK_SetDMA1 (una letra) | `src/dma.c` |
| Menús | SaveSlotMenuInputs, LoadSlotMenuInputs, StartMenuInputs, TitleMenuInputs, ListInputs (los botones de cada menú), ImmobileInputs (con un cuadro de texto abierto: acelerar, seguir, cerrar, elegir) | `src/menus.c` |
| Partidas guardadas | SaveGameSlot (todo el formato de la partida), LoadGameSlotResume, CheckSRAMIntegrity (el comprobador también compara la RAM con batería) | `src/save.c` |
| Sonido | UNK_Audio17 / 7 (esperar "listo"), UNK_Audio8, UNK_Audio15 / 19 (efectos de sonido), UNK_Audio16, UNK_Audio2 (órdenes al chip de sonido, comprobadas grabando sus respuestas) | `src/audio.c` |
| Cuadro de texto | PrintTextFrame (ADDDDFFFF: la impresora, código por código), StartTextBox, CloseTextBox (CODE_8393F9), SUB_83932D (borrar el cuadro), BlinkTextBoxArrow / TextBoxArrow / DrawTextBoxArrow (la flecha de "hay más texto"), FindLetterGraphics / DrawLetter / AdvanceLetterCursor / BlinkTextCursor (letras), PrintDigit (números) | `src/text.c` |
| Objetos | SpawnMapObject (CODE_81A500), CreateGameObject (CODE_858000), LoadGameObjectComponents, LoadSpriteComponent (sprites y los objetos que dejan las herramientas en el mapa) | `src/objects.c` |
| Herramientas | GetTileContent, UNK_CheckTileProperty, CheckToolSuccess, CalculateTileinFront, TileAtOffsetFromPlayer, PreCheckToolSuccess / Line / Square, SUB_82927D | `src/tools.c` |
| Usar las herramientas | Las 28 rutinas ToolUsed*: hoz, azada, martillo, hacha, sus versiones de oro, las cinco semillas, pintura, comida, medicina, poción, campana, ordeñadora, cepillo, habichuelas, semilla de gema, pluma azul, regadera, aspersor; SUB_829260 (usa el objeto en la mano: una rutina por objeto, por Tool_Used_Table) | `src/toolused.c` |

Algunas cosas que se descubrieron por el camino (detalles en los
comentarios de cada archivo):

- "1 de cada n" en el juego no es exactamente 1/n: RNGReturn0toA favorece
  un poco el 0.
- Lluvia 1/6 en primavera y 1/10 en verano y otoño; nieve 1/3 en invierno;
  huracán 1/30 en verano (1/60 con el caparazón de tortuga).
- El primer año trae dos tormentas fijas: invierno día 8 y verano día 30.
- De noche, la lluvia rompe cercas (1/96), el huracán además arranca pasto
  (1/16) y cultivos (1/4), y el año nuevo borra pasto (1/64) y cultivos
  (1/32).
- Un cultivo crece una etapa por noche solo si estaba regado; la lluvia
  riega todo. En otoño los cultivos regados solo se secan, y el primer día
  de invierno todos mueren y vuelven a tierra arada.
- El "desarrollo del rancho" de la evaluación final es simplemente cuántas
  casillas de la granja tienen cercas, cultivos o pasto.
- Una vaca con hambre puede enfermarse (1/4); si sigue enferma hasta el
  final, muere, salvo que mañana sea festival. El embarazo cuenta con un
  contador global, y la felicidad inicial de la cría (20 / 50 / 100) depende
  de la de la madre.
- Hijos: el primero pide 450 corazones de la esposa; el segundo, 650 y que
  el primero tenga 90 días; los dos, la casa en nivel 2. Un hijo nace 60
  días después de concebido.
- Si una herramienta funciona lo dice un bit de la tabla de propiedades de
  la casilla (hoz 01, azada 02, martillo 04, hacha 08, agua 10, semillas
  20), y solo desde los lados y en las estaciones que la casilla permite.
  Cualquier objeto que no esté en esa lista "funciona".
- El sufijo del día es "st/nd/rd" solo para los días 1-3, así que el juego
  escribe "21th", "22th" y "23th".
- **Un bug del juego original:** en CowFeedingandStatus, después de tirar el
  dado de 1/16 para una vaca que quedó afuera, el registro X se queda en 8
  bits; si además está la bandera de cerca rota ($0196 bit $0400),
  `LDY.W #$0002` en $83:BDE1 se lee como instrucción de 2 bytes y el `00`
  sobrante se ejecuta como `BRK`.
- Hallazgos con las herramientas: con la azada, una vez al día, 1 de 16
  una moneda (10 G) o si no 1 de 16 una bolsa de dinero (50 G); en
  primavera y verano 1 de 32 un topo; 1 de 64 una baya de poder, solo dos
  veces en todo el juego. La hoz encuentra una rana en verano (1 de 16,
  +2 de felicidad). Una baya más en la roca del manantial (martillo, 1 de
  4) y otra en un tocón fuera de la granja (hacha, 1 de 16).
- Las piedras grandes y los tocones se rompen al 6.º golpe (un mismo
  contador, $096D); el martillo y el hacha de oro, de un golpe. La azada
  de oro ara hasta 6 casillas en línea y para en la primera que no puede;
  la hoz de oro corta las 9 de alrededor. El hacha de oro se salta la
  revisión de "hoy no hay hallazgos".
- Las semillas solo prenden en su estación (maíz y tomate en verano, papa
  y nabo en primavera, pasto en primavera y verano); fuera de estación la
  casilla queda como un brote muerto.
- Pintar la casa: cuatro partes de la pared, +10 de felicidad cada una.
- El puntaje del rancho del final (0-999) suma dinero (máximo 78),
  animales (3 cada uno), energía máxima, corazones de las chicas,
  cultivos enviados, felicidad, la casa, los hijos, el reloj, el
  caparazón de tortuga, el desarrollo de la granja y la felicidad de cada
  vaca. **Errores:** corazones y envíos se recortan con `AND #$1FF` en vez
  de tener un tope (enviar 512 nabos cuenta 0), y del dinero solo cuentan
  los 16 bits bajos (655.360 G cuentan 0).
- El ánimo del perro cambia cada 2 segundos entre cinco estados y una
  tirada al azar decide qué hace; cada abrazo se cuenta ($7F1F52). Una
  gallina que no se levanta arranca otro guion cada $200 cuadros.
- Hay como mucho 10 "objetos del mapa" (los efectos que dejan las
  herramientas, como la hierba cortada). Si los 10 están en uso cuando hace
  falta un 11.º, el juego se congela: `BRA` a sí mismo en $81:A528.

## Carpetas

- `src/` — las rutinas reescritas y el comprobador (`hm_decomp.c/.h`).
- `data/decomp_names.tsv` — nombres de 499 rutinas (de HM-Decomp).
- `tools/build.sh` — genera, renombra y compila el ejecutable de prueba.
- `tools/check.sh` — corre las pruebas (al azar, partida nueva y noches).
- `tools/progress.py` — calcula el porcentaje de arriba.
- `tools/import_decomp_names.py`, `tools/decomp_overrides.py` — herramientas.
- `scripts/` — recorridos automáticos usados por las pruebas.
- `GUIA.md` — **una guía de cómo funciona el juego por dentro**, para
  quien no sabe nada de esto, con todo lo descubierto hasta ahora (en
  inglés: `GUIDE.md`).
- `DECOMPILACION.md` — el método explicado. `ESTADO.md` — estado y notas.

## Uso

Con los tres proyectos uno al lado del otro:

```
HarvestMoonSNESRecomp/   (git clone + bash tools/apply_patches.sh)
HarvestMoonSnesDecomp/
HM-Decomp/
```

```
cd HarvestMoonSnesDecomp
tools/build.sh --rom /ruta/a/tu/rom.sfc            # HM_RECOMP=... si la recompilación está en otro lado
HM_ROM=/ruta/a/tu/rom.sfc tools/check.sh 2000 10   # 10 semillas x 2000 llamadas al azar + partidas
python3 tools/progress.py --decomp ../HM-Decomp --update-readme
```

La descompilación **no modifica** el proyecto de recompilación: genera su
propia copia del código a partir de tu ROM, le pone los nombres y los
comprobadores, y compila un ejecutable de prueba en `build/`. Sin
`HM_DECOMP_CHECK` / `HM_DECOMP_FUZZ`, ese ejecutable juega exactamente igual
que el juego normal.

## Licencia

[PolyForm Noncommercial 1.0.0](LICENSE), la misma que HarvestMoonSNESRecomp:
uso libre sin fines comerciales. Cubre solo el código de este repositorio,
no el juego ni sus datos.

## Créditos

- [HM-Decomp](https://github.com/ElisaS-Codes/HM-Decomp), de ElisaS-Codes y
  DasK: el desensamblado, los nombres y el mapa de RAM.
- [snesrecomp](https://github.com/RetroPortingToolKit/snesrecomp): el
  recompilador estático sobre el que corre todo.
