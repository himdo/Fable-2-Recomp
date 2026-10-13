#!/usr/bin/env bash
# Build the Fable 2 ReXGlue project on Linux (x86-64).
#
# Toolchain: clang 20 + Ninja + CMake. The ReXGlue SDK (runtime, Vulkan GPU
# plugin and codegen tool) is built from the pinned source submodule
# (thirdparty/rexglue-sdk) as part of this build.
#
# Usage:
#   ./build.sh                 build the full recompiled executable (Release)
#   ./build.sh -debug [t]      build as Debug instead of Release
#   ./build.sh -clean          wipe the build dir for the config
#   ./build.sh <target>        build any other CMake target
#
# Needs default.xex in the project root (see README "How to extract").
# Compilers are taken from PATH (clang++-20 / clang++), or from a local
# unpacked toolchain under out/tooling/llvm20 when none is installed.
set -euo pipefail
cd "$(dirname "$0")"
ROOT="$PWD"

CONFIG=Release
TARGET=fable_2
CLEAN=0
for arg in "$@"; do
    case "$arg" in
        -debug|-d) CONFIG=Debug ;;
        -release|-r) CONFIG=Release ;;
        -clean|-c) CLEAN=1 ;;
        *) TARGET="$arg" ;;
    esac
done
PRESET="linux-amd64-$(echo "$CONFIG" | tr '[:upper:]' '[:lower:]')"
BUILD="$ROOT/out/build/$PRESET"

if [ "$CLEAN" = 1 ]; then
    rm -rf "$BUILD"
    echo "Cleaned $BUILD"
    exit 0
fi

# Clang: prefer one on PATH, else the local unpacked packages.
LOCAL_LLVM="$ROOT/out/tooling/llvm20/usr"
if command -v clang++-20 >/dev/null; then
    CC=clang-20 CXX=clang++-20
elif command -v clang++ >/dev/null; then
    CC=clang CXX=clang++
elif [ -x "$LOCAL_LLVM/lib/llvm-20/bin/clang++" ]; then
    CC="$LOCAL_LLVM/lib/llvm-20/bin/clang" CXX="$LOCAL_LLVM/lib/llvm-20/bin/clang++"
    export LD_LIBRARY_PATH="$LOCAL_LLVM/lib/x86_64-linux-gnu:$LOCAL_LLVM/lib/llvm-20/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
else
    echo "Error: clang 20 not found. Install it (apt install clang-20 lld-20)." >&2
    exit 1
fi
if ! command -v ninja >/dev/null; then
    if [ -x "$ROOT/out/tooling/bin/ninja" ]; then
        export PATH="$ROOT/out/tooling/bin:$PATH"
    else
        echo "Error: ninja not found. Install it (apt install ninja-build)." >&2
        exit 1
    fi
fi

# Optional locally unpacked -dev packages (for machines without root access;
# the normal route is: apt install libx11-xcb-dev libxss-dev).
LOCAL_PC="$ROOT/out/tooling/sysdeps/usr/lib/x86_64-linux-gnu/pkgconfig"
if [ -d "$LOCAL_PC" ]; then
    export PKG_CONFIG_PATH="$LOCAL_PC${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
    export LIBRARY_PATH="$(dirname "$LOCAL_PC")${LIBRARY_PATH:+:$LIBRARY_PATH}"
fi

if [ ! -f "$ROOT/default.xex" ]; then
    echo "Error: default.xex not found in $ROOT (see README \"How to extract\")." >&2
    exit 1
fi

# Pinned SDK source + its dependencies (no-op once fetched).
SDKSRC="$ROOT/thirdparty/rexglue-sdk"
if [ ! -f "$SDKSRC/CMakeLists.txt" ]; then
    git submodule update --init thirdparty/rexglue-sdk
fi
# Linux build fixes for the SDK fork (thirdparty/rexglue-sdk-linux.patch) go on
# top of the prepared tree. prepare_runtime_sdk.py only accepts an unmodified
# tree, so it runs before the patch and is skipped once the patch is in.
LINUX_PATCH="$ROOT/thirdparty/rexglue-sdk-linux.patch"
if ! git -C "$SDKSRC" apply --reverse --check "$LINUX_PATCH" 2>/dev/null; then
    python3 "$ROOT/tools/prepare_runtime_sdk.py" "$SDKSRC"
    git -C "$SDKSRC" apply "$LINUX_PATCH"
fi

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
        -DCMAKE_C_FLAGS="-march=x86-64-v2" -DCMAKE_CXX_FLAGS="-march=x86-64-v2" \
        -DREXSDK_DIR="$SDKSRC" \
        -DREXGLUE_ENABLE_TRACY=OFF -DREXGLUE_BUILD_TESTS=OFF \
        "${SDL_ARGS[@]}"
}

configure
# The recompiled sources are only known to CMake once codegen has run, so a
# fresh tree generates them first and then configures again to pick them up.
if [ ! -f "$ROOT/generated/default/sources.cmake" ]; then
    cmake --build "$BUILD" --target fable_2_codegen
    configure
fi
cmake --build "$BUILD" --target "$TARGET"
