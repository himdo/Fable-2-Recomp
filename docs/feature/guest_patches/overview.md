# Game patches: overview

**Status:** merged · **Commits:** cab738b, f146b02 (#208), bfe422d (#210), 8446622 (#228), 5f5f55f (#226) · **Last checked against:** 5f5f55f

## What it is

The recomp can change small parts of the original game while it runs, the same kind of changes as the community patches for the Xenia emulator. Each patch has an on/off switch in the `[patches]` section of `fable2_config.toml`, so players can turn any of them off if it causes trouble.

## What the patches do (examples)

- **Unlock website and Collectors Edition items.** Items that used to need an online registration or the Collectors Edition appear in the guild chest when a save loads.
- **Higher tick rate (off by default).** The game updates its world 15 times a second by default; this doubles it for smoother movement. It can change the game's timing, so it is optional.
- **Subtitles and HUD text every tick**, so text updates as smoothly as the rest of the screen.
- **Smoother cloth**, so capes and robes move with the drawn body instead of lagging behind.
- **Disable MSAA (off by default).** Turns off the game's 2x anti-aliasing: much less work for the graphics card, but jaggier edges.

The technical file also covers the FPS meter (1% lows, the worst frame and hitches), which isn't a patch but lives in the same code.

The technical file lists every patch, its exact switch and how it was checked.

## What players need to do

Nothing; the defaults are chosen to be safe. To change one, edit its `[patches]` line in `fable2_config.toml` and restart the game.
