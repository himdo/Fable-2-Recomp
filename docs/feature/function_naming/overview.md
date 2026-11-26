# Function naming for recent PRs: overview

**Status:** merged · **Commits:** f451b72 (#238) · **Last checked against:** 7863041

## What it is

The recomp turns every function in the original game into C++. Most of them only have automatic names like `sub_82278C90`, which say nothing about what they do. When a change relies on one of those functions, the project gives it a real name in `fable_2_manifest.toml`, so the next person reading the code knows what it is.

When the mouse aiming change (#183) was approved, the maintainer asked for this to be done for every recent change. This work named the 22 game functions that the last 40 changes on master used without a real name.

## What was named

- **Photo mode:** five game functions it calls. Three are the game's own Lua commands (hide the HUD, hide world icons, set the time of day). The other two rebuild the colour grading after a time-of-day change.
- **The texture morph fix:** the function that presents each frame, and the texture morph release that now runs after it.
- **Subtitles and HUD text every tick:** the per-tick game frame update, the menu and HUD update, and its timer.
- **Cloth smoothing:** the cloth update chain, the pose blend that draws characters, and the matrix helpers they share.
- **One wrong name fixed:** a function called `ComputeVectorHash` isn't a hash at all. It turns a rotation plus a scale into a matrix, so it is now `QuatScaleToMatrix`.

## What players notice

Nothing. Only names, comments and docs changed; the game behaves exactly the same.

## For contributors

Open branches that touch the renamed functions need the new names after merging master. PR #71 is the one known case.
