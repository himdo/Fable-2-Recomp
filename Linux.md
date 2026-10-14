# Building and launching on Linux

Linux support is new and experimental. The game builds as a native x86-64
executable and starts on Vulkan, but it has only had a short smoke test (see
[Known limitations](#known-limitations)).

Tested on Linux Mint 22.3 (Ubuntu 24.04 base) with an AMD GPU on Mesa RADV.

## 1. Install the build tools

```bash
sudo apt install clang-20 lld-20 ninja-build cmake python3 git pkg-config \
    libx11-dev libx11-xcb-dev libxcb1-dev libxext-dev libxrandr-dev \
    libxcursor-dev libxi-dev libxfixes-dev libxss-dev libxkbcommon-dev \
    libwayland-dev libvulkan-dev libgl-dev libegl-dev libgtk-3-dev \
    libasound2-dev libudev-dev libdbus-1-dev
```

You need CMake 3.25 or newer, Clang 18 or newer (20 is what was tested), and
a GPU with a working Vulkan driver.

## 2. Add the game content

The game files are not in this repository. Put these two items from your own
Fable II Game of the Year Edition disc image in the repository root:

- `default.xex`
- `data/` (about 6.5 GB)

Both are git-ignored. Any Xbox 360 ISO (XGD2) extractor can produce them; the
launcher's "Extract from ISO" button described in the README is Windows-only.

The supported disc versions and their SHA-256 hashes are listed in
`constants/game_versions.json`. Check yours with:

```bash
sha256sum default.xex
```

## 3. Build

```bash
./build.sh
```

The first run takes a while. It downloads the ReXGlue SDK source and its
dependencies (about 2 GB), builds the SDK, recompiles the game code, and
links the executable. Later runs only rebuild what changed.

Other forms:

| Command | Effect |
|---|---|
| `./build.sh -debug` | Debug build (not yet tested on Linux) |
| `./build.sh -clean` | Delete the build directory for the selected configuration |
| `./build.sh <target>` | Build a different CMake target |

The result is `out/build/linux-amd64-release/fable_2`, with `librexruntime.so`
and `librexgpu-xenos.so` staged next to it.

## 4. Launch

From the repository root:

```bash
./run.sh
```

To get a double-clickable launcher, run `./run.sh --make-shortcut` once. It
writes `Fable II.desktop` in the repository root; copy that file to your
desktop or to `~/.local/share/applications/` (for the application menu) if
you like. Re-run the command if you move the repository.

`run.sh` passes extra arguments on to the game and is equivalent to:

```bash
out/build/linux-amd64-release/fable_2 --game_data_root="$PWD"
```

`--game_data_root` tells the game where `default.xex` and `data/` are. Without
it the game only looks in the executable's own directory and exits with a
"default.xex was not found" error.

Useful options (see the README for the full list):

| Argument | Effect |
|---|---|
| `--game_data_root <path>` | Directory holding `default.xex` and `data/` |
| `--window_width` / `--window_height` / `--fullscreen` | Presentation options |
| `--skipHash` | Do not block launch when `default.xex` is not a catalogued version |

Saves, shader cache, logs and `fable2_config.toml` are written next to the
executable, in `out/build/linux-amd64-release/`. Back up the `saves/` folder
there to keep your progress. If something goes wrong, the log is in
`logs/fable_2_001.log`.

## Display and performance settings

Create `fable_2.toml` next to the executable
(`out/build/linux-amd64-release/fable_2.toml`) to make settings permanent.
Each line is `name = value`; the same names work on the command line as
`--name=value`, which takes priority.

```toml
# Internal render resolution multiplier. The game renders at 1280x720;
# 2 = 2560x1440, 3 = 3840x2160. Needs a restart. Experimental on Vulkan.
resolution_scale = 2

# The game locks itself to 30 fps by waiting two emulated 60 Hz vblanks per
# frame. vsync = false removes that lock; frame_limit caps the result
# (0 = unlimited). vsync = true restores the original locked 30 fps.
vsync = false
frame_limit = 60

# Present with vsync instead of immediate mode, so the window is not redrawn
# faster than the monitor refreshes.
vulkan_allow_present_mode_immediate = false
vulkan_allow_present_mode_mailbox = false
vulkan_allow_present_mode_fifo_relaxed = false

# Keep uploaded game memory on the GPU between frames instead of re-uploading
# all of it (about 15 MB) every frame. Set back to true if characters or
# animated objects look wrong.
clear_memory_page_state = false
```

Notes:

- Without the `vulkan_allow_present_mode_*` lines the window is presented
  in immediate mode, which keeps the main thread at about half a core for no
  benefit. With only the first two, the driver picks "relaxed" vsync, which
  tears whenever a frame is late; the third line selects strict vsync.
- `resolution_scale` logs a warning at startup that the game is presenting
  from an unscaled 1280x720 image, and a captured frame still looked soft, so
  on Vulkan it probably does not sharpen the final picture yet.
- The game's threads wait for each other, and for the end of each frame, by
  busy-polling, which pins CPU cores at 100% even at the menu. Those polls
  sleep 100 microseconds at a time instead (about 500 on Windows, the
  shortest sleep available there). `idle_yield_sleep_us` under
  `[perf]` in `fable2_config.toml` changes the sleep; 0 restores the spin.
- Frame rate is limited by the renderer's single GPU-command thread. With
  the 30 fps lock removed and no cap, a detailed outdoor area measured 72 to
  80 fps and lighter views 120 to 240 fps on a Ryzen 9 9950X / RX 7900 XTX
  machine with the settings above (62 to 68 fps in the same area with
  `clear_memory_page_state` left at its default of true).
  `readback_memexport = false` may raise that further but turns off a
  correctness feature, so expect visual glitches.
- If the picture tears on X11 with several monitors even with the vsync
  settings above, the cause is the desktop, not the game: turn on the
  driver's TearFree option (`xrandr --output <name> --set TearFree on` for
  amdgpu).
- Start-up shows a black screen for about 13 seconds, then the logo videos
  (25 to 40 seconds). `skip_intro_videos = true` under `[patches]` in
  `fable2_config.toml` skips the videos.
- The `high_tick_rate` and `higher_hf_tick_rate` options in
  `fable2_config.toml` (turn both on together) double the game's internal
  tick rates, which that file says improves UI frame rate and input delay.

## Hero and dog textures

On Linux (Vulkan), set these two values under `[patches]` in
`out/build/linux-amd64-release/fable2_config.toml`, otherwise the hero's skin
and hair and the whole dog render black:

```toml
realtime_texture_morphing = false
hero_dog_texture_readback = true
```

The shipped defaults are the other way round. The default mode builds those
textures entirely on the GPU, and on the Vulkan renderer the result comes out
black (cause not found yet; `readback_resolve = full` does not help). The
settings above use the game's original mode and read that one texture back to
the CPU while it is being rebuilt (at load and when the hero's appearance
changes), which costs nothing during normal play.

## Known limitations

- **Keyboard and mouse do not work as a gamepad.** That code
  (`src/input/keyboard_gamepad.h`) is Windows-only for now, so you need a
  controller.
- **The F5 external Lua runner is Windows-only.**
- **Some developer diagnostics are disabled:** the crash logger, the
  render-stall dumper and the heap scan.
- **The dog-fur depth-bias fix is probably not active**: the log reports its
  settings as missing on the Vulkan path.
- **Gameplay is only lightly tested.** Boot, the title screen, a level load
  and the opening cutscene render correctly, and a save has been loaded with a
  controller. Nothing longer has been checked.
- **The `.cmd` helper scripts** copied next to the executable are for Windows
  and can be ignored.

## How the Linux build differs from Windows

- The ReXGlue SDK is built from the `thirdparty/rexglue-sdk` submodule as part
  of the game build, and the renderer is Vulkan only (no D3D12).
- `build.sh` applies `thirdparty/rexglue-sdk-linux.patch` to the submodule.
  It holds the build fixes the SDK fork needs on Linux plus Linux/Vulkan
  performance fixes (cheaper write-watch fault handling, memexport readback
  buffer reuse, less lock contention in the shared-memory path), so the
  submodule shows as modified in `git status` after a build.
- The stable-hash PE cleaning, PDB staging and version-resource steps in
  `CMakeLists.txt` are Windows-only and are skipped.

## Building without root access

If you cannot install packages, `build.sh` also accepts tools unpacked into
`out/tooling/`:

- Clang 20 under `out/tooling/llvm20/usr/` (the contents of the `clang-20`,
  `lld-20`, `llvm-20` and related `.deb` packages, unpacked with `dpkg -x`)
- a `ninja` binary in `out/tooling/bin/`
- extra `-dev` packages under `out/tooling/sysdeps/usr/`, with the `prefix=`
  line of their `.pc` files edited to point at that directory

If `libxss-dev` is missing, the build carries on without the X11 screensaver
extension.
