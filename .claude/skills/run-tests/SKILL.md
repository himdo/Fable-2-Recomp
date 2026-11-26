---
name: run-tests
description: Run the local test suites that work on this machine, save the result as a baseline before a change, and compare after the change to catch regressions. Use before starting a fix or feature, after finishing it, and before opening a PR. Triggered by /run-tests [before|after].
---

# Run the tests before and after a change

The point is to compare: run the suites **before** changing anything (the baseline) and again **after**, so a new failure is never mistaken for an old one.

## 1. Pick the suites this machine can run

| Suite | Command | Runs on |
| --- | --- | --- |
| Version check | `python tools/generate_game_versions.py --check` | Any OS with Python |
| Manifest naming check | `python tools/check_manifest_names.py --base origin/master` | Any OS with Python and git |
| Python tests | `python -m pytest -q` | Any OS with Python 3.11+ and `pip install -r tests/requirements.txt` |
| Launcher tests | `dotnet run --project tests/Fable2.Launcher.ConfigTests -c Release` | Any OS with the .NET 8 SDK (CI runs it on Windows) |
| Native tests | `tests\run_native_tests.cmd` | Windows only: clang ≥ 22 on PATH, VS C++ tools, the SDK submodule checked out |

- Check what's available (`python --version`, `dotnet --list-sdks`, `where clang++`) and skip what isn't, saying so.
- On Windows in Git Bash or PowerShell, set `PYTHONIOENCODING=utf-8` for pytest.
- The native tests take a few minutes. Run them in the background when the change touches C++ (`src/`, `tests/native/`, the SDK pin).
- None of these build the game. If the change touches `src/`, `CMakeLists.txt` or the manifest, a Release build (`build.cmd -release fable_2`) is also part of "after". It needs the game files, so ask before starting one.

## 2. Before the change (`/run-tests before`)

Run the chosen suites from the repository root and write down for each one:

- the command;
- the summary line (e.g. `429 passed, 1 skipped, 15 xfailed`);
- the names of any failing tests.

Keep it in the conversation (or in the plan, if there is one). Don't write it into the repo.

## 3. After the change (`/run-tests after`)

Run the same suites again and compare with the baseline:

- **New failures** are regressions until shown otherwise. Fix them, or explain why they're expected.
- **Fewer xfails** means a known bug was fixed: the strict xfail now fails as XPASS. Remove its `@pytest.mark.xfail` marker in the same change.
- **More tests** is expected when the change adds tests.
- **A suite that couldn't run** (wrong OS, missing tool) must be listed as untested.

## 4. Report

Give a short table: suite, before, after, verdict. Put the same information in the PR description under "Tested" and "Not tested".
