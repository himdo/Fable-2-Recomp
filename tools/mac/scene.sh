#!/usr/bin/env bash
# Usage: scene.sh <name> "<schedule>" [game args...]
# schedule: space-separated steps, each "t:action" with action = e (press E,
# the guest A button), d (Down arrow = d-pad down), s (S = left stick down) f (F3, FPS overlay) or c (capture the game window). Times are seconds since launch.
set -u
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
SP="$(cd "$(dirname "$0")" && pwd)"
OUT="${OUT:-$REPO/out/mac-measure}"; mkdir -p "$OUT"
BUILD="$REPO/out/build/mac-arm64-release"
name=$1; schedule=$2; shift 2
cd "$BUILD"
./fable_2 --game_data_root "$REPO" \
  --vulkan_log_debug_messages=false "$@" > "$OUT/run_$name.log" 2>&1 &
pid=$!
start=$(date +%s)
wid=""
for step in $schedule; do
  t=${step%%:*}; action=${step##*:}
  while [ $(( $(date +%s) - start )) -lt "$t" ]; do
    kill -0 $pid 2>/dev/null || { echo "$name: game died at $(( $(date +%s) - start ))s"; tail -3 "$OUT/run_$name.log"; exit 1; }
    sleep 0.5
  done
  [ -z "$wid" ] && wid=$(osascript -l JavaScript "$SP/winlist.js" | awk '/Fable 2 Recompiled/{print $1; exit}')
  case $action in
    e) osascript -e "tell application \"System Events\" to set frontmost of (first process whose unix id is $pid) to true" \
                 -e 'tell application "System Events" to key down "e"' -e 'delay 0.25' \
                 -e 'tell application "System Events" to key up "e"' >/dev/null ;;
    d) osascript -e "tell application \"System Events\" to set frontmost of (first process whose unix id is $pid) to true" \
                 -e 'tell application "System Events" to key down (key code 125)' -e 'delay 0.25' \
                 -e 'tell application "System Events" to key up (key code 125)' >/dev/null ;;
    s) osascript -e "tell application \"System Events\" to set frontmost of (first process whose unix id is $pid) to true" \
                 -e 'tell application "System Events" to key down "s"' -e 'delay 0.25' \
                 -e 'tell application "System Events" to key up "s"' >/dev/null ;;
    f) osascript -e "tell application \"System Events\" to set frontmost of (first process whose unix id is $pid) to true" \
                 -e 'tell application "System Events" to key code 99' >/dev/null ;;
    c) screencapture -x -o -l "$wid" "$OUT/${name}_${t}s.png" 2>/dev/null ;;
  esac
done
kill -INT $pid 2>/dev/null; sleep 3; kill -9 $pid 2>/dev/null
echo "$name: done"
