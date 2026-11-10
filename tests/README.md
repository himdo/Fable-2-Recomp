# Tests

These tests check the launcher, the build tools and some runtime code. None of
them need game files, a GPU or an audio device.

## Quick start

From the repository root, with Python 3.11 or later and nothing else to install:

```sh
python tools/generate_game_versions.py --check
python -m unittest discover -s tests -p "test_*.py"
```

If `python` isn't found, use `python3`. Keep the quotes around `"test_*.py"`,
because zsh rejects the pattern without them.

## The test suites

| Suite | Command | Needs | In CI? |
|---|---|---|---|
| Version check | `python tools/generate_game_versions.py --check` | Python | Yes |
| Python tests | `python -m unittest discover -s tests -p "test_*.py"` | Python 3.11+ | Yes |
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
| `config/test_config_template.py` | The template in `src/core/fable2_config.cpp` and the launcher's default settings both match `config/fable2_config.toml` |
| `tools/repro/test_normalize_pe.py` | The stable-hash step pins timestamps and the PDB GUID, so two builds come out identical. Three known bugs are marked as expected failures |
| `tools/test_apply_recomp_patches.py` | The patch step applies each patch once, fails on a missing anchor and honors its on/off switch. One known issue is marked as an expected failure |
| `tools/test_fable2_control.py` | Each command sends the request the server expects, `state` strips spaces from button names and keeps values of 0, a token login goes first and stops on a bad token, and connection errors are reported as JSON |
| `tools/test_game_versions.py` | Generated version files match the catalogue, invalid or duplicate entries are rejected, the default profile must be compatible and in the validated code group, only those versions get a CMake profile, and `--check` reports stale files. One known issue is marked as an expected failure |
| `tools/test_make_hotfunc_overrides.py` | The override generator extracts and transforms functions correctly and never overwrites a hand-tuned file. Four known issues are marked as expected failures |
| `tools/test_manifest_collisions.py` | The check for manifest names that clash with DLL exports reports clashes and bad input, and the real manifest avoids the kernel wait names that once broke audio |
| `tools/test_package_release.py` | The release zip holds only the listed files, its build info is stamped with the version, symbols stay separate, checksums match, and a missing file stops packaging |
| `tools/test_prepare_runtime_sdk.py` | The SDK prepare step rejects a missing or non-git folder and a checkout without libmspack, skips setup with `--skip-dependencies`, and marks libmspack stored as plain files as set up. Uses small throwaway git repos. Two known issues are marked as expected failures |
| `tools/test_runtime_staging.py` | Debug and Release builds stage the SDK files, and staging fails before copying if an input is missing |

To run one file:

```sh
python -m unittest discover -s tests -p "test_config_template.py"
```

These tests use fake files in temporary folders, and their folders mirror the
code they cover. A new `test_*.py` file is picked up automatically, but a new
subfolder needs an empty `__init__.py`, or its tests are silently skipped.

### Launcher tests

`Fable2.Launcher.ConfigTests/Program.cs` covers the launcher's settings files,
graphics and FPS options, dropdown styling, game version checks, update checks
and ISO extraction from a synthetic disc image. It prints a line for each group
that passes. The project targets plain .NET 8, but CI only runs it on Windows.

### Native tests

`native/` holds C++ tests for audio pacing, FPS limiting, FPS metering,
keyboard and mouse map parsing, and the version catalogue in each build profile.
Test executables go under `out/tests`.

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
