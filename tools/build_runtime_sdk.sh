#!/usr/bin/env bash
# Matched runtime + Vulkan plugin built from the SDK fork (thirdparty/rexglue-sdk),
# staged over the official prebuilt SDK. macOS/Linux counterpart of
# tools/build_runtime_sdk.cmd. Usage: tools/build_runtime_sdk.sh [Release|Debug]
#
# REXGLUE_SDK_SOURCE / RUNTIME_SDK_BUILD override the SDK checkout and build
# tree (e.g. on an external disk; it must be case-sensitive and support
# symlinks, so not exFAT).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CONFIGURATION="${1:-Release}"
case "$CONFIGURATION" in Release|Debug) ;; *) echo "Error: configuration must be Release or Debug" >&2; exit 1 ;; esac

case "$(uname -s)-$(uname -m)" in
  Darwin-arm64) PLATFORM=mac-arm64; ARCH="-DCMAKE_OSX_ARCHITECTURES=arm64" ;;
  Darwin-x86_64) PLATFORM=mac-amd64; ARCH="-DCMAKE_OSX_ARCHITECTURES=x86_64" ;;
  Linux-x86_64) PLATFORM=linux-amd64; ARCH="" ;;
  Linux-aarch64) PLATFORM=linux-arm64; ARCH="" ;;
  *) echo "Error: unsupported host $(uname -s)-$(uname -m)" >&2; exit 1 ;;
esac

SOURCE="${REXGLUE_SDK_SOURCE:-$ROOT/thirdparty/rexglue-sdk}"
BUILD="${RUNTIME_SDK_BUILD:-$ROOT/out/build/runtime-sdk-$PLATFORM}"
STAGED="$ROOT/out/tooling/runtime-sdk/$PLATFORM"
OFFICIAL="$ROOT/out/tooling/rexglue-sdk-0.10.0/$PLATFORM"
if [ "$CONFIGURATION" = Debug ]; then
  BUILD="$BUILD-debug"
  STAGED="$ROOT/out/tooling/runtime-sdk-debug/$PLATFORM"
fi

if [ ! -f "$SOURCE/CMakeLists.txt" ]; then
  [ -z "${REXGLUE_SDK_SOURCE:-}" ] || { echo "Error: no SDK checkout at $SOURCE" >&2; exit 1; }
  git -C "$ROOT" submodule update --init thirdparty/rexglue-sdk
fi
[ -x "$OFFICIAL/bin/rexglue" ] || "$ROOT/tools/setup_sdk.sh"

python3 "$ROOT/tools/prepare_runtime_sdk.py" "$SOURCE"
# shellcheck disable=SC2086  # ARCH is empty or one flag
cmake -S "$SOURCE" -B "$BUILD" -G Ninja $ARCH \
  -DCMAKE_BUILD_TYPE="$CONFIGURATION" -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DREXGLUE_USE_VULKAN=ON \
  -DREXGLUE_ENABLE_TRACY=OFF -DREXGLUE_ENABLE_FIDELITYFX=OFF \
  -DREXGLUE_BUILD_TESTS=OFF
cmake --build "$BUILD" --target rexruntime rexgpu-xenos
python3 "$ROOT/tools/stage_renderer_sdk.py" "$OFFICIAL" "$SOURCE" "$BUILD" "$STAGED" \
  --configuration "$CONFIGURATION" --platform "$PLATFORM"
