# Guest-image patch system (Xenia patch format)

Runtime patching of the loaded `default.xex` guest image, using the op shape
of Xenia's game-patches (`patch.toml`) format.

## How it works

- `src/core/fable2_patches.h` / `src/core/fable2_patches.cpp` — the patch table +
  applier. Each op is `{be8|be16|be32|be64, address, value}`, exactly the
  `[[patch.be32]] address/value` shape from Xenia's patch files.
- **No patch file.** The table is built in code; each patch's `enabled` comes
  from a `[patches]` key in `fable2_config.toml` (loaded in
  `OnPostInitLogging`, before the patches are applied). The old
  `fable2_patches.toml` is no longer read and can be deleted.
- `Fable2App::OnPostLoadXexImage()` (src/core/fable_2_app.h) calls
  `fable2::patches::ApplyAll(runtime()->memory(), PPCImageConfig)` — the SDK
  hook that runs after `default.xex` is decrypted/expanded into the guest
  arena and before the guest module launches (the SDK documents this hook as
  the place for data patches).
- The SDK marks XEX code/rodata pages **read-only** after load
  (`XexModule::LoadContinue` page-descriptor pass), so `PageWriteGuard`
  flips the touched guest pages to R/W via `heap->Protect()` and restores the
  original protection after the write. Safe at this point: no guest thread
  exists yet.
- Every op is logged: `[patches] <patch> be8 0xADDR: 0xOLD -> 0xNEW
  (data: takes effect at runtime)`, plus a summary line.

## Current patches

None. The Xenia "High Tick Rate" and "Higher HF Tick Rate" patches (fixed
30/60 Hz ticks) were removed: the dynamic tick rate below replaces them. The
table in `fable2_patches.cpp` is kept, empty, for future data patches.

This is the fundamental recomp vs. emulator split: **data patches work, code
patches don't** (a code patch here would mean editing the recompiled C++ at
build time, which codegen would clobber on the next run), so every code op is
a mid-asm hook.

### Dynamic tick rate (always on, no config key)

The HF tick runs once per presented frame, with no frame cap, and the HF rate
is set to the measured frame rate (LF always half of it), so each tick moves
game time by one frame's worth and every frame shows exactly one new tick.
A rate that only follows the frame rate is not enough: two clocks that are
merely close drift against each other, frames alternate between a new tick
and none, and text and motion stutter (seen in the first test at Unlimited).
Above 144 Hz (`kMaxHfHz`) it ticks every Nth frame (N = ceil(fps / 144)).
Under 30 fps the game's own 30/15 Hz timer runs, so a 30 fps frame limit plays
like the original game (30 Hz HF, 15 Hz LF); that is the way to get the
original timing back. Game init (`sub_8233AE50`) is the only other writer of
the two rate doubles; `fable2_hook_skip_tick_rate_store` skips both stores
(0x8233AE98, 0x8233AEB4) so they cannot undo the current rate. Code: `src/core/fable2_tick_rate.{h,cpp}`,
the frame counter call in `src/diagnostics/fps_meter.h`, and
`apply_dynamic_tick_rate` in
`src/core/hotfunc/frame/ProcessGameFrame_82276C30.cpp`.

How the game uses the rates (read from the codegen output):

- `ProcessGameFrame_82276C30` is the game's fixed-step loop. It reads both
  doubles **once, on entry** (`compute_frame_timing`: HF period, HF rate, and
  HF ticks per LF tick = `trunc(HF * (1/LF))`, which must stay 2), then loops,
  yielding until the next HF tick is due, and can stay in that loop for a
  whole session. Writing new values to the globals alone would leave the loop
  on the old rate while every other reader saw the new one, so the new rate is
  applied inside the loop, between ticks: the loop's own rate registers are
  re-derived the same way and its anchor moves to the last processed tick,
  keeping the tick phase continuous.
- HF (0x83319518) is read in only 4 places: the loop above, two `x / HF`
  conversions (`sub_82186A80`, `sub_82278AE0`), and a startup initializer
  (`sub_83242668`) that copies it to 0x83497420 for `sub_8235ABA0`. That copy
  always equals HF in the original game, so it is updated with HF.
- LF (0x83319510) is read live in ~450 places, mostly `x / LF` (per-tick
  delta time, ticks to seconds) and `seconds * LF` (seconds to ticks). No
  cached copy of LF was found.

The frame rate is the count of `MainRenderLoop_82B9CD68` calls (one per
presented frame), sampled every 0.25 s and smoothed; it decides one tick per
frame vs. every Nth frame vs. the game timer, and is what gets logged. In the
loop, the due-tick rate register (f25) is held at 0 until a new frame is
presented, then set so exactly one tick is due. That tick lasts exactly the
real time since the previous one (clamped to 1/(2 * max) .. 1/30 s): HF is
set to its inverse right before it runs, so game time advances by what each
frame actually took. With an uneven frame rate a fixed tick length moves
things too far on short frames and too little on long ones, which reads as
judder. The game clock is nudged by at most 5% of a period toward real time;
a gap over the clamp runs up to 3 catch-up ticks back to back, and a longer
one (a load) resyncs to now, like the original.

The render thread draws one LF period in the past (`sub_8236C520` subtracts
1/LF from now). Since LF now changes every frame, the hook
`fable2_hook_render_time_lf` (0x8236C5A4) gives that code the smoothed rate
instead, so the delay does not jump frame to frame.

Rate changes are logged when they move more than 10%:
`[tick-rate] dynamic: HF 143.8 Hz, LF 71.9 Hz, one tick per frame, each as long as its frame (frame rate 143.8 fps)`.

Known limits:

- A duration the game already converted to LF ticks (`seconds * LF`, then
  counted down per tick) keeps its tick count while the tick length changes,
  so a timer running across a frame-rate swing finishes slightly early or
  late. Durations converted on every tick follow the real time.
- Tested in game (2026-10-10, 30 to 90 fps on an RTX 5080 at Unlimited):
  normal game speed, cloth mostly fine, the frame rate about the same as
  with fixed 30/15 Hz ticks. Text and motion still judder when the frame rate
  swings hard; that is uneven frame delivery, which no tick timing can hide.
  Logic that counts LF ticks without going through the rate runs faster at
  high frame rates (the old fixed High Tick Rate patch had the same risk at
  30 Hz LF).
- Every HF tick costs CPU, so ticking at a high frame rate can lower the
  frame rate; the loop then simply follows the lower rate.

### Interpolation (`interpolation`, on by default)

The game updates gameplay, AI, scripts and the GUI only on LF ticks, which
are every other HF tick. Characters are already drawn blended between their
last two LF poses at the render time (the engine's own interpolator at
instance+176, applied by sub_8222E300), and the camera is blended with the HF
fraction. Two things were not, and this option fixes them:

- **Text (subtitles, HUD).** sub_82278C90 calls the front-end GUI update
  (vtable[2], sub_82286B40) only when an LF boundary was crossed. The GUI
  measures its own elapsed time from the wall clock (sub_822B6D70), so the
  hook `fable2_hook_gui_every_tick` (0x82278D1C) runs it on every HF tick
  without changing its speed. With the dynamic tick rate that is every frame.
- **Cloth colliders.** sub_82A895C0 builds the cloth's collision shapes from
  the raw current pose, while the body is drawn blended, so the colliders ran
  up to one LF tick ahead and jumped every LF tick.
  `fable2_hook_cloth_collider_bone` (four sites, after `add r11,r11,r9`)
  points each read at the same bone blended the way the renderer blends it.

Tested in game (2026-10-10): builds, runs, the
GUI updates every tick; no in-game report on cloth with it yet.

## Validation (2026-09-15)

- Built (`build.cmd fable_2`, debug), launched the game (all ops applied,
  ~7 min run, clean shutdown; the 60 FPS entry later moved to a mid-asm
  hook and is no longer a guest-image patch):
  ```
  [patches] applying 'High Tick Rate' by Guy (2 ops) - Doubles tickrate to 30hz. ...
  [patches]   High Tick Rate be32 0x8233AEB4: 0xd8089510 -> 0x60000000 (code region: ...)
  [patches]   High Tick Rate be8 0x83319511: 0x2e -> 0x3e (data: takes effect at runtime)
  [patches] done: 2 op(s) applied, 0 skipped
  ```
- Game ran ~7 minutes with patches applied, clean user-initiated shutdown,
  no access violations or protection errors (first run, before the
  read-only-page handling existed, did fault on the code op — fixed by
  `PageWriteGuard`).

## Recomp-level patches (making code patches work)

Since guest .text is never executed, code-region Xenia patches must be
applied to the **recompiled code**. Two mechanisms, in order of preference:

### 1. Mid-asm hooks (preferred) — SDK-native, declarative

The SDK's `[[entrypoint.midasm_hook]]` config (manifest) injects a call to a
C++ function at a specific guest instruction address during codegen
(docs: https://rexglue-rexglue-sdk.mintlify.app/config/hooks). Registers
named in the hook are passed **by reference** (`PPCRegister&`), so the hook
function can rewrite them — "value injection".

Wiring (all three pieces):
1. `fable_2_manifest.toml` → `[[entrypoint.midasm_hook]]` with `address`,
   `name`, `registers`, and `after_instruction = true` so the call lands
   right after the patched instruction executes.
2. `src/core/fable2_hooks.cpp` → the hook function, plain C++ linkage matching
   the prototype codegen auto-emits into the generated code
   (`extern void fable2_hook_website_g1(PPCRegister& r9);`).
3. Nothing else — codegen handles the rest, and it survives codegen re-runs
   by construction (the manifest is a codegen input).

**Runtime toggle (done):** the hook body is C++ in the game process and
consults `fable2::config::Get()` on every call — a `[patches]` key in
`fable2_config.toml` enables/disables the patch with no rebuild.
(New hooks should follow the same pattern: read their toggle from
`fable2::config::Get()`.)

### 2. Post-codegen text patches (fallback)

`tools/apply_recomp_patches.py` mirrors a patch into codegen's emitted C++
by anchored text replacement; wired into CMakeLists.txt as a step **between
codegen and compiling fable_2_recomp** (keyed on
generated/default/codegen.build.stamp). Idempotent via a unique
`// [recomp-patch: <name>]` marker; missing anchor = build failure.
`FABLE2_RECOMP_PATCHES=0` skips all (A/B runs). Use only for patches that
can't be expressed as hooks (e.g. constants baked into memory operands).
Currently **empty**.

### Current recomp-level patches

| Patch | Mechanism | Change | Measured effect |
|---|---|---|---|
| Unlock Website Items (Guy) | mid-asm hook `fable2_hook_unlock_website` @ 0x8256E384 (after `rlwinm r9,r10,0,25,25`); toggle: `[patches] unlock_website` | Forces `r9 = 0x40` (bit 6) in `sub_8256E368`, so the website/Guild-chest item lookup reads as unlocked and runs the real lookup. Re-derives the Xenia "Unlock Website Items" intent for THIS build (the stock ops target a different revision's bytes). | Hook confirmed in generated code + clean startup; in-game chest unlock pending manual test |
| Unlock CE Content (Guy) | mid-asm hook `fable2_hook_unlock_ce` @ 0x824B3540 (after `rlwinm r10,r11,0,25,25`); toggle: `[patches] unlock_ce` | Forces `r10 = 0x40` (bit 6) in `sub_824B3528`, so the Collectors-Edition chest item lookup reads as unlocked and runs the real lookup. Same re-derivation approach. | Hook confirmed in generated code + clean startup; in-game chest unlock pending manual test |

To add a new code patch: a `[[entrypoint.midasm_hook]]` entry + hook
function gated on a `[patches]` config key (preferred), or an
`apply_recomp_patches.py` PATCHES entry (fallback). If it also needs a data
write, add it to the table in `src/core/fable2_patches.cpp`, gated on the
same key.

### FPS meter

`src/diagnostics/fps_meter.h` (included from main.cpp): strong override of the weak
recompiled `MainRenderLoop_82B9CD68` (0x82B9CD68, main loop, one call per frame; renamed from `sub_82B9CD68`) that counts
invocations in 5 s windows and appends `mainloop rate=NN.N/s` to
`fps_meter.log` next to the exe. Forward-only (counts, then calls the
original `__imp__` entry). Enabled with `FABLE2_FPS_METER=1`.

## Follow-ups

1. ~~Tie to config~~ **done**: every patch, data or code, is toggled by a
   `[patches]` key in `fable2_config.toml` (the separate
   `fable2_patches.toml` was removed).
2. **Other Xenia patches** for this title (from
   `4D5307F1 - Fable II (GOTY).patch.toml`). Done as mid-asm hooks:
   **Unlock Website Items**, **Unlock CE Content** (all re-derived for THIS
   build — see the recomp-level patches table above). Remaining (still to port,
   each needs the same build-mismatch investigation — the stock ops target a
   different revision): 1280x720 (be16 0x8238DF5A=0x0500), Disable MSAA
   (be8 0x8238DF3F=0x01), Disable Texture Morphing (be16 0x8220EF10=0x4280),
   21:9 / 32:9 widescreen — all .text/data-in-.text, so expect the same "applied
   but inert" behavior for their code ops if applied to the guest image.
3. **Oddity worth investigating (separate issue):** the recompiled C++ for
   `sub_8233AE50` (generated/default/fable_2_recomp.65.cpp:3211) was compiled
   from a word at 0x8233AEB4 of `0xD9009510` (stfd f0,0x9510(r8)), but the
   current `default.xex` image contains `0xD8089510` (stfd f8,0x1510(r0))
   there — verified two independent ways: an independent AES-CBC +
   basic-compression-block expansion of default.xex, and a live read of the
   running process's guest arena (see scratch/fable2_image_decrypted_expanded.bin
   for the fully expanded image). Neighboring words 0x8233AE54 and
   0x8233AE98 differ too. Same file (SHA-verified, unchanged since
   2026-08-26) and the v0.10.0 XEX loader source is byte-identical to the
   nightly's — so the prebuilt codegen tool's image and the runtime image
   differ by a handful of bytes (register fields transposed / stray bits).
   Possibly a `be<enum>` struct-layout difference in
   `xex2_opt_file_format_info` between the prebuilt 0.10.0 tool and the
   source-built nightly runtime (block table at +8 vs +12). If any recompiled
   function's data operands come from those bytes, the recompiled code is
   subtly out of sync with what the runtime loads. Low priority (the game
   plays fine), but worth a dedicated look if anything mysterious happens.

## XEX2 decoding notes (for future reference)

`default.xex` (GOTY, media 716F0A0D): XEX2, header 0x4000, base 0x82000000,
image_size 0x1620000, session key = AES-CBC(retail_key, secinfo+0x150),
encryption=NORMAL (AES-CBC, zero IV), compression=BASIC = a block table of
`{data_size, zero_size}` pairs: the file stream is the concatenation of the
block data only; each block expands to `data_size` decrypted bytes followed by
`zero_size` zero bytes. Block table (this file):
`(0x168000, 0x8000), (0x1158000, 0x8000), (0x58000, 0x1D0000), (0x120000, 0)`.
