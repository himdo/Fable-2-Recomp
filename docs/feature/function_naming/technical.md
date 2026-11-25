# Function naming for recent PRs: technical notes

**Status:** merged · **Commits:** f451b72 (#238); #183's own functions were named in its squash 49c70ab · **Last checked against:** 7863041

On approving #183, the maintainer asked that every game function a PR relies on get a real name in `fable_2_manifest.toml`. This pass named the 22 functions that master's last 40 commits (#158..#230) used without one. No behaviour changes: only names, comments and docs.

## Method

1. `git diff 2df8825~1 origin/master` (the last 40 commits, #158..#230): collect the `sub_8xxxxxxx` and `ProcessAnd…_8xxxxxxx` names that appear in added lines, plus raw `0x82……` function addresses used as call targets (the photo-mode `k…` constants).
2. For each one: the manifest status, how the code uses it (call or override, midasm hook host, or comment only), and the commit that introduced it.
3. Extra sources:
   - `git grep` over all 84 remote-tracking refs, plus commit messages (`git log --all --grep`);
   - the open PR diffs (#51, #71, #147, #202, #212, #213, #226, #231, #232);
   - compare diffs for the unmerged fork branches: Jack `fix-split-layout-scripts`, `frame-recorder`; Locme `dynamic-tick-rate`; `native-zlib-memcpy` came back empty.
4. Read each body in `generated/default/` before naming.

## Names

Verified = read in the generated code. Branch note = taken from Locme's `interp-presets` comments. Inference = from the instruction shape only.

| Address | Old | New | Basis |
|---|---|---|---|
| 82B69488 | PAPAP1265 | `PresentFrame_82B69488` | Verified: timebase, Clear (822069C0, flags 256), wait on the frame counter (82B89960) with Yield, then `MainRenderLoop_82B9CD68`, which holds the `VdSwap` import; stores the frame time at global +28408 |
| 8235E0D8 | PAPAP1410 | `TextureMorph_ReleaseJobs_8235E0D8` | Verified: critical section + sub_8235ABA0; #188 defers the lr=0x8236C7E8 call |
| 821E2D40 | PAPAP1467 | `Cloth_UpdateAll_821E2D40` | Verified: walks the cloth list at global 19456, frame dt/substeps, owner+208 = drawn matrix, calls Cloth_RunStep |
| 821AC058 | sub_ | `Cloth_RunStep_821AC058` | Verified: owner via 821E1AA8, RemoveFromLinkedList, Cloth_Step, queue 822AB070, owner+532 = 0 |
| 8219ADD8 | sub_ | `Cloth_Step_8219ADD8` | Verified callees (8218D730, 82A895C0, 82A88DA8, 82A76680, 822222D8, 8220B5C8) + #210 notes |
| 8218D730 | sub_ | `Cloth_SetWorldTransform_8218D730` | Verified: +192→+128, r4→+192, squared-distance test +240 vs +176 → reset 82A88E50 |
| 82A895C0 | sub_ | `Cloth_BuildColliders_82A895C0` | #210 notes; calls QuatScaleToMatrix ×4 |
| 822222D8 | sub_ | `Instance_UpdateMatrix_822222D8` | Verified: interpolator matrix (822333B8) → transposed into inst+336, 8220B698 → inst+384 |
| 821E1C98 | sub_ | `Instance_GetDrawnMatrix_821E1C98` | Verified: interpolator (inst+176) ? 822333B8 : 8220B5C8(inst+336) |
| 8220B5C8 | PAPAP1036 | `Matrix_3x4ToTransposed4x4_8220B5C8` | Verified: rows +0/+16/+32, constant 4th row, transposed out |
| 8222E300 | sub_ | `Pose_BlendForDraw_8222E300` | Branch note + #210 |
| 8222E5D0 | sub_ | `Pose_BlendBone_8222E5D0` | Branch note (sign-corrected quat lerp; t, s lerp) |
| 8222E208 | PAPAP1202 | `Bone_BuildMatrix_8222E208` | Branch note; Verified: same quat expansion as 8222E6C0 plus v1 translation |
| 8222E6C0 | ComputeVectorHash | `QuatScaleToMatrix_8222E6C0` | Inference: vspltw of v2 scale, xor sign masks, vmsum4fp128 = quaternion→matrix. 29 calling files; the old auto-description ("vector hash") was a guess |
| 82278C90 | sub_ | `GameFrame_HfTickUpdate_82278C90` | Verified: r6 = LF boundary flag; vtable[2] GUI call at 0x82278D40 |
| 82286B40 | sub_ | `GuiManager_Update_82286B40` | #208 notes; calls Timer_GetElapsed ×2 |
| 822B6D70 | sub_ | `Timer_GetElapsed_822B6D70` | Verified: first call stores the start (+104) and the flag +103, returns 0; later calls return (now−start)·const as an int |
| 823AC520 | sub_ | `LuaDebug_SetDrawGUI_823AC520` | Lua API name; writes 0x8331930E |
| 823AC5B8 | sub_ | `LuaDebug_SetShouldDrawWorldIcons_823AC5B8` | Lua API name |
| 8249A790 | sub_ | `LuaTiming_SetTimeOfDayAbsolute_8249A790` | Lua API name |
| 822BD510 | sub_ | `Theme_UpdateLighting_822BD510` | photo mode notes (world update calls it at 0x82276094) |
| 8218EB30 | sub_ | `Theme_Update_8218EB30` | photo mode notes |

PAPAP = ProcessAndProcessAndProcess.

## Files

- `fable_2_manifest.toml`:
  - 6 entries renamed in place, with their comment blocks replaced (the old automatic descriptions were long and partly wrong);
  - 16 new entries at the end of the file ("Functions used by recent PRs", grouped: game frame/GUI, pose, cloth, photo mode);
  - the #208/#210 midasm hook comments switched to the names and rewrapped.
- Code:
  - `src/core/fable2_morph_release_defer.h`: the overrides and `__imp__` calls;
  - `src/core/hotfunc/frame/ProcessGameFrame_82276C30.cpp`: the extern and the call;
  - `src/core/fable2_hooks.cpp` and `docs/patches.md` (now `docs/feature/guest_patches/technical.md`): comments, rewrapped;
  - `src/core/fable2_photo_mode.h`: a pointer comment only (it calls by raw address).
- Not changed:
  - `plans/process-game-frame-hand-tune.md` (a historical disassembly log);
  - other manifest entries' automatic callee lists that still say `sub_…` or `ComputeVectorHash` (same as earlier naming commits).
- **Line endings:** `fable_2_manifest.toml` and several sources are CRLF. Edit them with a tool that writes bytes; Git Bash `sed -i` silently turns CRLF into LF.

## Cross-PR impact

- **#71** (`remove-texture-readback-options`) edits `fable2_morph_release_defer.h` around the old `…1410_8235E0D8` override: it will hit a merge conflict, then a link error until it uses `TextureMorph_ReleaseJobs_8235E0D8` / `PresentFrame_82B69488`.
- None of the other open PRs or fork branches reference any of the 22 functions.
- Open PRs that leave unnamed functions (their authors' business, not changed here):
  - #213 mentions `ProcessAndProcessAndProcess21_823FE988` in comments;
  - #212 lists 6 `vexptefp` sites as `sub_…`.

## Verification

- TOML parses: 5077 function entries, no duplicate keys.
- `git grep` shows no old names in `src`, `docs` or `tools`, apart from the plans log.
- pytest: 429 passed, 1 skipped, 15 xfailed. Native tests: all 15 suites passed.
- Release build: result not recorded in these notes.
