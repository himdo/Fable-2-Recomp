"""Check how prepare_renderer_mspack.py turns libmspack's symlinks into real files, using small throwaway git repos."""
import contextlib
import importlib.util
import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

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


class PrepareRendererMspackTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        # The checkout sits one level down, so a test can put a file just outside it.
        self.outside = Path(temp.name).resolve()
        self.dependency = self.outside / "dependency"
        self.dependency.mkdir()

    # --- Happy flow -------------------------------------------------------

    def test_replaces_a_symlink_text_file_with_its_target(self):
        commit = make_dependency(self.dependency, {"link.c": "source.c"})

        run_tool(self.dependency, pin=commit)

        self.assertEqual(read(self.dependency / "link.c"), SOURCE_TEXT)

    def test_reports_how_many_files_it_replaced(self):
        commit = make_dependency(self.dependency, {"one.c": "source.c", "two.c": "source.c"})

        output = run_tool(self.dependency, pin=commit)

        self.assertIn("Materialized 2 pinned libmspack symlink files", output)

    def test_leaves_ordinary_files_alone(self):
        # notes.txt holds the same text a symlink leftover would, so only git's
        # file mode tells the tool not to touch it.
        commit = make_dependency(self.dependency, {"link.c": "source.c"},
                                 files={"notes.txt": "source.c"})

        run_tool(self.dependency, pin=commit)

        self.assertEqual(read(self.dependency / "notes.txt"), "source.c")

    def test_resolves_a_target_from_the_symlinks_own_folder(self):
        commit = make_dependency(self.dependency, {"sub/link.c": "../source.c"})

        run_tool(self.dependency, pin=commit)

        self.assertEqual(read(self.dependency / "sub" / "link.c"), SOURCE_TEXT)

    # --- Bad flow ---------------------------------------------------------

    def test_stops_when_the_checkout_is_not_at_the_public_pin(self):
        make_dependency(self.dependency, {"link.c": "source.c"})

        with self.assertRaises(SystemExit) as caught:
            run_tool(self.dependency, pin=WRONG_PIN)

        self.assertIn(f"Check out public libmspack pin {WRONG_PIN}", str(caught.exception))
        self.assertEqual(read(self.dependency / "link.c"), "source.c")

    def test_refuses_a_target_outside_the_checkout(self):
        # The outside file exists, so the refusal can only come from where it is.
        (self.outside / "secret.c").write_text("not part of libmspack", encoding="utf-8")
        commit = make_dependency(self.dependency, {"link.c": "../secret.c"})

        with self.assertRaises(SystemExit) as caught:
            run_tool(self.dependency, pin=commit)

        self.assertIn("Invalid pinned symlink target: link.c", str(caught.exception))
        self.assertEqual(read(self.dependency / "link.c"), "../secret.c")

    def test_refuses_a_target_that_does_not_exist(self):
        commit = make_dependency(self.dependency, {"link.c": "missing.c"})

        with self.assertRaises(SystemExit) as caught:
            run_tool(self.dependency, pin=commit)

        self.assertIn("Invalid pinned symlink target: link.c", str(caught.exception))

    def test_refuses_to_overwrite_a_local_edit(self):
        commit = make_dependency(self.dependency, {"link.c": "source.c"})
        (self.dependency / "link.c").write_text("someone's local change", encoding="utf-8")

        with self.assertRaises(SystemExit) as caught:
            run_tool(self.dependency, pin=commit)

        self.assertIn("Unexpected local edit; refusing to overwrite", str(caught.exception))
        self.assertEqual(read(self.dependency / "link.c"), "someone's local change")

    # --- Edge cases -------------------------------------------------------

    def test_running_twice_keeps_the_replaced_file(self):
        commit = make_dependency(self.dependency, {"link.c": "source.c"})
        run_tool(self.dependency, pin=commit)

        run_tool(self.dependency, pin=commit)

        self.assertEqual(read(self.dependency / "link.c"), SOURCE_TEXT)

    def test_accepts_a_symlink_text_file_ending_in_a_newline(self):
        commit = make_dependency(self.dependency, {"link.c": "source.c"})
        # write_bytes, because write_text would turn the newline into \r\n on Windows.
        (self.dependency / "link.c").write_bytes(b"source.c\n")

        run_tool(self.dependency, pin=commit)

        self.assertEqual(read(self.dependency / "link.c"), SOURCE_TEXT)

    def test_recreates_a_deleted_symlink_file(self):
        commit = make_dependency(self.dependency, {"link.c": "source.c"})
        (self.dependency / "link.c").unlink()

        run_tool(self.dependency, pin=commit)

        self.assertEqual(read(self.dependency / "link.c"), SOURCE_TEXT)

    @unittest.skipIf(sys.platform == "win32", "Windows CI can't always create real symlinks")
    def test_skips_a_symlink_that_is_already_real(self):
        commit = make_dependency(self.dependency, {"link.c": "source.c"})
        (self.dependency / "link.c").unlink()
        os.symlink("source.c", self.dependency / "link.c")

        output = run_tool(self.dependency, pin=commit)

        self.assertTrue((self.dependency / "link.c").is_symlink())
        self.assertIn("Materialized 0 pinned libmspack symlink files", output)


if __name__ == "__main__":
    unittest.main()
