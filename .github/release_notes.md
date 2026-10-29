**The first playable beta of Harvest Moon (SNES) running on its decompiled C** — no CPU emulation, no recompiled code. / **La primera beta jugable de Harvest Moon (SNES) corriendo sobre su C descompilado** — sin emular la CPU ni código recompilado.

> [!IMPORTANT]
> **You need your own legal copy of Harvest Moon (USA).** This download has no ROM, no code generated from it and no ready-made program: it is the source code plus a build script, and the game is built on your computer. / **Necesitas tu propia copia legal de Harvest Moon (USA).** La descarga no trae la ROM ni un programa ya hecho: es el código y un script que lo compila en tu computadora.

### How to play (Windows 10/11, x64 or ARM)
1. Download the `HarvestMoonNative-….zip` below and unzip it.
2. Double-click **`build.bat`**. The first time it downloads a C compiler and SDL3 (~190 MB, checked by SHA-256), then builds the game in about a minute.
3. Open **`HarvestMoonNative.exe`** and choose your ROM the first time; it is remembered.

Linux: `cmake -S native -B build/native && cmake --build build/native` (see `HOW_TO_PLAY.txt`).

### Cómo jugar (Windows 10/11, x64 o ARM)
1. Descarga el `HarvestMoonNative-….zip` de abajo y descomprímelo.
2. Doble clic en **`build.bat`** (o `compilar_nativo.bat`). La primera vez descarga un compilador y SDL3 (~190 MB), luego compila en un minuto.
3. Abre **`HarvestMoonNative.exe`** y elige tu ROM la primera vez.

### What's in it / Qué trae
- A launcher in the look of the game: Options, Controls (keyboard and gamepad, connected gamepads), Improvements, Save states (10 slots). English, Spanish and Italian.
- **Everything starts off** — the game is exactly the console's until you turn on what you like / **todo viene apagado** y lo activas tú:
  - 16:9 widescreen, Harvest Moon 64-style pause menu (M / R3), OPTIONS on the title screen, endless game after Year 3, fast text, longer days;
  - cheats: money, stamina, stop the clock, lumber and fodder, seeds and feed, weather.
- Fast forward (Tab / right trigger) with the music sped up, save states (F1–F10, Shift+F1–F10), full screen (F11).

This is a **beta**: please report problems in Issues. / Es una **beta**: reporta los problemas en Issues.
