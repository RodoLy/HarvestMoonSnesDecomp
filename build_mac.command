#!/bin/bash
# build_mac.command - builds the native Harvest Moon on this Mac.
#
# Double-click it in Finder (the first time macOS may say it is from an
# unidentified developer: right-click it > Open > Open). Or, in Terminal:
#     bash build_mac.command
#
# Needs Apple's command line tools (the C compiler); if they are missing it
# asks macOS to install them. The first time it downloads CMake (50 MB,
# checked by its SHA-256, into build/tools) unless you have one, and
# SDL3's source (window, sound, controls). Then it builds the game, a few
# minutes the first time, and leaves HarvestMoonNative and
# "Harvest Moon.command" (double-click to play) in this folder.
# Works on Apple Silicon (M1 and later) and Intel Macs, macOS 11 or newer.

cd "$(dirname "$0")" || exit 1
ROOT="$(pwd)"

finish() {
  echo
  read -r -p "Press Enter to close / Enter para cerrar... " _
  exit "$1"
}
fail() {
  echo
  echo "ERROR: $1"
  finish 1
}

# 1. the C compiler (Apple's command line tools)
if ! xcode-select -p >/dev/null 2>&1 || ! /usr/bin/xcrun --find clang >/dev/null 2>&1; then
  echo "Apple's command line tools are needed (the C compiler)."
  echo "Hacen falta las herramientas de linea de comandos de Apple (el compilador)."
  xcode-select --install >/dev/null 2>&1
  echo
  echo "A window to install them should open: accept it, wait until it ends,"
  echo "then open this file again."
  echo "Deberia abrirse una ventana para instalarlas: acepta, espera a que"
  echo "termine y vuelve a abrir este archivo."
  finish 1
fi

# 2. CMake: the one installed (Homebrew...), or a copy of its own
CMAKE="$(command -v cmake 2>/dev/null)"
if [ -z "$CMAKE" ]; then
  TOOLS="$ROOT/build/tools"
  [ -d "$ROOT/build/herramientas" ] && [ ! -d "$TOOLS" ] && mv "$ROOT/build/herramientas" "$TOOLS"   # its name before 0.2
  NAME="cmake-3.31.10-macos-universal"
  SHA="be9f3faeeaf7921cc2d77cea711dd5e6f72c63af2810cacd9205b3ce8d1593c9"
  CMAKE="$TOOLS/$NAME/CMake.app/Contents/bin/cmake"
  if [ ! -x "$CMAKE" ]; then
    mkdir -p "$TOOLS" || fail "cannot create $TOOLS"
    echo "Downloading / Descargando CMake (50 MB)..."
    curl -fL --progress-bar -o "$TOOLS/$NAME.tar.gz.part" \
      "https://github.com/Kitware/CMake/releases/download/v3.31.10/$NAME.tar.gz" \
      || fail "the download of CMake failed / la descarga de CMake fallo"
    GOT="$(shasum -a 256 "$TOOLS/$NAME.tar.gz.part" | cut -d' ' -f1)"
    if [ "$GOT" != "$SHA" ]; then
      rm -f "$TOOLS/$NAME.tar.gz.part"
      fail "the download of CMake is not the expected file / la descarga de CMake no coincide"
    fi
    tar -xzf "$TOOLS/$NAME.tar.gz.part" -C "$TOOLS" || fail "could not unpack CMake / no se pudo descomprimir CMake"
    rm -f "$TOOLS/$NAME.tar.gz.part"
  fi
fi

# 3. the console's chips (picture and sound): native/chips, or copied from
#    the recompilation project next to this one
CHIPS="$ROOT/native/chips"
if [ ! -f "$CHIPS/snes/ppu.c" ]; then
  RECOMP="$HM_RECOMP"
  if [ -z "$RECOMP" ]; then
    for c in "$ROOT/../HarvestMoonSNESRecomp" "$ROOT/../HarvestMoonRecomp"; do
      if [ -f "$c/snesrecomp/runner/src/snes/ppu.c" ]; then RECOMP="$c"; break; fi
    done
  fi
  [ -n "$RECOMP" ] || fail "native/chips is missing / falta la carpeta native/chips"
  R="$RECOMP/snesrecomp/runner/src"
  mkdir -p "$CHIPS/snes"
  for f in ppu ppu_legacy ws_shadow dma sdd1 apu spc dsp dsp_shadow audio_shadow color_lut; do
    cp "$R/snes/$f.c" "$CHIPS/snes/" || fail "cannot copy $f.c"
  done
  cp "$R"/snes/*.h "$CHIPS/snes/"
  for f in audio_trace.c audio_trace.h types.h debug_server.h; do cp "$R/$f" "$CHIPS/"; done
  cp "$RECOMP/src/hm_ram.h" "$CHIPS/"
fi

# 4. the game
echo "Building / Compilando (a few minutes the first time / unos minutos la primera vez)..."
"$CMAKE" -S native -B build/native-mac -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
  || fail "the build could not be set up / no se pudo preparar la compilacion"
"$CMAKE" --build build/native-mac --parallel "$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" \
  || fail "the build failed, see the messages above / la compilacion fallo, mira los mensajes de arriba"
cp build/native-mac/HarvestMoonNative "$ROOT/HarvestMoonNative" || fail "cannot copy HarvestMoonNative"

# 5. a double-click to play
cat > "$ROOT/Harvest Moon.command" <<'EOF'
#!/bin/bash
# Double-click to play / Doble clic para jugar.
cd "$(dirname "$0")" && exec ./HarvestMoonNative "$@"
EOF
chmod +x "$ROOT/Harvest Moon.command" "$ROOT/HarvestMoonNative"

echo
echo "Done / Listo: double-click \"Harvest Moon.command\" to play / doble clic para jugar."
finish 0
