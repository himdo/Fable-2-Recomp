#!/usr/bin/env bash
# Fable II (ReXGlue recompilation), Linux launcher for the packaged build.
#
# Put default.xex and the data/ folder from your own Fable II GOTY disc next
# to this script, then run it (or add it to Steam as a non-Steam game, with no
# Proton/compatibility tool). Saves, settings, logs and the shader cache are
# kept in this folder. Extra arguments are passed to the game.
set -uo pipefail
HERE="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
cd "$HERE" || exit 1

# Errors must be visible when started from Steam or a file manager.
fail() {
    echo "Error: $1" >&2
    if [ ! -t 2 ]; then
        if command -v zenity >/dev/null; then
            zenity --error --title="Fable II" --no-markup --width=460 --text="$1"
        elif command -v kdialog >/dev/null; then
            kdialog --title "Fable II" --error "$1"
        elif command -v notify-send >/dev/null; then
            notify-send "Fable II" "$1"
        fi
    fi
    exit 1
}

[ -x "$HERE/fable_2" ] || fail "fable_2 is missing or not executable in $HERE. Extract the whole package again."

# The build needs glibc 2.38 or newer (SteamOS 3.6+, Ubuntu 24.04+, ...).
NEED_GLIBC="@FABLE2_NEED_GLIBC@"
HAVE_GLIBC="$(ldd --version 2>/dev/null | head -1 | grep -oE '[0-9]+\.[0-9]+$')"
if [ -n "$HAVE_GLIBC" ] &&
   [ "$(printf '%s\n%s\n' "$NEED_GLIBC" "$HAVE_GLIBC" | sort -V | head -1)" != "$NEED_GLIBC" ]; then
    fail "This system has glibc $HAVE_GLIBC, but this build needs $NEED_GLIBC or newer. Update the system (on a Steam Deck: SteamOS 3.6 or newer)."
fi

[ -f "$HERE/default.xex" ] || fail "default.xex not found. Copy default.xex and the data folder from your Fable II disc into $HERE."
[ -d "$HERE/data" ] && [ ! -L "$HERE/data" ] || fail "The data folder was not found (or is a link). Copy the data folder from your Fable II disc into $HERE."

# Recomp helper scripts (F5 Lua etc.) live in the game's data folder.
if [ -d "$HERE/extras/scripts/recomp" ]; then
    mkdir -p "$HERE/data/scripts/recomp"
    cp -n "$HERE"/extras/scripts/recomp/* "$HERE/data/scripts/recomp/" 2>/dev/null
fi

# Optional extra settings for testing (environment variables such as
# RADV_DEBUG, or a stderr redirect), one shell line each. Not part of the
# package, so it survives installing a new one; delete it when done.
if [ -f "$HERE/launch_env" ]; then
    . "$HERE/launch_env"
fi

./fable_2 "$@"
status=$?
if [ "$status" != 0 ] && [ "$status" != 130 ]; then
    log="$(ls -t "$HERE"/logs/*.log 2>/dev/null | head -1)"
    fail "The game exited with status $status.${log:+ Log: $log}"
fi
exit 0
