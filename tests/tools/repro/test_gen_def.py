"""Check that gen_def.py writes a .def that keeps what consumers import plus the rex:: API."""
import contextlib
import gc
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from .fake_pe import (HOST_IMPORT, REX_API, RUNTIME_EXPORTS, STD_BLOAT, XENOS_IMPORT, export_dll,
                      import_exe)

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("gen_def", ROOT / "tools/repro/gen_def.py")
gen_def = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gen_def)


class GenDefTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix="fable2-gen-def-test-")
        self.addCleanup(temp.cleanup)
        # gen_def.py never closes its pefile objects, which keep the files mapped
        # until Python's garbage collector frees them, and Windows won't delete a
        # mapped file. Cleanups run last-added first, so this runs before the delete.
        self.addCleanup(gc.collect)
        self.folder = Path(temp.name)
        self.runtime = self.folder / "rexruntime.dll"
        self.runtime.write_bytes(export_dll(RUNTIME_EXPORTS))
        self.consumer = self.folder / "fable_2.exe"
        self.def_file = self.folder / "rexruntime.def"

    def run_main(self, *args):
        """Run gen_def.py with these arguments, and return what it printed to stderr."""
        argv = ["gen_def.py", *args, "--out", str(self.def_file)]
        errors = io.StringIO()
        with patch("sys.argv", argv), contextlib.redirect_stderr(errors):
            gen_def.main()
        return errors.getvalue()

    def kept_names(self):
        """Return the names listed in the written .def, without their leading dots."""
        lines = self.def_file.read_text(encoding="utf-8").splitlines()
        return [line[1:] for line in lines if line.startswith(".")]

    # --- Reading imports and exports --------------------------------------

    def test_lists_the_names_a_consumer_imports_from_the_runtime(self):
        self.consumer.write_bytes(import_exe({"rexruntime.dll": [HOST_IMPORT]}))

        names = gen_def.consumer_imports(str(self.consumer), gen_def.REX_RUNTIME_NAMES)

        self.assertEqual(names, {HOST_IMPORT})

    def test_ignores_imports_from_other_dlls(self):
        self.consumer.write_bytes(import_exe({"rexruntime.dll": [HOST_IMPORT], "KERNEL32.dll": ["Sleep"]}))

        names = gen_def.consumer_imports(str(self.consumer), gen_def.REX_RUNTIME_NAMES)

        self.assertNotIn("Sleep", names)

    def test_matches_the_runtime_name_in_any_case(self):
        self.consumer.write_bytes(import_exe({"REXRUNTIME.DLL": [HOST_IMPORT]}))

        names = gen_def.consumer_imports(str(self.consumer), gen_def.REX_RUNTIME_NAMES)

        self.assertEqual(names, {HOST_IMPORT})

    def test_lists_every_name_the_runtime_exports(self):
        self.assertEqual(gen_def.dll_exports(str(self.runtime)), RUNTIME_EXPORTS)

    # --- Happy flow -------------------------------------------------------

    def test_keeps_consumer_imports_and_the_rex_api(self):
        self.consumer.write_bytes(import_exe({"rexruntime.dll": [HOST_IMPORT]}))

        self.run_main(str(self.runtime), str(self.consumer))

        self.assertEqual(set(self.kept_names()), {REX_API, HOST_IMPORT})

    def test_combines_the_imports_of_every_consumer(self):
        self.consumer.write_bytes(import_exe({"rexruntime.dll": [HOST_IMPORT]}))
        plugin = self.folder / "rexgpu-xenos.dll"
        plugin.write_bytes(import_exe({"rexruntime.dll": [XENOS_IMPORT]}, is_dll=True))

        self.run_main(str(self.runtime), str(self.consumer), str(plugin))

        self.assertEqual(set(self.kept_names()), {REX_API, HOST_IMPORT, XENOS_IMPORT})

    def test_starts_the_def_with_its_header_lines(self):
        self.consumer.write_bytes(import_exe({"rexruntime.dll": [HOST_IMPORT]}))

        self.run_main(str(self.runtime), str(self.consumer))

        lines = self.def_file.read_text(encoding="utf-8").splitlines()
        self.assertEqual(lines[:2], ["LIBRARY rexruntime", "EXPORTS"])

    def test_lists_the_names_in_sorted_order(self):
        """Sorted output keeps the .def identical from build to build. Ten names, so
        an unsorted write can't come out in order by chance."""
        rex_names = sorted(f"?Function{i}@rex@@YAXXZ" for i in range(10))
        self.runtime.write_bytes(export_dll(rex_names))
        self.consumer.write_bytes(import_exe({"KERNEL32.dll": ["Sleep"]}))

        self.run_main(str(self.runtime), str(self.consumer))

        self.assertEqual(self.kept_names(), rex_names)

    def test_reports_the_counts(self):
        self.consumer.write_bytes(import_exe({"rexruntime.dll": [HOST_IMPORT]}))

        output = self.run_main(str(self.runtime), str(self.consumer))

        self.assertIn("current exports : 4", output)
        self.assertIn("KEPT (new def)  : 2", output)
        self.assertIn("dropped         : 2", output)

    # --- Bad flow ---------------------------------------------------------

    def test_skips_a_consumer_that_does_not_exist(self):
        self.consumer.write_bytes(import_exe({"rexruntime.dll": [HOST_IMPORT]}))

        self.run_main(str(self.runtime), str(self.folder / "missing.exe"), str(self.consumer))

        self.assertEqual(set(self.kept_names()), {REX_API, HOST_IMPORT})

    def test_drops_an_import_the_runtime_does_not_export(self):
        self.consumer.write_bytes(import_exe({"rexruntime.dll": [HOST_IMPORT, "?Gone@@YAXXZ"]}))

        output = self.run_main(str(self.runtime), str(self.consumer))

        self.assertIn("1 kept symbols not in current export set", output)
        self.assertNotIn("?Gone@@YAXXZ", self.kept_names())

    # --- Edge cases -------------------------------------------------------

    def test_tag_also_counts_imports_from_another_runtime_name(self):
        self.consumer.write_bytes(import_exe({"rexruntime_custom.dll": [HOST_IMPORT]}))

        self.run_main(str(self.runtime), str(self.consumer), "--tag", "REXRUNTIME_CUSTOM.DLL")

        self.assertIn(HOST_IMPORT, self.kept_names())

    def test_without_the_tag_another_runtime_name_is_ignored(self):
        self.consumer.write_bytes(import_exe({"rexruntime_custom.dll": [HOST_IMPORT]}))

        self.run_main(str(self.runtime), str(self.consumer))

        self.assertEqual(self.kept_names(), [REX_API])

    def test_never_keeps_unused_bloat(self):
        self.consumer.write_bytes(import_exe({"rexruntime.dll": [HOST_IMPORT]}))

        self.run_main(str(self.runtime), str(self.consumer))

        self.assertNotIn(STD_BLOAT, self.kept_names())


if __name__ == "__main__":
    unittest.main()
