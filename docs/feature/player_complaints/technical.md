# Fable II player complaints: technical notes

**Status:** no commit yet (research only) · **Commits:** none · **Last checked against:** 7863041

Claims are labelled **Verified** (checked in this repo or a primary source), **Reported** (a source says so, not checked here) or **Inference** (reasoned, not tested). This came out of the [November 2026 research](../../reference/research/README.md).

## Goal

Collect what players complained about in 2008–2009 reviews and forums, and what the recomp already covers, so new work targets real pain points. Which complaints are worth fixing is in [qol_candidates](../qol_candidates/technical.md). All quotes below are **Reported** by the cited source. Each section ends with what the recomp's code already has for it ("In the recomp").

**Inference:** complaints converge on interface latency and navigation, not on how much content there is.

Two sources are used throughout:

- `FABLE2_LUA_API.md` (repo root): every Lua function the game's scripts call, extracted from the three script banks and cross-checked against the engine's registration tables. A binding listed there exists in the game. What it does is **Inference** from its name unless a script or the code says more.
- `src/lua/console_cmds.lua`: the `f2` developer console ([console_commands](../console_commands/technical.md)). Its commands are quick workarounds today and ready-made Lua for future features.

## Findings

### Inventory and menus

- The inventory "seems to get bogged down once you've collected a fair amount of items", and managing or using items is "cumbersome" (Giant Bomb, Vinny Caravella, 21 Oct 2008).\[1\]
- "Every screen seems to take longer than it should, making for a tedious inventory and skill system" (mikeshea.net, 26 Oct 2008).\[2\]
- "The menus lag badly and weren't given enough thought" (videolamer).\[3\] RPGFan also reported sluggish menus.\[4\]
- Players report slow inventory menus in the recomp too ([player_reports](../../fix/player_reports/technical.md)).

**In the recomp:**

- **Verified:** `[patches] gui_every_hf_tick` (on by default, also in the launcher) updates subtitles and HUD text on every HF tick (30 Hz) instead of every LF tick (15 Hz). `GuiManager_Update_82286B40` times itself with `Timer_GetElapsed_822B6D70`, so the speed doesn't change, only the update rate (`config/fable2_config.toml`, [guest_patches](../guest_patches/technical.md)).
- **Verified:** `[patches] high_tick_rate` (off by default, marked unstable in the launcher) doubles the LF tick from 15 to 30 Hz. Its config comment says it "vastly improves in-game UI framerate and input delay". It has side effects ([high_tick_rate_side_effects](../../fix/high_tick_rate_side_effects/technical.md)).
- **Inference:** neither option changes how much work an inventory screen does, so the lag that grows with the number of items is still unexplained (R1 in [qol_candidates](../qol_candidates/technical.md)).

### Navigation

- No overall world map, so regions feel disconnected (DarkZero).\[5\]
- "The map is useless except for a vague general overview" (buttonmashing).\[6\]
- Finding "shops, family members and even your own house can be a chore" (Giant Bomb).\[1\]

**In the recomp:**

- **Verified (bindings in `FABLE2_LUA_API.md`):** the game's map is scriptable. `Player.SetMapScreenAsEnabled`, `Player.SetWorldMapHUDSuggestionAsValid` and the `WorldMapInfo` table (per-region gargoyle and chest counts) are called by the shipped scripts.
- **Verified:** `SetMinimapEnabled` and `DisplayFastTravelScreen` are registered in the engine but no shipped script calls them (`FABLE2_LUA_API.md`, section 4). **Inference:** a minimap and a fast-travel screen may be partly built. Which table they hang off, and whether they work, is unknown; `f2 dbg list Minimap` would be the first check.
- **Verified:** the console's `travel <place>`, `warp`, `gotoent <name>` and `tp <who>` move the hero anywhere today. They are debug tools, not a map fix.

### Controls

- B cancels in most games, but in Fable II it can fire a spell, which caused accidental town-burning (buttonmashing).\[6\]
- The expression menu is "counter-intuitive" (same source).\[6\] Issue #236 reports the expression wheel is hard to select with keyboard and mouse.\[7\]

**In the recomp:**

- **Verified:** the default keyboard map sends both the middle mouse button and the B key to the pad's B, and Escape to Pause (`[input] keyboard_gamepad_map` in `config/fable2_config.toml`). There is no per-context remap, so on keyboard and mouse the cancel key is still the spell key. The launcher has a keybind editor (`launcher/Fable2.Launcher/MainWindow.xaml`), so players can move B to another key, but not split cancel from casting.
- **Verified:** for expressions, the console has `expressions all|extend|succeed` (unlock all, make all extendable, force extended expressions to succeed) and `pose <expression>`, which photo mode's pose cards also use (`src/lua/console_cmds.lua`). `Debug.SetDrawExpressionMeter` exists as a binding.

### Co-op

- Online co-op was not working at launch. On 29 Sep 2008 TechCrunch reported that "Lionhead says it should have the mode up and running within a week of the game's release" (no link recorded).
- The host controls the camera, which hurts the henchman's aim (Critical Hits).\[8\]
- The left bumper only recentres on the main player (Co-Optimus).\[9\]
- Co-op achievements force achievement hunters into it (RPGFan).\[4\]

**In the recomp:**

- **Verified (bindings):** `Multiplayer.SetHenchmanGhostMode`, `SetHenchmanHiddenMode`, `IsHenchmanInGhostMode` and `DisplayCoopScreen` are the co-op hooks the scripts use. `Camera.SetZoomLevel` and `Camera.GetZoomLevel` exist, and the console's `zoom [level]` and `fov [degrees]` already call them.
- **Inference:** the shared camera's framing is script-driven (the console's `reloadcam` reloads the camera scripts), so a wider co-op view may be a Lua change. Not tried.
- Local co-op itself wasn't checked in the recomp.

### Depth and difficulty

- Few weapons, skills and spells; "after just one play through your character can already do everything"; jobs reuse "the same mini-game mechanic as social interaction" (videolamer).\[3\]
- "Still way too easy" (Critical Hits).\[8\]

**In the recomp:**

- **Verified:** difficulty settings are in progress on the branch `feature/game-difficulty`, which is further along than issue #214 (see its `docs/feature/difficulty_settings/`).
- **Verified:** the console's `damage <who> <multiplier>` (`Combat.SetDamageMultiplier`), `maxhp` and `regen` change combat balance by hand today.

### Death and consequence

Being knocked out only costs uncollected experience orbs and leaves a scar. DarkZero called this the "biggest failure, a lack of consequence".\[5\]

**In the recomp:**

- **Verified (difficulty branch, read-only):** the knockout is handled in the game's own Lua, in the `Hero_DeathTracker` update of the gameflow scripts. It shows the XP lost, restores health, adds a scar and clears the resurrection flag. It never saves or loads, and it doesn't touch gold.
- **Verified:** the game counts knockdowns (`Gameflow.FewestHeroKnockdowns`, `Gameflow.FewestHenchmanKnockdowns` in `FABLE2_LUA_API.md`).
- **Inference:** a harsher penalty fits in Lua next to that tracker, as Archon's Toolbox's "Death Matters" does (R7).

### The ending

- No final fight, and Reaver can't be punished (GameFAQs).\[10\]
- The Knothole Island DLC can revive the dog, which makes the "Love" wish feel pointless in hindsight (GameFAQs; Twenty Sided).\[10\]\[11\]
- Family revival brings no acknowledgement scene (GameFAQs).\[12\]
- The evil option's 1,000,000 gold payout is meaningless to players already rich from rent (Twenty Sided comments).\[13\]

**In the recomp:** nothing, and nothing planned. These need new content ([qol_candidates](../qol_candidates/technical.md), U3–U4).

### Knothole Island DLC

- Short: The Globe and Mail finished nearly everything in under three hours.\[14\]
- Dungeons are "rote", with too many flit switches (Joystiq).\[15\]
- No map on the start menu, and players must leave the island to get the next quest's details (Console Monster).\[16\]
- A single quest chain whose quest-giver reviewers found unlikeable (Silicon Sasquatch).\[17\]

**In the recomp:** nothing yet. Whether the recomp plays Knothole Island wasn't checked here.

### Bonus content

- fable2.com web unlocks (chicken suit, hero doll, pink dye) were lost on the original console. Only Hal's equipment survived, as paid DLC.\[18\]\[19\]\[20\]
- Pub Games tournament rewards became unobtainable after the Xbox 360 Store closed (TCRF).\[21\]\[22\] See [cut_content](../cut_content/technical.md).

**In the recomp (correction to the research):**

- **Verified:** `[patches] unlock_website` and `[patches] unlock_ce`, both **on by default** and in the launcher, force the website-registration and Collectors Edition gates and grant those items into the Guild chest at save load. They use mid-asm hooks in `GuildChest_GetWebsiteItem_8256E368` and `GuildChest_GetCEContent_824B3528` ([guest_patches](../guest_patches/technical.md)). The README lists "Guild chest fully unlocked".
- **Inference:** the website items are the fable2.com unlocks above. Which exact items appear wasn't checked here.
- **Not checked in game:** that the Collectors Edition unlock actually puts its items in the chest. It is on by default like the website unlock, but needs an in-game check before this doc calls it done.
- **Verified:** `AddDebugPubItems` is registered in the engine but no shipped script calls it (`FABLE2_LUA_API.md`, section 4). **Inference:** a debug path to the Pub Games items may exist (see R13).

### Already done in the recomp

- **Verified in the code:**
  - keyboard and mouse controls with a keybind editor in the launcher;
  - button prompts that switch live between controller icons and keyboard keycaps, following the last input device (`src/core/fable2_keycap_swap.h`, `src/gui/prompt_textures.h`; fixes in #191);
  - direct mouse camera ([mouse_camera](../mouse_camera/technical.md));
  - mouse aim and the stick swap (#183, merged, [mouse_aim](../mouse_aim/technical.md));
  - photo mode, on F6 by default (`[photo_mode]`, `src/core/fable2_photo_mode.h`; settings in the launcher);
  - a Quit item in the front-end main menu (`src/menu/main_menu.cpp`, `src/menu/menu_items.h`);
  - skipping the logo videos (`[patches] skip_intro_videos`, off by default, in the launcher);
  - VSync and the game and display FPS limits (the launcher writes the SDK's `vsync` cvar, `launcher/Fable2.Launcher/GraphicsSettings.cs`; [fps_cap](../../fix/fps_cap/technical.md));
  - the developer console ([console_commands](../console_commands/technical.md));
  - Hor+ widescreen ([fit_window](../fit_window/technical.md));
  - `disable_motion_blur`, `disable_msaa` and the tick-rate options ([guest_patches](../guest_patches/technical.md));
  - the fable2.com website items, back through `[patches] unlock_website` (above); the Collectors Edition unlock (`[patches] unlock_ce`) is also on by default but still needs an in-game check;
  - an achievements overlay and unlock pop-ups from the SDK (R12 in [qol_candidates](../qol_candidates/technical.md)).
- **Reported (1.5.0 release notes):** faster initial loading.\[23\] No commit for it was identified here.
- **In progress at the time of the research (Reported):**
  - difficulty settings (#214, sub-issues #217–#219; the branch `feature/game-difficulty` is further along than the issue, see its `docs/feature/difficulty_settings/`);
  - move while aiming;
  - an in-game settings menu;
  - blacksmithing/job-minigame and expression-hold timing (#216);
  - a property-management menu request (#211).\[7\]\[24\]
- **Verified:** the SDK's own settings menu (F4) can change some options in game, for example `disable_msaa` (comment in `src/core/fable_2_app.h`). A Fable-style in-game settings menu doesn't exist yet.

## What changed

Nothing in the code. This is research only.

## Not tested

- None of the 2008 complaints were reproduced in the recomp, apart from the slow inventory, which players report.
- The unused engine functions (`SetMinimapEnabled`, `DisplayFastTravelScreen`, `AddDebugPubItems`) weren't called. Their arguments and effects are unknown.
- Which website and Collectors Edition items the unlock patches grant wasn't listed here.

## Sources

1. [Fable II Review](https://giantbomb.com/reviews/fable-ii-review)
2. [Fable 2 and Buying Incomplete Unfinished Games : mikeshea.net](https://mikeshea.net/Fable_2_and_Buying_Incomp.html)
3. [Review](https://videolamer.com/review-fable-ii)
4. [Fable II Review](https://www.rpgfan.com/review/fable-ii/)
5. [Fable 2 Xbox 360 Review - DarkZero](https://darkzero.co.uk/game-reviews/fable-2-xbox-360/)
6. [Fable II Quirks - buttonmashing](https://buttonmashing.com/2008/10/24/fable-ii-quirks/)
7. [Issues · himdo/Fable-2-Recomp · GitHub](https://github.com/himdo/Fable-2-Recomp/issues)
8. [Review: Fable 2 — Critical Hits](https://critical-hits.com/blog/2008/10/27/review-fable-2/)
9. [Fable 2 Co-Op Review](https://www.co-optimus.com/ampfeature/92/fable-2-co-op-review.html)
10. [The Disappointing Ending (SPOILERS) - Fable II - GameFAQs](https://gamefaqs.gamespot.com/boards/927246-fable-ii/48102848)
11. [worst/most disturbing storyline ever \*spoiilers\* - Fable II](https://gamefaqs.gamespot.com/boards/927246-fable-ii/48595764)
12. [Was anyone disappointed with Fable 2's ending? (spoilers) - Fable II](https://gamefaqs.gamespot.com/boards/927246-fable-ii/47068641)
13. [Fable 2: Thematic Failures - Twenty Sided](https://www.shamusyoung.com/twentysidedtale/?p=2102)
14. [Knothole Island is fun, but also short and trivial - The Globe and Mail](https://www.theglobeandmail.com/technology/gaming/controller-freak/knothole-island-is-fun-but-also-short-and-trivial/article25683544/)
15. [Joystiq review: Fable 2's Knothole Island (DLC) - Engadget](https://www.engadget.com/2009-01-14-joystiq-review-fable-2s-knothole-island-dlc.html)
16. [Fable 2 Knothole Island DLC Review - Console Monster](https://www.consolemonster.com/news/fable-2-knothole-island-dlc-review/)
17. [Review: Fable II: Knothole Island DLC (XBL) — Silicon Sasquatch](https://www.siliconsasquatch.com/blog/2009/4/28/review-fable-ii-knothole-island-dlc-xbl)
18. [fable2.com how do I get the stuff? is it even still possible to get? I want it! - Fable II](https://gamefaqs.gamespot.com/boards/927246-fable-ii/80635992)
19. [Fable II Limited Collector's Edition](https://fable.fandom.com/wiki/Fable_II_Limited_Collector's_Edition)
20. [LF: Fable 2 and 3 Item/Maxed Weapons/Clothing (including DLC) Save(s)](https://www.xpgamesaves.com/threads/lf-fable-2-and-3-item-maxed-weapons-clothing-including-dlc-save-s.137895/)
21. [Fable II - The Cutting Room Floor](https://tcrf.net/Fable_II)
22. [Fable II - The Cutting Room Floor](https://tcrf.net/Fable_2)
23. [himdo/Fable-2-Recomp 1.5.0 on GitHub](https://newreleases.io/project/github/himdo/Fable-2-Recomp/release/1.5.0)
24. [Difficulty settings · Issue #214 · himdo/Fable-2-Recomp · GitHub](https://github.com/himdo/Fable-2-Recomp/issues/214)
