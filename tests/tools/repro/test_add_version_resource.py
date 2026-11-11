"""Check that add_version_resource.py grafts version info into a DLL, using fake PE files."""
import contextlib
import importlib.util
import io
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("version_resource", ROOT / "tools/repro/add_version_resource.py")
version_resource = importlib.util.module_from_spec(spec)
spec.loader.exec_module(version_resource)

# Where things sit in the fake DLL. The headers fill the first 0x400 bytes.
HEADERS_SIZE = 0x400
OPTIONAL_HEADER = 0x58
SECTION_TABLE = OPTIONAL_HEADER + 240
RESOURCE_DIRECTORY = OPTIONAL_HEADER + 112 + 2 * 8  # data directory 2 is the resources
SECTION_ALIGNMENT = 0x1000
FILE_ALIGNMENT = 0x200
RSRC_RVA = 0x1000
RELOC_RVA = 0x2000

RT_VERSION = 16
RT_MANIFEST = 24
US_ENGLISH = 0x0409
UNICODE_CODE_PAGE = 0x04B0
SUBDIRECTORY = 0x80000000  # set on a directory entry that points at another directory

MANIFEST = b'<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0"/>'
RELOC_DATA = b"RELOC-SECTION-MARKER"
FIELDS = {"company": "Test Company", "product": "Test Product", "description": "Test runtime",
          "filename": "test.dll", "version": "1.2.3.4"}


def align(size, alignment):
    return (size + alignment - 1) // alignment * alignment


def directory_header(entry_count):
    """A resource directory header: flags, time, version, named count, ID count."""
    return struct.pack("<IIHHHH", 0, 0, 0, 0, 0, entry_count)


def build_rsrc(resources):
    """Return a .rsrc section holding these {type ID: data} resources.

    Each resource gets name ID 1 and US English, like a real DLL's manifest.
    The tree is root -> one name directory per type -> one language directory
    per type -> one data entry per type, followed by the data itself.
    """
    types = sorted(resources)
    name_directories = 16 + 8 * len(types)
    language_directories = name_directories + 24 * len(types)
    data_entries = language_directories + 24 * len(types)
    data_start = data_entries + 16 * len(types)

    tree = bytearray(directory_header(len(types)))
    for i, type_id in enumerate(types):
        tree += struct.pack("<II", type_id, SUBDIRECTORY | (name_directories + 24 * i))
    for i in range(len(types)):
        tree += directory_header(1) + struct.pack("<II", 1, SUBDIRECTORY | (language_directories + 24 * i))
    for i in range(len(types)):
        tree += directory_header(1) + struct.pack("<II", US_ENGLISH, data_entries + 16 * i)
    data = bytearray()
    for type_id in types:
        tree += struct.pack("<IIII", RSRC_RVA + data_start + len(data), len(resources[type_id]), 0, 0)
        data += resources[type_id]
        data += b"\0" * (align(len(data), 4) - len(data))
    return bytes(tree + data)


def build_pe(resources, next_section_rva=RELOC_RVA, with_reloc=True, resource_rva=RSRC_RVA):
    """Return the bytes of a minimal 64-bit DLL: a .rsrc section, then optionally a .reloc section."""
    rsrc = build_rsrc(resources)
    section_list = [(b".rsrc", RSRC_RVA, rsrc)]
    if with_reloc:
        section_list.append((b".reloc", next_section_rva, RELOC_DATA))

    headers = bytearray(HEADERS_SIZE)
    headers[:2] = b"MZ"
    struct.pack_into("<I", headers, 0x3C, 0x40)  # where the PE header starts
    headers[0x40:0x44] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", headers, 0x44, 0x8664, len(section_list), 0, 0, 0, 240, 0x2022)
    struct.pack_into("<H", headers, OPTIONAL_HEADER, 0x20B)  # 64-bit optional header
    struct.pack_into("<II", headers, OPTIONAL_HEADER + 32, SECTION_ALIGNMENT, FILE_ALIGNMENT)
    struct.pack_into("<I", headers, OPTIONAL_HEADER + 108, 16)  # number of data directories
    struct.pack_into("<II", headers, RESOURCE_DIRECTORY, resource_rva, len(rsrc))

    body = bytearray()
    for i, (name, rva, content) in enumerate(section_list):
        raw_size = align(len(content), FILE_ALIGNMENT)
        header = SECTION_TABLE + 40 * i
        headers[header:header + 8] = name.ljust(8, b"\0")
        struct.pack_into("<IIII", headers, header + 8, len(content), rva, raw_size, HEADERS_SIZE + len(body))
        body += content.ljust(raw_size, b"\0")
    return bytes(headers + body)


# --- Reading the result, independently of the tool's own parser -------------

def sections(pe):
    """Return {name: {"vaddr", "rsize", "rptr"}} from the section table."""
    count = struct.unpack_from("<H", pe, 0x46)[0]
    result = {}
    for i in range(count):
        header = SECTION_TABLE + 40 * i
        name = pe[header:header + 8].rstrip(b"\0").decode()
        _, vaddr, rsize, rptr = struct.unpack_from("<IIII", pe, header + 8)
        result[name] = {"vaddr": vaddr, "rsize": rsize, "rptr": rptr}
    return result


def directory_entries(pe, offset):
    """Return the (ID, offset) entries of the resource directory at this file offset."""
    named, ids = struct.unpack_from("<HH", pe, offset + 12)
    return [struct.unpack_from("<II", pe, offset + 16 + 8 * i) for i in range(named + ids)]


def resource_types(pe):
    """Return the type IDs in the root resource directory, in the order they are stored."""
    root = sections(pe)[".rsrc"]["rptr"]
    return [type_id for type_id, _ in directory_entries(pe, root)]


def resource_entry(pe, type_id):
    """Follow the tree from this type to its first name and language, and return
    the (RVA, size) of its data."""
    root = sections(pe)[".rsrc"]["rptr"]
    type_entries = dict(directory_entries(pe, root))
    name_directory = root + (type_entries[type_id] & ~SUBDIRECTORY)
    _, language_directory = directory_entries(pe, name_directory)[0]
    _, data_entry = directory_entries(pe, root + (language_directory & ~SUBDIRECTORY))[0]
    return struct.unpack_from("<II", pe, root + data_entry)


def resource_data(pe, type_id):
    """Return the data of this resource type."""
    rsrc = sections(pe)[".rsrc"]
    rva, size = resource_entry(pe, type_id)
    start = rsrc["rptr"] + rva - rsrc["vaddr"]
    return pe[start:start + size]


def utf16(text):
    return text.encode("utf-16-le")


class AddVersionResourceTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix="fable2-version-resource-test-")
        self.addCleanup(temp.cleanup)
        self.path = Path(temp.name) / "test.dll"

    def add(self, pe):
        """Write `pe` to the fake DLL, run the tool on it, and return (result, new bytes)."""
        self.path.write_bytes(pe)
        result = version_resource.add_version_resource(str(self.path), FIELDS)
        return result, self.path.read_bytes()

    # --- Happy flow -------------------------------------------------------

    def test_adds_a_version_resource(self):
        result, pe = self.add(build_pe({RT_MANIFEST: MANIFEST}))

        self.assertTrue(result)
        self.assertIn(RT_VERSION, resource_types(pe))

    def test_version_resource_holds_the_given_fields(self):
        _, pe = self.add(build_pe({RT_MANIFEST: MANIFEST}))

        version = resource_data(pe, RT_VERSION)

        self.assertIn(utf16("Test Company"), version)
        self.assertIn(utf16("Test Product"), version)
        self.assertIn(utf16("1.2.3.4"), version)

    def test_keeps_the_existing_manifest(self):
        _, pe = self.add(build_pe({RT_MANIFEST: MANIFEST}))

        self.assertEqual(resource_data(pe, RT_MANIFEST), MANIFEST)

    def test_moves_the_next_section_without_changing_it(self):
        original = build_pe({RT_MANIFEST: MANIFEST})

        _, pe = self.add(original)

        reloc = sections(pe)[".reloc"]
        # Guard: the grown .rsrc really did push .reloc further into the file.
        self.assertGreater(reloc["rptr"], sections(original)[".reloc"]["rptr"])
        self.assertEqual(pe[reloc["rptr"]:reloc["rptr"] + len(RELOC_DATA)], RELOC_DATA)

    def test_resource_directory_covers_the_new_version_data(self):
        _, pe = self.add(build_pe({RT_MANIFEST: MANIFEST}))

        directory_rva, directory_size = struct.unpack_from("<II", pe, RESOURCE_DIRECTORY)
        version_rva, version_size = resource_entry(pe, RT_VERSION)

        self.assertEqual(directory_rva, RSRC_RVA)
        self.assertLessEqual(version_rva + version_size, directory_rva + directory_size)

    def test_main_names_the_version_after_the_file(self):
        self.path.write_bytes(build_pe({RT_MANIFEST: MANIFEST}))
        argv = ["add_version_resource.py", str(self.path), "--company", "Test Company"]

        with patch("sys.argv", argv), contextlib.redirect_stdout(io.StringIO()):
            version_resource.main()

        version = resource_data(self.path.read_bytes(), RT_VERSION)
        self.assertIn(utf16("test.dll"), version)  # OriginalFilename
        self.assertIn(utf16("test\0"), version)  # InternalName, the name without .dll

    # --- Bad flow ---------------------------------------------------------

    def test_skips_a_dll_without_resources(self):
        original = build_pe({RT_MANIFEST: MANIFEST}, resource_rva=0)

        result, pe = self.add(original)

        self.assertFalse(result)
        self.assertEqual(pe, original)

    def test_skips_when_the_next_section_leaves_no_room(self):
        # A manifest that nearly fills the 0x1000 bytes before .reloc starts.
        big_manifest = b"<assembly>" + b" " * 0xE00 + b"</assembly>"
        original = build_pe({RT_MANIFEST: big_manifest})

        result, pe = self.add(original)

        self.assertFalse(result)
        self.assertEqual(pe, original)

    # --- Edge cases -------------------------------------------------------

    def test_leaves_a_dll_that_already_has_version_info_alone(self):
        original = build_pe({RT_VERSION: b"existing version info", RT_MANIFEST: MANIFEST})

        result, pe = self.add(original)

        self.assertFalse(result)
        self.assertEqual(pe, original)

    def test_running_twice_changes_nothing_the_second_time(self):
        _, first = self.add(build_pe({RT_MANIFEST: MANIFEST}))

        result, second = self.add(first)

        self.assertFalse(result)
        self.assertEqual(second, first)

    def test_grows_a_resource_section_that_is_the_last_section(self):
        _, pe = self.add(build_pe({RT_MANIFEST: MANIFEST}, with_reloc=False))

        self.assertIn(utf16("Test Company"), resource_data(pe, RT_VERSION))

    # --- Known issues -----------------------------------------------------

    @unittest.expectedFailure
    def test_translation_lists_the_language_before_the_code_page(self):
        """Windows reads each Translation value as the language in the low word and
        the code page in the high word, so US English Unicode is stored 09 04 B0 04.
        The tool's own string table is keyed 040904B0, but it writes the Translation
        the other way round, so the two don't match.
        """
        _, pe = self.add(build_pe({RT_MANIFEST: MANIFEST}))

        version = resource_data(pe, RT_VERSION)

        # Guard: Translation is the last value in the resource, so its 4 bytes end it.
        self.assertIn(utf16("Translation"), version[-40:])
        self.assertEqual(version[-4:], struct.pack("<HH", US_ENGLISH, UNICODE_CODE_PAGE))

    @unittest.expectedFailure
    def test_lists_resource_types_in_ascending_order(self):
        """The PE format requires each resource directory's ID entries in ascending
        order, and Windows may search them assuming that. The tool appends the
        version (16) after the existing manifest (24) instead of sorting them.
        """
        _, pe = self.add(build_pe({RT_MANIFEST: MANIFEST}))

        self.assertEqual(resource_types(pe), [RT_VERSION, RT_MANIFEST])


if __name__ == "__main__":
    unittest.main()
