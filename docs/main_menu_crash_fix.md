# Fix: D3D12 / Vulkan source builds crashed at the main menu

**Date:** 2026-09-05
**Symptom:** `fable2.cmd d3d12` and `fable2.cmd vulkan` (the source-built
rexglue runtime + plugin) crashed as soon as the game reached the main menu,
with:

```
Unhandled guest access violation: read of guest 0x0000001C (host 0x000000010000001C)
  gpr: ... r4=0x00000000 ... r11=0x40001880 ...
  lr=0x83231C3C
```

plus `0x80000003` (int3) traps on several worker threads. The `prebuilt`
mode (stock 0.10.0 DLLs) was unaffected.

## Root cause

The crash is a NULL dereference inside the *game's own* memory-pool manager:

- `sub_83231BE8` reads the pool descriptor pointer from static
  `*0x83496BD4` (value `0x40001880`, valid), then reads the pool's
  free-list head `*(*(0x83496BD4) + 12)`, which was **0**.
- It passes that NULL head to `sub_832304E8`, whose first instruction
  `lwz r9, 28(r4)` faults at guest `0x1C`.

At startup the same field was non-NULL (init probe: `+12 (head) =
0x400018B0`), so something between startup and the main menu reset the pool
state. The difference between the working prebuilt and the crashing source
builds is **not** the GPU backend (both D3D12 and Vulkan crashed identically,
and the prebuilt D3D12 plugin works) — it is the local SDK modification in
`src/system/xmemory.cpp`, `BaseHeap::AllocFixed`.

The local "AllocFixed" patch made a **re-reserve of an already-reserved range
succeed (no-op)** on the theory that real XMem treats it as idempotent. That
theory is wrong for Fable 2:

- Fable 2 re-reserves **3856** fixed ranges during startup (visible as
  `[error] BaseHeap::AllocFixed attempting to reserve an already reserved
  range` in every prebuilt log).
- The game **branches on the result**: `X_STATUS_SUCCESS` is treated as a
  *fresh* allocation, which re-initializes the game's memory pools on top of
  the range and wipes the free-list heads.
- Stock behavior (fail → `X_STATUS_NO_MEMORY`) makes the game keep its
  existing pool state → works.
- Patched behavior (success) makes the game re-init pools at the wrong time →
  free-list head is NULL → main-menu crash.

Evidence (release build `logs/`):

| run | backend | AllocFixed behavior | AllocFixed errors | result |
|-----|---------|---------------------|-------------------|--------|
| fable_2_035 | D3D12 prebuilt | original (fail)   | 3856 | runs fine |
| fable_2_001 | D3D12 prebuilt | original (fail)   | 3856 | ran 13 min |
| fable_2_022 | D3D12 source   | patched (success) | 0    | main-menu crash |
| fable_2_040 | D3D12 source   | patched (success) | 0    | main-menu crash |
| fable_2_041 | Vulkan source  | patched (success) | 0    | main-menu crash |

`xmemory.cpp` is byte-identical between SDK v0.10.0 and the source tree
base, so the local patch was the only memory-behavior difference.

## Fix

`rexglue-sdk-src/src/system/xmemory.cpp`, `BaseHeap::AllocFixed`:
restored the original v0.10.0 logic — a reserve touching an already-reserved
page logs the error and **returns false**. (See NOTE comment in the code.)

Additionally hardened the crash diagnostic in
`rexglue-sdk-src/src/core/exception_handler_win.cpp`
(`LogUnhostedException`): it now checks every guest-memory read with
`VirtualQuery(MEM_COMMIT)` before dereferencing. Previously the diagnostic
itself raised a second access violation (reading the unmapped size-table at
`0x80180040`) while handling the crash, corrupting the crash log.

Rebuilt `rexruntime` (source, dual-backend) and re-staged:

- `out/build/win-amd64-release/rexruntime-vulkan.dll` (+ `.pdb`)

The GPU plugin was not changed.

## Verification

`tools/run_timed.ps1 <mode>` launches via `fable2.cmd` and auto-kills after
30 s:

- `d3d12`  → no guest AV, survives 30 s, main menu visible
  (`menu_proof_d3d12.png`).
- `vulkan` → no guest AV, survives 30 s+ (menu shader compilation is slower;
  first present arrives later than D3D12 — see note below).
- `prebuilt` → unchanged (stock DLLs, not touched by this fix).

## Notes

- The 3856 `[error] BaseHeap::AllocFixed attempting to reserve an already
  reserved range` lines at startup are **expected and harmless** now — the
  game relies on them failing. Do not "fix" them to success again.
- If the source runtime is rebuilt, stage it as
  `out/build/win-amd64-release/rexruntime-vulkan.dll` (the launcher copies it
  over `rexruntime.dll` on every launch).
- `tools/run_timed.ps1` (timed launch + log summary) and
  `tools/capture_menu.ps1` / `tools/capture_multi.ps1` (menu screenshots)
  were added for this debugging.
