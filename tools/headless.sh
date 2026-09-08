#!/bin/bash
# Run the game with no window and capture frames.
#   tools/headless.sh <script> [seconds] [outdir]
# Extra configuration comes from the environment (BOD_ARM_ENGINE, etc).
set -u
ROOT=/Users/tonyradtke/dev/bike_or_die
PK=$ROOT/vendor/PumpkinOS
RT=$ROOT/build/testrt
SCRIPT=$(cd "$(dirname "${1:-$ROOT/tools/scripts/boot.txt}")" && pwd)/$(basename "${1:-$ROOT/tools/scripts/boot.txt}")
SECS=${2:-30}
OUT=${3:-$ROOT/build/frames}
case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT";; esac

rm -rf "$OUT"; mkdir -p "$OUT"
if [ ! -d "$RT/vfs" ] || [ "${FRESH:-0}" = "1" ]; then
  rm -rf "$RT"; mkdir -p "$RT"
  cp -R "$PK/vfs" "$RT/vfs"
fi
ln -sfn "$PK/bin" "$RT/bin"; ln -sfn "$PK/script" "$RT/script"
cp -f "$PK/pumpkin" "$RT/pumpkin"

cd "$RT"
DYLD_LIBRARY_PATH="$PK/bin" \
BOD_TEST_OUT="$OUT" BOD_TEST_SCRIPT="$SCRIPT" BOD_TEST_PERIOD="${PERIOD:-500}" \
PUMPKIN_USER="${PUMPKIN_USER:-PalmDB}" PUMPKIN_DISPLAY_LE="${PUMPKIN_DISPLAY_LE:-1}" \
PUMPKIN_SOUND="${PUMPKIN_SOUND:-0}" BOD_RECOMP="${BOD_RECOMP:-1}" \
./pumpkin -d "${DBG:-1}" -f "$RT/pumpkin.log" -s libscriptlua "$PK/script/bodtest.lua" >/dev/null 2>&1 &
PID=$!
sleep "$SECS"
kill $PID 2>/dev/null; wait $PID 2>/dev/null
echo "frames: $(ls "$OUT" | wc -l | tr -d ' ')  log: $RT/pumpkin.log"
grep -ac "panic\|bad read\|bad write" "$RT/pumpkin.log" | sed 's/^/errors: /'
