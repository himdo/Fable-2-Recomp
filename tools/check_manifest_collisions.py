#!/usr/bin/env python3
"""check_manifest_collisions.py - guard against guest function names that
shadow rexruntimed.dll exports.

Why this matters
----------------
Codegen emits each recompiled guest function body as the C symbol
__imp__<manifest name>. The SDK runtime (rexruntimed.dll) exports
__imp__<XAPI name> for every xboxkrnl/xam import hook. If a manifest name
equals an XAPI name, the local definition SILENTLY shadows the DLL import
at link time (no linker error), and the generated import-thunk
registration (fable_2_register.cpp: SetFunction(0x832B2xxx,
__imp__<name>)) routes XAPI calls to the guest function instead of the SDK
kernel hook.

That is exactly what broke audio: naming the game-internal KeWait
re-implementations "KeWaitForSingleObject"/"KeWaitForMultipleObjects"
redirected the xboxkrnl import thunks 0x832B28AC/0x832B2CDC into the guest
wrappers, which expect millisecond timeouts where XAPI callers pass kernel
100ns LARGE_INTEGERs (0 means "infinite" there).

Usage: python tools/check_manifest_collisions.py [manifest.toml] [rexruntimed.dll]
Exit code 0 = clean, 1 = collision(s) found, 2 = usage/IO error.
"""

import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def manifest_names(path: Path) -> set[str]:
    names = set()
    for line in path.read_text(encoding="utf-8").splitlines():
        m = re.match(r'\s*name\s*=\s*"([^"]+)"', line)
        if m:
            names.add(m.group(1))
    return names


def pe_exports(path: Path) -> set[str]:
    """Read the PE export directory (pure python, PE32/PE32+)."""
    data = path.read_bytes()
    pe_off = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe_off : pe_off + 4] != b"PE\0\0":
        raise ValueError(f"{path}: not a PE file")
    machine, _, _, _, _, opt_size = struct.unpack_from("<HHIIIH", data, pe_off + 4)
    opt = pe_off + 24
    magic = struct.unpack_from("<H", data, opt)[0]
    # Export directory is data directory index 0 (RVA, size).
    dd_off = opt + (112 if magic == 0x20B else 96)  # PE32+ vs PE32
    exp_rva, exp_size = struct.unpack_from("<II", data, dd_off)
    if not exp_rva:
        return set()

    # Parse sections to map RVA -> file offset.
    sec_off = opt + opt_size
    nsec = struct.unpack_from("<H", data, pe_off + 6)[0]
    sections = []
    for i in range(nsec):
        o = sec_off + i * 40
        vsize, va, raw_size, raw_off = struct.unpack_from("<IIII", data, o + 8)
        sections.append((va, raw_off, raw_size))

    def rva2off(rva: int) -> int:
        for va, raw_off, raw_size in sections:
            if va <= rva < va + max(raw_size, 1):
                return raw_off + (rva - va)
        raise ValueError(f"RVA {rva:#x} outside sections")

    o = rva2off(exp_rva)
    struct.unpack_from("<IIHH", data, o)  # characteristics, timestamp, ver
    name_rva, base, n_funcs, n_names, func_rva, name_ptr_rva, ord_rva = (
        struct.unpack_from("<IIIIIII", data, o + 12)
    )
    name_off = rva2off(name_rva)
    dll_name = data[name_off : data.index(b"\0", name_off)].decode()
    names_off = rva2off(name_ptr_rva)
    exports = set()
    for i in range(n_names):
        nrva = struct.unpack_from("<I", data, names_off + i * 4)[0]
        noff = rva2off(nrva)
        exports.add(data[noff : data.index(b"\0", noff)].decode())
    print(f"  {Path(path).name}: {len(exports)} exports ({dll_name})")
    return exports


def main() -> int:
    args = sys.argv[1:]
    manifest = Path(args[0]) if len(args) > 0 else ROOT / "fable_2_manifest.toml"
    # Debug and release DLLs live in the SDK install; accept an explicit path
    # or probe the known locations.
    candidates = []
    if len(args) > 1:
        candidates = [Path(args[1])]
    else:
        for base in (ROOT / "thirdparty/rexglue-sdk", ROOT.parent / "rexglue-sdk-0.10.0-win-amd64"):
            for cfg in ("win-amd64",):
                for dll in ("rexruntimed.dll", "rexruntimedd.dll"):
                    p = base / cfg / "bin" / dll
                    if p.exists():
                        candidates.append(p)
    if not manifest.exists():
        print(f"error: manifest not found: {manifest}")
        return 2
    if not candidates:
        print("error: no rexruntimed*.dll found; pass the path as 2nd argument")
        return 2

    names = manifest_names(manifest)
    print(f"manifest {manifest.name}: {len(names)} named functions")
    dll_exports = set()
    for dll in candidates:
        try:
            dll_exports |= pe_exports(dll)
        except Exception as e:  # noqa: BLE001
            print(f"  skip {dll}: {e}")
    if not dll_exports:
        print("error: could not read any DLL exports")
        return 2

    bad = []
    for n in sorted(names):
        # __imp__<name> is the symbol codegen emits for the function body:
        # collides when the DLL exports __imp__<name>.
        if f"__imp__{n}" in dll_exports:
            bad.append((n, f"__imp__{n} shadows the DLL XAPI hook -> import thunks "
                           f"would call the guest body"))
        # <name> (weak thunk) shadows the DLL export of the bare name for any
        # reference in the exe.
        elif n in dll_exports:
            bad.append((n, "weak thunk shadows the DLL export of the bare name"))

    if bad:
        print("\nCOLLISIONS - rename these manifest entries (e.g. add a suffix):")
        for n, why in bad:
            print(f"  {n}: {why}")
        return 1
    print("OK - no manifest name collides with a rexruntimed DLL export")
    return 0


if __name__ == "__main__":
    sys.exit(main())
