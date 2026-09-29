#!/bin/bash
set -euo pipefail
project_root="$(cd "$(dirname "$0")" && pwd)"
if [[ "$(uname -s)" != Darwin ]]; then
    echo "This launcher runs on macOS." >&2
    exit 1
fi
case "$(uname -m)" in
    arm64) platform=mac-arm64 ;;
    x86_64) platform=mac-amd64 ;;
    *) echo "Unsupported Mac architecture." >&2; exit 1 ;;
esac
# Prefer the built Metal runtime on Apple Silicon.
# A fresh checkout retains the supported Vulkan build until Metal is installed.
backend="${FABLE2_MACOS_BACKEND:-auto}"
case "$backend" in
    auto|metal|vulkan) ;;
    *) echo "FABLE2_MACOS_BACKEND must be auto, metal, or vulkan." >&2; exit 1 ;;
esac
if [[ "$backend" != vulkan && "$platform" == mac-arm64 &&
      -x "$project_root/out/build/mac-arm64-metal-release/fable_2" &&
      -f "$project_root/out/build/mac-arm64-metal-release/librexgpu-metal.dylib" ]]; then
    exec "$project_root/run_macos_metal.command" "$@"
fi
if [[ "$backend" == metal ]]; then
    echo "The optional Apple Silicon Metal runtime is not installed. See docs/macos-metal.md." >&2
    exit 1
fi
build_dir="$project_root/out/build/$platform-release"
if [[ ! -x "$build_dir/fable_2" ]]; then
    echo "Build first with tools/build_macos.sh." >&2
    exit 1
fi
if [[ "$platform" == mac-arm64 && "${FABLE2_MACOS_TERRAIN_FIX:-1}" != 0 ]]; then
    python3 "$project_root/tools/macos_terrain_fix.py" "$build_dir/librexgpu-xenos.dylib"
fi
cd "$build_dir"
# Avoid the repeated Vulkan debug callback and MoltenVK warning stream during
# play. MoltenVK errors and the normal game/runtime logger remain enabled.
# Explicit environment values and command-line cvars can restore diagnostics.
export REX_VULKAN_LOG_DEBUG_MESSAGES="${REX_VULKAN_LOG_DEBUG_MESSAGES:-0}"
export MVK_CONFIG_LOG_LEVEL="${MVK_CONFIG_LOG_LEVEL:-1}"
exec ./fable_2 --game_data_root "$project_root" --fullscreen=false "$@"
