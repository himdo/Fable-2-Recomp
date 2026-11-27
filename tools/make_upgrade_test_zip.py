#!/usr/bin/env python3
"""Turn a published release zip into a test release for the launcher's in-place upgrade.

Usage: python tools/make_upgrade_test_zip.py Fable_2_recomp_v1.6.1_windows.zip --version 9.0.0
           [--replace Fable2Launcher.exe=out/tests/launcher-debug/Fable2Launcher.exe]
           [--drop data/scripts/recomp/F5.lua] [--output out/upgrade-test]

The result is packaged the way tools/package_release.py packages a release (same folder
name, version stamp and file list), so the launcher's upgrade can be tried without
building the game or publishing anything. See tests/README.md.
"""
import argparse
import importlib.util
from pathlib import Path
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("package_release", Path(__file__).with_name("package_release.py"))
packager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packager)


def repackage(source, output, version, replace=None, drop=()):
    with tempfile.TemporaryDirectory() as temp:
        with zipfile.ZipFile(source) as archive:
            archive.extractall(temp)
        # Release zips hold one top folder ("Fable 2 1.6.1").
        tops = list(Path(temp).iterdir())
        root = tops[0] if len(tops) == 1 and tops[0].is_dir() else Path(temp)
        files = {path.relative_to(root).as_posix(): path for path in sorted(root.rglob("*")) if path.is_file()}
        if packager.BUILD_INFO not in files:
            raise SystemExit(f"make_upgrade_test_zip: {source} is not a release zip")
        for name in drop:
            if name not in files:
                raise SystemExit(f"make_upgrade_test_zip: {name} is not in {source}")
            del files[name]
        for name, path in (replace or {}).items():
            files[name] = packager.require(Path(path))
        files[packager.BUILD_INFO] = packager.stamped_build_info(files[packager.BUILD_INFO], version, files)
        output.mkdir(parents=True, exist_ok=True)
        target = output / f"Fable_2_recomp_v{version}_windows.zip"
        packager.write_zip(target, f"Fable 2 {version}", files)
    return target


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("source", type=Path, help="a release zip downloaded from GitHub")
    parser.add_argument("--version", required=True, help="the version the test release claims to be")
    parser.add_argument("--replace", action="append", default=[], metavar="NAME=FILE",
                        help="put FILE in the zip as NAME, e.g. a freshly built launcher")
    parser.add_argument("--drop", action="append", default=[], metavar="NAME",
                        help="leave NAME out, as a release that no longer ships it would")
    parser.add_argument("--output", type=Path, default=ROOT / "out/upgrade-test")
    args = parser.parse_args()
    replace = dict(item.split("=", 1) for item in args.replace)
    print(f"Packaged {repackage(args.source, args.output, args.version, replace, args.drop)}")


if __name__ == "__main__":
    main()
