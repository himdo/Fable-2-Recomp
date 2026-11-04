#!/usr/bin/env python3
"""Assemble the release zip, symbols zip and SHA256SUMS from a finished Release build.

Usage: python tools/package_release.py [--version 1.6.0]
Only the files listed here are packaged, so game content and local saves stay out.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
BUILD_FILES = ("fable_2.exe", "rexruntime.dll", "rexgpu-xenos.dll", "fable2_build.json")
SYMBOL_FILES = ("fable_2.pdb", "rexruntime.pdb", "rexgpu-xenos.pdb")


def require(path):
    if not path.is_file():
        raise SystemExit(f"package_release: missing {path}")
    return path


def release_files(root, build, launcher):
    files = {name: require(build / name) for name in BUILD_FILES}
    files["Fable2Launcher.exe"] = require(launcher)
    files["README.md"] = require(root / "README.md")
    # From the repo: the build folder's copy may hold a developer's own settings.
    files["fable2_config.toml"] = require(root / "config/fable2_config.toml")
    # dir.manifest stays out: the real one comes from the disc, and the game lists these scripts itself.
    scripts = sorted((build / "data/scripts/recomp").glob("*.lua"))
    if not scripts:
        raise SystemExit("package_release: no Lua scripts in data/scripts/recomp")
    for script in scripts:
        files[f"data/scripts/recomp/{script.name}"] = script
    return files


def write_zip(path, folder, files):
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as archive:
        for name, source in files.items():
            if isinstance(source, str):
                archive.writestr(f"{folder}/{name}", source)
            else:
                archive.write(source, f"{folder}/{name}")


def stamped_build_info(source, version):
    info = json.loads(source.read_text(encoding="utf8"))
    info["version"] = version
    return json.dumps(info, indent=2) + "\n"


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def package(root, build, launcher, output, version):
    files = release_files(root, build, launcher)
    files["fable2_build.json"] = stamped_build_info(files["fable2_build.json"], version)
    symbols = {name: require(build / name) for name in SYMBOL_FILES}
    shutil.rmtree(output, ignore_errors=True)
    output.mkdir(parents=True)
    archives = [output / f"Fable_2_recomp_v{version}_windows.zip",
                output / f"Fable_2_recomp_v{version}_symbols.zip"]
    write_zip(archives[0], f"Fable 2 {version}", files)
    write_zip(archives[1], f"Fable 2 {version} symbols", symbols)
    sums = "".join(f"{sha256(path)}  {path.name}\n" for path in archives)
    (output / "SHA256SUMS").write_text(sums, encoding="ascii", newline="\n")
    return archives


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", help="defaults to git describe --tags")
    args = parser.parse_args()
    version = args.version or subprocess.check_output(
        ["git", "-C", str(ROOT), "describe", "--tags"], text=True).strip()
    archives = package(ROOT, ROOT / "out/build/win-amd64-release",
                       ROOT / "out/tests/launcher-build/Fable2Launcher.exe",
                       ROOT / "out/dist", version)
    for path in archives:
        print(f"Packaged {path}")


if __name__ == "__main__":
    main()
