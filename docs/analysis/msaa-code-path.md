# Fable 2 on ReXGlue/MoltenVK: the MSAA code path and the ~50 ms/frame

Date: 2026-10-13. Read-only code analysis. Sources: ReXGlue SDK `/Volumes/EXT/fable2/rexglue-sdk`
(branch `macos`, `bbadd11`), MoltenVK submodule `thirdparty/moltenvk` (`v1.4.2-10-g701747d6`),
SPIRV-Cross `spirv_msl.cpp` (upstream main; the checkout has no `External/` copy, MoltenVK fetches it at
build time, the relevant code has been stable since 2020). Measurements quoted are from
`Fable-2-Recomp/memory.md` (2026-10-12): 3D scene 104-112 ms/frame; all surfaces forced to 1x: 56-61 ms;
4x demoted to 2x: no change (~1 ms); Metal System Trace split of a 105 ms frame: fragment ~82 ms,
vertex ~30, pass fixed cost 1.3, restarts 8.8, transfer/compute 7; "the ~50 ms are in the 2x passes".

## TL;DR

The emulator does nothing MSAA-specific in the main scene pass except one thing: every translated pixel
shader that can be alpha-tested / alpha-to-mask / kill / write depth declares the SPIR-V capability
`SampleRateShading` (needed, historically, for the `gl_SampleMask` output it uses to emulate Xenos
alpha-to-mask). SPIRV-Cross's MSL backend treats *any* module declaring `SampleRateShading` as a
sample-rate shader (`CompilerMSL::is_sample_rate()`), injects a `[[sample_id]]` input and offsets
`gl_FragCoord` per sample. A Metal fragment function with a `[[sample_id]]` input executes **once per
sample**. At 1x that is free; at 2x those draws shade twice, at 4x four times. The main scene passes are
2x and the 4x passes are small/depth-only, which is why 4x->2x changed nothing while 2x->1x saved ~50 ms:
the cost is not "binary", it is "x2 on the pass that matters". Fix: stop declaring the capability
(`spirv_translator.cpp:2975-2980`); `BuiltIn SampleMask` only requires the `Shader` capability in SPIR-V.
One-line change, expected gain: most of the ~50 ms. Everything else MSAA-related (ownership transfers,
dumps, resolves) is real per-sample work but lives in the measured 7 ms transfer/compute bucket.

---

## 1. How the Vulkan backend (fbo path) creates and uses MSAA render targets

All paths below: `src/graphics/vulkan/render_target_cache.cpp` (RTC), shared logic in
`src/graphics/pipeline/render_target/cache.cpp` (shared RTC), pipelines in
`src/graphics/vulkan/pipeline_cache.cpp`, cvars in `src/graphics/flags.cpp`.

### 1.1 Path selection and capability probing
- `VulkanRenderTargetCache::Initialize`, RTC:203-213: `render_target_path_vulkan` ("" -> `kHostRenderTargets`,
  i.e. fbo; "fsi" -> `kPixelShaderInterlock`).
- 2x MSAA support, RTC:373-382: with `native_2x_msaa=true` (flags.cpp:18, default) 2x is native when
  the device reports `VK_SAMPLE_COUNT_2_BIT` in framebuffer color/depth/stencil and sampledImage
  color/depth/stencil counts. MoltenVK fills these from `supportsTextureSampleCount:` (MVKDevice.mm:2649-2651),
  which is true for 2 on Apple GPUs, so `msaa_2x_attachments_supported_ = true`. The "2x as samples 0 and
  3 of 4x" fallback (RTC:1877-1879, pipeline_cache.cpp:3290-3299, transfer 4383-4394) is therefore **not**
  taken. (memory.md already confirmed "2x nativo".)
- Float24 depth: `depth_float24_round_` / `depth_float24_convert_in_pixel_shader_` read at RTC:648-649
  from cvars that default to **false** (flags.cpp:19-21). Consequence: `Modification::pixel.depth_stencil_mode`
  is never `kFloat24*` (pipeline_cache.cpp:1003-1010), so `SpirvShaderTranslator::IsSampleRate()` is false
  and `description.sample_rate_shading` (pipeline_cache.cpp:1400-1404) is false for all game pipelines.

### 1.2 Host image creation (`VulkanRenderTarget` creation, RTC:1866-1900)
- `VkImageCreateInfo`: 2D, 1 mip, 1 layer; `extent.width = key.GetWidth() * scale_x`,
  `extent.height = GetRenderTargetHeight(pitch_tiles_at_32bpp, msaa) * scale_y`;
  `samples = 1 << msaa_samples` (RTC:1880); `tiling = OPTIMAL`; `usage = SAMPLED | (DEPTH_STENCIL_ATTACHMENT or
  COLOR_ATTACHMENT)`; `flags |= MUTABLE_FORMAT` when the ownership-transfer format differs
  (`GetColorOwnershipTransferVulkanFormat(format, msaa)`, RTC:1896-1900).
- **No** `VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT`, no lazily allocated memory: the image is a persistent
  EDRAM slice that must survive across render passes and be sampled by transfer/dump/resolve shaders.
- Size is EDRAM-derived, not screen-derived: shared RTC `GetRenderTargetHeight` (cache.cpp:791-810) returns
  `tile_rows * (16 >> (msaa >= 2x))` pixel rows and `RenderTargetKey::GetWidth` (cache.h:275-279) returns
  `pitch_tiles * (80 >> (msaa >= 4x))`. Sample count x pixel count is constant for a given EDRAM extent,
  so **a 2x or 4x host RT holds the same number of bytes as the 1x one** (this is why tile load/store
  bandwidth cannot explain a 1x vs MSAA delta).

### 1.3 Render pass / dynamic rendering (no resolve attachments anywhere)
- Dynamic rendering is the default (`vulkan_dynamic_rendering=true`, command_processor.cpp:87; RTC:4284).
  `VkRenderingAttachmentInfo` setup RTC:1520-1558: depth and every color attachment use
  `loadOp = LOAD`, `storeOp = STORE`, `resolveMode = VK_RESOLVE_MODE_NONE`.
- Legacy `VkRenderPass` fallback RTC:1575-1626: same LOAD/STORE on depth+stencil and color (stencil
  DONT_CARE only on color attachments). `samples` from the key (4x substituted for 2x only in the
  non-native case).
- Nothing in the RTC ever uses `vkCmdResolveImage`, Vulkan resolve attachments, or `VK_EXT_sample_locations`
  (the only `standardSampleLocations` reference, RTC:207, gates the FSI path).

### 1.4 Graphics pipelines (pipeline_cache.cpp:3279-3309)
- `rasterizationSamples = 1 << render_pass_key.msaa_samples` (3302-3303); 1x forced for rasterizer-discard
  pipelines and for passes without attachments (3283-3289).
- `sampleShadingEnable` only when `description.sample_rate_shading` (3305-3308) -> never for this game (1.1).
- `pSampleMask` only in the 2x-as-4x fallback (3296-3299) -> never. `alphaToCoverageEnable` is **not** used;
  Xenos alpha-to-mask is emulated inside the shader (1.7).

### 1.5 EDRAM ownership transfers between host render targets (`PerformTransfersAndResolveClears`, RTC:4549)
- Transfers are fragment draws into the destination RT's own render pass using dedicated transfer
  pipelines (`GetTransferPipelines`, RTC:~4380-4420) and generated SPIR-V (`GetTransferShader`, RTC:2300).
- Multisample destination (`dest_sample_count > 1`, RTC:4387-4400): `sampleShadingEnable = VK_TRUE`,
  `minSampleShading = 1.0` (device has `sampleRateShading`), i.e. one **per-sample** draw; the shader
  declares `gl_SampleID` (RTC:2589-2597) and reads the source as a multisampled sampled image
  (`makeImageType(..., ms = source_is_multisampled, ...)`, RTC:2444-2476, 2516) -> `OpImageFetch` with the
  sample index -> MSL `texture2d_ms<T>::read(coord, sample)`. Without `sampleRateShading` it would do one
  draw per sample with `pSampleMask` (RTC:2598-2600, 4396-4397); not our case.
- Depth transfers write `gl_FragDepth` and, when `VK_EXT_shader_stencil_export` is available, stencil in the
  same draw (RTC:2391, 2422-2423, 4416-4418). MoltenVK enables that extension when
  `_metalFeatures.stencilFeedback` is set, which happens for GPU family Apple5+ (MVKDevice.mm:2567-2571,
  3536-3537) -> available on the M3, so the 8-draws-per-transfer stencil-bit fallback
  (`need_stencil_bit_draws`, RTC:4845-4855, 5260-5270) is **not** taken.
- Formats: colour transfers go through UINT "ownership transfer" formats when bit-exact transfer is
  supported (RTC:384-400, `GetColorOwnershipTransferVulkanFormat`), chosen per `msaa_samples`.

### 1.6 Dumps and resolves
- `Resolve` (RTC:1127-1215): `direct_host_resolve=true` (shared cache.cpp:40, hot-reloadable) first tries
  `TryResolveCopyDirectly` (compute reading the host RT as a sampled image via the dump-pipeline key for
  `render_target->key().msaa_samples`, RTC:6002, 5957 `GetDirectResolvePipeline`), falling back to
  `DumpRenderTargets` (RTC:6022-6155: compute, RT sampled as `texture2d_ms` when `msaa != 1x`, RTC:5440-5466,
  writes the EDRAM storage buffer, one `vkCmdDispatch` per rectangle piece, RTC:6123-6139) followed by the
  `resolve_fast_*_1x2xmsaa_cs` / `resolve_fast_*_4xmsaa_cs` / `resolve_full_*_cs` copy shaders reading the
  EDRAM buffer. Both are **compute reading samples individually**; cost is linear in resolved samples
  (a 1280x720 2x resolve is ~1.8 M fetches, negligible).
- EDRAM addressing with MSAA: shared cache.cpp:191-193, 416-436, 629, 805-810, 831-834: `msaa_samples`
  changes tile width in pixels (80 at 1x/2x, 40 at 4x) and tile height (16 at 1x, 8 at 2x/4x); it never
  changes tile count. `kSysFlag_MsaaSamples` carries the guest value into shaders (spirv_translator.h:128).

### 1.7 What the SPIR-V translator emits that depends on MSAA (`spirv_translator.cpp`, `_rb.cpp`)
- `StartFragmentShaderBeforeMain`, spirv_translator.cpp:2947-2949:
  `alpha_to_coverage_possible = !is_depth_only_fragment_shader_ && writes_color_target(0) &&
  !IsExecutionModeEarlyFragmentTests()`.
  Early fragment tests (`DepthStencilMode::kEarlyHint`) are selected in
  `GetCurrentPixelShaderModification` (pipeline_cache.cpp:1011-1018) only when
  `implicit_early_z_write_allowed()` (= `!kills_pixels && !writes_depth && !memexport`, shader.h:943-948)
  **and** (`!writes_color_target(0)` or `!DoesCoverageDependOnAlpha(RB_COLORCONTROL)`, i.e. alpha test off
  and alpha-to-mask off). So every PS used with alpha test or alpha-to-mask enabled, and every PS that
  kills, writes oDepth or memexports, is **not** early-Z and has `alpha_to_coverage_possible = true`.
- For those shaders (2957-2958) `gl_FragCoord` is declared, and (2975-2980):
  ```
  if (edram_fragment_shader_interlock_ || IsSampleRate() ||
      (alpha_to_coverage_possible && features_.sample_rate_shading)) {
    // SampleMask depends on SampleRateShading in some SPIR-V revisions.
    builder_->addCapability(spv::CapabilitySampleRateShading);
  }
  ```
  and (3033-3039) a `gl_SampleMask` **output** is declared. `gl_SampleID` is declared only when
  `IsSampleRate()` (2981-2986) -> never here. `features_.sample_rate_shading` = device
  `sampleRateShading` (spirv_translator.cpp:62), true on MoltenVK (MVKDevice.mm:2764).
- `_rb.cpp:444-451` always stores the mask; `_rb.cpp:591-780` loads `system_constants.alpha_to_mask`
  (written by command_processor.cpp:6276-6279 from `RB_COLORCONTROL`) and, inside a
  `DontFlatten` branch, computes the Xenos dithered coverage per sample using `gl_FragCoord` and
  `kSysFlag_MsaaSamples` (1x/2x/4x sub-branches, 700-745). Alpha test uses `OpKill` (`_rb.cpp:431`).
- Nothing else in the translated game shaders depends on the sample count; the FSI-only paths
  (`FSI_*`, `gl_SampleMaskIn`, 3193-3210) are compiled out in the fbo path.

## 2. What MoltenVK turns each of those into (Metal)

| Vulkan object/op | MoltenVK code | Metal result |
|---|---|---|
| `VkImage` samples>1, usage SAMPLED+ATTACHMENT | `MVKImage::validateSamples` MVKImage.mm:1342-1362; storage mode MVKImage.mm:957-962, 1144-1147, 1801 | `MTLTextureType2DMultisample`, `MTLStorageModePrivate`. Memoryless is offered **only** for `TRANSIENT_ATTACHMENT` usage (lazily allocated memory types, 957-962) and a memoryless texture cannot be loaded (MVKRenderPass.mm:814-815) or sampled; the RTC needs both, so memoryless is impossible for EDRAM-backed RTs. |
| Attachment load/store | `MVKAttachmentDescription::populateMTLRenderPassAttachmentDescriptor` MVKRenderPass.mm:795-823 and `encodeStoreAction`/`getMTLStoreAction` 826-920 | `loadAction = Load` whenever `loadOverride || isResuming || !isRenderingEntireAttachment || !isFirstUseOfAttachment`, else mapped from the Vulkan op; `storeAction` is left `Unknown` and set at encoder end: `Store` when `storeOverride || !isRenderingEntireAttachment || !isLastUseOfAttachment`, `MultisampleResolve`/`StoreAndMultisampleResolve` only if a resolve attachment exists (none here), `DontCare` only for memoryless or an explicit DONT_CARE at last use. With the RTC's LOAD/STORE this is a full load and a full store of **all samples** of every attachment for every Vulkan render pass. |
| Render-pass restart (tessellation, indirect conversion, multiview) | `MVKCommandEncoder::beginMetalRenderPass(kMVKCommandUseRestartSubpass)` MVKCommandBuffer.mm:768-856, `restartMetalRenderPassIfNeeded` 851-856; call sites MVKCmdDraw.mm:291, 437, 576, 677, 741, 852, 1033, 1094, 1418, 1483 | `encodeStoreActions(true)` -> `Store` on everything, new `MTLRenderCommandEncoder` with `Load` on everything (`isRestart` -> `loadOverride`). Each restart = store+reload of the full EDRAM-sized attachment set. Sample-count neutral here (same bytes), and measured at 8.8 ms total before T3. |
| `rasterizationSamples`, `alphaToCoverageEnable`, `pSampleMask`, `sampleShadingEnable` | MVKPipeline.mm:2017-2031, 1676-1690 | `rasterSampleCount`; `alphaToCoverageEnabled` (unused by the RTC); a non-all-ones `pSampleMask` becomes `mslOptions.additional_fixed_sample_mask` (ANDed into the shader's `[[sample_mask]]`) - never for this game; `sampleShadingEnable && minSampleShading != 0` becomes `mslOptions.force_sample_rate_shading` - only the MSAA **transfer** pipelines. |
| SPIR-V `SampleRateShading` capability / `gl_SampleID` / `gl_SampleMask` | SPIRV-Cross `CompilerMSL::is_sample_rate()` spirv_msl.cpp:15040-15047; `needs_sample_id` 1915-1918; implicit `gl_SampleID` creation 606-626 (also 399-402); FragCoord fixup 15895-15899; names 18677-18680 | `is_sample_rate()` is true if `force_sample_rate_shading` **or the module declares `CapabilitySampleRateShading`**. If so and the shader reads `gl_FragCoord`, SPIRV-Cross adds a `uint gl_SampleID [[sample_id]]` input and emits `gl_FragCoord.xy += get_sample_position(gl_SampleID) - 0.5`. `gl_SampleMask` becomes `uint gl_SampleMask [[sample_mask]]` output. Per the MSL spec, a fragment function with a `[[sample_id]]` (or `[[sample_mask]]`) **input** executes once per sample; a `[[sample_mask]]` output alone does not. |
| `texture2DMS` fetches in transfer/dump/resolve shaders | SPIRV-Cross `OpImageFetch` on an ms image | `texture2d_ms<T>::read(uint2, sample)`; MoltenVK also exposes `multisampleLayeredRendering` on Apple7+ (MVKDevice.mm:2584-2586) which is irrelevant here (single layer). |
| `vkCmdCopyImage` / blits of MSAA images | MVKCmdTransfer.mm:127-130 (same sample count required), 815-929 (resolve via render pass) | Not used by the RTC for render targets. |
| Device features | MVKDevice.mm:2764 `sampleRateShading = true`; `shaderStorageImageMultisample` never set (false); `VK_EXT_sample_locations` only with programmable sample positions (3527-3528); stencil export on Apple5+ (3536) | The RTC needs none of the missing ones: it samples MSAA images (sampled, not storage), does not use sample locations, and stencil export is present. **No MSAA feature is missing on MoltenVK that would trigger a slower emulator fallback.** |

## 3. Explaining the measured "binary" ~50 ms

What does the code allow to differ between 1x and 2x/4x in the three heavy main-scene passes?

(a) **Emulator MSAA-only paths.** Real: per-sample transfer draws (1.5), per-sample dump/direct-resolve
compute (1.6). But those execute in their own render passes / compute dispatches and the trace put all
"transfer/compute" at ~7 ms, so they are not the 50 ms. No MoltenVK feature gap forces a fallback (table
above). Rejected as the main cause.

(b) **TBDR load/store at pass boundaries.** The RTC uses LOAD/STORE on every attachment, MoltenVK honours it
and additionally forces Store+Load on every restart. But host RTs are EDRAM-sized, so bytes are identical
at 1x and MSAA (1.2), and the trace measured pass fixed cost 1.3 ms + restarts 8.8 ms. Cannot be binary,
cannot be 50 ms. Rejected.

(c) **MoltenVK/SPIRV-Cross emulation that only bites when samples > 1 - the real one.** Every pixel shader
with `alpha_to_coverage_possible` (alpha test on, alpha-to-mask on, texkill/clip, oDepth, memexport;
see 1.7) declares `CapabilitySampleRateShading`. SPIRV-Cross's `is_sample_rate()` then classifies the whole
shader as sample-rate, injects `[[sample_id]]`, and Metal runs the fragment function **per sample**. At 1x
that is one invocation per pixel (no change); at 2x it is two, at 4x four, for every draw using such a
shader. Fable 2's main 2x passes are full of alpha-tested foliage/fur/hair, alpha-to-mask vegetation,
particles with kill, and (probably) shaders with clip - exactly the fragment-heavy draws. The 4x passes
(60 small passes/frame) presumably use depth-only (`is_depth_only_fragment_shader_`) or opaque early-Z
shaders, which never get the capability, so 4x->2x shows ~0 ms. The measured pattern - no change
4x->2x, ~50 ms 2x->1x - is exactly "x2 fragment cost on the 2x pass for sample-rate shaders", not a
binary on/off cost. Secondary effects of the same bug: per-sample `gl_FragCoord` offsets (different texture
coordinates per sample, worse quad/texture-cache coherence), per-sample interpolation, and visibility
resolved per sample for these already non-HSR (kill/sample_mask) draws. These can make the cost exceed
a clean x2, which reconciles "50 ms saved" with "82 ms total fragment" (a pure x2 on part of the pass
could not save more than half of it). Caveat: the 82/30/7 split and the 50 ms delta come from different
runs/builds, so the arithmetic is indicative only. The transfer pipelines are legitimately sample-rate
(by `sampleShadingEnable`) and are not affected by the fix.

(d) **Pipeline differences / HSR.** Opaque shaders get `ExecutionModeEarlyFragmentTests` (kEarlyHint), so
Apple HSR/early-Z works for them at any sample count. Alpha-tested shaders lose HSR at 1x too (OpKill +
`[[sample_mask]]` output) - not binary by itself, but (c) makes them 2x/4x more expensive. No
`alphaToCoverageEnable`, no `pSampleMask`, no `sampleShadingEnable` on game pipelines. Rejected as an
independent cause.

Residual alternative if (c) turns out not to be the whole story: Apple GPUs may implement 2x MSAA with the
same per-sample pipeline cost as 4x (blend/depth/stencil per sample, half-size tiles -> twice the tiles
and twice the per-tile primitive processing). That would also look "binary" between 2x and 4x. The
discriminating measurements in section 5 separate the two.

## 4. Fixes, ranked by expected gain / effort

1. **Do not declare `SampleRateShading` for `gl_SampleMask`-only shaders.** `spirv_translator.cpp:2975-2980`:
   change the condition to `edram_fragment_shader_interlock_ || IsSampleRate()`, keep the `gl_SampleMask`
   output creation at 3033-3039 unchanged. In current SPIR-V, `BuiltIn SampleMask` requires only the
   `Shader` capability (SPIRV-Headers grammar; spirv-val accepts it); `SampleRateShading` is required for
   `SampleId`/`SamplePosition`/`Sample`-decorated inputs, which this path does not use. The comment's
   "some SPIR-V revisions" refers to glslang's habit of adding the capability for `gl_SampleMask`, not to a
   validation rule. Side effect on all hosts: SPIRV-Cross stops offsetting `gl_FragCoord` per sample,
   which is the Xenos pixel-centre semantics the translator assumes anyway. Expected gain: most of the
   ~50 ms (the whole 2x->1x delta if (c) is the cause). Effort: one line + run with validation layers on
   once. Zero visual cost.
   Optional follow-up (1b): only create `gl_SampleMask` when the pipeline actually has
   `RB_COLORCONTROL.alpha_to_mask_enable` (new bit in `Modification::pixel`, set in
   `GetCurrentPixelShaderModification`, pipeline_cache.cpp:~990-1020) so alpha-tested shaders carry no
   `[[sample_mask]]` output and no coverage branch at all. Medium effort, small extra gain; mainly removes
   a non-HSR trigger for alpha-tested draws without alpha-to-mask.

2. **Zero-code confirmation** before anything bigger: see section 5. Do (1) and re-measure first; it is
   cheaper than any instrumentation.

3. **macOS option: guest MSAA on 1-sample host render targets ("host_msaa_1x").** Only if a residual
   MSAA cost remains after (1). The guest EDRAM layout must stay intact (the `RB_SURFACE_INFO` hack changed
   the guest's own layout and produced the garbage bands), so `RenderTargetKey::msaa_samples` and all shared
   RTC addressing (1.6) keep the guest value and only the *host* sample count is forced to 1. Touch points,
   all in `VulkanRenderTargetCache`: a `HostSampleCount(key)` helper used at image creation (RTC:1877-1881),
   legacy render pass attachment `samples` (RTC:1575-1600), graphics pipeline `rasterizationSamples`
   (pipeline_cache.cpp:3290-3303, via `render_target_cache_`), transfer pipelines
   (`dest_sample_count`, RTC:4383-4400) and transfer shaders (`dest_msaa_samples != 1x` branch RTC:2589-2600,
   `source_is_multisampled` RTC:2444-2476/2516 - read sample 0 / write one sample), dump shader
   (`source_is_multisampled`, RTC:5440-5466 - fetch the single host sample and write it to **every** guest
   sample slot of the EDRAM tile so `resolve_fast_*msaa_cs` averaging/selection reproduces the pixel) and
   the direct-resolve shader (RTC:~5880-5920, same replication). `kSysFlag_MsaaSamples` stays the guest
   value; the alpha-to-coverage code then produces 2/4-bit masks of which Metal honours bit 0, i.e.
   alpha-to-mask degrades to a dithered alpha test. Visual cost: no edge anti-aliasing, dithered A2M
   foliage; EDRAM layout and resolves remain correct. Effort: ~150-250 lines, moderate risk in the
   bit-exact UINT transfer formats (`GetColorOwnershipTransferVulkanFormat(format, msaa)`), stencil
   transfers and depth resolves. Xenia/ReXGlue have no existing knob for this: `draw_resolution_scale_*`
   is an upscale (more cost, not less) and nothing downgrades `msaa_samples`.

4. **Transfers/dumps stay per-sample** (correct and cheap, ~7 ms). Reducing them means fewer ownership
   changes (fewer EDRAM aliasing switches per frame), which is not MSAA-specific; leave for later.

5. **Memoryless / transient MSAA attachments: not applicable.** EDRAM contents must persist across passes
   and be sampled by compute/transfer shaders; MoltenVK only offers memoryless for `TRANSIENT_ATTACHMENT`
   images and cannot load them (MVKImage.mm:957-962, MVKRenderPass.mm:814-815).

6. **Render pass restarts with MSAA**: already addressed by T3 (tessellation out of Metal). The restart
   cost is sample-count neutral because attachment bytes are constant (1.2); nothing MSAA-specific to do.

## 5. Cheapest discriminating measurements (to run later)

1. **Apply fix 1 and re-run the usual base/MSAA1 pair** (`tools/mac/prof3d_hold.sh`, `phase.sh`). If base
   drops to ~60 ms, done; what remains between base and MSAA1 is the genuine per-sample hardware cost.
2. **Inspect one main-pass alpha-tested draw in the existing Xcode GPU capture** (`out/mac-measure/f2_3d.gputrace`,
   or a new one with `MVK_CONFIG_DEBUG=1` so MSL source is retained): look for `[[sample_id]]` and
   `get_sample_position(` in the fragment function. Presence = hypothesis (c) confirmed without rebuilding.
   In the same capture, Performance -> per-encoder counters: "Fragment Shader Invocations" vs
   "Fragments/Pixels" for the three big encoders; ~2x invocations per pixel at 2x confirms per-sample
   execution.
3. **Force all 2x surfaces to 4x** (one more `TEMPDIAG` switch next to `FABLE2_EXP_MSAA_MAX2` in
   `tools/mac/exp_switches.patch`, writing `msaa_samples = 2` instead of 1). If the frame time stays ~112 ms
   the MSAA cost is sample-count-independent (Apple 2x == 4x internally); if the main pass roughly doubles
   its MSAA share, the cost scales per sample and (c) is the mechanism.
4. **Count sample-rate pixel shaders**: log (once per translation) in `SpirvShaderTranslator` when
   `alpha_to_coverage_possible && !IsSampleRate()` adds the capability, and count draws using them per
   frame in the 2x vs 4x passes (`VulkanCommandProcessor::IssueDraw`). Confirms the "4x passes have none"
   half of the explanation.
5. If 1-3 leave a residue: Metal System Trace with GPU counters on base vs MSAA1 for the three big
   encoders (tile load/store bytes, fragment invocations, HSR/visibility counters) to split per-sample
   blend/depth cost from tile overhead.

## Appendix: file/line index

- SDK `src/graphics/vulkan/render_target_cache.cpp`: 203-213 path; 365-400 2x/transfer format probing;
  648-649 float24 cvars; 1127-1215 `Resolve`; 1520-1558 dynamic-rendering attachments; 1575-1626 legacy
  render pass; 1866-1900 image creation; 2300 `GetTransferShader` (2391/2422 stencil export, 2444-2476/2516
  ms source image, 2589-2600 `gl_SampleID`); 4284 dynamic rendering; 4380-4420 transfer pipeline multisample
  state; 4549 `PerformTransfersAndResolveClears` (4845-4855, 5260-5270 stencil-bit fallback); 5440-5466 dump
  shader ms image; 5957 `GetDirectResolvePipeline`; 6022-6155 `DumpRenderTargets`.
- SDK `src/graphics/vulkan/pipeline_cache.cpp`: 1003-1018 depth/stencil mode & early-Z hint; 1400-1404
  `sample_rate_shading`; 3279-3309 multisample state.
- SDK `src/graphics/pipeline/shader/spirv_translator.cpp`: 2947-2949, 2957-2958, 2975-2986, 3033-3039;
  `_rb.cpp`: 431, 444-451, 591-780. `include/rex/graphics/pipeline/shader/shader.h:943-948`.
- SDK `src/graphics/pipeline/render_target/cache.cpp`: 40 `direct_host_resolve`; 191-193, 416-436, 629,
  805-810, 831-834 MSAA in EDRAM geometry. `src/graphics/flags.cpp:18-25` cvars. `command_processor.cpp:
  87, 6104-6116, 6276-6279`.
- MoltenVK `GPUObjects/MVKRenderPass.mm`: 160-270, 795-823, 826-858, 891-920. `GPUObjects/MVKImage.mm`:
  957-962, 1144-1147, 1342-1362, 1801. `GPUObjects/MVKPipeline.mm`: 1676-1690, 2017-2031.
  `GPUObjects/MVKDevice.mm`: 2559-2592, 2649-2651, 2764, 3527-3537. `Commands/MVKCommandBuffer.mm`:
  768-856. `Commands/MVKCmdDraw.mm`: restart sites listed above. `Commands/MVKCmdTransfer.mm`: 127-130,
  815-929.
- SPIRV-Cross `spirv_msl.cpp` (upstream main): 399-402, 606-626, 1912-1918, 15040-15047, 15890-15899,
  18677-18680; `spirv_msl.hpp`: 327-330, 480-483.
