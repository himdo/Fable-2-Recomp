"""Check Debug/Release SDK staging with synthetic files, without game content."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("staging", ROOT / "tools/stage_renderer_sdk.py")
staging = importlib.util.module_from_spec(spec)
spec.loader.exec_module(staging)


class RuntimeStagingTests(unittest.TestCase):
    def stage(self, configuration, missing=None):
        with tempfile.TemporaryDirectory(prefix="fable2-sdk-test-") as directory:
            base = Path(directory)
            source, output = base / "source", base / "output"
            suffix = "d" if configuration == "Debug" else ""
            names = [f"{stem}{suffix}.{extension}" for stem in ("rexruntime", "rexgpu-xenos")
                     for extension in ("dll", "lib", "pdb")]
            if configuration == "Release":
                names.append("rexglue.exe")
            files = {
                output / "include/rex/version.h": "version",
                output / "lib/cmake/rexglue/rexglueConfig.cmake": "config",
            }
            for name in names:
                if name == missing:
                    continue
                if name.endswith(".pdb"):
                    files[source / "out/win-amd64" / name] = "symbols-" + name
                else:
                    folder = "lib" if name.endswith(".lib") else "bin"
                    files[output / folder / name] = "built-" + name
            for path, value in files.items():
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(value)
            args = ["stage_renderer_sdk.py", str(source), str(output), "--configuration", configuration]
            with patch("sys.argv", args), patch.object(staging.subprocess, "check_output",
                                                       return_value="synthetic-revision\n"):
                if missing:
                    with self.assertRaisesRegex(SystemExit, "Missing build input"):
                        staging.main()
                    self.assertFalse((output / "renderer-sdk-provenance.json").exists())
                    return
                staging.main()
            for stem in ("rexruntime", "rexgpu-xenos"):
                name = f"{stem}{suffix}.pdb"
                self.assertEqual((output / "bin" / name).read_text(), "symbols-" + name)
            provenance = json.loads((output / "renderer-sdk-provenance.json").read_text())
            self.assertEqual(provenance["configuration"], configuration)
            self.assertEqual(provenance["sdk_commit"], "synthetic-revision")

    def test_new_tool_clears_codegen_stamp(self):
        with tempfile.TemporaryDirectory(prefix="fable2-sdk-test-") as directory:
            base = Path(directory)
            source, output, stamp = base / "source", base / "output", base / "codegen.stamp"
            files = ["include/rex/version.h", "lib/cmake/rexglue/rexglueConfig.cmake", "bin/rexglue.exe",
                     "bin/rexruntime.dll", "bin/rexgpu-xenos.dll", "lib/rexruntime.lib", "lib/rexgpu-xenos.lib"]
            for name in files:
                (output / name).parent.mkdir(parents=True, exist_ok=True)
                (output / name).write_text("built")
            args = ["stage_renderer_sdk.py", str(source), str(output), "--codegen-stamp", str(stamp)]

            def run():
                with patch("sys.argv", args), patch.object(staging.subprocess, "check_output",
                                                           return_value="synthetic-revision\n"):
                    staging.main()

            run()
            stamp.write_text("fingerprint")
            run()
            self.assertTrue(stamp.exists())
            (output / "bin/rexglue.exe").write_text("rebuilt")
            run()
            self.assertFalse(stamp.exists())

    def test_debug_uses_debug_pair_without_tool(self):
        self.stage("Debug")

    def test_release_uses_release_pair(self):
        self.stage("Release")

    def test_missing_release_tool_fails(self):
        self.stage("Release", missing="rexglue.exe")

    def test_missing_debug_runtime_fails(self):
        self.stage("Debug", missing="rexruntimed.dll")

    def test_missing_debug_plugin_library_fails(self):
        self.stage("Debug", missing="rexgpu-xenosd.lib")


if __name__ == "__main__":
    unittest.main()
