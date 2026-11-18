"""Check that codegen can see the register flags in fable_2_manifest.toml.

Codegen reads its eight register flags (skip_lr, cr_as_local and so on) as keys of the
[entrypoint] table and takes false for a flag it does not find, without a warning. In TOML
a key written after an array-of-tables header such as [[entrypoint.midasm_hook]] belongs to
that table entry. So flags placed below the first hook become keys of a hook: the file
still parses, the values read as intended, and codegen ignores them. From 2026-09-15 the
flags sat under the last hook, and every build ran with all eight off while the file said
true.

The second group of tests guards the same mistake for any other key: a hook or an
invalid-instruction entry may only carry keys that codegen reads from it
(rexglue-sdk src/codegen/config.cpp).
"""
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

REGISTER_FLAGS = ("skip_lr", "skip_msr", "ctr_as_local", "xer_as_local", "reserved_as_local",
                  "cr_as_local", "non_argument_as_local", "non_volatile_as_local")
HOOK_KEYS = {"address", "name", "registers", "after_instruction", "return", "return_on_true",
             "return_on_false", "jump_address", "jump_address_on_true", "jump_address_on_false"}
INVALID_INSTRUCTION_KEYS = {"data", "size"}


def entrypoint(text=None):
    if text is None:
        text = (ROOT / "fable_2_manifest.toml").read_text(encoding="utf-8")
    return tomllib.loads(text)["entrypoint"]


def flags_codegen_sees(table):
    """The register flags that are boolean keys of the entrypoint table itself."""
    return {flag: table[flag] for flag in REGISTER_FLAGS if isinstance(table.get(flag), bool)}


def stray_keys(table):
    """{"midasm_hook[3]": ["skip_lr"], ...} for entries with keys codegen does not read."""
    found = {}
    for array, allowed in (("midasm_hook", HOOK_KEYS),
                           ("invalid_instructions", INVALID_INSTRUCTION_KEYS)):
        for n, entry in enumerate(table.get(array, [])):
            extra = sorted(set(entry) - allowed)
            if extra:
                found[f"{array}[{n}]"] = extra
    return found


MISPLACED = """
[entrypoint]
file_path = "default.xex"

[[entrypoint.midasm_hook]]
address = 0x82000000
name = "hook"

skip_lr = true
cr_as_local = true
"""


def describe_the_checks_themselves():
    def test_should_not_count_a_flag_written_below_a_hook():
        assert flags_codegen_sees(entrypoint(MISPLACED)) == {}

    def test_should_report_the_hook_that_swallowed_the_flags():
        assert stray_keys(entrypoint(MISPLACED)) == {"midasm_hook[0]": ["cr_as_local", "skip_lr"]}


def describe_the_register_flags():
    def test_should_all_be_keys_of_the_entrypoint_table():
        seen = flags_codegen_sees(entrypoint())
        missing = [flag for flag in REGISTER_FLAGS if flag not in seen]
        assert missing == [], (
            f"codegen can't see {missing}: in fable_2_manifest.toml they must be written "
            "above the first [[entrypoint.midasm_hook]], or they become keys of a hook and "
            "read as false.")


def describe_the_manifest_tables():
    def test_should_only_carry_keys_codegen_reads():
        assert stray_keys(entrypoint()) == {}, (
            "these entries have keys codegen ignores; a setting written below an "
            "[[entrypoint...]] header belongs to that entry, not to [entrypoint].")
