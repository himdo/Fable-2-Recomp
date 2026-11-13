# Tests

These tests check the launcher, the build tools and some runtime code. None of
them need game files, a GPU or an audio device.

## Quick start

From the repository root, with Python 3.11 or later. The first time, set up a
virtual environment with the test tools:

```sh
python -m venv .venv
.venv/bin/pip install -r tests/requirements.txt
```

On Windows, use `.venv\Scripts\pip` instead. Then run:

```sh
python tools/generate_game_versions.py --check
.venv/bin/python -m pytest
```

If `python` isn't found, use `python3`. The Python tests run with
[pytest](https://docs.pytest.org/). `pytest.ini` in the repository root sets
them up to print one line per test.

## The test suites

| Suite | Command | Needs | In CI? |
|---|---|---|---|
| Version check | `python tools/generate_game_versions.py --check` | Python | Yes |
| Python tests | `python -m pytest` | Python 3.11+ and `tests/requirements.txt` | Yes |
| Launcher tests | `dotnet run --project tests/Fable2.Launcher.ConfigTests -c Release` | .NET 8 SDK | Yes |
| Native tests | `tests\run_native_tests.cmd` | Windows, a VS x64 developer shell, Clang and the patched SDK source | No |

CI runs on Windows.

## What each suite checks

### Version check

Checks that the generated files in `constants/` match
`constants/game_versions.json`. If it reports stale constants, run it without
`--check` to regenerate them, then commit the result.

### Python tests

| File | What it checks |
|---|---|
| `config/test_config_template.py` | The template in `src/core/fable2_config.cpp`, the launcher's default settings, and the game's own config reader and defaults all match `config/fable2_config.toml`. Each staged value is the kind the game accepts, and each setting is stored under its own name. Each cvar the game seeds from the config is seeded from the setting of the same name, and its default in `src/main.cpp` matches the staged value |
| `tools/repro/test_add_version_resource.py` | The release step that adds version info to a DLL writes the given company, product and version, keeps the existing manifest, moves the sections after `.rsrc` without changing them, and names the version after the file. It leaves a DLL alone when it has no resources, already has version info, or has no room to grow. Two known issues are marked as expected failures |
| `tools/repro/test_clean_pe.py` | The release build's post-build step trims `rexruntime.dll` to what the game and the GPU plugin import plus the `rex::` API, keeps each name pointing at its own function, and pins every timestamp. It reports a broken DLL and carries on, skips a runtime without exports, leaves other DLLs' exports alone, and changes nothing on a second run |
| `tools/repro/test_gen_def.py` | The `.def` generator keeps what every consumer imports from the runtime plus the `rex::` API, in sorted order. It drops imports the runtime doesn't export, skips missing consumers, and honors `--tag` |
| `tools/repro/test_normalize_pe.py` | The stable-hash step pins timestamps and the PDB GUID, so two builds come out identical. It leaves files that aren't a PE alone, skips missing files, and exits with 2 on a broken PE. Three known bugs are marked as expected failures |
| `tools/repro/test_trim_exports.py` | The standalone export trimmer keeps only the names in a `.def`, keeps each pointing at its own function, and leaves a file alone when it isn't a PE, has no exports or matches nothing in the `.def` |
| `tools/test_apply_recomp_patches.py` | The patch step applies each patch once, fails on a missing anchor and honors its on/off switch. One known issue is marked as an expected failure |
| `tools/test_fable2_control.py` | Each command sends the request the server expects, `state` strips spaces from button names and keeps values of 0, a token login goes first and stops on a bad token, and connection errors and timeouts are reported as JSON. One known issue is marked as an expected failure |
| `tools/test_game_versions.py` | Generated version files match the catalogue, invalid or duplicate entries are rejected, the default profile must be compatible and in the validated code group, only those versions get a CMake profile, and `--check` reports stale files. One known issue is marked as an expected failure |
| `tools/test_make_hotfunc_overrides.py` | The override generator extracts and transforms functions correctly and never overwrites a hand-tuned file. Four known issues are marked as expected failures |
| `tools/test_manifest_collisions.py` | The check for manifest names that clash with DLL exports reports every clash, ignores commented-out names, rejects a missing, unreadable or empty DLL, and the real manifest avoids the kernel wait names that once broke audio. One known issue is marked as an expected failure |
| `tools/test_mouse_log_format.py` | The lines the game writes to `fable2_mouse.log` (poll lines from `src/input/mouse_diag.h`, camera updates from `mouse_diag.lua` with the direct mouse camera's turn, and the `f2 mouse inject` and `f2 mouse mode` notes) are read the way `mouse_stats.py` expects. Reads the format strings from the source, so a renamed field fails here |
| `tools/test_mouse_stats.py` | The `fable2_mouse.log` summariser splits the log into one run per `f2 mouse inject`, counts polls and camera updates, works out how much movement the camera saw with and without smoothing, scores the steady yaw per update while leaving out the ends of a run, adds up the total turn (stick and direct mouse camera) with the share that coasted on after the input stopped and how fast the turn got going, labels each run with its speed profile and mouse mode, ends a run early at its `f2 mouse stop` note, and prints the text and JSON reports |
| `tools/test_package_release.py` | The release zip holds only the listed files, its build info is stamped with the version while the build folder's copy stays unstamped, symbols stay separate, checksums match, files from an earlier package are removed, and a missing file, symbol file or recomp Lua script stops packaging before anything is written |
| `tools/test_prepare_renderer_mspack.py` | The libmspack fallback replaces symlinks that Windows checked out as text files with their targets' contents, and reports how many it replaced. It leaves ordinary files and real symlinks alone, and also works on a second run, a deleted file or a trailing newline. It stops when the checkout isn't at the public pin, a target is missing or outside the checkout, or a file has local edits. Uses small throwaway git repos |
| `tools/test_prepare_runtime_sdk.py` | The SDK prepare step rejects a missing or non-git folder and a checkout without libmspack, skips setup with `--skip-dependencies`, marks libmspack stored as plain files as set up (also on a second run), repins libmspack stored as a submodule, and says a failed submodule update needs network access. Uses small throwaway git repos. Two known issues are marked as expected failures |
| `tools/test_runtime_staging.py` | Debug and Release builds stage the SDK files, and staging fails before copying if an input is missing |

To run one file:

```sh
python -m pytest tests/config/test_config_template.py
```

To see how much of `tools/` the tests cover, as CI does:

```sh
python -m pytest --cov=tools --cov-report=term
```

These tests use fake files in temporary folders, and their folders mirror the
code they cover. A new `test_*.py` file is picked up automatically. A new
subfolder needs an empty `__init__.py`, so its test files don't clash with
same-named ones in other folders.

The tests are written as specs with
[pytest-describe](https://github.com/pytest-dev/pytest-describe):

```python
def describe_trim():
    def test_should_keep_only_the_names_in_the_def(dll, def_file):
        ...
```

- Each `describe_<function>()` block holds the tests for one function of the
  tool, and pytest-spec prints them as sentences under that heading.
- Each test is named `test_should_<behavior>`, and its body stays flat:
  arrange, act, assert, with no loops or branches.
- Temporary files come from pytest's `tmp_path` fixture, and swapped module
  values from `monkeypatch`, so both are undone after each test.
- A confirmed bug gets a test for the correct behavior, marked
  `@pytest.mark.xfail(strict=True, reason="...")`. With `strict`, the test
  fails once the bug is fixed, as a reminder to remove the marker.

### Launcher tests

`Fable2.Launcher.ConfigTests/Program.cs` covers the launcher's settings files,
graphics and FPS options, dropdown styling, game version checks, the game
folder check, the executable and folders a launch uses, update checks and ISO
extraction from a synthetic disc image. It prints a line for each group
that passes. The project targets plain .NET 8, but CI only runs it on Windows.

### Native tests

`native/` holds C++ tests for audio pacing, FPS limiting, FPS metering,
keyboard and mouse map parsing, mouse look (stick mapping, the velocity window
and the diagnostics' inject and summary maths), the direct mouse camera (turn
per count, when the mouse goes to the camera or the stick, the retries that
start `mouse_direct.lua`, the numbers it returns to Lua, the fallback for a
NaN or infinite setting, and the inject speed profiles), and the version
catalogue in each build profile.
Test executables go under `out/tests`.

A native test has no expected-failure marker like pytest's, so a confirmed bug
is checked with `KNOWN_BUG(condition, reason)` instead of `CHECK`. It prints
`KNOWN BUG` with the reason while the bug is there, and fails once the bug is
fixed, as a reminder to turn it into a `CHECK`.

The runner uses `thirdparty/rexglue-sdk` by default, and an optional first
argument selects a different SDK source folder. If that folder is empty, run
`git submodule update --init thirdparty/rexglue-sdk` first.

## Optional: checking your own game files

You can point these tests at your own files for local checks. Never commit
these files.

```cmd
dotnet run --project tests/Fable2.Launcher.ConfigTests -c Release -- <game folder>
out\tests\runtime-unit-tests\xex-goty-compatible.exe <path to default.xex>
```

Run `tests\run_native_tests.cmd` first to build the second one.
