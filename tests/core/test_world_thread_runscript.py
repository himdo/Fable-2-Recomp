"""Check that the host runs game Lua (RunScript) only inside the world-update service.

Every host RunScript goes through f5lua::run_external, and it must run inside
console::service_world_thread: photo mode's GameWorldState_Update hook calls that at the
start of each world update, on the world's own thread. Anywhere else, such as the
MainRenderLoop hook console::poll on the render thread, RunScript races the world update
and corrupts the game's script engine. That is how 1.6 crashed after alt-tab: #177 started
mouse_direct.lua from console::poll right after #184 had made the world thread the only
safe place (fixed by #189). The game also refuses such a call at runtime
(src/core/world_thread.h); these tests catch it before it is merged.

The tests read the C++ source. Comments, string literals and preprocessor lines are blanked
out, each function definition is found by its braces, and every `name(` inside its body counts
as a call. Names are unqualified, so overloads and same-named functions in other namespaces
share one node: a clash can only add callers, never hide one.
"""
from collections import deque
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "src"

RUN_SCRIPT = "run_external"
SERVICE = "service_world_thread"
# FABLE2_RUNSCRIPT_STRESS=render runs RunScript from the render thread on purpose, to
# reproduce the race; it is checked on its own below.
RESEARCH = "stress_tick"
SCRIPT_RUNNERS = {RUN_SCRIPT, "run_world_script", "poll_mouse_direct", "poll_world", SERVICE}
NOT_CALLS = {"if", "for", "while", "switch", "catch", "return", "sizeof", "alignof", "decltype",
             "defined", "static_assert", "noexcept"}

RAW_STRING = re.compile(r'(?:u8|[uUL])?R"([^()\\\s]{0,16})\(')
# The text before a `{` that opens a function body: a name, its parameter list and any
# qualifiers. Lambdas ([](...) {) and control blocks (if (...) {) don't match or are dropped.
DEFINITION = re.compile(
    r"(~?[A-Za-z_]\w*)\s*\(((?:[^()]|\([^()]*\))*)\)\s*"
    r"(?:(?:const|noexcept|override|final|mutable|volatile|&&|&)\s*|->\s*[\w:<>,*&\s]+?)*$")
CALL = re.compile(r"(~?[A-Za-z_]\w*)\s*\(")


def strip_comments_and_strings(text):
    """Blank out comments, string and char literals and preprocessor lines, keeping newlines."""
    out = list(text)
    n = len(text)

    def blank(start, end):
        for k in range(start, min(end, n)):
            if out[k] != "\n":
                out[k] = " "

    i = 0
    line_start = True
    while i < n:
        c = text[i]
        if c == "\n":
            line_start = True
            i += 1
            continue
        if c in " \t\r\f\v":
            i += 1
            continue
        if line_start and c == "#":
            j = i
            while j < n and not (text[j] == "\n" and text[j - 1] != "\\"):
                j += 1
            blank(i, j)
            i = j
            continue
        line_start = False
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            blank(i, j)
            i = j
            continue
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            blank(i, j)
            i = j
            continue
        raw = RAW_STRING.match(text, i)
        if raw and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == "_")):
            close = text.find(")" + raw.group(1) + '"', raw.end())
            j = n if close < 0 else close + len(raw.group(1)) + 2
            blank(i, j)
            i = j
            continue
        if c == "'" and is_digit_separator(text, i):
            i += 1
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


def is_digit_separator(text, i):
    """True for the ' in 5'000'000 (C++14), false for a char literal such as u8'a'."""
    k = i - 1
    while k >= 0 and (text[k].isalnum() or text[k] in "_'."):
        k -= 1
    return text[k + 1:i][:1].isdigit()


def definition_name(head):
    """The function name when `head` (the text before a `{`) ends a function header."""
    m = DEFINITION.search(head)
    if not m or m.group(1) in NOT_CALLS:
        return None
    name, args = m.group(1), m.group(2).strip()
    # A hook macro such as PPC_FUNC_IMPL(__imp__sub_82276C30) is named after its argument.
    if name.isupper() and re.fullmatch(r"\w+", args):
        return args
    return name


@dataclass(frozen=True)
class Function:
    name: str
    path: str
    body: str


def parse(code, path="<text>"):
    """Function definitions outside other functions, and whether the braces balance.

    `code` must already be stripped. Lambdas, local structs and control blocks stay inside
    their enclosing function's body.
    """
    functions = []
    current, start, depth = None, 0, 0
    blocks = 0
    last = 0
    for m in re.finditer(r"[{};]", code):
        c, i = m.group(), m.start()
        if current is not None:
            if c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
                if depth == 0:
                    functions.append(Function(current, path, code[start:i + 1]))
                    current = None
                    last = i + 1
            continue
        if c == "{":
            name = definition_name(code[max(last, i - 600):i])
            if name:
                current, start, depth = name, i, 1
            else:
                blocks += 1
        elif c == "}":
            blocks -= 1
        last = i + 1
    return functions, current is None and blocks == 0


def calls(function):
    return {name for name in CALL.findall(function.body)} - NOT_CALLS - {function.name}


def call_graph(functions):
    graph = {}
    for f in functions:
        graph.setdefault(f.name, set()).update(calls(f))
    return graph


def entry_points(graph, target=RUN_SCRIPT, stop=(SERVICE,), skip=(RESEARCH,)):
    """Each function that reaches `target` without being called by another one that does.

    Returns {entry: "entry -> ... -> target"}. The search walks callers back from `target`,
    doesn't walk past `stop` and leaves out `skip`.
    """
    callers = {}
    for caller, callees in graph.items():
        for callee in callees:
            callers.setdefault(callee, set()).add(caller)
    next_hop = {target: None}
    queue = deque([target])
    while queue:
        f = queue.popleft()
        if f in stop:
            continue
        for caller in sorted(callers.get(f, ())):
            if caller in skip or caller in next_hop:
                continue
            next_hop[caller] = f
            queue.append(caller)

    def chain(f):
        hops = [f]
        while next_hop[hops[-1]] is not None:
            hops.append(next_hop[hops[-1]])
        return " -> ".join(hops)

    entries = {f for f in next_hop
               if f in stop or not (callers.get(f, set()) - set(skip) - {f})}
    return {f: chain(f) for f in sorted(entries)}


@lru_cache(maxsize=None)
def source_tree(root=SRC):
    """Every function in the C++ sources under `root`, and the files whose braces don't balance."""
    functions, unbalanced = [], []
    for path in sorted(root.rglob("*")):
        if path.suffix not in {".h", ".hpp", ".cpp", ".cc", ".inl"}:
            continue
        rel = path.relative_to(root.parent).as_posix()
        found, balanced = parse(strip_comments_and_strings(path.read_text(encoding="utf-8",
                                                                          errors="replace")), rel)
        functions.extend(found)
        if not balanced:
            unbalanced.append(rel)
    return tuple(functions), tuple(unbalanced)


def functions_where(pattern, root=SRC):
    """(file, function) for each function whose body matches `pattern`."""
    rx = re.compile(pattern)
    return {(f.path, f.name) for f in source_tree(root)[0] if rx.search(f.body)}


def named(name, path, root=SRC):
    return [f for f in source_tree(root)[0] if f.name == name and f.path == path]


def describe_strip_comments_and_strings():
    def test_should_blank_comments_and_keep_line_numbers():
        text = "a(); // run_external(x)\n/* run_external(\n y) */ b();\n"
        expected = "a(); " + " " * 18 + "\n" + " " * 16 + "\n" + " " * 6 + " b();\n"
        assert strip_comments_and_strings(text) == expected

    def test_should_blank_string_and_char_literals():
        text = 'f("run_external(\\"x\\")", \'{\');'
        assert strip_comments_and_strings(text) == "f(" + " " * 21 + ", " + " " * 3 + ");"

    def test_should_blank_raw_strings():
        text = 'f(R"x(a { b " c)x");'
        assert strip_comments_and_strings(text) == "f(" + " " * 16 + ");"

    def test_should_keep_digit_separators():
        assert strip_comments_and_strings("x = 5'000'000; c = 'a';") == "x = 5'000'000; c =    ;"

    def test_should_blank_preprocessor_lines_with_continuations():
        text = "#define F(x) \\\n  { x(); }\nint a;\n"
        expected = " " * len("#define F(x) \\") + "\n" + " " * len("  { x(); }") + "\nint a;\n"
        assert strip_comments_and_strings(text) == expected


def describe_parse():
    def test_should_name_each_function_definition():
        code = "namespace a {\ninline void f(int x) { g(x); }\nstruct S { int h() const { return 1; } };\n}"
        functions, balanced = parse(code)
        assert ([f.name for f in functions], balanced) == (["f", "h"], True)

    def test_should_keep_lambdas_and_blocks_inside_their_function():
        code = "void f() { if (a) { b(); } auto l = [](int x) { c(x); }; }\nvoid g() {}"
        functions, _ = parse(code)
        assert [(f.name, sorted(calls(f))) for f in functions] == [("f", ["b", "c"]), ("g", [])]

    def test_should_not_count_declarations_or_initialisers():
        code = "void f(int x);\nstd::atomic<bool> ready{false};\nvoid g() { f(1); }"
        functions, balanced = parse(code)
        assert ([f.name for f in functions], balanced) == (["g"], True)

    def test_should_name_a_hook_macro_after_its_argument():
        functions, _ = parse("PPC_FUNC_IMPL(__imp__sub_82276C30) { poll(ctx, base); }")
        assert [f.name for f in functions] == ["__imp__sub_82276C30"]

    def test_should_report_unbalanced_braces():
        assert parse("void f() { if (a) {\n}")[1] is False


def describe_entry_points():
    def test_should_find_the_177_render_thread_call():
        # master 3635c93: the MainRenderLoop hook started mouse_direct.lua itself.
        graph = {
            "render_hook": {"poll"},
            "poll": {"poll_mouse_direct", "stress_tick"},
            "poll_mouse_direct": {"run_world_script"},
            "run_world_script": {"run_external"},
            "service_world_thread": {"run_world_script", "poll_world"},
            "poll_world": {"run_external"},
            "stress_tick": {"run_external"},
        }
        assert entry_points(graph) == {
            "render_hook": "render_hook -> poll -> poll_mouse_direct -> run_world_script -> run_external",
            "service_world_thread": "service_world_thread -> poll_world -> run_external",
        }

    def test_should_accept_runs_that_all_come_from_the_service():
        graph = {
            "render_hook": {"poll"},
            "poll": {"stress_tick"},
            "service_world_thread": {"poll_mouse_direct", "poll_world"},
            "poll_mouse_direct": {"run_world_script"},
            "run_world_script": {"run_external"},
            "poll_world": {"run_external"},
            "stress_tick": {"run_external"},
        }
        assert list(entry_points(graph)) == ["service_world_thread"]


def describe_run_external():
    def test_should_parse_every_source_file_with_balanced_braces():
        assert source_tree()[1] == ()

    def test_should_be_reached_only_from_service_world_thread():
        entries = entry_points(call_graph(source_tree()[0]))
        assert list(entries) == [SERVICE], (
            "RunScript (f5lua::run_external) is reached from outside "
            "console::service_world_thread. Off the world update it corrupts the game's "
            "script engine (the 1.6 alt-tab crash, #189). Start the script from "
            "service_world_thread instead:\n  " + "\n  ".join(entries.values()))


def describe_service_world_thread():
    def test_should_only_be_wired_to_the_world_thread_hook():
        callers = functions_where(r"\bservice_world_thread\s*\(")
        hooked = functions_where(
            r"world_thread_hook\(\)\s*=\s*\[[^\]]*\]\s*\([^)]*\)\s*\{\s*"
            r"(?:[\w:]*::)?service_world_thread\s*\(")
        assert (len(callers), callers == hooked) == (1, True)

    def test_should_have_its_hook_called_only_from_on_world_update():
        assert functions_where(r"world_thread_hook\(\)\s*\(") == {
            ("src/core/fable2_photo_mode.h", "on_world_update")}


def describe_world_thread_scopes():
    def test_should_open_the_service_scope_only_in_on_world_update():
        assert functions_where(r"\bServiceScope\b") == {
            ("src/core/fable2_photo_mode.h", "on_world_update")}

    def test_should_open_the_service_scope_around_the_hook_call():
        body = named("on_world_update", "src/core/fable2_photo_mode.h")[0].body
        assert re.search(r"ServiceScope\s+\w+\s*;\s*world_thread_hook\(\)\s*\(", body)

    def test_should_allow_research_runs_only_in_stress_tick():
        assert functions_where(r"\bResearchOffServiceScope\b") == {
            ("src/core/fable2_console.h", RESEARCH)}

    def test_should_allow_research_runs_only_for_the_render_thread_switch():
        body = named(RESEARCH, "src/core/fable2_console.h")[0].body
        assert (re.search(r"stress_mode\(\)\s*!=\s*thread[^;]*return", body) is not None,
                re.search(r"if\s*\(\s*thread\s*==\s*1\s*\)\s*\w+\.emplace\(\)", body) is not None) == (True, True)


def describe_main_render_loop_hook():
    def test_should_not_run_scripts():
        poll = named("poll", "src/core/fable2_console.h")
        assert (len(poll), calls(poll[0]) & SCRIPT_RUNNERS) == (1, set()), (
            "console::poll runs on the render thread (MainRenderLoop). It must only ask for a "
            "script; console::service_world_thread runs it on the world thread (#189).")
