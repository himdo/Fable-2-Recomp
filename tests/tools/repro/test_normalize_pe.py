"""Check the stable-hash PE normalizer with fake PE files, without a real build."""
import contextlib
import importlib.util
import io
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

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


class NormalizePeTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix="fable2-normalize-test-")
        self.addCleanup(temp.cleanup)
        self.path = Path(temp.name) / "fable_2.exe"

    def normalize(self, data):
        """Write `data` to the fake exe, normalize it and return the resulting bytes."""
        self.path.write_bytes(data)
        normalizer.normalize(str(self.path), normalizer.DEFAULT_STAMP, "fable_2.pdb", quiet=True)
        return self.path.read_bytes()

    def test_pins_the_pe_timestamp(self):
        result = self.normalize(build_pe())
        self.assertEqual(struct.unpack_from("<I", result, PE_STAMP_OFFSET)[0], normalizer.DEFAULT_STAMP)

    def test_pins_the_debug_entry_timestamp(self):
        result = self.normalize(build_pe())
        self.assertEqual(struct.unpack_from("<I", result, DEBUG_STAMP_OFFSET)[0], normalizer.DEFAULT_STAMP)

    def test_pins_the_pdb_guid(self):
        result = self.normalize(build_pe())
        self.assertEqual(result[GUID_OFFSET:GUID_OFFSET + 16], normalizer.DEFAULT_GUID)

    def test_two_builds_become_identical(self):
        first = self.normalize(build_pe(stamp=1, guid=b"\x01" * 16, pdb_path=b"D:/builds/one/fable_2.pdb"))
        second = self.normalize(build_pe(stamp=2, guid=b"\x02" * 16, pdb_path=b"C:/builds/two/fable_2.pdb"))
        self.assertEqual(first, second)

    def test_keeps_the_file_size(self):
        data = build_pe()
        self.assertEqual(len(self.normalize(data)), len(data))

    def test_second_run_keeps_the_same_bytes(self):
        first = self.normalize(build_pe())
        self.assertEqual(self.normalize(first), first)

    def test_skips_a_file_that_is_not_a_pe(self):
        self.path.write_bytes(b"not a pe file")
        with contextlib.redirect_stderr(io.StringIO()):
            changed = normalizer.normalize(str(self.path), normalizer.DEFAULT_STAMP, "fable_2.pdb", quiet=True)
        self.assertFalse(changed)
        self.assertEqual(self.path.read_bytes(), b"not a pe file")

    def test_skips_an_mz_file_without_a_pe_signature(self):
        # An MZ header alone is also how an old DOS program starts.
        data = bytearray(build_pe())
        data[0x40:0x44] = b"NOPE"  # where the PE signature should be
        self.path.write_bytes(data)
        errors = io.StringIO()
        with contextlib.redirect_stderr(errors):
            changed = normalizer.normalize(str(self.path), normalizer.DEFAULT_STAMP, "fable_2.pdb", quiet=True)

        self.assertFalse(changed)
        self.assertEqual(self.path.read_bytes(), data)
        self.assertIn("no PE sig", errors.getvalue())  # not the earlier "not MZ" skip

    def test_main_skips_a_missing_file(self):
        # A missing file is skipped, not an error, so one command can name files that may not exist.
        code, output, _ = run_main(str(self.path))

        self.assertEqual(code, 0)
        self.assertIn("skip (not present)", output)
        self.assertFalse(self.path.exists())

    def test_main_exits_2_on_a_broken_pe(self):
        # The headers end right after the PE signature, so reading them fails.
        data = build_pe()[:0x60]
        self.path.write_bytes(data)
        code, _, errors = run_main(str(self.path))

        self.assertEqual(code, 2)
        self.assertIn("ERROR", errors)
        self.assertEqual(self.path.read_bytes(), data)

    # Known bugs. The tool treats the PDB path as UTF-16, but the field is UTF-8 with a single NUL.
    # Once a bug is fixed its test reports an unexpected success, so remove the marker then.

    @unittest.expectedFailure
    def test_pdb_path_stays_readable_as_utf8(self):
        result = self.normalize(build_pe())
        path = result[PDB_PATH_OFFSET:].split(b"\0")[0]
        self.assertEqual(path, b"fable_2.pdb")

    @unittest.expectedFailure
    def test_bytes_after_the_pdb_path_are_untouched(self):
        after_path = b"\xAA" * 16
        result = self.normalize(build_pe(after_path=after_path))
        self.assertEqual(result[-len(after_path):], after_path)

    @unittest.expectedFailure
    def test_second_run_keeps_the_same_bytes_with_a_short_path(self):
        # The new name doesn't fit, so each run cuts it shorter: fable_2. -> fable_2 -> fable_
        first = self.normalize(build_pe(pdb_path=SHORT_PDB_PATH))
        self.assertEqual(self.normalize(first), first)


if __name__ == "__main__":
    unittest.main()
