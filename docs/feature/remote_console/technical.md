# Remote console: technical notes

**Status:** no commit yet · **Commits:** none · **Last checked against:** c2a9d12

Labels: **Verified** means read in the repo, or measured with the tests and checks below. **Inference** means reasoned, not tested.

## Goal

The remote-control server (`src/input/remote_control_server.h`, Debug builds only) lets a script or an AI assistant press buttons, take screenshots and read the front-end screen over a local TCP port. It couldn't run f2 console commands, so a tool on the server had no way to:
- load a save (`boot slot`);
- read values (`money`, `peek`);
- check memory (`expect`).

The function-mapping research listed this as missing ([function_mapping](../function_mapping/technical.md)). The f2 console can also be driven through `console\in.txt` in any build, which is how the scenario runner works (on `feature/scenario-runner`, not merged yet). This change gives tools that already talk to the server the same commands, without polling files.

## Design

### The `console` command

| | |
| --- | --- |
| **Request** | `{"cmd":"console","line":"money; stats","timeout_ms":30000}`. `line` holds one or more lines, and `;` chains work, exactly as typed after `f2` or in an `exec` file. `timeout_ms` is optional (default 30000, at most 600000) |
| **Reply** | `{"ok":…,"finished":…,"failing":N,"truncated":…,"lines":["[lua] = 120", …]}`, plus `"error"` when `ok` is false, and the request's `id` if it had one |
| **`ok`** | true when the commands finished in time and no reply was a failure |
| **A failing reply** | `[tag] ERR …` (any host command's error, such as a failed `expect`) or `[lua] error: …`. This is the same rule as `tools/run_scenario.py` (`IsFailingConsoleLine`) |
| **`truncated`** | true after 2,000 replies to one request; the rest is still written to `console\out.txt` |

### How it runs (Verified in the code)

- **Fable2App connects the server to the console** (`SetConsoleRunner` in `src/core/fable_2_app.h`, inside the `FABLE2_REMOTE_CONTROL` block). So the server header still doesn't include the console header. This is the same arrangement as `SetPresenterProvider`.
- **`fable2::console::run_remote` (in `src/core/fable2_console.h`):**
  - registers a **capture** for this request, with a numbered end marker;
  - sets `active`, as typing `f2` does, so the queue runs even with the file transport off;
  - queues the lines plus `echo __remote_done <n>` with `enqueue_text`, which locks the queue, so the server's thread may call it;
  - waits until the marker's reply arrives, the timeout passes, or the server stops.
- **`out()` hands every reply to the captures** (`feed_captures`).
  - Replies come from the game thread, the console thread and the Lua `cprint` hook, so the captures have their own lock. The lock order is always `out()`'s lock, then the capture lock.
  - An atomic count lets `out()` skip the capture lock when no request is waiting, which is always the case in Release builds, where the server doesn't exist.
- **Where the commands run:** only the queuing happens on the server's thread. Host commands still run on the game thread, and Lua runs on the world thread, through the queue as always. Nothing here calls `RunScript` (CLAUDE.md's alt-tab rule), and `tests/core/test_world_thread_runscript.py` still passes.
- **Waiting:**
  - The wait wakes at least every 250 ms to check whether the server is stopping, so `Stop()` doesn't hang on a long request. It never wakes past the deadline.
  - The connection is busy until the reply. The timer thread still releases timed pad inputs meanwhile.
- **After a timeout,** the queued lines still run. They can't be taken back out of the queue.

### Client

`tools/fable2_control.py console "<commands>"` or `console --file scenario.txt`, with `--timeout-ms N`.
- It sends the request and prints the reply, like every other command.
- It exits 0 when `ok` is true, otherwise 1.
- The socket waits for the timeout plus 5 s, because the server answers only once the commands finish. Every other command keeps the old 10 s.
- Giving neither or both of the commands and `--file`, a missing file, or a timeout below 1 ms is a usage error (exit 2).

### Why both this and the file route

The file route (`console\in.txt`) works in Release builds, which run much faster, and needs no server. This route needs a Debug build, but answers directly and fits tools that already use the server, such as a harness that presses buttons and takes `screenshot`s in between. Once both are merged, `tools/run_scenario.py` could get a `--port` option to use this route.

## What changed

- **`src/core/fable2_console.h`:**
  - the capture (`Capture`, `feed_captures`, called at the end of `out()`);
  - `run_remote`;
  - the includes `<condition_variable>` and `<functional>`.
- **`src/input/remote_control_server.h`:**
  - `ConsoleReply`, `ConsoleRunner` and `SetConsoleRunner`;
  - `IsFailingConsoleLine` and `HandleConsole`;
  - `console` in the dispatch, in the unknown-command list and in the header comment.
- **`src/core/fable_2_app.h`:** connects the two, Debug builds only.
- **`tools/fable2_control.py`:** the `console` subcommand (`build_console`), and a `timeout` parameter for `run()`.
- **`tests/tools/test_fable2_control.py`:** 10 tests:
  - the request from an argument or a file;
  - the default and smallest timeout;
  - the socket timeout, for `console` and for other commands;
  - the four usage errors;
  - `run()` passing its timeout to the connection.
- **`tests/README.md`:** the client's row.
- **`plans/ai-remote-input-control.md`:** `console` in the protocol table.

No game functions are called, hooked or overridden, so none needed names.

## Verification

On macOS:

| Check | Before | After |
| --- | --- | --- |
| `python -m pytest -q` | 494 passed, 15 xfailed | 504 passed, 15 xfailed |
| `python tools/generate_game_versions.py --check` | up to date | up to date |

- **Client coverage:** 97% of `tools/fable2_control.py`. The lines not run are `conn()`, which the tests replace, and `__main__`.
- **The client tests can fail:** the client was broken on purpose in five places, and each break failed a test.
- **`IsFailingConsoleLine`:** copied out of the header, compiled with clang and run against 11 replies. All 11 gave the expected answer, including `[] ERR x`, `[echo] ERRANDS done` and `[lua] = ERROR_NONE` (all not failures).
- **`feed_captures` and `run_remote`:** copied out of the header into a test program, with a stand-in queue that replies from another thread, and run under ThreadSanitizer:
  - a finished request returned its two replies and set `active`;
  - a 400 ms timeout returned after 400 ms with the replies so far, and removed its capture;
  - a stopping server ended the wait after about 250 ms.

  ThreadSanitizer reported no data races.

## Not tested

- **The whole build.** The game's code needs the SDK's bundled libraries, which aren't checked out on the Mac. So neither header has been compiled as part of the game yet. It needs a Debug build on Windows.
- **Against the game:**
  - a `console` request on a running Debug build;
  - Lua replies arriving in the reply;
  - a `boot slot 1` request with a long timeout;
  - stopping the game while a request waits.
- **Native tests:** none cover the server, and they need Windows.
- **Linux:** the Linux build, where the server exists in Debug builds too.

## Follow-ups

- On Windows: a Debug build, then `python tools/fable2_control.py console "status; money"` and a `--file` run with `boot slot 1`.
- After this and `feature/scenario-runner` are both merged: `--port` for `tools/run_scenario.py`, and a link from the `console_commands` doc to this route.
