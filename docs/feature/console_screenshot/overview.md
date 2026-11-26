# Console screenshot: overview

**Status:** no commit yet · **Commits:** none · **Last checked against:** c2a9d12

## What it does

The developer console gets a new command, `shot`, that saves a picture of what the game is showing at that moment. It uses the same capture as photo mode, but works at any time, not just in photo mode. Pictures go into a `shots` folder inside the game's `console` folder unless a file name is given.

Because `shot` is a console command, it can also be part of a scripted test: load a save, do something, take a picture. That lets an AI assistant running on the same computer look at the result of each step, not only read numbers.

## What's different now

The command is written, but hasn't been built or tried yet; that happens next on a Windows machine with the game files.

## What players need to do

Nothing. Photo mode's own screenshots work as before. `shot` is only for people using the developer console.
