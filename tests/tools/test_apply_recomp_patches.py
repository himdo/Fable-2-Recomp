"""Check the recomp patch step with fake generated files, without a game build."""
import importlib.util
from pathlib import Path
from unittest.mock import patch

import pytest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("recomp_patches", ROOT / "tools/apply_recomp_patches.py")
recomp_patches = importlib.util.module_from_spec(spec)
spec.loader.exec_module(recomp_patches)

SOURCE_FILE = "fable_2_recomp.0.cpp"
OTHER_SOURCE_FILE = "fable_2_recomp.1.cpp"
ANCHOR = "old_call();"
REPLACEMENT = "new_call(); // [recomp-patch: sample]"
SAMPLE_PATCH = ("sample", ANCHOR, REPLACEMENT)


@pytest.fixture
def generated(tmp_path, monkeypatch):
    """A fake generated/ folder that the tool patches instead of the real one."""
    monkeypatch.setattr(recomp_patches, "GENERATED_DIR", str(tmp_path))
    # A value set on the developer's machine must not change these results.
    monkeypatch.delenv("FABLE2_RECOMP_PATCHES", raising=False)
    return tmp_path


def write(generated, name, text):
    (generated / name).write_bytes(text.encode("utf-8"))


def read(generated, name):
    return (generated / name).read_bytes().decode("utf-8")


def run_patches(*patches):
    """Run the tool with only these patches, and return its exit code."""
    with patch.object(recomp_patches, "PATCHES", list(patches)):
        return recomp_patches.main()


def describe_main():
    # --- Happy flow -------------------------------------------------------

    def test_should_apply_a_patch(generated):
        write(generated, SOURCE_FILE, f"before\n{ANCHOR}\nafter\n")
        assert run_patches(SAMPLE_PATCH) == 0
        assert read(generated, SOURCE_FILE) == f"before\n{REPLACEMENT}\nafter\n"

    def test_should_keep_line_endings_and_non_ascii_text(generated):
        write(generated, SOURCE_FILE, f"// café\r\n{ANCHOR}\r\n")
        run_patches(SAMPLE_PATCH)
        assert read(generated, SOURCE_FILE) == f"// café\r\n{REPLACEMENT}\r\n"

    def test_should_patch_every_file_with_the_anchor(generated):
        write(generated, SOURCE_FILE, f"{ANCHOR}\n")
        write(generated, OTHER_SOURCE_FILE, f"{ANCHOR}\n")
        run_patches(SAMPLE_PATCH)
        assert read(generated, SOURCE_FILE) == f"{REPLACEMENT}\n"
        assert read(generated, OTHER_SOURCE_FILE) == f"{REPLACEMENT}\n"

    def test_should_patch_the_remaining_file_when_another_is_done(generated):
        write(generated, SOURCE_FILE, f"{REPLACEMENT}\n")
        write(generated, OTHER_SOURCE_FILE, f"{ANCHOR}\n")
        assert run_patches(SAMPLE_PATCH) == 0
        assert read(generated, OTHER_SOURCE_FILE) == f"{REPLACEMENT}\n"

    def test_should_count_an_already_patched_file_as_found(generated):
        write(generated, SOURCE_FILE, f"{REPLACEMENT}\n")
        assert run_patches(SAMPLE_PATCH) == 0

    def test_should_change_nothing_without_patches(generated):
        # This is the real setup today, because PATCHES is empty.
        write(generated, SOURCE_FILE, f"{ANCHOR}\n")
        assert run_patches() == 0
        assert read(generated, SOURCE_FILE) == f"{ANCHOR}\n"

    # --- Bad flow ---------------------------------------------------------

    def test_should_fail_on_a_missing_anchor(generated):
        write(generated, SOURCE_FILE, "nothing to patch\n")
        assert run_patches(SAMPLE_PATCH) == 1
        assert read(generated, SOURCE_FILE) == "nothing to patch\n"

    def test_should_fail_without_recomp_files(generated):
        write(generated, "other.cpp", f"{ANCHOR}\n")
        assert run_patches(SAMPLE_PATCH) == 1

    def test_should_fail_on_a_later_missing_anchor_and_keep_the_earlier_patch(generated):
        write(generated, SOURCE_FILE, f"{ANCHOR}\n")
        missing = ("missing", "not_in_the_file();", "x // [recomp-patch: missing]")
        assert run_patches(SAMPLE_PATCH, missing) == 1
        assert read(generated, SOURCE_FILE) == f"{REPLACEMENT}\n"

    def test_should_not_count_another_patchs_marker_as_found(generated):
        write(generated, SOURCE_FILE, "// [recomp-patch: sample2]\n")
        assert run_patches(SAMPLE_PATCH) == 1

    # --- Edge cases -------------------------------------------------------

    def test_should_change_nothing_on_a_second_run(generated):
        write(generated, SOURCE_FILE, f"{ANCHOR}\n")
        run_patches(SAMPLE_PATCH)
        assert run_patches(SAMPLE_PATCH) == 0
        assert read(generated, SOURCE_FILE) == f"{REPLACEMENT}\n"

    def test_should_leave_other_cpp_files_alone(generated):
        write(generated, SOURCE_FILE, f"{ANCHOR}\n")
        write(generated, "other.cpp", f"{ANCHOR}\n")
        run_patches(SAMPLE_PATCH)
        assert read(generated, "other.cpp") == f"{ANCHOR}\n"

    def test_should_leave_recomp_headers_alone(generated):
        write(generated, SOURCE_FILE, f"{ANCHOR}\n")
        write(generated, "fable_2_recomp.0.h", f"{ANCHOR}\n")
        run_patches(SAMPLE_PATCH)
        assert read(generated, "fable_2_recomp.0.h") == f"{ANCHOR}\n"

    def test_should_skip_everything_when_the_switch_is_zero(generated, monkeypatch):
        write(generated, SOURCE_FILE, f"{ANCHOR}\n")
        monkeypatch.setenv("FABLE2_RECOMP_PATCHES", "0")
        assert run_patches(SAMPLE_PATCH) == 0
        assert read(generated, SOURCE_FILE) == f"{ANCHOR}\n"

    def test_should_still_patch_for_other_switch_values(generated, monkeypatch):
        write(generated, SOURCE_FILE, f"{ANCHOR}\n")
        monkeypatch.setenv("FABLE2_RECOMP_PATCHES", "1")
        assert run_patches(SAMPLE_PATCH) == 0
        assert read(generated, SOURCE_FILE) == f"{REPLACEMENT}\n"

    # --- Known issues -----------------------------------------------------

    @pytest.mark.xfail(strict=True, reason="only the first copy is patched and the run reports success")
    def test_should_fail_on_an_anchor_found_twice_in_one_file(generated):
        # The tool's own comment says an anchor must occur exactly once per file.
        write(generated, SOURCE_FILE, f"{ANCHOR}\n{ANCHOR}\n")
        assert run_patches(SAMPLE_PATCH) == 1


def describe_the_real_patches():
    def test_should_each_carry_their_marker():
        # Without its marker, a patch applies once and the next run fails with "anchor not found".
        unmarked = [name for name, _, replacement in recomp_patches.PATCHES
                    if f"{recomp_patches.MARKER}{name}]" not in replacement]
        assert unmarked == []
