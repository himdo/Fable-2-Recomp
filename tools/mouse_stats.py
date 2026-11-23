#!/usr/bin/env python3
"""Summarise fable2_mouse.log into numbers for mouse-look stutter.

The log comes from src/input/mouse_diag.h (turn it on with `f2 mouse diag on`,
and the camera trace with `f2 mouse trace on`). Each `f2 mouse inject` run (and
each `f2 pad stick`) is reported as its own segment, so a before/after
comparison is two inject runs with the setting changed in between. Without inject runs the whole log is one
segment.

For each segment:
  polls, updates      input polls and camera updates, with rates
  polls/update        how many polls the game made per camera update
  visible             share of the x movement that reached the camera. The
                      camera reads the first poll after its previous update
                      (measured 2026-11-11), so this counts that poll's
                      movement: its own pixels with smoothing off (sm=0), or
                      its velocity window (w=) with smoothing on (sm=1)
  clamped             polls where the stick hit full deflection (speed cap)
  yaw per update      RotateRight going into each update, over the steady
                      middle of the run (the first and last 15% are skipped):
                      mean, standard deviation, and cv = sd / |mean|, the
                      "stutter score" (lower is smoother)
  zero                share of steady updates that got no yaw at all
  yaw total           sum of SmoothRotateRight (the yaw the game's stick path
                      applied)
  turn                what the camera actually turned, in degrees: the stick
                      path (srr) plus the direct mouse camera (my), over the run
                      and up to 500 ms after it
    total             the whole turn. The same movement should give the same
                      total whatever its speed (compare an `inject` over 100 ms
                      with one over 2000 ms)
    mouse             the part the direct mouse camera added
    tail              share of the turn that arrived after the input stopped,
                      and in how many camera updates (the "keeps going" coast)
    first             the first moving update's turn as a share of the steady
                      turn per update (how quickly the turn gets going)
  reticle             for runs where the mouse aimed (aim=1 polls, the aim stick
                      swap, `f2 mouse aim`) or the game moved the aim cursor
                      (a `pad stick` run while aiming): how far the cursor
                      (cur=, cursor units, the game's box is about -0.8..0.8)
                      moved from the last poll before the run to its end and in
                      how many of the game's cursor updates (atk=); how far it
                      moved in the 500 ms after (coasting) and by 2 s after
                      (settled: springing back to the middle), each with its
                      updates, or n/a when there were none (the game had stopped
                      aiming, so cur= froze); the share of aim polls; and the
                      game's cursor updates per second

Segments are labelled with the inject's speed profile (ramp, flick), the
mouse mode (`f2 mouse mode`), the aim mode (`f2 mouse aim`) and the aim input
(`f2 mouse aiminput`) when the log notes them.

usage: mouse_stats.py [--json] [--whole] <fable2_mouse.log>
"""
import argparse
import json
import math
import sys
from dataclasses import dataclass, field

STEADY_SKIP = 0.15  # fraction of a run skipped at each end for the steady stats
TAIL_US = 500_000   # smoothing keeps turning the camera after the input stops
# How long after a run the reticle is followed for a slow drift back to the middle
# (the game's own "leave aiming" time is 1 s).
RETICLE_SETTLE_US = 2_000_000
TURN_EPS = 1e-4     # degrees; smaller turns count as none


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


def float_pair(value):
    """'a,b' -> (float a, float b); None when missing, malformed or not finite."""
    try:
        a, b = value.split(",")
    except (AttributeError, ValueError):
        return None
    x, y = number(a), number(b)
    return None if x is None or y is None else (x, y)


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
    until: int = None  # the next run's start: later polls belong to it


# A `pad stick` run without its release note (an old log, or the log cut short)
# is taken to last its frame count at 30 frames a second.
PAD_FRAME_US = 33_333

RUN_NOTES = ("inject", "padstick")


def run_label(note, mode, aim, aim_input=None):
    """The label of an inject or `pad stick` run, with the modes in force."""
    if note.text == "padstick":
        label = (f"pad stick {note.fields.get('lx', '?')},{note.fields.get('ly', '?')} "
                 f"for {note.fields.get('frames', '?')} frames")
    else:
        ms = int(note.fields.get("ms", 0) or 0)
        label = f"inject {note.fields.get('dx', '?')},{note.fields.get('dy', '?')} px over {ms} ms"
        profile = note.fields.get("profile")
        if profile and profile != "even":
            label += f" ({profile})"
    tags = ([mode] if mode else []) + ([f"aim {aim}"] if aim else [])
    if aim_input:
        tags.append(f"{aim_input} aim")
    if tags:
        label += f" [{', '.join(tags)}]"
    return label


def run_end(note):
    """When a run's input ends, as far as its own note says."""
    if note.text == "padstick":
        return note.t + int(note.fields.get("frames", 0) or 0) * PAD_FRAME_US
    return note.t + int(note.fields.get("ms", 0) or 0) * 1000


def find_segments(records, whole=False):
    """One segment per inject or `pad stick` note, or the whole log when there are none."""
    runs = [r for r in records if r.kind == "N" and r.text in RUN_NOTES]
    if whole or not runs:
        if not records:
            return []
        return [Segment("whole log", records[0].t, records[-1].t)]
    segments = []
    mode = None
    aim = None
    aim_input = None
    stick = None  # the `pad stick` run still held
    for note in (r for r in records if r.kind == "N"):
        words = note.text.split()
        if len(words) == 2 and words[0] == "mode":
            mode = words[1]
            continue
        if len(words) == 2 and words[0] == "aim":
            aim = words[1]
            continue
        if len(words) == 2 and words[0] == "aiminput":
            aim_input = words[1]
            continue
        if note.text == "inject stopped":
            # `f2 mouse stop` cuts the running inject short.
            if segments and segments[-1].start <= note.t < segments[-1].end:
                segments[-1].end = note.t
            continue
        if note.text == "padstick released":
            # The console's release time beats the frame-count estimate.
            if stick is not None and stick.start <= note.t:
                stick.end = note.t
            stick = None
            continue
        if note.text not in RUN_NOTES:
            continue
        segments.append(Segment(run_label(note, mode, aim, aim_input), note.t, run_end(note)))
        stick = segments[-1] if note.text == "padstick" else None
    for seg, nxt in zip(segments, segments[1:]):
        seg.until = nxt.start
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
    turn_total: float = 0.0   # srr + my, inside the run and in the tail
    mouse_total: float = 0.0  # my alone
    tail_turn: float = 0.0    # srr + my after the input stopped
    tail_updates: int = 0     # updates after the input stopped that still turned
    first_turn: float = None  # the first update in the run that turned
    steady_turn: list = field(default_factory=list)
    aim_polls: int = 0           # polls in the run where the mouse drove the aim reticle
    ret_before: object = None  # the reticle at the last poll before the run
    ret_first: object = None   # ... at the first poll in the run
    ret_end: object = None     # ... at the last poll in the run
    ret_tail: object = None    # ... at the last poll up to TAIL_US after it
    ret_settled: object = None  # ... at the last poll up to RETICLE_SETTLE_US after it

    def aim_dict(self):
        end = self.ret_end
        if end is None:
            return None
        start = self.ret_before or self.ret_first
        run_updates = cursor_updates(start, end)
        if not self.aim_polls and not run_updates:
            return None  # neither the mouse nor the game moved the reticle
        tail = self.ret_tail or end
        settled = self.ret_settled or tail
        tail_updates = cursor_updates(end, tail)
        settle_updates = cursor_updates(end, settled)
        # cur= only changes while the game updates the cursor (while it aims): with
        # no update after the run, an unchanged value says nothing about the reticle.
        return {
            "moved": cur_delta(start, end),
            "updates": run_updates,
            "after_stop": cur_delta(end, tail) if tail_updates else None,
            "after_stop_updates": tail_updates,
            "settled": cur_delta(end, settled) if settle_updates else None,
            "settled_updates": settle_updates,
            "end": [round(v, 4) for v in end.cur],
            "aim_share": round(self.aim_polls / self.polls, 3) if self.polls else None,
            "ticks_per_s": cursor_rate(self.ret_first, end),
        }

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
            "turn": self.turn_dict(),
            "reticle": self.aim_dict(),
        }

    def turn_dict(self):
        steady, _ = mean_sd(self.steady_turn)
        return {
            "total": round(self.turn_total, 3),
            "mouse": round(self.mouse_total, 3),
            "tail_share": round(abs(self.tail_turn) / abs(self.turn_total), 3) if self.turn_total else None,
            "tail_updates": self.tail_updates,
            "first_share": (round(self.first_turn / steady, 3)
                            if steady and self.first_turn is not None else None),
        }


@dataclass
class Reading:
    """The aim reticle one poll reported: cur= and the game's cursor update count atk=."""
    t: int
    cur: tuple
    atk: int = None


def cur_delta(a, b):
    return None if a is None or b is None else [round(b.cur[0] - a.cur[0], 4), round(b.cur[1] - a.cur[1], 4)]


def cursor_updates(a, b):
    """The game's cursor updates between two readings; None without counts."""
    if a is None or b is None or a.atk is None or b.atk is None:
        return None
    return b.atk - a.atk


def cursor_rate(a, b):
    """Cursor updates per second between two readings, from their own times."""
    n = cursor_updates(a, b)
    if n is None or b.t <= a.t:
        return None
    return round(n / ((b.t - a.t) / 1e6), 1)


def track_reticle(stats, r, segment):
    """The aim reticle one poll reported, sorted into before, during and after a run.

    After the run, polls up to RETICLE_SETTLE_US count, but not those of the next run.
    """
    if segment.start <= r.t <= segment.end and r.fields.get("aim") == "1":
        stats.aim_polls += 1
    cur = float_pair(r.fields.get("cur"))
    if cur is None:
        return
    atk = number(r.fields.get("atk"))
    reading = Reading(r.t, cur, None if atk is None else int(atk))
    if r.t < segment.start:
        stats.ret_before = reading
    elif r.t <= segment.end:
        if stats.ret_first is None:
            stats.ret_first = reading
        stats.ret_end = reading
    elif segment.until is None or r.t < segment.until:
        if r.t <= segment.end + TAIL_US:
            stats.ret_tail = reading
        if r.t <= segment.end + RETICLE_SETTLE_US:
            stats.ret_settled = reading


def analyse(records, segment):
    """Stats for one segment. Records must be in time order."""
    span = max(segment.end - segment.start, 1)
    stats = Stats(segment.label, span / 1e6)
    steady_from = segment.start + STEADY_SKIP * span
    steady_to = segment.end - STEADY_SKIP * span
    after_update = False  # a camera update happened and its next poll is still to come
    for r in records:
        if r.kind == "P":
            track_reticle(stats, r, segment)
        if r.kind == "P" and segment.start <= r.t <= segment.end:
            x, y = poll_px(r)
            stats.polls += 1
            stats.px_x += x
            stats.px_y += y
            if x or y:
                stats.moving_polls += 1
                if r.fields.get("clamp") == "1":
                    stats.clamped += 1
            if after_update:
                # The poll the next camera update will read.
                seen = abs(pair(r.fields.get("w"))[0]) if r.fields.get("sm") == "1" else x
                stats.visible_x += seen
                after_update = False
        elif r.kind == "U" and segment.start <= r.t <= segment.end + TAIL_US:
            srr = number(r.fields.get("srr"))
            if srr is not None:
                stats.yaw_total += srr
            my = number(r.fields.get("my")) or 0.0
            turn = (srr or 0.0) + my
            stats.turn_total += turn
            stats.mouse_total += my
            if r.t > segment.end:
                stats.tail_turn += turn
                if abs(turn) > TURN_EPS:
                    stats.tail_updates += 1
                continue
            if stats.first_turn is None and abs(turn) > TURN_EPS:
                stats.first_turn = turn
            stats.updates += 1
            tick = number(r.fields.get("tick"))
            if tick is not None:
                stats.ticks.append(tick)
            after_update = True
            rr = number(r.fields.get("rr"))
            if steady_from <= r.t <= steady_to:
                stats.steady_rr.append(rr or 0.0)
                stats.steady_turn.append(turn)
    return stats


def format_stats(d):
    def show(value, unit=""):
        return "n/a" if value is None else f"{value}{unit}"

    yaw = d["yaw_per_update"]
    turn = d["turn"]
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
            f"  turn total {turn['total']} (mouse {turn['mouse']}); after the input stopped "
            f"{show(turn['tail_share'])} in {turn['tail_updates']} updates; "
            f"first update {show(turn['first_share'])} of steady",
        ]
    else:
        lines.append("  no camera updates: run `f2 mouse trace on` (with a save loaded) to get yaw numbers")
    aim = d.get("reticle")
    if aim:
        lines.append(format_reticle(aim, show))
    return "\n".join(lines)


def format_reticle(aim, show):
    def xy(v):
        return "n/a" if v is None else f"{v[0]},{v[1]}"

    def after(name, delta, updates):
        # No cursor update after the run: the game had stopped aiming, so cur= froze.
        if delta is None:
            return f"{name} n/a (no cursor updates: aim ended)"
        return f"{name} {xy(delta)} in {updates}"

    return (f"  reticle moved {xy(aim['moved'])} in {show(aim['updates'])} updates; "
            f"{after('after stop', aim['after_stop'], aim['after_stop_updates'])}; "
            f"{after('settled', aim['settled'], aim['settled_updates'])}; "
            f"at {xy(aim['end'])}; aim polls {show(aim['aim_share'])}; "
            f"cursor updates {show(aim['ticks_per_s'])}/s")


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
