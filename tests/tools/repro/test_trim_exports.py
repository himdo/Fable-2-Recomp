"""Check that trim_exports.py shrinks a DLL's export table to the names in a .def file."""
import contextlib
import importlib.util
import io
from pathlib import Path
from unittest.mock import patch

import pytest

from .fake_pe import (HOST_IMPORT, REX_API, RUNTIME_EXPORTS, STD_BLOAT, export_dll, exports_of,
                      function_address, no_tables_dll)

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("trim_exports", ROOT / "tools/repro/trim_exports.py")
trim_exports = importlib.util.module_from_spec(spec)
spec.loader.exec_module(trim_exports)


@pytest.fixture
def dll(tmp_path):
    """Where a test writes its fake runtime DLL."""
    return tmp_path / "rexruntime.dll"


@pytest.fixture
def def_file(tmp_path):
    """Where a test writes its .def file."""
    return tmp_path / "rexruntime.def"


def write_def(def_file, *lines):
    def_file.write_text("\n".join(lines) + "\n", encoding="utf-8")


def trim(dll, def_file):
    """Run trim() on the DLL and .def, and return what it printed to stderr."""
    errors = io.StringIO()
    with contextlib.redirect_stderr(errors):
        trim_exports.trim(str(dll), str(def_file))
    return errors.getvalue()


def describe_read_def_names():
    def test_should_read_names_written_with_a_leading_dot(def_file):
        write_def(def_file, "LIBRARY rexruntime", "EXPORTS", "." + REX_API)

        assert trim_exports.read_def_names(str(def_file)) == {REX_API}

    def test_should_read_names_written_without_a_dot(def_file):
        write_def(def_file, "EXPORTS", REX_API)

        assert trim_exports.read_def_names(str(def_file)) == {REX_API}

    def test_should_ignore_the_header_lines_and_blank_lines(def_file):
        write_def(def_file, "LIBRARY rexruntime", "", "EXPORTS", "   ", "." + REX_API)

        assert trim_exports.read_def_names(str(def_file)) == {REX_API}


def describe_trim():
    # --- Happy flow -------------------------------------------------------

    def test_should_keep_only_the_names_in_the_def(dll, def_file):
        dll.write_bytes(export_dll(RUNTIME_EXPORTS))
        write_def(def_file, "LIBRARY rexruntime", "EXPORTS", "." + REX_API, "." + HOST_IMPORT)

        trim(dll, def_file)

        assert set(exports_of(dll)) == {REX_API, HOST_IMPORT}

    def test_should_keep_each_name_pointing_at_its_own_function(dll, def_file):
        # Reversed ordinals: name 0 points at the last function, and so on, so
        # a trim that lined names up with the wrong functions would show.
        reversed_ordinals = list(reversed(range(len(RUNTIME_EXPORTS))))
        dll.write_bytes(export_dll(RUNTIME_EXPORTS, ordinals=reversed_ordinals))
        expected = {name: function_address(reversed_ordinals[RUNTIME_EXPORTS.index(name)])
                    for name in (REX_API, HOST_IMPORT)}
        write_def(def_file, "EXPORTS", "." + REX_API, "." + HOST_IMPORT)

        trim(dll, def_file)

        assert exports_of(dll) == expected

    def test_should_report_how_many_exports_remain(dll, def_file):
        dll.write_bytes(export_dll(RUNTIME_EXPORTS))
        write_def(def_file, "EXPORTS", "." + REX_API)

        output = trim(dll, def_file)

        assert "rexruntime.dll: exports 4 -> 1" in output

    # --- Bad flow ---------------------------------------------------------

    def test_should_leave_a_file_without_a_pe_header_alone(dll, def_file):
        original = b"MZ" + b"\0" * 0x3A + b"\x40\0\0\0" + b"not a PE header"
        dll.write_bytes(original)
        write_def(def_file, "EXPORTS", "." + REX_API)

        output = trim(dll, def_file)

        assert "skip (not PE)" in output
        assert dll.read_bytes() == original

    def test_should_leave_a_dll_without_exports_alone(dll, def_file):
        original = no_tables_dll()
        dll.write_bytes(original)
        write_def(def_file, "EXPORTS", "." + REX_API)

        output = trim(dll, def_file)

        assert "no export table" in output
        assert dll.read_bytes() == original

    def test_should_leave_the_dll_alone_when_the_def_matches_nothing(dll, def_file):
        original = export_dll(RUNTIME_EXPORTS)
        dll.write_bytes(original)
        write_def(def_file, "EXPORTS", ".?NotExported@@YAXXZ")

        output = trim(dll, def_file)

        assert "skip (nothing to keep)" in output
        assert dll.read_bytes() == original

    # --- Edge cases -------------------------------------------------------

    def test_should_warn_about_def_names_the_dll_does_not_export(dll, def_file):
        dll.write_bytes(export_dll(RUNTIME_EXPORTS))
        write_def(def_file, "EXPORTS", "." + REX_API, ".?NotExported@@YAXXZ")

        output = trim(dll, def_file)

        assert "1 def symbols not exported" in output
        assert set(exports_of(dll)) == {REX_API}

    def test_should_give_the_same_file_when_trimmed_twice(dll, def_file):
        dll.write_bytes(export_dll(RUNTIME_EXPORTS))
        write_def(def_file, "EXPORTS", "." + REX_API, "." + HOST_IMPORT)
        trim(dll, def_file)
        once = dll.read_bytes()

        trim(dll, def_file)

        assert dll.read_bytes() == once


def describe_main():
    def test_should_trim_the_named_dll(dll, def_file):
        dll.write_bytes(export_dll(RUNTIME_EXPORTS))
        write_def(def_file, "EXPORTS", "." + REX_API)
        argv = ["trim_exports.py", str(dll), str(def_file)]

        with patch("sys.argv", argv), contextlib.redirect_stderr(io.StringIO()):
            trim_exports.main()

        assert set(exports_of(dll)) == {REX_API}

    def test_should_do_nothing_when_the_dll_is_missing(tmp_path, def_file):
        write_def(def_file, "EXPORTS", "." + STD_BLOAT)
        argv = ["trim_exports.py", str(tmp_path / "missing.dll"), str(def_file)]

        with patch("sys.argv", argv):
            trim_exports.main()

        assert not (tmp_path / "missing.dll").exists()
