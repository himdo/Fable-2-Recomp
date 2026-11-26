# Remote console: overview

**Status:** no commit yet · **Commits:** none · **Last checked against:** c2a9d12

## What it does

Developer builds of the game have a remote-control connection that a script or an AI assistant on the same computer can use to press buttons and take screenshots. It can now also send developer-console commands over that connection, such as loading a save, showing the hero's gold, or checking a value in the game's memory. It gets back everything those commands printed, and whether any of them reported an error.

## What's different now

A tool that already drives the game over this connection can now also set it up and check the results, without anyone typing in the console. The change is written and its parts are tested on their own, but it hasn't been built or tried in the game yet; that happens next on a Windows machine.

## What players need to do

Nothing. The remote-control connection only exists in developer builds; the builds players download don't have it.
