# Probe timeline: technical notes

**Status:** no commit yet · **Commits:** none · **Last checked against:** c2a9d12

Labels: **Verified** means read in the repo or measured with the tool and its tests. **Inference** means reasoned, not tested.

## Goal

Most functions in `fable_2_manifest.toml` still have placeholder names (2,642 `sub_…` and `ProcessAndProcessAndProcessN_…` entries at 7863041, [manifest_naming_check](../manifest_naming_check/technical.md)). Naming them one feature at a time leaves most of the game unmapped. The goal here is **broad probing**: play a normal stretch of the game (say the prologue, the Heroes' Guild and Bower Lake) with the function tracer recording what runs, type a short note in the console at each moment worth marking, and afterwards get a shortlist of functions per moment.

This PR adds the half that runs on any machine: a Python tool that lines the logs up. It also fixes the log formats that the tracer will have to write, because the tracer doesn't write those logs yet (see "What the tracer still needs").

It also gives a way to measure the function-mapping research's **hot-path coverage** ([function_mapping](../function_mapping/technical.md)): how many of the functions a normal playthrough runs have real names.

## Design

### The three logs

All three are stamped with the same wall clock, in the SDK's log time format, so they line up.

| Log | Written by | Line | Used for |
| --- | --- | --- | --- |
| Game log (`logs/`) | The SDK logger, already | `[2026-11-26 14:03:12.345] [info] [<logger>] [t1234] [echo] first fight` | The notes |
| `fable2_func_firstseen.log` | The tracer, **not yet** | `[2026-11-26 14:03:12.345] sub_82100000` | When each function first ran |
| `fable2_func_minutes.log` | The tracer, **not yet** | `[2026-11-26 14:04:00.000] 1800 Cloth_Step_8219ADD8` | Calls per function per minute |

The times and counts are made up, and so is `sub_82100000` (the tests use the same kind of placeholder). `Cloth_Step_8219ADD8` is a real manifest name.

- **Notes (Verified in the code):** `f2 echo <text>` replies through `out()` in `src/core/fable2_console.h`. That logs `[echo] <text>` with `REXLOG_INFO` and also appends it to `console/out.txt`. The game log's file pattern is `[%Y-%m-%d %H:%M:%S.%e] [%l] [%n] [t%t] %v` (`thirdparty/rexglue-sdk/include/rex/logging/types.h`). `console/out.txt` has no timestamps, so the tool reads the game log. Only `[echo]` lines count; `f2 echo` with no text is skipped.
- **First seen:** one line per function. If a name appears more than once (for example, a log appended over two runs), the earliest time counts.
- **Minutes:** every line of one minute carries the same time, the moment that minute was written. A name repeated within one minute has its calls added up, so the tracer may write one line per thread.
- Lines that don't match a format are skipped, including blank lines. Windows line endings and bytes that aren't UTF-8 are read without error.

### Segments

The notes split the session into **segments**: each note's segment runs until the next note. Functions first seen before the first note get a segment of their own, labelled "(before the first note)", which is left out when it has nothing to list. A function first seen at exactly a note's time belongs to that note.

### First seen

Each segment lists the functions that ran for the first time in it, in time order. This is the main signal: whatever first runs after "opened the shop" is likely to be shop code. Loading screens produce long lists (Inference), so a note right after each load helps.

### Busier than elsewhere

First seen only shows where a function was first used. A helper first seen during loading may be the core of combat later. So, with `--minutes`, each segment also lists the functions that are **busier** there. A function counts when all of these hold:

1. It wasn't first seen in this segment (otherwise it is already listed).
2. It ran at least `--min-rate` times a minute in this segment (default 60, about once a second).
3. That rate is at least `--factor` times its rate in all the other minutes of the session (default 5), or it didn't run in them at all.

"Rate" is calls divided by the number of minutes in the segment. A minute belongs to the segment its time falls in, so a minute that a note splits counts entirely for the later note. This blurs each note's edges by up to a minute (Inference: small next to a play session). A segment with no minutes, or a session whose minutes all fall in one segment, gives no busier list: there is nothing to compare with.

The list is sorted by ratio, highest first. Functions that didn't run elsewhere come first, and ties are sorted by name.

### Options and exit codes

`python tools/probe_timeline.py [--unnamed] [--minutes LOG] [--factor N] [--min-rate N] <game log> <first-seen log>`

- `--unnamed` lists only placeholder names in both lists. It uses the same pattern as the naming check's `GENERIC`, and a test keeps the two identical.
- Exit 0 when done, and 2 when a log can't be read or an option is out of range (a factor of 1 or less, a negative minimum rate).

### What the tracer still needs (Windows)

`src/core/fable2_func_trace.h` already counts every call per function and rewrites `fable2_func_summary.log` every 5 seconds (Verified). The two new logs need:

- **First seen:** when a name is counted for the first time, append one line with the local wall-clock time in the format above.
- **Minutes:** once a minute, write the calls since the last write for every function called in that minute, all with one timestamp.
- Their own switches, so the existing trace and summary modes don't change.
- The same rotation as the other logs: `tools/fable2-functrace.cmd` renames the previous session's trace and summary to `*_prev.log` at launch, and the new logs need that too.
- A check that the game stays playable with counting on. If it doesn't, the existing filter switches help.

## What changed

- `tools/probe_timeline.py`: the new tool.
- `tests/tools/test_probe_timeline.py`: 43 tests in describe/should style (happy, bad and edge flows) covering the three log readers, segments, the busier rule, the rate format, the placeholder pattern matching the naming check, and `main()`.
- `tests/README.md`: a row for the new tests.
- `.gitignore`: the four explicit tracer log names became `fable2_func_*.log`, which also covers the two new logs. (Before, `git check-ignore` didn't ignore `fable2_func_firstseen.log` or `fable2_func_minutes.log`.)

No functions were named or renamed.

## Verification

All on macOS (Verified):

| Suite | Before | After |
| --- | --- | --- |
| `python -m pytest -q` | 494 passed, 15 xfailed | 537 passed, 15 xfailed |
| `python tools/generate_game_versions.py --check` | up to date | up to date |

- Coverage of `tools/probe_timeline.py` is 99%. The one line not run is `sys.exit(main())`.
- **The tests can fail.** The tool was broken on purpose in twelve places (for example `>=` changed to `>` at the factor, the first-seen exclusion removed, calls in one minute overwritten instead of added up), and each break failed at least one test. The one break that first passed showed an empty-note check that could never matter, and it was removed.
- `git check-ignore -v` ignores all six `fable2_func_*.log` names, and `git ls-files` tracks none of them.
- A run on made-up logs gave the expected first-seen and busier lists.

## Not tested

- On real logs: the tracer doesn't write the first-seen or minutes logs yet.
- Whether a real game log's `[echo]` lines look exactly like the pattern above. This was read in the code, not in a log from a run.
- Whether the defaults (factor 5, 60 calls a minute) give useful shortlists in real play.
- The launcher and native tests, which this change doesn't touch.

## Follow-ups

- The tracer changes above, on Windows, and a first probing run of the prologue.
- After a run: add `--json` if the lists are fed to other tools, and a hot-path coverage count (named functions out of all functions seen).
