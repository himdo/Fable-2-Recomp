#!/usr/bin/env python3
"""Play a scenario file through the f2 console and report whether it passed.

A scenario is a plain list of f2 console commands, the same as a file for
`f2 exec`: one command per line, `#` comments, `;` chains. For example:

    boot slot 1
    wait 2s
    expect 82F01234 100 32
    shot

The runner hands it to the running game through the console's file transport
(src/core/fable2_console.h, on by default in Release and Debug builds):

  1. waits until <root>/console/in.txt is free (the game deletes the file
     once it has read it, about every 10 frames);
  2. notes where console/out.txt ends, so only new replies count;
  3. writes the scenario plus a last `echo __scenario_done <id>` line to
     in.txt.tmp and renames it to in.txt, so the game never reads half a file;
  4. waits for the game to take in.txt, then reads out.txt until the echo of
     that marker arrives.

It then prints every reply the scenario produced. A reply is a failure when
the console marks it as an error ("[tag] ERR ...", for example a failed
`expect`) or Lua reports one ("[lua] error: ..."). Screenshots ("[shot] saved
<path> <w>x<h>") are listed at the end, so they can be opened afterwards.

usage: run_scenario.py --root <game root> [--timeout SECONDS] <scenario file>

  --root      the folder with the game data, which holds console/
  --timeout   how long the whole scenario may take (default 300 s; a `boot`
              alone can take a minute)

Exit codes: 0 passed, 1 a reply was a failure, 2 the scenario couldn't be run
or didn't finish.
"""
import argparse
import os
from pathlib import Path
import re
import sys
import time

POLL_SECONDS = 0.2    # how often out.txt and in.txt are checked
TAKE_SECONDS = 10.0   # how long the game may take to pick up in.txt
DEFAULT_TIMEOUT = 300.0
MARKER = "__scenario_done"
FAILURE = re.compile(r"^\[[^\]]+\] ERR ")
LUA_ERROR = "[lua] error:"
SCREENSHOT = re.compile(r"^\[shot\] saved (.+) \d+x\d+$")


def build_input(text: str, marker: str) -> str:
    """The text for in.txt: the scenario, then the echo of the end marker."""
    if text and not text.endswith("\n"):
        text += "\n"
    return f"{text}echo {MARKER} {marker}\n"


def scenario_lines(out_lines: list[str], marker: str) -> list[str] | None:
    """The replies before this run's end marker, or None while it hasn't arrived."""
    end = f"[echo] {MARKER} {marker}"
    lines = [line.rstrip("\r") for line in out_lines]
    if end not in lines:
        return None
    return lines[:lines.index(end)]


def failures(lines: list[str]) -> list[str]:
    """The replies the console or Lua marked as errors."""
    return [line for line in lines if FAILURE.match(line) or line.startswith(LUA_ERROR)]


def screenshots(lines: list[str]) -> list[str]:
    """The paths of the screenshots the scenario saved, in order."""
    return [match.group(1) for match in map(SCREENSHOT.match, lines) if match]


def read_from(path: Path, offset: int) -> list[str]:
    """The lines of a file after `offset` bytes. A file that got shorter is read from the start."""
    if not path.is_file():
        return []
    data = path.read_bytes()
    if len(data) < offset:
        offset = 0
    return data[offset:].decode("utf-8", errors="replace").splitlines()


def wait_until_gone(path: Path, seconds: float) -> bool:
    """Wait for a file to disappear; False if it is still there after `seconds`."""
    deadline = time.monotonic() + seconds
    while path.exists():
        if time.monotonic() >= deadline:
            return False
        time.sleep(POLL_SECONDS)
    return True


def new_marker() -> str:
    return f"{time.time_ns()}-{os.getpid()}"


def report(lines: list[str]) -> int:
    for line in lines:
        print(line)
    shots = screenshots(lines)
    if shots:
        print("Screenshots:")
        for shot in shots:
            print(f"  {shot}")
    failed = failures(lines)
    if failed:
        print(f"FAIL: {len(failed)} failing line{'' if len(failed) == 1 else 's'}")
        for line in failed:
            print(f"  {line}")
        return 1
    print(f"PASS ({len(lines)} line{'' if len(lines) == 1 else 's'})")
    return 0


def stop(message: str) -> int:
    print(f"run_scenario: {message}", file=sys.stderr)
    return 2


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("scenario", type=Path)
    parser.add_argument("--root", type=Path, required=True,
                        help="the folder with the game data, which holds console/")
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT,
                        help="how long the whole scenario may take, in seconds (default 300)")
    args = parser.parse_args(argv)
    if args.timeout <= 0:
        parser.error("--timeout must be more than 0")

    console = args.root / "console"
    if not console.is_dir():
        return stop(f"no console folder in {args.root} (is this the game root?)")
    try:
        text = args.scenario.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as error:
        return stop(str(error))

    in_file, out_file = console / "in.txt", console / "out.txt"
    not_taken = "the game didn't read console/in.txt (is it running, with console_enabled on?)"
    if not wait_until_gone(in_file, TAKE_SECONDS):
        return stop(not_taken)
    offset = out_file.stat().st_size if out_file.is_file() else 0
    marker = new_marker()
    temporary = console / "in.txt.tmp"
    temporary.write_bytes(build_input(text, marker).encode("utf-8"))
    os.replace(temporary, in_file)
    if not wait_until_gone(in_file, TAKE_SECONDS):
        in_file.unlink(missing_ok=True)  # don't let the scenario start later, unwatched
        return stop(not_taken)

    deadline = time.monotonic() + args.timeout
    while True:
        new_lines = read_from(out_file, offset)
        lines = scenario_lines(new_lines, marker)
        if lines is not None:
            return report(lines)
        if time.monotonic() >= deadline:
            for line in new_lines:
                print(line)
            return stop(f"the scenario didn't finish within {args.timeout:g} s")
        time.sleep(POLL_SECONDS)


if __name__ == "__main__":
    sys.exit(main())
