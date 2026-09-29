#!/usr/bin/env python3
"""Apply the confirmed ReXGlue 0.10.0 ARM64 terrain shader binding correction.

Only exact known libraries are accepted. Sign and verify a temporary copy
before atomically replacing the runtime library; leave the installed SDK alone.
"""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile

ORIGINAL_SHA256 = "bceece83d20572fb498fe0ac63fde1a3bbe428b24c9d3b55ae77114b2aa77797"
PATCHED_SHA256 = "6785404cf9261d230c089c6ec57b414699b94a00528d39b9124b6cee23cf13e1"
OLD = b"layout(set = 0, binding = 0, std140) uniform XeSystemConstants {\n"
NEW = b"layout(set = 1, binding = 0, std140) uniform XeSystemConstants {\n"


def patch_library(library):
    library = Path(library)
    if library.is_symlink():
        raise RuntimeError("Refusing to patch a linked graphics library; use a staged runtime copy.")
    data = library.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest not in (ORIGINAL_SHA256, PATCHED_SHA256):
        raise RuntimeError("Terrain fix supports only the known ARM64 ReXGlue 0.10.0 graphics library.")
    applied = digest == PATCHED_SHA256
    if not applied:
        if data.count(OLD) != 1:
            raise RuntimeError("Expected one terrain shader declaration.")
        patched = data.replace(OLD, NEW, 1)
        if len(patched) != len(data) or sum(a != b for a, b in zip(data, patched)) != 1:
            raise RuntimeError("Unexpected terrain shader patch.")
        with tempfile.TemporaryDirectory(prefix=".terrain-fix-", dir=library.parent) as temp:
            staged = Path(temp) / "librexgpu-xenos.dylib"
            staged.write_bytes(patched)
            shutil.copymode(library, staged)
            subprocess.run(["/usr/bin/codesign", "--force", "--sign", "-", str(staged)],
                           check=True, capture_output=True)
            subprocess.run(["/usr/bin/codesign", "--verify", "--strict", str(staged)],
                           check=True, capture_output=True)
            if hashlib.sha256(staged.read_bytes()).hexdigest() != PATCHED_SHA256:
                raise RuntimeError("Signed library differs from the expected corrected-library hash.")
            # Never rewrite an inode which a running game may have mapped.
            staged.replace(library)
    return {"original_sha256": ORIGINAL_SHA256, "patched_sha256": PATCHED_SHA256,
            "already_applied": applied,
            "shader_string_offset": data.index(NEW if applied else OLD),
            "change": "Tessellation helper constants: descriptor set 0 -> 1, binding 0 unchanged",
            "source_patch": "thirdparty/rexglue-sdk-local.patch"}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("library", type=Path)
    args = parser.parse_args()
    try:
        result = patch_library(args.library)
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        parser.exit(1, "Terrain fix: " + str(error) + "\n")
    if not result["already_applied"]:
        print("Applied the confirmed terrain shader correction.")
