# Scenario runner: technical notes

**Status:** no commit yet · **Commits:** none · **Last checked against:** c2a9d12

Labels: **Verified** means read in the repo or measured with the tool and its tests. **Inference** means reasoned, not tested.

## Goal

The function-mapping research proposes **scenario files**: short scripted play sessions that drive the game and check the result ([function_mapping](../function_mapping/technical.md), "Tie-in with the automated checking loop"). They would be E5 evidence for manifest names and regression tests for features. Scenarios also let an AI assistant running on the same machine play the game and see what happened, instead of only reading code.

The research assumed this needed new C++ in the Debug-only remote-control server. Reading the code shows that the f2 console's file transport already does most of it, in Release builds too. What was missing was a runner: something that hands a scenario to the game, waits until it's done, and reports pass or fail.

## Design

### What the console already provides (Verified in `src/core/fable2_console.h`)

| Need | Already there |
| --- | --- |
| Send commands | Lines written to `<game root>\console\in.txt` are read about every 10 frames. The file is then deleted, and its lines are queued: `#` comments, `;` chains, the same as `exec` files |
| Read replies | Host replies are appended to `console\out.txt` as `[tag] msg`, and failures as `[tag] ERR msg`. Lua replies tagged `[console] ` are copied there as `[lua] …` lines by the `cprint` hook |
| Wait for Lua | The queue holds until a Lua command replies, or gives up after 30 s with `[lua] ERR no reply from the Lua command within 30 s` |
| Start from a save | `boot slot <n>` goes from the start-up screens into a save, and prints `[boot] in game` or `[boot] ERR gave up: <step>` |
| Input, reads, checks | `pad`, `wait`, `waitfor ram`, `peek`, `expect` (`ok` or `ERR`), and Lua commands such as `money` and `stats` |
| Off switch | `console_enabled = false` or `FABLE2_CONSOLE=0` turns the transport off |

### The runner: `tools/run_scenario.py`

`python tools/run_scenario.py --root <game root> [--timeout 300] <scenario file>`

**The scenario file** is a plain list of console commands, the same format as an `exec` file, so the same file also runs by hand with `f2 exec`.

**Steps:**

1. Check that `<root>\console\` exists.
2. Wait up to 10 s for any earlier `in.txt` to be taken. If it isn't, stop, and leave that file alone, because it isn't the runner's.
3. Note the size of `out.txt`. Only replies written after that point count.
4. Write the scenario plus a last line `echo __scenario_done <id>` to `in.txt.tmp`, then rename it to `in.txt`, so the game never reads half a file. `<id>` is the time in nanoseconds plus the process id, so a marker from an earlier run never matches. The console's `echo` replies `[echo] <text>` (Verified).
5. Wait up to 10 s for the game to take `in.txt`. If it doesn't, delete the file again, so the scenario can't start later with nobody watching, and stop with a hint: is the game running, with `console_enabled` on?
6. Read `out.txt` from the noted size until `[echo] __scenario_done <id>` arrives, or until `--timeout` runs out. If `out.txt` got shorter, for example because someone cleared it, it is read from the start.

**The report:**

- Every reply the scenario produced is printed.
- A reply is a **failure** when it matches `^\[[^\]]+\] ERR ` (any host command's error, including a failed `expect` or `boot`) or starts with `[lua] error:`.
- **Screenshots** are lines of the form `[shot] saved <path> <w>x<h>`. They are listed at the end, so whoever runs the scenario knows which pictures to open. No console command writes these lines yet; a later `shot` command has to use exactly this form.

**Exit codes:**

| Code | Meaning |
| --- | --- |
| 0 | No reply was a failure |
| 1 | At least one reply was a failure |
| 2 | The scenario couldn't be run or didn't finish: no console folder, an unreadable scenario, `in.txt` not taken, or the timeout. A timeout also prints the replies so far |

Arguments that are out of range, such as a timeout of 0 or less, are usage errors (also 2).

### Example (untested)

```text
# Start from the first save, give 100 gold and look at the result.
boot slot 1
money
gold 100
money
```

Both `money` lines print the gold. The runner shows them, but it can't compare them yet (see "Follow-ups"). `expect <addr> <value>` is the check that exists today; it needs a known guest address, such as one found with `scan`.

### How an AI assistant uses it (Inference, to be tried on Windows)

On the machine that runs the game, the assistant writes a scenario, runs the runner, and reads the printed replies. It then opens any screenshots in the list, since Claude Code can read PNG files. That is the loop that gets it "closer to the game": it can try something, look at the result and try again, without anyone pressing buttons.

### Why not the remote-control server

The server (`src/input/remote_control_server.h`) only exists in Debug builds, which run much slower, and it has no console command. The file transport works in the Release builds that players and contributors run. A `console` command in the server is still useful for tools that already talk to it, so it's planned separately.

## What changed

- `tools/run_scenario.py`: the new runner, stdlib only.
- `tests/tools/test_run_scenario.py`: 35 tests in describe/should style. The `main()` tests run against a **fake game**, a thread that takes `in.txt`, deletes it and appends replies to `out.txt`, all inside a temporary folder.
- `tests/README.md`: a row for the new tests.
- `docs/feature/function_mapping/{technical,overview}.md`: the "What's missing for scenario files" list said nothing could read game state, run checks or load a save. It now lists what the console already does, and what is really still missing.
- `docs/feature/console_commands/technical.md`: points to the runner, and notes that Lua replies are also copied to `out.txt`.

No functions were named or renamed.

## Verification

All on macOS (Verified):

| Suite | Before | After |
| --- | --- | --- |
| `python -m pytest -q` | 494 passed, 15 xfailed | 529 passed, 15 xfailed |
| `python tools/generate_game_versions.py --check` | up to date | up to date |

- Coverage of `tools/run_scenario.py` is 99%. The one line not run is `sys.exit(main())`.
- The runner tests passed three runs in a row (they use threads and real timing, with 10 ms polls).
- **The tests can fail.** The runner was broken on purpose in eight places, for example: the ERR pattern loosened, Lua errors not counted, the marker check dropped, `out.txt` read from the start, the screenshot size not required, a direct write instead of the rename, and the unwatched `in.txt` left behind. Each break failed at least one test.
- A run by hand against a stand-in game printed the PASS report with its screenshot list, and the FAIL report for a failed `expect`.

## Not tested

- **Against the real game.** Nothing here has run with the game yet. In particular, these are unchecked:
  - whether `in.txt` is picked up from the game root that the launcher uses;
  - how long `boot slot 1` takes compared with the 300 s default;
  - whether `[lua]` lines really reach `out.txt` while a scenario runs.
- Two runners at once on one game root. The second waits for the first one's `in.txt`, but their replies would mix (Inference).
- The launcher and native tests, which this change doesn't touch.

## Follow-ups

- **A first real run on Windows,** before the PR: one small scenario on a Release build, with the result written here.
- **Comparing Lua values,** for example a runner line such as `#expect money = 220`, or a console command that turns a Lua reading into `ok`/`ERR`.
- **`shot`:** a console command that saves a screenshot using photo mode's capture (`fable2::photomode::take_screenshot`, Release builds) and writes `[shot] saved <path> <w>x<h>`. Planned on `feature/console-screenshot`.
- **A `console` command in the remote-control server,** with `--port` for the runner. Planned on `feature/remote-console`.
