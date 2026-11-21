# Windows runtime fixes: overview

**Status:** merged · **Commits:** 724495a, 359d3a1 (#148), e0c69c0 (#135), 5cc6513 (#229) · **Last checked against:** 5f5f55f

A group of fixes in the project's own copy of the runtime (`himdo/rexglue-sdk`), plus how to build it. The technical file has the build instructions.

## What players saw, and what's different now

- **No sound device, no game.** If Windows had no working audio output, the game could fail to run. Now it carries on silently, with the game's timing kept as if sound were playing. Real sound devices work as before. This is a start-up fallback: unplugging headphones mid-game isn't covered.
- **Stuck at 30 FPS.** The launcher's frame rate choices (30 up to unlimited) now really apply, and a separate host FPS limit caps what reaches the screen without slowing the game. See `docs/fix/fps_cap/`.
- **Flickering noise, coloured streaks and brief corruption** when new effects or areas first appeared. The renderer used to skip a draw while its shader was still being prepared in the background. Now it waits for it, so only the first use of an effect can cause a short stall.
- **The dog's fur flickered**, with blocky patches that got worse at higher resolutions. The fur is drawn as many thin layers that depended on the console's exact depth precision. A small extra depth offset for that one shader fixes it. It's on by default (`[patches] dog_fur_depth_bias` in `fable2_config.toml`).
- **Hero and dog textures** are read back from the graphics card with the upstream runtime's fix.

## What players need to do

Nothing. Known limits: frame drops below 20 FPS have been reported and aren't solved here, and rates above 60 FPS aren't guaranteed to keep every part of the game's timing correct.
