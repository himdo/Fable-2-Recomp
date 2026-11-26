# Manifest naming check: overview

**Status:** no commit yet · **Commits:** none · **Last checked against:** 7863041

## What it does

The project keeps a list of the game's functions with readable names, so people can find their way around the recompiled code and, one day, mod the game. Each name follows a fixed pattern, and each one comes with a note saying what the function does and how that's known.

Until now, only the maintainer checked that by hand. Now an automatic check runs on every pull request. It looks at the names a pull request adds or changes, and fails if one of them:

- is still a generic placeholder instead of a real name;
- doesn't follow the naming pattern;
- has no note explaining it.

For the whole list, it also checks that no two functions share a name and that each name matches its function's address.

## What's different now

Contributors find out about a naming mistake straight away, from the automatic checks, instead of waiting for a review. The thousands of older names that don't follow the rules yet aren't blocked. The check counts them, so it's clear how much renaming is left.

## What players need to do

Nothing. This only affects people who contribute to the project.
