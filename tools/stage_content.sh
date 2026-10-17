#!/usr/bin/env bash
# Stage the game content next to a built binary so the build directory is
# self-contained: <dest>/default.xex, data/, nxeart/, $SystemUpdate/ (saves/
# and cache/ are created by the game at runtime). macOS/Linux counterpart of
# stage_content.cmd. Incremental: only newer or missing files are copied.
#
# Usage: tools/stage_content.sh <content_root> <dest_dir>
set -euo pipefail

if [ $# -ne 2 ]; then
  echo "Usage: tools/stage_content.sh <content_root> <dest_dir>" >&2
  exit 1
fi
ROOT="$1"
DEST="$2"
if [ ! -f "$ROOT/default.xex" ]; then
  echo "Error: no default.xex in $ROOT - extract the game content there first." >&2
  exit 1
fi

mkdir -p "$DEST"
cp -p "$ROOT/default.xex" "$DEST/"
# shellcheck disable=SC2016  # '$SystemUpdate' is the literal folder name
for d in data nxeart '$SystemUpdate'; do
  [ -d "$ROOT/$d" ] || continue
  # data/dir.manifest is merged below, not copied (see the note there).
  exclude=()
  [ "$d" = data ] && exclude=(--exclude /dir.manifest)
  # ${a[@]+...}: an empty array trips set -u on macOS's bash 3.2.
  rsync -a --update ${exclude[@]+"${exclude[@]}"} "$ROOT/$d/" "$DEST/$d/"
done

# The build may already have created or extended $DEST/data/dir.manifest with
# the staged external-Lua entries, so keep every source line plus any
# destination-only lines (idempotent). A missing entry hides that asset from
# the game's VFS (guest null-reads at startup).
SRC_MANIFEST="$ROOT/data/dir.manifest"
DST_MANIFEST="$DEST/data/dir.manifest"
if [ -f "$SRC_MANIFEST" ]; then
  if [ -f "$DST_MANIFEST" ]; then
    awk '!seen[$0]++' "$SRC_MANIFEST" "$DST_MANIFEST" > "$DST_MANIFEST.tmp"
    mv "$DST_MANIFEST.tmp" "$DST_MANIFEST"
  else
    cp -p "$SRC_MANIFEST" "$DST_MANIFEST"
  fi
fi
