#!/usr/bin/env python3
"""check_manifest_names.py - check the naming rules for fable_2_manifest.toml.

CLAUDE.md asks for function names shaped like Subsystem_WhatItDoes_ADDRESS,
with a comment above each entry saying what the function does and how that
is known. tools/check_manifest_collisions.py checks names against the runtime
DLL; this tool checks the rules that need nothing but the manifest, so it can
run anywhere, CI included.

Rules for every named entry (master passes these):
  suffix   a name that ends in 8 hex digits ends in the entry's own address
  unique   no two entries share a name

Rules for new and renamed entries only (thousands of older entries don't pass
these yet):
  generic  the name isn't a placeholder (sub_..., ProcessAndProcessAndProcessN_...)
  format   the name looks like Subsystem_WhatItDoes_ADDRESS
  comment  a comment sits directly above the entry

Usage: python tools/check_manifest_names.py [--manifest PATH] [--base REF]

With --base, the new-entry rules check the entries that are new or renamed
since that git ref (for a pull request: the branch it merges into). Without
it, the tool checks the every-entry rules and prints how many older entries
break each of the others.

Exit code 0 = clean, 1 = a rule is broken, 2 = usage/IO error.
"""

import argparse
from dataclasses import dataclass
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent

HEADER = re.compile(r'^\[entrypoint\.functions\."0x([0-9A-Fa-f]+)"\]\s*$')
NAME = re.compile(r'^\s*name\s*=\s*"([^"]*)"')
FORMAT = re.compile(r"^[A-Za-z][A-Za-z0-9]*(_[A-Za-z0-9]+)*_[0-9A-F]{8}$")
GENERIC = re.compile(r"^(sub_|ProcessAndProcessAndProcess[0-9]*_)[0-9A-Fa-f]{8}$")
HEX_SUFFIX = re.compile(r"_([0-9A-Fa-f]{8})$")


@dataclass
class Entry:
    """One [entrypoint.functions."0x..."] table of the manifest."""
    address: int
    name: str | None  # None for an entry that only marks where a function starts
    line: int  # 1-based line of the table header
    has_comment: bool  # the line directly above the header is a comment


def parse_entries(text: str) -> list[Entry]:
    """Read every function entry from the manifest text, in file order."""
    lines = text.splitlines()
    entries = []
    for index, line in enumerate(lines):
        header = HEADER.match(line)
        if not header:
            continue
        name = None
        for following in lines[index + 1:]:
            if following.startswith("["):  # the next table: this entry has no name
                break
            found = NAME.match(following)
            if found:
                name = found.group(1)
                break
        has_comment = index > 0 and lines[index - 1].lstrip().startswith("#")
        entries.append(Entry(int(header.group(1), 16), name, index + 1, has_comment))
    return entries


def every_entry_problems(entries: list[Entry]) -> list[tuple[Entry, str]]:
    """The suffix and unique rules, for every named entry."""
    problems = []
    first_entry_with_name = {}
    for entry in entries:
        if entry.name is None:
            continue
        suffix = HEX_SUFFIX.search(entry.name)
        if suffix and int(suffix.group(1), 16) != entry.address:
            problems.append((entry, f"ends in {suffix.group(1)}, but the entry's address is "
                                    f"0x{entry.address:08X}"))
        if entry.name in first_entry_with_name:
            first = first_entry_with_name[entry.name]
            problems.append((entry, f"has the same name as the entry at 0x{first.address:08X}"))
        else:
            first_entry_with_name[entry.name] = entry
    return problems


def new_entry_problems(entry: Entry) -> list[str]:
    """The generic, format and comment rules, for one new or renamed entry."""
    problems = []
    if GENERIC.match(entry.name):
        problems.append("is a placeholder name; give it a real one")
    elif not FORMAT.match(entry.name):
        problems.append("doesn't follow Subsystem_WhatItDoes_ADDRESS")
    if not entry.has_comment:
        problems.append("has no comment above it saying what it does and how that is known")
    return problems


def changed_entries(entries: list[Entry], base_entries: list[Entry]) -> list[Entry]:
    """The named entries that are new since the base, or have a different name there."""
    base_names = {entry.address: entry.name for entry in base_entries}
    return [entry for entry in entries
            if entry.name is not None and base_names.get(entry.address) != entry.name]


def read_from_git(path: Path, ref: str) -> str:
    """Return the file at `path` as it is at the git ref `ref`."""
    path = path.resolve()
    top = subprocess.run(["git", "-C", str(path.parent), "rev-parse", "--show-toplevel"],
                         capture_output=True, text=True, check=True).stdout.strip()
    relative = path.relative_to(Path(top).resolve()).as_posix()
    return subprocess.run(["git", "-C", top, "show", f"{ref}:{relative}"],
                          capture_output=True, text=True, encoding="utf-8", check=True).stdout


def print_older_entry_counts(entries: list[Entry]) -> None:
    """How many entries break each new-entry rule: the naming debt still to pay off."""
    named = [entry for entry in entries if entry.name is not None]
    generic = [entry for entry in named if GENERIC.match(entry.name)]
    badly_shaped = [entry for entry in named
                    if not GENERIC.match(entry.name) and not FORMAT.match(entry.name)]
    uncommented = [entry for entry in named if not entry.has_comment]
    print("Not yet required of older entries:")
    print(f"  {len(generic)} placeholder names")
    print(f"  {len(badly_shaped)} names not shaped like Subsystem_WhatItDoes_ADDRESS")
    print(f"  {len(uncommented)} entries without a comment above them")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Check the naming rules for the manifest.")
    parser.add_argument("--manifest", type=Path, default=ROOT / "fable_2_manifest.toml")
    parser.add_argument("--base", help="git ref to compare with; the new-entry rules check "
                                       "only entries that are new or renamed since then")
    args = parser.parse_args(argv)

    if not args.manifest.exists():
        print(f"error: manifest not found: {args.manifest}")
        return 2
    entries = parse_entries(args.manifest.read_text(encoding="utf-8"))
    print(f"{args.manifest.name}: {sum(entry.name is not None for entry in entries)} named functions")

    problems = every_entry_problems(entries)
    if args.base:
        try:
            base_text = read_from_git(args.manifest, args.base)
        except (subprocess.CalledProcessError, ValueError, OSError) as error:
            print(f"error: can't read {args.manifest.name} at {args.base}: {error}")
            return 2
        checked = changed_entries(entries, parse_entries(base_text))
        print(f"{len(checked)} new or renamed since {args.base}")
        for entry in checked:
            problems += [(entry, problem) for problem in new_entry_problems(entry)]
    else:
        print_older_entry_counts(entries)

    if problems:
        print("\nPROBLEMS - fix these manifest entries:")
        for entry, problem in problems:
            print(f"  0x{entry.address:08X} {entry.name} (line {entry.line}): {problem}")
        return 1
    print("OK - the manifest follows the naming rules")
    return 0


if __name__ == "__main__":
    sys.exit(main())
