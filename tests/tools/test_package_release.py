import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("package_release", ROOT / "tools/package_release.py")
packager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packager)


class PackageReleaseTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.base = Path(temp.name)
        self.root, self.build = self.base / "repo", self.base / "build"
        self.launcher = self.base / "launcher/Fable2Launcher.exe"
        for path, text in {
            self.root / "README.md": "readme",
            self.root / "thirdparty/XenosTiling/LICENSE.txt": "xenos tiling licence",
            self.root / "config/fable2_config.toml": "repo config",
            self.launcher: "launcher",
            self.build / "fable_2.exe": "game",
            self.build / "fable_2.pdb": "symbols",
            self.build / "rexruntime.dll": "runtime",
            self.build / "rexruntime.pdb": "symbols",
            self.build / "rexgpu-xenos.dll": "plugin",
            self.build / "rexgpu-xenos.pdb": "symbols",
            self.build / "fable2_build.json": '{"profiles": ["goty"]}',
            self.build / "fable2_config.toml": "my own keybinds",
            self.build / "default.xex": "game code",
            self.build / "saves/slot1.sav": "save",
            self.build / "logs/fable_2_001.log": "log",
            self.build / "data/dir.manifest": "manifest",
            self.build / "data/art.pak": "game data",
            self.build / "data/scripts/quests/quest.lua": "game script",
            self.build / "data/scripts/recomp/F5.lua": "recomp script",
        }.items():
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)

    def package(self):
        return packager.package(self.root, self.build, self.launcher, self.base / "dist", "1.6.0")

    def test_release_zip_contains_only_the_listed_files(self):
        release, _ = self.package()
        with zipfile.ZipFile(release) as archive:
            names = set(archive.namelist())
            config = archive.read("Fable 2 1.6.0/fable2_config.toml").decode()
            build_info = json.loads(archive.read("Fable 2 1.6.0/fable2_build.json"))
        self.assertEqual(names, {f"Fable 2 1.6.0/{name}" for name in (
            "fable_2.exe", "rexruntime.dll", "rexgpu-xenos.dll", "fable2_build.json", "Fable2Launcher.exe",
            "README.md", "licenses/XenosTiling-LICENSE.txt", "fable2_config.toml", "data/scripts/recomp/F5.lua")})
        self.assertEqual(config, "repo config")
        self.assertEqual(build_info, {"profiles": ["goty"], "version": "1.6.0"})

    def test_symbols_are_separate(self):
        _, symbols = self.package()
        with zipfile.ZipFile(symbols) as archive:
            self.assertEqual(len(archive.namelist()), 3)
            self.assertTrue(all(name.endswith(".pdb") for name in archive.namelist()))

    def test_checksums_match_the_zips(self):
        archives = self.package()
        expected = "".join(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n" for path in archives)
        self.assertEqual((self.base / "dist/SHA256SUMS").read_text(), expected)

    def test_missing_file_fails(self):
        (self.build / "rexruntime.dll").unlink()
        with self.assertRaisesRegex(SystemExit, "rexruntime.dll"):
            self.package()
        self.assertFalse((self.base / "dist").exists())

    def test_missing_lua_scripts_fails(self):
        # The game's own scripts under data/scripts/quests don't count: only the recomp's do.
        (self.build / "data/scripts/recomp/F5.lua").unlink()

        with self.assertRaisesRegex(SystemExit, "no Lua scripts"):
            self.package()
        self.assertFalse((self.base / "dist").exists())

    def test_missing_symbols_fail(self):
        # A release without its symbols can't be debugged later, so packaging stops.
        (self.build / "fable_2.pdb").unlink()

        with self.assertRaisesRegex(SystemExit, "fable_2.pdb"):
            self.package()
        self.assertFalse((self.base / "dist").exists())

    def test_old_dist_files_are_removed(self):
        # A zip left over from an earlier version must not end up next to the new release.
        old_zip = self.base / "dist/Fable_2_recomp_v1.5.0_windows.zip"
        old_zip.parent.mkdir()
        old_zip.write_text("old release")
        self.package()

        self.assertFalse(old_zip.exists())

    def test_packaging_leaves_the_build_folder_file_unstamped(self):
        # Only the zip's copy of fable2_build.json gets the version. The build folder's copy
        # stays as the build wrote it.
        self.package()
        build_info = (self.build / "fable2_build.json").read_text()

        self.assertEqual(build_info, '{"profiles": ["goty"]}')


if __name__ == "__main__":
    unittest.main()
