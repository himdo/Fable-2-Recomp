# Research notes (November 2026)

Second-round research on the recomp scene, what Fable II players complained about, cut content, and a repeatable way to map the game's functions. It was gathered from public sources in November 2026 and split into one fix or feature folder per topic, each with a `technical.md` (for developers, with sources) and an `overview.md` (plain language). This page is the index.

Claims marked *unverified* or *speculating* haven't been checked against the game or the code. Many 2026 sources are secondary aggregators. None of this is a plan the maintainer has agreed to.

## Summary

The most useful thing the project can do next is turn manifest naming into an evidence-graded, repeatable process. The rest of the Xbox 360 recomp scene has shown that the long-lasting PC features (fixed 60 FPS timing, menu speed, native renderers) come from mapping the game's timers, GUI and render paths, while 2026's legal and AI developments reward projects that ship no game data and can explain every name they commit.

For Fable II specifically, the best candidates are code- and Lua-level QoL fixes to the systems players complained about in 2008 (inventory/menu lag, map, shop and family management, co-op camera), plus a small set of cut-content leads (the Singing Sword quest files, Pub Games rewards) that must be checked in the retail data before anyone commits to them.

## Topics

| Doc | What's in it |
| --- | --- |
| [feature/recomp_landscape](../../feature/recomp_landscape/technical.md) | ReXGlue's state, other Xbox 360 recomps and what they shipped, lessons for this project |
| [fix/high_tick_rate_side_effects](../../fix/high_tick_rate_side_effects/technical.md) | How other recomps fixed high-FPS speed-ups, and a per-system plan for `high_tick_rate` side effects |
| [feature/ai_assisted_naming](../../feature/ai_assisted_naming/technical.md) | LLM agents and MCP bridges for naming, their documented failure modes, and the project's public AI disclosure |
| [feature/legal_and_distribution](../../feature/legal_and_distribution/technical.md) | Nintendo's November 2026 DMCA wave, Microsoft's stance, clean-distribution rules |
| [feature/player_complaints](../../feature/player_complaints/technical.md) | 2008–2009 complaints by area, and what the recomp already does |
| [feature/qol_candidates](../../feature/qol_candidates/technical.md) | Reasonable (R1–R19) and out-of-scope (U1–U10) fixes and features |
| [feature/cut_content](../../feature/cut_content/technical.md) | Cut-content leads ranked by evidence, and what to search first |
| `feature/difficulty_settings` (on branch `feature/game-difficulty`) | Not split out here: the difficulty work on that branch is further along than this research and issue #214, and already has its own pair |
| [feature/function_mapping](../../feature/function_mapping/technical.md) | Lessons from PR #238, evidence levels E0–E5, techniques, session template, metrics, mapping priorities |
| [fix/player_reports](../../fix/player_reports/technical.md) | Open reports: i7-3770/AVX2 crash, slow menus, Android textures, AMD driver, notification black screen |

## What to investigate next (prioritised)

1. Land the difficulty work (branch `feature/game-difficulty`) with every hooked function named at E3/E4 and one scenario per multiplier, and bring issue #214 up to date with the branch's multipliers (decision 6), which replace the issue's table.
2. Map timers and tick scheduling, then fix the 2x side effects per system, keeping the job-meter sampling benefit. ([high_tick_rate_side_effects](../../fix/high_tick_rate_side_effects/technical.md))
3. Profile the inventory/GUI path behind the menu lag, and name its functions. ([qol_candidates](../../feature/qol_candidates/technical.md), R1)
4. Find out why the i7-3770 crashes. Master's build turns on no AVX or AVX2, so the research's AVX2 explanation is in doubt; ask for the crash log first. ([player_reports](../../fix/player_reports/technical.md))
5. Check whether the Singing Sword quest can be started: the game's script API list already shows its tables are on the disc. Also try the Pub Games prizes through the console, and report findings without sharing assets. ([cut_content](../../feature/cut_content/technical.md))
6. Add evidence levels and progress metrics to `/name-functions` and the collision-check tool. ([function_mapping](../../feature/function_mapping/technical.md))
7. Check that the SDK's achievement tracking (pop-ups, the F7 list) survives a restart; the commented-out overrides are only optional replacements for its overlay. ([qol_candidates](../../feature/qol_candidates/technical.md), R12)

## Caveats

- Many 2026 sources are secondary aggregators (HeldGames, CDKeyPrices, explainx). The DMCA dates are corroborated across several of them, but Microsoft's "Xbox Labs" framing comes from one secondary source.
- The `feature/game-difficulty` branch wasn't readable during the research. It has since been checked (25 Nov 2026, at bad29df) and is well ahead of what the research assumed.
- Cut-content completeness is unverified. TCRF's Fable II page is short and unsourced in places.
- "Return to Albion" claims are unverified.
