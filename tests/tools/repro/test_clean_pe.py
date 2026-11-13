"""Check clean_pe.py, the release build's post-build step, on a fake build folder."""
import contextlib
import importlib.util
import io
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

import pytest

from .fake_pe import (HOST_IMPORT, REX_API, RUNTIME_EXPORTS, STD_BLOAT, XENOS_IMPORT, export_dll,
                      exports_of, function_address, import_exe, no_tables_dll, timestamp_of)

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("clean_pe", ROOT / "tools/repro/clean_pe.py")
clean_pe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(clean_pe)


@pytest.fixture
def build(tmp_path):
    """The paths of a fake release build folder. Nothing is written until a test does it."""
    return SimpleNamespace(folder=tmp_path, runtime=tmp_path / "rexruntime.dll",
                           exe=tmp_path / "fable_2.exe", plugin=tmp_path / "rexgpu-xenos.dll")


def make_build_folder(build, runtime=None):
    """Fill the folder like a release build: the runtime, the game and the GPU plugin."""
    build.runtime.write_bytes(runtime if runtime is not None else export_dll(RUNTIME_EXPORTS))
    build.exe.write_bytes(import_exe({"rexruntime.dll": [HOST_IMPORT]}))
    build.plugin.write_bytes(import_exe({"rexruntime.dll": [XENOS_IMPORT]}, is_dll=True))


def clean(build, quiet=True):
    """Run clean_dir() on the folder and return its report lines and printed output."""
    output = io.StringIO()
    with contextlib.redirect_stdout(output), contextlib.redirect_stderr(io.StringIO()):
        report = clean_pe.clean_dir(str(build.folder), clean_pe.DEFAULT_STAMP, quiet)
    return report, output.getvalue()


def folder_bytes(build):
    return {path.name: path.read_bytes() for path in sorted(build.folder.iterdir())}


def describe_version_fields():
    def test_should_describe_the_runtime_dll():
        assert clean_pe.version_fields("out/rexruntime.dll")["description"] == "ReXGlue Runtime"

    def test_should_describe_the_xenos_plugin():
        assert clean_pe.version_fields("out/rexgpu-xenos.dll")["description"] == "ReXGlue Xenos GPU Plugin"

    def test_should_describe_any_other_dll_as_rexglue():
        assert clean_pe.version_fields("out/rexaudio.dll")["description"] == "ReXGlue"

    def test_should_take_the_file_names_from_the_path():
        fields = clean_pe.version_fields("out/rexruntime.dll")

        assert fields["filename"] == "rexruntime.dll"
        assert fields["internal"] == "rexruntime"


def describe_trim_export_table():
    def test_should_return_the_new_export_count(build):
        build.runtime.write_bytes(export_dll(RUNTIME_EXPORTS))

        count = clean_pe.trim_export_table(str(build.runtime), {REX_API, HOST_IMPORT})

        assert count == 2
        assert set(exports_of(build.runtime)) == {REX_API, HOST_IMPORT}

    def test_should_leave_the_dll_alone_when_nothing_matches(build):
        original = export_dll(RUNTIME_EXPORTS)
        build.runtime.write_bytes(original)

        count = clean_pe.trim_export_table(str(build.runtime), {"?NotExported@@YAXXZ"})

        assert not count
        assert build.runtime.read_bytes() == original


def describe_clean_dir():
    # --- Happy flow -------------------------------------------------------

    def test_should_trim_the_runtime_to_what_consumers_import_and_the_rex_api(build):
        make_build_folder(build)

        clean(build)

        assert set(exports_of(build.runtime)) == {REX_API, HOST_IMPORT, XENOS_IMPORT}

    def test_should_keep_each_name_pointing_at_its_own_function(build):
        # Reversed ordinals: name 0 points at the last function, and so on, so
        # a trim that lined names up with the wrong functions would show.
        reversed_ordinals = list(reversed(range(len(RUNTIME_EXPORTS))))
        make_build_folder(build, runtime=export_dll(RUNTIME_EXPORTS, ordinals=reversed_ordinals))
        expected = {name: function_address(reversed_ordinals[RUNTIME_EXPORTS.index(name)])
                    for name in (REX_API, HOST_IMPORT, XENOS_IMPORT)}

        clean(build)

        assert exports_of(build.runtime) == expected

    def test_should_pin_the_timestamp_of_every_exe_and_dll(build):
        make_build_folder(build)

        clean(build)

        assert timestamp_of(build.exe) == clean_pe.DEFAULT_STAMP
        assert timestamp_of(build.runtime) == clean_pe.DEFAULT_STAMP
        assert timestamp_of(build.plugin) == clean_pe.DEFAULT_STAMP

    def test_should_report_each_step(build):
        make_build_folder(build)

        report, _ = clean(build)

        assert "  rexruntime.dll: exports 4 -> 3" in report
        assert "  fable_2.exe: normalized" in report

    def test_should_only_trim_the_runtime_dll(build):
        make_build_folder(build)
        other = build.folder / "rexaudio.dll"
        other.write_bytes(export_dll(RUNTIME_EXPORTS, dll_name="rexaudio.dll"))

        clean(build)

        assert set(exports_of(other)) == set(RUNTIME_EXPORTS)

    def test_should_print_the_report_and_final_hashes_unless_quiet(build):
        make_build_folder(build)

        _, output = clean(build, quiet=False)

        assert "rexruntime.dll: exports 4 -> 3" in output
        assert "final hashes:" in output

    # --- Bad flow ---------------------------------------------------------

    def test_should_report_a_broken_rexglue_dll_and_carry_on(build):
        make_build_folder(build)
        (build.folder / "rexbroken.dll").write_bytes(b"not a PE file")

        report, _ = clean(build)

        assert any(line.startswith("  rexbroken.dll: version ERROR") for line in report)
        assert set(exports_of(build.runtime)) == {REX_API, HOST_IMPORT, XENOS_IMPORT}

    def test_should_skip_a_runtime_dll_without_exports(build):
        make_build_folder(build, runtime=no_tables_dll())

        report, _ = clean(build)

        assert not any("exports" in line for line in report)
        assert "  rexruntime.dll: normalized" in report

    # --- Edge cases -------------------------------------------------------

    def test_should_change_nothing_on_a_second_run(build):
        make_build_folder(build)
        clean(build)
        once = folder_bytes(build)

        clean(build)

        assert folder_bytes(build) == once

    def test_should_report_files_as_already_normalized_on_a_second_run(build):
        make_build_folder(build)
        clean(build)

        report, _ = clean(build)

        assert "  fable_2.exe: already normalized" in report

    def test_should_print_nothing_when_quiet(build):
        make_build_folder(build)

        _, output = clean(build, quiet=True)

        assert output == ""

    def test_should_never_keep_unused_bloat(build):
        make_build_folder(build)

        clean(build)

        assert STD_BLOAT not in exports_of(build.runtime)


def describe_main():
    def test_should_pin_the_given_stamp(build):
        make_build_folder(build)
        argv = ["clean_pe.py", str(build.folder), "--stamp", "0x12345678", "--quiet"]

        with patch("sys.argv", argv), contextlib.redirect_stderr(io.StringIO()):
            clean_pe.main()

        assert timestamp_of(build.exe) == 0x12345678
