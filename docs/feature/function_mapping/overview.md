# A repeatable function-mapping process: overview

**Status:** no commit yet (research only) · **Commits:** none · **Last checked against:** c2a9d12

## What this is

A proposed way of working for contributors who give the game's code real names. It builds on the first big naming pass ([function_naming](../function_naming/overview.md)). The full process and the numbers behind it are in [technical.md](technical.md).

## Where things stand

The project keeps a list of the game's functions with a name for each. Counted on 25 November 2026:

- **About 5,100 functions are on the list.** The game has 60,753 in all (counted in a build in November 2026), so most still have only an automatic name.
- **Just over half of the names are placeholders.** They were made automatically in one go in September 2026 and say nothing about what the code does. Since then only 14 of them have been replaced, because a function only gets a real name when someone's change needs it.
- **About 300 more names only describe the shape of the code** ("a jump", "a lookup"), not what it's for.
- **About 2,100 names say what the code does.** Almost none of them say how sure we are. An earlier naming pass did grade itself (high, medium or low confidence), but that habit wasn't kept.
- **The game's own script commands are listed by name:** about 180 engine functions and methods, plus more than 2,000 named tables that hold more of them. Only a handful have been matched to the code behind them. Each match is a name we can be sure of, so this is the best place to dig.

## The idea

- **Every name gets a confidence level.** It runs from "just a guess" to "checked in the game" to "checked by an automatic test". Features may only rely on the more certain names.
- **An automatic check for the naming rules.** Today the project only checks that a name doesn't clash with the toolkit's own names. A second check could also catch badly formed names and names with no explanation. It would only judge new names, so it doesn't fail on the old ones.
- **One part of the game per session, one small change per session.** For example: "which code gives the player gold?"
- **Better ways to find code,** starting with the game's own script commands, which come with names.
- **Honest progress numbers:** how many functions have a real name, how many are still placeholders, and how sure we are of them.
- **What to map next:** the game's timers, then the menus and inventory, then gold and property, combat, saves, quests and achievements.

## A real example

The difficulty settings work (not finished yet) already worked this way. It picked one question (where do gold, prices, damage and experience come from?). It added temporary logging and played the game to see what happened. It wrote down what it found. Then it named 74 functions in one go, each with a note saying whether it was checked in the code or only reasoned out. That's more labelled names than the whole rest of the list has today.

## Automatic test runs

Debug builds of the game can already be driven by a script: it can press buttons, move the sticks, take screenshots and see which menu is showing. It can't yet read anything from the game itself, such as how much gold the hero has. So it can play a sequence but can't check the result. Adding that is the missing piece for tests like "buy this item on Hardcore and check the price". These tests need the game files, so they would run on contributors' own machines, not on the project's automatic checks.

## Why it matters

Clear, checked names make the game easier to fix and to mod. They are also the groundwork for a full readable version of the game's code later on.

## What players need to do

Nothing. This only affects how contributors work.
