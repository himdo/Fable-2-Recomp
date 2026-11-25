# Rendering and performance (1.6 speed-ups and the bugs they exposed): overview

**Status:** merged · **Commits:** recomp 0b163fb (#149), 6e9f5c4 (#150), c2cad76 (#151), d28da25 (#143), 359d3a1 (#148), e0c69c0 (#135), 2df8825 (#158); SDK bda14fe (#25), e251d3a (#26), c04750f (#27), a56e5ae (#28), all shipped in 1.6.0 · **Last checked against:** 7863041

## What changed

Version 1.6.0 got a lot faster. At twice the normal internal resolution with no frame cap, two test saves went from about 28 FPS to 68 and 55 FPS.

- **The CPU no longer waits for the graphics card twice a frame.** The Xbox 360's graphics chip can write results straight into memory that the game then reads. Fable II does this to work out terrain detail and to move animated ("skinned") meshes such as swaying trees. On a PC the graphics card has its own memory, so the game used to stop and wait for those results to be copied back. Now the CPU works out the same results itself. The graphics card still draws everything as before; only the wait is gone.
- **Less copying of picture data** for full-screen draws. This also removed a purple bar at the bottom of the screen.
- **The game's memory is no longer copied to the graphics card in full every frame**, only what changed.
- **Smoother frame pacing**, and a separate "host FPS limit" in the launcher that caps what reaches the screen without slowing the game down.

## Bugs that showed up, and what happened to them

- **Flickering trees: fixed.** An old bug in the graphics layer made one kind of number always come out as 0. Skinned meshes therefore had no lighting direction on the graphics card, while the new CPU path had the right one, and the two took turns on screen. The new code only made the old bug visible. Older lighting reports may have been the same bug.
- **Flashing Lionhead logo at the start: fixed.** The intro video frees and re-allocates its memory every frame, and the graphics layer stopped watching that memory while it was free, so it missed the new frames. Copying everything every frame had been hiding this.
- **Broken trees and textures in Oakfield:** reported while this work landed. A later change (#158) that keeps the graphics card at most one frame behind may have fixed it, but that isn't confirmed in these notes.

## What players need to do

Nothing. Every speed-up has a switch in `fable2_config.toml` in case one causes trouble on your machine:

| Setting | Section | Default | Setting it the other way |
|---|---|---|---|
| `terrain_tess_cpu` | `[patches]` | `true` | `false`: the graphics card computes terrain detail again and the CPU waits (slower) |
| `skinning_cpu` | `[patches]` | `true` | `false`: the same for animated meshes |
| `faster_render_targets` | `[patches]` | `true` | `false`: the old, slower copying of picture data |
| `refresh_gpu_memory_each_frame` | `[perf]` | `false` | `true`: copy all memory every frame (about 11% slower at 2x, but hides memory-tracking bugs) |
