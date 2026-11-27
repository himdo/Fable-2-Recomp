# Difficulty settings: overview

**Status:** no PR yet · **Commits:** bd6846a, 4d14650 (probes), 891eab4 (names), c2215e2, 92583be (multipliers), bad29df (cards), on branch `feature/game-difficulty`, not on master · **Last checked against:** 7863041

A new feature in progress: choosing a difficulty when starting a new game. Feature code is on `feature/game-difficulty`:
- the multipliers (2026-11-22);
- since 2026-11-24, the difficulty cards on New Game and with F8 in game, plus a level file per save slot for the Continue screen's label (technical.md section 19).

All of it is committed on that branch (bad29df is the latest), but there's no pull request yet. The addresses, function names and evidence are in [technical.md](technical.md).

Progress so far:
- **2026-11-15: the idea and a first research pass.** Read-only searches of the game's code and data.
- **2026-11-20: research step 1.** Checked by hand and in game, with a temporary log probe on branch `feature/game-difficulty`.
- **2026-11-21: research step 2.** We found why the game's own price difficulty never kicked in, and built a bigger log probe for the next play session (see "Research step 2" below). The notes moved from a personal folder into the repo.
- **2026-11-22: research step 3, the multipliers decided, feature code step 1.** Gold, prices, damage taken and XP now follow the level (technical.md section 18).
- **2026-11-24: feature code step 2.**
  - The prologue checked: its gold and the music box can't get stuck on any level.
  - The difficulty cards on New Game (after boy or girl) and with F8 in game.
  - The level saved per slot, and a first go at showing it on the Continue screen.

Findings are marked as:
- confirmed in the code: read in the repo or the game's compiled code
- confirmed in the scripts: read from the game's own scripts
- needs testing: a reading not yet checked in the game

## The idea (delta4o, 2026-11-15)

- **When:** on New Game, the player picks a difficulty.
- **Order:** gender first, then difficulty. This is easier for audio. Once the difficulty is chosen, Theresa's original "destiny" line (the one that plays today after choosing boy or girl) plays. Some of her other audio might be reused; which line is still open.
- **Four levels, each with a subtitle:**

  | Level | Subtitle |
  | --- | --- |
  | Easy | "Little Sparrow" |
  | Medium | "Hero of Albion" |
  | Hard | "GuildMaster" |
  | Hardcore | "Archon's Heir" |

  delta4o chose these on 2026-11-20; they replace the earlier "Archon of their time" idea. Still to check: how the game itself spells Guildmaster, so the subtitle can match it.

- **Cards:** the levels are picked from cards like the save slots, using pictures that are already in the game files.
- **What it changes:**
  - how much gold the player gets, from every source;
  - how expensive things are;
  - how much damage enemies do;
  - how much health enemies have;
  - how much XP the hero gains (added 2026-11-22).

  **Easy is the original game**, with every multiplier at 1.0, and it's the default (changed 2026-11-22, decision 6; the first plan had Medium as the original game).
- **Hardcore:** no "Save and Continue" or "Save and Quit", to make it extra hard. The player shouldn't be forced to play a certain way to get a save, but also shouldn't be able to cheat the system.
- **Pause menu:** the difficulty shows somewhere under the game options, and it can be changed there.
- **Title screen:** the save slot shows its difficulty, e.g. "Hero 1 (Easy)" or "Hero 2 (Hard)". The other option is to put it on a new line.

## What we found

### The game already has a hidden difficulty system
- **Confirmed in the code.** Fable 2 has its own difficulty level per hero, plus a table of multipliers for:
  - damage the hero deals;
  - damage the hero takes;
  - XP gained;
  - shop prices (buying and selling);
  - spell mana cost (unused).
- **Confirmed in the code.** The multipliers only apply when the level is **above Normal**. On Normal the game skips the table, which fits "Normal = 1.0".
- **Confirmed in the scripts.** The Xbox achievement "Masochist" unlocks when the level is above Normal and was never changed. The original game had no menu for this; it seems to have been cut or kept for a hidden mode.
- **Not read yet:** the multipliers' actual values, which sit in a binary data file (`globals.gdb`).
- So, for damage and prices, the game's own system might do most of the work. Enemy health and gold aren't in the table.

### Gold
- **Confirmed in the code.** All gold, gained or spent, goes through one function: quests, jobs, pickups, chests, rent, shop sales, begging. So one hook can scale every source.
- Watch out for:
  - spending goes through the same function, so only gains should be scaled;
  - the hook must touch only the hero's purse, not NPCs, chests or shops;
  - in co-op the game splits gold with a second call, which mustn't be scaled twice;
  - a few quests set gold directly, which skips the function;
  - shop sales: if both "prices" and "gold gained" are scaled, selling an item is scaled twice.

### Prices
- **Confirmed in the code.** Shop buy and sell prices already include the game's difficulty multiplier: buying multiplies by it, selling divides by it.
- Property prices use different code, not looked at yet.

### Enemy damage and health
- **Confirmed in the code.** Every health change, for anyone, goes through one function.
  - When the target is the hero, that's where enemy damage can be scaled.
  - When the target is an enemy, that's where its effective health can be scaled.
- **Confirmed in the code.** Each creature also has its own damage multiplier that scripts can set.
- Watch out for:
  - quests that kill or hurt on purpose (with a huge number, say) use the same function;
  - allies, the dog and henchmen take damage the same way, so the code has to tell friend from foe.

### Saves
- **Confirmed in the code.** Every save goes through one function, and that function checks a game switch, "saving allowed". The game itself uses that switch in one quest (the Bloodstone assault).
- **Confirmed in the code.** The pause menu's Save box has **three** options: Save and Continue, Save and Quit, and **Quit without saving**.
- **Confirmed in the scripts.** Autosaves happen during the main quests, the jobs and at some chest, door and gold triggers. Other autosaves (moving between regions, sleeping, buying a house) probably exist in the engine but haven't been pinned down.
- **Confirmed in the scripts.** Dying never saves or loads: you lose XP, get a scar and carry on. Hardcore doesn't need to handle death.
- **The ways back to an older save:**
  - "Quit without saving";
  - closing the window;
  - the recomp's own Quit item on the title screen;
  - a prompt the game shows when leaving an area mid-quest, which reloads a "before the quest" save.

  Hiding the two save options alone wouldn't stop cheating.
- **Where saves live:** `saves/…/Hero000/` (one folder per slot). It first looked as if the game recreates this folder on every save; research step 1 showed it doesn't (see below).

### The new-game screen and Theresa
- **Confirmed in the code.** The boy/girl choice is a card screen (`choosecard`), driven by the game's compiled code, not by a script.
- **Confirmed in the game files.** Theresa's lines, in order (paraphrased):
  1. the story begins;
  2. she asks who the hero of the tale is (when the cards appear);
  3. his, or her, destiny unfolds (separate recordings and subtitles for boy and girl).
- Today line 3 plays the moment you confirm boy or girl (needs testing). To get delta4o's order, line 3 has to move to after the difficulty choice.
- **Unused Theresa lines exist** (paraphrased): she greets the player as a young man, then apologises and corrects herself for a young lady; says they remind her of another child; and offers to tell the hero's story. They have text and audio, but nothing in the game seems to play them. They could suit the difficulty screen.

### Save slot label
- **Confirmed in the code.** All six slot cards share **one** text line underneath, which shows the selected slot's name. The default name is "Hero" plus the slot number, e.g. "Hero 1".
- So "Hero 1 (Easy)" fits the existing line. The placeholder text suggests about 50 characters fit (needs testing). A separate new line would need new layout work.

### Pictures for the cards
- **Confirmed in the game files.** Card-sized pictures the recomp can already load (the photo mode uses the same kind):
  - the boy and girl cards and their card back;
  - the chapter pictures 1–3;
  - the tarot card frame;
  - the save-card back.
- The ability tarot cards (Toughness, Physique, Skill, Speed, Accuracy and so on) and the spell cards would also look right. They use a different texture format, so they may need extra decoding work.

### Things in the recomp we can reuse
- The title-screen menu is already modified (the Quit item). That's the place to step in on New Game.
- There's a bridge for changing GameFace screens (text, slides) from the recomp.
- The photo mode has a game-styled overlay menu that already draws game textures and the game font. It's a ready base if the difficulty cards are drawn by the recomp instead of the game.
- No one has added anything to the pause or options menus yet. That part is new ground.

## Research step 1 (2026-11-20)

### The game's hidden difficulty table has one row, not four
- **Confirmed in the code.** The game's reader asks only "which value" (damage caused, damage taken, XP, mana, prices), never "for which level". So the hidden table holds **one set of values**, used for every level above Normal.
- So per-level numbers (Easy, Hard, Hardcore) have to be ours. The good news: the game multiplies at exactly five spots, each one a "multiply by this number" step. We can give it our own number there, on every level, Normal included.
- **Confirmed in the code.** All five spots only apply to an entity that has the "player" part, and only when its level is above Normal.
- The game's own values are still unknown. The log probe shows them in game (below), so `globals.gdb` doesn't need decoding.

### Saving
- **Confirmed in the code.** The Save box's three buttons are built in order: Save and Continue, Save and Quit, Quit without saving. They do:
  - Save and Continue: save, stay in the game;
  - Save and Quit: save, then back to the title screen;
  - Quit without saving: straight back to the title screen.
- **Confirmed in the code.** The game only ever **opens** its save folders. It never asks for them to be wiped and recreated, and it has no "delete save" call at all.
- **Confirmed on disk.** In slot 1, the save files are from 2026-11-15, but `saveuid.bin`, the info file and the fail-quest save are still from 2026-11-07. So a save rewrites single files and the folder stays. A file of our own **inside** the slot folder may be safe after all (being tested with a marker file).
- Which code makes the region, sleep and house autosaves: being found with the log probe.

### Prices
- **Confirmed in the code.** House and shop (property) prices **don't** use the difficulty table. They're a base price times a tuning value, so they'd need their own hook if they should follow the price multiplier (open question 8).

### Menus
- **Confirmed in the code.** The other place that adds menu buttons at runtime is the **DLC list inside the Options menu**, not the pause menu. It still shows the game adding buttons to an Options page by itself, which is a useful model for the pause-menu entry.

### Slot label
- The text box's placeholder is 51 characters, so "Hero 1 (Hardcore)" (17) should fit on the same line. The box size can be read and a test label shown live from the console (below).

### In-game results (2026-11-20, late evening)

delta4o ran the checks on the slot 1 hero (chapter 3).

- **Confirmed in game.** `Player.SetDifficultyLevel` exists and works: `stats` showed level 2, then 3.
- **Confirmed in game.** The level **is saved with the hero**: it was still 2 after Save and Quit → Continue.
- **Confirmed in game.** After a change, `HasDifficultyLevelChanged` says `true`. So this test hero can no longer unlock Masochist (the game's rule).
- **Surprise: nothing changed.** At levels 2 and 3, shop prices stayed the same, and the game **never read its difficulty table**: the log has no value line at all, not even for the mood-ring purchase. So the game's own values are still unknown.
  - Note: the try at going back to level 1 failed. Two commands on one line (`...; f2 lua ...`) don't run, so type one per line. Level 1 vs 2 wasn't compared cleanly, but the missing table read is clear on its own.
  - No fighting yet, so damage dealt and taken are **untested**, not failed.
  - **Confirmed in the code** (found afterwards): `SetDifficultyLevel` writes the same slot the price check reads. So the open question was whether it writes it on the **same object**. Research step 2 found a simpler answer for shops (below).
- **Confirmed in game.** Each of the three saves in the session came from the pause menu's Save and Quit. **Sleeping in a bed made no save** through the save function.
- **Confirmed in game.** The marker file **survived a save** in the slot folder. A small difficulty file inside `HeroNNN/` is safe, which also suits the slot label.
- **Slot label.** `ui tree` ran twice and found the label at `Layer.loadgame.SaveGameDescription`. But neither the log nor `console/out.txt` has a `ui get` or `ui label` reply, so the box size and the "Hero 1 (Hardcore)" test **still need doing** (see the checklist below).
- Not done yet: region-change autosave, house purchase, fail-quest prompt.

## Research step 2 (2026-11-21)

### Why shop prices ignored the difficulty
- **Confirmed in the code.** When the game works out a shop price, the difficulty check doesn't look at the hero. It looks at a part that belongs to the **shop's stock**. That part has no "player" piece, so the game skips the difficulty multiplier altogether. That explains why the game never read its table while delta4o was buying and selling, whatever the level was set to.
- So the game's own price multiplier may never have applied to the hero's shopping, even in the original. If the new log confirms it, prices will need our own hook at the point where the game multiplies (the plan already had this as the fallback).
- Fighting and XP use the same kind of check on the hero, so they may still work. That's untested.

### What else the price is made of
- **Confirmed in the code.** A shop price is: the item's base value × the village economy × an item factor × the shopkeeper's own multiplier. Then come the difficulty step (skipped, see above) and a discount.
- **Needs testing.** When the hero passes a certain check (most likely owning the shop), the shopkeeper's multiplier is dropped and an owner discount applies.

### A bigger log probe
The temporary probe now also logs:
- which object the console's difficulty command really writes, compared with the hero's own;
- each of the game's five difficulty checks, with what it found;
- every price the shop works out, with all its parts;
- every gold change for the hero, and the first few for everyone else;
- health changes (the start of the friend-or-foe research);
- every save, autosave request and fail-quest save or load, and each button in the pause Save box;
- the New Game card events and the slot cards on the Continue screen.

It's still temporary and must be removed before the feature goes to master.

### The shout-out shop test (delta4o's idea)
Like using Cheat Engine: change one thing, watch what moves. While shopping, delta4o reads out what the screen shows, and Claude matches it with the log afterwards, by time. That tells us which part of the price formula each in-game modifier comes from, and whether the price shown is what's actually taken.

## Research step 3 (2026-11-22)

Details are in technical.md sections 15 (code reading) and 16 (in-game results).

### Every way gold comes in or goes out
- **Confirmed in the code.** Every gold change in the game is now accounted for:
  - **gains:** quest rewards, pub games, a wedding dowry, items worth gold, gift rewards, building income, gifts from other players, the co-op henchman's wage, rent, jobs, and shop and property sales;
  - **spending:** shops, property, tattoo removal, coach fares, family upkeep and allowances, fines;
  - **transfers:** in co-op, when player 2 leaves, their gold moves to player 1.
- The "more or less gold" rule has to skip transfers, refunds and gold that arrives over the network, or some gold gets scaled twice.
- **Confirmed in game.** Everything the game's scripts pay or charge (jobs, fines and more) comes through one doorway, so a gold hook can't tell those apart without help from the scripts.

### Prices
- **Confirmed in game:** the shop price is the item's value, plus the village's wealth, minus a discount for how much the shopkeeper **loves** and **fears** you (about 6% each in the test). Rude expressions took the Love discount away, and the price went up from 53 to 57, as predicted.
- **Owning the shop** replaces all of that with a flat owner discount (39% in the test).
- **The game's own difficulty price step never runs for the hero**, at any level. Prices will need our own hook.
- **Property:** the sale price matched the code to the coin (2123 for the Bowerstone Pie Stall). Buying works out the price from the building type, the village's economy, decoration, and the neighbours' opinion of you. One script command gives property away for free.

### The game's hidden difficulty
- **Confirmed in game:** the console command sets the hero's own difficulty level, and the game's checks for damage dealt, damage taken and XP all look at the hero.
- **But every value in the game's table is 1.0**, so the hidden system changes nothing in the shipped game. The checks are a good place to plug in our own values for damage and XP; the values themselves must be ours.
- The Ghoul augment's life-steal follows the damage dealt, so a damage multiplier will carry over to it.

### Saves
- **Confirmed in game:** what the Save box buttons do (Save and Quit, Quit without saving) and when a job autosave fires.

## Feature step 2 (2026-11-24)

### The prologue
**Confirmed in the scripts:** collecting the 5 gold and paying for the music box works on every level.
- The child's coins are handed out with a command that doesn't go through the gold multiplier, so each task still gives exactly 1 coin.
- The music box is a scripted payment, not a shop, so the price multiplier doesn't touch it either.
- The quest counts its own coin counter, not the purse.

### The difficulty cards
- **New Game:** after boy or girl, four cards appear: Easy "Little Sparrow", Medium "Hero of Albion", Hard "GuildMaster", Hardcore "Archon's Heir". Each shows its multipliers.
  - Easy is selected first. A card must be picked (there's no going back to boy or girl yet).
  - Then the game starts on that level.
  - **Confirmed in game:** four new games started on the chosen level. `stats` showed level 2 for Medium.
  - The level is set without the game's "changed" flag, so the Masochist achievement question (open question 5) stays open.
- **In game:** F8 opens the same cards and marks the current level.
  - Easy, Medium and Hard can be switched.
  - Hardcore can't be picked there ("New Game only"), and a Hardcore hero can't change level (decision 2).
  - Not tested in game yet. There's no controller shortcut yet.
- **Theresa's destiny line** now plays once a difficulty card is chosen, not on the boy or girl choice. The game then starts as long after the line as it did before. Built 2026-11-26; confirmed in game 2026-11-27 (girl on Hard: the line plays in full, then the game starts on Hard).
- **B on the New Game cards** goes back to boy or girl, and Theresa asks again who the hero is. Built 2026-11-26, fixed and confirmed in game 2026-11-27.

### The level on the Continue screen
- **Confirmed in game:** each save now writes the hero's level into a small file in its slot folder.
- **Confirmed in game:** the Continue screen shows "Hero 1 (Easy) / Bowerstone Market", "Hero 2 (Medium) / …" and so on. The level is written into the card's description text itself, because the GUI's text attribute doesn't change what's drawn.
  - The catch: the text can't grow, so the end of the chapter text is cut off by the same number of characters. See technical.md 19.4.

## Where to pick up next time

0. **2026-11-24 leftovers:**
   - the slot label cuts off the end of the chapter text (it needs a longer string, not an in-place edit);
   - check the cards' keyboard prompts ("E Choose" after keyboard input; built 2026-11-24, not tested);
   - F8 Easy → Hard → Easy in game;
   - round 4a: an unowned shop's buy and sell, buying a house;
   - backgrounds on the cards;
   - ~~B to go back to boy or girl, and Theresa's destiny line after the cards~~ (built 2026-11-26, technical.md 19.2);
   - a controller shortcut for F8.
1. ~~**Decide the starting multipliers**~~: done 2026-11-22 (decision 6). Easy is the original game.
2. ~~**Name the functions**~~: done 2026-11-22. 44 functions now have real names, among them the gold callers, the shop and property price steps, rent, and the combat checks (technical.md "What changed").
3. **Round 3 in game:** test the feature code (checklist below), and the leftovers from round 2: Golden touch on a monster, a region change and a house purchase, Save and Continue, the slot label, then New Game → boy or girl.
4. **Feature code, next steps:** ~~the New Game difficulty cards~~ (done 2026-11-24), the pause-menu entry (F8 for now), Hardcore saving, the slot label (started), then enemy health.
5. Next research: enemy health and telling friend from foe, then where the pause menu gets its items.
6. Tidy up before anything goes to master:
   - delete the probe and its temporary hooks;
   - delete the marker files in the slot folders;
   - remove the `saves.backup-difficulty-2026-11-20` backup.

   The slot 1 hero has "changed" set and was saved again on 2026-11-22 (5,358,567 gold); restore it from the backup if that matters.

## In-game checklist (round 3, feature code step 1)

**Run on 2026-11-22.** Everything matched: prices (shop and property, every level), job pay, rent, Golden touch, the fine, your own damage and the damage you take. One bug was found and fixed: the game caches the damage you take, so a level change only applied after a reload. Details in technical.md section 18.

Use the Release build in `out/build/win-amd64-release`. One console command per line. The slot 1 hero is probably at game level 3 (Hard), so set the level first. Each scaled gain writes a `[difficulty] gold` line to the newest `logs/fable_2_NNN.log`.

1. **Set Medium:** `lua Player.SetDifficultyLevel(QuestManager.HeroEntity, 2)`. The log should say `[difficulty] level Medium (game level 2)`.
2. **Shop prices** (×1.15 buy, ÷1.15 sell): the toy sword in your own shop should cost about 18 (was 16). The Pie Stall's Crumbly Blueberry Pie should cost about 25 (was 22), if the stall is still not yours. Sell something and compare with Easy.
3. **Gold** (×0.85): a blacksmith sword should pay 40 (was 47), and rent about 5,045 every 4 minutes (was 5,935). A guard fine should still be the full amount.
4. **Damage taken** (×1.5): a guard's hit should take about 45 health (was 30). Your own hits should still do the same damage as before.
5. **XP** (×0.85): XP orbs should give less than on Easy. (The Guild's scripted 350 Will XP stays whole, but that needs a new game.)
6. **Property:** the sale price of a property you own should be ÷1.15 (the Pie Stall: about 1,846 instead of 2,123).
7. **Back to Easy:** `lua Player.SetDifficultyLevel(QuestManager.HeroEntity, 1)`: everything should be as in the original again.

## In-game checklist (round 2)

**Run on 2026-11-22** (`fable_2_064.log`). Done: 1, 2 (no level comparison: pointless, because the price step never runs), 3, the job part of 4, two of the Save box buttons (5), and the slot cards (6). Still to do: listed under "Where to pick up next time", item 3.

Use the Release build in `out/build/win-amd64-release`. Type **one console command per line**; two on a line don't run. The slot 1 hero is at level 3. Each check writes `[difficulty-probe]` lines to the newest `logs/fable_2_NNN.log`.

1. **Which object the level is written to.** Load slot 1, then type `lua Player.SetDifficultyLevel(QuestManager.HeroEntity, 2)`, then `stats`.
2. **The shout-out shop test.** For each purchase, say:
   - which shop and which item;
   - your gold before;
   - the price shown;
   - what you think applies: liked or feared, the shopkeeper's reaction, whether you own the shop, how wealthy the village is;
   - your gold after.

   Buy **one item at a time**, a few seconds apart, and change **one thing at a time**:
   - the same item at level 1 (`lua Player.SetDifficultyLevel(QuestManager.HeroEntity, 1)`), then at 2 or 3;
   - a shop you own and one you don't;
   - before and after changing how the shopkeeper sees you (a few expressions);
   - also sell one item.
3. **Fighting and XP.** Hit an enemy, take a hit, and gain some XP.
4. **Gold and autosaves.** Pick up some gold and do a round of a job. Change region and buy a house.
5. **Optional, the Save box.** Try each button, leaving "Quit without saving" for last.
6. **Title screen.**
   - On the Continue screen, move between the slot cards.
   - Type `ui get Layer.loadgame.SaveGameDescription boxwidth boxheight size`, then `ui label Layer.loadgame.SaveGameDescription Hero 1 (Hardcore)`.
   - Then pick New Game, choose boy or girl, and quit.

## Decisions

**0. Scope of the first step** (asked 2026-11-20). Options:
  - gameplay core driven from the console;
  - **research only**;
  - new-game screen first.

**Choice: research only.** A second question asked whether enemy health goes in this step (options: later / include now). **Choice: later.** It needs a friend-or-foe check first.

**1. Hardcore saving** (asked 2026-11-15). Options:
  - **save on quit only**;
  - autosaves only, with all three pause options hidden;
  - leave it open.

**Choice: save on quit only.**
  - Autosaves stay.
  - "Save and Continue" and "Quit without saving" are removed.
  - Every way of quitting saves: "Save and Quit", closing the window, the recomp's Quit.
  - The leave-area reload prompt is blocked.

  The player never has to play a certain way to get a save, but can't roll back either.

**2. Changing difficulty mid-game** (asked 2026-11-15). Options:
  - **free, with Hardcore locked**;
  - fully free;
  - only to an easier level;
  - leave it open.

**Choice: free, with Hardcore locked.** Easy, Medium and Hard can be switched at any time from the pause menu. Hardcore can only be picked at New Game; it can't be entered or left later.

**3. Using the game's own difficulty system** (asked 2026-11-15). Options:
  - **investigate first**;
  - ignore it and use only our own multipliers;
  - use it as-is with the game's own values.

**Choice: investigate first.** It's the preferred route for damage and prices. Next step: read its real values from `globals.gdb`. The Masochist achievement question stays open (below).

**Result (2026-11-22):**
- The game's values are all 1.0, read live in game, so they suggest nothing.
- Its checks work for damage and XP, so they're the route there, with our values.
- For prices they never run, so prices need our own hook.

**4. Where these notes live** (asked 2026-11-21). Options:
  - keep them in a personal notes folder until the feature has a commit on master;
  - **move them into the repo's `docs/feature/` now**.

**Choice: move them now**, with the status "no commit yet".

**6. The multipliers** (asked 2026-11-22). delta4o corrected the plan first: **Easy is the original game**, and Medium, Hard and Hardcore only make it harder. Options for those three: steps that follow the XP values / gentler / steeper.

**Choice: steps that follow the XP values.** Gold follows the XP steps; prices rise by a similar amount; enemy damage climbs faster, because dying in Fable II costs little.

| | Gold gained | Prices | Enemy damage | Enemy health | XP |
| --- | --- | --- | --- | --- | --- |
| Easy | 1.0 | 1.0 | 1.0 | 1.0 | 1.0 |
| Medium | 0.85 | 1.15 | 1.5 | 1.25 | 0.85 |
| Hard | 0.7 | 1.3 | 2.0 | 1.5 | 0.7 |
| Hardcore | 0.65 | 1.4 | 2.5 | 2.0 | 0.65 |

The hero can buy about 0.74× as much on Medium, 0.54× on Hard and 0.46× on Hardcore (gold ÷ prices). Also decided:
- **Which gold scales:** earned gold. That's quests, jobs, pickups, chests, rewards, the dowry, rent and building income, and pub-game winnings. **Not** selling items or property: those already follow the price multiplier, so they'd be scaled twice. Transfers, refunds and gold copied over the network aren't scaled either.
- **Property** follows the price multiplier, buying and selling, like shop items.
- **XP** is scaled (the table's last column), through the game's own XP check, but only XP the hero collects. **XP a script hands out isn't scaled.** delta4o asked to check the Guild tutorial: it gives exactly 350 Will XP and won't continue until the hero buys a spell. The cheapest, Force Push, costs 300, so with the multiplier Medium, Hard and Hardcore would get stuck (297, 245 and 227 XP). Only that tutorial hands out XP from a script, so it now plays exactly as in the original on every level (technical.md 17.1).
- **Default:** the Easy card is selected when the New Game screen opens, and an existing save without a difficulty counts as Easy, so old saves play exactly as before.

**5. How much to log in the next play session** (asked 2026-11-21). Options:
  - **the five difficulty checks plus extras** (gold, health, saves, New Game, slot cards);
  - the five difficulty checks only.

**Choice: the checks plus extras.** delta4o also suggested the shout-out shop test (above).

## Open questions

1. ~~Medium's subtitle~~: answered 2026-11-20, "Hero of Albion".
2. ~~"Archon of their time": Hard or Hardcore?~~: answered 2026-11-20. Hard is "GuildMaster" and Hardcore "Archon's Heir". Check the game's own Guildmaster spelling.
3. **Slot label:** "Hero 1 (Easy)" on the same line, or a new line? The same line is much cheaper (see above).
4. ~~**Starting multiplier values**~~: decided 2026-11-22, see decision 6.
5. **Masochist achievement:** should Hard or Hardcore count as the game's "above Normal" level, so it can unlock? (Since decision 6, Easy is the original game, so Medium and up map naturally to the game's "above Normal".) And if difficulty changes mid-game, should it stop counting, as the original does?
6. ~~**XP**~~: decided 2026-11-22, scaled 1.0 / 0.85 / 0.7 / 0.65 (decision 6).
7. **Theresa's unused lines:** use any of them on the difficulty screen?
8. ~~**Property prices**~~: decided 2026-11-22, yes, buying and selling (decision 6). The gold-gain rule skips property sales.
9. ~~**Which gold gains scale**~~: decided 2026-11-22, earned gold but not sales (decision 6). Script-paid gold is all earned gold, so no script changes are needed.

## Proposed approach (cheapest first, not started)

1. ~~**Read the game's own multiplier values** and test the hero's level above Normal~~: done 2026-11-22. All 1.0; the checks fire on the hero for damage and XP, but not for prices.
2. **Enemy damage and XP** through the game's own checks, with our values per level. Easy is the game's level 1, where the checks don't run, so it stays the original game by itself. **Prices** need their own hook (shop items and property).
3. **Gold gained:** one hook on the gold function, only for the hero's earned gains (not sales, transfers, refunds or network copies).
4. **Enemy health:** a hook on the health function, or each enemy's own multiplier set when it spawns.
5. **Hardcore saving:** remove the two Save-box options, force a save on every quit, and block the reload prompt.
6. **Store the level** with the save: the game's own per-hero level, or a small file of our own next to (not inside) the save folder.
7. **Screens:**
   - the difficulty cards between the gender choice and the start of the game, with Theresa's "destiny" line moved to after the choice;
   - the pause-menu entry;
   - the slot label suffix.
