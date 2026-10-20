# HarvestMoonSnesDecomp — descompilación de Harvest Moon (SNES)

<!-- progress -->
![Descompilado: 2.49%](https://img.shields.io/badge/descompilado-2.49%25-2f855a)

**2.49%** del código del juego descompilado y verificado: 755 de 30359 instrucciones, 15 de 372 rutinas (4.0%).
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
| Clima | SetWeatherFlags, WeatherTomorrow | `src/weather.c` |
| Granja | GetTileIndex, LoadsDamageProbabilities, ClimateFarmDamageCheck, ClimateFarmDamagePrep | `src/farm.c` |
| Existencias | AddWood, AddGrass, AddPlayerHappiness | `src/stock.c` |
| Jugador | ChangeStamina | `src/stamina.c` |
| Animales | GetsCowPointer, GetChickenPointer | `src/animals.c` |

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
