# Alt-tab script crash: technical notes

**Status:** merged · **Commits:** c5c80ff (#189, the fix), 266f181 (#195, the guard) · **Last checked against:** c2a9d12

Labels: **Verified** means read in a log, the repo or the generated code. **Plan claim** means taken from a PR description or notes without re-checking. **Inference** means reasoned, not tested.

## Symptom

On master `3635c93` (1.6.0) the game crashed on the world thread, inside the game's own Lua update, one to several minutes into normal play. It was usually triggered by returning from alt-tab, sometimes right after loading a save or shortly after an `f2` console command. Two of the five crashes were an `abort` in `ppc_longjmp`; the other three were access violations deep in the Lua VM and its tables.

## Root cause (Verified, the #189 diff)

- `console::poll`, the `MainRenderLoop` hook on the **render thread**, called `poll_mouse_direct`, which ran `f5lua::run_external("scripts/recomp/mouse_direct.lua")`. This came from #177 (`80e8361`, direct mouse camera).
- #184 (`fbf77ce`, merged 12 minutes after #177) had moved every other host RunScript into `console::service_world_thread`, which runs at the start of the world update. The new call from #177 wasn't moved.
- `mousedirect::BootState().ShouldTry` retries the script start while the mouse moves and the script hasn't reported for 15 s. Coming back from alt-tab is exactly that case, so alt-tab set it off.
- A RunScript from the render thread can land in the middle of the world update and corrupt the Lua VM.
- **Repro (Plan claim, the #189 description):** on `3635c93`, 1 crash in 8 runs with the same top of stack (`HashLookupByKey_82228570` ← `ProcessAndDispatchTypedValue_822286D0`); with #189, 0 in 8. The #184 stress test crashed 4 of 4 runs with render-thread RunScript and 0 of 4 with world-thread RunScript (over 1500 calls each).

### Second hazard, fixed in the same PR

- `console_pump.lua` used to register itself with `GeneralScriptManager.AddScript`. GeneralScriptManager writes its scripts into save games (`GetSaveTable` / `LoadFromSave`), so loading a save brought back a second pump. Restored functions have no debug info and can't be recognised or stopped.
- After #189 the pump no longer registers. Each console command is a RunScript of `console_step.lua`. The pump runs about 250 ms after each `FINISHED_TRANSITIONING_TO_GAME` and neuters restored scripts that hold a raw `_G` field without `_Name` / `QuestName`. `F5.lua`'s update returns when idle.

## What changed

- **#189 (`c5c80ff`):** `poll_mouse_direct` moved into `service_world_thread`. Every host RunScript goes through `run_world_script`. The console pump changes above.
- **#195 (`266f181`):** prevention, so it can't come back.
  - `tests/core/test_world_thread_runscript.py` strips comments, strings and preprocessor lines from `src/**/*.{h,cpp}`, builds an unqualified-name call graph, and walks callers back from `run_external`. The only allowed entry point is `service_world_thread`, which is called only from the `world_thread_hook()` lambda, which is called only from `photomode::on_world_update`. `console::poll` (the render hook) must call none of the RunScript paths. On failure it prints the offending call chain.
  - `tests/lua/test_recomp_scripts.py`: `GeneralScriptManager.AddScript` appears only in `F5.lua` (which ends itself when it closes), and `F5.lua` keeps its idle return.
  - **Runtime gate**, `src/core/world_thread.h`: thread-local depth counters with RAII `ServiceScope` (opened in `on_world_update` around the hook) and `ResearchOffServiceScope` (for `FABLE2_RUNSCRIPT_STRESS=render`). `f5lua::run_external` refuses a script outside the scope and logs `[lua] RunScript refused for <path>` once per path (`src/core/fable2_f5_lua.h`).
  - `tests/native/test_world_thread.cpp` covers nesting, per-thread isolation and the research scope.
  - Decision when writing the guard: CI tests only / tests plus a log-only guard / tests plus a refuse-and-log guard. **Chosen: tests plus a refuse-and-log guard.**

## Verification

- Verified by running: the call-graph and Lua checks fail on `3635c93` (`MainRenderLoop_82B9CD68 -> poll -> poll_mouse_direct -> run_external`, and AddScript in `console_pump.lua`) and pass on `c5c80ff`.
- The ten newest branches on two contributors' forks were merged onto master in memory and checked. None would bring the problem back.
- Not tested: a long play session on the guard build specifically (the fix itself was tested in #189).

## Notes for future crashes like this

- **`abort` in `ppc_longjmp`** (`generated/default/fable_2_pch.h`): the recomp keeps a `thread_local` map of guest `jmp_buf` addresses. A guest `longjmp` (Lua's `luaD_throw`) aborts when this host thread never ran `setjmp` for that guest buffer. So the Lua state's error handler points to a buffer registered on another thread, or never registered: a sign of Lua running on one state from two threads (Inference).
- `GuildChest_ProcessItem(s)` and `ProcessAndSetjmpAndProcess*` are auto-generated names. The console's comments place Lua execution there, so they're likely the VM's execute and protected-call paths (Inference).
- Earlier placements that also crashed (comments in `fable2_console.h` and `console_pump.lua`): RunScript from `MainRenderLoop` per command, installing the pump while the world was mid-update, and installing it the moment the world first comes back on top.
- **Symbolising a crash:** `llvm-pdbutil dump --publics out/build/win-amd64-release/fable_2.pdb > publics.txt`, then map each `fable_2.exe+0xRVA` to the nearest public symbol below it (`.text` is at RVA 0x1000, so RVA = section offset + 0x1000). Use the PDB from the same build as the crash; `llvm-symbolizer` can't read the trimmed exe.
- **Useful switches:** `FABLE2_RUNSCRIPT_STRESS=world|render` runs a tiny script repeatedly for a fast repro. `FABLE2_PUMP_SETTLE_MS` overrides photo mode's 20 s pump warm-up.
- **Process lesson:** #184 merged right after #177 without re-running its checks against it, so neither PR was tested with the other. Requiring PR branches to be up to date with master, with the Tests check required, would have caught it.
