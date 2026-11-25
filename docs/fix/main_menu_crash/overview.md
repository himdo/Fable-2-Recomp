# Main menu crash on source builds: overview

**Status:** merged · **Commits:** afebcc5 · **Last checked against:** 7863041

## What players saw

Builds made from source (with the project's own copy of the runtime) crashed as soon as the game reached the main menu, on both the D3D12 and the Vulkan renderer. The older prebuilt runtime didn't crash.

## What caused it

At start-up the game asks the system for the same blocks of memory several thousand times. On a real console the repeated requests fail, and the game relies on that: a failure tells it "you already have this memory, keep using it". A local change to the runtime had made those repeated requests succeed instead. The game then thought it had fresh memory, set up its memory pools again on top of the old ones, and lost track of them, which crashed at the main menu.

## What's different now

The runtime behaves like the console again: a repeated request fails, and the game keeps its existing memory. The crash report was also made safer, so a crash no longer causes a second crash while it is being written down.

## What players need to do

Nothing. The start-up log still shows several thousand `AllocFixed attempting to reserve an already reserved range` errors. **They are expected and harmless**: the game depends on them.
