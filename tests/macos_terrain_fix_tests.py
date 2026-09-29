#!/usr/bin/env python3
"""File-preservation tests using synthetic data; no game or SDK binary required."""
import hashlib
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location(
    "macos_terrain_fix", Path(__file__).resolve().parents[1] / "tools/macos_terrain_fix.py")
terrain = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(terrain)


class TerrainHelperTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.library = Path(self.temp.name) / "librexgpu-xenos.dylib"
        self.original = b"synthetic library\0" + terrain.OLD + b"\0unchanged data"
        self.corrected = self.original.replace(terrain.OLD, terrain.NEW)
        self.library.write_bytes(self.original)
        self.original_hash = hashlib.sha256(self.original).hexdigest()
        self.corrected_hash = hashlib.sha256(self.corrected).hexdigest()
        for name, value in [("ORIGINAL_SHA256", self.original_hash),
                            ("PATCHED_SHA256", self.corrected_hash)]:
            patcher = mock.patch.object(terrain, name, value)
            patcher.start()
            self.addCleanup(patcher.stop)

    def assert_original_preserved(self, before):
        self.assertEqual(self.library.read_bytes(), self.original)
        self.assertEqual(self.library.stat().st_ino, before.st_ino)
        self.assertEqual(self.library.stat().st_mtime_ns, before.st_mtime_ns)
        self.assertEqual(list(self.library.parent.iterdir()), [self.library])

    def test_unknown_library_is_unchanged(self):
        self.library.write_bytes(b"different SDK version")
        before = self.library.stat()
        with mock.patch.object(terrain.subprocess, "run") as signer:
            with self.assertRaises(RuntimeError):
                terrain.patch_library(self.library)
            signer.assert_not_called()
        self.assertEqual(self.library.read_bytes(), b"different SDK version")
        self.assertEqual(self.library.stat().st_mtime_ns, before.st_mtime_ns)

    def test_link_cannot_modify_an_installed_sdk(self):
        link = self.library.parent / "linked.dylib"
        link.symlink_to(self.library)
        with self.assertRaises(RuntimeError):
            terrain.patch_library(link)
        self.assertEqual(self.library.read_bytes(), self.original)
        self.assertTrue(link.is_symlink())

    def test_signing_or_verification_failure_preserves_original(self):
        for failed_call in (0, 1):
            with self.subTest(failed_call=failed_call):
                before = self.library.stat()
                results = [None] * failed_call + [
                    subprocess.CalledProcessError(1, "codesign")]
                with mock.patch.object(terrain.subprocess, "run", side_effect=results):
                    with self.assertRaises(subprocess.CalledProcessError):
                        terrain.patch_library(self.library)
                self.assert_original_preserved(before)

    def test_unexpected_signed_bytes_preserve_original(self):
        before = self.library.stat()

        def unexpected_signature(command, **kwargs):
            if "--sign" in command:
                Path(command[-1]).write_bytes(b"unexpected signed output")

        with mock.patch.object(terrain.subprocess, "run", side_effect=unexpected_signature):
            with self.assertRaises(RuntimeError):
                terrain.patch_library(self.library)
        self.assert_original_preserved(before)

    def test_already_corrected_file_is_not_rewritten(self):
        self.library.write_bytes(self.corrected)
        before = self.library.stat()
        with mock.patch.object(terrain.subprocess, "run") as signer:
            result = terrain.patch_library(self.library)
            signer.assert_not_called()
        self.assertTrue(result["already_applied"])
        self.assertEqual(self.library.stat().st_ino, before.st_ino)
        self.assertEqual(self.library.stat().st_mtime_ns, before.st_mtime_ns)

    def test_replacement_leaves_an_open_original_unchanged(self):
        self.library.chmod(0o755)
        with self.library.open("rb") as original_handle:
            with mock.patch.object(terrain.subprocess, "run"):
                result = terrain.patch_library(self.library)
            self.assertEqual(original_handle.read(), self.original)
        self.assertFalse(result["already_applied"])
        self.assertEqual(self.library.read_bytes(), self.corrected)
        self.assertEqual(self.library.stat().st_mode & 0o777, 0o755)


if __name__ == "__main__":
    unittest.main()
