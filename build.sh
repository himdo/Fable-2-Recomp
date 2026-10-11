#!/usr/bin/env bash
# Build the Fable 2 ReXGlue project on macOS/Linux (counterpart of build.cmd).
#
# Toolchain: clang + Ninja + CMake (Homebrew on macOS: brew install cmake ninja).
# Preset: <platform>-debug by default, <platform>-release with -release, where
# <platform> is mac-arm64, mac-amd64, linux-amd64 or linux-arm64.
#
# Usage:
#   ./build.sh                 build fable_2_codegen (runs codegen from fable_2_manifest.toml)
#   ./build.sh fable_2         build the full recompiled executable
#   ./build.sh <other target>  build any other CMake target
#   ./build.sh -release [t]    build as Release (-O3) instead of Debug  (-r: same)
#   ./build.sh -clean [t]      wipe the build dir for the config instead of building  (-c: same)
#
# The game content (default.xex, data/, ...) is not part of the repo; codegen
# needs default.xex in the repo root. See tools/stage_content.sh to run in place.
set -euo pipefail
cd "$(dirname "$0")"

case "$(uname -s)-$(uname -m)" in
  Darwin-arm64)  PLATFORM=mac-arm64 ;;
  Darwin-x86_64) PLATFORM=mac-amd64 ;;
  Linux-x86_64)  PLATFORM=linux-amd64 ;;
  Linux-aarch64) PLATFORM=linux-arm64 ;;
  *) echo "build.sh: unsupported host $(uname -s)-$(uname -m)" >&2; exit 1 ;;
esac

if [ "${1:-}" = launcher ] || [ "${1:-}" = launcher-dev ]; then
  echo "build.sh: the WPF launcher is Windows-only; edit fable2_config.toml / fable_2.toml next to the binary instead." >&2
  exit 1
fi

CONFIG="$PLATFORM-debug"
TARGET=""
CLEAN=0
for arg in "$@"; do
  case "$arg" in
    -release|-r) CONFIG="$PLATFORM-release" ;;
    -clean|-c)   CLEAN=1 ;;
    *)           TARGET="$arg" ;;
  esac
done
TARGET="${TARGET:-fable_2_codegen}"

if [ "$CLEAN" = 1 ]; then
  if [ -d "out/build/$CONFIG" ]; then
    rm -rf "out/build/$CONFIG"
    echo "Cleaned out/build/$CONFIG"
  else
    echo "Nothing to clean: out/build/$CONFIG does not exist"
  fi
  exit 0
fi

# Official prebuilt SDK (codegen tool + runtime): prefer rexglue on PATH,
# else the package downloaded under out/tooling.
REXSDK=""
if command -v rexglue >/dev/null 2>&1; then
  REXSDK="$(cd "$(dirname "$(command -v rexglue)")/.." && pwd)"
fi
if [ ! -f "$REXSDK/lib/cmake/rexglue/rexglueConfig.cmake" ]; then
  REXSDK="$PWD/out/tooling/rexglue-sdk-0.10.0/$PLATFORM"
  [ -f "$REXSDK/lib/cmake/rexglue/rexglueConfig.cmake" ] || tools/setup_sdk.sh
fi

# Bootstrap CMake integration from the existing manifest on a fresh clone.
# Do not run rexglue init: that would replace the project's authored files.
if [ ! -f generated/rexglue.cmake ]; then
  "$REXSDK/bin/rexglue" codegen fable_2_manifest.toml
fi

# Link the runtime built from the SDK fork with the project's fixes when
# tools/build_runtime_sdk.sh has staged it; else the official runtime as-is.
# Codegen keeps running the official tool against its own runtime.
# ponytail: not rebuilt automatically (unlike build.cmd); rerun the script
# after changing the fork or its patches.
CODEGEN_TOOL="$REXSDK/bin/rexglue"
if [ -f "out/tooling/runtime-sdk/$PLATFORM/lib/cmake/rexglue/rexglueConfig.cmake" ]; then
  REXSDK="$PWD/out/tooling/runtime-sdk/$PLATFORM"
fi
cmake --preset "$CONFIG" \
  -DFABLE2_CODEGEN_TOOL="$CODEGEN_TOOL" \
  -DCMAKE_PREFIX_PATH="$REXSDK" \
  -Drexglue_DIR="$REXSDK/lib/cmake/rexglue" \
  -DREXGLUE_SDK_ROOT="$REXSDK"
cmake --build "out/build/$CONFIG" --target "$TARGET"
