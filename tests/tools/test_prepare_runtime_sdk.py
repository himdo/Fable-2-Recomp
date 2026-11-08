"""Check how prepare_runtime_sdk.py validates the SDK checkout, using small throwaway git repos."""
import contextlib
import importlib.util
import io
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

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


class PrepareRuntimeSdkCheckoutTests(unittest.TestCase):
    def test_missing_folder_fails(self):
        with tempfile.TemporaryDirectory(prefix="fable2-prepare-test-") as directory:
            source = Path(directory) / "missing"
            code, errors = run_prepare(str(source))

        self.assertEqual(code, 1)
        self.assertIn("does not exist", errors)

    def test_folder_that_is_not_a_checkout_fails(self):
        with tempfile.TemporaryDirectory(prefix="fable2-prepare-test-") as directory:
            code, errors = run_prepare(directory)

        self.assertEqual(code, 1)
        self.assertIn("is not a git work tree", errors)

    @unittest.expectedFailure
    def test_checkout_without_commits_fails(self):
        # Known issue: in a repo with no commits, `git rev-parse HEAD` fails but still
        # prints "HEAD", and the script only checks that something was printed. The check
        # is skipped, and the run fails later with "libmspack is missing" instead.
        with tempfile.TemporaryDirectory(prefix="fable2-prepare-test-") as directory:
            source = Path(directory)
            make_checkout(source, {})
            code, errors = run_prepare(str(source))

        self.assertEqual(code, 1)
        self.assertIn("has no commits", errors)

    @unittest.expectedFailure
    def test_unfetched_submodule_folder_fails(self):
        # Known issue: a never-fetched SDK submodule is an empty folder inside the Fable
        # repo, so git answers for the Fable repo and the folder passes as a checkout. The
        # run fails with "libmspack is missing" instead of saying to fetch the submodule.
        with tempfile.TemporaryDirectory(prefix="fable2-prepare-test-") as directory:
            parent = Path(directory)
            make_checkout(parent, {"README.md": "Fable repo"})
            source = parent / "thirdparty/rexglue-sdk"
            source.mkdir(parents=True)
            code, errors = run_prepare(str(source))

        self.assertEqual(code, 1)
        self.assertIn("is not a git work tree", errors)

    def test_skip_dependencies_accepts_a_checkout(self):
        with tempfile.TemporaryDirectory(prefix="fable2-prepare-test-") as directory:
            source = Path(directory)
            make_checkout(source, {"CMakeLists.txt": "project(sdk)"})
            expected = (0, "")
            response = run_prepare(str(source), "--skip-dependencies")

        self.assertEqual(response, expected)

    def test_skip_dependencies_leaves_libmspack_alone(self):
        # Only the checks run, so the libmspack marker folder must not be created.
        with tempfile.TemporaryDirectory(prefix="fable2-prepare-test-") as directory:
            source = Path(directory)
            make_checkout(source, {"thirdparty/libmspack/mspack.h": "header"})
            code, _ = run_prepare(str(source), "--skip-dependencies")
            marker_exists = (source / "thirdparty/libmspack/.git").exists()

        self.assertEqual(code, 0)
        self.assertFalse(marker_exists)


class PrepareRuntimeSdkLibmspackTests(unittest.TestCase):
    def test_missing_libmspack_fails(self):
        with tempfile.TemporaryDirectory(prefix="fable2-prepare-test-") as directory:
            source = Path(directory)
            make_checkout(source, {"CMakeLists.txt": "project(sdk)"})
            code, errors = run_prepare(str(source))

        self.assertEqual(code, 1)
        self.assertIn("libmspack is missing", errors)

    def test_uncommitted_libmspack_counts_as_missing(self):
        # The script looks at the last commit (HEAD), not at the files on disk.
        with tempfile.TemporaryDirectory(prefix="fable2-prepare-test-") as directory:
            source = Path(directory)
            make_checkout(source, {"CMakeLists.txt": "project(sdk)"})
            header = source / "thirdparty/libmspack/mspack.h"
            header.parent.mkdir(parents=True)
            header.write_text("header", encoding="utf-8")
            code, errors = run_prepare(str(source))

        self.assertEqual(code, 1)
        self.assertIn("libmspack is missing", errors)

    def test_vendored_libmspack_gets_a_git_marker(self):
        # The SDK's CMake treats a dependency as set up when <dependency>/.git exists, and
        # libmspack stored as plain files has none, so the script adds an empty folder.
        with tempfile.TemporaryDirectory(prefix="fable2-prepare-test-") as directory:
            source = Path(directory)
            make_checkout(source, {"thirdparty/libmspack/mspack.h": "header"})
            code, _ = run_prepare(str(source))
            marker_is_folder = (source / "thirdparty/libmspack/.git").is_dir()

        self.assertEqual(code, 0)
        self.assertTrue(marker_is_folder)


if __name__ == "__main__":
    unittest.main()
