"""Check that the recomp's Lua scripts leave nothing running in the game's script manager.

The game's GeneralScriptManager writes every script it is running into save games
(GetSaveTable / LoadFromSave) and brings them back when the save loads. The console's old
command pump was one of them, and it held `_G`, so every save carried the whole global table;
loading one restored the pump beside a new one and the game's script engine crashed soon
after (#189). Host features therefore run as one-shot scripts (RunScript of
console_step.lua or mouse_direct.lua) and register nothing. The F5 menu is the one exception:
it registers while the menu is open and ends itself when it closes.

The tests read src/lua/*.lua with comments and strings blanked out, so a mention in a comment
doesn't count.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
LUA = ROOT / "src/lua"

LONG_BRACKET = re.compile(r"\[(=*)\[")


def strip_lua_comments_and_strings(text):
    """Blank out Lua comments and string literals (short and long), keeping newlines."""
    out = list(text)
    n = len(text)

    def blank(start, end):
        for k in range(start, min(end, n)):
            if out[k] != "\n":
                out[k] = " "

    def long_end(level_start):
        m = LONG_BRACKET.match(text, level_start)
        if not m:
            return None
        close = text.find("]" + m.group(1) + "]", m.end())
        return n if close < 0 else close + len(m.group(1)) + 2

    i = 0
    while i < n:
        if text.startswith("--", i):
            end = long_end(i + 2)
            if end is None:
                end = text.find("\n", i)
                end = n if end < 0 else end
            blank(i, end)
            i = end
            continue
        c = text[i]
        if c == "[":
            end = long_end(i)
            if end is not None:
                blank(i, end)
                i = end
                continue
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            blank(i, j + 1)
            i = j + 1
            continue
        i += 1
    return "".join(out)


def scripts():
    return {p.name: strip_lua_comments_and_strings(p.read_text(encoding="utf-8"))
            for p in sorted(LUA.glob("*.lua"))}


def scripts_matching(pattern):
    rx = re.compile(pattern)
    return {name for name, code in scripts().items() if rx.search(code)}


def describe_strip_lua_comments_and_strings():
    def test_should_blank_line_and_long_comments():
        text = "a() -- AddScript(x)\n--[==[ AddScript(\n]] y ]==] b()"
        expected = "a() " + " " * 15 + "\n" + " " * 17 + "\n" + " " * len("]] y ]==]") + " b()"
        assert strip_lua_comments_and_strings(text) == expected

    def test_should_blank_short_and_long_strings():
        text = "f('--x', \"a\\\"b\", [[c\n]]) t[i] = 1"
        expected = "f(" + " " * 5 + ", " + " " * 6 + ", " + " " * 3 + "\n" + " " * 2 + ") t[i] = 1"
        assert strip_lua_comments_and_strings(text) == expected


def describe_GeneralScriptManager():
    def test_should_be_given_scripts_only_by_the_F5_menu():
        assert scripts_matching(r"\bGeneralScriptManager\s*[.:]\s*AddScript\b") == {"F5.lua"}, (
            "A script registered with GeneralScriptManager.AddScript is written into save games "
            "and comes back on load (#189). Run host features as one-shot RunScript files instead, "
            "like console_step.lua and mouse_direct.lua.")

    def test_should_only_be_used_by_the_F5_menu_and_the_console_cleanup():
        # console_pump.lua walks the list to retire console and F5 scripts restored from
        # older saves. Any other use (an alias, then AddScript) needs the same review.
        assert scripts_matching(r"\bGeneralScriptManager\b") == {"F5.lua", "console_pump.lua"}

    def test_should_have_the_F5_menu_end_itself_when_nothing_is_open():
        f5 = scripts()["F5.lua"]
        assert re.search(r"\bregistered\s*=\s*false\s+return\b", f5), (
            "F5.lua's update must end (return) when no menu is open, so the menu script, which "
            "holds _G, is never in the list when the game saves.")
