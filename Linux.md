# Building and playing on Linux (and the Steam Deck)

The game builds as a native x86-64 Linux executable and renders with Vulkan.
It is not Proton or Wine. Linux support is new: there is no launcher yet, so
setup is done from a terminal (see [Known limitations](#known-limitations)).

Tested on:

- Nobara 44 (KDE Plasma, Wayland) with a Ryzen 5 7600 and an RTX 3060 Ti on
  the NVIDIA proprietary driver: intro, new game, the opening chapters,
  combat and saving, at the game's 30 fps lock and at 60 fps. An earlier
  version was also run on Linux Mint 22.3 with an AMD GPU (Mesa RADV).
- Steam Deck OLED (SteamOS, Gaming Mode, Mesa RADV): many hours of play, with
  a package built on a PC (see [Steam Deck](#steam-deck)).

## 1. Install the tools

You need Clang 20 (Clang 22 is untested), lld, CMake 3.25 or newer, Ninja,
Python 3, Git, the .NET 8 SDK (for the disc extractor), a GPU with a working
Vulkan driver, and the development headers SDL3 is built against. On a Steam
Deck, see [below](#steam-deck) instead.

Fedora / Nobara:

```bash
sudo dnf install clang20 lld20 cmake ninja-build python3 git pkgconf-pkg-config \
    dotnet-sdk-8.0 vulkan-headers vulkan-loader-devel \
    libX11-devel libxcb-devel libXext-devel libXrandr-devel libXcursor-devel \
    libXfixes-devel libXi-devel libXScrnSaver-devel libxkbcommon-devel \
    wayland-devel wayland-protocols-devel libdecor-devel libdrm-devel \
    mesa-libgbm-devel mesa-libGL-devel mesa-libEGL-devel \
    alsa-lib-devel pulseaudio-libs-devel pipewire-devel dbus-devel \
    systemd-devel gtk3-devel
```

Debian / Ubuntu (24.04 or newer):

```bash
sudo apt install clang-20 lld-20 ninja-build cmake python3 git pkg-config \
    dotnet-sdk-8.0 libvulkan-dev \
    libx11-dev libx11-xcb-dev libxcb1-dev libxext-dev libxrandr-dev \
    libxcursor-dev libxi-dev libxfixes-dev libxss-dev libxkbcommon-dev \
    libwayland-dev libgl-dev libegl-dev libgtk-3-dev \
    libasound2-dev libudev-dev libdbus-1-dev
```

If your release has no `clang-20` package, add it from apt.llvm.org:

```bash
wget -qO llvm.sh https://apt.llvm.org/llvm.sh && chmod +x llvm.sh && sudo ./llvm.sh 20
```

On other distributions, install the equivalent packages.

## 2. Get the source

```bash
git clone --recurse-submodules https://github.com/himdo/Fable-2-Recomp.git
cd Fable-2-Recomp
```

## 3. Extract the game files

The game files are not in this repository. You need your own disc image of
Fable II Game of the Year Edition (the supported versions and their hashes are
in `constants/game_versions.json`).

Build the repository's disc extractor (a .NET 8 command-line tool):

```bash
dotnet publish tools/x360extract/X360Extract.csproj -c Release --self-contained false \
    -o out/tooling/x360extract
```

Then extract your ISO into the repository root:

```bash
out/tooling/x360extract/x360extract "/path/to/Fable II.iso" -o .
```

This copies `default.xex`, `data/` (about 6.5 GB), `nxeart` and
`$SystemUpdate/` out of the image. The ISO itself is not changed or needed
afterwards. All of these files are git-ignored. With only a newer .NET
installed, set `DOTNET_ROLL_FORWARD=Major` first. To check that your
`default.xex` is a supported version:

```bash
sha256sum default.xex
```

`default.xex` and `data/` must be real files, not symlinks: the code generator
refuses a `default.xex` that resolves outside the project, and the game's file
system does not follow a `data` symlink. To keep the game files elsewhere,
pass `--game_data_root=<dir>` to the game instead.

## 4. Build

```bash
./build.sh
```

The first build fetches the ReXGlue SDK's dependencies (about 2 GB), builds
the SDK, recompiles the game code from `default.xex` and links the executable
(tens of minutes; over an hour on a Steam Deck). Later builds only rebuild
what changed. If the build runs out of memory, limit the number of parallel
jobs, for example `CMAKE_BUILD_PARALLEL_LEVEL=8 ./build.sh`.

| Command | Effect |
|---|---|
| `./build.sh` | Release build, for any x86-64 CPU from about 2009 on (`--cpu generic`) |
| `./build.sh --cpu deck` | Tuned for the Steam Deck (Zen 2: AVX2, FMA) |
| `./build.sh --cpu native` | Tuned for the CPU you build on |
| `./build.sh --cpu deck package` | Portable package, see [below](#portable-package-build-once-copy-anywhere) |
| `./build.sh --sdk ../rexglue-sdk` | Build the SDK from another source tree instead of the submodule |
| `./build.sh -debug` | Debug build (`-O0`, far too slow to play) |
| `./build.sh -clean` | Delete the build directory for the selected configuration |
| `./build.sh <target>` | Build a different CMake target |

`./build.sh --help` lists the rest (profile-guided optimisation, ThinLTO).
Every `--cpu` level keeps the floating-point results identical to the Windows
build (`-ffp-contract=off`), so physics and animation behave the same.

The result is `out/build/linux-amd64-release/fable_2`, with
`librexruntime.so` and `librexgpu-xenos.so` next to it.

## 5. Play

From the repository root:

```bash
./run.sh
```

`run.sh` starts the game with `--game_data_root` set to the repository root,
which is where step 3 put the game files. Any extra arguments are passed on
to the game. If the game exits with an error, `run.sh` shows the path of the
newest log.

To get a double-clickable launcher, run `./run.sh --make-shortcut` once. It
writes `Fable II.desktop` in the repository root; copy it to your desktop or
to `~/.local/share/applications/`. Run the command again if you move the
repository.

Everything the game writes goes next to the executable, in
`out/build/linux-amd64-release/`:

| Path | Contents |
|---|---|
| `saves/` | Your saves. Back this folder up. |
| `logs/` | One log per run (`fable_2_001.log`, ...). |
| `cache/shaders/` | Shader cache. The first run stutters while it fills. |
| `fable2_config.toml` | Recomp settings (see the README). |
| `fable_2.toml` | Optional ReXGlue settings file (see below). |

Keyboard and mouse work as a gamepad, with mouse look (the pointer is locked
to the window), F4 for the debug menu, F5 for Lua and F9 to pause.

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

The game runs in Desktop Mode as it is. Adding it to Steam as a non-Steam
game is only needed to start it from Gaming Mode.

### With a package built on another PC (recommended)

1. On the PC, build the package (`./build.sh --cpu deck package`, for example
   in WSL2 on Windows) and copy `Fable2-Linux-deck.tar.gz`, `default.xex` and
   the `data` folder to a USB drive or SD card.
2. On the Deck, in Desktop Mode, copy the archive to `~/Games/` and extract it
   there (right-click → Extract → Extract archive here). Extracting on the Deck
   keeps the executable flags, which a copy from a FAT32/exFAT drive would lose.
3. Copy `default.xex` and the whole `data` folder into
   `~/Games/Fable2-Linux-deck/`, next to `Fable2.sh`.
4. **Desktop Mode:** double-click `Fable2.sh` (choose *Execute*), or run it
   from Konsole.
5. **Gaming Mode:** still in Desktop Mode, open Steam, choose *Games → Add a
   Non-Steam Game to My Library*, browse to `Fable2.sh` (set the file filter
   to *All files*) and add it. In the shortcut's *Properties → Compatibility*,
   make sure no compatibility tool (Proton) is forced. It then shows up in
   the library in Gaming Mode.

### Building on the Deck itself

SteamOS's root filesystem is read-only, so build inside a
[Distrobox](https://distrobox.it/) container (preinstalled on SteamOS 3.5 and
later). Everything below runs in Desktop Mode, in Konsole.

1. **Copy the project** (this repository plus the extracted `default.xex` and
   `data/`; the ISO is not needed) to the Deck, for example to
   `~/Games/Fable2/`. A USB drive works (no game file is over 4 GB). If it is
   FAT32 or exFAT, the scripts lose their executable bit, so run
   `chmod +x ~/Games/Fable2/Fable-2-Recomp/*.sh` afterwards.

2. **Create the build container** (once):

   ```bash
   distrobox create --name fable2-build --image ubuntu:24.04
   distrobox enter fable2-build
   ```

   Inside it, install the Debian / Ubuntu packages from
   [step 1](#1-install-the-tools), including Clang 20 from apt.llvm.org.

3. **Build** (inside the container):

   ```bash
   cd ~/Games/Fable2/Fable-2-Recomp
   ./build.sh --cpu deck
   ```

   Keep the Deck plugged in. If it runs out of memory while linking, close
   other apps, or run `CMAKE_BUILD_PARALLEL_LEVEL=4 ./build.sh --cpu deck`.

4. **Run** it from Konsole outside the container with `./run.sh`. If that
   fails with missing libraries or a `GLIBC_2.xx not found` error, run it in
   the container instead: `distrobox enter fable2-build -- ./run.sh`.

5. **Gaming Mode:** add `run.sh` to Steam as a non-Steam game, as in step 5
   above. If you had to run it in the container, point the shortcut at
   `/usr/bin/distrobox-enter` with launch options
   `fable2-build -- /home/deck/Games/Fable2/Fable-2-Recomp/run.sh`.

Suggested settings on the Deck:

- In `fable2_config.toml`, turn on `high_tick_rate` and `higher_hf_tick_rate`
  (together). The game's own logic otherwise ticks at 15 Hz, which feels
  choppy even at a high frame rate.
- For a steadier frame rate in the heaviest areas, set the Quick Access
  Menu's Performance tab to a 40 Hz refresh rate and 40 FPS limit.
- The first time each area loads, new shaders are compiled. That stalls for a
  moment instead of skipping draws, and the pipeline cache next to the
  executable makes later visits smooth.

## Settings

There is no launcher on Linux yet, so settings are set by hand. Each ReXGlue
setting can be given on the command line (`./run.sh --name=value`) or written
as `name = value` in `fable_2.toml` next to the executable (create the file if
it does not exist). The command line takes priority.

**Frame rate.** On Linux the game runs at up to 60 fps by default instead of
its own 30 fps lock: `fps_limit` under `[perf]` in `fable2_config.toml`
(30 = the original lock, 0 = unlimited, -1 = the platform default: 60 on
Linux, 30 on Windows). It sets the same SDK settings the Windows launcher
writes, `frame_limit` and `guest_vblank_pacing`, so setting either of those in
`fable_2.toml` or on the command line overrides it:

```toml
frame_limit = 60
guest_vblank_pacing = false
```

**Button icons:** the button prompts show the Steam Deck's buttons (L1/R1,
L2/R2, View/Menu, ...) in the game's own style. `deck_button_prompts` under
`[input]` in `fable2_config.toml` picks `"dark"` (the default) or `"light"`
buttons, or `"off"` for the game's original Xbox 360 icons.

**Menus:** menus respond faster than in the original game. A press moves the
selection at once, holding a direction starts repeating after 0.18 s instead
of 0.25 s and speeds up to 1.5 times the game's rate, and menu animations run
2.5 times faster. Under `[patches]` in `fable2_config.toml`,
`menu_instant_response` (`false` for the original behaviour),
`menu_repeat_delay` (`0.25` for the original), `menu_scroll_acceleration` and
`menu_animation_speed` (`1` for the original) control this.

**Skip the intro videos:** set `skip_intro_videos = true` under `[patches]`
in `fable2_config.toml`.

**Idle polling:** some game threads wait for each other by busy-polling.
`idle_yield_sleep_us` under `[perf]` in `fable2_config.toml` makes those
polls sleep briefly instead. The default, `-1`, means 100 microseconds on
Linux and off on Windows. On the Steam Deck the CPU power this saves goes to
the GPU. Setting it to `0` (always spin) is not recommended on Linux:
start-up can then stall on a white window.

### Measuring performance

- The game presents exactly one host frame per game frame, so the Steam
  Deck's performance overlay (or MangoHud) shows the game's real frame rate
  and frame times. The F3 debug overlay also shows it, but while any ImGui
  overlay is open the window is repainted at the display refresh rate, so
  external FPS counters then read the refresh rate instead.
- For measuring, `log_guest_fps = true` in `fable_2.toml` adds a line every
  5 seconds to the log (`logs/` next to the executable), like
  `[guest-fps] 47.9 fps over 5.0 s, slowest frame 61.2 ms, 3 of 240 frames
  over 33 ms; host presented 47.9/s`. The "over 33 ms" count is the number of
  visible stutters. With it on, the same summaries with CPU time per thread,
  CPU/GPU clocks and per-frame work counts also go to `perf_log.txt` next to
  the executable (`log_perf_file`), and `log_perf_profile = true` adds the
  busiest functions every 30 s. All three are off by default.

## Known limitations

- **No launcher.** Extraction and settings are done by hand (steps 3 and
  "Settings" above). The Windows launcher's settings map to `fable_2.toml` and
  `fable2_config.toml`.
- **The window opens slowly on Wayland.** Start-up shows a white window for
  roughly 5 to 25 seconds before the first video, and on KDE Plasma the
  window can grow slowly to full screen while the game loads. If it bothers
  you, `SDL_VIDEODRIVER=x11 ./run.sh` runs through XWayland, where the window
  opens at full size straight away.
- **Do not force strict vsync** with `vulkan_allow_present_mode_immediate`,
  `vulkan_allow_present_mode_mailbox` and
  `vulkan_allow_present_mode_fifo_relaxed` all set to `false`. On NVIDIA with
  Wayland that leaves a white window and the game never gets past the start.
  The default present mode works.
- **Occasional rendering glitches:** a texture or an object can go wrong for
  a single frame (the same as issue #13 on Windows), and the golden quest
  trail can flicker at some camera angles.
- **Higher internal resolution** (`resolution_scale`) does not sharpen the
  presented image yet, on Vulkan or D3D12.
- **Some developer diagnostics are Windows-only:** the crash logger, the
  render-stall thread dumper and the heap scan.
- The `.cmd` helper scripts copied next to the executable are for Windows and
  can be ignored.

If the hero or the dog render black (an older `fable2_config.toml` can still
have the earlier workaround in it), set these under `[patches]`:

```toml
realtime_texture_morphing = true
hero_dog_texture_readback = false
vulkan_texture_morph_fallback = false
```

## How the Linux build differs from Windows

- The ReXGlue SDK is built from the `thirdparty/rexglue-sdk` submodule as
  part of the game build, as pinned; nothing is patched on top of it.
- The renderer is Vulkan only (no D3D12).
- The stable-hash PE cleaning, PDB staging and version-resource steps in
  `CMakeLists.txt` are Windows-only and are skipped.

The Windows build tunes the D3D12 renderer for Fable II. Those Fable II fixes
live in shared SDK code and are used by the Vulkan renderer too:

| Fix | Setting (`fable2_config.toml`) | What it does |
|---|---|---|
| CPU terrain tessellation | `[patches] terrain_tess_cpu` | Stops a GPU wait + readback every frame |
| CPU skinning | `[patches] skinning_cpu` | Same, for the skinned memexport draws |
| Depth-clear transfer skip | `[patches] faster_render_targets` | Skips copying depth that a clear overwrites anyway |
| Wait for async pipelines | SDK setting `async_pipeline_wait` | Draws wait for their pipeline instead of drawing with an invisible placeholder (missing objects, stale textures) |
| Dog fur depth bias | `[patches] dog_fur_depth_bias` | Stops the dog's fur Z-fighting |
| Relative constant clamp | SDK setting `spirv_clamp_relative_float_constants` | Keeps the trail effect from reading out of bounds (an AMD GPU hang on D3D12) |
| Realtime skin morphing | `[patches] realtime_texture_morphing` | Builds morphed skin textures on the GPU. On by default on Vulkan too. |

On top of that, Linux leaves the frame-start GPU sync (`gpu_frame_sync`) off
by default, so the CPU does not wait for the GPU at every frame start.

## Building without root access

If you cannot install packages, `build.sh` also accepts tools unpacked into
`out/tooling/`:

- Clang 20 under `out/tooling/llvm20/usr/` (for example the contents of the
  Debian `clang-20`, `lld-20` and `llvm-20` packages, unpacked with `dpkg -x`)
- a `ninja` binary in `out/tooling/bin/`
- extra `-dev` packages under `out/tooling/sysdeps/usr/`, with the `prefix=`
  line of their `.pc` files edited to point at that directory

A toolchain unpacked elsewhere also works once its `bin/` is on `PATH` and its
`pkgconfig/` directory is on `PKG_CONFIG_PATH`. If the XScrnSaver headers are
missing, the build carries on without the X11 screensaver extension.
