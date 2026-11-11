#!/usr/bin/env python3
"""Summarise fable2_mouse.log into numbers for mouse-look stutter.

The log comes from src/input/mouse_diag.h (turn it on with `f2 mouse diag on`,
and the camera trace with `f2 mouse trace on`). Each `f2 mouse inject` run is
reported as its own segment, so a before/after comparison is two inject runs
with the setting changed in between. Without inject runs the whole log is one
segment.

For each segment:
  polls, updates      input polls and camera updates, with rates
  polls/update        how many polls the game made per camera update
  visible             share of the mouse movement the game could have seen,
                      assuming each camera update uses the last poll before it
                      (stick values are per poll, so the rest is lost)
  clamped             polls where the stick hit full deflection (speed cap)
  yaw per update      RotateRight going into each update, over the steady
                      middle of the run (the first and last 15% are skipped):
                      mean, standard deviation, and cv = sd / |mean|, the
                      "stutter score" (lower is smoother)
  zero                share of steady updates that got no yaw at all
  yaw total           sum of SmoothRotateRight (the yaw actually applied)

usage: mouse_stats.py [--json] [--whole] <fable2_mouse.log>
"""
import argparse
import json
import math
import sys
from dataclasses import dataclass, field

STEADY_SKIP = 0.15  # fraction of a run skipped at each end for the steady stats
TAIL_US = 500_000   # smoothing keeps turning the camera after the input stops


@dataclass
class Record:
    kind: str
    t: int
    fields: dict
    text: str = ""


def parse_line(line):
    """Return a Record for one log line, or None for comments and junk."""
    line = line.strip()
    if not line or line.startswith("#"):
        return None
    kind, _, rest = line.partition(" ")
    fields, words = {}, []
    for word in rest.split():
        key, eq, value = word.partition("=")
        if eq and key and key not in fields:
            fields[key] = value
        else:
            words.append(word)
    try:
        t = int(fields.get("t", ""))
    except ValueError:
        return None
    return Record(kind, t, fields, " ".join(words))


def number(value):
    """Lua and C numbers as float; None for nil, '?' and other non-numbers."""
    try:
        result = float(value)
    except (TypeError, ValueError):
        return None
    return result if math.isfinite(result) else None


def pair(value):
    """'a,b' -> (int a, int b); (0, 0) when missing or malformed."""
    try:
        a, b = value.split(",")
        return int(a), int(b)
    except (AttributeError, ValueError):
        return 0, 0


def poll_px(record):
    """Absolute movement (x, y) one poll carried: cursor plus injected."""
    ix, iy = pair(record.fields.get("inj"))
    dx = int(record.fields.get("dx", 0) or 0)
    dy = int(record.fields.get("dy", 0) or 0)
    return abs(dx) + abs(ix), abs(dy) + abs(iy)


def read_log(lines):
    return [r for r in (parse_line(line) for line in lines) if r is not None]


@dataclass
class Segment:
    label: str
    start: int
    end: int  # input end; camera stats also take TAIL_US after it


def find_segments(records, whole=False):
    """One segment per inject note, or the whole log when there are none."""
    injects = [r for r in records if r.kind == "N" and r.text == "inject"]
    if whole or not injects:
        if not records:
            return []
        return [Segment("whole log", records[0].t, records[-1].t)]
    segments = []
    for note in injects:
        ms = int(note.fields.get("ms", 0) or 0)
        label = f"inject {note.fields.get('dx', '?')},{note.fields.get('dy', '?')} px over {ms} ms"
        segments.append(Segment(label, note.t, note.t + ms * 1000))
    return segments


def mean_sd(values):
    if not values:
        return None, None
    mean = sum(values) / len(values)
    var = sum((v - mean) ** 2 for v in values) / len(values)
    return mean, math.sqrt(var)


@dataclass
class Stats:
    label: str
    seconds: float
    polls: int = 0
    updates: int = 0
    px_x: int = 0
    px_y: int = 0
    clamped: int = 0
    moving_polls: int = 0
    visible_x: int = 0
    ticks: list = field(default_factory=list)
    steady_rr: list = field(default_factory=list)
    yaw_total: float = 0.0

    def as_dict(self):
        mean, sd = mean_sd(self.steady_rr)
        zero = sum(1 for v in self.steady_rr if v == 0) / len(self.steady_rr) if self.steady_rr else None
        cv = sd / abs(mean) if mean else None
        return {
            "label": self.label,
            "seconds": round(self.seconds, 3),
            "polls": self.polls,
            "polls_per_s": round(self.polls / self.seconds, 1) if self.seconds else None,
            "updates": self.updates,
            "updates_per_s": round(self.updates / self.seconds, 1) if self.seconds else None,
            "polls_per_update": round(self.polls / self.updates, 2) if self.updates else None,
            "px": [self.px_x, self.px_y],
            "visible_share": round(self.visible_x / self.px_x, 3) if self.px_x else None,
            "clamped_share": round(self.clamped / self.moving_polls, 3) if self.moving_polls else None,
            "tick_rates": sorted(set(self.ticks)),
            "yaw_per_update": {
                "n": len(self.steady_rr),
                "mean": None if mean is None else round(mean, 4),
                "sd": None if sd is None else round(sd, 4),
                "cv": None if cv is None else round(cv, 3),
                "zero_share": None if zero is None else round(zero, 3),
            },
            "yaw_total": round(self.yaw_total, 3),
        }


def analyse(records, segment):
    """Stats for one segment. Records must be in time order."""
    span = max(segment.end - segment.start, 1)
    stats = Stats(segment.label, span / 1e6)
    steady_from = segment.start + STEADY_SKIP * span
    steady_to = segment.end - STEADY_SKIP * span
    last_poll_px = None  # movement of the latest poll, for `visible`
    for r in records:
        if r.kind == "P" and segment.start <= r.t <= segment.end:
            x, y = poll_px(r)
            stats.polls += 1
            stats.px_x += x
            stats.px_y += y
            if x or y:
                stats.moving_polls += 1
                if r.fields.get("clamp") == "1":
                    stats.clamped += 1
            last_poll_px = x
        elif r.kind == "U" and segment.start <= r.t <= segment.end + TAIL_US:
            srr = number(r.fields.get("srr"))
            if srr is not None:
                stats.yaw_total += srr
            if r.t > segment.end:
                continue
            stats.updates += 1
            tick = number(r.fields.get("tick"))
            if tick is not None:
                stats.ticks.append(tick)
            if last_poll_px is not None:
                stats.visible_x += last_poll_px
                last_poll_px = None  # one update can only use one poll
            rr = number(r.fields.get("rr"))
            if steady_from <= r.t <= steady_to:
                stats.steady_rr.append(rr or 0.0)
    return stats


def format_stats(d):
    def show(value, unit=""):
        return "n/a" if value is None else f"{value}{unit}"

    yaw = d["yaw_per_update"]
    lines = [
        f"{d['label']}  ({d['seconds']} s)",
        f"  polls {d['polls']} ({show(d['polls_per_s'])}/s), camera updates {d['updates']} "
        f"({show(d['updates_per_s'])}/s), polls/update {show(d['polls_per_update'])}",
        f"  movement {d['px'][0]},{d['px'][1]} px; clamped {show(d['clamped_share'])} of moving polls",
    ]
    if d["updates"]:
        lines += [
            f"  visible to the game: {show(d['visible_share'])} of x movement",
            f"  yaw per update (steady, n={yaw['n']}): mean {show(yaw['mean'])} sd {show(yaw['sd'])} "
            f"cv {show(yaw['cv'])} zero {show(yaw['zero_share'])}",
            f"  yaw total {d['yaw_total']}  tick rates {d['tick_rates'] or 'n/a'}",
        ]
    else:
        lines.append("  no camera updates: run `f2 mouse trace on` (with a save loaded) to get yaw numbers")
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description="Summarise fable2_mouse.log.")
    parser.add_argument("log", help="path to fable2_mouse.log")
    parser.add_argument("--json", action="store_true", help="print JSON instead of text")
    parser.add_argument("--whole", action="store_true", help="one segment for the whole log")
    args = parser.parse_args(argv)
    try:
        with open(args.log, encoding="utf-8", errors="replace") as f:
            records = read_log(f)
    except OSError as e:
        print(f"cannot read {args.log}: {e}", file=sys.stderr)
        return 1
    records.sort(key=lambda r: r.t)
    results = [analyse(records, s).as_dict() for s in find_segments(records, args.whole)]
    if not results:
        print("no records in the log", file=sys.stderr)
        return 1
    if args.json:
        print(json.dumps(results, indent=2))
    else:
        print("\n\n".join(format_stats(d) for d in results))
    return 0


if __name__ == "__main__":
    sys.exit(main())
