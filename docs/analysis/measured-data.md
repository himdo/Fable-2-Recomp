# Fable 2 Recomp on macOS: what the 2026-10-13 measurements say

Read-only analysis of `out/mac-measure/` (runs of 2026-10-13, same scene, M3 Pro). Script: `analyze.py` in this
directory (`python3 -I analyze.py mvk v log` for the MoltenVK/log tables; `sample_run`/`breakdown` for the samples).
Game logs: `fable_2_001` = fork_base, `002` = t3 (hang), `003` = t3off, `005` = t3diag, `006` = t3mirror,
`007` = full, `008` = full_noov, `009` = t3only, `010` = expadj (matched by launch time).

## Method and caveats

- MoltenVK blocks are cumulative averages; per-window values are block-differenced (60 presented frames each) and
  aligned to wall-clock with the run log mtime (+-2 s). "Steady 3D" = windows starting >= 12 s after the `sample`
  ended, i.e. launch+163 s to the end of HOLD (6-9 windows, 360-540 frames per run; only 2 windows for expadj and
  t3mirror, none for t3off whose run ended at +165 s).
- `sample` (1 ms, all threads, 10 s) perturbs the process: during the sample the MoltenVK frame rate fell
  (full 14.8 -> 7.3 FPS, fork_base 9.8 -> 4.9, t3only 13.7 -> 10, full_noov/expadj 14.5 -> 13.8) and the encode
  time rose 2-5x. Thread *shares* are used, converted to ms with the unperturbed steady frame time. Treat
  per-category ms as +-30 %.
- "Execute a MTLCommandBuffer on GPU" is commit-to-completion latency (queue wait + GPU time), not GPU busy time.
- The game's `guest frame (gate)` Hz and MoltenVK's frame interval agree to 0.1 FPS in every run: one present per
  guest frame, the pacer is not interfering.

## 1. Per-run MoltenVK numbers, steady 3D window

| run | FPS (mvk) | gate Hz med. | frame ms | cmd bufs/frame | exec latency ms | latency / frame | present ms | encode ms/cb | encode ms/frame | GPU mem MB |
|---|---|---|---|---|---|---|---|---|---|---|
| fork_base | 9.64 | 9.70 | 103.8 | 8.0 | 55.9 | 0.54 | 38.7 | 1.80 | 10.9 | 1616 |
| t3diag | 13.62 | 13.70 | 73.4 | 8.0 | 38.5 | 0.52 | 171 | 0.96 | 5.8 | 1609 |
| t3mirror (2 win) | 15.40 | 15.70 | 64.9 | 8.2 | 31.3 | 0.48 | 145 | 1.03 | 6.3 | - |
| t3only | 13.62 | 13.70 | 73.4 | 8.0 | 38.6 | 0.53 | 9.8 | 0.89 | 5.4 | 1595 |
| full | 14.80 | 14.80 | 67.5 | 7.0 | 98.0 | 1.45 | 4.4 | 1.05 | 5.2 | 1580 |
| full_noov | 14.81 | 14.90 | 67.5 | 7.0 | 97.6 | 1.45 | 9.8 | 1.08 | 5.4 | 1574 |
| expadj (2 win) | 14.95 | 14.95 | 66.9 | 7.0 | 96.9 | 1.45 | 92.1 | 1.05 | 5.3 | 1598 |
| t3off (entry only) | 7.9-8.1 | 7.85 | 125-130 | 7.1 | 181 | 1.4 | 140-165 | 2.4 | 17 | 1637 |

Other MoltenVK counters, all runs: drawable acquisition 0.01-0.02 ms; MTLCommandBuffer retrieval 0.004-0.01 ms;
vkQueueSubmit encoding == encode (same numbers). Shader work in the steady window: 0 SPIR-V->MSL conversions,
0 MTLLibrary compiles, 0 pipeline compiles in every run (all ~390 conversions and ~560-640 pipeline compiles
happen before/at scene entry). Metal pipeline compile cost per run: fork_base 77.5 ms avg x 644 (cold Metal
cache, first build run), expadj 75 ms x 560 (new MSL after the exp_adjust change), all other runs 0.1-6 ms
(Metal on-disk cache hits). GPU memory 1.57-1.64 GB of 13.6 GB.

Base vs t3only vs full: frame 103.8 -> 73.4 -> 67.5 ms (-30.4, -5.9). Command buffers per frame 8 -> 8 -> 7.
Encode per frame 10.9 -> 5.4 -> 5.2 ms. Completion latency 0.54 -> 0.53 -> **1.45 frames**: up to t3only the GPU
queue drains every frame (the memexport readback full path waits for the GPU), with himdo#12 command buffers
complete 1.45 frames after commit, i.e. the queue never drains and the GPU is continuously fed.

"Present swapchains in on GPU" is not usable as a bottleneck indicator: 4-10 ms in t3only/full/full_noov but
92-171 ms in t3diag/t3mirror/expadj at the same FPS and frame interval (window/display state artefact).

## 2. Where the frame goes (full, 67.5 ms)

Thread shares from the samples (percent of the thread's samples; "fence" = `mvkWaitForFences` reached from
`IssueDraw_MemexportReadback*`):

| run | 3D Engine cpu | GameThread cpu / yield | GPU Commands cpu / wait | fence (memexport) | MoltenVK record+encode | MVK descriptors | texture cache | IssueDraw bookkeeping | shared mem upload | register/packet parse | Metal dispatch thread cpu / wait |
|---|---|---|---|---|---|---|---|---|---|---|---|
| fork_base | 98.7 | 45 / 53 | 66 / 34 | 32.0 | 36.2 | 3.5 | 5.0 | 5.8 | 4.5 | 9.1 | 39 / 28 |
| t3only | 97.8 | 46 / 51 | 68 / 32 | 28.9 | 27.0 | 3.9 | 7.9 | 6.7 | 7.7 | 11.4 | 42 / 9 |
| full | 96.4 | 52 / 45 | 85 / 15 | 14.1 | 42.5 | 6.8 | 10.0 | 7.5 | 6.0 | 3.3 | 6 / 93 |
| full_noov | 97.5 | 49 / 48 | 80 / 20 | 18.4 | 41.0 | 6.5 | 11.1 | 6.6 | 3.8 | 3.0 | 1 / 99 |
| expadj | 96.9 | 51 / 45 | 83 / 17 | 15.9 | 40.7 | 6.7 | 10.1 | 8.8 | 4.0 | 3.7 | 1 / 98 |
| t3off | 97.6 | 63 / 34 | 84 / 16 | 7.2 (+5 other waits) | 39.4 | 4.8 | 7.5 | 7.4 | 8.2 | 13.5 | 41 / 41 |

Main (UI) thread: 1-2 % CPU, 98 % in `Cocoa_PumpEventsUntilDate`. GPU VSync thread: 0 %. Pipeline creation
threads: idle. The "Metal dispatch" column is `com.Metal.CommandQueueDispatch`; its "cpu" is time inside the
`iokit_user_client_trap` submit call (kernel-side wait, not user CPU), its "wait" is `io_connect_method`; the shift
between the two is a consequence of the submission pattern, not a cost.

Conversion to ms/frame for full (share x 67.5 ms, +-30 %):

| component (full) | ms/frame | basis |
|---|---|---|
| GPU busy | >= 60 (90 %+) | inferred: completion latency 1.45 frames (queue never empty) and t3only GPU ~ 59 ms (73.4 ms x ~80 % busy, same GPU work) |
| GPU Commands thread on-CPU | 54-57 | measured share 80-85 % |
| - of which MoltenVK vkCmd recording + encode | 25-28 | share 41-42 %; encode proper 5.2 ms measured by MoltenVK, the rest is MVKCommandBuffer/MVKCmd* recording and queue objects |
| - MoltenVK descriptor set updates | 4-5 | share 6.5-6.8 % |
| - texture cache | 7 | share 10-11 % |
| - IssueDraw/UpdateBindings bookkeeping | 5 | share 7-9 % |
| - shared memory uploads (T1 path) | 3-4 | share 4-6 % |
| - render target cache | 2 | share 3 % |
| - register / packet parsing | 4 | share 6-7 % (was 11-13 % before himdo#12) |
| GPU Commands thread fence wait (memexport slot of the previous frame) | 10-13 | share 14-19 % |
| Guest "3D Engine" | 65 (spin) | 96-98 % on-CPU in `FrameLimiterWait_82242628` -> `GpuProgressCheck_82B9BF90`: the guest polls the GPU progress counter; it is waiting for the GPU, it burns a core but is not on the critical path |
| GameThread | 33 cpu + 30 yield | own core, not waiting on graphics |
| Present / drawable acquisition / UI | < 1 | measured |
| Shader or pipeline compilation | 0 | measured (steady window) |

Answer: at 14.8 FPS the frame is **GPU-bound, with the GPU Commands thread's CPU time only ~10-13 ms behind**.
The CP thread runs about one frame ahead and then waits (10-13 ms) for the fence of the submission that wrote the
previous occurrence of the memexport readback slot; that wait is the queue-depth limiter, not a serialization
problem any more. Nothing waits on present, drawable acquisition or semaphores. The guest render thread is
throttled by GPU progress (busy-spin), not by the host.

In fork_base and t3only the shape is different: completion latency ~0.5 frame and 29-32 % of the CP thread in the
memexport *full-path* drain (`vkWaitForFences` after each memexport draw): CPU and GPU alternate, the GPU idles
while the CP thread encodes the next chunk (the 2026-10-12 Metal trace put the GPU at 82 % busy in that state).
That is why a pure CPU/synchronization change (himdo#12) still bought 6 ms.

WAIT_REG_MEM: the `long-waits ... total sleep 767 ms/s` lines in the logs are from the menus only (540-770
waits/s at 30 FPS); in the 3D window there are no such lines in any run, and the samples show 0 % of the CP thread
sleeping there. The CP thread does not sleep in the 3D scene.

## 3. What changed base -> t3only -> full, vs the design expectations

| step | frame ms | CP thread on-CPU ms (share x frame) | CP fence wait ms | encode ms/frame (MVK) | inferred GPU ms |
|---|---|---|---|---|---|
| fork_base | 103.8 | ~69 (66 %) | ~33 (32 %) | 10.9 | ~85 (82 % busy per mst_t1 trace) |
| t3only | 73.4 | ~50 (68 %) | ~21 (29 %) | 5.4 | ~59 |
| full | 67.5 | ~55 (80-85 %) | ~11 (14-19 %) | 5.2 | 60-65 |

- T3 (-30.4 ms): matches the plan's upper estimate (13.5 ms tessellation compute + 12.6 ms tessellated vertices +
  ~9 ms tessellation restarts = ~35 ms, "at most 25-30 ms"). But the saving is not GPU-only: the CP thread lost
  ~19 ms/frame of CPU (encode halved 10.9 -> 5.4 ms; 485 tessellated draws became instanced draws without
  render-pass restarts on the MoltenVK side). In a drain-serialized frame CPU savings count 1:1, so the 30 ms is
  the sum of GPU and CPU savings, and the GPU-only part is roughly 26 ms (85 -> 59).
- himdo#12 (-5.9 ms): CPU/sync only, as described. Fence wait -10 ms, register-write parsing 11 % -> 3 %, but the
  per-occurrence readback slots and the deeper queue add some CP work (share up to 80-85 %); net -6 ms. It also
  changed the Metal submission pattern (8 -> 7 command buffers per frame).
- Overlay: full vs full_noov identical (67.5 / 67.5 ms). Confirmed "no cost".
- exp_adjust: 66.9 vs 67.5 ms (+1 %, 2 windows, 12 gate lines): noise, not a gain. The run also paid ~560 Metal
  pipeline recompiles at 75 ms each at scene entry (new MSL).
- MSAA (-48 ms at base), point sprites (-14 ms), tessellation factor 1 (-6 ms), render-pass restarts (~10 ms):
  not re-measured on the T3/full build; see section 5 for how they combine with the CPU numbers above.
- t3off (T3 plugin, emulation off) measured 7.9-8.1 FPS at scene entry, below the 9.7 base. It is the only run
  without HOLD (log ends at +165 s) and the entry phase is slower in every run (full_noov ran at 9.5-10 FPS until
  +145 s before settling at 14.8), so this is probably not a real regression, but "t3off == base" was not
  verified by these data.

## 4. The t3 hang at the title screen (run_t3, log 002, sample_t3)

Facts:
- Window stayed white (the capture shows an empty white client area): no frame was ever presented. run_t3.log has
  no MoltenVK statistics block at all (< 60 presents in 135 s). The pacer logged 0.1 Hz then 0.0 Hz.
- Log 002: 3 pipelines created at +0.7 s (two on the CP thread, one on a creation thread), then
  `Skipping Vulkan frame presentation due to async placeholder draw usage in this frame` at 21:36:37.732, then
  nothing graphics-related for 2 minutes. Log 001 (base) shows exactly the same three pipelines and the same skip
  message at +0.9 s, followed 0.7 s later by the next pipelines and 21-30 Hz frames.
- sample_t3 (2081 samples): GPU Commands 99.3 % in `CommandProcessor::WorkerThreadMain -> rex::thread::Wait`
  (no commands in the ring buffer); all 8 `VulkanPipelineCache::CreationThread`s and the storage thread 100 %
  idle; Metal dispatch/completion queues idle; main thread in `Cocoa_PumpEventsUntilDate`; GPU VSync thread
  sleeping normally. "3D Engine": 78 % `rex::thread::MaybeYield` (guest `Yield_82CBD098`) + 13 % CPU inside
  guest `sub_82B89960 <- sub_82B69488 <- sub_8236C520 <- sub_8236C360 <- sub_82CCA400`, a yield-spin that is
  *not* the normal limiter spin (`FrameLimiterWait_82242628 / GpuProgressCheck_82B9BF90`). GameThread 100 % in
  `RtlEnterCriticalSection -> xeKeWaitForSingleObject` (blocked on a critical section), Main XThread and several
  XThreads in `XObject::Wait`.

Interpretation: it is not a GPU hang (nothing in flight), not a pipeline-compile deadlock (creation threads idle,
no pending request), not MoltenVK. The host has no work; the guest render thread spins on a condition only the
GPU/host side can satisfy while holding a critical section the game thread needs, so the whole guest stalls. The
only guest-visible event that differed from a normal start is that the very first frame's draws ran on async
placeholder pipelines and its presentation was skipped (`vulkan_async_skip_incomplete_frames`, default true;
`IssueSwap` returns after `EndSubmission` without presenting). The base run survived the same skip, so a race is
required; the T3 build widened the window because `Modification::kVersion` 12 -> 13 invalidated the pipeline
storage (every first-frame pipeline went through the async placeholder path, hence only 3 pipelines logged), and
the four later launches had a warm cache, which explains the non-reproduction. Most likely mechanism: the guest's
first-frame completion wait (GPU progress counter / swap or vblank flag) was satisfied in the base timing but
missed in this one: a lost wake-up between the first `XE_SWAP` and the guest's wait loop, with the frame-skip
path as the suspect. Confidence: medium-low (no guest PC captured, no probe of the awaited address).
To test without guessing: cold-start with the pipeline storage deleted and (a) `--async_shader_compilation=false`
or (b) `--vulkan_async_skip_incomplete_frames=false`; if it reproduces, dump the address `sub_82B89960` polls.

## 5. Ranked remaining bottlenecks (full build, 67.5 ms/frame) and what is measured vs inferred

| # | bottleneck | est. ms/frame | confidence | status |
|---|---|---|---|---|
| 1 | GPU fragment work on the 3 main passes with MSAA 2x/4x | 45-50 of the ~62 ms GPU | medium | inferred from the 2026-10-12 A/B at base (MSAA 1x: -48 ms); additivity with T3 not re-measured |
| 2 | GPU Commands thread CPU | 54-57 | medium-high (total), medium (split) | measured share x steady frame; the next wall: ~18 FPS cap even with zero GPU cost |
| 2a | - MoltenVK command recording + encoding (+ descriptor updates) | 30-33 | medium | share 47-49 %; encode proper 5 ms measured |
| 2b | - texture cache + IssueDraw bookkeeping + RT cache | 14 | medium | shares 10 + 7-9 + 3 % |
| 2c | - shared memory uploads + register/packet parsing | 7-8 | medium | shares 4-6 + 6-7 % |
| 3 | CP fence wait on the previous memexport slot | 10-13 | high (measured) | queue-depth limiter; only pays off once the GPU is faster than the CP thread |
| 4 | Point sprites (snow) | ~14 GPU | low-medium | 2026-10-12 A/B at base, not re-measured |
| 5 | Render-pass / encoder restarts other than tessellation | <= 10 | low | 2026-10-12 trace estimate, not re-measured |
| - | Present, drawable acquisition, UI thread, shader compilation, GPU memory, guest 3D Engine spin, GameThread | ~0 | high | measured; none is on the critical path |

Consequence for the plan: the plan's implicit assumption that GPU savings convert 1:1 into frame time holds only
down to ~55 ms/frame. If MSAA removal delivers its -48 ms on this build, the GPU would be at ~15-20 ms but the
frame would stop at the CP thread's ~55 ms (~18 FPS). 30 FPS needs both: GPU <= 33 ms *and* the GPU Commands
thread <= 33 ms (-40 % CPU, mostly MoltenVK recording/encoding, descriptor updates and texture-cache work).

Measured (directly in these files): frame times and FPS per run; command buffers per frame; completion latency;
encode times; shader/pipeline compile counts; GPU memory; thread on-CPU vs wait shares and wait sites; the hang's
thread states; absence of WAIT_REG_MEM sleeps and of present/drawable waits in 3D.
Inferred: GPU busy ms (from latency shape and the t3only -> full comparison); the ms split of the CP thread (sample
shares scaled by the steady frame time; the sampler slowed the process 1.05-2x); everything about MSAA, point
sprites and pass restarts (from 2026-10-12 experiments on a different build); the hang mechanism.

Points where memory.md / TODO.md are contradicted or need a correction:
- TODO baseline "CPU explica el 8 %": on the current build the GPU Commands thread is on-CPU 80-85 % of the frame
  (54-57 ms). The CPU is the second wall and becomes the first after any GPU win larger than ~12 ms.
- memory.md "T3 ahorra como mucho ~25-30 ms/frame ... lo grande es el fragment": the 30 ms measured includes
  ~19 ms of CP-thread CPU (encode halved); the GPU-only part is ~26 ms. The fragment claim is still plausible but
  was never measured on the T3 build.
- "exp_adjust ~15 FPS": 14.95 vs 14.80, two windows; it is noise, not an improvement (and it was the right fix
  for other reasons).
- MoltenVK "Execute a MTLCommandBuffer" in memory.md (43 ms, "cola, no solo GPU") is correctly described as queue
  latency; note it is 98 ms on the current build *because* the queue is now deep, not because the GPU got slower.
- The earlier "hilo 3D Engine espera a ese hilo (GpuProgressCheck)" is correct, but it is a busy-spin in the
  hotfunc, 96-98 % of a core in every run; it is not a cost on the critical path and does not change with FPS.
- The WAIT_REG_MEM diagnostic lines ("total sleep 767 ms/s") belong to the menus; they do not describe the 3D
  scene and must not be read as CP idle time there.
- t3off was slower than base (7.9 vs 9.7 FPS) in the only window it has; "emulation off == base" is unverified.
