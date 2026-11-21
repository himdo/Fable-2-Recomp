# Mouse camera (direct mouse look): overview

**Status:** merged · **Commits:** 80e8361 (#177), shipped in 1.6.0; follow-up fixes in #189 and #227 · **Last checked against:** 5f5f55f

## What players saw before

The mouse was turned into a pretend controller stick, so it felt like one:

- **A top speed.** A fast flick turned the camera no faster than a fully pushed stick.
- **Coasting.** After the mouse stopped, the camera kept turning for a moment, because the game smooths the stick.
- **Small up/down movements were ignored**, and some movement was lost between checks.

## What's different now

- **The camera turns by a fixed angle for each bit of mouse movement**, with no top speed and no coasting. A quick flick and a slow sweep of the same distance turn the camera the same amount.
- **Up/down moves the camera height**, within the same limits as the stick, and drifts back to the middle the way it does with a controller.
- **Everything else works as before.** Walls, the height limit and the controller stick still apply, and the mouse simply adds to the stick. During target lock, cutscenes, menus and other cameras, the mouse still acts like the stick, and movement made while the camera can't use it is thrown away, so the camera never jumps afterwards.
- **A controller plays exactly as before**, and switching between controller and mouse works at any time.

## What players need to do

Nothing; direct mode is the default. To change it:

- **Launcher:** the Camera mode choice (Direct or Stick) and the mouse sensitivity slider.
- **`fable2_config.toml`, `[input]` section:** `mouse_look_mode` (`direct` or `stick`), `mouse_look_sens_x` / `mouse_look_sens_y` (degrees per bit of movement, default 0.06), `mouse_look_invert_y`, `mouse_look_scale` (overall sensitivity).
- **In-game console:** `f2 mouse` shows the settings; `f2 mouse mode|sens|invert` change them live.

The in-game camera sensitivity setting also scales the mouse.

## Known issues

The first release with this (1.6.0) could crash after alt-tab. That was fixed in 1.6.1; see `docs/fix/alt_tab_script_crash/`.
