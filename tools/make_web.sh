#!/bin/bash
# Build the WebAssembly version and stage it as a directory that can be served
# as-is. The counterpart of tools/make_app.sh.
#
#   tools/make_web.sh [outdir]
#
# Serve it with tools/webserver.py, which sets the two headers SharedArrayBuffer
# needs; on a host that will not set them, coi-serviceworker.js does it instead.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PK=$ROOT/vendor/PumpkinOS
EM=$PK/src/emscripten
OUT=${1:-$ROOT/build/web}

command -v emcc >/dev/null || { echo "emcc not found in PATH" >&2; exit 1; }

echo "==> recompiled game code (wasm)"
make -C "$ROOT/src" WASM=1 -j"${JOBS:-6}"

echo "==> PumpkinOS (wasm)"
make -C "$PK/src" OSNAME=Emscripten TYPE=release BITS=32 JOBS="${JOBS:-8}"

echo "==> staging $OUT"
rm -rf "$OUT"; mkdir -p "$OUT"
cp "$EM/pumpkin.js" "$EM/pumpkin.wasm" "$EM/pumpkin.data" "$EM/coi-serviceworker.js" "$OUT/"
# pumpkin.html is the template; serve it as the directory index too.
cp "$EM/pumpkin.html" "$OUT/index.html"
cp "$EM/pumpkin.html" "$OUT/pumpkin.html"

du -ch "$OUT"/* | tail -1
echo "built: $OUT"
echo "run:   tools/webserver.py 8080 $OUT"
