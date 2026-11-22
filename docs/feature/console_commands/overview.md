# Developer console: overview

**Status:** merged · **Commits:** 4f0afe5 (#127); later commands in #169, #177, #183, #184 · **Last checked against:** 5f5f55f

## What it is

A text console inside the game for testing and debugging. Press `~` to open it and type a command after the word `f2`, for example `f2 god on` or `f2 warp 1 2 3`. Commands can also be sent from a text file, which lets scripts and tools drive the game while it runs.

## What it can do

- **Move around and set up tests:** teleport, save and return to positions, pause and step frame by frame, change the time of day.
- **Change the hero:** toggles such as god mode, stats, gold and items.
- **Inspect the game:** read and search memory, list objects, dump data, and read or change runtime settings.
- **Feature controls:** widescreen (`f2 ws`), mouse look and mouse aim (`f2 mouse`), a pretend controller for scripted tests (`f2 pad`), and photo mode.
- **Run the game's own script functions** directly.

The technical file lists every command with its arguments and examples.

## What players need to do

Nothing; normal play doesn't need the console. It's meant for testers and developers. Some commands can break a save (for example by giving items or changing quest state), so try them on a save you don't mind losing.
