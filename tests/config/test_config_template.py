import json
import re
import tomllib
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# Only the launcher uses these keys, so the game never reads them.
LAUNCHER_ONLY = {("input", "keyboard_prompt_images")}


def staged_settings():
    """Return {(section, key): value} for every setting in the staged config."""
    sections = tomllib.loads((ROOT / "config/fable2_config.toml").read_text(encoding="utf-8"))
    settings = {}
    for section, table in sections.items():
        for key, value in table.items():
            settings[(section, key)] = value
    return settings


def engine_source():
    """Return the text of the game's config reader."""
    return (ROOT / "src/core/fable2_config.cpp").read_text(encoding="utf-8")


def engine_read_calls():
    """Return (member, type, section, key) for each read line in fable2_config.cpp.

    A read line looks like `values.member = Read<type>(table, "section", "key", ...)`.
    """
    pattern = r'values\.(\w+)\s*=[^;]*?Read<([\w:]+)>\(\s*[^,]+,\s*"(\w+)",\s*"(\w+)"'
    return re.findall(pattern, engine_source())


def engine_reads():
    """Return the (section, key) pairs the game reads."""
    return {(section, key) for _, _, section, key in engine_read_calls()}


# The TOML value kinds each Read<type> accepts. Read<double> also takes whole numbers.
# Uses exact types, because Python's bool is a kind of int (True == 1).
ACCEPTED_KINDS = {"bool": {bool}, "int64_t": {int}, "double": {int, float}, "std::string": {str}}


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

        # C# bools and ints are valid JSON as written. The keybind map is a joined string constant.
        literals = re.findall(r"public (?:bool|int) (\w+) \{ get; set; \} = ([^;]+);", launcher)
        defaults = {name: json.loads(value) for name, value in literals}
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


class EngineConfigReaderTests(unittest.TestCase):
    """The game's own config reader (src/core/fable2_config.cpp and .h) against the staged config."""

    def test_understands_every_read_call(self):
        """Fail if a Read<...>() call in fable2_config.cpp is written in a shape the regex misses.

        The other tests in this class only see the read lines engine_read_calls() finds. If a
        reformat broke the regex, they would compare empty results and pass without checking
        anything, so this guards them.
        """
        read_calls_in_source = engine_source().count("Read<")
        self.assertEqual(
            len(engine_read_calls()), read_calls_in_source,
            "engine_read_calls() missed a Read<...>() call. Check its regex against the new line shape.",
        )

    def test_engine_reads_every_staged_setting(self):
        """Fail if the game doesn't read a setting from the section it is staged in.

        Each setting in config/fable2_config.toml needs its own Read<...>() call in
        src/core/fable2_config.cpp. A missing call, a misspelled key or the wrong section
        name gives no error: the game silently ignores the setting and uses its default.
        """
        staged = set(staged_settings()) - LAUNCHER_ONLY
        self.assertEqual(engine_reads(), staged)

    def test_engine_defaults_match_staged_config(self):
        """Fail if a default in the game's Values struct differs from the staged config.

        src/core/fable2_config.h repeats the defaults from config/fable2_config.toml. The game
        uses them when a key or the whole file is missing, so a stale default changes how the
        game behaves for anyone whose config file predates the setting.
        """
        header = (ROOT / "src/core/fable2_config.h").read_text(encoding="utf-8")

        # C++ bools, ints and floats are valid JSON once the float suffix "f" is dropped.
        literals = re.findall(r"^\s*(?:bool|int32_t|float)\s+(\w+)\s*=\s*([^;]+);", header, re.M)
        defaults = {name: json.loads(value.removesuffix("f")) for name, value in literals}
        keybinds = re.search(r"kDefaultKeyboardGamepadMap =(.*?);", header, re.S).group(1)
        defaults["keyboard_gamepad_map"] = "".join(re.findall(r'"([^"]*)"', keybinds))

        # Each Values member has the same name as the key it is read from.
        staged = staged_settings()
        engine_values = {(section, key): defaults.get(key) for section, key in engine_reads()}
        staged_values = {(section, key): staged.get((section, key)) for section, key in engine_reads()}

        self.maxDiff = None  # Show the whole diff, so every mismatched setting is named.
        self.assertEqual(engine_values, staged_values)

    def test_staged_values_are_the_kind_the_engine_reads(self):
        """Fail if a staged value is the wrong kind, such as mouse_look = 1 instead of true.

        Read<...>() in src/core/fable2_config.cpp only accepts the exact TOML kind for bools,
        integers and strings. Anything else is logged as a warning and the default is used,
        so the staged value would be ignored. The defaults test can't see this, because
        Python treats True and 1 as equal.
        """
        staged = staged_settings()
        wrong_kinds = {
            (section, key): f"Read<{read_type}>, staged as {type(staged.get((section, key))).__name__}"
            for _, read_type, section, key in engine_read_calls()
            if type(staged.get((section, key))) not in ACCEPTED_KINDS[read_type]
        }
        self.assertEqual(wrong_kinds, {})

    def test_engine_stores_each_setting_under_its_own_name(self):
        """Fail if a Read<...>() call stores a setting in another setting's member.

        The read lines are copied from one another, so a missed edit such as
        `values.terrain_tess_cpu = Read<bool>(..., "skinning_cpu", ...)` is easy to make.
        Every key would still be read, so the other tests pass, but the game would apply
        skinning_cpu's value to terrain_tess_cpu.
        """
        wrong_members = {
            (section, key): f"stored in values.{member}"
            for member, _, section, key in engine_read_calls()
            if member != key
        }
        self.assertEqual(wrong_members, {})


if __name__ == "__main__":
    unittest.main()
