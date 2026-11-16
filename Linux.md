# Building and playing on Linux

The game builds as a native x86-64 Linux executable and renders with Vulkan.
It is not Proton or Wine. Linux support is new: there is no launcher yet, so
setup is done from a terminal, and you need a controller (see
[Known limitations](#known-limitations)).

Tested on Nobara 44 (KDE Plasma, Wayland) with a Ryzen 5 7600 and an RTX 3060
Ti on the NVIDIA proprietary driver: intro, new game, the opening chapters,
combat and saving, at the game's 30 fps lock and at 60 fps. An earlier version
was also run on Linux Mint 22.3 with an AMD GPU (Mesa RADV).

## 1. Install the tools

You need Clang 20 (Clang 22 is untested), lld, CMake 3.25 or newer, Ninja,
Python 3, Git, the .NET 8 SDK (for the disc extractor), a GPU with a working
Vulkan driver, and the development headers SDL3 is built against.

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
afterwards. All of these files are git-ignored. To check that your
`default.xex` is a supported version:

```bash
sha256sum default.xex
```

## 4. Build

```bash
./build.sh
```

The first build fetches the ReXGlue SDK's dependencies, builds the SDK,
recompiles the game code from `default.xex` and links the executable. Later
builds only rebuild what changed. If the build runs out of memory, limit the
number of parallel jobs, for example `CMAKE_BUILD_PARALLEL_LEVEL=8 ./build.sh`.

| Command | Effect |
|---|---|
| `./build.sh` | Release build |
| `./build.sh -debug` | Debug build (not yet tested on Linux) |
| `./build.sh -clean` | Delete the build directory for the selected configuration |
| `./build.sh <target>` | Build a different CMake target |

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

## Settings

There is no launcher on Linux yet, so settings are set by hand. Each setting
can be given on the command line (`./run.sh --name=value`) or written as
`name = value` in `out/build/linux-amd64-release/fable_2.toml` (create the
file if it does not exist). The command line takes priority.

**60 fps.** The game locks itself to 30 fps by default. These are the same
values the Windows launcher writes for a 60 fps limit:

```toml
frame_limit = 60
guest_vblank_pacing = false
```

Other limits follow the same pattern (for example `frame_limit = 120`, or `0`
for unlimited); only 30 and 60 fps have been tested on Linux.

**Skip the intro videos:** set `skip_intro_videos = true` under `[patches]`
in `fable2_config.toml`.

**Idle polling:** some game threads wait for each other by busy-polling.
`idle_yield_sleep_us` under `[perf]` in `fable2_config.toml` makes those
polls sleep briefly instead. The default, `-1`, means 100 microseconds on
Linux and off on Windows. Setting it to `0` (always spin) is not recommended
on Linux: start-up can then stall on a white window.

## Known limitations

- **You need a controller.** Keyboard and mouse controls, and the keyboard
  shortcuts (photo mode keys, the F5 Lua runner, the F9 console), are
  Windows-only for now.
- **No launcher.** Extraction and settings are done by hand (steps 3 and
  "Settings" above).
- **Some D3D12 renderer fixes are not on Vulkan yet:** skipping depth
  transfers (`skip_depth_clear_transfers`), the frame-start GPU sync
  (`gpu_sync_at_frame_start`) and the dog-fur depth bias. The log reports
  these as missing; the game runs without them.
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
- **Occasional rendering glitches:** a texture can go wrong for a single
  frame (the same as issue #13 on Windows), and the golden quest trail can
  disappear at some camera angles.
- **Some developer diagnostics are Windows-only:** the crash logger and the
  render-stall thread dumper.
- The `.cmd` helper scripts copied next to the executable are for Windows and
  can be ignored.

If the hero or the dog render black, set `realtime_texture_morphing = false`
and `hero_dog_texture_readback = true` under `[patches]` in
`fable2_config.toml`. This was needed on earlier versions; with the current
defaults the hero renders correctly in testing.

## How the Linux build differs from Windows

- The ReXGlue SDK is built from the `thirdparty/rexglue-sdk` submodule as
  part of the game build, as pinned; nothing is patched on top of it.
- The renderer is Vulkan only (no D3D12).
- The stable-hash PE cleaning, PDB staging and version-resource steps in
  `CMakeLists.txt` are Windows-only and are skipped.

## Building without root access

If you cannot install packages, `build.sh` also accepts tools unpacked into
`out/tooling/`:

- Clang 20 under `out/tooling/llvm20/usr/` (for example the contents of the
  Debian `clang-20`, `lld-20` and `llvm-20` packages, unpacked with `dpkg -x`)
- a `ninja` binary in `out/tooling/bin/`
- extra `-dev` packages under `out/tooling/sysdeps/usr/`, with the `prefix=`
  line of their `.pc` files edited to point at that directory

If the XScrnSaver headers are missing, the build carries on without the X11
screensaver extension.
