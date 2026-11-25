# Integrated graphics crash: overview

**Status:** merged for laptops with a graphics card; open for machines with only built-in graphics · **Commits:** 4d58a5f (#55), shipped in 1.5.0 · **Last checked against:** 7863041

Issues: [#42](https://github.com/himdo/Fable-2-Recomp/issues/42), [#48](https://github.com/himdo/Fable-2-Recomp/issues/48).

## What players saw

On laptops and handhelds, the opening cutscene played, the screen went black for the move into gameplay, and then the game closed. A few people saw it before the main menu or when loading a save.

## What caused it

There are two kinds of machine here.

- **Laptops with a real graphics card as well as a built-in one.** Windows started the game on the weak built-in chip instead of the real card, and that chip couldn't keep up.
- **Machines with only built-in graphics** (Intel Iris Xe, Intel Arc in Core Ultra chips, AMD Vega 8). There is no other card to switch to. Built-in chips are much slower than graphics cards, and the first heavy scene can take so long that Windows decides the chip has frozen and resets it. Even on a machine where it didn't crash, the game ran at under 1 frame per second.

## What's different now

- **Since 1.5.0 (#55), the game asks for the real graphics card by itself** on laptops that have one.
- **Machines with only built-in graphics are not supported.** Some handhelds, such as the ROG Ally X, do run it, but most built-in chips either crash or are far too slow to play.

## What players need to do

- **Laptop with a graphics card:** nothing on 1.5.0 or later. If it still starts on the built-in chip, open Windows Settings → System → Display → Graphics, add `fable_2.exe` and choose High performance.
- **Built-in graphics only:** expect it not to work, or to run very slowly. If you try a newer version, post your log on #42.
