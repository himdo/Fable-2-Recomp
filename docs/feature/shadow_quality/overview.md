# Shadow quality (fine grain and filter radius): overview

**Status:** no commit yet · **Commits:** — · **Last checked against:** c2a9d12

## What it does

The game softens the edges of sun shadows with a pattern that changes from pixel to pixel. At a higher internal resolution that pattern is enlarged along with everything else, so the soft edge of a shadow on the road or across a face looks blotchy and grainy. This adds two settings for it.

## What's different now

- **Shadow Fine Grain** (on by default) makes the pattern change at every screen pixel, so a shadow's edge is a smooth, soft gradient instead of blotches. At internal resolution 1x nothing changes.
- **Shadow Filter Radius** sets how soft nearby shadows are. 1.0 is the game's own look. Lower values are sharper; 0.75 is sharper and still soft.

Distant shadows use a different, simpler filter and don't change.

## What players need to do

Nothing for the fine grain. For sharper shadows, set `shadow_filter_radius` in the `[patches]` section of `fable2_config.toml`, for example `shadow_filter_radius = 0.75`.

## Known issues

- Values below about 0.5 can make shadow edges jagged and show dark stripes on flat ground.
