# Cut and unused content: overview

**Status:** no commit yet (research only) · **Commits:** none · **Last checked against:** c2a9d12

## What this is

A list of content that was cut from Fable II before release, and how likely it is that some of it is still in the game files. Nothing has been restored yet. The details and sources are in [technical.md](technical.md).

## What we found

- **The Singing Sword quest** is the best lead, and stronger than the research first thought. The project already keeps a list of every name the game's scripts use. That list shows the quest's own script pieces are still on the disc, for the sword, the quest-giver, the giant chicken and a set of statues. Nothing in the finished game ever calls them. Its voice lines aren't on the disc, apart from villagers reacting to the sword. Whether the places and characters it needs are still there isn't known yet.
- **The Pub Games rewards**, 15 items players used to unlock by playing a separate game, can't be earned any more. The game has its own hidden developer helper that seems to hand out these prizes, and the recomp's console can already call it. The disc does still hold all 15 items, and saves still have room for the old unlock. One command in a loaded game would show whether they can be handed out.
- **Dunecrest**, an early name for Rookridge, is all over the finished game's files: in nearly 30 of its scripts and in the names of two sets of sound files. It looks like the name the developers kept using inside the game for the place players know as Rookridge.
- **Leftover developer tools:** the game's engine offers 65 script commands that no script uses, mostly debug and editor helpers.
- **Website and Collectors Edition extras** are already back. The recomp unlocks the items that used to need an online registration or the Collectors Edition, and they appear in the guild chest when a save loads. They're on by default. The Collectors Edition part still needs a check in game, and it isn't known yet whether the Hall of the Dead dungeon follows the same switch.
- **Things only seen in concept art**, such as rapiers and bows, don't appear in the list of script names. They're probably not in the files.

## How it can be checked

The project already has tools that read your own copy of the game: a disc-image reader, a reader for the game's script and art archives, and the in-game console, which can search the game's names, load any region, spawn characters and hand out items. The technical notes list concrete steps for each lead.

Any experiment with quests should use a spare save, because quest changes can be written into the save.

## What could change

If the Singing Sword quest or the Pub Games prizes turn out to be complete on the disc, they could be switched back on from the player's own copy. Nothing would be shared.

## What players need to do

Nothing. The website and Collectors Edition unlocks are already on by default.
