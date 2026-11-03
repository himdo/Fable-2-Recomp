# Fit game to window (widescreen)

`src/core/fable2_fit_window.h` renders the game at the window's aspect ratio
(4:3 up to 4:1) and stretches the frame to fill the window.
`src/core/fable2_fit_window_ui.h` moves the HUD and menus to match. Turn it
off with `fit_game_to_window = false` in `fable_2.toml`.

Everything follows the aspect live: resizing the window, `f2 ws aspect` or
`f2 ws on|off` in the developer console updates open screens without a
restart. At 16:9 every element is back at its authored place. See
`docs/console/CONSOLE-COMMANDS.md` (Widescreen) for the `ws` commands.

## What is handled

| Screen | Behaviour |
|---|---|
| 3D view | Hor+: wider window, wider view. |
| HUD | Edge elements move to the window edges. |
| Book screens (pause menu, Items, Weapons, shops) | Covers at the window edges, the menu column and header beside the left cover, the page whole against the right cover. |
| Main menu | Covers at the window edges (vanilla visible width), paper under the right cover, waiting character cards off-screen; on New Game the cards slide in to their vanilla spots. |
| Loading screens, saving / verifying banners | The game's own 2D pieces stay 16:9 in the middle with their text inside (`fit_window_native_16x9`). |
| Videos (Bink) | Letterboxed to 16:9 with bars while they play. |

Checked with screenshots at 4:3 (1440x1080), 16:9, 21:9 (2560x1080), 2.39:1
(3440x1440) and 32:9 (3440x968).

## Tuning (live)

| Setting | Default | Meaning |
|---|---|---|
| `f2 ws tune cover <f>` | 1 | Menu covers move by f x the extra width per side. |
| `f2 ws tune page <f>` | 1.2 | Book pages (text, buttons, clip masks, minimap markers) move by f x the extra width. |
| `f2 ws tune card <f>` | 1.8 | Waiting main menu cards move by f x the extra width. |
| `f2 ws tune bookcover <f>` | 1.2 | Book screens (pause, shops, Items): the right cover moves by f x the extra width. |
| `f2 ws tune paper <f>` | 1.5 | Book screens: the page's paper backing moves by f x the extra width. |
| `fit_window_quad_power` | 1 | The game's 2D quads are squeezed by ((16/9)/aspect)^this. |
| `fit_window_text_power` | 1 | The game's 2D text x moves to the centre by ((16/9)/aspect)^this. |

## Known issues

### Title screen box stays 16:9

On a window wider than 16:9, the title screen's black/grey box ("Press A to
start", legal text) stays 16:9 and the main menu's scrolling panorama shows at
its sides until the box fades out after the legal text.

What was tried (fit window builds 21-28, console tests):

- The box is the title's `White` plane. Its `scale.x`, `position.z` and the
  title camera's `position.z` all read back as set but are put back by the
  title's timeline when it draws. Only its opacity sticks.
- `clone()` of `White` is refused ("Clone cannot be attached to a different
  time context").
- Hiding the panorama while the box shows needs a signal from the title's Lua
  state to the panorama's, which is a different state. A host relay worked,
  but it only gives black sides (not the box's colour), so it was removed.
- Searching guest memory for the box's 12.8 keyframe (`findf`, `pokef`) did
  not find the value that sizes it.

Open: find the title timeline's keyframe data for `White.scale.x`, or where
GameFace sizes that layer.

### Frozen-scene snapshots are 16:9 ("Scene paused", opening the pause menu)

Pausing an in-engine cutscene shows the frozen scene at 16:9 in the middle
with dark red at the sides. Pressing Start in game does the same for a moment
before the pause menu appears, with stale content from earlier frames at the
sides. The picture is not distorted: only the middle 16:9 of the frame is
taken and redrawn. No GameFace screen runs Lua during this pause and no
native 2D quad draws it (build 38 logs every quad caller), so the snapshot
is copied and drawn by a path that works in 16:9. Not traced yet: a scan
for direct `lis 0x820A / lfs -0x184` loads of the patched aspect
(0x8209FE7C) found none, so the readers build that address another way.
