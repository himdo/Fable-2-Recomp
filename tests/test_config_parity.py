"""fable2_config.toml contract: launcher (C#) and runtime (C++) must agree on keys."""
from pathlib import Path
import re
import tomllib
import unittest

ROOT = Path(__file__).resolve().parents[1]
CPP = (ROOT / "src/core/fable2_config.cpp").read_text(encoding="utf-8")
CS = (ROOT / "launcher/Fable2.Launcher/Fable2ConfigFile.cs").read_text(encoding="utf-8")


def toml_keys(doc):
    return {f"{section}.{key}" for section, table in doc.items() for key in table}


class ConfigParityTests(unittest.TestCase):
    def setUp(self):
        self.staged = tomllib.loads((ROOT / "config/fable2_config.toml").read_text(encoding="utf-8"))
        template = re.search(r'R"TOML_EOF\((.*?)\)TOML_EOF"', CPP, re.S)
        self.assertIsNotNone(template, "kTemplate raw string not found")
        self.template = tomllib.loads(template.group(1))
        # Read<T>(table, "section", "key", ...) may span lines.
        self.cpp_keys = {f"{s}.{k}" for s, k in re.findall(r'Read<[^>]+>\(\s*[^,]+,\s*"(\w+)",\s*"(\w+)"', CPP)}
        # Fable2ConfigFile.Write's managed ("section", "key", value) tuples.
        self.cs_keys = {f"{s}.{k}" for s, k in re.findall(r'\("(\w+)", "(\w+)",', CS)}

    def test_cpp_template_matches_staged_config(self):
        self.assertEqual(self.template, self.staged)

    def test_runtime_reads_every_documented_key(self):
        self.assertEqual(self.cpp_keys, toml_keys(self.staged))

    def test_launcher_manages_every_runtime_key(self):
        self.assertEqual(self.cs_keys, self.cpp_keys)


if __name__ == "__main__":
    unittest.main()
