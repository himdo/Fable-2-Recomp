"""Check the scenario runner (tools/run_scenario.py) against a fake game, without game files."""
import importlib.util
from pathlib import Path
import threading
import time

import pytest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("run_scenario", ROOT / "tools/run_scenario.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class FakeGame:
    """Stands in for the game's console: takes console/in.txt and appends replies to out.txt.

    `replies` are written for each scenario it takes, then (unless finish=False) the echo of the
    end marker, as the real console would when it reaches the scenario's last line.
    """

    def __init__(self, console, replies, finish=True, newline="\n"):
        self.console, self.replies, self.finish, self.newline = console, replies, finish, newline
        self.taken = []
        self.stopping = threading.Event()
        self.thread = threading.Thread(target=self.run, daemon=True)

    def run(self):
        in_file = self.console / "in.txt"
        while not self.stopping.is_set():
            if in_file.exists():
                text = in_file.read_text(encoding="utf-8")
                in_file.unlink()
                self.taken.append(text)
                lines = list(self.replies)
                last = text.splitlines()[-1]
                if self.finish and last.startswith("echo "):
                    lines.append("[echo] " + last.removeprefix("echo "))  # what the console's echo replies
                with (self.console / "out.txt").open("ab") as out:
                    out.write("".join(line + self.newline for line in lines).encode("utf-8"))
            time.sleep(0.005)


@pytest.fixture(autouse=True)
def quick(monkeypatch):
    """Poll fast and give up on in.txt quickly, so the tests take milliseconds."""
    monkeypatch.setattr(runner, "POLL_SECONDS", 0.01)
    monkeypatch.setattr(runner, "TAKE_SECONDS", 0.3)


@pytest.fixture
def game_root(tmp_path):
    """A game root with an empty console folder and a one-line scenario next to it."""
    (tmp_path / "console").mkdir()
    (tmp_path / "scenario.txt").write_text("status\n", encoding="utf-8")
    return tmp_path


@pytest.fixture
def play(game_root):
    """Start a fake game with the given replies; it stops when the test ends."""
    games = []

    def start(replies, **options):
        game = FakeGame(game_root / "console", replies, **options)
        game.thread.start()
        games.append(game)
        return game

    yield start
    for game in games:
        game.stopping.set()
        game.thread.join()


def run(game_root, *options):
    return runner.main([str(game_root / "scenario.txt"), "--root", str(game_root), *options])


def describe_build_input():
    # --- Happy flow -------------------------------------------------------

    def test_should_end_the_scenario_with_the_echo_of_the_marker():
        assert runner.build_input("boot slot 1\nstatus\n", "7-42") == "boot slot 1\nstatus\necho __scenario_done 7-42\n"

    # --- Edge cases -------------------------------------------------------

    def test_should_add_a_missing_last_line_ending():
        assert runner.build_input("status", "7-42") == "status\necho __scenario_done 7-42\n"

    def test_should_send_only_the_marker_for_an_empty_scenario():
        assert runner.build_input("", "7-42") == "echo __scenario_done 7-42\n"


def describe_scenario_lines():
    # --- Happy flow -------------------------------------------------------

    def test_should_return_the_replies_before_the_marker():
        lines = ["[status] ok", "[expect] ok: 82F01234 = 0x00000064", "[echo] __scenario_done 7-42"]
        assert runner.scenario_lines(lines, "7-42") == ["[status] ok", "[expect] ok: 82F01234 = 0x00000064"]

    def test_should_return_none_until_the_marker_arrives():
        assert runner.scenario_lines(["[status] ok"], "7-42") is None

    # --- Edge cases -------------------------------------------------------

    def test_should_ignore_an_earlier_run_s_marker():
        assert runner.scenario_lines(["[echo] __scenario_done 6-41", "[status] ok"], "7-42") is None

    def test_should_ignore_a_marker_that_is_not_complete_yet():
        # The game may be halfway through writing the line.
        assert runner.scenario_lines(["[echo] __scenario_done 7-4"], "7-42") is None

    def test_should_read_windows_line_endings():
        assert runner.scenario_lines(["[status] ok\r", "[echo] __scenario_done 7-42\r"], "7-42") == ["[status] ok"]


def describe_failures():
    # --- Happy flow -------------------------------------------------------

    def test_should_find_console_errors_from_any_command():
        lines = ["[expect] ERR 82F01234 = 0x00000063, expected 0x00000064", "[boot] ERR gave up: title",
                 "[status] ok"]
        assert runner.failures(lines) == lines[:2]

    def test_should_find_lua_errors():
        assert runner.failures(["[lua] error: attempt to call a nil value", "[lua] = 120"]) == [
            "[lua] error: attempt to call a nil value"]

    # --- Edge cases -------------------------------------------------------

    def test_should_not_count_an_ok_reply():
        assert runner.failures(["[expect] ok: 82F01234 = 0x00000064"]) == []

    def test_should_not_count_a_reply_that_only_mentions_err():
        assert runner.failures(["[lua] = ERROR_NONE", "[echo] ERRANDS done", "[lua] the error count is 0"]) == []


def describe_screenshots():
    # --- Happy flow -------------------------------------------------------

    def test_should_list_the_saved_files_in_order():
        lines = [r"[shot] saved C:\game\console\shots\a.png 1920x1080", "[status] ok",
                 r"[shot] saved C:\game\console\shots\b.png 960x540"]
        assert runner.screenshots(lines) == [r"C:\game\console\shots\a.png", r"C:\game\console\shots\b.png"]

    # --- Edge cases -------------------------------------------------------

    def test_should_keep_spaces_in_a_path():
        assert runner.screenshots([r"[shot] saved C:\My Games\shot 1.png 1920x1080"]) == [r"C:\My Games\shot 1.png"]

    def test_should_skip_a_failed_screenshot():
        assert runner.screenshots(["[shot] ERR capture failed: no presenter"]) == []


def describe_read_from():
    # --- Happy flow -------------------------------------------------------

    def test_should_read_only_what_comes_after_the_offset(tmp_path):
        out = tmp_path / "out.txt"
        out.write_bytes(b"old\nnew\n")
        assert runner.read_from(out, len(b"old\n")) == ["new"]

    # --- Edge cases -------------------------------------------------------

    def test_should_read_nothing_from_a_missing_file(tmp_path):
        assert runner.read_from(tmp_path / "out.txt", 0) == []

    def test_should_read_a_file_that_got_shorter_from_the_start(tmp_path):
        # Someone cleared out.txt while the scenario ran.
        out = tmp_path / "out.txt"
        out.write_bytes(b"new\n")
        assert runner.read_from(out, 100) == ["new"]

    def test_should_read_bytes_that_are_not_utf_8(tmp_path):
        out = tmp_path / "out.txt"
        out.write_bytes(b"[lua] = \xff\n")
        assert runner.read_from(out, 0) == ["[lua] = \ufffd"]


def describe_main():
    # --- Happy flow -------------------------------------------------------

    def test_should_pass_when_no_reply_is_an_error(game_root, play, capsys):
        play(["[status] frames 200, queue 0, frozen 0, trace off, errors 0"])
        assert run(game_root) == 0
        assert capsys.readouterr().out == ("[status] frames 200, queue 0, frozen 0, trace off, errors 0\n"
                                           "PASS (1 line)\n")

    def test_should_fail_and_list_the_failing_lines(game_root, play, capsys):
        play(["[boot] ERR gave up: title", "[status] ok"])
        assert run(game_root) == 1
        assert capsys.readouterr().out == ("[boot] ERR gave up: title\n[status] ok\n"
                                           "FAIL: 1 failing line\n  [boot] ERR gave up: title\n")

    def test_should_list_the_screenshots(game_root, play, capsys):
        play([r"[shot] saved C:\game\console\shots\a.png 1920x1080"])
        assert run(game_root) == 0
        assert "Screenshots:\n  C:\\game\\console\\shots\\a.png\nPASS" in capsys.readouterr().out

    def test_should_send_the_scenario_with_the_marker(game_root, play):
        game = play([])
        run(game_root)
        assert game.taken[0].startswith("status\necho __scenario_done ")

    def test_should_only_count_replies_written_after_it_started(game_root, play, capsys):
        (game_root / "console" / "out.txt").write_text("[boot] ERR gave up: title\n", encoding="utf-8")
        play(["[status] ok"])
        assert run(game_root) == 0
        assert "gave up" not in capsys.readouterr().out

    def test_should_wait_for_an_earlier_in_txt_to_be_taken(game_root, play):
        (game_root / "console" / "in.txt").write_text("status\n", encoding="utf-8")
        game = play([])
        assert run(game_root) == 0
        assert len(game.taken) == 2

    # --- Bad flow ---------------------------------------------------------

    def test_should_exit_2_without_a_console_folder(tmp_path, capsys):
        (tmp_path / "scenario.txt").write_text("status\n", encoding="utf-8")
        assert runner.main([str(tmp_path / "scenario.txt"), "--root", str(tmp_path)]) == 2
        assert "no console folder" in capsys.readouterr().err

    def test_should_exit_2_when_the_scenario_file_is_missing(game_root, capsys):
        assert runner.main([str(game_root / "missing.txt"), "--root", str(game_root)]) == 2
        assert "missing.txt" in capsys.readouterr().err

    def test_should_exit_2_and_take_back_in_txt_when_the_game_does_not_read_it(game_root, capsys):
        # No fake game: nothing ever takes in.txt.
        assert run(game_root) == 2
        assert "didn't read console/in.txt" in capsys.readouterr().err
        assert not (game_root / "console" / "in.txt").exists()

    def test_should_exit_2_when_an_earlier_in_txt_is_never_taken(game_root, capsys):
        (game_root / "console" / "in.txt").write_text("someone else's commands\n", encoding="utf-8")
        assert run(game_root) == 2
        assert "didn't read console/in.txt" in capsys.readouterr().err
        # Not ours, so it stays.
        assert (game_root / "console" / "in.txt").read_text(encoding="utf-8") == "someone else's commands\n"

    def test_should_exit_2_and_show_the_replies_so_far_when_it_does_not_finish(game_root, play, capsys):
        play(["[boot] waiting for the title"], finish=False)
        assert run(game_root, "--timeout", "0.2") == 2
        captured = capsys.readouterr()
        assert captured.out == "[boot] waiting for the title\n"
        assert "didn't finish within 0.2 s" in captured.err

    @pytest.mark.parametrize("timeout", ["0", "-5"])
    def test_should_give_a_usage_error_for_a_timeout_that_is_not_positive(game_root, capsys, timeout):
        with pytest.raises(SystemExit) as stopped:
            run(game_root, "--timeout", timeout)
        assert stopped.value.code == 2
        assert "--timeout" in capsys.readouterr().err

    # --- Edge cases -------------------------------------------------------

    def test_should_leave_no_temporary_file_behind(game_root, play):
        play([])
        run(game_root)
        assert sorted(path.name for path in (game_root / "console").iterdir()) == ["out.txt"]

    def test_should_read_windows_line_endings_from_out_txt(game_root, play, capsys):
        play(["[status] ok"], newline="\r\n")
        assert run(game_root) == 0
        assert capsys.readouterr().out == "[status] ok\nPASS (1 line)\n"

    def test_should_pass_a_scenario_with_no_replies(game_root, play, capsys):
        play([])
        assert run(game_root) == 0
        assert capsys.readouterr().out == "PASS (0 lines)\n"
