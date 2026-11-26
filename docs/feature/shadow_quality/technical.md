# Shadow quality (fine grain and filter radius): technical notes

**Status:** no commit yet · **Commits:** — · **Last checked against:** c2a9d12

## Goal

At an internal resolution above 1x, the sun shadow's soft edge looks blotchy on the road and on faces. The goal was to find out whether that is an emulation fault or the game's own filter, and to give a sharper, cleaner result as an option without replacing the shader.

## Design

**The shadow pass (Verified, read in the ucode dump).** The sun shadow is a deferred screen-space pass, pixel shader `0F99810F072BF0FC`, drawn into a 1120x720 (guest) target. It reads the scene depth and two directional cascades, both 1024x1024 on the console (`k_24_8_FLOAT`).

- Cascade 0 (near the camera) uses a 12-tap Poisson disc. Every offset is multiplied by `c77.x` (the radius, in shadow-map UV). A 32x32 noise texture rotates the pattern per pixel, and each tap is a binary compare. The result is the average.
- Cascade 1 uses a fixed 2x2 bilinear PCF.
- The noise coordinate is `(|pixel position| + c47.xy) * c47.zw`.

**Why it is blotchy at a higher internal resolution (Verified, read in the SDK translator).** The DXBC and SPIR-V translators give the shader its pixel position in guest pixels: the host position is floored, then multiplied by `1 / resolution_scale`. That is correct emulation. But the noise rotation then changes once per guest pixel, so at scale 3 each rotation covers 3x3 host pixels. The 12-tap pattern makes a dither three times coarser than the output. At scale 1 the picture matches the console.

**The softness itself is the game's (Inference, from the constants).** The radius is a constant in UV, and two 1024x1024 cascades cover the whole view. A larger internal resolution makes the maps 3072x3072 but leaves the filter as wide as before. The ROV render path doesn't change it either (Reported).

**Fix without a shader replacement.** Both changes are constant changes. The SDK cvar `pixel_shader_constant_overrides` (in the GPU plugin, D3D12 and Vulkan) multiplies or replaces chosen constant components of one guest pixel shader while its constant buffer is built. The guest registers and the translated shaders are unchanged, so pipelines, the shader storage and ReShade addons that match translated shaders by their bytecode are unaffected.

- `shadow_fine_grain` adds `c47.z*rsx,c47.w*rsy` (`rsx`/`rsy` = draw resolution scale): one rotation per host pixel.
- `shadow_filter_radius` adds `c77.x*<factor>`.

`Fable2App::OnPostSetup` builds the spec, for example `0F99810F072BF0FC:c47.z*rsx,c47.w*rsy,c77.x*0.75`, and seeds the cvar the same way `dog_fur_depth_bias` seeds `depth_bias_shader`. A value set in `fable_2.toml` or on the command line wins.

## What changed

- `config/fable2_config.toml`, `src/core/fable2_config.cpp` (template and reader), `src/core/fable2_config.h`: `[patches] shadow_fine_grain` (default true) and `shadow_filter_radius` (default 1.0; out-of-range values fall back to 1.0 with a warning).
- `src/core/fable_2_app.h`: seeding in `OnPostSetup`.
- SDK (`himdo/rexglue-sdk`, PR from branch `feature/pixel-shader-constant-overrides`), pinned here by the SDK bump in this PR:
  - `include/rex/graphics/util/shader_constant_overrides.h`: parser and apply;
  - the D3D12 and Vulkan command processors;
  - the cvar in `src/graphics/flags.cpp`;
  - Catch2 tests in `tests/unit/graphics/shader_constant_overrides_test.cpp`.
- No game functions are called, hooked or overridden, so no manifest names.

## Verification

- Python tests (`python -m pytest`): 494 passed, 15 xfailed before and after. The config template, reader and staged config agree.
- `tools/check_manifest_names.py --base origin/master`: OK (no manifest changes). `tools/generate_game_versions.py --check`: OK.
- SDK unit tests for the parser: 55 assertions in 3 test cases pass (built on Linux with clang).
- The SDK's Vulkan command processor compiles on Linux with the change.
- In game on Windows (D3D12) at resolution scale 3, with the SDK change applied to SDK c26d8b65 and the cvar set directly: the fine grain removes the blotchy edge, and 0.75 is sharper and still soft (Reported). Both were toggled live from the console.

## Not tested

- In game on a build without the separate HDR output changes. The build tested in game also carried them, but the shadow change shares no code with them.
- The D3D12 path compiled on this branch (only the earlier SDK c26d8b65 build was compiled and played).
- The Vulkan path in game.
- The `[patches]` keys end to end in a Release build of this branch.
- The other shadow-pass variants in the shader bank (`1917EF71`, `3DC1C94E`, `6AB6A952`, `E028ED26`, `1B4B5AE1`), which weren't seen drawing. If they draw somewhere, check their constants and add them to the spec.

## Follow-ups

- Launcher rows (a checkbox and a number) once the keys are settled.
- An F4 settings menu entry for live tuning.
