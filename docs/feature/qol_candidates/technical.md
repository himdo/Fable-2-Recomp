# Quality-of-life candidates: technical notes

**Status:** no commit yet (research only) · **Commits:** none · **Last checked against:** c2a9d12

Claims are labelled **Verified** (checked in this repo or a primary source), **Reported** (a source says so, not checked here) or **Inference** (reasoned, not tested). This came out of the [November 2026 research](../../reference/research/README.md).

## Goal

Sort the [player complaints](../player_complaints/technical.md) into fixes a recompilation can reasonably make and ideas that are out of scope. "Reasonable" means it fits inside the recomp's code and Lua, without new content, new voice acting or shipped assets. Effort estimates are **Inference**.

"Binding" below means a Lua function listed in `FABLE2_LUA_API.md` (repo root), so it exists in the game. "Console" means a command in `src/lua/console_cmds.lua` ([console_commands](../console_commands/technical.md)). Names from the branch `feature/game-difficulty` were read there (read-only) and **aren't in master's manifest yet**.

## Design (proposed)

### Summary

| # | Feature / fix | Effort | Where it stands |
| --- | --- | --- | --- |
| R1 | Inventory/shop menu latency fix | Medium | Partly mitigated by `gui_every_hf_tick` and `high_tick_rate`; the cause isn't found |
| R2 | "Cancel" that never casts a spell (KB/M and pad option) | Small | Not started; the launcher can already rebind keys |
| R3 | Expression wheel usable with KB/M (#236) and a hold-timing fix (#216) | Small–Medium | Being worked on; console workarounds exist |
| R4 | Map markers for owned houses, family members and shops | Medium | Not started; map and breadcrumb bindings exist |
| R5 | Property management menu (collect/set rent from anywhere) (#211) | Medium | Not started; the rent and property code is mapped on the difficulty branch |
| R6 | Difficulty tiers (Easy/Medium/Hard/Hardcore) | Medium | In progress on `feature/game-difficulty` |
| R7 | Optional knockout penalty (gold/XP loss) | Small | Not started; the knockout runs in the game's Lua |
| R8 | Optional block on the rent-via-system-clock exploit | Small | Not started; how rent reads the clock isn't known |
| R9 | Save backups, later more save slots | Small (backup) / Large (UI) | Not started; the save gate and files are mapped on the difficulty branch |
| R10 | Collectibles tracker (gargoyles, silver keys, Demon Doors) | Small–Medium | Not started; the game already counts some per region |
| R11 | Breadcrumb trail colour/visibility options | Small | Not started; the game has a full breadcrumb API |
| R12 | Achievements surfaced locally | Small (check only) | **Largely done by the SDK** (correction): overlay on F7, unlock pop-ups, saved unlocks |
| R13 | Pub Games reward items, *if they're in the retail data* | Small–Medium | Not started; an unused `AddDebugPubItems` engine function exists |
| R14 | Distance fog, LOD and pop-in options | Medium–Large | Console LOD switches exist; no fog control found |
| R15 | Per-system high-tick-rate correction | Medium | See [high_tick_rate_side_effects](../../fix/high_tick_rate_side_effects/technical.md) |
| R16 | Co-op camera: wider view / zoom-out for local co-op | Medium | Not started; camera zoom bindings exist |
| R17 | Optional potion auto-use / quick-slot | Medium | Not started |
| R18 | Known quest-bug fixes (Hammer follow in the Spire, invisible dog, Abbot quest) | Small–Medium each | Console workarounds exist |
| R19 | Fishing as a fourth job (opt-in mod, not core) | Medium | Archon's Toolbox has it in Lua\[2\] |

### What each item builds on

#### R1: menu latency

- **Verified:** `GuiManager_Update_82286B40` is the menu and HUD update, called from `GameFrame_HfTickUpdate_82278C90` and timed with `Timer_GetElapsed_822B6D70` (manifest; [function_naming](../function_naming/technical.md)).
- **Verified:** two options already raise the GUI's update rate. `[patches] gui_every_hf_tick` is on by default and moves HUD/subtitle updates from 15 to 30 Hz. `[patches] high_tick_rate` (off, marked unstable in the launcher) doubles the LF tick; its config comment says it "vastly improves in-game UI framerate and input delay" (`config/fable2_config.toml`).
- **Inference:** the complaint is lag that grows with the item count, which a higher tick rate doesn't remove. The next step is profiling an inventory screen with many items. The function-trace build can do that (`fable2-functrace.cmd`, README "function trace" section). The unused engine function `SetDrawGUIScreenNamesAndStates` (`FABLE2_LUA_API.md`, section 4) may show which GUI screen is active.

#### R2: cancel without casting

- **Verified:** the default `[input] keyboard_gamepad_map` sends both the middle mouse button and the B key to the pad's B (the spell button), and Escape to Pause. The mapping is fixed per key, with no context (`src/input/keyboard_gamepad.h`).
- **Verified:** the launcher's keybind editor can move these to other keys (`launcher/Fable2.Launcher/MainWindow.xaml`), but every key still maps to a single pad button.
- **Inference:** a context-aware remap needs a "menu or conversation is open" signal. `src/core/fable2_ui_events.h` already records when each Gameface UI event last fired, which could provide one. A pad option would mean rewriting B in the host pad layer (`src/input/host_pad.h`) while that signal is set.

#### R3: expressions

- **Verified (console):** `expressions all|extend|succeed` unlocks every expression, makes them extendable, or forces extended expressions to succeed (`Debug.SetForceExtendedExpressionsToSucceed`). `pose <expression>` performs any expression through `ExpressionPerformer.PerformExpression`, which photo mode's pose cards also use.
- **Verified (bindings):** `ExpressionPerformer` (19 members) and `Debug.SetDrawExpressionMeter`.
- **Inference:** a keyboard-friendly wheel could be a host overlay that calls `ExpressionPerformer.PerformExpression`, as the pose cards do, instead of fixing the game's wheel.

#### R4: map markers

- **Verified (bindings):** `Player.SetMapScreenAsEnabled`, `Player.SetWorldMapHUDSuggestionAsValid` and `WorldMapInfo` (per-region chest and gargoyle counts). The breadcrumb can be pointed at any entity (`Breadcrumber.OverrideTargetEntity`, `Gameflow.SetBreadcrumbOnGiver`).
- **Verified:** `SetMinimapEnabled` and `DisplayFastTravelScreen` are registered in the engine but never called by a shipped script (`FABLE2_LUA_API.md`, section 4). **Inference:** a dormant minimap or fast-travel screen. Check first with `f2 dbg list Minimap`.
- **Inference:** "show me my house" could start as a console command that points the breadcrumb at the property entity, before any map UI work.

#### R5: property menu

- **Verified (bindings):** `PlayerProperties.PurchaseProperty`, `PurchasePropertyQuietly` and `GetIsRentalProperty`.
- **Verified (difficulty branch):** rent and income are paid by `Ownership_PayDueRent_828AEA50` and `Ownership_UpdateIncome_828AECD0`, which add the stored amount to the owner's gold once a game tick passes a stored due time. Buying and selling are `Ownership_BuyProperty_828AF6D0` and `Ownership_SellProperty_828B01C8`; `Ownership_OwnsProperty_828B0B38` is the "already owns it" check; prices are `Property_GetBuyPrice_8274DD78` and `Property_GetSellPrice_8274DFD0`. All gold goes through `MoneyComp_Modify_8239E698`.
- **Still generic:** the real-estate screen's handler is `sub_825986E0` (difficulty branch). It needs a name before a menu can reuse it.

#### R6: difficulty

- **Verified:** in progress on `feature/game-difficulty`: gold, prices, damage taken and XP scale with the level; New Game cards and an F8 picker; enemy health not yet. Fable III PC's Hardcore mode is the precedent; issue #83 shows demand.\[1\] See the branch's `docs/feature/difficulty_settings/`.

#### R7: knockout penalty

- **Verified (difficulty branch):** the knockout is handled in the game's Lua (`Hero_DeathTracker` in the gameflow scripts). It shows the XP lost, restores health, adds a scar and clears the resurrection flag. It never saves and doesn't touch gold. All health changes go through `HealthComp_Modify_8268BD90` (`Health.Modify`).
- **Verified (console):** `orbs`, `orbpickup`, `scar`, `cantdie` and `gold` already reach the pieces a penalty would use.
- **Inference:** a recomp Lua script can wrap the tracker's update and take gold through `Money.Modify` (which goes through `MoneyComp_Modify_8239E698`), as Archon's Toolbox "Death Matters" does.\[2\] Recomp scripts must not register with `GeneralScriptManager` (`CLAUDE.md`).

#### R8: rent clock exploit

- **Verified (difficulty branch):** in-game rent is paid when the game tick passes a due time stored on the ownership component. **Verified in the generated code** (Windows, 26 Nov 2026): that "tick" is a counter at 0x8331950C that `GameWorldState_Update_821D43D8` adds 1 to on every world update, so rent counts world updates, not seconds (see [high_tick_rate_side_effects](../../fix/high_tick_rate_side_effects/technical.md)).
- **Verified (binding):** `Timing.GetRealWorldHour` exists; the console's `clock` prints frames, tick rate, day, time of day and real-world time.
- **Inference:** the exploit (changing the system clock to collect rent) needs a real-time read on load or resume, which hasn't been found. In the recomp the guest's clock is the PC's clock as passed through the SDK kernel, so a fix would either clamp that clock for the game or cap the payout in the rent functions. Find the real-time read first.

#### R9: save backups and slots

- **Verified:** saves live in `saves/` next to the exe (`src/core/fable_2_app.h`, `OnConfigurePaths`: `user_data_root = exe_dir / "saves"`). The README tells players to back up that folder. No backup code exists.
- **Verified (difficulty branch):** every save goes through `SaveManager_Save_824E8990`, with the Lua bindings `LuaSaveManager_SaveGame_824E8978` and `LuaSaveManager_AutoSaveGame_824E8D28`. One slot is a `Hero<NNN>` folder holding `mainsave.bin`, `herosave.bin`, `chaptersave.bin`, `saveuid.bin` and `texturemorphs.bin`, plus `failquestsave.bin` once a quest has been failed (**Verified** in local saves on Windows; no file there is a separate header). The SDK may recreate the whole folder on each save.
- **Inference:** a backup is a host-side copy of `saves/` triggered after a save finishes. It can't happen inside a hook, because hooks run on guest threads and must not do file I/O (`CLAUDE.md`). An in-game slot picker is larger and needs the front-end save screens mapped.

#### R10: collectibles tracker

- **Verified (bindings):** `WorldMapInfo.EnableGargoyles`, `IncrementGargoylesKilled`, `AddExtraChestCountForRegion`, `Inventory.SetSilverKeysToAddToRegionTotal`, `Chest.GetNumberOfSilverKeysNeeded` and `Gameflow.DemonDoorCompleteTable`.
- **Verified (console):** `stats`, `trackedstat list` and `title list` print the hero's tracked stats and titles.
- **Inference:** the world map already keeps per-region counts, so a tracker can read the same state from Lua in the console or the F5 runner.\[3\] Only setters show up in the shipped scripts, so the read side may need the stored tables rather than a getter.

#### R11: breadcrumb options

- **Verified (bindings):** `Breadcrumber` has 34 members, including `SetDrawTrail`, `SetTrailLength`, `SetParticlesPerMetre`, `SetParticleAlphaMultiplier`, per-mode brightness and length setters, and `SetMinLightingMult`.
- **Inference:** options are a small recomp Lua script that sets these after the game's own scripts do. No colour setter is listed; colour may be in the particle data rather than Lua.

#### R12: achievements (correction)

- **Verified (SDK at the pinned commit):** the SDK catches the game's `XGIUserWriteAchievements` calls and unlocks the achievement host-side (`src/kernel/xam/apps/xgi_app.cpp`). It reads the achievement list from the game's own title data, saves unlocks to `saves/achievements/4D5307F1.toml` (`src/system/kernel_state.cpp`), shows a pop-up on unlock and has an overlay toggled with **F7** (`bind_achievements`, `src/ui/rex_app.cpp`).
- **Verified:** the commented-out lines in `src/core/fable_2_app.h` are optional overrides that would replace the SDK's overlay and pop-up. Fable 2 uses the SDK's defaults.
- **Reported (difficulty branch, seen in game 2026-11-24):** F7 opens the SDK's achievements overlay; the branch moved its own picker to F8 for that reason.
- **Remaining:** check in game that unlocks are recorded and survive a restart, then document the key. `Stats.UnlockAchievement` and `Stats.UnlockDLCAchievement` are the game's Lua side.

#### R13: Pub Games items

- **Verified:** `AddDebugPubItems` is registered in the engine but never called by a shipped script (`FABLE2_LUA_API.md`, section 4).
- **Verified (difficulty branch):** pub-game winnings are paid by `PubGame_PayResult_825ABDC0`. A `Fable2PubInfo.xml` (UTF-16) sits in the save slot, but not in every one: in four local slots on Windows only `Hero000` had it. Its character record has `Name`, `GoldBalance`, `Debt`, `Rating`, `BestTournamentRank` and `SharedInventoryMask` fields.
- **Inference:** `SharedInventoryMask` in `Fable2PubInfo.xml` is probably where the cross-title Pub Games prizes were recorded. The 15 prize item records are on the disc (see [cut_content](../cut_content/technical.md)). `f2 dbg list Pub` would show whether `AddDebugPubItems` is a `Debug` function; calling it would answer whether the items are on disc. See [cut_content](../cut_content/technical.md).\[4\]

#### R14: fog, LOD and pop-in

- **Verified (console):** `lod low|full|off` (`Debug.ForceLowLoDLevel`, `Debug.SetFullLODForAllCreatures`), `vis lod` (LOD visualisation) and `canopy` (tree canopy fading).
- **Verified (bindings):** `Debug.SetLimitCharacterDistance`, `Debug.UseLODCheck`, `Debug.EnableLoDLevelVis`.
- **Inference:** creature LOD can be a Lua option today. No fog or draw-distance binding was found, so fog and world pop-in need renderer functions mapped. None are named yet.

#### R15: high tick rate

See [high_tick_rate_side_effects](../../fix/high_tick_rate_side_effects/technical.md).

#### R16: co-op camera

- **Verified (bindings and console):** `Camera.SetZoomLevel` and `Camera.GetZoomLevel`, which the console's `zoom [level]` calls; `fov [degrees]`; `reloadcam` reloads the camera scripts. The co-op bindings are `Multiplayer.SetHenchmanGhostMode`, `SetHenchmanHiddenMode` and `DisplayCoopScreen`.
- **Inference:** trying `f2 zoom` during local co-op is the cheapest test of a wider view. `[input] camera_distance` is only the mouse-orbit radius and doesn't help here.

#### R17: potions

- **Verified (console):** `give <itemtype>`, `have`, `take` and `giveall potions` (`Debug.AddAllPotions`) work on the inventory.
- **Inference:** auto-use needs a "health is low" check (`hp` reads health) and an item-use call. No item-use binding was identified here.

#### R18: quest bugs

- **Verified (console):** workarounds for stuck or missing companions exist: `dog here`, `bring <who>`, `follow`, `gotoent`, `tp`, `up` and `snap`.
- **Inference:** a real fix needs the quest manager mapped (QuestManager natives), item 6 on the [mapping priority list](../function_mapping/technical.md#prioritised-systems-to-map-next).

#### R19: fishing

Archon's Toolbox already implements it in Lua.\[2\] Porting it would be an optional recomp script.

### Out of scope

| # | Idea | Why not | Effort if attempted |
| --- | --- | --- | --- |
| U1 | Co-op as your own hero with independent cameras / split screen | Needs a second render view and a rework of the character/save model. Engine-level. | Large |
| U2 | Reviving Xbox Live online co-op | Network services are gone; replacement netcode is a project in itself. "Return to Albion" claims this, not reproduced.\[5\] | Large |
| U3 | New final boss fight / ending rewrite / killing Reaver | New content, animation and voice acting; changes authorial design. | Large |
| U4 | Family acknowledgement scene after the Love ending | New dialogue and voice. | Large |
| U5 | More weapons, spells and skill trees | New assets and balance. Better as optional mods than in core. | Large |
| U6 | Seafaring / sailing | Never built; no evidence in the data. | Large |
| U7 | Knothole: more quests | New content. | Large |
| U8 | Restoring the fable2.com web unlocks | **Already done**, not out of scope: the "Unlock Website Items" patch (`[patches] unlock_website`, on by default) grants them from the player's own copy through a mid-asm hook in `GuildChest_GetWebsiteItem_8256E368` (**Verified** in `config/fable2_config.toml` and [guest_patches](../guest_patches/technical.md)). "Unlock CE Content" (`[patches] unlock_ce`, `GuildChest_GetCEContent_824B3528`) does the same for the Collectors Edition items and still needs an in-game check. Shipping files stays forbidden. | none |
| U9 | Removing the breadcrumb, map or expression systems entirely | Breaks design. Options are fine, removal is not. | — |
| U10 | Fable III content or assets in Fable II | Asset distribution and a different game. | n/a |

### What to map first

Most reasonable items need a subsystem mapped first ([priority list](../function_mapping/technical.md#prioritised-systems-to-map-next)):

- GUI/inventory for R1 and R2;
- economy and property for R5 and R8, and combat for R7 (the difficulty branch has already named the gold, shop, property, damage-taken and save functions, which R5, R7, R8 and R9 can reuse once it merges);
- the save system's front end for R9's slot picker;
- the quest manager for R13 and R18;
- renderer LOD and fog for R14.

R11 and R12 need no mapping: R11 is Lua over existing bindings, and R12 is mostly done by the SDK.

## What changed

Nothing in the code. This is a backlog, not agreed with the maintainer.

## Verification

- **Verified in this repo:** the config keys and their comments in `config/fable2_config.toml`; the launcher's settings and keybind editor (`launcher/Fable2.Launcher/`); the console commands in `src/lua/console_cmds.lua`; the bindings in `FABLE2_LUA_API.md`; the save path in `src/core/fable_2_app.h`; the achievement code in `thirdparty/rexglue-sdk` at the pinned commit 22ef700.
- **Read-only from `origin/feature/game-difficulty`** (at bad29df): the rent, property, gold, health, knockout and save findings, and the function names from that branch.

## Not tested

- None of the items were prototyped. Effort estimates are guesses.
- The unused engine functions (`SetMinimapEnabled`, `DisplayFastTravelScreen`, `AddDebugPubItems`, `SetDrawGUIScreenNamesAndStates`) weren't called; their arguments and effects are unknown.
- The SDK achievement unlocks weren't checked in game from this branch.

## Sources

1. [Archons Toolbox Support · Issue #83 · himdo/Fable-2-Recomp · GitHub](https://github.com/himdo/Fable-2-Recomp/issues/83)
2. [GitHub - JustSomeGuy1234/Archons-Toolbox: A Mod Manager for Fable 2 Lua Mods · GitHub](https://github.com/JustSomeGuy1234/Archons-Toolbox)
3. [GitHub - himdo/Fable-2-Recomp · GitHub](https://github.com/himdo/Fable-2-Recomp)
4. [Fable II - The Cutting Room Floor](https://tcrf.net/Fable_2)
5. [GitHub - Fable2Recomp/Fable-Return-to-Albion: A Fable 2 & 3 Remake · GitHub](https://github.com/Fable2Recomp/Fable-Return-to-Albion)
