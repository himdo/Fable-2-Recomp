import hashlib
import importlib.util
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
            self.root / "config/fable2_config.toml": "repo config",
            self.launcher: "launcher",
            self.build / "fable_2.exe": "game",
            self.build / "fable_2.pdb": "symbols",
            self.build / "rexruntime.dll": "runtime",
            self.build / "rexruntime.pdb": "symbols",
            self.build / "rexgpu-xenos.dll": "plugin",
            self.build / "rexgpu-xenos.pdb": "symbols",
            self.build / "fable2_build.json": "{}",
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
        self.assertEqual(names, {f"Fable 2 1.6.0/{name}" for name in (
            "fable_2.exe", "rexruntime.dll", "rexgpu-xenos.dll", "fable2_build.json", "Fable2Launcher.exe",
            "README.md", "fable2_config.toml", "data/scripts/recomp/F5.lua")})
        self.assertEqual(config, "repo config")

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


if __name__ == "__main__":
    unittest.main()
