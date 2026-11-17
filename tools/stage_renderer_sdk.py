"""Finish an SDK installed from the fork: add PDBs and record what was built.

Usage: python tools/stage_renderer_sdk.py SDK_SOURCE OUTPUT
       [--configuration Release|Debug] [--codegen-stamp PATH]
OUTPUT is the prefix passed to cmake --install. The native application must be
rebuilt against it. Game dumps and saves are never packaged.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("source", "output"):
        parser.add_argument(name, type=Path)
    parser.add_argument("--configuration", choices=("Release", "Debug"), default="Release")
    parser.add_argument("--codegen-stamp", type=Path)
    args = parser.parse_args()
    source, output = args.source.resolve(), args.output.resolve()
    native = source / "out/win-amd64"
    suffix = "d" if args.configuration == "Debug" else ""
    runtime = f"rexruntime{suffix}"
    plugin = f"rexgpu-xenos{suffix}"
    required = [output / "bin" / f"{runtime}.dll", output / "bin" / f"{plugin}.dll",
                output / "lib" / f"{runtime}.lib", output / "lib" / f"{plugin}.lib",
                output / "include/rex/version.h", output / "lib/cmake/rexglue/rexglueConfig.cmake"]
    # Only Release installs the codegen tool.
    if args.configuration == "Release":
        required.append(output / "bin/rexglue.exe")
    for path in required:
        if not path.is_file():
            raise SystemExit(f"Missing build input: {path}")
    # Carry the PDBs (when present) so the host build can stage them next to
    # the exe for debugger/profiler symbolization.
    for name in (f"{runtime}.pdb", f"{plugin}.pdb"):
        if (native / name).is_file():
            shutil.copy2(native / name, output / "bin" / name)
    revision = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    provenance = {
        "configuration": args.configuration,
        "sdk_commit": revision,
        "note": "Native headers, integration sources and DLLs must stay matched.",
        "runtime_sha256": hashlib.sha256((output / "bin" / f"{runtime}.dll").read_bytes()).hexdigest(),
        "plugin_sha256": hashlib.sha256((output / "bin" / f"{plugin}.dll").read_bytes()).hexdigest(),
    }
    provenance_path = output / "renderer-sdk-provenance.json"
    if args.configuration == "Release":
        tool_hash = hashlib.sha256((output / "bin/rexglue.exe").read_bytes()).hexdigest()
        provenance["codegen_tool_sha256"] = tool_hash
        # The tool's up-to-date check doesn't cover its own code, so a new tool
        # has to clear it or the old generated code would be kept.
        previous = json.loads(provenance_path.read_text()) if provenance_path.is_file() else {}
        if args.codegen_stamp and previous.get("codegen_tool_sha256") != tool_hash:
            args.codegen_stamp.unlink(missing_ok=True)
    provenance_path.write_text(json.dumps(provenance, indent=2), encoding="utf-8")
    print(f"Matched native SDK staged at {output}")


if __name__ == "__main__":
    main()
