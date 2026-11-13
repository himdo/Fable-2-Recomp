"""Check that trim_exports.py shrinks a DLL's export table to the names in a .def file."""
import contextlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from .fake_pe import (HOST_IMPORT, REX_API, RUNTIME_EXPORTS, STD_BLOAT, export_dll, exports_of,
                      function_address, no_tables_dll)

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("trim_exports", ROOT / "tools/repro/trim_exports.py")
trim_exports = importlib.util.module_from_spec(spec)
spec.loader.exec_module(trim_exports)


class TrimExportsTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix="fable2-trim-exports-test-")
        self.addCleanup(temp.cleanup)
        self.folder = Path(temp.name)
        self.dll = self.folder / "rexruntime.dll"
        self.def_file = self.folder / "rexruntime.def"

    def write_def(self, *lines):
        self.def_file.write_text("\n".join(lines) + "\n", encoding="utf-8")

    def trim(self):
        """Run trim() on the DLL and .def, and return what it printed to stderr."""
        errors = io.StringIO()
        with contextlib.redirect_stderr(errors):
            trim_exports.trim(str(self.dll), str(self.def_file))
        return errors.getvalue()

    # --- Reading the .def -------------------------------------------------

    def test_reads_names_written_with_a_leading_dot(self):
        self.write_def("LIBRARY rexruntime", "EXPORTS", "." + REX_API)

        self.assertEqual(trim_exports.read_def_names(str(self.def_file)), {REX_API})

    def test_reads_names_written_without_a_dot(self):
        self.write_def("EXPORTS", REX_API)

        self.assertEqual(trim_exports.read_def_names(str(self.def_file)), {REX_API})

    def test_ignores_the_header_lines_and_blank_lines(self):
        self.write_def("LIBRARY rexruntime", "", "EXPORTS", "   ", "." + REX_API)

        self.assertEqual(trim_exports.read_def_names(str(self.def_file)), {REX_API})

    # --- Happy flow -------------------------------------------------------

    def test_keeps_only_the_names_in_the_def(self):
        self.dll.write_bytes(export_dll(RUNTIME_EXPORTS))
        self.write_def("LIBRARY rexruntime", "EXPORTS", "." + REX_API, "." + HOST_IMPORT)

        self.trim()

        self.assertEqual(set(exports_of(self.dll)), {REX_API, HOST_IMPORT})

    def test_each_kept_name_still_points_at_its_own_function(self):
        # Reversed ordinals: name 0 points at the last function, and so on, so
        # a trim that lined names up with the wrong functions would show.
        reversed_ordinals = list(reversed(range(len(RUNTIME_EXPORTS))))
        self.dll.write_bytes(export_dll(RUNTIME_EXPORTS, ordinals=reversed_ordinals))
        expected = {name: function_address(reversed_ordinals[RUNTIME_EXPORTS.index(name)])
                    for name in (REX_API, HOST_IMPORT)}
        self.write_def("EXPORTS", "." + REX_API, "." + HOST_IMPORT)

        self.trim()

        self.assertEqual(exports_of(self.dll), expected)

    def test_reports_how_many_exports_remain(self):
        self.dll.write_bytes(export_dll(RUNTIME_EXPORTS))
        self.write_def("EXPORTS", "." + REX_API)

        output = self.trim()

        self.assertIn("rexruntime.dll: exports 4 -> 1", output)

    # --- Bad flow ---------------------------------------------------------

    def test_leaves_a_file_without_a_pe_header_alone(self):
        original = b"MZ" + b"\0" * 0x3A + b"\x40\0\0\0" + b"not a PE header"
        self.dll.write_bytes(original)
        self.write_def("EXPORTS", "." + REX_API)

        output = self.trim()

        self.assertIn("skip (not PE)", output)
        self.assertEqual(self.dll.read_bytes(), original)

    def test_leaves_a_dll_without_exports_alone(self):
        original = no_tables_dll()
        self.dll.write_bytes(original)
        self.write_def("EXPORTS", "." + REX_API)

        output = self.trim()

        self.assertIn("no export table", output)
        self.assertEqual(self.dll.read_bytes(), original)

    def test_leaves_the_dll_alone_when_the_def_matches_nothing(self):
        original = export_dll(RUNTIME_EXPORTS)
        self.dll.write_bytes(original)
        self.write_def("EXPORTS", ".?NotExported@@YAXXZ")

        output = self.trim()

        self.assertIn("skip (nothing to keep)", output)
        self.assertEqual(self.dll.read_bytes(), original)

    # --- Edge cases -------------------------------------------------------

    def test_warns_about_def_names_the_dll_does_not_export(self):
        self.dll.write_bytes(export_dll(RUNTIME_EXPORTS))
        self.write_def("EXPORTS", "." + REX_API, ".?NotExported@@YAXXZ")

        output = self.trim()

        self.assertIn("1 def symbols not exported", output)
        self.assertEqual(set(exports_of(self.dll)), {REX_API})

    def test_trimming_twice_gives_the_same_file(self):
        self.dll.write_bytes(export_dll(RUNTIME_EXPORTS))
        self.write_def("EXPORTS", "." + REX_API, "." + HOST_IMPORT)
        self.trim()
        once = self.dll.read_bytes()

        self.trim()

        self.assertEqual(self.dll.read_bytes(), once)

    def test_main_does_nothing_when_the_dll_is_missing(self):
        self.write_def("EXPORTS", "." + STD_BLOAT)
        argv = ["trim_exports.py", str(self.folder / "missing.dll"), str(self.def_file)]

        with patch("sys.argv", argv):
            trim_exports.main()

        self.assertFalse((self.folder / "missing.dll").exists())

    def test_main_trims_the_named_dll(self):
        self.dll.write_bytes(export_dll(RUNTIME_EXPORTS))
        self.write_def("EXPORTS", "." + REX_API)
        argv = ["trim_exports.py", str(self.dll), str(self.def_file)]

        with patch("sys.argv", argv), contextlib.redirect_stderr(io.StringIO()):
            trim_exports.main()

        self.assertEqual(set(exports_of(self.dll)), {REX_API})


if __name__ == "__main__":
    unittest.main()
