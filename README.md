# Fable II for Steam Deck

A native **Steam Deck / Linux** version of [**Fable 2 Recomp**](https://github.com/himdo/Fable-2-Recomp), himdo's static recompilation of Fable II for PC. It runs directly on SteamOS with Vulkan, without Proton or an emulator, and is tuned for the Deck's CPU, GPU and 15 W power budget.

**[⬇ Download the latest release](https://github.com/darokk/Fable-2-Recomp/releases/latest)** · [Install](#install) · [Playing](#playing) · [Settings](#settings) · [Performance](#performance) · [What this fork changes](#what-this-fork-changes) · [Credits](#credits)

> [!IMPORTANT]
> The download holds **no game content**. You need your own copy of **Fable II Game of the Year Edition** as a disc image (`.iso`). This project does not condone piracy: links to or requests for game files will be removed.

## Install

You need a Steam Deck with SteamOS 3.6 or newer, your Fable II GOTY disc image, and about 7 GB of free space.

1. **Download** `Fable2-SteamDeck-<version>.tar.gz` from the [latest release](https://github.com/darokk/Fable-2-Recomp/releases/latest).
2. **Extract it on the Deck, in Desktop Mode:** move the file to `~/Games/` (the *Games* folder in your home folder), right-click it and choose *Extract* → *Extract archive here*. This creates the folder `Fable2-SteamDeck`.
3. **Put your disc image** (`.iso`) in `~/Downloads`. The `Fable2-SteamDeck` folder or an SD card work too.
4. **Double-click `Fable2.sh`** in the `Fable2-SteamDeck` folder and choose *Execute*. On the first start it:
   - finds your disc image and asks to extract the game files from it (about 6.7 GB, a few minutes, with a progress bar). If it can't find one, it lets you pick the file;
   - offers to **add Fable II to your Steam library**. Choose *Add to Steam*;
   - starts the game.

That's it: from now on, start **Fable II** from your library in **Gaming Mode** like any other game. The disc image isn't changed and isn't needed any more, so you can delete it.

<details>
<summary>Adding it to Steam by hand, copying the game files yourself</summary>

- **Steam shortcut by hand:** in Desktop Mode, open Steam → *Games* → *Add a Non-Steam Game to My Library*, browse to `Fable2.sh` (set the file type to *All files*) and add it. In the shortcut's *Properties → Compatibility*, make sure no Proton version is forced.
- **Game files by hand:** instead of the disc image, you can copy `default.xex` and the whole `data` folder from your disc into the `Fable2-SteamDeck` folder, next to `Fable2.sh`.
- **From a terminal:** `./Fable2.sh` asks the same questions in the terminal.
</details>

### Updating

Download the new release and extract it over your existing `~/Games/Fable2-SteamDeck` folder. Your saves, game files and shader cache stay where they are, and the game starts straight away. The settings file `fable2_config.toml` is replaced with the new defaults, so if you changed settings, back it up first and copy your changes back afterwards.

### Uninstalling

Delete the `Fable2-SteamDeck` folder (it holds everything: game files, saves, settings, logs, shader cache) and remove the Fable II shortcut from your Steam library.

## Playing

- **Controls:** the Deck's controls work like an Xbox 360 controller, with the Steam Deck's own buttons shown in the game's prompts. In Desktop Mode, keyboard and mouse work too (see [Keyboard and mouse](#keyboard-and-mouse)).
- **Saves** are kept in `Fable2-SteamDeck/saves`. Back up that folder to keep your progress.
- **First visits:** the first time an area loads, its shaders are compiled, which can cause brief stalls. Later visits are smooth.
- **Battery and frame rate:** the game runs at up to 60 fps. For a steadier rate in the heaviest areas, or longer battery life, set the Quick Access Menu's Performance tab to 40 Hz with a 40 fps limit.

### Keyboard and mouse

Keyboard and mouse act as a gamepad (together with a real one), with mouse look: moving the mouse turns the camera.

| Key | Game input |
|---|---|
| `W` `A` `S` `D` | Move (left stick) |
| Left mouse button | `X` (melee) |
| Right mouse button | `Y` (ranged) |
| Middle mouse button / `B` | `B` (will) |
| `Shift` / `E` | `A` |
| `Q` / `R` | Left / right trigger |
| `Z` / `Tab` | Left / right shoulder |
| Arrow keys | D-pad |
| `Escape` | Start (pause menu) |
| `M` | Back |

To remap, edit `keyboard_gamepad_map` under `[input]` in `fable2_config.toml` (format `Key:Button,Key:Button,...`); the mouse look settings are in the same section. The details are in [UPSTREAM-README.md](UPSTREAM-README.md#keyboard-controls).

### Known issues

- A texture or object can occasionally render wrong for a single frame (as in the Windows version).
- In Desktop Mode, the window can stay white for several seconds at start-up before the first video. Gaming Mode isn't affected.

If something goes wrong, the game shows an error with the path of its log file. For a bug report, set `log_level = "info"` in `fable_2.toml`, reproduce the problem and [open an issue](https://github.com/darokk/Fable-2-Recomp/issues) with the newest file from `logs/`.

## Settings

Settings are in `fable2_config.toml` in the `Fable2-SteamDeck` folder (edit it in Desktop Mode with any text editor). The most useful ones:

| Setting | Default | What it does |
|---|---|---|
| `fps_limit` (`[perf]`) | `60` | Frame rate cap; `30` is the original game's lock |
| `high_tick_rate`, `higher_hf_tick_rate` (`[patches]`) | `false` | Set both to `true` for smoother game logic (it otherwise updates at 15 Hz, which can feel choppy even at 60 fps) |
| `deck_button_prompts` (`[input]`) | `"dark"` | Button icons: `"dark"`, `"light"`, or `"off"` for the original Xbox 360 icons |
| `menu_instant_response` (`[patches]`) | `true` | Menus react to a press straight away |
| `menu_repeat_delay` (`[patches]`) | `0.18` | Seconds before holding a direction starts repeating (original: `0.25`) |
| `menu_scroll_acceleration` (`[patches]`) | `1.5` | Held scrolling speeds up to this factor (original: `1`) |
| `menu_animation_speed` (`[patches]`) | `2.5` | Speed of menu slides and fades (original: `1`) |
| `interpolation` (`[patches]`) | `true` | Capes and coats move smoothly with the body |

Every setting is described in the file itself.

## Performance

Measured on a Steam Deck OLED in Gaming Mode, 90-second runs from the same saves:

| Scene | Fable II for Steam Deck | First Linux build |
|---|---|---|
| Interiors and lighter areas (e.g. Bowerstone Inn) | 60 fps (cap) | 37–55 fps |
| Bowerstone Market, standing | 60 fps (cap) | about 51 fps |
| Bowerstone Market, turning the camera | about 50 fps, no stutters | 44 fps, frequent 70 ms+ stutters |
| Busy village, many NPCs | about 52 fps | 33–35 fps |
| Bowerstone Old Town (childhood, snow) | 58.6 fps | – (54.5 fps in 1.0.1) |

The game presents one frame per game frame, so the Deck's performance overlay shows its real frame rate.

## What this fork changes

On top of Fable 2 Recomp and its native Linux build, with the renderer work in the [SDK fork](https://github.com/darokk/rexglue-sdk):

**Performance**
- Vulkan renderer work for the Deck: GPU memory handling, render target and occlusion query fixes, and the fix for stutters when turning the camera.
- The recompiled game code is built with profile-guided optimisation from Deck play sessions, ThinLTO and the Deck's Zen 2 CPU as the target.
- Five of the recompiler's register optimisations are on, including lock-free atomic updates on the game thread (+5–7.5% in 1.0.2).
- Game threads that wait in a loop sleep briefly instead of spinning, which leaves more of the 15 W budget to the GPU.
- The Lua garbage collector, about half of the game thread's time in busy areas, runs as native code.
- 60 fps cap by default, no CPU wait for the GPU at the start of each frame, and one presented frame per game frame.

**Steam Deck experience**
- Steam Deck button icons in Fable II's own style, in the game and the pub games (dark, light or off).
- Faster, more responsive menus: instant response, a shorter hold delay, scroll acceleration and quicker animations, each adjustable.
- Menus laid out for the Deck's 16:10 screen.
- First-start installer: finds your disc image, extracts the game files and adds the game to Steam.

**Fixes**
- A crash after committing a crime with guards nearby (Linux builds only).
- Correct hero and dog textures and eyes on Vulkan.

**From upstream**, pulled in when it's noteworthy: for example himdo's cloth interpolation (smooth capes and coats).

### Releases

| Version | Date | Highlights |
|---|---|---|
| [1.0.3](https://github.com/darokk/Fable-2-Recomp/releases/tag/steamdeck-1.0.3) | 2026-11-21 | First-start installer: game files from your disc image, *Add to Steam* |
| [1.0.2](https://github.com/darokk/Fable-2-Recomp/releases/tag/steamdeck-1.0.2) | 2026-11-20 | Crime/guard crash fixed, up to 7.5% faster, smooth capes and coats |
| [1.0.1](https://github.com/darokk/Fable-2-Recomp/releases/tag/steamdeck-1.0.1) | 2026-11-20 | Steam Deck button icons, faster menus |
| [1.0](https://github.com/darokk/Fable-2-Recomp/releases/tag/steamdeck-1.0) | 2026-11-19 | First Steam Deck release |

## Building from source

The code is on the [`steam-deck-linux`](https://github.com/darokk/Fable-2-Recomp/tree/steam-deck-linux) branch. It builds with the SDK fork [darokk/rexglue-sdk](https://github.com/darokk/rexglue-sdk) (branch `steam-deck-vulkan`); [Linux.md](https://github.com/darokk/Fable-2-Recomp/blob/steam-deck-linux/Linux.md) has the full instructions, including the Deck package (`./build.sh --cpu deck package`) and building on the Deck itself. The developer documentation of the main project (user config, in-game console, F5 Lua scripts, tracing, remote control) is in [UPSTREAM-README.md](UPSTREAM-README.md).

## Credits

- **[Fable 2 Recomp](https://github.com/himdo/Fable-2-Recomp)** by **himdo** and its contributors: the recompilation of Fable II itself, its patches, the native Linux build this fork starts from, and the disc reader that the installer's extractor is ported from. All credit for making Fable II run natively goes to them. For the Windows version, go there.
- **[ReXGlue SDK](https://github.com/rexglue/rexglue-sdk)**, the Xbox 360 static recompilation toolkit, built on the work of [Xenia](https://github.com/xenia-project/xenia) and [XenonRecomp](https://github.com/hedge-dev/XenonRecomp).
- Steam Deck button glyphs from a free glyph pack, restyled for Fable II. Keycap font: Google Sans (SIL Open Font License).

Fable II is © Microsoft; this project is not affiliated with Microsoft, Lionhead Studios or Valve. The code is under the [MIT licence](LICENSE), like Fable 2 Recomp.
