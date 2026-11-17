# In-game PC Settings

The port's own settings (`fable2_config.toml`) can be changed inside the game.
A **PC Settings** item sits in the main menu (above Quit) and at the bottom of
the pause (Start) menu. It opens a page drawn like the game's own menus: the
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
until it is released, so it does not also close the pause menu.

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

The items are added with the game's own menu code (`src/menu/main_menu.cpp`,
which already added Quit to the main menu):

| Address | Name | Role |
|---|---|---|
| `0x826CA6E0` | `FrontMenu_Populate` | builds the main menu; the recomp's items are spawned after it returns |
| `0x826CB808` | `ExpandableMenu_SpawnButton` | adds one main-menu item (event type 2, element `*(menu + 20)`) |
| `0x826C81A0` | `FrontMenu_HandleEvent` | main-menu handler: a chosen item's event points at its name |
| `0x82842860` | `PauseMenu_Populate` | builds the pause menu (World Map ... Options); PC Settings is spawned after it returns |
| `0x824083C0` | `PauseMenu_SpawnButton` | adds one pause-menu item, same arguments, event type 300, element `*(*(*(0x834968A4) + 0x38) + 0xC)` |
| `0x8240EAA0` | `PauseMenu_HandleEvent` | pause-menu handler: types 2 and 300 are a chosen item |
| `0x82412378` | `PauseMenu_OpenItemScreen` | opens the screen for a chosen item (named for reference, not hooked) |
| `0x822B2D60` | `InputGetPadState_822B2D60` | every guest pad read; captured for the page and blanked while it is open |

Choosing the item only raises a flag; the page is drawn on the UI thread
(`src/core/port_settings_dialog.h`) and reads the controller from the pad
reads (`src/core/port_settings_pad.h`). No game script is registered, so
nothing of the page ends up in a save.

The log shows `[pc-settings] opened`, each change
(`[pc-settings] input.mouse_look_scale = 272`) and what was saved.
