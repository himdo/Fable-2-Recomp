# Test suite: technical notes

**Status:** merged · **Commits:** 8957735 (#166), be11165 (#174), ffa15b3 (#196), c764cba (#197), 5cc6513 (#229) · **Last checked against:** 5f5f55f

These notes record what the test suite knows that the test code doesn't say out loud: known bugs, coverage gaps, quirks and open questions. For how to run the tests and the per-file summaries, see `tests/README.md`.

## How to run

| Suite | Command | Needs |
|---|---|---|
| Version check | `python tools/generate_game_versions.py --check` | Python |
| Python tests | `python -m pytest` (CI adds `--cov=tools --cov-report=term`) | Python 3.11+, `pip install -r tests/requirements.txt` |
| Launcher tests | `dotnet run --project tests/Fable2.Launcher.ConfigTests -c Release` | .NET 8 |
| Native tests | `tests\run_native_tests.cmd` (`FABLE2_COVERAGE=1` for coverage) | Windows, clang ≥ 22 on PATH, VS C++ tools, SDK submodule |

None of them need game files. CI (`.github/workflows/tests.yml`) runs all four on `windows-latest` for every PR and every push to master.

Baseline at 5f5f55f: **429 passed, 1 skipped, 15 xfailed.**

## Conventions

- **Style.** Tests are pytest-describe specs: `describe_<thing>` blocks, `test_should_<behaviour>` names, and inside each block the tests are grouped as happy flow, bad flow, edge cases and known issues.
- **Known bugs are strict xfails.** Each one is a test for the *correct* behaviour, marked `@pytest.mark.xfail(strict=True, reason=...)`. When the bug is fixed the test starts passing, `strict` turns that into a failure, and whoever fixed it removes the marker.
- **Loading tools.** `tools/` isn't a package, so each test file loads its tool with `importlib.util.spec_from_file_location(...)`.
- **The script entry line.** Almost every tool has one line no test reaches: `main()` under `if __name__ == "__main__":`. It is left out of the gap lists below.

## Known bugs (15 xfail tests)

| Tool | Bug |
|---|---|
| `tools/repro/add_version_resource.py` | **`Translation` halves are swapped.** It writes `B0 04 09 04`, which doesn't match its own `040904B0` string table, so a reader using `Translation` looks for a table that doesn't exist |
| | **Resource type entries are unsorted.** Version (16) is appended after manifest (24); PE requires ascending IDs |
| `tools/repro/normalize_pe.py` (2 bugs, 3 tests) | **The PDB path is written as UTF-16 into a UTF-8 field.** Read as UTF-8 it says `f`. Clearing up to a UTF-16 double NUL also zeroes the bytes after the path |
| | **Short paths get cut further on every run** (`fable_2.` → `fable_2` → `fable_`), so the output isn't stable, which defeats the tool's purpose |
| `tools/apply_recomp_patches.py` | An anchor that appears twice in one file only has its first copy replaced, and the run still reports success |
| `tools/fable2_control.py` | A reply that isn't JSON ends in a `JSONDecodeError` traceback, although the tool promises JSON output |
| `tools/generate_game_versions.py` | A `defaultProfile` that names no profile raises a bare `StopIteration` instead of a `ValueError` |
| `tools/make_hotfunc_overrides.py` | **A UTF-8 BOM hides the `// Hand-tuned override` marker**, so the generator would overwrite hand work. The most serious one: Windows editors add BOMs easily |
| | A mistyped function name does nothing and still reports success |
| | `FUNCTIONS` doesn't match the real files: `LookStickProcess` lives in `unsorted/`, `RightStickLookProcess` has no file. A full run would create duplicates |
| | The hand-tuned `ui_rendering` entries are stale (harmless, since hand-tuned entries are skipped) |
| `tools/check_manifest_collisions.py` | The default DLL search folders and names are stale. `build.cmd` installs to `out/tooling/runtime-sdk*/win-amd64/bin` |
| `tools/prepare_runtime_sdk.py` | **An empty repo passes the HEAD check.** `git rev-parse HEAD` fails but prints `HEAD`, and only the output is checked. Fix: check the return code of `git rev-parse --verify HEAD` |
| | **An unfetched submodule passes as a checkout**, because git answers for the parent repo. Fix: compare `git rev-parse --show-toplevel` with the source folder. Both cases end in a misleading "libmspack is missing" |

## Coverage gaps worth knowing

- **add_version_resource:** named (non-numeric) resource types are skipped, so they are dropped from the rebuilt tree; no test has one.
- **clean_pe:** the "pefile is required" exit; trimming giving up on unresolvable export tables; the version step with DLLs not named `rex*`; a normalize step that raises.
- **normalize_pe:** the PE32 (32-bit) header branch; non-CodeView debug entries; CodeView data too small or without `RSDS`; the non-quiet output.
- **make_hotfunc_overrides:** CRLF handling in `extract_body`; `transform_hoist`, which only works on the real `ProcessGameFrame_82276C30` and would need a large copy of generated code to test.
- **manifest_collisions:** a DLL with no export directory at all (the test uses one with zero names); a corrupt export table; DLLs in default locations (unreachable because of the known bug).
- **prepare_runtime_sdk:** the "has no commits" failure (unreachable because of known bug 1); running `prepare_renderer_mspack.py` after a real submodule update (needs network).
- **fable2_control:** the real socket in `conn()`; every test replaces it.
- **package_release:** `main()` argument parsing and the `git describe --tags` default version.
- **game_versions:** the write branch of `main()` without `--check`.
- **config_template:** value ranges (e.g. a negative scale), and whether the launcher reads every staged key.
- **Launcher:** no WPF code (windows, code-behind) is tested, only the linked logic classes.

## Quirks

- **`tests/config/test_config_template.py` parses C++ and C# with regexes.** A code-style change can break it without any behaviour change. Two guards make that fail loudly: the read-call count and the launcher property-set comparison. It checks that four places agree on every setting: `config/fable2_config.toml`, the `kTemplate` in `src/core/fable2_config.cpp`, `launcher/Fable2.Launcher/Fable2ConfigFile.cs` and the cvar defaults in `src/main.cpp`.
- **`test_should_match_the_committed_generated_files`** fails if the game-version catalogue is edited without regenerating. That's intended.
- **`make_hotfunc_overrides` tests patch `ROOT`**, because `os.path.relpath` fails across drives on Windows.
- **One `prepare_renderer_mspack` test is skipped on Windows** (it needs symlinks).
- **The launcher test program stops at the first failing `Require`**, so one failure hides later ones. Its output lines don't map one to one to its 14 groups: group 8's checks print under "Extraction hash gate", group 3's anti-aliasing checks under the frame-rate line.
- **Bad-flow tests in `game_versions`** assert `ValueError` without `match=`, so a different validation firing first would still pass.
- **pytest-spec display:** a `describe_main` that follows another file's `describe_main` prints without a heading, and test names starting with `describe` lose that word. The code is fine.

## Open questions for the maintainer

- `clean_pe` crashes on a consumer with no import table, and reports a non-PE file as "already normalized".
- Are `tools/repro/gen_def.py` and `tools/repro/trim_exports.py` still used, now that `clean_pe.py` does both jobs?
- `prepare_renderer_mspack`: a folder that isn't a git repo ends in a `CalledProcessError` traceback, and there's no rollback when one symlink target is invalid.
- `fable2_control`: `script --file` with a missing file and invalid JSON passed to `raw` end in tracebacks; should they print a JSON error? Whether `RT` works as a trigger name is decided in C++ (`HandleState`), which a Python test can't reach.
- `add_version_resource`: `read_sections` reads `SectionAlignment` (offset 32) where it means `FileAlignment` (offset 36); no `VS_FIXEDFILEINFO` block is written; two `open(...)` calls are never closed (`ResourceWarning`).
- `apply_recomp_patches`: a failed patch keeps the earlier ones (no rollback). The test documents this; it doesn't say it's desired.

## Not covered by these notes

Suites added after the restyle: `tests/tools/test_merge_dir_manifest.py`, `test_mouse_log_format.py`, `test_mouse_stats.py`, `tests/core/*`, `tests/lua/*` (`test_recomp_scripts*.py`, `test_world_thread_runscript.py`) and the 15 Catch2 files in `tests/native/`.
