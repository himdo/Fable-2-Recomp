#!/bin/bash
set -euo pipefail
project_root="$(cd "$(dirname "$0")" && pwd)"
runtime="$project_root/out/build/mac-arm64-metal-release"
game_data_root="${FABLE2_GAME_DATA_ROOT:-$project_root}"
if [[ "$(uname -s)" != Darwin || "$(uname -m)" != arm64 ]]; then
    echo "This Metal build requires Apple Silicon macOS." >&2; exit 1
fi
if [[ ! -x "$runtime/fable_2" || ! -f "$runtime/librexgpu-metal.dylib" ]]; then
    echo "Build first with python3 tools/build_macos_metal.py." >&2; exit 1
fi
if pgrep -x fable_2 >/dev/null; then
    echo "Close the running game before starting another instance." >&2; exit 1
else
    process_status=$?
    if [[ "$process_status" != 1 ]]; then
        echo "Unable to check for an existing game." >&2; exit 1
    fi
fi
export FABLE_STARTUP_LOCK_HANDOFF="${FABLE_STARTUP_LOCK_HANDOFF:-1}"
cd "$runtime"
exec ./fable_2 --game_data_root "$game_data_root" --fullscreen=false --gpu_plugin=metal \
    --async_shader_compilation=true --metal_pipeline_creation_threads=2 \
    --metal_type0_register_batching=true \
    --metal_constant_payload_cache=false --metal_force_bc_decompress=false \
    --metal_backend_telemetry=false --metal_root_rebuild_detail_telemetry=false \
    --metal_probe_capture_interval=0 "$@"
