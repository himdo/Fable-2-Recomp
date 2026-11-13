"""Build small fake 64-bit PE files with export or import tables, for the tools/repro tests.

A real DLL lists the functions it offers in an export table, and a real exe lists
the functions it needs from each DLL in an import table. These builders write just
enough of a PE file around those tables for pefile and the repro tools to read them.
"""
import struct

import pefile

HEADERS_SIZE = 0x400
OPTIONAL_HEADER = 0x58
SECTION_TABLE = OPTIONAL_HEADER + 240
DATA_DIRECTORIES = OPTIONAL_HEADER + 112
SECTION_RVA = 0x1000
SECTION_ALIGNMENT = 0x1000
FILE_ALIGNMENT = 0x200
TIMESTAMP_OFFSET = 0x48

# Mangled C++ names like the ones rexruntime.dll exports.
REX_API = "?Init@Runtime@rex@@QEAAXXZ"  # part of the rex:: API, always kept
HOST_IMPORT = "?Log@@YAXPEBD@Z"  # not rex::, but fable_2.exe imports it
XENOS_IMPORT = "?Present@@YAXXZ"  # not rex::, but the Xenos GPU plugin imports it
STD_BLOAT = "??0exception@std@@QEAA@XZ"  # leaked by WINDOWS_EXPORT_ALL_SYMBOLS, nobody imports it
RUNTIME_EXPORTS = sorted([REX_API, HOST_IMPORT, XENOS_IMPORT, STD_BLOAT])


def align(size, alignment):
    return (size + alignment - 1) // alignment * alignment


def function_address(index):
    """The made-up code address of function number `index` in a fake DLL."""
    return 0x2000 + 0x10 * index


class Section:
    """The bytes of the fake file's one section. add() places a piece and returns
    its RVA, the address it will have once Windows loads the file."""

    def __init__(self):
        self.data = bytearray()

    def add(self, piece):
        self.data += b"\0" * (align(len(self.data), 8) - len(self.data))
        rva = SECTION_RVA + len(self.data)
        self.data += piece
        return rva


def build_pe(section, export_table=(0, 0), import_table=(0, 0), is_dll=True):
    """Wrap one section in PE headers. Each table is (RVA, size), or (0, 0) for none."""
    raw_size = align(len(section.data), FILE_ALIGNMENT)
    headers = bytearray(HEADERS_SIZE)
    headers[:2] = b"MZ"
    struct.pack_into("<I", headers, 0x3C, 0x40)  # where the PE header starts
    headers[0x40:0x44] = b"PE\0\0"
    characteristics = 0x2022 if is_dll else 0x0022
    struct.pack_into("<HHIIIHH", headers, 0x44, 0x8664, 1, 0x11111111, 0, 0, 240, characteristics)
    struct.pack_into("<H", headers, OPTIONAL_HEADER, 0x20B)  # 64-bit optional header
    struct.pack_into("<II", headers, OPTIONAL_HEADER + 32, SECTION_ALIGNMENT, FILE_ALIGNMENT)
    struct.pack_into("<II", headers, OPTIONAL_HEADER + 56, 0x3000, HEADERS_SIZE)  # image and header sizes
    struct.pack_into("<I", headers, OPTIONAL_HEADER + 108, 16)  # number of data directories
    struct.pack_into("<II", headers, DATA_DIRECTORIES, *export_table)  # data directory 0
    struct.pack_into("<II", headers, DATA_DIRECTORIES + 8, *import_table)  # data directory 1
    headers[SECTION_TABLE:SECTION_TABLE + 8] = b".rdata\0\0"
    struct.pack_into("<IIII", headers, SECTION_TABLE + 8, len(section.data), SECTION_RVA, raw_size, HEADERS_SIZE)
    struct.pack_into("<I", headers, SECTION_TABLE + 36, 0x40000040)  # readable initialized data
    return bytes(headers + section.data.ljust(raw_size, b"\0"))


def export_dll(names, ordinals=None, dll_name="rexruntime.dll"):
    """Return a fake DLL that exports `names`.

    Name number i points at function number ordinals[i], whose address is
    function_address(ordinals[i]). By default each name points at the function
    with the same number.
    """
    ordinals = ordinals if ordinals is not None else list(range(len(names)))
    section = Section()
    dll_name_rva = section.add(dll_name.encode() + b"\0")
    name_rvas = [section.add(name.encode() + b"\0") for name in names]
    functions_rva = section.add(struct.pack(f"<{len(names)}I", *[function_address(i) for i in range(len(names))]))
    names_rva = section.add(struct.pack(f"<{len(names)}I", *name_rvas))
    ordinals_rva = section.add(struct.pack(f"<{len(names)}H", *ordinals))
    # IMAGE_EXPORT_DIRECTORY: flags, time, version, DLL name, first ordinal,
    # function count, name count, then where the three lists start.
    directory = struct.pack("<IIHHIIIIIII", 0, 0, 0, 0, dll_name_rva, 1, len(names), len(names),
                            functions_rva, names_rva, ordinals_rva)
    directory_rva = section.add(directory)
    return build_pe(section, export_table=(directory_rva, len(directory)))


def import_exe(imports, is_dll=False):
    """Return a fake exe (or DLL) that imports {dll name: [function names]}."""
    section = Section()
    descriptors = bytearray()
    for dll_name, names in imports.items():
        dll_name_rva = section.add(dll_name.encode() + b"\0")
        # Each name is stored after a 2-byte "hint", which the tools ignore.
        name_rvas = [section.add(struct.pack("<H", 0) + name.encode() + b"\0") for name in names]
        lookup = struct.pack(f"<{len(names) + 1}Q", *name_rvas, 0)
        lookup_rva = section.add(lookup)
        address_rva = section.add(lookup)  # Windows fills this copy in at load time
        # IMAGE_IMPORT_DESCRIPTOR: lookup list, time, forwarder chain, DLL name, address list.
        descriptors += struct.pack("<IIIII", lookup_rva, 0, 0, dll_name_rva, address_rva)
    descriptors += bytes(20)  # an all-zero descriptor ends the list
    descriptors_rva = section.add(bytes(descriptors))
    return build_pe(section, import_table=(descriptors_rva, len(descriptors)), is_dll=is_dll)


def no_tables_dll():
    """Return a fake DLL with neither exports nor imports."""
    section = Section()
    section.add(b"just data")
    return build_pe(section)


# --- Reading results back with pefile, independently of the tools ---------

def exports_of(path):
    """Return {name: function address} for every name the file exports."""
    pe = pefile.PE(str(path))
    try:
        return {symbol.name.decode(): symbol.address for symbol in pe.DIRECTORY_ENTRY_EXPORT.symbols}
    finally:
        pe.close()


def timestamp_of(path):
    """Return the build timestamp in the file's PE header."""
    return struct.unpack_from("<I", path.read_bytes(), TIMESTAMP_OFFSET)[0]
