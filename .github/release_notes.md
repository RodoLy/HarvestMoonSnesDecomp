**Harvest Moon (SNES) running on its code rewritten in C**, ready to play on Windows, Mac, Linux and Android (the AYN Thor's two screens included). / **Harvest Moon (SNES) corriendo sobre su código reescrito en C**, listo para jugar en Windows, Mac, Linux y Android (con las dos pantallas de la AYN Thor).

> [!IMPORTANT]
> **You need your own legal copy of Harvest Moon (USA).** These downloads have no ROM and nothing taken from it: the first time, the program asks for your copy, checks it and remembers it. / **Necesitas tu propia copia legal de Harvest Moon (USA).** Las descargas no traen la ROM ni nada sacado de ella: la primera vez el programa te pide tu copia, la revisa y la recuerda.

### Downloads / Descargas

| | |
|---|---|
| **Windows** 10/11 (x64) | `HarvestMoon-{VERSION}-Windows.zip`: unzip, open `HarvestMoonNative.exe` / descomprime y abre `HarvestMoonNative.exe` |
| **Mac** (Apple Silicon and Intel, macOS 11+) | `HarvestMoon-{VERSION}-Mac.zip`: unzip, double-click `Harvest Moon.command` / descomprime y doble clic en `Harvest Moon.command` |
| **Android** 8+ (phones, tablets, AYN Thor) | `HarvestMoon-{VERSION}-Android.apk`: open it on the device to install it / ábrelo en el equipo para instalarlo |
| **Linux** (x86_64) | `HarvestMoon-{VERSION}-Linux.tar.gz`: unpack, run `./HarvestMoonNative` / descomprime y ejecuta `./HarvestMoonNative` |

**The first time / La primera vez:**
- **Windows** may say *"Windows protected your PC"*: the program is not signed with a paid certificate. Click **More info → Run anyway**. / Windows puede decir *«Windows protegió tu PC»*: el programa no está firmado con un certificado de pago. Da clic en **Más información → Ejecutar de todas formas**.
- **Mac** may say it is *from an unidentified developer*: right-click `Harvest Moon.command` → **Open** → **Open** (or, in Terminal, `xattr -dr com.apple.quarantine` on the folder). / Mac puede decir que es *de un desarrollador no identificado*: clic derecho en `Harvest Moon.command` → **Abrir** → **Abrir** (o, en Terminal, `xattr -dr com.apple.quarantine` sobre la carpeta).
- **Android** asks to allow installing apps from the file manager or browser you open the APK with. On the AYN Thor, launch the app on the top screen: the pause menu stays on the lower one. / **Android** pide permitir instalar apps desde el explorador o navegador con que abras el APK. En la AYN Thor, abre la app en la pantalla de arriba: el menú de pausa se queda en la de abajo.

Prefer to build it yourself? The source and the build scripts (`build.bat`, `build_mac.command`, `compilar_android.bat`) are in the repository. / ¿Prefieres compilarlo tú? El código y los scripts están en el repositorio.

### What's in it / Qué trae
- The whole game in C, with the console's picture and sound. / Todo el juego en C, con la imagen y el sonido de la consola.
- A launcher in the look of the game (English, Español, Italiano): options, controls (keyboard, gamepads, touch), save states. / Un launcher con el aspecto del juego: opciones, controles (teclado, gamepads, táctil), estados guardados.
- **Everything starts off**: the game is exactly the console's until you turn on what you like. / **Todo viene apagado**: el juego es el de la consola hasta que activas lo que quieras.
  - Improvements / Mejoras: widescreen, Harvest Moon 64-style pause menu, crop bag, walk through crops, one seed at a time, mouse controls, your wife helps on the farm, fast text, longer days, endless game, OPTIONS on the title screen.
  - Cheats / Trucos: money, stamina, stop the clock, lumber and fodder, seeds and feed, weather.
- **Mods**: drop the community's IPS / BPS patches into `mods/` and turn them on in the launcher (see `mods/MODS.md`). / Pon los parches IPS / BPS de la comunidad en `mods/` y actívalos en el launcher (ver `mods/MODS.es.md`).
- Android: touch controls in the game's style; on two-screen devices (AYN Thor) a live pause menu on the second screen. / Android: controles táctiles con el estilo del juego; en equipos de dos pantallas (AYN Thor), el menú de pausa en vivo en la segunda pantalla.

This is a **beta**: please report problems in Issues. / Es una **beta**: reporta los problemas en Issues.
