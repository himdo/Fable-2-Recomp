"""Check Debug/Release SDK staging with synthetic files, without game content."""
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
from unittest.mock import patch

import pytest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("staging", ROOT / "tools/stage_renderer_sdk.py")
staging = importlib.util.module_from_spec(spec)
spec.loader.exec_module(staging)

SHARED_FILES = ["include/rex/version.h", "lib/cmake/rexglue/rexglueConfig.cmake"]
RELEASE_FILES = SHARED_FILES + ["bin/rexglue.exe", "bin/rexruntime.dll", "bin/rexgpu-xenos.dll",
                                "lib/rexruntime.lib", "lib/rexgpu-xenos.lib"]
# Only Release installs the codegen tool (rexglue.exe).
DEBUG_FILES = SHARED_FILES + ["bin/rexruntimed.dll", "bin/rexgpu-xenosd.dll",
                              "lib/rexruntimed.lib", "lib/rexgpu-xenosd.lib"]


def make_install(output, names):
    """Write each install file with its own content, so a hash of the wrong file shows up."""
    for name in names:
        path = output / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("built-" + name, encoding="utf-8")


def run_staging(source, output, *options):
    """Run the staging script on a fake install and return the provenance record it writes."""
    argv = ["stage_renderer_sdk.py", str(source), str(output), *options]
    with (
        patch("sys.argv", argv),
        patch.object(staging.subprocess, "check_output", return_value="synthetic-revision\n"),
        contextlib.redirect_stdout(io.StringIO()),
    ):
        staging.main()
    return json.loads((output / "renderer-sdk-provenance.json").read_text(encoding="utf-8"))


def sha256_of(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def make_build(base, configuration, missing=None):
    """Write what staging reads for one configuration, and return (source, output).

    The install tree goes in output/, and the symbols in source/out/win-amd64, as the SDK
    build leaves them. The file named `missing`, if any, is left out.
    """
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
    return source, output


def describe_main():
    # --- Happy flow -------------------------------------------------------

    def test_should_stage_the_debug_pair_without_the_codegen_tool(tmp_path):
        source, output = make_build(tmp_path, "Debug")

        record = run_staging(source, output, "--configuration", "Debug")

        assert (output / "bin/rexruntimed.pdb").read_text() == "symbols-rexruntimed.pdb"
        assert (output / "bin/rexgpu-xenosd.pdb").read_text() == "symbols-rexgpu-xenosd.pdb"
        assert record["configuration"] == "Debug"
        assert record["sdk_commit"] == "synthetic-revision"

    def test_should_stage_the_release_pair(tmp_path):
        source, output = make_build(tmp_path, "Release")

        record = run_staging(source, output, "--configuration", "Release")

        assert (output / "bin/rexruntime.pdb").read_text() == "symbols-rexruntime.pdb"
        assert (output / "bin/rexgpu-xenos.pdb").read_text() == "symbols-rexgpu-xenos.pdb"
        assert record["configuration"] == "Release"
        assert record["sdk_commit"] == "synthetic-revision"

    # --- Bad flow ---------------------------------------------------------

    def test_should_fail_without_the_release_codegen_tool(tmp_path):
        source, output = make_build(tmp_path, "Release", missing="rexglue.exe")

        with pytest.raises(SystemExit, match="Missing build input"):
            run_staging(source, output, "--configuration", "Release")

        assert not (output / "renderer-sdk-provenance.json").exists()

    def test_should_fail_without_the_debug_runtime(tmp_path):
        source, output = make_build(tmp_path, "Debug", missing="rexruntimed.dll")

        with pytest.raises(SystemExit, match="Missing build input"):
            run_staging(source, output, "--configuration", "Debug")

        assert not (output / "renderer-sdk-provenance.json").exists()

    def test_should_fail_without_the_debug_plugin_library(tmp_path):
        source, output = make_build(tmp_path, "Debug", missing="rexgpu-xenosd.lib")

        with pytest.raises(SystemExit, match="Missing build input"):
            run_staging(source, output, "--configuration", "Debug")

        assert not (output / "renderer-sdk-provenance.json").exists()

    # --- Edge cases -------------------------------------------------------

    def test_should_work_without_symbols(tmp_path):
        # PDBs are optional: the source folder has no out/win-amd64 build output at all.
        source, output = tmp_path / "source", tmp_path / "output"
        make_install(output, RELEASE_FILES)

        record = run_staging(source, output)

        assert record["configuration"] == "Release"
        assert list((output / "bin").glob("*.pdb")) == []


def describe_provenance_record():
    def test_should_hash_each_release_file(tmp_path):
        source, output = tmp_path / "source", tmp_path / "output"
        make_install(output, RELEASE_FILES)

        record = run_staging(source, output)

        assert record["runtime_sha256"] == sha256_of(output / "bin/rexruntime.dll")
        assert record["plugin_sha256"] == sha256_of(output / "bin/rexgpu-xenos.dll")
        assert record["codegen_tool_sha256"] == sha256_of(output / "bin/rexglue.exe")

    def test_should_hash_the_debug_files_for_a_debug_build(tmp_path):
        source, output = tmp_path / "source", tmp_path / "output"
        make_install(output, DEBUG_FILES)

        record = run_staging(source, output, "--configuration", "Debug")

        assert record["runtime_sha256"] == sha256_of(output / "bin/rexruntimed.dll")
        assert record["plugin_sha256"] == sha256_of(output / "bin/rexgpu-xenosd.dll")

    def test_should_leave_out_the_tool_hash_for_a_debug_build(tmp_path):
        source, output = tmp_path / "source", tmp_path / "output"
        make_install(output, DEBUG_FILES)

        record = run_staging(source, output, "--configuration", "Debug")

        assert "codegen_tool_sha256" not in record


def describe_codegen_stamp():
    def test_should_keep_the_stamp_when_the_tool_is_unchanged(tmp_path):
        source, output, stamp = tmp_path / "source", tmp_path / "output", tmp_path / "codegen.stamp"
        make_install(output, RELEASE_FILES)
        run_staging(source, output, "--codegen-stamp", str(stamp))
        stamp.write_text("fingerprint", encoding="utf-8")

        run_staging(source, output, "--codegen-stamp", str(stamp))

        assert stamp.exists()

    def test_should_clear_the_stamp_when_the_tool_changes(tmp_path):
        source, output, stamp = tmp_path / "source", tmp_path / "output", tmp_path / "codegen.stamp"
        make_install(output, RELEASE_FILES)
        run_staging(source, output, "--codegen-stamp", str(stamp))
        stamp.write_text("fingerprint", encoding="utf-8")
        (output / "bin/rexglue.exe").write_text("rebuilt", encoding="utf-8")

        run_staging(source, output, "--codegen-stamp", str(stamp))

        assert not stamp.exists()

    def test_should_clear_the_stamp_on_the_first_release_staging(tmp_path):
        # With no earlier record there is no proof the tool is unchanged, so the stamp goes.
        source, output, stamp = tmp_path / "source", tmp_path / "output", tmp_path / "codegen.stamp"
        make_install(output, RELEASE_FILES)
        stamp.write_text("fingerprint", encoding="utf-8")

        run_staging(source, output, "--codegen-stamp", str(stamp))

        assert not stamp.exists()

    def test_should_keep_the_stamp_on_a_debug_staging(tmp_path):
        # Debug and Release share one stamp, and only Release installs the tool, so a
        # Debug run must leave the stamp alone even if the earlier record looks out of date.
        source, output, stamp = tmp_path / "source", tmp_path / "output", tmp_path / "codegen.stamp"
        make_install(output, DEBUG_FILES)
        (output / "renderer-sdk-provenance.json").write_text(
            json.dumps({"codegen_tool_sha256": "old"}), encoding="utf-8")
        stamp.write_text("fingerprint", encoding="utf-8")

        run_staging(source, output, "--configuration", "Debug", "--codegen-stamp", str(stamp))

        assert stamp.exists()
