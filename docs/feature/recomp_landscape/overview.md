# Xbox 360 recomp landscape: overview

**Status:** no commit yet (research only) · **Commits:** none · **Last checked against:** c2a9d12

## What this is

A look at the other Xbox 360 games being turned into PC versions the same way as Fable 2 Recomp, and what they've added. Nothing in the game has changed because of it yet. The details and sources are in [technical.md](technical.md).

## What we found

- Fable 2 Recomp helped start the toolkit everyone now uses: the person who made it was first approached about this project.
- By November 2026 about 50 Xbox 360 games were being ported this way, and at least 29 of them could be played.
- Other ports have added things like their own graphics engines, smooth 60 FPS, mouse look and installers that work from a disc image.
- The ports people trust most keep a clear list of what each fix touches and what is still broken.
- Another project, "Fable: Return to Albion", says it has online co-op working. Nobody has confirmed that.

## How Fable 2 Recomp compares

| Other ports have | Fable 2 Recomp has | Still missing |
|---|---|---|
| Their own graphics engine | The toolkit's standard graphics engine, on DirectX 12 by default, with Vulkan in progress on Windows | A graphics engine of its own |
| Linux and phone versions | A Linux version using Vulkan (set up from a terminal, controller needed) | A phone version (that's a separate project), and a Linux launcher |
| Smooth 60 FPS | Frame limits from 30 FPS to unlimited, frame smoothing, and the 30 FPS lock fixed | Some timers run too fast at the faster update rates |
| Higher internal resolution and upscaling | Rendering at up to 4 times the resolution, sharper textures, smoothing of jagged edges, widescreen at any window shape | An upscaler such as FSR |
| Mouse look | Full keyboard and mouse support: mouse camera, mouse aiming and keyboard button icons | Some in-game text still shows controller buttons |
| Skipping intro videos | An option to skip them | — |
| Installing from a disc image | A launcher button that checks your disc image and copies the game out of it | Only on Windows |
| A page of fix switches for testers | A Patches page in the launcher with a switch for each game fix | Speed settings aren't on that page, and nothing can be switched while playing |

## What the project builds on

- **The toolkit:** the project uses its own copy of the toolkit, version 0.10.0, and fixes graphics bugs in that copy.
- **Earlier community fixes:** several fixes people made for the Fable II emulator years ago are now built in. Among them are faster game updates, unlocked bonus items, skipping intro videos and turning off edge smoothing.
- **Modders' work:** the tool that reads the game's script archives is based on what Fable II modders worked out. A popular Fable II mod manager isn't supported yet, though players have asked for it.

## What could change

Ideas this project could borrow:

- fixing high frame rates one game system at a time ([high_tick_rate_side_effects](../../fix/high_tick_rate_side_effects/overview.md));
- growing the launcher's Patches page into a full set of switches that lets testers turn individual fixes off to find the cause of a problem;
- automatic checks for the Linux version, like the Windows checks that already exist;
- support for existing Fable II mods.

## What players need to do

Nothing.
