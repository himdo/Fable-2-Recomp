# Plan: 1:1 Direct Mouse Camera Control

Goal: make the mouse drive the third-person camera orbit **exactly** like the right stick, so
there is no perceptible difference between the two inputs, and combining them (mouse + stick
simultaneously) just works — same angle, same feel, same native limits.

## STATUS (current approach — 1:1 native right-stick injection)

**Confirmed target:** `RightStickLookProcess_832BB018` (guest 0x832BB018,
generated/default/fable_2_recomp.139.cpp) is the game's **active per-frame right-stick-Y
(sThumbRY) consumer** — verified at runtime (the `[ry-probe]` fired only here, many times per
frame, with physical right-stick input). The earlier candidate `LookStickProcess_83045778` (the
8304x per-controller cluster) is **dead** during normal gameplay (its whole call chain never
runs); that override is now a faithful no-op kept for reference.

**How the injection works:** `RightStickLookProcess` reads the right-stick Y u16 from a local
interleaved stick buffer (`r10+14`, where `r10 = r1+708`) into r23, then packs it (r23<<8 | r23,
rlwimi) into the per-controller input state that drives the hero-camera orbit. The hotfunc
override (`src/core/hotfunc/camera/RightStickLookProcess_832BB018.cpp`) adds the per-frame mouse Y
delta, scaled by `mouse_look_stick_scale` (stick u16 units per host pixel), to that u16 right
after the read. The native orbit/easing/clamp pipeline then runs on the combined stick + mouse
value — so the mouse **shares the stick's exact orbit bounds** (it can't compound past the native
limits, because it feeds the same u16 the stick does).

**Mouse-delta plumbing:** the host input driver accumulates the raw cursor displacement in
`fable2::mouselook::dy_acc()` (`src/input/mouse_look.h`). `RightStickLookProcess` **peeks** it
(no reset) so every per-frame call sees the same value; the legacy direct-mouse hook
(`fable2_hook_direct_mouse_look`, at CECCameraUpdateFrame) **resets** it once per frame via
`take_delta()` and skips its orbit logic when `mouse_look_native_inject` is on. Frame order
(input processing → camera update) guarantees the peek precedes the reset.

**Config (`[input]` in fable2_config.toml):**
- `direct_mouse_look = true` — master switch for the direct-mouse path.
- `mouse_look_native_inject = true` — use the 1:1 native right-stick injection (this build's
  target). When false, the legacy external/native-target orbit runs instead.
- `mouse_look_stick_scale = 64` — stick u16 units per host mouse pixel (512 px = a full
  ~32768 deflection at the default). Negative flips the direction.
- `mouse_look_stick_diag = true` — logs `[ml-stick] rest=.. inject=.. mdy=.. scale=..` (capped)
  so the rest value / deflection range can be measured and the scale calibrated.

**Open item:** the exact scale (rest value + full-deflection range of the `r10+14` u16) is being
measured from the `[ml-stick]` log; `mouse_look_stick_scale` is the tuning knob. The mouse is
velocity-based (orbits while moving, stops when idle) vs. the stick's position-based hold — same
function and bounds, slightly different feel.

## Why the current (re-implemented) orbit feels off

The current hook (6th iteration) re-implements the orbit externally: it rotates the eye at
`cam+144` around a low-passed pivot and writes a hand-built look-at quaternion to `cam+160/176`.
This is close but differs from native in ways that make it feel "floaty and wrong":

- The pivot is a **low-pass estimate**, so it lags and drifts relative to the true hero focus.
- The look-at orientation is **reconstructed** each frame, losing the game's own banking/tilt and
  the exact eye-to-focus vector the native camera uses.
- The native orbit's **easing is a different character** than our instantaneous rotation.
- We maintain a **separate** cumulative offset; the native stick integrates into a controller
  angle. Two parallel systems can't share a state/limit cleanly.

The fix is to stop re-implementing and instead **feed the mouse into the game's own orbit path**.

## What we know about the native orbit (from RE)

1. **Single DOF theta.** The right stick drives one orbit angle. The three target scalars are
   rigidly coupled: `a68 = theta`, `a72 = theta + 1.5`, `a76 = theta + 5.0`. Current angles
   `a44/48/52` ease toward targets `a68/72/76` each frame in `CECCameraUpdateFrame`.
2. **Angle setter** `CECCameraSetTarget_8241BDF8` (vtable-dispatched) writes `f1..f6` →
   `cam+68/72/76/80/84/88` (targets) and optionally the current `cam+44/48/52`. It does **no
   clamping**.
3. **Native limits** in theta/a68 units: **floor 1.458, ceiling 4.067** (default 2.810). Measured
   from the user pushing the stick full range. The orbit is **pitch-inverted**: stick up
   decreases the angle (camera goes to the floor).
4. **Native pitch clamp** lives in the vtable-dispatched controller method that computes the new
   angles before calling the setter (not in the setter).
5. **Stick normalization** (delegate-confirmed): raw u16 (0..65535, centered `0x4000`) → `rotlwi`
   ×4 → × `0.4375` → a normalized **position** (level, ~±1), not a per-frame rate.
6. **Pitch is 1:1 with our mouse offset:** rotating the orbit (radius ~3.07) by 1 rad == an a68
   change of 1 rad. So `cum_pitch` (radians) is directly additive to a68.

## The design: offset the native target angle, reuse the native easing

**Correction (verified via `[ml-set]` log, fable_2_010.log):** hooking the angle setter
(`CECCameraSetTarget_8241BDF8`) was the WRONG injection point. That setter is a generic low-level
"write 6 angles" called for a *pool* of ~10 camera objects (`0x422Exxxx`) every frame with
`f1 ≈ 0`, and it is **never** called for the active hero-follow camera (`0x4294EE50`, whose
`a68` stays 2.813 untouched). So the hero's orbit target angle is set elsewhere (its own
controller code), not through this setter.

**Revised approach:** offset the hero camera's *target* orbit angles directly, in the
`CECCameraUpdateFrame` hook where `r31` is the active camera:

- In `fable2_hook_direct_mouse_look` (at `0x822A47F8`), after the hero-flag gate and the
  cumulative-pitch accumulation:
  - Read the native target `a68 = cam+68`.
  - Clamp `cum_pitch` so `native + cum_pitch ∈ [floor, ceil]` (shared native bounds).
  - Write `cam+68 = native + cum_pitch`, and restore the rigid coupling
    `cam+72 = cam+68 + 1.5`, `cam+76 = cam+68 + 5.0`.
  - The game's own easing then drives the current angle `cam+44 → cam+68`, so the orbit radius,
    easing, and banking are all the game's native behavior (1:1 with the stick).
- Because the mouse writes the *same* `a68` the stick drives, they share one orbit + one set of
  bounds and combine as `final = clamp(stick + mouse)`.
- **Open risk (being measured now):** does the controller re-commit `cam+68` fresh every frame
  (→ stable) or freeze it (→ a per-frame `+ cum_pitch` compounds)? The `[ml-orb]` diagnostic logs
  `a44`/`a68_pre`/`a68_post`/`cum_pitch` each frame to answer this. If it compounds, reconstruct
  the true native base by subtracting the last-applied offset before re-adding.

### SECOND correction (verified via fable_2_011.log) — the angle scalars don't drive the eye

The `+cum_pitch` approach **does not move the camera.** The log proves it:
```
[ml-d] cam=0x4294EE50 ang44=3.738 ang48=5.238 ang52=8.738  tgt68=2.813  center144=204.14,93.75,54.67
```
`ang44` moved to my offset (3.738, coupling intact) and `a68_pre` stayed fresh (2.813, **no
compounding** — the controller re-commits `a68` every frame, so case "a" holds). But `center144`
(the **eye**) barely moved (54.66 → 54.67). So `cam+44/68` are **outputs** the controller writes
alongside the eye — they are NOT the driver. The eye is computed by the controller (owner at
`cam+4`) from its **own internal state**. Offset the angle scalars and the eye doesn't follow.

**Consequence:** to move the eye 1:1 with the stick, I must inject into the **same input path the
stick uses** — i.e. synthesize the gamepad right-stick from the mouse so the *native* controller
does the orbit. This is the user's exact ask ("export the input control functionality for the
camera then inject our own direct control"). The old external orbit (cam+144) is kept as the
`mouse_look_legacy_orbit` fallback.

### Phase 2 direction: synthesize the right-stick from the mouse
- Drive the gamepad right-stick **RY** (pitch) — and **RX** (yaw, if the stick has a yaw DOF) —
  from the per-frame mouse velocity, so the native controller integrates the orbit exactly as it
  does for a real stick: move mouse = orbit, stop = hold, native easing + native floor/ceiling.
- **Deadzone:** the stick consumer zeroes deflections below ~0.25 normalized (raw < 0x4000 from
  center). Injecting the **normalized float after the deadzone** gives fine 0..1 control; driving
  the raw u16 means small moves are eaten (coarse). (Injection point being located by the delegate.)
- **Cadence:** mouse is a velocity, stick is a level → `stick_level = clamp(mouse_velocity * sens,
  -1, 1)` each frame; a stationary mouse → level 0 → the orbit holds (no spring-back).

### THIRD correction (verified via fable_2_011.log [ml-ctl] + [ml-orb]) — timing, not the value

The `+cum_pitch` to `cam+68` **does not move the eye** even though `a44` eased to the offset. The
reason is *timing*: the controller sets `a68` **and** computes the transform (position `+144` +
orientation `+160/176`) from it each frame, *earlier* than the `CECCameraUpdateFrame` hook. So
offsetting `a68` at `0x822A47F8` is too late (the transform is already computed), and the
controller resets `a68` to its native value next frame. The `[ml-ctl]` dump also shows the
controller's own fields (`c0-c128`) are **constant** — only the sub-object transform (`+124`:
position `+32/36/40`, orientation `+48/52/56/60`) changes, and that change is the hero *walking*,
not the orbit. So the orbit angle is not a controller scalar I can grab from `+4` — it flows
through the controller's update as `a68` → transform.

**Revised 1:1 plan:** hook the controller update at the point where it **sets `a68`** and offset
`a68` there (before the transform is computed), so the *same function* computes the transform from
the offset `a68` and the native radius/easing/banking all apply. The generic setter
(`CECCameraSetTarget_8241BDF8`) is NOT the hero's path (pool-only, `f1≈0`); the hero's setter is
another function (candidates: `83221D10`, `8281BF98`, `82B1E210`, `826328D8`). A delegate is
locating it.

**Interim state:** `mouse_look_legacy_orbit` now defaults to **true** (the working OLD re-
implemented orbit) so the user has a functioning camera while the 1:1 path is finished. Flip the
default to false once the controller-update `a68` offset is confirmed to move the eye.

### FOURTH correction (read the generated `CECCameraUpdateFrame`, recomp.81.cpp:1602) — the real
injection point

The transform is computed by a **vtable-dispatched method**, not from the stored `a68`. In
`CECCameraUpdateFrame` (r31 = hero cam) the sequence at `0x822A47F8` is:
```
r11 = *(cam+4); r4 = *(r11+124);        // r4 = the "look" sub-object
vtable = *(r4+0); method = *(vtable+64); // vtable[16] = the transform method
bctrl  -> method(r3 = r1+96)            // computes the transform into the r1+96 buffer
```
The method reads the sub-object's state (orbit angle, hero position, radius) and writes the
transform. My hook at `0x822A47F8` runs **after** this bctrl — too late to affect the transform.

**Plan:** hook *before* the bctrl (with `r4` = the sub-object in registers), and offset the
**orbit-angle field in the sub-object** by the cumulative mouse pitch. The method then computes
the orbit 1:1 (native radius/easing/banking/limits). One diagnostic run is needed: extend the
`[ml-ctl]` dump to the sub-object `0..256` + the resolved `vt16` method address, and have the user
do a **full-range right-stick sweep** (direct_mouse_look off) so the sweep reveals which
sub-object field is the orbit angle. Then offset that field before the bctrl.

### FIFTH correction (fable_2_013.log stick sweep) — the pragmatic 1:1 orbit

The stick sweep (fable_2_013.log) showed the sub-object's position (offsets 32/36/40) is
**near the eye** (≈ 204.14/93.75/54.95 vs eye 204.14/93.75/54.66, ~0.29 apart) — so it is the
**camera's position (the orbit result)**, NOT the hero's position. The owner (controller) fields
are also constant. The camera's actual position is computed into the `r1+96` buffer by the
owner's vtable slot-0x74 update (a `bctrl` distinct from the copy at `0x82794ED8`), and it eases
the CECCamera's current angle (`cam+44`, coupled `48/52`) toward the target (`cam+68`).

So the hero's position (the orbit center) is NOT directly readable — it is `eye + forward *
camera_distance` (the hero sits ~3.1 units in front of the eye). That estimate was correct all
along; the "floaty" feel was the **low-pass lag** (alpha 0.15) on the pivot, not a wrong pivot.

Rather than chase the runtime-dispatched slot-0x74 method, the **pragmatic 1:1 orbit** (now the
default, `mouse_look_legacy_orbit=true`) rewrites `legacy_orbit_2axis` to:
1. Use the **fresh** hero-pivot estimate (`eye + forward * camera_distance`) every frame — the
   old low-pass (alpha 0.15) lagged the pivot behind the hero (the "floaty" feel); the fresh
   estimate has no lag.
2. Make it **1-axis pitch** (mouse Y only, around the world right/X axis) to match the native
   single-DOF stick (the mouse X yaw is ignored),
3. Write the roll-free look-at orientation (camera faces the hero) to `cam+160/176`.

This orients the eye around the true hero position (eye + forward * distance) with the native
pitch limits, so it tracks the stick's behavior without re-deriving the dispatched transform
method. Verify: mouse up/down orbits the camera up/down around the hero (no left/right yaw),
centered on the hero, no lag.

### SIXTH correction (user direction) — pull the stick handler in via HotFunc, inject at the input
The re-implemented orbit (above) cannot share the native **bounds** with the stick: when the mouse
and stick are combined the orbit exceeds the native limits, which the user confirmed is wrong. The
native-path injection points tried so far (the angle setter `CECCameraSetTarget_8241BDF8`, the
`cam+68` target offset, the transform sub-object field) are all the **output side** — the value the
controller *writes*, not the input the controller *reads*. The controller reads the **right stick**.

**New strategy:** override the per-controller input handler that reads the right stick, and add the
mouse offset to the **stick RY** before the native pipeline runs. The entire native pipeline
(normalize → controller → orbit, with the native floor/ceiling clamp) then runs on
`stick + mouse`, so mouse and stick share one value, one path, and identical bounds by
construction — 1:1 with zero re-implementation.

- **Target:** `LookStickProcess_83045778` (the per-controller input handler; `fable_2_recomp.18.cpp:29914`). It
  reads `RY` from the shared XINPUT_STATE buffer (`lhz r10,10(r11)` where `r11 = *(r31+24)`), scales
  it, calls `VectorMathThunk_821F4D88` (→ `0x8222CA28`), and caches the result at
  `handler+56` for the camera controller. It is called via a direct `bl`, so a strong-symbol override
  intercepts it cleanly.
- **Mechanism:** the HotFunc override (`src/core/hotfunc/camera/LookStickProcess_83045778.cpp`, generated by
  `tools/make_hotfunc_overrides.py`) is a behavior-identical strong `extern "C"` copy of the function.
  The mouse offset is folded in by writing `RY' = clamp(stick_RY + mouse_offset, 0, 65535)` back to
  the buffer at `+10` **at the top of the function** (before the first `lhz`), so every downstream
  read (phase 1 normalize + phase 2 magnitude gate + per-entry handlers) sees `stick + mouse`.
- **Mapping:** `cum_pitch` (radians, from the mouse) → u16 units via `16384 / 1.509 ≈ 10857` u16 per
  radian (full stick deflection 16384 ↔ full native orbit range ~1.509 rad). `cum_pitch` is still
  accumulated by `fable2_hook_direct_mouse_look` (kept for now); the override reads it via
  `fable2::mouselook::cum_pitch()`.
- **Removed:** the `fable2_hook_direct_mouse_stick` midasm hook (function + manifest entry) — it did
  the same RY rewrite as a hook; the HotFunc override supersedes it. The old external orbit
  (`legacy_orbit_2axis`, behind `mouse_look_legacy_orbit`) stays as an A/B fallback.

### Mouse input model
- Keep the existing `mouse_look` cumulative offset: `cum_yaw`/`cum_pitch` from
  `fable2_take_mouse_delta()`, scaled by `mouse_look_scale` and `mouse_look_pitch_sens`.
  - **Pitch (mouse up/down)** maps to the native single orbit angle (a68). This is the primary
    axis and gets the exact native feel + clamp.
  - **Yaw (mouse left/right):** the native stick has **no independent yaw** (single DOF). Two
    options:
    - **(a) Recommended for true 1:1:** drive yaw through the game's own *heading/turn* input path
      (the same channel A/D or left-stick uses to rotate the camera around the hero), so the mouse
      yaw and stick yaw share the native mechanism too. This needs a second hook at the yaw
      injection point (to be located — see below).
    - **(b) Simpler:** if the hero-camera orbit also responds to a *second* angle group, add
      `cum_yaw` to that group in the setter (the `a80/84/88` targets or the controller's heading
      state). Must be verified which group produces the around-the-hero yaw.
  - The plan starts with pitch (fully native) and adds yaw once its native channel is confirmed.

### State ownership (shared, not parallel)
- The **only** mouse-owned state is the accumulated offset (`cum_pitch`, `cum_yaw`), which is a
  pure *delta added at the setter* — it is **not** a parallel camera pose. The camera's actual pose
  is always the native game value. This is what removes the "floaty" feel: the eye, focus,
  orientation, easing, and banking are all produced by the game; we only nudge the target angle.
- No pivot, no low-pass, no reconstructed quaternion → none of those artifacts remain.

## Why combining mouse + stick "just works"

- Stick: the game's input handler writes `theta` (clamped) into `a68` via the setter.
- Mouse: our setter hook adds `cum_pitch` to that same `a68` (then clamps).
- Both operate on one value through one path, so `final = clamp(theta_stick + mouse_offset)`.
  They don't fight, don't reset each other, and respect the shared native limit. When both are at
  rest, `mouse_offset = 0` and the camera is pure-native.

## Implementation steps

1. **Add the setter hook**
   - Manifest: `[hooks.fable2_hook_direct_mouse_look_target]` at `0x8241BDF8`,
     `registers = ["r3", "f1", "f2", "f3"]`.
   - `fable2_hooks.cpp`: new hook fn. Read hero flag (`r3+108`); if hero, add `cum_pitch` to
     `f1/f2/f3`, clamp `f1` to `[pitch_floor, pitch_ceil]`, restore coupling `f2=f1+1.5`,
     `f3=f1+5.0`.
   - `mouse_look.h`: expose `cum_pitch()`/`cum_yaw()` and a way to build the per-frame delta
     (reuse `fable2_take_mouse_delta()`). Consume the delta each setter call.
2. **Replace the external orbit** — remove (or gate off) the `cam+144/160/176` orbit writes from
   the existing hook at `0x822A47F8`; the setter hook becomes the direct-mouse path.
3. **Config** — repurpose existing keys:
   - `mouse_look_pitch_sens` (rad/pixel), `mouse_look_scale`, `direct_mouse_look` (toggle).
   - `mouse_look_pitch_floor=1.458`, `mouse_look_pitch_ceil=4.067` (already present).
   - Drop/retire the now-unused `camera_eye_off`, `camera_distance`, `camera_transform_off`,
     `camera_rot_*` orbit fields (or keep for the fallback path).
4. **Yaw (phase 2)** — locate the native around-the-hero heading injection (log the setter's
   return address / grep for the heading clamp constants), hook it, and add `cum_yaw` there.
5. **Rebuild** game + launcher; update the manifest hook comment; update the launcher config
   descriptions (the sensitivity slider already applies since it scales `cum_pitch`).

## Open questions to verify before/at implementation

1. **How often does the setter fire and with what values when the stick is at rest?** The setter
   writes *target* angles; if it's called every frame with the controller's current theta, adding
   our offset each call is correct. If it's only called on input, we may need to also touch the
   current-angle easing. → Confirm by logging `f1` across frames at the setter (a quick one-run
   log, or reuse `[ml-dump]` a68 tracking). This is the main risk: we must ensure the offset is
   applied to the *authoritative* target every frame, not just when the stick moves.
2. **Does the controller *reset* a68 to a rest value each frame** (making a per-call add a
   persistent offset) or does it *integrate* stick rate into a persistent theta (making our add a
   one-shot)? From the diagnostic, a68 is stable at rest and follows stick position — consistent
   with the controller owning a persistent theta and the setter just committing it. Our per-frame
   add to the committed target then behaves as a persistent offset. Verify with the setter log.
3. **Pitch-inversion sign:** native is pitch-inverted (stick up = angle down). Choose
   `mouse_look_pitch_sens` sign so mouse-up goes up. (Config-driven, trivial to flip.)
4. **Yaw channel:** confirm which angle group / controller state produces around-the-hero yaw, so
   phase 2 can share it natively.

## Implementation checklist

> **Status (phase 3 — HotFunc input injection):** The setter-injection and `cam+68` angle-offset
> approaches were both tried and **rejected** — they are the controller's *output* side and do not
> move the eye (verified fable_2_010/011.log). The re-implemented external orbit (`legacy_orbit_2axis`,
> `mouse_look_legacy_orbit=true`) works but cannot share the native bounds with the stick (user
> confirmed combined input exceeds the limits). **Current approach:** override the per-controller
> input handler `LookStickProcess_83045778` via a HotFunc strong-symbol override
> (`src/core/hotfunc/camera/LookStickProcess_83045778.cpp`) and fold the accumulated mouse offset into the stick
> **RY** at the top of the function, so the entire native pipeline (normalize → controller → orbit,
> with the native floor/ceiling clamp) runs on `stick + mouse` — 1:1 bounds by construction.
> The `fable2_hook_direct_mouse_stick` midasm hook that did the same RY rewrite has been removed
> (function + manifest entry); `fable2_hook_direct_mouse_look` is kept for `cum_pitch` accumulation
> (and the legacy-orbit A/B fallback). Yaw is intentionally deferred to phase 2 (the native stick
> is single-DOF; the a80/84/88 group is all zeros, so it is not the yaw channel).
>
> **Next steps:** (1) wire the RY injection into the HotFunc override (write
> `RY' = clamp(stick_RY + cum_pitch*10857, 0, 65535)` to `*(r31+24)+10` at function entry, gated on
> `mouse_look && direct_mouse_look`); (2) mark the override `// Hand-tuned override` so the
> generator stops regenerating it; (3) test 1:1 parity + combined-input bounds vs. the stick.

### B0. HotFunc input injection (current phase)
- [x] Add `LookStickProcess_83045778` to `tools/make_hotfunc_overrides.py` (new `camera` category, `nv=True`); generate `src/core/hotfunc/camera/LookStickProcess_83045778.cpp` (behavior-identical strong-symbol copy).
- [x] Remove the `fable2_hook_direct_mouse_stick` midasm hook (function in `fable2_hooks.cpp` + manifest entry) — superseded by the HotFunc override.
- [x] `mouse_look.h`: add big-endian `read_guest_u16`/`write_guest_u16` (matches `REX_LOAD/STORE_U16`) for the RY access.
- [ ] Wire the RY injection into the override (write `RY' = clamp(stick_RY + cum_pitch*10857, 0, 65535)` to `*(r31+24)+10` at entry, gated on `mouse_look && direct_mouse_look`).
- [ ] Mark the override `// Hand-tuned override` (first line) so `make_hotfunc_overrides.py` stops regenerating it.
- [ ] Verify `LookStickProcess_83045778` is the handler that drives the *hero* camera (not just a sibling 8304x member) — confirm via a test run (mouse orbit works) or a `[ml-ctl]`/log check. If it is the wrong sibling, relocate the injection to the correct 8304x member.
- [ ] Disable/retire the `fable2_hook_direct_mouse_look` external orbit writes once the native-path injection is confirmed (so the orbit is applied exactly once), keeping only the `cum_pitch` accumulation.

### B1. (superseded) cam+68 target offset

### A. Verify before coding (de-risk)
- [x] Add a one-run log at the setter entry (`0x8241BDF8`): record `r3` (cam), `f1` (a68), and the saved `lr` (return address), each frame, capped.
      → done as the `[ml-set]` log (cam, f1_raw, cum_pitch, f1/f2/f3_out, floor/ceil), gated on `mouse_look_camera_diag`, capped at 400. It logs f1_raw (before modification) so we can see the controller's own theta.
- [ ] Run with the stick at rest: confirm how often the setter fires and whether `f1` is stable (persistent theta) vs. only changing on input. *(needs a test run)*
- [ ] Run pushing the stick up/down: confirm `f1` follows the stick position and resets to rest when the stick is released (proves a per-frame add = persistent offset). *(needs a test run)*
- [ ] Confirm `f1_raw` does NOT track our modified angle (no feed-back/runaway). *(needs a test run — the key correctness check)*
- [ ] Confirm the setter is NOT called for cutscene/secondary cameras in a way that breaks them (hero-flag gate covers this; verify in log). *(needs a test run)*

### B. Hook + config plumbing
- [x] `fable2_hooks.cpp`: in `fable2_hook_direct_mouse_look` (at `0x822A47F8`), after the hero-flag
      gate + cumulative accumulation, the **default path** offsets the target angles: read `a68 =
      cam+68`, clamp `cum_pitch` to `[floor - a68, ceil - a68]`, write `cam+68 = a68 + cum_pitch`,
      `cam+72 = +1.5`, `cam+76 = +5.0`; at rest (`cum_pitch == 0`) leave the target untouched.
- [x] `fable2_hooks.cpp`: moved the old 2-axis orbit into `legacy_orbit_2axis()` (forward-declared);
      the `0x822A47F8` hook calls it only when `mouse_look_legacy_orbit` is true.
- [x] Removed the abandoned setter hook: `fable_2_manifest.toml` no longer has the
      `0x8241BDF8` entry; `fable2_hook_direct_mouse_look_target` deleted from `fable2_hooks.cpp`.
- [x] `mouse_look.h`: `cum_pitch()`/`cum_yaw()` + `take_delta()` + `mouse_look_scale` (already present).
- [x] `[ml-orb]` diagnostic (gated on `mouse_look_camera_diag`, capped 400): logs `a44`/`a68_pre`/
      `a68_post`/`cum_pitch` to confirm the controller re-commits `cam+68` fresh (stable) vs.
      freezes it (would compound).
- [x] `fable2_config.h` / `fable2_config.cpp` / `config/fable2_config.toml` / runtime toml: added `mouse_look_legacy_orbit`; confirmed `mouse_look_pitch_sens`, `mouse_look_scale`, `mouse_look_pitch_floor=1.458`, `mouse_look_pitch_ceil=4.067`, `camera_hero_flag_off=108`, `direct_mouse_look` present and consistent.
- [x] Update the manifest hook comment to describe the setter-injection approach.

### C. Yaw (phase 2)
- [ ] Locate the native around-the-hero heading/turn injection point (log setter/caller or grep heading clamp constants).
- [ ] Hook it and add `cum_yaw` (scaled) there, gated on the hero camera.
- [ ] Confirm mouse left/right and stick turn share the same native heading channel.

### D. Build
- [x] Release build compiles (only the pre-existing unrelated warning). Reconfirm the recompiler emitted the hook at the `CECCameraSetTarget_8241BDF8` entry (before the `stfs f1..f6` stores).
- [x] Runtime `fable2_config.toml` + repo `config/fable2_config.toml` updated with `mouse_look_legacy_orbit`.
- [x] Launcher unaffected (the legacy flag is TOML-only, no new UI); sensitivity slider scales the direct path because the setter hook reads `mouse_look_scale`.

## Testing checklist

### Basic behavior (phase 1 = pitch axis only; yaw deferred to phase 2)
- [ ] With `direct_mouse_look = true`, mouse up/down orbits the camera vertically like the right stick; direction is natural (mouse up = camera up, not inverted).
- [ ] Mouse left/right does nothing in phase 1 (the native stick has no yaw DOF); confirm it does NOT accidentally yaw/roll the camera.
- [ ] Holding the mouse still keeps the camera at the aimed angle (no spring-back to default).
- [ ] No visible lag/float/drift vs. native stick; the orbit radius, easing, and banking look identical (they ARE the game's own).
- [ ] Sensitivity slider changes mouse orbit speed (`mouse_look_scale`).

### 1:1 parity vs. stick
- [ ] Move mouse to the same orbit angle as a full stick push; the eye position / view matches the native result exactly.
- [ ] The floor and ceiling limits are identical to the stick (cannot over-rotate past a68 ∈ [1.458, 4.067]); at the limits the camera stops cleanly (no wobble/overshoot).
- [ ] The native easing/settle (release behavior) looks the same as the stick (it's the same easing).

### Combined inputs
- [ ] Move stick and mouse in the same direction → angles add, no fighting.
- [ ] Move stick and mouse in opposite directions → smooth net result, no snapping.
- [ ] Stick at rest + mouse offset held, then move the stick → the camera moves from the mouse-set angle, and releasing the stick returns to the mouse-set angle (offset persists across stick use).
- [ ] Full-range: max stick + max mouse does not exceed the native ceiling/floor (shared clamp).

### Regression / safety
- [ ] Opening cutscene and other scripted cameras play normally (hero-flag gate); camera does not go wild.
- [ ] Scene transitions / teleports don't leave a stale offset or drift.
- [ ] `direct_mouse_look = false` → pure native behavior (no mouse camera effect beyond legacy mapping).
- [ ] `mouse_look = false` → no mouse look at all.
- [ ] No guest crash / access violation (pointer guards intact).
- [ ] Secondary camera (a108 = 0.0) is never modified by the hook.

### Log sanity (optional, with `mouse_look_camera_diag`/`dump`)
- [ ] `[ml-dump]` shows `a68` track the mouse offset and respect [1.458, 4.067].
- [ ] Setter log shows the hook adding `cum_pitch` each frame to the committed `f1`.

## Acceptance criteria

- Moving the mouse orbits the camera identically to the right stick (same radius, easing,
  banking, and floor/ceiling limits).
- No lag/float/drift relative to native.
- Holding the mouse still keeps the camera at the aimed angle (no spring-back).
- Moving mouse and stick simultaneously blends smoothly into one angle; neither resets the other.
- Cutscene / scripted cameras are unaffected (hero-flag gate).
- Toggling `direct_mouse_look` off returns to pure native behavior.
