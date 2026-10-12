#!/usr/bin/env bash
# Launch, navigate into the 3D scene, then sample 10 s. Args: name [game args]
name=$1; shift
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
SP="$(cd "$(dirname "$0")" && pwd)"
OUT="${OUT:-$REPO/out/mac-measure}"; mkdir -p "$OUT"
BUILD="$REPO/out/build/mac-arm64-release"
cd "$BUILD"
MVK_CONFIG_PERFORMANCE_TRACKING=1 MVK_CONFIG_PERFORMANCE_LOGGING_FRAME_COUNT=60 MVK_CONFIG_ACTIVITY_PERFORMANCE_LOGGING_STYLE=0 \
  ./fable_2 --game_data_root "$REPO" --fullscreen=false "$@" > "$OUT/run_$name.log" 2>&1 &
pid=$!
key() { osascript -e "tell application \"System Events\" to set frontmost of (first process whose unix id is $pid) to true" \
  -e "tell application \"System Events\" to key down \"$1\"" -e 'delay 0.25' -e "tell application \"System Events\" to key up \"$1\"" >/dev/null; }
sleep 25; key e; sleep 12; key s; sleep 3; key e
sleep 105
wid=$(osascript -l JavaScript "$SP/winlist.js" | awk '/Fable 2 Recompiled/{print $1; exit}')
screencapture -x -o -l "$wid" "$OUT/${name}_3d.png"
sample $pid 10 -file "$OUT/sample_$name.txt" >/dev/null 2>&1
kill -INT $pid; sleep 3; kill -9 $pid 2>/dev/null
L=logs/$(ls -t logs | head -1)
echo "fps: $(grep 'guest vblank pacer' $L | tail -15 | sed -E 's/.*guest frame \(gate\) ~([0-9.]+) Hz.*/\1/' | tr '\n' ' ')"
