# Hero / Dog black-texture fix (targeted readback-resolve)

**STATUS: IMPLEMENTED + BUILT (pending in-game verification)**

Implementation is complete and the Release build is green; both the staged
`rexgpu-xenos.dll` (cvar + gated readback + diagnostic log) and `fable_2.exe`
(config toggle + `OnPostSetup` seeding) verified to contain the change. The
only remaining step is in-game verification (needs a display/save).

Goal: fix the rendering bug where the hero and the dog render completely black, by
forcing a CPU readback of their render-to-texture result only when the game regenerates
those textures — mirroring the "unofficial Xenia femtofork for Fable II" without paying the
frame-rate cost of a global `readback_resolve`.

## Credit

The root-cause analysis, the hero/dog texture guest address (`0x12704000`), the
"readback only the affected texture, only while it's being resolved" mechanism, and the
resolution-scaled downscale all come from **just-harry**, author of the *Unofficial Xenia:
Femtofork for Fable II* (`github.com/just-harry/unofficial-xenia-femtofork-for-fable-ii`).
This recomp port reuses that insight (per the femtofork's `xe::f2::player_and_dog_textures_address`
and its D3D12 readback approach); the implementation here adapts it to the ReXGlue SDK.
Thanks, just-harry.

---

## 1. Root cause

The hero/dog face + skin textures are not shipped as static images; the game **renders them
to a render target and then resolves (EDRAM copy) the result into a guest-memory texture**.
That resolved texture is what the character is sampled with.

On a unified-memory console the resolve lands in the same memory the GPU reads, so it "just
works". On a split-memory host the render target lives in host VRAM; the guest-memory copy at
the texture's address only gets filled if the emulator **reads the resolved pixels back to the
CPU**. When readback is disabled (the default), the guest-memory texture stays at its
initial (black) contents, so the character renders black.

The texture is regenerated at runtime by many triggers (wardrobe/makeup change, morality/purity
change, save load, dog-breed potion, unequip/reequip). There is no single CPU call site — the
one common, reliable signal is the **GPU resolve copy whose destination is the hero/dog
texture address**.

The femtofork confirms the guest address: `xe::f2::player_and_dog_textures_address =
0x12704000` (both hero and dog resolve to this base in Fable II GOTY/Platinum).

## 2. What the fork does (the reference)

- At the type-3 draw/copy dispatch it detects the regeneration:
  `prim_type == 0x08 (quad)` && `num_indices == 0x03` &&
  `RB_COPY_DEST_BASE == 0x12704000`.
- It then performs a readback **for that copy only** (skipping depth copies and only
  `8_8_8_8` color). With resolution scaling it downscales the upscaled resolve back to the
  guest texture size before copying to guest memory.
- Net effect: readback happens "for a few moments" — only while the affected texture is being
  resolved — with ~zero steady-state cost. (The fork notes it works on D3D12, not Vulkan.)

## 3. Feasibility in this recompile — YES

Everything needed already exists in the ReXGlue SDK the recompile uses:

- **`readback_resolve` cvar** (`src/graphics/command_processor.cpp`): string
  `none|fast|some|full`, `.lifecycle(kHotReload)`. Consumed per-copy by
  `D3D12CommandProcessor::IssueCopy()` and `VulkanCommandProcessor::IssueCopy()` via
  `GetReadbackResolveMode()`.
- **Full readback machinery** already implemented in **both** backends
  (`IssueCopy_ReadbackResolvePath`), **including the resolution-scaled downscale path**
  (the exact piece the fork adds) — D3D12 in
  `src/graphics/d3d12/command_processor.cpp`, Vulkan in `src/graphics/vulkan/command_processor.cpp`.
- **`RB_COPY_DEST_BASE`** register is available on the command-processor thread
  (`register_file_->values[XE_GPU_REG_RB_COPY_DEST_BASE]`, index `0x2319`) and is already read
  the same way in `src/graphics/util/draw.cpp`. This is the fork's detection signal.
- **Runtime cvar set from the app** works: `rex::cvar::SetFlagByName(...)` (already used by
  `Fable2App` to seed input cvars from `fable2_config.toml`).
- **Default run uses the source-built plugin** (`tools/fable2.cmd` d3d12 → source
  `rexgpu-xenos.dll`), which is compiled from `thirdparty/rexglue-sdk` — so editing the SDK
  source and rebuilding the SDK is picked up by the default launcher. (The `prebuilt` mode and
  the Debug config use the prebuilt 0.10.0 plugin and would NOT pick up the change.)

### What is missing
A way to **enable readback for only the hero/dog resolve** instead of all resolves. The global
`readback_resolve=full` cvar stalls every copy (the fork reports 180–400 FPS → 25–60 FPS), so
we gate the existing readback path on a **destination-address match**.

## 4. Chosen design

Add a small, **backend-agnostic, hot-reloadable** SDK cvar that force-readbacks a resolve when
its destination base is in a caller-supplied list. The Fable-2 address stays in the app config
(keeps the SDK generic); the SDK only knows "read back resolves to these guest addresses".

- **SDK (generic):**
  - New cvar `readback_resolve_force_addresses` (string, comma-separated guest base addresses,
    default `""`). Each entry matches `[addr, addr + 1 MiB)`.
  - New `CommandProcessor::ShouldForceReadbackResolve(uint32_t base) const` helper (parses the
    cvar; cheap no-op when empty).
  - `D3D12CommandProcessor::IssueCopy()` and `VulkanCommandProcessor::IssueCopy()`: when
    `readback_resolve` mode is `kDisabled`, additionally take the readback path if
    `ShouldForceReadbackResolve(register_file_->values[XE_GPU_REG_RB_COPY_DEST_BASE])` (the
    fork's exact detection signal). This reuses the existing readback body verbatim (scaled
    downscale + format handling inherited automatically) — no body refactor needed.
  - The Vulkan readback body has an extra `if (readback_mode == kDisabled) return true;` guard
    (the D3D12 body does not); it is extended with the same `ShouldForceReadbackResolve` check
    so a forced copy is not dropped. In the force case the mode stays `kDisabled`, which yields
    an immediate (accurate) sync readback — exactly what we want.

- **App (Fable-2 specific):**
  - `fable2_config.toml [patches]` new toggle `hero_dog_texture_readback` (default `true`).
  - `Fable2App` seeds the SDK cvar in **`OnPostSetup()`**, NOT `OnPostInitLogging()`: the
    cvar is defined in the GPU plugin (`command_processor.cpp`), which is loaded in
    `OnPreSetup` — after `OnPostInitLogging`. `OnPostSetup` runs after plugin load (in
    `ConstructRuntime`), so the cvar is registered by then. When enabled, seeds
    `"0x12704000"`; when disabled, `""`. A/B-able at runtime via the F3 console cvar or the
    config toggle, no rebuild.

This matches the user's intent ("enable readback_resolve for a few moments, then turn it back
off"): the readback is only active for the hero/dog resolve copies, which occur only during
regeneration.

### Alternative considered (rejected)
A guest-side midasm hook that flips `readback_resolve=full` for an N-frame window. Rejected:
regeneration has many CPU triggers (no single hook point), the GPU copy is async (window timing
is fragile), and `full` mode stalls every copy in the window. The SDK address gate is exact,
cheap, and catches all triggers in one place.

## 5. Implementation (files)

SDK (`thirdparty/rexglue-sdk`):
1. `src/graphics/command_processor.cpp`
   - Define `readback_resolve_force_addresses` cvar (string, hot-reload).
   - Implement `CommandProcessor::ShouldForceReadbackResolve(uint32_t) const`.
2. `include/rex/graphics/command_processor.h`
   - Declare `bool ShouldForceReadbackResolve(uint32_t) const;`
   - `REXCVAR_DECLARE(std::string, readback_resolve_force_addresses);` (in `include/rex/graphics/flags.h`).
3. `src/graphics/d3d12/command_processor.cpp` — `IssueCopy()` force-gate.
4. `src/graphics/vulkan/command_processor.cpp` — `IssueCopy()` force-gate.

App (`src/`):
5. `src/core/fable2_config.h` / `fable2_config.cpp` — add `hero_dog_texture_readback`
   (default `true`); load key + sync embedded TOML template.
6. `config/fable2_config.toml` — add documented key (keep in sync with template).
7. `src/core/fable_2_app.h` — seed the SDK cvar from the toggle in the cvar-seed step.

## 6. Toggling
- **On by default** (`hero_dog_texture_readback = true` is the built-in default). The existing
  build dir's `fable2_config.toml` may predate the new key (the build stages it once so user
  edits survive); a missing key just keeps the `true` default, so the fix is active either way.
- **To A/B off:** add `hero_dog_texture_readback = false` under `[patches]` in
  `fable2_config.toml` and relaunch, **or** live via the F3 console cvar
  `readback_resolve_force_addresses` (set to `""` to disable, `"0x12704000"` to enable).
- **To add a second address** (e.g. if the dog resolves elsewhere): set the cvar to a
  comma list, e.g. `0x12704000,0x12710000`, or extend the value seeded in `Fable2App::OnPostSetup`.

## 7. Verification
1. Build SDK (`tools/build_sdk_vulkan.cmd`) + recompile (`build.cmd -release fable_2`).
2. Load an affected save with the hero/dog black. Trigger a regeneration (change makeup,
   shift morality, or reload). Confirm hero + dog are no longer black.
3. Toggle `[patches] hero_dog_texture_readback = false` → black returns (A/B check).
4. Confirm FPS is unchanged in normal play (readback only happens on the hero/dog resolve).
5. (If the dog still reads black) log `RB_COPY_DEST_BASE` on forced copies to discover any
   second address and add it to the list.

## 8. Risks / open questions
- **Address stability:** `0x12704000` is from the femtofork (GOTY/Platinum). The recompile uses
  the same guest code, so the allocation should be identical, but verify at runtime (step 5).
  If it differs, only the config list value needs changing.
- **Dog address:** the fork uses the single base for both; if the dog resolves to a different
  base, add it to `readback_resolve_force_addresses`.
- **Debug / prebuilt builds** use the prebuilt 0.10.0 plugin and will not show the fix; test
  with the source plugin (default `fable2.cmd` d3d12 / release).
- **Vulkan** is a secondary target (recompile Vulkan is WIP; the fork's fix is D3D12-only). The
  gate is added to both backends for completeness but D3D12 is the one that must work.
