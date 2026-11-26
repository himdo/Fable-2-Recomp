# Console screenshot: technical notes

**Status:** no commit yet · **Commits:** none · **Last checked against:** c2a9d12

Labels: **Verified** means read in the repo or measured. **Inference** means reasoned, not tested.

## Goal

A scenario played through the f2 console (`tools/run_scenario.py` and `docs/feature/scenario_runner/`, on `feature/scenario-runner`, not merged yet) can read values and check memory, but it can't show what was on screen. An AI assistant running on the game's machine can open PNG files, so a screenshot at any step of a scenario lets it see the result of what it did, not just read numbers.

Before this change, screenshots came only from two places (Verified):

- photo mode, through its screenshot key (`fable2::photomode::take_screenshot` in `src/core/fable2_photo_mode.h`);
- the remote-control server's `screenshot` command, which exists only in Debug builds (`src/input/remote_control_server.h`).

The console had no way to take one.

## Design

A new host command, `shot [file]`, in `run_host_command` (`src/core/fable2_console.h`).

- **The capture:** it uses the same `fable2::framecapture::CaptureGuestOutputToPng` as photo mode and the server (`src/diagnostics/fable2_frame_capture.h`). That code reads the game's own rendered frame back from the GPU at the game's resolution, so covering windows never show up, and it works with D3D12 and Vulkan (Verified in the header's comments). The renderer comes from `fable2::photomode::presenter_provider()` (`src/input/photo_mode_state.h`), which `src/core/fable_2_app.h` sets at start-up for every build type. So `shot` works in Release builds and without photo mode on.
- **Where the file goes:**
  - With no argument, it goes to `<game root>\console\shots\shot_<YYYYMMDD>_<HHMMSS>.png`, with `_2`, `_3` and so on added when that name is taken. This matches photo mode's naming.
  - A relative `<file>` is relative to `console\`, like `exec`. The rest of the line is the name, so it may contain spaces.
  - The folder is created if needed.
- **The reply:**
  - `[shot] saved <path> <w>x<h>` when it worked. This is the line `tools/run_scenario.py` lists under "Screenshots:", so the two must keep the same form.
  - An extra `[shot] the frame looks black (a loading screen or a fade?)` when the capture's brightness check says so. It isn't an error.
  - `[shot] ERR capture failed: <reason>`, or `[shot] ERR no renderer to capture from yet` before the window exists.
- **Threads:** the capture waits for a GPU readback, so, as in photo mode, it runs on a thread of its own, not on the game thread that runs the console queue.
  - The command bumps `shot_seq` and sets `shot_wait`.
  - The thread writes its reply through `out()`, which has its own lock, and then stores its number in the atomic `shot_done`.
  - The queue holds while `shot_wait` is set, in the same way it waits for a Lua reply (`lua_seq`/`lua_done`). So the next command, and a scenario's end marker, come after the reply.
  - After 15 s the queue gives up with `[shot] ERR the capture didn't finish within 15 s`. A late thread from an earlier `shot` can't release a newer one, because the numbers differ.
- **Guest memory:** the thread touches none. The command does a little file-system work on the game thread (`exists`, `create_directories`), as `exec` and `out.txt` already do.
- **Linux:** the one platform-specific call is `localtime_s` on Windows; other platforms use `localtime_r`, as photo mode does.
- **Not while paused:** `shot` isn't in `runs_while_paused`, so while `pause` holds the game it waits in the queue like most commands. **Inference:** a capture of a held frame might work, but that's untested, so it's left out.

## What changed

- `src/core/fable2_console.h`:
  - the `shot` command;
  - three state fields and a deadline (`shot_seq`, `shot_done`, `shot_wait`, `shot_deadline`);
  - the queue's wait for it, and `shot_wait` in the idle check;
  - `shot` added to `help`, `hosthelp` and the command list at the top of the file;
  - the include of `fable2_frame_capture.h`, which `main.cpp`, the only file that includes the console header, already pulled in through photo mode.
- `docs/feature/console_commands/technical.md`: a `shot` row in the photo mode and research table.

No game functions are called, hooked or overridden, so none needed names.

## Verification

- `python -m pytest -q` on macOS: 494 passed, 15 xfailed, before and after. This includes `tests/core/test_world_thread_runscript.py`, which parses this file's C++ and checks that only the world-update service runs game Lua. The new thread only writes replies.
- `python tools/generate_game_versions.py --check`: up to date.
- **Not built.** This was written on a Mac, where the SDK's bundled libraries (spdlog and others) aren't checked out, so even a syntax check failed on a missing header before reaching this code. It needs a Release build on Windows.

## Not tested

- Everything in game:
  - that `shot` saves a PNG and replies as described;
  - how long a capture takes;
  - the black-frame line on a loading screen;
  - a path with spaces.
- **Whether an AI assistant can open the PNG.** An uncompressed 1920×1080 PNG is about 6 MB, because the capture writes stored, uncompressed deflate blocks. If that is too big to read, `shot` needs a smaller size option.
- **The Linux build** (`localtime_r` branch).
- **The native and launcher tests,** which need Windows or don't cover this.

## Follow-ups

- **On Windows:** build Release, run `shot` in the `~` console and from a scenario, check the reply and the file, and try opening the PNG in Claude Code.
- **A size option,** for example `shot half`, if the full-size PNG is too large to read.
