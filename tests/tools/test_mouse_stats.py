"""Check the fable2_mouse.log summariser with made-up logs, without a running game."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("mouse_stats", ROOT / "tools/mouse_stats.py")
mouse_stats = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mouse_stats)


def poll(t, dx=0, dy=0, inj=(0, 0), clamp=0, sm=0, w=(0, 0)):
    return (f"P t={t} tid=1 gap=0 dx={dx} dy={dy} inj={inj[0]},{inj[1]} rx=0 ry=0 clamp={clamp} "
            f"look=1 sm={sm} w={w[0]},{w[1]}")


def update(t, rr, srr=None, tick=30):
    srr = rr if srr is None else srr
    return f"U t={t} f=1 tick={tick} act=2:0.500/0.000 rr={rr} srr={srr} mho=0.0000>0.0000 cmho=0.0000 man=1"


def run(lines, *args):
    """Write `lines` to a temp log, run main() and return (exit code, stdout)."""
    with tempfile.TemporaryDirectory(prefix="fable2-mouse-stats-") as tmp:
        log = Path(tmp) / "fable2_mouse.log"
        log.write_text("\n".join(lines) + "\n", encoding="utf-8")
        output = io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(io.StringIO()):
            code = mouse_stats.main([str(log), *args])
    return code, output.getvalue()


class ParseTests(unittest.TestCase):
    def test_reads_fields_and_free_text(self):
        record = mouse_stats.parse_line("N t=42 inject dx=10 dy=-3 ms=500")
        self.assertEqual(record.kind, "N")
        self.assertEqual(record.t, 42)
        self.assertEqual(record.text, "inject")
        self.assertEqual(record.fields["dx"], "10")
        self.assertEqual(record.fields["ms"], "500")

    def test_skips_comments_blank_lines_and_lines_without_a_time(self):
        self.assertIsNone(mouse_stats.parse_line("# fable2_mouse.log v1"))
        self.assertIsNone(mouse_stats.parse_line("   "))
        self.assertIsNone(mouse_stats.parse_line("P dx=1"))
        self.assertIsNone(mouse_stats.parse_line("P t=abc dx=1"))

    def test_numbers_treat_lua_nil_and_unknown_as_missing(self):
        self.assertEqual(mouse_stats.number("-1.5000"), -1.5)
        self.assertIsNone(mouse_stats.number("nil"))
        self.assertIsNone(mouse_stats.number("?"))
        self.assertIsNone(mouse_stats.number(None))
        self.assertIsNone(mouse_stats.number("nan"))

    def test_poll_movement_adds_cursor_and_injected_pixels(self):
        record = mouse_stats.parse_line(poll(1, dx=-3, dy=2, inj=(4, -1)))
        self.assertEqual(mouse_stats.poll_px(record), (7, 3))


class SegmentTests(unittest.TestCase):
    def test_each_inject_note_is_its_own_segment(self):
        records = mouse_stats.read_log([
            "N t=1000 inject dx=100 dy=0 ms=200",
            poll(2000, inj=(10, 0)),
            "N t=5000000 inject dx=-50 dy=5 ms=1000",
        ])
        segments = mouse_stats.find_segments(records)
        self.assertEqual([(s.start, s.end) for s in segments], [(1000, 201000), (5000000, 6000000)])
        self.assertEqual(segments[1].label, "inject -50,5 px over 1000 ms")

    def test_whole_log_when_there_are_no_inject_runs_or_when_asked(self):
        records = mouse_stats.read_log([
            "N t=10 inject dx=1 dy=0 ms=1",
            poll(20),
            poll(90),
        ])
        whole = mouse_stats.find_segments(records, whole=True)
        self.assertEqual([(s.label, s.start, s.end) for s in whole], [("whole log", 10, 90)])
        plain = mouse_stats.find_segments(records[1:])
        self.assertEqual([(s.start, s.end) for s in plain], [(20, 90)])

    def test_no_records_gives_no_segments(self):
        self.assertEqual(mouse_stats.find_segments([]), [])


class AnalyseTests(unittest.TestCase):
    def analyse(self, lines):
        records = sorted(mouse_stats.read_log(lines), key=lambda r: r.t)
        segment = mouse_stats.find_segments(records)[0]
        return mouse_stats.analyse(records, segment).as_dict()

    def test_two_polls_per_update_means_half_the_movement_is_visible(self):
        # 10 px per poll, two polls per camera update: the camera reads only
        # the first poll after its previous update.
        lines = ["N t=0 inject dx=200 dy=0 ms=1000"]
        for i in range(10):
            base = i * 100_000
            lines += [update(base + 1_000, -2.0), poll(base + 10_000, inj=(10, 0)),
                      poll(base + 60_000, inj=(10, 0))]
        d = self.analyse(lines)
        self.assertEqual(d["polls"], 20)
        self.assertEqual(d["updates"], 10)
        self.assertEqual(d["polls_per_update"], 2.0)
        self.assertEqual(d["px"], [200, 0])
        self.assertEqual(d["visible_share"], 0.5)
        self.assertEqual(d["tick_rates"], [30.0])

    def test_with_smoothing_the_read_poll_carries_its_whole_window(self):
        # Same movement, but each poll reports the movement of the last 100 ms
        # (w=), so the poll the camera reads carries all of it.
        lines = ["N t=0 inject dx=200 dy=0 ms=1000"]
        for i in range(10):
            base = i * 100_000
            lines += [update(base + 1_000, -2.0), poll(base + 10_000, inj=(10, 0), sm=1, w=(20, 0)),
                      poll(base + 60_000, inj=(10, 0), sm=1, w=(20, 0))]
        d = self.analyse(lines)
        self.assertEqual(d["visible_share"], 1.0)

    def test_steady_yaw_skips_the_ends_and_scores_the_variation(self):
        # Updates every 50 ms over one second; the steady middle (150-850 ms)
        # holds seven -1s and seven -3s, so mean -2, sd 1, cv 0.5. The ends
        # would skew it.
        lines = ["N t=0 inject dx=100 dy=0 ms=1000"]
        for i in range(1, 20):
            t = i * 50_000 + 1_000
            if t < 150_000 or t > 850_000:
                rr = -100.0
            else:
                rr = -1.0 if i % 2 else -3.0
            lines.append(update(t, rr))
        yaw = self.analyse(lines)["yaw_per_update"]
        self.assertEqual(yaw["mean"], -2.0)
        self.assertAlmostEqual(yaw["sd"], 1.0, places=3)
        self.assertAlmostEqual(yaw["cv"], 0.5, places=3)
        self.assertEqual(yaw["zero_share"], 0.0)

    def test_updates_without_yaw_count_as_zero(self):
        lines = ["N t=0 inject dx=100 dy=0 ms=1000"]
        lines += [update(500_000, "nil"), update(510_000, -2.0)]
        yaw = self.analyse(lines)["yaw_per_update"]
        self.assertEqual(yaw["n"], 2)
        self.assertEqual(yaw["zero_share"], 0.5)

    def test_yaw_total_includes_the_smoothing_tail_but_rates_do_not(self):
        lines = ["N t=0 inject dx=100 dy=0 ms=100"]
        lines += [update(50_000, -1.0, srr=-0.4), update(300_000, 0.0, srr=-0.6),
                  update(900_000, 0.0, srr=-9.0)]
        d = self.analyse(lines)
        self.assertEqual(d["updates"], 1)  # only the one inside the run
        self.assertEqual(d["yaw_total"], -1.0)  # -0.4 inside + -0.6 in the tail; 900 ms is past it

    def test_clamped_share_counts_only_moving_polls(self):
        lines = ["N t=0 inject dx=100 dy=0 ms=1000",
                 poll(10, inj=(50, 0), clamp=1), poll(20, inj=(5, 0)), poll(30), poll(40, clamp=1)]
        d = self.analyse(lines)
        self.assertEqual(d["clamped_share"], 0.5)


class MainTests(unittest.TestCase):
    def test_text_report_explains_missing_camera_updates(self):
        code, out = run(["N t=0 inject dx=10 dy=0 ms=100", poll(10, inj=(10, 0))])
        self.assertEqual(code, 0)
        self.assertIn("inject 10,0 px over 100 ms", out)
        self.assertIn("f2 mouse trace on", out)

    def test_json_report_has_one_entry_per_run(self):
        code, out = run(["N t=0 inject dx=10 dy=0 ms=100", update(50, -1.0),
                         "N t=1000000 inject dx=20 dy=0 ms=100", update(1000050, -2.0)], "--json")
        self.assertEqual(code, 0)
        data = json.loads(out)
        self.assertEqual([d["label"] for d in data],
                         ["inject 10,0 px over 100 ms", "inject 20,0 px over 100 ms"])

    def test_missing_or_empty_log_fails(self):
        code, _ = run(["# only a header"])
        self.assertEqual(code, 1)
        output = io.StringIO()
        with contextlib.redirect_stderr(output):
            code = mouse_stats.main([str(ROOT / "no-such-dir" / "fable2_mouse.log")])
        self.assertEqual(code, 1)
        self.assertIn("cannot read", output.getvalue())


if __name__ == "__main__":
    unittest.main()
