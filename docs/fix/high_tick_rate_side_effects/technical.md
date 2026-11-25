# High tick rate side effects: technical notes

**Status:** no commit yet (research only) · **Commits:** none · **Last checked against:** 7863041

Claims are labelled **Verified** (checked in this repo or a primary source), **Reported** (a source says so, not checked here) or **Inference** (reasoned, not tested). This came out of the [November 2026 research](../../reference/research/README.md). Code was read in `src/`, the hand-tuned overrides and `fable_2_manifest.toml`; `generated/` isn't available on the machine this was written on, so nothing here was read in the generated code itself.

## Symptom

**Reported:** with `[patches] high_tick_rate` (and `higher_hf_tick_rate`) on, systems that count ticks run at the new rate. Blacksmithing and other job minigames feel easier, and timing-based systems such as expression holds behave differently (#216). The option itself, its data patch and its mid-asm hook are documented in [guest_patches](../../feature/guest_patches/technical.md).

## The options as they are today

### Config keys (Verified)

All three live in `[patches]` of `config/fable2_config.toml`, with the same text in the `kTemplate` string of `src/core/fable2_config.cpp` (read in `Load()`, lines ~666–674).

| Key | Default | What it does | Log line |
| --- | --- | --- | --- |
| `high_tick_rate` | `false` | LF tick 15 → 30 Hz (Xenia "High Tick Rate" by Guy) | `[tick-rate] LF tick forced to 30 Hz (high_tick_rate)` |
| `higher_hf_tick_rate` | `false` | HF tick 30 → 60 Hz (Xenia "Higher HF Tick Rate" by Ultra). Ignored unless `high_tick_rate` is on | `[tick-rate] HF tick forced to 60 Hz (higher_hf_tick_rate)`, or a warning that it was ignored |
| `gui_every_hf_tick` | `true` | Runs the GUI update on every HF tick instead of every LF tick | `[gui-every-hf-tick] on: GUI updates every HF tick` |

- **Launcher (Verified):** `launcher/Fable2.Launcher/MainWindow.xaml.cs` lists "High Tick Rate" and "Higher HF Tick Rate" as **(unstable)** patch rows (red accent). Ticking Higher HF ticks High Tick Rate; unticking High Tick Rate unticks Higher HF. `Fable2ConfigFile.cs` reads and writes both keys (`HighTickRate` is commented `// unstable`).
- **Restart needed (Verified):** each hook caches its config value in a function-local `static` on first use (`src/core/fable2_hooks.cpp`), and the data patch is applied once at load, so changing a key takes effect on the next launch.

### How the rates are set (Verified)

Two guest doubles hold the rates: the **LF rate at 0x83319510** (15.0) and the **HF rate at 0x83319518** (30.0, copied from 0x833194F0). Game init (`sub_8233AE50`, still unnamed) writes both every time it runs: the HF store at 0x8233AE98, the LF store at 0x8233AEB4.

Each option has two halves, gated on the same key:

1. **Data half** in `src/core/fable2_patches.cpp` (`BuildPatches()`): one big-endian byte per double. `0x83319511 = 0x3E` turns 15.0 into 30.0; `0x83319519 = 0x4E` turns 30.0 into 60.0. Higher HF's data half is gated on `high_tick_rate && higher_hf_tick_rate`.
2. **Code half**: the mid-asm hooks `fable2_hook_high_tick_rate_skip_store` (0x8233AEB4) and `fable2_hook_high_hf_tick_rate_skip_store` (0x8233AE98) in `src/core/fable2_hooks.cpp`, declared in `fable_2_manifest.toml` with `jump_address_on_true`. Returning true jumps past the store, which is what the Xenia patch's NOP did. Without them the game overwrote the patched values and the data patch had no effect.

The 2:1 HF:LF ratio matters: with `high_tick_rate` alone both run at 30 Hz (1:1) and cloth misbehaves; Higher HF alone (60:15, 4:1) is refused for the same reason.

### How the game schedules ticks (Verified in the hand-tuned copy; meaning is Inference)

`ProcessGameFrame_82276C30` has a hand-tuned override in `src/core/hotfunc/frame/ProcessGameFrame_82276C30.cpp` that keeps every guest statement in the original order. Its `compute_frame_timing` and `process_frame_work` blocks show the scheduler:

- **Periods and ratio.** It reads both doubles each frame. The HF period is `k / HF` and the LF period is `k / LF`, where `k` is the double at 0x8209FFD8 (Inference: 1.0). The number of HF ticks per LF tick is `max(int(HF / LF), 1)`: 2 by default and with both options, 1 with `high_tick_rate` alone.
- **Wall clock in, whole ticks out.** Time comes from the inlined `GetTimebase_8221EB58`, made into seconds with the base and frequency doubles at 0x83496EC0/0x83496EC8. The target tick count is `elapsed seconds × HF rate + ticks already done`, truncated to an integer, and the loop runs the difference. So the game is already a real-time accumulator, like the Deadly Premonition fix below: raising the rate gives *more ticks per second*, each one *shorter*.
- **LF boundary.** On each HF tick, an LF boundary is crossed when `tick / ratio` goes up. That flag is passed to `DispatchFrameCallback_821A6010` (r5) and to `GameFrame_HfTickUpdate_82278C90` (r6), together with the tick's game time (`frame+272`, built from the tick index × HF period) and the fraction of the LF tick reached so far (`(tick mod ratio + 1) / ratio`).
- **Consequence (Inference):** anything that uses the period or the fraction stays at the right speed. Anything that adds a fixed amount per tick, or counts ticks against a fixed number, runs at the new rate.

### What already uses real time or the rate (Verified unless marked)

| System | How it times itself | Where |
| --- | --- | --- |
| Tick scheduler | Wall clock → whole HF ticks | `ProcessGameFrame_82276C30` (above) |
| Front-end GUI, subtitles, HUD text | `Timer_GetElapsed_822B6D70` (start stored at `timer+104` from `GetTimebase_8221EB58`, then `(now − start) × const`) | `GuiManager_Update_82286B40`, made per-HF-tick by `fable2_hook_gui_every_tick` (0x82278D1C → 0x82278D40) |
| Character drawing, camera | Blended between the last two LF poses with the LF fraction | `Pose_BlendForDraw_8222E300`; cloth follows since `[patches] interpolation` (see [guest_patches](../../feature/guest_patches/technical.md)) |
| Ranged aim cursor | `cursor += input × k × speed / LF tick rate` | `AimCursorUpdate_827B80B0` ([mouse_aim](../../feature/mouse_aim/technical.md)) |
| Host mouse code | Assumes one camera update = one LF tick: 66,667 µs, or 33,333 µs when `high_tick_rate` is on | `DefaultWindowUs()` in `src/input/mouse_stick.h`, used for `RouteTimeoutUs` (`src/input/mouse_direct.h`) and the aim timeout (`src/input/keyboard_gamepad.h`); `tests/native/test_mouse_aim.cpp` checks both values |
| Game scripts | 222 calls of `Timing.GetTickRate` in 86 files, `Timing.SecondsToFrames` / `FramesToSeconds`, and waits given in seconds (`Timing.Wait(seconds)`, `HaveSecondsElapsedSince(world_frame, seconds)`) | **Reported:** `FABLE2_LUA_API.md` and `docs/reference/lua_call_shapes.md`. Whether these convert with the patched rate is not known |

### Candidates that count ticks (Inference until tested)

| Candidate | Why it's suspect | Notes |
| --- | --- | --- |
| Job minigames (blacksmith, woodcutter, bartender) and expression holds (#216) | Feel easier/different at the higher rate (Reported). The meter or cursor probably steps a fixed amount per LF tick while its window keeps its width | Native code not located yet |
| Script timers built on `Timing.GetWorldFrame` | 548 calls in 106 files (Reported, `FABLE2_LUA_API.md`). A world frame is a tick count, so "N frames since" means half the time at 30 Hz unless the script converts with `GetTickRate` | `GetCombatInfo.LastAttackTick` and `Combat.GetLastEnrageTick` are stored as ticks (Reported, `lua_call_shapes.md`) |
| `Navigation.PreventMovementForXFrames` | Takes frames, not seconds | Reported, `FABLE2_LUA_API.md` |
| Rent and income | **Reported** on branch `feature/game-difficulty` (its `docs/feature/difficulty_settings/technical.md`): `Ownership_PayDueRent_828AEA50` / `Ownership_UpdateIncome_828AECD0` pay out once "the game tick passes" a stored value. If that is an LF tick count, rent arrives twice as fast | Names exist on that branch only |
| Time of day and world clock | `Timing.GetWorldSecondsPerSecond`, `SetDaySpeed`; unknown whether the clock advances per tick or per second | Testable with the `time` console command |
| Ring-buffer/pad tick objects | `RingBuffer_TickPadWithFillByte_832B68A0` limits itself by a tick rate in ms (manifest comment), so it already uses time (Inference) | Probably unaffected |

### Why job minigames feel easier (Inference)

Blacksmithing pay is worked out by the job script per success (Reported, difficulty branch), so it isn't a pay bug. The likely cause is the meter: if its cursor moves a fixed step per LF tick, at 30 Hz it moves twice as fast, but if the *input* is also sampled twice as often, the player gets twice the chances to hit a window of the same width. Which of the two dominates hasn't been measured.

### How other recomps fixed the same thing (all Reported)

Deadly Premonition's recomp unlocks the logic tick from the 30 FPS vblank gate and feeds it whole ticks through an error accumulator. Its README says this "changes the game's time base rather than just doubling the frame rate", and the doubled sounds and prop swaps at 60 FPS went away with it.\[1\] Fable II's own scheduler already works that way (above), so the remaining work is in the per-tick consumers.

| Project | What it did |
| --- | --- |
| Deadly Premonition | Keeps the console's tick semantics and accumulates real time into whole ticks.\[1\] |
| LARecomp | Its 2x speed bug needed hooks on *two* fixed-timestep paths, and the timer reset guard had to stay, because dropping it caused an audio blowout.\[2\] |
| Astral Chain PC recomp (PR #62) | Takes the 60 FPS step from the game's own frame timer "once per game frame, in the same place the game stores its step". Keeps stick gestures at their 30 FPS timing; a scripted run confirmed "game speed 1.000 at 60 fps in gameplay".\[3\] |
| ACNH recomp | Logic runs once per frame, so "video.frame_rate = 60 also makes it run twice as fast", until a SpeedFix rescales game speed.\[4\] |
| LCS (PS2) recomp | Fixes per system (vehicle rolling resistance, wanted-star blink timing moved to monotonic real time).\[5\] |
| Majistic Recomp | Per-system `anim_speed` normalisation for facial and gesture animations that ran 2x–4x too fast above 30 FPS.\[6\] |

**Inference:** a global tick-rate increase always leaves tick-counting systems running fast. The fix is per system: find each counter that assumes a fixed tick length, then either scale its increment by the real step or move it to wall-clock time.

## Proposed fix

1. **Measure first** (see the test plan). Find out what `Timing.GetTickRate` returns with the patch and whether `Timing.Wait` and the world clock keep real time.
2. **Name the anchors.** The Lua `Timing.*` natives (`GetTickRate`, `GetWorldFrame`, `Wait`, `GetSecondsSince`, `SecondsToFrames`, `GetTimeStep`) have no manifest names yet, apart from `LuaTiming_SetTimeOfDayAbsolute_8249A790`. Their binding table gives E3 names; their bodies show which global each reads. Name the init function `sub_8233AE50` at the same time.
3. **One system per change, each behind its own `[patches]` switch,** in the style of `gui_every_hf_tick`:
   - **Job meters and expression holds (#216):** find the meter's per-tick step through the job scripts' natives, then scale the step by `15 / LF rate` (or read `Timer_GetElapsed_822B6D70`), keeping the faster input sampling.
   - **Script frame counts:** if `GetWorldFrame`-based timers turn out to run fast, a hook on the native could report frames in 15 Hz units (`frames × 15 / LF rate`) while `GetTickRate` reports 15. That is a single choke point but changes every script at once, so it needs the widest testing.
   - **Rent and income:** scale the due-tick comparison, or keep it if it turns out to use game time. Coordinate with `feature/game-difficulty`, which already overrides money paths.
4. **Keep what already works:** the scheduler, the GUI, the aim cursor and the host mouse windows already handle the rate; don't touch them.
5. **Expose the result** as fixes under the existing options rather than new top-level options, and drop "(unstable)" from the launcher rows once the known side effects are gone.

Mapping timers and tick scheduling is first on the [mapping priority list](../../feature/function_mapping/technical.md#prioritised-systems-to-map-next).

## What changed

Nothing in the code yet.

## Test plan

No test needs new code except the last one. Compare each at the defaults, with `high_tick_rate`, and with both options.

| What | How | Pass |
| --- | --- | --- |
| Options applied | Read the log for the `[tick-rate]` lines | Lines present for each option on |
| Rate as scripts see it | Console: `f2 lua return Timing.GetTickRate()`; or `f2 mouse trace on` and read `tick=` in the `[f2diag] U` lines | Records the real value (15 / 30?) |
| Script frames per second | `f2 lua return Timing.GetWorldFrame()` twice, 10 s of real time apart | 150 frames at 15 Hz; record the value at 30 Hz |
| Lua wait | Time a `Timing.Wait(5)` against the log timestamps. Inference: it probably has to run inside a script thread (for example a recomp script started by the F5 runner), not straight from `f2 lua` | 5 s at every setting |
| World clock | `f2 time` now and after 60 s of real time | Same advance at every setting |
| Rent | Own a rented house, note gold before and after a fixed real-time wait | Same payout per real minute |
| Blacksmith | Same sword, same save, count hits in a fixed number of attempts | Comparable success rate |
| Regression scenario | Once the remote-control server can read Lua values (it has `cvar`, `game_state` and input commands today, no Lua), add "run 60 s at high_tick_rate; assert the in-game clock advanced 60 s" | Automated (E5 in [function_mapping](../../feature/function_mapping/technical.md#evidence-levels)) |

Existing coverage (Verified): `tests/config/test_config_template.py` checks the keys agree across the four config places, `tests/Fable2.Launcher.ConfigTests/Program.cs` round-trips all three keys, and `tests/native/test_mouse_aim.cpp` checks the aim timeouts for both camera windows. Nothing tests game timing.

## Verification

- **Verified in this repo:** the keys, defaults and log lines above; `BuildPatches()` in `src/core/fable2_patches.cpp`; the three hooks in `src/core/fable2_hooks.cpp` and their `[[entrypoint.midasm_hook]]` entries; the scheduler in `src/core/hotfunc/frame/ProcessGameFrame_82276C30.cpp`; `DefaultWindowUs()` and its test; the launcher rows.
- Nothing was run in game for this doc.

## Not tested

- The blacksmithing explanation, and which other systems are affected, haven't been measured.
- What `Timing.GetTickRate`, `Timing.Wait` and `Timing.GetWorldFrame` do under the patch.
- Whether rent is counted in ticks (the difficulty branch's wording suggests it).
- The constant at 0x8209FFD8 wasn't read; 1.0 is assumed.

## Follow-ups

- Name `sub_8233AE50` (writes both rate doubles) and the Lua `Timing.*` natives with `/name-functions`.
- Unnamed callers of `GetTimebase_8221EB58` (manifest comment) are candidates for "already real time": `sub_82BBB2A8`, `sub_82D02A88`, `sub_82A3E7C0`, `sub_82CFECA8`, `sub_82A57418`, `sub_82AA2E80`, `sub_82AB2968`, `sub_82BE3E00`, `sub_8238A2D8`. The named ones are `Timer_GetElapsed_822B6D70`, `PresentFrame_82B69488` and `GpuInterruptHandler_82B9C530`.
- [mouse_camera](../../feature/mouse_camera/technical.md) lists `high_tick_rate` as not tested at merge; the camera script runs per LF tick, so it should be part of the same test pass.

## Sources

1. [GitHub - LittleBitUA/DPRecomp: Deadly Premonition (Xbox 360) native PC port by static recompilation - v2.0.14: native DirectX 12 renderer ready for testing (up to 4x internal resolution); 60 FPS, mouse look, keyboard driving, FSR 3, ISO installer, PAL + USA · GitHub](https://github.com/LittleBitUA/DPRecomp)
2. [GitHub - mzzvxm/LARecomp · GitHub](https://github.com/mzzvxm/larecomp)
3. [Take the 60 fps step from the game's frame timer and keep stick gestures at their 30 fps timing by capibawa · Pull Request #62 · captainj-sparrow/astral-chain-pc-recomp · GitHub](https://github.com/captainj-sparrow/astral-chain-pc-recomp/pull/62)
4. [GitHub - Nycolazs/ACNHRecomp · GitHub](https://github.com/Nycolazs/ACNHRecomp)
5. [GitHub - NicoWayne95/lcs-ps2-recomp: Experimental PS2-to-Windows recompilation project focused on compatibility, graphics rendering and performance. · GitHub](https://github.com/NicoWayne95/lcs-ps2-recomp)
6. [GitHub - kelvynzucco/majistic-recomp: High-performance static recompilation of the Xbox 360 classic for Windows x64 PC (Direct3D 12, 60 FPS, C++23). · GitHub](https://github.com/kelvynzucco/majistic-recomp)
