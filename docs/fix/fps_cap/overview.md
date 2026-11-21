# 30 FPS cap: overview

**Status:** merged · **Commits:** 73d89b0, 62b4db2, d28da25 (#143), e0c69c0 (#135) · **Last checked against:** 5f5f55f

## What players saw

The original game is locked to 30 frames per second, and early recomp builds were too, even on fast PCs. With vsync on, the window could also report 30 frames more than the monitor's refresh rate (for example about 90 on a 60 Hz screen) without the game itself running faster.

## What caused it

The game paces itself on a counter that the graphics side ticks 60 times a second, and it waits for two ticks per frame: 2 × 1/60 s = 30 frames per second. Speeding up or switching off that pacing removes the cap; nothing in the game's own frame limiter needed changing.

The "refresh rate + 30" number came from the recomp drawing its own overlay once per monitor refresh, plus once more for every game frame, with nothing limiting the total.

## What's different now

- **The launcher sets the frame rate.** It offers 30, 60, 120, 144, 165, 240 and unlimited, and turns off the 30 FPS pacing when you pick 60 or more.
- **A separate host FPS limit** caps what reaches the screen (what overlays such as NVIDIA's report) without slowing the game.
- **With vsync on, the screen updates at most once per monitor refresh.**
- The F3 counter shows the game's own frame rate.

## What players need to do

Pick a frame rate in the launcher. Very high frame rates aren't guaranteed to keep every part of the game's timing correct; 60 is the tested target.
