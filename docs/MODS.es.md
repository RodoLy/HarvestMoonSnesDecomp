# Cómo hacer un mod

*[English](MODS.md)*

La versión nativa carga **mods**: cambios a los dibujos, textos, mapas,
música o tablas del juego, hechos por cualquiera y activados en el
launcher (**Mods**, en su primera página). Tu archivo de ROM nunca se
modifica: los mods que activas se aplican a una copia en memoria al
iniciar el juego (y al **Reiniciar**).

## Qué es un mod

Una carpeta dentro de `mods/`, junto al programa (en Android, en la
carpeta de la app en el almacenamiento compartido,
`Android/data/io.github.rodriloy.harvestmoon/files/mods/`, junto a
`config.ini`):

```
mods/
  Textos en español/
    mod.ini
    espanol.ips
  Ropa de invierno/
    mod.ini
    ropa.bps
```

- **`mod.ini`** (opcional): lo que muestra el launcher.

  ```ini
  [Mod]
  Name = Ropa de invierno
  Author = Tu nombre
  Version = 1.0
  Description = El granjero usa bufanda y abrigo todo el año.
  ```

  Sin él, el mod aparece con el nombre de su carpeta.
- **`*.ips` y `*.bps`**: los parches, que se aplican en el orden de sus
  nombres de archivo. Son los parches de siempre del ROM hacking, así que
  cualquier parche hecho para **Harvest Moon (USA)** se puede usar.

## Cómo hacer el parche

1. Copia tu ROM y modifica la copia con las herramientas de siempre: un
   editor de tiles (YY-CHR, Tile Molester) para los dibujos, un editor
   hexadecimal con la tabla de texto del juego para los textos, un editor
   de mapas...
2. Crea el parche a partir de la ROM original y la modificada con
   [Floating IPS (Flips)](https://github.com/Alcaro/Flips). **BPS** es
   mejor opción (revisa que se aplique a la ROM correcta); **IPS** también
   funciona.
3. Pon el parche y un `mod.ini` en una carpeta, la carpeta en `mods/`, y
   actívalo en el launcher.

Lo único que se comparte es el parche: nunca compartas la ROM.

## Qué funciona

Este programa es el juego reescrito en C: lee los **datos** de la ROM
(gráficos, textos, mapas, paletas, música, tablas del juego) pero no su
**código** 65816. Así que lo que un parche cambia en los datos se ve en
el juego, y lo que cambia en el código no hace nada. La mayoría de los
hacks de gráficos y textos son datos; los que agregan código nuevo (una
fuente de ancho variable, eventos nuevos) solo funcionan a medias.

## Detalles

- Los parches IPS son para la ROM **sin** cabecera de copiador (`.sfc`,
  2 MB). Para un parche hecho para una ROM con cabecera (`.smc`), agrega
  `Header = yes` en `mod.ini`.
- Los mods se aplican en el orden en que los activaste. Un parche BPS
  revisa la ROM que recibe: si otro mod cambió la ROM antes, no se aplica
  y el launcher dice por qué. Los IPS se aplican tal cual (si dos cambian
  los mismos bytes, gana el último).
- Un mod que falla se deja fuera; los demás sí se aplican.
- Activar o desactivar mods dentro del juego tiene efecto al
  **Reiniciar** (o la próxima vez que abras el programa). Los estados
  guardados son de los mods con los que se hicieron.
