"""Check clean_pe.py, the release build's post-build step, on a fake build folder."""
import contextlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from .fake_pe import (HOST_IMPORT, REX_API, RUNTIME_EXPORTS, STD_BLOAT, XENOS_IMPORT, export_dll,
                      exports_of, function_address, import_exe, no_tables_dll, timestamp_of)

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("clean_pe", ROOT / "tools/repro/clean_pe.py")
clean_pe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(clean_pe)


class CleanPeTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix="fable2-clean-pe-test-")
        self.addCleanup(temp.cleanup)
        self.folder = Path(temp.name)
        self.runtime = self.folder / "rexruntime.dll"
        self.exe = self.folder / "fable_2.exe"
        self.plugin = self.folder / "rexgpu-xenos.dll"

    def make_build_folder(self, runtime=None):
        """Fill the folder like a release build: the runtime, the game and the GPU plugin."""
        self.runtime.write_bytes(runtime if runtime is not None else export_dll(RUNTIME_EXPORTS))
        self.exe.write_bytes(import_exe({"rexruntime.dll": [HOST_IMPORT]}))
        self.plugin.write_bytes(import_exe({"rexruntime.dll": [XENOS_IMPORT]}, is_dll=True))

    def clean(self, quiet=True):
        """Run clean_dir() on the folder and return its report lines and printed output."""
        output = io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(io.StringIO()):
            report = clean_pe.clean_dir(str(self.folder), clean_pe.DEFAULT_STAMP, quiet)
        return report, output.getvalue()

    def folder_bytes(self):
        return {path.name: path.read_bytes() for path in sorted(self.folder.iterdir())}

    # --- Version info fields ----------------------------------------------

    def test_describes_the_runtime_dll(self):
        self.assertEqual(clean_pe.version_fields("out/rexruntime.dll")["description"], "ReXGlue Runtime")

    def test_describes_the_xenos_plugin(self):
        self.assertEqual(clean_pe.version_fields("out/rexgpu-xenos.dll")["description"],
                         "ReXGlue Xenos GPU Plugin")

    def test_describes_any_other_dll_as_rexglue(self):
        self.assertEqual(clean_pe.version_fields("out/rexaudio.dll")["description"], "ReXGlue")

    def test_takes_the_file_names_from_the_path(self):
        fields = clean_pe.version_fields("out/rexruntime.dll")

        self.assertEqual(fields["filename"], "rexruntime.dll")
        self.assertEqual(fields["internal"], "rexruntime")

    # --- Trimming one export table ----------------------------------------

    def test_trim_export_table_returns_the_new_export_count(self):
        self.runtime.write_bytes(export_dll(RUNTIME_EXPORTS))

        count = clean_pe.trim_export_table(str(self.runtime), {REX_API, HOST_IMPORT})

        self.assertEqual(count, 2)
        self.assertEqual(set(exports_of(self.runtime)), {REX_API, HOST_IMPORT})

    def test_trim_export_table_leaves_the_dll_alone_when_nothing_matches(self):
        original = export_dll(RUNTIME_EXPORTS)
        self.runtime.write_bytes(original)

        count = clean_pe.trim_export_table(str(self.runtime), {"?NotExported@@YAXXZ"})

        self.assertFalse(count)
        self.assertEqual(self.runtime.read_bytes(), original)

    # --- Happy flow -------------------------------------------------------

    def test_trims_the_runtime_to_what_consumers_import_and_the_rex_api(self):
        self.make_build_folder()

        self.clean()

        self.assertEqual(set(exports_of(self.runtime)), {REX_API, HOST_IMPORT, XENOS_IMPORT})

    def test_each_kept_name_still_points_at_its_own_function(self):
        # Reversed ordinals: name 0 points at the last function, and so on, so
        # a trim that lined names up with the wrong functions would show.
        reversed_ordinals = list(reversed(range(len(RUNTIME_EXPORTS))))
        self.make_build_folder(runtime=export_dll(RUNTIME_EXPORTS, ordinals=reversed_ordinals))
        expected = {name: function_address(reversed_ordinals[RUNTIME_EXPORTS.index(name)])
                    for name in (REX_API, HOST_IMPORT, XENOS_IMPORT)}

        self.clean()

        self.assertEqual(exports_of(self.runtime), expected)

    def test_pins_the_timestamp_of_every_exe_and_dll(self):
        self.make_build_folder()

        self.clean()

        self.assertEqual(timestamp_of(self.exe), clean_pe.DEFAULT_STAMP)
        self.assertEqual(timestamp_of(self.runtime), clean_pe.DEFAULT_STAMP)
        self.assertEqual(timestamp_of(self.plugin), clean_pe.DEFAULT_STAMP)

    def test_reports_each_step(self):
        self.make_build_folder()

        report, _ = self.clean()

        self.assertIn("  rexruntime.dll: exports 4 -> 3", report)
        self.assertIn("  fable_2.exe: normalized", report)

    def test_only_trims_the_runtime_dll(self):
        self.make_build_folder()
        other = self.folder / "rexaudio.dll"
        other.write_bytes(export_dll(RUNTIME_EXPORTS, dll_name="rexaudio.dll"))

        self.clean()

        self.assertEqual(set(exports_of(other)), set(RUNTIME_EXPORTS))

    def test_prints_the_report_and_final_hashes_unless_quiet(self):
        self.make_build_folder()

        _, output = self.clean(quiet=False)

        self.assertIn("rexruntime.dll: exports 4 -> 3", output)
        self.assertIn("final hashes:", output)

    def test_main_pins_the_given_stamp(self):
        self.make_build_folder()
        argv = ["clean_pe.py", str(self.folder), "--stamp", "0x12345678", "--quiet"]

        with patch("sys.argv", argv), contextlib.redirect_stderr(io.StringIO()):
            clean_pe.main()

        self.assertEqual(timestamp_of(self.exe), 0x12345678)

    # --- Bad flow ---------------------------------------------------------

    def test_reports_a_broken_rexglue_dll_and_carries_on(self):
        self.make_build_folder()
        (self.folder / "rexbroken.dll").write_bytes(b"not a PE file")

        report, _ = self.clean()

        self.assertTrue(any(line.startswith("  rexbroken.dll: version ERROR") for line in report))
        self.assertEqual(set(exports_of(self.runtime)), {REX_API, HOST_IMPORT, XENOS_IMPORT})

    def test_skips_a_runtime_dll_without_exports(self):
        self.make_build_folder(runtime=no_tables_dll())

        report, _ = self.clean()

        self.assertFalse(any("exports" in line for line in report))
        self.assertIn("  rexruntime.dll: normalized", report)

    # --- Edge cases -------------------------------------------------------

    def test_running_twice_changes_nothing_the_second_time(self):
        self.make_build_folder()
        self.clean()
        once = self.folder_bytes()

        self.clean()

        self.assertEqual(self.folder_bytes(), once)

    def test_second_run_reports_files_as_already_normalized(self):
        self.make_build_folder()
        self.clean()

        report, _ = self.clean()

        self.assertIn("  fable_2.exe: already normalized", report)

    def test_quiet_run_prints_nothing(self):
        self.make_build_folder()

        _, output = self.clean(quiet=True)

        self.assertEqual(output, "")

    def test_never_keeps_unused_bloat(self):
        self.make_build_folder()

        self.clean()

        self.assertNotIn(STD_BLOAT, exports_of(self.runtime))


if __name__ == "__main__":
    unittest.main()
