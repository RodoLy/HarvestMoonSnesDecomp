#!/usr/bin/env bash
# Build the decompilation's test executable.
#
#   tools/build.sh [--rom /path/to/rom.sfc]
#
# The decompilation does not have its own copy of the game: it builds the
# RECOMPILATION project (../HarvestMoonSNESRecomp, or HM_RECOMP=...) with
#   1. its own copy of the generated code (build/gen), produced from your ROM
#      with the recompilation's configuration,
#   2. the HM-Decomp names and the check wrappers applied to that copy
#      (tools/decomp_overrides.py),
#   3. the rewritten routines in src/ compiled in (HM_EXTRA_SOURCES).
# The recompilation project's own files are never modified.
#
# Result: build/game/HarvestMoonSNESRecomp(.exe). It plays exactly like the
# normal game; HM_DECOMP_CHECK=1 / HM_DECOMP_FUZZ=... turn the checks on.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RECOMP="$(cd "${HM_RECOMP:-$ROOT/../HarvestMoonSNESRecomp}" && pwd)"
PYTHON=${PYTHON:-python3}
ROM=""
EXTRA_CMAKE=()
while [ $# -gt 0 ]; do
  case "$1" in
    --rom) ROM=$2; shift 2 ;;
    -D*) EXTRA_CMAKE+=("$1"); shift ;;
    *) echo "build.sh: unknown argument $1" >&2; exit 2 ;;
  esac
done
CLI="$RECOMP/snesrecomp/snesrecomp_cli.py"
B="$ROOT/build"
mkdir -p "$B"
export RUSTUP_TOOLCHAIN=${RUSTUP_TOOLCHAIN:-stable}

RESOLVE=(resolve-rom --project-root "$RECOMP" --name rom.sfc --name harvestmoon.sfc --name harvestmoon.smc)
[ -n "$ROM" ] && RESOLVE+=(--rom "$ROM")
ROM=$("$PYTHON" "$CLI" "${RESOLVE[@]}"); ROM=${ROM%$'\r'}
IDENT="$RECOMP/snesrecomp/tools/rom_identity.py"
CRC=$("$PYTHON" "$IDENT" "$RECOMP/rom_identity.txt" --get expected_crc32)
SHA=$("$PYTHON" "$IDENT" "$RECOMP/rom_identity.txt" --get expected_sha256)

echo "== 1/3 Generating code from $ROM (recompilation config) =="
GEN_ARGS=(--rom "$ROM" --cfg-dir "$RECOMP/recomp" --out-dir "$B/gen" --funcs-h "$B/funcs.h"
          --project-root "$RECOMP" --expected-crc32 "$CRC" --expected-sha256 "$SHA")
for p in "$RECOMP"/recomp/profiles/*.json "$RECOMP"/recomp/profiles/*.jsonl; do
  [ -f "$p" ] && GEN_ARGS+=(--profile-manifest "$p")
done
rm -f "$B/gen/.snesrecomp-cache.json"
"$PYTHON" "$CLI" generate "${GEN_ARGS[@]}" | grep -E "analysis:|v2_emit: [0-9]+ roots|postprocess|apply_overrides|error" || true

echo "== 2/3 Names and check wrappers =="
"$PYTHON" "$ROOT/tools/decomp_overrides.py" --gen-dir "$B/gen" \
  --names "$ROOT/data/decomp_names.tsv" --decomp "$ROOT/src/hm_decomp.c"

echo "== 3/3 Building the test executable =="
SOURCES=$(ls "$ROOT"/src/*.c | tr '\n' ';')
cmake -S "$RECOMP" -B "$B/game" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DHM_GEN_DIR="$B/gen" -DHM_EXTRA_SOURCES="$SOURCES" \
  -DHM_EXTRA_INCLUDE_DIRS="$RECOMP/src;$ROOT/src" "${EXTRA_CMAKE[@]}" >/dev/null
cmake --build "$B/game" | tail -1
echo "Done: $B/game"
