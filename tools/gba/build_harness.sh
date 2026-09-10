#!/bin/bash
# Build tools/gba/gbarun against the libmgba in build/mgba (see build/mgba/build-lib).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
M=$ROOT/build/mgba
cc -O2 -DUSE_PNG -o "$ROOT/build/gbarun" "$ROOT/tools/gba/gbarun.c" \
  -I"$M/include" -I"$M/build-lib/include" -I/opt/homebrew/include \
  "$M/build-lib/libmgba.a" -L/opt/homebrew/lib -lpng -lz -lm -lpthread -framework CoreFoundation -framework OpenGL
echo "built $ROOT/build/gbarun"
