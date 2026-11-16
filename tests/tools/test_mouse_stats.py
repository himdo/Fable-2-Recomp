"""Check the fable2_mouse.log summariser with made-up logs, without a running game."""
import importlib.util
import json
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("mouse_stats", ROOT / "tools/mouse_stats.py")
mouse_stats = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mouse_stats)


def poll(t, dx=0, dy=0, inj=(0, 0), clamp=0, sm=0, w=(0, 0)):
    """A P line as src/input/mouse_diag.h writes it."""
    return (f"P t={t} tid=1 gap=0 dx={dx} dy={dy} inj={inj[0]},{inj[1]} rx=0 ry=0 clamp={clamp} "
            f"look=1 sm={sm} w={w[0]},{w[1]}")


def update(t, rr, srr=None, tick=30):
    """A U line as the host writes it for scripts/recomp/mouse_diag.lua."""
    srr = rr if srr is None else srr
    return f"U t={t} f=1 tick={tick} act=2:0.500/0.000 rr={rr} srr={srr} mho=0.0000>0.0000 cmho=0.0000 man=1"


def direct_update(t, my, srr=0.0):
    """A camera update where the direct mouse camera added `my` degrees."""
    return (f"U t={t} f=1 tick=15 act=- rr=0.0000 srr={srr} mho=0.0000>0.0000 cmho=0.0000 "
            f"man=1 my={my} mdh=0.0000")


def inject(t, dx, dy, ms):
    """The note `f2 mouse inject` writes."""
    return f"N t={t} inject dx={dx} dy={dy} ms={ms}"


def two_polls_per_update(sm=0, w=(0, 0)):
    """A one-second inject run of 200 px: ten camera updates, each followed by two 10 px polls.

    The camera reads only the first poll after its previous update.
    """
    lines = [inject(0, 200, 0, 1000)]
    for i in range(10):
        base = i * 100_000
        lines += [update(base + 1_000, -2.0),
                  poll(base + 10_000, inj=(10, 0), sm=sm, w=w),
                  poll(base + 60_000, inj=(10, 0), sm=sm, w=w)]
    return lines


def alternating_yaw_run():
    """Camera updates every 50 ms over a one-second run.

    The steady middle (150-850 ms) holds seven -1s and seven -3s, so mean -2, sd 1 and
    cv 0.5. The updates at the ends get -100, which would skew those numbers.
    """
    lines = [inject(0, 100, 0, 1000)]
    for i in range(1, 20):
        t = i * 50_000 + 1_000
        if t < 150_000 or t > 850_000:
            lines.append(update(t, -100.0))
        else:
            lines.append(update(t, -1.0 if i % 2 else -3.0))
    return lines


def ramping_direct_run():
    """Direct mouse updates every 50 ms over a one-second run: 1 degree, then 2 from the second update on.

    The steady middle starts at 150 ms, so the steady turn per update is 2.
    """
    lines = [inject(0, 100, 0, 1000)]
    for i in range(1, 20):
        lines.append(direct_update(i * 50_000, -float(min(i, 2))))
    return lines


def coasting_stick_run():
    """A stick-path run where smoothing keeps turning for three updates after the inject ends at 200 ms."""
    return [inject(0, 100, 0, 200),
            update(60_000, -8.0, srr=-3.2), update(130_000, -8.0, srr=-5.1),
            update(260_000, 0.0, srr=-3.1), update(330_000, 0.0, srr=-1.8),
            update(400_000, 0.0, srr=-0.8)]


def records_of(lines):
    return sorted(mouse_stats.read_log(lines), key=lambda r: r.t)


def analyse_first(lines):
    """Stats for the first segment of the log, as the report's dict."""
    records = records_of(lines)
    segment = mouse_stats.find_segments(records)[0]
    return mouse_stats.analyse(records, segment).as_dict()


@pytest.fixture
def run_main(tmp_path, capsys):
    """Return a function that writes lines to a log, runs main() on it and returns (code, out, err)."""
    def run(lines, *args):
        log = tmp_path / "fable2_mouse.log"
        log.write_text("\n".join(lines) + "\n", encoding="utf-8")
        code = mouse_stats.main([str(log), *args])
        captured = capsys.readouterr()
        return code, captured.out, captured.err

    return run


def describe_parse_line():
    def test_should_read_the_kind_and_time():
        record = mouse_stats.parse_line("N t=42 inject dx=10 dy=-3 ms=500")

        assert (record.kind, record.t) == ("N", 42)

    def test_should_read_each_key_value_field():
        record = mouse_stats.parse_line("N t=42 inject dx=10 dy=-3 ms=500")

        assert record.fields == {"t": "42", "dx": "10", "dy": "-3", "ms": "500"}

    def test_should_keep_words_without_a_value_as_text():
        record = mouse_stats.parse_line("N t=42 inject dx=10 dy=-3 ms=500")

        assert record.text == "inject"

    def test_should_keep_the_first_value_of_a_repeated_key():
        record = mouse_stats.parse_line("P t=1 dx=1 dx=2")

        assert (record.fields["dx"], record.text) == ("1", "dx=2")

    def test_should_skip_a_comment():
        assert mouse_stats.parse_line("# fable2_mouse.log v1") is None

    def test_should_skip_a_blank_line():
        assert mouse_stats.parse_line("   ") is None

    def test_should_skip_a_line_without_a_time():
        assert mouse_stats.parse_line("P dx=1") is None

    def test_should_skip_a_line_whose_time_is_not_a_number():
        assert mouse_stats.parse_line("P t=abc dx=1") is None


def describe_number():
    def test_should_read_a_lua_number():
        assert mouse_stats.number("-1.5000") == -1.5

    def test_should_treat_lua_nil_as_missing():
        assert mouse_stats.number("nil") is None

    def test_should_treat_an_unknown_value_as_missing():
        assert mouse_stats.number("?") is None

    def test_should_treat_a_missing_field_as_missing():
        assert mouse_stats.number(None) is None

    def test_should_treat_nan_as_missing():
        assert mouse_stats.number("nan") is None

    def test_should_treat_infinity_as_missing():
        assert mouse_stats.number("inf") is None


def describe_pair():
    def test_should_read_two_whole_numbers():
        assert mouse_stats.pair("4,-1") == (4, -1)

    def test_should_give_zeros_for_a_missing_field():
        assert mouse_stats.pair(None) == (0, 0)

    def test_should_give_zeros_for_three_numbers():
        assert mouse_stats.pair("1,2,3") == (0, 0)

    def test_should_give_zeros_for_values_that_are_not_numbers():
        assert mouse_stats.pair("a,b") == (0, 0)


def describe_poll_px():
    def test_should_add_cursor_and_injected_pixels():
        record = mouse_stats.parse_line(poll(1, dx=-3, dy=2, inj=(4, -1)))

        assert mouse_stats.poll_px(record) == (7, 3)

    def test_should_treat_an_empty_movement_as_zero():
        record = mouse_stats.parse_line("P t=1 dx= dy=2")

        assert mouse_stats.poll_px(record) == (0, 2)


def describe_find_segments():
    def test_should_make_one_segment_per_inject_note():
        records = mouse_stats.read_log([
            inject(1000, 100, 0, 200),
            poll(2000, inj=(10, 0)),
            inject(5_000_000, -50, 5, 1000),
        ])

        segments = mouse_stats.find_segments(records)

        assert [(s.start, s.end) for s in segments] == [(1000, 201_000), (5_000_000, 6_000_000)]

    def test_should_name_a_segment_after_its_inject_run():
        records = mouse_stats.read_log([inject(5_000_000, -50, 5, 1000)])

        segments = mouse_stats.find_segments(records)

        assert segments[0].label == "inject -50,5 px over 1000 ms"

    def test_should_use_the_whole_log_when_there_are_no_inject_runs():
        records = mouse_stats.read_log([poll(20), poll(90)])

        segments = mouse_stats.find_segments(records)

        assert [(s.label, s.start, s.end) for s in segments] == [("whole log", 20, 90)]

    def test_should_use_the_whole_log_when_asked():
        records = mouse_stats.read_log([inject(10, 1, 0, 1), poll(20), poll(90)])

        segments = mouse_stats.find_segments(records, whole=True)

        assert [(s.label, s.start, s.end) for s in segments] == [("whole log", 10, 90)]

    def test_should_give_no_segments_without_records():
        assert mouse_stats.find_segments([]) == []

    def test_should_name_the_speed_profile_and_the_mouse_mode():
        # An even profile is the default, so it isn't named.
        records = mouse_stats.read_log([
            "N t=10 inject dx=100 dy=0 ms=200 profile=even",
            "N t=1000000 mode direct",
            "N t=2000000 inject dx=100 dy=0 ms=100 profile=flick",
            "N t=3000000 mode stick",
            "N t=4000000 inject dx=100 dy=0 ms=100 profile=ramp",
        ])

        segments = mouse_stats.find_segments(records)

        assert [s.label for s in segments] == ["inject 100,0 px over 200 ms",
                                               "inject 100,0 px over 100 ms (flick) [direct]",
                                               "inject 100,0 px over 100 ms (ramp) [stick]"]

    def test_should_keep_the_mouse_mode_for_every_later_run():
        records = mouse_stats.read_log(["N t=10 mode direct", inject(1000, 10, 0, 100), inject(2_000_000, 20, 0, 100)])

        segments = mouse_stats.find_segments(records)

        assert [s.label for s in segments] == ["inject 10,0 px over 100 ms [direct]",
                                               "inject 20,0 px over 100 ms [direct]"]

    def test_should_not_take_a_route_note_as_the_mouse_mode():
        # The input driver writes "route direct" or "route stick" whenever the route changes.
        records = mouse_stats.read_log(["N t=10 mode stick", "N t=20 route direct", inject(1000, 10, 0, 100)])

        segments = mouse_stats.find_segments(records)

        assert segments[0].label == "inject 10,0 px over 100 ms [stick]"

    def test_should_ignore_a_mode_note_without_a_mode():
        records = mouse_stats.read_log(["N t=10 mode", inject(1000, 10, 0, 100)])

        segments = mouse_stats.find_segments(records)

        assert segments[0].label == "inject 10,0 px over 100 ms"

    def test_should_end_a_run_at_its_inject_stopped_note():
        records = mouse_stats.read_log([
            inject(0, 2400, 0, 4000),
            poll(100_000, inj=(60, 0)),
            "N t=1000000 inject stopped",
        ])

        segments = mouse_stats.find_segments(records)

        assert [(s.start, s.end) for s in segments] == [(0, 1_000_000)]


def describe_analyse():
    def test_should_count_input_polls_and_camera_updates():
        d = analyse_first(two_polls_per_update())

        assert (d["polls"], d["updates"], d["polls_per_update"]) == (20, 10, 2.0)

    def test_should_add_up_the_movement():
        d = analyse_first(two_polls_per_update())

        assert d["px"] == [200, 0]

    def test_should_see_half_the_movement_with_two_polls_per_update():
        d = analyse_first(two_polls_per_update())

        assert d["visible_share"] == 0.5

    def test_should_see_all_the_movement_when_the_read_poll_carries_its_smoothing_window():
        # Each poll reports the movement of the last 100 ms (w=), so the poll the camera
        # reads carries all of it.
        d = analyse_first(two_polls_per_update(sm=1, w=(20, 0)))

        assert d["visible_share"] == 1.0

    def test_should_see_nothing_when_a_smoothed_poll_has_no_window():
        lines = [inject(0, 10, 0, 1000), update(1_000, -1.0), "P t=10000 dx=0 dy=0 inj=10,0 sm=1"]

        d = analyse_first(lines)

        assert d["visible_share"] == 0.0

    def test_should_list_the_tick_rates_seen():
        d = analyse_first(two_polls_per_update())

        assert d["tick_rates"] == [30.0]

    def test_should_skip_an_unknown_tick_rate():
        d = analyse_first([inject(0, 10, 0, 1000), update(500_000, -1.0, tick="?")])

        assert d["tick_rates"] == []

    def test_should_leave_the_ends_of_the_run_out_of_the_steady_yaw():
        yaw = analyse_first(alternating_yaw_run())["yaw_per_update"]

        assert yaw["n"] == 14

    def test_should_score_how_much_the_steady_yaw_varies():
        yaw = analyse_first(alternating_yaw_run())["yaw_per_update"]

        assert (yaw["mean"], yaw["sd"], yaw["cv"], yaw["zero_share"]) == (-2.0, 1.0, 0.5, 0.0)

    def test_should_count_updates_without_yaw_as_zero():
        yaw = analyse_first([inject(0, 100, 0, 1000), update(500_000, "nil"), update(510_000, -2.0)])["yaw_per_update"]

        assert (yaw["n"], yaw["zero_share"]) == (2, 0.5)

    def test_should_give_no_stutter_score_when_the_camera_never_turned():
        yaw = analyse_first([inject(0, 100, 0, 1000), update(500_000, 0.0), update(510_000, 0.0)])["yaw_per_update"]

        assert (yaw["cv"], yaw["zero_share"]) == (None, 1.0)

    def test_should_count_only_updates_inside_the_run():
        lines = [inject(0, 100, 0, 100),
                 update(50_000, -1.0, srr=-0.4), update(300_000, 0.0, srr=-0.6), update(900_000, 0.0, srr=-9.0)]

        d = analyse_first(lines)

        assert d["updates"] == 1

    def test_should_add_the_smoothing_tail_to_the_yaw_total():
        # -0.4 inside the run and -0.6 in the 500 ms tail; 900 ms is past the tail.
        lines = [inject(0, 100, 0, 100),
                 update(50_000, -1.0, srr=-0.4), update(300_000, 0.0, srr=-0.6), update(900_000, 0.0, srr=-9.0)]

        d = analyse_first(lines)

        assert d["yaw_total"] == -1.0

    def test_should_count_clamped_polls_only_among_moving_polls():
        lines = [inject(0, 100, 0, 1000),
                 poll(10, inj=(50, 0), clamp=1), poll(20, inj=(5, 0)), poll(30), poll(40, clamp=1)]

        d = analyse_first(lines)

        assert d["clamped_share"] == 0.5

    def test_should_not_count_polls_from_a_later_run():
        lines = [inject(0, 5, 0, 100), poll(50_000, inj=(5, 0)),
                 inject(1_000_000, 7, 0, 100), poll(1_050_000, inj=(7, 0))]

        d = analyse_first(lines)

        assert (d["polls"], d["px"]) == (1, [5, 0])

    def test_should_not_count_polls_from_before_the_run():
        d = analyse_first([poll(500, inj=(9, 0)), inject(1000, 5, 0, 100), poll(50_000, inj=(5, 0))])

        assert (d["polls"], d["px"]) == (1, [5, 0])

    def test_should_add_the_direct_mouse_turn_to_the_stick_turn():
        lines = [inject(0, 100, 0, 200), direct_update(50_000, -3.0), direct_update(120_000, -2.0, srr=-0.5)]

        turn = analyse_first(lines)["turn"]

        assert turn["total"] == -5.5

    def test_should_report_the_direct_mouse_turn_on_its_own():
        lines = [inject(0, 100, 0, 200), direct_update(50_000, -3.0), direct_update(120_000, -2.0, srr=-0.5)]

        turn = analyse_first(lines)["turn"]

        assert turn["mouse"] == -5.0

    def test_should_find_no_coast_when_the_turn_stops_with_the_input():
        lines = [inject(0, 100, 0, 200), direct_update(50_000, -3.0), direct_update(120_000, -2.0, srr=-0.5)]

        turn = analyse_first(lines)["turn"]

        assert (turn["tail_updates"], turn["tail_share"]) == (0, 0.0)

    def test_should_add_the_coast_after_the_input_stops_to_the_turn_total():
        turn = analyse_first(coasting_stick_run())["turn"]

        assert turn["total"] == -14.0

    def test_should_count_the_updates_that_coast_after_the_input_stops():
        turn = analyse_first(coasting_stick_run())["turn"]

        assert turn["tail_updates"] == 3

    def test_should_report_the_share_of_the_turn_that_coasted():
        turn = analyse_first(coasting_stick_run())["turn"]

        assert turn["tail_share"] == pytest.approx(5.7 / 14.0, abs=5e-4)

    def test_should_compare_the_first_turn_with_the_steady_turn():
        turn = analyse_first(ramping_direct_run())["turn"]

        assert turn["first_share"] == 0.5

    def test_should_give_no_coast_share_when_the_camera_never_turned():
        turn = analyse_first([inject(0, 100, 0, 200), direct_update(50_000, 0.0)])["turn"]

        assert turn["tail_share"] is None

    def test_should_give_no_first_share_when_the_camera_never_turned():
        turn = analyse_first([inject(0, 100, 0, 200), direct_update(50_000, 0.0)])["turn"]

        assert turn["first_share"] is None

    def test_should_give_no_first_share_when_the_steady_turn_is_zero():
        # The first update turns, then the steady middle (150-850 ms) doesn't.
        lines = [inject(0, 100, 0, 1000), direct_update(50_000, -1.0), direct_update(500_000, 0.0)]

        turn = analyse_first(lines)["turn"]

        assert turn["first_share"] is None

    def test_should_not_take_a_turn_below_the_threshold_as_the_first_turn():
        # -0.00005 degrees is below TURN_EPS; the first real turn is the -2 at 200 ms.
        lines = [inject(0, 100, 0, 1000), direct_update(50_000, -0.00005),
                 direct_update(200_000, -2.0), direct_update(300_000, -2.0)]

        turn = analyse_first(lines)["turn"]

        assert turn["first_share"] == 1.0

    def test_should_not_count_a_turn_below_the_threshold_as_coasting():
        lines = [inject(0, 100, 0, 200), direct_update(100_000, -2.0), direct_update(300_000, -0.00005)]

        turn = analyse_first(lines)["turn"]

        assert turn["tail_updates"] == 0


def describe_format_stats():
    def test_should_report_how_much_movement_the_game_saw():
        text = mouse_stats.format_stats(analyse_first(two_polls_per_update()))

        assert "  visible to the game: 0.5 of x movement" in text.splitlines()

    def test_should_report_the_steady_yaw_per_update():
        text = mouse_stats.format_stats(analyse_first(two_polls_per_update()))

        assert "  yaw per update (steady, n=7): mean -2.0 sd 0.0 cv 0.0 zero 0.0" in text.splitlines()

    def test_should_report_the_yaw_total_and_tick_rates():
        text = mouse_stats.format_stats(analyse_first(two_polls_per_update()))

        assert "  yaw total -20.0  tick rates [30.0]" in text.splitlines()

    def test_should_report_the_turn_with_the_direct_mouse_part():
        text = mouse_stats.format_stats(analyse_first([inject(0, 10, 0, 100), direct_update(50_000, -1.2)]))

        assert "turn total -1.2 (mouse -1.2)" in text

    def test_should_mark_the_turn_shares_as_not_available_when_the_camera_never_turned():
        text = mouse_stats.format_stats(analyse_first([inject(0, 10, 0, 100), direct_update(50_000, 0.0)]))

        assert ("  turn total 0.0 (mouse 0.0); after the input stopped n/a in 0 updates; "
                "first update n/a of steady") in text.splitlines()

    def test_should_mark_a_missing_number_as_not_available():
        text = mouse_stats.format_stats(analyse_first([inject(0, 10, 0, 100), poll(10, inj=(10, 0))]))

        assert "polls/update n/a" in text

    def test_should_explain_how_to_get_camera_updates_when_there_are_none():
        text = mouse_stats.format_stats(analyse_first([inject(0, 10, 0, 100), poll(10, inj=(10, 0))]))

        assert "f2 mouse trace on" in text


def describe_main():
    def test_should_name_each_inject_run_in_the_text_report(run_main):
        code, out, _ = run_main([inject(0, 10, 0, 100), poll(10, inj=(10, 0))])

        assert code == 0
        assert out.startswith("inject 10,0 px over 100 ms")

    def test_should_print_one_json_entry_per_inject_run(run_main):
        lines = [inject(0, 10, 0, 100), update(50, -1.0), inject(1_000_000, 20, 0, 100), update(1_000_050, -2.0)]

        code, out, _ = run_main(lines, "--json")

        assert code == 0
        assert [d["label"] for d in json.loads(out)] == ["inject 10,0 px over 100 ms", "inject 20,0 px over 100 ms"]

    def test_should_merge_the_runs_into_one_when_asked_for_the_whole_log(run_main):
        lines = [inject(0, 10, 0, 100), update(50, -1.0), inject(1_000_000, 20, 0, 100), update(1_000_050, -2.0)]

        _, out, _ = run_main(lines, "--json", "--whole")

        assert [d["label"] for d in json.loads(out)] == ["whole log"]

    def test_should_sort_lines_written_out_of_order(run_main):
        # The writer thread and the Lua trace append from different threads.
        lines = two_polls_per_update()

        _, in_order, _ = run_main(lines, "--json")
        _, reversed_order, _ = run_main(lines[::-1], "--json")

        assert reversed_order == in_order

    def test_should_fail_on_a_log_without_records(run_main):
        code, _, err = run_main(["# only a header"])

        assert code == 1
        assert "no records in the log" in err

    def test_should_fail_on_a_missing_log(tmp_path, capsys):
        code = mouse_stats.main([str(tmp_path / "missing" / "fable2_mouse.log")])

        assert code == 1
        assert "cannot read" in capsys.readouterr().err
