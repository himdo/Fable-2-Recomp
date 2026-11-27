# Fable 2 Recomp: Second-Round Research — Recomp Landscape, Player Complaints, Cut Content and a Function-Mapping Plan

The most useful thing the project can do next is turn manifest naming into an evidence-graded, repeatable process. The rest of the Xbox 360 recomp scene has shown that the long-lasting PC features (fixed 60 FPS timing, menu speed, native renderers) come from mapping the game's timers, GUI and render paths, while 2026's legal and AI developments reward projects that ship no game data and can explain every name they commit. For Fable II specifically, the best candidates are code- and Lua-level QoL fixes to the systems players complained about in 2008 (inventory/menu lag, map, shop and family management, co-op camera), plus a small set of cut-content leads (the Singing Sword quest files, Pub Games rewards) that must be checked in the retail data before anyone commits to them.

## TL;DR

- **Landscape:** ReXGlue (v0.10.0, released 21 Aug 2026) now underpins dozens of Xbox 360 ports. The ones that fixed high-frame-rate speed-ups (Deadly Premonition, LARecomp, Perfect Dark Zero) did it by finding and re-timing the game's own frame timer and fixed-timestep paths, not by raising tick rates globally. AI agents driving Ghidra/IDA through MCP are now standard practice, but published evaluations show they drift into inconsistent names over a large binary. Nintendo's November 2026 DMCA wave against recomps (one of them openly AI-built) is the main new legal risk. Microsoft has not taken comparable action against Xbox 360 recomps (unverified beyond the sources checked).
- **Players and cut content:** The most common and most fixable 2008 complaints are laggy inventory/menus, a weak map, finding shops/family/houses, the B-button cancel that casts spells, the shared co-op camera, and the ease of combat. These are "reasonable" recomp targets. New quests, voice acting, an ending rewrite, independent co-op cameras and online co-op revival are "unreasonable". The top cut-content lead is the Singing Sword quest, which TCRF reports in the retail files. It is unverified how complete its scripts and dialogue are.
- **Mapping:** The difficulty work and PR #238 (22 functions named from generated code and the game's Lua bindings) show the working method. Formalise it as evidence levels E0–E5, one subsystem per session, one small PR per session, and an automated scenario check through the debug remote-control server. Map next: GUI/inventory, timers/tick scheduling, economy/property, save system, quest/script manager, achievements.

## Key Findings

1. **The Fable 2 Recomp is part of ReXGlue's origin story.** In a March 2026 Read Only Memo interview, ReXGlue's creator "Tom" said someone first contacted him about "a community working on the Fable 2 Recomp", whose Discord was owned by Loreaxe (Ryan Fisher). He then "adopt[ed] Xenia's codebase wholesale and replace[d] its JIT backend with AOT".\[1\] The project's fixes therefore matter upstream as well.
2. **The most transferable lesson from other 360 recomps is time-base surgery, not tick doubling.** Deadly Premonition's recomp unlocks the logic tick from the 30 FPS vblank gate and feeds it whole ticks through an error accumulator. Its README says this "changes the game's time base rather than just doubling the frame rate", and the doubled sounds and prop swaps reported at 60 FPS went away with it.\[2\] LARecomp found that its 2x speed bug needed hooks on two fixed-timestep paths and had to keep a timer reset guard, because dropping it caused an audio blowout.\[3\]
3. **AI-assisted naming works only with external memory and verification.** A June 2026 arXiv study (arXiv:2606.06838) by Neil Archibald and Ruben Thijssen of the Commonwealth Bank of Australia gave an LLM agent direct control of Ghidra through LaurieWired's GhidraMCP; the authors report that this phase "suffered from incomplete coverage and inconsistent improvements due to lack of quantitative guidance". The agent improved the first functions, then degraded, and later "misremember[ed] Function A's signature", producing inconsistent naming across the binary. Fable 2 Recomp's own most recent naming PR flags `QuatScaleToMatrix_8222E6C0` as "the least certain name here", and that name replaced the earlier, wrong `ComputeVectorHash`.\[4\] Confident wrong names already exist in the manifest.
4. **Player complaints converge on interface latency and navigation, not content volume.** Giant Bomb's 2008 review said the inventory "seems to get bogged down once you've collected a fair amount of items" and that finding "shops, family members and even your own house can be a chore".\[5\] RPGFan, mikeshea.net and videolamer all independently reported sluggish or lagging menus.\[6\]\[7\]\[8\]
5. **Cut content is thinly documented.** TCRF's Fable II page lists only two retail-data items: the Singing Sword cut quest (identified by file names) and Pub Games-exclusive rewards.\[9\]\[10\] Most other "cut" Fable II material (rapiers, bows, early UI, Banshee and Nightmare concepts, the Dunecrest name for Rookridge) is evidenced by concept art, guides and footage, not by retail data.\[11\]

---

## PART 1 — Second Round on the Decomp/Recomp Landscape (mid-2025 to Nov 2026)

### 1.1 ReXGlue's state and ecosystem

- **Release cadence.** ReXGlue v0.10.0 was released on 21 Aug 2026. A tracking site listed its last push as 2 Oct 2026 and zero commits in the 30 days to 25 Oct 2026,\[12\] which suggests upstream has slowed and per-game forks carry much of the work. Fable 2 Recomp itself builds against "the project's fork" of the SDK.\[13\]
- **Architecture statement from its author.** The graphics system is "an abstract interface in the SDK… injected at build time". The Xenos GPU backend is described as a subsystem the project "fully intend[s] to replace with native rendering over time".\[1\] This matters for the open "ReGPU" issue (#187) in Fable 2 Recomp.\[14\]
- **Scale.** HeldGames' November 2026 list counts 50 Xbox 360 recomps, at least 29 playable and 33 with an Android build. It counts 83 original-Xbox titles with builds, after a separate original-Xbox toolkit (sp00nznet's xboxrecomp, plus an "XBE Recomp" toolkit shared by about twenty ports) took off in October–November 2026.\[15\]

### 1.2 Other Xbox 360 recomps and the features they shipped

| Project | Base | Features/QoL worth noting for Fable 2 |
|---|---|---|
| Perfect Dark Zero Recomp | ReXGlue | Reused community Xenia patches (60 FPS, 16:9) plus a fan "Reinstated" project's high-FPS physics, cutscene timing fixes, menu pointer and intro skip.\[16\] Shows the value of importing existing community knowledge (for Fable II: Archon's Toolbox, the femtofork patches). |
| Deadly Premonition Recomp (v2.0.14) | static recomp | Native DirectX 12 renderer (up to 4x internal resolution), 60 FPS through a time-base change, mouse look, FSR 3, an ISO installer, and "one present per game frame" so the UI stops repainting on every monitor refresh.\[2\] |
| LARecomp (Midnight Club LA) | static recomp | Real-frame-delta injection behind a debug-menu "FIXES" page of triage switches. Documented a frame-rate-collapse workaround while waiting for an SDK fix.\[3\] |
| Majistic Recomp (Majin) | static recomp | Per-system `anim_speed` normalisation for facial and gesture animations that ran 2x–4x too fast above 30 FPS, and multi-threaded PSO pre-compilation to remove shader stutter.\[17\] |
| GTA V (TU27) recomp | ReXGlue v0.10.0 | New Vulkan renderer that "replays the game's draws natively" on PC, Mac and phones.\[18\] A detailed "Keeping it clean" policy for forks.\[18\] |
| WetRecomp, Blue Dragon (ReBlue), Nuts & Bolts (reNut), Halo 3 beta, Viva Piñata, Lost Odyssey, Saints Row, Spider-Man 3, NFS Carbon and others | ReXGlue | Shows how broad the base is. Blue Dragon is the creator's pilot title for multi-disc work.\[1\]\[12\]\[19\] |
| "Fable: Return to Albion" (Fable2Recomp org) | ReXGlue + Godot 4.7.1 | Claims a Godot front-end around a recompiled Fable II/III simulation, with local co-op, "RexNet online multiplayer"\[20\] and a "Fully Playable" status.\[21\] **Unverified marketing-style claims.** Treat them as unconfirmed until reproduced. Its co-op claims are the only public lead on reviving online play. |

Observation: the projects with the strongest reputations ship (a) a per-system list of fixes tied to specific game functions, and (b) clear statements of what is still broken. That is a documentation and mapping practice, not an engine feature.

### 1.3 Fixed-timestep and high-FPS fixes (directly relevant to `high_tick_rate`)

- **Deadly Premonition:** keep the console's tick semantics and accumulate real time into whole ticks.\[2\]
- **LARecomp:** there were *two* fixed-timestep paths, and the timer reset guard had to stay.\[3\]
- **Astral Chain PC recomp (PR #62):** takes the 60 FPS step from the game's own frame timer "once per game frame, in the same place the game stores its step". It keeps stick gestures at their 30 FPS timing, and a scripted run confirmed "game speed 1.000 at 60 fps in gameplay".\[22\]
- **ACNH recomp:** documents the failure mode plainly. Logic runs once per frame, so "video.frame_rate = 60 also makes it run twice as fast", until a SpeedFix rescales game speed.\[23\]
- **LCS (PS2) recomp:** fixes per system (vehicle rolling resistance, wanted-star blink timing moved to monotonic real-time).\[24\]

Lesson: a global tick-rate increase always leaves tick-counting systems running fast. The fix is per-system: find each counter that assumes a fixed tick length, then either scale its increment by the real step or move it to wall-clock time.

### 1.4 AI/LLM-assisted function identification and naming

- **MCP bridges are mainstream.** GhidraMCP (LaurieWired, 12.0k stars and 1.3k forks on its GitHub repository page) exposes decompile, rename, cross-reference and import queries to any MCP client. bethington's ghidra-mcp v5.0 adds "convention enforcement" at the tool layer, which forces agents to follow naming rules.\[25\] Other bridges include pyghidra-mcp and RE-MCP (formerly ida-mcp, which gained a Ghidra backend in May 2026, including Ghidra's Function ID library matching).\[26\] REA 6.7 (Oct 2026) wraps Ghidra, Hopper or IDA, and its example rebuilt a function that matched the original on 3,205 test inputs and 63 bytes of machine code.\[27\]
- **Failure modes are documented.** Without measurable feedback the agent's quality decayed and naming became inconsistent across functions.\[28\] SK2Decompile (arXiv 2509.22114) separates structure recovery from identifier naming,\[29\] which supports treating naming as its own verified step.
- **Implication for Fable 2:** the manifest plus per-entry evidence comments is the external memory these agents lack. Use the agent to propose names and to draft evidence, and require an evidence level (Part 4) before merging.

### 1.5 Measuring mapping progress honestly

There is no standard in the 360 recomp scene. Fable 2 Recomp's README is unusually explicit: "Name what you discover… give it a real name… with a comment saying what it does and how you know. Rename generic sub_ / ProcessAndProcessAndProcessN_ names and wrong ones."\[13\] PR #240 added a CLAUDE.md and a `/name-functions` skill that "follows the same process as… #238".\[30\] Honest metrics proposed in Part 4 separate *descriptive* names from placeholders and weight them by evidence.

### 1.6 Legal and takedown developments

- **Nintendo, November 2026.** Nintendo filed four DMCA notices at GitHub on 6 Nov 2026. Removed projects include PowerMoon (Super Mario Odyssey), SkyRecomp (Skyward Sword HD), Project Tandem (Double Dash) and MTF-Recomp (Mario Tennis Fever).\[31\]\[32\] MTF-Recomp stated it was "built with heavy help from an AI coding agent (Claude)".\[33\] Hookshot (Ocarina of Time remake recomp, more than 8,500 stars) was taken down on 13 Nov 2026. These are notices, not court judgments.\[34\] Secondary coverage says ports of *current* titles and asset distribution were the common factors (partly unverified).
- **Microsoft.** Secondary reporting describes an October 2026 "Xbox Labs" note by Asha Sharma that names decomps, recomps and ports as areas of interest. Microsoft did not answer The Verge on unauthorized IP use.\[35\] **Speculating:** this lowers but does not remove risk for a 2008 first-party title, especially with a new *Fable* due in 2027.
- **Clean-distribution norms.** The GTA V recomp states that "the output of the recompiler is the game's code in another form… It isn't legal to hand it to someone else". Its rule is that no recompiled code, shaders or game files appear in any repository or release.\[18\] Fable 2 Recomp already ships no game data.\[36\] Keep `generated/` out of releases and forks, as now.

### 1.7 Community governance around AI contributions

Fable 2 Recomp states that it began as a test of a local model (Qwen3.8 27b) and that humans then mapped the functions.\[13\] Directories such as Emulation King label it "Mostly AI-built", and recomp.gg says "people wrote the code with an AI's help".\[37\]\[38\] Contributor PRs carry Claude co-author trailers.\[39\]\[40\] Lesson: disclosure is already public, so the defensible position is *verifiability*. Every AI-proposed name carries evidence and a test, which answers "AI slop" criticism with checkable artefacts.

### 1.8 Lessons for Fable 2 Recomp

1. Re-time per system (timers, GUI, jobs, animation) instead of only raising LF/HF rates.
2. Add a "FIXES" triage page to the in-game settings or console (LARecomp's model) so testers can isolate regressions.
3. Import existing Fable II community knowledge: Archon's Toolbox (Lua mod framework, CombatRebalance, Death Matters), the femtofork patches, Fable2Modding's Lua table lists.\[41\]\[42\]\[43\]
4. Make evidence-graded naming the contribution standard. Tool-layer convention enforcement (ghidra-mcp style) can be copied by extending `tools/check_manifest_collisions.py`.
5. Keep the asset boundary strict and don't target unreleased or current IP.

---

## PART 2 — What Original Fable II Players Complained About

### 2.1 Additional, specific complaints (beyond the first round)

- **Inventory and menus.** The inventory "seems to get bogged down" as items accumulate, and managing or using items is "cumbersome" (Giant Bomb, Vinny Caravella, 21 Oct 2008).\[5\] "Every screen seems to take longer than it should, making for a tedious inventory and skill system" (mikeshea.net, 26 Oct 2008).\[7\] "The menus lag badly and weren't given enough thought" (videolamer).\[8\] This matches the recomp's own current reports of slow inventory menus.
- **Navigation.** There is no overall world map, so regions feel disconnected (DarkZero).\[44\] "The map is useless except for a vague general overview" (buttonmashing).\[45\] Finding shops, family members and your own house is "a chore" (Giant Bomb).\[5\]
- **Controls.** B cancels in most games but in Fable II it can fire a spell, which caused accidental town-burning (buttonmashing). The expression menu is "counter-intuitive" (same source).\[45\] The recomp's own issue #236 reports that the expression wheel is hard to select with keyboard and mouse.\[14\]
- **Co-op specifics.** Online co-op was not functional at launch; on 29 Sep 2008 TechCrunch reported that "Lionhead says it should have the mode up and running within a week of the game's release". The host controls the camera, which hurts the henchman's aim (Critical Hits).\[46\] The left bumper only recentres on the main player (Co-Optimus).\[47\] There are co-op achievements, so achievement hunters are forced into it (RPGFan).\[6\]
- **Depth.** Few weapons, skills and spells; "after just one play through your character can already do everything"; jobs reuse "the same mini-game mechanic as social interaction" (videolamer).\[8\] The game is "still way too easy" (Critical Hits).\[46\]
- **Death and consequence.** Being knocked out only costs uncollected experience orbs and leaves a scar. Reviewers called this the "biggest failure, a lack of consequence" (DarkZero).\[44\]
- **Ending.** There is no final fight and Reaver can't be punished (GameFAQs). The Knothole Island DLC can revive the dog, which makes the "Love" wish feel pointless in hindsight (GameFAQs; Twenty Sided).\[48\]\[49\] Family revival brings no acknowledgement scene (GameFAQs).\[50\] The evil option's 1,000,000 gold payout is meaningless to players already rich from rent (Twenty Sided comments).\[51\]
- **Knothole Island DLC.** Short: The Globe and Mail finished nearly everything in under three hours.\[52\] Dungeons are "rote", with too many flit switches (Joystiq).\[53\] There is no map on the start menu, and players must leave the island to receive the next quest's details (Console Monster).\[54\] The quests are a single chain whose quest-giver reviewers found unlikeable (Silicon Sasquatch).\[55\]
- **Bonus content lost.** fable2.com web unlocks (chicken suit, hero doll, pink dye) are gone. Only Hal's equipment survives, as a paid DLC (GameFAQs thread).\[56\]\[57\]\[58\] Pub Games tournament rewards became unobtainable after the Xbox 360 Store closed (TCRF).\[9\]\[10\]

### 2.2 Already done in the recomp (not proposed)

Keyboard/mouse prompts with automatic icon switching; direct mouse camera; photo mode; developer console (about 110 commands including godmode, noclip, warp, giveall); Hor+ widescreen up to 4:1; Quit item in the main menu; frame limiter and 60 FPS; VSync; skipping intro videos; faster initial loading. Also shipped: optional `disable_motion_blur` and High/Higher HF tick-rate options (1.5.0).\[59\]

**In progress (delta4o and others):** difficulty settings (#214, with sub-issues #217–#219); mouse aim-down-sight (draft PR #183); move while aiming; stick swap; an in-game settings menu; blacksmithing/job-minigame and expression-hold timing (#216); a property-management menu request (#211).\[14\]\[60\]

### 2.3 REASONABLE to add or fix in a recompilation

| # | Feature / fix | Why it fits | Effort |
|---|---|---|---|
| R1 | Inventory/shop menu latency fix | Most-cited 2008 complaint and a current player report. Needs profiling the GUI/GameFace update path (`GuiManager_Update` is already named). | Medium |
| R2 | Remap "cancel" away from spell-cast in menus/conversations (KB/M and pad option) | Pure input layer, optional toggle. | Small |
| R3 | Expression wheel usable with KB/M (#236) and a hold-timing fix (#216) | Already being worked on. Input plus Lua. | Small–Medium |
| R4 | Map markers for owned houses, family members and shops | Uses existing map UI. Likely Lua-driven (speculating). | Medium |
| R5 | Property management menu (collect/set rent from anywhere) (#211) | Existing economy data. Optional. | Medium |
| R6 | Difficulty tiers (Easy/Medium/Hard/Hardcore) | Fable III PC's Hardcore mode is the precedent. Community demand shown by issue #83.\[61\] | Medium (in progress)\[61\] |
| R7 | Optional knockout penalty (gold/XP loss) | Archon's Toolbox "Death Matters" proves it is feasible in Lua. | Small\[42\] |
| R8 | Optional disabling of the rent-via-system-clock exploit | Clock hook. Off by default. | Small |
| R9 | Multiple manual save slots / save backups | Single-slot complaint. The host side can copy the `saves/` folder. A full in-game slot UI needs the save system mapped. | Small (backup) / Large (UI) |
| R10 | Collectibles tracker (gargoyles, silver keys, Demon Door status) via the console/F5 menu | Read-only Lua queries. | Small–Medium\[13\] |
| R11 | Breadcrumb trail colour/visibility options | Renderer/Lua parameter. | Small |
| R12 | Achievements surfaced locally (re-enable the commented-out overlay hooks in `fable_2_app.h`) | Shows progress without Xbox Live. | Medium |
| R13 | Restore Pub Games reward items *if they already exist in retail data* | No asset distribution. Only spawns content already on disc. | Small–Medium (unverified)\[10\] |
| R14 | Distance fog, LOD and pop-in options | PC expectation. Needs renderer LOD functions mapped. | Medium–Large |
| R15 | Per-system high-tick-rate correction (keep job-meter benefits, fix 2x timers) | Core stability work. See Part 5. | Medium |
| R16 | Co-op camera: wider tether / zoom-out option for local co-op | Parameter change only, not independent cameras. | Medium (speculating) |
| R17 | Optional potion auto-use / quick-slot | Input plus Lua over existing items. | Medium |
| R18 | Known quest-bug fixes (Hammer follow in the Spire, invisible dog, Abbot quest) | "How it was always supposed to be". | Small–Medium each |
| R19 | Fishing as a fourth job (opt-in mod, not core) | Archon's Toolbox already implements it in Lua. Port as an optional mod. | Medium\[42\] |

### 2.4 UNREASONABLE (or out of scope)

| # | Idea | Why not | Effort if attempted |
|---|---|---|---|
| U1 | Co-op as your own hero with independent cameras / split screen | Needs a second render view and a rework of the character/save model. Engine-level. | Large |
| U2 | Reviving Xbox Live online co-op | Network services are gone. Replacement netcode is a project in itself. "Return to Albion" claims this, unverified. | Large\[21\] |
| U3 | New final boss fight / ending rewrite / killing Reaver | Needs new content, animation and voice acting. Changes authorial design. | Large |
| U4 | Family acknowledgement scene after the Love ending | Needs new dialogue and voice. | Large |
| U5 | More weapons, spells and skill trees | New assets and balance. Better done as optional mods than in core. | Large |
| U6 | Seafaring / sailing | Never built. No evidence in the data. | Large |
| U7 | Knothole: more quests | New content. | Large |
| U8 | Restoring fable2.com web unlocks (chicken suit, etc.) by shipping files | Only legitimate if the items are on disc. Shipping assets is forbidden. | n/a |
| U9 | Removing breadcrumb, map or expression systems entirely | Breaks design. Options are fine, removal is not. | — |
| U10 | Fable III content or assets in Fable II | Asset distribution and a different game. | n/a |

---

## PART 3 — Cut and Unused Content Still Recoverable

**Method caveat:** public documentation of Fable II's *retail-data* leftovers is thin. TCRF lists only two data-backed sections. Everything below is graded by evidence type. Nothing here should be distributed. All restoration must read the player's own `data/` files (for example `gamescripts.bnk`, art and audio banks) at runtime.

| Rank | Item | Evidence it exists in retail data | Completeness | Restoration feasibility |
|---|---|---|---|---|
| 1 | **Singing Sword cut quest** (quest-giver Esmeralda; a "mega chicken" encounter; keep-or-give-up choice) | TCRF says it is "buried within the Fable II files", identified "according to the file names".\[10\] A YouTube "Fable II Cut Content: The Singing Sword" video exists.\[10\]\[62\] | Unknown. File names prove intent, not playable scripts. | If quest Lua plus dialogue banks exist in `gamescripts*.bnk`, a Lua re-enable or quest-start via the F5 runner is plausible. If only names exist, it needs new content (unreasonable). **Verify first.** |
| 2 | **Pub Games tournament rewards** (15 unique items; the Fable Wiki lists the Championship Pistol from Fortune's Tower, the Championship Cutlass from Keystone, the Highroller's Coat, the Plaits Hairstyle Card, Tribal Warrior Face and Body Tattoos, the Mysterious Ring, Dog Training: Backflip, and Potions of Will, Strength and Skill; achievement "I Want It All - Earn all 15 unique items") | TCRF: "unless it already existed in the game files", which is explicitly unconfirmed.\[9\]\[63\] | Items were granted via a cross-title save flag. The flag itself is lost. | If the item definitions are on the GOTY disc, a console `give` command proves it immediately. The `giveall` command may already reveal them (unverified). |
| 3 | **Unused areas / characters / objects** (TCRF category tags) | TCRF category tags only.\[10\] No details on the page.\[9\] | Unknown | Level and asset survey needed. Possibly debug-warp-reachable. |
| 4 | **Lua-level debug/unused functions in the game's own scripts** | Fable2Modding decompiles the three script `.bnk` files. Keshire (se7ensins) noted that the free DLC packs contain "overriding startup script directories". | Real code\[43\]\[64\] | High: the recomp already runs arbitrary Lua (F5 runner).\[13\] Diff the scripts in the three banks for orphaned quest/debug functions. |
| 5 | **Hall of the Dead dungeon, Wreckager, Hal's armour** (pre-order/LCE bonus) | Unlocked by DLC/token. The token unlock still exists as paid DLC since 2014.\[57\] | Complete content | Only if the content is in the GOTY data or the user's own DLC. No distribution. |
| 6 | **Cut combat mechanics, rapiers, bows, elaborate early UI** | TCRF *prerelease* page (concept art, dev-diary footage) | Concept-level\[11\] | Rapiers and bows: check item tables for leftover entries. Otherwise not recoverable. |
| 7 | **Dunecrest (Rookridge's earlier name), beta maps, Rose's earlier look** | Fandom/Unseen64 images and the Limited Edition guide\[65\] | Pre-release only\[65\] | Searching strings for "Dunecrest" is cheap and would confirm or reject it. |
| 8 | **Fishing** (pre-release idea) | No retail-data evidence found. Archon's Toolbox implements a new fishing job. | Fan-made\[42\] | Optional mod, not restoration. |
| 9 | **Unused Fable II assets in Fable III** | Fandom says these sit in *Fable III*'s files\[65\] | — | Not usable: wrong game and asset distribution. |

**Top gold nuggets to investigate:**
1. Singing Sword (string, file and script search in `gamescripts*.bnk` and dialogue banks).
2. Pub Games items (console item-ID dump compared against the item tables).
3. A script-bank diff for orphaned quest/debug Lua.
4. The unused-areas tag (level list against the warp table).

**Assets-distribution flag:** items 5 and 9, and any fable2.com web unlock missing from the disc, cannot be restored without distributing assets. Exclude them.

---

## PART 4 — Manifest Exploration: What the Difficulty Work Taught, and a Repeatable Process

### 4.1 What can be confirmed from the difficulty work

**Access limitation:** the `feature/game-difficulty` branch itself could not be read during this research (the fetch was refused or rate-limited). Specific difficulty-branch function names and addresses are therefore **not confirmed here**.

What is confirmed:

- **Issue #214** (opened by delta4o, 17 Nov 2026) defines four tiers affecting gold and experience gain and cost, enemy health and damage received:\[60\]

  | Tier | Gold gain | Gold spend | Enemy health | Enemy damage |
  |---|---|---|---|---|
  | Easy | original | original | original | original |
  | Medium | ×0.9 | ×1.1 ("effective increase of 20%") | ×1.2 | ×1.2 |
  | Hard | ×0.8 | ×1.2 | ×1.2 (as printed) | ×1.2 (as printed) |
  | Hardcore | ×0.7 | ×1.3 | ×1.6 | ×1.6 |

  Hard's ×1.2 health and damage matches Medium in the issue text, which is probably a typo worth fixing. No experience multipliers are listed yet. Sub-issues cover old-save compatibility (#217), selection on new game (#218) and selection in settings (#219). The issue also floats save-mechanic changes for extra challenge.\[60\]
- **PR #238** (delta4o, merged by himdo) went through master's last 40 commits and named the 22 functions they call, hook or describe.\[4\] Examples:
  - Photo mode: `LuaDebug_SetDrawGUI_823AC520`, `LuaTiming_SetTimeOfDayAbsolute_8249A790`.
  - Frame and GUI: `PresentFrame_82B69488`, `GameFrame_HfTickUpdate_82278C90`, `GuiManager_Update_82286B40`, `Timer_GetElapsed_822B6D70`.
  - Cloth and pose: `Cloth_Step_8219ADD8`, `Pose_BlendForDraw_8222E300`.

  The techniques it states:
  - reading each body in the generated code;
  - matching the game's Lua commands in `FABLE2_LUA_API.md` to their native bodies;
  - following call chains (`PresentFrame` → `MainRenderLoop_82B9CD68` → `VdSwap`);
  - reusing another contributor's branch notes;
  - flagging instruction-pattern-only names as least certain.

  himdo's review said the PR "improves readability and reusability a ton".
- **The naming rule is now policy** (README, plus CLAUDE.md and the `/name-functions` skill in PR #240).\[13\]\[30\]

**Speculating** about the difficulty branch's mapped systems: scaling gold gain/spend, XP and enemy health/damage requires hooks at the gold-transaction, experience-award, damage-application and creature-spawn/health-init points. Those would be the natural names that branch produced. Record them with evidence levels when the PR lands.

### 4.2 Evidence levels (what counts as "verified")

| Level | Meaning | Minimum evidence recorded in the manifest comment |
|---|---|---|
| E0 | Placeholder (`sub_X`, `ProcessAndProcessAnd…`) | none |
| E1 | Pattern guess (instruction shape, size, constants) | "pattern: …" |
| E2 | Static read (body read, callers and callees named, arguments described) | callers/callees, argument meaning |
| E3 | Anchored (Lua binding name from `FABLE2_LUA_API.md`, debug/assert string, vtable slot of a known class) | the anchor, quoted |
| E4 | Runtime-verified (a hook or probe observed the expected values in game, or an on/off A/B diff changed the predicted behaviour) | scenario, observed values, commit |
| E5 | Regression-tested (an automated scenario through the remote-control server asserts the behaviour) | scenario file name |

Rules:
- Only E3 and above may be renamed into "public API" names that features depend on.
- E1 names carry a `?` suffix in the comment (not in the symbol).
- Any rename of an existing name must cite stronger evidence than the original.

### 4.3 Naming conventions

Keep the README rule `Subsystem_WhatItDoes_ADDRESS` (for example `Cloth_Step_8219ADD8`):\[13\]

- Use `Lua<Library>_<Binding>` for native Lua bindings.
- Use `<Class>_<Method>` when a vtable or class is known.
- Normalise older names without the address suffix (for example `FrontMenu_Populate`) on touch.
- Never encode a guess as fact. Prefer `Combat_ApplyDamageCandidate_…` over a confident wrong name.

### 4.4 Candidate-finding techniques, in yield order

1. **Lua binding tables:** the registration code maps names to native function pointers, which gives an E3 anchor for each binding. `FABLE2_LUA_API.md` and the LuaPlus bound-method dispatcher already probed by the F5 runner are the entry points.\[13\]
2. **Cross-references outward from already-named functions** (the call-chain method of #238).
3. **Debug and assert strings and format strings** referenced in the binary.
4. **Vtables:** group functions by vtable slot, then name the class from one anchored method.
5. **Probe logging:** hook a candidate, log arguments and return values to the console, trigger the action in game.
6. **A/B diff:** toggle a hook that perturbs a value (for example a ×2 multiplier) and confirm the predicted gameplay change.
7. **Data-driven anchors:** script `.bnk` Lua calling into C reveals which natives a quest or system uses.
8. **Agent assistance:** a Ghidra/IDA MCP agent drafts names and evidence, and a human (or the scenario loop) promotes the level.

### 4.5 Session template (one subsystem, one PR)

1. **Pick a target** from the priority list (4.7) and write a one-paragraph question ("Which functions award gold?").
2. **Seed** from Lua bindings and strings, then expand callers and callees two levels.
3. **Probe** each candidate in a debug build and record the observations.
4. **Name**, with an evidence level and a comment covering arguments, effect and how-known.
5. **Add one scenario file** for the most important function (E5).
6. **Open a PR** with no behaviour change. Body: target, the names table, evidence, uncertain names flagged. Run the collision-check tool and the tests.
7. **Update the progress metrics.**

### 4.6 Honest progress metrics

- **Descriptive-name coverage:** named (E1+) / roughly 60,700 total. Report E3+ separately.
- **Placeholder debt:** the count of `ProcessAndProcessAnd…` and `sub_` names in the manifest. The user's count is more than half of roughly 5,000.
- **Hot-path coverage** (speculating as a useful metric): the share of the functions executed in a standard scenario run that are E2 or higher, measured by an instrumented debug build. This tracks the code that matters.
- **Per-subsystem dashboards:** GUI, timers, economy, combat, save, quest, render.

### 4.7 Prioritised systems to map next

1. **Timers and tick scheduling** (LF/HF update, `Timer_GetElapsed`, job meters). Unblocks the high-tick-rate fix and the job/expression timing work.
2. **GUI/GameFace and inventory** (`GuiManager_Update` and its descendants). Unblocks the menu-lag fix, the settings menu and difficulty selection.
3. **Economy and property** (gold transactions, rent, shop prices, clock). Needed by difficulty, the rent-exploit toggle and the property menu.
4. **Combat and creature stats** (damage application, health init, knockout). Needed by difficulty and the death penalty.
5. **Save system.** Old-save compatibility (#217), slots and backups.
6. **Quest/script manager** (QuestManager natives). Needed for cut-quest investigation and bug fixes.
7. **Achievements** (the overlay hooks commented out in `fable_2_app.h`).
8. **Dog AI and pathfinding.** Invisible-dog and following bugs.
9. **Renderer LOD and fog.** Pop-in options and the ReGPU work (#187).
10. **System notifications.** Issue #237 (screen turns black on a "free title" notification)\[14\] suggests a XAM/notification path worth naming (unverified cause).

### 4.8 Tie-in with the automated checking loop

Scenario files (input sequences plus console reads) sent through the debug-build remote-control server serve two purposes:
- They are E5 evidence for names.
- They are regression tests for features. Examples: "buy item X at shop Y on Hardcore; assert gold delta = price × 1.3"; "run 60 s at high_tick_rate; assert in-game clock advanced 60 s".

Keep scenarios small and deterministic: start from a fixed save, disable random events where possible.

---

## PART 5 — Other Open Threads

### 5.1 `high_tick_rate` side effects

- **Diagnosis.** LF 15→30 Hz and HF 30→60 Hz double the rate of every system that counts ticks. PR #208 already moved GUI updates to every HF tick, and `GameFrame_HfTickUpdate_82278C90` and `Timer_GetElapsed_822B6D70` are named. **Speculating:** blacksmithing feels easier because the job meter's input sampling or cursor step runs at the higher rate while the success window keeps its width.
- **Recommendation:**
  - Keep the higher sample rate for input and the meter.
  - Scale every tick-counted *timer* (cooldowns, timed events, clock-driven Lua waits) by `baseline_tick / current_tick`, or move it to `Timer_GetElapsed`.
  - Do this per system, as LARecomp, Astral Chain and Majistic did.
  - Add one scenario per fixed system that asserts wall-clock duration.
  - Whether the Lua scheduler counts ticks or seconds is **unknown**. Test it with a Lua wait of a known duration at both rates.

### 5.2 Player reports

- **i7-3770 crash on the loading screen.** Intel's ARK page for the i7-3770 (Ivy Bridge) lists its instruction set extensions as "Intel® SSE4.1, Intel® SSE4.2, Intel® AVX", with no AVX2 (introduced with 4th-gen Core). Another recomp confirmed exactly this CPU fails on AVX2 builds (illegal instruction 0xC000001D), and shipped a `*_SSE4.exe` that the normal executable launches on non-AVX2 CPUs. The LCS recomp moved to an AVX1 baseline.\[24\] LASRecomp kept AVX2 for speed.\[66\]
  - Recommendation: check the CMake flags (`-mavx2`/`/arch:AVX2`, `-mfma`) in both the project and the ReXGlue fork.
  - Add a CPUID check with a clear error dialog.\[67\]
  - Optionally build a second baseline binary.\[68\]
- **Slow inventory menus:** see R1 and mapping priority 2.
- **Android texture issues:** route to FableDroid. Likely GPU-backend specific (unverified).
- **Outdated AMD driver crash:** add the driver version to the log header and to the issue template (#221).\[14\]

### 5.3 Decompilation vs. recompilation

The README already states the long-term goal is "a readable decompilation that supports modding".\[13\] Evidence-graded naming serves *both* paths: names, types and evidence are prerequisites for any later matching decompilation, and they make the recomp moddable now. A full matching decomp of roughly 60,700 functions requires the original XDK compiler toolchain and years of effort. That decision belongs to the maintainer. **Recommendation:** continue the recomp and treat mapping as shared groundwork.

---

## Caveats

- Many 2026 sources are secondary aggregators (HeldGames, CDKeyPrices, explainx). The DMCA dates are corroborated across several of them, but Microsoft's "Xbox Labs" framing comes from one secondary source.
- The difficulty branch was not readable, so Part 4.1 relies on issue #214 and PR #238.
- Cut-content completeness is unverified. TCRF's Fable II page is short and unsourced in places.
- "Return to Albion" claims are unverified.

## What to Investigate Next (prioritised)

1. Fix the Hard-tier typo in #214, then land the difficulty PR with every hooked function named at E3/E4 and one scenario per multiplier.
2. Map timers and tick scheduling, then fix the 2x side effects per system, keeping the job-meter sampling benefit.
3. Profile the inventory/GUI path behind the menu lag, and name its functions.
4. Check AVX2 flags, then add a CPUID gate or a baseline build for Ivy Bridge players.
5. Run a script-bank and string search for the Singing Sword quest, "Dunecrest" and Pub Games item IDs, and report findings without sharing assets.
6. Add evidence levels and progress metrics to `/name-functions` and the collision-check tool.
7. Re-enable the achievement overlay hooks once achievement functions are named.

## Sources

1. [Interview: How ReXGlue is bringing the Xbox 360 into the static recompilation era](https://readonlymemo.com/rexglue-xbox-360-recompilation-interview/)
2. [GitHub - LittleBitUA/DPRecomp: Deadly Premonition (Xbox 360) native PC port by static recompilation - v2.0.14: native DirectX 12 renderer ready for testing (up to 4x internal resolution); 60 FPS, mouse look, keyboard driving, FSR 3, ISO installer, PAL + USA · GitHub](https://github.com/LittleBitUA/DPRecomp)
3. [GitHub - mzzvxm/LARecomp · GitHub](https://github.com/mzzvxm/larecomp)
4. [Name the game functions used by recent PRs by delta4o · Pull Request #238 · himdo/Fable-2-Recomp](https://github.com/himdo/Fable-2-Recomp/pull/238)
5. [Fable II Review](https://giantbomb.com/reviews/fable-ii-review)
6. [Fable II Review](https://www.rpgfan.com/review/fable-ii/)
7. [Fable 2 and Buying Incomplete Unfinished Games : mikeshea.net](https://mikeshea.net/Fable_2_and_Buying_Incomp.html)
8. [Review](https://videolamer.com/review-fable-ii)
9. [Fable II - The Cutting Room Floor](https://tcrf.net/Fable_II)
10. [Fable II - The Cutting Room Floor](https://tcrf.net/Fable_2)
11. [Prerelease:Fable II - The Cutting Room Floor](https://tcrf.net/Prerelease:Fable_II)
12. [ReXGlue SDK: Xbox 360 executables source port, download & source · Recomp](https://recompsoftware.com/p/rexglue/)
13. [GitHub - himdo/Fable-2-Recomp · GitHub](https://github.com/himdo/Fable-2-Recomp)
14. [Issues · himdo/Fable-2-Recomp · GitHub](https://github.com/himdo/Fable-2-Recomp/issues)
15. [Recompiled Games List: Every Native PC Port by System (2026)](https://heldgames.com/guides/recompiled-games-list)
16. [GitHub - nikolaygorb/PerfectDarkZeroRecomp: Perfect Dark Zero \[Xbox360\] game static recompilation project based on RexGlue-SDK · GitHub](https://github.com/nikolaygorb/PerfectDarkZeroRecomp)
17. [GitHub - kelvynzucco/majistic-recomp: High-performance static recompilation of the Xbox 360 classic for Windows x64 PC (Direct3D 12, 60 FPS, C++23). · GitHub](https://github.com/kelvynzucco/majistic-recomp)
18. [GitHub - andrewnakas/gtav-recomp: GTA V (Xbox 360, TU27) static recompilation with ReXGlue · GitHub](https://github.com/andrewnakas/gtav-recomp)
19. [GitHub - nikolaygorb/WetRecomp: Wet \[Xbox360\] game static recompilation project based on RexGlue-SDK · GitHub](https://github.com/nikolaygorb/WetRecomp)
20. [GitHub - Fable2Recomp/Fable-Return-to-Albion: A Fable 2 & 3 Remake · GitHub](https://github.com/Fable2Recomp/Fable2Remake)
21. [GitHub - Fable2Recomp/Fable-Return-to-Albion: A Fable 2 & 3 Remake · GitHub](https://github.com/Fable2Recomp/Fable-Return-to-Albion)
22. [Take the 60 fps step from the game's frame timer and keep stick gestures at their 30 fps timing by capibawa · Pull Request #62 · captainj-sparrow/astral-chain-pc-recomp · GitHub](https://github.com/captainj-sparrow/astral-chain-pc-recomp/pull/62)
23. [GitHub - Nycolazs/ACNHRecomp · GitHub](https://github.com/Nycolazs/ACNHRecomp)
24. [GitHub - NicoWayne95/lcs-ps2-recomp: Experimental PS2-to-Windows recompilation project focused on compatibility, graphics rendering and performance. · GitHub](https://github.com/NicoWayne95/lcs-ps2-recomp)
25. [ghidra-mcp](https://www.bioplayground.com/en/ai-tools/ghidra-mcp)
26. [IDA-MCP Is Now RE-MCP With Ghidra Support](https://jtsylve.blog/post/2026/05/04/ida-mcp-becomes-re-mcp)
27. [Reverse Engineer Anything Explained (2026): Ghidra via MCP](https://www.sevydevy.com/learn/reverse-engineer-anything-explained-ai-agents-ghidra-mcp)
28. [Improving Decompiled Code Readability with LLM Agents](https://medium.com/commbank-technology/improving-decompiled-code-readability-with-llm-agents-f67bf10d2505)
29. [1 Introduction](https://arxiv.org/html/2606.06838)
30. [Add CLAUDE.md, Claude skills and a fix/feature docs layout by delta4o · Pull Request #240 · himdo/Fable-2-Recomp · GitHub](https://github.com/himdo/Fable-2-Recomp/pull/240)
31. [Mario Odyssey And Skyward Sword Decomps Taken Down By Nintendo](https://www.thegamer.com/nintendo-takes-down-four-fan-ported-pc-games-dmca-wave/)
32. [GitLab Is Not Purging Recomp Projects. Two Throwaway Accounts Vanished and the Scene Could Not Tell the Difference](https://heldgames.com/guides/gitlab-recomp-takedowns)
33. [Nintendo DMCA Hits Odyssey & Zelda PC Ports - CDKeyPrices.com News](https://cdkeyprices.com/news/nintendo-takes-down-mario-odyssey-and-skyward-sword-recomp-ports)
34. [Ocarina of Time Remake on PC: DMCA Risks for Downloads and Tools](https://finalboss.io/ocarina-of-time-remake-on-pc-what-nintendos-dmca-claims-mean)
35. [Xbox Labs: Microsoft Backs AI Game Ports (Oct 2026)](https://www.explainx.ai/blog/xbox-labs-ai-game-decompiling-recompiling-ports-asha-sharma-2026)
36. [\[Project\]: Fable-2-Recomp — Fable 2 Xbox 360 static recomp · C++ · Issue #192 · kaixinbaba/awesome-rea · GitHub](https://github.com/kaixinbaba/awesome-rea/issues/192)
37. [Fable 2 Recomp: Fable II PC Port - Emulation King](https://emulationking.com/microsoft/xbox360/games/fableii/ports/fable2recomp/)
38. [Fable-2-Recomp: Fable II recompilation · recomp.gg](https://recomp.gg/p/fable-2-recomp)
39. [Make dir.manifest self-heal idempotent and drop duplicate lines by Locme · Pull Request #61 · himdo/Fable-2-Recomp · GitHub](https://github.com/himdo/Fable-2-Recomp/pull/61)
40. [Add tests for the manifest collision check and the PE normalizer by delta4o · Pull Request #134 · himdo/Fable-2-Recomp · GitHub](https://github.com/himdo/Fable-2-Recomp/pull/134)
41. [GitHub - just-harry/unofficial-xenia-femtofork-for-fable-ii: An unofficial fork of Xenia with game-specific-hacks for Fable II. · GitHub](https://github.com/just-harry/unofficial-xenia-femtofork-for-fable-ii)
42. [GitHub - JustSomeGuy1234/Archons-Toolbox: A Mod Manager for Fable 2 Lua Mods · GitHub](https://github.com/JustSomeGuy1234/Archons-Toolbox)
43. [GitHub - JustSomeGuy1234/Fable2Modding: Scripts and information on modding Fable 2 for the Xbox 360, mostly the game's Lua implementation. · GitHub](https://github.com/JustSomeGuy1234/Fable2Modding)
44. [Fable 2 Xbox 360 Review - DarkZero](https://darkzero.co.uk/game-reviews/fable-2-xbox-360/)
45. [Fable II Quirks - buttonmashing](https://buttonmashing.com/2008/10/24/fable-ii-quirks/)
46. [Review: Fable 2 — Critical Hits](https://critical-hits.com/blog/2008/10/27/review-fable-2/)
47. [Fable 2 Co-Op Review](https://www.co-optimus.com/ampfeature/92/fable-2-co-op-review.html)
48. [The Disappointing Ending (SPOILERS) - Fable II - GameFAQs](https://gamefaqs.gamespot.com/boards/927246-fable-ii/48102848)
49. [worst/most disturbing storyline ever \*spoiilers\* - Fable II](https://gamefaqs.gamespot.com/boards/927246-fable-ii/48595764)
50. [Was anyone disappointed with Fable 2's ending? (spoilers) - Fable II](https://gamefaqs.gamespot.com/boards/927246-fable-ii/47068641)
51. [Fable 2: Thematic Failures - Twenty Sided](https://www.shamusyoung.com/twentysidedtale/?p=2102)
52. [Knothole Island is fun, but also short and trivial - The Globe and Mail](https://www.theglobeandmail.com/technology/gaming/controller-freak/knothole-island-is-fun-but-also-short-and-trivial/article25683544/)
53. [Joystiq review: Fable 2's Knothole Island (DLC) - Engadget](https://www.engadget.com/2009-01-14-joystiq-review-fable-2s-knothole-island-dlc.html)
54. [Fable 2 Knothole Island DLC Review - Console Monster](https://www.consolemonster.com/news/fable-2-knothole-island-dlc-review/)
55. [Review: Fable II: Knothole Island DLC (XBL) — Silicon Sasquatch](https://www.siliconsasquatch.com/blog/2009/4/28/review-fable-ii-knothole-island-dlc-xbl)
56. [fable2.com how do I get the stuff? is it even still possible to get? I want it! - Fable II](https://gamefaqs.gamespot.com/boards/927246-fable-ii/80635992)
57. [Fable II Limited Collector's Edition](https://fable.fandom.com/wiki/Fable_II_Limited_Collector's_Edition)
58. [LF: Fable 2 and 3 Item/Maxed Weapons/Clothing (including DLC) Save(s)](https://www.xpgamesaves.com/threads/lf-fable-2-and-3-item-maxed-weapons-clothing-including-dlc-save-s.137895/)
59. [himdo/Fable-2-Recomp 1.5.0 on GitHub](https://newreleases.io/project/github/himdo/Fable-2-Recomp/release/1.5.0)
60. [Difficulty settings · Issue #214 · himdo/Fable-2-Recomp · GitHub](https://github.com/himdo/Fable-2-Recomp/issues/214)
61. [Archons Toolbox Support · Issue #83 · himdo/Fable-2-Recomp · GitHub](https://github.com/himdo/Fable-2-Recomp/issues/83)
62. [Fable II Cut Content: The Singing Sword - YouTube](https://www.youtube.com/watch?v=DXphnOWLhOY)
63. [Fable II Pub Games](https://en.wikipedia.org/wiki/Fable_II_Pub_Games)
64. [Fable 2 LUA Scripts](https://www.se7ensins.com/forums/threads/fable-2-lua-scripts.264461/)
65. [Cut Content](https://fable.fandom.com/wiki/Cut_Content)
66. [No AVX2 for older CPUs request · Issue #186 · peiikai/LASRecomp · GitHub](https://github.com/peiikai/LASRecomp/issues/186)
67. [github.com](https://github.com/browseros-ai/BrowserOS/issues/202)
68. [Avx2 Cpu Instructions: Fix Unsupported Errors (patches)](https://laptopjudge.com/avx2-cpu-instructions-fix-unsupported-errors/)
