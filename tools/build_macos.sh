#!/bin/bash
# Build the native macOS release. Arguments are passed to CMake configuration.
set -euo pipefail

project_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$project_root"
if [[ "$(uname -s)" != Darwin ]]; then
    echo "This script builds on macOS." >&2
    exit 1
fi
case "$(uname -m)" in
    arm64)
        platform=mac-arm64
        sdk_sha256=1192638b51a6963aa6a4f24b77ebcb90c16f7fd0b91b311e11a90100f5274326
        ;;
    x86_64)
        platform=mac-amd64
        sdk_sha256=a71b12e8c91d7f9084683166c59a176634e6adc711f2c60f80cc4a286aa13642
        ;;
    *) echo "Unsupported Mac architecture." >&2; exit 1 ;;
esac

for tool in cmake clang++ python3 curl ditto shasum; do
    command -v "$tool" >/dev/null || { echo "Missing build tool: $tool" >&2; exit 1; }
done
if [[ ! -f default.xex || ! -d data ]]; then
    echo "Extract default.xex and data/ into the project root first." >&2
    exit 1
fi

# Upstream now tracks thirdparty/rexglue-sdk as a source submodule.
# Keep downloaded Mac binaries outside that checkout.
sdk_root="${REXGLUE_SDK_ROOT:-$project_root/out/sdk/$platform}"
if [[ ! -x "$sdk_root/bin/rexglue" ]]; then
    if [[ -n "${REXGLUE_SDK_ROOT:-}" ]]; then
        echo "REXGLUE_SDK_ROOT does not contain bin/rexglue: $sdk_root" >&2
        exit 1
    fi
    archive="$project_root/out/downloads/rexglue-sdk-0.10.0-$platform.zip"
    mkdir -p "$(dirname "$archive")" "$project_root/out/sdk"
    curl --fail --location --show-error \
        "https://github.com/rexglue/rexglue-sdk/releases/download/v0.10.0/rexglue-sdk-0.10.0-$platform.zip" \
        --output "$archive"
    printf '%s  %s\n' "$sdk_sha256" "$archive" | shasum -a 256 -c -
    ditto -x -k "$archive" "$project_root/out/sdk"
fi

# The source ZIP omits generated/rexglue.cmake. Generate only that missing
# integration file in a temporary scaffold; keep the game's manifest and code.
if [[ ! -f generated/rexglue.cmake ]]; then
    scaffold="$(mktemp -d "${TMPDIR:-/tmp}/fable2-scaffold.XXXXXX")"
    trap 'rm -rf "$scaffold"' EXIT
    "$sdk_root/bin/rexglue" init --project-name fable_2 \
        --xex-path "$project_root/default.xex" --project-root "$scaffold"
    mkdir -p generated
    cp "$scaffold/generated/rexglue.cmake" generated/rexglue.cmake
fi

configure() {
    cmake --preset "$platform-release" -G 'Unix Makefiles' \
        -DCMAKE_PREFIX_PATH="$sdk_root" -DREXGLUE_SDK_ROOT="$sdk_root" "$@"
}
configure "$@"
cmake --build "out/build/$platform-release" --target fable_2_codegen
# Reconfigure after code generation so CMake sees the new sources.cmake.
configure "$@"
cmake --build "out/build/$platform-release" --target fable_2_app \
    --parallel "${FABLE2_BUILD_JOBS:-4}"
if [[ "$platform" == mac-arm64 && "${FABLE2_MACOS_TERRAIN_FIX:-1}" != 0 ]]; then
    python3 tools/macos_terrain_fix.py "out/build/$platform-release/librexgpu-xenos.dylib"
fi
echo "Built: $project_root/out/build/$platform-release/Fable II.app"
