# Cut and unused content: technical notes

**Status:** no commit yet (research only) · **Commits:** none · **Last checked against:** 7863041

Claims are labelled **Verified** (checked in this repo or a primary source), **Reported** (a source says so, not checked here) or **Inference** (reasoned, not tested). This came out of the [November 2026 research](../../reference/research/README.md).

## Goal

Find which content cut from Fable II is still in the retail data, and whether any of it can be restored without distributing assets.

**Method caveat (Reported):** public documentation of Fable II's retail-data leftovers is thin. TCRF's Fable II page lists only two data-backed items: the Singing Sword cut quest (identified by file names) and Pub Games-exclusive rewards.\[1\]\[2\] Most other "cut" material (rapiers, bows, early UI, Banshee and Nightmare concepts, the Dunecrest name for Rookridge) is evidenced by concept art, guides and footage, not by retail data.\[3\] TCRF's page is short and unsourced in places.

**Nothing here may be distributed.** Any restoration must read the player's own `data/` files (for example `gamescripts.bnk`, art and audio banks) at runtime. See [legal_and_distribution](../legal_and_distribution/technical.md).

## What the repo already knows

`FABLE2_LUA_API.md` (repo root) is a catalogue of every Lua name the game's scripts use. It was built from all three script banks (`gamescripts.bnk`, `gamescripts_r.bnk`, `guiscripts.bnk`, 1,139 decompiled files) plus the loose `Startup` scripts, cross-checked against the recompiled engine's registration tables (its section 7 describes the method). It holds names only, no script code. Several leads show up in it.

| Lead | What the catalogue shows | Label |
|---|---|---|
| Singing Sword | A family of `QO390_*` quest tables in the in-game Lua state, each defined in one shipped script file: `QO390_SingingSword`, `QO390_Esmerelda`, `QO390_EsmereldaInCave`, `QO390_JulianSword`, `QO390_MegaChicken`, four `QO390_DirectionalStatue*` and four `QO390_Statue0*` tables. The quest-thread tables set `Init`, `Update` and `OnExit`. No shipped script calls their members (0 calls). | **Verified** (catalogue, section 2) |
| Dunecrest | `QO200_DunecrestInn`, `QO900_DemonDoorDunecrest` and the script table `DunecrestCommunityService` exist in the in-game state. | **Verified** (catalogue, sections 2 and 5) |
| Pub Games rewards | `Debug.AddAllCarbonatedPrizes(entity)` is defined in the game's own Lua (`scripts\miscellaneous\weaponinventory.lua`, per `docs/reference/lua_call_shapes.md`) and called once. `StartPubGame` is an engine global, `VillageCrimeManager.CheckPubGameDebts` is called once, and `AddDebugPubItems` is registered by the engine but never called by any shipped script. | **Verified** (catalogue, sections 1, 2 and 4); "Carbonated" being the Pub Games' internal name is **Inference** |
| Website and CE items | `PlayerWebsiteUnlocks.IsItemUnlocked` (8 calls, 1 file) and `Stats.IsCollectorsEdition` (3 calls, 3 files) gate items in the scripts. | **Verified** (catalogue) |
| Unused engine functions | Section 4 lists 65 engine functions that no shipped script calls, mostly debug, console and editor helpers (for example `AddDebugPubItems`, `DisplayFastTravelScreen`, `SetMinimapEnabled`, `ShowAssertScreen`, plus weapon-type names such as `Bow`, `Crossbow`, `Pistol` and `Rifle`). | **Verified** (catalogue, section 4); whether `Bow` points to cut bows is **Inference** and unlikely, since it sits next to the other weapon types |

The catalogue proves that **scripts** for the Singing Sword quest shipped, not just file names. It doesn't show whether the quest is ever loaded, whether its level, characters and dialogue lines exist, or whether it runs.

### Website and Collectors Edition unlocks (already shipped)

**Verified** ([guest_patches](../guest_patches/technical.md), `config/fable2_config.toml`, `fable_2_manifest.toml`):

- `[patches] unlock_website` (default `true`) hooks the Guild-chest website-item getter `GuildChest_GetWebsiteItem_8256E368`. It forces both "registered on fable2.com" checks and the grant method's result to 1, so the website items are granted into the Guild chest when a save loads. Its port of the Xenia "Unlock Website Items" patch was re-derived for this build.
- `[patches] unlock_ce` (default `true`) does the same in the Collectors Edition getter `GuildChest_GetCEContent_824B3528`. The hook is confirmed in the generated code; the in-game chest unlock is still marked as needing a manual test.
- The chest click runs `GuildChest_ProcessItems_822DE008` → `GuildChest_ProcessItem_82228830` per item.

So the website unlocks (the research's U8) are already restored from the player's own data, and part of lead 5 is covered. Which exact items each getter grants isn't recorded in the repo. Whether the Hall of the Dead dungeon depends on the same CE check or on a separate token isn't known (**Inference:** the three `Stats.IsCollectorsEdition` call sites are the place to look).

## Findings: leads, ranked

| Rank | Item | Evidence it exists in retail data | Completeness | Restoration feasibility |
|---|---|---|---|---|
| 1 | **Singing Sword cut quest** (quest-giver Esmeralda; a "mega chicken" encounter; keep-or-give-up choice) | TCRF says it is "buried within the Fable II files", identified "according to the file names".\[2\] A YouTube "Fable II Cut Content: The Singing Sword" video exists.\[4\] **Verified:** the `QO390_*` quest tables ship in the in-game scripts (see above). | Scripts exist. Levels, characters and dialogue are unknown. | A quest start from the console or the F5 runner is plausible if the quest module still loads. If its world content is missing, it needs new content (out of scope). |
| 2 | **Pub Games tournament rewards** (15 unique items; the Fable Wiki lists the Championship Pistol from Fortune's Tower, the Championship Cutlass from Keystone, the Highroller's Coat, the Plaits Hairstyle Card, Tribal Warrior Face and Body Tattoos, the Mysterious Ring, Dog Training: Backflip, and Potions of Will, Strength and Skill; achievement "I Want It All") | TCRF: "unless it already existed in the game files", which is explicitly unconfirmed.\[1\]\[5\] **Verified:** the game's own debug helper `Debug.AddAllCarbonatedPrizes` exists, and the console's `giveall prizes` calls it. | Items were granted via a cross-title save flag. The flag itself is lost. | `giveall prizes` in a loaded game is a one-command test (not yet run). |
| 3 | **Unused areas / characters / objects** | TCRF category tags only, no details.\[1\]\[2\] | Unknown | Level and asset survey needed. `warp level` can load any region by name. |
| 4 | **Lua-level debug/unused functions in the game's own scripts** | Fable2Modding decompiles the three script `.bnk` files; Keshire (se7ensins) noted the free DLC packs contain "overriding startup script directories".\[6\]\[7\] **Verified:** `FABLE2_LUA_API.md` section 4 lists 65 engine functions no script calls. | Real code | High: the recomp already runs arbitrary Lua (console `lua`, F5 runner).\[8\] |
| 5 | **Hall of the Dead dungeon, Wreckager, Hal's armour** (pre-order/LCE bonus) | Unlocked by DLC/token; the token unlock still exists as paid DLC since 2014.\[9\] **Verified:** `unlock_ce` already forces the Collectors Edition Guild-chest check. | Complete content | Chest items: likely covered by `unlock_ce` (in-game test pending). The dungeon: unknown. No distribution either way. |
| 6 | **Cut combat mechanics, rapiers, bows, elaborate early UI** | TCRF *prerelease* page (concept art, dev-diary footage)\[3\] | Concept-level | Check item tables for leftover entries. "Rapier" doesn't appear in the Lua catalogue (**Verified**). Otherwise not recoverable. |
| 7 | **Dunecrest (Rookridge's earlier name), beta maps, Rose's earlier look** | Fandom/Unseen64 images and the Limited Edition guide\[10\] **Verified:** "Dunecrest" survives in three shipped script table names. | The name survived; maps and looks are pre-release only | Confirmed as a leftover name. Whether it was a separate area or just an old name is **Inference** (most likely an old name). |
| 8 | **Fishing** (pre-release idea) | No retail-data evidence. Archon's Toolbox implements a new fishing job.\[11\] | Fan-made | Optional mod, not restoration. |
| 9 | **Unused Fable II assets in Fable III** | Fandom says these sit in *Fable III*'s files\[10\] | — | Not usable: wrong game and asset distribution. |

**Excluded because they'd need assets distributed:** item 9, any part of item 5 not on the player's disc, and any fable2.com web unlock missing from the disc.

## Tooling available

All of these read the player's own files; none ship game data. **Verified** (read in the repo):

| Tool | What it does | Use for |
|---|---|---|
| `tools/x360extract/` | C# CLI (and library shared with the launcher) that reads an Xbox 360 disc image directly: `--list` prints the GDFX tree, the default extracts every file, `--unpack-stfs` expands STFS packages. | Getting `data/` from your own disc image, and listing what's on it. |
| `tools/bnk.lua` | Despite its extension, a Python script. Reads `.bnk` banks: `list <bnk>`, `extract <bnk> <name-substr> <outdir>`, `replace <bnk> <name-substr> <payload>`. | Listing and extracting script and art bank entries by name. |
| `tools/lua_callshapes.py` | Walks the Lua 5.1 bytecode in the banks and records argument counts for every named call. Output: `docs/reference/lua_call_shapes.md`. | Learning how to call a quest or debug function correctly. |
| `tools/xex_image.py` | Decrypts and expands `default.xex` into the raw guest image (base 0x82000000). Needs `pycryptodome`. | Input for the two tools below. |
| `tools/image_refs.py` | Finds strings in the guest image and every reference to them (data pointers in registration tables, `lis`/`addi` pairs in code). | Finding the native side of a Lua name. |
| `tools/image_dis.py` | Disassembles a range of the guest image with resolved constants. Needs `capstone`. | Reading the code around a reference. |
| `tools/texture_export/` | Exports original and edited PNGs of the GUI textures the keycap prompts change. | Not for cut content; a model for a read-only asset viewer. |

In game (console, [console_commands](../console_commands/technical.md)): `find <text>` searches global and `QuestManager.`/`Debug.` names; `lua <code>` runs Lua; `level` prints the loaded level; `warp level <region> [marker] [world]` calls `Debug.LoadLevel`; `giveall <category>` and `weapon <record>` add items; `spawn`/`creatures` create entities; `f5` runs the F5 menu (`src/lua/F5.lua`, which lists scripts staged from `src/lua/` into `data/scripts/recomp/`). `tools/fable2_control.py` drives the debug-build remote-control server for repeatable runs.

**Caveat (Verified, `CLAUDE.md`):** host scripts must never be registered with `GeneralScriptManager`, because it writes them into saves. **Inference:** `QuestManager.AddQuestToPermanentsTables` and similar calls will also end up in the save, so test quest experiments on a throwaway save.

## Design (proposed investigation steps)

1. **Singing Sword.**
   - `bnk.lua list` on your `gamescripts.bnk`, then look for the entry that defines the `QO390_*` tables. Read it locally only; never commit it.
   - In a loaded game: `find QO390` to see whether the tables are loaded at runtime. Then read the `QuestManager.LoadedQuestModules` and `QuestManager.CurrentlyRunningQuests` tables with `lua` to see whether the quest module is registered. `docs/reference/lua_call_shapes.md` lists none of `LoadQuestModule`, `GetLoadedQuestModules` or `GetQuestInstanceWithName`, so read their native bodies (via `image_refs.py`) before calling them.
   - Check whether the world content exists: `bnk.lua list` on the level and dialogue banks for matching names, and `spawn` the characters' records, if the scripts name any.
2. **Pub Games items.** On a throwaway save, run `giveall prizes` and compare the inventory with the 15-item list. If nothing appears, look up `AddDebugPubItems` with `image_refs.py` to find its native body.
3. **Orphaned Lua.** Compare `FABLE2_LUA_API.md` section 5 (script-defined API) against the call counts: tables and functions with 0 calls are candidates. Section 4 already covers the engine side.
4. **Unused areas.** List the level names in the level banks (`bnk.lua list`) and compare them with the regions the console's `travel` presets and `warp level` reach.
5. **Collectors Edition.** Do the pending in-game test of `unlock_ce`, then find the three `Stats.IsCollectorsEdition` call sites to see what else the flag gates.

Mapping the quest/script manager (QuestManager natives) helps with all of these ([priority list](../function_mapping/technical.md#prioritised-systems-to-map-next)). Report findings without pasting game text, scripts or asset data; paraphrase instead.

## What changed

Nothing. This is research only.

## Verification

- **Verified in this repo:** the `FABLE2_LUA_API.md` and `docs/reference/lua_call_shapes.md` entries above; the tools' usage lines in their headers and READMEs; the `unlock_website`/`unlock_ce` keys and defaults in `config/fable2_config.toml`; the four `GuildChest_*` names in `fable_2_manifest.toml`; the console commands in [console_commands](../console_commands/technical.md).

## Not tested

- No retail data was searched for this doc and no command was run in game. The catalogue facts come from a past analysis of the banks.
- Whether the `QO390_*` quest loads, and what `giveall prizes` gives, are open.
- `unlock_ce` hasn't been tested in game.

## Sources

1. [Fable II - The Cutting Room Floor](https://tcrf.net/Fable_II)
2. [Fable II - The Cutting Room Floor](https://tcrf.net/Fable_2)
3. [Prerelease:Fable II - The Cutting Room Floor](https://tcrf.net/Prerelease:Fable_II)
4. [Fable II Cut Content: The Singing Sword - YouTube](https://www.youtube.com/watch?v=DXphnOWLhOY)
5. [Fable II Pub Games](https://en.wikipedia.org/wiki/Fable_II_Pub_Games)
6. [GitHub - JustSomeGuy1234/Fable2Modding: Scripts and information on modding Fable 2 for the Xbox 360, mostly the game's Lua implementation. · GitHub](https://github.com/JustSomeGuy1234/Fable2Modding)
7. [Fable 2 LUA Scripts](https://www.se7ensins.com/forums/threads/fable-2-lua-scripts.264461/)
8. [GitHub - himdo/Fable-2-Recomp · GitHub](https://github.com/himdo/Fable-2-Recomp)
9. [Fable II Limited Collector's Edition](https://fable.fandom.com/wiki/Fable_II_Limited_Collector's_Edition)
10. [Cut Content](https://fable.fandom.com/wiki/Cut_Content)
11. [GitHub - JustSomeGuy1234/Archons-Toolbox: A Mod Manager for Fable 2 Lua Mods · GitHub](https://github.com/JustSomeGuy1234/Archons-Toolbox)
