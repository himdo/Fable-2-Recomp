# AMD GPU hang in Wraithmarsh: technical notes

**Status:** merged · **Commits:** SDK 0fe6654 (himdo/rexglue-sdk#29), first picked up by the master pin in 2df8825 (#158), shipped in 1.6.0 · **Last checked against:** c2a9d12 (SDK pin 22ef700)

Issue: [#126](https://github.com/himdo/Fable-2-Recomp/issues/126). SDK paths are relative to `thirdparty/rexglue-sdk/`; line numbers refer to SDK `85d96f2` (the pin at the time) unless noted. Labels: **Verified** means read in code, a log or git. **Reported** means said by a player and not checked. **Inference** means reasoned, not tested.

## Symptom

On recent AMD cards (RX 7000 and 9000 series), the game hung in Wraithmarsh fights, mostly around hollow men and banshees. The GPU stopped, Windows reset it after about 2 s (TDR), and the game logged `D3D12 device removed … DEVICE_HUNG`, or aborted silently. NVIDIA players got through the same areas fine. No setting avoided it on 1.5.0.

## Root cause (Verified)

- **The hang is a GPU page fault in one draw:** Fable II's trail or ribbon effect, vertex shader `87D12B6DEC03F597`, a non-indexed triangle strip of 62 to 422 vertices drawn around hollow men.
- **The shader reads float constants with a relative index.** It evaluates a curve, fetches four control points from `vf0`, lerps their `.w` values and truncates the result into `a0` (`maxas`), then reads `c[114+a0]`, `c[115+a0]`, `c[134+a0]` and `c[135+a0]`.
- **The translator only clamped `a0` to −256..255:**

  ```text
  round_ni r19.w, r19.w        ; a0 = floor(x + 0.5)
  max r19.w, r19.w, l(-256.0)
  min r19.w, r19.w, l(255.0)
  ftoi r19.w, r19.w
  add r18.xyz, CB1[1][r19.w + 135].zxyz, -CB1[1][r19.w + 134].zxyz
  ```

  `CB1` is `CB1[1:1][256], dynamicIndexed`, so the index range is −142 to 390 against 256 entries. A NaN input becomes −256, because D3D `max` returns the non-NaN operand.
- **Float constants are root CBVs** (`src/graphics/d3d12/command_processor.cpp`, root signature setup, bindful and bindless). Root descriptors carry no size, so nothing bounds the read. A negative index gives a negative 32-bit byte offset, which AMD hardware adds to the 64-bit base as unsigned, so the read lands at **base + 4 GB − n**, an unmapped page.
- **The proof:** in two runs, the DRED page-fault address was exactly the bound constant buffer's own 4 KB page plus `0x1_0000_0000` (CB `0x5161E6F00` → fault `0x6161E6000`; CB `0x502D79F00` → fault `0x602D79000`).
- **Where the negative `a0` comes from (Inference):** the shader also fetches control point −1, the 32 bytes before the trail's buffer, which belong to something else. A CPU-side check saw only clean data from point 0 onward. This also explains the random timing.
- **Why only AMD (Inference):** NVIDIA evidently bounds or wraps such reads, so the bad index just returns a wrong constant. On the Xbox 360 the read stays inside the constant file.

## What changed

`src/graphics/pipeline/shader/dxbc_translator.cpp`, `DxbcShaderTranslator::LoadOperand`, the `kConstantFloat` case with relative addressing, now clamps `a0 + N` to 0..255 before the read:

```cpp
a_.OpIAdd(index_dest, relative_src, dxbc::Src::LI(int32_t(operand.storage_index)));
a_.OpIMax(index_dest, index_src, dxbc::Src::LI(0));
a_.OpIMin(index_dest, index_src, dxbc::Src::LI(255));
```

- It covers every shader with relative float addressing: 18 in Fable II (17 vertex, 1 pixel), including the bone-palette style `c[28..31+a0]`.
- `dxbc_clamp_relative_float_constants` (default true, read at translation, needs a restart) turns it off for comparison.
- Visual cost: an out-of-range index now reads constant 0 or 255 instead of faulting. At most a slightly wrong colour or width on the odd trail vertex.

## Verification

On an RX 9070 XT with the reporter's save that hung within a minute (`resolution_scale = 3`):

| Run | Build | Result |
|---|---|---|
| Fix on | master `e0c69c0` + local patch | No hang, about 5 minutes through Wraithmarsh |
| Control | same, `--dxbc_clamp_relative_float_constants=false` | Hang 18 s after load, same draw, fault = CB page + 4 GB |
| Clean PR branch | master `e0c69c0` + SDK `038ff5c` only | No hang, about 5 minutes |

**Not tested:** the other reporters' repros (a banshee fight while aiming a spell may go through a different effect shader), RDNA 3 cards other than the reporter's own, and the Vulkan backend.

## How it was found

| Step | What it showed |
|---|---|
| 1.5.0 and master `e0c69c0` both hang with the reporter's save | 1.6's speed-ups don't fix it |
| `--d3d12_debug=true` gave no DRED output | The presenter thread aborts first, silently (see below) |
| Patched SDK logs DRED on any GPU loss | A `DrawInstanced` with a page fault, not a long-running job |
| Per-draw markers mapped host-side | The hung draw is `VS 87D12B6DEC03F597`, the trail |
| Skip that draw | No hang through Wraithmarsh |
| Trail without its pixel shader, `resolution_scale = 1`, NaN/Inf position cleanup, tiled shared memory off, bindless off, 4 KB padding around the CB | All still hang: not textures, scaling, positions, memory mode or descriptors |
| CB addresses in the draw tags | Fault = CB page + 4 GB: the root cause |

## Notes for the next GPU hang

- **Two threads can notice a GPU loss.** The command processor (`BeginSubmission`) logs `D3D12 device removed` and the DRED dump, but only when a frame or submission opens (`command_processor.cpp:3362-3369`), so the next frame's first draw reports `Failed in backend` as a consequence. The presenter's paint (`src/ui/presenter.cpp:533-541`, `:662-666`) goes straight to `OnHostGpuLossFromAnyThread` (`src/graphics/graphics_system.cpp:220-235`) and `abort()`, with no log line. On master the presenter usually wins, which is why many logs end silently.
- **DRED needs `--d3d12_debug=true`** (`src/ui/d3d12/d3d12_provider.cpp:219-243`). Without Windows' Graphics Tools the debug layer fails to load, but DRED still turns on, so there is no debug-layer slowdown.
- **Windows' record of a TDR:** WER `LiveKernelEvent` 141 (dump under `LiveKernelReports\WATCHDOG`, admin only), plus Application Error 1000 for `fable_2.exe` with `0xc0000409` in `ucrtbase.dll`.
- **Diagnostic SDK branch** (`amd-hang-diagnostics`, local, not pushed): DRED on any GPU loss, per-draw `SetMarker` tags (`--gpu_debug_markers=true`) mapped host-side to name the `HUNG DRAW`, fault-address placement against shared memory, EDRAM and upload pages, and bisection switches (`--debug_skip_draws_vs`, `--debug_no_ps_for_vs`, `--debug_inspect_vs`, `--debug_sanitize_position_vs`). Worth offering upstream as an opt-in PR.

## Follow-ups

1. **Pin guard test** (recomp repo, Python): read `dxbc_translator.cpp` and check that the `kConstantFloat` relative path still has `OpIMax` / `OpIMin` with `LI(255)`, skipping when the submodule isn't checked out. It stops a future SDK resync from dropping the clamp silently. Not written yet.
2. **Translator unit test** (SDK): needs a test target that compiles the translator (it lives in `rexgpu-xenos.dll` with hidden visibility) and a synthetic Xenos shader. Never use game ucode.
3. **Vulkan:** `SpirvShaderTranslator::GetStorageAddressingIndex` (`spirv_translator.cpp`, about line 3459) builds the same unclamped `a0 + N`. Probably harmless because the UBO descriptor is sized (Inference), but undefined behaviour without `robustBufferAccess`. The same clamp would make both backends match.
4. **Manual check before releases** that touch shaders or the SDK pin: an AMD card, a save at the Wraithmarsh bridge, 5 minutes of combat with the hollow men. On 1.5.0 it hung within 15 to 80 seconds.
5. Confirm #126 is closed.

## Not the same problem

- **Integrated graphics (#42, #48, #63):** the same `DEVICE_HUNG` message, on weak built-in GPUs right after the opening cutscene. See `docs/fix/integrated_graphics_crash/`.
- **#67** (9070 XT, crash at boot on 1.4.0): closed as a duplicate of #42, but that CPU has no iGPU. The log is gone; probably unrelated.
- **#60, #66, #99:** AMD performance, not crashes.
