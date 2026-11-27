# High tick rate side effects: overview

**Status:** no commit yet (research only) · **Commits:** none · **Last checked against:** c2a9d12

## What the options do

Fable II updates its world in small steps called ticks. There are two kinds:

- **World ticks**, 15 a second. Gameplay, characters, scripts, the camera and the menus move forward once per world tick.
- **Fast ticks**, 30 a second, two for every world tick. They keep drawing and animation smooth between world ticks.

The launcher's advanced patch list has three related switches:

| Switch | Default | What it does |
| --- | --- | --- |
| **High Tick Rate** (marked unstable) | Off | Doubles the world ticks to 30 a second. The game feels more responsive and menus feel smoother. |
| **Higher HF Tick Rate** (marked unstable) | Off | Doubles the fast ticks to 60 a second. Only works together with High Tick Rate: the launcher turns High Tick Rate on when you tick this one, and turns this one off when you untick High Tick Rate. |
| **GUI Every HF Tick** | On | Updates subtitles and on-screen text on every fast tick, so they move more smoothly. They time themselves with a real clock, so they don't speed up. |

Turn on **both** tick-rate switches together. With only High Tick Rate, both kinds of tick run at the same speed and capes and coats behave strangely. Changes take effect the next time the game starts.

## What players saw

The game itself keeps real time well: it works out how many ticks to run from a real clock, so the world doesn't run at double speed overall. But some parts of the game count ticks instead of seconds, and those speed up:

- **Job minigames feel easier.** Blacksmithing in particular, and the woodcutting and bartending minigames probably too.
- **Expressions you hold** seem to fill at a different speed.
- **Possibly faster than normal:** rent coming in, some script timers (for example how long characters wait before doing something), and the day-night clock. These are suspicions that still need checking.

Things that are already fine at the higher rate: subtitles and text, the ranged aim cursor, the mouse camera and how characters are drawn.

## What caused it

Most of the game measures time in seconds, but some parts were written as "wait this many ticks" when the tick speed never changed. Doubling the ticks makes those waits half as long. Other PC ports of Xbox 360 games ran into exactly the same problem.

## What's different now

Nothing yet. This is a plan:

1. **Measure:** check, at each setting, how fast the day-night clock, rent, script waits and the minigames run compared with a real clock.
2. **Fix one part at a time:** keep the extra responsiveness, but make each affected timer use real seconds again. Each fix would get its own switch, so it can be turned off if it causes trouble.
3. Once the known problems are gone, the two options would no longer need the "unstable" label.

The details are in [technical.md](technical.md).

## What players need to do

Nothing. If job minigames or timed events feel off, turn the two tick-rate options off; that's the original game. Subtitles every fast tick can stay on either way.
