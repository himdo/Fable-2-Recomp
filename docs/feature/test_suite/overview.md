# Test suite: overview

**Status:** merged · **Commits:** 8957735 (#166), be11165 (#174), ffa15b3 (#196), c764cba (#197), 5cc6513 (#229) · **Last checked against:** 7863041

## What it is

The project has automatic tests that check its tools, its settings files and its launcher, and that some past bugs stay fixed. None of them need the game. They run on GitHub for every pull request, and anyone can run them on their own computer before and after a change to make sure nothing broke.

There are four groups:

- **Tool tests (Python).** The build and packaging tools are checked with small pretend files in a temporary folder, so the project's real files are never touched.
- **Settings checks.** The same setting is written down in four places (the settings file, the game's built-in copy, the launcher and the game's defaults). The tests make sure all four agree, so a new setting can't be forgotten in one of them.
- **Launcher tests.** The logic behind the launcher: saving your settings, recognising your game version, copying from a disc image, choosing what to start, and the update check. The windows and buttons themselves aren't tested.
- **Native tests (C++).** Checks on the game's own code that can run without the game. These only run on Windows.

## Known problems

Fifteen tests describe bugs that haven't been fixed yet. Each one checks for the *correct* behaviour and is marked "expected to fail", so the suite stays green. When someone fixes the bug, that test starts passing and reminds them to remove the mark.

The problems are all in developer tools, not in the game. The most important ones:

- **The speed-up copy generator could overwrite hand-made work** if an editor adds an invisible marker (a "byte order mark") to the start of a hand-tuned file.
- **The build-number tool doesn't give the same result twice** for short file paths, which is the one thing it is meant to do.
- **The version-information tool writes one value back to front,** so programs that read it may not find the version details.
- **The SDK preparation tool gives a misleading "libmspack is missing" message** when the real problem is an empty repository or a submodule that was never downloaded.

The technical notes list all fifteen.

## What isn't checked

- The launcher's windows and buttons.
- Starting the real game, or anything that needs the game files or a graphics card.
- Some newer tests (mouse logging, the recomp Lua scripts, the world-thread script guard and the native tests) aren't described in these notes yet.
