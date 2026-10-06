"""Compile the material shaders (materials/src/*.hlsl) for Direct3D 12.

Each source lists the translation modifications it replaces in a
`// xe_modifications:` line; each is compiled with XE_MODIFICATION_INDEX set
to its position into
  materials/d3d12/<HASH>_<MODIFICATION>.dxbc
which the game loads from <executable folder>/materials (the build copies the
folder there). See materials/README.md.

    python tools/build_material_shaders.py [--fxc PATH] [names...]
"""
import argparse
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ROOT / "materials" / "src"
OUTPUT = ROOT / "materials" / "d3d12"


def find_fxc():
    kits = Path(r"C:\Program Files (x86)\Windows Kits\10\bin")
    candidates = sorted(kits.glob("10.*/x64/fxc.exe"))
    return candidates[-1] if candidates else None


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--fxc", type=Path, default=find_fxc())
    parser.add_argument("--sources", type=Path, default=SOURCES,
                        help="folder of the .hlsl sources (default: materials/src)")
    parser.add_argument("--output", type=Path, default=OUTPUT,
                        help="folder for the .dxbc files (default: materials/d3d12)")
    parser.add_argument("names", nargs="*", help="shader hashes to build (default: all)")
    args = parser.parse_args()
    if not args.fxc or not args.fxc.is_file():
        raise SystemExit("fxc.exe not found (Windows SDK); pass --fxc")
    args.output.mkdir(parents=True, exist_ok=True)
    failed = False
    for source in sorted(args.sources.glob("*.hlsl")):
        name = source.stem.upper()
        if args.names and name not in (n.upper() for n in args.names):
            continue
        match = re.search(r"^// xe_modifications:(.*)$", source.read_text(), re.MULTILINE)
        if not match:
            print(f"{source.name}: no `// xe_modifications:` line", file=sys.stderr)
            failed = True
            continue
        for index, modification in enumerate(match.group(1).split()):
            output = args.output / f"{name}_{modification.upper()}.dxbc"
            result = subprocess.run(
                [str(args.fxc), "/nologo", "/O3", "/T", "ps_5_1", "/E", "main",
                 # The translator's bindless descriptor arrays.
                 "/enable_unbounded_descriptor_tables",
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
