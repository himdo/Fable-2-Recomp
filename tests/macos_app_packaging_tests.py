#!/usr/bin/env python3
"""Publishing must never destroy the previous app on a failed rebuild."""
import importlib.util
from pathlib import Path
import plistlib
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('packager', Path(__file__).resolve().parents[1] / 'tools/package_macos_app.py')
packager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packager)

class PublishTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.old = self.make_app('Fable II.app', 'old')
        self.new = self.make_app('staging/Fable II.app', 'new')
        self.state = self.root / 'Application Support/saves/hero.bin'
        self.state.parent.mkdir(parents=True)
        self.state.write_bytes(b'player progress')

    def make_app(self, name, value):
        app = self.root / name
        (app / 'Contents').mkdir(parents=True)
        (app / 'Contents/Info.plist').write_bytes(plistlib.dumps({'CFBundleIdentifier': 'org.fable2recomp.metal'}))
        (app / 'Contents/version').write_text(value)
        return app

    def test_repeat_build_replaces_app_and_preserves_external_state(self):
        packager.publish(self.new, self.old, replace=True)
        self.assertEqual((self.old / 'Contents/version').read_text(), 'new')
        self.assertEqual(self.state.read_bytes(), b'player progress')

    def test_failed_publish_restores_previous_app(self):
        rename = Path.rename
        def fail(source, destination):
            if source == self.new:
                raise OSError('simulated publication failure')
            return rename(source, destination)
        with patch.object(Path, 'rename', fail):
            with self.assertRaises(OSError):
                packager.publish(self.new, self.old, replace=True)
        self.assertEqual((self.old / 'Contents/version').read_text(), 'old')

    def test_unrelated_app_is_preserved(self):
        (self.old / 'Contents/Info.plist').write_bytes(plistlib.dumps({'CFBundleIdentifier':'other.app'}))
        with self.assertRaises(ValueError):
            packager.publish(self.new, self.old, replace=True)
        self.assertEqual((self.old / 'Contents/version').read_text(), 'old')

    def test_symlink_is_not_followed(self):
        link = self.root / 'linked.app'
        link.symlink_to(self.old)
        with self.assertRaises(ValueError):
            packager.publish(self.new, link, replace=True)
        self.assertEqual((self.old / 'Contents/version').read_text(), 'old')

    def test_manual_packaging_needs_explicit_replace(self):
        with self.assertRaises(FileExistsError):
            packager.publish(self.new, self.old)
        self.assertEqual((self.old / 'Contents/version').read_text(), 'old')

if __name__ == '__main__':
    unittest.main()
