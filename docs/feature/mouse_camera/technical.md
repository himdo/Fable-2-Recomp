# Mouse camera (direct mouse look): technical notes

**Status:** merged · **Commits:** 80e8361 (#177), shipped in 1.6.0; follow-up fixes c5c80ff (#189) and aae1116 (#227) · **Last checked against:** c2a9d12

Labels: **Verified** means read in the repo, the game scripts (paraphrased, never pasted) or a log. **Inference** means reasoned, not tested.

80e8361 (#177) also carries the smooth-stick commits of the closed #175. The later fixes: c5c80ff (#189) moved the script start to the world thread, and aae1116 (#227) stopped Steam's controller-to-mouse layout from turning the camera.

## The problem

Before #177 the mouse was turned into a pretend right stick (`src/input/mouse_stick.h`), so it inherited the stick's limits:

- **Speed cap (Verified).** Past a certain movement per input poll the pretend stick is fully deflected, and `SACCamera` turns at most about 120°/s times the in-game sensitivity factor. At the default `mouse_look_scale` the stick saturates at 128 px per poll.
- **Coast (Verified).** The camera script smooths the stick, so a flick keeps turning after the mouse stops: in a measured 150 ms flick, 67–83% of the turn arrived after the mouse stopped.
- **Lost movement and deadzone (Verified in scripts):** only the last stick reading before each camera update is used, and up/down below 10% stick is ignored.
- **Poll-rate dependence and no raw input (Inference).**
- The maintainer's verdict on the smooth-stick attempt (#175): it still feels like "the mouse is the stick". No stick-based fix can remove the cap and the coast.

## Past attempts (for the record)

| Approach | Result |
| --- | --- |
| Pretend right stick | Speed cap and coast |
| Rotate the camera transforms directly | Turned on the spot instead of orbiting the hero |
| Rebuild the orbit from outside the game's logic | Felt good at first, then "floaty"; mouse plus stick passed the limits, because two owners kept their own camera angle |
| Change the camera's target values | Didn't move the camera |
| Add to the stick in `LookStickProcess_83045778` | Wrong function; never runs in normal play |

**Design lesson:** keep no camera angle on the host side, only movement not yet delivered.

## How the camera script works (Verified, paraphrased)

- `SACCamera.FrameUpdate` starts each update from the previous update's camera position, applies the stick turn in `ApplyRotateVector`, flattens it in `CalculateXY` and stores the result as the next starting position, before collision. No stored yaw rebuilds the position, so an extra turn added after the game's own turn simply sticks.
- `FrameUpdate` calls its methods by name on `self`, so class-level wrappers take effect on the next update, and the cameras built on `SACCamera` (MagicSAC, Tracking) are covered too.
- `CheckForTeleport` is the first unconditional call after the alive check: the natural drain point, early enough to mark the update as a manual turn before auto-alignment runs.
- Turn angles are in degrees.
- `GetHeight` clamps the manual height to ±0.7, blends it into a smoothed copy and lets it drift back to the middle. Adding the same delta to both, before the clamp, shifts the result by that delta and keeps the shared limit.
- Ranged aiming doesn't go through this script at all; see `docs/feature/mouse_aim/`.

## What changed (#177)

- **`src/lua/mouse_direct.lua`** wraps three `SACCamera` methods:
  - `CheckForTeleport` asks the host for the mouse movement since the last update (`cprint "[f2mouse] 1"`, or `0` when the camera can't turn: target lock, rotation disabled), applies the in-game sensitivity and marks the update as a manual turn.
  - `ApplyRotateVector` turns the returned position further about `PreBoundFocalPos` by the mouse yaw.
  - `GetHeight` adds the mouse height to the manual height and its smoothed copy, so it applies at once within the same limit as the stick.
  - The stick is untouched; the mouse adds to it. Wrappers record the original in a weak registry table so they always call the game's function and running the file again is harmless.
- **`src/input/mouse_direct.h`:** `DirectState` accumulates raw counts and routes them. Direct only while the script accepted a drain within `RouteTimeoutUs` (max(2.5 × update, 200 ms)); otherwise the movement goes to the pretend stick (target lock, cutscenes, menus, other cameras) and anything not delivered is discarded, so nothing snaps later. `ToCamera`: yaw = −dx · sens_x · scale/256; height = dy · rad(sens_y) · scale/256 (± invert). `Boot` retries starting the script with backoff while the mouse moves and nothing reports.
- **`src/core/fable2_console.h`:** the `cprint` override handles `[f2mouse] 1|0|boot|note …` and returns the numbers to Lua as two TValues on the stack.
- **No per-frame script.** An early version kept a `GeneralScriptManager` "keeper"; that manager writes scripts into save games, and a restored keeper wrapped on top of the new wrappers looped the camera until the game ran out of memory. Saves from that early build still carry it: the script detects its heartbeat and leaves the camera alone.
- **Settings** (`[input]` in `config/fable2_config.toml`, cvars of the same names): `mouse_look_mode = "direct"` (or `stick`), `mouse_look_sens_x` / `_y` (degrees per count, default 0.06), `mouse_look_invert_y`, `mouse_look_scale` (the launcher slider, 256 = 1×), `mouse_look_smoothing` (stick mode). The launcher's red "Direct mouse look (experimental)" checkbox, which switched on an older dead path, was replaced with a Camera mode choice.
- **Console:** `f2 mouse`, `f2 mouse mode|sens|invert|smooth`, `f2 mouse inject … even|ramp|flick` (flick = minimum-jerk profile, for repeatable tests).
- **Tools:** `fable2_mouse.log`; `tools/mouse_stats.py` reports the total turn per run, the share that arrived after the mouse stopped and how fast the turn starts.

## Verification (Verified in game and in logs)

1500 counts at sensitivity 0.04:

| | Stick | Direct |
| --- | --- | --- |
| 150 ms flick: total turn | −20.6° | −60.0° |
| 2 s sweep: total turn | −46.8° | −60.0° |
| Turn arriving after the flick ended | 67–83% over 7–8 updates | ≤ 2.7% in 1 update |

- 750 counts × 0.12 gave +90.4° in `f2 campos`, unchanged 1 s later.
- Flicks and slow sweeps turn the same amount; cutscenes, alt-tab, F4 and lock-on behave; a clean new game runs through the prologue.
- Native `tests/native/test_mouse_direct.cpp` (including NaN/infinite settings falling back to defaults), `tests/tools/test_mouse_stats.py`, `test_mouse_log_format.py`.
- **Not tested at merge:** `high_tick_rate` and whether slow turns look steppy at the 15 Hz camera update, cutscenes beyond the prologue, many saves loaded in a row.

## Follow-ups that happened

- **#189 (`c5c80ff`):** #177 started `mouse_direct.lua` from the render thread, which caused the alt-tab crash. See `docs/fix/alt_tab_script_crash/`.
- **#227 (`aae1116`):** Steam's controller-to-mouse layout no longer turns the camera.

## Rules for code in this area

- Never register host scripts with `GeneralScriptManager`: it writes them into save games (`tests/lua/test_recomp_scripts.py` enforces this).
- Host `RunScript` only from the world-update service (`tests/core/test_world_thread_runscript.py` enforces this).
- Wrappers must be safe to run again and must always call the game's original.
