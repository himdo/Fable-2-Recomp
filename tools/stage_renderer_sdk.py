"""Stage matched fork headers/runtime/plugin with an official codegen package.

Usage: python tools/stage_renderer_sdk.py OFFICIAL_SDK SDK_SOURCE BUILD_DIR OUTPUT
       [--configuration Release|Debug] [--platform win-amd64|mac-arm64|linux-amd64|...]
The native application must be rebuilt against OUTPUT. Codegen continues using
OFFICIAL_SDK/bin/rexglue.exe with its original runtime, not OUTPUT/bin/rexglue.exe.
Only SDK files are copied; game dumps and saves are never packaged.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("official", "source", "build", "output"):
        parser.add_argument(name, type=Path)
    parser.add_argument("--configuration", choices=("Release", "Debug"), default="Release")
    parser.add_argument("--platform", default="win-amd64")
    args = parser.parse_args()
    official, source, build, output = (getattr(args, name).resolve()
        for name in ("official", "source", "build", "output"))
    if any(output == path or path.is_relative_to(output) or output.is_relative_to(path)
           for path in (official, source, build)):
        raise SystemExit("Output must be separate from every input tree")
    native = source / "out" / args.platform
    windows = args.platform.startswith("win-")
    suffix = "d" if args.configuration == "Debug" else ""
    runtime = f"rexruntime{suffix}"
    plugin = f"rexgpu-xenos{suffix}"
    if windows:
        runtime_file, plugin_file, tool = f"{runtime}.dll", f"{plugin}.dll", "bin/rexglue.exe"
    else:
        ext = "dylib" if args.platform.startswith("mac-") else "so"
        runtime_file, plugin_file, tool = f"lib{runtime}.{ext}", f"lib{plugin}.{ext}", "bin/rexglue"
    required = [native / runtime_file, native / plugin_file, build / "include/rex/version.h", official / tool]
    if windows:
        required += [native / f"{runtime}.lib", native / f"{plugin}.lib"]
    for path in required:
        if not path.is_file():
            raise SystemExit(f"Missing build input: {path}")
    shutil.copytree(official, output, dirs_exist_ok=True)
    shutil.copytree(source / "include/rex", output / "include/rex", dirs_exist_ok=True)
    shutil.copy2(build / "include/rex/version.h", output / "include/rex/version.h")
    shutil.copytree(source / "thirdparty/sdl3/include/SDL3", output / "include/SDL3", dirs_exist_ok=True)
    for path in native.glob("*.lib"):
        shutil.copy2(path, output / "lib" / path.name)
    # Shared libraries live in bin/ on Windows and lib/ elsewhere. The macOS
    # Vulkan loader and MoltenVK are rebuilt from the SDK's own pins; keep them
    # matched with the plugin when the build produced them.
    shared = [runtime_file, plugin_file]
    if not windows:
        shared += [path.name for path in native.glob("lib*vulkan*.dylib")]
        shared += [path.name for path in native.glob("libMoltenVK.dylib")]
    for name in shared:
        shutil.copy2(native / name, output / ("bin" if windows else "lib") / name)
    # Carry the PDBs (when present) so the host build can stage them next to
    # the exe for debugger/profiler symbolization.
    for name in (f"{runtime}.pdb", f"{plugin}.pdb"):
        if (native / name).is_file():
            shutil.copy2(native / name, output / "bin" / name)
    for name in ("rex_app.cpp", "windowed_app_main_sdl.cpp"):
        shutil.copy2(source / "src/ui" / name, output / "share/rexglue" / name)
    revision = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    provenance = {
        "configuration": args.configuration,
        "sdk_commit": revision,
        "codegen_tool": str(official / tool),
        "note": "Native headers, integration sources and DLLs must stay matched. Official tool uses its own runtime.",
        "runtime_sha256": hashlib.sha256((native / runtime_file).read_bytes()).hexdigest(),
        "plugin_sha256": hashlib.sha256((native / plugin_file).read_bytes()).hexdigest(),
    }
    (output / "renderer-sdk-provenance.json").write_text(json.dumps(provenance, indent=2), encoding="utf-8")
    print(f"Matched native SDK staged at {output}")
    print(f"Configure host with -DFABLE2_CODEGEN_TOOL={official / tool}")


if __name__ == "__main__":
    main()
