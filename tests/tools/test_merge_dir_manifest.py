"""tools/merge_dir_manifest.py, which stage_content.cmd uses to stage dir.manifest."""
import importlib.util
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("merge_dir_manifest", ROOT / "tools/merge_dir_manifest.py")
merger = importlib.util.module_from_spec(spec)
spec.loader.exec_module(merger)

DISC = b"art\\gui\\gui.gdb\r\nlevels.bnk\r\nscripts\\quests\\quest.lua\r\n"


@pytest.fixture
def files(tmp_path):
    """A disc manifest and the path of a staged one, which each test creates (or not)."""
    source = tmp_path / "content/data/dir.manifest"
    source.parent.mkdir(parents=True)
    source.write_bytes(DISC)
    return source, tmp_path / "build/data/dir.manifest"


def run(source, destination):
    return merger.main(["merge_dir_manifest.py", str(source), str(destination)])


def describe_main():
    def test_should_copy_the_disc_manifest_when_none_is_staged(files):
        source, destination = files

        assert run(source, destination) == 0
        assert destination.read_bytes() == DISC

    def test_should_keep_the_build_entries_after_the_disc_entries(files):
        source, destination = files
        destination.parent.mkdir(parents=True)
        destination.write_bytes(b"scripts\\recomp\\F5.lua\r\nscripts\\recomp\\console_step.lua\r\n")

        run(source, destination)

        assert destination.read_bytes() == DISC + b"scripts\\recomp\\F5.lua\r\nscripts\\recomp\\console_step.lua\r\n"

    def test_should_not_repeat_lines_both_have(files):
        source, destination = files
        destination.parent.mkdir(parents=True)
        destination.write_bytes(b"levels.bnk\r\nscripts\\recomp\\F5.lua\r\n")

        run(source, destination)

        assert destination.read_bytes().count(b"levels.bnk") == 1
        assert destination.read_bytes().endswith(b"scripts\\recomp\\F5.lua\r\n")

    def test_should_change_nothing_when_run_again(files):
        source, destination = files
        destination.parent.mkdir(parents=True)
        destination.write_bytes(b"scripts\\recomp\\F5.lua\r\n")
        run(source, destination)
        once = destination.read_bytes()

        run(source, destination)

        assert destination.read_bytes() == once

    def test_should_treat_lf_and_crlf_lines_alike_and_drop_blank_lines(files):
        source, destination = files
        destination.parent.mkdir(parents=True)
        destination.write_bytes(b"levels.bnk\n\nscripts\\recomp\\F5.lua\n   \n")

        run(source, destination)

        assert destination.read_bytes() == DISC + b"scripts\\recomp\\F5.lua\r\n"

    def test_should_pass_names_in_any_encoding_through_unchanged(files):
        source, destination = files
        source.write_bytes(DISC + b"text\\caf\xe9.bnk\r\n")

        run(source, destination)

        assert destination.read_bytes() == DISC + b"text\\caf\xe9.bnk\r\n"

    def test_should_leave_no_temporary_file_behind(files):
        source, destination = files

        run(source, destination)

        assert sorted(p.name for p in destination.parent.iterdir()) == ["dir.manifest"]

    def test_should_write_a_staged_only_line_once(files):
        source, destination = files
        destination.parent.mkdir(parents=True)
        destination.write_bytes(b"scripts\\recomp\\F5.lua\r\nscripts\\recomp\\F5.lua\r\n")

        run(source, destination)

        assert destination.read_bytes() == DISC + b"scripts\\recomp\\F5.lua\r\n"

    def test_should_keep_the_staged_lines_when_the_disc_manifest_is_empty(files):
        # ensure_recomp_manifest.cmake creates an empty manifest itself, so empty files do occur.
        source, destination = files
        source.write_bytes(b"")
        destination.parent.mkdir(parents=True)
        destination.write_bytes(b"scripts\\recomp\\F5.lua\n")

        run(source, destination)

        assert destination.read_bytes() == b"scripts\\recomp\\F5.lua\r\n"

    def test_should_replace_a_temporary_file_left_by_an_earlier_run(files):
        source, destination = files
        destination.parent.mkdir(parents=True)
        (destination.parent / "dir.manifest.tmp").write_bytes(b"half a manifest")

        run(source, destination)

        assert sorted(p.name for p in destination.parent.iterdir()) == ["dir.manifest"]
        assert destination.read_bytes() == DISC

    def test_should_keep_repeated_disc_lines_in_disc_order(files):
        # The docstring promises every source line in source order, repeats included.
        source, destination = files
        source.write_bytes(b"levels.bnk\r\nart\\gui\\gui.gdb\r\nlevels.bnk\r\n")

        run(source, destination)

        assert destination.read_bytes() == b"levels.bnk\r\nart\\gui\\gui.gdb\r\nlevels.bnk\r\n"

    def test_should_read_a_last_line_without_a_line_ending(files):
        source, destination = files
        source.write_bytes(b"levels.bnk\r\nart\\gui\\gui.gdb")

        run(source, destination)

        assert destination.read_bytes() == b"levels.bnk\r\nart\\gui\\gui.gdb\r\n"

    def test_should_fail_without_a_source_manifest(files, tmp_path):
        _, destination = files

        assert run(tmp_path / "missing/dir.manifest", destination) == 1
        assert not destination.exists()

    def test_should_fail_with_the_wrong_number_of_arguments():
        assert merger.main(["merge_dir_manifest.py"]) == 2
