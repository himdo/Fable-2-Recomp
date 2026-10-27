"""Prepare the ReXGlue SDK source tree for the build.

The SDK is consumed as a git submodule (thirdparty/rexglue-sdk) pinned to a
fork commit (himdo/rexglue-sdk, branch CustomD3D12Renderer) that bakes in all
of this project's SDK fixes. Previously those fixes were applied from patch
files under thirdparty/ (rexglue-sdk-runtime-fixes.patch,
rexglue-sdk-debug-exports.patch, rexglue-sdk-local.patch); that flow is
retired and the patches have been removed - new SDK changes are committed to
the fork directly and the submodule pin is advanced.

No game content is read. Use --skip-dependencies for a quick work-tree
validation without network/dependency setup.
"""
import argparse
from pathlib import Path
import subprocess
import sys

# Public libmspack commit the SDK's broken pin is repointed at (only needed
# for older fork commits that still carry libmspack as a submodule gitlink).
MSPACK_PIN = "305907723a4e7ab2018e58040059ffb5e77db837"


def fail(message):
    print(f"prepare_runtime_sdk: error: {message}", file=sys.stderr)
    raise SystemExit(1)


def git(source, *args, check=True):
    result = subprocess.run(["git", "-C", str(source), *args],
                            capture_output=True, text=True)
    if check and result.returncode != 0:
        detail = (result.stderr or result.stdout).strip()
        fail(f"git {' '.join(args)} failed (exit {result.returncode}):\n{detail}")
    return result


def libmspack_mode(source):
    """Return the mode of thirdparty/libmspack at HEAD ('160000' submodule
    gitlink, '040000' vendored plain files), or None if absent."""
    result = git(source, "ls-tree", "HEAD", "--", "thirdparty/libmspack", check=False)
    if result.returncode != 0 or not result.stdout.strip():
        return None
    return result.stdout.split()[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--skip-dependencies", action="store_true")
    args = parser.parse_args()
    source = args.source.resolve()

    if not source.is_dir():
        fail(f"SDK source directory does not exist: {source}")
    if git(source, "rev-parse", "--is-inside-work-tree", check=False).returncode != 0:
        fail(f"{source} is not a git work tree; run "
             "'git submodule update --init thirdparty/rexglue-sdk' from the repo root.")

    head = git(source, "rev-parse", "HEAD", check=False).stdout.strip()
    if not head:
        fail(f"{source} has no commits (empty SDK checkout).")
    print(f"SDK revision {head} (fork commit with all project fixes baked in).")

    if not args.skip_dependencies:
        # libmspack is either a submodule gitlink (older fork commits) or
        # plain vendored files (current fork commits). Only the gitlink form
        # needs the index repin + submodule fetch + Windows symlink
        # materialization; the original SDK pin for it is unavailable on its
        # public remote.
        mode = libmspack_mode(source)
        if mode == "160000":
            # git submodule update reads the index, not the worktree gitlink.
            git(source, "update-index", "--cacheinfo",
                f"160000,{MSPACK_PIN},thirdparty/libmspack")
        elif mode != "040000":
            fail("thirdparty/libmspack is missing from the SDK tree at HEAD "
                 "(expected a submodule gitlink or vendored files).")
        result = subprocess.run(["git", "-C", str(source), "submodule", "update",
                                 "--init", "--recursive"])
        if result.returncode != 0:
            fail("SDK submodule update failed; a fresh clone needs network "
                 "access to fetch the SDK submodules.")
        if mode == "160000":
            result = subprocess.run([sys.executable,
                                     str(Path(__file__).with_name("prepare_renderer_mspack.py")),
                                     str(source / "thirdparty" / "libmspack")])
            if result.returncode != 0:
                fail("prepare_renderer_mspack.py failed.")
        else:
            # The SDK's thirdparty/CMakeLists.txt treats a dependency as
            # initialized when <dep>/.git exists. Vendored libmspack has no
            # .git, so drop an empty marker directory (invisible to git
            # status) to satisfy that check.
            marker = source / "thirdparty" / "libmspack" / ".git"
            if not marker.exists():
                marker.mkdir(parents=True, exist_ok=True)


if __name__ == "__main__":
    main()
