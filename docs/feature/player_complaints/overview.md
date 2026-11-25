# Fable II player complaints: overview

**Status:** no commit yet (research only) · **Commits:** none · **Last checked against:** 7863041

## What this is

A list of what players and reviewers complained about when Fable II came out in 2008, and what the recomp already does about each one. The quotes and sources are in [technical.md](technical.md). Which complaints are worth fixing is in [qol_candidates](../qol_candidates/overview.md).

## What players complained about, and where things stand

- **Slow menus.** The inventory got slow with lots of items, and every screen took too long. Players say the same about the recomp today.
  - Today: on-screen text and subtitles already update twice as often as on the console (on by default). **High Tick Rate** in the launcher makes menus react faster, but it's marked unstable and speeds up some minigames.
  - The real cause of the slowdown with many items hasn't been found yet.
- **Finding your way.** There was no world map, and it was hard to find shops, family members and your own house.
  - Today: nothing for normal play. The game's code contains an unused minimap switch and an unused fast-travel screen, which are worth a look.
- **Cancel casts a spell.** The button that cancels things in most games casts a spell in Fable II, so players set towns on fire by accident.
  - Today: with keyboard and mouse, both the B key and the middle mouse button do both jobs. The launcher's **Keybinds** section lets you move them to other keys, but cancel and spell still share one button.
- **Expressions are awkward**, especially with a mouse.
  - Today: photo mode's pose cards can play any expression. The developer console can unlock all expressions or make the long versions always succeed.
- **Co-op.** Online co-op didn't work at launch, the second player had no camera of their own, and some achievements needed co-op.
  - Today: nothing new for co-op. The game's camera zoom can be changed from the developer console, which might help local co-op.
- **Too easy.** Few weapons and spells, and being knocked out cost almost nothing.
  - Difficulty levels are being built on a separate branch and aren't in a release yet. Being knocked out is handled by the game's own scripts, so a tougher penalty could be added there.
- **The ending.** No final fight, and some choices felt pointless afterwards. That needs new story and voice lines, so it's out of reach.
- **Knothole Island (DLC).** Short, with repetitive dungeons. Nothing changed.
- **Lost extras.** The bonus items from the old Fable II website **are back**: the recomp puts them in the Guild chest when a save loads. This is on by default. The Collectors Edition items are unlocked the same way, but that still needs checking in game. The Pub Games rewards are still missing; the game's code has an unused debug function for them that is worth checking.

## What the recomp already does

- Keyboard and mouse controls you can rebind in the launcher, with on-screen button icons that switch between keyboard and controller as you play.
- Mouse camera and mouse aiming.
- Photo mode (F6 by default).
- A Quit option in the main menu.
- Skipping the logo videos (off by default, in the launcher).
- VSync and frame rate limits (launcher).
- Widescreen up to 4:1.
- The old website items in the Guild chest (on by default). The Collectors Edition items are unlocked too, not yet checked in game.
- An achievements list (F7) with pop-ups when you unlock one.
- A developer console.

Being worked on at the time of this research: difficulty settings, an in-game settings menu, a property menu, and better timing for job minigames and expressions.

## What players need to do

Nothing. To try what exists today:

- **High Tick Rate**, **Skip intro videos**, the website and Collectors Edition items, photo mode and the keybinds are all in the launcher. They can also be changed in the game's config file next to the game.
- Press **F7** in game for the achievements list.
