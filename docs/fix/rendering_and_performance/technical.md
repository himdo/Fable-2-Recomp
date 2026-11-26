# Rendering and performance (1.6 speed-ups and the bugs they exposed): technical notes

**Status:** merged · **Commits:** recomp 0b163fb (#149), 6e9f5c4 (#150), c2cad76 (#151), d28da25 (#143), 359d3a1 (#148), e0c69c0 (#135), 2df8825 (#158); SDK bda14fe (#25), e251d3a (#26), c04750f (#27), a56e5ae (#28), all shipped in 1.6.0 · **Last checked against:** 7863041

Line numbers refer to recomp `e0c69c0` and SDK `85d96f2`, where this was first written. SDK paths are relative to `thirdparty/rexglue-sdk/`. Labels: **Verified** means read in code, git history or a PR description. **Reported** means said by a developer and not checked. **Inference** means reasoned, not tested.

## Concepts

| Term | Meaning here |
|---|---|
| Memexport | A Xenos (360 GPU) feature: a shader writes results straight to memory through `eM` exports. On the 360 the CPU just reads that memory |
| Readback | On PC the GPU output stays in VRAM, so the SDK copies memexport output back to guest memory and the CPU stalls until it arrives (`d3d12_readback_memexport`; `readback_memexport_fast` hands back the previous draw's output) |
| Shared memory and page state | The SDK mirrors guest physical memory into a GPU buffer and tracks which pages are valid. CPU writes are caught through page protection ("access callbacks") |
| `clear_memory_page_state` | SDK cvar, default `true`: mark every page invalid at frame end so it's uploaded again. A safety net for writes the tracking misses |

## The speed-ups

- **CPU memexport (SDK #25, recomp #149).** `src/graphics/d3d12/command_processor.cpp:2360-2374` recognises Fable II's terrain tessellation and skinning vertex shaders by exact hash (skinning is `F6C546D6C2645541`) and computes their output on the CPU (`fable2_terrain_tess.cpp`, `fable2_skinning.cpp`). The draw is still issued, so the GPU copy stays correct; only the readback wait (`:2804`) is skipped. Any other memexport shader still reads back. An earlier version skipped the draw entirely and made distant terrain flicker.
- **Narrower render-target transfers (SDK #26, recomp #150):** depth transfers are skipped for draws that replace all depth. Also removed a purple bar at the bottom of the screen.
- **No full GPU memory refresh every frame (recomp #151):** adds `[perf] refresh_gpu_memory_each_frame = false`, which sets `clear_memory_page_state = false`.
- **Frame pacing:** SDK #22 (host present limit), #23 and #24 (limiter and vblank worker), recomp #143 (PAUSE spins in the limiter wait) and #135 (host FPS limit in the launcher).
- **Measured (Reported, PR descriptions, 2x, uncapped, two saves):** 28.7 → 61.3 and 27.3 → 49.7 FPS with #25; 68.4 and 55.4 with #26/#150 added; #151 gave 34 → 38 indoors.
- A check build compared about 10 million terrain values between CPU and GPU and found no differences.

## Bug 1: tree flicker (SDK #27, `c04750f`)

- **Cause (Verified, PR diff):** in `src/graphics/pipeline/shader/dxbc_translator_memexport.cpp`, the ±0.5 rounding bias for signed integer formats was written into the export value instead of its temp, so every signed integer memexport came out as 0 (also the 16:16:16:16 signed path). 3-line fix; the Vulkan translator isn't affected.
- **Why it flickered:** skinning exports normals as signed 2:10:10:10. The GPU copy had zero normals while the CPU path from #25 wrote real ones, and page re-uploads alternated between the two.
- **Verified by the author:** a CPU-vs-GPU compare at the reported spot showed every normal as `00000000` before the fix and 1180 of 1180 skinning draws matching after.
- **Inference:** before #27, skinned meshes had zero normals on the D3D12 GPU path in 1.5.0 as well. Older reports such as #13 and #68 may be worth retesting.

## Bug 2: Lionhead logo flash (SDK #28, `a56e5ae`)

- **Cause (Verified, #28 description):** the GPU could upload a physical range while the guest had it free or read-only. `EnableAccessCallbacks` leaves those pages unwatched, so after the guest allocated the range again its writes were never seen and the GPU kept the old copy. Fable II frees and reallocates its Bink frame buffers every frame, so intro frames showed stale (blank) data. The per-frame refresh removed by #151 had been hiding this.
- **Fix:** `PhysicalHeap` runs the invalidation callbacks for the range after a committed allocation and when a range is made writable. It fixes the whole class (any freed and reallocated physical memory), not only Bink.
- **Measured by the author:** 270–340 stale pages per frame before, 0 after. Intro frames that flipped went from 26–34% to 1–2%, the same as with the refresh on.
- **Why a bisect blamed #152 (Inference):** the bisect landed on `0e2346b`, a side-branch commit whose only parent is master before #149–#151. Plain `git bisect` (without `--first-parent`) tests such commits. Combined with the 1–2% residual flip rate, a short run at any commit can look "bad". For this kind of bug use `git bisect start --first-parent` and a fixed number of intro viewings per step.

## Bug 3: Oakfield trees and textures

Reported while this batch landed: Oakfield, probably the most tree-heavy area, showed tree and texture corruption. Its cause wasn't pinned down in these notes. #158 (`2df8825`, "Keep the GPU at most one frame behind to stop the flicker", SDK `gpu_sync_at_frame_start`) landed afterwards and may cover it; **not confirmed here**. If it comes back, the four cvars below are `kHotReload` and can be flipped one at a time in the `~` console at the spot.

## Config switches

| `fable2_config.toml` key | Section | Default | SDK cvar(s) it seeds |
|---|---|---|---|
| `terrain_tess_cpu` | `[patches]` | `true` | `terrain_tess_cpu` |
| `skinning_cpu` | `[patches]` | `true` | `skinning_cpu`; with both on, `readback_memexport_fast = false` (the fast path would hand moving geometry stale data) |
| `faster_render_targets` | `[patches]` | `true` | `execute_unclipped_draw_vs_on_cpu`, `skip_depth_clear_transfers` |
| `refresh_gpu_memory_each_frame` | `[perf]` | `false` | `clear_memory_page_state = false` when `false` |

- Seeded in `src/core/fable_2_app.h` (around lines 196–230 at `e0c69c0`). The app only seeds a cvar whose source is still `kDefault`, so `fable_2.toml`, `REX_*` environment variables and the command line win.
- Every SDK default is `true`; with the app defaults only `clear_memory_page_state` and `readback_memexport_fast` end up different.

## Debugging tools

- The developer console (`~`, then `f2 <command>`; see the console commands reference) has memory commands (`peek`, `poke`, `find`, `scan …`, `dumpmem`, `waitfor ram`) and `cvar <name> [value]` to read or set SDK cvars at runtime.
- A CPU-vs-GPU compare build at the reported spot is how bug 1 was found; the same approach fits other memexport bugs.

## Not verified here

Nothing in these notes was run in the game by the author of these notes; the measurements come from the PR descriptions.
