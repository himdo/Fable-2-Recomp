# Building and launching on Linux (and the Steam Deck)

Linux support is new and experimental. The game builds as a native x86-64
executable that renders with Vulkan (the SDK has no D3D12 on Linux). See
[Known limitations](#known-limitations) before reporting problems.

## How this differs from the Windows build

The Windows build tunes the D3D12 renderer for Fable II. Those Fable II fixes
now live in shared SDK code and are used by the Vulkan renderer too:

| Fix | Setting (`fable2_config.toml`) | What it does |
|---|---|---|
| CPU terrain tessellation | `[patches] terrain_tess_cpu` | Stops a GPU wait + readback every frame |
| CPU skinning | `[patches] skinning_cpu` | Same, for the skinned memexport draws |
| Depth-clear transfer skip | `[patches] faster_render_targets` | Skips copying depth that a clear overwrites anyway |
| Wait for async pipelines | SDK cvar `async_pipeline_wait` | Draws wait for their pipeline instead of drawing with an invisible placeholder (missing objects, stale textures) |
| Dog fur depth bias | `[patches] dog_fur_depth_bias` | Stops the dog's fur Z-fighting |
| Relative constant clamp | SDK cvar `spirv_clamp_relative_float_constants` | Keeps the trail effect from reading out of bounds (an AMD GPU hang on D3D12) |
| Hero/dog texture fallback | `[patches] vulkan_texture_morph_fallback` | On Vulkan, uses the texture readback mode, because realtime morphing renders the hero and dog black there |

On top of that, Linux gets:

- Keyboard and mouse as a gamepad, mouse look (pointer lock), F4 debug menu,
  F5 Lua and F9 pause, from the game window's own input events.
- Busy-wait polls (the frame limiter wait and back-to-back yields) sleep
  100 µs at a time instead of spinning a core at 100%. On the Steam Deck that
  CPU power goes back to the GPU. `[perf] idle_yield_sleep_us` in
  `fable2_config.toml` sets the sleep; 0 restores the spin.
- `--cpu deck` builds the recompiled code for the Deck's Zen 2 CPU (AVX2, FMA).

## 1. Install the build tools

Ubuntu 24.04 or a derivative (on a Steam Deck, see [below](#steam-deck)):

```bash
sudo apt install wget gnupg lsb-release software-properties-common \
    cmake ninja-build build-essential git python3 pkg-config autoconf \
    libgtk-3-dev libx11-xcb-dev libxss-dev libvulkan-dev \
    libwayland-dev libwayland-bin wayland-protocols libxkbcommon-dev libdecor-0-dev \
    libasound2-dev libpulse-dev libpipewire-0.3-dev
wget -qO llvm.sh https://apt.llvm.org/llvm.sh && chmod +x llvm.sh && sudo ./llvm.sh 20
```

You need CMake 3.25 or newer, Clang 20 and a GPU with a working Vulkan driver.

## 2. Add the game content

The game files are not in this repository. Put these two items from your own
Fable II Game of the Year Edition disc image in the repository root:

- `default.xex`
- `data/` (about 6.5 GB)

Both are git-ignored, and both must be real files, not symlinks: the code
generator refuses a `default.xex` that resolves outside the project, and the
game's file system does not follow a `data` symlink. To keep the content
elsewhere, pass `--game_data_root=<dir>` to the game instead (`./run.sh`
passes the repository root). The supported versions and their SHA-256 hashes are in
`constants/game_versions.json`; check yours with `sha256sum default.xex`.

To extract them, use the repository's own extractor (`tools/x360extract`,
.NET 8) on any machine with the .NET SDK:

```bash
dotnet run --project tools/x360extract -c Release -- "<your disc>.iso" --files default.xex,data -o .
```

With only a newer .NET installed, set `DOTNET_ROLL_FORWARD=Major` first.

## 3. Build

```bash
./build.sh                      # SDK from the thirdparty/rexglue-sdk submodule
./build.sh --sdk ../rexglue-sdk # SDK from another source tree (e.g. a branch)
```

The first run takes a long time (tens of minutes; over an hour on a Steam
Deck). It fetches the SDK's dependencies (about 2 GB), builds the SDK, runs
the code generator on `default.xex`, compiles about 300 recompiled translation
units and links the executable. Later runs only rebuild what changed.

| Option | Effect |
|---|---|
| `--cpu generic` | x86-64-v2, runs on any 64-bit CPU from ~2009 on (default) |
| `--cpu deck` | Tuned for the Steam Deck (Zen 2) |
| `--cpu native` | Tuned for the CPU you build on |
| `-debug` | Debug build (`-O0`, far too slow to play) |
| `-clean` | Delete the build directory for the selected configuration |
| `<target>` | Build a different CMake target |

Every `--cpu` level keeps the floating-point results identical to the Windows
build (`-ffp-contract=off`), so physics and animation behave the same.

The result is `out/build/linux-amd64-release/fable_2`, with `librexruntime.so`
and `librexgpu-xenos.so` next to it.

## 4. Launch

```bash
./run.sh
```

`./run.sh --make-shortcut` writes a `Fable II.desktop` launcher in the
repository root. Extra arguments are passed to the game, which runs as:

```bash
out/build/linux-amd64-release/fable_2 --game_data_root="$PWD"
```

Saves, the shader cache, logs and `fable2_config.toml` are written next to the
executable, in `out/build/linux-amd64-release/`. Back up `saves/` there. If
something goes wrong, the newest log is in `logs/`.

## Portable package (build once, copy anywhere)

```bash
./build.sh --cpu deck package
```

builds the Release game and writes `out/dist/Fable2-Linux-<cpu>/` plus a
`.tar.gz` of it (about 40 MB). The package holds no game content: extract it
on the target machine, copy `default.xex` and `data/` next to `Fable2.sh`, and
run `Fable2.sh`. Saves, settings, logs and the shader cache are kept in that
folder. The binaries need glibc 2.38 or newer (Ubuntu 24.04, SteamOS 3.6 and
later); `Fable2.sh` checks and says so if the system is too old.

## Steam Deck

### With a package built on another PC (recommended)

1. On the PC, build the package (`./build.sh --cpu deck package`, for example
   in WSL2 on Windows) and copy `Fable2-Linux-deck.tar.gz`, `default.xex` and
   the `data` folder to a USB drive or SD card.
2. On the Deck, in Desktop Mode, copy the archive to `~/Games/` and extract it
   there (right-click → Extract → Extract archive here). Extracting on the Deck
   keeps the executable flags, which a copy from a FAT32/exFAT drive would lose.
3. Copy `default.xex` and the whole `data` folder into
   `~/Games/Fable2-Linux-deck/`, next to `Fable2.sh`.
4. Double-click `Fable2.sh` (choose *Execute*), or run it from Konsole, to
   test it in Desktop Mode.
5. For Gaming Mode, add `Fable2.sh` to Steam as described in step 5 below.

### Building on the Deck itself

SteamOS's root filesystem is read-only, so build inside a
[Distrobox](https://distrobox.it/) container (preinstalled on SteamOS 3.5 and
later). Everything below runs in Desktop Mode, in Konsole.

1. **Copy the project** (both repositories plus the extracted `default.xex`
   and `data/`; the ISO is not needed) to the Deck, for example to
   `~/Games/Fable2/`. A USB drive works (no game file is over 4 GB). If it is
   FAT32 or exFAT, the scripts lose their executable bit, so run
   `chmod +x ~/Games/Fable2/Fable-2-Recomp/*.sh` afterwards.

2. **Create the build container** (once):

   ```bash
   distrobox create --name fable2-build --image ubuntu:24.04
   distrobox enter fable2-build
   ```

   Inside it, run the two commands from [step 1](#1-install-the-build-tools).

3. **Build** (inside the container):

   ```bash
   cd ~/Games/Fable2/Fable-2-Recomp
   ./build.sh --cpu deck
   ```

   Keep the Deck plugged in. If it runs out of memory while linking, close
   other apps, or add `-j4` by running `CMAKE_BUILD_PARALLEL_LEVEL=4 ./build.sh --cpu deck`.

4. **Run** it from Konsole outside the container with `./run.sh`. If that
   fails with missing libraries or a `GLIBC_2.xx not found` error, run it in
   the container instead: `distrobox enter fable2-build -- ./run.sh`.

5. **Gaming Mode:** in Steam, *Add a Non-Steam Game*, browse to `run.sh`
   (set the file filter to *All files*), and make sure no compatibility tool
   (Proton) is forced for it. If you had to run it in the container, point the
   shortcut at `/usr/bin/distrobox-enter` with launch options
   `fable2-build -- /home/deck/Games/Fable2/Fable-2-Recomp/run.sh`.

Suggested settings on the Deck:

- In `out/build/linux-amd64-release/fable2_config.toml`, turn on
  `high_tick_rate` and `higher_hf_tick_rate` (together). The game's own logic
  otherwise ticks at 15 Hz, which feels choppy even at a high frame rate.
- The game is capped at 60 fps by default on Linux (`[perf] fps_limit` in
  `fable2_config.toml`; 30 = the original lock, 0 = unlimited). For a steadier
  40 fps, set the Quick Access Menu's Performance tab to a 40 Hz refresh rate
  and 40 FPS limit as well.
- The first time each area loads, new shaders are compiled. That stalls for a
  moment instead of skipping draws, and the pipeline cache next to the
  executable makes later visits smooth.

## Measuring performance

- The game presents exactly one host frame per game frame, so the Steam
  Deck's performance overlay (or MangoHud) shows the game's real frame rate
  and frame times. The F3 debug overlay also shows it, but while any ImGui
  overlay is open the window is repainted at the display refresh rate, so
  external FPS counters then read the refresh rate instead.
- Every 5 seconds the log (`logs/` next to the executable) gets a line like
  `[guest-fps] 47.9 fps over 5.0 s, slowest frame 61.2 ms, 3 of 240 frames
  over 33 ms; host presented 47.9/s`. The "over 33 ms" count is the number of
  visible stutters. `log_guest_fps = false` in `fable_2.toml` turns it off.

## Display and performance settings

`fable_2.toml` next to the executable holds SDK settings, one `name = value`
per line. The same names work on the command line as `--name=value`, which
takes priority.

The frame rate cap is `[perf] fps_limit` in `fable2_config.toml` (default 60
on Linux). It sets the SDK cvars `frame_limit` and `guest_vblank_pacing`, the
same ones the Windows launcher uses, so setting either of those here
overrides it.

```toml
# Present with strict vsync (no tearing, no busy main thread).
vulkan_allow_present_mode_immediate = false
vulkan_allow_present_mode_mailbox = false
vulkan_allow_present_mode_fifo_relaxed = false
```

`clear_memory_page_state` is already off by default (the
`[perf] refresh_gpu_memory_each_frame = false` setting in
`fable2_config.toml`).

## Known limitations

- **Only lightly tested.** The build (`--cpu deck`, clang 20) has been
  compiled on Ubuntu 24.04 under WSL2 and boots through the intro to the
  title screen there, on Mesa's CPU renderer (llvmpipe), so neither GPU
  behaviour nor performance has been checked on a real Linux GPU driver yet.
- **Higher internal resolution** (`resolution_scale`) does not sharpen the
  presented image yet, on Vulkan or D3D12.
- **Some developer diagnostics are Windows-only:** the crash logger, the
  render-stall dumper and the heap scan.
- **The Windows launcher (WPF) is not ported.** Its settings map to
  `fable_2.toml` and `fable2_config.toml`, which you can edit by hand.
- **The `.cmd` helper scripts** copied next to the executable are for Windows
  and can be ignored.

## Building without root access

`build.sh` takes its tools from `PATH`, so a toolchain unpacked anywhere (for
example `.deb` packages extracted with `dpkg -x`) works once its `bin/` is on
`PATH` and its `pkgconfig/` directory is on `PKG_CONFIG_PATH`. If
`libxss-dev` is missing, the build carries on without the X11 screensaver
extension.
