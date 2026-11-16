import hashlib
import importlib.util
import json
from pathlib import Path
import zipfile

import pytest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("make_upgrade_test_zip", ROOT / "tools/make_upgrade_test_zip.py")
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


@pytest.fixture
def published(tmp_path):
    """A release zip as GitHub serves it: one top folder, and build info from before the file list existed."""
    path = tmp_path / "Fable_2_recomp_v1.6.1_windows.zip"
    with zipfile.ZipFile(path, "w") as archive:
        for name, text in {
            "fable_2.exe": "game",
            "Fable2Launcher.exe": "launcher 1.6.1",
            "fable2_build.json": '{"profiles": ["goty"], "version": "1.6.1"}',
            "fable2_config.toml": "default settings",
            "data/scripts/recomp/F5.lua": "f5",
            "data/scripts/recomp/stress_noop.lua": "noop",
        }.items():
            archive.writestr(f"Fable 2 1.6.1/{name}", text)
    return path


def read(path):
    with zipfile.ZipFile(path) as archive:
        return {name: archive.read(name) for name in archive.namelist()}


def describe_repackage():
    # --- Happy flow -------------------------------------------------------

    def test_should_package_the_release_under_the_new_version(published, tmp_path):
        test_zip = tool.repackage(published, tmp_path / "out", "9.0.0")

        contents = read(test_zip)
        build_info = json.loads(contents["Fable 2 9.0.0/fable2_build.json"])

        assert test_zip.name == "Fable_2_recomp_v9.0.0_windows.zip"
        assert set(contents) == {f"Fable 2 9.0.0/{name}" for name in (
            "fable_2.exe", "Fable2Launcher.exe", "fable2_build.json", "fable2_config.toml",
            "data/scripts/recomp/F5.lua", "data/scripts/recomp/stress_noop.lua")}
        assert build_info["version"] == "9.0.0"
        assert build_info["profiles"] == ["goty"]

    def test_should_add_the_file_list_the_launcher_checks(published, tmp_path):
        # Published 1.6.x zips have no list, and the launcher refuses a release without one.
        test_zip = tool.repackage(published, tmp_path / "out", "9.0.0")

        contents = read(test_zip)
        listed = json.loads(contents.pop("Fable 2 9.0.0/fable2_build.json"))["release"]

        assert listed["files"] == {name.removeprefix("Fable 2 9.0.0/"): hashlib.sha256(data).hexdigest()
                                   for name, data in contents.items()}
        assert listed["user_files"] == ["fable2_config.toml"]

    def test_should_swap_in_and_leave_out_the_given_files(published, tmp_path):
        # A different launcher makes the upgrade replace the one that is running, and a
        # dropped script makes it remove a file.
        launcher = tmp_path / "new-launcher.exe"
        launcher.write_text("launcher 9.0.1")

        test_zip = tool.repackage(published, tmp_path / "out", "9.0.1", {"Fable2Launcher.exe": launcher},
                                  ["data/scripts/recomp/stress_noop.lua"])

        contents = read(test_zip)
        listed = json.loads(contents["Fable 2 9.0.1/fable2_build.json"])["release"]["files"]

        assert contents["Fable 2 9.0.1/Fable2Launcher.exe"] == b"launcher 9.0.1"
        assert "Fable 2 9.0.1/data/scripts/recomp/stress_noop.lua" not in contents
        assert "data/scripts/recomp/stress_noop.lua" not in listed
        assert listed["Fable2Launcher.exe"] == hashlib.sha256(b"launcher 9.0.1").hexdigest()

    # --- Bad flow ---------------------------------------------------------

    def test_should_stop_when_a_file_to_leave_out_is_not_there(published, tmp_path):
        with pytest.raises(SystemExit, match="typo.lua"):
            tool.repackage(published, tmp_path / "out", "9.0.0", drop=["data/scripts/recomp/typo.lua"])

    def test_should_stop_when_a_file_to_swap_in_is_missing(published, tmp_path):
        with pytest.raises(SystemExit, match="no-such-launcher.exe"):
            tool.repackage(published, tmp_path / "out", "9.0.0",
                           {"Fable2Launcher.exe": tmp_path / "no-such-launcher.exe"})

    def test_should_stop_when_the_zip_is_not_a_release(tmp_path):
        other = tmp_path / "other.zip"
        with zipfile.ZipFile(other, "w") as archive:
            archive.writestr("notes.txt", "not a release")

        with pytest.raises(SystemExit, match="not a release zip"):
            tool.repackage(other, tmp_path / "out", "9.0.0")
