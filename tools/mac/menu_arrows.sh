#!/usr/bin/env bash
# Does the main menu cursor move with the Down arrow (T6)? Title -> menu with E at
# 25 s, hold Down (key code 125) 0.4 s at 37 s, capture at 40 s; then S, capture
# at 44 s. Compare <name>_before.png (36 s), <name>_arrow.png and <name>_s.png.
name=${1:-menu_arrows}; shift
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
SP="$(cd "$(dirname "$0")" && pwd)"
OUT="${OUT:-$REPO/out/mac-measure}"; mkdir -p "$OUT"
cd "$REPO/out/build/mac-arm64-release"
./fable_2 --game_data_root "$REPO" --fullscreen=false "$@" > "$OUT/run_$name.log" 2>&1 &
pid=$!
front="tell application \"System Events\" to set frontmost of (first process whose unix id is $pid) to true"
key() { osascript -e "$front" -e "tell application \"System Events\" to key down \"$1\"" -e 'delay 0.25' \
  -e "tell application \"System Events\" to key up \"$1\"" >/dev/null; }
at() { while [ $SECONDS -lt $1 ]; do sleep 0.2; done; }
cap() { wid=$(osascript -l JavaScript "$SP/winlist.js" | awk '/Fable 2 Recompiled/{print $1; exit}')
  screencapture -x -o -l "$wid" "$OUT/${name}_$1.png"; }
at 25; key e
at 36; cap before
at 37; osascript -e "$front" -e 'tell application "System Events" to key down (key code 125)' -e 'delay 0.4' \
  -e 'tell application "System Events" to key up (key code 125)' >/dev/null
at 40; cap arrow
at 41; key s
at 44; cap s
kill -INT $pid; sleep 3; kill -9 $pid 2>/dev/null
