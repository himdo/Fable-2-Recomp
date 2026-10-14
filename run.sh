#!/usr/bin/env bash
# Launch the Linux build of Fable 2 (build it first with ./build.sh).
#
# Usage:
#   ./run.sh [game args...]    run the Release build
#   ./run.sh -debug [args...]  run the Debug build instead
#   ./run.sh --make-shortcut   write "Fable II.desktop" in the project root, a
#                              double-clickable launcher that can be copied to
#                              the desktop or ~/.local/share/applications
set -uo pipefail
ROOT="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"

# Errors must be visible when started from a file manager (no terminal).
fail() {
    echo "Error: $1" >&2
    if [ ! -t 2 ]; then
        if command -v zenity >/dev/null; then
            zenity --error --title="Fable II" --no-markup --width=420 --text="$1"
        elif command -v notify-send >/dev/null; then
            notify-send "Fable II" "$1"
        fi
    fi
    exit 1
}

if [ "${1:-}" = "--make-shortcut" ]; then
    SHORTCUT="$ROOT/Fable II.desktop"
    cat > "$SHORTCUT" <<EOF
[Desktop Entry]
Type=Application
Name=Fable II
Comment=Fable II (ReXGlue recompilation)
Exec="$ROOT/run.sh"
Path=$ROOT
Icon=$ROOT/assets/app-icon.png
Terminal=false
Categories=Game;
EOF
    chmod +x "$SHORTCUT"
    # GNOME-based file managers only launch .desktop files marked trusted.
    gio set "$SHORTCUT" metadata::trusted true 2>/dev/null || true
    echo "Wrote $SHORTCUT"
    exit 0
fi

PRESET=linux-amd64-release
if [ "${1:-}" = "-debug" ] || [ "${1:-}" = "-d" ]; then
    PRESET=linux-amd64-debug
    shift
fi
BUILD="$ROOT/out/build/$PRESET"

[ -x "$BUILD/fable_2" ] || fail "No build found at $BUILD/fable_2. Run ./build.sh first."
[ -f "$ROOT/default.xex" ] || fail "default.xex not found in $ROOT (see Linux.md, \"Add the game content\")."

cd "$BUILD"
./fable_2 --game_data_root="$ROOT" "$@"
status=$?
if [ "$status" != 0 ]; then
    log="$(ls -t "$BUILD"/logs/*.log 2>/dev/null | head -1)"
    fail "The game exited with status $status.${log:+ Log: $log}"
fi
