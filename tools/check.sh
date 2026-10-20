#!/usr/bin/env bash
# Run the decompilation checks headlessly.
#
#   tools/check.sh [iterations]
#
# 1. Fuzz: at frame 1500 of the attract demo, every rewritten routine is
#    called <iterations> times (default 2000) with random inputs; each call
#    is compared with the original byte for byte, then the machine is restored.
# 2. Live: a new game is played by script with every real call checked.
# Exits non-zero on any mismatch.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GAME="$ROOT/build/game"
N=${1:-2000}
OUT="$ROOT/build/check"; rm -rf "$OUT"; mkdir -p "$OUT"
EXE="$GAME/HarvestMoonSNESRecomp"; [ -x "$EXE" ] || EXE="$EXE.exe"
run() {  # name script env...
  local name=$1 script=$2; shift 2
  (cd "$GAME" && env "$@" SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SNESRECOMP_NO_LAUNCHER=1 \
     "$EXE" --script "$script" > "$OUT/$name.log" 2>&1) || true
  echo "-- $name"; grep "^\[decomp-check\]  \|MISMATCH" "$OUT/$name.log" || echo "(no report: see $OUT/$name.log)"
}
run fuzz "$ROOT/scripts/demo.txt" HM_DECOMP_FUZZ=1500:$N
run live "$ROOT/scripts/newgame.txt" HM_DECOMP_CHECK=1
! grep -q "MISMATCH=[1-9]\|MISMATCH " "$OUT"/*.log
