# Fit game to window (widescreen): overview

**Status:** merged · **Commits:** 4f0afe5 (#127) · **Last checked against:** 5f5f55f

## What it does

The original game is made for 16:9 screens. With this feature, the game draws the world at your window's shape, from 4:3 up to very wide screens, so a wider window shows more of the world at the sides instead of stretching the picture. The HUD moves to the edges of the window, and the menus and book screens (pause menu, items, shops) spread out to match.

Some things stay 16:9 on purpose, in the middle of the screen: loading screens, saving banners and videos (with black bars).

Resizing the window updates everything straight away; no restart is needed.

## What players need to do

Nothing; it's on by default. To turn it off, set `fit_game_to_window = false` in `fable_2.toml`. The developer console has `f2 ws on|off` and `f2 ws aspect` to try it live.

## Known issues

- **Menus are tuned for 16:9 up to about 21:9.** On narrower screens (4:3, 16:10) the menus keep their 16:9 layout and lose their edges. Around 32:9, black bars can appear between the menu background and the covers.
- **The title screen's "Press A to start" box stays 16:9**, with the main menu's scenery showing at the sides until it fades.
- **Paused cutscenes and the moment before the pause menu opens** show the frozen picture at 16:9, with coloured or stale bars at the sides.
