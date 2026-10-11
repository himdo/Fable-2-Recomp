# Graph Report - Fable-2-Recomp  (2026-10-11)

## Corpus Check
- 141 files · ~248,594 words
- Verdict: corpus is large enough that graph structure adds value.
- Unclassified: 41 file(s) not represented in the graph (top: .cmd 19, (none) 8, .patch 5)

## Summary
- 2036 nodes · 3925 edges · 121 communities (93 shown, 28 thin omitted)
- Extraction: 98% EXTRACTED · 2% INFERRED · 0% AMBIGUOUS · INFERRED: 81 edges (avg confidence: 0.83)
- Token cost: 0 input · 0 output

## Graph Freshness
- Built from commit: `7dbf1f4a`
- Run `git rev-parse HEAD` and compare to check if the graph is stale.
- Run `graphify update .` after code changes (no API cost).

## Community Hubs (Navigation)
- xex_verify.h
- fable2_ui_input_probe.h
- fable2_state_probe.h
- VkFns
- GamepadDriver
- fps_probe.h
- fable2_modmenu.h
- VulkanApp
- fable2_font_probe.h
- fable2_text_append.h
- fable_2_pch
- Slot
- Value
- ControlServer
- MainWindow
- Window
- Hero dog targeted readback plan
- X360Disc
- KeyboardGamepadDriver
- MainWindow.xaml.cs
- Idx
- ExtractionProgress
- .ExtractClicked
- fable2_deadbeef.h
- chrono
- Fable2ConfigValues
- fable2_func_trace.h
- fable2_ui_render_probe.h
- vulkan_smoke.cpp
- RoutedEventArgs
- fable_2_app.h
- argparse
- F5.lua
- normalize_pe.py
- Fable2App
- fable2_hooks.cpp
- fable2_frame_capture.h
- fable2_glyph_probe.h
- struct
- .Classify
- IsoImage
- Parser
- chest_siblings.txt 8-sibling item-op dispatch script
- .Init
- test_game_versions.py
- fable2_modmenu.h mod menu module
- Interval
- .ReadDirectory
- clean_pe.py
- json
- Values
- X360Disc facade
- StfsContainer
- cstdio
- fable2_stall_dump.h
- fable2_config.cpp
- CallActivePlayerMethod_821A11E8.cpp
- remote_control_server.h
- ProcessGameFrame_82276C30 hand-tuned override
- fable2_f5_lua.h
- ThreadState
- Fable2ConfigFile
- Patch
- State
- fable2_ui_text_dump.h
- gaddr
- Dictionary
- X360Extract.Core.csproj
- DiscExtraction
- RemoteControlServer
- QueryActivePlayerMethod_821EC8A0.cpp
- .CreateRenderPassAndCommands
- Shared version catalogue (game_versions.json)
- gaddr
- gaddr
- fable2_heap_scan.h
- Program
- DispatchFrameCallback_821A6010.cpp
- gaddr
- fable2_list_dump.h
- gaddr
- gen_def.py
- GetWindowPresenter
- RuntimeStagingTests
- Curated export table
- gaddr
- UITextItem_Dispatch
- trim_exports.py
- cstdlib
- modifyMoney.lua
- Type
- clean_pe.py build orchestrator
- HotFuncYieldSlider
- modifyMoralityPurity.lua
- CatalogueTests
- rexglue_vulkan_plugin wrapper project
- DiscProgressPhase
- fable2_hook_disable_motion_blur mid-asm hook
- fable2_menu_probe.h
- fable_2 executable target
- Hooking recompiled functions (weak wrapper and strong override)
- UIFont_RenderGlyph
- fable_2_recomp_patches custom target
- REXGLUE_SDK_ROOT and REXGLUE_SDK_SOURCE
- vulkan-1.dll loader staging
- UIText_RenderCurrent
- UIText_RenderWithFont
- Launcher Advanced Options page
- Self-contained single-file publish
- Remote control server
- build.sh script
- GraphicsOptions.cs
- stage_content.sh

## God Nodes (most connected - your core abstractions)
1. `VkFns` - 83 edges
2. `MainWindow` - 67 edges
3. `VulkanApp` - 55 edges
4. `ControlServer` - 54 edges
5. `Window` - 52 edges
6. `Value` - 33 edges
7. `Slot` - 31 edges
8. `X360Disc` - 27 edges
9. `Idx` - 27 edges
10. `KeyboardGamepadDriver` - 23 edges

## Surprising Connections (you probably didn't know these)
- `Fable 2 ReXGlue Recompiled Project` --semantically_similar_to--> `Windows launcher (WPF settings launcher)`  [INFERRED] [semantically similar]
  README.md → launcher/README.md
- `DiscExtraction` --references--> `GdfxEntry`  [EXTRACTED]
  launcher/Fable2.Launcher/DiscExtraction.cs → tools/x360extract/lib/Gdfx.cs
- `DiscExtraction` --references--> `X360Disc`  [EXTRACTED]
  launcher/Fable2.Launcher/DiscExtraction.cs → tools/x360extract/lib/X360Disc.cs
- `DiscExtraction` --references--> `GdfxFilesystem`  [EXTRACTED]
  launcher/Fable2.Launcher/DiscExtraction.cs → tools/x360extract/lib/Gdfx.cs
- `main()` --calls--> `Check()`  [INFERRED]
  tests/native/test_xex_verify.cpp → src/core/xex_verify.h

## Import Cycles
- None detected.

## Hyperedges (group relationships)
- **Guild chest click flow** — tools_chest_debug_sub_82228c70, tools_chest_debug_guildchest_processitems_822de008, tools_chest_debug_guildchest_processitem_82228830, tools_chest_siblings_sub_8256e4f8, tools_chest_debug_guildchest_getwebsiteitem_8256e368, tools_chest_debug_guildchest_getcecontent_824b3528 [EXTRACTED 1.00]
- **Mod menu selection mirror and activation** — plans_main_menu_mod_item_pressbitdispatch, plans_main_menu_mod_item_mirror_index, plans_main_menu_mod_item_modmenu_module [EXTRACTED 1.00]
- **Remote input injection pipeline** — plans_ai_remote_input_control_remotecontrolserver, plans_ai_remote_input_control_remoteinputstate, plans_ai_remote_input_control_remotegamepaddriver [EXTRACTED 1.00]
- **Hero/dog readback gate path in SDK command processor** — plans_hero_dog_texture_readback_should_force, plans_hero_dog_texture_readback_issuecopy_d3d12, plans_hero_dog_texture_readback_issuecopy_vulkan, plans_hero_dog_texture_readback_draw_indx_2_gate [EXTRACTED 1.00]
- **X360Disc reader stack** — tools_x360extract_lib_readme_x360disc, tools_x360extract_lib_readme_extraction_options, tools_x360extract_lib_readme_gdfxfilesystem, tools_x360extract_lib_readme_isoimage, tools_x360extract_lib_readme_stfscontainer [EXTRACTED 1.00]
- **CMake build-time generated and staged artifacts** — cmakelists_fable2_target, cmakelists_lua_staging, cmakelists_recomp_patches, cmakelists_vulkan_loader [INFERRED 0.75]
- **Guest function hooking and instrumentation probes** — text_rendering_notes_hook_pattern, text_rendering_notes_ui_render_probe, text_rendering_notes_deadbeef_canary, readme_func_trace [INFERRED 0.75]
- **UI text per-frame render chain** — text_rendering_notes_uitext_frame_render, text_rendering_notes_uitext_frame_render_iter, text_rendering_notes_uitext_item_dispatch, text_rendering_notes_uitextprompt_render, text_rendering_notes_uitextprompt_renderelement, text_rendering_notes_sub_82c12f18 [INFERRED 0.85]

## Communities (121 total, 28 thin omitted)

### Community 0 - "xex_verify.h"
Cohesion: 0.05
Nodes (44): Find(), GameVersion, code_group, compatible, hash, id, language, name (+36 more)

### Community 1 - "fable2_ui_input_probe.h"
Cohesion: 0.08
Nodes (52): add_runtime_addr(), all_regs(), anchor_is_proc(), be32(), decode_u16be(), delay_seconds(), dump_bytes(), dump_cap() (+44 more)

### Community 2 - "fable2_state_probe.h"
Cohesion: 0.06
Nodes (47): aread(), arena(), be32(), boot_ms(), CurrentFrontEndValue(), CurrentState(), CurrentStateName(), do_sample() (+39 more)

### Community 3 - "VkFns"
Cohesion: 0.04
Nodes (14): VkFns, destroy_command_pool, destroy_device, destroy_fence, destroy_framebuffer, destroy_image_view, destroy_instance, destroy_render_pass (+6 more)

### Community 4 - "GamepadDriver"
Cohesion: 0.05
Nodes (19): GamepadDriver, enabled_, have_prev_, kDeviceId, packet_number_, prev_, state_, InputSnapshot (+11 more)

### Community 5 - "fps_probe.h"
Cohesion: 0.05
Nodes (41): ApplyAll(), BuildPatches(), Flip, page, size, Memory, PageWriteGuard, access_ (+33 more)

### Community 6 - "fable2_modmenu.h"
Cohesion: 0.09
Nodes (35): Activate(), arm_scanner(), enabled(), find_downloadable(), find_needle(), FrontMenu_PressBitDispatch(), host_addr(), in_main_menu() (+27 more)

### Community 7 - "VulkanApp"
Cohesion: 0.05
Nodes (26): VulkanApp, command_buffers, command_pool, device, extent, fns, format, frame (+18 more)

### Community 8 - "fable2_font_probe.h"
Cohesion: 0.06
Nodes (36): BatchedYield(), SkippedYieldFence(), handler(), in_guest_mem(), is_guest_ctx(), log_line(), ProbeState, counts (+28 more)

### Community 9 - "fable2_text_append.h"
Cohesion: 0.11
Nodes (29): adv(), append_segment(), collect_subitems(), dump_char_cands(), dump_words(), element_is_safe(), find_last_element(), guest_alloc() (+21 more)

### Community 10 - "fable_2_pch"
Cohesion: 0.09
Nodes (30): check_loop_backedge(), compute_frame_timing(), compute_loop_timing(), decrement_global_counter(), frame_gate(), guest_fma(), probe_and_setup(), process_frame_work() (+22 more)

### Community 11 - "Slot"
Cohesion: 0.08
Nodes (25): Slot, kBtnA, kBtnB, kBtnBack, kBtnDown, kBtnL3, kBtnLB, kBtnLeft (+17 more)

### Community 12 - "Value"
Cohesion: 0.19
Nodes (12): IsStickSlot(), JEsc(), Parse(), SlotFromName(), SlotName(), Value, arr, boolean (+4 more)

### Community 13 - "ControlServer"
Cohesion: 0.08
Nodes (20): CloseSock(), Config, host, port, token, ControlServer, cfg_, client_socket_ (+12 more)

### Community 14 - "MainWindow"
Cohesion: 0.11
Nodes (5): KeybindEntry, Button, Key, KeyCaptureField, MainWindow

### Community 15 - "Window"
Cohesion: 0.08
Nodes (40): AdvancedContent, AdvancedFooter, AdvancedStatusText, AnisotropicCombo, AntiAliasingCombo, ConfigStateText, DisplayModeCombo, ExtractionBytesText (+32 more)

### Community 16 - "Hero dog targeted readback plan"
Cohesion: 0.07
Nodes (29): Baked morph mode (flag 0), sub_82A55EC0 morph output format selection, fable2_hook_realtime_texture_morphing at 0x82A7607C, sub_82A76018 morph job creation, sub_82A69728 morph renderer, Realtime texture morphing plan, Realtime morph mode (flag 1), Hero and dog texture address 0x12704000 (+21 more)

### Community 17 - "X360Disc"
Cohesion: 0.12
Nodes (14): GdfxEntry, ExtractionOptions, FileChunk, Files, Progress, UnpackStfs, ExtractionResult, X360Disc (+6 more)

### Community 18 - "KeyboardGamepadDriver"
Cohesion: 0.06
Nodes (24): ClampStick(), FillGuestTarget(), KeyboardGamepadDriver, bindings_, cached_text_, console_open_, cursor_hidden_, kDeviceId (+16 more)

### Community 19 - "MainWindow.xaml.cs"
Cohesion: 0.09
Nodes (7): Fable2Launcher.Constants, Fable2Launcher, X360Extract, Application, App, GameLaunchPlan, GameLaunchPlanner

### Community 20 - "Idx"
Cohesion: 0.07
Nodes (27): Idx, kCount, kSub_82A6EF40, kSub_82A8D2D0, kSub_82C09018, kSub_82C12CF8, kSub_82C12D00, kSub_82C12D90 (+19 more)

### Community 21 - "ExtractionProgress"
Cohesion: 0.12
Nodes (14): ExtractionCancelledException, ExtractionProgress, CompletedBytes, CurrentFile, CurrentFileSize, CurrentFileWritten, Percent, Phase (+6 more)

### Community 24 - "fable2_deadbeef.h"
Cohesion: 0.12
Nodes (35): append(), gread(), gwrite(), host_addr(), host_writable(), in_image(), is_heap_ptr(), is_text() (+27 more)

### Community 25 - "chrono"
Cohesion: 0.13
Nodes (4): name(), start_writer(), Registrar, t

### Community 26 - "Fable2ConfigValues"
Cohesion: 0.15
Nodes (13): Fable2ConfigValues, DisableMotionBlur, HeroDogTextureReadback, HigherHfTickRate, HighTickRate, HotFuncYieldEvery, KeyboardGamepadMap, MouseLook (+5 more)

### Community 27 - "fable2_func_trace.h"
Cohesion: 0.12
Nodes (10): commit_run(), ensure_sweeper(), file(), final_summary_guard, flush(), flush_buffer(), is_sub_name(), sweeper_loop() (+2 more)

### Community 28 - "fable2_ui_render_probe.h"
Cohesion: 0.16
Nodes (18): Fable2FuncTraceFlush(), Fable2FuncTraceSetEnabled(), active(), func_trace_auto(), hook(), init_once(), logf(), now_us() (+10 more)

### Community 29 - "vulkan_smoke.cpp"
Cohesion: 0.10
Nodes (10): Get(), HaveInstanceExtension(), LoadVulkan(), PauseExit(), acquire_next_image, queue_present, queue_submit, reset_fences (+2 more)

### Community 30 - "RoutedEventArgs"
Cohesion: 0.11
Nodes (10): AdvancedOptionsButton, CancelButton, ExtractAnywaysButton, ExtractButton, HotFuncYieldBox, LaunchButton, MouseSensitivityBox, StopButton (+2 more)

### Community 31 - "fable_2_app.h"
Cohesion: 0.08
Nodes (6): DetectEol(), EnsureComplete(), Key(), Apply(), GuestByte(), GuestPtr()

### Community 32 - "argparse"
Cohesion: 0.22
Nodes (8): git(), main(), fail(), git(), libmspack_mode(), main(), patch_state(), main()

### Community 33 - "F5.lua"
Cohesion: 0.13
Nodes (15): Memory::AccessViolationCallback, Guest read of 0x00000000 access violation, F5 guest null access violation plan, poll_mainloop MainRenderLoop hook, run_external pre-call validation of this and head, run_external, CScriptManager::RunScript (0x825ADB40), build_page() (+7 more)

### Community 34 - "normalize_pe.py"
Cohesion: 0.20
Nodes (7): main(), e_lfanew(), main(), normalize(), read_sections(), rva_to_offset(), sha256()

### Community 36 - "fable2_hooks.cpp"
Cohesion: 0.26
Nodes (9): fable2_hook_ce_g1(), fable2_hook_ce_g1b(), fable2_hook_ce_grantavail(), fable2_hook_disable_motion_blur(), fable2_hook_realtime_texture_morphing(), fable2_hook_skip_intro_videos(), fable2_hook_website_g1(), fable2_hook_website_g1b() (+1 more)

### Community 37 - "fable2_frame_capture.h"
Cohesion: 0.17
Nodes (14): Adler32(), CaptureGuestOutputToPng(), PngChunk(), PutBE16(), PutBE32(), Result, avg_luma, bytes (+6 more)

### Community 38 - "fable2_glyph_probe.h"
Cohesion: 0.31
Nodes (15): dump_block(), dump_glyphrun(), dump_item(), dump_str(), dump_words(), host_addr(), load_be(), log_line() (+7 more)

### Community 39 - "struct"
Cohesion: 0.23
Nodes (13): add_version_resource(), _align(), _binary_value(), _build_rsrc(), build_vs_version_info(), e_lfanew(), main(), _pad() (+5 more)

### Community 40 - ".Classify"
Cohesion: 0.31
Nodes (4): GameVersion, GameVersions, GameCompatibility, GameCompatibilityInspector

### Community 41 - "IsoImage"
Cohesion: 0.16
Nodes (11): IsoEntry, IsDirectory, IsFile, IsoEntryKind, Directory, File, Parent, VolumeTag (+3 more)

### Community 42 - "Parser"
Cohesion: 0.35
Nodes (4): Parser, end_, err_, p_

### Community 43 - "chest_siblings.txt 8-sibling item-op dispatch script"
Cohesion: 0.14
Nodes (16): chest_click.txt ready-state breakpoint script, GuildChest_GetCEContent (824B3528), GuildChest_GetWebsiteItem (8256E368), GuildChest_ProcessItem (82228830), GuildChest_ProcessItems (822DE008), chest_debug.txt full WinDbg breakpoint script, Click handler sub_82228C70, GuildChest_GetWebsiteItem (8256E368) sibling entry (+8 more)

### Community 44 - ".Init"
Cohesion: 0.12
Nodes (11): create_device, create_swapchain, create_win32_surface, enumerate_physical_devices, get_device_queue, get_properties, get_queue_family_properties, get_surface_capabilities (+3 more)

### Community 45 - "test_game_versions.py"
Cohesion: 0.11
Nodes (4): ConfigParityTests, toml_keys(), generate(), main()

### Community 46 - "fable2_modmenu.h mod menu module"
Cohesion: 0.13
Nodes (15): Background front-end string scanner thread, UITextContainer_ProcessChildren, Downloadable Content row repurposed as MOD MENU, FrontMenu_InputEvent (0x82BE3F30), Selection mirror index 0..5, fable2_modmenu.h mod menu module, Main menu mod item plan, FrontMenu_PollEventQueue (0x82185C90) (+7 more)

### Community 47 - "Interval"
Cohesion: 0.14
Nodes (11): ClearSlotInSnapshot(), Interval, end, open, slot, start, value, ResolveTimeline() (+3 more)

### Community 49 - "clean_pe.py"
Cohesion: 0.23
Nodes (10): clean_dir(), export_names(), import_names_from(), main(), r2o(), read_sections(), sha256(), trim_export_table() (+2 more)

### Community 50 - "json"
Cohesion: 0.40
Nodes (5): build_state(), conn(), exchange(), main(), run()

### Community 51 - "Values"
Cohesion: 0.13
Nodes (14): Values, config_version, disable_motion_blur, hero_dog_texture_readback, high_tick_rate, higher_hf_tick_rate, hotfunc_yield_every, keyboard_gamepad_map (+6 more)

### Community 52 - "X360Disc facade"
Cohesion: 0.14
Nodes (15): ExtractionOptions, GdfxFilesystem, IsoImage, NuGet packaging of X360Extract library, StfsContainer, X360Disc facade, Fable II launcher (library consumer), GDFX directory tree walk (+7 more)

### Community 54 - "cstdio"
Cohesion: 0.18
Nodes (4): DeadbeefDialog, enabled(), register_overlay(), scale()

### Community 55 - "fable2_stall_dump.h"
Cohesion: 0.23
Nodes (10): dump_threads(), fmt_ip(), loop(), ModuleRange, base, end, name, safe_read() (+2 more)

### Community 56 - "fable2_config.cpp"
Cohesion: 0.16
Nodes (3): Load(), Read(), TypeName()

### Community 57 - "CallActivePlayerMethod_821A11E8.cpp"
Cohesion: 0.42
Nodes (8): gaddr(), gload32(), gload64(), gload8(), gstore32(), gstore64(), gstore8(), vcall()

### Community 58 - "remote_control_server.h"
Cohesion: 0.21
Nodes (3): ClampForSlot(), IsTriggerSlot(), SlotButtonMask()

### Community 59 - "ProcessGameFrame_82276C30 hand-tuned override"
Cohesion: 0.18
Nodes (10): decrement_global_counter CAS helper, compute_loop_timing, Goto elimination into if/else and do-while, guest_fma helper (target fma), tools/make_hotfunc_overrides.py, ProcessGameFrame hand-tune plan, ProcessGameFrame_82276C30 hand-tuned override, Region helpers (probe_and_setup and siblings) (+2 more)

### Community 60 - "fable2_f5_lua.h"
Cohesion: 0.26
Nodes (5): logf(), logline(), LuaBind_RunScript_82806168(), poll_mainloop(), run_external()

### Community 61 - "ThreadState"
Cohesion: 0.17
Nodes (10): ThreadState, buf, counts, dead, last, lastPtr, m, run (+2 more)

### Community 63 - "Patch"
Cohesion: 0.18
Nodes (10): Op, address, value, width, Patch, author, desc, is_enabled (+2 more)

### Community 64 - "State"
Cohesion: 0.15
Nodes (12): State, end_us, func_trace_on, skip, start_us, suppress, t0_us, window_logged_open (+4 more)

### Community 65 - "fable2_ui_text_dump.h"
Cohesion: 0.27
Nodes (10): dump(), env_dbl(), env_long(), now_s(), page_readable(), plausible(), safe_read(), safe_read32() (+2 more)

### Community 66 - "gaddr"
Cohesion: 0.53
Nodes (6): gaddr(), gload32(), gload64(), gstore32(), gstore64(), gstore8()

### Community 68 - "X360Extract.Core.csproj"
Cohesion: 0.17
Nodes (8): net8.0-windows, Microsoft.NET.Sdk, net8.0, Microsoft.NET.Sdk, net8.0, Microsoft.NET.Sdk, net8.0, Microsoft.NET.Sdk

### Community 69 - "DiscExtraction"
Cohesion: 0.17
Nodes (7): DiscExtraction, DefaultXex, Gdfx, IsoPath, Plan, RootEntries, TotalBytes

### Community 70 - "RemoteControlServer"
Cohesion: 0.23
Nodes (9): tools/fable2_control.py client, Game state classifier (game_state command), JSON-lines TCP protocol, KeyboardGamepadDriver, AI Remote Input Control Plan, RemoteControlServer, RemoteGamepadDriver, RemoteInputState (+1 more)

### Community 71 - "QueryActivePlayerMethod_821EC8A0.cpp"
Cohesion: 0.49
Nodes (5): gaddr(), gload32(), gload8(), gstore32(), vcall()

### Community 72 - ".CreateRenderPassAndCommands"
Cohesion: 0.17
Nodes (11): allocate_command_buffers, begin_command_buffer, cmd_begin_render_pass, cmd_end_render_pass, create_command_pool, create_fence, create_framebuffer, create_image_view (+3 more)

### Community 73 - "Shared version catalogue (game_versions.json)"
Cohesion: 0.18
Nodes (10): Application icon (asset provenance), FABLE2_CODEGEN_TOOL, FABLE2_BUILD_PROFILE selection, Shared version catalogue (game_versions.json), Extract from ISO, Windows launcher (WPF settings launcher), Fable 2 ReXGlue Recompiled Project, fable_2_manifest.toml (+2 more)

### Community 74 - "gaddr"
Cohesion: 0.52
Nodes (7): gaddr(), gload32(), gload64(), gload8(), gstore32(), gstore64(), gstore8()

### Community 75 - "gaddr"
Cohesion: 0.53
Nodes (5): gaddr(), gload32(), gload64(), gstore32(), gstore64()

### Community 76 - "fable2_heap_scan.h"
Cohesion: 0.31
Nodes (9): env_int(), maybe_request(), needle_search(), now_s(), ptr_backref_search(), scan_all(), seh_dump_runs(), start_scanner_once() (+1 more)

### Community 77 - "Program"
Cohesion: 0.47
Nodes (3): OptionException, Options, Program

### Community 78 - "DispatchFrameCallback_821A6010.cpp"
Cohesion: 0.44
Nodes (6): gaddr(), gload32(), gload64(), gstore32(), gstore64(), vcall()

### Community 79 - "gaddr"
Cohesion: 0.53
Nodes (5): gaddr(), gload32(), gload64(), gstore32(), gstore64()

### Community 80 - "fable2_list_dump.h"
Cohesion: 0.47
Nodes (7): td_dump_block(), td_follow(), td_now(), td_rd(), textobj_dump(), textobj_hook(), UIText_FrameRender()

### Community 81 - "gaddr"
Cohesion: 0.53
Nodes (5): gaddr(), gload32(), gload64(), gstore32(), gstore64()

### Community 82 - "gen_def.py"
Cohesion: 0.47
Nodes (3): consumer_imports(), dll_exports(), main()

### Community 85 - "Curated export table"
Cohesion: 0.31
Nodes (7): BitDefender Ulise generic heuristic detection, Authenticode code signing, Curated export table, Host exe to rexruntime.dll ABI contract, AV-clean builds plan, VS_VERSION_INFO resource, VirusTotal gate (Phase 4 ops)

### Community 86 - "gaddr"
Cohesion: 0.64
Nodes (5): gaddr(), gload32(), gload8(), gstore32(), gstore8()

### Community 87 - "UITextItem_Dispatch"
Cohesion: 0.22
Nodes (9): DEADBEEF canary (UI text append), Localized Press A to start prompt strings, sub_82C12F18 (draw-object vtable slot 8), Font / UI-text rendering pipeline, UIText_FrameRender, UIText_FrameRenderIter, UITextItem_Dispatch, UITextPrompt_Render (+1 more)

### Community 88 - "trim_exports.py"
Cohesion: 0.39
Nodes (7): e_lfanew(), main(), read_def_names(), read_sections(), rva_to_offset(), trim(), name_at()

### Community 89 - "cstdlib"
Cohesion: 0.21
Nodes (6): Fable2FuncTraceSetFilter(), delay_seconds(), duration_seconds(), enabled(), MainRenderLoop_82B9CD68(), run_window()

### Community 90 - "modifyMoney.lua"
Cohesion: 0.43
Nodes (6): apply_amount(), build_page(), get_hero(), open_money_menu(), run(), settle_box()

### Community 92 - "Type"
Cohesion: 0.29
Nodes (7): Type, Array, Bool, Null, Number, Object, String

### Community 93 - "clean_pe.py build orchestrator"
Cohesion: 0.38
Nodes (5): BitDefender Ulise generic AV flag, clean_pe.py build orchestrator, rexruntime.dll export table trimming (~10140 to ~3919), normalize_pe.py core normalizer, Volatile PE fields pinned by normalizer

### Community 94 - "HotFuncYieldSlider"
Cohesion: 0.40
Nodes (3): HotFuncYieldSlider, MouseSensitivitySlider, Slider

### Community 95 - "modifyMoralityPurity.lua"
Cohesion: 0.47
Nodes (3): open_menu(), run(), settle_box()

### Community 97 - "rexglue_vulkan_plugin wrapper project"
Cohesion: 0.33
Nodes (3): Dual D3D12 and Vulkan rexruntime superset, REXGLUE_SDK_SOURCE variable, rexglue_vulkan_plugin wrapper project

### Community 98 - "DiscProgressPhase"
Cohesion: 0.33
Nodes (6): DiscProgressPhase, EnteringDirectory, ExpandingStfs, FileWritten, ScanningHeader, WritingFile

### Community 99 - "fable2_hook_disable_motion_blur mid-asm hook"
Cohesion: 0.60
Nodes (5): sub_822A47C0 camera per-frame update, [patches] disable_motion_blur toggle, Disable motion blur plan, Renderer blur request (camera +0xD4), fable2_hook_disable_motion_blur mid-asm hook

### Community 100 - "fable2_menu_probe.h"
Cohesion: 0.60
Nodes (3): FrontMenu_EnsureElement(), FrontMenu_InputEvent(), FrontMenu_PollEventQueue()

### Community 102 - "fable_2 executable target"
Cohesion: 0.50
Nodes (4): fable_2 executable target, fable_2_lua_staging custom target, FABLE2_PROFILER_FRIENDLY option, F5 external Lua script

### Community 103 - "Hooking recompiled functions (weak wrapper and strong override)"
Cohesion: 0.50
Nodes (3): Guest function-call tracing (fable2_func_trace.log), PPCContext (emulated register file), fable2_ui_render_probe.h (UI render probe)

### Community 104 - "UIFont_RenderGlyph"
Cohesion: 0.67
Nodes (3): UIFont_EmitGlyphQuad, UIFont_RenderGlyph, UIText_RenderElement (per-glyph draw)

## Ambiguous Edges - Review These
- `Dog 512x512 fmt6 5-mip chain` → `Shimmer hypothesis: dynamic 256 mip0 overlaps 512 mip0`  [AMBIGUOUS]
  plans/hero-dog-shifty-texture-mipmap.md · relation: references
- `Dynamic 256x256 fmt3 sampled texture` → `Shimmer hypothesis: dynamic 256 mip0 overlaps 512 mip0`  [AMBIGUOUS]
  plans/hero-dog-shifty-texture-mipmap.md · relation: references

## Knowledge Gaps
- **421 isolated node(s):** `code_group`, `compatible`, `hash`, `id`, `language` (+416 more)
  These have ≤1 connection - possible missing edges or undocumented components. (Counts symbols only; 724 node(s) total have ≤1 connection when file, concept and rationale nodes are included.)
- **28 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `ExtractionProgress` connect `ExtractionProgress` to `.Classify`, `Dictionary`, `MainWindow.xaml.cs`, `.ExtractClicked`?**
  _High betweenness centrality (0.084) - this node is a cross-community bridge._
- **Are the 2 inferred relationships involving `Window` (e.g. with `.GetDeviceState()` and `.KeyboardGamepadDriver()`) actually correct?**
  _`Window` has 2 INFERRED edges - model-reasoned connections that need verification._
- **What connects `code_group`, `compatible`, `hash` to the rest of the system?**
  _421 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `xex_verify.h` be split into smaller, more focused modules?**
  _Cohesion score 0.05367231638418079 - nodes in this community are weakly interconnected._
- **Why does `Window` connect `Window` to `MainWindow`, `KeyboardGamepadDriver`, `RoutedEventArgs`, `.ExtractClicked`, `.LoadConfiguration`, `HotFuncYieldSlider`?**
  _High betweenness centrality (0.070) - this node is a cross-community bridge._

### Low-confidence Hints
_AMBIGUOUS edges — the extractor was unsure. Verify before acting on these._

- **What is the exact relationship between `Dog 512x512 fmt6 5-mip chain` and `Shimmer hypothesis: dynamic 256 mip0 overlaps 512 mip0`?**
  _Edge tagged AMBIGUOUS (relation: references) - confidence is low._
- **What is the exact relationship between `Dynamic 256x256 fmt3 sampled texture` and `Shimmer hypothesis: dynamic 256 mip0 overlaps 512 mip0`?**
  _Edge tagged AMBIGUOUS (relation: references) - confidence is low._