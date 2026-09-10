#!/bin/bash
# Run the GBA build headlessly: tools/gba/run.sh [-x] <script> [seconds] [outdir]
# -x samples the program counter and symbolizes the hottest addresses.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PROF=""
if [ "${1:-}" = "-x" ]; then PROF="-x"; shift; fi
SCRIPT=${1:-$ROOT/build/gba/test/boot.txt}
SECS=${2:-10}
OUT=${3:-$ROOT/build/gba/test}
mkdir -p "$OUT"
"$ROOT/build/gbarun" $PROF ${PROFFROM:+-y $PROFFROM} -r "$ROOT/build/gba/bod.gba" -s "$SCRIPT" -o "$OUT" -t "$SECS" -S "$OUT/game.sav" > "$OUT/run.log" 2>&1
if [ -n "$PROF" ]; then
  grep '^\[profile\] .*0x' "$OUT/run.log" | while read -r tag pct addr; do
    sym=$(arm-none-eabi-addr2line -f -e "$ROOT/build/gba/bod.elf" "$addr" | head -1)
    echo "$pct $addr $sym"
  done
fi
grep -v '^\[profile\]' "$OUT/run.log" | grep -v 'Bad BIOS Load8\|Bad memory Store8\|Bad memory Load8' | tail -${LINES:-40}
