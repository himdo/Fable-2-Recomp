#!/usr/bin/env bash
# One GPU frame capture (.gputrace, open in Xcode) of the 3D scene. Args: name [game args]
# MoltenVK on-demand scope (3): each byte written to /tmp/MoltenVKCapturePipe-<random>
# captures the next frame. SCOPE=2 captures the first frame instead (sanity check).
name=$1; shift
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
SP="$(cd "$(dirname "$0")" && pwd)"
OUT="${OUT:-$REPO/out/mac-measure}"; mkdir -p "$OUT"
BUILD="$REPO/out/build/mac-arm64-release"
[ -z "$(pgrep -x fable_2)" ] || { echo "fable_2 already running" >&2; exit 1; }
rm -rf "$OUT/$name.gputrace" /tmp/MoltenVKCapturePipe-*
cd "$BUILD"
MTL_CAPTURE_ENABLED=1 MVK_CONFIG_AUTO_GPU_CAPTURE_SCOPE=${SCOPE:-3} \
  MVK_CONFIG_AUTO_GPU_CAPTURE_OUTPUT_FILE="$OUT/$name.gputrace" \
  ./fable_2 --game_data_root "$REPO" --fullscreen=false "$@" > "$OUT/run_$name.log" 2>&1 &
pid=$!
key() { osascript -e "tell application \"System Events\" to set frontmost of (first process whose unix id is $pid) to true" \
  -e "tell application \"System Events\" to key down \"$1\"" -e 'delay 0.25' -e "tell application \"System Events\" to key up \"$1\"" >/dev/null; }
sleep 25; key e; sleep 12; key s; sleep 3; key e; sleep 20; key e
wid=$(osascript -l JavaScript "$SP/winlist.js" | awk '/Fable 2 Recompiled/{print $1; exit}')
# Shots at 130/150 s are references; the trigger fires at TRIG (default 152 s, after the 150 s shot).
while [ $SECONDS -lt 130 ]; do sleep 1; done
screencapture -x -o -l "$wid" "$OUT/${name}_130s.png"
while [ $SECONDS -lt 150 ]; do sleep 1; done
screencapture -x -o -l "$wid" "$OUT/${name}_150s.png"
while [ $SECONDS -lt ${TRIG:-152} ]; do sleep 1; done
# Trigger once: a second byte would restart the capture onto the same (existing) file and fail.
if [ "${SCOPE:-3}" = 3 ]; then
  for p in /tmp/MoltenVKCapturePipe-*; do [ -p "$p" ] && echo 1 > "$p"; done
fi
sleep 8
screencapture -x -o -l "$wid" "$OUT/${name}_post.png"
sleep 3
kill -INT $pid; sleep 3; kill -9 $pid 2>/dev/null
grep -i "mvk-.*capture\|capture" "$OUT/run_$name.log" | head
du -sh "$OUT/$name.gputrace" 2>&1
