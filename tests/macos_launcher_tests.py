"""Check backend routing without starting a game or reading real saves."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[1] / 'run_macos.command'

class LauncherTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        shutil.copy2(SOURCE, self.root / SOURCE.name)
        self.bin = self.root / 'bin'
        self.bin.mkdir()
        self.script(self.bin / 'uname', 'if [ "$1" = -s ]; then echo Darwin; else echo "${TEST_ARCH:-arm64}"; fi')
        for arch in ('arm64', 'amd64'):
            self.script(self.root / f'out/build/mac-{arch}-release/fable_2', 'printf "vulkan\\n"; printf "%s\\n" "$@"')
        self.env = dict(os.environ, PATH=str(self.bin)+':'+os.environ['PATH'], FABLE2_MACOS_TERRAIN_FIX='0', FABLE2_MACOS_BACKEND='auto')
    def script(self, path, text):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#!/bin/bash\n'+text+'\n')
        path.chmod(0o755)
    def metal(self):
        self.script(self.root/'run_macos_metal.command', 'printf "metal\\n"; printf "%s\\n" "$@"')
        self.script(self.root/'out/build/mac-arm64-metal-release/fable_2', 'exit 99')
        (self.root/'out/build/mac-arm64-metal-release/librexgpu-metal.dylib').touch()
    def run_launcher(self, backend='auto', arch='arm64'):
        return subprocess.run(['bash',str(self.root/SOURCE.name),'--test=value with spaces'], env=dict(self.env,FABLE2_MACOS_BACKEND=backend,TEST_ARCH=arch),text=True,capture_output=True)
    def test_fresh_clone_falls_back(self):
        p=self.run_launcher(); self.assertEqual(p.returncode,0); self.assertTrue(p.stdout.startswith('vulkan\n'))
    def test_installed_metal_and_arguments(self):
        self.metal();p=self.run_launcher();self.assertEqual(p.stdout,'metal\n--test=value with spaces\n')
    def test_explicit_vulkan(self):
        self.metal();self.assertTrue(self.run_launcher('vulkan').stdout.startswith('vulkan\n'))
    def test_missing_metal_reports_error(self):
        p=self.run_launcher('metal');self.assertEqual(p.returncode,1);self.assertIn('not installed',p.stderr)
    def test_intel_retains_vulkan(self):
        self.metal();self.assertTrue(self.run_launcher(arch='x86_64').stdout.startswith('vulkan\n'))
    def test_invalid_backend(self):
        self.assertEqual(self.run_launcher('typo').returncode,1)
    def test_partial_install_falls_back(self):
        self.metal();(self.root/'out/build/mac-arm64-metal-release/librexgpu-metal.dylib').unlink()
        self.assertTrue(self.run_launcher().stdout.startswith('vulkan\n'))

if __name__ == '__main__': unittest.main()
