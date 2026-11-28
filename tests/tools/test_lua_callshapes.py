"""tools/lua_callshapes.py, which writes docs/reference/lua_call_shapes.md from the script banks.

Only the literal formatting is tested: the bytecode walk needs the game's banks.
"""
import importlib.util
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("lua_callshapes", ROOT / "tools/lua_callshapes.py")
shapes = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shapes)


def describe_kdesc():
    @pytest.mark.parametrize("key", [
        "GUI_BED_COST",
        "TEXT_QUEST_QC010_GOLD_10",
        "RoundTimer",
        "Intro.bik",
        "Art\\GUI\\WorldMap\\Bowerstone\\firstXX.tex",
        "Debug.",
        "-1",
        "",
    ])
    def test_should_print_keys_ids_and_paths(key):
        assert shapes.kdesc(("str", key)) == '"%s"' % key

    @pytest.mark.parametrize("text", [
        "Find the key.",
        "Hello there",
        " ",
        "Wait...",
        "Ready?",
        "It's done",
        "Ünïcode",
        "x" * 65,
    ])
    def test_should_hide_anything_that_reads_like_text(text):
        assert shapes.kdesc(("str", text)) == shapes.REDACTED

    def test_should_print_numbers_bools_and_nil():
        assert shapes.kdesc(("num", 3.0)) == "3"
        assert shapes.kdesc(("num", 0.75)) == "0.75"
        assert shapes.kdesc(("bool", True)) == "true"
        assert shapes.kdesc(("nil", None)) == "nil"


def describe_write_md():
    def test_should_explain_the_hidden_strings(tmp_path):
        out = tmp_path / "shapes.md"

        shapes.write_md(shapes.Analysis(), out, ["`gamescripts.bnk`"])

        assert '`"…"` a string' in out.read_text(encoding="utf-8")

    def test_should_never_write_a_hidden_string(tmp_path):
        analysis = shapes.Analysis()
        call = analysis.calls["GUI.DisplayInfoBox"]
        call["n"] = 1
        call["argc"]["1"] = 1
        call["lits"].add(shapes.kdesc(("str", "A line of on-screen text")))
        out = tmp_path / "shapes.md"

        shapes.write_md(analysis, out, ["`gamescripts.bnk`"])

        text = out.read_text(encoding="utf-8")
        assert "on-screen text" not in text
        assert '`("…")`' in text
