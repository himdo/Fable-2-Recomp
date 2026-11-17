# Advanced Options (in game)

The port's own settings (`fable2_config.toml`) can be changed inside the game,
on the **Advanced Options** page. It opens from:

- the main menu: **Advanced Options** (above Quit);
- the pause (Start) menu: **Options > Advanced**, after the game's own Game,
  Video, Controls and Audio.

The page is drawn like the game's own menus: the
game's GUI typeface, its Options icon and controller button art, cream text
with a gold highlight. No ImGui windows.

## Using it

The page has five tabs: **Display**, **Frame Rate**, **Mouse**, **Fixes** and
**Photo Mode**. The line under the list describes the selected setting and
says when a change takes effect.

| | Controller | Keyboard | Mouse |
|---|---|---|---|
| Move | D-pad / left stick | arrow keys, W / S | point at a line |
| Change | A, D-pad left / right | Enter, Space, E, arrow keys left / right | click (the left half of a choice goes back), drag a bar, wheel |
| Default value | Y | Delete | |
| Tab | LB / RB | Page Up / Page Down, Z / Tab, 1-5 | click a tab |
| Back (saves) | B or Start | Escape, Backspace, B | right click |

The keys mapped onto the controller (`[input] keyboard_gamepad_map`) work as
they do in the game's menus. The button prompts follow the last device used.

While the page is open the game sees no controller input, so the menu under
it does not move. The button that closed the page is held back from the game
until it is released, so it does not also close the Options menu.

## When a change takes effect

- **At once**: the value goes to the cvar or the setting the game reads every
  frame (motion blur, GPU frame sync, faster render targets, GPU memory
  refresh, thread yielding, everything on the Mouse tab, photo mode on/off,
  its controller shortcut and camera speed).
- **When a save is loaded**: website and Collector's Edition items.
- **After a restart**: marked *Restart* next to the name. Once changed it
  reads *Restart to apply*, and the page lists those settings above the
  prompts.

If `fable_2.toml`, a `REX_*` environment variable or the command line sets
the same cvar, that source still wins at the next start; the description says
so.

## Saving

Leaving the page writes the changed keys to `fable2_config.toml` next to the
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
                .Restart());
```

- `Toggle` (bool), `Choice` (a list of values) or `Slider` (int or float
  range with a step and a format).
- The default is used when the key is missing from the file; keep it equal
  to the template in `src/core/fable2_config.cpp`.
- `.Restart()`, `.NextLoad(fn)` or `.Live(fn)` / `.LiveCvars({...})`: `Field(&Values::x)`
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

**Input.** `InputGetPadState_822B2D60` (every guest pad read) is captured for
the page and blanked while it is open (`src/core/port_settings_pad.h`).
Choosing an item only raises a flag; the page is drawn on the UI thread
(`src/core/port_settings_dialog.h`).

The log shows `[recomp-settings] row added to OptionsMenu`,
`[advanced-options] opened`, each change
(`[advanced-options] input.mouse_look_scale = 272`) and what was saved.
