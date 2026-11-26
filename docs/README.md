# Docs

Every fix and feature has its own folder with two files:

- **`technical.md`** for developers: symptom or goal, root cause or design, what changed (with file paths), how it was verified and what wasn't tested.
- **`overview.md`** for players and non-developers: what people saw before, what's different now, and anything they need to do.

Both files start with the same header: `Status` (merged / open PR / no commit yet), `Commits` (short hash and PR number of the work on master) and `Last checked against` (the master commit the doc was last checked against). Fixes live in `fix/`, features in `feature/`. Guides and reference material that aren't tied to one change live in `reference/` and don't need a pair.

When you finish a fix or a feature, add or update its folder and its row below. Refer to game functions by their names in `fable_2_manifest.toml`, and name any you worked out that still have a generic `sub_` or `ProcessAndProcessAndProcessN_` name: the long-term goal is a readable decompilation that supports modding, so what you discover belongs in the manifest. Claude Code users can run `/work-doc`. Never paste game text, dialogue, scripts or asset data into a doc; paraphrase instead.

## Fixes

| Fix | Status | Commits | Summary |
| --- | --- | --- | --- |
| [alt_tab_script_crash](fix/alt_tab_script_crash/overview.md) | merged | c5c80ff (#189), 266f181 (#195) | Crash after alt-tab: the direct mouse camera started a game script from the render thread |
| [amd_gpu_hang](fix/amd_gpu_hang/overview.md) | merged | SDK 0fe6654 (rexglue-sdk#29), pin in 2df8825 (#158) | RX 7000/9000 GPU hang in Wraithmarsh: negative relative constant index read 4 GB away |
| [fps_cap](fix/fps_cap/overview.md) | merged | 73d89b0, d28da25 (#143), e0c69c0 (#135) | The game's 30 FPS lock, and vsync presenting at refresh + 30 |
| [high_tick_rate_side_effects](fix/high_tick_rate_side_effects/overview.md) | no commit yet (research only) | — | Job minigames and other tick-counted timers run fast with High Tick Rate; per-system plan |
| [integrated_graphics_crash](fix/integrated_graphics_crash/overview.md) | merged (hybrid laptops); iGPU-only open | 4d58a5f (#55) | Crash after the opening cutscene on integrated graphics |
| [main_menu_crash](fix/main_menu_crash/overview.md) | merged | afebcc5 | Source builds crashed at the main menu; repeated memory reserves must fail |
| [player_reports](fix/player_reports/overview.md) | no commit yet (research only) | — | Open reports: i7-3770 crash (master builds without AVX), slow menus, Android textures, AMD driver, notification black screen |
| [rendering_and_performance](fix/rendering_and_performance/overview.md) | merged | #143, #148–#151, #158, SDK #25–#28 | 1.6 speed-ups and the tree flicker and logo flash they exposed |
| [runtime_fixes](fix/runtime_fixes/overview.md) | merged | 724495a, 359d3a1 (#148), e0c69c0 (#135) | Audio fallback, frame limits, async pipeline wait, dog fur depth bias, and how the SDK is built |
| [two_controller_black_menu](fix/two_controller_black_menu/overview.md) | merged | a4b0274 | Black main menu when a second controller pressed A |

## Features

| Feature | Status | Commits | Summary |
| --- | --- | --- | --- |
| [ai_assisted_naming](feature/ai_assisted_naming/overview.md) | no commit yet (research only) | — | What 2026 research says about AI agents naming functions, and the project's AI disclosure |
| [brazilian_portuguese](feature/brazilian_portuguese/overview.md) | open PR #242 | bf3eb48, b53cbb8, c5d3c15 | Install or remove a Brazilian Portuguese text pack from the Windows launcher and select it in the game |
| [console_commands](feature/console_commands/overview.md) | merged | 4f0afe5 (#127) | The `f2` developer console and its command reference |
| [cut_content](feature/cut_content/overview.md) | no commit yet (research only) | — | Cut-content leads ranked by evidence: the Singing Sword's scripts are on the disc; website and CE unlocks already ship |
| [fit_window](feature/fit_window/overview.md) | merged | 4f0afe5 (#127) | Render at the window's aspect ratio, with the HUD and menus following |
| [function_mapping](feature/function_mapping/overview.md) | no commit yet (research only) | — | Proposed evidence levels E0–E5, session template, metrics and mapping priorities |
| [function_naming](feature/function_naming/overview.md) | merged | f451b72 (#238) | Manifest names for the 22 functions recent PRs relied on |
| [german_goty_support](feature/german_goty_support/overview.md) | merged | bfcc5d6, 77059af | Accept the German GOTY XEX alongside USA/Europe |
| [guest_patches](feature/guest_patches/overview.md) | merged | cab738b, #208, #210, #226, #228 | Xenia-style guest patches, mid-asm hook patches and their `[patches]` switches |
| [legal_and_distribution](feature/legal_and_distribution/overview.md) | no commit yet (research only) | — | Late-2026 recomp takedowns and the no-game-data distribution rules |
| [manifest_naming_check](feature/manifest_naming_check/overview.md) | no commit yet | — | A CI check that new and renamed manifest names follow the naming rules and have a comment |
| [mouse_aim](feature/mouse_aim/overview.md) | merged | 49c70ab (#183) | The mouse moves the ranged aim reticle; highlight release |
| [mouse_camera](feature/mouse_camera/overview.md) | merged | 80e8361 (#177) | Direct mouse camera: a fixed angle per count, no stick top speed or coast |
| [player_complaints](feature/player_complaints/overview.md) | no commit yet (research only) | — | What players complained about in 2008–2009, and what the recomp already covers |
| [qol_candidates](feature/qol_candidates/overview.md) | no commit yet (research only) | — | Reasonable (R1–R19) and out-of-scope (U1–U10) fixes and features, with what each builds on; achievements and website items already work |
| [recomp_landscape](feature/recomp_landscape/overview.md) | no commit yet (research only) | — | Other Xbox 360 recomps and what Fable 2 Recomp can borrow from them |
| [test_suite](feature/test_suite/overview.md) | merged | 8957735 (#166), be11165 (#174), 5cc6513 (#229) | Known bugs (strict xfails), coverage gaps and quirks of the test suite |

## Reference

| Doc | What it is |
| --- | --- |
| [hotfunc_overrides.md](reference/hotfunc_overrides.md) | How strong-symbol overrides of recompiled functions work, and the rules for hand-tuned ones |
| [gui_lua_bridge.md](reference/gui_lua_bridge.md) | The bridge into the GameFace GUI Lua state, with addresses |
| [lua_call_shapes.md](reference/lua_call_shapes.md) | Generated by `tools/lua_callshapes.py`: how many arguments the game's scripts pass to each engine function |
| [icon_button_a_notes.md](reference/icon_button_a_notes.md) | Research on where the A-button icon is loaded and drawn (the related branch isn't merged) |
| [research/](reference/research/README.md) | Index of the November 2026 research: summary, what to investigate next, and links to its fix and feature pairs |
| [analysis/](reference/analysis/) | Raw address lists from a function-seed scan |
| [screenshots/](reference/screenshots/) | Images used by other docs (the launcher README) |
