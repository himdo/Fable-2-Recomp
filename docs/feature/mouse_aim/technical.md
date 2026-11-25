# Mouse aim (ranged aim reticle): technical notes

**Status:** merged · **Commits:** 49c70ab (#183) · **Last checked against:** 7863041

Labels: **Verified** means read in the repo, the generated code or a log. **Inference** means reasoned, not tested. Game scripts are paraphrased, never pasted.

49c70ab is the squash of #183: the stick swap, direct aim, the config and launcher settings, the highlight release and the function names.

## Goal

While aiming a ranged weapon, the mouse moves the aim reticle instead of the camera, and WASD stops moving it. A real controller aims exactly as before. The maintainer asked for no smoothing on aim, and for pad play and pad↔mouse swaps to be unaffected.

## How the game aims (Verified)

- **Scripts (paraphrased):** holding the ranged button enters player mode `PLAYER_MODE_AIM_RANGED = 23`. `cameramanager.lua` then prefers `RangedCombatCamera` over `SACCamera`. `RangedCombatCamera` has an empty `ProcessGameAction`: the camera only *follows* the native aim point (`Targeting.GetCameraSafeTargetedPosition`, rotated 11° to the side, mirrored for pistols, pitch limited to ±30°). No script moves the aim. `combatbalance.lua` sets the cursor speeds with `Debug.SetRangedCursorXSpeed(1.8)` / `YSpeed(1.5)`.
- **Native aim chain:**
  1. Action 71 (`RANGED_CURSOR_MOVEMENT`) handler `sub_82747090` acts only in mode 23 and stores (x, y) on the mode object.
  2. `AimTargetingUpdate_827B6228` runs each tick: `AimStickFlickDetect_827B5888` (flick/held flags at `self+232/+233` from the **raw** left stick), then `AimStickShape_827B60C8` (cubic response, flick thresholds 0.2/0.1, exponential smoothing with alpha 0.235 at `0x83320DE0`), then **`AimCursorUpdate_827B80B0`**, then the sub-target pick.
  3. `AimCursorUpdate_827B80B0` only runs while aiming. Cursor at `self+32` (x) / `+36` (y). Free move: `cursor += input × k × speed / LF tick rate`, then a box clamp (±0.8; ±1.5 in one branch). It is the only reader of the two speed globals (`0x8331941C`, `0x83319420`).
- **End to end:** WASD → left stick → merged XInput → action 71 in mode 23 → curve + smoothing → per-tick integration and clamp → targeting position → `RangedCombatCamera` turns toward it.

## What changed

### Direct aim (default, `mouse_aim_input = "direct"`)

- **Hook:** `src/core/hotfunc/camera/AimCursorUpdate_827B80B0.cpp` wraps the cursor update. Before calling `__imp__AimCursorUpdate_827B80B0`, it adds the drained mouse delta to `self+32/+36`. The game's own box clamp and target picking still apply; nothing is smoothed or capped. Because the function only runs while aiming, it doubles as aim detection.
- **Sensitivity:** `mouse_aim_sens` (x, cursor units per count, default 0.0007) and `mouse_aim_y_ratio` (default 1.0). Cursor units are normalised per axis, so y = x × render aspect × y_ratio (`AimAspect(CurrentAspect())`, falls back to 16:9 outside 0.5..8). Both axes then move at the same on-screen speed on any screen shape.
- **Measured (Verified, mouse log):** a 300 px flick moves the cursor in 2 updates with no swing back. The cursor offset decays as the aim camera turns toward the reticle; that is the game keeping the world aim point, not a bug (Inference).

### When the mouse aims (`mouse_aim_mode`)

`both` (default): the aim button (any key bound to pad Y) is held **and** the game is aiming. `rmb`: the aim button alone. `detect`: the game aiming alone. `off`: never. Logic in `src/input/mouse_aim.h` and the aim branch of `src/input/keyboard_gamepad.h`.

### Stick aim (`mouse_aim_input = "stick"`)

The earlier approach, kept as an option: while aiming, the mouse drives the guest **left** stick through the inverse of the cubic curve (`mouse_aim_scale`, default 256). It keeps the game's smoothing and speed cap.

### The highlight release (the review bug)

- **Report:** with direct aim, safety off and aiming down sights at a person, the auto-highlight held the reticle and the mouse could no longer move it. A pad could move off.
- **Cause (Verified in generated code):** while a target is highlighted (`self+56` handle valid), `AimCursorUpdate_827B80B0` jumps past the free move (`loc_827B8450` → `loc_827B85F0`) and only re-picks when the flick detector's `+232/+233` flags are set. Those come from the raw left stick, which direct aim keeps at 0, so the highlight never let go.
- **Fix:** let go and follow the mouse (chosen over copying the controller flick).
  - On a tick with mouse movement and a valid target, the hook releases the highlight in a guest frame: it stores the target's world position (`AimTargetGetWorldPos_827BB550`) to `self+128` so the next tick projects to the same screen point (no jump), then empties the handle with the same sequence as `AimTargetingReset_827B5F60` (`WeakHandleRelease_821940C8`, `WeakHandleAssign_82978470`). Never write the ref-counted handle at `+56` directly.
  - `src/core/hotfunc/camera/AimTargetSearch_827B9CD0.cpp` suppresses the target pick while the mouse moved within 150 ms, so the reticle doesn't grab every person it crosses. While the mouse rests it scales the pick zone (`mouse_aim_assist`, default 1).
  - **Stickiness** (`mouse_aim_stickiness`, 0.1): a small pull doesn't break the lock. On break only the excess pull is handed over, so the release tick matches a normal moving tick.
  - **Slow-movement magnetism** (`mouse_aim_magnet_speed`, 0.35): slow mouse movement feeds the game a tiny stick nudge so its narrow pick cone works like a pad's, then takes back the nudge's own cursor move.
  - **Wheel hops** (`mouse_aim_hop_ms`, 100 ms between hops): the mouse wheel hops between targets.
  - `mouse_aim_release` (default true; `f2 mouse aimrelease off`) restores the game's own behaviour. These release cvars are console only, not in `fable2_config.toml`.
- **Accepted, not chased:** a rare backward jump 1–2 updates after a *fast* release, and far wheel hops drifting to the side.

### Settings, console, logging

- `config/fable2_config.toml` `[input]`: `mouse_aim_mode`, `mouse_aim_input`, `mouse_aim_sens`, `mouse_aim_y_ratio`, `mouse_aim_invert_y`, `mouse_aim_scale`, read at start-up into cvars of the same names (`src/core/fable2_config.cpp`, `src/main.cpp`). The launcher's Mouse panel has an aim mode dropdown and a sensitivity box (`launcher/Fable2.Launcher/Fable2ConfigFile.cs`, `MainWindow.xaml`).
- Console only: `f2 mouse aim`, `aiminput`, `aimsens`, `aiminvert`, `aimscale`, `aimassist`, `aimsticky`, `aimmagnet`, `aimhop`, `aimrelease`, and `f2 pad hold` for scripted aim tests.
- `fable2_mouse.log` P lines record the reticle (`cur=`, `atk=`), target (`tgt`/`tf`) and the `mrel`/`msup`/`mhop`/`mmag` counters. `tools/mouse_stats.py` reports reticle movement per run.
- Manifest names added for the aim chain: `AimTargetingUpdate_827B6228`, `AimCursorUpdate_827B80B0`, `AimTargetSearch_827B9CD0`, `AimTargetGetWorldPos_827BB550`, `AimProjectToCursor_827BB948`, `AimTargetingReset_827B5F60`, `AimStickFlickDetect_827B5888`, `AimStickShape_827B60C8`, `Vec2NormalizeGetLength_822A8EF8`, `WeakHandleAssign_82978470`, `WeakHandleRelease_821940C8`.

## Verification

- `tests/native/test_mouse_aim.cpp` (release truth table, pick hold, stickiness, magnetism, aspect sensitivity), `tests/native/test_mouse_direct.cpp`, `tests/tools/test_mouse_log_format.py`, `tests/tools/test_mouse_stats.py`, launcher tests for the aim settings.
- In game: direct aim, both aim modes, release, sweep, rest relock, pad play and `aimrelease off`/`on` behave as designed. The maintainer tested the review fix ("fixes the stuck aiming").
- **Not tested:** pistol vs rifle/crossbow differences in depth, `high_tick_rate` (the integration divides by the LF rate, so speed should be unchanged: Inference), first-person camera.

## Constraints carried over from the mouse camera (#177)

- Never register host scripts with `GeneralScriptManager` (it writes them into saves); host RunScript only from the world update (see `docs/fix/alt_tab_script_crash/`).
- Wrappers must be safe to run again and must always call the game's original.
- Hooks touch registers only; no file I/O on guest threads; guest memory is big-endian.
