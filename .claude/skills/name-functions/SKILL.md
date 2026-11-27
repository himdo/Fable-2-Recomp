---
name: name-functions
description: Give real names in fable_2_manifest.toml to the game functions a change calls, hooks, overrides or explains, with a comment saying what each does and how that is known. Use whenever work touches a recompiled function that still has a generic name (sub_XXXXXXXX, ProcessAndProcessAndProcessN_XXXXXXXX) or a wrong one, before opening a PR, or on request. Triggered by /name-functions [address|name ...].
---

# Name what you discover

The project's long-term goal is a readable decompilation that supports modding. Every PR that works out what a game function does should leave that knowledge in `fable_2_manifest.toml`, so the next person reads `Cloth_Step_8219ADD8` instead of `sub_8219ADD8`.

## 1. Collect the functions

- **With arguments:** the given addresses or names.
- **Without:** everything the current branch touches. Look in `git diff master...HEAD` for:
  - `sub_8xxxxxxx` and `ProcessAndProcessAndProcess<N>_8xxxxxxx` names in added lines;
  - raw `0x82……`/`0x83……` addresses used as call targets, hook hosts or in comments;
  - names that turned out to be wrong (the old automatic descriptions are often guesses).
- For each one, note how the change uses it (called, overridden, hook host, comment only) and whether the manifest already has an entry for its address (`grep -n '"0xADDRESS"' fable_2_manifest.toml`).

## 2. Work out what each one does

- Read its body in `generated/default/fable_2_recomp.*.cpp` (`DEFINE_REX_FUNC(<name>)`); the PPC instructions are kept as comments.
- Look at what it calls and who calls it, and at anything the change itself learned (logs, tests, in-game behaviour).
- Other sources: the game's Lua API names (`docs/reference/lua_call_shapes.md`, `FABLE2_LUA_API.md`), `FUNCTION_NAMES.md`, existing manifest comments, and other branches (`git grep <address> $(git for-each-ref --format='%(refname)' refs/remotes)`).
- Label each conclusion: **Verified** (read in the generated code or seen at runtime) or **Inference** (from the instruction shape or the call site only). Don't present a guess as fact; a cautious name with an honest comment is better than a confident wrong one.

## 3. Choose the name

- `Subsystem_WhatItDoes_ADDRESS`, address in upper-case hex without `0x`: `Cloth_Step_8219ADD8`, `Timer_GetElapsed_822B6D70`, `TextureMorph_ReleaseJobs_8235E0D8`.
- Lua bindings take their Lua name: `LuaDebug_SetDrawGUI_823AC520`.
- Reuse the subsystem prefixes already in the manifest (`grep -o 'name = "[A-Za-z]*_' fable_2_manifest.toml | sort | uniq -c | sort -rn | head -40`).
- A name must not equal an XAPI export of the runtime (`KeWaitForSingleObject`, …); add `_Guest` in that case.

## 4. Write the manifest entry

```toml
# Game frame update, run on every HF tick from ProcessGameFrame_82276C30.
# r3 = the game object, f1/f2 = frame times, r6 low byte = an LF tick boundary
# was crossed. Calls GuiManager_Update_82286B40 only on LF ticks (Verified in
# the generated code).
[entrypoint.functions."0x82278C90"]
name = "GameFrame_HfTickUpdate_82278C90"
```

- The comment says what it does, its arguments and return value where known, important structure offsets or globals it uses, who calls it, and the evidence.
- **Existing entry for the address:** rename it in place and replace its old automatic description. TOML keys must stay unique; never add a second entry.
- **New entries:** group them in a section near related names, or at the end of the file under a heading that names the PR's topic.
- `fable_2_manifest.toml` is CRLF in a Windows checkout. Edit with a tool that keeps line endings (not Git Bash `sed -i`).

## 5. Update every use of the old name

- `git grep -n '<old name>'` in `src/`, `tools/`, `tests/`, `docs/` and the manifest's own comments, and switch to the new name.
- Hot-function overrides are named after the function: rename `src/core/hotfunc/<category>/<old>.cpp` with `git mv`, and its `extern "C"` definition and `__imp__` calls with it.
- Leave historical logs (for example under `plans/`) as they are.

## 6. Check

1. `python -c "import tomllib; tomllib.load(open('fable_2_manifest.toml','rb'))"`: the manifest still parses (no duplicate keys).
2. `python tools/check_manifest_names.py --base origin/master`: every entry the branch adds or renames has a real name in the right format, a comment above it, its own address as the suffix and a unique name. CI runs the same check.
3. `python tools/check_manifest_collisions.py fable_2_manifest.toml out/tooling/runtime-sdk/win-amd64/bin/rexruntime.dll`: no clash with the runtime's exports. Pass the DLL path; the default search folders are out of date. It needs a built SDK; if there isn't one, say the check wasn't run.
4. `git grep` shows no old names left outside historical logs.
5. Build (`build.cmd -release fable_2`) so codegen regenerates with the new names. It needs the game files; if they aren't available, say so.

## 7. Report

List each function as `address  new name  (was …)  Verified/Inference`, grouped by subsystem. Put the same list in the PR description, and mention the names in the fix or feature doc (`/work-doc`).
