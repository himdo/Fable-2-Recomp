#!/usr/bin/env bash
# Fetch the prebuilt ReXGlue SDK (v0.10.0) for this host into
# out/tooling/rexglue-sdk-0.10.0/<platform> (bin/, include/, lib/).
# No-op if already present. macOS/Linux counterpart of setup_sdk.cmd; run
# automatically by build.sh when the SDK is missing.
set -euo pipefail

VER=0.10.0
case "$(uname -s)-$(uname -m)" in
  Darwin-arm64)  PLATFORM=mac-arm64 ;;
  Darwin-x86_64) PLATFORM=mac-amd64 ;;
  Linux-x86_64)  PLATFORM=linux-amd64 ;;
  Linux-aarch64) PLATFORM=linux-arm64 ;;
  *) echo "setup_sdk.sh: unsupported host $(uname -s)-$(uname -m)" >&2; exit 1 ;;
esac

REPO="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$REPO/out/tooling/rexglue-sdk-$VER"
SDK="$DEST/$PLATFORM"
if [ -f "$SDK/lib/cmake/rexglue/rexglueConfig.cmake" ]; then
  echo "ReXGlue SDK v$VER already present: $SDK"
  exit 0
fi

ZIP="rexglue-sdk-$VER-$PLATFORM.zip"
URL="https://github.com/rexglue/rexglue-sdk/releases/download/v$VER/$ZIP"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "Downloading $URL"
curl -L --fail -sS -o "$TMP/$ZIP" "$URL"
mkdir -p "$DEST"
unzip -q -o "$TMP/$ZIP" -d "$DEST"

if [ ! -f "$SDK/lib/cmake/rexglue/rexglueConfig.cmake" ]; then
  echo "setup_sdk.sh: SDK incomplete after extract; expected $SDK" >&2
  exit 1
fi
echo "OK: ReXGlue SDK v$VER -> $SDK"
