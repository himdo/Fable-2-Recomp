import json
import re
import tomllib
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class ConfigTemplateTests(unittest.TestCase):
    def test_staged_config_matches_embedded_template(self):
        source = (ROOT / "src/core/fable2_config.cpp").read_text(encoding="utf-8")
        embedded = re.search(r'R"TOML_EOF\((.*?)\)TOML_EOF"', source, re.S).group(1)
        staged = (ROOT / "config/fable2_config.toml").read_text(encoding="utf-8")
        self.assertEqual(staged.strip().splitlines(), embedded.strip().splitlines())

    def test_launcher_defaults_match_staged_config(self):
        """Fail if a default in the launcher differs from the staged config.

        Fable2ConfigValues repeats the defaults from config/fable2_config.toml. The launcher
        shows them when a key or the whole file is missing, and saving the Advanced page writes
        them to the file, so a stale default can change how the game behaves. Settings are read
        from the C# source, so new ones are checked automatically.
        """
        launcher = (ROOT / "launcher/Fable2.Launcher/Fable2ConfigFile.cs").read_text(encoding="utf-8")
        sections = tomllib.loads((ROOT / "config/fable2_config.toml").read_text(encoding="utf-8"))
        staged = {}
        for section in sections.values():
            staged.update(section)

        # C# bool, int, double and string literals are valid JSON as written. The keybind map is
        # a joined string constant.
        values_class = re.search(r"public class Fable2ConfigValues\s*\{(.*?)\n\}", launcher, re.S).group(1)
        literals = re.findall(r"public (?:bool|int|double|string) (\w+) \{ get; set; \} = ([^;]+);", values_class)
        defaults = {name: json.loads(value) for name, value in literals if value != "DefaultKeyboardGamepadMap"}
        keybinds = re.search(r"DefaultKeyboardGamepadMap =(.*?);", launcher, re.S).group(1)
        defaults["KeyboardGamepadMap"] = "".join(re.findall(r'"([^"]*)"', keybinds))

        # Fable2ConfigFile.Read() pairs each TOML key with the property it fills.
        settings = dict(re.findall(r'key == "(\w+)"\) values\.(\w+) =', launcher))
        # Also fails if a regex stops matching, or Read() never fills a property.
        self.assertEqual(sorted(settings.values()), sorted(defaults))

        self.maxDiff = None  # Show the whole diff, so every mismatched setting is named.
        launcher_values = {key: defaults[prop] for key, prop in settings.items()}
        staged_values = {key: staged.get(key) for key in settings}
        self.assertEqual(launcher_values, staged_values)


if __name__ == "__main__":
    unittest.main()
