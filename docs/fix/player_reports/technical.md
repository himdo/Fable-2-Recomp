# Open player reports: technical notes

**Status:** no commit yet (research only) · **Commits:** none · **Last checked against:** c2a9d12

Claims are labelled **Verified** (checked in this repo or a primary source), **Reported** (a source says so, not checked here) or **Inference** (reasoned, not tested). This came out of the [November 2026 research](../../reference/research/README.md).

Code was read at master 7863041. SDK code was read at the commit master pins (`thirdparty/rexglue-sdk` at 22ef700) with `git show 22ef700:<path>`, because the local submodule checkout on the Mac this was first written on was at a different commit (c26d8b6). On the Windows machine used to check it on 26 Nov 2026, the submodule is at the pinned 22ef700. The GitHub issues couldn't be opened from this machine (`gh` wasn't signed in), so their wording comes from the research, not from the issues themselves.

## Symptom

Problems players reported against the recomp at the time of the research. None is fixed yet.

| Report | Source |
|---|---|
| Crash on the loading screen with an Intel i7-3770 | Reported by a player |
| Slow inventory menus | Reported by players |
| Texture issues on Android | Reported by players |
| Crash with an outdated AMD driver | Reported; issue #221\[1\] |
| Black screen when a "free title" notification appears | Reported; issue #237\[1\] |

## What every report can use: the logs a Windows build already writes

**Verified:** these are compiled into every Windows build, Release included (`#ifdef _WIN32` only):

| File (next to the exe) | Written by | What it holds |
|---|---|---|
| `logs/` | the SDK logger, started before `OnPostInitLogging` in `src/core/fable_2_app.h` | The game log. Near the start it has `[native-build] configuration=… linked_sdk=…`, `[build-profile] detected …` and the `[xex-verify]` hash lines, then the GPU adapter line (see the AMD section). |
| `fable2_crash.log` | `src/diagnostics/fable2_crash_log.h` | Host crashes: unhandled exception (code and address), `abort()`, `std::terminate`, invalid CRT parameter, pure virtual call. The call stack is written as module+offset; `llvm-symbolizer --obj=fable_2.exe 0x<offset>` turns it into names. |
| `fable2_av_probe.log` | `src/diagnostics/fable2_av_probe.h` (included from `src/main.cpp`) | Guest-code access violations with the guest state. Rate-limited: the first 10 events per (fault, rip) pair, then every 2048th, at most 20,000 lines. |
| `fable2_stall_dump.log` | `src/diagnostics/fable2_stall_dump.h` (included from `src/main.cpp`) | When the render thread goes silent for more than 4 s: the RIP and first stack words of every thread, at most once per 5 s, up to 40 dumps. |

Opt-in tools, all off by default:

- `FABLE2_FPS_METER=1`: `src/diagnostics/fps_meter.h` overrides `MainRenderLoop_82B9CD68` and writes `fps_meter.log`, one line per 5 s with the rate, the slowest 1 %, the worst frame and the number of frames over 50 ms.
- `FABLE2_FUNC_TRACE=1` (or `tools/fable2-functrace.cmd`): `src/core/fable2_func_trace.h`, force-included into every recompiled function through the precompiled header (`CMakeLists.txt`). Logs each guest function entry to `fable2_func_trace.log` and call counts to `fable2_func_summary.log`. `FABLE2_TRACE_WINDOW=1` with `FABLE2_TRACE_DELAY`, `FABLE2_TRACE_DUR` and `FABLE2_TRACE_FILTER` traces only a time window (`src/diagnostics/fps_meter.h`).
- `tools/run_profiler.cmd`: launches a `win-amd64-release-profiling` build (`CMakePresets.json`) paced to the guest vblank, for an external profiler.

## Root cause

### i7-3770 crash on the loading screen

- **Reported (Intel ARK):** the i7-3770 (Ivy Bridge) supports "Intel® SSE4.1, Intel® SSE4.2, Intel® AVX", with no AVX2 (introduced with 4th-gen Core).
- **Reported:** another recomp confirmed exactly this CPU fails on AVX2 builds (illegal instruction 0xC000001D), and shipped a `*_SSE4.exe` that the normal executable launches on non-AVX2 CPUs. The LCS recomp moved to an AVX1 baseline.\[2\] LASRecomp kept AVX2 for speed.\[3\]
- **Verified: master's build doesn't ask the compiler for AVX or AVX2, apart from zlib-ng.**
  - The project's `windows-base` preset in `CMakePresets.json` sets no `-march`. The only `-march` values there are `armv8-a`, for the ARM64 presets.
  - The recompiled game code (`fable_2_recomp`) gets `-msse4.1` and nothing wider, from `rexglue_apply_target_settings` in the SDK's `cmake/rexglue_helpers.cmake`, applied by `rexglue_setup_target` in `generated/rexglue.cmake`.
  - The runtime and the D3D12 plugin are built by `tools/build_runtime_sdk.cmd` with `-march=x86-64-v2` (SSE4.2 and POPCNT, no AVX). So are the Vulkan plugin (`tools/build_sdk_vulkan.cmd`) and the Linux SDK build in `build.sh`.
  - The SDK's own presets use `x86-64-v2` on Windows and Linux. Only its `mac-base` preset uses `x86-64-v3` (AVX2), and this project doesn't build that one.
  - `include/rex/ppc/intrinsics.h` in the SDK includes SIMDe's `avx.h` and `avx2.h`. **Inference:** SIMDe falls back to SSE code when the compiler isn't allowed AVX2, so this doesn't add AVX2 instructions on its own.
  - No CPUID or CPU-feature check exists in `src/` or in the SDK's `src/` and `include/` (no `cpuid`, `__builtin_cpu_supports` or `IsProcessorFeaturePresent`).
  - **The exception is zlib-ng**, the host inflate library that `src/core/hotfunc/zlib/native_inflate.cmake` fetches. It builds a few files per instruction set: `-mavx2` for `adler32_avx2.c`, `chunkset_avx2.c`, `compare256_avx2.c` and `slide_hash_avx2.c`, and `-mavx512*` for four more (**Verified** in the Debug build's `compile_commands.json` on Windows). zlib-ng picks one of these at runtime with its own CPUID check, and `native_inflate.cmake` doesn't turn that off. **Inference:** so an i7-3770 runs the SSE versions and this isn't the crash. It is the one place where AVX2 code is in the binary.
- **Not on master:** the `202_2` branch's Steam Deck build (`72b2b56`) adds `build.sh --cpu deck` with `-march=znver2`, which does allow AVX2. A Windows build made with similar flags would crash on an i7-3770, but nothing on master does that.
- **Inference:** a source build of master shouldn't hit an AVX2 illegal instruction, so the AVX2 explanation is now doubtful for current builds. Still open: the build the player used (an older release, a prebuilt plugin, or a fork may have been compiled differently), a third-party DLL, or a cause unrelated to the CPU.

### Slow inventory menus

- **Reported:** matches the most-cited 2008 complaint ([player_complaints](../../feature/player_complaints/technical.md#inventory-and-menus)).
- **Verified:** the menus are Anark GameFace 4.0.5 presentations whose scripts run in GameFace's own Lua 5.1 state, separate from the game's Lua ([gui_lua_bridge](../../reference/gui_lua_bridge.md), code in `src/core/fable2_gui_lua.h`).
- **Verified:** `GuiManager_Update_82286B40` is the front-end GUI manager's update (subtitles, HUD text). It is vtable[2] of the front-end object, called from `GameFrame_HfTickUpdate_82278C90`, and it times itself with `Timer_GetElapsed_822B6D70` (comments in `fable_2_manifest.toml`). `[patches] gui_every_hf_tick` (default true, `config/fable2_config.toml`) calls it on every HF tick instead of only on LF ticks. Because it measures elapsed time, calling it more often only refreshes the text more often.
- **Inference:** it isn't known whether the inventory screens update through `GuiManager_Update_82286B40` or through another GameFace path. Nobody has measured where the time goes, so the cause is still unknown. The original game was slow here too, so the slowness may be the game's own code and not the recomp's.

### Android textures

- **Verified:** this repo has no Android build. The only traces are `.gitignore` (`thirdparty/android-ndk/`), a comment in `src/core/fable_2_app.h` and notes in `plans/ai-remote-input-control.md`. In the SDK there is the Xenia-era Vulkan code: an Android-only swapchain format (`R8G8B8A8`) in the Vulkan presenter (visible in `thirdparty/rexglue-sdk-local.patch`), and a static assert in `include/rex/graphics/pipeline/shader/spirv_translator.h` that keeps shaders within 4 descriptor sets, the limit on Mali, PowerVR and Adreno 6xx and older.
- **Inference:** Android builds come from elsewhere (FableDroid) and use the Vulkan backend. Texture problems there are most likely in the Vulkan backend or the mobile GPU driver, not in code this repo controls.

### Crash with an outdated AMD driver

- **Verified:** on D3D12 (the default backend), `src/ui/d3d12/d3d12_provider.cpp` in the SDK logs `DXGI adapter: <name> (vendor 0x…, device 0x…)`, with **no driver version**. On Vulkan, `src/ui/vulkan/vulkan_device.cpp` logs the device name, API version, vendor, device and `driver version 0x…`, but only as the raw packed number.
- **Verified:** the repo has no issue templates (`.github/` holds only `workflows/tests.yml`), so nothing asks reporters for their driver version.
- **Verified:** the RX 7000/9000 hang in Wraithmarsh was a different bug and is fixed in the SDK ([amd_gpu_hang](../amd_gpu_hang/technical.md)).

### Black screen on a "free title" notification

- **Verified:** the game reads system notifications every frame. `ProcessGameFrame_82276C30` (`src/core/hotfunc/frame/ProcessGameFrame_82276C30.cpp`) calls `PollAndProcessOsNotifications_8229A9D8`, which reads the queue with `XNotifyGetNext`. `DispatchOsNotifications_82185080` handles types 9, 10, 11 and above 11 (`fable_2_manifest.toml`; the names come from automatic descriptions, not checked here).
- **Verified:** in the SDK, notification 0x9 (`XN_SYS_UI`, the system UI opening or closing) is sent by `KernelState::BroadcastNotification` from `src/kernel/xam/xam_ui.cpp`. Each XAM dialog helper sends `true` when it opens and `false` when it closes. `XamShowSigninUI` in `src/kernel/xam/xam_user.cpp` sends 0xA (sign-in changed) and then 0x9 `false`.
- **Inference:** a game typically dims or pauses while the system UI is up. If anything sends "UI opened" without a matching "closed", the game would stay dark. It isn't known whether a "free title" notification even reaches the guest. If it's a Windows or Xbox-app notification on the host, the cause is more likely the window losing focus or fullscreen than XAM. It may be related to the alt-tab path ([alt_tab_script_crash](../alt_tab_script_crash/technical.md)).

## Proposed fixes

- **i7-3770:**
  1. Ask the reporter for `fable2_crash.log` and the build they used. An illegal-instruction crash is logged with code 0xC000001D, and the module+offset shows which file holds the instruction (`fable_2.exe`, `rexruntime.dll`, the GPU plugin or a third-party DLL).
  2. If it is AVX2 after all, find what turns it on in that build, and add a startup check: `__builtin_cpu_supports` behind `#ifdef _WIN32` with a non-Windows branch, called from `OnPostInitLogging` in `src/core/fable_2_app.h`, that shows a clear message instead of crashing.\[4\]
  3. A second, baseline binary is only worth it if a wider `-march` is ever adopted (for example a Deck-style Windows build).\[5\]
- **Slow menus:**
  1. Fill the inventory with the console's `giveall items` command ([console_commands](../../feature/console_commands/technical.md)) so the slowdown can be reproduced on demand.
  2. Measure with `FABLE2_FPS_METER=1` (worst frame and hitches while scrolling).
  3. Run a time-windowed trace (`FABLE2_TRACE_WINDOW=1`, `FABLE2_TRACE_DELAY`, `FABLE2_TRACE_DUR`) around one menu open, and read `fable2_func_summary.log` to see which guest functions run most.
  4. Name what turns up with `/name-functions` (priority 2 on the [mapping list](../../feature/function_mapping/technical.md#prioritised-systems-to-map-next)). Use `tools/run_profiler.cmd` for host-side hot spots.
- **Android textures:** send reporters to FableDroid. This repo can't reproduce it.
- **AMD driver:**
  1. Log the D3D12 driver version next to the adapter line. `IDXGIAdapter::CheckInterfaceSupport` returns the user-mode driver version. That needs a change to the SDK fork (`src/ui/d3d12/d3d12_provider.cpp`), or a host-side DXGI query in `OnPostInitLogging` behind `#ifdef _WIN32`.
  2. Decode the Vulkan `driverVersion` the way the vendor packs it.
  3. Add `.github/ISSUE_TEMPLATE/` with fields for the GPU, driver version, CPU and backend, and a request to attach `logs/` and `fable2_crash.log` (#221).
- **Notification black screen:**
  1. Find out what kind of notification it is (in the game or from Windows).
  2. With a debug build, log every notification `DispatchOsNotifications_82185080` receives (type and data), and check that each 0x9 `true` gets a `false`.
  3. If the window loses focus instead, check `fable2_stall_dump.log` and the game log around the moment it happens.

## What changed

Nothing yet.

## Not tested

- No report was reproduced, and no build was run.
- The build the i7-3770 player used is unknown. Only master's build files were checked.
- The SIMDe fallback, and the path the inventory screens use, weren't checked.
- The notification handling was read in the manifest and in SDK sources, not watched in game.

## Sources

1. [Issues · himdo/Fable-2-Recomp · GitHub](https://github.com/himdo/Fable-2-Recomp/issues)
2. [GitHub - NicoWayne95/lcs-ps2-recomp: Experimental PS2-to-Windows recompilation project focused on compatibility, graphics rendering and performance. · GitHub](https://github.com/NicoWayne95/lcs-ps2-recomp)
3. [No AVX2 for older CPUs request · Issue #186 · peiikai/LASRecomp · GitHub](https://github.com/peiikai/LASRecomp/issues/186)
4. [github.com](https://github.com/browseros-ai/BrowserOS/issues/202)
5. [Avx2 Cpu Instructions: Fix Unsupported Errors (patches)](https://laptopjudge.com/avx2-cpu-instructions-fix-unsupported-errors/)
