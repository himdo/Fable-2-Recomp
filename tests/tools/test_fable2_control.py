"""Check how fable2_control.py builds its state command, without a running game."""
import argparse
import contextlib
import importlib.util
import io
from pathlib import Path
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("control", ROOT / "tools/fable2_control.py")
control = importlib.util.module_from_spec(spec)
spec.loader.exec_module(control)

BASE_STATE = {"cmd": "state"}


def make_args(buttons=None, lt=None, rt=None, lx=None, ly=None, rx=None, ry=None):
    return argparse.Namespace(buttons=buttons, lt=lt, rt=rt, lx=lx, ly=ly, rx=rx, ry=ry)


class Fable2ControlTests(unittest.TestCase):
    def test_no_options_build_state(self):
        args = make_args()
        expected = BASE_STATE
        response = control.build_state(args)

        self.assertEqual(response, expected)

    def test_button_build_state(self):
        args = make_args(buttons="A,,B")
        expected = {**BASE_STATE, "buttons": ["A", "B"]}
        response = control.build_state(args)

        self.assertEqual(response, expected)

    def test_empty_buttons_build_state(self):
        args = make_args(buttons="")
        expected = BASE_STATE
        response = control.build_state(args)

        self.assertEqual(response, expected)

    def test_only_commas_build_state(self):
        args = make_args(buttons=",")
        expected = {**BASE_STATE, "buttons": []}
        response = control.build_state(args)

        self.assertEqual(response, expected)

    def test_lt_trigger_build_state(self):
        args = make_args(lt=255)
        expected = {**BASE_STATE, "triggers": {"LT": 255}}
        response = control.build_state(args)

        self.assertEqual(response, expected)

    def test_rt_trigger_build_state(self):
        args = make_args(rt=128)
        expected = {**BASE_STATE, "triggers": {"RT": 128}}
        response = control.build_state(args)

        self.assertEqual(response, expected)

    def test_both_triggers_build_state(self):
        args = make_args(lt=255, rt=128)
        expected = {**BASE_STATE, "triggers": {"LT": 255, "RT": 128}}
        response = control.build_state(args)

        self.assertEqual(response, expected)

    def test_zero_lt_trigger_build_state(self):
        # 0 is a real value (trigger released), so it must not be dropped like a missing option.
        args = make_args(lt=0)
        expected = {**BASE_STATE, "triggers": {"LT": 0}}
        response = control.build_state(args)

        self.assertEqual(response, expected)

    def test_zero_rt_trigger_build_state(self):
        args = make_args(rt=0)
        expected = {**BASE_STATE, "triggers": {"RT": 0}}
        response = control.build_state(args)

        self.assertEqual(response, expected)

    def test_left_stick_build_state(self):
        args = make_args(lx=-100, ly=200)
        expected = {**BASE_STATE, "stk": {"lx": -100, "ly": 200}}
        response = control.build_state(args)

        self.assertEqual(response, expected)

    def test_right_stick_build_state(self):
        args = make_args(rx=300, ry=-400)
        expected = {**BASE_STATE, "stk": {"rx": 300, "ry": -400}}
        response = control.build_state(args)

        self.assertEqual(response, expected)

    def test_ly_only_build_state(self):
        args = make_args(ly=200)
        expected = {**BASE_STATE, "stk": {"ly": 200}}
        response = control.build_state(args)

        self.assertEqual(response, expected)

    def test_rx_only_build_state(self):
        args = make_args(rx=300)
        expected = {**BASE_STATE, "stk": {"rx": 300}}
        response = control.build_state(args)

        self.assertEqual(response, expected)

    def test_ry_only_build_state(self):
        args = make_args(ry=-400)
        expected = {**BASE_STATE, "stk": {"ry": -400}}
        response = control.build_state(args)

        self.assertEqual(response, expected)

    def test_zero_stick_build_state(self):
        # 0 is a real value (stick centered), so it must not be dropped like a missing option.
        args = make_args(lx=0)
        expected = {**BASE_STATE, "stk": {"lx": 0}}
        response = control.build_state(args)

        self.assertEqual(response, expected)

    def test_all_options_build_state(self):
        args = make_args(buttons="A,B", lt=255, rt=128, lx=-100, ly=200, rx=300, ry=-400)
        expected = {
            **BASE_STATE,
            "buttons": ["A", "B"],
            "triggers": {"LT": 255, "RT": 128},
            "stk": {"lx": -100, "ly": 200, "rx": 300, "ry": -400},
        }
        response = control.build_state(args)

        self.assertEqual(response, expected)

    def test_state_command_sends_the_parsed_options(self):
        argv = ["fable2_control.py", "state", "--buttons", "A,B", "--lt", "0", "--ry", "-5"]
        expected = {**BASE_STATE, "buttons": ["A", "B"], "triggers": {"LT": 0}, "stk": {"ry": -5}}
        with patch("sys.argv", argv), patch.object(control, "run", return_value=0) as run:
            control.main()

        run.assert_called_once_with("127.0.0.1", 8791, "", expected)

    def test_state_command_rejects_a_non_integer_value(self):
        argv = ["fable2_control.py", "state", "--lt", "abc"]
        with (
            patch("sys.argv", argv),
            contextlib.redirect_stderr(io.StringIO()),
            self.assertRaises(SystemExit) as caught,
        ):
            control.main()

        self.assertEqual(caught.exception.code, 2)


if __name__ == "__main__":
    unittest.main()
