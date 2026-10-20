# Cómo se hace la descompilación

Este documento explica el método, paso a paso.

## Punto de partida

El proyecto de recompilación (HarvestMoonSNESRecomp) ya traduce todo el juego
a C, pero ese C es una
traducción literal de instrucciones del 65816: `cpu->A`, banderas, saltos.
Funciona, pero no se entiende. **Decompilar** es reescribir cada rutina como
C que una persona pueda leer, con nombres que digan qué hace.

## Paso 1: nombres (hecho)

`tools/import_decomp_names.py` toma los nombres que la comunidad le puso a
las rutinas en el desensamblado HM-Decomp (ElisaS-Codes) y los guarda en
`data/decomp_names.tsv`. `tools/build.sh` genera su propia copia del código y
`tools/decomp_overrides.py` le pone esos nombres: `bank_83_8138_M1X0` pasa a
llamarse `GetRNG_M1X0`. Hoy: **478 rutinas con nombre**. Es solo un cambio de
nombre: el programa hace exactamente lo mismo.

## Paso 2: reescrituras en C legible (empezado)

Cada rutina reescrita vive en `src/<sistema>.c`:

| Rutina | Archivo | Qué hace |
|---|---|---|
| GetRNG | rng.c | Generador de números aleatorios (LFSR de 24 bits) |
| AddWood / AddGrass / AddPlayerHappiness | stock.c | Suma a madera, forraje y felicidad, tope 999 |
| ChangeStamina | stamina.c | Energía del jugador y las etapas de cansancio |
| GetsCowPointer / GetChickenPointer | animals.c | Dónde están los datos de cada vaca y gallina |
| DivisionUnsigned | math.c | División de 16 bits (por hardware o a mano) |
| RNGReturn0toA | rng.c | "Tirar un dado de n caras" (0..n-1) |
| SetWeatherFlags / WeatherTomorrow | weather.c | El clima de mañana y sus banderas |
| GetTileIndex | farm.c | De posición en píxeles a número de casilla |
| LoadsDamageProbabilities / ClimateFarmDamageCheck / ClimateFarmDamagePrep | farm.c | Lo que el mal tiempo rompe en la granja de noche |

Las direcciones de memoria tienen nombre en `src/hm_ram.h` del proyecto de
recompilación (es el mapa de memoria que comparten los dos proyectos).

## Cómo sabemos que una reescritura es correcta

No basta con que "parezca bien". Cada rutina reescrita se compara contra la
original, byte por byte:

* **Mientras juegas** (`HM_DECOMP_CHECK=1`): en cada llamada se guarda una
  copia de la memoria, corre la rutina original, luego la reescritura sobre
  la copia, y se comparan la memoria completa (128 KB) y los registros.
* **Con entradas al azar** (`HM_DECOMP_FUZZ=<cuadro>:<veces>`): en ese cuadro
  se llama a cada rutina miles de veces con valores aleatorios (incluidos los
  bordes, como 0, 998, 999, 1000) y se compara cada llamada. Después la
  máquina se restaura y el juego sigue igual.

Ejemplo real: la prueba al azar encontró que `ChangeStamina` decide el signo
de la energía con la bandera N que dejó *quien la llama*, no con el valor en
A. Es una rareza del código original; la reescritura ahora la imita.

Otro ejemplo: el comprobador compara toda la WRAM, y la pila vive ahí. Si
la rutina original hace PHA o JSL, deja bytes debajo del puntero de pila, y
la reescritura tiene que dejar los mismos (incluido el banco que guarda el
JSL). Por eso `HmRegs` lleva S y PB, y hay ayudantes `hm_push8`, `hm_jsl`,
`hm_rtl`.

Algunas rutinas (WeatherTomorrow) no tienen cuerpo compilado: el juego las
corre en el intérprete de la recompilación. Esas se marcan `HM_INTERP` y la
prueba al azar llama al original por el intérprete para comparar.

Las rutinas que se llaman con JSR (en vez de JSL) se marcan `HM_JSR`. Las
que necesitan entradas grandes (una granja entera) llevan una función
`prep` que las prepara al azar, y un tope de llamadas (`fuzz_max`).

Resultado actual: 15 rutinas, 10 semillas × 2.000 llamadas al azar cada una
(`HM_DECOMP_FUZZ_SEED`; 40 granjas enteras por semilla para las de la
granja) + las llamadas reales de una partida nueva y de 10 noches forzadas:
**0 diferencias**.

## El porcentaje

`tools/progress.py` cuenta, en el desensamblado de HM-Decomp, las rutinas de
código y sus instrucciones, y cuántas de ellas están registradas en
`src/hm_decomp.c`. El porcentaje principal es por instrucciones. Con
`--update-readme` lo escribe en el README.

## Cómo seguir

1. Elegir una rutina "hoja" (que no llame a otras) de HM-Decomp.
2. Escribirla en `src/`, registrarla en `src/hm_decomp.c` con su rango de
   entradas para la prueba al azar.
3. `tools/build.sh` y luego `tools/check.sh`.
4. Cuando todas las hojas de un sistema estén verificadas, subir un nivel
   (rutinas que llaman a esas hojas).

El juego (proyecto de recompilación) no contiene nada de esto. Cuando un
sistema entero esté verificado aquí, se podrá decidir si el juego pasa a
usar la versión reescrita.
