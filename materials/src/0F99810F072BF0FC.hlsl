// Fable II's sun shadows (guest pixel shader 0F99810F072BF0FC): the
// screen-space shadow mask the world materials sample - the visible fraction
// of the sun at each pixel, from the near and far shadow map cascades.
//
// Rewritten as percentage-closer soft shadows (PCSS): a blocker search finds
// how far the shadow casters are from the surface, and the filter widens with
// that distance times the sun's angular size - sharp where objects touch the
// ground, softer further away, like real sun shadows. Each tap is filtered
// bilinearly (2x2 texels gathered at once) on a per-pixel rotated Poisson
// disk, with receiver plane depth bias against shadow acne. With
// material_soft_shadows off, a small fixed filter.
//
// The cascades' blend and the distance fade are the game's.
//
// xe_modifications: 0000400100000000

// The translation's bindings (shader_0F99810F072BF0FC_*.bindings.txt).
#define XE_FLOAT_CONSTANT_COUNT 25
#define XE_DESCRIPTOR_INDEX_VECTOR_COUNT 4
#include "../include/xenos_d3d12.hlsli"
#include "../include/material_lighting.hlsli"

// Constants (the guest float constants, packed in ascending order).
// c12: x near cascade end, y fade start, z near to far blend width,
//      w the value without shadows.
#define kCascades xe_float_constants[0]
#define kCameraPosition xe_float_constants[1]  // c31
// c36-c39, c40-c43: screen clip space to the near and far cascades' (u, v,
// depth, w).
#define kNearShadowRow(i) xe_float_constants[2 + (i)]
#define kFarShadowRow(i) xe_float_constants[6 + (i)]
#define kScreen xe_float_constants[10]  // c46: xy 1 / size, zw offset
// c116-c119: screen clip space to the world.
#define kClipToWorldRow(i) xe_float_constants[13 + (i)]
#define kFarCascadeExtension xe_float_constants[17]  // c248: x
#define kFade xe_float_constants[18]                 // c249: w fade rate

// The sun's angular radius at material_shadow_softness 1, in degrees -
// larger than the real sun's 0.27 for visibly soft shadows.
static const float kSunAngularRadius = 1.5;
// How far casters can be from receivers and still widen the shadow, in world
// units.
static const float kMaxCasterDistance = 12.0;

struct Cascade {
  uint fetch;
  uint sampler_slot;
  uint srv_slot;
  // Screen clip space to (u, v, depth, w).
  float4 rows[4];
  uint blocker_taps;
  uint filter_taps;
  // The point in the cascade: the texture coordinates, and the reference
  // (1 - depth, lit where greater than the stored value) with its change
  // across the texture (receiver plane depth bias). Computed outside any
  // branches, for the derivatives.
  float2 uv;
  float reference;
  float2 reference_gradient;
};

Cascade MakeCascade(uint fetch, uint sampler_slot, uint srv_slot, float4 rows[4],
                    uint blocker_taps, uint filter_taps, float4 clip) {
  Cascade cascade;
  cascade.fetch = fetch;
  cascade.sampler_slot = sampler_slot;
  cascade.srv_slot = srv_slot;
  cascade.rows = rows;
  cascade.blocker_taps = blocker_taps;
  cascade.filter_taps = filter_taps;
  float4 projected = float4(dot(rows[0], clip), dot(rows[1], clip), dot(rows[2], clip),
                            dot(rows[3], clip));
  float3 coords = projected.xyz / projected.w;
  cascade.uv = coords.xy;
  cascade.reference = 1.0 - coords.z;
  float3 dx = ddx(float3(cascade.uv, cascade.reference));
  float3 dy = ddy(float3(cascade.uv, cascade.reference));
  float jacobian = dx.x * dy.y - dx.y * dy.x;
  cascade.reference_gradient =
      (abs(jacobian) > 1.0e-14)
          ? float2(dy.y * dx.z - dx.y * dy.z, dx.x * dy.z - dy.x * dx.z) / jacobian
          : 0.0;
  return cascade;
}

float3 ProjectRows(float4 rows[4], float4 position) {
  float4 projected = float4(dot(rows[0], position), dot(rows[1], position),
                            dot(rows[2], position), dot(rows[3], position));
  return projected.xyz / projected.w;
}

float3 ClipToWorld(float4 clip) {
  float4 rows[4] = {kClipToWorldRow(0), kClipToWorldRow(1), kClipToWorldRow(2),
                    kClipToWorldRow(3)};
  return ProjectRows(rows, clip);
}

// The scale of the world-to-cascade mapping (affine for the sun's orthographic
// projection) around a point: x - texture coordinates per world unit, y - depth
// per world unit. Solved from the two transforms of four points near it.
float2 CascadeWorldScales(float4 rows[4], float4 clip) {
  float4 points[4] = {
      clip,
      clip + float4(0.02, 0.0, 0.0, 0.0),
      clip + float4(0.0, 0.02, 0.0, 0.0),
      float4(clip.xy, clip.z * 0.99, 1.0),
  };
  float3 world[4], cascade[4];
  [unroll] for (uint i = 0; i < 4; ++i) {
    world[i] = ClipToWorld(points[i]);
    cascade[i] = ProjectRows(rows, points[i]);
  }
  float3 a = world[1] - world[0], b = world[2] - world[0], c = world[3] - world[0];
  float3 bc = cross(b, c), ca = cross(c, a), ab = cross(a, b);
  float determinant = dot(a, bc);
  float3 d1 = cascade[1] - cascade[0], d2 = cascade[2] - cascade[0], d3 = cascade[3] - cascade[0];
  float3 u_gradient = d1.x * bc + d2.x * ca + d3.x * ab;
  float3 depth_gradient = d1.z * bc + d2.z * ca + d3.z * ab;
  // Zero (unknown) if the points are degenerate.
  return (abs(determinant) >= 1.0e-20)
             ? float2(length(u_gradient), length(depth_gradient)) / abs(determinant)
             : 0.0;
}

float4 GatherDepths(Cascade cascade, float2 uv) {
  return xe_textures_2d[XeDescriptorIndex(cascade.srv_slot)].GatherRed(
      xe_samplers[XeDescriptorIndex(cascade.sampler_slot)], float3(uv, 0.0));
}

// The visible fraction of the sun from the cascade's point (1 - lit).
float SunVisibility(Cascade cascade, float4 clip, float2 rotation, bool soft) {
  float2 uv = cascade.uv;
  float reference = cascade.reference;
  float2 size;
  {
    float elements;
    xe_textures_2d[XeDescriptorIndex(cascade.srv_slot)].GetDimensions(size.x, size.y, elements);
  }
  float texel = 1.0 / size.x;
  float exp_scale = XeFetchExpScale(cascade.fetch);
  float2 world_scales = CascadeWorldScales(cascade.rows, clip);
  bool scales_valid = world_scales.x > 0.0 && world_scales.y > 0.0;

  // Taps compare against the surface's own depth there - limited at depth
  // discontinuities, where the derivatives are meaningless.
  float max_gradient = scales_valid ? 3.0 * world_scales.y / world_scales.x : 0.0;
  float2 gradient = clamp(cascade.reference_gradient, -max_gradient, max_gradient);
  // Half a texel of a 45 degree slope.
  float bias = scales_valid ? 0.5 * texel / world_scales.x * world_scales.y : 0.0;
  reference += bias;

  float filter_radius = 1.25 * texel;
  // Whether any caster is near enough to shadow the point at all.
  bool any_blocker = true;
  if (soft && scales_valid) {
    float tan_angle = tan(radians(kSunAngularRadius * XeShadowSoftness()));
    float search_radius =
        clamp(kMaxCasterDistance * tan_angle * world_scales.x, filter_radius, 48.0 * texel);
    float blocker_sum = 0.0, blocker_count = 0.0;
    for (uint i = 0; i < cascade.blocker_taps; ++i) {
      float2 offset = Rotate(kPoissonDisk16[i], rotation) * search_radius;
      float4 depths = GatherDepths(cascade, uv + offset) * exp_scale;
      float tap_reference = reference + dot(gradient, offset);
      float4 is_blocker = (depths >= tap_reference) ? 1.0 : 0.0;
      blocker_sum += dot(is_blocker, depths);
      blocker_count += dot(is_blocker, 1.0);
    }
    any_blocker = blocker_count > 0.0;
    float caster_distance =
        (blocker_sum / max(blocker_count, 1.0) - reference) / world_scales.y;
    filter_radius = clamp(caster_distance * tan_angle * world_scales.x, filter_radius,
                          search_radius);
  }

  float visible = float(cascade.filter_taps);
  if (any_blocker) {
    visible = 0.0;
    for (uint i = 0; i < cascade.filter_taps; ++i) {
      float2 offset = Rotate(kPoissonDisk16[i], rotation) * filter_radius;
      float2 tap_uv = uv + offset;
      float4 depths = GatherDepths(cascade, tap_uv) * exp_scale;
      float tap_reference = reference + dot(gradient, offset);
      float4 lit = (tap_reference > depths) ? 1.0 : 0.0;
      // Gather order: (-, +), (+, +), (+, -), (-, -).
      float2 weights = frac(tap_uv * size - 0.5);
      visible += lerp(lerp(lit.w, lit.z, weights.x), lerp(lit.x, lit.y, weights.x), weights.y);
    }
  }
  return visible / float(cascade.filter_taps);
}

struct Input {
  float4 position : SV_Position;
  bool is_front_face : SV_IsFrontFace;
};

[earlydepthstencil]
void main(Input input, out float4 color_out : SV_Target0) {
  bool soft = (XeMaterialFeatures() & XE_MATERIAL_FEATURE_SOFT_SHADOWS) != 0;

  // The pixel's position from the scene's depth.
  float2 pixel = abs(XePsParamGen(input.position, input.is_front_face).xy);
  float2 depth_uv = (pixel + kScreen.zw + 0.5) * kScreen.xy;
  float depth = XeTextureFetch2D(13u, 1u, 2u, 3u, 0x1u, depth_uv, kXeFetchRoundingOffset,
                                 XE_FETCH_COMPUTED_LOD, 0.0, 0.0).x;
  float4 clip = float4(depth_uv.x * 2.0 - 1.0, 1.0 - depth_uv.y * 2.0, 1.0 - depth, 1.0);
  float camera_distance = length(ClipToWorld(clip) - kCameraPosition.xyz);

  float angle = InterleavedGradientNoise(input.position.xy) * (2.0 * kMaterialPi);
  float2 rotation = float2(cos(angle), sin(angle));

  float4 near_rows[4] = {kNearShadowRow(0), kNearShadowRow(1), kNearShadowRow(2),
                         kNearShadowRow(3)};
  float4 far_rows[4] = {kFarShadowRow(0), kFarShadowRow(1), kFarShadowRow(2), kFarShadowRow(3)};
  Cascade near_cascade = MakeCascade(6u, 7u, 8u, near_rows, 12u, soft ? 16u : 8u, clip);
  Cascade far_cascade = MakeCascade(7u, 10u, 11u, far_rows, 8u, 8u, clip);

  float near_visibility = 0.0, far_visibility = 0.0;
  if (camera_distance < kCascades.x + kCascades.z) {
    near_visibility = SunVisibility(near_cascade, clip, rotation, soft);
  }
  if (camera_distance > kCascades.x && camera_distance < kCascades.y + kFarCascadeExtension.x) {
    far_visibility = SunVisibility(far_cascade, clip, rotation, soft);
  }
  float blend = saturate(XeMul(camera_distance - kCascades.x, rcp(kCascades.z)));
  float visibility = lerp(near_visibility, far_visibility, blend);
  float fade = saturate(XeMul(kFade.w, camera_distance - kCascades.y));
  visibility = lerp(visibility, kCascades.w, fade);
  color_out = XeColorOutput(float4(visibility.xxx, 1.0), 0u);
}
