# Fable 2 (ReXGlue) 30 fps cap — investigation log

Goal: run the recompiled Fable 2 uncapped / at 60 fps instead of the native 30 fps lock.

## ✅ RESOLVED — uncapped (20–100 fps observed)

**Fix:** run with the SDK `vsync` cvar disabled → env var **`REX_VSYNC=0`**.

With `vsync=false` the ReXGlue vblank worker fires at ~1000 Hz instead of 60 Hz
(`graphics_system.cpp` vblank worker: `vsync ? vsync_interval : guest_tick/1000`),
so the GPU vblank counter the guest paces on advances ~1000/s. The guest's
per-frame "wait for the counter to advance +2 units" gate (2 × 16.6 ms = 33 ms
= 30 fps) then completes in ~2 ms → the frame cap is gone.

No rebuild required; it's a runtime cvar. Confirmed: vblank callback rate
jumps ~60/s → ~1000–2500/s, and the game runs uncapped (user-verified 20–100 fps
via F3, no timing breakage).

**Permanent launcher:** `tools/fable2-uncapped.cmd` (staged next to the exe by
CMake). It sets `REX_VSYNC=0` and calls `fable2.cmd`. To run capped (native
30 fps), use `fable2.cmd` directly (leaves `REX_VSYNC` unset → default true).

### Why not the other candidates
- CPU frame limiter (`sub_82242628`) — no-op'ing it made the main loop free-spin
  at 18M/s but FPS stayed 30 → it is NOT the gate (secondary CPU wait only).
- GPU `WAIT_REG_MEM` pacing — real, but it's driven by the same vblank counter;
  disabling `vsync` removes the pacing at the source (no SDK patch/rebuild needed).

### Cleanup done
- `src/fps_probe.h` instrumentation disabled (include commented out in
  `fable_2_app.h`); file kept for re-measurement.
- SDK `command_processor.cpp` `[wrm]` instrumentation reverted (git clean).
- Temp SDK build tree + script removed.
- `FPS_CAP_INVESTIGATION.md` kept as the record.

---

## Setup / how to reproduce measurements

- Build: `cmd //c "build.cmd -release fable_2"` (project root)
- Run: `Set-Location out/build/win-amd64-release; cmd //c "fable2.cmd d3d12"` (~110 s timeout to reach title loop)
- Probe: `src/fps_probe.h` (included from `src/fable_2_app.h:23`). Strong `extern "C"`
  overrides of weak recompiled symbols forward to `__imp__sub_XXXX` after logging.
  Logs to `fps_probe.log` in CWD.
- Guest arena host base = `0x100000000` (identity map for guest addrs < 0xE0000000;
  +0x1000 host offset for ≥ 0xE0000000, per `REX_PHYS_HOST_OFFSET` in `fable_2_pch.h`).
  Guest values are **big-endian** on the host (use `__builtin_bswap32`).

## What was ruled out (earlier sessions)

| Candidate | Result |
|---|---|
| `sub_82BA2568` frame_wait (KeQuerySystemTime poller) | Never called (flag at +23968 off) |
| `sub_82CBD098` yield | ~150 M calls, negligible duration — not the limiter |
| `sub_82CC2028` KeDelayExecutionThread wrapper | Not the primary limiter |
| Host vblank worker at 30 Hz | No — guest vblank cb `sub_82B9B8D8` max interval ≈ 19 ms → ~60 Hz |
| `sub_82CC2948` GPU cb, `sub_82B9F598` render thread | Never fire on title screen |

## Key guest functions (recompiled, `generated/default/`)

| Addr | Role |
|---|---|
| `sub_82B9CD68` (recomp.246:20628) | Main render loop; ~33 ms period |
| `sub_82242628` | **Frame limiter / GPU-progress wait** (h4; 30–53 ms per call) |
| `sub_82B9BF90` (h5) | GPU "has it progressed ≥ 5000 ticks?" check, called in the limiter's spin loop |
| `sub_82B9BA58` (recomp.289:19146) | "Begin frame"; only writer of the limiter struct field +10908 |
| `sub_82B9B8D8` (recomp.93:20842) | vblank interrupt cb (~60 Hz); clears flag bits under spinlock, calls user cb |
| `sub_82B9C530` | GPU-interrupt handler (timestamp + counter under spinlock) |
| `sub_821E8D20` (h13) | Fast helper (maxdur 0.036 ms) — not a sleep |

## Limiter anatomy (`sub_82242628`)

Signature: `limiter(limit*, now, r5=flag)`, `r6` mostly 0.

- `limit*+10896` = pointer to a **global counter struct** (live value `0xFFC83000`)
- `limit*+10908` = **deadline** (counter units)
- Wait: spin calling `sub_82B9BF90` until `*counter >= deadline`
- `sub_82B9BF90` reads `*counter` and a command-buffer value (+88 of a ptr at
  `r13+256`), and returns ready when progress since last check ≥ **5000** (ticks)
  or the deadline is met; also gates on a flag byte at video-struct +10941 bit 1.

### Live values captured (title loop, probe `limiter` lines)

```
now4=0000018B f10896=FFC83000 gfc=0000018B f10908=0000018F r5=3
now4=0000018D f10896=FFC83000 gfc=0000018D f10908=00000191 r5=3
now4=0000018F ... gfc=0000018F f10908=00000193
```

- `now` (r4 at entry) == `*counter` (gfc) at call time
- `deadline (10908) == gfc + 2` always
- gfc advances **+2 per limiter call**; limiter calls are ~33 ms apart →
  counter ≈ 60 units/s (either +1 per 60 Hz vblank, or +2 per 30 Hz frame —
  both consistent so far; high-frequency sampling added to disambiguate)

### Begin-frame deadline math (`sub_82B9BA58`, recomp.289:19537+)

```
lwz  r11, 10908(r31)      ; frame field
lwz  r10, 10896(r31)      ; global counter ptr
addi r11, r11, -2
stw  r11, 0(r10)          ; *counter = field10908 - 2   (keeps gfc == now)
```
Plus init: field 10908 = 3 if 0; and `*counter+4 = field48 | (field14920 & 3)`,
`*counter+60 = r29`, `VdSetSystemCommandBufferGpuIdentifierAddress(counter+8)`.

**Interpretation:** the game paces itself by waiting for the GPU progress counter
to advance **2 units past the frame start** — i.e. two 60 Hz vblank ticks =
33 ms → 30 fps.

## Decisive experiment (this session)

`FPS_PROBE_NO_LIMITER=1` makes `sub_82242628` return 1 immediately (no spin).
Result: **main loop free-spins at 8–18 M calls/s** (`mainloop summary rate=`),
yet the F3 overlay still reads **30 fps**.
→ The CPU limiter is NOT the frame gate. The gate is the **GPU command
processor**: it executes the guest's `WAIT_REG_MEM` packet (polls the vblank
counter, `Sleep(wait/0x100)` ms until advanced) at ~30 Hz. Frames only present
when the GPU finishes a frame's commands, which is paced at 33 ms.

## The vblank counter chain

- SDK `GraphicsSystem::MarkVblank()` (graphics_system.cpp:319) fires at the
  guest video-mode refresh rate (60 Hz, `vsync` cvar default true) and calls
  `command_processor_->increment_counter()`.
- The counter value is written back to guest memory by the GPU packet
  `EVENT_WRITE_SHD` (command_processor.cpp:1209, `data_value = counter_`) at
  the heap address the game set up (observed `0xFFC83000` this session; the
  pointer is stored at `video_struct+10896`).
- The game's CPU limiter (`sub_82242628`) and its GPU `WAIT_REG_MEM` both wait
  on this counter advancing **+2 units = 2 vblanks = 33 ms** per frame → 30 fps.
- End-of-frame `sub_822981F0` (recomp.33:1934) does `field10908 += 2` (the
  per-frame deadline advance) and rewrites the counter globals; begin-frame
  `sub_82B9BA58` sets `*counter = field10908 - 2` and calls the limiter (r5=4).

## Uncap plan (GPU-side, in SDK source)

Patch `CommandProcessor::ExecutePacketType3_WAIT_REG_MEM`
(command_processor.cpp:978): when the wait polls the vblank counter and blocks,
exit **1 unit early** (or cap the sleep so the effective wait is 1 vblank).
Rebuild `rexgpu-xenos.dll` (D3D12) from `rexglue-sdk-src` and drop it next to
the exe (the game loads the plugin DLL by name; the prebuilt `.lib` is just an
import stub).

Instrumentation added (temporary): logs every memory `WAIT_REG_MEM` with
`wait_info/addr/ref/mask/wait` (first 40) plus total block ms when ≥ 3 ms.

## If revisiting

- To re-measure frame pacing: uncomment the `#include "fps_probe.h"` in
  `src/fable_2_app.h` and rebuild.
- If a *capped-but-smoother* 60 fps is ever wanted instead of fully uncapped,
  the surgical option is a source patch making the vblank worker run at exactly
  2× (120 Hz) so the guest's "+2 units" = 16.6 ms; that needs a plugin/runtime
  rebuild from `rexglue-sdk-src`.
- `REX_VSYNC=0` is the shipped solution (fully uncapped).

## Files

- `src/fps_probe.h` — all probes + limiter experiment (temporary)
- `src/fable_2_app.h` — includes probe (no-limiter define reverted)
- `D:/projects/hacking/Windows/rexglue-sdk-src/src/graphics/command_processor.cpp`
  — `[wrm]` instrumentation in `ExecutePacketType3_WAIT_REG_MEM` (temporary)
- `D:/projects/hacking/Windows/rexglue-sdk-src/out/build/d3d12-fps/` — fresh
  D3D12 build tree (Ninja Multi-Config, Release), building `rexgpu-xenos`
- `out/build/win-amd64-release/fps_probe.log` — latest probe output
- `generated/default/` — recompiled guest code (read-only; regenerated by build)
- SDK source: `D:/projects/hacking/Windows/rexglue-sdk-src` (v0.10.0)
  - `src/graphics/graphics_system.cpp:149-180` — host vblank worker (uses guest
    `VdQueryVideoMode().refresh_rate`, `vsync` cvar default **true**)
  - `src/graphics/command_processor.cpp:38` — `vsync` cvar
  - `src/system/xmemory.cpp:134+` — guest arena mapping (4 GB, identity at 0x100000000)
