#!/bin/bash
set -euo pipefail
project_root="$(cd "$(dirname "$0")" && pwd)"
export FABLE2_MACOS_BACKEND=vulkan
exec "$project_root/run_macos.command" "$@"
