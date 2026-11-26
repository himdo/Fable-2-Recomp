# Difficulty settings: technical notes

**Status:** no PR yet · **Commits:** bd6846a, 4d14650 (probes), 891eab4 (names), c2215e2, 92583be (multipliers), bad29df (cards), on branch `feature/game-difficulty`, not on master · **Last checked against:** 7863041

## Goal

On New Game, after the boy/girl cards, the player picks one of four difficulties from cards. Then Theresa's "destiny" line plays. The level scales:

- gold gained;
- prices;
- enemy damage;
- enemy health;
- XP gained.

**Easy is the original game** (every multiplier 1.0) and the default; the other levels only make it harder (decision 6, 2026-11-22). On top of that:

- **Hardcore** removes the ways back to an older save.
- **The pause menu** shows the level and can change it (Hardcore is locked).
- **The save slot label** shows it, for example "Hero 1 (Easy)".

The plain-language version, with the decisions and open questions, is in [overview.md](overview.md).

**Branch:** `feature/game-difficulty`. Unmerged PRs (`feature/context-docs`, `feature/claude-files`) are merged into it, because the feature will take a while to reach master.

## Design

Still at the research stage: there is no feature code yet.

- Sections 1–11 come from read-only search passes on 2026-11-15, over the generated code, the decrypted `default.xex` and the game data. Their addresses were not all re-checked by hand.
- Sections 12–13 (2026-11-20) and 14 (2026-11-21) re-checked them by hand and in game.

**Where sections disagree, the later one wins.** Functions are named by their names in `fable_2_manifest.toml`; those still called `sub_` aren't understood well enough to name.

Labels:
- **Verified:** read in the generated code or asm, the scripts or the data, or seen in game.
- **Reported:** someone said it, or it comes from notes or outside sources (older sections call this "Plan claim").
- **Inference:** reasoned, not tested.

Generated code lives in `generated/default/fable_2_recomp.N.cpp` (written `.N` below). Script content is paraphrased.

### How the data was read

- **Scripts.** `gamescripts.bnk`, `gamescripts_r.bnk` and `guiscripts.bnk` were read in memory with `tools/bnk.lua`, which is a Python module despite its name, together with `tools/lua_callshapes.py`. `scripts\quests\gameflow.txt` ships as plain Lua source inside the bank.
- **Strings and constants.** These came from `default.xex`, decrypted in memory with Windows BCrypt.
- **`language/.../book.babel`** (format worked out by the search pass):
  - Header: `u32 0x5B010000`, then `u32 n`.
  - Index: `n-1` entries of 12 bytes, `{u32 key, u32 blockTag, u32 offsetInBlock}`, sorted by key.
  - Blocks: `{u32 tag, u32 compressedLen, u32 uncompressedLen, zlib (sync-flushed)}`.
  - Strings: `u32 charCount` followed by UTF-16BE text.
  - **Key** = 32-bit FNV-1 hash of the `TEXT_…` / `GUI_…` symbol name. This was checked against several Theresa lines and `GUI_DEFAULT_SAVE_GAME_NAME`.
- **Helper scripts** were throwaway (not in the repo): a bank reader, a babel reader, an in-memory XEX decrypter, a string peeker and caller/call-site scanners. Recreate them as needed.

### 1. Built-in difficulty system (Verified)

- **Lua API.** `Player.SetDifficultyLevel`, `Player.GetDifficultyLevel` and `Player.HasDifficultyLevelChanged` (strings from 0x820AC7C8). The player-state field names `DifficultyLevel` and `DifficultyLevelChanged` are at 0x820AD144.
- **Inference:** these fields are saved with the hero.
- **Tweak record.** `HeroCombatBalanceParams.DifficultyLevelMultipliers` (field names from 0x820ACFD4). The field names also appear in `data/Globals/globals.gdb`, which is binary and has **not been decoded**.
- **Reader.** `Difficulty_GetMultiplier_823C92F0(int idx)` (`.185:4150`) reads the record through `ProcessAndProcessAndProcess2670_82196E10(record, name)`, which returns the float or 0.0. Any other index returns 1.0.

| idx | Field | Applied in | How |
| --- | --- | --- | --- |
| 0 | `DamageCaused` | `Combat_ScaleDamageDealt_826A41C8` @0x826A456C | `hit+56 *= mult` |
| 1 | `DamageReceived` | `Combat_GetDamageTakenFactor_8249F298` @0x8249F638 | `f31 /= mult` |
| 2 | `ExperienceReceived` | `Experience_ScaleAward_8251CDD8` @0x8251CF3C | |
| 3 | `SpellManaCost` | no callers | dead |
| 4 | `StockItemPrice` | buy `Item_GetBuyPrice_828E7F78` @0x828E8294; sell `Item_GetSellPrice_828E8320` @0x828E84B0 | buy ×, sell ÷ |

- **The gate.** Every caller checks `playerComp(type 33)+80 > 1` and uses 1.0 otherwise. So on Normal the reader isn't even called.
- **Achievement.** `gameflow`: if `GetDifficultyLevel(Hero) > 1` and `not HasDifficultyLevelChanged(Hero)`, then `ACHIEVEMENT_MASOCHIST` unlocks.
- **No menu.** There is no difficulty item in `optionsscreen.fac`.
- **Console.** `f2 stats` prints the level (`src/lua/console_cmds.lua:~1110`). Calling it without the hero argument crashes.
- **Who it affects (Inference).**
  - `Combat_ScaleDamageDealt_826A41C8`, called from `sub_826A33A8`, scales a hit when the **attacker** has the player component.
  - `Combat_GetDamageTakenFactor_8249F298`, called from `sub_8268DEF0` and through it the hit handler `Combat_ApplyHit_8268AB30`, computes the hero's damage-taken factor from `Toughness`, `BarkskinMultiplier` and `BerserkValues.DamageTakenMultiplier`, and stores it at `entity+2164`.
  - Risk: co-op player 2 also has component 33.
- **Hook options.**
  1. Strong-override `Difficulty_GetMultiplier_823C92F0` to return our values per index. This only helps above Normal, because of the gate.
  2. Mid-asm hooks at the multiply sites, which work on every level:
     - `0x826A457C` (damage caused)
     - `0x8249F644` (damage received)
     - `0x828E82A0` (buy)
     - `0x828E84BC` (sell)
  3. Mid-asm hooks at each `lwz r11,80(rX); cmpwi r11,1` gate.

### 2. Gold (Verified)

#### The choke point

`MoneyComp_Modify_8239E698(moneyComp r3, int delta r4, r5, r6, r7)` (`.35:3966`):
- It does `comp+12 += delta` and clamps to [0, 999,999,999] (0x8239EA5C–0x8239EA98), then posts a change message.
- When delta > 0 with r6 = 1 and co-op active, it takes the share (the float at a global's `+176`) and recurses with r5 = 1.

#### Lua bindings

Registered in `LuaMoney_Register_8239EEE0`:
- `Money.Get` is 0x830418B0, which reads `+12`.
- `Money.Modify` is `LuaMoney_Modify_8239EE98`, which calls the choke point with r5 = 0, r6 = 1, r7 = 0.
- `Money.Set` is `LuaMoney_Set_8239EEA8`, a direct store plus clamp. It **bypasses** the choke point. It is used in `qc010` and `qo150`, and in `onactionusetakeitems` (to zero the chest).

#### Lua callers

There are 52 `Money.Modify(hero, n)` sites in 32 files:
- Quest rewards: `qr_assassinationglobal`, `qr_creaturehunter`, `qr_slavetrader`, `qr_archaeologyglobal`, `qc120_arena`, `qo890_colosseum`, `qo660`, `qo820`.
- Jobs: `qj001` to `qj004` and `barmanjobgroupmind`.
- Gold pickups: `onactionusepickup`.
- Chests: `onactionusetakeitems`.
- Begging: `behaviournoticeexpression`.
- Extortion: `behaviourrespondtocrime`.
- Paying to sleep, a negative amount: `onactionusebed`.

Corrected 2026-11-24: `Gameflow.ChildhoodVars.GiveGold` was listed here, but it uses `Money.Set`, not `Money.Modify` (section 19.1).

#### Native callers

There are 25. A `neg r4` before the call means the gold is being spent.

| Caller | What it does |
| --- | --- |
| `Shopkeeper_BuyItem_827AF850` | Shop buy: `-Shopkeeper_GetBuyPrice_827AF5A0` |
| `Shopkeeper_SellItem_827AFB58` | Shop sell: `+Shopkeeper_GetSellPrice_827AF6F8` |
| `Ownership_PayDueRent_828AEA50`, `Ownership_UpdateIncome_828AECD0` | Rent and income. These are the only callers with **r7 = 1**; `"RealEstateRentMedium"` appears here. |
| `Ownership_BuyProperty_828AF6D0` | Property buy: `-PropertyBuyPrice`, r6 = 1 (Verified 2026-11-22, see 15.3) |
| `Ownership_SellProperty_828B01C8` | Property sell: `+PropertySellPrice`, r6 = 1 (Verified 2026-11-22, see 15.3) |
| `Combat_ApplyHit_8268AB30` @0x8268B604 | "RichGoldPerHit" |

The other 17 callers were identified on 2026-11-22; see 15.1. In short:
- **Gains:**
  - quest rewards and pub games;
  - the wedding dowry and money items;
  - gift/reward objects and building income pickups;
  - orb gifts (received and refunded);
  - network replays and the co-op henchman's wage.
- **Spending:**
  - tattoo removal (listed as a gain before 2026-11-22);
  - coach/ferry fares and family upkeep;
  - the household allowance;
  - sending orb gifts.
- **Transfers:** two co-op moves of player 2's gold to player 1.

#### Rules for a gain hook (Inference; revised 2026-11-22, see 15.2)

- Scale only when `delta > 0`.
- Scale only when `comp+4 == Player_GetEntity_8222C210()` (player 1), the same check used in the choke point and in `HealthComp_Modify_8268BD90`.
- Lua-driven gold (jobs, fines, quest scripts, pickups) all arrives from one caller, 0x8239F998 in `LuaMoney_CallMethod_8239F830`, the `Money.Modify` glue (16.5). The caller can't tell those sources apart, so choose per source in the scripts or accept scaling all Lua gains.
- Skip the co-op share recursion by its **caller** (the two recursive `bl`s inside `MoneyComp_Modify_8239E698`, at 0x8239EA58 and 0x8239EC80; the second one's branch isn't read yet), not by r5 = 1. The henchman wage and the end-of-co-op transfer also pass r5 = 1.
- Skip network replays, where the owner's type-91 component has byte `+107` set. They take a short path that only adds and clamps, so the gold was already scaled on the machine that sent it.
- Don't scale transfers and refunds: `Coop_MoveGoldToPlayer1_82413620`, `Network_DropClient_8230FEB0`, and the orb-gift refund through `sub_8238D190`.
- Decide whether shop-sell income is excluded. Selling is already divided by `StockItemPrice`, so it would otherwise be scaled twice.

### 3. Prices (Verified)

- **Buy:** `Item_GetBuyPrice_828E7F78(itemComp, r4, f1, f2, f3)` (`.219:14334`). The price is the base value (`Item_GetBaseValue_828E7C58`) × the economy term (`Item_GetBuyOpinionFactor_828E8C80(f1, f2)`) × `item+36` × f3 (the shopkeeper multiplier) × `StockItemPrice` (level > 1 only) × the `item+60` discount. The result is floored at 0 and converted to int.
- **Sell:** `Item_GetSellPrice_828E8320`. The price is the base value × `item+40` × `Item_GetSellOpinionFactor_828E8EC0` ÷ `StockItemPrice`. A discount at `item+64` applies, and the result is clamped against the buy price and `item+108`.
- **Wrappers:** `Shopkeeper_GetBuyPrice_827AF5A0` (buy) and `Shopkeeper_GetSellPrice_827AF6F8` (sell) read f1 and f2 from component type 47 (+44/+48) on the shopkeeper. The default is at 0x820994B4. f3 is `shopkeeperComp+12`.
- ~~**Inference:** f1 and f2 are the village economy and wealth.~~ **Corrected 2026-11-22 (15.3):** type 47 is the opinion component, and `+44` / `+48` are its **Love** and **Fear** axes. So f1 and f2 are the shopkeeper's Love and Fear values.
- **`Shopkeeper.ApplyPriceMultiplier`** is `sub_827AFD40` (`comp+12 *= f1`). It is called only from `miscfunctions.lua`, next to the `EconomyManager` village-wealth calls.
- **Property prices** (`Property_GetBuyPrice_8274DD78` / `Property_GetSellPrice_8274DFD0`): analysed on 2026-11-22, see 15.3.

### 4. Damage and health (Verified unless marked)

#### The choke point

`HealthComp_Modify_8268BD90(healthComp r3, float delta f1, sourceEntity r5)` (`.171:10597`), which is also `Health.Modify`:
- It does `comp+20 += delta` and clamps to the max at `+28`.
- The damageable-by check is `sub_8268DC60`. The invulnerable flag is byte `+305`.
- God-mode bytes:
  - player 1 (`Player_GetEntity_8222C210`): `0x83496B27`
  - player 2 (`sub_828BEEC0`): `0x83496B3C`
  - everyone else: `0x83496B3D`

#### Bindings

| Lua | Native |
| --- | --- |
| `Health.Get` | `sub_824B5498` |
| `Health.GetMax` | `sub_82350F48` |
| `Health.SetMax` | `sub_8268F5B8` |
| `Health.GetBase` | `sub_8228AB70` |
| `Health.ModifyOverTime` | `sub_8268C578` |
| `Health.SetAsInvulnerable` | `sub_8268F6F8` |

#### Callers

- The hit handler `Combat_ApplyHit_8268AB30`, at 0x8268AF94 (`-f31`, attacker from `sub_825575C8`) and at 0x8268B964 (vampiric).
- `sub_8268D858`: leech, regen and over-time.
- Heal-to-max and max changes: `sub_822ABC20`, `sub_8268E920`, `sub_824A9698`.
- `sub_823A78E8` and `sub_826B5308`.

#### Per-entity damage multiplier

- `Combat.SetDamageMultiplier` is `sub_826F97E0` (`stfs f1,56(r3)`), registered in `sub_82795108`.
- **Inference:** the hit damage is `src+56 × src+60`.
- Only `ai\combatinfo.lua` sets it today.
- **Option:** set it per enemy at spawn from Lua. This needs no native hook, but it only scales damage, not health.

#### Enemy health

- **Inference:** the health values come from `globals.gdb`, which hasn't been decoded. `data/entity/` holds only `locomotion.loco`.
- **Options:**
  - scale enemy health with `Health.SetMax`, or override the health setting per entity at spawn;
  - divide incoming damage for non-player targets at `HealthComp_Modify_8268BD90`.

#### Rules for a damage hook (Inference)

- Scale only when `delta < 0`.
- When the target is player 1, scale by the enemy-damage factor. When the target is not a player, scale by 1 / enemy-health.
- Check the source (r5) and the faction, because allies, the dog and henchmen share this path. The faction component hasn't been identified yet.
- Leave out scripted kills (`Health.Modify(-1000000)`-style calls) and quest damage, for example by ignoring hits with a null source or very large amounts.

#### Console

In `src/lua/console_cmds.lua`:
- `god` / `godmode` (355–379)
- `hp`, `heal`, `kill`, `maxhp`, `invuln`, `cantdie`, `regen` (876–937)
- `damage <who> <mult>` (~1840), which uses `Combat.SetDamageMultiplier`
- `gold` / `money` (488, 1084)

`src/lua/modifyMoney.lua` also touches gold.

### 5. Saves (Verified unless marked)

#### The pause Save box

`sub_82842860` (`.51:13531`) builds it, and `sub_8240F040` (`.209:4472`) calls it. The title is `GUI_SCREEN_PAUSE_SAVING`. The buttons are:
- 0: `GUI_SCREEN_PAUSE_SAVE_AND_CONTINUE` (0x820DCF38)
- 1: `GUI_SCREEN_PAUSE_SAVE_AND_QUIT` (0x820DCF5C)
- 2: `GUI_SCREEN_PAUSE_QUIT_DONT_SAVE` (0x820DCF94)

#### The handler (Inference: the sub-code mapping still needs a live check)

`PauseSaveBox_HandleEvent_82413988` (`.236`, called from `sub_8240ED08`) handles event 46:

| Sub-code | Action | Button |
| --- | --- | --- |
| 23 | save (r7 = 1) | Save and Continue |
| 24 | save (r7 = 0), then `0x8233CAC8` | Save and Quit |
| 25 | `0x8233CAC8` only | Quit without saving |

`0x8233CAC8` is the quit-to-front-end routine (it uses `"QuitToFrontEnd(%s)"` at 0x820A6194). The manifest misnames it `InitializeGameEntity_8233CAC8`, and it has a hand-tuned override at `src/core/hotfunc/entity/InitializeGameEntity_8233CAC8.cpp`.

#### The save path

The Lua bindings are registered in `sub_824EC750` (`.49:6528`) on the save-manager object:

| Lua | Native | Behaviour |
| --- | --- | --- |
| `SaveGame` | `LuaSaveManager_SaveGame_824E8978` | calls `SaveManager_Save_824E8990(mgr, 0)` |
| `AutoSaveGame` | `LuaSaveManager_AutoSaveGame_824E8D28` | arg set: sets pending byte +2088; otherwise throttled by `FrameLastAutoSaved` (+2008) unless force byte +2089 is set, then calls `SaveManager_Save_824E8990(mgr, 1, 0, 0, 1)` |
| `SaveFailQuestSave` | `LuaSaveManager_SaveFailQuestSave_824E8D18` | sets byte +2090 |
| `SetSavingAsAllowed` | `sub_824EC748` | sets byte +2093 |
| `LoadFailQuestSave` | `LuaSaveManager_LoadFailQuestSave_824ECCB8` | |

- **`SaveManager_Save_824E8990`** (`.190`) is where every save goes. It first calls `sub_824EBF30`, which returns false when +2093 is 0. **This is the best single gate.**
- Other callers of `SaveManager_Save_824E8990`, roles not identified: `sub_82837610`, `PauseSaveBox_HandleEvent_82413988` (twice), `sub_82275F00`, `sub_82198860`, and `Network_DropClient_8230FEB0`.
- **Inference:** `Network_DropClient_8230FEB0` drains a pending-request queue and is called by the quit routine, so quitting may flush a queued save.

#### Script triggers

- `AutoSaveGame` is called in 18 scripts: the main quests qc010–qc240, qj001–003 and `generictriggers.lua` (chest and door unlocks, gold collection).
- `SetSavingAsAllowed` is used only in `qc200_bloodstoneassault`.
- `SaveFailQuestSave` is used in 5 quests. `SetToFailQuestSaveBeforeEnteringLevel` and `ClearFailQuestSaveLevel` are used in about 25.
- The script save system is in `scripts\miscellaneous\saveload\saveloadsystem.lua` and `postscriptsloaded.lua`.
- **Inference:** the `SAVE_CONTEXT_*` tags (shop, family, marriage, gifts, each quest) point to native autosaves on those events. The region, sleep and property autosaves **haven't been identified**; they are probably among the unidentified callers above.

#### Death

`Hero_DeathTracker:Update` (`gameflow`, around lines 3666–3764) shows the XP lost (`GUI_DEATH_XP`), resets health, adds a scar and clears the resurrection flag. It never saves or loads.

#### Ways back to an older save

- Sub-code 25, Quit without saving. Its loading screen is `GUI_LOADING_SCREEN_QUITING_WITHOUT_SAVE`.
- `LoadFailQuestSave()` (`gameflow` around line 661), from the leave-area reload prompt. It loads `failquestsave.bin`.
- The recomp's own front-end Quit (`src/menu/menu_items.h`), which closes the window.
- `f2 quitmenu confirm` (`console_cmds.lua:614`, `Debug.QuitToFrontEnd`). This is a console command, so it can be left as is.
- Closing the window. No autosave on close was found.

#### On disk

The paths are set in `OnConfigurePaths`, `src/core/fable_2_app.h:680-689`.
- Data folder: `<exe>/saves/B13EBABEBABEBABE/4D5307F1/00000001/Hero000/` holds:
  - `mainsave.bin`
  - `herosave.bin`
  - `chaptersave.bin`
  - `failquestsave.bin`
  - `saveuid.bin` (8 bytes)
  - `texturemorphs.bin`
  - `Fable2PubInfo.xml`
- Header: `.../4D5307F1/Headers/00000001/Hero000.header`.
- The slot name is the `HeroNNN` folder name.

#### SDK

In `thirdparty/rexglue-sdk/src/system/xam/content_manager.cpp`:
- The paths are built by `ResolvePackageRoot` / `ResolvePackagePath` (88–115).
- The game's only `XamContentCreateEx` call is wrapped by `sub_82CBD118` / `sub_82CBD240`. The three callers checked pass OPEN_EXISTING, so how the save folder is created is still unclear.
- **Risk:** with CREATE_ALWAYS or TRUNCATE (`xam_content.cpp:158-197`), `DeleteContent` runs `remove_all` on the whole `Hero000` folder (line 377).
- **Inference:** the header's timestamp matched the last save, which suggests the folder is recreated on every save.

#### Where to keep the level

- **The game's own per-hero `DifficultyLevel`.** It is already saved. Set it with `Player.SetDifficultyLevel`. This ties in with the Masochist question.
- **A Lua save-table entry** (`GetSaveTable` / `LoadFromSave` / the permanents in `saveloadsystem.lua`).
- **A sidecar file outside `Hero000`**, e.g. `saves/<xuid>/<title>/recomp/Hero000.toml`, keyed by the slot or by `saveuid.bin`. The title screen can read it without loading the save, which helps the slot label.

#### Hardcore design: decision 1, save on quit only (Inference, not built)

- Remove buttons 0 and 2 in `sub_82842860`, or reject sub-codes 23 and 25 in `PauseSaveBox_HandleEvent_82413988`. That leaves Save and Quit.
- Force a save before closing the window or using the recomp Quit: `SetQuitHandler` / `RequestClose` in `fable_2_app.h:171`. The save has to finish before the process exits, so check the save is complete, not just queued.
- Block `LoadFailQuestSave` (`LuaSaveManager_LoadFailQuestSave_824ECCB8`), and possibly stop `SaveFailQuestSave` so no fail-quest file exists.
- Leave autosaves and the +2093 gate alone. Hardcore doesn't block saving; it blocks going back.
- **Open:** what happens on a crash or a forced kill. The player loses progress back to the last autosave. That is a roll-back, but not one the player can choose.

### 6. Pause and options menus

- **Verified.** The layout is `art\gui\gameface\pausemainmenu.{fac,bgf,bsg}` (`guiscripts.bnk`). The population tables are in `scripts\gameface\expandablemenu\expandablemenupopulationscript.lua`:
  - `OptionsMenu`: Game / Video / Controls / Audio (`EVENT_TYPE_OPTIONS_*_ITEM`)
  - `PauseMenu`
  - `HenchmanPauseMenu`: Abilities / Config / Quit
- **Verified.** The Game options page (`optionsscreen.fac`) has Subtitles, Breadcrumb Size, Tutorial Boxes, Multiplayer Orbs and Joinable. The pause wheel icons are built natively (strings near 0x820DCFD4).
- **Verified.** Nothing injects into the pause or options menus yet. Two places touch menus today:
  - The front-end main menu: `src/menu/main_menu.cpp` strong-overrides `FrontMenu_Populate` (0x826CA6E0) and adds items through `ExpandableMenu_SpawnButton` (0x826CB808), which fires the Lua `g_Menu:SpawnButton`. `FrontMenu_HandleEvent` (0x826C81A0) matches items by name. Items are in `menu_items.h` and labels in `menu_labels.h`; "RecompQuit" is the only item.
  - `sub_826CA0B0` (`.240`) is the only other caller of `ExpandableMenu_SpawnButton`. Its role is unknown; it may be the options or pause population, which makes it worth a look.
- **Inference:** the pause-menu entry (decision 2) can be either:
  - an `OptionsMenu` or Game-page item, using the `ExpandableMenu_SpawnButton` pattern;
  - an ImGui panel like photo mode's (`src/core/photo_mode_dialog.h`).

### 7. New-game flow (Verified unless marked)

#### The screen

The `choosecard` layer of `art\gui\gameface\frontendmainmenu.bgf` (`guiscripts.bnk`):
- **Cards.** Boy and Girl, with front textures `cardboy` / `cardgirl` and the back `cardback`.
- **Slides.** `Intro`, `BoytoGirl`, `GirltoBoy`, `ChooseBoy`, `ChooseGirl`, `Outro`.
- **Behaviour chunks.** These only call `fireEvent`:
  - `behaviorboy` → `BOY_ACTIVE`
  - `behaviorgirl` → `GIRL_ACTIVE`
  - `behaviorboyselected` / `behaviorgirlselected` → `_SELECTED`
  - `behaviorunfreeze`
  - `behaviordialogactive`
- **Logic.** All of it is native. `scripts\gameface\frontend.lua` only handles title-screen slide navigation.

#### Native functions

| Function | Role |
| --- | --- |
| `FrontMenu_HandleEvent` 0x826C81A0 (`.167`) | New Game → `loc_826C863C`: `choosecard` slide 1, plays `FE_20` (~11467–11494) |
| `sub_826C61B8` (`.266` ~11187) | internal event 36 → slide 3, plays `FE_10` |
| `FrontMenu_HandleCardInput_826C72A8` (`.0:10435`) | input: toggles cards (slides 2–5). Confirm Boy goes to slide 6 and plays `FE_30_HM` (~11553); Confirm Girl goes to slide 7 and plays `FE_30_HF` (~11738). Both set `+24` = 1 while the card flips. |
| `FrontMenu_HandleUnfreeze_826C9460` (`.124`) | `UNFREEZE` replays per state `+104` (only while `+101` is set and `+192` / `+212` are clear): 0 with `+108` = 2 → slide 1 + `FE_20`; 3 / 4 with `+100` set → slide 6 / 7 + `_30_HM` / `_30_HF`; 5 with `+108` = 0 → load-card screen; then `+24` = `+108`. `DIALOG_ACTIVE` → `+24` = 6 (19.2) |
| `FrontMenu_HandleCardEvent_826C9188` (`.98`) | events: `BOY_ACTIVE` → 3, `GIRL_ACTIVE` → 4, `UNFREEZE` → 0. `BOY_SELECTED` → `FrontMenu_StartNewGame_8237A0C8("CreatureHeroChild")`; `GIRL_SELECTED` → `FrontMenu_StartNewGame_8237A0C8("CreatureHeroFemaleChild")` |
| `FrontMenu_StartNewGame_8237A0C8` | the actual new-game start (`GUI_SCREEN_FRONT_END_NEW_GAME`, `QC010_ChildhoodStart`). **The natural hook for the difficulty step.** |

**Inference:** the "destiny" line plays on the A press, together with the card flip, before the `_SELECTED` event.

**Helpers (Inference):**
- `Speech_PlayByKey_82306098` plays a speech line by key (r5), into a handle (r3) that the caller releases straight away with `Handle_Release_822ADE08` (19.2).
- `sub_823F8C30` + `sub_823F9140(r5 = slide)` look up an element and go to a slide.

#### Event and element globals

At `0x834A0000 + offset`; their initializers are around `sub_8325AF18`–`sub_8325BC28`.
- Speech: `FE_10` −20564, `FE_20` −20340, `30_HM` −20336, `30_HF` −20332.
- Elements: `choosecard` −20424, `loadgame` −20420, `SaveGameDescription` −20416.
- Event hashes: `BOY_ACTIVE` −20456, `GIRL_ACTIVE` −20452, `BOY_SELECTED` −20448, `GIRL_SELECTED` −20444, `LOAD_CARD_ACTIVE` −20440, `LOAD_CARD_SELECTED` −20436, `UNFREEZE` −20432, `DIALOG_ACTIVE` −20428.

#### Theresa speech

In `language/en-uk/speech/speech.bnk`, under `language\speech\TEXT\QUEST\QC010\FRONT\END\...`; subtitles in `book.babel`:

| Key | When it plays (paraphrased) |
| --- | --- |
| `TEXT_QUEST_QC010_FRONT_END_10` | The story begins (opening line) |
| `TEXT_QUEST_QC010_FRONT_END_20` | Asks who the hero of the tale is (the cards appear) |
| `TEXT_QUEST_QC010_FRONT_END_30_HM` | His destiny unfolds (boy chosen) |
| `TEXT_QUEST_QC010_FRONT_END_30_HF` | Her destiny unfolds (girl chosen) |

#### Unused lines

`TEXT_QUEST_QC000_UI_NEW_GAME_10`, `_20_HM|HF`, `_30_*` and `_40_*`: Theresa greets the player, corrects herself for a girl, says they remind her of another child, and offers to tell the hero's story (paraphrased). Also `QC000_UI_LOAD_GAME_*` (picking the story back up, with chapter recaps) and `QC000_UI_SAVE_GAME_10`. They have text and wavs, but their names appear in no exe or script bank. **Inference:** they were cut, or are referenced only by hash.

#### Plan for decision order: gender, then difficulty, then the line (Inference)

- Suppress the `FE_30_*` play in `826C72A8` (or the replay in `826C9460`).
- On `_SELECTED`, hold off on `FrontMenu_StartNewGame_8237A0C8` and show the difficulty cards.
- On the difficulty confirm, play `FE_30_HM` / `_HF` by key through `82306098`, store the level, then call `FrontMenu_StartNewGame_8237A0C8` with the saved creature name.
- Back/cancel from the difficulty cards returns to the gender cards.

### 8. Save-slot cards and label

#### Layout

The `loadgame` layer of `frontendmainmenu.bgf` (Verified):
- `BehaviorLoadCard` fires `LOAD_CARD_ACTIVE`; `behaviorloadselected` fires `LOAD_CARD_SELECTED`.
- `Card1`–`Card6` each hold:
  - `back` (`cardback`)
  - `front` (its material points at the placeholder video `testability.bik`)
  - `tarot_card_frame`
  - `screenshot` (`LoadingCardBack`)
- There is **one shared** `SaveGameDescription` text element (font "maiandra gd fable"), with placeholder text of about 50 characters.
- Then a `dialog` paper box with `yousure`, `GUI_CONTINUE_WITHOUT_SAVING` and Cancel.

#### Label code (Verified)

- `FrontMenu_SelectLoadCard_826C9008` (`.156:10839`) selects a card. It clamps to 6, sets slide 2 on the old card and slide 1 on the new one, plays a sound, then sets the `loadgame.SaveGameDescription` text from a string list on the frontend object (~+120) through `sub_823F8268`.
- The default name is `GUI_DEFAULT_SAVE_GAME_NAME` = "Hero %1". Its hash is computed in `sub_8324E640` (FNV-1 via `0x821F3C28`) and stored at `0x83498364`. It is used in `sub_824E4CD8` (`.181` around 6931 and 6968), which builds the save name.
- The main-menu item is internally Load Game but shows "Continue" (`GUI_SCREEN_FRONT_END_LOAD_GAME`).

#### Adding "(Easy)" (Inference)

- Strong-override `FrontMenu_SelectLoadCard_826C9008`, or `sub_823F8268` when the target is `SaveGameDescription`, and append the suffix to a **new** string. Don't patch the string in place: in-place relabels longer than the original crashed in the mod-menu work (`plans/main-menu-mod-item.md`).
- Or set `textstring` through the GUI Lua bridge (`src/core/fable2_gui_lua.h`, `docs/reference/gui_lua_bridge.md`).
- The level per slot must be readable without loading the save, which favours the sidecar file (section 5).
- A second line would need a new text element in the `.bgf` or a host overlay. The box width and wrap settings haven't been read.

#### Existing strings (Verified)

`book.babel` already has entries for the words difficulty (`0x5B3C36BA`), easy (`0xEB30AC72` / `…79`) and several for normal. Their symbol names are unknown, and they may belong to the pub games. There is no bare entry for hard, only a pub-game odds label that contains the word.

### 9. Texture candidates for difficulty cards (Verified)

Sizes come from `gui_texture_headers.bnk`; the textures themselves are in `art/gui/gui_textures.bnk`.

**512² format 39 (DXT5).** The format `photo_mode_icons.h` already decodes:
- `Art\GUI\FrontEnd\Textures\cardboy`, `cardgirl`, `cardback`, `chapter1`–`3`, `fe_elements`
- `Art\GUI\Frames\tarot_card_frame`, `paperA/B/C`
- `Art\GUI\QuestScreenShots\LoadingCardBack`
- `Art\GUI\LoadingScreens\Loading_Animation`

**Format 35** (may need a new decoder):
- `Art\GUI\AbilityTarot\*` at 256²: toughness, physique, skill, speed, accuracy, brutal, the spells, and `will_shock` at 512
- `Art\GUI\FrontEnd\Textures\PanoramicA–N`, `SpringA–F`
- `Art\GUI\QuestScreenShots\*_Epilogue_Screen_Image`
- `Art\GUI\Frames\frames_01/02/04`
- `Art\GUI\Persistant\frame_tiled_background`

**Other:** `Art\GUI\WheelIcons\dlc2\icons_cursed_skull` (a small icon, possibly for Hardcore).

**Inference:** for in-game cards, reuse `cardback` + `tarot_card_frame` with an ability-tarot or chapter picture on the front. `src/gui/bank_rebuild.h` can add new textures to the bank if custom art is ever wanted.

### 10. Repo patterns to reuse (Verified)

- **Mid-asm hooks.**
  - Declare them as `[[entrypoint.midasm_hook]]` in `fable_2_manifest.toml` (`address`, `name`, `registers`, `after_instruction`).
  - Implement them in `src/core/fable2_hooks.cpp`, taking `PPCRegister&`, and gate them on `fable2::config::Get()`.
  - Pattern to copy: `fable2_hook_disable_motion_blur(PPCRegister& f12)` (lines 113–125), which rewrites a float register.
- **Strong overrides.**
  - Define `extern "C" <name>` (the function's manifest name) and forward to `__imp__<name>`; this branch's `src/core/hotfunc/difficulty/Difficulty.cpp` does this for every hook it adds, and `src/core/hotfunc/vblank/FrameLimiterWait_82242628.cpp` is a hand-tuned example (`docs/reference/hotfunc_overrides.md`).
  - These suit `MoneyComp_Modify_8239E698`, `HealthComp_Modify_8268BD90` and `Difficulty_GetMultiplier_823C92F0`: adjust `ctx.r4` / `ctx.f1` before the call, or `ctx.f1` after it.
- **Config.**
  - Add a field to `Values` in `src/core/fable2_config.h` (~166).
  - Read it in `fable2_config.cpp` (577–625).
  - Add it to `kTemplate` and to `config/fable2_config.toml`.
  - A `[difficulty]` section must be added to `kKnownSections` (~468).
  - **Inference:** the per-save level shouldn't live in the global config. The config is the right place only for the per-level multiplier values.
- **Launcher.** `launcher/Fable2.Launcher/Fable2ConfigFile.cs` (read ~145, write ~202) and `MainWindow.xaml.cs` (`AddPatchRow` ~999–1042).
- **Front-end and UI.**
  - `src/menu/main_menu.cpp`, `src/core/fable2_gui_lua.h`.
  - `src/core/fable2_ui_events.h`: every `fireEvent`, fed from `OnGamefaceMessage` in `src/core/fable2_fit_window_ui.h` around line 779. The console `boot` command (`fable2_console.h` around 2008) already drives the title screen and slot selection with it.
- **Overlay.** `src/core/photo_mode_dialog.h`, `photo_mode_icons.h` and `photo_mode_font.h`: a game-styled ImGui menu that loads DXT5 textures and the game font.
- **Debug only.** `src/diagnostics/fable2_modmenu.h` and `fable2_menu_probe.h`.
- **No precedent.** No existing gameplay patch multiplies a value. The motion-blur float rewrite and the meter wrappers are the nearest.

### 11. Decisions (2026-11-15)

| # | Question | Options | Choice |
| --- | --- | --- | --- |
| 1 | Hardcore saving | save on quit only / autosaves only / leave open | **Save on quit only.** Autosaves stay; Save and Continue and Quit without saving are removed; every quit saves (Save and Quit, window close, recomp Quit); the fail-quest reload is blocked. |
| 2 | Mid-game change | free with Hardcore locked / fully free / lower only / leave open | **Free, Hardcore locked.** Easy, Medium and Hard can be switched from the pause menu; Hardcore is New Game only, with no entering or leaving later. |
| 3 | Built-in difficulty system | investigate first / own multipliers only / built-in as-is | **Investigate first.** It is the preferred route for damage and prices. Decode `globals.gdb` next. The Masochist side effect is still open. |

| 6 | Multipliers (2026-11-22) | steps like XP / gentler / steeper | **Steps like XP**, with **Easy = the original game** (delta4o's correction). Values in section 17. |

The remaining open questions (slot label format, Masochist, unused Theresa lines) are listed in `overview.md`.

### 19. The prologue, and feature code step 2: the difficulty cards (2026-11-24)

#### 19.1 The childhood gold and the music box (Verified in the script bytecode)

delta4o asked whether collecting the 5 gold and paying for the music box still works on the harder levels. It does, on every level, because none of that gold reaches our hooks. Read from `data/gamescripts.bnk` (the same bytecode is in `gamescripts_r.bnk`), with the `tools/bnk.lua` reader that `tools/lua_callshapes.py` uses; line numbers are the debug line info.

- **The counter.** `qc010_childhood.lua` sets `Gameflow.ChildhoodVars.Gold = 0` and `GoldNeeded = 5` (L317–318).
- **Earning.** `GiveGold()` (L158–169) adds 1 to the counter and does `Money.Set(hero, Money.Get(hero) + 1)`. `Money.Set` is `LuaMoney_Set_8239EEA8`, a store plus clamp that never calls `MoneyComp_Modify_8239E698` (section 2). So the gold hook never sees these coins, and no rounding happens.
- **The tasks.** Each calls `GiveGold()` once per playthrough (where there are two calls, they are the good and evil endings):

  | Task | Gold |
  | --- | --- |
  | qc020 Pose for Picture | 1 |
  | qc030 Beetle Hunter | 1 |
  | qc040 Bullies and Dog | 0 |
  | qc045 Love Letter | 1 |
  | qc050 Wanted | 1 |
  | qc055 Wino | 1 |

  That's 5 coins against a price of 5, with none spare. `TakeGold()` (L171–178) exists but nothing calls it.
- **Paying.** The music box is a scripted prompt, not a shop screen: `QC010_Murgo.BuyMusicBoxToaster` (L2657–2748) asks, and `CustomUpdate` state 14 plays the cutscene and calls `PayForBox()` (L2817), which does `Money.Set(hero, Money.Get(hero) - 5)`. So `Item_GetBuyPrice_828E7F78` isn't involved, and there's no money check.
- **The gates.** `QC010_Childhood.Update` (L666) and `QC010_Rose.CustomUpdate` (L1691) read the Lua counter, never the wallet. Rose's lines pick by the counter too.
- **Inference:** loose gold picked up in Old Town (`onactionusepickup`, `Money.Modify`) is scaled, but it only changes the wallet, which nothing in qc010 reads. Pickups placed in Old Town weren't looked for.

#### 19.2 The New Game cards

`src/core/difficulty_dialog.h` (drawing), `src/core/hotfunc/difficulty/{Difficulty.cpp,difficulty.h}` (logic).

- **The intercept.** `FrontMenu_HandleCardEvent_826C9188` starts the game on `BOY_SELECTED` / `GIRL_SELECTED`: it builds the creature name and calls `FrontMenu_StartNewGame_8237A0C8` with r3 = `*(*(*(0x83496928)+8)+80)`. That path reads only the event's hash at `+4`, never r3 (Verified in the generated code). The override holds the event back and opens the cards.
- **The start.** Once a card is chosen, the pad read (`on_pad_read` in `fable2_photo_mode.h`, user 0) replays the original handler with an 8-byte event `{0, hash}` on a fresh guest stack frame. It runs on the thread that sent the event (checked by thread id; after 2 s it runs anyway, and the log says `OTHER`). The game's own path then builds the name and starts the game.
- **The level.** After `FINISHED_TRANSITIONING_TO_GAME` (newer than the start), on the world update, the chosen level is written to player 1's type-33 `+80` and the type-57 `+2164` cache is cleared. It's written directly, **without** the changed flag `+38` that `Player.SetDifficultyLevel` sets, so the Masochist question (open question 5) stays open. Easy writes nothing and only logs the fresh hero's default level.
- **Input.** The dialog has its own counters, fed from the pad read, which also blanks the pad for the game while the cards are up and until it is released. The keyboard map reaches the same pad state, so the keys that drive the game's menus drive the cards.
- **Theresa's destiny line after the pick (built 2026-11-26, delta4o's request of 2026-11-24).** Read in the generated code:
  - The game plays the line from `FrontMenu_HandleCardInput_826C72A8` on the A press that flips the card, with `Speech_PlayByKey_82306098(r3 = an 8-byte handle on the stack, r4 = *(*(*(0x83496928)+8)+80), r5 = &key, r6 = 0)`. The boy's call returns to `0x826C7A50` (key at `0x834A0000 - 20336`, `FE_30_HM`), the girl's to `0x826C7B98` (`-20332`, `FE_30_HF`). `FrontMenu_HandleUnfreeze_826C9460` replays the same lines for card states 3 / 4 after a dialog (returns `0x826C9638` / `0x826C96B0`).
  - Each call is followed by `Handle_Release_822ADE08(handle)`, which does a vcall through `*(handle+4)` only when `+4` is non-zero, then zeroes `+0` and `+4`. Nothing reads the call's result.
  - The feature overrides `Speech_PlayByKey_82306098`. At those four return addresses it zeroes the handle and returns, so nothing plays and the release does nothing. Every other line goes to the original.
  - Once a difficulty card is chosen, the pad read (on the card thread) plays the line itself, with the same arguments, and releases its handle. The key is picked from the held event (boy or girl).
  - The game itself plays the line on the A press and starts the game some time later, after the card has flipped. To keep that lead, the time from the skipped call to the `_SELECTED` event is measured and logged, and the start waits that long after our line (at most 4 s).
- **B on the New Game cards (built 2026-11-26).** Read in the generated code:
  - `FrontMenu_HandleCardEvent_826C9188` and `FrontMenu_HandleUnfreeze_826C9460` are both registered with the front-end object as `this` (code at 0x826C6D0C… and 0x826C6EB0 / 0x826C6ED0), so the `r3` held back with the `_SELECTED` event is the object both work on.
  - `+24` is the card screen's input state: New Game sets it to 2; `BOY_ACTIVE` / `GIRL_ACTIVE` (`FrontMenu_HandleCardEvent_826C9188`) set 3 / 4; the A press sets 1; UNFREEZE in the card handler sets 0.
  - The game's own way back to the gender cards is after its "overwrite a save?" dialog on New Game: it sets `+104` = 0, `+108` = 2, `+112` = 0 (just before `0x826C863C`), and the dialog's UNFREEZE then reaches `FrontMenu_HandleUnfreeze_826C9460`, which for `+104` = 0 and `+108` = 2 goes to slide 1 of `choosecard`, plays `FE_20` and sets `+24` = `+108` = 2, as New Game does. That only happens while `+101` is set and `+192` / `+212` are clear; with `+101` clear it goes to slide 8 instead (not decoded).
  - So B closes the cards, blanks the pad until B is released (B on the gender cards goes back to the main menu), and on the next pad read on the card thread writes those three fields and calls `FrontMenu_HandleUnfreeze_826C9460` with an UNFREEZE event on a fresh guest stack frame. The fields are logged before and after (`[difficulty] New Game: back to boy or girl (...)`).
  - The New Game cards now show "B Back" too.
- **Seen in game (2026-11-24, `fable_2_071.log` and `fable_2_072.log`):** four new games, on Medium (twice), Hard and Hardcore.
  - Each started on the thread that sent the card event, so the 2 s fallback never ran.
  - A fresh hero has game level 1 with the changed flag 0. After the start, the level was 2, 3 and 4.
  - The level was written 3.4–4.1 s after the start.
  - `stats` showed level 2 on the Medium game.
  - delta4o: the cards look good; backgrounds on the cards would be a nice iteration. The card back is loaded (`extra images: 37 of 37`) but drawn faintly, at 55% under a dark panel.
  - **The prompts follow the input device (built 2026-11-24, not tested yet):** at first the cards always showed the controller's A button. They now check whether the last input came from the keyboard or mouse (`ActiveDeviceIsSynthetic(0)`, the same check as the game's prompt swap in `fable2_keycap_swap.h`).
    - After keyboard or mouse input, they show a key cap with the key that `[input] keyboard_gamepad_map` binds to that button: the first one-letter key, else the first key that isn't a mouse button. With the default map that's "E" for A and "B" for B.
    - After controller input, they show the controller icons, as before.

#### 19.3 Changing the level in game (F8)

- **F8** opens the same cards over a running game (a pad shortcut isn't chosen yet). It was F7 at first, but the SDK already binds F7 to its achievements overlay (`bind_achievements` in `rex_app.cpp`), so each press toggled both (seen in game 2026-11-24). F3, F4, F5, F6, F7, F9, F12 and the backtick are all taken. The current level is marked.
- **Hardcore** can't be entered in game (its card says "New Game only"). On a Hardcore hero the cards only show the level (decision 2).
- **Applying.** On the next world update, a native call to `LuaPlayer_SetDifficultyLevel_823C9458` (our override, which also clears the damage-taken cache). That does set the changed flag, as the game's own command does.
- **Inference until tested:** the world update runs while the pause menu is open, so the change applies there too.
- **Seen in game (2026-11-24, `fable_2_075.log`, still on F7):** Medium → Hard, Hard → Medium and Medium → Easy. `stats` went 2 → 3, and B closed the cards without a change.
  - Right after Hard, rent was +5,958 → +4,171 (×0.7).
  - The diamond in the Bowerstone gift store (base value 6,250), shout-out test:

    | Store | Level | Expected | Seen |
    | --- | --- | --- | --- |
    | Owned | Hard | 3,938 × 1.3 | 5,119 ✓ |
    | Not owned (store sold) | Easy | the game's price | 5,207 |
    | Not owned | Medium | 5,207 × 1.15 = 5,988 | 5,988 ✓ |
    | Not owned | Hard | 5,207 × 1.3 = 6,769 | 6,769 ✓ |

    So buying in a shop the hero doesn't own is confirmed.

#### 19.4 The slot label on the Continue screen

- **The level per slot.** After each save made in this run (its `mainsave.bin` written in the last 30 s), `<slot folder>/recomp_difficulty.txt` gets the level name. This is done on the UI thread, every 2 s, from the level last seen in game. A slot with no file counts as Easy. **Seen in game 2026-11-24:** Hero001 Medium, Hero002 Hard, Hero003 Hardcore.
- **Slot names.** `Headers/00000001/HeroNNN.header` holds the slot name in UTF-16 big-endian at `+8` ("Hero 1") and the folder name in ASCII at `+0x108` ("Hero000"). Read on disk.
- **How the game sets the label.** `FrontMenu_SelectLoadCard_826C9008` calls `sub_823F8268(element, *(r3+20), &names[card])` with `names = *(r3+120)`. That sets the element's text object (`*(*(element+4))`, string slot `+140`). A string handle points to a ref-counted record `{+0 UTF-16BE chars, +4 length, +12 refcount}`, and the text object keeps a reference to it (`ProcessAndReplaceRefCounted_822B85C8`). Verified in the generated code. The text is the whole card description: "Hero 2 / Bowerstone Old Town", a line break, then the chapter text (seen in game).
- **What didn't work (2026-11-24):**
  - Reading `textstring` with `getAttribute` returns the layout's placeholder ("fdggdf dfg …"), not the live text.
  - Setting `textstring` has no visible effect.
  - A first native try crashed: a stale header (`Hero004`, its save deleted) gave a card whose record pointed into text, and "QC01" was read as a pointer.
- **In place (seen in game 2026-11-24, second try):** the level is written into the description record itself. " (Medium)" goes after the name, the rest moves right, and as many characters drop off the end of the chapter text, so the length and the allocation don't change. Every address is checked first (`VirtualQuery`: committed, writable, aligned).
  - Done in `FrontMenu_SelectLoadCard_826C9008`, Hero 2, 3 and 4 showed Medium, Hard and Hardcore, and Hero 1 showed Easy after being selected again.
  - But the first card shown on opening had no level: opening the screen sets card 0's text through another path. `SelectLoadCard` is then called with -1, which only plays a sound.
- **Current version:** the same in-place write, moved into `sub_823F8268` (the element-text setter that both paths use). It acts on any text that reads "<known slot name> / …" followed by a line break.
  - **Seen in game (screenshots, 2026-11-24):** "Hero 1 (Easy) / Bowerstone Market", "Hero 2 (Medium) / Bowerstone Old Town", "Hero 3 (Hard) / …".
  - The first card shows its level straight away on opening, also after B and Continue again (confirmed by delta4o on the 21:38 build).
- **Known problem:** the chapter text under the name loses as many characters from its end as the level adds, so its last word or two are cut off mid-word (seen on two slots), because the string can't grow in place. The fix is a new, longer string: find the game's wide-string constructor (or what the `+8` field of the record is, in case it's a capacity).
- **A cleaner way, later:** build a new string with the game's wide-string constructor (not found yet), or add the level where the description is built.

### 18. Feature code, step 1 (2026-11-22)

`src/core/hotfunc/difficulty/Difficulty.cpp`, strong overrides that call the originals (`__imp__`). There's no config key: Easy is the original game, so the level is the switch.

| Override | What it does |
| --- | --- |
| `Difficulty_GetMultiplier_823C92F0` | Returns idx 1 (DamageReceived) = enemy damage and idx 2 (ExperienceReceived) = XP. The other indexes return 1.0. It doesn't call the original (the shipped table is all 1.0). |
| `LuaExperience_Modify_8251C8E0` | Sets a thread-local flag around the original, so the XP index returns 1.0 for scripted XP (17.1). |
| `Item_GetBuyPrice_828E7F78`, `Item_GetSellPrice_828E8320` | Buy × prices, sell ÷ prices, rounded to the nearest coin, never below 1. |
| `Property_GetBuyPrice_8274DD78`, `Property_GetSellPrice_8274DFD0` | The same for property. |
| `MoneyComp_Modify_8239E698` | `delta > 0` to player 1 is × gold, except the eight return addresses listed in the file (shop and property sales, the co-op recursion, the two transfers, orb gifts, network messages) and network replays (type-91 `+107`). |

- **The level:** player 1's type-33 `+80`. 1 or lower is Easy, 2 Medium, 3 Hard, 4 or higher Hardcore. A fresh hero's default isn't read yet; anything ≤ 1 is Easy either way. Set it with `lua Player.SetDifficultyLevel(QuestManager.HeroEntity, n)`. That also sets the changed flag `+38`, which blocks the Masochist achievement (open question 5).
- **Logging:** `[difficulty] level ...` when the level read changes, and `[difficulty] gold +a -> +b from <caller>` for every scaled gain.
- **Jobs (checked 2026-11-22, delta4o's question):** promotion is unchanged. All three job scripts (`qj001` woodcutter, `qj002` blacksmith, `qj003` bartender) work out the pay themselves (blacksmith: `ceil(4 × level² × chain multiplier × (1 + 0.1 × Physique))`) and add it to their own `MoneyEarned` **before** `Money.Modify`. Our multiplier scales only what arrives. So promotion, which compares `TotalMoneyEarnedAs<Job>` with fixed thresholds (blacksmith 60 / 800 / 8,000 / 32,000; bartender 80 / 800 / 8,000 / 32,000; woodcutter 100 / 1,000 / 10,000 / 40,000), takes the same number of successes on every level. The job's "gold until promotion" counter, the `STAT_MONEY_MADE_JOB_*` tracked stat and the village-wealth boost (`MoneyEarned × 0.001`) use the unscaled pay; the "current gold" counter reads the real balance (Verified in the script bytecode). In game on Medium: 40 / 80 / 120 for the first three swords (47 / 94 / 141 × 0.85).
- **The damage-taken cache (found in game 2026-11-22):** `Combat_GetDamageTakenFactor_8249F298` returns the value cached at `+2164` of the hero's **type-57** component unless it is 0.0, and the game clears it on load. So a level change only reached the damage taken after a reload. The feature now also overrides `LuaPlayer_SetDifficultyLevel_823C9458`, which clears that cache after the original runs (the probe gave up its override of it).
- **The price pairs call each other (found in game 2026-11-22):**
  - `Item_GetSellPrice_828E8320` ends with `sell = min(max(raw, 1), Item_GetBuyPrice_828E7F78(...) × stock+108)`, rounded (Verified in the generated code);
  - `Property_GetBuyPrice_8274DD78` floors at `Property_GetSellPrice_8274DFD0`.

  The inner calls reached our overrides too, so the item sell cap was worked out from the *scaled* buy price. In an owned shop the cap is what binds (raw sell with the ×1.4 owner bonus, about 5,513 for the diamond, against a cap of about 4,048). So the ÷ undid the scaled cap, and sell prices didn't change on Medium or Hard: 4,048 on both. Hardcore showed 3,938 because the cap rose above the raw price.

  **Fix:** a thread-local depth counter (`PriceScope`). While one price is being worked out, the partner function returns the game's own value, and only the outermost call is scaled. So every level gets the game's Easy price × or ÷ its multiplier.
- **The game's own sell price moves.** The diamond's Easy sell price was 4,048, then 3,938 later the same evening, then 4,127 once the merchant had sold her only diamond. Inference: shop stock and village wealth feed into it. The shop screen keeps showing the old price until it is reopened.
- **In game (2026-11-22, `fable_2_065.log` and `fable_2_066.log`, Release build of this step plus the cache fix):**

  | Check | Level | Expected | Seen | |
  | --- | --- | --- | --- | --- |
  | Toy sword, own shop | Medium | 16 × 1.15 → 18 | 18 | ✓ |
  | Diamond, own shop (base value 6,250) | all four | the game's 3,938; × 1.15 → 4,529; × 1.3 → 5,119; × 1.4 → 5,513 | 3,938; 4,529; 5,119; 5,513 | ✓ |
  | Diamond sell, before the price-pair fix | Easy / Medium / Hard / Hardcore | 4,048 ÷ m | 4,048 / 4,048 / 4,048 / 3,938 | ✗, fixed |
  | Diamond sell, after the fix | Easy / Medium / Hard / Hardcore | 3,938 ÷ m | 3,938 / 3,432 / 3,029 / 2,813 | ✓ (Medium: the game's own price moved; rechecked as 4,127 → 3,589 ✓) |
  | Pie, own Pie Stall (the round-2 sale was undone by the reload) | Medium | 15 × 1.15 → 17 | 17, gold −17 | ✓ |
  | Blacksmith, 3 swords | Medium | 47 / 94 / 141 × 0.85 | 40 / 80 / 120 (from 0x8239F998) | ✓ |
  | Rent | Hard, then Medium | × 0.7 / × 0.85 | +5,958 → +4,171; +35,748 → +30,386; +5,958 → +5,064 | ✓ |
  | Golden touch on a kill ("RichGoldPerHit", 0x8268B608) | Medium, Hard | 50 × 0.85 / × 0.7 | +43 / +35 | ✓ |
  | Guard fine | Medium | unscaled | −250 | ✓ |
  | Hero's melee hit, Ghoul life-steal | Easy, Medium | unchanged | −157.70, +39.42 on both | ✓ |
  | Guard's hit on the hero | Medium | raw 21 × 1.5 | −31.50 (the same guard hit a villager for −21.00) | ✓ |
  | Guard's hit on the hero | Hard | 21 × 2.0 = 42 | the DamageReceived gate re-read at level 3 after the cache clear; the probe's 3-line limit hid the amount | gate ✓, amount not logged |
  | XP orbs | Medium, Hard | × 0.85 / × 0.7 | the XP gate fired at level 2 and 3; the amount isn't logged | gate ✓ |
  | Pie Stall sale screen | Easy / Medium / Hard | 2,125 / ÷ 1.15 / ÷ 1.3 | 2,125 / 1,848 / 1,635 | ✓ |

  The sale screen shows our cut as a bigger "Selling fees" line (−514 on Medium, base + economy − final).
- **Not tested in game:** a shop the hero doesn't own, buying property (the floor case of the price-pair fix), the Guild tutorial's scripted XP (needs a new game), co-op.
- **Not done:** enemy health (decision 0), the New Game cards, the pause-menu entry, Hardcore saving, the slot label.
- **Inference until tested:** the gates still read each component's own level, so a co-op player 2 at game level 1 isn't affected; prices are scaled for every caller of the item price functions.

### 17. The multipliers (decision 6, 2026-11-22)

| Level | Game level (type 33 `+80`) | Gold gained | Prices | Enemy damage | Enemy health | XP |
| --- | --- | --- | --- | --- | --- | --- |
| Easy | 1 | 1.0 | 1.0 | 1.0 | 1.0 | 1.0 |
| Medium | 2 (Inference: the mapping isn't built yet) | 0.85 | 1.15 | 1.5 | 1.25 | 0.85 |
| Hard | 3 | 0.7 | 1.3 | 2.0 | 1.5 | 0.7 |
| Hardcore | 4 | 0.65 | 1.4 | 2.5 | 2.0 | 0.65 |

Default and missing value: Easy (old saves and the New Game screen's first card).

What each column means for the code (Inference until built):
- **Easy needs no code path.** All five gates in section 1 skip at level 1, and the gold and price hooks pass 1.0 through.
- **Enemy damage** goes through the DamageReceived gate in `Combat_GetDamageTakenFactor_8249F298`, which does `f31 /= mult` on the hero's damage-taken factor (stored at `entity+2164`). The hit path (`sub_8268DEF0`) then divides the damage by that factor. So **damage taken = damage × DamageReceived**, and the gate gets the enemy-damage value itself (1.5 / 2.0 / 2.5). Corrected 2026-11-22: this said `1 / enemy damage` before; Verified in the generated code. It covers all damage the hero takes, not only from enemies (Inference). `Difficulty_GetMultiplier_823C92F0` has no level argument and the table is one row (13.1), so per-level values need an override or a hook that reads the hero's level. Don't feed the DamageCaused gate: the hero's own damage stays as it is, and so does the Ghoul life-steal worked out from it (16.2).
- **XP** goes through the ExperienceReceived gate in `Experience_ScaleAward_8251CDD8` (a plain multiply), but **only for XP the hero collects**. XP handed out by a script (Lua `Experience.Modify`) is not scaled (decision 6, 2026-11-22), because the Guild tutorial would get stuck otherwise (17.1).
- **Prices:** the StockItemPrice gate never runs for the hero (16.3), so shop prices need a hook on `Item_GetBuyPrice_828E7F78` (× prices) and `Item_GetSellPrice_828E8320` (÷ prices, as the game's own design divides). Property follows the same rule at the returns of `Property_GetBuyPrice_8274DD78` and `Property_GetSellPrice_8274DFD0`, which the real-estate screen also reads. `LuaPlayerProperties_PurchasePropertyQuietly_828AFBB8` stays free.
- **Gold gained:** a hook on `MoneyComp_Modify_8239E698` for `delta > 0` to player 1, following the rules in section 2. Earned gold is scaled, rent and pub-game winnings included. Skip shop sales (`Shopkeeper_SellItem_827AFB58`), property sales (`Ownership_SellProperty_828B01C8`), the co-op share recursion, network replays (type-91 `+107`), the transfers (`Coop_MoveGoldToPlayer1_82413620`, `Network_DropClient_8230FEB0`) and the orb-gift refund. Lua gold all arrives through `LuaMoney_CallMethod_8239F830` and is all earned gold, so it's scaled as one.
- **Enemy health** is still later (decision 0): it needs the friend-or-foe check.

#### 17.1 The Guild tutorial XP check (2026-11-22)

delta4o asked whether every level still gets enough XP at the Guild to buy a skill, since the tutorial asks for one.

- **The grant.** `qc060_newbeginning` sets `FreeWillExp = 350` in its init. At the Guild power-up ("MUSIC_QC060_HERO_POWERUP", after `HeroPowerUpThread` starts) it calls `Experience.Modify(hero, EXPERIENCE_WILL, self.FreeWillExp, false)` (Verified in the script bytecode).
- **The wait.** It then loops Theresa's `THERESA_SEAL_BUY_SPELL_WAIT_10/20/30` lines until `HeroBoughtWillAbility` is true, which means any of the eight Will spells at level 1 or more. Then it sets `HeroBoughtSpell` and the story moves on (Verified in the bytecode). There is no other way out of the loop.
- **The other three `Experience.Modify` calls** in that script (1000 general, strength and skill XP) only run with the quest's `DEBUG_MODE`, a developer skip to Thag's cave. `FABLE2_LUA_API.md` lists `Experience.Modify` in this one file only.
- **The Lua path is scaled.** `LuaExperience_Modify_8251C8E0` converts the amount to a float and calls `Experience_Add_8251C0A8` (r6 = 0, r7 = the 4th argument, r8 = 1). For a positive single-player grant, that calls `Experience_ScaleAward_8251CDD8` (at 0x8251C4B0). Verified in the generated code.
- **Ability costs** (`ExperienceCost`, read from `globals.gdb`; the level-to-level steps are ×6, ×6, ×4, ×3 for every ability, which confirms the field):

  | Will spell | Level 1 | 2 | 3 | 4 | 5 |
  | --- | --- | --- | --- | --- | --- |
  | Force Push | 300 | 1,800 | 10,800 | 43,200 | 129,600 |
  | Fireball | 350 | 2,100 | 12,600 | 50,400 | 151,200 |
  | Vortex | 400 | 2,400 | 14,400 | 57,600 | 172,800 |
  | Lightning, Summon Blades | 450 | 2,700 | 16,200 | 64,800 | 194,400 |
  | Raise Dead | 500 | 3,000 | 18,000 | 72,000 | 216,000 |
  | Slow Time, Chaos | 600 | 3,600 | 21,600 | 86,400 | 259,200 |

  Some Strength and Skill level-1 costs, for reference: Physique 600, Toughness 450, Speed 450.
- **Result with XP scaled:** 350 × 0.85 = 297.5, × 0.7 = 245, × 0.65 = 227.5. All are below Force Push's 300, so on Medium, Hard and Hardcore the hero can't buy a spell and the quest can't continue. Inference: the hero has no other XP at that point. The script gives none before the power-up, but this isn't checked in game.
- **Decision (delta4o):** don't scale scripted XP. A hook can tell the two apart by marking calls that come through `LuaExperience_Modify_8251C8E0` (for example a thread-local flag set around it), so `Experience_ScaleAward_8251CDD8` leaves them alone (Inference until built).
- **How `globals.gdb` was read** (throwaway script, not in the repo):
  - the file is the header `GDB\0`, then object records from 0x20, then the field layouts from 0x154A94 (7,389 parsed). Each layout is a word `00 00 nn ff`, then nn field-name hashes, then nn `type << 24 | slot` words;
  - then a sorted list of 71,374 words from 0x1C0A68, and a name table of `{u32 FNV-1 hash, zero-ended name}` entries;
  - an ability-level record holds `[layout id] [ExperienceCost] [parent] [NameTextTag hash] [DescriptionTextTag hash]`. It was found through the hash of its `GUI_SCREEN_ABILITIES_..._NAME_LEVEL_0n` tag. Toughness and Accuracy use other layouts with the same values.

### 12. Next research steps (rewritten 2026-11-20)

Status of the 2026-11-15 list:

| # | Step | Status |
| --- | --- | --- |
| 1 | `DifficultyLevelMultipliers` values | Not decoded from `globals.gdb` (the field names sit in a hashed name pool; the record layout is unknown). **Replaced by a live log** (13.6, check B1). Enemy health from gdb: still open, deferred with enemy health. |
| 2 | `SetDifficultyLevel(hero, 2)` in game | **Done** (13.8a): the level sticks and is saved, but the gates never fired. Why is the next step. |
| 3 | Save-box sub-codes 23/24/25 | **Verified statically** (13.2). Live use optional. |
| 4 | Native autosave callers | Partial (13.8a): sleeping makes no save; region change and house purchase still to test. |
| 5 | Save folder recreated? | **No.** Static (13.4) plus the live marker test (13.8a): a sidecar inside `HeroNNN/` is safe. |
| 6 | Faction component | **Deferred** with enemy health (decision 0). |
| 7 | Property prices | **Verified** (13.5): no difficulty gate. |
| 8 | `sub_826CA0B0` | **Verified** (13.5): Options → DLC list, not the pause menu. |
| 9 | `SaveGameDescription` box | Geometry not decoded from the `.bgf`; live check B5 via `ui get` / `ui label`. |

Still to do after the in-game checks:
- Find the game's spelling of "Guildmaster" (`book.babel`: the 2026-11-15 format notes didn't reproduce, see 13.7).
- Find the actual pause-menu (or Game options page) population point for the difficulty entry.
- Next step after research: enemy health and the friend-or-foe check.

### 13. Research step (2026-11-20)

All of it was read by hand in the generated code on `feature/game-difficulty` at `49c70ab`, unless marked.

#### 13.1 Multiplier reader and gate sites (Verified)

- **`Difficulty_GetMultiplier_823C92F0` takes only the field index.** It takes `r3` = idx and nothing else: it builds the record name `HeroCombatBalanceParams` / `DifficultyLevelMultipliers` (strings 0x820ACFD4 / 0x820ACFEC), switches on idx 0–4 and returns the float in f1. There is **no level argument**, so the table is **one row** applied to every level above 1. Per-level values must be ours. This answers decision 3: the game's *multiply sites* are the route, the game's *values* are only a reference.
- **The gate shape.** Each of the five callers looks up component type 33 (the player component) on the entity with a binary search. It then does `lwz r11,80(comp); cmpwi r11,1; ble → fallback`, otherwise `li r3,idx; bl Difficulty_GetMultiplier_823C92F0`. Both paths meet at a `loc_` label that holds the multiply. A mid-asm hook on that label instruction (before it executes) sees f1 = the multiplier on **every** level and can replace it.

| idx | Field | Function | Hook address (label) | Instruction | Fallback (level ≤ 1) |
| --- | --- | --- | --- | --- | --- |
| 0 | DamageCaused | `Combat_ScaleDamageDealt_826A41C8` (`.11`) | 0x826A457C | `lfs f0,56(r29)` then `fmuls f13,f0,f1` (+4), stored back to hit+56 | constant 1.0 at 0x820994C0 (`lis -32246, -27456`) |
| 1 | DamageReceived | `Combat_GetDamageTakenFactor_8249F298` (`.52`) | 0x8249F644 | `fdivs f31,f31,f1` | f30 |
| 2 | ExperienceReceived | `Experience_ScaleAward_8251CDD8` (`.268`) | 0x8251CF48 | `fmuls f31,f1,f31` | `lfs f1,0(r5)` (**not** a constant: the value at r5) |
| 4 | StockItemPrice, buy | `Item_GetBuyPrice_828E7F78` (`.219`) | 0x828E82A0 | `fmuls f31,f1,f31` | f28 |
| 4 | StockItemPrice, sell | `Item_GetSellPrice_828E8320` (`.59`) | 0x828E84BC | `fdivs f31,f31,f1` | f30 (`lfs f30,12(r31)`, r31 = 0x820994B4, so the same 1.0 at 0x820994C0) |

- **Verified:** 0x820994C0 holds 1.0 (read from the decrypted image). **Inference:** f30 and f28 at the other sites are 1.0 too (not traced).
- **idx 2 (XP)**: its fallback is read from r5, so the hook there must not assume 1.0.
- **Who.** When the entity has no player component, the code skips straight past the multiply (`beq` to the next label), so a hook at the label only ever sees player entities (player 1 or 2).
- **Inference:** for DamageCaused that entity is the attacker, for DamageReceived the target, and for prices the buyer or seller.

#### 13.2 Pause Save box (Verified)

- **The builder.** `sub_82842860` adds the buttons with `sub_82408338`, passing `r7` = button index and `r4` = the label: 0 = `..._SAVE_AND_CONTINUE` (0x820DCF38), 1 = `..._SAVE_AND_QUIT` (0x820DCF5C), 2 = `..._QUIT_DONT_SAVE` (0x820DCF94); r8 = 2 for all three.
- **The handler.** `PauseSaveBox_HandleEvent_82413988` (`.236:4435`): `r4` = event (46 = chosen; 47 is another path), `r5` = sub-code.
  - 23 → `SaveManager_Save_824E8990(mgr, 0, 0, 0, r7 = 1)`, stays in game.
  - 24 → plays a sound (`0x82306098`), then `SaveManager_Save_824E8990(…, r7 = 0)`, then the quit routine `0x8233CAC8`.
  - 25 → the same sound, then `0x8233CAC8` only.
  - `mgr` = `*(*(0x83496920)+12)+148` (lis -31927 + 26912).
- What each code does fixes which button it belongs to (23 Continue, 24 Quit, 25 Don't save), so the 2026-11-15 mapping holds.
- **Hardcore options.** Reject r5 = 23 and 25 in a wrapper of `PauseSaveBox_HandleEvent_82413988`, or skip the index 0 and 2 `sub_82408338` calls (`sub_82842860` is long, so the wrapper route is cheaper).

#### 13.3 Callers of the save function `SaveManager_Save_824E8990` (Verified list, roles pending)

| Caller | File | Arguments | Notes |
| --- | --- | --- | --- |
| `LuaSaveManager_SaveGame_824E8978` | `.57` | `(mgr, 0)` | Lua `SaveGame` |
| `LuaSaveManager_AutoSaveGame_824E8D28` | `.208` | `(mgr, 1, 0, 0, 1)` | Lua `AutoSaveGame` |
| `PauseSaveBox_HandleEvent_82413988` | `.236` | ×2 | Save box (13.2) |
| `sub_82837610` | `.163:13478` | `(mgr, 0, 1, 0, 1)` | only when `sub_824EBE60` is true; first does `sub_823B3E00` with a float |
| `Network_DropClient_8230FEB0` | `.251:3927` | `(mgr, 0, 0, 0, 1)` | only when `sub_8230FDD8` is true (the queue-drain inference stands) |
| `sub_82275F00` | `.256` | | |
| `sub_82198860` | `.75` | | |

- The roles come from the probe log (13.6, B3): each save logs its caller's return address.

#### 13.4 Save folder (Verified statically; on disk agrees)

- **The imports.** The game imports only `XamContentCreateEx` (0x832B24BC), `XamContentClose`, `…GetCreator`, `…CreateEnumerator` and `…GetDeviceState`. There is **no `XamContentDelete`**.
- **The callers.** `XamContentCreateEx` is reached only through `sub_82CBD118`, and that only from `sub_82CBD240`, whose three callers all pass flags **3 (OPEN_EXISTING)**:
  - `sub_824E62B8` (`.139`)
  - `sub_823C2270` (`.225`)
  - `sub_826C0460` (`.78`)
- **The wrapper's check.** `sub_82CBD118` also refuses any flags other than 3 when the content type's high bits are set.
- So the SDK's delete-and-recreate paths (CREATE_ALWAYS / TRUNCATE_EXISTING → `DeleteContent` → `remove_all`, in `thirdparty/rexglue-sdk/src/kernel/xam/xam_content.cpp:158-197`) are never taken by this game.
- **On disk.** In `Hero000`, `mainsave.bin`, `herosave.bin` and `chaptersave.bin` are from 2026-11-15, while `saveuid.bin`, `Fable2PubInfo.xml`, `failquestsave.bin` and `Headers/…/Hero000.header` are still from 2026-11-07. So saves rewrite single files and the folder survives. This corrects the 2026-11-15 inference.
- **Inference, open.** How a *new* slot folder is created, given that only OPEN_EXISTING is ever used. Possibly the SDK or app makes the folders (`OnConfigurePaths`).
- A sidecar inside `HeroNNN/` looks safe unless the game deletes unknown files itself. Live check B4 (marker files).

#### 13.5 Other static answers (Verified)

- **Property prices.** `Property_GetBuyPrice_8274DD78` (`.111:12190`) and `Property_GetSellPrice_8274DFD0` (`.268:12138`) do `price = int(base × tweak float)`. The float comes from `ProcessAndProcessAndProcess2670_82196E10` when the record has the field (`sub_829FB978` check), then it's stored via `Property_GetOpinionPriceAdjustment_8274D6D8`. There's no `+80` gate and no `Difficulty_GetMultiplier_823C92F0`, so they don't follow difficulty. A price hook would sit on the `fmuls` before each `fctiwz`.
- **`sub_826CA0B0`** (`.240:10345`, called from `sub_826CA388` and `sub_826C6C60`) walks a global list (0x833197BC: count +4, entries with a name at +88 and a flag at +92). It spawns one button per entry under `GUI_SCREEN_OPTIONS_DLC` (0x820CD3C4), next to `"Language"` (0x820CD3DC). It's the **Options → DLC** population, not the pause menu, but it's a native precedent for adding buttons to an Options submenu at runtime.
- **`SaveGameDescription`.** The `.bgf` keeps names in a string pool; the element geometry is in undecoded binary records. Its placeholder text is 51 characters of keyboard mash. "Hero 1 (Hardcore)" is 17, so the same line should fit (Inference). Live check B5.

#### 13.6 Temporary probe (not committed)

`src/core/hotfunc/unsorted/DifficultyProbe.cpp`, an untracked file, built into `out/build/win-amd64-release/fable_2.exe` on 2026-11-20. Strong-override wrappers:
- `Difficulty_GetMultiplier_823C92F0`: on its first call it reads all five fields (context saved and restored around each `__imp__` call) and logs `[difficulty-probe] DifficultyLevelMultipliers: …`. It also logs the first real read of each idx with its caller (lr).
- `SaveManager_Save_824E8990`: logs every save: caller (lr), r4–r7, the save-allowed byte `mgr+2093` and the result.

Logs go to the usual `out/build/win-amd64-release/logs/fable_2_NNN.log`. **Delete the file after the checks.** `fable_2_manifest.toml` shows modified after the build, but that's the line-ending-only codegen quirk; restore it with `git checkout`.

Prepared:
- the saves backed up to `out/build/win-amd64-release/saves.backup-difficulty-2026-11-20`;
- `difficulty-probe-marker.txt` written into each `Hero000`–`Hero004` folder at 23:39 on 2026-11-20.

In-game checks (the console lines are in `overview.md`):
- B1 values;
- B2 level field (0–3, `HasDifficultyLevelChanged`, survives save/reload?);
- B3 autosave callers (region change, sleep, house purchase);
- B4 marker survives a save;
- B5 label box (`ui get … boxwidth boxheight size`, `ui label`);
- B6 optional: fail-quest prompt location.

#### 13.7 Tools this session

- **`xex_image.py`.** `tools/xex_image.py` needs pycryptodome, which isn't installed. A throwaway PowerShell port using .NET AES (not in the repo) gives the same image (23,166,976 bytes, base 0x82000000).
- **String peeker.** Throwaway; reads the strings at given addresses.
- **`book.babel`.** The 2026-11-15 layout didn't reproduce:
  - the header is `5B010000`, then n = 56000;
  - 12-byte index entries run to about 0x0A4108;
  - no zlib stream was found at the expected block starts.
  - The blocks may be raw deflate. Not pursued (only needed for the Guildmaster spelling).

#### 13.8a In-game results (2026-11-20 23:48–00:00, log `out/build/win-amd64-release/logs/fable_2_063.log`)

**The hero and the level field:**
- The hero is in slot 1 (`Hero000`), chapter 3.1, 5.2M gold.
- **Verified.** `Player.SetDifficultyLevel(QuestManager.HeroEntity, n)` works, and `stats` read back 2, then 3.
  - Note: a console line chaining `...; f2 lua ...` fails to compile, because the whole rest of the line goes to Lua. Type one command per line.
- **Verified.** The level persists through Save and Quit → Continue: `stats` read 2 at 23:55:41, after the 23:55:09 save.
- **Verified.** `HasDifficultyLevelChanged` → `true` after a change.
- **Inference:** slot 1's hero now has the changed flag set (Masochist lost for that hero) and is stored at level 3. The backup predates it.

**The multiplier reader never ran:**
- **Verified.** There was **no** `[difficulty-probe] DifficultyLevelMultipliers` or first-read line, so `Difficulty_GetMultiplier_823C92F0` never ran.
- That was at levels 2 and 3, during shop buying and selling (gold 5,215,541 → 5,221,314) and the mood-ring purchase.
- Shop prices looked unchanged, which agrees.
- delta4o confirmed there was no combat (no hits dealt or taken), so idx 0/1 are **untested**.
- **The level 1 comparison wasn't clean.** The 23:53:46 attempt to set level 1 failed to compile (chained line), and every `stats` read afterwards showed 2 or 3. The decisive evidence is the absent reader call at levels 2 and 3 during buying (the mood ring) and selling, not the price comparison.
- **Inference / open:** check that the shop UI's price and the purchase really go through `Item_GetBuyPrice_828E7F78` / `Item_GetSellPrice_828E8320`. It's possible they don't, and that, not the object mismatch, could explain the silence.

**The SetDifficultyLevel native (Verified, after the session):**
- The binding is registered in `sub_823C6030` (`.89` ~5490):
  - `SetDifficultyLevel` → `LuaPlayer_SetDifficultyLevel_823C9458`
  - `GetDifficultyLevel` → `LuaPlayer_GetDifficultyLevel_82612AC0`
- `LuaPlayer_SetDifficultyLevel_823C9458`: `if (obj+80 != r4) { obj+80 = r4; obj+38 = 1 }`. `LuaPlayer_GetDifficultyLevel_82612AC0`: `return obj+80`.
- So it's the same **offset** the five gates read on the component type 33 object. Which object the binding passes as `obj` hasn't been traced. **Hypothesis:** it's the hero's Lua-side player object, not the component the gates find, or the gate's lookup fails on the hero.

**The buy function's lookups (Verified, partial):**
- `Item_GetBuyPrice_828E7F78` starts from player 1 (`Player_GetEntity_8222C210`, the global at `0x83496920`), checks flag 0x40 at `+144`, looks up **component type 69** on it, and calls `Ownership_OwnsProperty_828B0B38(comp69, *(r29+4))`.
- The type 33 lookup and the `+80` gate come later (0x828E81CC–0x828E8288). The entity that lookup runs on hasn't been traced. The register path from the start to `r6`/`r10` at the gate is the next thing to read.

**Next diagnostic:**
- Log `r3` in a wrapper of `LuaPlayer_SetDifficultyLevel_823C9458`.
- Add a mid-asm hook at 0x828E8278 (registers r10, r11: found flag + comp), plus `REX_LOAD_U32(r10+80)`. This needs temporary manifest entries and a codegen run.
- Then compare the two objects.

**Saves:**
- **Verified.** 3 saves, all `from 82413AA8` with r4–r7 = 0, allowed = 1. That's the sub-code 24 path in `PauseSaveBox_HandleEvent_82413988` (Save and Quit; the r7 = 0 save).
- **Verified.** Sleeping in a bed produced **no** `SaveManager_Save_824E8990` call.
- Region change and house purchase weren't tested.

**Save folder:**
- **Verified.** `Hero000/difficulty-probe-marker.txt` survived the 23:58:50 save (mainsave and herosave rewritten), and `saveuid.bin` and the header stayed at 2026-11-07. This settles step 5: a sidecar inside `HeroNNN/` is safe.

**Label:**
- **Verified.** The live element is `?.Layer.loadgame.SaveGameDescription`. Its layout log: position (-28, -195), scale 1, opacity 0 until a card is selected.
- **Verified:** only `ui tree` ran (23:59:09, 23:59:48, also in `console/out.txt`). There's no `ui get` / `ui label` reply, so B5 is still open.
- The console doesn't log typed commands, only replies.

**State left behind (to clean up):**
- the untracked `src/core/hotfunc/unsorted/DifficultyProbe.cpp`;
- the marker files in `Hero000`–`Hero004`;
- `fable_2_manifest.toml` modified (EOL only);
- the backup `saves.backup-difficulty-2026-11-20`.

#### 13.8 Decision recorded 2026-11-20

| # | Question | Options | Choice |
| --- | --- | --- | --- |
| 0 | Scope of the first step | gameplay core via console / research only / new-game screen first | **Research only** |
| 0b | Enemy health in this step? | later / include now | **Later** (needs the friend-or-foe check) |
| – | Subtitles | (delta4o's own) | Easy "Little Sparrow", Medium "Hero of Albion", Hard "GuildMaster", Hardcore "Archon's Heir" |

### 14. Research step (2026-11-21)

Read by hand in the generated code on `feature/game-difficulty` at 55edd59 (master 5f5f55f, with `feature/context-docs` and `feature/claude-files` merged in), unless marked. Where this section disagrees with an older one, this section wins.

#### 14.1 The buy-price gate doesn't look at the hero (Verified)

- `Shopkeeper_GetBuyPrice_827AF5A0` (r3 = the shopkeeper component, r4 = the item) does not pass the shopkeeper to `Item_GetBuyPrice_828E7F78`. It passes the return value of `Shopkeeper_GetStockComponent_827B0358`: the **type-124 component** on the entity behind the weak handle at `shopkeeper+16`.
- `Item_GetBuyPrice_828E7F78` keeps that component in r29 and runs the StockItemPrice gate's type-33 lookup on `*(r29+4)`, the entity that owns it (0x828E8164–0x828E8278). That entity is not player 1. If it has no type-33 component, the code branches straight past the multiply and `Difficulty_GetMultiplier_823C92F0` is never called.
- This matches the silence in 13.8a (no reader call while buying and selling at levels 2 and 3) without any mismatch in `LuaPlayer_SetDifficultyLevel_823C9458`.
- **Inference:** the sell side (`Item_GetSellPrice_828E8320`) has the same shape, because its gate also reads `*(r29+4)` (0x828E8370 onwards).
- **Inference:** the game's StockItemPrice multiplier may never have applied to the hero's shopping. If so, the prices part of the feature needs our own hook at the multiply (13.1) whatever the level object turns out to be. The probe confirms this or rules it out (14.4, check C).

#### 14.2 Component lookup (Verified for types 33, 47, 66, 69 and 124)

Every inlined lookup read so far has the same shape:

1. **Presence bit.** Bit `type & 31` of the word at `entity + 36 + 4 * (type >> 5)`. Type 33 → `+40` bit 1; 47 → `+40` bit 15; 66 → `+44` bit 2; 69 → `+44` bit 5; 124 → `+48` bit 28. The base 36 for types under 32 is Inference.
2. **Fast table.** If `entity+140` is not 0, the slot is the byte at `*(entity+140) + type`, and the component is the word at `*(entity+72) + 8 * slot + 4`.
3. **Otherwise** a binary search over the 8-byte `{s32 type, u32 component}` pairs in `[*(entity+72), *(entity+76))`.

A component's owner entity is at `component+4` (the same check as in `MoneyComp_Modify_8239E698`).

#### 14.3 Player 1 and the price formula (Verified)

- **Player 1's entity.** `Player_GetEntity_8222C210` follows the weak handle at `playerObj+104`. The price functions get `playerObj` as `*(*(*(*(*(0x83496920)+12)+88)+4)+0)`. `Item_GetBuyOpinionFactor_828E8C80` inlines the same chain.
- **Buy price.** `Item_GetBuyPrice_828E7F78(stock r3, item r4, f1, f2, f3)`:
  1. `f31 = f3` (the shopkeeper multiplier). If player 1 has flag 0x40 at `+144` and a **type-69 component**, and `Ownership_OwnsProperty_828B0B38(comp69, *(stock+4))` is true, then `f31 = 1.0` and `r27 = 1`.
  2. `price = base (Item_GetBaseValue_828E7C58) × economy (Item_GetBuyOpinionFactor_828E8C80(f1, f2)) × stock+36 × f31`.
  3. The StockItemPrice gate (14.1).
  4. If `r27`, multiply by the discount at `stock+60`.
  5. Floor at 0, convert to int, return in r3.
- **Inference:** the type-69 check is "the hero owns this shop", which drops the shopkeeper multiplier and applies an owner discount. The shout-out test (14.5) is meant to confirm it before `Ownership_OwnsProperty_828B0B38` gets a name.
- **Sell price.** `Item_GetSellPrice_828E8320`: `base × Item_GetSellOpinionFactor_828E8EC0(f1, f2) × stock+40`, divided by StockItemPrice behind the same gate, then the same player 1 / type-69 check and a discount at `stock+64`.
- `Shopkeeper_GetBuyPrice_827AF5A0` and the sell wrapper have 64 call sites between them, most in shop UI code around 0x828E2xxx. So the UI asks for prices often, not only on a purchase.

#### 14.4 Probe, round 2

`src/core/hotfunc/unsorted/DifficultyProbe.cpp` (committed on this branch as bd6846a, rewritten this session) plus TEMPORARY `fable2_probe_*` midasm hooks at the end of `fable_2_manifest.toml`. **Remove both before any PR to master.** Every line starts with `[difficulty-probe]` in `out/build/win-amd64-release/logs/fable_2_NNN.log`.

| Check | Where | What it logs |
| --- | --- | --- |
| A | `LuaPlayer_SetDifficultyLevel_823C9458` (wrapper) | The bound object, old and new level, its `+38` byte and `+4`, player 1's entity and type-33 component, and whether they are the same object |
| B | Midasm hooks after each gate's `lwz r11,80(rX)`: 0x826A455C (DamageCaused, r31), 0x8249F628 (DamageReceived), 0x8251CF2C (XP), 0x828E8284 (buy), 0x828E84A0 (sell), all r10 except the first | The component found, its owner, whether that is player 1, the level read and `+38`. First 3 hits per site, then on every change |
| C | `Item_GetBuyPrice_828E7F78` / `Item_GetSellPrice_828E8320` (wrappers) plus hooks 0x828E8160 / 0x828E8374 | Caller, stock component, its owner, whether that is player 1 and has type 33, f1–f3, base, economy, `stock+36` / `+40`, shopkeeper-or-owned multiplier, owned flag, `stock+60` / `+64`, gate fired or skipped, and the price. Once per item, then on change |
| – | `Difficulty_GetMultiplier_823C92F0` | All five table values on the first call, then the first read of each index with its caller |
| E | `MoneyComp_Modify_8239E698` | Every player 1 gold change (caller, r5–r7, balance before and after), plus the first 3 per caller for anyone else |
| F | `HealthComp_Modify_8268BD90` | First 3 per caller and per target/source kind: delta, target and whether it is player 1, source and whether it is player 1, health before and after, max |
| G | `SaveManager_Save_824E8990`, `LuaSaveManager_AutoSaveGame_824E8D28`, `_SaveFailQuestSave_824E8D18`, `_LoadFailQuestSave_824ECCB8`, `PauseSaveBox_HandleEvent_82413988` | Every call. AutoSaveGame adds defer, force, the pending byte and `FrameLastAutoSaved`. The Save box logs only events 46 and 47, with the sub-code |
| H | `FrontMenu_StartNewGame_8237A0C8`, `FrontMenu_HandleCardEvent_826C9188` | The r4 creature string, if it's in the image; the card event name (matched against the eight event globals) and the state at `+24` |
| I | `FrontMenu_SelectLoadCard_826C9008` | The card index |

The probe adds no RunScript and no GeneralScriptManager registration, and the midasm hooks only read registers and guest memory. The price factor functions `Item_GetBaseValue_828E7C58` and `Item_GetBuyOpinionFactor_828E8C80` run their own lookups and calls, so the probe doesn't call them again; it reads their results from registers instead.

#### 14.5 In-game checks for round 2 (not yet run)

The overview gives these in plain steps. In short:

1. **A.** `SetDifficultyLevel` to 2, then `stats`.
2. **The shout-out shop test** (delta4o's idea, in the style of Cheat Engine). For each purchase, delta4o reads out the gold before, the price shown, the modifiers they believe apply (liked or feared, the shopkeeper's reaction, owning the shop, how wealthy the village is) and the gold after. One item at a time, one changed variable at a time: level 1 against 2 or 3, an owned shop against one that isn't, before and after changing the shopkeeper's opinion, plus one sale. The C and E lines are then matched to each purchase by timestamp. That shows which factor carries each modifier, whether the price shown is the amount taken, and whether the buy and sell gates ever fire.
3. **Combat** (B idx 0/1, F) and **XP** (B idx 2).
4. **Gold sources** (E), the region change and house purchase (G).
5. **Optional:** each Save box button (G).
6. **Front end:** load cards (I), the label size and the test label from 13.8a, then New Game → gender (H).

**How to read the results:**
- If the shop's stock owner isn't player 1 or has no type 33, the built-in price gate can't help the hero, and prices go through our own hook.
- If B's combat gates read 2 or 3 on player 1's component, the built-in route works for damage.

#### 14.6 Decisions (2026-11-21)

| # | Question | Options | Choice |
| --- | --- | --- | --- |
| 4 | Where this topic's docs live | keep in the personal context folder / move to `docs/feature/` now | **Move to `docs/feature/difficulty_settings/` now** (status "no commit yet") |
| 5 | Probe scope for round 2 | gate tracing and extras / gate tracing only | **Gate tracing and extras** |
| – | Shop test method | (delta4o's own idea) | **The shout-out test** (14.5) |

### 15. Research step (2026-11-22): the unknown gold callers

Read by hand in the generated code and the decrypted image on `feature/game-difficulty` at 4d14650. Strings were resolved from the image (base 0x82000000) with throwaway scripts in the session scratchpad; none were added to the repo. Where this section disagrees with section 2, this section wins.

#### 15.1 The 17 callers

r5 / r6 / r7 are the `MoneyComp_Modify_8239E698` arguments (r5 = 1 on the co-op share recursion, r6 = share with co-op, r7 = 1 for rent and income).

**Gains:**

| Caller | What it does | r5 r6 | Evidence |
| --- | --- | --- | --- |
| `Quest_GiveRewards_825FC180` | **Quest rewards.** It pays `quest+36`, which Lua `QuestTracker.SetRewardMoney` (`LuaQuestTracker_SetRewardMoney_825FB298`) sets. Renown is at `+32`, and items follow. It is reached from `QuestTracker.SetAsCompleted` (`LuaQuestTracker_SetAsCompleted_825F9748`) and `QuestTracker.GiveRewards` (`LuaQuestTracker_GiveRewards_825FC878`). | 0 1 | Verified (the binding table is registered in the function holding 0x825FE168) |
| `PubGame_PayResult_825ABDC0` | **Pub games.** It pays the net result `sub_82EE1DF0(obj+124) - obj+96`, which **can be negative**. Then it does the "PurityBalancing" step with "MaxGoldForScalingFromGambling" and "MaxPurityModifierForGambling". | 0 1 | Verified |
| `Marriage_PayDowry_8282AEB8` | **Wedding.** It reads "RenownBoostForMarrying" and "DowryForMarrying" from the spouse's data (`sub_827699A0`, name hashes in the globals 0x8349BC3C / 0x8349BC40) and pays the dowry. Its caller `sub_82824C38` holds the marriage strings. | 0 1 | Verified |
| `Inventory_GiveItemOrMoney_8261A738` | **Money items.** When an item's definition has a "MoneyComponent", it pays the "Money" value instead of adding an item. r6 = the item's "SplitWithHenchmanIfGivenToHero". It is part of the inventory "add items" bindings (`sub_8261AD88`), and `sub_828E92D0` (hero titles) and `sub_828EB1F0` also call it. | 0 flag | Verified for the money part; the exact Lua name is Inference |
| `GiftReward_Give_827ADCF8` | **Gift/reward object** (vtable 0x82002320). The constructor `GiftReward_Init_827ADBD8` (made by `sub_8244FD18`) reads "IsMoneyGift", "MoneyAmount" (`+32`), "GiftItem" (`+28`) and "IsRandomReward". This method pays MoneyAmount, then gives the item to the recipient's type-13 component. | 0 1 | Verified; who hands these out is not traced |
| `BuildingIncomePickup_Update_828B46E8` | **Building income pickup** (vtable 0x820E0E10, next to "PickupDistance", "BuildingUID" and "BuildingIncome"). When player 1 is within `+28` (PickupDistance), they get the whole balance of the pickup entity's own type-81 component. It is logged to player 1's type-69 component (`sub_828B21F0`), then the pickup is removed. | 0 1 | Verified; the removal is Inference |
| `OrbGift_Apply_8238D4A8` | **Orb gift: apply.** It adds the gold at `gift+112`, then the item, to an entity. It is called on receipt (`sub_8238C488`, "GUI_NET_RECEIVE_GOLD") and when a gift fails (`sub_8238D190`, "GUI_ORB_GIFT_FAILED": the refund to the sender). | 0 1 | Verified |
| `Network_HandleMessage_8237DE18` | **Network replay**, case 43 of its 270-case message switch (called from `sub_82317598`, "CECNetwork"). It sets byte `+107` on the entity's type-91 component, then calls the choke point with `msg+4`. | 0 1 | Verified |
| `Henchman_PayWage_824A6FD8` | **Co-op henchman wage.** It looks up "GoldEarnedPerMinute" in "HenchmanJobLevels" by minutes played ("MinimumNumberOfMinutesPlayedToReachLevel", `obj+1060`). Fractions build up at `obj+1064`, and whole coins are paid. | **1** 1 | Verified |

**Spending** (each negates the amount):

| Caller | What it does | Evidence |
| --- | --- | --- |
| `TattooShop_RemoveTattoo_82593950` | **Tattoo removal:** `-"PriceOfTattooRemoval"`. Listed as a gain before; the `neg` sits well before the call. | Verified |
| `Travel_ChargeFare_82609B90` | **Coach/ferry fare.** It is charged only when the balance covers it. It is reached from the Lua travel bindings near "FastTravelToStop" (`sub_823AD510`) and "TravelToStopInRegion" (`sub_82609DE0`), and from `sub_82837610`. | Verified for the domain; which name pairs with which pointer is Inference (names and pointers interleave in those tables) |
| `sub_8281E6A8` | **Family upkeep.** The cost is `r10 × r25` (Inference: days × the daily allowance; "DailyUpkeepCost" sits next to the wedding strings), and the amount is recorded at `+172`. The callers are `sub_828214A8` (divorce and "children taken" text), `sub_8282BBE8` and the pause-menu builder `sub_82842860`. | Inference |
| `sub_82597DC0` | **Household allowance, real-estate menu** (GUI table 0x820C1670). It sets a household's amount through `sub_8282BBE8`, then charges `new - old (+172)` right away, capped at the balance. | Inference |
| `sub_8238BEB0` | **Orb gift: send.** It takes r5 gold from the sender, removes the item (`sub_8261AF88`) and autosaves. It is called from the gift menu (`sub_825A0F50`, `sub_825A1510`). | Inference |
| `sub_825A11F0` | **Gift menu** (GUI table 0x820C1E14). It takes r4 gold, then calls `sub_825A0D98`. | Inference |

**Transfers** (r6 = 0):

| Caller | What it does | Evidence |
| --- | --- | --- |
| `Coop_MoveGoldToPlayer1_82413620` | **End of co-op.** It moves all of player 2's gold (`playerObj+112`) to player 1 (`+104`) with r5 = 1, then sets player 2 to 0. Its caller `sub_824130C0` looks like the co-op teardown. | Verified for the move; the caller's role is Inference |
| `Network_DropClient_8230FEB0` | **Client dropped** ("GUI_NET_DROPPED_CLIENT"). Player 1 gets the gold of the co-op partner (`sub_828BEEC0(playerObj)`, the same helper the choke point uses for the share), then the game saves. | Verified for the call; the direction is Inference |

#### 15.2 What this changes

- **Money component = type 81** (presence bit 17 of `+44`). It is found by that type in every caller above.
- **The network component is type 91.** Its byte `+107` means "applying a received message". In that case the choke point skips everything except `comp+12 += delta` and the clamp (the `+107` test at 0x8239E7D8, the stores at 0x8239E7FC and 0x8239E81C), so there's no co-op share and no change message.
- **r5 = 1 is not only the share recursion.** The henchman wage and the end-of-co-op transfer also pass it. So the gain-hook rule now goes by caller (section 2).
- **Rent and income** (`Ownership_PayDueRent_828AEA50` / `Ownership_UpdateIncome_828AECD0`, r7 = 1) pay the owner of the calling component: `comp+128` once the game tick passes `comp+132`, after which `+128` is zeroed. The building pickups (`BuildingIncomePickup_Update_828B46E8`, r7 = 0) are a separate route, so rent doesn't pass through them.
- **Pub games pass net results**, so a positive delta there is winnings, not a whole payout.
- **Named on 2026-11-22:** the Verified callers above (list under "What changed"). Still unnamed: `sub_828BEEC0` (Inference: the co-op partner getter) and the spending callers marked Inference.

#### 15.3 Property prices, buying and selling (Verified unless marked)

Here `P` is the property's **type-67 component**, which every price function takes in r3. Data values in quotes are read from the property's definition.

- **Base value**, `Property_GetBaseValue_8274D1E0(P)`:
  - `int("BasicSalePrice" × (1 + P+112 × pct × 0.01))`, plus `P+44 × P+116`.
  - `pct` is `+16` of a type-65 component. To find it, the code follows the property entity's type-66 component through the weak handle at `comp66+12` (`sub_82455F20`), then takes the type-65 component on that entity. Without one, `pct` = 0.
  - **Inference:** type 65 / 66 are the region or village link and its economy. `P+44` × `P+116` is an added amount, maybe upgrades; not traced.
- **Sell price**, `Property_GetSellPrice_8274DFD0(P)`: `int(base × "SellingMultiplier")`.
- **Buy price**, `Property_GetBuyPrice_8274DD78(P)`:
  1. `v = base + A`, where A is `Property_GetTypePriceAdjustment_8274D4D0(P, base)`, the "TypePriceAdjustments" step. It sums `"AdjustValue" × base` over every entry whose "BuildingType" bits match `P+28`.
  2. If `P+128` is not 0 and the definition has "NotForSaleAdjustment": `v = int(v × NotForSaleAdjustment)`.
  3. `v += B`, where B is `Property_GetOpinionPriceAdjustment_8274D6D8(P, v)`, the "OpinionPriceAdjustments" step:
     - it averages the five opinion axes over the property's occupants (the list at `P+120`) and the entities listed on the property entity's type-41 component (`+24`), skipping any without a type-47 component;
     - it multiplies each average by the matching table weight and returns `int(v × Σ)`.
  4. If byte `P+81` is set, player 1 is valid (`+144` & 0x40) and has a type-33 component, and the definition has "PlayerKilledOccupantAdjustment": `v = int(v × PlayerKilledOccupantAdjustment)`.
  5. **The buy price is floored at the sell price.**
- **Type 47 = the opinion component.** Its five floats at `+44 / +48 / +52 / +56 / +60` are, in order, "LoveAxis", "FearAxis", "AttractivenessAxis", "MoralityAxis" and "RespectAxis". The name list is a global array at 0x8349B408, filled at startup by the code at `fable_2_recomp.67.cpp:34693` (generated code, for reference only). This corrects the f1/f2 guess in section 3.
- **Buying**, `Ownership_BuyProperty_828AF6D0(ownerComp r3, property entity r4)`:
  1. If `Ownership_OwnsProperty_828B0B38(r3, r4)` is true (already owned), it does nothing.
  2. Otherwise it creates a 68-byte ownership record (`sub_828AC940`), stores the price at `record+48`, and charges `-price` with r6 = 1.
  3. Lua `PurchaseProperty` is `LuaPlayerProperties_PurchaseProperty_828AFB80`, which checks that the buyer is valid, then jumps here.
  4. It is also called from the real-estate GUI (`sub_82598548`).
- **Lua `PurchasePropertyQuietly`** (`LuaPlayerProperties_PurchasePropertyQuietly_828AFBB8`) does the same setup and stores the price, but **never charges gold**. It's a free grant, so a buy-price hook won't see it.
- **Selling**, `Ownership_SellProperty_828B01C8(ownerComp r3, property entity r4)`: only when `Ownership_OwnsProperty_828B0B38` is true. It pays `+sell price` with r6 = 1, then the clean-up calls. Its caller is the real-estate GUI (`sub_825986E0`).
- **`Ownership_OwnsProperty_828B0B38` is the "already owns this" check.** Buy and sell branch opposite ways on it. Its use in `Item_GetBuyPrice_828E7F78` (14.3), on player 1's type-69 component and the stock owner, now reads as "the hero owns this shop's building". Confirmed in game on 2026-11-22 (16.3) and named.
- **Other price users:**
  - `sub_825981C0` and `sub_825974C0`, the real-estate GUI (table 0x820C1670), probably the price shown;
  - `sub_828ED8C0` and `sub_8274E940`;
  - **Inference:** the Lua "GetCurrentPropertyValue", whose name sits next to the price strings.
- **For the feature:**
  - Property buying and selling go through the choke point like any other gold change. The cleanest hook point for prices is the return of `Property_GetBuyPrice_8274DD78` / `Property_GetSellPrice_8274DFD0`, which the GUI also reads, so the price shown would match the price charged.
  - Selling is a gain with r6 = 1. Like shop selling, a gain hook would scale it on top of any sell-price change, so exclude `Ownership_SellProperty_828B01C8` there or leave the sell price alone.

### 16. In-game results, round 2 (2026-11-22, log `out/build/win-amd64-release/logs/fable_2_064.log`)

The Release build from 4d14650 (built 2026-11-21 17:37, probe included) on the dev machine. Slot 1 hero, 16:35–17:25. delta4o played and read out the screens; Claude matched the `[difficulty-probe]` lines by time. Where this section disagrees with an older one, this section wins.

#### 16.1 The level object (check A, Verified)

`lua Player.SetDifficultyLevel(QuestManager.HeroEntity, 2)` wrote to `42160FB0`, which is **player 1's type-33 component**. Its `+80` went 3 → 2 and `+38` (changed) = 1. So the console command, the gates and `stats` all use the same object.

#### 16.2 The game's table and the combat gates (checks B, F, Verified)

- **The gates fire for the hero.** DamageCaused (idx 0), DamageReceived (idx 1) and Experience (idx 2) each found player 1's type-33 component at level 2. The first reads came from 0x826A4570, 0x8249F63C and 0x8251CF40, the return addresses of the three multiply sites in section 1.
- **The table is all 1.0:** `DamageCaused`, `DamageReceived`, `ExperienceReceived`, `SpellManaCost` and `StockItemPrice` are each 1.0000. With one row for every level above 1 (13.1), **the game's built-in difficulty changes nothing in the shipped data** except the Masochist flag. That answers decision 3 and 13.1 item 1: the game's gates and multiply sites are a usable route for damage and XP, but every value has to be ours.
- **Health** (`HealthComp_Modify_8268BD90`, caller 0x8268AF98 in the hit handler `Combat_ApplyHit_8268AB30`):
  - the hero's melee did -157.7 twice and -9.5 once to guards (600 max);
  - a guard hit the hero for -30, twice (600 max).
- **Life-steal.** Each of the hero's melee hits gave **+39.42 = 25% of 157.7** to the hero (source = target = hero, caller 0x8268B968). This is the Ghoul augment on the hero's Master Cleaver. Because it's worked out from the damage, a damage multiplier carries through to it. A heal hook must leave it alone.
- **Golden touch** (also on the cleaver) produced **no** gold lines on any hit (guards or other targets). Its payout at 0x8268B604 ("RichGoldPerHit") is still untested; try a hostile creature.
- **Ranged (Inference).** Crossbow hits added no new lines. The health limit is per caller, so ranged most likely uses the same caller as melee.

#### 16.3 Shop prices (checks C, E, Verified)

Every price line said `gate skipped`: the stock's owner is never player 1 and has no type-33 component (14.1). So **prices need our own hook**, at any level.

| Shop | Item | Menu | Log | Gold |
| --- | --- | --- | --- | --- |
| A shop the hero owns | toy sword | base 25, wealth +4.9%, owner discount 39%, price 16 | base 26.214, f1 1.0, f2 0.0692, f1/f2 term 1.0, owned 1, `stock+60` 0.6 → 16 | -16 from 0x827AF9A4 |
| Bowerstone Pie Stall (sold first, so not owned) | Crumbly Blueberry Pie | base 24, wealth +4.9%, Love 6%, Fear 6%, price 22 | base 25.165, f1 0.2891, f2 0.2953, term **0.8725** → 21.96 → 22 | -22 from 0x827AF9A4 |
| Same, after rude expressions | Apple Pie Pocket | base 58, Love 0%, price 57 (was 53) | base 60.816; the probe's item limit was full, so no new line | not bought |

- **Base (`Item_GetBaseValue_828E7C58`) = the item value × village wealth.** For example 25 × 1.0486 = 26.214.
- **f1 = Love and f2 = Fear** of the shopkeeper (type-47 `+44` / `+48`). `Item_GetBuyOpinionFactor_828E8C80(f1, f2)` is the **opinion discount**. Read in the generated code on 2026-11-22: `1 / (1 + stock+52 × max(Love, 0) + stock+48 × |min(Love, 0)| + stock+56 × max(Fear, 0))`, with the weights on the type-124 stock component. The two in-game points give weights of about 0.269 (Love) and 0.232 (Fear) for the Pie Stall (Inference: worked out from 0.8725 and 0.936). For the Apple Pie Pocket, Love 0 and Fear 0.2953 give 0.936 × 60.816 = 56.9 → 57, which fits.
- **Owning the shop also removes the opinion discount.** With f1 = 1.0 the term was still 1.0. The 0.6 owner discount applies instead of the shopkeeper multiplier and the opinion term. The opinion factor returns 1.0 itself when `Ownership_OwnsProperty_828B0B38` is true for player 1's type-69 component and the stock owner (Verified in the generated code, 2026-11-22).
- **Prices round to the nearest whole number** (15.73 → 16, 21.96 → 22, 53.06 → 53), not down. The "floor at 0, convert to int" step in 14.3 is a clamp plus rounding.
- **Owner bonus on selling** is `stock+64` = 1.4.
- **Opening a shop asks for about 3,000 prices** (every item, buy and sell, from 0x828E8654 / 0x827AF830 and the menu at 0x827AF6D8), and about 100 more after each purchase. A price hook has to be cheap.

#### 16.4 Property sale (Verified)

The Bowerstone Pie Stall's sell screen showed:
- Base Value 2250 ("BasicSalePrice");
- **Decoration 0** (so `P+44 × P+116` is the decoration value);
- Town Economy +109;
- Selling Fees -236, so SellingMultiplier = 0.9;
- **Final 2123** = `int(2359 × 0.9)`;
- Profit 23. ~~against the 2100 stored at `record+48`~~: corrected 2026-11-22, it stayed 23 when the sale price changed to 1,848 and 1,635 (feature step 1 in game), so it isn't sale price minus purchase price. Probably the rent profit (Inference).

The log has `gold +2123 from 0x828B0438` (inside `Ownership_SellProperty_828B01C8`) with r6 = 1, which matches 15.3.

#### 16.5 Other gold (check E, Verified)

- **Rent.** On load, +131,076 from 0x828AEBAC (`Ownership_PayDueRent_828AEA50`) and +5,958 from 0x828AF2E4 (`Ownership_UpdateIncome_828AECD0`), both with r7 = 1. In play, +5,935 from `Ownership_UpdateIncome_828AECD0` about every 4 minutes. After the Pie Stall sale it paid 23 less, probably that stall's share.
  - The load payout was identical (+131,076 from 5,227,209) on a second load about 50 minutes later. So it's stored in, or worked out from, the save, not real time.
- **Lua gold all comes from 0x8239F998,** the return address in `LuaMoney_CallMethod_8239F830`, the Lua glue that finds the entity's type-81 component and calls the `Money.Modify` body `LuaMoney_Modify_8239EE98`: the guard fine (-250) and the blacksmith job (+47, +94, +141, one call per sword; sword *n* pays n × 47). **A gain hook can't tell Lua sources apart by caller**; that needs Lua-level context, or the scripts themselves.

#### 16.6 Saves and the front end (checks G, I, Verified)

- Save box **sub-code 25** = Quit without saving (no save followed). **Sub-code 24** = Save and Quit: `save from 0x82413AA8` with r4–r7 = 0.
- Job-end autosave: `AutoSaveGame` from 0x824EEC8C deferred (defer 1, pending 0), then from 0x82275FFC (defer 0, pending 1) it saved through `SaveManager_Save_824E8990` (caller 0x824E8DC8, r4 = 1, r7 = 1).
- Continue screen: `SelectLoadCard` -1, 1, 0 (callers 0x826C7728 / 0x826C75DC).
- **The reload undid the session.** "Quit without saving" took the game back to the last autosave, from before the session: gold back to 5,227,209, and the Pie Stall sale and the purchases gone. Slot 1 was then saved by the job-end autosave and Save and Quit, at 5,358,567 gold. Its level is whatever that save had (probably 3; not checked).

#### 16.7 Not run this round

- The level 1 against 2/3 price comparison (pointless, since the price gate never fires).
- Gold pickups.
- Golden touch on a hostile creature.
- Region change and house purchase.
- Save and Continue.
- The `ui get` / `ui label` slot-label checks.
- New Game → gender (check H).

#### 16.8 Probe limits found

- `g_price_seen` stops at 4096 items. Two shops filled it, so later stock items were never logged. Raise it, or key it by item type, next round.
- The health lines stop after 3 per caller and kind. One fight used them all up.

## What changed

On `feature/game-difficulty`. Nothing here is meant for master yet.

- `src/core/hotfunc/difficulty/Difficulty.cpp` (2026-11-22): **feature code, step 1** (section 18). It applies the decision-6 multipliers for gold gained, prices, damage taken and collected XP, from the hero's game level.
- `src/core/hotfunc/unsorted/DifficultyProbe.cpp`: the research probe (14.4). bd6846a holds round 1; round 2 is the rewrite on 2026-11-21. On 2026-11-22 its overrides of `Difficulty_GetMultiplier_823C92F0`, both item price functions and `MoneyComp_Modify_8239E698` moved to the feature file. **Temporary: remove it before any PR to master.**
- `fable_2_manifest.toml`: a "Difficulty settings" block at the end with:
  - the names below;
  - seven TEMPORARY `fable2_probe_*` midasm hooks (14.4), to remove together with the probe.
- `docs/feature/difficulty_settings/`: these docs. They moved here on 2026-11-21 from a personal notes folder (decision 4), and there's a row in `docs/README.md`.

Functions named on 2026-11-21. All were generic `sub_` names before:

| Address | Name | Evidence |
| --- | --- | --- |
| 0x823C92F0 | `Difficulty_GetMultiplier_823C92F0` | Verified (13.1) |
| 0x823C9458 | `LuaPlayer_SetDifficultyLevel_823C9458` | Verified (13.8a, the binding registration and the body) |
| 0x82612AC0 | `LuaPlayer_GetDifficultyLevel_82612AC0` | Verified (13.8a) |
| 0x828E7F78 | `Item_GetBuyPrice_828E7F78` | Verified (14.1, 14.3) |
| 0x828E8320 | `Item_GetSellPrice_828E8320` | Verified (13.1, 14.3) |
| 0x827AF5A0 | `Shopkeeper_GetBuyPrice_827AF5A0` | Verified (14.1) |
| 0x827AF6F8 | `Shopkeeper_GetSellPrice_827AF6F8` | Inference (from the 2026-11-15 search pass; body not re-read) |
| 0x827B0358 | `Shopkeeper_GetStockComponent_827B0358` | Verified (14.1); what its entity is, is open |
| 0x824E8990 | `SaveManager_Save_824E8990` | Verified (5, 13.3, 13.8a) |
| 0x824E8978 | `LuaSaveManager_SaveGame_824E8978` | Verified (5) |
| 0x824E8D28 | `LuaSaveManager_AutoSaveGame_824E8D28` | Verified (5, re-read 2026-11-21) |
| 0x824E8D18 | `LuaSaveManager_SaveFailQuestSave_824E8D18` | Verified (5, re-read 2026-11-21) |
| 0x824ECCB8 | `LuaSaveManager_LoadFailQuestSave_824ECCB8` | Verified as the binding (5) |
| 0x82413988 | `PauseSaveBox_HandleEvent_82413988` | Verified (13.2) |
| 0x8222C210 | `Player_GetEntity_8222C210` | Verified (14.3) |
| 0x8239E698 | `MoneyComp_Modify_8239E698` | Verified (2) |
| 0x8268BD90 | `HealthComp_Modify_8268BD90` | Verified (4) |
| 0x8237A0C8 | `FrontMenu_StartNewGame_8237A0C8` | Verified (7) |
| 0x826C9188 | `FrontMenu_HandleCardEvent_826C9188` | Verified (7, re-read 2026-11-21) |
| 0x826C9008 | `FrontMenu_SelectLoadCard_826C9008` | Verified (8, re-read 2026-11-21) |

Functions named on 2026-11-22 from sections 15 and 16. All were generic `sub_` names before:

| Address | Name | Evidence |
| --- | --- | --- |
| 0x8239EEE0 | `LuaMoney_Register_8239EEE0` | Verified (2) |
| 0x8239EE98 | `LuaMoney_Modify_8239EE98` | Verified (2) |
| 0x8239EEA8 | `LuaMoney_Set_8239EEA8` | Verified (2) |
| 0x8239F830 | `LuaMoney_CallMethod_8239F830` | Verified (code read 2026-11-22; 16.5) |
| 0x825FC180 | `Quest_GiveRewards_825FC180` | Verified (15.1) |
| 0x825FE168 | `LuaQuestTracker_Register_825FE168` | Verified (15.1) |
| 0x825FB298 | `LuaQuestTracker_SetRewardMoney_825FB298` | Verified (15.1) |
| 0x825F9748 | `LuaQuestTracker_SetAsCompleted_825F9748` | Verified (15.1) |
| 0x825FC878 | `LuaQuestTracker_GiveRewards_825FC878` | Verified (15.1) |
| 0x825ABDC0 | `PubGame_PayResult_825ABDC0` | Verified (15.1) |
| 0x8282AEB8 | `Marriage_PayDowry_8282AEB8` | Verified (15.1) |
| 0x8261A738 | `Inventory_GiveItemOrMoney_8261A738` | Verified for the money part (15.1) |
| 0x827ADBD8 | `GiftReward_Init_827ADBD8` | Verified (15.1) |
| 0x827ADCF8 | `GiftReward_Give_827ADCF8` | Verified (15.1) |
| 0x828B46E8 | `BuildingIncomePickup_Update_828B46E8` | Verified (15.1) |
| 0x8238D4A8 | `OrbGift_Apply_8238D4A8` | Verified (15.1) |
| 0x8237DE18 | `Network_HandleMessage_8237DE18` | Verified (15.1) |
| 0x824A6FD8 | `Henchman_PayWage_824A6FD8` | Verified (15.1) |
| 0x82593950 | `TattooShop_RemoveTattoo_82593950` | Verified for the charge (15.1) |
| 0x82609B90 | `Travel_ChargeFare_82609B90` | Verified for the charge (15.1) |
| 0x82413620 | `Coop_MoveGoldToPlayer1_82413620` | Verified for the move (15.1) |
| 0x8230FEB0 | `Network_DropClient_8230FEB0` | Verified for the gold and save calls; its role is Inference (15.1) |
| 0x827AF850 | `Shopkeeper_BuyItem_827AF850` | Verified (2, 16.3) |
| 0x827AFB58 | `Shopkeeper_SellItem_827AFB58` | Verified (2) |
| 0x828E7C58 | `Item_GetBaseValue_828E7C58` | Verified at runtime (16.3) |
| 0x828E8C80 | `Item_GetBuyOpinionFactor_828E8C80` | Verified (16.3) |
| 0x828E8EC0 | `Item_GetSellOpinionFactor_828E8EC0` | Inference (mirror of the buy factor) |
| 0x8274D1E0 | `Property_GetBaseValue_8274D1E0` | Verified (15.3, 16.4) |
| 0x8274D4D0 | `Property_GetTypePriceAdjustment_8274D4D0` | Verified (15.3) |
| 0x8274D6D8 | `Property_GetOpinionPriceAdjustment_8274D6D8` | Verified (15.3) |
| 0x8274DD78 | `Property_GetBuyPrice_8274DD78` | Verified (15.3) |
| 0x8274DFD0 | `Property_GetSellPrice_8274DFD0` | Verified (15.3, 16.4) |
| 0x828B0B38 | `Ownership_OwnsProperty_828B0B38` | Verified (15.3, 16.3) |
| 0x828AF6D0 | `Ownership_BuyProperty_828AF6D0` | Verified (15.3) |
| 0x828B01C8 | `Ownership_SellProperty_828B01C8` | Verified (15.3, 16.4) |
| 0x828B4240 | `LuaPlayerProperties_Register_828B4240` | Verified (15.3) |
| 0x828AFB80 | `LuaPlayerProperties_PurchaseProperty_828AFB80` | Verified (15.3) |
| 0x828AFBB8 | `LuaPlayerProperties_PurchasePropertyQuietly_828AFBB8` | Verified (15.3) |
| 0x828AEA50 | `Ownership_PayDueRent_828AEA50` | Verified (15.2, 16.5) |
| 0x828AECD0 | `Ownership_UpdateIncome_828AECD0` | Verified (15.2, 16.5) |
| 0x826A41C8 | `Combat_ScaleDamageDealt_826A41C8` | Verified (1, 16.2) |
| 0x8249F298 | `Combat_GetDamageTakenFactor_8249F298` | Verified (1, 16.2) |
| 0x8251CDD8 | `Experience_ScaleAward_8251CDD8` | Verified for the multiply (1, 16.2) |
| 0x8268AB30 | `Combat_ApplyHit_8268AB30` | Verified (4, 16.2) |
| 0x8251D958 | `LuaExperience_Register_8251D958` | Verified (17.1) |
| 0x8251C8E0 | `LuaExperience_Modify_8251C8E0` | Verified (17.1) |
| 0x8251C0A8 | `Experience_Add_8251C0A8` | Verified for the calls (17.1) |

Functions named on 2026-11-26 for B and the destiny line (19.2). Read in the generated code:

| Address | Name | Was | Evidence |
| --- | --- | --- | --- |
| 0x826C72A8 | `FrontMenu_HandleCardInput_826C72A8` | `ProcessAndProcessAndProcess1819_826C72A8` | Verified (7, 19.2) |
| 0x826C9460 | `FrontMenu_HandleUnfreeze_826C9460` | `sub_826C9460` | Verified (19.2) |
| 0x82306098 | `Speech_PlayByKey_82306098` | `ProcessAndProcessAndProcess2485_82306098` | Verified at its front-end calls; the body wasn't read (19.2) |
| 0x822ADE08 | `Handle_Release_822ADE08` | `ProcessAndVcallAndClear3_822ADE08` | Verified (19.2) |

These stay unnamed until their role is confirmed:
- the co-op partner getter `sub_828BEEC0` (Inference);
- the spending callers marked Inference in 15.1 (`sub_8281E6A8`, `sub_82597DC0`, `sub_8238BEB0`, `sub_825A11F0`);
- the binder `sub_823D62F8`.

## Verification

Run on Windows 11 (the dev machine) on 2026-11-21, before and after the round 2 probe and the names:

| Suite | Before | After | Verdict |
| --- | --- | --- | --- |
| `python tools/generate_game_versions.py --check` | up to date | up to date | same |
| `python -m pytest -q` | 429 passed, 1 skipped, 15 xfailed | 429 passed, 1 skipped, 15 xfailed | same |
| Launcher tests (`dotnet run --project tests/Fable2.Launcher.ConfigTests -c Release`) | passed | passed | same |
| `tests\run_native_tests.cmd` (needs LLVM's `bin` on PATH) | exit 0, all passed | exit 0, 14 suites all passed | same |

- **Manifest:** it parses with `tomllib`, and `tools/check_manifest_collisions.py` against `out/tooling/runtime-sdk/win-amd64/bin/rexruntime.dll` reports no clash (5120 named functions).
- **Build:** `build.cmd -release fable_2` succeeded. In the regenerated code, each of the seven `fable2_probe_*` hooks is called right after its intended instruction (the gates' `lwz r11,80(rX)`, and the two `fmuls` after the economy calls), with the registers in the manifest's order.
- **In game (2026-11-22):** round 2 ran on this build; results in section 16 (`fable_2_064.log`).
- **Feature step 1 (2026-11-22):** the Release build succeeded with no new warnings. The test suites match the baseline (the table above). In game: see section 18.

## Not tested

- Round 2 left these out (16.7):
  - gold pickups;
  - Golden touch on a hostile creature;
  - the region-change and house-purchase autosaves;
  - Save and Continue;
  - the fail-quest prompt;
  - the label size and test label;
  - New Game → gender.
- Co-op (player 2 also has a type-33 component).

## Follow-ups

1. ~~Run 14.5~~: done 2026-11-22 (section 16).
   - Prices need our own hook.
   - Damage and XP can go through the game's gates, with our values (the shipped table is all 1.0).
   - Still to check in game: a quest reward, a pub game, a coach fare, a gold pickup and Golden touch.
2. ~~Name the roles the log confirmed~~: done 2026-11-22 (44 functions, listed under "What changed"). Still unnamed: `sub_828BEEC0` and the spending callers marked Inference in 15.1.
3. Next research after that: enemy health and the friend-or-foe check (decision 0b), and the pause-menu population point.
4. Find the game's spelling of "Guildmaster" (section 13.7).
5. **Clean-up** before any PR to master:
   - the probe file and the TEMPORARY hooks;
   - the `difficulty-probe-marker.txt` files in `Hero000`–`Hero004`;
   - the `saves.backup-difficulty-2026-11-20` backup. Slot 1's hero has the changed flag set and was saved again on 2026-11-22 (16.6); restore it from the backup if that matters.
