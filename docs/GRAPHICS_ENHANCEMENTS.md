# Graphics enhancements

Modern rendering on top of, and in place of, Fable II's own: screen-space
effects added to the game's frame, and *material shaders* that replace some of
the game's shaders outright. Everything is configured live from the in-game
**Graphics Enhancements** menu (**F6**), and all of it lives in the ReXGlue SDK
as a patch (`thirdparty/rexglue-sdk-graphics-fx.patch`) plus this repository's
`materials/` folder.

- [Using it](#using-it)
- [Effects](#effects)
- [Material shaders](#material-shaders)
- [Settings reference](#settings-reference)
- [How it works](#how-it-works)
- [Working on it](#working-on-it)
- [Known limits](#known-limits)

## Using it

Press **F6** in game. The menu frees the mouse while it's open (the game
doesn't see the clicks), and **F6** again closes it. Changes apply instantly;
**Save** writes them to `fable_2.toml` next to the exe, **Reset to defaults**
restores the shipped look. Presets (Off / Low / Medium / High / Ultra) switch
which effects run and their quality, keeping the tuning.

The key is the `bind_effects_menu` cvar (default `F6`).

Every setting is also a cvar, so it can be put in `fable_2.toml`, passed on
the command line (`fable_2.exe --scene_fx_gi=true`) or changed from the F3
console.

The effects work on SDR and HDR displays alike: they're applied to the
game's HDR scene before its own bloom, exposure and tonemapping.

## Effects

| Effect | What it does | Default |
|---|---|---|
| Ambient occlusion | Ground-truth AO with visibility bitmasks (GTAO + the MXAO/"SSILVB" sector approach): soft darkening in corners and under objects without halos around thin geometry, with a multi-bounce compensation. Half or full resolution, depth-aware denoising. | On |
| Global illumination | One bounce of light between nearby surfaces, gathered with the AO's rays from the previous frame's image (color bleeding, lit shadows). | Off |
| Contact shadows | Short screen-space rays toward the sun for the fine shadows the game's shadow maps are too coarse for. | On |
| Volumetric lighting | Light shafts: sunlight scattering in the air, shadowed by the game's own sun shadow maps (both cascades), smoothed over frames. | On |
| Fog | Analytic height fog lit by the sky color, with a glow toward the sun. | On |
| Reflections | Screen-space reflections with Fresnel, on all surfaces or floors only. | Off |
| Sharpening | Contrast-adaptive sharpening of the final image (no upscaling). | Off |
| Color grading | Exposure, contrast, saturation, vibrance, temperature, tint, gamma, shadow lift, vignette, film grain. | Off |
| Texture filtering | Anisotropic filtering override (2x-16x). | Game's |
| Internal resolution | 1x-3x render scale (restart). | 1x |
| Anti-aliasing | FXAA on the output (restart). | Off |

The **Debug** section shows each effect's buffer on its own (split view,
AO, volumetrics, GI, fog, contact shadows, reflections).

## Material shaders

Material shaders are rewritten versions of the game's own shaders with modern
lighting - they change how surfaces are lit, not the finished image. They're
found by the guest shader's fingerprint (its microcode hash) and loaded in
place of the emulator's translation. Currently (Direct3D 12):

| Guest shader | What it renders | Upgrade |
|---|---|---|
| `8D900846800943C8` | The main world material: buildings, ground, walls, props | GGX specular with Schlick Fresnel from the material's specular map (instead of a fixed Phong lobe), Fresnel-weighted cube map reflections |
| `0F99810F072BF0FC` | The sun shadow mask (both cascades) | Percentage-closer soft shadows: blocker search, penumbra from the caster distance and the sun's size, bilinear PCF on a rotated Poisson disk, receiver plane depth bias |

Settings (menu: **Materials**):

| Cvar | Default | |
|---|---|---|
| `material_shaders` | `true` | Load the material shaders (restart to apply) |
| `material_shaders_path` | `materials` | Their folder, relative to the exe |
| `material_soft_shadows` | `true` | PCSS soft shadows (off: a small fixed filter) |
| `material_shadow_softness` | `1.0` | The sun's size: larger is softer |
| `material_specular` | `true` | GGX highlights (off: the classic Phong lighting) |
| `material_specular_intensity` | `1.0` | Highlight and reflection strength |
| `material_roughness` | `0.45` | Surface roughness: lower is sharper |

See [materials/README.md](../materials/README.md) for how they're made.

## Settings reference

Effects (`GPU/Effects`):

| Cvar | Default | |
|---|---|---|
| `scene_fx_ao` | `true` | Ambient occlusion |
| `scene_fx_ao_quality` | `2` | 0 low ... 3 ultra (directions and steps) |
| `scene_fx_ao_radius` | `1.5` | World units (meters) |
| `scene_fx_ao_strength` | `1.0` | |
| `scene_fx_ao_thickness` | `0.5` | Assumed occluder thickness |
| `scene_fx_ao_power` | `1.5` | Contrast |
| `scene_fx_ao_albedo` | `0.4` | Multi-bounce compensation (0 off) |
| `scene_fx_ao_full_resolution` | `false` | Full resolution AO, GI, contact shadows and reflections |
| `scene_fx_gi` | `false` | Global illumination |
| `scene_fx_gi_intensity` | `1.0` | |
| `scene_fx_contact_shadows` | `true` | Contact shadows |
| `scene_fx_contact_shadows_length` | `0.5` | Ray length (meters) |
| `scene_fx_contact_shadows_thickness` | `0.3` | |
| `scene_fx_contact_shadows_strength` | `0.6` | |
| `scene_fx_reflections` | `false` | Screen-space reflections |
| `scene_fx_reflections_quality` | `2` | |
| `scene_fx_reflections_intensity` | `1.0` | |
| `scene_fx_reflections_reflectance` | `0.04` | Reflectance at normal incidence |
| `scene_fx_reflections_distance` | `30.0` | Ray length (meters) |
| `scene_fx_reflections_thickness` | `0.5` | |
| `scene_fx_reflections_floors_only` | `false` | |
| `scene_fx_volumetrics` | `true` | Volumetric light shafts |
| `scene_fx_volumetrics_quality` | `2` | 16 / 24 / 40 / 64 steps |
| `scene_fx_volumetrics_intensity` | `1.0` | |
| `scene_fx_volumetrics_density` | `0.004` | Haze |
| `scene_fx_volumetrics_anisotropy` | `0.6` | Forward scattering (glow toward the sun) |
| `scene_fx_volumetrics_distance` | `120.0` | Meters marched |
| `scene_fx_volumetrics_temporal` | `true` | Accumulate over frames |
| `scene_fx_volumetrics_full_resolution` | `false` | |
| `scene_fx_sun_tint` | `1.0,0.9,0.75` | Sunlight color for the shafts and fog |
| `scene_fx_fog` | `true` | Height fog |
| `scene_fx_fog_density` | `0.0005` | |
| `scene_fx_fog_ground_density` | `0.003` | |
| `scene_fx_fog_ground_height` | `-2.0` | |
| `scene_fx_fog_ground_falloff` | `0.15` | |
| `scene_fx_fog_brightness` | `1.0` | |
| `scene_fx_fog_sun_glow` | `0.5` | |
| `scene_fx_fog_distance` | `1000.0` | |
| `scene_fx_sharpen` | `false` | Sharpening |
| `scene_fx_sharpen_amount` | `0.5` | |
| `scene_fx_grading` | `false` | Color grading (`scene_fx_grading_*`: exposure, contrast, saturation, vibrance, temperature, tint, gamma, shadow_lift) |
| `scene_fx_vignette` | `0.0` | |
| `scene_fx_film_grain` | `0.0` | |
| `scene_fx_debug` | `0` | 1 split, 2 AO, 3 volumetrics, 4 GI, 5 fog, 6 contact shadows, 7 reflections |
| `scene_fx_trace` | `false` | Log the effects' decisions for three frames |
| `scene_fx_camera_constant` / `scene_fx_world_constant` | `0` / `4` | Where the game keeps its view-projection and world matrices in the vertex shader constants |

## How it works

All code is in the SDK (applied as `thirdparty/rexglue-sdk-graphics-fx.patch`):

- `src/graphics/pipeline/scene_effects.cpp` - the backend-independent core.
  It watches the guest's draws and eDRAM resolves:
  - **Camera:** the game's vertex shaders take a per-object
    world-view-projection in `c0-c3` and the object's world matrix in
    `c4-c6`, so the view-projection is `WVP * W^-1`. The most common one
    among the depth pass's draws is the scene's camera, which keeps the
    effects stable while objects with their own transforms are drawn.
  - **Sun shadows:** orthographic depth resolves are the sun's two shadow
    cascades (30 and 70 world units wide), kept across frames.
  - **Scene depth:** the full-screen depth resolve after the depth pre-pass,
    converted to linear distance (the game uses reversed depth).
  - **Effects:** at the first resolve of the HDR scene (the game draws it in
    two tiles with predicated tiling), they're computed once and composited
    into each tile with two blended draws: a multiply (AO, contact shadows,
    GI, fog transmittance) and an add (fog light, shafts, reflections).
  - **Image effects:** sharpening and grading at the final image's resolve.
- `src/graphics/d3d12/scene_effects.cpp`, `src/graphics/vulkan/scene_effects.cpp`
  - the backends: textures, compute dispatches and full-screen draws.
- `src/graphics/shaders/scene_fx/*.hlsl` - the shaders, one HLSL source per
  pass for both backends (FXC to DXBC, glslang to SPIR-V).
- `src/ui/overlay/effects_overlay.cpp` - the F6 menu.
- `src/graphics/pipeline/material_shaders.cpp` and
  `src/graphics/d3d12/pipeline_cache.cpp` - loading the material shaders in
  place of translations, and `xe_material_params` in the translator's system
  constants (`dxbc_translator.h`) for their settings.

## Working on it

Rebuild the effect shaders after editing `src/graphics/shaders/scene_fx/`
(needs FXC from the Windows SDK and glslangValidator built from the SDK's
glslang - see the script's header):

```
python tools/build_scene_fx_shaders.py
```

The SDK changes are carried as a patch: after editing any of the files listed
in `tools/regen_graphics_fx_patch.py`, regenerate it, or the build's
`prepare_runtime_sdk.py` step rejects the stale patch:

```
python tools/regen_graphics_fx_patch.py
build.cmd -release fable_2
```

`scene_fx_trace` and the remote control's `screenshot` command
(`tools/fable2_control.py`) are the tools for checking a change in game.

## Known limits

- The effects are complete on Direct3D 12. The Vulkan backend of the effects
  is in, but on Vulkan the game currently misses some ground surfaces (also
  with every effect off) - Direct3D 12 is recommended.
- Material shaders are Direct3D 12 only (the Vulkan translation has a different
  interface).
- Screen-space effects only see what's on screen: reflections and GI fade at
  the screen's edges.
- The effects follow the game's camera and sun; scenes with unusual shadow
  setups (interiors, cutscenes) may get fewer shafts.
