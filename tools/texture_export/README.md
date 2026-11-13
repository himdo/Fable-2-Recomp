# Texture Export Tool

Exports original and edited PNG images for every GUI texture modified by the
keycap swap feature (`GenerateSidecar` in `src/gui/keyboard_prompts.h`).

## What It Does

Reads the game's texture and header banks from `data/art/gui/`, decodes each
modified texture, applies the same keycap replacements the game would use, and
writes side-by-side PNG comparisons to `tools/texture_export/out/`.

Textures exported:

| Texture | Size | What changes |
|---|---|---|
| 45 controller icons | 64×64 / 32×32 | Full keycap replacement |
| `abyx.tex` | 256×256 | A/B/Y/X face buttons (top + second row) |
| `frames_elements.tex` | 512×512 | B keycap at (46,193), A keycap at (13,372) |

Output naming: `<name>_original.png` and `<name>_edited.png` per texture.
Controller icons use `<subdir>_<leaf>_original.png` to disambiguate the 64×64
(`Controller_`) and 32×32 (`24x24_`) tiers.

## Prerequisites

1. **Build the main project first** (the tool links against the prebuilt
   `z-ng.lib` from the zlib-ng CMake dependency):

   ```
   build.cmd -release fable_2
   ```

2. **Game data** must be present at `data/art/gui/gui_textures.bnk` and
   `data/art/gui/gui_texture_headers.bnk` (relative to the repo root).

3. **clang++** on PATH or at `C:\clang+llvm-23.1.2-x86_64-pc-windows-msvc\bin\`.

## Build

```
tools\texture_export\build.cmd
```

Produces `tools\texture_export\texture_export.exe`.

## Run

Run from the repo root (the tool reads `data/art/gui/` using a relative path):

```
cd <repo root>
tools\texture_export\texture_export.exe
```

Output goes to `tools/texture_export/out/` (created automatically).

## Keyboard Mapping

The tool uses the default keyboard-to-gamepad mapping:

| Game button | Key |
|---|---|
| A | E |
| B | B |
| X | LMB |
| Y | RMB |
| LB | Z |
| RB | Tab |
| LT | Q |
| RT | R |
| Start | Esc |
| Back | M |
| Stick up/down/left/right | W / S / A / D |
| D-pad up/down/left/right | Up / Down / Left / Right |

To change the mapping, edit the `addLabel` calls in `main.cpp`.
