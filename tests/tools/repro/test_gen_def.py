"""Check that gen_def.py writes a .def that keeps what consumers import plus the rex:: API."""
import contextlib
import importlib.util
import io
from pathlib import Path
from unittest.mock import patch

import pytest

from .fake_pe import (HOST_IMPORT, REX_API, RUNTIME_EXPORTS, STD_BLOAT, XENOS_IMPORT, export_dll,
                      import_exe)

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("gen_def", ROOT / "tools/repro/gen_def.py")
gen_def = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gen_def)


@pytest.fixture
def runtime(tmp_path):
    """A fake rexruntime.dll that exports RUNTIME_EXPORTS."""
    path = tmp_path / "rexruntime.dll"
    path.write_bytes(export_dll(RUNTIME_EXPORTS))
    return path


@pytest.fixture
def consumer(tmp_path):
    """Where a test writes its fake fable_2.exe."""
    return tmp_path / "fable_2.exe"


@pytest.fixture
def def_file(tmp_path):
    """Where gen_def.py writes the .def."""
    return tmp_path / "rexruntime.def"


def run_main(def_file, *args):
    """Run gen_def.py with these arguments, and return what it printed to stderr."""
    argv = ["gen_def.py", *args, "--out", str(def_file)]
    errors = io.StringIO()
    with patch("sys.argv", argv), contextlib.redirect_stderr(errors):
        gen_def.main()
    return errors.getvalue()


def kept_names(def_file):
    """Return the names listed in the written .def, without their leading dots."""
    lines = def_file.read_text(encoding="utf-8").splitlines()
    return [line[1:] for line in lines if line.startswith(".")]


def describe_consumer_imports():
    def test_should_list_the_names_a_consumer_imports_from_the_runtime(consumer):
        consumer.write_bytes(import_exe({"rexruntime.dll": [HOST_IMPORT]}))

        names = gen_def.consumer_imports(str(consumer), gen_def.REX_RUNTIME_NAMES)

        assert names == {HOST_IMPORT}

    def test_should_ignore_imports_from_other_dlls(consumer):
        consumer.write_bytes(import_exe({"rexruntime.dll": [HOST_IMPORT], "KERNEL32.dll": ["Sleep"]}))

        names = gen_def.consumer_imports(str(consumer), gen_def.REX_RUNTIME_NAMES)

        assert "Sleep" not in names

    def test_should_match_the_runtime_name_in_any_case(consumer):
        consumer.write_bytes(import_exe({"REXRUNTIME.DLL": [HOST_IMPORT]}))

        names = gen_def.consumer_imports(str(consumer), gen_def.REX_RUNTIME_NAMES)

        assert names == {HOST_IMPORT}


def describe_dll_exports():
    def test_should_list_every_name_the_runtime_exports(runtime):
        assert gen_def.dll_exports(str(runtime)) == RUNTIME_EXPORTS


def describe_main():
    # --- Happy flow -------------------------------------------------------

    def test_should_keep_consumer_imports_and_the_rex_api(runtime, consumer, def_file):
        consumer.write_bytes(import_exe({"rexruntime.dll": [HOST_IMPORT]}))

        run_main(def_file, str(runtime), str(consumer))

        assert set(kept_names(def_file)) == {REX_API, HOST_IMPORT}

    def test_should_combine_the_imports_of_every_consumer(tmp_path, runtime, consumer, def_file):
        consumer.write_bytes(import_exe({"rexruntime.dll": [HOST_IMPORT]}))
        plugin = tmp_path / "rexgpu-xenos.dll"
        plugin.write_bytes(import_exe({"rexruntime.dll": [XENOS_IMPORT]}, is_dll=True))

        run_main(def_file, str(runtime), str(consumer), str(plugin))

        assert set(kept_names(def_file)) == {REX_API, HOST_IMPORT, XENOS_IMPORT}

    def test_should_start_the_def_with_its_header_lines(runtime, consumer, def_file):
        consumer.write_bytes(import_exe({"rexruntime.dll": [HOST_IMPORT]}))

        run_main(def_file, str(runtime), str(consumer))

        lines = def_file.read_text(encoding="utf-8").splitlines()
        assert lines[:2] == ["LIBRARY rexruntime", "EXPORTS"]

    def test_should_list_the_names_in_sorted_order(runtime, consumer, def_file):
        """Sorted output keeps the .def identical from build to build. Ten names, so
        an unsorted write can't come out in order by chance."""
        rex_names = sorted(f"?Function{i}@rex@@YAXXZ" for i in range(10))
        runtime.write_bytes(export_dll(rex_names))
        consumer.write_bytes(import_exe({"KERNEL32.dll": ["Sleep"]}))

        run_main(def_file, str(runtime), str(consumer))

        assert kept_names(def_file) == rex_names

    def test_should_report_the_counts(runtime, consumer, def_file):
        consumer.write_bytes(import_exe({"rexruntime.dll": [HOST_IMPORT]}))

        output = run_main(def_file, str(runtime), str(consumer))

        assert "current exports : 4" in output
        assert "KEPT (new def)  : 2" in output
        assert "dropped         : 2" in output

    # --- Bad flow ---------------------------------------------------------

    def test_should_skip_a_consumer_that_does_not_exist(tmp_path, runtime, consumer, def_file):
        consumer.write_bytes(import_exe({"rexruntime.dll": [HOST_IMPORT]}))

        run_main(def_file, str(runtime), str(tmp_path / "missing.exe"), str(consumer))

        assert set(kept_names(def_file)) == {REX_API, HOST_IMPORT}

    def test_should_drop_an_import_the_runtime_does_not_export(runtime, consumer, def_file):
        consumer.write_bytes(import_exe({"rexruntime.dll": [HOST_IMPORT, "?Gone@@YAXXZ"]}))

        output = run_main(def_file, str(runtime), str(consumer))

        assert "1 kept symbols not in current export set" in output
        assert "?Gone@@YAXXZ" not in kept_names(def_file)

    # --- Edge cases -------------------------------------------------------

    def test_should_count_imports_from_the_tagged_runtime_name(runtime, consumer, def_file):
        consumer.write_bytes(import_exe({"rexruntime_custom.dll": [HOST_IMPORT]}))

        run_main(def_file, str(runtime), str(consumer), "--tag", "REXRUNTIME_CUSTOM.DLL")

        assert HOST_IMPORT in kept_names(def_file)

    def test_should_ignore_another_runtime_name_without_the_tag(runtime, consumer, def_file):
        consumer.write_bytes(import_exe({"rexruntime_custom.dll": [HOST_IMPORT]}))

        run_main(def_file, str(runtime), str(consumer))

        assert kept_names(def_file) == [REX_API]

    def test_should_never_keep_unused_bloat(runtime, consumer, def_file):
        consumer.write_bytes(import_exe({"rexruntime.dll": [HOST_IMPORT]}))

        run_main(def_file, str(runtime), str(consumer))

        assert STD_BLOAT not in kept_names(def_file)
