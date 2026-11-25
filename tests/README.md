# Tests

These tests check the launcher, the build tools and some runtime code. None of
them need game files, a GPU or an audio device.

The known bugs behind the strict xfails, the coverage gaps and the quirks of
each suite are written up in
[docs/feature/test_suite/technical.md](../docs/feature/test_suite/technical.md).

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
| Native tests | `tests\run_native_tests.cmd` | Windows, Clang with the Visual Studio C++ tools installed, and the SDK submodule | Yes |

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
| `core/test_recomp_scripts_startup.py` | The recomp Lua scripts are copied into a separate game folder (`--game_data_root`) from `OnPostInitLogging` and nowhere later. The SDK lists the game folder once, when it mounts it right after that hook, so a script copied later can't be opened by the game until the next start. Reads the C++ source |
| `core/test_world_thread_runscript.py` | Every host RunScript (`f5lua::run_external`) is reached only from `console::service_world_thread`, which runs at the start of a world update on the world's own thread. That function is wired only to photo mode's world-update hook, inside the scope the runtime gate checks, and the MainRenderLoop hook `console::poll` starts no script. RunScript from any other thread corrupts the game's script engine (the 1.6 alt-tab crash, #189). Reads the C++ source and prints the call chain that breaks the rule |
| `lua/test_recomp_scripts.py` | No recomp Lua script registers itself with the game's GeneralScriptManager, which writes its running scripts into save games, except the F5 menu, which ends itself when the menu closes. Only the F5 menu and the console's save cleanup touch the GeneralScriptManager |
| `tools/repro/test_add_version_resource.py` | The release step that adds version info to a DLL writes the given company, product and version, keeps the existing manifest, moves the sections after `.rsrc` without changing them, and names the version after the file. It leaves a DLL alone when it has no resources, already has version info, or has no room to grow. Two known issues are marked as expected failures |
| `tools/repro/test_clean_pe.py` | The release build's post-build step trims `rexruntime.dll` to what the game and the GPU plugin import plus the `rex::` API, keeps each name pointing at its own function, and pins every timestamp. It reports a broken DLL and carries on, skips a runtime without exports, leaves other DLLs' exports alone, and changes nothing on a second run |
| `tools/repro/test_gen_def.py` | The `.def` generator keeps what every consumer imports from the runtime plus the `rex::` API, in sorted order. It drops imports the runtime doesn't export, skips missing consumers, and honors `--tag` |
| `tools/repro/test_normalize_pe.py` | The stable-hash step pins timestamps and the PDB GUID, so two builds come out identical. It leaves files that aren't a PE alone, skips missing files, and exits with 2 on a broken PE. Three known bugs are marked as expected failures |
| `tools/repro/test_trim_exports.py` | The standalone export trimmer keeps only the names in a `.def`, keeps each pointing at its own function, and leaves a file alone when it isn't a PE, has no exports or matches nothing in the `.def` |
| `tools/test_apply_recomp_patches.py` | The patch step applies each patch once, fails on a missing anchor and honors its on/off switch. One known issue is marked as an expected failure |
| `tools/test_fable2_control.py` | Each command sends the request the server expects, `state` strips spaces from button names and keeps values of 0, a token login goes first and stops on a bad token, and connection errors and timeouts are reported as JSON. One known issue is marked as an expected failure |
| `tools/test_game_versions.py` | Generated version files match the catalogue, invalid or duplicate entries are rejected, the default profile must be compatible and in the validated code group, only those versions get a CMake profile, and `--check` reports stale or missing files. Without `--check` it writes them, with LF line endings even on Windows. One known issue is marked as an expected failure |
| `tools/test_make_hotfunc_overrides.py` | The override generator extracts and transforms functions correctly and never overwrites a hand-tuned file. Four known issues are marked as expected failures |
| `tools/test_manifest_collisions.py` | The check for manifest names that clash with DLL exports reports every clash, ignores commented-out names, rejects a missing, unreadable or empty DLL, and the real manifest avoids the kernel wait names that once broke audio. One known issue is marked as an expected failure |
| `tools/test_merge_dir_manifest.py` | Content staging's `dir.manifest` merge copies the disc manifest when none is staged, and otherwise keeps every disc line in disc order (repeats included) followed by the lines only the staged manifest has, each written once. Every line is written with CRLF, whatever the input used and even without a final line ending; blank lines are dropped and names in any encoding pass through. An empty disc manifest keeps the staged lines, a second run changes nothing, and no temporary file is left behind, even one from an earlier run. A missing disc manifest or the wrong arguments fail |
| `tools/test_mouse_log_format.py` | The lines the game writes to `fable2_mouse.log` (poll lines from `src/input/mouse_diag.h`, camera updates from `mouse_diag.lua` with the direct mouse camera's turn, and the `f2 mouse inject` and `f2 mouse mode` notes) are read the way `mouse_stats.py` expects. Reads the format strings from the source, so a renamed field fails here |
| `tools/test_mouse_stats.py` | The `fable2_mouse.log` summariser splits the log into one run per `f2 mouse inject`, counts polls and camera updates, works out how much movement the camera saw with and without smoothing, scores the steady yaw per update while leaving out the ends of a run, adds up the total turn (stick and direct mouse camera) with the share that coasted on after the input stopped and how fast the turn got going, labels each run with its speed profile and mouse mode, ends a run early at its `f2 mouse stop` note, and prints the text and JSON reports |
| `tools/test_package_release.py` | The release zip holds only the listed files, its build info is stamped with the version while the build folder's copy stays unstamped, symbols stay separate, checksums match, files from an earlier package are removed, and a missing file, symbol file, launcher, repo config, recomp Lua script or keycap font stops packaging before anything is written (the build folder's own config never stands in). `main()` passes the given version, or the one from `git describe --tags`, with the standard build, launcher and output folders |
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
NaN or infinite setting, and the inject speed profiles), the RunScript gate
(scripts only run inside the world-update service, per thread; see
`src/core/world_thread.h`), the GUI bank file writes (a bank is replaced in
full or left as it was; see `src/gui/bank_file.h`), and the version catalogue
in each build profile.
Test executables go under `out/tests`.

`test_recomp_scripts.cpp` covers the copy of the recomp Lua scripts into a
separate game folder (`src/core/recomp_scripts.h`), including the console's
`console_run.lua`, which is only copied when the game folder has none.

They use [Catch2](https://github.com/catchorg/Catch2) v3, from the SDK's own
`thirdparty/catch2`, and read like the Python specs:

```cpp
TEST_CASE("DirectState", "[mouse][direct]") {
  mdir::DirectState st;  // runs again, fresh, for each SECTION

  SECTION("drops kept movement on Clear") {
    ...
    CHECK(dx == 0);
  }
}
```

- Each `TEST_CASE` covers one function or type, and each `SECTION` in it is
  one behaviour, named as a sentence. Catch2 runs the test case once per
  section, so the setup above the sections is fresh each time.
- The run prints each test case with its sections marked `ok` or `FAIL`, like
  pytest-spec (`native/catch_spec.cpp`). Catch2 then prints each failed check
  with its values, such as `dx == 0` with expansion `3.0 == 0`, and a summary.
- Write one comparison per `CHECK`: Catch2 can't show the values of `a && b`.
  Use `CHECK_THAT(x, WithinAbs(want, tolerance))` for floating point.
- A confirmed bug gets a `TEST_CASE` tagged `[!shouldfail]` that checks the
  correct behaviour, the native form of pytest's strict xfail. It's marked
  `known bug` while it fails, and the run fails once the bug is fixed, as a
  reminder to remove the tag.
- To run one test case or section, pass its name or tag to the executable, for
  example `out\tests\runtime-unit-tests\mouse-direct.exe "DirectState"` or
  `mouse.exe "[stick]"`. `-?` lists the options.

The runner uses `thirdparty/rexglue-sdk` by default, and an optional first
argument selects a different SDK source folder. The tests need the SDK's source
and headers, unpatched, plus four of its own submodules (fmt, simde, spdlog and
Catch2). If they're missing, fetch only those, as CI does:

```sh
git submodule update --init --depth 1 thirdparty/rexglue-sdk
git -C thirdparty/rexglue-sdk submodule update --init --depth 1 thirdparty/fmt thirdparty/simde thirdparty/spdlog thirdparty/catch2
```

Clang 22 or newer must be on `PATH` (for example `C:\Program Files\LLVM\bin`):
the STL of MSVC 14.52 and later refuses older Clang with error STL1000. CI
installs a pinned LLVM for this (see `.github/workflows/tests.yml`). A Visual
Studio developer shell isn't needed.

To see how much of the project's own code (`src/`, `constants/`) the native
tests run, as CI does, set `FABLE2_COVERAGE=1`:

```cmd
set FABLE2_COVERAGE=1
tests\run_native_tests.cmd
```

The tests are then built with clang's source coverage into
`out\tests\runtime-unit-tests-coverage`, and the script prints line, function
and branch coverage per file. It also writes an HTML report with each line's
count to `coverage-html\index.html` in that folder; CI shows the printed report
only. A file shows up only when a test includes it, so a header no test touches
isn't listed at 0%.

## Optional: checking your own game files

You can point these tests at your own files for local checks. Never commit
these files.

```cmd
dotnet run --project tests/Fable2.Launcher.ConfigTests -c Release -- <game folder>
out\tests\runtime-unit-tests\xex-goty-compatible.exe --xex <path to default.xex>
```

Run `tests\run_native_tests.cmd` first to build the second one.
