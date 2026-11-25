# A repeatable function-mapping process: technical notes

**Status:** no commit yet (research only) · **Commits:** none · **Last checked against:** 7863041

Claims are labelled **Verified** (checked in this repo or a primary source), **Reported** (a source says so, not checked here) or **Inference** (reasoned, not tested). This came out of the [November 2026 research](../../reference/research/README.md).

## Goal

Turn manifest naming into an evidence-graded, repeatable process, so names stay trustworthy as more people (and AI agents) add them. It builds on the `Subsystem_WhatItDoes_ADDRESS` rule in `CLAUDE.md` and the `/name-functions` skill. The one naming pass done so far is [function_naming](../function_naming/technical.md); why AI-proposed names need external evidence is in [ai_assisted_naming](../ai_assisted_naming/technical.md).

## Findings

### What PR #238 showed

**Verified** ([function_naming](../function_naming/technical.md)): PR #238 (delta4o, merged by himdo) went through master's last 40 commits and named the 22 functions they call, hook or describe.\[1\] Examples:

- Photo mode: `LuaDebug_SetDrawGUI_823AC520`, `LuaTiming_SetTimeOfDayAbsolute_8249A790`.
- Frame and GUI: `PresentFrame_82B69488`, `GameFrame_HfTickUpdate_82278C90`, `GuiManager_Update_82286B40`, `Timer_GetElapsed_822B6D70`.
- Cloth and pose: `Cloth_Step_8219ADD8`, `Pose_BlendForDraw_8222E300`.

The techniques it used:

- reading each body in the generated code;
- matching the game's Lua commands in `FABLE2_LUA_API.md` to their native bodies;
- following call chains (`PresentFrame_82B69488` → `MainRenderLoop_82B9CD68` → `VdSwap`);
- reusing another contributor's branch notes;
- flagging names based only on instruction patterns as least certain.

**Reported:** himdo's review said the PR "improves readability and reusability a ton". The rule is now policy: the README, plus `CLAUDE.md` and `/name-functions` from PR #240, which "follows the same process as… #238".\[2\]\[3\] There is no standard for measuring mapping progress in the 360 recomp scene.

### Where the manifest stands (Verified, measured at 7863041)

Counted with a short Python script over `fable_2_manifest.toml` on master (not committed):

| What | Count |
| --- | ---: |
| `[entrypoint.functions."0x…"]` entries | 5,078 (2 have no `name`) |
| `ProcessAndProcessAndProcess<N>_…` placeholders | 2,642 (52%) |
| Shape-only names from the instruction pattern: `Func_` 137, `SwitchDispatch_` 72, `TailCall_` 72, `IndirectCall_` 8, `VcallSlot3_` 7, `VcallChain_` 7, `ReturnConstant_` 7, `RetAddr_` 6, `VtableSlotX3_` 5 | 321 |
| Other names that start with `Process` (for example `ProcessAndCallMethods_82BFCD38`), most from the same automatic pass | 1,013 |
| Everything else (927 distinct prefixes; the most common are `Tree_`, `Story_`, `LoadingScreen_`, `UIText_`, `FrontMenu_`, `Cloth_`) | 1,100 |
| Lua binding names (`Lua<Library>_<Binding>`) | 5 |
| Names without the `_ADDRESS` suffix (for example `Story_FirstChildCombat`, `LoadingScreen_Virtual43`, `GetNewGameLoadingGlobal`) | 56 |
| Names whose suffix doesn't match the entry's address | 0 |
| Duplicate names | 0 |
| Entries with a comment block directly above | 4,697 (381 have none) |
| Comment lines that use the words **Verified** or **Inference** | 0 and 1 |
| `[[entrypoint.midasm_hook]]` entries | 19 |

What this means:

- **No entry is called `sub_…`.** Codegen gives `sub_<address>` only to functions that have *no* manifest entry, so every `sub_` name in `generated/` is a function nobody has listed yet. The E0 level below covers both those and the placeholders.
- **The placeholders came in one batch and have barely moved since.** The manifest's history (`git show <commit>:fable_2_manifest.toml`):

  | Commit | Date | Entries | Placeholders |
  |---|---|---:|---:|
  | d076fda (first) | 2026-09-02 | 99 | 0 |
  | 1522cfd | 2026-09-16 | 998 | 0 |
  | 9743b4e | 2026-09-21 | 4,842 | 2,499 |
  | b96896b | 2026-10-08 | 5,045 | 2,656 |
  | 9b3f5b0 | 2026-11-05 | 5,052 | 2,651 |
  | f451b72 (#238) | 2026-11-21 | 5,077 | 2,642 |
  | 7863041 (master) | 2026-11-25 | 5,078 | 2,642 |

  So 14 placeholders have been resolved in seven weeks. **Inference:** the debt only shrinks when a PR needs a specific function, which is why the priority list below matters.
- **Most comments are old automatic descriptions,** for example "This is the 'process this value' routine, part of the 0x82BFF process cluster", with callee lists that still say `sub_…`. They describe the shape, not the purpose, and none carries an evidence label.
- **The total number of functions isn't recorded anywhere in the repo.** The research's ~60,700 (**Reported**) couldn't be checked: `generated/` isn't committed apart from `generated/rexglue.cmake`, and no tool or doc on master writes the count down. A build log or a count of `DEFINE_REX_FUNC` in `generated/default/` on a Windows build would settle it.

### Tooling that already exists (Verified)

- **`tools/check_manifest_collisions.py`** reads every `name = "…"` line with a regex and compares it with the PE exports of the runtime DLL. It fails (exit 1) when a name equals an exported `__imp__<name>` or a bare export, because that silently routes kernel import thunks into the guest body (the audio breakage it was written for). Exit 2 is a usage or IO error. It checks nothing else: not the format, not the address suffix, not comments. Its tests are in `tests/tools/test_manifest_collisions.py` (`describe_pe_exports`, `describe_main`, and `describe_the_real_manifest`, which keeps the kernel-wait names free).
- **`.claude/skills/name-functions/SKILL.md`** already asks for two labels, **Verified** (read in the generated code or seen at runtime) and **Inference** (instruction shape or call site only). These map onto E2/E4 and E1 below.
- **`FUNCTION_NAMES.md`** documents the first naming pass: 520 names, each with a high/medium/low **confidence** grade, an earlier version of the same idea. Its data files are still in `tools/`: `final_names.json` (398 entries: 27 high, 199 medium, 172 low confidence; categories `body` 167, `tailcall` 75, `switchdisp` 73, vtable 64, others 19), `classified.json` (the same 398 with their first and last instruction; 316 in the SDK runtime range, 82 in game code), `story_overrides.json` (19 story-beat names) and `manual_overrides.json` (4). No script in the repo reads them any more.
- **`docs/reference/analysis/`** holds the raw output of a function-seed scan: `scan_branch_targets.txt` (339,207 branch-target addresses), `scan_new_candidates.txt` (402 candidate function starts) and `fable2_seed_candidates.json` (10 addresses).
- **`FABLE2_LUA_API.md`** lists the game's Lua API from the decompiled script banks (1,139 files): 81 engine global functions, 2,004 engine global tables and members, 98 engine metatable/instance methods, and 65 engine functions registered but never called by the shipped scripts. It records **names only, no native addresses**, so it isn't an E3 anchor on its own: someone still has to find each binding's native body. `docs/reference/lua_call_shapes.md` (generated by `tools/lua_callshapes.py`) adds how many arguments each call passes.
- **`tools/make_hotfunc_overrides.py`** copies recompiled functions into `src/core/hotfunc/<category>/` as strong overrides. There are 25 override files, 18 of them hand-tuned. Each override is named after its function, so overriding a function is another reason to name it first.

## Design (proposed)

### Evidence levels

| Level | Meaning | Minimum evidence recorded in the manifest comment |
| --- | --- | --- |
| E0 | Placeholder (`sub_X`, `ProcessAndProcessAnd…`) | none |
| E1 | Pattern guess (instruction shape, size, constants) | "pattern: …" |
| E2 | Static read (body read, callers and callees named, arguments described) | callers/callees, argument meaning |
| E3 | Anchored (Lua binding name from `FABLE2_LUA_API.md`, debug/assert string, vtable slot of a known class) | the anchor, quoted |
| E4 | Runtime-verified (a hook or probe observed the expected values in game, or an on/off A/B diff changed the predicted behaviour) | scenario, observed values, commit |
| E5 | Regression-tested (an automated scenario through the remote-control server asserts the behaviour) | scenario file name |

Rules:

- Only E3 and above may become "public API" names that features depend on.
- E1 names carry a `?` in the comment (not in the symbol).
- A rename of an existing name must cite stronger evidence than the original.

How today's names would grade (**Inference**): the 2,642 placeholders are E0. The 321 shape names and most of the 1,013 other `Process…` names are E1. The `FUNCTION_NAMES.md` confidence grades map roughly as high → E2, medium/low → E1. The 22 names from #238 are E2 or E3, as its "Verified"/"Branch note"/"Inference" column shows.

### Naming conventions

Keep `Subsystem_WhatItDoes_ADDRESS` (for example `Cloth_Step_8219ADD8`),\[2\] and:

- Use `Lua<Library>_<Binding>` for native Lua bindings.
- Use `<Class>_<Method>` when a vtable or class is known.
- Normalise the 56 older names without the address suffix (for example `FrontMenu_Populate`, `Story_FirstChildCombat`) when you touch them.
- Never encode a guess as fact. Prefer `Combat_ApplyDamageCandidate_…` over a confident wrong name.

### A convention check in `tools/check_manifest_collisions.py` (proposed)

A second mode that needs no DLL, so it can run in CI (which never builds the game). On master today (**Verified** counts, measured as above) it would find:

| Rule | Fails today |
| --- | ---: |
| Name matches `^[A-Za-z][A-Za-z0-9]*(_[A-Za-z0-9]+)*_[0-9A-F]{8}$` | 56 |
| The suffix equals the entry's address | 0 |
| Names are unique | 0 |
| A comment block sits above the entry | 381 |
| The comment carries an evidence label | almost all |

**Inference:** run the last two rules only on entries a branch adds or renames (compare with `master`), or the check fails on day one. The first three could be enforced everywhere once the 56 are renamed. New tests would go in `tests/tools/test_manifest_collisions.py`, in the existing `describe_…` style.

### Ways to find candidates, best yield first

1. **Lua binding tables:** the registration code maps names to native function pointers, which gives an E3 anchor for each binding. `FABLE2_LUA_API.md` supplies the names (81 global functions and 98 instance methods, plus the tables' members) but no addresses (**Verified**, see above). Only 5 manifest names on master are Lua bindings. **Verified:** the shared LuaPlus bound-method dispatcher that every Lua→C++ method call goes through is probed by the F5 runner in `src/core/fable2_f5_lua.h`. Its manifest name `LuaBind_RunScript_82806168` is too narrow, as that file's own comment says. Walking from that dispatcher to the registration tables would produce the binding→address map.\[2\]
2. **Cross-references outward from already-named functions** (the call-chain method of #238).
3. **Debug, assert and format strings** referenced in the binary.
4. **Vtables:** group functions by vtable slot, then name the class from one anchored method. 64 of the 398 entries in `tools/final_names.json` are vtable thunks already grouped this way.
5. **Probe logging:** hook a candidate, log arguments and return values to the console, trigger the action in game.
6. **A/B diff:** toggle a hook that perturbs a value (for example ×2) and confirm the predicted gameplay change.
7. **Data-driven anchors:** script `.bnk` Lua calling into native code shows which natives a quest or system uses. `tools/lua_callshapes.py` already walks the bytecode for every call.
8. **Agent assistance:** a Ghidra/IDA MCP agent drafts names and evidence, and a human (or the scenario loop) promotes the level.

### Session template (one subsystem, one PR)

1. **Pick a target** from the priority list and write a one-paragraph question ("Which functions award gold?").
2. **Seed** from Lua bindings and strings, then expand callers and callees two levels.
3. **Probe** each candidate in a debug build and record the observations.
4. **Name**, with an evidence level and a comment covering arguments, effect and how it's known.
5. **Add one scenario file** for the most important function (E5).
6. **Open a PR** with no behaviour change: target, names table, evidence, uncertain names flagged. Run `tools/check_manifest_collisions.py` and the tests.
7. **Update the progress metrics.**

#### Worked example: the difficulty branch (Verified, read-only from `origin/feature/game-difficulty` at bad29df)

The branch followed this template almost step for step before the template existed:

| Step | Commit(s) | What happened |
| --- | --- | --- |
| Target and seed | the branch's `docs/feature/difficulty_settings/technical.md` | The question: where do gold, prices, damage and XP come from? Seeded from the script banks (`tools/bnk.lua`, `tools/lua_callshapes.py`) and the XEX string table. |
| Probe | bd6846a, 4d14650 | A temporary log probe (`src/core/hotfunc/unsorted/DifficultyProbe.cpp`). |
| Record | 96f6554, f25e24d | The gold callers, property prices and two rounds of in-game results, written into the doc. |
| Name | 891eab4 | 74 new manifest entries, with 19 Lua binding names (`LuaMoney_Modify_8239EE98`, `LuaPlayer_SetDifficultyLevel_823C9458`, …). Their comment lines carry **Verified** 65 times and **Inference** 9 times, against 0 and 1 in all of master. Example: `MoneyComp_Modify_8239E698`, the gold choke point, with its registers, clamp and caller rules. |
| Feature | 946eddb, c2215e2, 92583be, bad29df | Decisions, then the overrides, then fixes found in game (the damage-taken cache, the price pairs calling each other), then the UI. |

Two differences from the template: the names landed in the same branch as the feature rather than in a separate no-behaviour-change PR, and there are no scenario files (E5), because the remote-control server can't read game state yet (next section).

### Progress metrics

Today's numbers (**Verified**, at 7863041) are in the table under "Where the manifest stands".

- **Descriptive-name coverage:** entries that are neither placeholders nor shape-only names, over the total function count. That's 2,113 of 5,078 manifest entries (42%). The total function count isn't recorded in the repo (the research says ~60,700, **Reported**). Report E3+ separately once labels exist.
- **Placeholder debt:** 2,642 `ProcessAndProcessAndProcess<N>_` names (52% of entries), plus every function with no entry at all (the `sub_` names in `generated/`). It has fallen by 14 since 8 Oct 2026.
- **Evidence coverage:** entries whose comment carries a label. Today 0 on master; 74 on the difficulty branch.
- **Hot-path coverage** (Inference: a useful metric): the share of functions executed in a standard scenario run that are E2 or higher, measured by an instrumented debug build. The existing function trace (`tools/fable2-functrace.cmd`) is the obvious starting point.
- **Per-subsystem dashboards:** GUI, timers, economy, combat, save, quest, render.

### Prioritised systems to map next

1. **Timers and tick scheduling** (LF/HF update, `Timer_GetElapsed_822B6D70`, job meters). Unblocks [high_tick_rate_side_effects](../../fix/high_tick_rate_side_effects/technical.md) and the job/expression timing work.
2. **GUI/GameFace and inventory** (`GuiManager_Update_82286B40` and its descendants). Unblocks the menu-lag fix, the settings menu and difficulty selection.
3. **Economy and property** (gold transactions, rent, shop prices, clock). Needed by difficulty, the rent-exploit toggle and the property menu. **Verified:** largely done on `feature/game-difficulty` (commit 891eab4 names the gold, shop, property and combat functions, for example `MoneyComp_Modify_8239E698`, `Item_GetBuyPrice_828E7F78` and `Property_GetBuyPrice_8274DD78`). Not on master yet.
4. **Combat and creature stats** (damage application, health init, knockout). Needed by difficulty and the death penalty. Damage taken and XP are mapped on the same branch (`Combat_GetDamageTakenFactor_8249F298`, `Experience_ScaleAward_8251CDD8`); enemy health isn't.
5. **Save system.** Old-save compatibility (#217), slots and backups. The difficulty branch has started here too (`SaveManager_Save_824E8990`, `LuaSaveManager_SaveGame_824E8978`).
6. **Quest/script manager** (QuestManager natives). Needed for [cut_content](../cut_content/technical.md) and quest-bug fixes.
7. **Achievements** (the overlay overrides commented out in `src/core/fable_2_app.h`).
8. **Dog AI and pathfinding.** Invisible-dog and following bugs.
9. **Renderer LOD and fog.** Pop-in options and the ReGPU work (#187).
10. **System notifications.** Issue #237 (black screen on a "free title" notification)\[4\] suggests a XAM/notification path worth naming (cause unverified).

### Tie-in with the automated checking loop

Scenario files (input sequences plus console reads) sent through the debug-build remote-control server serve two purposes:

- They are E5 evidence for names.
- They are regression tests for features, for example "buy item X at shop Y on Hardcore; assert gold delta = price × 1.3", or "run 60 s at high_tick_rate; assert the in-game clock advanced 60 s".

Keep scenarios small and deterministic: start from a fixed save, and disable random events where possible.

**What exists today (Verified):**

- **The server** is `src/input/remote_control_server.h`, started from `src/core/fable_2_app.h`. `CMakeLists.txt` defines `FABLE2_REMOTE_CONTROL` only for Debug builds, so Release never opens the port. It listens on 127.0.0.1:8791 (or the next free port up to +9), speaks one JSON object per line, and has an optional shared token (`auth`).
- **Its commands:** `ping`, `auth`, `info`, `enable`/`disable`, `press`, `release`, `stick`, `state`, `clear`, `script`, `get_state` (the pad), `game_state` (PreMainMenu / PressAScreen / MainMenuMovie / MainMenu / unknown), `screenshot` (a PNG of the game frame) and `cvar` (get or set).
- **`script`** runs a timed list of steps atomically. Each step is `{"delay_ms", "op": "press"|"release"|"stick", "input", "hold_ms"}`.
- **The client** is `tools/fable2_control.py` (stdlib only; `script --file repro.json`), copied next to the exe only in Debug builds. **The smoke test** `tools/remote_smoke.cmd` waits for the port, pings, presses A, and checks the pad state.

**What's missing for scenario files (Inference):**

- **Reading game state.** Nothing returns gold, health, the clock or any guest memory, so a scenario can drive input but can't assert anything beyond the menu state and a screenshot.
- **A console channel.** The f2 console already runs host commands (`run_host_command` in `src/core/fable2_console.h`) and queues Lua for the world-update thread (`service_world_thread`). A `console` server command that forwards a line there, and returns what it prints, would give scenarios both reads (Lua queries, memory peeks) and setup (warps, giving items). It must not start scripts from the server's own thread; see the alt-tab crash rule in `CLAUDE.md`.
- **Assertions and a runner.** `script` has no `expect` step. A small runner in `tools/` could play a scenario file, issue console reads, and compare them with expected values.
- **A fixed starting point.** There's no command to load a given save, so "start from a fixed save" is manual today.
- **Running it.** The server is Debug-only and needs the game files, so these scenarios can never run in CI. They would be a local check each contributor runs, with the result written into the manifest comment (E5).

### Decompilation vs. recompilation

The long-term goal is "a readable decompilation that supports modding".\[2\] Evidence-graded naming serves both paths: names, types and evidence are prerequisites for any later matching decompilation, and they make the recomp moddable now. A full matching decomp of roughly 60,700 functions (Reported; not countable from the repo) needs the original XDK compiler toolchain and years of effort; that decision belongs to the maintainer. **Recommendation:** continue the recomp and treat mapping as shared groundwork.

## What changed

Nothing yet. Evidence levels and metrics aren't in `/name-functions` or `tools/check_manifest_collisions.py`.

## Verification

- **Verified in this repo, at 7863041:** the counts in "Where the manifest stands" (a Python count over `fable_2_manifest.toml`, plus `git show` of older commits for the history); the tools, data files and server commands listed above, read in the source.
- **Verified on `origin/feature/game-difficulty`, at bad29df:** the commit sequence and the 74 new entries. `MoneyComp_Modify_8239E698`, `Item_GetBuyPrice_828E7F78`, `Property_GetBuyPrice_8274DD78`, `Combat_GetDamageTakenFactor_8249F298`, `Experience_ScaleAward_8251CDD8`, `SaveManager_Save_824E8990`, `LuaSaveManager_SaveGame_824E8978`, `LuaMoney_Modify_8239EE98` and `LuaPlayer_SetDifficultyLevel_823C9458` are in that branch's manifest, **not yet on master**. Every other name above is in master's manifest.

## Not tested

- The process hasn't been run end to end.
- The total function count (~60,700) couldn't be checked without a build.
- The convention check is a design; the "fails today" numbers come from the same count, not from a working tool.
- The remote-control server wasn't run (Debug build, Windows only).

## Follow-ups

- Add evidence levels and progress metrics to `/name-functions` and the collision-check tool.
- Rename `LuaBind_RunScript_82806168` to what it is (the shared LuaPlus bound-method dispatcher), with `/name-functions`.
- Record the total function count from a build, so coverage has a denominator.

## Sources

1. [Name the game functions used by recent PRs by delta4o · Pull Request #238 · himdo/Fable-2-Recomp](https://github.com/himdo/Fable-2-Recomp/pull/238)
2. [GitHub - himdo/Fable-2-Recomp · GitHub](https://github.com/himdo/Fable-2-Recomp)
3. [Add CLAUDE.md, Claude skills and a fix/feature docs layout by delta4o · Pull Request #240 · himdo/Fable-2-Recomp · GitHub](https://github.com/himdo/Fable-2-Recomp/pull/240)
4. [Issues · himdo/Fable-2-Recomp · GitHub](https://github.com/himdo/Fable-2-Recomp/issues)
