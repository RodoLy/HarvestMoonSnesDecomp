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

> [!WARNING]
> **Your computer or phone will warn you the first time: that is expected.** This is a free hobby project, not a company's app: the programs are not signed with a paid certificate (Windows, Mac) and the APK is not from the Play Store, so the system cannot vouch for them and asks you first. Nothing is wrong with them: they are built by GitHub itself from the public source in this repository (see the **Actions** tab), and each file's SHA-256 is below so you can check it is the same one. / **Tu computadora o teléfono te va a advertir la primera vez: es normal.** Es un proyecto gratuito hecho por aficionados, no una app de una empresa: los programas no están firmados con un certificado de pago (Windows, Mac) y el APK no viene de la Play Store, así que el sistema no puede responder por ellos y te pregunta antes. No tienen nada malo: los compila GitHub mismo con el código público de este repositorio (ver la pestaña **Actions**), y abajo está el SHA-256 de cada archivo para comprobar que es el mismo.

**What you will see, and what to do / Qué vas a ver y qué hacer:**
- **Windows** — *"Windows protected your PC"*: click **More info → Run anyway**. Your browser may also say the file *"isn't commonly downloaded"*: choose **Keep**. / *«Windows protegió tu PC»*: da clic en **Más información → Ejecutar de todas formas**. El navegador también puede decir que el archivo *«no se descarga habitualmente»*: elige **Conservar**.
- **Mac** — *"cannot be opened because it is from an unidentified developer"*: right-click `Harvest Moon.command` → **Open** → **Open** (on macOS 15: System Settings → Privacy & Security → **Open Anyway**; or, in Terminal, `xattr -dr com.apple.quarantine` on the folder). / *«no se puede abrir porque es de un desarrollador no identificado»*: clic derecho en `Harvest Moon.command` → **Abrir** → **Abrir** (en macOS 15: Ajustes del Sistema → Privacidad y seguridad → **Abrir igualmente**; o, en Terminal, `xattr -dr com.apple.quarantine` sobre la carpeta).
- **Android** — *"For your security, your phone is not allowed to install unknown apps from this source"*: tap **Settings** and allow it for that browser or file manager, then go back and **Install**. Play Protect may offer to scan it: let it, then **Install anyway** if it asks. On the AYN Thor, launch the app on the top screen: the pause menu stays on the lower one. / *«Por tu seguridad, tu teléfono no puede instalar apps desconocidas de esta fuente»*: toca **Ajustes** y permítelo para ese navegador o explorador, luego regresa e **Instalar**. Play Protect puede ofrecer analizarla: déjalo, y si pregunta, **Instalar de todas formas**. En la AYN Thor, abre la app en la pantalla de arriba: el menú de pausa se queda en la de abajo.
- **Linux** — if it does not start, make it executable: `chmod +x HarvestMoonNative`. / si no arranca, hazlo ejecutable: `chmod +x HarvestMoonNative`.

Prefer not to run a downloaded program? Build it yourself from the source with `build.bat` (Windows: no warning then, it is made on your PC), `build_mac.command` or `build_android.bat`. / ¿Prefieres no abrir un programa descargado? Compílalo tú con `build.bat` (en Windows así no sale el aviso, porque se hace en tu PC), `build_mac.command` o `build_android.bat`.

### New in 0.2.0-beta

- **Ready-made downloads** for Windows, Mac (Apple Silicon and Intel), Linux and **Android** — no need to build anything.
- **Android** (phones, tablets, AYN Thor): touch controls in the look of the game's menus (in the side bars at 4:3, see-through with widescreen); on two-screen devices like the **AYN Thor**, the pause menu stays **live on the second screen**, used by touch.
- **Mods**: a `mods` folder and a **Mods** page in the launcher; drop the community's IPS / BPS patches in and turn them on. Your ROM is never changed.
- **The launcher by day**: the start screen's farm under a sunny sky.
- New improvements (all off by default):
  - **Your wife helps on the farm**: married with her ten hearts, talk to her again after her line of the day and she offers to help for a day, three days or a week; mornings she feeds the animals and waters (half of the work at most, hopping over fences), then goes to her midday chores.
  - **Her old clothes some days**: one day in two your wife wears the clothes she wore before the wedding (an improvement of its own).
  - **Crop bag**: 9 slots for what you carry, inside the pause menu; hold Q at the shipping box to ship it all.
  - **Walk through crops**: crops no longer block the way, and the plants sway as you pass.
  - **One seed at a time**: each use sows only the tile in front of you.
  - **Mouse controls**: click to use a tool, walk or talk; the pause menu works with the mouse too.
- **Pause menu** (Harvest Moon 64 style): tools and seeds with their original sprites, an Inventory tab, and an **Animals** tab (cows with ten hearts of happiness, chickens).
- **Widescreen** follows your screen's shape (16:10 or 16:9) and now covers the start screens too (logo, intro, title, diaries).
- Fixes: the intro road no longer goes crooked when Start is pressed; picking up with "walk through crops" no longer harvests endlessly. On Windows, a ROM in a folder with accents or other letters now opens; a ROM still in its .zip / .7z download is recognised and the launcher says to extract it.

### Novedades de la 0.2.0-beta

- **Descargas listas para jugar** para Windows, Mac (Apple Silicon e Intel), Linux y **Android**: ya no hay que compilar nada.
- **Android** (teléfonos, tablets, AYN Thor): controles táctiles con el aspecto de los menús del juego (en las franjas de los lados en 4:3, transparentes con pantalla ancha); en equipos de dos pantallas como la **AYN Thor**, el menú de pausa queda **en vivo en la segunda pantalla** y se usa tocándolo.
- **Mods**: una carpeta `mods` y una página **Mods** en el launcher; pon ahí los parches IPS / BPS de la comunidad y actívalos. Tu ROM nunca se modifica.
- **El launcher de día**: la granja de la pantalla de inicio bajo un cielo soleado.
- Mejoras nuevas (todas apagadas de fábrica):
  - **Tu esposa te ayuda en la granja**: casado y con sus diez corazones, háblale otra vez después de su frase del día y te ofrece ayudarte un día, tres días o una semana; en las mañanas alimenta a los animales y riega (como máximo la mitad del trabajo, brincando las cercas) y luego se va a sus quehaceres del mediodía.
  - **Su ropa de soltera algunos días**: uno de cada dos días tu esposa se viste con la ropa de antes de la boda (una mejora aparte).
  - **Mochila de cosechas**: 9 espacios para lo que cargas, dentro del menú de pausa; mantén Q en la caja de envíos para enviarlo todo.
  - **Caminar entre cultivos**: los cultivos ya no bloquean el paso y las plantas se mecen al pasar.
  - **Una semilla a la vez**: cada uso siembra solo la casilla de enfrente.
  - **Controles con ratón**: clic para usar una herramienta, caminar o hablar; el menú de pausa también funciona con el ratón.
- **Menú de pausa** (estilo Harvest Moon 64): herramientas y semillas con sus sprites originales, una pestaña de Inventario y una de **Animales** (vacas con diez corazones de felicidad, gallinas).
- **Pantalla ancha** que toma la forma de tu pantalla (16:10 o 16:9) y ahora también en las pantallas de inicio (logo, introducción, título, diarios).
- Arreglos: el camino de la introducción ya no se tuerce al presionar Start; recoger con «caminar entre cultivos» ya no cosecha sin fin. En Windows, una ROM en una carpeta con acentos u otras letras ya abre; una ROM todavía dentro de su .zip / .7z se reconoce y el launcher indica descomprimirla.

### What's in it / Qué trae
- The whole game in C, with the console's picture and sound. / Todo el juego en C, con la imagen y el sonido de la consola.
- A launcher in the look of the game (English, Español, Italiano): options, controls (keyboard, gamepads, touch), save states. / Un launcher con el aspecto del juego: opciones, controles (teclado, gamepads, táctil), estados guardados.
- **Everything starts off**: the game is exactly the console's until you turn on what you like. / **Todo viene apagado**: el juego es el de la consola hasta que activas lo que quieras.
  - Improvements / Mejoras: widescreen, Harvest Moon 64-style pause menu, crop bag, walk through crops, one seed at a time, mouse controls, your wife helps on the farm, her old clothes some days, fast text, longer days, endless game, OPTIONS on the title screen.
  - Cheats / Trucos: money, stamina, stop the clock, lumber and fodder, seeds and feed, weather.
- **Mods**: drop the community's IPS / BPS patches into `mods/` and turn them on in the launcher (see `mods/MODS.md`). / Pon los parches IPS / BPS de la comunidad en `mods/` y actívalos en el launcher (ver `mods/MODS.es.md`).
- Android: touch controls in the game's style; on two-screen devices (AYN Thor) a live pause menu on the second screen. / Android: controles táctiles con el estilo del juego; en equipos de dos pantallas (AYN Thor), el menú de pausa en vivo en la segunda pantalla.

This is a **beta**: please report problems in Issues. / Es una **beta**: reporta los problemas en Issues.
