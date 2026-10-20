#!/usr/bin/env bash
# Run the decompilation checks headlessly.
#
#   tools/check.sh [iterations] [seeds]
#
# 1. Fuzz: at frame 1500 of the attract demo, every rewritten routine is
#    called <iterations> times (default 2000) with random inputs; each call
#    is compared with the original byte for byte, then the machine is restored.
#    Repeated with <seeds> different random seeds (default 3).
# 2. Live: a new game is played by script with every real call checked.
# 3. Nights: the same new game, then ten nights forced (poke 0973 02) on
#    chosen dates (festival eves, first-year storm days...), checked live.
# Exits non-zero on any mismatch.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GAME="$ROOT/build/game"
N=${1:-2000}
SEEDS=${2:-3}
OUT="$ROOT/build/check"; rm -rf "$OUT"; mkdir -p "$OUT"
EXE="$GAME/HarvestMoonSNESRecomp"; [ -x "$EXE" ] || EXE="$EXE.exe"
# HM_ROM=/path/rom.sfc passes the ROM explicitly (otherwise the game uses the
# ROM it remembers from a previous run, rom.cfg).
ROMARG=(); [ -n "${HM_ROM:-}" ] && ROMARG=(--rom "$(cd "$(dirname "$HM_ROM")" && pwd)/$(basename "$HM_ROM")")
run() {  # name script env...
  local name=$1 script=$2; shift 2
  (cd "$GAME" && env "$@" SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SNESRECOMP_NO_LAUNCHER=1 \
     "$EXE" "${ROMARG[@]}" --no-launcher --script "$script" > "$OUT/$name.log" 2>&1) || true
  echo "-- $name"; grep "^\[decomp-check\]  \|MISMATCH" "$OUT/$name.log" || echo "(no report: see $OUT/$name.log)"
}
for s in $(seq 1 "$SEEDS"); do
  run fuzz$s "$ROOT/scripts/demo.txt" HM_DECOMP_FUZZ=1500:$N HM_DECOMP_FUZZ_SEED=$s
done
run live "$ROOT/scripts/newgame.txt" HM_DECOMP_CHECK=1
run nights "$ROOT/scripts/nights.txt" HM_DECOMP_CHECK=1
! grep -q "MISMATCH=[1-9]\|MISMATCH " "$OUT"/*.log
