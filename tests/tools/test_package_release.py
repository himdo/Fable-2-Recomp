import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
from types import SimpleNamespace
import zipfile

import pytest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("package_release", ROOT / "tools/package_release.py")
packager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packager)


@pytest.fixture
def inputs(tmp_path):
    """A fake repo, launcher and build folder, with everything packaging needs and some it must leave out."""
    folders = SimpleNamespace(base=tmp_path, root=tmp_path / "repo", build=tmp_path / "build",
                              launcher=tmp_path / "launcher/Fable2Launcher.exe", dist=tmp_path / "dist")
    for path, text in {
        folders.root / "README.md": "readme",
        folders.root / "thirdparty/XenosTiling/LICENSE.txt": "xenos tiling licence",
        folders.root / "config/fable2_config.toml": "repo config",
        folders.root / "assets/fonts/Google Sans/GoogleSans.ttf": "keycap font",
        folders.root / "assets/fonts/Google Sans/OFL.txt": "font licence",
        folders.launcher: "launcher",
        folders.build / "fable_2.exe": "game",
        folders.build / "fable_2.pdb": "symbols",
        folders.build / "rexruntime.dll": "runtime",
        folders.build / "rexruntime.pdb": "symbols",
        folders.build / "rexgpu-xenos.dll": "plugin",
        folders.build / "rexgpu-xenos.pdb": "symbols",
        folders.build / "fable2_build.json": '{"profiles": ["goty"]}',
        folders.build / "fable2_config.toml": "my own keybinds",
        folders.build / "default.xex": "game code",
        folders.build / "saves/slot1.sav": "save",
        folders.build / "logs/fable_2_001.log": "log",
        folders.build / "data/dir.manifest": "manifest",
        folders.build / "data/art.pak": "game data",
        folders.build / "data/scripts/quests/quest.lua": "game script",
        folders.build / "data/scripts/recomp/F5.lua": "recomp script",
    }.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
    return folders


def package(inputs):
    return packager.package(inputs.root, inputs.build, inputs.launcher, inputs.dist, "1.6.0")


def fake_package(calls):
    """A stand-in for package() that records what main() passes it instead of building zips."""
    def record(root, build, launcher, output, version):
        calls.append(SimpleNamespace(root=root, build=build, launcher=launcher, output=output, version=version))
        return [Path("release.zip")]
    return record


def fake_git(commands, output):
    """A stand-in for subprocess.check_output that records the command and prints `output`."""
    def run(command, **kwargs):
        commands.append(command)
        return output
    return run


def describe_package():
    # --- Happy flow -------------------------------------------------------

    def test_should_put_only_the_listed_files_in_the_release_zip(inputs):
        release, _ = package(inputs)

        with zipfile.ZipFile(release) as archive:
            names = set(archive.namelist())
            config = archive.read("Fable 2 1.6.0/fable2_config.toml").decode()
            build_info = json.loads(archive.read("Fable 2 1.6.0/fable2_build.json"))

        assert names == {f"Fable 2 1.6.0/{name}" for name in (
            "fable_2.exe", "rexruntime.dll", "rexgpu-xenos.dll", "fable2_build.json", "Fable2Launcher.exe",
            "README.md", "licenses/XenosTiling-LICENSE.txt", "fable2_config.toml", "data/scripts/recomp/F5.lua",
            "assets/fonts/Google Sans/GoogleSans.ttf", "assets/fonts/Google Sans/OFL.txt")}
        assert config == "repo config"
        assert build_info == {"profiles": ["goty"], "version": "1.6.0"}

    def test_should_put_the_symbols_in_their_own_zip(inputs):
        _, symbols = package(inputs)

        with zipfile.ZipFile(symbols) as archive:
            names = archive.namelist()

        assert len(names) == 3
        assert all(name.endswith(".pdb") for name in names)

    def test_should_write_checksums_that_match_the_zips(inputs):
        archives = package(inputs)

        expected = "".join(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n" for path in archives)
        assert (inputs.dist / "SHA256SUMS").read_text() == expected

    # --- Bad flow ---------------------------------------------------------

    def test_should_stop_when_a_file_is_missing(inputs):
        (inputs.build / "rexruntime.dll").unlink()

        with pytest.raises(SystemExit, match="rexruntime.dll"):
            package(inputs)

        assert not inputs.dist.exists()

    def test_should_stop_when_the_recomp_lua_scripts_are_missing(inputs):
        # The game's own scripts under data/scripts/quests don't count: only the recomp's do.
        (inputs.build / "data/scripts/recomp/F5.lua").unlink()

        with pytest.raises(SystemExit, match="no Lua scripts"):
            package(inputs)

        assert not inputs.dist.exists()

    def test_should_stop_when_symbols_are_missing(inputs):
        # A release without its symbols can't be debugged later, so packaging stops.
        (inputs.build / "fable_2.pdb").unlink()

        with pytest.raises(SystemExit, match="fable_2.pdb"):
            package(inputs)

        assert not inputs.dist.exists()

    def test_should_stop_when_the_font_folder_is_missing(inputs):
        # The keycap prompts read the bundled font at runtime, so a release without it is broken.
        shutil.rmtree(inputs.root / "assets/fonts")

        with pytest.raises(SystemExit, match="missing assets/fonts"):
            package(inputs)

        assert not inputs.dist.exists()

    def test_should_stop_when_the_font_folder_is_empty(inputs):
        # Only an empty "Google Sans" subfolder is left: no font file to ship.
        (inputs.root / "assets/fonts/Google Sans/GoogleSans.ttf").unlink()
        (inputs.root / "assets/fonts/Google Sans/OFL.txt").unlink()

        with pytest.raises(SystemExit, match="assets/fonts is empty"):
            package(inputs)

        assert not inputs.dist.exists()

    def test_should_stop_when_the_repo_config_is_missing(inputs):
        # The build folder's copy may hold a developer's own settings, so it must never stand in.
        (inputs.root / "config/fable2_config.toml").unlink()

        with pytest.raises(SystemExit, match="fable2_config.toml"):
            package(inputs)

        assert not inputs.dist.exists()

    def test_should_stop_when_the_launcher_is_missing(inputs):
        inputs.launcher.unlink()

        with pytest.raises(SystemExit, match="Fable2Launcher.exe"):
            package(inputs)

        assert not inputs.dist.exists()

    # --- Edge cases -------------------------------------------------------

    def test_should_remove_old_dist_files(inputs):
        # A zip left over from an earlier version must not end up next to the new release.
        old_zip = inputs.dist / "Fable_2_recomp_v1.5.0_windows.zip"
        old_zip.parent.mkdir()
        old_zip.write_text("old release")

        package(inputs)

        assert not old_zip.exists()

    def test_should_leave_the_build_folder_file_unstamped(inputs):
        # Only the zip's copy of fable2_build.json gets the version. The build folder's copy
        # stays as the build wrote it.
        package(inputs)

        assert (inputs.build / "fable2_build.json").read_text() == '{"profiles": ["goty"]}'


def describe_main():
    # --- Happy flow -------------------------------------------------------

    def test_should_package_the_version_given(monkeypatch, capsys):
        calls = []
        monkeypatch.setattr(packager, "package", fake_package(calls))
        monkeypatch.setattr("sys.argv", ["package_release.py", "--version", "1.7.0"])

        packager.main()

        assert calls[0].version == "1.7.0"
        assert calls[0].build == packager.ROOT / "out/build/win-amd64-release"
        assert calls[0].launcher == packager.ROOT / "out/tests/launcher-build/Fable2Launcher.exe"
        assert calls[0].output == packager.ROOT / "out/dist"
        assert "Packaged release.zip" in capsys.readouterr().out

    def test_should_default_to_the_git_tag(monkeypatch):
        # build.cmd package runs the tool without --version, so this is how real releases get theirs.
        calls, commands = [], []
        monkeypatch.setattr(packager, "package", fake_package(calls))
        monkeypatch.setattr(packager.subprocess, "check_output", fake_git(commands, "v1.6.1-3-gabc1234\n"))
        monkeypatch.setattr("sys.argv", ["package_release.py"])

        packager.main()

        assert commands[0] == ["git", "-C", str(packager.ROOT), "describe", "--tags"]
        assert calls[0].version == "v1.6.1-3-gabc1234"
