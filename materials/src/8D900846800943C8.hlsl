// Fable II's main world material (guest pixel shader 8D900846800943C8):
// buildings, ground, walls and props - most of the scene.
//
// Rewritten with modern lighting, from the material's inputs:
// - Sun highlights: GGX microfacet specular with Schlick Fresnel, using the
//   material's specular map as the reflectance at normal incidence (F0) - small
//   bright highlights that grow towards grazing angles, instead of a broad
//   fixed-exponent Phong lobe.
// - Environment reflections (on materials that have a cube map) weighted by
//   Fresnel, so they're strongest at grazing angles.
// With material_specular off, the classic lighting: Phong highlights with an
// exponent of 4 and constant-weighted reflections.
//
// The rest takes the material's inputs as they are: the albedo, specular and
// normal maps, the screen-space sun shadow mask (soft with
// 0F99810F072BF0FC), vertex lighting with sky occlusion, the lightmap, and the
// distance fog ramps.
//
// xe_modifications: 0000000F0040007F 0000400F0040007F

// The translation's bindings (shader_8D900846800943C8_*.bindings.txt).
#define XE_FLOAT_CONSTANT_COUNT 21
#define XE_DESCRIPTOR_INDEX_VECTOR_COUNT 7
#include "../include/xenos_d3d12.hlsli"
#include "../include/material_lighting.hlsli"

// Material constants (the guest float constants, packed in ascending order).
#define kShadowMaskTransform xe_float_constants[0]  // c0: xy offset, zw scale
#define kAlbedoUvTransform xe_float_constants[1]    // c8: xy scale, zw offset
#define kSpecularUvTransform xe_float_constants[2]  // c9
#define kNormalUvTransform xe_float_constants[3]    // c10
#define kCameraPosition xe_float_constants[4]       // c19
#define kAmbient xe_float_constants[5]              // c20: rgb ambient, a exposure
#define kVertexLight xe_float_constants[6]          // c21: rgb scale, a power
#define kSkyOcclusion xe_float_constants[7]         // c22: rgb fully occluded
#define kSpecularControl xe_float_constants[8]      // c23: a N.L sharpness
#define kLightmap xe_float_constants[9]             // c24: z scale
#define kOutput xe_float_constants[10]              // c26: rgb color, a alpha
#define kDiffuse xe_float_constants[11]             // c27: a ambient diffuse scale
#define kFogBlend xe_float_constants[12]            // c28: x amount, y color scale
#define kTint xe_float_constants[13]                // c49
#define kFogSunDirection xe_float_constants[14]     // c66
#define kFog xe_float_constants[15]                 // c67: x start, z color, w density
#define kFogColor xe_float_constants[16]            // c68
#define kSunDirection xe_float_constants[17]        // c80: toward the sun
#define kSunColor xe_float_constants[18]            // c87

// Texture fetches: fetch constant, then the sampler and the unsigned and
// signed texture descriptor slots.
static const float2 kFetchOffset = kXeFetchRoundingOffset;
float4 FetchAlbedo(float2 uv) {
  return XeTextureFetch2D(0u, 4u, 5u, 6u, 0xFu, uv, kFetchOffset, XE_FETCH_COMPUTED_LOD, 0.0,
                          0.0);
}
float3 FetchSpecular(float2 uv) {
  return XeTextureFetch2D(1u, 7u, 8u, 9u, 0x7u, uv, kFetchOffset, XE_FETCH_COMPUTED_LOD, 0.0,
                          0.0).rgb;
}
float2 FetchNormal(float2 uv) {
  return XeTextureFetch2D(2u, 1u, 2u, 3u, 0x3u, uv, kFetchOffset, XE_FETCH_COMPUTED_LOD, 0.0,
                          0.0).xy;
}
float FetchSunVisibility(float2 uv) {
  return XeTextureFetch2D(8u, 10u, 11u, 12u, 0x1u, uv, kFetchOffset, XE_FETCH_COMPUTED_LOD, 0.0,
                          0.0).x;
}
float3 FetchLightmap(float2 uv) {
  return XeTextureFetch2D(13u, 13u, 14u, 15u, 0x7u, uv, kFetchOffset, XE_FETCH_COMPUTED_LOD, 0.0,
                          0.0).rgb;
}
// The cube map from a direction (any length).
float3 FetchEnvironment(float3 direction) {
  // Projected onto the cube's faces, as the translation samples it.
  direction /= max(max(abs(direction.x), abs(direction.y)), abs(direction.z));
  float face_lod = XeFetchLod(14u, XE_FETCH_COMPUTED_LOD, 0.0, 0.0);
  float3 gradient_scale = exp2(face_lod);
  bool2 srvs = XeFetchSrvsNeeded(14u, 0x7u);
  float4 unsigned_value = 0.0, signed_value = 0.0;
  if (srvs.x) {
    unsigned_value = XeSampleCube(17u, 16u, direction, true, ddx_coarse(direction) * gradient_scale,
                                  ddy_coarse(direction) * gradient_scale, 0.0);
  }
  if (srvs.y) {
    signed_value = XeSampleCube(18u, 16u, direction, true, ddx_coarse(direction) * gradient_scale,
                                ddy_coarse(direction) * gradient_scale, 0.0);
  }
  return XeFetchFinish(14u, 0x7u, unsigned_value, signed_value).rgb;
}
float3 FetchFogColor(float coordinate) {
  return XeTextureFetch1D(4u, 19u, 20u, 21u, 0x7u, coordinate, kXeFetchRoundingOffset,
                          XE_FETCH_COMPUTED_LOD, 0.0, 0.0).rgb;
}
float3 FetchFogTransmittance(float coordinate) {
  return XeTextureFetch1D(5u, 22u, 23u, 24u, 0x7u, coordinate, kXeFetchRoundingOffset,
                          XE_FETCH_COMPUTED_LOD, 0.0, 0.0).rgb;
}

#if XE_MODIFICATION_INDEX == 0
#define XE_EARLY_DEPTH_STENCIL 0
#else
#define XE_EARLY_DEPTH_STENCIL 1
#endif

struct Input {
  float4 texcoord : TEXCOORD0;
  float4 vertex_normal : TEXCOORD1;
  float4 world_position : TEXCOORD2;
  float4 tangent : TEXCOORD3;
  float4 binormal : TEXCOORD4;
  float4 occlusion_direction : TEXCOORD5;
  centroid float4 vertex_color : TEXCOORD6;
  float4 position : SV_Position;
  bool is_front_face : SV_IsFrontFace;
};

#if XE_EARLY_DEPTH_STENCIL
[earlydepthstencil]
void main(Input input, out float4 color_out : SV_Target0) {
#else
void main(Input input, out float4 color_out : SV_Target0, out uint coverage_out : SV_Coverage) {
#endif
  bool modern = (XeMaterialFeatures() & XE_MATERIAL_FEATURE_SPECULAR) != 0;
  float2 uv = input.texcoord.xy;

  // Surface.
  float4 albedo_sample = FetchAlbedo(uv * kAlbedoUvTransform.xy + kAlbedoUvTransform.zw);
  float3 albedo = albedo_sample.rgb * albedo_sample.rgb;
  float alpha = albedo_sample.a;
  if (XeBoolConstant(128u)) {
    // Stored premultiplied.
    albedo = XeMul(albedo.rgbr, rcp(alpha * alpha)).rgb;
  }
  float3 specular_sample =
      FetchSpecular(uv * kSpecularUvTransform.xy + kSpecularUvTransform.zw);
  float3 specular_color = specular_sample * specular_sample;
  float2 normal_xy = FetchNormal(uv * kNormalUvTransform.xy + kNormalUvTransform.zw) * 2.0 - 1.0;
  float normal_z = sqrt(abs(1.0 - dot(normal_xy, normal_xy)));
  float3 n = normalize(normal_xy.x * input.tangent.xyz + normal_xy.y * input.binormal.xyz +
                       normal_z * normalize(input.vertex_normal.xyz));
  float3 to_camera = kCameraPosition.xyz - input.world_position.xyz;
  float camera_distance = length(to_camera);
  float3 v = to_camera / camera_distance;

  // The sun, with the screen-space shadow mask.
  float3 l = kSunDirection.xyz;
  float n_dot_l = saturate(dot(n, l));
  float2 pixel = abs(XePsParamGen(input.position, input.is_front_face).xy);
  float sun_visibility =
      FetchSunVisibility(pixel * kShadowMaskTransform.zw + kShadowMaskTransform.xy);
  float3 sun = kSunColor.rgb * sun_visibility;

  // Ambient: vertex lighting, darkened facing away from the open sky.
  float sky = saturate(dot(normalize(input.occlusion_direction.xyz), n));
  float3 sky_occlusion = lerp(kSkyOcclusion.rgb, 1.0, sky * sky);
  float3 vertex_light;
  [unroll] for (uint i = 0; i < 3; ++i) {
    vertex_light[i] = exp2(XeMul(kVertexLight.a, log2(abs(input.vertex_color[i]))));
  }
  float3 ambient = vertex_light * kVertexLight.rgb * sky_occlusion + kAmbient.rgb;

  float3 color = albedo * (kDiffuse.a * ambient + sun * n_dot_l);

  // Sun highlights.
  float roughness = XeRoughness();
  if (modern) {
    color += SpecularGgx(n, v, l, specular_color, roughness) * sun * XeSpecularIntensity();
  } else {
    float r_dot_v = saturate(dot(reflect(-l, n), v));
    float r_dot_v_2 = r_dot_v * r_dot_v;
    color += specular_color * sun * (r_dot_v_2 * r_dot_v_2) *
             saturate(kSpecularControl.a * n_dot_l);
  }

  if (XeBoolConstant(133u)) {
    float3 lightmap = FetchLightmap(uv);
    color += lightmap * lightmap * kLightmap.z;
  }

  if (XeBoolConstant(134u)) {
    // The reflection, in the cube map's space: in the frame of the vertex's
    // tangent, binormal and normal, Y and Z swapped.
    float3 reflected = reflect(-v, n);
    float3 frame = reflected.x * input.tangent.xyz + reflected.y * input.binormal.xyz +
                   reflected.z * input.vertex_normal.xyz;
    float3 environment = FetchEnvironment(float3(frame.x, frame.z, -frame.y));
    environment *= environment;
    float3 reflectance = modern ? FresnelSchlickRoughness(specular_color, dot(n, v), roughness) *
                                      XeSpecularIntensity()
                                : specular_color;
    color += environment * reflectance;
  }

  color *= kTint.rgb;

  // Distance fog: the color from a ramp by the angle to the sun, the
  // transmittance from a ramp by the distance.
  float fog_distance = camera_distance - kFog.x;
  float3 fog_ramp_color = FetchFogColor(saturate(dot(-v, kFogSunDirection.xyz)));
  float3 fog_transmittance = FetchFogTransmittance(XeMul(kFog.w, fog_distance));
  if (fog_distance > 0.0) {
    float3 fog_color = lerp(kFogColor.rgb, fog_ramp_color, saturate(XeMul(kFog.z, fog_distance)));
    float3 fogged = color * fog_transmittance + (1.0 - fog_transmittance) * kFogBlend.y * fog_color;
    color = lerp(color, fogged, kFogBlend.x);
  }

  float4 result = float4(kAmbient.a * kOutput.rgb * color, kOutput.a * alpha);
#if !XE_EARLY_DEPTH_STENCIL
  XeAlphaTest(result.a);
  coverage_out = XeAlphaToCoverage(result.a, input.position);
#endif
  color_out = XeColorOutput(result, 0u);
}
