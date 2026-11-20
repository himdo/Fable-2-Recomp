"""Merge the disc's dir.manifest into a staged one (called by tools/stage_content.cmd).

The game indexes data/ from dir.manifest at startup, so a file that is not a
manifest line is invisible to it. stage_content.cmd can't simply copy the
manifest: the build may already have created or extended the destination with
the staged external-Lua entries (tools/ensure_recomp_manifest.cmake), and
`xcopy /D` skips a destination that is newer than the source, which would
leave an incomplete index.

Usage: merge_dir_manifest.py <source dir.manifest> <destination dir.manifest>

The result is every source line, in source order, followed by the lines that
only the destination has (in their order). Lines are compared without their
line ending and blank lines are dropped, as ensure_recomp_manifest.cmake does;
every line is written with CRLF. Running it again changes nothing. Lines are
handled as bytes, so names in any encoding pass through unchanged. The
destination is replaced in one step (written to a temporary file, then
renamed), so a failed write never leaves half a manifest behind.
"""
import os
import sys
from pathlib import Path


def read_lines(path):
    """Return the non-blank lines of a manifest, without line endings."""
    lines = []
    for line in Path(path).read_bytes().splitlines():
        line = line.rstrip(b"\r")
        if line.strip():
            lines.append(line)
    return lines


def merge(source_lines, destination_lines):
    """Return the source lines followed by the lines only the destination has."""
    seen = set(source_lines)
    merged = list(source_lines)
    for line in destination_lines:
        if line not in seen:
            seen.add(line)
            merged.append(line)
    return merged


def write_lines(path, lines):
    """Replace the file at `path` with `lines`, CRLF-terminated, in one step."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_bytes(b"".join(line + b"\r\n" for line in lines))
    os.replace(temporary, path)


def main(argv):
    if len(argv) != 3:
        print("usage: merge_dir_manifest.py <source dir.manifest> <destination dir.manifest>",
              file=sys.stderr)
        return 2
    source, destination = argv[1], argv[2]
    if not Path(source).is_file():
        print(f"merge_dir_manifest: no source manifest at {source}", file=sys.stderr)
        return 1
    destination_lines = read_lines(destination) if Path(destination).is_file() else []
    write_lines(destination, merge(read_lines(source), destination_lines))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
