"""Apply the source-only runtime fixes to a known-good SDK revision.

No game content is read. Conflicting SDK edits are rejected, not overwritten.
Use --skip-dependencies for patch validation without network/dependency setup.

Accepted SDK states:
  * HEAD at the base pin - a clean SDK checkout; the patches under
    thirdparty/ are applied on top of it, and
  * any other revision where every patch is either already applied - i.e. a
    fork commit with the fixes baked into the tree (no pin list to maintain) -
    or applies cleanly on top of it (a follow-up fix the fork doesn't carry).
"""
import argparse
from pathlib import Path
import subprocess
import sys

# Clean base revision the patches were generated against.
SDK_PIN = "babc769a94be5618010abfd075ed84f3c2bc09f5"
# Public libmspack commit the SDK's broken pin is repointed at.
MSPACK_PIN = "305907723a4e7ab2018e58040059ffb5e77db837"
PATCHES = (
    "rexglue-sdk-runtime-fixes.patch",
    "rexglue-sdk-debug-exports.patch",
    "rexglue-sdk-vulkan-present-gate.patch",
    "rexglue-sdk-graphics-fx.patch",
)


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


def patch_state(source, patch):
    """Classify the worktree as 'applied', 'clean', or 'conflict' for a patch."""
    if git(source, "apply", "--reverse", "--check", str(patch), check=False).returncode == 0:
        return "applied"
    if git(source, "apply", "--check", str(patch), check=False).returncode == 0:
        return "clean"
    if patch_content_present(source, patch):
        return "applied"  # baked in by a fork at different offsets
    return "conflict"


def patch_content_present(source, patch):
    """True when every line the patch adds exists in its target file and
    every line it removes (and does not re-add) is gone."""
    added, removed, target = {}, {}, None
    for line in patch.read_text(encoding="utf-8").splitlines():
        if line.startswith("+++ "):
            target = line[4:].removeprefix("b/") if line[4:] != "/dev/null" else None
        elif line.startswith("--- "):
            continue
        elif target and line.startswith("+"):
            added.setdefault(target, set()).add(line[1:])
        elif target and line.startswith("-"):
            removed.setdefault(target, set()).add(line[1:])
    for target in set(added) | set(removed):
        path = source / target
        if not path.is_file():
            return False
        lines = set(path.read_text(encoding="utf-8", errors="replace").splitlines())
        if not added.get(target, set()) <= lines:
            return False
        if (removed.get(target, set()) - added.get(target, set())) & lines:
            return False
    return True


def libmspack_mode(source):
    """Return the mode of thirdparty/libmspack at HEAD ('160000' submodule
    gitlink, '040000' vendored plain files), or None if absent."""
    result = git(source, "ls-tree", "HEAD", "--", "thirdparty/libmspack", check=False)
    if result.returncode != 0 or not result.stdout.strip():
        return None
    return result.stdout.split()[0]


def submodules(source):
    """Yield (path, gitlink commit, url) for each submodule of `source`."""
    listing = git(source, "config", "-f", ".gitmodules", "--get-regexp",
                  r"^submodule\..*\.path$", check=False).stdout.split("\n")
    for line in filter(None, listing):
        key, path = line.split(" ", 1)
        name = key[len("submodule."):-len(".path")]
        # The index, like git submodule: main() may have repinned a gitlink.
        entry = git(source, "ls-files", "--stage", "--", path, check=False).stdout.split()
        if len(entry) < 2 or entry[0] != "160000":
            continue  # vendored as plain files at this revision
        url = git(source, "config", "-f", ".gitmodules",
                  f"submodule.{name}.url").stdout.strip()
        yield source / path, entry[1], url


def checked_out_at(target, sha):
    return ((target / ".git").exists() and
            git(target, "rev-parse", "HEAD", check=False).stdout.strip() == sha)


def submodules_current(source):
    """True when every submodule, recursively, is at its gitlink commit."""
    return all(checked_out_at(target, sha) and submodules_current(target)
               for target, sha, _ in submodules(source))


def init_submodules_native(source):
    """Recursively check out every submodule at its recorded gitlink commit
    using only git builtins (init/fetch/checkout), never git-submodule.sh."""
    for target, sha, url in submodules(source):
        if checked_out_at(target, sha):
            init_submodules_native(target)
            continue
        if not (target / ".git").exists():
            target.mkdir(parents=True, exist_ok=True)
            git(target, "init", "-q")
            git(target, "remote", "add", "origin", url)
        print(f"Fetching {target} @ {sha[:12]}")
        if git(target, "fetch", "-q", "--depth", "1", "origin", sha,
               check=False).returncode != 0:
            git(target, "fetch", "-q", "origin")  # server refuses SHA fetches
        git(target, "checkout", "-q", "--force", sha)
        init_submodules_native(target)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--skip-dependencies", action="store_true")
    args = parser.parse_args()
    source = args.source.resolve()
    patch_directory = Path(__file__).resolve().parents[1] / "thirdparty"

    if not source.is_dir():
        fail(f"SDK source directory does not exist: {source}")
    if git(source, "rev-parse", "--is-inside-work-tree", check=False).returncode != 0:
        fail(f"{source} is not a git work tree; run "
             "'git submodule update --init thirdparty/rexglue-sdk' from the repo root.")

    head = git(source, "rev-parse", "HEAD", check=False).stdout.strip()
    if not head:
        fail(f"{source} has no commits (empty SDK checkout).")

    # Keep follow-up fixes separate so existing patched SDK checkouts can upgrade.
    states = {}
    for name in PATCHES:
        patch = patch_directory / name
        if not patch.is_file():
            fail(f"patch file is missing: {patch}")
        states[name] = patch_state(source, patch)

    # Apply whatever is missing, refuse conflicting edits. A fork revision may
    # already bake in some or all of the patches.
    for name, state in states.items():
        if state == "applied":
            print(f"Already applied: {name}")
        elif state == "clean":
            git(source, "apply", str(patch_directory / name))
            print(f"Applied: {name}")
        elif head == SDK_PIN:
            detail = git(source, "apply", "--check",
                         str(patch_directory / name), check=False).stderr.strip()
            fail(f"SDK patch {name} conflicts with local edits:\n{detail}\n"
                 "The SDK work tree is not at a clean accepted revision. Restore it with:\n"
                 f"  git -C {source} checkout -- . && git -C {source} clean -fd")
        else:
            fail(f"unexpected SDK revision {head}: patch {name} is neither baked "
                 f"in nor applicable.\n"
                 f"Either check out the base revision ({SDK_PIN}) so the patches "
                 f"can be applied, or use a fork revision that bakes them in.\n"
                 f"Restore the base with: git -C {source} checkout {SDK_PIN}")

    if not args.skip_dependencies:
        # libmspack is either a submodule gitlink (base pin and older fork
        # commits) or plain vendored files (newer fork commits). Only the
        # gitlink form needs the index repin + submodule fetch + Windows
        # symlink materialization; the original SDK pin for it is unavailable
        # on its public remote.
        mode = libmspack_mode(source)
        if mode == "160000":
            # git submodule update reads the index, not the patched worktree gitlink.
            git(source, "update-index", "--cacheinfo",
                f"160000,{MSPACK_PIN},thirdparty/libmspack")
        elif mode != "040000":
            fail("thirdparty/libmspack is missing from the SDK tree at HEAD "
                 "(expected a submodule gitlink or vendored files).")
        # Skip git submodule when nothing needs fetching: on hosts where its
        # sh forks fail it spends about a minute retrying before giving up.
        if submodules_current(source):
            print("SDK submodules already at their recorded commits.")
        elif subprocess.run(["git", "-C", str(source), "submodule", "update",
                             "--init", "--recursive"]).returncode != 0:
            # `git submodule` is an MSYS shell script; on some Windows hosts
            # every sh fork dies (0xC0000142). Retry with plain git commands.
            print("prepare_runtime_sdk: git submodule failed; retrying with "
                  "native git fetches", file=sys.stderr)
            init_submodules_native(source)
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
