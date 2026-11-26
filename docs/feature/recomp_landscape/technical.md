# Xbox 360 recomp landscape: technical notes

**Status:** no commit yet (research only) · **Commits:** none · **Last checked against:** 7863041

Claims are labelled **Verified** (checked in this repo or a primary source), **Reported** (a source says so, not checked here) or **Inference** (reasoned, not tested). This came out of the [November 2026 research](../../reference/research/README.md).

## Goal

Find out what other Xbox 360 recomps (mid-2025 to November 2026) shipped and how, so Fable 2 Recomp can reuse what worked. The timestep lessons are in [high_tick_rate_side_effects](../../fix/high_tick_rate_side_effects/technical.md), AI tooling in [ai_assisted_naming](../ai_assisted_naming/technical.md) and takedowns in [legal_and_distribution](../legal_and_distribution/technical.md).

## Findings

### Fable 2 Recomp and ReXGlue's origin

**Reported:** in a March 2026 Read Only Memo interview, ReXGlue's creator "Tom" said someone first contacted him about "a community working on the Fable 2 Recomp", whose Discord was owned by Loreaxe (Ryan Fisher). He then "adopt[ed] Xenia's codebase wholesale and replace[d] its JIT backend with AOT".\[1\] The project's fixes therefore matter upstream as well.

### ReXGlue's state

- **Release cadence (Reported).** v0.10.0 was released on 21 Aug 2026. A tracking site listed its last push as 2 Oct 2026 and zero commits in the 30 days to 25 Oct 2026,\[2\] which suggests upstream has slowed and per-game forks carry much of the work.
- **This project uses a fork (Verified).** The build uses `thirdparty/rexglue-sdk` (`himdo/rexglue-sdk`), as `CLAUDE.md` says.\[3\]
- **Architecture (Reported).** The graphics system is "an abstract interface in the SDK… injected at build time", and the Xenos GPU backend is a subsystem the project "fully intend[s] to replace with native rendering over time".\[1\] Relevant to the open ReGPU issue (#187).\[4\]
- **Scale (Reported).** HeldGames' November 2026 list counts 50 Xbox 360 recomps (at least 29 playable, 33 with an Android build) and 83 original-Xbox titles with builds, after sp00nznet's xboxrecomp and an "XBE Recomp" toolkit shared by about twenty ports took off in October–November 2026.\[5\]

### Other recomps and what they shipped (all Reported)

| Project | Base | Features/QoL worth noting for Fable 2 |
|---|---|---|
| Perfect Dark Zero Recomp | ReXGlue | Reused community Xenia patches (60 FPS, 16:9) plus a fan "Reinstated" project's high-FPS physics, cutscene timing fixes, menu pointer and intro skip.\[6\] Shows the value of importing existing community knowledge. |
| Deadly Premonition Recomp (v2.0.14) | static recomp | Native DirectX 12 renderer (up to 4x internal resolution), 60 FPS through a time-base change, mouse look, FSR 3, an ISO installer, and "one present per game frame" so the UI stops repainting on every monitor refresh.\[7\] |
| LARecomp (Midnight Club LA) | static recomp | Real-frame-delta injection behind a debug-menu "FIXES" page of triage switches. Documented a frame-rate-collapse workaround while waiting for an SDK fix.\[8\] |
| Majistic Recomp (Majin) | static recomp | Per-system `anim_speed` normalisation for animations that ran 2x–4x too fast above 30 FPS, and multi-threaded PSO pre-compilation to remove shader stutter.\[9\] |
| GTA V (TU27) recomp | ReXGlue v0.10.0 | New Vulkan renderer that "replays the game's draws natively" on PC, Mac and phones. A detailed "Keeping it clean" policy for forks.\[10\] |
| WetRecomp, Blue Dragon (ReBlue), Nuts & Bolts (reNut), Halo 3 beta, Viva Piñata, Lost Odyssey, Saints Row, Spider-Man 3, NFS Carbon and others | ReXGlue | Shows how broad the base is. Blue Dragon is the creator's pilot title for multi-disc work.\[1\]\[2\]\[11\] |
| "Fable: Return to Albion" (Fable2Recomp org) | ReXGlue + Godot 4.7.1 | Claims a Godot front-end around a recompiled Fable II/III simulation, with local co-op, "RexNet online multiplayer"\[12\] and a "Fully Playable" status.\[13\] Marketing-style claims, not reproduced. Its co-op claims are the only public lead on reviving online play. |

**Inference:** the projects with the strongest reputations ship (a) a per-system list of fixes tied to specific game functions, and (b) clear statements of what is still broken. That is a documentation and mapping practice, not an engine feature.

## What this repo already has

All **Verified** in the repo at 7863041 unless marked.

### Feature comparison

| Other projects have | Fable 2 Recomp has | Gap |
|---|---|---|
| Native DirectX 12 or Vulkan renderer that replaces the emulated GPU (Deadly Premonition, GTA V)\[7\]\[10\] | The SDK's emulated Xenos GPU backend in a D3D12 plugin (`rexgpu-xenos.dll`, the default). A Vulkan plugin (`rexgpu-xenos-vulkan.dll`, `--gpu_plugin=xenos-vulkan`) is built from the SDK source with the project's local patch; on Windows it is "stage 2, in progress" (README, "Vulkan renderer"). | No native renderer. That's the open ReGPU issue (#187).\[4\] |
| Linux builds (GTA V on PC, Mac and phones)\[10\] | A native x86-64 Linux build that renders with Vulkan (`build.sh`, `Linux.md`), tested on Nobara 44 with an RTX 3060 Ti and earlier on Linux Mint with Mesa RADV. No launcher on Linux, and a controller is needed (`Linux.md`). | Linux isn't built by CI. The README's feature checklist still shows "Vulkan support" and "Linux Builds" unchecked. |
| Android builds (33 of the 50 listed 360 recomps)\[5\] | None in this repo. `.gitignore` reserves `thirdparty/android-ndk/`. | **Inference:** Android work happens elsewhere (FableDroid). |
| 60 FPS by changing the time base (Deadly Premonition, Astral Chain)\[7\] | Frame limits of 30/60/120/144/165/240/unlimited for the game and separately for presenting (launcher; `frame_limit`, `host_frame_limit`), the 30 FPS lock fix ([fps_cap](../../fix/fps_cap/technical.md)), frame interpolation (`[patches] interpolation`), and the High/Higher HF tick-rate patches. | Tick-counted systems still run fast at higher tick rates ([high_tick_rate_side_effects](../../fix/high_tick_rate_side_effects/technical.md)). |
| Internal resolution up to 4x, FSR 3 (Deadly Premonition)\[7\] | Internal render scale 1x–4x (`resolution_scale`), anisotropic filtering, FXAA / FXAA extreme, output resolution, window or fullscreen, VSync (launcher graphics page). Hor+ widescreen at the window's aspect ([fit_window](../fit_window/technical.md)). | No FSR or other upscaler. |
| Mouse look and keyboard driving (Deadly Premonition)\[7\] | A full keyboard/mouse layer (`src/input/`): direct mouse camera ([mouse_camera](../mouse_camera/technical.md)), mouse aim ([mouse_aim](../mouse_aim/technical.md)), keyboard-to-pad mapping (`[input] keyboard_gamepad_map`), keycap prompt icons. | Some in-game text still names controller buttons (README checklist). |
| Intro skip (Perfect Dark Zero)\[6\] | `[patches] skip_intro_videos`, a port of just-harry's femtofork patch. Off by default. | — |
| ISO installer (Deadly Premonition)\[7\] | The launcher's **Extract from ISO** checks the image against `constants/game_versions.json`, then extracts `default.xex` and `data/`. It uses the same reader library as `tools/x360extract`. | Windows only (WPF launcher). |
| A "FIXES" page of triage switches (LARecomp)\[8\] | The launcher's **Patches** page has a checkbox for each `[patches]` key: website and CE unlocks, intro skip, both tick-rate patches, GUI every HF tick, interpolation, motion blur, MSAA, realtime texture morphing, hero/dog texture readback. Performance switches live in `config/fable2_config.toml` (for example `terrain_tess_cpu`, `skinning_cpu`, `faster_render_targets`, `gpu_frame_sync`) but aren't on that page. | No in-game version; changes apply on the next launch. |
| Shader pre-compilation against stutter (Majistic)\[9\] | Not found in this repo. **Inference:** pipeline caching, if any, lives in the SDK. | Not investigated. |
| Developer menu and console | The `f2` developer console (about 200 distinct command words in its reference, including subcommands and the giveall categories) ([console_commands](../console_commands/technical.md)), the F5 Lua script menu, photo mode, and the Debug-only remote-control server (`tools/fable2_control.py`). | — |

### SDK fork state

- The build uses the `himdo/rexglue-sdk` fork as the submodule `thirdparty/rexglue-sdk`. Master pins it at **22ef700** ("Linux/Vulkan: build fix, and Fable II's CPU terrain/skinning memexport on Vulkan (#33)", 17 Nov 2026). The SDK's `CMakeLists.txt` declares **version 0.10.0**.
- `thirdparty/rexglue-sdk-submodule-pins.txt` records the SDK's own submodule commits (FFmpeg, SDL3, imgui, spdlog, tracy, MoltenVK and others) for reproducible builds.
- `thirdparty/rexglue-sdk-local.patch` holds the project's local changes to the SDK source (11 files, mostly the Vulkan command processor and texture cache), applied when `tools/setup_sdk_src.cmd` fetches the source into `thirdparty/rexglue-sdk-src/` for the Vulkan plugin.
- **Inference:** with upstream quiet since October,\[2\] the fork is where this project's renderer fixes land (for example the AMD hang fix, rexglue-sdk#29, see [amd_gpu_hang](../../fix/amd_gpu_hang/technical.md)).

### Community knowledge already imported

| Source | What came in | Where |
|---|---|---|
| Xenia game patches for Fable II (Guy, Ultra) | High Tick Rate, Higher HF Tick Rate, Unlock Website Items, Unlock CE Content, Disable MSAA, all redone as data patches or mid-asm hooks for this build | [guest_patches](../guest_patches/technical.md), `config/fable2_config.toml` |
| just-harry's Unofficial Xenia femtofork\[14\] | The intro-video skip, and the approach and guest address behind the hero/dog texture readback | `[patches] skip_intro_videos`, `hero_dog_texture_readback`; [runtime_fixes](../../fix/runtime_fixes/technical.md) |
| Fable II modding community (Fable2Modding\[16\]) | The `.bnk` layout `tools/bnk.lua` reads, worked out "from the file layout + the modding community's" replace script | `tools/bnk.lua`; `FABLE2_LUA_API.md` section 6 cross-checks a community list of Lua names |
| Archon's Toolbox\[15\] | Nothing yet. Issue #83 asks for support for it.\[4\] | — |

## Design (proposed)

1. Re-time per system (timers, GUI, jobs, animation) instead of only raising LF/HF rates. See [high_tick_rate_side_effects](../../fix/high_tick_rate_side_effects/technical.md).
2. Extend the launcher's **Patches** page into a full "FIXES" triage page (LARecomp's model): add the performance switches, and add a console command to toggle them in game where that's safe, so testers can isolate regressions.
3. Keep importing Fable II community knowledge. The Xenia and femtofork patches are in; Archon's Toolbox (Lua mod framework, CombatRebalance, Death Matters) is not (#83).\[14\]\[15\]\[16\]
4. Close the platform gaps in the table: put a Linux build in CI, and update the README checklist for Vulkan and Linux.
5. Make evidence-graded naming the contribution standard ([function_mapping](../function_mapping/technical.md)). Tool-layer convention enforcement (ghidra-mcp style) could be copied by extending `tools/check_manifest_collisions.py`.
6. Keep the asset boundary strict and don't target unreleased or current IP ([legal_and_distribution](../legal_and_distribution/technical.md)).

## What changed

Nothing in the code. This is research only.

## Verification

- **Verified in this repo:** the SDK pin (`git ls-tree HEAD thirdparty/rexglue-sdk`) and its version, `thirdparty/rexglue-sdk-submodule-pins.txt`, `thirdparty/rexglue-sdk-local.patch`, `config/fable2_config.toml`, the launcher's graphics and Patches pages (`launcher/Fable2.Launcher/MainWindow.xaml` and `.xaml.cs`), `Linux.md`, the README, and `tools/check_manifest_collisions.py`.
- Everything about other projects comes from their READMEs and news coverage, read in November 2026.

## Not tested

- None of the other projects' builds or claims were run or reproduced.
- This project's own features were read in the code, not run, for this doc.
- Many 2026 sources are secondary aggregators (HeldGames and similar).

## Sources

1. [Interview: How ReXGlue is bringing the Xbox 360 into the static recompilation era](https://readonlymemo.com/rexglue-xbox-360-recompilation-interview/)
2. [ReXGlue SDK: Xbox 360 executables source port, download & source · Recomp](https://recompsoftware.com/p/rexglue/)
3. [GitHub - himdo/Fable-2-Recomp · GitHub](https://github.com/himdo/Fable-2-Recomp)
4. [Issues · himdo/Fable-2-Recomp · GitHub](https://github.com/himdo/Fable-2-Recomp/issues)
5. [Recompiled Games List: Every Native PC Port by System (2026)](https://heldgames.com/guides/recompiled-games-list)
6. [GitHub - nikolaygorb/PerfectDarkZeroRecomp: Perfect Dark Zero \[Xbox360\] game static recompilation project based on RexGlue-SDK · GitHub](https://github.com/nikolaygorb/PerfectDarkZeroRecomp)
7. [GitHub - LittleBitUA/DPRecomp: Deadly Premonition (Xbox 360) native PC port by static recompilation - v2.0.14: native DirectX 12 renderer ready for testing (up to 4x internal resolution); 60 FPS, mouse look, keyboard driving, FSR 3, ISO installer, PAL + USA · GitHub](https://github.com/LittleBitUA/DPRecomp)
8. [GitHub - mzzvxm/LARecomp · GitHub](https://github.com/mzzvxm/larecomp)
9. [GitHub - kelvynzucco/majistic-recomp: High-performance static recompilation of the Xbox 360 classic for Windows x64 PC (Direct3D 12, 60 FPS, C++23). · GitHub](https://github.com/kelvynzucco/majistic-recomp)
10. [GitHub - andrewnakas/gtav-recomp: GTA V (Xbox 360, TU27) static recompilation with ReXGlue · GitHub](https://github.com/andrewnakas/gtav-recomp)
11. [GitHub - nikolaygorb/WetRecomp: Wet \[Xbox360\] game static recompilation project based on RexGlue-SDK · GitHub](https://github.com/nikolaygorb/WetRecomp)
12. [GitHub - Fable2Recomp/Fable-Return-to-Albion: A Fable 2 & 3 Remake · GitHub](https://github.com/Fable2Recomp/Fable2Remake)
13. [GitHub - Fable2Recomp/Fable-Return-to-Albion: A Fable 2 & 3 Remake · GitHub](https://github.com/Fable2Recomp/Fable-Return-to-Albion)
14. [GitHub - just-harry/unofficial-xenia-femtofork-for-fable-ii: An unofficial fork of Xenia with game-specific-hacks for Fable II. · GitHub](https://github.com/just-harry/unofficial-xenia-femtofork-for-fable-ii)
15. [GitHub - JustSomeGuy1234/Archons-Toolbox: A Mod Manager for Fable 2 Lua Mods · GitHub](https://github.com/JustSomeGuy1234/Archons-Toolbox)
16. [GitHub - JustSomeGuy1234/Fable2Modding: Scripts and information on modding Fable 2 for the Xbox 360, mostly the game's Lua implementation. · GitHub](https://github.com/JustSomeGuy1234/Fable2Modding)
