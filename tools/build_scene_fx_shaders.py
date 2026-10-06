"""Compile the SDK's scene-effect shaders for both GPU backends.

Sources: thirdparty/rexglue-sdk/src/graphics/shaders/scene_fx/*.hlsl
Outputs, as C headers the backends #include:
  .../shaders/bytecode/d3d12_5_1/<name>.h   DXBC from FXC (Windows SDK)
  .../shaders/vulkan_spirv/<name>.h         SPIR-V from glslangValidator

    python tools/build_scene_fx_shaders.py [--glslang PATH] [--fxc PATH]

glslangValidator: build it from the SDK's own glslang (CMake target
glslangValidator, compiled with MSVC cl) - the default path below.
"""
import argparse
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SHADERS = ROOT / "thirdparty" / "rexglue-sdk" / "src" / "graphics" / "shaders"
SOURCES = SHADERS / "scene_fx"
DXBC_OUT = SHADERS / "bytecode" / "d3d12_5_1"
SPIRV_OUT = SHADERS / "vulkan_spirv"

# (output name, source, stage, extra defines)
VARIANTS = (
    ("scene_fx_depth_copy_cs", "scene_fx_depth_copy_cs.hlsl", "cs", ()),
    ("scene_fx_depth_copy_msaa_cs", "scene_fx_depth_copy_cs.hlsl", "cs", ("FX_MSAA",)),
    ("scene_fx_shadow_copy_cs", "scene_fx_depth_copy_cs.hlsl", "cs", ("FX_RAW",)),
    ("scene_fx_ao_prefilter_cs", "scene_fx_ao_prefilter_cs.hlsl", "cs", ()),
    ("scene_fx_ao_cs", "scene_fx_ao_cs.hlsl", "cs", ()),
    ("scene_fx_ao_gi_cs", "scene_fx_ao_cs.hlsl", "cs", ("FX_GI",)),
    ("scene_fx_color_capture_cs", "scene_fx_color_capture_cs.hlsl", "cs", ()),
    ("scene_fx_color_capture_msaa_cs", "scene_fx_color_capture_cs.hlsl", "cs", ("FX_MSAA",)),
    ("scene_fx_color_copy_cs", "scene_fx_color_capture_cs.hlsl", "cs", ("FX_FULL",)),
    ("scene_fx_color_copy_msaa_cs", "scene_fx_color_capture_cs.hlsl", "cs",
     ("FX_FULL", "FX_MSAA")),
    ("scene_fx_contact_shadows_cs", "scene_fx_contact_shadows_cs.hlsl", "cs", ()),
    ("scene_fx_reflections_cs", "scene_fx_reflections_cs.hlsl", "cs", ()),
    ("scene_fx_ao_blur_cs", "scene_fx_ao_blur_cs.hlsl", "cs", ()),
    ("scene_fx_blur_rgba_cs", "scene_fx_ao_blur_cs.hlsl", "cs", ("FX_RGBA",)),
    ("scene_fx_sky_color_cs", "scene_fx_sky_color_cs.hlsl", "cs", ()),
    ("scene_fx_sky_color_msaa_cs", "scene_fx_sky_color_cs.hlsl", "cs", ("FX_MSAA",)),
    ("scene_fx_volumetric_cs", "scene_fx_volumetric_cs.hlsl", "cs", ()),
    ("scene_fx_temporal_cs", "scene_fx_temporal_cs.hlsl", "cs", ()),
    ("scene_fx_fullscreen_vs", "scene_fx_fullscreen_vs.hlsl", "vs", ()),
    ("scene_fx_composite_ps", "scene_fx_composite_ps.hlsl", "ps", ()),
    ("scene_fx_image_ps", "scene_fx_image_ps.hlsl", "ps", ()),
)
GLSLANG_STAGE = {"cs": "comp", "vs": "vert", "ps": "frag"}


def find_fxc():
    kits = Path(r"C:\Program Files (x86)\Windows Kits\10\bin")
    candidates = sorted(kits.glob("10.*/x64/fxc.exe"))
    return candidates[-1] if candidates else None


def run(command):
    result = subprocess.run([str(part) for part in command], capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stdout + result.stderr, file=sys.stderr)
        raise SystemExit(f"failed: {' '.join(str(part) for part in command)}")


def normalize_line_endings(path):
    # FXC writes CRLF; the SDK keeps LF (and the patch must match the tree).
    data = path.read_bytes()
    path.write_bytes(data.replace(b"\r\n", b"\n"))


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--fxc", type=Path, default=find_fxc())
    parser.add_argument("--glslang", type=Path,
                        default=ROOT / "out" / "build" / "glslang-msvc" / "StandAlone" /
                        "glslangValidator.exe")
    args = parser.parse_args()
    if not args.fxc or not args.fxc.is_file():
        raise SystemExit("fxc.exe not found (Windows SDK); pass --fxc")
    if not args.glslang.is_file():
        raise SystemExit(f"glslangValidator not found at {args.glslang}; pass --glslang")

    for name, source, stage, defines in VARIANTS:
        source_path = SOURCES / source
        run([args.fxc, "/nologo", "/O3", "/T", f"{stage}_5_1", "/E", "main",
             *[f"/D{define}=1" for define in defines],
             "/Vn", name, "/Fh", DXBC_OUT / f"{name}.h", source_path])
        run([args.glslang, "-D", "-V", "--target-env", "vulkan1.0",
             "-S", GLSLANG_STAGE[stage], "-e", "main", "-DSCENE_FX_VULKAN=1",
             *[f"-D{define}=1" for define in defines],
             "--vn", name, "-o", SPIRV_OUT / f"{name}.h", source_path])
        normalize_line_endings(DXBC_OUT / f"{name}.h")
        normalize_line_endings(SPIRV_OUT / f"{name}.h")
        print(f"built {name}")


if __name__ == "__main__":
    main()
