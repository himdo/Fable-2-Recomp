import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class ConfigTemplateTests(unittest.TestCase):
    def test_staged_config_matches_embedded_template(self):
        source = (ROOT / "src/core/fable2_config.cpp").read_text(encoding="utf-8")
        embedded = re.search(r'R"TOML_EOF\((.*?)\)TOML_EOF"', source, re.S).group(1)
        staged = (ROOT / "config/fable2_config.toml").read_text(encoding="utf-8")
        self.assertEqual(staged.strip().splitlines(), embedded.strip().splitlines())


if __name__ == "__main__":
    unittest.main()
