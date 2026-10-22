# Custom D3D12 Renderer (`rexgpu-custom`)

**STATUS: M0 COMPLETE (2026-10-22)** — `rexgpu-custom.dll` boots the game to the main menu with `--gpu_plugin=custom` (release build, ~70s to menu, stable, no crash). Progress visible via window title, the plugin's click-through overlay window (top-left), and `logs/renderer-custom.log` (2s cadence). M1 (first real pixels) is next.

Goal: replace the built-in D3D12 Xenos renderer (the SDK's prebuilt
`rexgpu-xenos.dll` plugin) with a new, hand-optimized D3D12 renderer built
**inside this project** (`src/renderer/`, staged as `rexgpu-custom.dll`). It
must boot the game as early as possible, show visible progress while features
land, and finally out-perform the built-in renderer (lower CPU cost, no
startup shader-compile hitches, same or better frame time) with identical
visuals.

## Build & test protocol (binding)

- **All renderer tests happen in the Release build, in
  `out/build/win-amd64-release/`.**
- Build: `build.cmd -release fable_2` (or `build_release_wrapper.cmd`). This
  configures the `win-amd64-release` preset (CMakePresets.json) into
  `out/build/win-amd64-release/` and stages everything next to `fable_2.exe`.
- The custom plugin is staged next to the exe as **`rexgpu-custom.dll`**
  (Release) / `rexgpu-customd.dll` (Debug) by a POST_BUILD step, mirroring how
  `rexglue_setup_target(fable_2 GPU_PLUGINS xenos)` stages `rexgpu-xenos.dll`.
- Select it at launch, no rebuild needed:
  `out\build\win-amd64-release\fable_2.exe --gpu_plugin=custom`
  (and a `gpu_plugin` key in `fable2_config.toml`, see M0.5).
- The stock built-in renderer stays intact (`--gpu_plugin=xenos`) for A/B
  comparison at all times.
- Smoke checklist after every milestone (release folder):
  1. `build.cmd -release fable_2` → exit 0.
  2. Run `fable_2.exe --gpu_plugin=custom` in the release folder.
  3. Check `logs/` (SDK log + our `renderer-custom.log`) for the startup
     banner + per-milestone acceptance log lines.
  4. Play until the acceptance gate below (logo → splash → main menu → first
     gameplay scene). Screenshot command (F-key overlay) is the visual check.

## Why the built-in renderer is "bad" (what we must beat)

The built-in D3D12 backend lives in `thirdparty/rexglue-sdk/src/graphics/d3d12/`
(~20k lines) and is generic to *all* Xenia-style titles, tuned for correctness
not speed:

1. **Generic per-packet dispatch.** Every PM4 packet from the guest ring
   buffer goes through a large switch + virtual calls
   (`d3d12/command_processor.cpp`, 5k lines), with guest-memory access going
   through the memory layer per packet.
2. **Shader path is heavy.** Xenos ALU → DXBC (`dxbc_translator_*`, ~5 files)
   → DXIL via the full **DXC compiler in-process**
   (`pipeline_cache.cpp`: `DxcCompiler`/`DxcConverter`), plus root-signature
   creation per pipeline. First-frame compile hitches + cache-miss stalls.
3. **Per-draw CPU work.** Constant buffers copied per draw, descriptor
   allocation per bind, barrier insertion scattered per state change, upload
   maps/unmaps per texture chunk.
4. **Presentation path** runs the guest frame through the generic
   "guest output" refresh machinery (format conversions, FXAA option,
   internal-state copies) instead of a direct blit.
5. No Fable-2-specific fast paths: we know the title's texture formats
   (mostly DXT1/DXT3, A8R8G8B8, X8R8G8B8, D24S8), its draw-call shapes
   (many small draws, vertex textures for particles/fur), and its shader
   corpus (fixed, ~finite set — pre-compilable).

Our renderer exploits all five: title-tuned specialization, offline/async
shader compilation, zero per-draw allocations, batched barriers, direct
swapchain blit.

## Architecture

### Plugin integration (SDK plugin ABI, v1)

`rex::system::LoadGpuPlugin(name)` loads `rexgpu-<name>[d].dll` from the exe
directory and requires two C exports (`include/rex/system/gpu_plugin.h`):

```cpp
extern "C" {
  uint32_t rex_gpu_abi_version(void);                    // return 1
  rex::system::IGraphicsSystem* rex_gpu_create(uint32_t abi_version,
      const rex::system::GpuCreateInfo* info);           // our system
}
```

`IGraphicsSystem` (the only interface we must satisfy)
(`include/rex/system/interfaces/graphics.h`):
`SetupPresentation`, `SetupGuestGpu`, `has_presentation`, `Shutdown`,
plus optional `provider()`/`presenter()` (needed for the F3 overlay,
`screenshot` command, remote control), `SetInterruptCallback`,
`InitializeRingBuffer`, `EnableReadPointerWriteBack`,
`InitializeShaderStorage`.

Wiring: `Fable2App::OnPreSetup` (src/core/fable_2_app.h) already routes
`config.gpu_plugin`; we extend it so `gpu_plugin == "custom"` calls
`rex::system::LoadGpuPlugin("custom", "d3d12")` (same pattern as the
xenos-vulkan branch).

### Code layout (new files, all in this project)

The tree is split **common vs. per-backend** from day one so a Vulkan
renderer can be added later (separate `src/renderer/vulkan/` + its own
plugin DLL) without touching this implementation's files. This build is
D3D12-only; every file under `d3d12/` is backend-specific and the plugin
entry point is explicitly labeled d3d12.

```
src/renderer/
  common/                      # backend-agnostic (a future Vulkan renderer reuses this)
    progress_checklist.h/.c    # feature checklist: name, milestone, implemented flag, counters
    progress_report.h/.cpp     # window title + log + (per-backend) HUD sink interface
    cvars.h/.cpp               # shared REXCVAR knobs (custom_renderer_*; per-backend ones prefixed)
  d3d12/                       # THIS implementation (all of it D3D12-specific)
    renderer_main.cpp          # d3d12 plugin entry: rex_gpu_abi_version / rex_gpu_create
    custom_graphics_system.h/.cpp   # inherits rex::graphics::GraphicsSystem
    custom_command_processor.h/.cpp # inherits rex::graphics::CommandProcessor (the hot core)
    device.h/.cpp              # ID3D12Device, command allocators, queues, fences
    descriptor_manager.h/.cpp  # CPU/GPU heaps; bindless SRV/UAV/CBV strategy
    upload_ring.h/.cpp         # per-frame ring upload buffer (constants, vertex data, textures)
    guest_resources.h/.cpp     # guest-addr -> D3D12Resource map (textures, RTs, depth, v/i-bufs)
    pipeline.h/.cpp            # PSX/VSO combo -> pipeline cache, root signatures
    shader_backend.h/.cpp      # DXBC (reused translator) -> DXIL -> cached bytecode
    swapchain.h/.cpp           # DXGI swapchain + present path + overlay HUD
    progress_hud.h/.cpp        # visible progress: on-screen overlay (renders the common checklist)
```

Notes on the split:
- `d3d12/renderer_main.cpp` exports the plugin ABI for the D3D12 plugin
  only. When Vulkan lands it ships as its own plugin entry
  (`src/renderer/vulkan/renderer_main.cpp`) and DLL (e.g.
  `rexgpu-custom-vulkan.dll`, selected with `--gpu_plugin=custom-vulkan`),
  mirroring the SDK's `xenos` / `xenos-vulkan` pair. The alternative — one
  dual-backend plugin choosing via `GpuCreateInfo::backend` like the SDK's
  plugin does — stays available later; nothing here blocks either choice.
- The reused SDK Xenos layer (see below) is also backend-agnostic and is
  compiled per plugin, same as the SDK's own plugin builds it.
- The shared `common/progress_*` code owns the *data* of progress (the
  checklist + counters); each backend supplies its own *display* (the D3D12
  HUD quad pass now, a Vulkan pass later), so progress tracking survives a
  backend split 1:1.

### Reuse vs. rewrite (the key cost-saver)

We compile the SDK's **backend-agnostic Xenos layer into our plugin** (the
SDK's own plugin does exactly this — see
`thirdparty/rexglue-sdk/src/graphics/CMakeLists.txt`; it links only
`rexruntime` and the D3D12 backend files):

- **Reuse (compile from `thirdparty/rexglue-sdk/src/graphics/`):**
  - `graphics_system.cpp` (base `GraphicsSystem`: MMIO thunks, vsync worker,
    register file wiring, presentation setup)
  - `command_processor.cpp` (base `CommandProcessor`: GPU-Commands worker
    thread, ring buffer read/write pointers, PM4 packet disassembly →
    virtual `Issue*`/`WriteRegister` hooks, interrupts, gamma-ramp init)
  - `register_file.cpp`, `registers.cpp`, `packet_disassembler.cpp`,
    `sampler_info.cpp`, `frame_limiter.cpp`, `util/draw*.cpp`, `xenos.cpp`
  - Xenos format + shader-translation core: `format/ucode.cpp`,
    `pipeline/shader/shader.cpp`, `dxbc.cpp`, `dxbc_translator*.cpp`
    (Xenos ALU → DXBC), plus `thirdparty/dxbc/DXBCChecksum.cpp`
    (the DXBC itself is reused — the translation is correct; we only make
    the *hosting* of it faster)
  - `primitive_processor.cpp` (guest PP register semantics)
- **Reuse from `rexruntime.dll` (linked, not compiled):** `rex::ui::d3d12::
  D3D12Provider` (device factory + window), presenter plumbing,
  `rex::thread`, `rex::cvar`, `rex::logging`, `rex::perf`, memory/kernel.
- **Rewrite (ours, the actual renderer):** `custom_command_processor`
  (replaces `d3d12/command_processor.cpp`), caches, descriptor/upload
  strategy, pipeline/shader backend, swapchain + HUD. ~6–8k lines target
  instead of ~20k, because we skip what we don't need (Vulkan paths,
  4D530AA4-specific corner cases, generic FXAA pass, etc.) and specialize
  for Fable 2.

> The base `CommandProcessor` gives us the whole **boot plumbing for free**:
> worker thread, ring-buffer init, MMIO `WriteRegister` dispatch,
> `PM4_*` decoding, interrupt callback, pause/resume, save/restore.
> Our backend only implements the `SetupContext/ShutdownContext`,
> `IssueDraw/IssueCopy/IssueSwap/LoadShader/WriteRegister(backend)` etc.
> virtuals — the same shape as the SDK's `D3D12CommandProcessor`.

### Boot sequence (what "booted" means, step by step)

1. `rex_gpu_create` returns our `CustomGraphicsSystem` (log banner:
   `[custom-renderer] loaded (ABI 1, D3D12)`).
2. `SetupPresentation` → `D3D12Provider::Create()`, presenter, window
   created; progress HUD starts (milestone M0 is visible from here).
3. `SetupGuestGpu` → MMIO register thunks installed in the guest,
   `CustomCommandProcessor` constructed, "GPU Commands" thread started.
4. Guest HALC init: `InitializeRingBuffer(ptr, size_log2)` (2 MB ring for
   Fable 2), `EnableReadPointerWriteBack`, `SetInterruptCallback`
   (`VdSetGraphicsInterruptCallback`).
5. Guest boots: Xbox logo (`4D530AA4`) → Fable splash → main menu. The game
   keeps advancing as long as we honor: `PM4_INTERRUPT`/vblank callbacks,
   `PM4_XE_SWAP` (present), `PM4_WAIT_FOR_IDLE`/coherency, and MMIO reads
   (e.g. `COHER_STATUS_HOST`, bin raster counters) returning sane values.

### Progress visibility (required early, then stays)

The renderer reports progress three ways, all visible before any real
rendering exists:

1. **Window title:** `"Custom D3D12 — M3 45% — 2.1M cmds — 612 shaders — last: PM4_DRAW_INDX"`
   (same mechanism the built-in uses: presenter title text).
2. **On-screen HUD overlay** (toggle with a cvar `custom_renderer_hud`, on
   by default until M5): drawn by the swapchain present pass *on top of the
   guest frame* — pure plugin-side quads/text, no app coupling. Shows:
   - milestone reached + implemented/total PM4 opcodes + implemented/total
     shader features (as tracked by a feature-checklist struct)
   - commands/sec, draws/sec, shader compiles (hit/miss), upload bytes/frame,
     frame time ms
   - "unsupported" counters per opcode (what is still no-op'd)
3. **Dedicated log** (`logs/renderer-custom.log`, spdlog via `rex::logging`
   if available, else own FILE*): per-frame throttled stats + every
   first-time opcode hit + every unsupported opcode (once per opcode).
   This gives the "progress it picks up as its progressing" a permanent
   record.

A `src/renderer/common/progress_checklist.c` feature-checklist table (name, milestone,
implemented flag, counter) is the single source of truth; the HUD/title/log
all render from it.

## CMake wiring (concrete)

In root `CMakeLists.txt` (near the `fable_2` target block). The source list
is written as `common/` + `d3d12/` so a future `rexgpu-custom-vulkan`
target can reuse the `common/` + SDK-Xenos lists verbatim and only add its
own `src/renderer/vulkan/` sources:

```cmake
set(REXGPU_CUSTOM_COMMON_SOURCES
  src/renderer/common/progress_checklist.c
  src/renderer/common/progress_report.cpp
  src/renderer/common/cvars.cpp
)
set(REXGPU_CUSTOM_D3D12_SOURCES
  src/renderer/d3d12/renderer_main.cpp
  src/renderer/d3d12/custom_graphics_system.cpp
  src/renderer/d3d12/custom_command_processor.cpp
  src/renderer/d3d12/device.cpp
  src/renderer/d3d12/descriptor_manager.cpp
  src/renderer/d3d12/upload_ring.cpp
  src/renderer/d3d12/guest_resources.cpp
  src/renderer/d3d12/pipeline.cpp
  src/renderer/d3d12/shader_backend.cpp
  src/renderer/d3d12/swapchain.cpp
  src/renderer/d3d12/progress_hud.cpp
)
  # Reused SDK Xenos layer (same set as the SDK's plugin core):
  ${FABLE2_SDK_SRC}/src/graphics/graphics_system.cpp
  ${FABLE2_SDK_SRC}/src/graphics/command_processor.cpp
  ${FABLE2_SDK_SRC}/src/graphics/register_file.cpp
  ${FABLE2_SDK_SRC}/src/graphics/registers.cpp
  ${FABLE2_SDK_SRC}/src/graphics/packet_disassembler.cpp
  ${FABLE2_SDK_SRC}/src/graphics/sampler_info.cpp
  ${FABLE2_SDK_SRC}/src/graphics/frame_limiter.cpp
  ${FABLE2_SDK_SRC}/src/graphics/primitive_processor.cpp
  ${FABLE2_SDK_SRC}/src/graphics/util/draw.cpp
  ${FABLE2_SDK_SRC}/src/graphics/util/draw_extent_estimator.cpp
  ${FABLE2_SDK_SRC}/src/graphics/xenos.cpp
  ${FABLE2_SDK_SRC}/src/graphics/format/ucode.cpp
  ${FABLE2_SDK_SRC}/src/graphics/pipeline/shader/shader.cpp
  ${FABLE2_SDK_SRC}/src/graphics/pipeline/shader/dxbc.cpp
  ${FABLE2_SDK_SRC}/src/graphics/pipeline/shader/dxbc_translator.cpp
  ${FABLE2_SDK_SRC}/src/graphics/pipeline/shader/dxbc_translator_alu.cpp
  ${FABLE2_SDK_SRC}/src/graphics/pipeline/shader/dxbc_translator_fetch.cpp
  ${FABLE2_SDK_SRC}/src/graphics/pipeline/shader/dxbc_translator_memexport.cpp
  ${FABLE2_SDK_SRC}/src/graphics/pipeline/shader/dxbc_translator_om.cpp
  ${FABLE2_SDK_SRC}/thirdparty/dxbc/DXBCChecksum.cpp
)
set(REXGPU_CUSTOM_SOURCES ${REXGPU_CUSTOM_COMMON_SOURCES}
    ${REXGPU_CUSTOM_D3D12_SOURCES})
add_library(rexgpu-custom SHARED ${REXGPU_CUSTOM_SOURCES})
set_target_properties(rexgpu-custom PROPERTIES CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
target_include_directories(rexgpu-custom PRIVATE
    ${FABLE2_SDK_SRC}/include ${FABLE2_SDK_SRC}
    src/renderer ${CMAKE_CURRENT_SOURCE_DIR}/src/renderer/d3d12)
target_compile_definitions(rexgpu-custom PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(rexgpu-custom PRIVATE rexruntime d3d12 dxgi dxguid
    d3dcompiler_47 xxHash::xxhash)
# Debug builds import rexruntimed.dll -> use the d-postfixed name:
set_target_properties(rexgpu-custom PROPERTIES
    DEBUG_POSTFIX "d")  # or match rexglue_setup_target's naming scheme
add_custom_command(TARGET fable_2 POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:rexgpu-custom> $<TARGET_FILE_DIR:fable_2>/rexgpu-custom.dll
  VERBATIM)
```

Notes:
- `FABLE2_SDK_SRC` is already resolved in CMakeLists.txt (used by the
  Vulkan-plugin staging stage 2, ~line 358); reuse it.
- If any of the reused SDK sources are not compiled in the SDK's own plugin
  (i.e. live in `rexcore` inside rexruntime), drop them from the list to
  avoid duplicate global state — the SDK's plugin CMake list above is the
  authoritative "what's safe to compile here" list.
- App-side: extend `Fable2App::OnPreSetup`'s plugin switch with the
  `"custom"` case; add `gpu_plugin` (values: `builtin|xenos-vulkan|custom`)
  to `fable2_config.toml` (`fable2::config::Values`), read it in
  `OnPreSetup` directly from the toml (before `OnPostInitLogging` runs).
- Add `tools/run_custom_renderer.cmd` (staged next to exe): runs the
  release exe with `--gpu_plugin=custom` and `REX_VSYNC` pass-through,
  mirroring `fable2-uncapped.cmd`, so perf tests are reproducible.

## Milestones

Each milestone is independently shippable: the game still boots to the main
menu after it. Work top-down; do not skip the acceptance gate.

### M0 — Plugin boots the game with a progress HUD (black screen is fine)

Smallest possible real plugin. **Acceptance: game boots Xbox logo → splash →
main menu with `--gpu_plugin=custom`, screen black, HUD + window title show
command counters climbing, no crash within 5 min of menu idling.**

- [x] 0.1 Create `src/renderer/d3d12/renderer_main.cpp`: export
      `rex_gpu_abi_version()` (returns 1) and `rex_gpu_create()` returning a
      minimally valid `CustomGraphicsSystem`. Add the CMake target + staging
      (above). Verify `LoadGpuPlugin` succeeds: run with
      `--gpu_plugin=custom`, confirm the loader log has no error and our
      banner is in `logs/`. — **DONE**: banner in `logs/fable_2_045.log`,
      ABI 1, plus extra `rex_custom_renderer_snapshot` export.
- [x] 0.2 `CustomGraphicsSystem` (thin subclass of
      `rex::graphics::GraphicsSystem`): `CreateProvider` →
      `rex::ui::d3d12::D3D12Provider::Create()`; `CreateCommandProcessor` →
      `CustomCommandProcessor`; `name()` → `"Custom D3D12"`.
- [x] 0.3 `CustomCommandProcessor` (subclass of base `CommandProcessor`)
      implementing the boot-critical virtuals only:
      - `SetupContext` → create D3D12 device (reuse provider's), queues,
        fence ring; return true.
      - `IssueSwap(frontbuffer_ptr, w, h)` → **no-op present**: flip the
        swapchain to a black/clear resource so the presenter has valid
        frames (window must not go black-crash or hang).
      - `IssueDraw`/`IssueCopy`/`LoadShader`/`WriteRegister(backend)` →
        record counters in the progress checklist, do nothing else.
      - `InitializeShaderStorage` → create `cache/shaders-custom/`, no-op.
      - Keep base-class interrupt/vblank behavior (do NOT override).
- [x] 0.4 Verify boot to main menu. Fix whatever the guest waits on until it
      boots (expected suspects: `COHER_STATUS_HOST` reads, vblank pacing,
      `PM4_WAIT_FOR_IDLE` completion — all handled by base class; if stuck,
      diff register-write handling against the SDK d3d12 backend). —
      **DONE**: no guest-side fixes needed; base class handled everything.
      Booted Xbox logo → main menu in ~70s; stable idle at menu; counters
      ~9B register writes / 35K swaps / 30M shaders / 74M draws.
- [x] 0.5 Progress visibility: progress checklist struct + window-title
      update (2 Hz) + `logs/renderer-custom.log` with throttled per-frame
      stats and first-hit/unsupported lines. `custom_renderer_hud` cvar.
      — **DONE**: first-hit lines for PA_SC_WINDOW_SCISSOR_TL,
      VGT_DRAW_INITIATOR, RB_COPY_DEST_BASE, shader loads, draws all logged.
      NOTE: the on-screen HUD is a plugin-owned Win32 click-through layered
      window (top-left), not an app-side ImGui dialog — rexruntime's export
      surface (211 symbols) does not expose `ImGuiDialog` or the raw ImGui
      API, so app-side dialog subclasses cannot link. Once the plugin owns
      the present pass (M1), the HUD can move to a GPU-drawn pass.
- [x] 0.6 Config wiring: `gpu_plugin` key in `fable2_config.toml`,
      `OnPreSetup` branch, `tools/run_custom_renderer.cmd` staged.

### M1 — First real pixels: present the guest front buffer

**Acceptance: Xbox boot logo and Fable splash are visible (pixel-correct
enough to recognize) via `PM4_XE_SWAP` presenting the guest front buffer.**

- [ ] 1.1 `guest_resources`: map guest texture pointers → D3D12Resources
      (format/width/height from the game's texture table registers /
      `PM4_SET_CONSTANT` context; Fable 2 front buffer is
      1920×1080×A8B8G8R8-XR — confirm from log of first `PM4_XE_SWAP`).
- [ ] 1.2 Upload path: on first touch of a guest buffer, copy guest RAM →
      upload ring → `CopyResource` into the texture (full-buffer initial
      upload; front buffer re-uploaded each frame is acceptable here).
- [ ] 1.3 `IssueSwap` real version: resolve/copy guest front buffer into
      the presenter's guest-output surface (reuse `D3D12Presenter`'s refresh
      context exactly as the built-in does, `command_processor.cpp` ~L1991),
      present.
- [ ] 1.4 Dirty tracking: track guest writes to the front buffer region
      (invalidate on MEM writes / `PM4_MEM_WRITE` to that range) so we only
      re-upload what changed.
- [ ] 1.5 HUD: swap counts, upload bytes/frame.

### M2 — Texture cache (game UI becomes readable)

**Acceptance: main-menu background + text/UI textures correct; no black
UI; DXT1/DXT3/A8R8G8B8/X8R8G8B8 all verified with screenshots.**

- [ ] 2.1 Texture cache keyed by (guest addr, format, w, h, mip count),
      from guest texture registers (XenOS texture setup: format, pitch,
      mipmap pointers) — mirror the fields the built-in
      `texture_cache.cpp` reads, but store them in a flat struct +
      open-addressing hash (no `std::unordered_map` in the hot path).
- [ ] 2.2 Formats: DXT1, DXT3/2, DXT5/4 (block-copied straight into the
      GPU resource; no decompress), A8B8G8R8, A8R8G8B8, X8R8G8B8,
      A16B16G16R16F (if the game uses it), A8 (check first-hit log),
      Y16X16 (bilinear heightmaps if used).
- [ ] 2.3 Upload on create + incremental updates: chunked row-range
      re-upload via the upload ring when the guest writes texture memory
      (write-tracking hook from the memory layer, same invalidation source
      as 1.4).
- [ ] 2.4 Mipmaps: Fable 2 streams mip data (IM_LOAD / SET_CONSTANT2
      writes into mip pointers) — upload per mips level as written; no GPU
      gen-mips (Xenos has none; the game uploads them).
- [ ] 2.5 Texture destroy/evict: `PM4_INVALIDATE_STATE` + ring eviction
      policy (LRU over guest addresses, capacity ~256 MB; log evictions).
- [ ] 2.6 HUD: texture cache hit/miss/evict/bytes.

### M3 — Render targets, clears, bins (scene structure appears)

**Acceptance: 3D scene geometry regions render to the right places (colors
wrong is OK); clears (viewport/depth) correct; no flicker from stale RTs.**

- [ ] 3.1 Render-target cache: guest RT pointers → D3D12 textures
      (color A8B8G8R8/X8R8G8B8, depth D24S8 → `D32_FLOAT_S8X24_UINT`),
      sized from the frame-buffer registers; capacity + LRU.
- [ ] 3.2 Raster-bin mapping: Fable 2 uses multiple rasterization bins
      (viewport/RT/depth triplets from bin registers) — map
      (viewport + bin) → (RT, DS, viewport, scissor) and switch cheaply.
- [ ] 3.3 `PM4_EVENT_WRITE` / CLEAR commands → D3D12 `ClearRenderTargetView`
      / `ClearDepthStencilView` (match the built-in's clear semantics,
      incl. depth/stencil-only and sub-region clears).
- [ ] 3.4 Blit/copy: `PM4_CONTEXT_UPDATE` / copy engine ops →
      `CopyTextureRegion`/`CopyResource` incl. resolve (MSAA is not used by
      Xenos — no MSAA paths needed at all; this is a real simplification).
- [ ] 3.5 `IssueSwap` upgraded: present the *resolved* back buffer (the
      built-in's `back_buffer_texture` + pending-swap logic,
      `CommandProcessor::SwapState`), not the raw front buffer.
- [ ] 3.6 HUD: RT/bin counts, clear/copy op counts.

### M4 — Shaders + first real draws (game looks like Fable 2)

**Acceptance: main-menu 3D background and character renders with correct
shading on ≥1 scene; first shader compile < 200 ms; HUD shows shader
hit/miss.**

- [ ] 4.1 Shader load: `LoadShader` (Xenos VS/PS guest code) → reuse
      `DxbcTranslator` → DXBC blob (this part is shared, correct, fast
      enough). Cache by DXBC checksum (reuse `DXBCChecksum.cpp`).
- [ ] 4.2 DXIL: DXBC → DXIL via `d3d12/ShaderBlob`/DXC on first miss;
      store compiled `ID3DBlob` in `cache/shaders-custom/<title_id>/` keyed
      by DXBC hash (persistent; warm starts compile nothing).
- [ ] 4.3 `ID3D12PipelineState` cache: key = (VS hash, PS hash, bin state
      struct: blend/depth/stencil/sampler-set/viewport/vertex-format).
      Struct-packed 64-bit hash, open-addressing table.
- [ ] 4.4 Root signatures: **one** small root signature per (VS,PS) pair:
      CBV slot (per-draw constants, from upload ring), SRV bindless range
      (texture descriptors), DSV/RTV bound via descriptor tables or
      pipeline-bound where possible — avoid per-draw heap switches.
- [ ] 4.5 Per-draw constants: Xenos ALU constants (256×4×float VS, 256×4×
      float PS) + texture constants → staged into the upload ring
      (one `Map` per frame, not per draw); vertex/index buffers bound from
      guest memory via the upload ring (streaming vertex data) or mapped
      GPU resources when the game keeps them resident.
- [ ] 4.6 `IssueDraw`: set pipeline (cache hit), bind constants (CBV from
      ring), bind SRVs (bindless indices), `IASetVertexBuffers/
      IndexBuffer`, `DrawIndexedInstanced` with the guest's primitive type,
      index count, base vertex. Track in HUD.
- [ ] 4.7 State from registers → PSX: blend, depth-func, stencil ops,
      cull, fill, viewport, scissor — decoded from bin/raster registers
      (reuse `sampler_info.cpp` + the built-in's decode tables).
- [ ] 4.8 Samplers: 16 sampler slots; sampler descriptor heap; Fable 2's
      sampler set is small and stable — cache sampler-state → heap index.
- [ ] 4.9 Shader compile perf: async compile thread (compile on a worker,
      draw with a fallback/no-op until ready), DXC options set for speed
      (`OptimizationLevel=Default`, no debug), log every >50 ms compile.
- [ ] 4.10 HUD: shader hit/miss/compile-time, per-frame draw count.

### M5 — Full opcode coverage (gameplay-able, parity)

**Acceptance: full playthrough of the first act (Albion) with correct
visuals; unsupported-opcode counter = 0 over the session; FPS overlay shows
guest 30 fps pacing preserved.**

- [ ] 5.1 Implement every remaining PM4 opcode from the base-class switch
      (`command_processor.cpp` ~L873–990): `ME_INIT, NOP, INTERRUPT,
      INDIRECT_BUFFER(_PFD), WAIT_REG_MEM, REG_RMW, REG_TO_MEM, MEM_WRITE,
      COND_WRITE, EVENT_WRITE(_SHD/_EXT/_ZPD), DRAW_INDX(_2), SET_CONSTANT
      (2), LOAD_ALU_CONSTANT, SET_SHADER_CONSTANTS, IM_LOAD(_IMMEDIATE),
      INVALIDATE_STATE, VIZ_QUERY, SET_BIN_MASK(_LO/HI), SET_BIN_SELECT
      (_LO/HI), CONTEXT_UPDATE, WAIT_FOR_IDLE` — each with a checklist
      entry; the built-in backend is the reference for semantics.
- [ ] 5.2 Vertex fetch variants: all Xenos vertex formats Fable 2 emits
      (confirmed by first-hit log — expect V2/V3/V4 × F32/F16, NRM,
      TEXCOORD sets, packed); generic fetch via a small vertex-fetch setup
      (Xenos fetch is register-driven: replicate the built-in's fetch
      layout build).
- [ ] 5.3 Texture ops: 2D/3D (if any), texture rectangles, `PM4` texture
      writes, CPU-generated texture upload (the game's DXT-compression
      path writes DXT blocks from CPU — must land in the GPU texture
      without a readback).
- [ ] 5.4 `VIZ_QUERY` / raster counters: return correct bin raster
      statistics (game may branch on them; return sane 0s if unused —
      confirm by log).
- [ ] 5.5 Interrupt/callback fidelity: `PM4_INTERRUPT` →
      `DispatchInterruptCallback` with correct source, exactly once, and
      vblank pacing via the base-class frame limiter (guest 30/60 fps
      timing must match the built-in's).
- [ ] 5.6 `MakeCoherent/PrepareForWait/ReturnFromWait` semantics for
      `VdWaitForGpuInterrupt`-style guest waits (no deadlocks, no busy
      spin).
- [ ] 5.7 Pause/resume + device-loss: `ClearCaches`,
      `InvalidateGpuMemory`, `OnHostGpuLossFromAnyThread` paths.
- [ ] 5.8 Port the two Fable-2 host fixes: `readback_resolve_force_
      addresses` support (hero/dog black textures — guest 0x12704000
      resolve readback; see plans/hero-dog-texture-readback.md) and
      `depth_bias_shader` (dog fur Z-fighting; see
      docs/RUNTIME_FIXES.md).
- [ ] 5.9 `screenshot` command + F3 overlay Guest-FPS (`kFrameTimeUs` perf
      counter) working with our presenter.
- [ ] 5.10 A/B visual pass: side-by-side screenshots vs `xenos` plugin for:
      main menu, character select, first indoor scene, outdoor daylight,
      night scene, hero close-up (face morphing), dog close-up.

### M6 — CPU fast path (kill per-frame overhead)

**Acceptance: renderer CPU cost (GPU-Commands thread, measured) ≤ 50 % of
the built-in's, at identical in-game FPS.**

- [ ] 6.1 Zero-allocation frame path: per-frame arena for draw records;
      all tables (textures, RTs, pipelines, shaders) open-addressing with
      64-bit mixed keys; no `std::string`/`shared_ptr`/mutex in the hot
      path (single owner thread + lock-free mailbox from app threads, like
      the base class `CallInThread`).
- [ ] 6.2 Direct guest-memory access: use the host-pointer mapping
      (see `generated/default/fable_2_pch.h` `REX_PHYS_HOST_OFFSET`
      pattern) so ring-buffer packet reads are plain pointer reads, not
      per-packet `Memory::Read` calls; keep a fallback path for split
      pages.
- [ ] 6.3 One `Map`/`Unmap` per frame for the whole upload ring
      (constants + vertex streaming + texture chunks); double-buffered
      64 MB ring with fence-tracked reuse.
- [ ] 6.4 Batched barriers: track per-resource state; emit barriers in
      ≤1 batch per frame-section; exploit no-MSAA + few state classes to
      precompute per-pipeline expected states (skip barriers when
      unchanged, like the built-in's `PushTransitionBarrier` but with a
      cheaper table).
- [ ] 6.5 Descriptor strategy locked in: bindless SRV heap (all textures
      resident, index-stable) + sampler heap + per-draw CBV from ring;
      ≤1 descriptor-heap bind per pipeline switch per frame.
- [ ] 6.6 Opcode dispatch: replace the generic per-packet virtual dispatch
      with a dense function-pointer table on the base class's decoded PM4
      type (keep the base class for the rare ones); confirm with a
      per-opcode time histogram in the HUD.
- [ ] 6.7 Profile: `fable_2_profiler` build (out/build/
      win-amd64-release-profiling) + VS profiler on the GPU-Commands
      thread for menu/indoor/outdoor; iterate until the acceptance gate.

### M7 — GPU fast path (swapchain, batching, no hitches)

**Acceptance: no visible hitch > 250 ms during the first hour (cold start
included, with warm shader cache); tearing-free or clean tearing; GPU
time/frame below the built-in's on the same scene.**

- [ ] 7.1 Swapchain: DXGI flip-model swapchain (16:9, 1920×1080 internal,
      letterboxed/scaled to window), `Present(1)` + optional
      `ALLOW_TEARING` behind `custom_renderer_tearing`; no intermediate
      format conversion (guest A8B8G8R8 → DXGI_FORMAT_R8G8B8A8_UNORM
      straight).
- [ ] 7.2 Present pass = single small command list: (optional overlay HUD
      quad) + `CopyResource` back buffer → swapchain back buffer +
      `Present`. Measure; target < 0.1 ms CPU.
- [ ] 7.3 In-flight frames: 3-frame fence ring; command allocator pool;
      no CPU-GPU sync except vblank pacing.
- [ ] 7.4 Persistent D3D12 pipeline cache (device-level
      `ID3D12Device::CreatePipelineCache` + disk blob) on top of our own
      DXIL file cache.
- [ ] 7.5 Cold-start shader precompile (optional, behind
      `custom_renderer_precompile=1`): tool mode that boots headless and
      walks the XEX shader list compiling everything up front (title's
      shader corpus is finite — list it from `fable_2_manifest.toml` /
      first-run log).
- [ ] 7.6 Gamma ramp: implement the DC LUT path (base class already feeds
      us gamma table writes) as a final full-screen shader or
      swapchain-gamma emulation — must match the built-in's look
      (A/B screenshot in a dark scene).
- [ ] 7.7 Frame pacing: match guest vsync (REX_VSYNC semantics), verify
      with the fps_probe tooling (see fable_2_app.h notes).

### M8 — Correctness tail + quality parity

**Acceptance: no visual regressions vs built-in in the M5.10 A/B set
(better allowed); 2-hour stability playthrough without crash/artifacts;
save/load with the renderer active works.**

- [ ] 8.1 Race/stress: rapid window resize, minimize/restore, alt-tab,
      device-loss injection (`ID3D12Device::SetStressMode`), fast-forward
      (REX tick-rate override) — no crashes, correct recovery.
- [ ] 8.2 Memory: guest-side texture/RT eviction stress (long session,
      city travel) — LRU works, no GPU OOM, no leaked D3D12 resources
      (track `GetResourceHeapProperties` counts in HUD in debug).
- [ ] 8.3 Edge scenes: water, snow, fire/particles (vertex textures +
      blending), cutscenes, letterboxed cinematic, pause menu (2D UI
      path), achievements popup, loading screens.
- [ ] 8.4 Save/restore: `Save/Restore` byte-stream support
      (GraphicsSystem base API) — at minimum: no corruption; restore
      mid-game keeps state (check against built-in behavior).
- [ ] 8.5 Motion blur / post: the game does its own post-FX in-shader —
      verify the RT/resolve chain for its scratch targets (this is where
      `plans/disable-motion-blur.md` interactions live; keep the config
      switch working against our renderer).
- [ ] 8.6 Turn the HUD off by default (`custom_renderer_hud=false`),
      keep window-title minimal (FPS + "Custom D3D12").

### M9 — Out-perform the built-in (the bar)

**Acceptance (measured in out/build/win-amd64-release, same GPU/drivers,
`fable2-uncapped`-style no-cap config for raw numbers + capped for
pacing):**

| Metric (menu / indoor / outdoor, 60 s samples) | Built-in | Custom (target) |
|---|---|---|
| GPU-Commands thread CPU % | baseline | ≤ 50 % |
| Frame time p50 / p95 (ms) | baseline | ≤ baseline / ≤ baseline |
| Cold start: time-to-first-game-frame | baseline | < 2× (warm cache: 1.0×) |
| Max hitch (ms, 1 h) | baseline | < 250 |
| Sustained guest FPS | baseline | ≥ baseline |

- [ ] 9.1 Benchmark harness: a small CMake/run-script pair that runs both
      `--gpu_plugin=xenos` and `--gpu_plugin=custom` through the same
      scripted route (menu → save-load → 3 landmarks), dumping
      `logs/` + the HUD stats + GPU-Commands thread time (profiler or
      `GetThreadTimes` sampling); store results in `scratch/bench/`.
- [ ] 9.2 Profile the built-in first (VS/PerfView) to identify its real
      top-3 costs on this machine; make sure our M6/M7 items hit those.
- [ ] 9.3 Iterate: per-opcode histogram (M6.6) + RenderDoc capture
      (built-in supports it; add `ID3D12CommandQueue` capture support to
      ours — the d3d12 provider likely exposes the device) to close any
      remaining gap.
- [ ] 9.4 Default-flip (optional final step): make `custom` the default
      `gpu_plugin` in `fable2_config.toml` once the acceptance table
      passes on the reference hardware; keep `xenos` as the fallback
      (one-key switch if anything regresses).

## Design decisions (locked)

- **Plugin, not SDK patch.** Everything lives in this project
  (`src/renderer/`); the SDK submodule stays pristine so SDK updates don't
  fight us. Only reuse-compiled SDK sources (no modification).
- **Backend-split tree from day one.** `src/renderer/common/` is
  backend-agnostic; `src/renderer/d3d12/` (entry point included) holds this
  D3D12-only implementation. A future Vulkan renderer is a new
  `src/renderer/vulkan/` directory + plugin DLL reusing `common/` and the
  same SDK Xenos layer — no restructuring of this work.
- **D3D12 only.** No Vulkan/multi-backend abstraction in our plugin.
- **No MSAA.** Xenos never emits multisampled RTs; delete the whole code
  path the built-in keeps for safety.
- **Reuse the DXBC translator + base CommandProcessor.** The expensive
  correctness work (Xenos packet semantics, ALU → DXBC) is already
  battle-tested; we only rewrite the D3D12 hosting + caches + present path.
- **Single owner thread model.** GPU-Commands thread owns all D3D12 state;
  app/UI thread talks via the base class's mailbox + presenter (matches
  the SDK's threading contract, avoids locks in the hot path).
- **Progress-first development.** HUD/title/log exist at M0 and the
  feature checklist is updated with every opcode/feature — so "what is the
  renderer doing / what's left" is always visible.

## Risks & mitigations

- **Boot stuck in M0.4** (guest waits on something we no-op): mitigation —
  diff every MMIO write against the built-in's handling (log both); the
  base class handles the hard part (ring/vblank), the rest is register
  reads returning sane defaults.
- **Duplicate global state** if a "reused" SDK source actually lives in
  rexruntime: mitigation — start from the SDK plugin's own CMake source
  list (authoritative) and let link errors guide trimming.
- **DXC compile hitches despite async**: mitigation — persistent DXIL
  cache + optional offline precompile (M7.5); Fable 2's shader set is
  finite.
- **Visual drift** vs built-in (gamma, bias, clear semantics):
  mitigation — A/B screenshot harness at M5.10 and M8; the built-in is the
  reference oracle.
- **Perf goal unmet on weak GPUs**: mitigation — the acceptance table is
  measured on the reference machine; cvars
  (`custom_renderer_quality`, resolution scale) can trade quality for
  speed if needed.
- **SDK ABI drift** on submodule update: mitigation — `rex_gpu_create`
  checks `kGpuPluginAbiVersion`; pin behavior, log the version on load.

## Definition of done (whole plan)

`fable_2.exe` launched from `out/build/win-amd64-release/` with
`--gpu_plugin=custom` (or the config default after M9.4): boots to the
main menu faster than the built-in, plays the whole game with no visual
regressions, shows ≥ the M9 metrics, and the built-in remains available
one flag away for A/B comparison.
