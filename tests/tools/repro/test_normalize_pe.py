"""Check the stable-hash PE normalizer with fake PE files, without a real build."""
import contextlib
import importlib.util
import io
from pathlib import Path
import struct
from unittest.mock import patch

import pytest

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("normalizer", ROOT / "tools/repro/normalize_pe.py")
normalizer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(normalizer)

SECTION_OFFSET = 0x400
SECTION_RVA = 0x1000
DEBUG_ENTRY_SIZE = 28
PE_STAMP_OFFSET = 0x48
DEBUG_STAMP_OFFSET = SECTION_OFFSET + 4
RECORD_OFFSET = SECTION_OFFSET + DEBUG_ENTRY_SIZE
GUID_OFFSET = RECORD_OFFSET + 4
PDB_PATH_OFFSET = RECORD_OFFSET + 24
ZERO_PADDING = b"\0" * 16
LONG_PDB_PATH = b"D:/builds/fable-2/release/fable_2.pdb"  # long enough to hold the new name
SHORT_PDB_PATH = b"D:/out/fable_2.pdb"  # too short to hold the new name as UTF-16


def build_pe(stamp=0x11111111, guid=b"\x77" * 16, pdb_path=LONG_PDB_PATH, after_path=ZERO_PADDING):
    """Return the bytes of a minimal 64-bit PE with one CodeView debug record.

    The PDB path is UTF-8 with a single NUL at the end, then `after_path` follows it.
    """
    record = b"RSDS" + guid + struct.pack("<I", 1) + pdb_path + b"\0"
    entry = struct.pack("<IIHHIIII", 0, stamp, 0, 0, 2, len(record),
                        SECTION_RVA + DEBUG_ENTRY_SIZE, RECORD_OFFSET)
    section = entry + record + after_path

    headers = bytearray(SECTION_OFFSET)
    headers[:2] = b"MZ"
    struct.pack_into("<I", headers, 0x3C, 0x40)  # where the PE header starts
    headers[0x40:0x44] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", headers, 0x44, 0x8664, 1, stamp, 0, 0, 240, 0x22)  # one section
    struct.pack_into("<H", headers, 0x58, 0x20B)  # 64-bit optional header
    struct.pack_into("<I", headers, 0x58 + 108, 16)  # number of data directories
    struct.pack_into("<II", headers, 0x58 + 112 + 6 * 8, SECTION_RVA, DEBUG_ENTRY_SIZE)  # debug directory
    struct.pack_into("<IIII", headers, 0x148 + 8, len(section), SECTION_RVA, len(section), SECTION_OFFSET)
    return bytes(headers) + section


def run_main(*args):
    """Run the tool's main() and return (exit code, printed output, error output)."""
    output, errors = io.StringIO(), io.StringIO()
    argv = ["normalize_pe.py", *args]
    with patch("sys.argv", argv), contextlib.redirect_stdout(output), \
            contextlib.redirect_stderr(errors):
        try:
            normalizer.main()
        except SystemExit as exit_request:
            return exit_request.code, output.getvalue(), errors.getvalue()
    return 0, output.getvalue(), errors.getvalue()


@pytest.fixture
def exe(tmp_path):
    """Where a test writes its fake exe."""
    return tmp_path / "fable_2.exe"


def normalize(exe, data):
    """Write `data` to the fake exe, normalize it and return the resulting bytes."""
    exe.write_bytes(data)
    normalizer.normalize(str(exe), normalizer.DEFAULT_STAMP, "fable_2.pdb", quiet=True)
    return exe.read_bytes()


def describe_normalize():
    # --- Happy flow -------------------------------------------------------

    def test_should_pin_the_pe_timestamp(exe):
        result = normalize(exe, build_pe())
        assert struct.unpack_from("<I", result, PE_STAMP_OFFSET)[0] == normalizer.DEFAULT_STAMP

    def test_should_pin_the_debug_entry_timestamp(exe):
        result = normalize(exe, build_pe())
        assert struct.unpack_from("<I", result, DEBUG_STAMP_OFFSET)[0] == normalizer.DEFAULT_STAMP

    def test_should_pin_the_pdb_guid(exe):
        result = normalize(exe, build_pe())
        assert result[GUID_OFFSET:GUID_OFFSET + 16] == normalizer.DEFAULT_GUID

    def test_should_make_two_builds_identical(exe):
        first = normalize(exe, build_pe(stamp=1, guid=b"\x01" * 16, pdb_path=b"D:/builds/one/fable_2.pdb"))
        second = normalize(exe, build_pe(stamp=2, guid=b"\x02" * 16, pdb_path=b"C:/builds/two/fable_2.pdb"))
        assert first == second

    def test_should_keep_the_file_size(exe):
        data = build_pe()
        assert len(normalize(exe, data)) == len(data)

    # --- Bad flow ---------------------------------------------------------

    def test_should_skip_a_file_that_is_not_a_pe(exe):
        exe.write_bytes(b"not a pe file")
        with contextlib.redirect_stderr(io.StringIO()):
            changed = normalizer.normalize(str(exe), normalizer.DEFAULT_STAMP, "fable_2.pdb", quiet=True)
        assert not changed
        assert exe.read_bytes() == b"not a pe file"

    def test_should_skip_an_mz_file_without_a_pe_signature(exe):
        # An MZ header alone is also how an old DOS program starts.
        data = bytearray(build_pe())
        data[0x40:0x44] = b"NOPE"  # where the PE signature should be
        exe.write_bytes(data)
        errors = io.StringIO()
        with contextlib.redirect_stderr(errors):
            changed = normalizer.normalize(str(exe), normalizer.DEFAULT_STAMP, "fable_2.pdb", quiet=True)

        assert not changed
        assert exe.read_bytes() == data
        assert "no PE sig" in errors.getvalue()  # not the earlier "not MZ" skip

    # --- Edge cases -------------------------------------------------------

    def test_should_keep_the_same_bytes_on_a_second_run(exe):
        first = normalize(exe, build_pe())
        assert normalize(exe, first) == first

    # --- Known issues -----------------------------------------------------

    # The tool treats the PDB path as UTF-16, but the field is UTF-8 with a single NUL.
    # strict=True makes a fixed bug fail its test, so the marker gets removed then.

    @pytest.mark.xfail(strict=True, reason='the tool treats the PDB path as UTF-16, but the field is UTF-8 with a single NUL')
    def test_should_keep_the_pdb_path_readable_as_utf8(exe):
        result = normalize(exe, build_pe())
        path = result[PDB_PATH_OFFSET:].split(b"\0")[0]
        assert path == b"fable_2.pdb"

    @pytest.mark.xfail(strict=True, reason='the tool treats the PDB path as UTF-16, but the field is UTF-8 with a single NUL')
    def test_should_leave_the_bytes_after_the_pdb_path_alone(exe):
        after_path = b"\xAA" * 16
        result = normalize(exe, build_pe(after_path=after_path))
        assert result[-len(after_path):] == after_path

    @pytest.mark.xfail(strict=True, reason="the new name doesn't fit a short path, so each run cuts it shorter")
    def test_should_keep_the_same_bytes_on_a_second_run_with_a_short_path(exe):
        # The new name doesn't fit, so each run cuts it shorter: fable_2. -> fable_2 -> fable_
        first = normalize(exe, build_pe(pdb_path=SHORT_PDB_PATH))
        assert normalize(exe, first) == first


def describe_main():
    def test_should_skip_a_missing_file(exe):
        # A missing file is skipped, not an error, so one command can name files that may not exist.
        code, output, _ = run_main(str(exe))

        assert code == 0
        assert "skip (not present)" in output
        assert not exe.exists()

    def test_should_exit_with_2_on_a_broken_pe(exe):
        # The headers end right after the PE signature, so reading them fails.
        data = build_pe()[:0x60]
        exe.write_bytes(data)
        code, _, errors = run_main(str(exe))

        assert code == 2
        assert "ERROR" in errors
        assert exe.read_bytes() == data
