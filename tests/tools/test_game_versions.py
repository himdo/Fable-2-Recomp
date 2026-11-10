"""Catalogue validation without game files or modifying the source catalogue."""
import contextlib
import copy
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("generator", ROOT / "tools/generate_game_versions.py")
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


def make_root(directory, catalogue):
    """Write the catalogue and its up-to-date generated files into a fake repo, and return it."""
    root = Path(directory)
    (root / "constants").mkdir()
    (root / "constants/game_versions.json").write_text(json.dumps(catalogue), encoding="utf-8")
    with patch.object(generator, "ROOT", root):
        for name, lines in generator.generate().items():
            (root / "constants" / name).write_text("\n".join(lines) + "\n", encoding="utf-8")
    return root


def run_check(root):
    """Run the generator with --check against a fake repo."""
    argv = ["generate_game_versions.py", "--check"]
    with patch.object(generator, "ROOT", root), patch("sys.argv", argv), \
            contextlib.redirect_stdout(io.StringIO()):
        generator.main()


class CatalogueTests(unittest.TestCase):
    def setUp(self):
        self.catalogue = json.loads((ROOT / "constants/game_versions.json").read_text())

    def generate(self, catalogue):
        with tempfile.TemporaryDirectory(prefix="fable2-catalogue-test-") as directory:
            fixture = Path(directory)
            (fixture / "constants").mkdir()
            (fixture / "constants/game_versions.json").write_text(json.dumps(catalogue))
            saved_root = generator.ROOT
            try:
                generator.ROOT = fixture
                return generator.generate()
            finally:
                generator.ROOT = saved_root

    def test_generated_files_match_catalogue(self):
        for name, lines in self.generate(self.catalogue).items():
            self.assertEqual((ROOT / "constants" / name).read_text(), "\n".join(lines) + "\n")

    def test_extra_locale_is_generated_in_all_languages(self):
        extra = copy.deepcopy(self.catalogue["versions"][0])
        extra.update(id="synthetic-locale", hash="a" * 64, name="Synthetic locale", language=12)
        self.catalogue["versions"].append(extra)
        for lines in self.generate(self.catalogue).values():
            self.assertIn("synthetic-locale", "\n".join(lines))

    def test_invalid_or_duplicate_entries_are_rejected(self):
        for change in ({"hash": "invalid"}, {"id": "../bad"}, {"language": 13},
                       {"compatible": "true"}, {"requiredFiles": ["../gamefile"]},
                       {"requiredFiles": "not-an-array"}):
            with self.subTest(change=change):
                catalogue = copy.deepcopy(self.catalogue)
                catalogue["versions"][0].update(change)
                with self.assertRaises(ValueError):
                    self.generate(catalogue)
        self.catalogue["versions"].append(copy.deepcopy(self.catalogue["versions"][0]))
        with self.assertRaises(ValueError):
            self.generate(self.catalogue)

    @unittest.expectedFailure
    def test_unknown_default_profile_is_rejected(self):
        # Known issue: next() finds no version with that id and raises a bare StopIteration,
        # so the error doesn't say what is wrong. Every other check raises a ValueError.
        self.catalogue["defaultProfile"] = "no-such-profile"

        with self.assertRaisesRegex(ValueError, "(?i)default"):
            self.generate(self.catalogue)

    def test_incompatible_default_profile_is_rejected(self):
        self.catalogue["versions"][0]["compatible"] = False  # versions[0] is the default profile

        with self.assertRaisesRegex(ValueError, "Default version must belong"):
            self.generate(self.catalogue)

    def test_default_profile_from_another_code_group_is_rejected(self):
        self.catalogue["versions"][0]["codeGroup"] = "another-code-group"

        with self.assertRaisesRegex(ValueError, "Default version must belong"):
            self.generate(self.catalogue)

    def test_incompatible_versions_stay_out_of_the_cmake_profiles(self):
        # The C# and C++ files still list an incompatible version, so the launcher can name
        # it, but CMake must not build a profile for it.
        # Guard: a version from another code group would stay out anyway, and pass for the
        # wrong reason. The real incompatible versions are all in another code group.
        self.assertEqual(self.catalogue["versions"][1]["codeGroup"], self.catalogue["codeGroup"])
        self.catalogue["versions"][1]["compatible"] = False
        incompatible_id = self.catalogue["versions"][1]["id"]
        generated = self.generate(self.catalogue)
        cmake = "\n".join(generated["game_versions.cmake"])
        launcher = "\n".join(generated["GameVersions.cs"])

        self.assertIn(f'"{incompatible_id}"', launcher)
        self.assertNotIn(f'"{incompatible_id}"', cmake)

    def test_compatible_versions_from_another_code_group_stay_out_of_the_cmake_profiles(self):
        # Only versions that share the validated guest code can use the same build.
        # Guard: an incompatible version would stay out anyway, and pass for the wrong reason.
        self.assertTrue(self.catalogue["versions"][1]["compatible"])
        self.catalogue["versions"][1]["codeGroup"] = "another-code-group"
        other_id = self.catalogue["versions"][1]["id"]
        generated = self.generate(self.catalogue)
        cmake = "\n".join(generated["game_versions.cmake"])

        self.assertNotIn(f'"{other_id}"', cmake)

    def test_check_passes_when_the_generated_files_are_current(self):
        with tempfile.TemporaryDirectory(prefix="fable2-catalogue-test-") as directory:
            root = make_root(directory, self.catalogue)
            run_check(root)  # raises SystemExit if anything is stale

    def test_check_reports_a_stale_generated_file(self):
        with tempfile.TemporaryDirectory(prefix="fable2-catalogue-test-") as directory:
            root = make_root(directory, self.catalogue)
            (root / "constants/game_versions.h").write_text("// edited by hand\n", encoding="utf-8")

            with self.assertRaisesRegex(SystemExit, "Stale constants: game_versions.h"):
                run_check(root)


if __name__ == "__main__":
    unittest.main()
