# Estado de la descompilación (al 19-oct-2026)

Memoria del proyecto para retomarlo sin perder nada. El clima y el daño
del clima a la granja ya están reescritos y verificados. Porcentaje actual
en el README (`tools/progress.py`).

## Hecho

- **Proyecto separado** del juego. Se compila con `tools/build.sh` (usa la
  recompilación como base, sin modificarla) y se prueba con `tools/check.sh`.
- **Nombres**: 478 rutinas renombradas con los nombres de HM-Decomp
  (`data/decomp_names.tsv`).
- **Comprobador**: cada rutina reescrita se compara byte por byte con la
  original, en partidas reales (`HM_DECOMP_CHECK=1`) y con entradas al azar
  (`HM_DECOMP_FUZZ=<cuadro>:<veces>`).
- **15 rutinas reescritas y verificadas** (0 diferencias):

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

## Lo siguiente

El crecimiento nocturno de los cultivos: `NightlyFarmTilesCheck` ($82:A811).
Antes, sus hojas. Después `CowFeedingandStatus` ($83:BC5A) para los
animales.

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
