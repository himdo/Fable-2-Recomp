# Advanced Options (in game)

The port's own settings (`fable2_config.toml`) can be changed inside the game,
on the **Advanced Options** page. It opens from:

- the main menu: **Advanced Options** (above Quit);
- the pause (Start) menu: **Options > Advanced**, after the game's own Game,
  Video, Controls and Audio.

It is a page of the book, as the game's own options pages (Game, Video,
Audio) are: the game's menu stays on the left page (stepping back a little)
and the settings are written on the right-hand page, on the paper the game's
own pages use (`frames_page_texture.tex`), in brown ink with a deep red
highlight, in the game's GUI typeface, with the game's Options icon and
controller button art. The book's leather cover
(`frame_new_right_top/bottom.tex`) is drawn again over the paper's edge, so
the cover stays on top as it does on the game's pages. Like the game's
pages, the paper has a copper rim on both sides, from the page-edge strip in
`frames_elements.tex`: along the cover, and on the outer edge, which curves
in towards the middle of the page (drawn in thin slices so the paper follows
the curve). The text keeps inside
the page: a name too long for its line drops to the small font (then the
value does, then the name is shortened), and the description stops above the
button prompts. One tab shows at a time (LB / RB or Page Up / Page
Down, or the chevrons beside its name). Never ImGui windows.

## Using it

The page has five tabs: **Display**, **Frame Rate**, **Mouse**, **Fixes** and
**Photo Mode**. The text under the list
describes the selected setting and says when a change takes effect.

**Display** starts with the window fitting the game already does
(`src/core/fable2_fit_window.h`), now saved in `[display]`:

| Setting | Key | Values |
|---|---|---|
| Fit to Window | `fit_to_window` | On: render at the window's shape and fill it. Off: the original 16:9 picture with black bars |
| Aspect Ratio | `aspect_ratio` | Match Window (`"auto"`), 4:3, 16:10, 16:9, 21:9, 32:9; the file also takes any `"w:h"` or number between 4:3 and 4:1 |
| HUD at the Edges | `hud_to_edges` | the HUD at the edges of a wide picture, from the next screen that opens |
| 16:9 Loading Screens | `native_16x9_screens` | loading screens and the saving banner kept 16:9 (recommended: On, shown as *On (Recommended)*) |
| Draw Distance | `[patches] view_distance_multiplier` | 1.00x to 10.00x: how far objects, buildings, villagers and creatures are drawn before they pop out |
| Detail Distance | `[patches] lod_distance_multiplier` | 1.00x to 10.00x: how far out models keep their full-detail version |

The window settings apply at once. At startup `fit_to_window`, `hud_to_edges` and
`native_16x9_screens` seed the cvars `fit_game_to_window`,
`fit_window_hud_to_edges` and `fit_window_native_16x9` (fable_2.toml or the
command line still win), and `aspect_ratio` sets the forced aspect the
console's `ws aspect` sets.

| | Controller | Keyboard | Mouse |
|---|---|---|---|
| Move | D-pad / left stick | arrow keys, W / S | point at a line |
| Change | D-pad / left stick left / right | arrow keys left / right | click (the left half of a choice goes back), drag a bar, wheel |
| Default value | Y | Delete | |
| Tab | LB / RB | Page Up / Page Down, Z / Tab, 1-5 | click a chevron |
| Accept (saves) | A | Enter, Space, E | |
| Cancel (puts back) | B or Start | Escape, Backspace, B | right click |

As on the game's own options pages, **Accept** keeps the changes and saves
them, and **Cancel** puts every value changed on this visit back (live too)
and saves nothing.

The keys mapped onto the controller (`[input] keyboard_gamepad_map`) work as
they do in the game's menus. The button prompts follow the last device used.

While the page is open the game sees no controller input, so the menu under
it does not move. The button that closed the page is held back from the game
until it is released, so it does not also close the Options menu.

## When a change takes effect

Every setting applies while the game runs; none needs a restart.

- **At once**: the value goes to the cvar, the guest memory or the setting
  the game reads every frame: window fit and aspect ratio, motion blur, faster
  render targets, the dog fur fix and the skin texture readback (both are
  GPU cvars read per draw), High Tick Rate and Higher Physics Rate (the tick
  doubles are rewritten in guest memory and their hooks follow the setting),
  GPU frame sync, terrain detail and skinning on the CPU (with the fast
  memexport readback kept in step), GPU memory refresh, thread yielding,
  everything on the Mouse tab, and all of photo mode (the keys switch through
  atomics the input poll reads).
- **Real-Time Skin Textures**: used from the next skin texture the game builds
  (changing clothes, loading a save).
- **From the next area or save load**: Draw Distance and Detail Distance
  (each object takes them as it loads; the draw multipliers are rewritten in
  guest memory at once, the LOD hook reads the setting each time).
- **When a save is loaded**: website and Collector's Edition items.
- **When the game starts**: Skip Intro Videos (the videos only play then).

A setting can still be declared `.Restart()` in the schema; the page then
marks it and lists it under the description once changed.

If `fable_2.toml`, a `REX_*` environment variable or the command line sets
the same cvar, that source still wins at the next start; the description says
so.

## Saving

Accepting writes the changed keys to `fable2_config.toml` next to the
exe and shows *Settings Saved*. Only the value of each changed line is
rewritten: indentation, spacing, inline comments, the comment blocks, unknown
keys and sections, and the line endings stay as they were. A key missing from
the file goes at the end of its section; a missing section at the end of the
file. The file is written to a temporary file first and then swapped in.

These are the same rules as the launcher's `Fable2ConfigFile.Write`, so the
game and the launcher can both edit the file. The launcher reads everything
the page writes, and saving the same values from the launcher changes no
line. `tests/native/test_config_file_edit.cpp` covers the rules.

## Adding a setting

Add one entry to `Settings()` in `src/core/port_settings_schema.h`, in the
tab it belongs to:

```cpp
s.push_back(Choice(kFrameRate, "perf", "frame_rate", "Frame Rate",
                   "The highest frame rate the game runs at.",
                   Int(30), {{Int(30), "30 FPS"}, {Int(60), "60 FPS"}, {Int(120), "120 FPS"}})
                .Live([](const Value& v) { /* apply it */ }));
```

- `Toggle` (bool), `Choice` (a list of values) or `Slider` (int or float
  range with a step and a format).
- The default is used when the key is missing from the file; keep it equal
  to the template in `src/core/fable2_config.cpp`.
- `.Live(fn)` / `.LiveCvars({...})`, `.NextLoad(fn)`, `.AtStart(fn)` or
  `.Restart()`: `Field(&Values::x)`
  writes a bool or number field of `fable2::config::Values`; `LiveCvars` sets
  cvars (and can update a field too). String fields are never written while
  the game runs, since other threads read them.

The page reads values straight from the file, so a key does not need a
`Values` field to be listed.

## How it hooks the game

**Main menu.** The item is added with the game's own menu code
(`src/menu/main_menu.cpp`, which already added Quit):

| Address | Name | Role |
|---|---|---|
| `0x826CA6E0` | `FrontMenu_Populate` | builds the main menu; the recomp's items are spawned after it returns |
| `0x826CB808` | `ExpandableMenu_SpawnButton` | adds one main-menu item (event type 2, element `*(menu + 20)`) |
| `0x826C81A0` | `FrontMenu_HandleEvent` | main-menu handler: a chosen item's event points at its name |

**Pause menu, Options.** Its rows are not added by game code. The Options
screen's GameFace Lua fills its menu from a table: `g_Menu:SetPopulationTable()`
(`ExpandableMenuPopulationScript.lua`) picks the rows for the menu's
`MenuPopulationScript` (`OptionsMenu`: Game, Video, Controls, Audio). A short
Lua script (`src/core/port_settings_lua.h`) runs in each GameFace screen as it
loads, through the same call `src/core/fable2_fit_window_ui.h` uses for its
layout script, and each frame from `GUI.onUpdate`. Once per menu it wraps
`SetPopulationTable` to append the Advanced row, and refills an Options menu
that filled itself before the wrapper was there (the menu fills itself in its
`onInitialize`, where `g_Menu` is also set). It is GameFace's Lua, a separate
VM from the game's scripts, so nothing is registered with the game's script
manager and nothing ends up in a save.

A on an Options row is handled in C++:

| Address | Name | Role |
|---|---|---|
| `0x82582E50` | `OptionsMenu_HandleInput` | Options screen input; A (`0x24`) with no options page open reads the highlighted row and opens page *event type - 70*. For the Advanced row the Advanced Options page opens instead and the game's handler is skipped |
| `0x8259DB00` | `ExpandableMenu_GetHighlightedItem` | asks the Lua menu for the highlighted row (`GetHighlightedMenuItemData`): +0 event type, +4 ID, +8 name, +12 text tag |

The row's event type is the Game row's (70), so if the hook ever missed, A
would open the game's own Game page, nothing worse.

**View distance.** The game has a draw distance multiplier per object class
(static, animated, villager, creature; floats at `0x83319368`..`0x83319374`,
1.0 in the image) that only a debug Lua setter changes;
`GraphicComponent_InitDrawDistances_825470E0` multiplies each object's
MaxDrawDistance by one as it loads. The "View Distance" data patch
(`src/core/fable2_patches.cpp`) sets them at startup and
`fable2::patches::SetDrawDistance` while the game runs. The mid-asm hook
`fable2_hook_lod_distance` (before `stfs f1, 52(r31)` at `0x825471C4`)
scales each model's LodDistance. The launcher has sliders for both.

**Input.** `InputGetPadState_822B2D60` (every guest pad read) is captured for
the page and blanked while it is open (`src/core/port_settings_pad.h`).
Choosing an item only raises a flag; the page is drawn on the UI thread
(`src/core/port_settings_dialog.h`).

The log shows `[recomp-settings] row added to OptionsMenu`,
`[advanced-options] opened`, each change
(`[advanced-options] input.mouse_look_scale = 272`) and what was saved.
