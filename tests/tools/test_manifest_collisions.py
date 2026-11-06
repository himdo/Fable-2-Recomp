"""Check the manifest/DLL name collision guard with fake DLLs, without game files."""
import contextlib
import importlib.util
import io
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

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


def run_check(manifest, dll):
    """Run the tool's main() and return (exit code, printed output)."""
    output = io.StringIO()
    args = ["check_manifest_collisions.py", str(manifest), str(dll)]
    with patch("sys.argv", args), contextlib.redirect_stdout(output):
        code = collisions.main()
    return code, output.getvalue()


class ManifestCollisionTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix="fable2-collision-test-")
        self.addCleanup(temp.cleanup)
        base = Path(temp.name)
        self.manifest = base / "manifest.toml"
        self.manifest.write_text('[[functions]]\nname = "GuestFunction"\n')
        self.dll = base / "rexruntimed.dll"

    def test_reads_export_names_from_a_dll(self):
        self.dll.write_bytes(build_pe(["__imp__NtClose", "RealExport"]))
        with contextlib.redirect_stdout(io.StringIO()):
            exports = collisions.pe_exports(self.dll)
        self.assertEqual(exports, {"__imp__NtClose", "RealExport"})

    def test_no_collision_passes(self):
        self.dll.write_bytes(build_pe(["__imp__NtClose"]))
        code, _ = run_check(self.manifest, self.dll)
        self.assertEqual(code, 0)

    def test_name_matching_an_imp_export_fails(self):
        self.dll.write_bytes(build_pe(["__imp__GuestFunction"]))
        code, output = run_check(self.manifest, self.dll)
        self.assertEqual(code, 1)
        self.assertIn("GuestFunction", output)

    def test_name_matching_a_bare_export_fails(self):
        self.dll.write_bytes(build_pe(["GuestFunction"]))
        code, output = run_check(self.manifest, self.dll)
        self.assertEqual(code, 1)
        self.assertIn("GuestFunction", output)

    def test_missing_manifest_is_a_usage_error(self):
        self.dll.write_bytes(build_pe(["__imp__NtClose"]))
        self.manifest.unlink()
        code, _ = run_check(self.manifest, self.dll)
        self.assertEqual(code, 2)

    def test_dll_that_is_not_a_pe_is_a_usage_error(self):
        self.dll.write_bytes(b"\0" * 0x100)
        code, _ = run_check(self.manifest, self.dll)
        self.assertEqual(code, 2)

    def test_real_manifest_keeps_the_kernel_wait_names_free(self):
        # These names once hijacked the kernel imports and broke audio (see the README).
        names = collisions.manifest_names(ROOT / "fable_2_manifest.toml")
        self.assertNotIn("KeWaitForSingleObject", names)
        self.assertNotIn("KeWaitForMultipleObjects", names)
        self.assertIn("KeWaitForSingleObject_Guest", names)
        self.assertIn("KeWaitForMultipleObjects_Guest", names)


if __name__ == "__main__":
    unittest.main()
