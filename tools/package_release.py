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
BUILD_INFO = "fable2_build.json"
BUILD_FILES = ("fable_2.exe", "rexruntime.dll", "rexgpu-xenos.dll", BUILD_INFO)
SYMBOL_FILES = ("fable_2.pdb", "rexruntime.pdb", "rexgpu-xenos.pdb")
# Licences of third-party code that ships in the release, as <zip name>: <repo path>.
# - XenosTiling: Xenia's tiling code, copied into src/gui/xenos_tiling.h (fable_2.exe).
# - rexglue-sdk: rexruntime.dll and rexgpu-xenos.dll are built from it. It is
#   BSD-3-Clause (with the Xenia-derived code), which asks binary releases to
#   reproduce the notice. It lives in the SDK submodule.
# - toml++: compiled into fable_2.exe (src/core/fable2_config.cpp). MIT.
LICENSES = {
    "licenses/XenosTiling-LICENSE.txt": "thirdparty/XenosTiling/LICENSE.txt",
    "licenses/rexglue-sdk-LICENSE.txt": "thirdparty/rexglue-sdk/LICENSE",
    "licenses/tomlplusplus-LICENSE.txt": "thirdparty/tomlplusplus/LICENSE",
}
# The layout the launcher's in-place upgrade understands. Launchers already installed
# refuse a number they don't know and send the player to the release page instead, so
# bump it whenever an older launcher could no longer install the zip correctly.
UPGRADE_FORMAT = 1
# Files the player edits. An upgrade installs them only when they are missing.
USER_FILES = ("fable2_config.toml",)


def require(path):
    if not path.is_file():
        raise SystemExit(f"package_release: missing {path}")
    return path


def font_files(root):
    """Collect the bundled keycap font tree, laid out as CMake stages it next to
    the exe at <exe>/assets/fonts/... (read at runtime by
    src/gui/keycap_render.cpp). The OFL license ships alongside the .ttf, as the
    Open Font License requires.
    """
    fonts = root / "assets/fonts"
    if not fonts.is_dir():
        raise SystemExit("package_release: missing assets/fonts (bundled keycap font)")
    files = {p.relative_to(root).as_posix(): p for p in sorted(fonts.rglob("*")) if p.is_file()}
    if not files:
        raise SystemExit("package_release: assets/fonts is empty (missing bundled keycap font)")
    return files


def release_files(root, build, launcher):
    files = {name: require(build / name) for name in BUILD_FILES}
    files["Fable2Launcher.exe"] = require(launcher)
    files["README.md"] = require(root / "README.md")
    for name, source in LICENSES.items():
        files[name] = require(root / source)
    # From the repo: the build folder's copy may hold a developer's own settings.
    files["fable2_config.toml"] = require(root / "config/fable2_config.toml")
    # dir.manifest stays out: the real one comes from the disc, and the game lists these scripts itself.
    scripts = sorted((build / "data/scripts/recomp").glob("*.lua"))
    if not scripts:
        raise SystemExit("package_release: no Lua scripts in data/scripts/recomp")
    for script in scripts:
        files[f"data/scripts/recomp/{script.name}"] = script
    files.update(font_files(root))
    return files


def write_zip(path, folder, files):
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as archive:
        for name, source in files.items():
            if isinstance(source, str):
                archive.writestr(f"{folder}/{name}", source)
            else:
                archive.write(source, f"{folder}/{name}")


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def stamped_build_info(source, version, files):
    """The build info as it ships: the version, plus what the launcher's in-place upgrade
    needs to check a download and to tell this release's files from the player's own.
    The file can't hold its own hash, so it is the one file left out of the list.
    """
    info = json.loads(source.read_text(encoding="utf8"))
    info["version"] = version
    info["release"] = {
        "format": UPGRADE_FORMAT,
        "user_files": [name for name in USER_FILES if name in files],
        "files": {name: sha256(path) for name, path in sorted(files.items()) if name != BUILD_INFO},
    }
    return json.dumps(info, indent=2) + "\n"


def package(root, build, launcher, output, version):
    files = release_files(root, build, launcher)
    files[BUILD_INFO] = stamped_build_info(files[BUILD_INFO], version, files)
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
