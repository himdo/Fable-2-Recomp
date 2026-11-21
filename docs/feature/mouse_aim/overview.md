# Mouse aim (ranged aim reticle): overview

**Status:** merged · **Commits:** 49c70ab (#183) · **Last checked against:** 5f5f55f

## What players saw before

With keyboard and mouse, aiming a gun or crossbow felt like using a controller stick. WASD moved the reticle, and the mouse turned a camera that wasn't even in use while aiming. The reticle had a top speed and smoothing, so quick, precise aim wasn't possible.

## What's different now

- **While you aim, the mouse moves the reticle**, and WASD stops moving it. It moves exactly as far as the mouse, with no top speed, deadzone or smoothing. Up/down and sideways move at the same speed on screen on any screen shape.
- **The game's own targeting still works.** The reticle still highlights people and enemies near it, and the aim camera still turns toward where you aim.
- **Moving the mouse lets go of a highlighted target**, without a jump. While you keep moving, the reticle doesn't grab every person it passes; once the mouse rests, the game picks a target near the reticle again. Small movements don't break a lock straight away, slow movements are gently pulled toward targets the way a controller is, and the mouse wheel hops between targets.
- **A real controller aims exactly as before**, and switching between controller and mouse works at any time.

## What players need to do

Nothing; it is on by default. To change it, use the launcher's Mouse panel (aim mode and aim sensitivity), the `[input]` section of `fable2_config.toml`, or the `f2 mouse aim…` console commands:

| Setting | Default | What it does |
| --- | --- | --- |
| `mouse_aim_mode` | `both` | When the mouse aims: `both` = aim button held and the game is aiming; `rmb` = the aim button alone; `detect` = the game aiming alone; `off` = never |
| `mouse_aim_input` | `direct` | `direct` moves the reticle itself; `stick` uses the older controller-stick style |
| `mouse_aim_sens` | `0.0007` | How far the reticle moves sideways per bit of mouse movement |
| `mouse_aim_y_ratio` | `1` | Up/down speed compared with sideways |
| `mouse_aim_invert_y` | `false` | Invert up/down |
| `mouse_aim_scale` | `256` | Sensitivity for the `stick` style |

## Known rough edges

- After a very fast flick off a target, the reticle can occasionally jump back a little for a moment.
- Hopping to a far target with the wheel can drift slightly to the side.

Both are rare and were accepted for now.
