"""Check the recomp patch step with fake generated files, without a game build."""
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("recomp_patches", ROOT / "tools/apply_recomp_patches.py")
recomp_patches = importlib.util.module_from_spec(spec)
spec.loader.exec_module(recomp_patches)

SOURCE_FILE = "fable_2_recomp.0.cpp"
OTHER_SOURCE_FILE = "fable_2_recomp.1.cpp"
ANCHOR = "old_call();"
REPLACEMENT = "new_call(); // [recomp-patch: sample]"
SAMPLE_PATCH = ("sample", ANCHOR, REPLACEMENT)


class ApplyRecompPatchesTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix="fable2-patches-test-")
        self.addCleanup(temp.cleanup)
        self.generated = Path(temp.name)
        for patcher in (patch.dict(os.environ),
                        patch.object(recomp_patches, "GENERATED_DIR", str(self.generated)),
                        patch("builtins.print")):
            patcher.start()
            self.addCleanup(patcher.stop)
        # A value set on the developer's machine must not change these results.
        os.environ.pop("FABLE2_RECOMP_PATCHES", None)

    def write(self, name, text):
        (self.generated / name).write_bytes(text.encode("utf-8"))

    def read(self, name):
        return (self.generated / name).read_bytes().decode("utf-8")

    def run_patches(self, *patches):
        with patch.object(recomp_patches, "PATCHES", list(patches)):
            return recomp_patches.main()

    def test_applies_a_patch(self):
        self.write(SOURCE_FILE, f"before\n{ANCHOR}\nafter\n")
        self.assertEqual(self.run_patches(SAMPLE_PATCH), 0)
        self.assertEqual(self.read(SOURCE_FILE), f"before\n{REPLACEMENT}\nafter\n")

    def test_keeps_line_endings_and_non_ascii_text(self):
        self.write(SOURCE_FILE, f"// café\r\n{ANCHOR}\r\n")
        self.run_patches(SAMPLE_PATCH)
        self.assertEqual(self.read(SOURCE_FILE), f"// café\r\n{REPLACEMENT}\r\n")

    def test_second_run_changes_nothing(self):
        self.write(SOURCE_FILE, f"{ANCHOR}\n")
        self.run_patches(SAMPLE_PATCH)
        self.assertEqual(self.run_patches(SAMPLE_PATCH), 0)
        self.assertEqual(self.read(SOURCE_FILE), f"{REPLACEMENT}\n")

    def test_already_patched_file_counts_as_found(self):
        self.write(SOURCE_FILE, f"{REPLACEMENT}\n")
        self.assertEqual(self.run_patches(SAMPLE_PATCH), 0)

    def test_another_patchs_marker_does_not_count_as_found(self):
        self.write(SOURCE_FILE, "// [recomp-patch: sample2]\n")
        self.assertEqual(self.run_patches(SAMPLE_PATCH), 1)

    def test_patches_every_file_with_the_anchor(self):
        self.write(SOURCE_FILE, f"{ANCHOR}\n")
        self.write(OTHER_SOURCE_FILE, f"{ANCHOR}\n")
        self.run_patches(SAMPLE_PATCH)
        self.assertEqual(self.read(SOURCE_FILE), f"{REPLACEMENT}\n")
        self.assertEqual(self.read(OTHER_SOURCE_FILE), f"{REPLACEMENT}\n")

    def test_patches_the_remaining_file_when_another_is_done(self):
        self.write(SOURCE_FILE, f"{REPLACEMENT}\n")
        self.write(OTHER_SOURCE_FILE, f"{ANCHOR}\n")
        self.assertEqual(self.run_patches(SAMPLE_PATCH), 0)
        self.assertEqual(self.read(OTHER_SOURCE_FILE), f"{REPLACEMENT}\n")

    def test_leaves_other_cpp_files_alone(self):
        self.write(SOURCE_FILE, f"{ANCHOR}\n")
        self.write("other.cpp", f"{ANCHOR}\n")
        self.run_patches(SAMPLE_PATCH)
        self.assertEqual(self.read("other.cpp"), f"{ANCHOR}\n")

    def test_leaves_recomp_headers_alone(self):
        self.write(SOURCE_FILE, f"{ANCHOR}\n")
        self.write("fable_2_recomp.0.h", f"{ANCHOR}\n")
        self.run_patches(SAMPLE_PATCH)
        self.assertEqual(self.read("fable_2_recomp.0.h"), f"{ANCHOR}\n")

    def test_missing_anchor_is_an_error(self):
        self.write(SOURCE_FILE, "nothing to patch\n")
        self.assertEqual(self.run_patches(SAMPLE_PATCH), 1)
        self.assertEqual(self.read(SOURCE_FILE), "nothing to patch\n")

    def test_no_recomp_files_is_an_error(self):
        self.write("other.cpp", f"{ANCHOR}\n")
        self.assertEqual(self.run_patches(SAMPLE_PATCH), 1)

    def test_later_missing_anchor_fails_and_keeps_the_earlier_patch(self):
        self.write(SOURCE_FILE, f"{ANCHOR}\n")
        missing = ("missing", "not_in_the_file();", "x // [recomp-patch: missing]")
        self.assertEqual(self.run_patches(SAMPLE_PATCH, missing), 1)
        self.assertEqual(self.read(SOURCE_FILE), f"{REPLACEMENT}\n")

    def test_zero_switch_skips_everything(self):
        self.write(SOURCE_FILE, f"{ANCHOR}\n")
        os.environ["FABLE2_RECOMP_PATCHES"] = "0"
        self.assertEqual(self.run_patches(SAMPLE_PATCH), 0)
        self.assertEqual(self.read(SOURCE_FILE), f"{ANCHOR}\n")

    def test_other_switch_values_still_patch(self):
        self.write(SOURCE_FILE, f"{ANCHOR}\n")
        os.environ["FABLE2_RECOMP_PATCHES"] = "1"
        self.assertEqual(self.run_patches(SAMPLE_PATCH), 0)
        self.assertEqual(self.read(SOURCE_FILE), f"{REPLACEMENT}\n")

    def test_no_patches_changes_nothing(self):
        # This is the real setup today, because PATCHES is empty.
        self.write(SOURCE_FILE, f"{ANCHOR}\n")
        self.assertEqual(self.run_patches(), 0)
        self.assertEqual(self.read(SOURCE_FILE), f"{ANCHOR}\n")

    def test_real_patches_carry_their_marker(self):
        # Without its marker, a patch applies once and the next run fails with "anchor not found".
        unmarked = [name for name, _, replacement in recomp_patches.PATCHES
                    if f"{recomp_patches.MARKER}{name}]" not in replacement]
        self.assertEqual(unmarked, [])

    @unittest.expectedFailure
    def test_anchor_found_twice_in_one_file_is_an_error(self):
        # Known issue: only the first copy is patched and the run reports success, although
        # the tool's own comment says an anchor must occur exactly once per file.
        self.write(SOURCE_FILE, f"{ANCHOR}\n{ANCHOR}\n")
        self.assertEqual(self.run_patches(SAMPLE_PATCH), 1)


if __name__ == "__main__":
    unittest.main()
