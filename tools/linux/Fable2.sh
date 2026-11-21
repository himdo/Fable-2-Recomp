#!/usr/bin/env bash
# Fable II (ReXGlue recompilation), Linux launcher for the packaged build.
#
# Run it (or add it to Steam as a non-Steam game, with no Proton/compatibility
# tool). On the first start, when default.xex and the data/ folder are not
# here yet, it finds your Fable II GOTY disc image (or asks for it) and
# extracts them with extras/x360extract.py; copying them in by hand works too.
# Saves, settings, logs and the shader cache are kept in this folder. Extra
# arguments are passed to the game.
set -uo pipefail
HERE="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
cd "$HERE" || exit 1

# Errors must be visible when started from Steam or a file manager.
# Messages may contain \n for line breaks.
fail() {
    local m
    m="$(printf '%b' "$1")"
    echo "Error: $m" >&2
    if [ ! -t 2 ]; then
        if command -v zenity >/dev/null; then
            zenity --error --title="Fable II" --no-markup --width=460 --text="$m"
        elif command -v kdialog >/dev/null; then
            kdialog --title "Fable II" --error "$m"
        elif command -v notify-send >/dev/null; then
            notify-send "Fable II" "$m"
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

[ -L "$HERE/data" ] && fail "The data folder in $HERE is a link. The game cannot read its files through a link: replace it with the real folder."

# --- First start: extract the game files from the user's disc image ---------
# default.xex and data/ come from the user's own Fable II disc. When they are
# missing, look for the disc image (.iso) in the usual places, or let the user
# pick it, and extract them with extras/x360extract.py (needs only python3).
EXTRACT="$HERE/extras/x360extract.py"
VERSIONS="$HERE/extras/game_versions.json"
TITLE="Fable II"
if [ -t 0 ] && [ -t 1 ]; then UI=tty
elif [ -z "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]; then UI=none
elif command -v zenity >/dev/null; then UI=zenity
elif command -v kdialog >/dev/null; then UI=kdialog
else UI=none
fi

# ask "text" ["yes label" "no label"]: 0 = yes
ask() {
    case "$UI" in
        tty) local a; printf '\n%b\n%s? [y/N] ' "$1" "${2:-Continue}"; read -r a; [[ "$a" == [yYjJ]* ]] ;;
        zenity) zenity --question --title="$TITLE" --no-markup --width=480 \
                    --ok-label="${2:-Yes}" --cancel-label="${3:-No}" --text="$(printf '%b' "$1")" ;;
        kdialog) kdialog --title "$TITLE" --yes-label "${2:-Yes}" --no-label "${3:-No}" \
                    --yesno "$(printf '%b' "$1")" ;;
        *) return 1 ;;
    esac
}
info() {
    case "$UI" in
        tty) printf '\n%b\n' "$1" ;;
        zenity) zenity --info --title="$TITLE" --no-markup --width=480 --text="$(printf '%b' "$1")" ;;
        kdialog) kdialog --title "$TITLE" --msgbox "$(printf '%b' "$1")" ;;
    esac
}
pick_iso() {
    case "$UI" in
        # The prompt goes to stderr: stdout is the answer ($(pick_iso)).
        tty) local p; printf '\nPath to your Fable II disc image (.iso), or empty to cancel:\n> ' >&2
             read -r p || return 1
             p="${p/#\~/$HOME}"; p="${p%\"}"; p="${p#\"}"; p="${p%\'}"; p="${p#\'}"; printf '%s' "$p" ;;
        zenity) zenity --file-selection --title="Select your Fable II disc image (.iso)" \
                    --filename="$HOME/" --file-filter="Disc images (*.iso) | *.iso *.ISO" \
                    --file-filter="All files | *" ;;
        kdialog) kdialog --title "Select your Fable II disc image (.iso)" \
                    --getopenfilename "$HOME" "*.iso *.ISO|Disc images (*.iso)" ;;
    esac
}

# Disc images that are a supported Fable II, in the usual places (the game
# folder and the one above it, Downloads, Desktop, Games, home, SD cards).
find_isos() {
    local d
    {
        for d in "$HERE" "$(dirname "$HERE")" "$HOME/Downloads" "$HOME/Desktop" "$HOME/Games" "$HOME"; do
            [ -d "$d" ] && find "$d" -maxdepth 2 -type f -iname '*.iso' -size +6G 2>/dev/null
        done
        for d in /run/media/*/ /run/media/*/*/ /media/*/ /mnt/*/; do
            [ -d "$d" ] && find "$d" -maxdepth 2 -type f -iname '*.iso' -size +6G 2>/dev/null
        done
    } | while read -r f; do readlink -f "$f"; done | awk '!seen[$0]++' | while read -r f; do
        python3 "$EXTRACT" probe "$f" --versions "$VERSIONS" >/dev/null 2>&1 && printf '%s\n' "$f"
    done
}

# Extracts the missing game files from $1 into $HERE, with a progress window.
extract_from() {
    local iso="$1" errlog status
    errlog="$(mktemp)"
    case "$UI" in
        zenity)
            ( python3 "$EXTRACT" extract "$iso" "$HERE" --versions "$VERSIONS" --force \
                  --missing-only --zenity 2>"$errlog"; echo $? >"$errlog.status" ) |
                zenity --progress --title="$TITLE" --width=520 --auto-close \
                    --text="Extracting the game files from your disc image..." --percentage=0
            ;;
        kdialog)
            kdialog --title "$TITLE" --passivepopup "Extracting the game files from your disc image. This takes a few minutes; the game starts when it is done." 15 &
            python3 "$EXTRACT" extract "$iso" "$HERE" --versions "$VERSIONS" --force \
                --missing-only 2>"$errlog"; echo $? >"$errlog.status"
            ;;
        *)
            python3 "$EXTRACT" extract "$iso" "$HERE" --versions "$VERSIONS" --force \
                --missing-only 2> >(tee "$errlog" >&2); echo $? >"$errlog.status"
            ;;
    esac
    status="$(cat "$errlog.status" 2>/dev/null || echo 1)"
    if [ "$status" != 0 ]; then
        local msg
        msg="$(grep '^x360extract:' "$errlog" | tail -1 | sed 's/^x360extract: //')"
        rm -f "$errlog" "$errlog.status"
        [ -n "$msg" ] || msg="The extraction was cancelled or stopped."
        fail "The game files could not be extracted.\n\n$msg"
    fi
    rm -f "$errlog" "$errlog.status"
}

install_game_files() {
    [ "$UI" = none ] && fail "default.xex and the data folder are missing. Copy them from your Fable II disc into $HERE, or run Fable2.sh from a terminal to extract them from your disc image."
    command -v python3 >/dev/null || fail "default.xex and the data folder are missing, and python3 (needed to extract them from your disc image) is not installed. Copy default.xex and the data folder from your Fable II disc into $HERE."
    [ -f "$EXTRACT" ] || fail "extras/x360extract.py is missing. Extract the whole package again."

    local iso="" found
    found="$(find_isos)"
    if [ -n "$found" ]; then
        local first n
        first="$(printf '%s\n' "$found" | head -1)"
        n="$(printf '%s\n' "$found" | wc -l)"
        if [ "$n" -gt 1 ] && [ "$UI" = zenity ]; then
            iso="$(printf '%s\n' "$found" | zenity --list --title="$TITLE" --width=640 --height=300 \
                --text="Fable II's game files aren't installed yet. Pick the disc image to extract them from:" \
                --column="Disc image")" || exit 0
        elif ask "Fable II's game files aren't installed yet.\n\nFound your disc image:\n$first\n\nExtract the game files from it now? This copies about 6.7 GB into\n$HERE\nand takes a few minutes." "Extract" "Choose another file"; then
            iso="$first"
        fi
    else
        info "Fable II's game files aren't installed yet.\n\nNext, select the disc image (.iso) of your Fable II Game of the Year disc. The game files (about 6.7 GB) are extracted from it into\n$HERE\nThe disc image itself is not changed."
    fi

    while [ -z "$iso" ]; do
        iso="$(pick_iso)" || exit 0
        [ -n "$iso" ] || exit 0
        local probe status
        probe="$(python3 "$EXTRACT" probe "$iso" --versions "$VERSIONS" 2>&1)"; status=$?
        case "$status" in
            0) ;;
            3) local hash="${probe#* }"; hash="${hash%% *}"
               if ! ask "This disc image is not a Fable II version this build supports:\n$(printf '%s' "$probe" | sed 's/^unsupported [0-9a-f]* /Known version: /; s/^unknown [0-9a-f]*/Unknown default.xex/')\n(SHA-256 $hash)\n\nThe supported discs are the Fable II Game of the Year editions (see Linux.md). Extract it anyway?" "Extract anyway" "Choose another file"; then
                   iso=""
               fi ;;
            *) info "That file can't be used:\n${probe#x360extract: }\n\nSelect a complete Fable II disc image (.iso)."; iso="" ;;
        esac
    done

    extract_from "$iso"

    # Offer a Steam library shortcut (SteamOS), when Steam is running and the
    # game was not started from Steam already. steamos-add-to-steam hands
    # Steam a steam:// link, so it runs in the background; the shortcut is
    # added through a link named "Fable II" so the library shows that name.
    if command -v steamos-add-to-steam >/dev/null && [ "$UI" != tty ] &&
       [ -z "${SteamGameId:-}${SteamAppId:-}" ] && pgrep -x steam >/dev/null; then
        if ask "The game files are installed.\n\nAdd Fable II to your Steam library, so you can start it in Gaming Mode?" "Add to Steam" "Not now"; then
            ln -sfn Fable2.sh "$HERE/Fable II"
            (steamos-add-to-steam "$HERE/Fable II" >/dev/null 2>&1 &)
        fi
    fi
}

if [ ! -f "$HERE/default.xex" ] || [ ! -d "$HERE/data" ]; then
    install_game_files
fi
[ -f "$HERE/default.xex" ] || fail "default.xex not found. Copy default.xex and the data folder from your Fable II disc into $HERE."
[ -d "$HERE/data" ] || fail "The data folder was not found. Copy the data folder from your Fable II disc into $HERE."

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
