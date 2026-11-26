# Manifest naming check: technical notes

**Status:** merged · **Commits:** 2f46d83 (#249) · **Last checked against:** c2a9d12

Labels: **Verified** means read in the repo or measured with the tool. **Inference** means reasoned, not tested.

## Goal

CLAUDE.md asks for every game function a PR touches to get a name shaped like `Subsystem_WhatItDoes_ADDRESS`, with a comment above its manifest entry saying what it does and how that is known. Until now only the maintainer's review enforced that. `tools/check_manifest_collisions.py` checks names, but only against the runtime DLL's exports, so it needs a Windows build and CI can't run it.

The goal is a check that needs nothing but `fable_2_manifest.toml`, so it runs on any machine and on every pull request. It came out of the function-mapping research (`docs/feature/function_mapping/`, in the research-notes PR), which proposed it as a second mode of the collision check.

## Design

A separate tool, `tools/check_manifest_names.py`, rather than a mode of `check_manifest_collisions.py`. That tool's arguments are about finding the DLL, and the two checks share no code.

### Rules

| Rule | Checks | Applies to | Master at 7863041 (Verified) |
| --- | --- | --- | ---: |
| suffix | A name that ends in 8 hex digits ends in the entry's own address | every named entry | 0 fail |
| unique | No two entries share a name | every named entry | 0 fail |
| generic | The name isn't a placeholder (`sub_…`, `ProcessAndProcessAndProcessN_…`) | new and renamed entries | 2,642 older entries |
| format | The name matches `^[A-Za-z][A-Za-z0-9]*(_[A-Za-z0-9]+)*_[0-9A-F]{8}$` | new and renamed entries | 56 older entries |
| comment | The line directly above the entry's header is a comment | new and renamed entries | 381 older entries |

- The last three can't apply to every entry yet, or the check would fail on day one. Some of the 56 badly shaped names are deliberate, too: `KeWaitForSingleObject_Guest` and `KeWaitForMultipleObjects_Guest` must keep those names (see the `_Guest` note in CLAUDE.md), and `tests/tools/test_manifest_collisions.py` checks them.
- The generic rule isn't in the research proposal. It comes from CLAUDE.md: a PR that adds an entry should give it a real name.
- **comment** only checks that a comment is there, not what it says. A section heading followed by a blank line doesn't count.
- An entry without a `name` (it only marks where a function starts, like issue #38's pressure-plate entry) is skipped by every rule.
- There's no evidence-label rule. The research proposes labels E0–E5, but the project hasn't adopted them, so there's nothing to check yet.

### Two ways to run it

- **`--base <ref>`:** checks suffix and unique on every entry, and the other three on the entries that are new since `<ref>` or have a different name there. It fails (exit 1) if any rule is broken. An entry whose comment changed but whose name didn't isn't checked.
- **No `--base`:** checks suffix and unique, and prints how many older entries break each of the other rules. That count is the naming debt still to pay off.

Exit codes follow the collision check: 0 clean, 1 a rule broken, 2 a usage or IO error (missing manifest, unknown git ref).

The tool reads the manifest line by line: a `[entrypoint.functions."0x…"]` header, then the first `name = "…"` before the next table. It ignores `[[entrypoint.midasm_hook]]` tables, whose names are hook names, not function names. It gets the base version with `git show <ref>:fable_2_manifest.toml`.

### CI

`.github/workflows/tests.yml` checks out two commits (`fetch-depth: 2`) and runs `python tools/check_manifest_names.py --base HEAD~1` after the version-file check.

- **On a pull request** GitHub tests its merge of the branch into master, so `HEAD~1` is master and only the PR's own entries are checked (Verified by simulating the merge locally, see below).
- **On a push to master** `HEAD~1` is the commit before it, so a squash-merged PR is checked once more.
- GitHub doesn't run pull-request checks on a branch that conflicts with master, so a conflicting branch is only checked after it's brought up to date.

## What changed

- `tools/check_manifest_names.py`: the new tool.
- `tests/tools/test_manifest_names.py`: 33 tests covering parsing, both rule groups, which entries count as changed, both ways to run it, and the real manifest. The `--base` tests use small throwaway git repos with commit signing switched off.
- `.github/workflows/tests.yml`: `fetch-depth: 2` and the new step.
- `tests/README.md`, `CLAUDE.md`, `.claude/skills/name-functions/SKILL.md`, `.claude/skills/run-tests/SKILL.md`: the new check in their lists of checks and suites.

No functions were named or renamed.

## Verification

All Verified, on macOS (Apple Silicon):

| Check | Result |
| --- | --- |
| `python -m pytest -q` before | 430 passed, 15 xfailed |
| `python -m pytest -q` after | 463 passed, 15 xfailed (the 33 new tests) |
| The tool on master, no `--base` | exit 0; reports 2,642 placeholders, 56 badly shaped names, 381 entries without a comment |
| The tool on master, `--base origin/master` | exit 0; 0 entries new or renamed |
| `fix/docs-last-checked` merged into master, `--base HEAD~1` (what CI would run on that PR) | exit 0; its 4 new names checked |
| The manifest of `feature/game-difficulty` merged into master, checked against master | 67 new or renamed entries, 0 problems |

On Windows (26 Nov 2026, at c2a9d12): without `--base` the tool exits 0 with 5,080 named functions and the same 2,642 / 56 / 381 counts. The CI step has passed on every push to master since #249 merged (run 45696294630 onwards).

## Not tested

- Nothing open: the step runs in CI on GitHub, and the tool runs on Windows by hand.

## Follow-ups

- Rename the 56 badly shaped names (all but the two `_Guest` ones), then apply the format rule to every entry.
- Once evidence labels are adopted, add a rule that new entries carry one.
- Report the naming debt over time (placeholders, descriptive-name coverage), as the function-mapping research proposes.
