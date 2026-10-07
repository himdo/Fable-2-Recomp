# Material shaders

Rewritten versions of Fable II's own shaders with modern lighting, loaded by
the ReXGlue SDK in place of its translations of the game's Xbox 360 shaders
(Direct3D 12). See [docs/GRAPHICS_ENHANCEMENTS.md](../docs/GRAPHICS_ENHANCEMENTS.md)
for what they change and their settings (F6 menu, **Materials**).

```
materials/
  include/xenos_d3d12.hlsli       the translator's interface and semantics
  include/material_lighting.hlsli GGX, Fresnel, sampling patterns
  src/<HASH>.hlsl                 the material shaders
  d3d12/<HASH>_<MOD>.dxbc         compiled (staged next to the exe by the build)
  generated/                      local, ignored - see "Studying a shader"
```

## How a replacement works

When the emulator translates a guest shader, the SDK looks for
`<exe>/materials/d3d12/<HASH>_<MODIFICATION>.dxbc` (then `<HASH>.dxbc`) and
uses it instead of the translation (`material_shaders = false` turns this
off). The replacement keeps the translation's interface, which is what the
emulator binds for it:

- **Inputs:** the interpolators the vertex shader writes (`TEXCOORD0...`),
  then `SV_Position` and `SV_IsFrontFace`. The *modification* (the 64-bit
  suffix) records which interpolators exist, which are centroid-sampled, the
  pixel parameter register, and the early depth mode - a shader is compiled
  once per modification it replaces.
- **Constants:** `xe_float_constants` holds the guest float constants the
  shader uses, packed in ascending order (`c8, c9, c22...`); bool constants
  are bits of `xe_bool_constants`; texture fetch constants are in
  `xe_fetch_constants`.
- **Textures and samplers** are bindless: `xe_descriptor_indices` gives the
  heap index of each binding, and each texture fetch constant has an unsigned
  and a signed texture binding (see `XeTextureFetch2D`).
- **Outputs:** `SV_Target0...`, and `SV_Coverage` unless the early depth
  mode is on; the alpha test, alpha to coverage and the color exponent bias
  come from the system constants (`XeAlphaTest`, `XeAlphaToCoverage`,
  `XeColorOutput`).
- **Settings:** `xe_material_params` in the system constants -
  `XeMaterialFeatures()`, `XeShadowSoftness()`, `XeSpecularIntensity()`,
  `XeRoughness()` (filled from the `material_*` cvars every draw, so they
  change live).

`include/xenos_d3d12.hlsli` declares all of this and implements what the
translator does: texture fetches with the fetch constant's LOD bias, signs,
gamma and exponent bias, the Xbox 360's multiply where 0 times anything is 0,
the pixel parameters, and the epilogue.

## Building

```
python tools/build_material_shaders.py
```

compiles every `src/*.hlsl` with FXC (Windows SDK) for each modification in
its `// xe_modifications:` line, into `d3d12/`. `build.cmd` copies `d3d12/`
next to `fable_2.exe` (the `fable_2_materials_staging` target).

## Making a new one

1. **Find the shader.** Log a frame with the GPU frame logger
   (`gpu_frame_log` cvar, e.g. `tools/fable2_control.py cvar gpu_frame_log
   frame.txt`): each pass lists its vertex and pixel shader hashes and draw
   counts.
2. **Dump it** by running the game with `--dump_shaders=<folder>`. For each
   shader this writes the microcode disassembly
   (`shader_<HASH>.ucode.frag`), the translation, and its bindings
   (`shader_<HASH>_<MOD>.d3d12_rtv.bindings.txt`: the packed float constants,
   and the descriptor index of each texture and sampler).
3. **Study it.** `tools/xenos_to_hlsl.py <folder> <HASH> -o
   materials/generated/<HASH>.hlsl` converts the microcode into a faithful
   HLSL port (one statement per instruction, with the translator's semantics)
   that renders the same as the translation. It compiles like a material
   shader - `python tools/build_material_shaders.py --sources materials/generated
   --output out/build/win-amd64-release/materials/d3d12` - which is how this
   interface was verified.
4. **Write the material shader** in `src/<HASH>.hlsl` with the same inputs,
   bindings and outputs, from what the port shows the shader does.

**Ports stay local.** A port is a translation of the game's own code, like the
recompiled code in `generated/` - it isn't committed (`materials/generated/`
is ignored). The shaders in `src/` are written from scratch: modern lighting
built on the material's inputs (textures, constants, interpolators), not the
game's code.

## Debugging

- The log says which material shaders were loaded: `Material shader <HASH>
  (modification <MOD>): <file>`.
- A replacement that doesn't match the translation's interface makes the
  pipeline fail to create (logged by the D3D12 pipeline cache) and the draws
  using it disappear. Compare the signatures with
  `fxc /dumpbin <file>`: inputs, outputs and resource bindings must match the
  translation's (`shader_<HASH>_<MOD>.d3d12_rtv.bin.frag`).
- FXC reserves some words as identifiers: `linear`, `texture`, `sampler_state`,
  `pass`.
