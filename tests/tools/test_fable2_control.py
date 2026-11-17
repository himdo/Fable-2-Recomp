"""Check the requests fable2_control.py sends and its server connection, without a running game."""
import argparse
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import socket
from unittest.mock import patch

import pytest

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


def script_request_for(folder, contents):
    """Save contents as a script file in folder and return the request the script command sends."""
    script = folder / "steps.json"
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


def describe_build_state():
    def test_should_build_a_bare_state_without_options():
        args = make_args()
        expected = BASE_STATE
        response = control.build_state(args)

        assert response == expected

    def test_should_list_the_buttons_and_skip_empty_names():
        args = make_args(buttons="A,,B")
        expected = {**BASE_STATE, "buttons": ["A", "B"]}
        response = control.build_state(args)

        assert response == expected

    def test_should_leave_out_an_empty_button_list():
        args = make_args(buttons="")
        expected = BASE_STATE
        response = control.build_state(args)

        assert response == expected

    def test_should_send_no_buttons_for_only_commas():
        args = make_args(buttons=",")
        expected = {**BASE_STATE, "buttons": []}
        response = control.build_state(args)

        assert response == expected

    def test_should_strip_spaces_around_button_names():
        args = make_args(buttons=" A , B ")
        expected = {**BASE_STATE, "buttons": ["A", "B"]}
        response = control.build_state(args)

        assert response == expected

    def test_should_send_no_buttons_for_only_spaces():
        args = make_args(buttons=" , ")
        expected = {**BASE_STATE, "buttons": []}
        response = control.build_state(args)

        assert response == expected

    def test_should_pass_rt_through_as_a_button_name():
        # The game pulls the trigger for this name, so the tool sends it unchanged.
        args = make_args(buttons="A,RT")
        expected = {**BASE_STATE, "buttons": ["A", "RT"]}
        response = control.build_state(args)

        assert response == expected

    def test_should_set_the_left_trigger():
        args = make_args(lt=255)
        expected = {**BASE_STATE, "triggers": {"LT": 255}}
        response = control.build_state(args)

        assert response == expected

    def test_should_set_the_right_trigger():
        args = make_args(rt=128)
        expected = {**BASE_STATE, "triggers": {"RT": 128}}
        response = control.build_state(args)

        assert response == expected

    def test_should_set_both_triggers():
        args = make_args(lt=255, rt=128)
        expected = {**BASE_STATE, "triggers": {"LT": 255, "RT": 128}}
        response = control.build_state(args)

        assert response == expected

    def test_should_keep_a_left_trigger_of_zero():
        # 0 is a real value (trigger released), so it must not be dropped like a missing option.
        args = make_args(lt=0)
        expected = {**BASE_STATE, "triggers": {"LT": 0}}
        response = control.build_state(args)

        assert response == expected

    def test_should_keep_a_right_trigger_of_zero():
        args = make_args(rt=0)
        expected = {**BASE_STATE, "triggers": {"RT": 0}}
        response = control.build_state(args)

        assert response == expected

    def test_should_set_the_left_stick():
        args = make_args(lx=-100, ly=200)
        expected = {**BASE_STATE, "stk": {"lx": -100, "ly": 200}}
        response = control.build_state(args)

        assert response == expected

    def test_should_set_the_right_stick():
        args = make_args(rx=300, ry=-400)
        expected = {**BASE_STATE, "stk": {"rx": 300, "ry": -400}}
        response = control.build_state(args)

        assert response == expected

    def test_should_set_only_the_left_stick_y():
        args = make_args(ly=200)
        expected = {**BASE_STATE, "stk": {"ly": 200}}
        response = control.build_state(args)

        assert response == expected

    def test_should_set_only_the_right_stick_x():
        args = make_args(rx=300)
        expected = {**BASE_STATE, "stk": {"rx": 300}}
        response = control.build_state(args)

        assert response == expected

    def test_should_set_only_the_right_stick_y():
        args = make_args(ry=-400)
        expected = {**BASE_STATE, "stk": {"ry": -400}}
        response = control.build_state(args)

        assert response == expected

    def test_should_keep_a_stick_value_of_zero():
        # 0 is a real value (stick centered), so it must not be dropped like a missing option.
        args = make_args(lx=0)
        expected = {**BASE_STATE, "stk": {"lx": 0}}
        response = control.build_state(args)

        assert response == expected

    def test_should_combine_all_options():
        args = make_args(buttons="A,B", lt=255, rt=128, lx=-100, ly=200, rx=300, ry=-400)
        expected = {
            **BASE_STATE,
            "buttons": ["A", "B"],
            "triggers": {"LT": 255, "RT": 128},
            "stk": {"lx": -100, "ly": 200, "rx": 300, "ry": -400},
        }
        response = control.build_state(args)

        assert response == expected


def describe_main():
    # --- Happy flow -------------------------------------------------------

    def test_should_send_the_parsed_state_options():
        argv = ["fable2_control.py", "state", "--buttons", "A,B", "--lt", "0", "--ry", "-5"]
        expected = {**BASE_STATE, "buttons": ["A", "B"], "triggers": {"LT": 0}, "stk": {"ry": -5}}
        with patch("sys.argv", argv), patch.object(control, "run", return_value=0) as run:
            control.main()

        run.assert_called_once_with("127.0.0.1", 8791, "", expected)

    def test_should_send_ping():
        expected = {"cmd": "ping"}
        response = request_for("ping")

        assert response == expected

    def test_should_send_info():
        expected = {"cmd": "info"}
        response = request_for("info")

        assert response == expected

    def test_should_send_clear():
        expected = {"cmd": "clear"}
        response = request_for("clear")

        assert response == expected

    def test_should_send_enable():
        expected = {"cmd": "enable"}
        response = request_for("enable")

        assert response == expected

    def test_should_send_disable():
        expected = {"cmd": "disable"}
        response = request_for("disable")

        assert response == expected

    def test_should_send_get_state_with_an_underscore():
        # The command line uses a dash, but the server only knows "get_state".
        expected = {"cmd": "get_state"}
        response = request_for("get-state")

        assert response == expected

    def test_should_send_game_state_with_an_underscore():
        expected = {"cmd": "game_state"}
        response = request_for("game-state")

        assert response == expected

    def test_should_hold_a_press_by_default():
        # Without --hold the server keeps the input pressed until release or clear.
        expected = {"cmd": "press", "input": "A"}
        response = request_for("press", "A")

        assert response == expected

    def test_should_send_a_press_with_its_hold_and_value():
        expected = {"cmd": "press", "input": "LT", "hold_ms": 120, "value": 200}
        response = request_for("press", "LT", "--hold", "120", "--value", "200")

        assert response == expected

    def test_should_send_release():
        expected = {"cmd": "release", "input": "B"}
        response = request_for("release", "B")

        assert response == expected

    def test_should_send_a_stick_with_the_default_hold():
        # Unlike press, stick always sends hold_ms: about one frame at 60 Hz.
        expected = {"cmd": "stick", "input": "StkLx", "value": 1000, "hold_ms": 16}
        response = request_for("stick", "StkLx", "1000")

        assert response == expected

    def test_should_send_a_stick_with_a_custom_hold():
        expected = {"cmd": "stick", "input": "StkLx", "value": 1000, "hold_ms": 50}
        response = request_for("stick", "StkLx", "1000", "--hold", "50")

        assert response == expected

    def test_should_send_screenshot():
        expected = {"cmd": "screenshot", "path": "shot.png"}
        response = request_for("screenshot", "shot.png")

        assert response == expected

    def test_should_read_a_cvar_without_a_value():
        expected = {"cmd": "cvar", "name": "mouse_look_scale"}
        response = request_for("cvar", "mouse_look_scale")

        assert response == expected

    def test_should_send_a_cvar_value_as_text():
        # The server reads the value as a string, so "512" must not become 512.
        expected = {"cmd": "cvar", "name": "mouse_look_scale", "value": "512"}
        response = request_for("cvar", "mouse_look_scale", "512")

        assert response == expected

    def test_should_send_raw_json_unchanged():
        expected = {"cmd": "info", "id": 7}
        response = request_for("raw", '{"cmd": "info", "id": 7}')

        assert response == expected

    def test_should_pass_the_connection_options_to_run():
        argv = ["fable2_control.py", "--host", "10.0.0.2", "--port", "9000",
                "--token", "secret", "ping"]
        expected = ("10.0.0.2", 9000, "secret")
        with patch("sys.argv", argv), patch.object(control, "run", return_value=0) as run:
            control.main()
        response = run.call_args.args[:3]

        assert response == expected

    def test_should_send_a_script_as_a_list_of_steps(tmp_path):
        expected = {"cmd": "script", "steps": SCRIPT_STEPS}
        response = script_request_for(tmp_path, SCRIPT_STEPS)

        assert response == expected

    def test_should_unwrap_a_script_steps_object(tmp_path):
        # The file may also hold {"steps": [...]}; the request is the same either way.
        expected = {"cmd": "script", "steps": SCRIPT_STEPS}
        response = script_request_for(tmp_path, {"steps": SCRIPT_STEPS})

        assert response == expected

    # --- Bad flow ---------------------------------------------------------

    def test_should_reject_a_non_integer_state_value():
        argv = ["fable2_control.py", "state", "--lt", "abc"]
        with (
            patch("sys.argv", argv),
            contextlib.redirect_stderr(io.StringIO()),
            pytest.raises(SystemExit) as caught,
        ):
            control.main()

        assert caught.value.code == 2

    def test_should_reject_a_press_of_an_unknown_input():
        argv = ["fable2_control.py", "press", "Q"]
        with (
            patch("sys.argv", argv),
            contextlib.redirect_stderr(io.StringIO()),
            pytest.raises(SystemExit) as caught,
        ):
            control.main()

        assert caught.value.code == 2

    # --- Edge cases -------------------------------------------------------

    def test_should_keep_a_press_value_of_zero():
        expected = {"cmd": "press", "input": "LT", "value": 0}
        response = request_for("press", "LT", "--value", "0")

        assert response == expected


def describe_run():
    # --- Happy flow -------------------------------------------------------

    def test_should_return_zero_on_an_ok_reply():
        server = FakeSocket(b'{"ok": true}\n')
        expected = (0, {"ok": True})
        response = run_against(server)

        assert response == expected

    def test_should_send_only_the_command_without_a_token():
        server = FakeSocket(b'{"ok": true}\n')
        expected = [{"cmd": "ping"}]
        run_against(server)

        assert server.sent == expected

    def test_should_log_in_first_with_a_token():
        server = FakeSocket(b'{"ok": true}\n', b'{"ok": true}\n')
        expected = [{"cmd": "auth", "token": "secret"}, {"cmd": "ping"}]
        run_against(server, token="secret")

        assert server.sent == expected

    # --- Bad flow ---------------------------------------------------------

    def test_should_return_one_on_an_error_reply():
        server = FakeSocket(b'{"ok": false, "error": "unknown cmd"}\n')
        expected = (1, {"ok": False, "error": "unknown cmd"})
        response = run_against(server)

        assert response == expected

    def test_should_stop_after_a_failed_login():
        # The command must never reach a server that rejected the token.
        server = FakeSocket(b'{"ok": false, "error": "bad token"}\n')
        expected_sent = [{"cmd": "auth", "token": "wrong"}]
        expected = (1, {"ok": False, "error": "bad token"})
        response = run_against(server, token="wrong")

        assert server.sent == expected_sent
        assert response == expected

    def test_should_report_a_refused_connection():
        refused = ConnectionRefusedError("connection refused")
        output = io.StringIO()
        with patch.object(control, "conn", side_effect=refused), contextlib.redirect_stdout(output):
            code = control.run("127.0.0.1", 8791, "", {"cmd": "ping"})
        expected = (1, {"ok": False, "error": "connection refused"})
        response = (code, json.loads(output.getvalue()))

        assert response == expected

    def test_should_report_a_server_that_hangs_up():
        # No replies at all, so the first read comes back empty.
        server = FakeSocket()
        expected = (1, {"ok": False, "error": "server closed the connection"})
        response = run_against(server)

        assert response == expected

    def test_should_report_a_server_that_times_out():
        # A timeout is an OSError, so it is reported as JSON like a refused connection.
        server = SilentSocket()
        expected = (1, {"ok": False, "error": "timed out"})
        response = run_against(server)

        assert response == expected

    # --- Edge cases -------------------------------------------------------

    def test_should_join_a_reply_split_over_two_reads():
        server = FakeSocket(b'{"ok":', b' true}\n')
        expected = (0, {"ok": True})
        response = run_against(server)

        assert response == expected

    # --- Known issues -----------------------------------------------------

    @pytest.mark.xfail(strict=True, reason="run() only catches OSError, so a reply that isn't JSON ends in a JSONDecodeError traceback")
    def test_should_report_a_reply_that_is_not_json():
        # The tool's docstring promises JSON output either way, so shell loops can always parse it.
        server = FakeSocket(b"not json\n")
        code, reply = run_against(server)

        assert code == 1
        assert not reply["ok"]
