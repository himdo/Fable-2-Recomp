# Fable 2 Developer Console: Command Reference

> **console-v4 usage:** every command below is typed in the SDK's `~` console behind the word `f2`, for example `f2 warp 1 2 3` or `f2 god on`. Chain with `;` inside one line: `f2 warp save a; wait 60; warp a`. `f2-console.cmd` and its separate window are no longer needed, and nothing runs until the first `f2` command is typed. `find <word>` is the name search; the SDK's own `find` and `echo` commands are untouched.

Source: `console/build/console_cmds.lua` (console-v2). Every command below is registered with `add()` in that file.

## How the console works

- Double-click `f2-console.cmd` to open the `f2>` prompt. Each line you type is passed to the game. If the game is not running, the script starts it with the console enabled.
- The game ignores commands unless it was started through `f2-console.cmd` or has `console_enabled=true` set.
- Replies go to the game log as `[console] ...` lines (via `cprint`). The `f2-console.cmd` window shows the last few of them.
- A leading `!` (for example `!info`) also shows the reply in a message box.
- If the first word is a command, that command runs. Anything else runs as Lua. Lua results print as `= <value>` and errors as `error: ...`.
- Command names are case-insensitive. Arguments are split on spaces.
- Engine calls print `<label>: ok -> <values>` on success, `<label> failed: <error>` on error, or `<label>: not available in this game build` if the function is missing.
- Typed values are converted as follows: numbers become numbers, `true`/`on`/`yes` become `true`, `false`/`off`/`no` become `false`, `nil` becomes nil, and anything else stays a string.
- Hero lookup tries `QuestManager.HeroEntity`, then `Debug.GetHero`, then `GetPlayerHero`. Commands that need the hero print `no hero (load a save first)` when none is found.
- **Name clashes:** a line whose first word is a command runs that command, even if you meant Lua. For example, `level = 3` runs `level`. Write `lua level = 3` instead.

**args unconfirmed** marks a command that passes arguments to an engine `Debug.*` or `Timing.*` function. The script header says these argument shapes are not all confirmed, and the script does not record which ones are. Commands whose Debug/Timing calls take no arguments are not marked.

## Info

| Command | Usage | What it does | Notes |
|---|---|---|---|
| `help` | `help [command]` | Lists all commands. With a name, shows that command's usage line. | Also prints a reminder about `!` and Lua. |
| `pos` | `pos` | Prints the hero position as `pos x y z` (3 decimals). | Prints `no position` if the hero cannot be read. |
| `level` | `level` | Prints the loaded level name. | Calls the global `GetLevelLoaded`, not a Debug function. |
| `info` | `info` | Prints hero position, level, time of day, game time and hero target. | Calls `Timing.GetTimeOfDay`, `Debug.GetTime` and `Debug.GetHeroTarget`, all with no arguments. Each part reports on its own. |
| `target` | `target` | Prints the entity the hero is targeting, and its position. | Calls `Debug.GetHeroTarget` with no arguments. Prints `no target` when there is none. |
| `ent` | `ent <name>` | Finds an entity by name and prints it and its position. | args unconfirmed (`Debug.GetEntityWithName`). Does not move the hero. |

## Warp

| Command | Usage | What it does | Notes |
|---|---|---|---|
| `warp` | `warp <x> <y> <z>` | Moves the hero to the given coordinates. | Sets the position on the hero entity directly (`SetPosition(CVector3)`), not through a Debug function. |
| `warp` | `warp save <name>` | Saves the hero's current position under `<name>`. | Names are lowercased. Places are held in memory (`__f2_console`) for the session. Nothing in this script writes them to disk. |
| `warp` | `warp <name>` | Moves the hero to a saved place. | Uses the saved position. |
| `warp` | `warp list` | Lists saved places, sorted. | Prints `places: (none)` when there are none. |
| `warp` | `warp level <levelname>` | Loads the named level. | args unconfirmed (`Debug.LoadLevel`). |
| `warp` | `warp rel <dx> <dy> <dz>` | Moves the hero by an offset from its current position. | Position-only; not a Debug call. |
| `gotoent` | `gotoent <name>` | Teleports the hero to a named entity. | args unconfirmed (`Debug.GetEntityWithName`). Prints `entity not found: <name>` on a miss. |
| `up` | `up [metres]` | Moves the hero straight up. Default 5. | Handy when stuck in geometry. Position-only; not a Debug call. |

## Hero toggles

These toggles are not all hero-specific. `magic`, `orbs` and `tutorials` are global. `gui`, `names`, `navgraph`, `debugpage` and `combatdebug` are debug display switches.

For `[on|off]` toggles, no argument or any value other than `off`/`false`/`no` turns the setting on.

| Command | Usage | What it does | Notes |
|---|---|---|---|
| `god` | `god [on\|off]` | Hero god mode. `god on all` (or `god off all`) also sets henchman god mode. | args unconfirmed (`Debug.SetHeroGodMode`, `Debug.SetHenchmanGodMode`). Henchman is set only when the third word is `all`. |
| `freecam` | `freecam [on\|off]` | Free camera. With no argument, flips the current state. | args unconfirmed (`Debug.SetUseFreeCamera`). Reads state with `Debug.GetUseFreeCamera`, which takes no arguments. |
| `gui` | `gui [on\|off]` | Draws the HUD. | args unconfirmed (`Debug.SetDrawGUI`). |
| `names` | `names [on\|off] [filter]` | Draws entity names over entities. Optional filter. | args unconfirmed (`Debug.SetDrawEntityNames`, `Debug.SetDrawEntityNamesFilter`). The filter is passed as typed. |
| `navgraph` | `navgraph [on\|off]` | Draws the navigation graph. | args unconfirmed (`Debug.SetDrawNavGraph`). |
| `debugpage` | `debugpage <n>` | Sets the on-screen debug page. | args unconfirmed (`Debug.SetDrawDebugPage`). With no number, sets 0. |
| `combatdebug` | `combatdebug [on\|off]` | Draws behaviour names and combat group debug. | args unconfirmed (`Debug.SetDrawBehaviourNames`, `Debug.SetDrawCombatMultiplier`). Combat multiplier is set to 1 when on and 0 when off. |
| `magic` | `magic [on\|off]` | Global magic enabled. | args unconfirmed (`Debug.SetGlobalMagicEnabled`). |
| `orbs` | `orbs [on\|off]` | Experience orbs. | args unconfirmed (`Debug.SetExperienceOrbsEnabled`). |
| `tutorials` | `tutorials [on\|off]` | Tutorials on or off. | args unconfirmed (`Debug.SetTutorialsEnabled`). |
| `lod` | `lod low\|full\|off` | `low` forces low LOD. `full` sets full LOD for all creatures. `off` clears both. | args unconfirmed (`Debug.ForceLowLoDLevel`, `Debug.SetFullLODForAllCreatures`). Any other word prints the usage. |
| `fast` | `fast [on\|off]` | Calls `Debug.MakeGameGoFast`. | args unconfirmed. The script does not say what the function changes. |

## Stats and items

| Command | Usage | What it does | Notes |
|---|---|---|---|
| `herolevel` | `herolevel <n>` | Sets hero level. | args unconfirmed (`Debug.SetHeroLevel`). Needs a number; otherwise prints the usage. |
| `will` | `will <n>` | Sets hero Will. | args unconfirmed (`Debug.SetHeroWill`). |
| `strength` | `strength <n>` | Sets hero Strength. | args unconfirmed (`Debug.SetHeroStrength`). |
| `morality` | `morality <n>` | Sets hero morality. | args unconfirmed (`Debug.SetHeroMorality`). Usage text: 1000 good, -1000 evil, 0 neutral. |
| `purity` | `purity <n>` | Sets hero purity. | args unconfirmed (`Debug.SetHeroPurity`). Usage text: 1000 pure, -1000 corrupt, 0 neutral. |
| `age` | `age <n>` | Sets hero age. | args unconfirmed (`Debug.SetHeroAge`). |
| `fat` | `fat <n>` | Sets hero fat. | args unconfirmed (`Debug.SetHeroFat`). |
| `tan` | `tan <n>` | Sets hero tan. | args unconfirmed (`Debug.SetHeroTan`). |
| `skill` | `skill <name> <n>` | Calls `Debug.SetHeroSkill(name, n)`. `n` defaults to 1. | args unconfirmed (stated in the script usage). The name goes through the typed-value conversion. |
| `gold` | `gold <n>` | Adds gold, or subtracts it if negative. | Uses `Money.Modify` on the hero. Not a Debug or Timing function, so not marked. Needs a hero. |
| `giveall` | `giveall <category>` | Adds every item of a kind. Categories are listed below. | `giveall list` or `giveall` alone shows the categories. An unknown name prints `unknown category (giveall list)`. Only `wear` passes an argument (`true`); args unconfirmed for that category. |
| `weapon` | `weapon <name>` | Gives the hero a weapon by name. | args unconfirmed (`Debug.GiveHeroWeapon`). |

### giveall categories

`giveall list` prints these names in alphabetical order. Each category calls the listed `Debug.*` function. Only `wear` passes an argument (`true`), so args unconfirmed applies to that row alone.

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
| `wear` | `Debug.SetHeroWearingAllClothing` | Called with `true`. args unconfirmed. |
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
| `time` | `time [hour]` | With no argument, prints the time of day. With an hour, sets it. | No argument calls `Timing.GetTimeOfDay` with none. Setting calls `Timing.SetTimeOfDay(hour)`, args unconfirmed. The usage text assumes hour 0-24. |
| `ffwd` | `ffwd <hour>` | Fast-forwards the time of day to an hour. | args unconfirmed (`Timing.FastForwardTimeOfDayTo`). Needs an hour. |

## Spawning and cleanup

| Command | Usage | What it does | Notes |
|---|---|---|---|
| `spawn` | `spawn <name> [count]` | Creates an entity next to the hero. Repeats `count` times (default 1). | args unconfirmed (`Debug.CreateEntityByHero`). Stops at the first failure. |
| `spawnat` | `spawnat <name> <x> <y> <z>` | Creates an entity at a position. | args unconfirmed (`Debug.CreateEntityAtPosition`). The position is built as a `CVector3`. |
| `creatures` | `creatures <family> [count]` | Spawns a creature family. `count` defaults to 1. | args unconfirmed (`Debug.SpawnCreatureFamily`). |
| `combatants` | `combatants [n]` | Creates `n` combatants around the hero. Default 3. | args unconfirmed (`Debug.CreateCombatantsAroundHero`). |
| `killall` | `killall` | Destroys all creature entities. | Calls `Debug.DestroyAllCreatureEntities` with no arguments. |
| `destroytype` | `destroytype <type>` | Destroys all entities of a type. | args unconfirmed (`Debug.DestroyAllEntitiesOfType`). |
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
f2> pos
[console] pos 412.000 -87.500 3.250
f2> warp save bridge
[console] saved place bridge
f2> warp rel 0 0 40
[console] warped to 412.00 -87.50 43.25
f2> warp bridge
[console] warped to 412.00 -87.50 3.25
f2> warp list
[console] places: bridge
```

### 2. Debugging combat

```
f2> god on all
[console] hero god: ok
[console] henchman god: ok
f2> combatdebug on
[console] behaviour names: ok
[console] combat multiplier: ok
f2> combatants 4
[console] combatants: ok
f2> target
[console] target <entity>
f2> !info
(message box shows the info lines)
f2> killall
[console] killall: ok
f2> resetcreatures
[console] reset: ok
f2> combatdebug off
f2> god off all
```

### 3. Discovering an unknown function

```
f2> find hairstyle
[console] 1 matches
[console] Debug.AddAllHairstyles
f2> get Debug.AddAllHairstyles
[console] Debug.AddAllHairstyles = function: 0x...
f2> dump Timing
[console] N members
[console] ...
f2> call Debug.AddAllHairstyles
[console] Debug.AddAllHairstyles: ok
```

`giveall hairstyles` calls the same function. The `find` match count depends on the build.

## Host commands (console-v3)

These run in the game's C++ (`fable2_console.h`), not in Lua. They answer in the `f2>` window (via `console\out.txt`). Lines are queued in order; chain commands with `;`. Addresses are hexadecimal Xbox 360 guest addresses (with or without `0x`); values are big-endian as the game stores them. Raw Lua and `!` lines keep their semicolons.

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
