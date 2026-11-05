"""Catalogue validation without game files or modifying the source catalogue."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("generator", ROOT / "tools/generate_game_versions.py")
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


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


if __name__ == "__main__":
    unittest.main()
