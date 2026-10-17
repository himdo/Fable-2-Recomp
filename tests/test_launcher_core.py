"""Tests for tools/mac/fable2_launcher.py."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "mac"))

from fable2_launcher import (
    GameCatalog,
    TomlConfigManager,
    apply_preset,
    PRESETS,
    get_repo_root,
)


class TestGameCatalog(unittest.TestCase):
    def setUp(self):
        self.catalog = GameCatalog()

    def test_catalog_loads_versions(self):
        self.assertGreater(len(self.catalog.versions), 0)

    def test_find_by_hash_goty_spain(self):
        h = "6422e22327b576f6d188206ab00112eedc780f3d130a45ca6ac42f7e4b99da6c"
        info = self.catalog.find_by_hash(h)
        self.assertIsNotNone(info)
        self.assertEqual(info["id"], "goty-spain")
        self.assertTrue(info["compatible"])

    def test_find_by_hash_unknown(self):
        self.assertIsNone(self.catalog.find_by_hash("0000000000000000000000000000000000000000000000000000000000000000"))

    def test_verify_directory_against_repo(self):
        res = self.catalog.verify_directory(ROOT)
        self.assertTrue(res["installed"])
        self.assertEqual(res["profile_id"], "goty-spain")
        self.assertTrue(res["compatible"])
        self.assertEqual(len(res["missing_required_files"]), 0)


class TestTomlConfigManager(unittest.TestCase):
    def test_read_and_update_sections_preserves_comments(self):
        sample_toml = (
            "# Top level header\n"
            "[patches]\n"
            "# Skip videos toggle\n"
            "skip_intro_videos = false  # inline comment\n"
            "unlock_website = true\n"
            "\n"
            "[custom_unmanaged]\n"
            "custom_key = \"custom_value\"\n"
        )
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "fable2_config.toml"
            p.write_text(sample_toml, encoding="utf-8")

            # 1. Read
            secs = TomlConfigManager.read_section_values(p)
            self.assertEqual(secs["patches"]["skip_intro_videos"], "false")
            self.assertEqual(secs["custom_unmanaged"]["custom_key"], '"custom_value"')

            # 2. Update existing key
            TomlConfigManager.update_section_value(p, "patches", "skip_intro_videos", "true")

            updated_text = p.read_text(encoding="utf-8")
            self.assertIn("# Top level header", updated_text)
            self.assertIn("# Skip videos toggle", updated_text)
            self.assertIn("skip_intro_videos = true  # inline comment", updated_text)
            self.assertIn('[custom_unmanaged]\ncustom_key = "custom_value"', updated_text)

            # 3. Add new key to existing section
            TomlConfigManager.update_section_value(p, "patches", "high_tick_rate", "false")
            updated_text2 = p.read_text(encoding="utf-8")
            self.assertIn("high_tick_rate = false", updated_text2)

    def test_read_and_update_root_values(self):
        sample_sdk = (
            "# ReXGlue config\n"
            "window_width = 1280  # initial width\n"
            "window_height = 720\n"
            "[table_ignore]\n"
            "ignore_me = 1\n"
        )
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "fable_2.toml"
            p.write_text(sample_sdk, encoding="utf-8")

            vals = TomlConfigManager.read_root_values(p)
            self.assertEqual(vals["window_width"], "1280")
            self.assertEqual(vals["window_height"], "720")
            self.assertNotIn("ignore_me", vals)

            TomlConfigManager.update_root_values(p, {"window_width": "1920", "vsync": "true"})
            updated = p.read_text(encoding="utf-8")
            self.assertIn("window_width = 1920  # initial width", updated)
            self.assertIn("vsync = true", updated)
            self.assertIn("[table_ignore]\nignore_me = 1", updated)


class TestPresets(unittest.TestCase):
    def test_all_presets_valid(self):
        for name in ("original", "balanced", "enhanced", "high-fps"):
            self.assertIn(name, PRESETS)
            p = PRESETS[name]
            self.assertIn("window_width", p["fable_2_toml"])
            self.assertIn("window_height", p["fable_2_toml"])
            self.assertIn("fullscreen", p["fable_2_toml"])

    def test_apply_preset_to_temp_dir(self):
        with tempfile.TemporaryDirectory() as td:
            b_dir = Path(td)
            cfg = b_dir / "fable2_config.toml"
            cfg.write_text("[patches]\nskip_intro_videos = false\n", encoding="utf-8")

            res = apply_preset("enhanced", build_dir=b_dir)
            self.assertTrue(res)

            # Check fable_2.toml
            sdk_vals = TomlConfigManager.read_root_values(b_dir / "fable_2.toml")
            self.assertEqual(sdk_vals["fullscreen"], "true")
            self.assertEqual(sdk_vals["window_width"], "3024")

            # Check fable2_config.toml
            cfg_secs = TomlConfigManager.read_section_values(cfg)
            self.assertEqual(cfg_secs["patches"]["skip_intro_videos"], "true")


if __name__ == "__main__":
    unittest.main()
