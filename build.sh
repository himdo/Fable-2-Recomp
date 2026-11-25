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
#   --trace          keep the guest function-entry trace hook
#                    (FABLE2_FUNC_TRACE, see README); off by default here
#                    because it costs a check on every guest call
#   --pgo-generate   build the runtime libraries instrumented for profile-
#                    guided optimization (they write fable2-pgo-*.profraw
#                    files while the game runs)
#   --pgo-use <file> optimize the runtime libraries with a profile merged
#                    from those files (llvm-profdata merge -o <file> ...)
#   --lto            ThinLTO for the GPU plugin library (needs lld)
#   --game-lto       ThinLTO for the game code too (the recompiled code and the
#                    app; needs lld): lets the compiler inline across the
#                    recompiled code's source files
#   --game-pgo-generate  instrument the game code itself (the recompiled code
#                    and the app) too; it writes rex-pgo-*.profraw next to the
#                    executable every 30 s of play and when quitting from the
#                    console
#   --game-pgo-use <file>  optimize the game code with a merged profile (the
#                    same .profdata as --pgo-use works for both)
#   --cpu <level>    code generation target (also: FABLE2_CPU=<level>):
#                      generic  x86-64-v2, runs on any CPU from ~2009 on (default)
#                      deck     Steam Deck (Zen 2: AVX2, FMA)
#                      native   the CPU this is being built on
#   target           any CMake target (default: fable_2), or "package":
#                    build fable_2 (Release) and write a portable, ready to
#                    run folder + .tar.gz to out/dist/Fable2-Linux-<cpu>
#                    (tools/package_linux.sh), to copy to another machine
#
# Needs default.xex in the project root (see Linux.md, "Extract the game files").
# Compilers are taken from PATH (clang++-20 / clang++), or from a local
# unpacked toolchain under out/tooling/llvm20 when none is installed (see
# Linux.md, "Building without root access").
set -euo pipefail
cd "$(dirname "$0")"
ROOT="$PWD"

CONFIG=Release
TARGET=fable_2
CLEAN=0
SDKSRC="${REXSDK_SOURCE:-}"
CPU="${FABLE2_CPU:-generic}"
FUNC_TRACE=OFF
PGO=OFF
LTO=OFF
GAME_LTO=OFF
PGO_PROFILE=
GAME_PGO=OFF
GAME_PGO_PROFILE=
while [ $# -gt 0 ]; do
    case "$1" in
        -debug|-d) CONFIG=Debug ;;
        -release|-r) CONFIG=Release ;;
        -clean|-c) CLEAN=1 ;;
        --sdk) shift; SDKSRC="${1:?--sdk needs a path}" ;;
        --sdk=*) SDKSRC="${1#--sdk=}" ;;
        --cpu) shift; CPU="${1:?--cpu needs a level}" ;;
        --cpu=*) CPU="${1#--cpu=}" ;;
        --trace) FUNC_TRACE=ON ;;
        --pgo-generate) PGO=GENERATE ;;
        --lto) LTO=ON ;;
        --game-lto) GAME_LTO=ON ;;
        --game-pgo-generate) GAME_PGO=GENERATE ;;
        --game-pgo-use) shift; GAME_PGO=USE; GAME_PGO_PROFILE="$(readlink -f "${1:?--game-pgo-use needs a .profdata file}")" ;;
        --game-pgo-use=*) GAME_PGO=USE; GAME_PGO_PROFILE="$(readlink -f "${1#--game-pgo-use=}")" ;;
        --pgo-use) shift; PGO=USE; PGO_PROFILE="$(readlink -f "${1:?--pgo-use needs a .profdata file}")" ;;
        --pgo-use=*) PGO=USE; PGO_PROFILE="$(readlink -f "${1#--pgo-use=}")" ;;
        -h|--help) sed -n '2,45p' "$0"; exit 0 ;;
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
# Deck: no vzeroupper. It exists to avoid the SSE/AVX transition penalty of
# Intel CPUs; AMD's Zen 2 has none, but executes it as 12 micro-ops, and the
# compiler puts one at the exit of every function that used 256-bit registers
# (the command processor's packet and draw functions run it millions of times
# a second). LTO links generate code too, so they get the option as well.
NO_VZEROUPPER="-mllvm -x86-use-vzeroupper=false"
LTO_LINK_FLAGS=
[ "$CPU" = deck ] && LTO_LINK_FLAGS="-Wl,-mllvm,-x86-use-vzeroupper=false"
case "$CPU" in
    generic) ARCH_FLAGS="-march=x86-64-v2" ;;
    deck) ARCH_FLAGS="-march=znver2 -mtune=znver2 $NO_VZEROUPPER" ;;
    native) ARCH_FLAGS="-march=native" ;;
    *) echo "Error: --cpu must be generic, deck or native (got '$CPU')" >&2; exit 1 ;;
esac
ARCH_FLAGS="$ARCH_FLAGS -ffp-contract=off"

need() {
    command -v "$1" >/dev/null || { echo "Error: $1 not found. $2" >&2; exit 1; }
}
# Clang: prefer clang 20 by name, else whatever clang is on PATH, else the
# local unpacked packages.
LOCAL_LLVM="$ROOT/out/tooling/llvm20/usr"
if command -v clang++-20 >/dev/null; then
    CC=clang-20 CXX=clang++-20
elif command -v clang++ >/dev/null; then
    CC=clang CXX=clang++
elif [ -x "$LOCAL_LLVM/lib/llvm-20/bin/clang++" ]; then
    CC="$LOCAL_LLVM/lib/llvm-20/bin/clang" CXX="$LOCAL_LLVM/lib/llvm-20/bin/clang++"
    export LD_LIBRARY_PATH="$LOCAL_LLVM/lib/x86_64-linux-gnu:$LOCAL_LLVM/lib/llvm-20/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
else
    echo "Error: clang not found. Install clang 20 and lld"          "(Debian/Ubuntu: apt install clang-20 lld-20;"          "Fedora: dnf install clang20 lld20; Arch: pacman -S clang lld)." >&2
    exit 1
fi
if ! command -v ninja >/dev/null && [ -x "$ROOT/out/tooling/bin/ninja" ]; then
    export PATH="$ROOT/out/tooling/bin:$PATH"
fi
need cmake "Install CMake 3.25 or newer."
need ninja "Install Ninja (Debian/Ubuntu/Fedora: ninja-build; Arch: ninja)."
need python3 "Install Python 3."
need git "Install git."

# Optional locally unpacked -dev packages (for machines without root access;
# the normal route is installing the X11 xcb and XScrnSaver dev packages).
LOCAL_PC="$ROOT/out/tooling/sysdeps/usr/lib/x86_64-linux-gnu/pkgconfig"
if [ -d "$LOCAL_PC" ]; then
    export PKG_CONFIG_PATH="$LOCAL_PC${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
    export LIBRARY_PATH="$(dirname "$LOCAL_PC")${LIBRARY_PATH:+:$LIBRARY_PATH}"
fi

if [ ! -f "$ROOT/default.xex" ]; then
    echo "Error: default.xex not found in $ROOT (see Linux.md, \"Extract the game files\")." >&2
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
# it only inhibits the screensaver, so build without it when the XScrnSaver
# headers are not installed instead of failing the configure.
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
        -DFABLE2_FUNC_TRACE="$FUNC_TRACE" \
        -DREXGLUE_PGO="$PGO" -DREXGLUE_PGO_PROFILE="$PGO_PROFILE" \
        -DREXGLUE_LTO="$LTO" -DREXGLUE_LTO_LINK_FLAGS="$LTO_LINK_FLAGS" -DFABLE2_LTO="$GAME_LTO"         -DREXGLUE_GPU_LINE_TABLES=ON         -DFABLE2_PGO="$GAME_PGO" -DFABLE2_PGO_PROFILE="$GAME_PGO_PROFILE" \
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
# --game-lto: call the recompiled functions directly (__imp__NAME) wherever
# nothing overrides them, so the link-time optimizer can inline them. Only
# calls not yet rewritten change, so a rebuild recompiles nothing extra.
# Codegen is brought up to date first: the build below would otherwise
# regenerate the sources after the rewrite (manifest or codegen config newer)
# and compile them without direct calls.
if [ "$GAME_LTO" = ON ] && [ -d "$ROOT/generated/default" ]; then
    cmake --build "$BUILD" --target fable_2_codegen
    python3 "$ROOT/tools/direct_guest_calls.py" "$ROOT/generated/default" "$ROOT/src"
fi
if [ "$TARGET" = package ]; then
    [ "$CONFIG" = Release ] || { echo "Error: package needs a Release build" >&2; exit 1; }
    cmake --build "$BUILD" --target fable_2
    "$ROOT/tools/package_linux.sh" "$BUILD" "$CPU"
    exit 0
fi
cmake --build "$BUILD" --target "$TARGET"
echo "Built $TARGET in $BUILD. Run it with ./run.sh"
