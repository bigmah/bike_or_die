#!/bin/bash
# Build and run the offscreen test of the macOS window's sheets -- the pause
# menu, the game's dialogs, the level pack sheet -- against a simulated game.
# Nothing is shown on a screen; see tools/src/macsheets.m.
#
#   tools/macsheets.sh [outdir]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
L=$ROOT/vendor/PumpkinOS/src/liblsdl2
OUT=${1:-$ROOT/build/macsheets}
SDL=$(sdl2-config --prefix)

mkdir -p "$OUT"
clang -fobjc-arc -Wall -Wno-unused -I"$L" -I"$SDL/include" -o "$OUT/macsheets" \
  "$ROOT/tools/src/macsheets.m" "$L/liblsdl2_mac.m" "$L/liblsdl2_mac_form.m" \
  "$L/liblsdl2_mac_pause.m" "$L/liblsdl2_mac_keys.m" \
  -L"$SDL/lib" -lSDL2 -framework Cocoa -Wl,-export_dynamic
"$OUT/macsheets" "$OUT" 2>"$OUT/log.txt"
