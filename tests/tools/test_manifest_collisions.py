"""Check the manifest/DLL name collision guard with fake DLLs, without game files."""
import contextlib
import importlib.util
import io
from pathlib import Path
import struct
from unittest.mock import patch

import pytest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("collisions", ROOT / "tools/check_manifest_collisions.py")
collisions = importlib.util.module_from_spec(spec)
spec.loader.exec_module(collisions)

SECTION_OFFSET = 0x400
SECTION_RVA = 0x1000
EXPORT_DIRECTORY_SIZE = 40


def build_pe(exports):
    """Return the bytes of a minimal 64-bit PE whose export table lists `exports`."""
    name_table_rva = SECTION_RVA + EXPORT_DIRECTORY_SIZE
    strings_rva = name_table_rva + 4 * len(exports)
    strings = b""
    name_table = b""
    for name in exports:
        name_table += struct.pack("<I", strings_rva + len(strings))
        strings += name.encode() + b"\0"
    dll_name_rva = strings_rva + len(strings)
    strings += b"fake.dll\0"
    directory = struct.pack("<IIHHIIIIIII", 0, 0, 0, 0, dll_name_rva, 1, len(exports), len(exports),
                            0, name_table_rva, 0)
    section = directory + name_table + strings

    headers = bytearray(SECTION_OFFSET)
    headers[:2] = b"MZ"
    struct.pack_into("<I", headers, 0x3C, 0x40)  # where the PE header starts
    headers[0x40:0x44] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", headers, 0x44, 0x8664, 1, 0, 0, 0, 240, 0x22)  # one section
    struct.pack_into("<H", headers, 0x58, 0x20B)  # 64-bit optional header
    struct.pack_into("<II", headers, 0x58 + 112, SECTION_RVA, len(section))  # export directory
    struct.pack_into("<IIII", headers, 0x148 + 8, len(section), SECTION_RVA, len(section), SECTION_OFFSET)
    return bytes(headers) + section


def run_check(manifest, dll=None):
    """Run the tool's main() and return (exit code, printed output).

    Without a dll, the tool looks for the runtime DLL in its default places.
    """
    output = io.StringIO()
    args = ["check_manifest_collisions.py", str(manifest)]
    if dll is not None:
        args.append(str(dll))
    with patch("sys.argv", args), contextlib.redirect_stdout(output):
        code = collisions.main()
    return code, output.getvalue()


@pytest.fixture
def manifest(tmp_path):
    """A manifest that names one guest function, GuestFunction."""
    path = tmp_path / "manifest.toml"
    path.write_text('[[functions]]\nname = "GuestFunction"\n')
    return path


@pytest.fixture
def dll(tmp_path):
    """Where a test writes the fake runtime DLL. It doesn't exist until the test writes it."""
    return tmp_path / "rexruntimed.dll"


def describe_pe_exports():
    def test_should_read_export_names_from_a_dll(dll):
        dll.write_bytes(build_pe(["__imp__NtClose", "RealExport"]))

        with contextlib.redirect_stdout(io.StringIO()):
            exports = collisions.pe_exports(dll)

        assert exports == {"__imp__NtClose", "RealExport"}


def describe_main():
    # --- Happy flow -------------------------------------------------------

    def test_should_pass_without_a_collision(manifest, dll):
        dll.write_bytes(build_pe(["__imp__NtClose"]))

        code, _ = run_check(manifest, dll)

        assert code == 0

    def test_should_ignore_a_commented_out_name(manifest, dll):
        manifest.write_text('[[functions]]\n# name = "GuestFunction"\n')
        dll.write_bytes(build_pe(["__imp__GuestFunction"]))

        code, _ = run_check(manifest, dll)

        assert code == 0

    # --- Bad flow ---------------------------------------------------------

    def test_should_fail_on_a_name_matching_an_imp_export(manifest, dll):
        dll.write_bytes(build_pe(["__imp__GuestFunction"]))

        code, output = run_check(manifest, dll)

        assert code == 1
        assert "GuestFunction" in output

    def test_should_fail_on_a_name_matching_a_bare_export(manifest, dll):
        dll.write_bytes(build_pe(["GuestFunction"]))

        code, output = run_check(manifest, dll)

        assert code == 1
        assert "GuestFunction" in output

    def test_should_report_every_collision(manifest, dll):
        manifest.write_text('[[functions]]\nname = "GuestFunction"\n'
                            '[[functions]]\nname = "OtherFunction"\n')
        dll.write_bytes(build_pe(["__imp__GuestFunction", "OtherFunction"]))

        code, output = run_check(manifest, dll)

        assert code == 1
        assert "GuestFunction:" in output
        assert "OtherFunction:" in output

    def test_should_give_a_usage_error_for_a_missing_manifest(manifest, dll):
        dll.write_bytes(build_pe(["__imp__NtClose"]))
        manifest.unlink()

        code, _ = run_check(manifest, dll)

        assert code == 2

    def test_should_give_a_usage_error_for_a_dll_that_is_not_a_pe(manifest, dll):
        dll.write_bytes(b"\0" * 0x100)

        code, _ = run_check(manifest, dll)

        assert code == 2

    def test_should_give_a_usage_error_for_a_missing_dll(manifest, dll):
        # A missing manifest also exits with 2, so the message shows which check stopped it.
        code, output = run_check(manifest, dll)

        assert code == 2
        assert "could not read any DLL exports" in output

    def test_should_give_a_usage_error_for_a_dll_without_exports(manifest, dll):
        # An empty export table can't prove there are no collisions, so it counts as unreadable.
        dll.write_bytes(build_pe([]))

        code, output = run_check(manifest, dll)

        assert code == 2
        assert "could not read any DLL exports" in output

    # --- Known issues -----------------------------------------------------

    @pytest.mark.xfail(strict=True, reason="without a DLL argument the tool looks in old SDK folders, "
                       "not where build.cmd installs the runtime")
    def test_should_find_the_dll_the_build_installs(manifest, monkeypatch):
        # Without a DLL argument the tool looks in thirdparty/rexglue-sdk and
        # ../rexglue-sdk-0.10.0-win-amd64 for rexruntimed.dll or rexruntimedd.dll. build.cmd
        # installs the runtime to out/tooling/runtime-sdk/win-amd64/bin/rexruntime.dll instead,
        # so the default run never finds a DLL and exits with a usage error.
        root = manifest.parent
        installed = root / "out/tooling/runtime-sdk/win-amd64/bin/rexruntime.dll"
        installed.parent.mkdir(parents=True)
        installed.write_bytes(build_pe(["__imp__NtClose"]))
        monkeypatch.setattr(collisions, "ROOT", root)

        code, _ = run_check(manifest)

        assert code == 0


def describe_the_real_manifest():
    def test_should_keep_the_kernel_wait_names_free():
        # These names once hijacked the kernel imports and broke audio (see the README).
        names = collisions.manifest_names(ROOT / "fable_2_manifest.toml")

        assert "KeWaitForSingleObject" not in names
        assert "KeWaitForMultipleObjects" not in names
        assert "KeWaitForSingleObject_Guest" in names
        assert "KeWaitForMultipleObjects_Guest" in names
