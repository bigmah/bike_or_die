#!/bin/bash
# run.sh [-o out.png] [-w wait] [-s script] [-k actions] [-t hold] [-K keep]
# Boots PumpkinOS, optionally drives it, screenshots just its window.
# actions: comma separated
#   m:X/Y   click at window-relative point
#   t:TEXT  type text
#   c:N[/down|/up]  key by macOS keycode (tap by default)
#   d:S     delay S seconds
#   p:FILE  take an intermediate screenshot
set -u
ROOT=/Users/tonyradtke/dev/bike_or_die
MI=$ROOT/tools/macinput
cd $ROOT/vendor/PumpkinOS
OUT=$ROOT/build/shot.png; WAIT=6; SCRIPT=./script/bod.lua; KEYS=""; HOLD=0; KEEP=0
while getopts "o:w:s:k:t:K" opt; do case $opt in
  o) OUT=$OPTARG;; w) WAIT=$OPTARG;; s) SCRIPT=$OPTARG;; k) KEYS=$OPTARG;; t) HOLD=$OPTARG;; K) KEEP=1;;
esac; done
rm -f pumpkin.log
DYLD_LIBRARY_PATH=./bin ./pumpkin -d 1 -f pumpkin.log -s libscriptlua "$SCRIPT" >/dev/null 2>&1 &
PID=$!
sleep "$WAIT"
bounds() { osascript -e "tell application \"System Events\" to tell (first process whose unix id is $PID) to get {position, size} of window 1" 2>/dev/null | tr -d ' '; }
# Put the window somewhere unobstructed and make sure it really is frontmost --
# a click into a background window is swallowed by macOS.
for try in 1 2 3 4 5; do
  osascript -e "tell application \"System Events\" to tell (first process whose unix id is $PID) to set position of window 1 to {80, 80}" 2>/dev/null
  osascript -e "tell application \"System Events\" to tell (first process whose unix id is $PID) to perform action \"AXRaise\" of window 1" 2>/dev/null
  osascript -e "tell application \"System Events\" to set frontmost of (first process whose unix id is $PID) to true" 2>/dev/null
  sleep 0.5
  FM=$(osascript -e "tell application \"System Events\" to get frontmost of (first process whose unix id is $PID)" 2>/dev/null)
  [ "$FM" = "true" ] && break
done
WID=$($MI winid $PID 2>/dev/null)
B=$(bounds); WX=$(echo $B|cut -d, -f1); WY=$(echo $B|cut -d, -f2)
B=$(bounds); WX=$(echo $B|cut -d, -f1); WY=$(echo $B|cut -d, -f2)
shot() { local f=$1
  local b=$(bounds)
  if [ -n "$b" ]; then screencapture -x -o -R"$(echo $b|cut -d, -f1),$(echo $b|cut -d, -f2),$(echo $b|cut -d, -f3),$(echo $b|cut -d, -f4)" "$f"
  else screencapture -x -o "$f"; fi; }
if [ -n "$KEYS" ]; then
  IFS=',' read -ra A <<< "$KEYS"
  for k in "${A[@]}"; do
    case "$k" in
      d:*) sleep "${k#d:}";;
      p:*) shot "${k#p:}";;
      t:*) $MI type "${k#t:}";;
      m:*) V="${k#m:}"; $MI click $((WX+$(echo $V|cut -d/ -f1))) $((WY+$(echo $V|cut -d/ -f2)));;
      c:*) V="${k#c:}"; KC=$(echo $V|cut -d/ -f1); ST=$(echo $V|cut -d/ -f2 -s); $MI key "$KC" "${ST:-tap}";;
    esac
  done
  sleep 0.8
fi
shot "$OUT"
[ "$HOLD" != "0" ] && sleep "$HOLD"
if [ "$KEEP" = "1" ]; then echo "PID=$PID (left running)"; else kill $PID 2>/dev/null; wait $PID 2>/dev/null; fi
echo "captured $OUT (win $B)"
