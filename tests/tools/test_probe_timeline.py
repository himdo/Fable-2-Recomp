"""Check the probing timeline (tools/probe_timeline.py) on made-up logs, without a running game."""
from datetime import datetime
import importlib.util
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("timeline", ROOT / "tools/probe_timeline.py")
timeline = importlib.util.module_from_spec(spec)
spec.loader.exec_module(timeline)
spec = importlib.util.spec_from_file_location("names", ROOT / "tools/check_manifest_names.py")
names = importlib.util.module_from_spec(spec)
spec.loader.exec_module(names)


def at(clock):
    """A time on the test day, e.g. at("14:03:12.345")."""
    return datetime.strptime(f"2026-11-26 {clock}", timeline.TIME_FORMAT)


def note(clock, text):
    """A game log line for `f2 echo <text>`, in the SDK's file pattern."""
    return f"[2026-11-26 {clock}] [info] [fable2] [t1234] [echo] {text}"


def seen(clock, name):
    """A first-seen log line."""
    return f"[2026-11-26 {clock}] {name}"


def minute(clock, calls, name):
    """A minutes log line: `name` was called `calls` times in the minute written at `clock`."""
    return f"[2026-11-26 {clock}] {calls} {name}"


# A session in two parts for the busier tests: a quiet start (14:00) and a first fight (14:10).
NOTES = [(at("14:00:00.000"), "start"), (at("14:10:00.000"), "first fight")]
FIRST_SEEN = {"sub_82100000": at("14:00:30.000"), "sub_82200000": at("14:00:40.000")}


def quiet_then(fight_calls):
    """Three quiet minutes with each function at 60 calls, then one fight minute with `fight_calls`."""
    lines = [minute(f"14:0{n}:00.000", 60, name) for n in (1, 2, 3) for name in FIRST_SEEN]
    lines += [minute("14:11:00.000", calls, name) for name, calls in fight_calls.items()]
    return timeline.read_minutes(lines)


def fight(minutes, **options):
    """The first-fight segment of that session."""
    return timeline.build_segments(NOTES, FIRST_SEEN, minutes=minutes, **options)[1]


def busier_names(segment):
    return [busier.name for busier in segment.busier]


def describe_read_notes():
    # --- Happy flow -------------------------------------------------------

    def test_should_read_the_time_and_the_note():
        assert timeline.read_notes([note("14:03:12.345", "first fight")]) == [(at("14:03:12.345"), "first fight")]

    def test_should_ignore_lines_that_are_not_echo_replies():
        lines = [
            "[2026-11-26 14:00:00.000] [info] [fable2] [t1234] [stats] gold 120",
            "[2026-11-26 14:00:01.000] [warning] [gpu] [t99] something else",
        ]
        assert timeline.read_notes(lines) == []

    # --- Edge cases -------------------------------------------------------

    def test_should_skip_an_empty_note():
        # `f2 echo` with no text still logs an [echo] line.
        assert timeline.read_notes([note("14:00:00.000", "")]) == []

    def test_should_keep_a_note_that_mentions_echo():
        assert timeline.read_notes([note("14:00:00.000", "said [echo] twice")]) == [(at("14:00:00.000"), "said [echo] twice")]

    def test_should_read_windows_line_endings():
        assert timeline.read_notes([note("14:00:00.000", "start") + "\r"]) == [(at("14:00:00.000"), "start")]

    def test_should_sort_notes_by_time():
        lines = [note("14:05:00.000", "later"), note("14:01:00.000", "earlier")]
        assert [text for _, text in timeline.read_notes(lines)] == ["earlier", "later"]


def describe_read_first_seen():
    # --- Happy flow -------------------------------------------------------

    def test_should_read_each_function_and_its_time():
        lines = [seen("14:00:00.100", "sub_82100000"), seen("14:00:00.200", "Cloth_Step_8219ADD8")]
        assert timeline.read_first_seen(lines) == {"sub_82100000": at("14:00:00.100"),
                                                   "Cloth_Step_8219ADD8": at("14:00:00.200")}

    # --- Bad flow ---------------------------------------------------------

    def test_should_skip_lines_that_do_not_match():
        lines = ["", "garbage", "[14:00:00] sub_82100000", seen("14:00:00.100", "two words")]
        assert timeline.read_first_seen(lines) == {}

    # --- Edge cases -------------------------------------------------------

    def test_should_keep_the_earliest_time_of_a_repeated_name():
        lines = [seen("14:05:00.000", "sub_82100000"), seen("14:01:00.000", "sub_82100000")]
        assert timeline.read_first_seen(lines) == {"sub_82100000": at("14:01:00.000")}


def describe_build_segments():
    # --- Happy flow -------------------------------------------------------

    def test_should_give_each_note_the_functions_first_seen_until_the_next_note():
        notes = [(at("14:00:00.000"), "entered the Guild"), (at("14:10:00.000"), "first fight")]
        first_seen = {"sub_82100000": at("14:02:00.000"), "sub_82200000": at("14:11:00.000")}
        segments = timeline.build_segments(notes, first_seen)
        assert [(s.note, [name for _, name in s.functions]) for s in segments] == [
            ("entered the Guild", ["sub_82100000"]),
            ("first fight", ["sub_82200000"]),
        ]

    def test_should_list_a_segment_s_functions_in_time_order():
        notes = [(at("14:00:00.000"), "start")]
        first_seen = {"sub_82200000": at("14:00:02.000"), "sub_82100000": at("14:00:01.000")}
        segment = timeline.build_segments(notes, first_seen)[0]
        assert [name for _, name in segment.functions] == ["sub_82100000", "sub_82200000"]

    def test_should_list_only_placeholders_when_asked():
        notes = [(at("14:00:00.000"), "start")]
        first_seen = {"sub_82100000": at("14:00:01.000"), "Cloth_Step_8219ADD8": at("14:00:02.000")}
        segment = timeline.build_segments(notes, first_seen, unnamed_only=True)[0]
        assert [name for _, name in segment.functions] == ["sub_82100000"]

    # --- Edge cases -------------------------------------------------------

    def test_should_put_functions_before_the_first_note_in_their_own_segment():
        notes = [(at("14:10:00.000"), "start")]
        first_seen = {"sub_82100000": at("14:00:00.000")}
        segments = timeline.build_segments(notes, first_seen)
        assert [(s.start, s.note) for s in segments] == [(None, timeline.BEFORE_FIRST_NOTE),
                                                         (at("14:10:00.000"), "start")]

    def test_should_leave_out_the_segment_before_the_first_note_when_it_is_empty():
        segments = timeline.build_segments([(at("14:00:00.000"), "start")], {})
        assert [s.note for s in segments] == ["start"]

    def test_should_give_a_function_seen_at_a_note_s_time_to_that_note():
        notes = [(at("14:00:00.000"), "start"), (at("14:10:00.000"), "first fight")]
        segments = timeline.build_segments(notes, {"sub_82100000": at("14:10:00.000")})
        assert segments[1].functions == [(at("14:10:00.000"), "sub_82100000")]

    def test_should_keep_notes_with_no_new_functions():
        notes = [(at("14:00:00.000"), "start"), (at("14:10:00.000"), "nothing new here")]
        segments = timeline.build_segments(notes, {"sub_82100000": at("14:01:00.000")})
        assert segments[1].functions == []

    def test_should_put_everything_in_one_segment_without_notes():
        segments = timeline.build_segments([], {"sub_82100000": at("14:00:00.000")})
        assert [(s.note, len(s.functions)) for s in segments] == [(timeline.BEFORE_FIRST_NOTE, 1)]


def describe_read_minutes():
    # --- Happy flow -------------------------------------------------------

    def test_should_read_the_calls_per_function_in_each_minute():
        lines = [minute("14:01:00.000", 60, "sub_82100000"), minute("14:01:00.000", 5, "sub_82200000"),
                 minute("14:02:00.000", 70, "sub_82100000")]
        assert timeline.read_minutes(lines) == {
            at("14:01:00.000"): {"sub_82100000": 60, "sub_82200000": 5},
            at("14:02:00.000"): {"sub_82100000": 70},
        }

    # --- Bad flow ---------------------------------------------------------

    def test_should_skip_lines_that_do_not_match():
        lines = ["", "garbage", seen("14:01:00.000", "sub_82100000"),
                 minute("14:01:00.000", -5, "sub_82100000"), minute("14:01:00.000", "many", "sub_82100000")]
        assert timeline.read_minutes(lines) == {}

    # --- Edge cases -------------------------------------------------------

    def test_should_add_up_a_name_repeated_in_one_minute():
        # The tracer may write one line per thread.
        lines = [minute("14:01:00.000", 60, "sub_82100000"), minute("14:01:00.000", 40, "sub_82100000")]
        assert timeline.read_minutes(lines) == {at("14:01:00.000"): {"sub_82100000": 100}}


def describe_find_busier():
    # --- Happy flow -------------------------------------------------------

    def test_should_flag_a_function_called_much_more_often_than_elsewhere():
        segment = fight(quiet_then({"sub_82100000": 600}))
        assert segment.busier == [timeline.Busier("sub_82100000", 600, 60)]

    def test_should_sort_by_how_much_busier_highest_first():
        segment = fight(quiet_then({"sub_82100000": 600, "sub_82200000": 1200}))
        assert busier_names(segment) == ["sub_82200000", "sub_82100000"]

    def test_should_put_a_function_idle_elsewhere_first():
        # sub_82300000 ran once before the fight (first seen), then not again until it.
        first_seen = {**FIRST_SEEN, "sub_82300000": at("14:00:50.000")}
        minutes = quiet_then({"sub_82100000": 6000, "sub_82300000": 100})
        segment = timeline.build_segments(NOTES, first_seen, minutes=minutes)[1]
        assert busier_names(segment) == ["sub_82300000", "sub_82100000"]

    def test_should_not_flag_a_function_under_the_factor():
        assert fight(quiet_then({"sub_82100000": 240})).busier == []

    def test_should_not_flag_a_function_under_the_minimum_rate():
        segment = fight(quiet_then({"sub_82100000": 600}), min_rate=1000)
        assert segment.busier == []

    def test_should_use_the_factor_given():
        assert busier_names(fight(quiet_then({"sub_82100000": 240}), factor=3)) == ["sub_82100000"]

    def test_should_list_only_placeholders_when_asked():
        first_seen = {"sub_82100000": at("14:00:30.000"), "Cloth_Step_8219ADD8": at("14:00:40.000")}
        lines = [minute(f"14:0{n}:00.000", 60, name) for n in (1, 2, 3) for name in first_seen]
        lines += [minute("14:11:00.000", 600, name) for name in first_seen]
        segments = timeline.build_segments(NOTES, first_seen, unnamed_only=True,
                                           minutes=timeline.read_minutes(lines))
        assert busier_names(segments[1]) == ["sub_82100000"]

    # --- Edge cases -------------------------------------------------------

    def test_should_flag_a_function_exactly_at_the_factor():
        assert busier_names(fight(quiet_then({"sub_82100000": 300}))) == ["sub_82100000"]

    def test_should_leave_out_a_function_first_seen_in_the_same_segment():
        # It's already in the segment's first-seen list.
        first_seen = {**FIRST_SEEN, "sub_82300000": at("14:10:30.000")}
        minutes = quiet_then({"sub_82300000": 600})
        segment = timeline.build_segments(NOTES, first_seen, minutes=minutes)[1]
        assert segment.busier == []

    def test_should_count_a_minute_written_at_a_note_s_time_for_that_note():
        lines = [minute(f"14:0{n}:00.000", 60, "sub_82100000") for n in (1, 2, 3)]
        lines.append(minute("14:10:00.000", 600, "sub_82100000"))
        assert busier_names(fight(timeline.read_minutes(lines))) == ["sub_82100000"]

    def test_should_compare_nothing_when_every_minute_is_in_one_segment():
        lines = [minute("14:11:00.000", 600, "sub_82100000"), minute("14:12:00.000", 900, "sub_82100000")]
        assert fight(timeline.read_minutes(lines)).busier == []

    def test_should_flag_a_function_missing_from_the_first_seen_log():
        lines = [minute(f"14:0{n}:00.000", 60, "sub_82900000") for n in (1, 2, 3)]
        lines.append(minute("14:11:00.000", 600, "sub_82900000"))
        assert busier_names(fight(timeline.read_minutes(lines))) == ["sub_82900000"]

    def test_should_keep_the_part_before_the_first_note_when_only_its_busier_list_has_names():
        # Nothing was first seen before 14:00, but sub_82900000 (not in the first-seen log) was busy then.
        lines = [minute("13:59:00.000", 600, "sub_82900000"),
                 minute("14:01:00.000", 60, "sub_82900000"), minute("14:02:00.000", 60, "sub_82900000")]
        segments = timeline.build_segments(NOTES, FIRST_SEEN, minutes=timeline.read_minutes(lines))
        assert (segments[0].note, busier_names(segments[0])) == (timeline.BEFORE_FIRST_NOTE, ["sub_82900000"])


def describe_per_minute():
    def test_should_round_to_one_decimal_and_drop_a_trailing_zero():
        assert [timeline.per_minute(rate) for rate in (600.0, 100 / 3, 0.04)] == ["600/min", "33.3/min", "0/min"]


def describe_placeholder():
    def test_should_match_the_naming_check_s_rule():
        # The two tools must agree on what counts as a placeholder name.
        assert timeline.PLACEHOLDER.pattern == names.GENERIC.pattern


def describe_main():
    # --- Happy flow -------------------------------------------------------

    def test_should_print_each_note_with_its_functions(tmp_path, capsys):
        game_log = tmp_path / "game.log"
        game_log.write_text(note("14:00:00.000", "entered the Guild") + "\n", encoding="utf-8")
        first_seen_log = tmp_path / "fable2_func_firstseen.log"
        first_seen_log.write_text(seen("14:00:01.250", "sub_82100000") + "\n", encoding="utf-8")
        assert timeline.main([str(game_log), str(first_seen_log)]) == 0
        assert capsys.readouterr().out == ("14:00:00  entered the Guild  (1 function)\n"
                                           "  14:00:01.250  sub_82100000\n")

    def test_should_print_the_busier_functions_with_a_minutes_log(tmp_path, capsys):
        game_log = tmp_path / "game.log"
        game_log.write_text("\n".join([note("14:00:00.000", "start"), note("14:10:00.000", "first fight")]),
                            encoding="utf-8")
        first_seen_log = tmp_path / "fable2_func_firstseen.log"
        first_seen_log.write_text(seen("14:00:01.000", "sub_82100000"), encoding="utf-8")
        minutes_log = tmp_path / "fable2_func_minutes.log"
        minutes_log.write_text("\n".join([minute("14:01:00.000", 60, "sub_82100000"),
                                          minute("14:11:00.000", 600, "sub_82100000")]), encoding="utf-8")
        assert timeline.main([str(game_log), str(first_seen_log), "--minutes", str(minutes_log)]) == 0
        assert capsys.readouterr().out == ("14:00:00  start  (1 function)\n"
                                           "  14:00:01.000  sub_82100000\n"
                                           "14:10:00  first fight  (0 functions)\n"
                                           "  busier than elsewhere (1):\n"
                                           "    sub_82100000  600/min here, 60/min elsewhere\n")

    # --- Bad flow ---------------------------------------------------------

    def test_should_exit_2_when_a_log_is_missing(tmp_path, capsys):
        game_log = tmp_path / "game.log"
        game_log.write_text("", encoding="utf-8")
        assert timeline.main([str(game_log), str(tmp_path / "missing.log")]) == 2
        assert "missing.log" in capsys.readouterr().err

    def test_should_exit_2_when_the_minutes_log_is_missing(tmp_path, capsys):
        empty = tmp_path / "empty.log"
        empty.write_text("", encoding="utf-8")
        assert timeline.main([str(empty), str(empty), "--minutes", str(tmp_path / "missing.log")]) == 2
        assert "missing.log" in capsys.readouterr().err

    @pytest.mark.parametrize("option", [["--factor", "1"], ["--factor", "0.5"], ["--min-rate", "-1"]])
    def test_should_give_a_usage_error_for_an_option_out_of_range(tmp_path, capsys, option):
        empty = tmp_path / "empty.log"
        empty.write_text("", encoding="utf-8")
        with pytest.raises(SystemExit) as stopped:
            timeline.main([str(empty), str(empty), *option])
        assert stopped.value.code == 2
        assert option[0] in capsys.readouterr().err

    # --- Edge cases -------------------------------------------------------

    def test_should_read_a_log_with_bytes_that_are_not_utf_8(tmp_path, capsys):
        game_log = tmp_path / "game.log"
        game_log.write_bytes(b"\xff\xfe junk\n" + note("14:00:00.000", "start").encode() + b"\n")
        first_seen_log = tmp_path / "fable2_func_firstseen.log"
        first_seen_log.write_text("", encoding="utf-8")
        assert timeline.main([str(game_log), str(first_seen_log)]) == 0
        assert capsys.readouterr().out == "14:00:00  start  (0 functions)\n"
