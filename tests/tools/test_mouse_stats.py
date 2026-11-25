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


def aim_poll(t, cur, atk=0, aim=1, inj=(0, 0)):
    """A P line from the aim stick swap: the mouse drove the left stick, cur= is the reticle."""
    return poll(t, inj=inj) + f" dir=0 aim={aim} lx=0 ly=0 cur={cur[0]},{cur[1]} atk={atk}"


def pad_stick(t, lx, ly, frames):
    """The note `f2 pad stick` writes."""
    return f"N t={t} padstick lx={lx} ly={ly} frames={frames}"


def pad_released(t):
    """The note the console writes when a `pad stick` is let go."""
    return f"N t={t} padstick released"


def aim_run():
    """A one-second inject run while aiming, with the game updating the cursor at 15 Hz.

    The reticle goes from 0.1,0 to 0.5,0.2, eases back a little in the tail, then springs
    back to the middle by 2 s after.
    """
    return [aim_poll(-10_000, (0.1, 0.0), atk=99, aim=0),  # before the run
            inject(0, 200, 0, 1000),
            aim_poll(200_000, (0.2, 0.05), atk=102, inj=(10, 0)),
            aim_poll(600_000, (0.4, 0.1), atk=108, inj=(10, 0)),
            aim_poll(1_000_000, (0.5, 0.2), atk=114),
            aim_poll(1_300_000, (0.45, 0.2), atk=118, aim=0),  # inside the 500 ms tail
            aim_poll(2_000_000, (0.0, 0.0), atk=129, aim=0),   # settled, within 2 s
            aim_poll(3_500_000, (0.3, 0.3), atk=151, aim=0)]   # too late to count


def describe_reticle():
    def test_should_measure_the_move_from_the_last_poll_before_the_run_to_its_end():
        aim = analyse_first(aim_run())["reticle"]

        assert aim["moved"] == [0.4, 0.2]

    def test_should_count_the_games_cursor_updates_during_the_run():
        aim = analyse_first(aim_run())["reticle"]

        assert aim["updates"] == 15

    def test_should_measure_from_the_first_poll_in_the_run_when_none_came_before():
        aim = analyse_first(aim_run()[1:])["reticle"]

        assert aim["moved"] == [0.3, 0.15]

    def test_should_measure_the_drift_after_the_input_stopped_within_the_tail():
        aim = analyse_first(aim_run())["reticle"]

        assert (aim["after_stop"], aim["after_stop_updates"]) == ([-0.05, 0.0], 4)

    def test_should_measure_where_the_reticle_settled_within_two_seconds():
        aim = analyse_first(aim_run())["reticle"]

        assert (aim["settled"], aim["settled_updates"]) == ([-0.5, -0.2], 15)

    def test_should_not_count_a_frozen_reticle_as_staying_put():
        # cur= only changes while the game aims: with no update after the run, the
        # unchanged value says nothing.
        lines = aim_run()[:5] + [aim_poll(1_300_000, (0.5, 0.2), atk=114, aim=0),
                                 aim_poll(2_000_000, (0.5, 0.2), atk=114, aim=0)]

        aim = analyse_first(lines)["reticle"]

        assert (aim["after_stop"], aim["settled"]) == (None, None)

    def test_should_stop_following_the_reticle_at_the_next_run():
        lines = aim_run()[:6] + [inject(1_500_000, 100, 0, 500), aim_poll(2_000_000, (0.0, 0.0), atk=129)]

        aim = analyse_first(lines)["reticle"]

        assert aim["settled"] == [-0.05, 0.0]  # the tail reading, not the next run's poll

    def test_should_report_where_the_reticle_ended():
        aim = analyse_first(aim_run())["reticle"]

        assert aim["end"] == [0.5, 0.2]

    def test_should_report_the_share_of_polls_that_aimed():
        aim = analyse_first(aim_run())["reticle"]

        assert aim["aim_share"] == 1.0

    def test_should_measure_the_cursor_rate_from_the_readings_own_times():
        aim = analyse_first(aim_run())["reticle"]

        assert aim["ticks_per_s"] == 15.0

    def test_should_skip_a_reticle_value_that_is_not_a_number():
        lines = aim_run()
        lines.insert(4, aim_poll(800_000, ("nan", "0.1"), atk=111))

        aim = analyse_first(lines)["reticle"]

        assert aim["moved"] == [0.4, 0.2]

    def test_should_report_a_run_where_only_the_game_moved_the_reticle():
        # A `pad stick` run while aiming: no aim polls, but the cursor updated.
        lines = [aim_poll(-10_000, (0.0, 0.0), atk=10, aim=0), pad_stick(0, 6000, 0, 30),
                 aim_poll(500_000, (0.06, 0.0), atk=17, aim=0), pad_released(1_000_000)]

        aim = analyse_first(lines)["reticle"]

        assert (aim["moved"], aim["updates"], aim["aim_share"]) == ([0.06, 0.0], 7, 0.0)

    def test_should_report_no_reticle_when_neither_the_mouse_nor_the_game_moved_it():
        lines = [inject(0, 100, 0, 1000), aim_poll(100_000, (0.2, 0.0), atk=5, aim=0),
                 aim_poll(900_000, (0.2, 0.0), atk=5, aim=0)]

        assert analyse_first(lines)["reticle"] is None

    def test_should_report_no_reticle_for_logs_from_before_the_aim_fields():
        assert analyse_first(two_polls_per_update())["reticle"] is None

    def test_should_print_the_reticle_line_for_an_aiming_run():
        text = mouse_stats.format_stats(analyse_first(aim_run()))

        assert ("  reticle moved 0.4,0.2 in 15 updates; after stop -0.05,0.0 in 4; "
                "settled -0.5,-0.2 in 15; at 0.5,0.2; aim polls 1.0; cursor updates 15.0/s"
                ) in text.splitlines()

    def test_should_print_why_there_is_no_drift_when_aim_had_ended():
        lines = aim_run()[:5]

        text = mouse_stats.format_stats(analyse_first(lines))

        assert "after stop n/a (no cursor updates: aim ended)" in text
        assert "settled n/a (no cursor updates: aim ended)" in text

    def test_should_not_print_a_reticle_line_for_a_run_that_did_not_aim():
        text = mouse_stats.format_stats(analyse_first(two_polls_per_update()))

        assert "reticle" not in text


def describe_pad_stick_runs():
    def test_should_start_a_run_at_the_note():
        records = records_of([pad_stick(1_000, 6000, 0, 30), pad_released(501_000)])

        segments = mouse_stats.find_segments(records)

        assert [(s.start, s.end) for s in segments] == [(1_000, 501_000)]

    def test_should_name_the_run_after_the_stick_and_frames():
        records = records_of([pad_stick(1_000, 6000, -200, 30), pad_released(501_000)])

        segments = mouse_stats.find_segments(records)

        assert segments[0].label == "pad stick 6000,-200 for 30 frames"

    def test_should_estimate_the_end_from_the_frames_without_a_release_note():
        records = records_of([pad_stick(0, 3000, 0, 30)])

        segments = mouse_stats.find_segments(records)

        assert segments[0].end == 30 * mouse_stats.PAD_FRAME_US

    def test_should_sit_alongside_inject_runs_in_time_order():
        records = records_of([pad_stick(0, 3000, 0, 30), pad_released(500_000),
                              inject(1_000_000, 100, 0, 500)])

        segments = mouse_stats.find_segments(records)

        assert [s.label for s in segments] == ["pad stick 3000,0 for 30 frames",
                                               "inject 100,0 px over 500 ms"]

    def test_should_end_where_the_next_run_begins():
        records = records_of([pad_stick(0, 3000, 0, 30), pad_released(500_000),
                              inject(1_000_000, 100, 0, 500)])

        segments = mouse_stats.find_segments(records)

        assert (segments[0].until, segments[1].until) == (1_000_000, None)

    def test_should_carry_the_mode_tags():
        records = records_of(["N t=1 mode direct", "N t=2 aim detect", pad_stick(10, 3000, 0, 30)])

        segments = mouse_stats.find_segments(records)

        assert segments[0].label == "pad stick 3000,0 for 30 frames [direct, aim detect]"

    def test_should_ignore_a_release_note_with_no_stick_held():
        records = records_of([inject(0, 100, 0, 500), pad_released(100_000)])

        segments = mouse_stats.find_segments(records)

        assert segments[0].end == 500_000


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
