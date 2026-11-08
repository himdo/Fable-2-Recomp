#!/usr/bin/env bash
# Build the Fable 2 ReXGlue project on Linux (x86-64, Vulkan).
#
# Toolchain: clang 20 + Ninja + CMake >= 3.25 + Python 3. The ReXGlue SDK
# (runtime, Vulkan GPU plugin and codegen tool) is built from source as part of
# this build (REXSDK_DIR), from the pinned submodule thirdparty/rexglue-sdk or
# from the tree given with --sdk.
#
# Usage:
#   ./build.sh [options] [target]
#
# Options:
#   -release, -r     Release build (-O3, the default)
#   -debug, -d       Debug build (-O0, much slower at runtime)
#   -clean, -c       wipe the build dir for the selected config and stop
#   --sdk <path>     build the SDK from this source tree instead of the
#                    submodule (also: REXSDK_SOURCE=<path>)
#   --cpu <level>    code generation target (also: FABLE2_CPU=<level>):
#                      generic  x86-64-v2, runs on any CPU from ~2009 on (default)
#                      deck     Steam Deck (Zen 2: AVX2, FMA)
#                      native   the CPU this is being built on
#   target           any CMake target (default: fable_2)
#
# Needs default.xex in the project root (see Linux.md, "Add the game content").
set -euo pipefail
cd "$(dirname "$0")"
ROOT="$PWD"

CONFIG=Release
TARGET=fable_2
CLEAN=0
SDKSRC="${REXSDK_SOURCE:-}"
CPU="${FABLE2_CPU:-generic}"
while [ $# -gt 0 ]; do
    case "$1" in
        -debug|-d) CONFIG=Debug ;;
        -release|-r) CONFIG=Release ;;
        -clean|-c) CLEAN=1 ;;
        --sdk) shift; SDKSRC="${1:?--sdk needs a path}" ;;
        --sdk=*) SDKSRC="${1#--sdk=}" ;;
        --cpu) shift; CPU="${1:?--cpu needs a level}" ;;
        --cpu=*) CPU="${1#--cpu=}" ;;
        -h|--help) sed -n '2,25p' "$0"; exit 0 ;;
        -*) echo "Error: unknown option $1 (see ./build.sh --help)" >&2; exit 1 ;;
        *) TARGET="$1" ;;
    esac
    shift
done
PRESET="linux-amd64-$(echo "$CONFIG" | tr '[:upper:]' '[:lower:]')"
BUILD="$ROOT/out/build/$PRESET"

if [ "$CLEAN" = 1 ]; then
    rm -rf "$BUILD"
    echo "Cleaned $BUILD"
    exit 0
fi

# Code generation target. -ffp-contract=off keeps every build bit-identical in
# its float math to the Windows build: the recompiled VMX128 code spells
# vmaddfp as a separate multiply and add (they round twice, as on the Xbox
# 360), and with FMA available clang would otherwise fuse them. Explicit fused
# instructions (fmadd -> std::fma) stay fused, and become one instruction
# instead of a libm call once FMA is enabled.
case "$CPU" in
    generic) ARCH_FLAGS="-march=x86-64-v2" ;;
    deck) ARCH_FLAGS="-march=znver2 -mtune=znver2" ;;
    native) ARCH_FLAGS="-march=native" ;;
    *) echo "Error: --cpu must be generic, deck or native (got '$CPU')" >&2; exit 1 ;;
esac
ARCH_FLAGS="$ARCH_FLAGS -ffp-contract=off"

need() {
    command -v "$1" >/dev/null || { echo "Error: $1 not found. $2" >&2; exit 1; }
}
# Clang: prefer clang 20 by name, else whatever clang is on PATH.
if command -v clang++-20 >/dev/null; then
    CC=clang-20 CXX=clang++-20
elif command -v clang++ >/dev/null; then
    CC=clang CXX=clang++
else
    echo "Error: clang 20 not found (see Linux.md, \"Install the build tools\")." >&2
    exit 1
fi
need cmake "Install CMake 3.25 or newer."
need ninja "Install Ninja (apt install ninja-build)."
need python3 "Install Python 3."
need git "Install git."

if [ ! -f "$ROOT/default.xex" ]; then
    echo "Error: default.xex not found in $ROOT (see Linux.md, \"Add the game content\")." >&2
    exit 1
fi

# SDK source tree: --sdk / REXSDK_SOURCE, else the pinned submodule.
if [ -z "$SDKSRC" ]; then
    SDKSRC="$ROOT/thirdparty/rexglue-sdk"
    if [ ! -f "$SDKSRC/CMakeLists.txt" ]; then
        git submodule update --init thirdparty/rexglue-sdk
    fi
fi
SDKSRC="$(cd "$SDKSRC" && pwd)"
[ -f "$SDKSRC/CMakeLists.txt" ] || { echo "Error: no SDK source tree at $SDKSRC" >&2; exit 1; }
echo "ReXGlue SDK: $SDKSRC ($(git -C "$SDKSRC" rev-parse --abbrev-ref HEAD 2>/dev/null || echo '?') @ $(git -C "$SDKSRC" rev-parse --short HEAD 2>/dev/null || echo '?'))"
echo "CPU target:  $CPU ($ARCH_FLAGS)"

# Fetch the SDK's own submodules and fix its libmspack pin (no-op once done).
python3 "$ROOT/tools/prepare_runtime_sdk.py" "$SDKSRC"

# SDL3 treats the X11 screensaver extension as required when X11 is enabled;
# it only inhibits the screensaver, so build without it when libxss-dev is
# not installed instead of failing the configure.
SDL_ARGS=()
if [ ! -f /usr/include/X11/extensions/scrnsaver.h ]; then
    SDL_ARGS+=(-DSDL_X11_XSCRNSAVER=OFF)
fi

configure() {
    cmake -S "$ROOT" -B "$BUILD" -G Ninja \
        -DCMAKE_BUILD_TYPE="$CONFIG" \
        -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
        -DCMAKE_C_FLAGS="$ARCH_FLAGS" -DCMAKE_CXX_FLAGS="$ARCH_FLAGS" \
        -DREXSDK_DIR="$SDKSRC" \
        -DREXGLUE_USE_VULKAN=ON \
        -DREXGLUE_ENABLE_TRACY=OFF -DREXGLUE_BUILD_TESTS=OFF \
        "${SDL_ARGS[@]}"
}

# A CPU target change has to reach every object, so start over when it changes.
STAMP="$BUILD/.fable2_arch_flags"
if [ -f "$STAMP" ] && [ "$(cat "$STAMP")" != "$ARCH_FLAGS" ]; then
    echo "CPU target changed, reconfiguring from scratch."
    rm -rf "$BUILD"
fi

configure
mkdir -p "$BUILD"
echo "$ARCH_FLAGS" > "$STAMP"
# The recompiled sources are only known to CMake once codegen has run, so a
# fresh tree generates them first and then configures again to pick them up.
if [ ! -f "$ROOT/generated/default/sources.cmake" ]; then
    cmake --build "$BUILD" --target fable_2_codegen
    configure
fi
cmake --build "$BUILD" --target "$TARGET"
echo "Built $TARGET in $BUILD. Run it with ./run.sh"
