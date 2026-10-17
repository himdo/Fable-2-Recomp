"""Check Debug/Release SDK staging with synthetic files, without game content."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("staging", ROOT / "tools/stage_renderer_sdk.py")
staging = importlib.util.module_from_spec(spec)
spec.loader.exec_module(staging)


class RuntimeStagingTests(unittest.TestCase):
    def stage(self, configuration, missing=None):
        with tempfile.TemporaryDirectory(prefix="fable2-sdk-test-") as directory:
            base = Path(directory)
            official, source, build, output = [base / name for name in
                                               ("official", "source", "build", "output")]
            files = {
                official / "bin/rexglue.exe": "official-codegen",
                official / "share/rexglue/rex_app.cpp": "old-app",
                source / "include/rex/test.h": "matched-header",
                source / "thirdparty/sdl3/include/SDL3/test.h": "matched-sdl",
                build / "include/rex/version.h": "matched-version",
                source / "src/ui/rex_app.cpp": "matched-app",
                source / "src/ui/windowed_app_main_sdl.cpp": "matched-entry",
            }
            for suffix in ("", "d"):
                for stem in ("rexruntime", "rexgpu-xenos"):
                    for extension, folder in (("dll", "bin"), ("lib", "lib")):
                        name = f"{stem}{suffix}.{extension}"
                        files[official / folder / name] = "old-" + name
                        if name != missing:
                            files[source / "out/win-amd64" / name] = "patched-" + name
            for path, value in files.items():
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(value)
            args = ["stage_renderer_sdk.py", *map(str, (official, source, build, output)),
                    "--configuration", configuration]
            with patch("sys.argv", args), patch.object(staging.subprocess, "check_output",
                                                       return_value="synthetic-revision\n"):
                if missing:
                    with self.assertRaisesRegex(SystemExit, "Missing build input"):
                        staging.main()
                    self.assertFalse(output.exists())
                    return
                staging.main()
            suffix = "d" if configuration == "Debug" else ""
            for stem in ("rexruntime", "rexgpu-xenos"):
                name = f"{stem}{suffix}.dll"
                self.assertEqual((output / "bin" / name).read_text(), "patched-" + name)
                name = f"{stem}{suffix}.lib"
                self.assertEqual((output / "lib" / name).read_text(), "patched-" + name)
            self.assertEqual((output / "bin/rexglue.exe").read_text(), "official-codegen")
            self.assertEqual((official / "bin" / f"rexruntime{suffix}.dll").read_text(),
                             f"old-rexruntime{suffix}.dll")
            self.assertEqual((output / "include/rex/version.h").read_text(), "matched-version")
            provenance = json.loads((output / "renderer-sdk-provenance.json").read_text())
            self.assertEqual(provenance["configuration"], configuration)
            self.assertEqual(provenance["sdk_commit"], "synthetic-revision")

    def test_debug_uses_patched_debug_pair(self):
        self.stage("Debug")

    def test_release_uses_patched_release_pair(self):
        self.stage("Release")

    def test_missing_debug_runtime_does_not_fall_back_to_official(self):
        self.stage("Debug", missing="rexruntimed.dll")

    def test_missing_debug_plugin_library_fails_before_copying(self):
        self.stage("Debug", missing="rexgpu-xenosd.lib")

    def test_mac_stages_dylibs_into_lib(self):
        with tempfile.TemporaryDirectory(prefix="fable2-sdk-test-") as directory:
            base = Path(directory)
            official, source, build, output = [base / name for name in
                                               ("official", "source", "build", "output")]
            native = source / "out/mac-arm64"
            files = {
                official / "bin/rexglue": "official-codegen",
                official / "lib/librexruntime.dylib": "old-runtime",
                official / "lib/libMoltenVK.dylib": "old-moltenvk",
                official / "share/rexglue/rex_app.cpp": "old-app",
                native / "librexruntime.dylib": "patched-runtime",
                native / "librexgpu-xenos.dylib": "patched-plugin",
                source / "include/rex/test.h": "matched-header",
                source / "thirdparty/sdl3/include/SDL3/test.h": "matched-sdl",
                build / "include/rex/version.h": "matched-version",
                source / "src/ui/rex_app.cpp": "matched-app",
                source / "src/ui/windowed_app_main_sdl.cpp": "matched-entry",
            }
            for path, value in files.items():
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(value)
            args = ["stage_renderer_sdk.py", *map(str, (official, source, build, output)),
                    "--platform", "mac-arm64"]
            with patch("sys.argv", args), patch.object(staging.subprocess, "check_output",
                                                       return_value="synthetic-revision\n"):
                staging.main()
            self.assertEqual((output / "lib/librexruntime.dylib").read_text(), "patched-runtime")
            self.assertEqual((output / "lib/librexgpu-xenos.dylib").read_text(), "patched-plugin")
            # MoltenVK was not rebuilt: the official one stays.
            self.assertEqual((output / "lib/libMoltenVK.dylib").read_text(), "old-moltenvk")
            self.assertEqual((output / "bin/rexglue").read_text(), "official-codegen")


if __name__ == "__main__":
    unittest.main()
