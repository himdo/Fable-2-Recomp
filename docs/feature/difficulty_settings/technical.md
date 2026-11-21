# Difficulty settings: technical notes

**Status:** no commit yet · **Commits:** bd6846a (research probe, branch `feature/game-difficulty`, not on master) · **Last checked against:** 5f5f55f

## Goal

On New Game, after the boy/girl cards, the player picks one of four difficulties from cards. Then Theresa's "destiny" line plays. The level scales:

- gold gained;
- prices;
- enemy damage;
- enemy health.

Normal is the default, with every multiplier at 1.0. On top of that:

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
| 0 | `DamageCaused` | `sub_826A41C8` @0x826A456C | `hit+56 *= mult` |
| 1 | `DamageReceived` | `sub_8249F298` @0x8249F638 | `f31 /= mult` |
| 2 | `ExperienceReceived` | `sub_8251CDD8` @0x8251CF3C | |
| 3 | `SpellManaCost` | no callers | dead |
| 4 | `StockItemPrice` | buy `Item_GetBuyPrice_828E7F78` @0x828E8294; sell `Item_GetSellPrice_828E8320` @0x828E84B0 | buy ×, sell ÷ |

- **The gate.** Every caller checks `playerComp(type 33)+80 > 1` and uses 1.0 otherwise. So on Normal the reader isn't even called.
- **Achievement.** `gameflow`: if `GetDifficultyLevel(Hero) > 1` and `not HasDifficultyLevelChanged(Hero)`, then `ACHIEVEMENT_MASOCHIST` unlocks.
- **No menu.** There is no difficulty item in `optionsscreen.fac`.
- **Console.** `f2 stats` prints the level (`src/lua/console_cmds.lua:~1110`). Calling it without the hero argument crashes.
- **Who it affects (Inference).**
  - `sub_826A41C8`, called from `sub_826A33A8`, scales a hit when the **attacker** has the player component.
  - `sub_8249F298`, called from `sub_8268DEF0` and through it the hit handler `sub_8268AB30`, computes the hero's damage-taken factor from `Toughness`, `BarkskinMultiplier` and `BerserkValues.DamageTakenMultiplier`, and stores it at `entity+2164`.
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

Registered in `sub_8239EEE0`:
- `Money.Get` is 0x830418B0, which reads `+12`.
- `Money.Modify` is `sub_8239EE98`, which calls the choke point with r5 = 0, r6 = 1, r7 = 0.
- `Money.Set` is `sub_8239EEA8`, a direct store plus clamp. It **bypasses** the choke point. It is used in `qc010` and `qo150`, and in `onactionusetakeitems` (to zero the chest).

#### Lua callers

There are 52 `Money.Modify(hero, n)` sites in 32 files:
- Quest rewards: `qr_assassinationglobal`, `qr_creaturehunter`, `qr_slavetrader`, `qr_archaeologyglobal`, `qc120_arena`, `qo890_colosseum`, `qo660`, `qo820`.
- Jobs: `qj001` to `qj004` and `barmanjobgroupmind`.
- Gold pickups: `onactionusepickup`.
- Chests: `onactionusetakeitems`.
- Begging: `behaviournoticeexpression`.
- Extortion: `behaviourrespondtocrime`.
- Paying to sleep, a negative amount: `onactionusebed`.
- `Gameflow.ChildhoodVars.GiveGold`.

#### Native callers

There are 25. A `neg r4` before the call means the gold is being spent.

| Caller | What it does |
| --- | --- |
| `sub_827AF850` | Shop buy: `-Shopkeeper_GetBuyPrice_827AF5A0` |
| `sub_827AFB58` | Shop sell: `+Shopkeeper_GetSellPrice_827AF6F8` |
| `sub_828AEA50`, `sub_828AECD0` | Rent and income. These are the only callers with **r7 = 1**; `"RealEstateRentMedium"` appears here. |
| `sub_828AF6D0` | Property buy (Inference) |
| `sub_828B01C8` | Property sell (Inference) |
| `sub_8268AB30` @0x8268B604 | "RichGoldPerHit" |

Other callers:
- Spending (they negate the amount): `sub_82597DC0`, `sub_82609B90`, `sub_8238BEB0`, `sub_825A11F0`, `sub_8281E6A8`.
- Gains, purpose not identified: `sub_827ADCF8`, `sub_828B46E8`, `sub_8237DE18`, `sub_8238D4A8`, `sub_8282AEB8`, `sub_825FC180`, `sub_8261A738`, `sub_82593950`, `sub_825ABDC0`, `sub_824A6FD8`.
- Transfers (r6 = 0): `sub_8230FEB0`, `sub_82413620`.

#### Rules for a gain hook (Inference)

- Scale only when `delta > 0`.
- Scale only when `comp+4 == Player_GetEntity_8222C210()` (player 1), the same check used in the choke point and in `HealthComp_Modify_8268BD90`.
- Skip the recursive call (r5 = 1).
- Decide whether shop-sell income is excluded. Selling is already divided by `StockItemPrice`, so it would otherwise be scaled twice.

### 3. Prices (Verified)

- **Buy:** `Item_GetBuyPrice_828E7F78(itemComp, r4, f1, f2, f3)` (`.219:14334`). The price is the base value (`sub_828E7C58`) × the economy term (`sub_828E8C80(f1, f2)`) × `item+36` × f3 (the shopkeeper multiplier) × `StockItemPrice` (level > 1 only) × the `item+60` discount. The result is floored at 0 and converted to int.
- **Sell:** `Item_GetSellPrice_828E8320`. The price is the base value × `item+40` × `sub_828E8EC0` ÷ `StockItemPrice`. A discount at `item+64` applies, and the result is clamped against the buy price and `item+108`.
- **Wrappers:** `Shopkeeper_GetBuyPrice_827AF5A0` (buy) and `Shopkeeper_GetSellPrice_827AF6F8` (sell) read f1 and f2 from component type 47 (+44/+48) on the shopkeeper. The default is at 0x820994B4. f3 is `shopkeeperComp+12`.
- **Inference:** f1 and f2 are the village economy and wealth.
- **`Shopkeeper.ApplyPriceMultiplier`** is `sub_827AFD40` (`comp+12 *= f1`). It is called only from `miscfunctions.lua`, next to the `EconomyManager` village-wealth calls.
- **Property prices** (`sub_8274DD78` / `sub_8274DFD0`): **not analysed**.

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

- The hit handler `sub_8268AB30`, at 0x8268AF94 (`-f31`, attacker from `sub_825575C8`) and at 0x8268B964 (vampiric).
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
- Other callers of `SaveManager_Save_824E8990`, roles not identified: `sub_82837610`, `PauseSaveBox_HandleEvent_82413988` (twice), `sub_82275F00`, `sub_82198860`, and `sub_8230FEB0`.
- **Inference:** `sub_8230FEB0` drains a pending-request queue and is called by the quit routine, so quitting may flush a queued save.

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
| `ProcessAndProcessAndProcess1819_826C72A8` (`.0:10435`) | input: toggles cards (slides 2–5). Confirm Boy sets state `+104` and `+108` = 3, slide 6, plays `FE_30_HM` (~11553). Confirm Girl sets them to 4, slide 7, plays `FE_30_HF` (~11738). |
| `sub_826C9460` (`.124`) | `UNFREEZE` replays per state: 0 → slide 1 + `FE_20`; 3 → slide 6 + `_30_HM`; 4 → slide 7 + `_30_HF`; 5 → load-card screen. `DIALOG_ACTIVE` → 6 |
| `FrontMenu_HandleCardEvent_826C9188` (`.98`) | events: `BOY_ACTIVE` → 3, `GIRL_ACTIVE` → 4, `UNFREEZE` → 0. `BOY_SELECTED` → `FrontMenu_StartNewGame_8237A0C8("CreatureHeroChild")`; `GIRL_SELECTED` → `FrontMenu_StartNewGame_8237A0C8("CreatureHeroFemaleChild")` |
| `FrontMenu_StartNewGame_8237A0C8` | the actual new-game start (`GUI_SCREEN_FRONT_END_NEW_GAME`, `QC010_ChildhoodStart`). **The natural hook for the difficulty step.** |

**Inference:** the "destiny" line plays on the A press, together with the card flip, before the `_SELECTED` event.

**Helpers (Inference):**
- `ProcessAndProcessAndProcess2485_82306098` plays a speech or sound event by key (r5).
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
  - Define `extern "C" sub_XXXX` and forward to `__imp__sub_XXXX`; the example is `src/core/hotfunc/gui/MeterHitTest_824F1F00.cpp` (`docs/reference/hotfunc_overrides.md`).
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

The remaining open questions (Medium subtitle, Archon placement, slot label format, starting values, Masochist, XP, unused Theresa lines, property prices) are listed in `overview.md`.

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
| 0 | DamageCaused | `sub_826A41C8` (`.11`) | 0x826A457C | `lfs f0,56(r29)` then `fmuls f13,f0,f1` (+4), stored back to hit+56 | constant 1.0 at 0x820994C0 (`lis -32246, -27456`) |
| 1 | DamageReceived | `sub_8249F298` (`.52`) | 0x8249F644 | `fdivs f31,f31,f1` | f30 |
| 2 | ExperienceReceived | `sub_8251CDD8` (`.268`) | 0x8251CF48 | `fmuls f31,f1,f31` | `lfs f1,0(r5)` (**not** a constant: the value at r5) |
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
| `sub_8230FEB0` | `.251:3927` | `(mgr, 0, 0, 0, 1)` | only when `sub_8230FDD8` is true (the queue-drain inference stands) |
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

- **Property prices.** `sub_8274DD78` (`.111:12190`) and `sub_8274DFD0` (`.268:12138`) do `price = int(base × tweak float)`. The float comes from `ProcessAndProcessAndProcess2670_82196E10` when the record has the field (`sub_829FB978` check), then it's stored via `sub_8274D6D8`. There's no `+80` gate and no `Difficulty_GetMultiplier_823C92F0`, so they don't follow difficulty. A price hook would sit on the `fmuls` before each `fctiwz`.
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
- `Item_GetBuyPrice_828E7F78` starts from player 1 (`Player_GetEntity_8222C210`, the global at `0x83496920`), checks flag 0x40 at `+144`, looks up **component type 69** on it, and calls `sub_828B0B38(comp69, *(r29+4))`.
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

- **Player 1's entity.** `Player_GetEntity_8222C210` follows the weak handle at `playerObj+104`. The price functions get `playerObj` as `*(*(*(*(*(0x83496920)+12)+88)+4)+0)`. `sub_828E8C80` inlines the same chain.
- **Buy price.** `Item_GetBuyPrice_828E7F78(stock r3, item r4, f1, f2, f3)`:
  1. `f31 = f3` (the shopkeeper multiplier). If player 1 has flag 0x40 at `+144` and a **type-69 component**, and `sub_828B0B38(comp69, *(stock+4))` is true, then `f31 = 1.0` and `r27 = 1`.
  2. `price = base (sub_828E7C58) × economy (sub_828E8C80(f1, f2)) × stock+36 × f31`.
  3. The StockItemPrice gate (14.1).
  4. If `r27`, multiply by the discount at `stock+60`.
  5. Floor at 0, convert to int, return in r3.
- **Inference:** the type-69 check is "the hero owns this shop", which drops the shopkeeper multiplier and applies an owner discount. The shout-out test (14.5) is meant to confirm it before `sub_828B0B38` gets a name.
- **Sell price.** `Item_GetSellPrice_828E8320`: `base × sub_828E8EC0(f1, f2) × stock+40`, divided by StockItemPrice behind the same gate, then the same player 1 / type-69 check and a discount at `stock+64`.
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

The probe adds no RunScript and no GeneralScriptManager registration, and the midasm hooks only read registers and guest memory. The price factor functions `sub_828E7C58` and `sub_828E8C80` run their own lookups and calls, so the probe doesn't call them again; it reads their results from registers instead.

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

## What changed

Research only, on `feature/game-difficulty`. Nothing here is meant for master yet.

- `src/core/hotfunc/unsorted/DifficultyProbe.cpp`: the research probe (14.4). bd6846a holds round 1; round 2 is the rewrite on 2026-11-21. **Temporary: remove it before any PR to master.**
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

These stay unnamed until their role is confirmed:
- the gate functions `sub_826A41C8`, `sub_8249F298` and `sub_8251CDD8`;
- the ownership check `sub_828B0B38`;
- the price factors `sub_828E7C58`, `sub_828E8C80` and `sub_828E8EC0`;
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
- **In game:** nothing yet; see 14.5 and the overview's checklist.

## Not tested

- Everything in 14.5: the round 2 probe hasn't run in game yet.
- Combat (DamageCaused and DamageReceived), XP, the region-change and house-purchase autosaves, the fail-quest prompt, and the label size and test label.
- The game's own five table values: they have never been logged.
- Whether `sub_828B0B38` is the "owns this shop" check.
- Co-op (player 2 also has a type-33 component).

## Follow-ups

1. Run 14.5 and read the log. Decide the prices route (the built-in gate or our own hook) and the damage route from it.
2. Name the gate functions and `sub_828B0B38` once the log confirms their roles.
3. Next research after that: enemy health and the friend-or-foe check (decision 0b), and the pause-menu population point.
4. Find the game's spelling of "Guildmaster" (section 13.7).
5. **Clean-up** before any PR to master:
   - the probe file and the TEMPORARY hooks;
   - the `difficulty-probe-marker.txt` files in `Hero000`–`Hero004`;
   - the `saves.backup-difficulty-2026-11-20` backup. Slot 1's hero is at level 3 with the changed flag set; restore it from the backup if that matters.
