# Legal risk and clean distribution: overview

**Status:** no commit yet (research only) · **Commits:** none · **Last checked against:** 7863041

## What this is

A summary of the takedowns of fan PC ports in late 2026, and the rules that keep Fable 2 Recomp on the safe side. It isn't legal advice. The details and sources are in [technical.md](technical.md).

## What we found

- In November 2026 Nintendo had several fan PC ports of its games removed from GitHub. Ports of recent games and projects that shared game files seem to have been the main targets.
- Microsoft hasn't acted against Xbox 360 ports so far, and one report says it is even interested in them. That rests on a single report, and a new Fable game is due in 2027.
- Careful projects never share the game's files or the code generated from them.

## How this project stays safe

- **No game files in the project.** The project's files are set up so that the game itself, its data, the converted game code and your saves can't be added by accident. Today nothing from the game is stored there.
- **The download only holds the project's own work.** The release is built from a fixed list: the program, the launcher, the settings file, the project's own small scripts, a font and the licences. An automatic test fails if anything else, such as a game file or a save, ends up in it.
- **The automatic checks never need the game.** They run on every change without game files, so the game never reaches the build servers.
- **It only works with a real disc.** The launcher checks your disc image against a list of known retail versions before it copies anything.
- **Developer tools stay out of the download.** The remote-control feature used for automated testing only exists in developer builds.
- **No piracy.** The project's front page says it doesn't condone piracy, and anyone who posts links gets removed and banned.
- **The project's own code is open source** under a permissive licence. That covers only the project's code, not the game.

## What could be better

- Nothing automatically checks that a game file was never added to the project, only that none ends up in the download. A small extra check would cover that.

## What players need to do

Nothing new. You still need your own copy of the game.
