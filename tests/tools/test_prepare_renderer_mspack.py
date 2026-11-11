"""Check how prepare_renderer_mspack.py turns libmspack's symlinks into real files, using small throwaway git repos."""
import contextlib
import importlib.util
import io
import os
from pathlib import Path
import subprocess
import sys
from unittest.mock import patch

import pytest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("renderer_mspack", ROOT / "tools/prepare_renderer_mspack.py")
renderer_mspack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(renderer_mspack)

# Settings for the test repos only: a throwaway identity, and no commit signing,
# so a signing key in the developer's own git config is never asked for.
GIT_SETTINGS = [
    "-c", "user.name=Test",
    "-c", "user.email=test@example.com",
    "-c", "commit.gpgsign=false",
    "-c", "init.defaultBranch=main",
]

SOURCE_TEXT = "int answer = 42;"

# A commit that the test repos are never at.
WRONG_PIN = "0" * 40


def git(directory, *args, stdin=None):
    """Run git in this folder and return what it printed."""
    result = subprocess.run(["git", *GIT_SETTINGS, "-C", str(directory), *args],
                            check=True, capture_output=True, text=True, input=stdin)
    return result.stdout.strip()


def make_dependency(directory, links, files=None):
    """Create a repo holding source.c, these {name: target} symlinks and any extra
    {name: text} files, and return its commit.

    The symlinks are recorded straight into git's index instead of being made on
    disk, because Windows CI can't always create real symlinks. Each one is then
    written into the folder as a small text file holding its target, which is what
    a Windows checkout without symlink support leaves behind.
    """
    git(directory, "init")
    (directory / "source.c").write_text(SOURCE_TEXT, encoding="utf-8")
    for name, text in (files or {}).items():
        (directory / name).write_text(text, encoding="utf-8")
    git(directory, "add", "-A")
    for name, target in links.items():
        blob = git(directory, "hash-object", "-w", "--stdin", stdin=target)
        git(directory, "update-index", "--add", "--cacheinfo", f"120000,{blob},{name}")
        path = directory / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(target, encoding="utf-8")
    git(directory, "commit", "-m", "Test dependency")
    return git(directory, "rev-parse", "HEAD")


def run_tool(directory, pin):
    """Run the script on this folder, with `pin` standing in for the public libmspack pin.

    Returns what it printed.
    """
    argv = ["prepare_renderer_mspack.py", str(directory)]
    output = io.StringIO()
    with patch("sys.argv", argv), patch.object(renderer_mspack, "PUBLIC_PIN", pin), \
            contextlib.redirect_stdout(output):
        renderer_mspack.main()
    return output.getvalue()


def read(path):
    return path.read_text(encoding="utf-8")


@pytest.fixture
def outside(tmp_path):
    """The folder that holds the checkout, so a test can put a file just outside it."""
    return tmp_path


@pytest.fixture
def dependency(outside):
    """An empty folder for the libmspack checkout, one level below `outside`."""
    folder = outside / "dependency"
    folder.mkdir()
    return folder


def describe_main():
    # --- Happy flow -------------------------------------------------------

    def test_should_replace_a_symlink_text_file_with_its_target(dependency):
        commit = make_dependency(dependency, {"link.c": "source.c"})

        run_tool(dependency, pin=commit)

        assert read(dependency / "link.c") == SOURCE_TEXT

    def test_should_report_how_many_files_it_replaced(dependency):
        commit = make_dependency(dependency, {"one.c": "source.c", "two.c": "source.c"})

        output = run_tool(dependency, pin=commit)

        assert "Materialized 2 pinned libmspack symlink files" in output

    def test_should_leave_ordinary_files_alone(dependency):
        # notes.txt holds the same text a symlink leftover would, so only git's
        # file mode tells the tool not to touch it.
        commit = make_dependency(dependency, {"link.c": "source.c"}, files={"notes.txt": "source.c"})

        run_tool(dependency, pin=commit)

        assert read(dependency / "notes.txt") == "source.c"

    def test_should_resolve_a_target_from_the_symlinks_own_folder(dependency):
        commit = make_dependency(dependency, {"sub/link.c": "../source.c"})

        run_tool(dependency, pin=commit)

        assert read(dependency / "sub" / "link.c") == SOURCE_TEXT

    # --- Bad flow ---------------------------------------------------------

    def test_should_stop_when_the_checkout_is_not_at_the_public_pin(dependency):
        make_dependency(dependency, {"link.c": "source.c"})

        with pytest.raises(SystemExit) as caught:
            run_tool(dependency, pin=WRONG_PIN)

        assert f"Check out public libmspack pin {WRONG_PIN}" in str(caught.value)
        assert read(dependency / "link.c") == "source.c"

    def test_should_refuse_a_target_outside_the_checkout(outside, dependency):
        # The outside file exists, so the refusal can only come from where it is.
        (outside / "secret.c").write_text("not part of libmspack", encoding="utf-8")
        commit = make_dependency(dependency, {"link.c": "../secret.c"})

        with pytest.raises(SystemExit) as caught:
            run_tool(dependency, pin=commit)

        assert "Invalid pinned symlink target: link.c" in str(caught.value)
        assert read(dependency / "link.c") == "../secret.c"

    def test_should_refuse_a_target_that_does_not_exist(dependency):
        commit = make_dependency(dependency, {"link.c": "missing.c"})

        with pytest.raises(SystemExit) as caught:
            run_tool(dependency, pin=commit)

        assert "Invalid pinned symlink target: link.c" in str(caught.value)

    def test_should_refuse_to_overwrite_a_local_edit(dependency):
        commit = make_dependency(dependency, {"link.c": "source.c"})
        (dependency / "link.c").write_text("someone's local change", encoding="utf-8")

        with pytest.raises(SystemExit) as caught:
            run_tool(dependency, pin=commit)

        assert "Unexpected local edit; refusing to overwrite" in str(caught.value)
        assert read(dependency / "link.c") == "someone's local change"

    # --- Edge cases -------------------------------------------------------

    def test_should_keep_the_replaced_file_when_run_twice(dependency):
        commit = make_dependency(dependency, {"link.c": "source.c"})
        run_tool(dependency, pin=commit)

        run_tool(dependency, pin=commit)

        assert read(dependency / "link.c") == SOURCE_TEXT

    def test_should_accept_a_symlink_text_file_ending_in_a_newline(dependency):
        commit = make_dependency(dependency, {"link.c": "source.c"})
        # write_bytes, because write_text would turn the newline into \r\n on Windows.
        (dependency / "link.c").write_bytes(b"source.c\n")

        run_tool(dependency, pin=commit)

        assert read(dependency / "link.c") == SOURCE_TEXT

    def test_should_recreate_a_deleted_symlink_file(dependency):
        commit = make_dependency(dependency, {"link.c": "source.c"})
        (dependency / "link.c").unlink()

        run_tool(dependency, pin=commit)

        assert read(dependency / "link.c") == SOURCE_TEXT

    @pytest.mark.skipif(sys.platform == "win32", reason="Windows CI can't always create real symlinks")
    def test_should_skip_a_symlink_that_is_already_real(dependency):
        commit = make_dependency(dependency, {"link.c": "source.c"})
        (dependency / "link.c").unlink()
        os.symlink("source.c", dependency / "link.c")

        output = run_tool(dependency, pin=commit)

        assert (dependency / "link.c").is_symlink()
        assert "Materialized 0 pinned libmspack symlink files" in output
