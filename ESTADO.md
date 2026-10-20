# Estado de la descompilación (pausada el 19-oct-2026)

Memoria del proyecto para retomarlo sin perder nada. Por ahora el trabajo
sigue en la recompilación (HarvestMoonSNESRecomp).

## Hecho

- **Proyecto separado** del juego. Se compila con `tools/build.sh` (usa la
  recompilación como base, sin modificarla) y se prueba con `tools/check.sh`.
- **Nombres**: 478 rutinas renombradas con los nombres de HM-Decomp
  (`data/decomp_names.tsv`).
- **Comprobador**: cada rutina reescrita se compara byte por byte con la
  original, en partidas reales (`HM_DECOMP_CHECK=1`) y con entradas al azar
  (`HM_DECOMP_FUZZ=<cuadro>:<veces>`).
- **7 rutinas reescritas y verificadas** (0 diferencias):

| Rutina | Dirección | Archivo |
|---|---|---|
| GetRNG | $83:8138 | src/rng.c |
| AddWood | $83:B224 | src/stock.c |
| AddGrass | $83:B253 | src/stock.c |
| AddPlayerHappiness | $83:B282 | src/stock.c |
| ChangeStamina | $81:D061 | src/stamina.c |
| GetsCowPointer | $83:C9A7 | src/animals.c |
| GetChickenPointer | $83:C995 | src/animals.c |

Rareza encontrada: ChangeStamina decide el signo del cambio con la bandera N
que deja quien la llama, no con el valor de A.

## Lo siguiente que iba a hacer: el clima

Ya leído en HM-Decomp (bank_80/82/83), falta escribirlo:

1. **DivisionUnsigned** ($83:8082): $7E / $80. Divisor < 255 usa el divisor
   de hardware ($4204-$4206, resultado en $4214, resto en $4216 -> $7E);
   divisor >= 255 divide a mano restando (16 vueltas, resultado en $82).
   Ojo: divisor 0 en hardware da cociente $FFFF y resto = dividendo.
2. **RNGReturn0toA** ($80:89F9): paso = 255 / A; r = GetRNG(); devuelve
   cuántos múltiplos de `paso` caben antes de llegar a r (máx. A-1). Llama a
   DivisionUnsigned y GetRNG (ambas deben estar reescritas antes).
3. **SetWeatherFlags** ($82:8C09): convierte el clima de mañana en banderas
   $0196 (tabla $82:8CED: 0004, 0002, 0008, 0010, 0200, 0100).
   Rarezas: soleado solo pone bandera en verano; lluvia en invierno = nieve;
   nieve en primavera = lluvia; clima 4 y 5 también tocan $7F1F64/$7F1F6E.
4. **WeatherTomorrow** ($82:8CF9): festivales por fecha; tormenta, huracán
   (el caparazón de tortuga $7F1F6C bit $1000 duplica el valor que se le
   pasa a RNGReturn0toA), lluvia y nieve por estación (tablas $82:8EB2-8EC5).

Otras hojas candidatas (no llaman a nada): LoadsDamageProbabilities,
SetTimeofDayPalette, CalculateTileinFront, SUB_809EBC (cámara al cambiar de
pantalla), y los comandos de eventos `Event_*` del banco 84.

## Cómo retomarlo

```
HM_RECOMP=../HarvestMoonRecomp/codigo-fuente tools/build.sh
tools/check.sh 2000
```

Para cada rutina nueva: escribirla en `src/`, registrarla en
`src/hm_decomp.c` (con su rango de entradas para la prueba al azar),
compilar y comprobar. Ver `DECOMPILACION.md`.
