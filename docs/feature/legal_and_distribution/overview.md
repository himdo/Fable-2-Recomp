# Legal risk and clean distribution: overview

**Status:** research; its proposed test merged · **Commits:** 1a5bc57 (#250, the tracked-files test) · **Last checked against:** c2a9d12

## What this is

A summary of the takedowns of fan PC ports in late 2026, and the rules that keep Fable 2 Recomp on the safe side. It isn't legal advice. The details and sources are in [technical.md](technical.md).

## What we found

- In November 2026 Nintendo had several fan PC ports of its games removed from GitHub. Ports of recent games and projects that shared game files seem to have been the main targets.
- Microsoft hasn't acted against Xbox 360 ports so far, and one report says it is even interested in them. That rests on a single report, and a new Fable game is due in 2027.
- Careful projects never share the game's files or the code generated from them.

## How this project stays safe

- **No game files in the project.** The project's files are set up so that the game itself, its data, the converted game code and your saves can't be added by accident. Today nothing from the game is stored there, and an automatic test fails if a game file is ever added.
- **The download only holds the project's own work.** The release is built from a fixed list: the program, the launcher, the settings file, the project's own small scripts, a font and the licences. An automatic test fails if anything else, such as a game file or a save, ends up in it.
- **The automatic checks never need the game.** They run on every change without game files, so the game never reaches the build servers.
- **It only works with a real disc.** The launcher checks your disc image against a list of known retail versions before it copies anything.
- **Developer tools stay out of the download.** The remote-control feature used for automated testing only exists in developer builds.
- **No piracy.** The project's front page says it doesn't condone piracy, and anyone who posts links gets removed and banned.
- **The project's own code is open source** under a permissive licence. That covers only the project's code, not the game.

## What could be better

- The automatic check that no game file was added knows the game's main file types. A full copy of the disc turned out to hold about ten more types that only the game uses. Adding those to the check is still to do.
- A reference page listing how the game's scripts call the engine quoted some of the game's own on-screen text and messages. That text has been taken out, and the tool that writes the page now leaves it out by itself.
- The download doesn't yet include the licences of two pieces of borrowed code: the toolkit the port is built on and a small settings reader.

## What players need to do

Nothing new. You still need your own copy of the game.
