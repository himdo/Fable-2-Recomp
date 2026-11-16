"""Check how prepare_runtime_sdk.py validates the SDK checkout, using small throwaway git repos."""
import contextlib
import importlib.util
import io
from pathlib import Path
import subprocess
from unittest.mock import patch

import pytest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("prepare", ROOT / "tools/prepare_runtime_sdk.py")
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)

# Settings for the test repos only: a throwaway identity, and no commit signing,
# so a signing key in the developer's own git config is never asked for.
GIT_SETTINGS = [
    "-c", "user.name=Test",
    "-c", "user.email=test@example.com",
    "-c", "commit.gpgsign=false",
    "-c", "init.defaultBranch=main",
]


def git(directory, *args):
    subprocess.run(["git", *GIT_SETTINGS, "-C", str(directory), *args],
                   check=True, capture_output=True)


def make_checkout(directory, files):
    """Create a git repo with these {path: text} files committed, or no commit if empty."""
    git(directory, "init")
    for name, text in files.items():
        path = directory / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    if files:
        git(directory, "add", "-A")
        git(directory, "commit", "-m", "Test checkout")


# Stands in for the SDK's own libmspack pin, which its public remote no longer has.
BROKEN_LIBMSPACK_PIN = "1" * 40


def make_gitlink_checkout(directory):
    """Create a git repo that records libmspack as a submodule gitlink at the broken pin.

    There is no .gitmodules entry, so `git submodule update` fails at once without
    going to the network.
    """
    make_checkout(directory, {"CMakeLists.txt": "project(sdk)"})
    git(directory, "update-index", "--add", "--cacheinfo",
        f"160000,{BROKEN_LIBMSPACK_PIN},thirdparty/libmspack")
    git(directory, "commit", "-m", "Add libmspack as a submodule")


def libmspack_index_entry(directory):
    """Return git's index line for thirdparty/libmspack: mode, commit, stage and path."""
    result = subprocess.run(["git", "-C", str(directory), "ls-files", "-s", "thirdparty/libmspack"],
                            check=True, capture_output=True, text=True)
    return result.stdout


def run_prepare(*args):
    """Run the script with these arguments and return its exit code and error output."""
    argv = ["prepare_runtime_sdk.py", *args]
    errors = io.StringIO()
    with patch("sys.argv", argv), contextlib.redirect_stderr(errors):
        try:
            prepare.main()
        except SystemExit as exit_request:
            return exit_request.code, errors.getvalue()
    return 0, errors.getvalue()


def describe_checkout_checks():
    # --- Bad flow ---------------------------------------------------------

    def test_should_fail_for_a_missing_folder(tmp_path):
        code, errors = run_prepare(str(tmp_path / "missing"))

        assert code == 1
        assert "does not exist" in errors

    def test_should_fail_for_a_folder_that_is_not_a_checkout(tmp_path):
        code, errors = run_prepare(str(tmp_path))

        assert code == 1
        assert "is not a git work tree" in errors

    # --- Known issues -----------------------------------------------------

    @pytest.mark.xfail(strict=True, reason="`git rev-parse HEAD` prints HEAD even when it fails, so "
                       "an empty repo passes the check")
    def test_should_fail_for_a_checkout_without_commits(tmp_path):
        # In a repo with no commits, `git rev-parse HEAD` fails but still prints "HEAD",
        # and the script only checks that something was printed. The check is skipped,
        # and the run fails later with "libmspack is missing" instead.
        make_checkout(tmp_path, {})

        code, errors = run_prepare(str(tmp_path))

        assert code == 1
        assert "has no commits" in errors

    @pytest.mark.xfail(strict=True, reason="git answers for the parent repo, so an unfetched "
                       "submodule folder passes as a checkout")
    def test_should_fail_for_an_unfetched_submodule_folder(tmp_path):
        # A never-fetched SDK submodule is an empty folder inside the Fable repo, so git
        # answers for the Fable repo and the folder passes as a checkout. The run fails
        # with "libmspack is missing" instead of saying to fetch the submodule.
        make_checkout(tmp_path, {"README.md": "Fable repo"})
        source = tmp_path / "thirdparty/rexglue-sdk"
        source.mkdir(parents=True)

        code, errors = run_prepare(str(source))

        assert code == 1
        assert "is not a git work tree" in errors

    # --- Edge cases -------------------------------------------------------

    def test_should_accept_a_checkout_with_skip_dependencies(tmp_path):
        make_checkout(tmp_path, {"CMakeLists.txt": "project(sdk)"})

        response = run_prepare(str(tmp_path), "--skip-dependencies")

        assert response == (0, "")

    def test_should_leave_libmspack_alone_with_skip_dependencies(tmp_path):
        # Only the checks run, so the libmspack marker folder must not be created.
        make_checkout(tmp_path, {"thirdparty/libmspack/mspack.h": "header"})

        code, _ = run_prepare(str(tmp_path), "--skip-dependencies")

        assert code == 0
        assert not (tmp_path / "thirdparty/libmspack/.git").exists()


def describe_libmspack_setup():
    # --- Happy flow -------------------------------------------------------

    def test_should_give_vendored_libmspack_a_git_marker(tmp_path):
        # The SDK's CMake treats a dependency as set up when <dependency>/.git exists, and
        # libmspack stored as plain files has none, so the script adds an empty folder.
        make_checkout(tmp_path, {"thirdparty/libmspack/mspack.h": "header"})

        code, _ = run_prepare(str(tmp_path))

        assert code == 0
        assert (tmp_path / "thirdparty/libmspack/.git").is_dir()

    def test_should_repin_libmspack_stored_as_a_submodule(tmp_path):
        # `git submodule update` reads the index, so the script points the index at a commit
        # the public remote still has. The run then fails at the update, as there is no
        # remote here, but the repin has already happened.
        make_gitlink_checkout(tmp_path)

        code, _ = run_prepare(str(tmp_path))

        assert code == 1
        assert prepare.MSPACK_PIN in libmspack_index_entry(tmp_path)

    # --- Bad flow ---------------------------------------------------------

    def test_should_fail_when_libmspack_is_missing(tmp_path):
        make_checkout(tmp_path, {"CMakeLists.txt": "project(sdk)"})

        code, errors = run_prepare(str(tmp_path))

        assert code == 1
        assert "libmspack is missing" in errors

    def test_should_count_uncommitted_libmspack_as_missing(tmp_path):
        # The script looks at the last commit (HEAD), not at the files on disk.
        make_checkout(tmp_path, {"CMakeLists.txt": "project(sdk)"})
        header = tmp_path / "thirdparty/libmspack/mspack.h"
        header.parent.mkdir(parents=True)
        header.write_text("header", encoding="utf-8")

        code, errors = run_prepare(str(tmp_path))

        assert code == 1
        assert "libmspack is missing" in errors

    def test_should_mention_network_access_when_the_submodule_update_fails(tmp_path):
        # A fresh clone without network access is the usual cause, so the error says so.
        make_gitlink_checkout(tmp_path)

        code, errors = run_prepare(str(tmp_path))

        assert code == 1
        assert "submodule update failed" in errors
        assert "network access" in errors

    # --- Edge cases -------------------------------------------------------

    def test_should_keep_the_vendored_marker_on_a_second_run(tmp_path):
        # The build runs this step every time, so a second run must succeed too.
        make_checkout(tmp_path, {"thirdparty/libmspack/mspack.h": "header"})
        first_code, _ = run_prepare(str(tmp_path))

        second_code, _ = run_prepare(str(tmp_path))

        assert first_code == 0
        assert second_code == 0
        assert (tmp_path / "thirdparty/libmspack/.git").is_dir()
