"""Compile the material shaders (materials/src/*.hlsl) for Direct3D 12.

What a material shader is: the game's own shaders are Xbox 360 microcode that
the ReXGlue SDK translates to Direct3D 12 at run time. A material shader is
hand-written HLSL that the SDK loads in place of one of those translations, to
render that material better (see materials/README.md). This script turns the
HLSL sources into the compiled DXBC files the game loads.

Each source names the translations it replaces on a `// xe_modifications:`
line - one guest shader can be translated several times ("modifications":
different interpolator layouts, early depth...), and each needs its own
compiled variant. Every listed modification is compiled with
XE_MODIFICATION_INDEX set to its position on that line, into

    materials/d3d12/<HASH>_<MODIFICATION>.dxbc

The build copies materials/ next to the game's executable, where the SDK finds
them (or set the `material_shaders_path` cvar). Rebuild after editing a source,
then reload in game: F6 > Materials > "Reload from disk".

Usage:

    python tools/build_material_shaders.py                  # every shader
    python tools/build_material_shaders.py 8D900846800943C8 # just these

Needs FXC, the Windows SDK's HLSL compiler (shader model 5.1). It is found on
PATH: run from a "Developer Command Prompt / PowerShell for VS" (or after
vcvars64.bat), which puts the Windows SDK's bin folder on PATH, or pass
--fxc <path to fxc.exe>.
"""
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ROOT / "materials" / "src"
OUTPUT = ROOT / "materials" / "d3d12"


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--fxc", default="fxc",
                        help="the FXC compiler: a path, or a name looked up on PATH "
                             "(default: fxc)")
    parser.add_argument("--sources", type=Path, default=SOURCES,
                        help="folder of the .hlsl sources (default: materials/src)")
    parser.add_argument("--output", type=Path, default=OUTPUT,
                        help="folder for the .dxbc files (default: materials/d3d12)")
    parser.add_argument("names", nargs="*", help="shader hashes to build (default: all)")
    args = parser.parse_args()
    fxc = shutil.which(args.fxc)
    if not fxc:
        raise SystemExit(f"{args.fxc} not found: run from a Visual Studio developer shell "
                         "(it puts the Windows SDK's fxc.exe on PATH) or pass --fxc")
    args.output.mkdir(parents=True, exist_ok=True)
    failed = False
    for source in sorted(args.sources.glob("*.hlsl")):
        name = source.stem.upper()
        if args.names and name not in (n.upper() for n in args.names):
            continue
        # Which translations this source replaces.
        match = re.search(r"^// xe_modifications:(.*)$", source.read_text(), re.MULTILINE)
        if not match:
            print(f"{source.name}: no `// xe_modifications:` line", file=sys.stderr)
            failed = True
            continue
        for index, modification in enumerate(match.group(1).split()):
            output = args.output / f"{name}_{modification.upper()}.dxbc"
            result = subprocess.run(
                [fxc, "/nologo", "/O3", "/T", "ps_5_1", "/E", "main",
                 # The translator binds textures and samplers as unbounded
                 # (bindless) descriptor arrays; the replacement must match.
                 "/enable_unbounded_descriptor_tables",
                 # Selects the inputs of this modification in the source.
                 f"/DXE_MODIFICATION_INDEX={index}", "/Fo", str(output), str(source)],
                capture_output=True, text=True)
            if result.returncode != 0:
                print(result.stdout + result.stderr, file=sys.stderr)
                print(f"failed: {source.name} ({modification})", file=sys.stderr)
                failed = True
                continue
            print(f"built {output}")
    if failed:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
