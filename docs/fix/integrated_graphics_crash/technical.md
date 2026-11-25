# Integrated graphics crash: technical notes

**Status:** merged for hybrid laptops; open for iGPU-only machines · **Commits:** 4d58a5f (#55), shipped in 1.5.0 · **Last checked against:** 7863041

Issues: [#42](https://github.com/himdo/Fable-2-Recomp/issues/42) (main thread), [#48](https://github.com/himdo/Fable-2-Recomp/issues/48), [#63](https://github.com/himdo/Fable-2-Recomp/issues/63) (closed as a duplicate). SDK line numbers refer to `85d96f2`; paths are relative to `thirdparty/rexglue-sdk/`. Labels: **Verified**, **Reported**, **Inference** as in the other fix notes.

## Symptom

On laptops and handhelds running on integrated graphics, the opening cutscene plays, the screen goes black for the move into gameplay, and the game closes within 1 to 2 minutes. Less often it happens before the main menu or when loading a save. Logs end in either `D3D12 device removed … DEVICE_HUNG` or one or more `Failed to create graphics pipeline with VS …, PS …` lines, then nothing.

## Two different cases

1. **Hybrid laptops (fixed).** Windows started the game on the built-in chip although a discrete card was present (e.g. an RTX 4090 laptop running on Intel UHD). Forcing the discrete card fixed it for two reporters.
   **What changed (#55, `4d58a5f`):** `src/main.cpp` exports `NvOptimusEnablement` and `AmdPowerXpressRequestHighPerformance`, so the drivers pick the discrete GPU by themselves. A per-app Windows Graphics setting still takes priority. It does nothing on single-GPU machines.
2. **iGPU-only machines (not fixed).** Intel Iris Xe, Intel Arc in Core Ultra chips, AMD Vega 8. There is no other card. Even where it runs, the maintainer measured under 1 FPS after the transition on an i5-1135G7 with Iris Xe (3D engine at 100% on the character select screen).

## Log analysis (Verified unless marked)

Seven logs from #42 (1.2.0 to 1.5.0; adapters Iris Xe `0x46A8`/`0xA7A1`, Arc `0x7D55`/`0x64A0`, UHD `0xA788`, Intel `0xA7AC`, Vega 8 `0x164C`).

- **Red herrings:** the 3856 `BaseHeap::AllocFixed attempting to reserve an already reserved range` lines (expected on every pre-1.5.0 build) and the single `CommandProcessor::WriteRegister index out of bounds: 28685` warning, which also appears in every working AMD desktop log.
- **Pipeline failures.** `D3D12PipelineCache` (`src/graphics/d3d12/pipeline_cache.cpp:3049-3061`) logs the failure but **throws away the HRESULT**, so `DXGI_ERROR_DEVICE_REMOVED`, `E_OUTOFMEMORY` and `E_INVALIDARG` look the same.
  - *Reading A, the device was already lost (Inference):* one log has seven failures within 28 ms on seven worker threads, each a different shader pair. That looks like a lost device, not seven rejections.
  - *Reading B, the Intel driver rejects one shader (Inference):* another log has exactly one failure, VS `19A01C01290E20A7` / PS `718C12FF9E1B888B`, which was also the last of the seven on a different Intel chip.
  - One reporter got both endings on the same laptop, depending on the version, so the two are probably one problem.
- **DEVICE_HUNG detection** is the same as in the AMD notes: `BeginSubmission` (`command_processor.cpp:3353`) only checks `GetDeviceRemovedReason()` when opening a frame or submission, so the first draw of the next frame logs `Failed in backend`. The Vega 8 log's `PM4_DRAW_INDX_2(3, 8, 2): Failed in backend (… edram_mode=5)` is identical to an AMD desktop log on #126, which makes it the game's routine frame-opening draw, not a clue about the scene.
- **Arc 140V log stops during a load.** The last lines are a motion-blur request, `[texture-morph] building hero/dog textures in realtime (GPU) mode`, then a `WAIT_REG_MEM` summary covering 2716 ms instead of about 1000 ms: a GPU stall starting (Inference).
- DRED is off in all these logs (`d3d12_debug` defaults to false).

## Possible causes (none tested)

- **The chip is too slow for the first heavy frame** and one job passes Windows' 2-second TDR limit (likeliest). The 1.6 speed-ups may help.
- **An Intel D3D12 driver bug** with a specific shader pair (Reading B).
- **The realtime GPU texture build** on load, for the Arc 140V case.

## Next steps

1. **Log the pipeline HRESULT** in `pipeline_cache.cpp` (hex), and on `DXGI_ERROR_DEVICE_REMOVED` also log `GetDeviceRemovedReason()`. A few lines; settles Reading A vs B and helps every future GPU report.
2. Ask an Iris Xe player to try 1.6 or later.
3. Compare the Vulkan renderer on an Intel chip (`--gpu_plugin=xenos-vulkan` on a source build). A reporter said Xenia Canary's Vulkan backend works where its D3D12 one fails on the same handheld.
4. Try `realtime_texture_morphing = false` on the Arc 140V.

## Not the same problem

- The AMD desktop hang (`docs/fix/amd_gpu_hang/`): desktop cards that run for hours, then hang in specific Wraithmarsh fights.
- #67: closed as a duplicate of #42, but that CPU has no integrated graphics.
