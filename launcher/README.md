# Windows launcher

A .NET 8 WPF settings launcher for verified GOTY USA/Europe and German GOTY
dumps. It validates the original XEX and content markers, then checks the
native build descriptor. Unknown revisions and known mixed retail/TU1 content
are rejected; original game files are never modified.

## Extracting from an ISO

"Extract from ISO" opens a file picker for a raw Xbox 360 disc image (`.iso`)
and extracts straight into the folder the launcher is running from (next to
`fable_2.exe`) — there is no destination picker. The ISO content is written in
two phases:

1. **`default.xex` only** — extracted and SHA-256-hashed first.
2. **The remaining disc** (`data`, `nxeart`, `$SystemUpdate`, …) — written if
   the hash matches a known-good build in the version catalogue
   (`GameCompatibilityInspector.CheckHash`, same catalogue the launcher uses
   to validate game folders). If the hash does **not** match, extraction pauses
   on the progress screen with the reason and a red **Extract Anyways** button:
   click it to keep the XEX and write the rest of the disc, or press **Stop**
   to bail out (which deletes the ~21 MB XEX so nothing partial is left behind).

On success the launcher's folder is automatically set as the selected game
folder. The
GDFX/ISO 9660 readers come from the shared `X360Extract` library
(`tools/x360extract/lib`, referenced as a project dependency), so the output is
byte-for-byte identical to the standalone `x360extract` tool; STFS containers
are copied raw.

Settings: 720p/1080p/1440p/4K output, 1x–4x internal render scale, anisotropic
filtering (game default through 16x), none/FXAA/FXAA Extreme, VSync,
windowed/borderless/exclusive fullscreen and 30/60/120/144/165/240/unlimited FPS. Output size
is separate from the original 720p guest mode. The FPS options need the
matched source-built Release runtime described in
[the runtime guide](../docs/RUNTIME_FIXES.md). Higher rates are not a promise
of correct timing in every scene.

The **Advanced Options** button in the top bar opens a second page for the
recomp's own settings (stored in `fable2_config.toml` next to `fable_2.exe`,
separate from the engine's `fable_2.toml`):

- **Keybinds** — the keyboard→gamepad map as a list of editable rows (host key
  → guest input, with add/remove). Mouse look and mouse sensitivity are also
  here.
- **Patches** — a checkbox per recomp-level patch (unlock website/CE, skip
  intro videos, disable motion blur, realtime texture morphing, hero/dog
  texture readback, dog-fur depth bias).
- **Performance** — the hot-function yield-batching factor.

The Save button writes only the managed keys back to `fable2_config.toml`
non-destructively: it updates keys in place (keeping indentation, spacing and
inline comments), appends any missing keys or sections, leaves every other key
and section untouched, and makes a one-time `.launcher-backup` before the first
write. Keybind and patch changes take effect the next time the game launches.

Dropdowns use dark text on a light background, including the selected item.
The launcher and game use a project-owned book/tree icon documented in
[asset provenance](../assets/README.md).

Build from the repository root:

```cmd
build.cmd launcher                 self-contained (default; ships with .NET)
build.cmd launcher-dev             framework-dependent (needs .NET installed)
dotnet run --project tests/Fable2.Launcher.ConfigTests -c Release
```

The tests also cover the extraction hash gate (every known-good hash is
accepted, every unsupported/unknown hash is rejected before extraction).

## Shipping without a .NET install

The default build is a **self-contained single-file publish**: the .NET 8
Desktop Runtime is bundled inside `Fable2Launcher.exe` (~160 MB), so users
need no .NET install and no separate runtime installer is shipped. The same
applies to `x360extract.exe` (see `tools/x360extract`, default build is
self-contained too). `build.cmd launcher-dev` produces the small
framework-dependent exe for fast local iteration; it requires the .NET 8
Desktop Runtime on the target machine.

Output is `out/tests/launcher-build/Fable2Launcher.exe`. Put it beside
`fable_2.exe`, its matched DLLs, `fable2_build.json` and `app-icon.png`.

The launcher defaults to its own folder on first launch and remembers the
selected location in `launcher-game-path.txt` beside it. No developer path,
global fallback or automatic dump selection is embedded. The directory must
be writable to persist settings.

`launcher-settings.toml` stores only the managed preferences. The launcher
reads existing `fable_2.toml` first for legacy configurations, then overlays
those preferences so runtime rewrites cannot reset output resolution or render
scale. Save/start writes both files. Unknown engine settings are preserved,
and the first engine-config write creates a one-time backup. Saves, caches and
logs remain beside the native EXE, not in the original dump.

F3 in the game shows the guest-swap FPS counter in Release.
`StartWithoutAudio.cmd`, staged with the native EXE, exercises the clocked
silent fallback without disabling any Windows device. Exit existing game and
launcher processes before running that test.

### Keyboard and mouse prompt images

Open **Advanced Options**, check **Show keyboard and mouse prompt images**,
save, and launch the game from the launcher. Shared controller prompts use
keycap images generated from the keyboard and mouse bindings effective at
startup. If an action has several bindings, the prompt prefers a regular key
over an alternate mouse button or modifier. Unbound controls retain their
original artwork. The main-menu selection badge uses separate artwork and
still shows the controller's A button.

The game loads these textures at startup. Relaunch after changing bindings.
To use the original controller prompts, clear the checkbox and launch again.
Controller input remains available in both modes.

The option requires a writable extracted game folder on NTFS. It creates a
read-only game-data view with two generated GUI banks and hard links to the
other files. Your disc image and extracted banks are preserved. The launcher
can reuse the view under `cache/keyboard-prompts` in the game-data folder.
This cache can be removed while the game and launcher are closed. Saves
continue to use the existing save folder beside the executable.


Launcher overview supplied by the tester (before the high-refresh presets were added):

![Launcher overview](../docs/screenshots/launcher.png)
