# HarvestMoonSnesDecomp — descompilación de Harvest Moon (SNES)

[English](README.md) · **Español**

<!-- progress -->
![Descompilado: 10.49%](https://img.shields.io/badge/descompilado-10.49%25-2f855a)

**10.49%** del código del juego descompilado y verificado: 3204 de 30543 instrucciones, 44 de 376 rutinas (11.7%).
Se calcula con `python3 tools/progress.py --decomp ../HM-Decomp` sobre el desensamblado de HM-Decomp.
<!-- /progress -->

El juego reescrito, rutina por rutina, como C que una persona puede leer.
Cada rutina reescrita se **comprueba contra la original byte por byte**: la
memoria completa (128 KB de WRAM, pila incluida) y los registros tienen que
quedar idénticos.

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
| Animales | GetsCowPointer, GetChickenPointer, AddCowHappiness, CowFeedingandStatus (la noche de los animales) | `src/animals.c` |
| Eventos | SetCCPoiner, ResetsCurrentCCStructureLong / Short, SUB_8480F8, UNK_LoadCCDataLong / Short (arrancar un guion de evento) | `src/events.c` |
| Edición del mapa | SUB_81A801, SUB_81A83A, SUB_82B060 (cambiar casillas del mapa actual) | `src/mapedit.c` |
| Herramientas | GetTileContent, UNK_CheckTileProperty, CheckToolSuccess, CalculateTileinFront, TileAtOffsetFromPlayer, PreCheckToolSuccess / Line / Square | `src/tools.c` |

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

## Carpetas

- `src/` — las rutinas reescritas y el comprobador (`hm_decomp.c/.h`).
- `data/decomp_names.tsv` — nombres de 478 rutinas (de HM-Decomp).
- `tools/build.sh` — genera, renombra y compila el ejecutable de prueba.
- `tools/check.sh` — corre las pruebas (al azar, partida nueva y noches).
- `tools/progress.py` — calcula el porcentaje de arriba.
- `tools/import_decomp_names.py`, `tools/decomp_overrides.py` — herramientas.
- `scripts/` — recorridos automáticos usados por las pruebas.
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
