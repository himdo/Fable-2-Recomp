# Scenario runner: overview

**Status:** no commit yet · **Commits:** none · **Last checked against:** c2a9d12

## What it does

The game has a developer console that can load a save, press buttons, read values such as the hero's gold, and check a value in the game's memory. It can also take its commands from a file.

This adds a tool that writes a short list of such commands (a "scenario") into that file, waits while the running game works through it, and then reports whether every step went well. For example: load the first save, give the hero some gold, and show the gold before and after.

Scenarios are meant for two things:

- **Checking features:** a contributor can rerun the same scenario after a change and see straight away if something broke.
- **Letting an AI assistant see the game:** running on the same computer as the game, it can try something, read the results and look at any screenshots, without anyone at the controller.

## What's different now

The tool and its tests are in place. It hasn't been run against the real game yet; that happens next, on a Windows machine with the game files. Screenshots from the console and comparing values such as gold are planned as separate steps.

## What players need to do

Nothing. This only affects people who contribute to the project.
