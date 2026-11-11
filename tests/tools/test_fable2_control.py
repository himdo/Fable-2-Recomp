"""Check the requests fable2_control.py sends and its server connection, without a running game."""
import argparse
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import socket
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("control", ROOT / "tools/fable2_control.py")
control = importlib.util.module_from_spec(spec)
spec.loader.exec_module(control)

BASE_STATE = {"cmd": "state"}


def make_args(buttons=None, lt=None, rt=None, lx=None, ly=None, rx=None, ry=None):
    return argparse.Namespace(buttons=buttons, lt=lt, rt=rt, lx=lx, ly=ly, rx=rx, ry=ry)


def request_for(*command):
    """Run the tool with these arguments and return the request it would send."""
    argv = ["fable2_control.py", *command]
    with patch("sys.argv", argv), patch.object(control, "run", return_value=0) as run:
        control.main()
    return run.call_args.args[3]


def script_request_for(contents):
    """Save contents as a script file and return the request the script command sends."""
    with tempfile.TemporaryDirectory(prefix="fable2-script-test-") as directory:
        script = Path(directory) / "steps.json"
        script.write_text(json.dumps(contents), encoding="utf-8")
        return request_for("script", "--file", str(script))


class FakeSocket:
    """Stands in for the server: records each request and answers from a list of replies."""

    def __init__(self, *replies):
        self.replies = list(replies)
        self.sent = []

    def sendall(self, data):
        self.sent.append(json.loads(data))

    def recv(self, size):
        # An empty reply is how a real socket says the server closed the connection.
        if not self.replies:
            return b""
        return self.replies.pop(0)

    def __enter__(self):
        return self

    def __exit__(self, *error):
        return False


class SilentSocket(FakeSocket):
    """Stands in for a server that takes the request but never answers in time."""

    def recv(self, size):
        raise socket.timeout("timed out")


def run_against(server, token=""):
    """Send a ping to the fake server and return the exit code and the printed reply."""
    output = io.StringIO()
    with patch.object(control, "conn", return_value=server), contextlib.redirect_stdout(output):
        code = control.run("127.0.0.1", 8791, token, {"cmd": "ping"})
    return code, json.loads(output.getvalue())


SCRIPT_STEPS = [
    {"delay_ms": 500, "op": "press", "input": "LB", "hold_ms": 2000},
    {"delay_ms": 2100, "op": "press", "input": "A", "hold_ms": 80},
]


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

    def test_spaced_buttons_build_state(self):
        args = make_args(buttons=" A , B ")
        expected = {**BASE_STATE, "buttons": ["A", "B"]}
        response = control.build_state(args)

        self.assertEqual(response, expected)

    def test_only_spaces_build_state(self):
        args = make_args(buttons=" , ")
        expected = {**BASE_STATE, "buttons": []}
        response = control.build_state(args)

        self.assertEqual(response, expected)

    def test_rt_in_buttons_build_state(self):
        # The game pulls the trigger for this name, so the tool sends it unchanged.
        args = make_args(buttons="A,RT")
        expected = {**BASE_STATE, "buttons": ["A", "RT"]}
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


class Fable2ControlCommandTests(unittest.TestCase):
    def test_ping_command(self):
        expected = {"cmd": "ping"}
        response = request_for("ping")

        self.assertEqual(response, expected)

    def test_info_command(self):
        expected = {"cmd": "info"}
        response = request_for("info")

        self.assertEqual(response, expected)

    def test_clear_command(self):
        expected = {"cmd": "clear"}
        response = request_for("clear")

        self.assertEqual(response, expected)

    def test_enable_command(self):
        expected = {"cmd": "enable"}
        response = request_for("enable")

        self.assertEqual(response, expected)

    def test_disable_command(self):
        expected = {"cmd": "disable"}
        response = request_for("disable")

        self.assertEqual(response, expected)

    def test_get_state_command_uses_an_underscore(self):
        # The command line uses a dash, but the server only knows "get_state".
        expected = {"cmd": "get_state"}
        response = request_for("get-state")

        self.assertEqual(response, expected)

    def test_game_state_command_uses_an_underscore(self):
        expected = {"cmd": "game_state"}
        response = request_for("game-state")

        self.assertEqual(response, expected)

    def test_press_command_holds_by_default(self):
        # Without --hold the server keeps the input pressed until release or clear.
        expected = {"cmd": "press", "input": "A"}
        response = request_for("press", "A")

        self.assertEqual(response, expected)

    def test_press_command_sends_hold_and_value(self):
        expected = {"cmd": "press", "input": "LT", "hold_ms": 120, "value": 200}
        response = request_for("press", "LT", "--hold", "120", "--value", "200")

        self.assertEqual(response, expected)

    def test_press_command_keeps_a_value_of_zero(self):
        expected = {"cmd": "press", "input": "LT", "value": 0}
        response = request_for("press", "LT", "--value", "0")

        self.assertEqual(response, expected)

    def test_press_command_rejects_an_unknown_input(self):
        argv = ["fable2_control.py", "press", "Q"]
        with (
            patch("sys.argv", argv),
            contextlib.redirect_stderr(io.StringIO()),
            self.assertRaises(SystemExit) as caught,
        ):
            control.main()

        self.assertEqual(caught.exception.code, 2)

    def test_release_command(self):
        expected = {"cmd": "release", "input": "B"}
        response = request_for("release", "B")

        self.assertEqual(response, expected)

    def test_stick_command_sends_the_default_hold(self):
        # Unlike press, stick always sends hold_ms: about one frame at 60 Hz.
        expected = {"cmd": "stick", "input": "StkLx", "value": 1000, "hold_ms": 16}
        response = request_for("stick", "StkLx", "1000")

        self.assertEqual(response, expected)

    def test_stick_command_sends_a_custom_hold(self):
        expected = {"cmd": "stick", "input": "StkLx", "value": 1000, "hold_ms": 50}
        response = request_for("stick", "StkLx", "1000", "--hold", "50")

        self.assertEqual(response, expected)

    def test_screenshot_command(self):
        expected = {"cmd": "screenshot", "path": "shot.png"}
        response = request_for("screenshot", "shot.png")

        self.assertEqual(response, expected)

    def test_cvar_command_without_value_reads(self):
        expected = {"cmd": "cvar", "name": "mouse_look_scale"}
        response = request_for("cvar", "mouse_look_scale")

        self.assertEqual(response, expected)

    def test_cvar_command_sends_the_value_as_text(self):
        # The server reads the value as a string, so "512" must not become 512.
        expected = {"cmd": "cvar", "name": "mouse_look_scale", "value": "512"}
        response = request_for("cvar", "mouse_look_scale", "512")

        self.assertEqual(response, expected)

    def test_raw_command_sends_the_json_unchanged(self):
        expected = {"cmd": "info", "id": 7}
        response = request_for("raw", '{"cmd": "info", "id": 7}')

        self.assertEqual(response, expected)

    def test_connection_options_reach_run(self):
        argv = ["fable2_control.py", "--host", "10.0.0.2", "--port", "9000",
                "--token", "secret", "ping"]
        expected = ("10.0.0.2", 9000, "secret")
        with patch("sys.argv", argv), patch.object(control, "run", return_value=0) as run:
            control.main()
        response = run.call_args.args[:3]

        self.assertEqual(response, expected)

    def test_script_command_sends_a_list_of_steps(self):
        expected = {"cmd": "script", "steps": SCRIPT_STEPS}
        response = script_request_for(SCRIPT_STEPS)

        self.assertEqual(response, expected)

    def test_script_command_unwraps_a_steps_object(self):
        # The file may also hold {"steps": [...]}; the request is the same either way.
        expected = {"cmd": "script", "steps": SCRIPT_STEPS}
        response = script_request_for({"steps": SCRIPT_STEPS})

        self.assertEqual(response, expected)


class Fable2ControlConnectionTests(unittest.TestCase):
    def test_run_returns_zero_on_an_ok_reply(self):
        server = FakeSocket(b'{"ok": true}\n')
        expected = (0, {"ok": True})
        response = run_against(server)

        self.assertEqual(response, expected)

    def test_run_returns_one_on_an_error_reply(self):
        server = FakeSocket(b'{"ok": false, "error": "unknown cmd"}\n')
        expected = (1, {"ok": False, "error": "unknown cmd"})
        response = run_against(server)

        self.assertEqual(response, expected)

    def test_run_without_token_sends_only_the_command(self):
        server = FakeSocket(b'{"ok": true}\n')
        expected = [{"cmd": "ping"}]
        run_against(server)

        self.assertEqual(server.sent, expected)

    def test_run_with_token_logs_in_first(self):
        server = FakeSocket(b'{"ok": true}\n', b'{"ok": true}\n')
        expected = [{"cmd": "auth", "token": "secret"}, {"cmd": "ping"}]
        run_against(server, token="secret")

        self.assertEqual(server.sent, expected)

    def test_run_stops_after_a_failed_login(self):
        # The command must never reach a server that rejected the token.
        server = FakeSocket(b'{"ok": false, "error": "bad token"}\n')
        expected_sent = [{"cmd": "auth", "token": "wrong"}]
        expected = (1, {"ok": False, "error": "bad token"})
        response = run_against(server, token="wrong")

        self.assertEqual(server.sent, expected_sent)
        self.assertEqual(response, expected)

    def test_run_reports_a_refused_connection(self):
        refused = ConnectionRefusedError("connection refused")
        output = io.StringIO()
        with patch.object(control, "conn", side_effect=refused), contextlib.redirect_stdout(output):
            code = control.run("127.0.0.1", 8791, "", {"cmd": "ping"})
        expected = (1, {"ok": False, "error": "connection refused"})
        response = (code, json.loads(output.getvalue()))

        self.assertEqual(response, expected)

    def test_run_reports_a_server_that_hangs_up(self):
        # No replies at all, so the first read comes back empty.
        server = FakeSocket()
        expected = (1, {"ok": False, "error": "server closed the connection"})
        response = run_against(server)

        self.assertEqual(response, expected)

    def test_run_reports_a_server_that_times_out(self):
        # A timeout is an OSError, so it is reported as JSON like a refused connection.
        server = SilentSocket()
        expected = (1, {"ok": False, "error": "timed out"})
        response = run_against(server)

        self.assertEqual(response, expected)

    @unittest.expectedFailure
    def test_run_reports_a_reply_that_is_not_json(self):
        # Known issue: run() only catches OSError, so a reply that isn't JSON ends in a
        # JSONDecodeError traceback. The tool's docstring promises JSON output either way,
        # so shell loops can always parse it.
        server = FakeSocket(b"not json\n")
        code, reply = run_against(server)

        self.assertEqual(code, 1)
        self.assertFalse(reply["ok"])

    def test_run_joins_a_reply_split_over_two_reads(self):
        server = FakeSocket(b'{"ok":', b' true}\n')
        expected = (0, {"ok": True})
        response = run_against(server)

        self.assertEqual(response, expected)


if __name__ == "__main__":
    unittest.main()
