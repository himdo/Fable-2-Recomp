#!/usr/bin/env python3
"""Line up a playthrough's console notes with the functions the game ran.

For broad probing: play normally, type a note in the console at each moment
worth marking (`f2 echo entered the Guild`, `f2 echo first fight`), and let
the function tracer record what runs. This tool splits the session at the
notes and lists, for each note:

  first seen   the functions that ran for the first time between this note
               and the next one
  busier       (with --minutes) functions that already ran before this note
               but are called much more often until the next one, such as
               combat code during the first fight

Those are the candidates to name for that moment of play. Read each one in the
generated code first: the timing shows where a function is used, not what it
does.

Inputs, all stamped with the same wall clock:

  game log        the game's log from logs/. Console replies are logged as
                  "[2026-11-26 14:03:12.345] [info] [...] [t1234] [echo] <note>"
                  (the SDK's file pattern, rex/logging/types.h, around
                  src/core/fable2_console.h's out()). Only [echo] lines count
                  as notes; empty notes are skipped.
  first-seen log  one line per guest function, the first time it ran:
                  "[2026-11-26 14:03:12.345] <function name>". If a name
                  appears more than once, the earliest time counts.
  minutes log     (optional) every minute, one line per function called in
                  that minute: "[2026-11-26 14:04:00.000] <calls> <name>",
                  every line of one minute with the same time (when it was
                  written). A name repeated within one minute has its calls
                  added up.

The tracer doesn't write the first-seen or minutes logs yet; these are the
formats it will have to use.

Functions first seen before the first note go in a segment of their own. A
function first seen at exactly a note's time belongs to that note. A minute
belongs to the segment its time falls in, so a minute that a note splits
counts for the later note.

A function is busier in a segment when it wasn't first seen there, it ran at
least --min-rate times a minute there, and that rate is at least --factor
times its rate in all the other minutes of the session (or it didn't run in
them at all). The busier list is sorted by that ratio, highest first.

usage: probe_timeline.py [--unnamed] [--minutes LOG] [--factor N] [--min-rate N]
                         <game log> <first-seen log>

  --unnamed      list only placeholder names (sub_..., ProcessAndProcessAndProcessN_...)
  --minutes LOG  the minutes log, for the busier lists
  --factor N     how many times busier, more than 1 (default 5)
  --min-rate N   the fewest calls a minute that count as busy (default 60)

Exit codes: 0 done, 2 a file could not be read or an option is out of range.
"""
import argparse
from collections import Counter
from dataclasses import dataclass, field
from datetime import datetime
import math
from pathlib import Path
import re
import sys

# The timestamp at the start of every line of all three logs.
TIMESTAMP = r"\[(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3})\]"
TIME_FORMAT = "%Y-%m-%d %H:%M:%S.%f"
NOTE_LINE = re.compile(TIMESTAMP + r" .*?\[echo\] (.*)$")
FIRST_SEEN_LINE = re.compile(TIMESTAMP + r" (\S+)$")
MINUTE_LINE = re.compile(TIMESTAMP + r" (\d+) (\S+)$")
# The same rule as GENERIC in tools/check_manifest_names.py (a test checks they match).
PLACEHOLDER = re.compile(r"^(sub_|ProcessAndProcessAndProcess[0-9]*_)[0-9A-Fa-f]{8}$")
BEFORE_FIRST_NOTE = "(before the first note)"
DEFAULT_FACTOR = 5.0
DEFAULT_MIN_RATE = 60.0


@dataclass
class Busier:
    name: str
    rate: float        # calls a minute in this segment
    other_rate: float  # calls a minute in every other minute of the session


@dataclass
class Segment:
    start: datetime | None  # None for the part before the first note
    note: str
    functions: list[tuple[datetime, str]] = field(default_factory=list)
    busier: list[Busier] = field(default_factory=list)


def parse_time(text: str) -> datetime:
    return datetime.strptime(text, TIME_FORMAT)


def read_notes(lines) -> list[tuple[datetime, str]]:
    """The non-empty [echo] notes in a game log, in time order."""
    notes = []
    for line in lines:
        # Stripping also drops the space after "[echo]" in an empty note, so it doesn't match.
        match = NOTE_LINE.match(line.strip())
        if match:
            notes.append((parse_time(match.group(1)), match.group(2).strip()))
    return sorted(notes, key=lambda note: note[0])


def read_first_seen(lines) -> dict[str, datetime]:
    """Each function's earliest time in a first-seen log."""
    first_seen = {}
    for line in lines:
        match = FIRST_SEEN_LINE.match(line.strip())
        if not match:
            continue
        time, name = parse_time(match.group(1)), match.group(2)
        if name not in first_seen or time < first_seen[name]:
            first_seen[name] = time
    return first_seen


def read_minutes(lines) -> dict[datetime, Counter]:
    """The calls per function in each minute of a minutes log, keyed by the minute's time."""
    minutes = {}
    for line in lines:
        match = MINUTE_LINE.match(line.strip())
        if match:
            calls = minutes.setdefault(parse_time(match.group(1)), Counter())
            calls[match.group(3)] += int(match.group(2))
    return minutes


def owner(segments: list[Segment], time: datetime) -> int:
    """The index of the segment a time falls in. segments[0] is the part before the first note."""
    found = 0
    for index, segment in enumerate(segments[1:], start=1):
        if segment.start <= time:
            found = index
    return found


def count_by_segment(segments, minutes):
    """How many minutes fall in each segment, and the calls per function in them."""
    minute_count = [0] * len(segments)
    segment_calls = [Counter() for _ in segments]
    for time, calls in minutes.items():
        index = owner(segments, time)
        minute_count[index] += 1
        segment_calls[index].update(calls)
    return minute_count, segment_calls


def find_busier(segments, minutes, first_seen, factor, min_rate, unnamed_only):
    """Fill in each segment's busier list from the minutes log."""
    total = sum(minutes.values(), Counter())
    minute_count, segment_calls = count_by_segment(segments, minutes)
    for index, segment in enumerate(segments):
        count, other_count = minute_count[index], len(minutes) - minute_count[index]
        if count == 0 or other_count == 0:
            continue  # no minutes here, or none elsewhere to compare with
        for name, calls in segment_calls[index].items():
            if unnamed_only and not PLACEHOLDER.match(name):
                continue
            if name in first_seen and owner(segments, first_seen[name]) == index:
                continue  # already in this segment's first-seen list
            rate = calls / count
            other_rate = (total[name] - calls) / other_count
            if rate >= min_rate and rate >= factor * other_rate:
                segment.busier.append(Busier(name, rate, other_rate))
        segment.busier.sort(key=lambda b: (-(b.rate / b.other_rate if b.other_rate else math.inf), b.name))


def build_segments(notes, first_seen, unnamed_only=False, minutes=None,
                   factor=DEFAULT_FACTOR, min_rate=DEFAULT_MIN_RATE) -> list[Segment]:
    """One segment per note, plus one before the first note when it has anything to list."""
    segments = [Segment(None, BEFORE_FIRST_NOTE)] + [Segment(time, note) for time, note in notes]
    for name, time in sorted(first_seen.items(), key=lambda item: (item[1], item[0])):
        if unnamed_only and not PLACEHOLDER.match(name):
            continue
        segments[owner(segments, time)].functions.append((time, name))
    if minutes:
        find_busier(segments, minutes, first_seen, factor, min_rate, unnamed_only)
    before = segments[0]
    return segments if before.functions or before.busier else segments[1:]


def per_minute(rate: float) -> str:
    return f"{round(rate, 1):g}/min"


def format_segments(segments: list[Segment]) -> str:
    lines = []
    for segment in segments:
        start = segment.start.strftime("%H:%M:%S") if segment.start else "--:--:--"
        count = len(segment.functions)
        lines.append(f"{start}  {segment.note}  ({count} function{'' if count == 1 else 's'})")
        for time, name in segment.functions:
            lines.append(f"  {time.strftime('%H:%M:%S.%f')[:-3]}  {name}")
        if segment.busier:
            lines.append(f"  busier than elsewhere ({len(segment.busier)}):")
            for busier in segment.busier:
                lines.append(f"    {busier.name}  {per_minute(busier.rate)} here, "
                             f"{per_minute(busier.other_rate)} elsewhere")
    return "\n".join(lines) + "\n"


def read_lines(path: Path) -> list[str]:
    return path.read_text(encoding="utf-8", errors="replace").splitlines()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("game_log", type=Path)
    parser.add_argument("first_seen_log", type=Path)
    parser.add_argument("--unnamed", action="store_true",
                        help="list only placeholder names (sub_...)")
    parser.add_argument("--minutes", type=Path, help="the minutes log, for the busier lists")
    parser.add_argument("--factor", type=float, default=DEFAULT_FACTOR,
                        help="how many times busier, more than 1 (default 5)")
    parser.add_argument("--min-rate", type=float, default=DEFAULT_MIN_RATE,
                        help="the fewest calls a minute that count as busy (default 60)")
    args = parser.parse_args(argv)
    if args.factor <= 1:
        parser.error("--factor must be more than 1")
    if args.min_rate < 0:
        parser.error("--min-rate can't be negative")
    try:
        notes = read_notes(read_lines(args.game_log))
        first_seen = read_first_seen(read_lines(args.first_seen_log))
        minutes = read_minutes(read_lines(args.minutes)) if args.minutes else None
    except OSError as error:
        print(f"probe_timeline: {error}", file=sys.stderr)
        return 2
    segments = build_segments(notes, first_seen, args.unnamed, minutes, args.factor, args.min_rate)
    sys.stdout.write(format_segments(segments))
    return 0


if __name__ == "__main__":
    sys.exit(main())
