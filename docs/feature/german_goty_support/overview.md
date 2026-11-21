# German GOTY support: overview

**Status:** merged · **Commits:** bfcc5d6, 724495a, 77059af · **Last checked against:** 5f5f55f

## What it does

The recomp works with your own copy of Fable II Game of the Year. It now accepts two editions:

- **GOTY USA/Europe**
- **German GOTY**

The two game files differ in only a few hundred bytes of header information. The game code itself is identical, so one build can run both. The launcher checks that your copy is one of these two exact files (by its checksum) and refuses anything else, so an unsupported version can't start by accident.

The game starts in English for the USA/Europe edition and in German for the German edition, unless you choose another language.

## What isn't supported

- The German **retail / title update 1** edition (not GOTY). It needs its own conversion work. Swapping in the USA/Europe game file doesn't work either; it freezes during the intro.
- Russian editions and other disc or update revisions.

## What players need to do

Copy the game from your own GOTY disc (USA/Europe or German) with the launcher. The launcher tells you if your copy isn't supported.
