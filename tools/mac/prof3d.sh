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
# Title -> main menu -> Continue -> save slot. The last E sometimes does not
# register (the save screen is still animating in), so press it again at 60 s:
# in game, A is harmless there. Capture at ~140 s, well past the ~40 s load.
sleep 25; key e; sleep 12; key s; sleep 3; key e; sleep 20; key e
# Three captures: the game fades to black between the load and the first
# cutscene, so one shot can land on a black frame. The last one is "_3d.png".
wid=$(osascript -l JavaScript "$SP/winlist.js" | awk '/Fable 2 Recompiled/{print $1; exit}')
# The intro cutscene ends at a variable time (up to ~170 s since the intro
# videos are skipped); measure only from 180 s, in the street scene.
for t in 180 200 220; do
  while [ $SECONDS -lt $t ]; do sleep 1; done
  screencapture -x -o -l "$wid" "$OUT/${name}_${t}s.png"
done
cp "$OUT/${name}_220s.png" "$OUT/${name}_3d.png"
# TRACE=1: record a 5 s Metal System Trace instead of sampling the CPU.
if [ "${TRACE:-0}" = 1 ]; then
  rm -rf "$OUT/mst_$name.trace"
  xcrun xctrace record --template 'Metal System Trace' --attach $pid --time-limit 5s \
    --output "$OUT/mst_$name.trace" > "$OUT/xctrace_$name.log" 2>&1
else
  sample $pid 10 -file "$OUT/sample_$name.txt" >/dev/null 2>&1
fi
# HOLD=N: keep the game running N more seconds so the log covers +40 s of 3D.
sleep "${HOLD:-0}"
# Capture the end of the window too: the FPS below comes from its last lines.
screencapture -x -o -l "$wid" "$OUT/${name}_end.png"
# FLICKER=N: a burst of N captures, scored by flicker.py (frame-to-frame change).
if [ "${FLICKER:-0}" -gt 0 ]; then
  rm -rf "$OUT/${name}_flk"; mkdir -p "$OUT/${name}_flk"
  for i in $(seq -w 1 "$FLICKER"); do screencapture -x -o -l "$wid" "$OUT/${name}_flk/$i.png"; done
  python3 "$SP/flicker.py" "$OUT/${name}_flk"
fi
kill -INT $pid; sleep 3; kill -9 $pid 2>/dev/null
L=logs/$(ls -t logs | head -1)
echo "fps: $(grep 'guest vblank pacer' $L | tail -15 | sed -E 's/.*guest frame \(gate\) ~([0-9.]+) Hz.*/\1/' | tr '\n' ' ')"
