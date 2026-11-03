# Fable 2 Developer Console: Command Reference

Build: **console-v9**. Source: `src/core/fable2_console.h` (host commands) and `src/lua/console_cmds.lua` (Lua commands).

## How the console works

- Every command is typed in the SDK's `~` console behind the word `f2`, for example `f2 warp 1 2 3` or `f2 god on`. Chain commands with `;` inside one line: `f2 warp save a; wait 60; warp a`.
- Nothing runs until the first `f2` command is typed, so a normal launch is unchanged. On that first command, `console\autoexec.txt` (if present) runs first.
- The queue runs one command per frame on the game thread (from the `MainRenderLoop` hook). Host commands (memory, files, cvars, timers) are handled in C++; every other line goes to Lua.
- Host replies go to the `~` console (`REXLOG_INFO`) and to `<game root>\console\out.txt`. Lua replies go through `cprint` as `[console] ...` lines; whether `cprint` reaches the `~` console is unconfirmed, so check `logs\` if they do not show.
- A leading `!` (for example `f2 !info`) also shows the Lua reply in a message box.
- If the first word is a command, that command runs. Anything else runs as Lua. Lua results print as `= <value>` and errors as `error: ...`.
- Command names are case-insensitive. Arguments are split on spaces.
- Engine calls print `<label>: ok -> <values>` on success, `<label> failed: <error>` on error, or `<label>: not available in this game build` if the function is missing.
- Typed values are converted as follows: numbers become numbers, `true`/`on`/`yes` become `true`, `false`/`off`/`no` become `false`, `nil` becomes nil, and anything else stays a string.
- Hero lookup tries `QuestManager.HeroEntity`, then `Debug.GetHero`, then `GetPlayerHero`. Commands that need the hero print `no hero (load a save first)` when none is found.
- **Name clashes:** a line whose first word is a command runs that command, even if you meant Lua. For example, `level = 3` runs `level`. Write `lua level = 3` instead.
- `help` lists commands by group; `help <group>` shows the usage of every command in a group; `help <command>` shows one.

**Argument shapes** (console-v7) come from the game's own compiled scripts: `tools/lua_callshapes.py` reads every call in `gamescripts.bnk`/`guiscripts.bnk` and records how many arguments it passes; the result is in [LUA-CALL-SHAPES.md](LUA-CALL-SHAPES.md). A command is only marked **args unconfirmed** when no shipped script calls that function. Many of these use *shape probing*: the console tries `(entity, args)` first and then `(args)`, and the reply ends in `[shape 1]` or `[shape 2]` to say which one the game accepted. Please send those lines back; they turn guesses into facts.

**Who:** commands that take `[who]` accept `hero` (or `me`), `target` (what the hero is targeting), `dog`, `henchman`, or an entity name (looked up with `Debug.GetEntityWithName`).

**Crash risk:** `pcall` catches Lua errors, not a fault inside the engine. Calling a method on a dead or unloaded entity can still crash the game; prefer live targets.

## Info

| Command | Usage | What it does | Notes |
|---|---|---|---|
| `help` | `help [command]` | Lists all commands. With a name, shows that command's usage line. | Also prints a reminder about `!` and Lua. |
| `pos` | `pos` | Prints the hero position as `pos x y z` (3 decimals). | Prints `no position` if the hero cannot be read. |
| `level` | `level` | Prints the loaded level name. | Calls the global `GetLevelLoaded`, not a Debug function. |
| `info` | `info` | Prints hero position, level, time of day, game time and hero target. | Calls `Timing.GetTimeOfDay`, `Debug.GetTime` and `Debug.GetHeroTarget`, all with no arguments. Each part reports on its own. |
| `target` | `target` | Prints the entity the hero is targeting, and its position. | Calls `Debug.GetHeroTarget` with no arguments. Prints `no target` when there is none. |
| `ent` | `ent <name>` | Finds an entity by name and prints it and its position. | Same shape as the scripts (`Debug.GetEntityWithName`). Does not move the hero. |

## Warp

| Command | Usage | What it does | Notes |
|---|---|---|---|
| `warp` | `warp <x> <y> <z>` | Moves the hero to the given coordinates. | Sets the position on the hero entity directly (`SetPosition(CVector3)`), not through a Debug function. |
| `warp` | `warp save <name>` | Saves the hero's current position under `<name>`. | Names are lowercased. Places are held in memory (`__f2_console`) for the session. Nothing in this script writes them to disk. |
| `warp` | `warp <name>` | Moves the hero to a saved place. | Uses the saved position. |
| `warp` | `warp list` | Lists saved places, sorted. | Prints `places: (none)` when there are none. |
| `warp` | `warp level <region> [marker] [world]` | Loads a region at a start marker: `Debug.LoadLevel(world, region, marker)`. World defaults to `Albion`, marker to `""`. | Same shape as the game's scripts. `travel` has named presets. |
| `warp` | `warp rel <dx> <dy> <dz>` | Moves the hero by an offset from its current position. | Position-only; not a Debug call. |
| `gotoent` | `gotoent <name>` | Teleports the hero to a named entity. | Same shape as the scripts (`Debug.GetEntityWithName`). Prints `entity not found: <name>` on a miss. |
| `up` | `up [metres]` | Moves the hero straight up. Default 5. | Handy when stuck in geometry. Position-only; not a Debug call. |

## Hero toggles

These toggles are not all hero-specific. `magic`, `orbs` and `tutorials` are global. `gui`, `names`, `navgraph`, `debugpage` and `combatdebug` are debug display switches.

For `[on|off]` toggles, no argument or any value other than `off`/`false`/`no` turns the setting on.

| Command | Usage | What it does | Notes |
|---|---|---|---|
| `god` | `god [on\|off]` | Hero god mode. `god on all` (or `god off all`) also sets henchman god mode. | Same shape as the scripts (`Debug.SetHeroGodMode`, `Debug.SetHenchmanGodMode`). Henchman is set only when the third word is `all`. |
| `freecam` | `freecam [on\|off]` | Free camera. With no argument, flips the current state. | Same shape as the scripts (`Debug.SetUseFreeCamera`). Reads state with `Debug.GetUseFreeCamera`, which takes no arguments. |
| `gui` | `gui [on\|off]` | Draws the HUD. | Same shape as the scripts (`Debug.SetDrawGUI`). |
| `names` | `names [on\|off] [filter]` | Draws entity names over entities. Optional filter. | Same shape as the scripts (`Debug.SetDrawEntityNames`, `Debug.SetDrawEntityNamesFilter`). The filter is passed as typed. |
| `navgraph` | `navgraph [on\|off]` | Draws the navigation graph. | Same shape as the scripts (`Debug.SetDrawNavGraph`). |
| `debugpage` | `debugpage [page]` | Sets the on-screen debug page. | The scripts pass a page name (`"player"`, the default). |
| `combatdebug` | `combatdebug [on\|off]` | Draws behaviour names and combat group debug. | args unconfirmed (`Debug.SetDrawBehaviourNames`, `Debug.SetDrawCombatMultiplier`). Combat multiplier is set to 1 when on and 0 when off. |
| `magic` | `magic [on\|off]` | Global magic enabled. | Same shape as the scripts (`Debug.SetGlobalMagicEnabled`). |
| `orbs` | `orbs [on\|off]` | Experience orbs. | Same shape as the scripts (`Debug.SetExperienceOrbsEnabled`). |
| `tutorials` | `tutorials [on\|off]` | Tutorials on or off. | Same shape as the scripts (`Debug.SetTutorialsEnabled`). |
| `lod` | `lod low\|full\|off` | `low` forces low LOD. `full` sets full LOD for all creatures. `off` clears both. | Same shape as the scripts (`Debug.ForceLowLoDLevel`, `Debug.SetFullLODForAllCreatures`). Any other word prints the usage. |
| `fast` | `fast [on\|off]` | Calls `Debug.MakeGameGoFast`. | Same shape as the scripts. The script does not say what the function changes. |

## Stats and items

| Command | Usage | What it does | Notes |
|---|---|---|---|
| `herolevel` | `herolevel <n>` | Sets hero level. | Same shape as the scripts (`Debug.SetHeroLevel`). Needs a number; otherwise prints the usage. |
| `will` | `will <0-1>` | Sets hero Will (0 to 1, as the scripts use). | Same shape as the scripts (`Debug.SetHeroWill`). |
| `strength` | `strength <0-1>` | Sets hero Strength (0 to 1). | Same shape as the scripts (`Debug.SetHeroStrength`). |
| `morality` | `morality <n>` | Sets hero morality. | Same shape as the scripts (`Debug.SetHeroMorality`). Usage text: 1000 good, -1000 evil, 0 neutral. |
| `purity` | `purity <n>` | Sets hero purity. | Same shape as the scripts (`Debug.SetHeroPurity`). Usage text: 1000 pure, -1000 corrupt, 0 neutral. |
| `age` | `age <n>` | Sets hero age. | Same shape as the scripts (`Debug.SetHeroAge`). |
| `fat` | `fat <0-1>` | Sets hero fat (0 to 1). | Same shape as the scripts (`Debug.SetHeroFat`). |
| `tan` | `tan <0-1>` | Sets hero tan (0 to 1). | Same shape as the scripts (`Debug.SetHeroTan`). |
| `skill` | `skill <0-1>` | `Debug.SetHeroSkill(value)`. | The scripts pass 0 to 1 (e.g. 0.3, 0.5). |
| `gold` | `gold <n>` | Adds gold, or subtracts it if negative. | Uses `Money.Modify` on the hero. Not a Debug or Timing function, so not marked. Needs a hero. |
| `giveall` | `giveall <category>` | Adds every item of a kind. Categories are listed below. | `giveall list` shows the categories. `Debug.AddAll*` get the hero (`AddAll*(entity)`), `weapons` is `AddAllWeaponsWithSlots(hero, slots)` (`giveall weapons [slots]`, default 4), `melee`/`ranged` take nothing, `wear` passes `true`. |
| `weapon` | `weapon <record>` | Gives the hero a weapon, e.g. `weapon ObjectInventory_L_LongSword_Iron`. | `Debug.GiveHeroWeapon(weapon_name, start_sheathed)`; passes `true`. |

### giveall categories

`giveall list` prints these names in alphabetical order. Each category calls the listed `Debug.*` function. Since console-v7 the `AddAll*` functions get the hero (`AddAll*(entity)`, as the game defines them), `GiveAll*` get nothing and `wear` gets `true`.

| Category | Engine function | Notes |
|---|---|---|
| `weapons` | `Debug.AddAllWeaponsWithSlots` | |
| `melee` | `Debug.GiveAllMeleeWeapons` | |
| `ranged` | `Debug.GiveAllRangedWeapons` | |
| `legendary` | `Debug.AddAllLegendaryWeapons` | |
| `heavy` | `Debug.AddAllHeavyWeapons` | |
| `light` | `Debug.AddAllLightWeapons` | |
| `crossbows` | `Debug.AddAllCrossbows` | |
| `firearms` | `Debug.AddAllFirearms` | |
| `clothing` | `Debug.AddAllClothing` | |
| `accessories` | `Debug.AddAllClothingAccessories` | |
| `wear` | `Debug.SetHeroWearingAllClothing` | Called with `true`. Same shape as the scripts. |
| `dyes` | `Debug.AddAllDyes` | |
| `hairstyles` | `Debug.AddAllHairstyles` | |
| `makeup` | `Debug.AddAllMakeUp` | |
| `tattoos` | `Debug.AddAllTattoos` | |
| `potions` | `Debug.AddAllPotions` | |
| `food` | `Debug.AddAllFoodAndDrink` | |
| `furniture` | `Debug.AddAllFurniture` | |
| `tools` | `Debug.AddAllTools` | |
| `gifts` | `Debug.AddAllGifts` | |
| `trophies` | `Debug.AddAllTrophies` | |
| `cards` | `Debug.AddAllCards` | |
| `books` | `Debug.AddAllGeneralBooks` | |
| `diary` | `Debug.AddAllLuciensDiaryPages` | |
| `expressions` | `Debug.AddAllExpressionBooks` | |
| `dogbooks` | `Debug.AddAllDogBooks` | |
| `collars` | `Debug.AddAllDogCollars` | |
| `augmentations` | `Debug.AddAllAugmentations` | |
| `prizes` | `Debug.AddAllCarbonatedPrizes` | |
| `scriptitems` | `Debug.AddAllScriptItems` | |
| `items` | `Debug.AddAllInventoryItems` | |

## Time

| Command | Usage | What it does | Notes |
|---|---|---|---|
| `time` | `time [hour]` | With no argument, prints the time of day. With an hour, sets it. | No argument calls `Timing.GetTimeOfDay` with none. Setting calls `Timing.SetTimeOfDay(hour)`, Same shape as the scripts. The usage text assumes hour 0-24. |
| `ffwd` | `ffwd <hour>` | Fast-forwards the time of day to an hour. | Same shape as the scripts (`Timing.FastForwardTimeOfDayTo`). Needs an hour. |

## Spawning and cleanup

| Command | Usage | What it does | Notes |
|---|---|---|---|
| `spawn` | `spawn <record> [count] [entityname]` | Creates an entity next to the hero, e.g. `spawn CreatureFaunaChicken 3`. Up to 50. | `Debug.CreateEntityByHero(record_name, entity_name)`. Stops at the first failure. |
| `spawnat` | `spawnat <record> <x> <y> <z> [entityname]` | Creates an entity at a position. | `Debug.CreateEntityAtPosition(record, entity_name, CVector3)`; the name defaults to `f2_<record>`. |
| `creatures` | `creatures <family>\|list` | Spawns a creature family, e.g. `creatures CreatureFamilyHobbeMedium03a`. | `Debug.SpawnCreatureFamily(recordname)`. `list` shows the families the scripts use. |
| `combatants` | `combatants [record] [n]` | Creates combatants around the hero (default `CreatureHobbe`, 3). | `Debug.CreateCombatantsAroundHero(type, num, preserve_faction, radius)`. |
| `killall` | `killall` | Destroys all creature entities. | `Debug.DestroyAllCreatureEntities(false)`, as the scripts call it. |
| `destroytype` | `destroytype <type>` | Destroys all entities of a type. | Same shape as the scripts (`Debug.DestroyAllEntitiesOfType`). |
| `resetcreatures` | `resetcreatures` | Resets spawned creatures. | Calls `Debug.ResetSpawnedCreatures` with no arguments. |

## Reload and session

| Command | Usage | What it does | Notes |
|---|---|---|---|
| `reloadai` | `reloadai` | Reloads AI scripts. | Calls `Debug.ReloadAI` with no arguments. |
| `reloadcam` | `reloadcam` | Reloads camera scripts. | Calls `Debug.ReloadCameras` with no arguments. |
| `quitmenu` | `quitmenu confirm` | Quits to the front end. | Without `confirm`, prints a reminder and does nothing. Calls `Debug.QuitToFrontEnd` with no arguments. The usage text warns that unsaved progress is lost. |

## Discovery

| Command | Usage | What it does | Notes |
|---|---|---|---|
| `dump` | `dump <table>` | Lists the member names of a table, sorted, 8 per line. Defaults to `_G`. | Names only, not values. Prints `cannot enumerate` for values that cannot be listed. |
| `find` | `find <text>` | Searches global names, plus names in `Debug.`, `Timing.`, `Money.`, `GUI.`, `Player.` and `QuestManager.`. Case-insensitive substring match. | Shows up to 60 hits, then `... narrow the search`. Calls nothing. |
| `call` | `call <path> [args...]` | Calls any function by path, for example `call Debug.SetHeroLevel 20`. | Arguments go through the typed-value conversion. args unconfirmed for `Debug.*` and `Timing.*` targets, since the script passes whatever you type. |
| `get` | `get <path>` | Prints a global or member value. | Strings are shown in quotes. Other values use `tostring`, so functions and tables show as addresses. A missing path prints `nil`. |
| `set` | `set <path> <value>` | Assigns a global or table member. | Writes directly with no checks on the field. Prints the new value, or `set failed`. |
| `lua` | `lua <code>` | Runs Lua. Tries `return <code>` first, then the code as a statement. | Statements work, for example `lua level = 3`. Prints `= <result>` or `error: ...`. The same Lua fallback applies to any line that is not a command. |

## Aliases

| Command | Usage | What it does | Notes |
|---|---|---|---|
| `alias` | `alias <name> <command...>` | Saves a shortcut that expands to the command text. | Lasts until the game closes; held in memory. Names are lowercased. Re-using a name overwrites it. Aliases are checked before built-in commands, so an alias with a built-in name overrides that command. Expansion can nest up to 5 levels deep. Extra words typed after the alias name are dropped. |

## Examples

Numbers and entity names in the outputs below are illustrative.

### 1. Warping

```
f2 pos
[console] pos 412.000 -87.500 3.250
f2 warp save bridge
[console] saved place bridge
f2 warp rel 0 0 40
[console] warped to 412.00 -87.50 43.25
f2 warp bridge
[console] warped to 412.00 -87.50 3.25
f2 warp list
[console] places: bridge
```

### 2. Debugging combat

```
f2 god on all
[console] hero god: ok
[console] henchman god: ok
f2 combatdebug on
[console] behaviour names: ok
[console] combat multiplier: ok
f2 combatants 4
[console] combatants: ok
f2 target
[console] target <entity>
f2 !info
(message box shows the info lines)
f2 killall
[console] killall: ok
f2 resetcreatures
[console] reset: ok
f2 combatdebug off
f2 god off all
```

### 3. Discovering an unknown function

```
f2 find hairstyle
[console] 1 matches
[console] Debug.AddAllHairstyles
f2 get Debug.AddAllHairstyles
[console] Debug.AddAllHairstyles = function: 0x...
f2 dump Timing
[console] N members
[console] ...
f2 call Debug.AddAllHairstyles
[console] Debug.AddAllHairstyles: ok
```

`giveall hairstyles` calls the same function. The `find` match count depends on the build.

## Widescreen (console-v11)

Controls the fit-window module (`src/core/fable2_fit_window.h`), which renders at the window's aspect ratio instead of a letterboxed 16:9 image. `ws` is short for `widescreen`.

| Command | Usage | What it does | Notes |
|---|---|---|---|
| `ws` | `ws` / `ws status` | Shows whether the fit is on, the window size, the aspect in use (window or forced), the two floats the camera reads (0x8209FE7C, 0x8209FDF4) and the UI cvars. | |
| `ws on` / `ws off` | `ws on\|off` | Turns the fit on or off while the game runs. Off writes the stock 16:9 and 4:3 back and returns `present_letterbox` and the safe area to their defaults. | `on` installs it if `fit_game_to_window` was false at startup. |
| `ws aspect` | `ws aspect <w:h\|number\|auto>` | Forces an aspect (e.g. `21:9`, `32:9`, `2.39`) instead of the window's; `auto` follows the window again. | Clamped to 4:3..4:1. Not saved between runs; put it in `consoleutoexec.txt`. |
| `ws hud` | `ws hud on\|off` | `fit_window_hud_to_edges`: moves the HUD and menus out to the window edges. | Takes effect when the next UI screen loads. |
| `ws native` | `ws native on\|off` | `fit_window_native_16x9`: keeps the loading screens and saving banner at 16:9. | |
| `ws log` | `ws log on\|off` | `fit_window_ui_log`: the `[fit-window]` layout log. | |
| `ws apply` | `ws apply` | Writes the aspect again now. | |

## Host commands (console-v3)

These run in the game's C++ (`fable2_console.h`), not in Lua. They answer in the `~` console and in `console\out.txt`. Lines are queued in order; chain commands with `;`. Addresses are hexadecimal Xbox 360 guest addresses (with or without `0x`); values are big-endian as the game stores them. Raw Lua and `!` lines keep their semicolons.

| Command | Usage | What it does | Notes |
|---|---|---|---|
| `peek` | `peek <addr> [bytes]` | Hex dump, 16 bytes a line (default 16, max 256). | Errors if the range is not readable. |
| `poke` | `poke <addr> <value> [8\|16\|32]` | Writes a value and prints the read-back. | Size comes from how the value is written (`0x70` byte, `0x0070` half, else word) unless given. Must be aligned. |
| `expect` | `expect <addr> <value> [8\|16\|32]` | Prints `ok` or `ERR` with the actual value. | For scripted checks. |
| `find` | `find <number> [size] [from] [to]` | Counts matches and lists the first 32 addresses. | Default range 40000000-50000000. `find <word>` is still the Lua name search. |
| `freeze` / `unfreeze` | `freeze <addr> <value> [size]` / `unfreeze <addr>\|all` | Rewrites a value every frame. | |
| `trace` | `trace <addr> [bytes] [lines]` / `trace off` | Prints each changed word with the frame number. | Default 4 bytes, 200 lines. |
| `wait` | `wait <frames>` / `wait <n>ms` / `wait <n>s` | Holds the queue. | |
| `waitfor` | `waitfor ram <addr> [op] <value> [size] [timeout <t>]` | Holds the queue until the memory condition is true, or times out (default 30 s, counted as an error). | Ops `==` `!=` `<` `>` `<=` `>=` `&`; compares as signed. |
| `exec` | `exec <file>` | Queues the lines of a file (relative to `console\`); `#` starts a comment. | |
| `echo` | `echo <text>` | Prints text. | |
| `status` | `status` | Frames, queue length, frozen count, trace, errors. | Also written to `console\state.txt` about once a second. |
| `hosthelp` | `hosthelp` | Lists host commands. | Lua `help` lists the Lua ones. |

None of these have been tested in game; they need real Fable 2 addresses to be useful.

## Dumps (console-v5)

Use these to find what can be read and edited, then change it with `set`, `call`, or `poke`.

| Command | Usage | What it does | Notes |
|---|---|---|---|
| `dump` | `dump <path> [depth]` | Lists a table or object with values (default depth 1). | Output goes to the log as `[console]` lines (up to 3000 lines). |
| `dumpall` | `dumpall [depth] [filter]` | Lists every global table, function and value (default depth 3). `filter` is a substring of the path. | Up to 20000 lines to the log. |
| `methods` | `methods <path>` | Lists the methods of an object or class, for example `methods hero` or `methods Debug`. | Reads the object's metatable. |
| `cvars` | `cvars [text]` | Writes every SDK and game cvar with value, default, range and description to `console\cvars.txt`. | Change one by typing its name and a value in the `~` console. |
| `dumpmem` | `dumpmem <addr> <bytes> [file]` | Writes raw guest memory (up to 64 MB) to `console\<file>`. | Default file `mem-<addr>.bin`. |

## Host commands added in console-v6

| Command | Usage | What it does | Notes |
|---|---|---|---|
| `cvar` | `cvar <name> [value]` | Prints an SDK/game cvar with its default, or sets it. | Same as typing the cvar in `~`, but works in `exec` files, `autoexec.txt`, aliases and `;` chains. `cvars <text>` finds names. |
| `history` | `history [n]` / `history save <file>` / `history clear` | Lists the last n commands (default 20, keeps 200). `save` writes them to `console\<file>` for `exec`. | `again` and `history` themselves are not recorded. |
| `again` | `again [n]` | Re-runs the last command, or history entry n. | |
| `repeat` | `repeat <n> <command...>` | Queues a command n times (1-1000), one per frame. | Works for host and Lua commands. |
| `every` | `every <frames\|Ns\|Nms> <command...>` / `every list` / `every stop <n>\|all` | Re-queues a command on a period, e.g. `every 2s hp`. | Skipped while more than 256 commands are queued. Stops when the game closes. |
| `autoexec` | `autoexec` | Runs `console\autoexec.txt` again. | It also runs once automatically, before the first `f2` command. One command per line, `#` comments. |
| `peekf` | `peekf <addr> [count]` | Reads big-endian floats (up to 64), 4 per line. | |
| `pokef` | `pokef <addr> <float>` | Writes a float and prints the read-back. | Address must be 4-aligned. |
| `freezef` | `freezef <addr> <float>` | Holds a float every frame. | `unfreeze` releases it. |
| `peekstr` | `peekstr <addr> [max]` | Reads a NUL-terminated string (default 256, max 4096 bytes). | Non-printable bytes show as `\xNN`. |
| `findstr` | `findstr <text> [from] [to]` | Searches memory for ASCII text; lists the first 32 hits. | Default range 40000000-50000000. Matches within one word (no spaces); case-sensitive. |
| `findf` | `findf <float> [tolerance] [from] [to]` | Searches for a float within a tolerance (default 0.001). | The tolerance must contain a `.` so it is not read as an address. |
| `fill` | `fill <addr> <bytes> <byte>` | Writes one byte value over a range (up to 64 KB). | |
| `scan start` | `scan start <value>\|any [8\|16\|32\|f] [from] [to]` | Starts a value scan: records every address holding the value. | `any` needs a range of 16 MB or less. Up to 4 million candidates. `f` scans floats. |
| `scan next` | `scan next <value>\|changed\|same\|inc\|dec\|gt <v>\|lt <v>` | Keeps only candidates matching the new condition. | Change the value in game between steps. |
| `scan list` / `scan clear` | `scan list [n]` / `scan clear` | Shows candidates (default 20) / frees them. | |

Finding an address with `scan`: note your gold, `f2 scan start 1234`; spend or gain some, `f2 scan next 1180`; repeat until a few remain; `f2 scan list`; then `f2 freeze <addr> 99999` or `f2 trace <addr>`.

## Lua commands added in console-v6

### Health

| Command | Usage | What it does | Notes |
|---|---|---|---|
| `hp` | `hp [n\|full] [who]` | Prints health as `cur / max`, or sets it. | Sets by calling `Health.Modify(ent, n - cur)`. |
| `heal` | `heal [who]` | Restores full health. | |
| `maxhp` | `maxhp <n> [who]` | `Health.SetMax`. | Same shape as the scripts. |
| `invuln` | `invuln [on\|off] [who]` | `Health.SetAsInvulnerable(ent, bool)`. | Works on any entity, including the dog. |
| `cantdie` | `cantdie [on\|off] [who]` | `Health.SetCantDie`: takes damage, never dies. | |
| `regen` | `regen <rate> [who] [third]` | `Health.SetRegeneration(entity, rate, 100)`. | The only script call passes 100 last; its meaning is unknown. |
| `kill` | `kill [who]` | Reduces health below 0 (default: target). | |
| `deplete` | `deplete` | `Debug.DepleteHealth` (the game's own debug helper). | |

### Hero

| Command | Usage | What it does | Notes |
|---|---|---|---|
| `mana` | `mana [on\|off]` | Infinite Will energy (`Debug.SetHeroManaInfinite`). | Used by the game's GDC/E3 startup scripts. |
| `sprint` | `sprint [on\|off]` | `Debug.SetHeroSprintingEnabled`. | Same source. |
| `speed` | `speed <multiplier>\|off` | Overrides hero movement speed (`Player.SetPlayerMovementSpeedOverridden` + `SetOverriddenMovementSpeedMultiplier`). | `(hero, value)`, as in the scripts. |
| `footlock` | `footlock [on\|off]` | Foot IK locking (`Debug.SetHeroFootLockingEnabled`). | |
| `safety` | `safety [on\|off]` | `Player.SetSafetyMode`. | `(hero, value)`, as in the scripts. |
| `orbpickup` | `orbpickup [on\|off]` | `Player.SetAbleToCollectExperienceOrbs`. | `(hero, value)`, as in the scripts. |
| `orbspawn` | `orbspawn` | `Debug.SpawnExperienceOrbs`. | |
| `scar` | `scar [face\|random]` | Adds a scar (`Debug.AddHeroFaceScar` / `AddHeroRandomScar`). | |
| `outfit` | `outfit <name>\|list` | Debug outfits: barbarian brawler crossdresser goth martialarts pirate pureevil swashbuckler year. | `Debug.SetHeroWearing<Name>Outfit`. |
| `dye` | `dye <clothing record> <dye record>` | Dyes a worn item, e.g. `dye ObjectClothingCoatPoshM ObjectInventoryDyeClothingBasicBlue01`. | `Debug.SetHeroWornItemDye(clothing, true, dye)`. |
| `expressions` | `expressions all\|extend\|succeed [on\|off]` | Unlock all expressions / make all extendable / force extended ones to succeed. | |
| `entitytype` | `entitytype <record>` | `Debug.ChangePlayerEntityType(record)`. | The scripts only use `CreatureHero` and `CreatureHeroFemale`; other records are untested. |

### Money and stats

| Command | Usage | What it does | Notes |
|---|---|---|---|
| `money` | `money` / `money set <n>` | Prints gold (`Money.Get(hero)`) or sets it exactly (`Money.Set`). | `gold <n>` still adds. |
| `stats` | `stats` | Prints morality, purity, renown, strength, will, skill, fat, tan, height, title, chapter, seconds played, metres travelled, combat multiplier, affordable abilities, gold and difficulty. | Each getter is shape probed; `n/a` means neither shape worked. |
| `renown`, `addmorality`, `addpurity`, `addstrength`, `addwill`, `addskill`, `addfat`, `addtan`, `height` | `<command> <n>` | Adds to the stat. | `Stats.Modify*(hero, n)`. `morality`/`purity` (set exactly) still exist. |
| `ability` | `ability <name> [level]` / `ability list [filter]` | Prints or sets an ability, e.g. `ability fireball 5`. | `Stats.Get/SetHeroAbilityLevel(hero, EHeroAbilityType.*, level)`. The name is any unique part of an `EHeroAbilityType` key. |
| `title` | `title` / `title <name>` / `title list` | Prints the title, or unlocks one, e.g. `title ChickenChaser`. | `Stats.AddUnlockedTitle(hero, "ObjectInventoryHeroTitle<Name>")`; `list` shows the 22 titles the scripts use. |
| `chapter` | `chapter [n]` | Prints or sets chapter progress. | Same shape as the scripts. |
| `trackedstat` | `trackedstat <name> [n]` / `trackedstat list [filter]` | Prints or sets a tracked stat, e.g. `trackedstat groin_shots`. | `Stats.Get/SetTrackedStat(hero, ETrackedStatType.*[, n])`. |

### Items

| Command | Usage | What it does | Notes |
|---|---|---|---|
| `give` | `give <itemtype> [count]` | `Inventory.AddItemOfType(hero, type)`, up to 999 times. | The item type is an object record name; the exact form is unconfirmed. |
| `have` | `have <itemtype>` | Count of that item. | |
| `take` | `take <itemtype>` | Removes all of that item. | |
| `invcount` | `invcount` | Number of items carried. | |
| `weapons` | `weapons rusty\|iron\|steel\|masterwork\|legendary\|gun\|pistol\|all` | The game's debug weapon sets. | |
| `henchweapon` | `henchweapon <name>` | `Debug.GiveHenchmanWeapon`. | |
| `giveall` (new categories) | `giveall dlc1\|dlc1weapons\|dlc1clothing\|dlc1potions\|dlc1misc\|dlc1augs\|dlc1augweapons\|dlc2\|augweapons\|suits\|dogitems\|randomaug\|laser` | More `Debug.AddAll*` sets. | |

### World and time

| Command | Usage | What it does | Notes |
|---|---|---|---|
| `dayspeed` | `dayspeed [n]` | Sets day speed (`Timing.SetDaySpeed`); alone prints world seconds per second. | Used by the startup scripts. |
| `stoptime` | `stoptime [on\|off]` | `Timing.SetTimeAsStopped`. | |
| `midnight` | `midnight` | Stops time at midnight. | |
| `day` | `day [n]` | Prints or sets the day count. | |
| `nextday` | `nextday [n]` | Advances the day count (default 1). | `Timing.AdvanceDayCount(n)`. |
| `moon` | `moon <phase>` | `Timing.SetMoonPhase(phase)`. | The scripts pass 4. |
| `clock` | `clock` | World/program frame, tick rate, time step, day, time of day, world seconds per second, real-world time. | |
| `season` | `season winter\|summer` | `Debug.SwapToWinter` / `SwapToSummer`. | |
| `ground` | `ground [x y z]` | Landscape height and water depth (default: under the hero). | One argument, as in the scripts; the console passes a `CVector3` first, then `(x, y)`. |
| `snap` | `snap` | Drops the hero onto the landscape surface. | Uses `ground`'s height. |
| `autosave` | `autosave` | `AutoSaveGame(true, true)`, as every script call does. | |
| `sfx` | `sfx <event>` | Plays a sound event on the hero, e.g. `sfx SE_GOLD_PAYMENT`. | `Sound.PlayEvent(entity, event, "")`. |
| `havok` | `havok friction\|restitution\|angulardamping\|maxlinear\|maxangular\|minmass <value>` | Physics tuning. | From the E3 startup script. |

### Camera

| Command | Usage | What it does | Notes |
|---|---|---|---|
| `fov`, `zoom`, `roll`, `blur` | `<command> [value]` | Print or set the camera value. | `Camera.Get*(cam)` / `Camera.Set*(cam, value)` with `cam = CameraManager.GetCurrentCamera().Camera`; `fov <n>` uses the game's `Debug.SetFOV(fov)`. |
| `dof` | `dof` | Prints depth of field. | Setting it takes 7 values (`Camera.SetDOF`); use `call`. |
| `shake` | `shake` | Screen shake on the hero. | `Player.AddScreenShake(hero, {})`; the table's fields are unknown, so this may do nothing. |
| `campos` | `campos` | Camera position, forward vector and angles. | |
| `camto` | `camto <x> <y> <z>` | Moves the camera (`Debug.MoveCameraToPosition`). | |
| `camlook` | `camlook <x> <y> <z>` | Points the camera focus (`Debug.MoveCameraFocusToPosition`). | |
| `canopy` | `canopy [on\|off]` | Tree canopy fading. | |
| `trackcam` | `trackcam` | Toggles the target-tracking camera. | |

### UI

`ui <sub>`; `ui list` shows all of them.

| Sub-command | What it does | Notes |
|---|---|---|
| `ui hud on\|off` | Draws the whole HUD (`Debug.SetDrawGUI`). | Same as `gui`. |
| `ui msg <text>` / `msg <text>` | Message box. | |
| `ui info <text>` | Info box. | `GUI.DisplayInfoBox(text)`. |
| `ui close <boxname>` | Removes a named display box. | `GUI.RemoveDisplayBox(name)`; boxes are removed by name. |
| `ui fadeout [s]` / `ui fadein [s]` | Screen fades (default 1 s). | `GUI.FadeScreenOut/In(seconds)`. |
| `ui toggle <element> [on\|off]` | `GUI.ToggleElement(name, bool)`. | The scripts only use `DPadUp`, `DPadDown`, `DPadRight`. |
| `ui icons on\|off` | Player icons and world icons. | |
| `ui vignette on\|off`, `ui dpad on\|off`, `ui highlight on\|off`, `ui version on\|off`, `ui pausedtext on\|off`, `ui expmeter on\|off` | Individual HUD parts. | |
| `ui interesting on\|off`, `ui suggestions on\|off` | Object highlighting / automatic HUD suggestions. | `(hero, value)`, as in the scripts. |
| `ui counter <n> [max]\|off`, `ui timer <s>\|off` | On-screen counter / countdown. | `GUI.SetCounter("f2counter", "", n[, max])`, `GUI.SetTimer("f2timer", s)`; `off` removes them by name. |
| `ui state` | Whether a UI, quick menu, display box or movie is active, and whether the screen is faded. | |
| `ui credits` | Credits screen. | |
| `ui consolescale <n>` | Engine debug console scale. | |

#### Editing HUD elements (console-v8)

The game-side `GUI` table reaches HUD elements **by name**. Names come from `art/gui/gameface/hud.bgf`; `ui elements [filter]` lists them (PlayerHealth, HealthAndMana, Minimap, WorldIcons, Subtitles, DPad*, ShardHealth, ExpressionMeter, ExperienceOrbs, ...). The game's own scripts only pass the names noted below, so other names are untested: a name the engine does not know may do nothing.

| Sub-command | What it does | Engine call |
|---|---|---|
| `ui elements [filter]` | Lists HUD element names. | |
| `ui show <element>` / `ui hide <element>` | Shows or hides an element. | `GUI.ToggleElement(name, bool)`; scripts use DPadUp/DPadDown/DPadRight. |
| `ui fade <element> in\|out` | Fades an element in or out. | `GUI.FadeElementIn` / `FadeElementOutSlow(name)`; scripts use ShardHealth. |
| `ui text <element> <TEXT_ID>` | Sets an element's text to a string-table entry (not free text). | `GUI.SetGUIText(name, id)`, e.g. `ui text ShardHealth TEXT_CREATURE_NAME_TROLL`. |
| `ui meter <element> <percent> [seconds]` | Sets a meter's fill, optionally animated. | `GUI.SetMeterPercentOverTime`; scripts use ShardHealth. |
| `ui fill low\|med\|high <r> <g> <b>` | Meter fill colours (0-255). | `GUI.Set{Low,Med,High}FillColour`. |
| `ui fillpct <low%> <med%>` | Where the fill colour changes. | `GUI.SetFillColourPercentages` (scripts: 18 28). |
| `ui fadecolour <r> <g> <b> [s]` | Fades the screen to a colour; `ui fadein` undoes it. | `GUI.FadeScreenOutToColour`. |
| `ui dpadcenter on\|off`, `ui abilities on\|off`, `ui gdc on\|off` | D-pad centring, abilities in the experience display, the GDC demo HUD. | `GUI.SetDPadCenter`, `SetExperienceShouldShowAbilities`, `ShowGDCUI`. |
| `ui suggestmode normal\|expressions` | HUD suggestion mode. | `GUI.SetHUDSuggestionMode(EHUDSuggestionMode.*)`. |
| `ui icon add\|remove [who] [type]` | Puts a world icon over an entity (default target, type `WI_QUEST_GIVER`). | `GUI.AddWorldIcon(name, entity, "", EWorldIconType.*)`. |
| `ui timertext <seconds> <TEXT_ID>` | Countdown with a label. | `GUI.SetTimerWithText`. |

#### Position, scale, rotation, opacity, tint, free text (console-v9)

These edit element attributes directly inside the GameFace GUI Lua state, through the bridge described in [GUI-LUA-BRIDGE.md](GUI-LUA-BRIDGE.md). Replies come back as `[gui]` lines (they may arrive a frame later). An element is found by path (`Scene.Layer.Minimap`) or by name anywhere in the tree (`Minimap`).

| Sub-command | What it does |
|---|---|
| `ui tree [element] [depth]` | Lists the live element hierarchy with full paths (default `Scene`, depth 3, up to 300 lines). |
| `ui get <element> [attr...]` | Position, scale, rotation, opacity and visibility, or the named attributes. |
| `ui pos <element> <x> <y>` / `ui move <element> <dx> <dy>` | Sets or offsets `position.x/y`. |
| `ui scale <element> <sx> [sy]` | Sets `scale.x/y` (one value scales both). |
| `ui rotate <element> <degrees>` | Sets `rotation.z`. |
| `ui opacity <element> <0-100>` | Sets `opacity`. |
| `ui visible <element> on\|off` | Sets `active`. |
| `ui tint <element> <r> <g> <b> [a]` | Sets `diffuse.r/g/b[/a]`. The value range is unconfirmed (try 0-255, then 0-1). |
| `ui textcolor <element> <r> <g> <b>` | Sets `textcolor.r/g/b`. |
| `ui label <element> <free text>` | Sets `textstring` to any text. |
| `ui attr <element> <attribute> <value>` | Sets any attribute (numbers, `on`/`off`, or text). |
| `ui hudscale <s>` / `ui hudopacity <0-100>` | Scales or fades the whole HUD layer (`Layer`). |
| `ui reset <element>\|all` | Restores every attribute `ui` changed. |
| `ui export` | Prints `ui attr ...` lines for everything changed; put them in `console\autoexec.txt` to reapply after a restart. |
| `gui <lua>` | Runs any Lua in the GUI state; `return` a value to see it (`gui on`/`gui off` alone is still the HUD toggle). Helpers: `__f2.need(name)`, `__f2.desc(name, el)`, plus the game's `getElement`, `getAttribute`, `setAttribute`, `getChildren`. |
| `gui status` / `gui list` / `gui use <n\|auto>` | Bridge state (captured states, target, GUI thread, pending, errors) / every captured GameFace state (probed for `g_HUD`) / pick one. |

Example: `f2 ui tree Layer 2`, then `f2 ui scale Minimap 1.5; ui move Minimap -40 20`, `f2 ui export`, `f2 ui reset all`.

### Rendering

| Command | Usage | What it does | Notes |
|---|---|---|---|
| `engine` | `engine [field] [value]` | Lists `Debug.EngineConfig`, or reads/sets one field. | |
| `shadows`, `tonemap`, `lightmaps`, `prt`, `pointlights`, `spotlights`, `whitetex`, `trees`, `mist`, `repeatedmeshes` | `<command> [on\|off]` | Toggles that `EngineConfig` switch (no argument flips it). | Field names come from the startup scripts. |
| `vis` | `vis <mode>\|off\|list` | Engine visualisation modes: lightcache lightcount lightpass lightzone lod sortprim vertexcost shadowcasters. | |
| `lightdebug` | `lightdebug off\|nomain\|nolightmaps\|noambient\|nolocal\|missing` | Lighting debug functions from `StartupConsoleScript.lua`. | May only exist when that script ran. |
| `particles` | `particles [on\|off]` | Particle updates. | |
| `entdebug` | `entdebug [on\|off]` | Per-entity debug info. | |
| `facing` | `facing [who]` | Draws an entity's facing direction (default target). | `Debug.DrawEntityFacing(entity)`. |
| `generators`, `combatdraw`, `memstats` | | Creature generators, combat debug drawing, memory stats. | |

### Entities

| Command | Usage | What it does | Notes |
|---|---|---|---|
| `inspect` | `inspect [who]` | Name, id, position, distance to hero, health, invulnerability, gold, can-fight (default: target). | |
| `tp` | `tp <who>` | Teleports the hero next to an entity. | |
| `bring` | `bring <who> [distance]` | Moves an entity next to the hero (default 2 m). | |
| `near` | `near [radius] [type]` | Lists entities around the hero (default 10 m). | `Debug.GetAllEntitiesAroundHero(type, range)`. |
| `flee` | `flee [who]` | Makes a combatant flee (default target). | `Debug.MakeFlee(combatant)`. |
| `control` | `control [who]` | Takes control of a creature. | `Debug.ControlCreature(dood, targ)`; experimental. |
| `teleportent` | `teleportent <entityname>` | Moves a named entity to the player. | `Debug.TeleportEntityToPlayer(entityname)`. |
| `follow` | `follow` | Makes the target follow the hero. | |
| `combat` | `combat <who> fight\|flee\|attackable on\|off` | `Combat.SetCanFight` / `SetCanFlee` / `SetCanBeAttacked`. | |
| `damage` | `damage <who> <multiplier>` | `Combat.SetDamageMultiplier`. | Same shape as the scripts. |
| `dog` | `dog here\|pos\|bark\|growl\|whimper\|worms on\|off\|god on\|off\|inspect` | The hero's dog. | |

### Debug functions

| Command | Usage | What it does | Notes |
|---|---|---|---|
| `dbg` | `dbg list [filter]` / `dbg <Name> [args]` | Lists every function in the `Debug` table, or calls one (case-insensitive name). | Covers the ~250 Lua-defined debug helpers the game ships. |
| `probe` | `probe <path> [args]` | Calls a function with `(hero, args)` then `(args)` and reports which shape worked. | The fastest way to confirm an argument shape. |
| `quick` | `quick <name> [args]` / `quick list [filter]` | About 80 one-word shortcuts for the game's own debug helpers (zombies, trolls, bandits, daggers, marryall, winter, openshops, beamup, ...). | `quick list` shows each name, the function and its parameter names read from the scripts, e.g. `daggers=HouseOfFlyingDaggers(num weapon_name)`. Several change the save; use a throwaway save. |
| `travel` | `travel <place>\|list` | Loads a region at a start marker the game's scripts use: bowerstone, slums, colosseum, bloodstone, bowerlake, brightwood, fairfax, ravenscar, spire, temple, westcliff, wraithmarsh, caves... | `Debug.LoadLevel("Albion", region, marker)`. |

### Places

| Command | Usage | What it does | Notes |
|---|---|---|---|
| `place` | `place set <name> <x> <y> <z>` / `place del <name>` / `place export` | Defines or removes a warp place; `export` prints `place set` lines. | Put exported lines in `console\autoexec.txt` and the places survive restarts. |
| `near_place` | `near_place` | The saved place closest to the hero. | |

Status: console-v6/v7 were checked in a mock harness (fake engine tables) only, and v7's argument shapes were matched against the game's compiled scripts. Nothing has run in the game yet.