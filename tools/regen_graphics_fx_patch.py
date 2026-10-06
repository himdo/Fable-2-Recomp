"""Regenerate thirdparty/rexglue-sdk-graphics-fx.patch from the SDK checkout.

The scene effects (and the GPU frame logger) live in the SDK as working-tree
changes; prepare_runtime_sdk.py re-applies this patch onto a fresh checkout.
Run this after every edit to the files below, or prepare will reject the stale
patch as "neither baked in nor applicable".

    python tools/regen_graphics_fx_patch.py
"""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / "thirdparty" / "rexglue-sdk"
PATCH = ROOT / "thirdparty" / "rexglue-sdk-graphics-fx.patch"

# Git pathspecs (globs are matched by git).
PATHS = (
    # The in-game graphics enhancements menu (F6).
    "include/rex/rex_app.h",
    "include/rex/ui/overlay/effects_overlay.h",
    "src/system/rexruntime.def",
    "src/ui/CMakeLists.txt",
    "src/ui/overlay/effects_overlay.cpp",
    "src/ui/rex_app.cpp",
    # The effects: shared core, then the backends.
    "include/rex/graphics/pipeline/render_target/cache.h",
    "include/rex/graphics/pipeline/scene_effects.h",
    "src/graphics/pipeline/scene_effects.cpp",
    "include/rex/graphics/d3d12/command_processor.h",
    "include/rex/graphics/d3d12/render_target_cache.h",
    "include/rex/graphics/d3d12/scene_effects.h",
    "include/rex/graphics/vulkan/command_processor.h",
    "include/rex/graphics/vulkan/render_target_cache.h",
    "include/rex/graphics/vulkan/scene_effects.h",
    "src/graphics/CMakeLists.txt",
    "src/graphics/command_processor.cpp",
    "src/graphics/d3d12/command_processor.cpp",
    "src/graphics/d3d12/scene_effects.cpp",
    "src/graphics/vulkan/command_processor.cpp",
    "src/graphics/vulkan/scene_effects.cpp",
    "src/graphics/shaders/scene_fx",
    "src/graphics/shaders/bytecode/d3d12_5_1/scene_fx_*.h",
    "src/graphics/shaders/vulkan_spirv/scene_fx_*.h",
    # Material shaders: loading the replacements, their bindings and settings.
    "include/rex/graphics/pipeline/material_shaders.h",
    "src/graphics/pipeline/material_shaders.cpp",
    "include/rex/graphics/pipeline/shader/dxbc_translator.h",
    "src/graphics/pipeline/shader/dxbc_translator.cpp",
    "include/rex/graphics/pipeline/shader/shader.h",
    "include/rex/graphics/d3d12/pipeline_cache.h",
    "src/graphics/d3d12/pipeline_cache.cpp",
)


def git(*args, capture=False):
    return subprocess.run(["git", "-C", str(SDK), *args], check=True, text=True,
                          capture_output=capture).stdout


def main():
    # New files only show up in a diff once git knows about them.
    untracked = git("ls-files", "--others", "--exclude-standard", "--", *PATHS,
                    capture=True).split()
    if untracked:
        git("add", "--intent-to-add", "--", *untracked)
    git("diff", "--binary", f"--output={PATCH}", "--", *PATHS)
    files = git("diff", "--name-only", "--", *PATHS, capture=True).split()
    print(f"wrote {PATCH.relative_to(ROOT)} ({len(files)} files)")


if __name__ == "__main__":
    main()
