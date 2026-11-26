# Probe timeline: overview

**Status:** no commit yet · **Commits:** none · **Last checked against:** c2a9d12

## What it does

The project gives the game's functions readable names, so people can find their way around the code and, one day, mod the game. Thousands still have placeholder names, and working out which ones matter is slow.

This adds a tool for a different approach: play a normal stretch of the game while a recording notes which functions run and when, and type a short note at each moment worth marking, like "entered the Guild" or "first fight". Afterwards, the tool lists for each note:

- the functions that ran for the first time after it;
- the functions that already existed but suddenly got much busier, like combat code during a fight.

That gives contributors a short list of functions to look at for each part of the game, instead of thousands to choose from. The tool only says where to look. Each function still has to be read before it gets a name.

## What's different now

The tool and its tests are in place. The recording part isn't yet: the game's function tracer still has to be taught to write the two logs the tool reads, which needs a Windows machine. The formats those logs must use are written down, so that step can follow.

## What players need to do

Nothing. This only affects people who contribute to the project.
