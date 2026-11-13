"""Check that tools/mouse_stats.py reads fable2_mouse.log the way the game writes it.

The log is written by three places: src/input/mouse_diag.h (P poll lines and N notes),
scripts/recomp/mouse_diag.lua (U camera update lines, through the host's cprint hook in
src/core/fable2_console.h) and the `f2 mouse inject` command (the inject note). These tests
read the format strings from those sources, fill them in with sample values and run the
result through the summariser. A renamed or reshaped field fails here, instead of giving
wrong numbers without any error.
"""
import importlib.util
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("mouse_stats", ROOT / "tools/mouse_stats.py")
mouse_stats = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mouse_stats)

DIAG_H = ROOT / "src/input/mouse_diag.h"
DIAG_LUA = ROOT / "src/lua/mouse_diag.lua"
CONSOLE_H = ROOT / "src/core/fable2_console.h"

# key=<one or two printf conversions>, such as dx=%d, inj=%d,%d or mho=%s>%s.
CONVERSION = r"%[-\w.]+"
FIELD = re.compile(rf"(\w+)=({CONVERSION}(?:[,>]{CONVERSION})?)")


def source(path):
    return path.read_text(encoding="utf-8")


def snprintf_formats():
    """Each snprintf format in mouse_diag.h, with its adjacent string literals joined."""
    calls = re.findall(r"std::snprintf\(\s*buf,\s*sizeof buf,\s*((?:\"[^\"]*\"\s*)+)", source(DIAG_H))
    return ["".join(re.findall(r'"([^"]*)"', literals)) for literals in calls]


def poll_formats():
    """The format of the P line OnPoll writes."""
    return [f for f in snprintf_formats() if f.startswith("P ")]


def update_formats():
    """The format of the U line mouse_diag.lua prints, tag included."""
    return re.findall(r'"(\[f2diag\] U [^"]*)"', source(DIAG_LUA))


def diag_tags():
    """The tag the host's cprint hook routes into the log."""
    return re.findall(r'kDiag\s*=\s*"([^"]*)"', source(CONSOLE_H))


def inject_notes():
    """The pieces of the note `f2 mouse inject` writes: literals, and calls on a variable.

    A call is std::to_string(dx) or a name helper such as md::ProfileName(profile); each piece
    is returned as (literal, "") or ("", variable).
    """
    calls = re.findall(r'Note\(\s*("inject dx="(?:\s*\+\s*(?:"[^"]*"|[\w:]+\(\w+\)))*)\s*\)',
                       source(CONSOLE_H))
    return [re.findall(r'"([^"]*)"|[\w:]+\((\w+)\)', call) for call in calls]


def fill(fmt, samples):
    """Fill each key=%... field with samples[key], and any other field with 0."""
    return FIELD.sub(lambda m: f"{m.group(1)}={samples.get(m.group(1), '0')}", fmt).replace("\\n", "")


def poll_line():
    """A P line as OnPoll writes it, for a poll that moved and was clamped, with smoothing on."""
    samples = {"t": "123456", "dx": "-3", "dy": "2", "inj": "4,-1", "clamp": "1", "sm": "1", "w": "20,0"}
    return fill(poll_formats()[0], samples)


def update_line():
    """A U line as it reaches the log: the Lua line with the host's time added by OnLuaLine."""
    samples = {"f": "1", "tick": "30", "act": "2:0.500/0.000", "rr": "-2.0000", "srr": "-1.5000",
               "mho": "0.0000>0.0000", "cmho": "0.0000", "man": "1"}
    lua_line = fill(update_formats()[0], samples)
    text = lua_line[len(diag_tags()[0]):]
    return "U t=500000 " + text[len("U "):]


def inject_note_line(profile="even"):
    """The N line for `f2 mouse inject 2400 0 4000 <profile>`, as Diag::Note writes it."""
    values = {"dx": "2400", "dy": "0", "ms": "4000", "profile": profile}
    note = "".join(literal or values[name] for literal, name in inject_notes()[0])
    return "N t=1000 " + note


def describe_poll_line():
    def test_should_be_found_once_in_the_source():
        assert len(poll_formats()) == 1

    def test_should_use_only_fields_with_a_value():
        # Every printf conversion sits in a key=value field, so fill() replaces all of them.
        assert "%" not in poll_line()

    def test_should_parse_as_a_poll_at_its_time():
        record = mouse_stats.parse_line(poll_line())

        assert (record.kind, record.t) == ("P", 123456)

    def test_should_carry_the_cursor_and_injected_movement():
        record = mouse_stats.parse_line(poll_line())

        assert mouse_stats.poll_px(record) == (7, 3)

    def test_should_carry_the_clamp_flag():
        record = mouse_stats.parse_line(poll_line())

        assert record.fields.get("clamp") == "1"

    def test_should_carry_the_smoothing_flag():
        record = mouse_stats.parse_line(poll_line())

        assert record.fields.get("sm") == "1"

    def test_should_carry_the_smoothing_window_movement():
        record = mouse_stats.parse_line(poll_line())

        assert mouse_stats.pair(record.fields.get("w")) == (20, 0)


def describe_camera_update_line():
    def test_should_be_found_once_in_the_lua_trace():
        assert len(update_formats()) == 1

    def test_should_be_tagged_with_the_tag_the_host_routes_into_the_log():
        assert update_formats()[0].startswith(diag_tags()[0])

    def test_should_get_its_time_from_the_host():
        # OnLuaLine turns "U <rest>" into "U t=<us> <rest>"; update_line() repeats that.
        assert '"U t=" + std::to_string(t) + " "' in source(DIAG_H)

    def test_should_parse_as_an_update_at_its_time():
        record = mouse_stats.parse_line(update_line())

        assert (record.kind, record.t) == ("U", 500000)

    def test_should_carry_the_yaw_going_into_the_update():
        record = mouse_stats.parse_line(update_line())

        assert mouse_stats.number(record.fields.get("rr")) == -2.0

    def test_should_carry_the_yaw_the_update_applied():
        record = mouse_stats.parse_line(update_line())

        assert mouse_stats.number(record.fields.get("srr")) == -1.5

    def test_should_carry_the_tick_rate():
        record = mouse_stats.parse_line(update_line())

        assert mouse_stats.number(record.fields.get("tick")) == 30.0


def describe_inject_note():
    def test_should_be_found_once_in_the_console():
        assert len(inject_notes()) == 1

    def test_should_be_written_as_a_note_line():
        # Diag::Note writes "N t=<us> <text>"; inject_note_line() repeats that.
        assert '"N t=" + std::to_string(NowUs()) + " " + text' in source(DIAG_H)

    def test_should_start_a_run_as_long_as_the_inject():
        records = mouse_stats.read_log([inject_note_line()])

        segments = mouse_stats.find_segments(records)

        assert [(s.start, s.end) for s in segments] == [(1000, 4_001_000)]

    def test_should_name_the_run_after_its_movement():
        records = mouse_stats.read_log([inject_note_line()])

        segments = mouse_stats.find_segments(records)

        assert segments[0].label == "inject 2400,0 px over 4000 ms"

    def test_should_name_the_speed_profile_in_the_run():
        records = mouse_stats.read_log([inject_note_line(profile="flick")])

        segments = mouse_stats.find_segments(records)

        assert segments[0].label == "inject 2400,0 px over 4000 ms (flick)"
