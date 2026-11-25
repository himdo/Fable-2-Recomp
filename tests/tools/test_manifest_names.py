"""Check the manifest naming rules (tools/check_manifest_names.py) on small manifests, without game files."""
import importlib.util
from pathlib import Path
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("names", ROOT / "tools/check_manifest_names.py")
names = importlib.util.module_from_spec(spec)
spec.loader.exec_module(names)

GOOD_ENTRY = '# Steps the cloth simulation (read in the generated code).\n' \
             '[entrypoint.functions."0x8219ADD8"]\nname = "Cloth_Step_8219ADD8"\n'
OTHER_GOOD_ENTRY = '# Updates every cloth (read in the generated code).\n' \
                   '[entrypoint.functions."0x821E2D40"]\nname = "Cloth_UpdateAll_821E2D40"\n'
PLACEHOLDER_ENTRY = '[entrypoint.functions."0x82A2C520"]\nname = "ProcessAndProcessAndProcess12_82A2C520"\n'


def entry(address, name, comment="# What it does and how that is known."):
    """The text of one manifest entry. Pass comment=None for an entry without a comment."""
    header = f'[entrypoint.functions."{address}"]\nname = "{name}"\n'
    return header if comment is None else f"{comment}\n{header}"


def git(repo, *args):
    """Run git in `repo`, without the user's signing or identity settings."""
    subprocess.run(["git", "-C", str(repo), "-c", "user.name=Test", "-c", "user.email=test@example.com",
                    "-c", "commit.gpgsign=false", *args], check=True, capture_output=True)


@pytest.fixture
def manifest(tmp_path):
    """A manifest with one good entry."""
    path = tmp_path / "fable_2_manifest.toml"
    path.write_text(GOOD_ENTRY, encoding="utf-8")
    return path


@pytest.fixture
def repo(tmp_path):
    """A git repo whose first commit (tag `base`) holds a manifest with one good entry and one placeholder."""
    git(tmp_path, "init", "-q")
    (tmp_path / "fable_2_manifest.toml").write_text(GOOD_ENTRY + PLACEHOLDER_ENTRY, encoding="utf-8")
    git(tmp_path, "add", "fable_2_manifest.toml")
    git(tmp_path, "commit", "-q", "-m", "base")
    git(tmp_path, "tag", "base")
    return tmp_path


def describe_parse_entries():
    # --- Happy flow -------------------------------------------------------

    def test_should_read_the_address_and_the_name():
        entries = names.parse_entries(GOOD_ENTRY)

        assert len(entries) == 1
        assert entries[0].address == 0x8219ADD8
        assert entries[0].name == "Cloth_Step_8219ADD8"

    def test_should_note_a_comment_directly_above():
        entries = names.parse_entries(GOOD_ENTRY)

        assert entries[0].has_comment

    def test_should_read_entries_in_file_order():
        entries = names.parse_entries(GOOD_ENTRY + OTHER_GOOD_ENTRY)

        assert [found.name for found in entries] == ["Cloth_Step_8219ADD8", "Cloth_UpdateAll_821E2D40"]

    # --- Edge cases -------------------------------------------------------

    def test_should_not_count_a_comment_above_a_blank_line():
        text = '# A section heading.\n\n[entrypoint.functions."0x8219ADD8"]\nname = "Cloth_Step_8219ADD8"\n'

        entries = names.parse_entries(text)

        assert not entries[0].has_comment

    def test_should_read_an_entry_without_a_name_as_none():
        # Some entries only mark where a function starts (e.g. issue #38's pressure plate).
        text = '[entrypoint.functions."0x82988EA8"]\n' + GOOD_ENTRY

        entries = names.parse_entries(text)

        assert entries[0].name is None
        assert entries[1].name == "Cloth_Step_8219ADD8"

    def test_should_ignore_midasm_hook_tables():
        text = '[[entrypoint.midasm_hook]]\naddress = 0x822A49E8\nname = "fable2_hook_disable_motion_blur"\n'

        entries = names.parse_entries(text)

        assert entries == []


def describe_every_entry_problems():
    # --- Happy flow -------------------------------------------------------

    def test_should_pass_names_that_end_in_their_own_address():
        entries = names.parse_entries(GOOD_ENTRY + OTHER_GOOD_ENTRY)

        problems = names.every_entry_problems(entries)

        assert problems == []

    def test_should_allow_a_name_without_an_address():
        # The _Guest kernel wrappers keep these names on purpose (see check_manifest_collisions.py).
        entries = names.parse_entries(entry("0x822D74A0", "KeWaitForSingleObject_Guest"))

        problems = names.every_entry_problems(entries)

        assert problems == []

    # --- Bad flow ---------------------------------------------------------

    def test_should_fail_on_a_suffix_that_is_not_the_address():
        entries = names.parse_entries(entry("0x8219ADD8", "Cloth_Step_8219ADD0"))

        problems = names.every_entry_problems(entries)

        assert len(problems) == 1
        assert "0x8219ADD8" in problems[0][1]

    def test_should_fail_on_a_name_used_twice():
        entries = names.parse_entries(GOOD_ENTRY + entry("0x821E2D40", "Cloth_Step_8219ADD8"))

        problems = names.every_entry_problems(entries)

        # The duplicate also has the wrong suffix; the name check is the second problem.
        assert "same name as the entry at 0x8219ADD8" in problems[1][1]

    # --- Edge cases -------------------------------------------------------

    def test_should_skip_entries_without_a_name():
        entries = names.parse_entries('[entrypoint.functions."0x82988EA8"]\n')

        problems = names.every_entry_problems(entries)

        assert problems == []


def describe_new_entry_problems():
    # --- Happy flow -------------------------------------------------------

    def test_should_pass_a_well_shaped_name_with_a_comment():
        new = names.parse_entries(GOOD_ENTRY)[0]

        assert names.new_entry_problems(new) == []

    # --- Bad flow ---------------------------------------------------------

    def test_should_fail_on_a_sub_placeholder():
        new = names.parse_entries(entry("0x8233AE50", "sub_8233AE50"))[0]

        assert names.new_entry_problems(new) == ["is a placeholder name; give it a real one"]

    def test_should_fail_on_a_process_and_process_placeholder():
        new = names.parse_entries(entry("0x82A2C520", "ProcessAndProcessAndProcess12_82A2C520"))[0]

        assert names.new_entry_problems(new) == ["is a placeholder name; give it a real one"]

    def test_should_fail_on_a_name_without_the_address():
        new = names.parse_entries(entry("0x8297EB28", "Swimming"))[0]

        assert names.new_entry_problems(new) == ["doesn't follow Subsystem_WhatItDoes_ADDRESS"]

    def test_should_fail_on_a_lowercase_address():
        new = names.parse_entries(entry("0x8219ADD8", "Cloth_Step_8219add8"))[0]

        assert names.new_entry_problems(new) == ["doesn't follow Subsystem_WhatItDoes_ADDRESS"]

    def test_should_fail_on_an_entry_without_a_comment():
        new = names.parse_entries(entry("0x8219ADD8", "Cloth_Step_8219ADD8", comment=None))[0]

        problems = names.new_entry_problems(new)

        assert problems == ["has no comment above it saying what it does and how that is known"]

    # --- Edge cases -------------------------------------------------------

    def test_should_report_a_bad_name_and_a_missing_comment_together():
        new = names.parse_entries(entry("0x8233AE50", "sub_8233AE50", comment=None))[0]

        assert len(names.new_entry_problems(new)) == 2


def describe_changed_entries():
    # --- Happy flow -------------------------------------------------------

    def test_should_return_an_added_entry():
        base = names.parse_entries(GOOD_ENTRY)
        head = names.parse_entries(GOOD_ENTRY + OTHER_GOOD_ENTRY)

        changed = names.changed_entries(head, base)

        assert [found.name for found in changed] == ["Cloth_UpdateAll_821E2D40"]

    def test_should_return_a_renamed_entry():
        base = names.parse_entries(PLACEHOLDER_ENTRY)
        head = names.parse_entries(entry("0x82A2C520", "Morph_Process_82A2C520"))

        changed = names.changed_entries(head, base)

        assert [found.name for found in changed] == ["Morph_Process_82A2C520"]

    def test_should_skip_an_unchanged_entry():
        base = names.parse_entries(PLACEHOLDER_ENTRY)
        head = names.parse_entries(PLACEHOLDER_ENTRY)

        assert names.changed_entries(head, base) == []

    # --- Edge cases -------------------------------------------------------

    def test_should_skip_an_unchanged_entry_whose_comment_changed():
        base = names.parse_entries(GOOD_ENTRY)
        head = names.parse_entries(GOOD_ENTRY.replace("read in the generated code", "traced at runtime"))

        assert names.changed_entries(head, base) == []

    def test_should_return_an_entry_that_gets_its_first_name():
        base = names.parse_entries('[entrypoint.functions."0x82988EA8"]\n')
        head = names.parse_entries(entry("0x82988EA8", "Physics_PressurePlate_82988EA8"))

        changed = names.changed_entries(head, base)

        assert [found.name for found in changed] == ["Physics_PressurePlate_82988EA8"]


def describe_main():
    # --- Happy flow -------------------------------------------------------

    def test_should_pass_a_clean_manifest(manifest):
        code = names.main(["--manifest", str(manifest)])

        assert code == 0

    def test_should_pass_without_a_base_despite_older_placeholders(manifest, capsys):
        # Without --base only the every-entry rules count; the rest is reported as debt.
        manifest.write_text(GOOD_ENTRY + PLACEHOLDER_ENTRY, encoding="utf-8")

        code = names.main(["--manifest", str(manifest)])

        assert code == 0
        assert "1 placeholder names" in capsys.readouterr().out

    # --- Bad flow ---------------------------------------------------------

    def test_should_fail_on_a_name_used_twice(manifest, capsys):
        manifest.write_text(GOOD_ENTRY + entry("0x821E2D40", "Cloth_Step_8219ADD8"), encoding="utf-8")

        code = names.main(["--manifest", str(manifest)])

        assert code == 1
        assert "same name as the entry at 0x8219ADD8" in capsys.readouterr().out

    def test_should_give_a_usage_error_for_a_missing_manifest(manifest):
        manifest.unlink()

        code = names.main(["--manifest", str(manifest)])

        assert code == 2


def describe_main_with_a_base():
    # --- Happy flow -------------------------------------------------------

    def test_should_pass_when_the_branch_adds_a_good_entry(repo):
        manifest = repo / "fable_2_manifest.toml"
        manifest.write_text(GOOD_ENTRY + PLACEHOLDER_ENTRY + OTHER_GOOD_ENTRY, encoding="utf-8")

        code = names.main(["--manifest", str(manifest), "--base", "base"])

        assert code == 0

    def test_should_ignore_placeholders_the_base_already_had(repo):
        # The base's placeholder entry is unchanged, so it isn't checked.
        code = names.main(["--manifest", str(repo / "fable_2_manifest.toml"), "--base", "base"])

        assert code == 0

    # --- Bad flow ---------------------------------------------------------

    def test_should_fail_when_the_branch_adds_a_placeholder(repo, capsys):
        manifest = repo / "fable_2_manifest.toml"
        manifest.write_text(GOOD_ENTRY + PLACEHOLDER_ENTRY + entry("0x8233AE50", "sub_8233AE50"),
                            encoding="utf-8")

        code = names.main(["--manifest", str(manifest), "--base", "base"])

        assert code == 1
        assert "sub_8233AE50" in capsys.readouterr().out

    def test_should_fail_when_the_branch_adds_an_entry_without_a_comment(repo, capsys):
        manifest = repo / "fable_2_manifest.toml"
        manifest.write_text(GOOD_ENTRY + PLACEHOLDER_ENTRY + entry("0x821E2D40", "Cloth_UpdateAll_821E2D40",
                                                                   comment=None), encoding="utf-8")

        code = names.main(["--manifest", str(manifest), "--base", "base"])

        assert code == 1
        assert "has no comment above it" in capsys.readouterr().out

    def test_should_give_a_usage_error_for_an_unknown_ref(repo):
        code = names.main(["--manifest", str(repo / "fable_2_manifest.toml"), "--base", "no-such-ref"])

        assert code == 2


def describe_the_real_manifest():
    def test_should_follow_the_every_entry_rules():
        # Guards the suffix and unique rules on master, even where CI doesn't pass --base.
        code = names.main(["--manifest", str(ROOT / "fable_2_manifest.toml")])

        assert code == 0
