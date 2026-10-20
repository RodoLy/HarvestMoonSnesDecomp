# HarvestMoonDecomp — descompilación de Harvest Moon (SNES)

Proyecto **separado** de la recompilación. Aquí el juego se reescribe, rutina
por rutina, como C que una persona puede leer, y cada rutina reescrita se
comprueba contra la original byte por byte.

| | HarvestMoonSNESRecomp | HarvestMoonDecomp (este) |
|---|---|---|
| Para qué | Jugar: el port de PC con sus mejoras | Entender el juego: código legible |
| Resultado | El juego (`.exe`) | Rutinas en `src/` + un ejecutable de prueba |
| Depende de | Tu ROM | Tu ROM y el proyecto de recompilación |

La descompilación **no modifica** el proyecto de recompilación. Lo usa como
base: genera su propia copia del código a partir de tu ROM, le aplica los
nombres y los comprobadores, y compila un ejecutable de prueba en `build/`.

## Carpetas

- `src/` — las rutinas reescritas (`rng.c`, `stock.c`, `stamina.c`, `animals.c`)
  y el comprobador (`hm_decomp.c/.h`).
- `data/decomp_names.tsv` — nombres de 478 rutinas (de HM-Decomp).
- `tools/build.sh` — genera, renombra y compila el ejecutable de prueba.
- `tools/check.sh` — corre las pruebas (al azar y en una partida real).
- `tools/import_decomp_names.py`, `tools/decomp_overrides.py` — herramientas.
- `scripts/` — recorridos automáticos usados por las pruebas.
- `DECOMPILACION.md` — el método explicado.

## Uso

Con los dos proyectos uno al lado del otro:

```
HarvestMoonSNESRecomp/
HarvestMoonDecomp/
```

En tu PC la recompilación está en `Documentos\HarvestMoonRecomp\codigo-fuente`,
así que se indica con `HM_RECOMP`:

```
HM_RECOMP=../HarvestMoonRecomp/codigo-fuente tools/build.sh
tools/check.sh 2000       # 2000 llamadas al azar por rutina + partida nueva
```

## Estado

7 rutinas reescritas y verificadas: 0 diferencias en 2.000 llamadas al azar
cada una y en 5.549 llamadas reales durante una partida nueva.
