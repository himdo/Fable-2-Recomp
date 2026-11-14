"""Catalogue validation without game files or modifying the source catalogue."""
import contextlib
import copy
import importlib.util
import io
import json
from pathlib import Path
from unittest.mock import patch

import pytest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("generator", ROOT / "tools/generate_game_versions.py")
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


def make_root(directory, catalogue):
    """Write the catalogue and its up-to-date generated files into a fake repo, and return it."""
    root = Path(directory)
    (root / "constants").mkdir()
    (root / "constants/game_versions.json").write_text(json.dumps(catalogue), encoding="utf-8")
    with patch.object(generator, "ROOT", root):
        for name, lines in generator.generate().items():
            (root / "constants" / name).write_text("\n".join(lines) + "\n", encoding="utf-8")
    return root


def run_check(root):
    """Run the generator with --check against a fake repo."""
    argv = ["generate_game_versions.py", "--check"]
    with patch.object(generator, "ROOT", root), patch("sys.argv", argv), \
            contextlib.redirect_stdout(io.StringIO()):
        generator.main()


@pytest.fixture
def catalogue():
    """A fresh copy of the real catalogue, which each test may change."""
    return json.loads((ROOT / "constants/game_versions.json").read_text())


@pytest.fixture
def generate(tmp_path, monkeypatch):
    """Return a function that generates the files for a catalogue, in a fake repo.

    The real constants/ folder is never touched: ROOT points at tmp_path until the test ends.
    """
    (tmp_path / "constants").mkdir()
    monkeypatch.setattr(generator, "ROOT", tmp_path)

    def run(catalogue):
        (tmp_path / "constants/game_versions.json").write_text(json.dumps(catalogue))
        return generator.generate()

    return run


def describe_generate():
    # --- Happy flow -------------------------------------------------------

    def test_should_match_the_committed_generated_files(catalogue, generate):
        generated = generate(catalogue)

        expected = {name: "\n".join(lines) + "\n" for name, lines in generated.items()}
        committed = {name: (ROOT / "constants" / name).read_text() for name in generated}
        assert committed == expected

    def test_should_write_an_extra_locale_into_every_language(catalogue, generate):
        extra = copy.deepcopy(catalogue["versions"][0])
        extra.update(id="synthetic-locale", hash="a" * 64, name="Synthetic locale", language=12)
        catalogue["versions"].append(extra)

        generated = generate(catalogue)

        missing_from = [name for name, lines in generated.items() if "synthetic-locale" not in "\n".join(lines)]
        assert missing_from == []

    def test_should_keep_incompatible_versions_out_of_the_cmake_profiles(catalogue, generate):
        # The C# and C++ files still list an incompatible version, so the launcher can name
        # it, but CMake must not build a profile for it.
        # Guard: a version from another code group would stay out anyway, and pass for the
        # wrong reason. The real incompatible versions are all in another code group.
        assert catalogue["versions"][1]["codeGroup"] == catalogue["codeGroup"]
        catalogue["versions"][1]["compatible"] = False
        incompatible_id = catalogue["versions"][1]["id"]

        generated = generate(catalogue)

        assert f'"{incompatible_id}"' in "\n".join(generated["GameVersions.cs"])
        assert f'"{incompatible_id}"' not in "\n".join(generated["game_versions.cmake"])

    def test_should_keep_versions_from_another_code_group_out_of_the_cmake_profiles(catalogue, generate):
        # Only versions that share the validated guest code can use the same build.
        # Guard: an incompatible version would stay out anyway, and pass for the wrong reason.
        assert catalogue["versions"][1]["compatible"]
        catalogue["versions"][1]["codeGroup"] = "another-code-group"
        other_id = catalogue["versions"][1]["id"]

        generated = generate(catalogue)

        assert f'"{other_id}"' not in "\n".join(generated["game_versions.cmake"])

    # --- Bad flow ---------------------------------------------------------

    def test_should_reject_an_invalid_hash(catalogue, generate):
        catalogue["versions"][0]["hash"] = "invalid"

        with pytest.raises(ValueError):
            generate(catalogue)

    def test_should_reject_an_id_that_is_a_path(catalogue, generate):
        catalogue["versions"][0]["id"] = "../bad"

        with pytest.raises(ValueError):
            generate(catalogue)

    def test_should_reject_an_unknown_language(catalogue, generate):
        catalogue["versions"][0]["language"] = 13

        with pytest.raises(ValueError):
            generate(catalogue)

    def test_should_reject_compatible_written_as_text(catalogue, generate):
        catalogue["versions"][0]["compatible"] = "true"

        with pytest.raises(ValueError):
            generate(catalogue)

    def test_should_reject_a_required_file_outside_the_game_folder(catalogue, generate):
        catalogue["versions"][0]["requiredFiles"] = ["../gamefile"]

        with pytest.raises(ValueError):
            generate(catalogue)

    def test_should_reject_required_files_that_are_not_a_list(catalogue, generate):
        catalogue["versions"][0]["requiredFiles"] = "not-an-array"

        with pytest.raises(ValueError):
            generate(catalogue)

    def test_should_reject_a_duplicate_version(catalogue, generate):
        catalogue["versions"].append(copy.deepcopy(catalogue["versions"][0]))

        with pytest.raises(ValueError):
            generate(catalogue)

    def test_should_reject_an_incompatible_default_profile(catalogue, generate):
        catalogue["versions"][0]["compatible"] = False  # versions[0] is the default profile

        with pytest.raises(ValueError, match="Default version must belong"):
            generate(catalogue)

    def test_should_reject_a_default_profile_from_another_code_group(catalogue, generate):
        catalogue["versions"][0]["codeGroup"] = "another-code-group"

        with pytest.raises(ValueError, match="Default version must belong"):
            generate(catalogue)

    @pytest.mark.xfail(strict=True, reason="next() finds no version with that id and raises a bare "
                       "StopIteration, so the error doesn't say what is wrong")
    def test_should_reject_an_unknown_default_profile(catalogue, generate):
        # Every other check raises a ValueError that names the problem.
        catalogue["defaultProfile"] = "no-such-profile"

        with pytest.raises(ValueError, match="(?i)default"):
            generate(catalogue)


def describe_check():
    def test_should_pass_when_the_generated_files_are_current(catalogue, tmp_path):
        root = make_root(tmp_path, catalogue)

        run_check(root)  # raises SystemExit if anything is stale

    def test_should_report_a_stale_generated_file(catalogue, tmp_path):
        root = make_root(tmp_path, catalogue)
        (root / "constants/game_versions.h").write_text("// edited by hand\n", encoding="utf-8")

        with pytest.raises(SystemExit, match="Stale constants: game_versions.h"):
            run_check(root)
