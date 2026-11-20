#!/usr/bin/env bash
# Assemble a portable Linux package from a finished Release build:
#   out/dist/Fable2-Linux-<cpu>/        (runnable folder)
#   out/dist/Fable2-Linux-<cpu>.tar.gz  (the same, with permissions kept)
#
# Usage: tools/package_linux.sh <build dir> <cpu level>
# Called by ./build.sh package. The package holds no game content: the user
# copies default.xex and data/ next to Fable2.sh.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${1:?build dir}"
CPU="${2:-generic}"
NAME="Fable2-Linux-$CPU"
DIST="$ROOT/out/dist"
PKG="$DIST/$NAME"

require() { [ -e "$1" ] || { echo "package_linux: missing $1" >&2; exit 1; }; }
for f in fable_2 librexruntime.so librexgpu-xenos.so fable2_build.json; do
    require "$BUILD/$f"
done
require "$ROOT/config/fable2_config.toml"
require "$ROOT/tools/linux/Fable2.sh"

# The newest glibc symbol version any shipped binary needs, for the launcher's
# start-up check.
NEED_GLIBC="$(for f in fable_2 librexruntime.so librexgpu-xenos.so; do
    objdump -T "$BUILD/$f"; done | grep -oE 'GLIBC_[0-9]+\.[0-9]+' | sort -uV | tail -1)"
NEED_GLIBC="${NEED_GLIBC#GLIBC_}"

rm -rf "$PKG" "$DIST/$NAME.tar.gz"
mkdir -p "$PKG/extras/scripts/recomp" "$PKG/licenses"
cp "$BUILD/fable_2" "$BUILD/librexruntime.so" "$BUILD/librexgpu-xenos.so" \
   "$BUILD/fable2_build.json" "$PKG/"
# Pristine defaults, not the build tree's copy (a test run may have edited it).
cp "$ROOT/config/fable2_config.toml" "$PKG/"
# GPU frame sync stays at its Linux default (off) unless a user sets it.
sed -i 's/^gpu_frame_sync = true$/# gpu_frame_sync = true/' "$PKG/fable2_config.toml"
cp "$ROOT/README.md" "$ROOT/Linux.md" "$PKG/"
cp "$ROOT/thirdparty/XenosTiling/LICENSE.txt" "$PKG/licenses/XenosTiling-LICENSE.txt"
cp "$ROOT/assets/app-icon.png" "$PKG/" 2>/dev/null || true
# The keycap font and the Steam Deck button glyphs (src/gui/keycap_render.cpp,
# src/gui/deck_prompts.h) are loaded from assets/ next to the executable.
mkdir -p "$PKG/assets/fonts"
cp -r "$ROOT/assets/fonts/Google Sans" "$PKG/assets/fonts/"
cp -r "$ROOT/assets/deck_prompts" "$PKG/assets/"
cp "$ROOT"/src/lua/*.lua "$PKG/extras/scripts/recomp/"
sed "s/@FABLE2_NEED_GLIBC@/$NEED_GLIBC/" "$ROOT/tools/linux/Fable2.sh" > "$PKG/Fable2.sh"
chmod 755 "$PKG/Fable2.sh" "$PKG/fable_2"
# Strip debug info from the shipped copies (line tables only, but large).
strip --strip-debug "$PKG/fable_2" "$PKG/librexruntime.so" "$PKG/librexgpu-xenos.so"

cat > "$PKG/PUT_GAME_FILES_HERE.txt" <<EOF
Copy these two items from your own Fable II Game of the Year disc into this
folder, next to Fable2.sh:

  default.xex
  data/          (the whole folder, about 6.5 GB)

Then run Fable2.sh. On a Steam Deck, add Fable2.sh to Steam as a non-Steam
game (file type "All files") and do not force a compatibility tool (Proton).
Needs glibc $NEED_GLIBC or newer (SteamOS 3.6+). Built for CPU level: $CPU.
See Linux.md for settings.
EOF

tar -C "$DIST" -czf "$DIST/$NAME.tar.gz" "$NAME"
echo "Package: $PKG"
echo "Archive: $DIST/$NAME.tar.gz ($(du -h "$DIST/$NAME.tar.gz" | cut -f1)), needs glibc $NEED_GLIBC+"
