# Alt-tab script crash: overview

**Status:** merged · **Commits:** c5c80ff (#189, the fix), 266f181 (#195, the guard) · **Last checked against:** c2a9d12

## What players saw

In version 1.6.0 the game could crash after a few minutes of normal play. It happened most often when coming back to the game after alt-tab or clicking another window, and sometimes right after loading a save.

## What caused it

The direct mouse camera (added in #177) starts a small game script to take over the camera. When the mouse moved and that script hadn't answered for a while, the camera tried to start it again. Coming back from another window is exactly that situation.

The problem was *where* it started the script from: the part of the program that draws the picture, not the part that runs the game world. Starting a game script from there can land in the middle of the game's own script update and damage the script engine, which then crashes. Another change (#184), merged twelve minutes later, moved every other script start to the safe place. Its branch didn't include this new one, so this one was missed.

A second problem was fixed at the same time: the console's helper script was being saved into save games, so loading such a save started a second copy of it.

## What's different now

- **Fixed in 1.6.1** (#189). Every game script the recomp starts now goes through the safe place at the start of the world update. The console helper is no longer saved into save games.
- **It can't quietly come back** (#195, on master since 1.6.1 and in the next release). Automatic tests fail if any code starts a game script from anywhere else. If it still happens in a running game, the script is refused and one warning is written to the game log instead of crashing.

## What players need to do

Nothing. Update to 1.6.1 or later. Photo mode, which was disabled during testing as a precaution, is safe to use again.
