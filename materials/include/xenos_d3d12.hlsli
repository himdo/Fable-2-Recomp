// The interface of the ReXGlue SDK's Direct3D 12 shader translations (Xenia's
// DxbcShaderTranslator, host render target path, bindless resources) for
// material shaders - pixel shaders replacing the translations of the game's
// own shaders (see materials/README.md):
// - The translator's constant buffers, with the material settings in the
//   system constants (xe_material_params).
// - The Xenos ALU semantics the translator emulates (0 * anything = 0, Shader
//   Model 3 NaN behavior, the special rcp / log variants).
// - Texture fetching like the translator does it (fetch constants, signs,
//   gamma, exponent bias, offsets, LOD bias).
// - The pixel shader epilogue: alpha test, alpha to coverage, color exponent
//   bias.
//
// Before including, a shader defines XE_FLOAT_CONSTANT_COUNT and
// XE_DESCRIPTOR_INDEX_VECTOR_COUNT (the sizes in its bindings).

#ifndef XENOS_D3D12_HLSLI_
#define XENOS_D3D12_HLSLI_

// Constant buffers.

cbuffer xe_system_cbuffer : register(b0) {
  uint xe_flags;
  float2 xe_tessellation_factor_range;
  uint xe_line_loop_closing_index;
  uint xe_vertex_index_endian;
  uint xe_vertex_index_offset;
  uint2 xe_vertex_index_min_max;
  float4 xe_user_clip_planes[6];
  float3 xe_ndc_scale;
  float xe_point_vertex_diameter_min;
  float3 xe_ndc_offset;
  float xe_point_vertex_diameter_max;
  float2 xe_point_constant_diameter;
  float2 xe_point_screen_diameter_to_ndc_radius;
  uint4 xe_texture_swizzled_signs[2];
  uint xe_textures_resolution_scaled;
  uint2 xe_sample_count_log2;
  float xe_alpha_test_reference;
  uint xe_alpha_to_mask;
  uint xe_edram_32bpp_tile_pitch_dwords_scaled;
  uint xe_edram_depth_base_dwords_scaled;
  uint xe_edram_padding;
  float4 xe_color_exp_bias;
  float2 xe_edram_poly_offset_front;
  float2 xe_edram_poly_offset_back;
  uint4 xe_edram_stencil[2];
  uint4 xe_edram_rt_base_dwords_scaled;
  uint4 xe_edram_rt_format_flags;
  float4 xe_edram_rt_clamp[4];
  uint4 xe_edram_rt_keep_mask[2];
  uint4 xe_edram_rt_blend_factors_ops;
  float4 xe_edram_blend_constant;
  // rex/graphics/pipeline/material_shaders.h.
  float4 xe_material_params[4];
};

cbuffer xe_float_cbuffer : register(b1) {
  // The guest float constants the shader uses, in ascending order (or all 256
  // with dynamic addressing).
  float4 xe_float_constants[XE_FLOAT_CONSTANT_COUNT];
};

cbuffer xe_bool_loop_cbuffer : register(b2) {
  uint4 xe_bool_constants[2];
  uint4 xe_loop_constants[8];
};

cbuffer xe_fetch_cbuffer : register(b3) {
  uint4 xe_fetch_constants[48];
};

cbuffer xe_descriptor_indices_cbuffer : register(b4) {
  uint4 xe_descriptor_indices[XE_DESCRIPTOR_INDEX_VECTOR_COUNT];
};

// Bindless resources.
SamplerState xe_samplers[] : register(s0, space0);
Texture2DArray<float4> xe_textures_2d[] : register(t0, space1);
Texture3D<float4> xe_textures_3d[] : register(t0, space2);
TextureCube<float4> xe_textures_cube[] : register(t0, space3);

// xe_flags (DxbcShaderTranslator kSysFlag_*).
#define XE_SYS_FLAG_PRIMITIVE_POLYGONAL (1u << 4)
#define XE_SYS_FLAG_PRIMITIVE_LINE_SHIFT 5u
#define XE_SYS_FLAG_ALPHA_PASS_IF_LESS_SHIFT 7u
#define XE_SYS_FLAG_CONVERT_COLOR_0_TO_GAMMA_SHIFT 10u

// Material settings (material_shaders.h).
#define XE_MATERIAL_FEATURE_SOFT_SHADOWS (1u << 0)
#define XE_MATERIAL_FEATURE_SPECULAR (1u << 1)
uint XeMaterialFeatures() { return asuint(xe_material_params[0].x); }
float XeShadowSoftness() { return xe_material_params[0].y; }
float XeSpecularIntensity() { return xe_material_params[0].z; }
float XeRoughness() { return xe_material_params[0].w; }

// What the translator bakes into its shaders.
#define XE_TRANSLATION_GAMMA_RT_AS_UNORM8 (1u << 0)
#define XE_TRANSLATION_MSAA_2X_SUPPORTED (1u << 1)
#define XE_TRANSLATION_FUZZY_ALPHA_EPSILON (1u << 2)
#define XE_TRANSLATION_SCALED_TEXTURE_OFFSETS (1u << 3)
float2 XeDrawResolutionScale() { return xe_material_params[3].xy; }
uint XeTranslationFlags() { return asuint(xe_material_params[3].z); }

bool XeBoolConstant(uint index) {
  return (xe_bool_constants[index >> 7][(index >> 5) & 3] & (1u << (index & 31))) != 0;
}

uint XeDescriptorIndex(uint slot) { return xe_descriptor_indices[slot >> 2][slot & 3]; }

uint XeFetchConstantDword(uint fetch, uint dword) {
  uint index = fetch * 6u + dword;
  return xe_fetch_constants[index >> 2][index & 3];
}

// ALU (dxbc_translator_alu.cpp).

static const float kXeFltMax = 3.402823466e+38;

// Shader Model 3: +-0 or denormal * anything = +0.
float XeMul(float a, float b) { return (min(abs(a), abs(b)) == 0.0) ? 0.0 : a * b; }
float4 XeMul(float4 a, float4 b) {
  float4 product = a * b;
  return (min(abs(a), abs(b)) == 0.0) ? 0.0 : product;
}
float4 XeMad(float4 a, float4 b, float4 c) { return XeMul(a, b) + c; }
// Shader Model 3 NaN behavior (a op b ? a : b).
float XeMax(float a, float b) { return (a >= b) ? a : b; }
float4 XeMax(float4 a, float4 b) { return (a >= b) ? a : b; }
float XeMin(float a, float b) { return (a < b) ? a : b; }
float4 XeMin(float4 a, float4 b) { return (a < b) ? a : b; }
float4 XeSeq(float4 a, float4 b) { return (a == b) ? 1.0 : 0.0; }
float4 XeSgt(float4 a, float4 b) { return (b < a) ? 1.0 : 0.0; }
float4 XeSge(float4 a, float4 b) { return (a >= b) ? 1.0 : 0.0; }
float4 XeSne(float4 a, float4 b) { return (a != b) ? 1.0 : 0.0; }
float4 XeCndEq(float4 a, float4 b, float4 c) { return (a == 0.0) ? b : c; }
float4 XeCndGe(float4 a, float4 b, float4 c) { return (a >= 0.0) ? b : c; }
float4 XeCndGt(float4 a, float4 b, float4 c) { return (0.0 < a) ? b : c; }
float4 XeDp4(float4 a, float4 b) {
  float result = XeMul(a.x, b.x);
  result += XeMul(a.y, b.y);
  result += XeMul(a.z, b.z);
  result += XeMul(a.w, b.w);
  return result;
}
float4 XeDp3(float4 a, float4 b) {
  float result = XeMul(a.x, b.x);
  result += XeMul(a.y, b.y);
  result += XeMul(a.z, b.z);
  return result;
}
float4 XeDp2Add(float4 a, float4 b, float4 c) {
  float result = XeMul(a.x, b.x);
  result += XeMul(a.y, b.y);
  return result + c.x;
}
float4 XeMax4(float4 a) { return max(max(a.x, a.y), max(a.z, a.w)); }
// The operand is swizzled .zzxy: x in z, y in w, z in x. Result: T coordinate,
// S coordinate, 2 * major axis, face index.
float4 XeCube(float4 operand) {
  float cube_x = operand.z, cube_y = operand.w, cube_z = operand.x;
  float4 result;
  if (abs(cube_z) >= abs(cube_x) && abs(cube_z) >= abs(cube_y)) {
    bool negative = cube_z < 0.0;
    result = float4(-cube_y, negative ? -cube_x : cube_x, 2.0 * cube_z, negative ? 5.0 : 4.0);
  } else if (abs(cube_y) >= abs(cube_x)) {
    bool negative = cube_y < 0.0;
    result = float4(negative ? -cube_z : cube_z, cube_x, 2.0 * cube_y, negative ? 3.0 : 2.0);
  } else {
    bool negative = cube_x < 0.0;
    result = float4(-cube_y, negative ? cube_z : -cube_z, 2.0 * cube_x, negative ? 1.0 : 0.0);
  }
  return result;
}
// As the translator does it (W from the third component of the second operand).
float4 XeDst(float4 a, float4 b) { return float4(1.0, XeMul(a.y, b.y), a.z, b.z); }
// setp_*_push: p0 = a.w == 0 && b.w op 0; result = a.x == 0 && b.x op 0 ? 0 :
// a.x + 1.
float4 XeSetpPush(float4 a, bool4 b_condition, inout bool p0) {
  p0 = a.w == 0.0 && b_condition.w;
  return (a.x == 0.0 && b_condition.x) ? 0.0 : a.x + 1.0;
}

float XeLogC(float a) {
  float result = log2(a);
  return (result == -1.#INF) ? -kXeFltMax : result;
}
// +-Infinity to +-FLT_MAX.
float XeClampInfinity(float a) {
  return asfloat(asuint(a) + ((abs(a) == 1.#INF) ? 0xFFFFFFFFu : 0u));
}
// +-Infinity to +-0.
float XeFlushInfinity(float a) {
  return asfloat(asuint(a) & ((abs(a) != 1.#INF) ? 0xFFFFFFFFu : 0x80000000u));
}
float XeMulsPrev2(float a, float b, float ps) {
  if (ps != -kXeFltMax && -abs(ps) >= -kXeFltMax && -abs(b) >= -kXeFltMax && 0.0 < b) {
    return XeMul(a, ps);
  }
  return -kXeFltMax;
}

// Gamma (dxbc_translator.cpp PWLGammaToLinear / PreSaturatedLinearToPWLGamma).

float XePwlGammaToLinear(float gamma) {
  float scale, offset;
  if (gamma >= 96.0 / 255.0) {
    bool high = gamma >= 192.0 / 255.0;
    scale = high ? 8.0 / 1024.0 : 4.0 / 1024.0;
    offset = high ? -1024.0 : -256.0;
  } else {
    bool high = gamma >= 64.0 / 255.0;
    scale = high ? 2.0 / 1024.0 : 1.0 / 1024.0;
    offset = high ? -64.0 : 0.0;
  }
  float value = saturate(gamma) * (255.0 * 1024.0);
  value = value * scale + offset;
  value += trunc(value * scale);
  return value * (1.0 / 1023.0);
}

float XeLinearToPwlGamma(float value) {
  float scale, offset;
  if (value >= 128.0 / 1023.0) {
    bool high = value >= 512.0 / 1023.0;
    scale = high ? 1023.0 / 8.0 : 1023.0 / 4.0;
    offset = high ? 128.0 / 255.0 : 64.0 / 255.0;
  } else {
    bool high = value >= 64.0 / 1023.0;
    scale = high ? 1023.0 / 2.0 : 1023.0;
    offset = high ? 32.0 / 255.0 : 0.0;
  }
  return trunc(value * scale) * (1.0 / 255.0) + offset;
}

// Texture fetching (dxbc_translator_fetch.cpp).

// XeTextureFetch* flags.
#define XE_FETCH_COMPUTED_LOD 1u  // UseComputedLOD (and not UseRegisterGradients)
#define XE_FETCH_REGISTER_LOD 2u  // UseRegisterLOD
#define XE_FETCH_BASE_MAP 4u      // MipFilter=basemap
#define XE_FETCH_UNNORMALIZED 8u  // UnnormalizedTextureCoords

// The rounding epsilon the translator adds to the offsets (texels).
static const float kXeFetchRoundingOffset = 1.5 / 1024.0;

// The post-swizzle TextureSign of each component (0 unsigned, 1 signed, 2
// unsigned biased, 3 gamma).
uint4 XeTextureSigns(uint fetch) {
  uint signs = xe_texture_swizzled_signs[fetch >> 4][(fetch >> 2) & 3] >> ((fetch & 3) * 8);
  return (signs >> uint4(0, 2, 4, 6)) & 3u;
}

bool XeIsTextureResolutionScaled(uint fetch) {
  return (xe_textures_resolution_scaled & (1u << fetch)) != 0;
}

// Per axis, whether the offsets and coordinates of a texture fetch are scaled
// (if the texture is resolution-scaled, and the translator scales offsets).
bool2 XeScaledFetchAxes(uint fetch) {
  bool scaled = (XeTranslationFlags() & XE_TRANSLATION_SCALED_TEXTURE_OFFSETS) != 0 &&
                XeIsTextureResolutionScaled(fetch);
  return scaled ? (XeDrawResolutionScale() > 1.0) : bool2(false, false);
}

float XeFetchLod(uint fetch, uint flags, float lod_bias, float register_lod) {
  // Fetch constant LOD bias * 32.
  float lod = float(int(XeFetchConstantDword(fetch, 4) << 10) >> 22) * (1.0 / 32.0);
  if (flags & XE_FETCH_REGISTER_LOD) {
    lod += register_lod;
  }
  return lod + lod_bias;
}

// The multiplier of the fetch constant's exponent bias.
float XeFetchExpScale(uint fetch) {
  int exp_adjust = int(XeFetchConstantDword(fetch, 3) << 13) >> 26;
  return asfloat(exp_adjust * 0x00800000 + 0x3F800000);
}

// Signs, gamma and the exponent bias of the fetched result.
float4 XeFetchFinish(uint fetch, uint used_mask, float4 unsigned_value, float4 signed_value) {
  uint4 signs = XeTextureSigns(fetch);
  float4 result = (signs == 1u) ? signed_value : unsigned_value;
  [unroll] for (uint i = 0; i < 4; ++i) {
    if (!(used_mask & (1u << i))) {
      continue;
    }
    if (signs[i] == 2u) {
      result[i] = result[i] * 2.0 - 1.0;
    } else if (signs[i] == 3u) {
      result[i] = XePwlGammaToLinear(result[i]);
    }
  }
  int exp_adjust = int(XeFetchConstantDword(fetch, 3) << 13) >> 26;
  return result * asfloat(exp_adjust * 0x00800000 + 0x3F800000);
}

// Which of the signed and unsigned versions to sample for the used
// components: x - unsigned needed (not all signed), y - signed needed.
bool2 XeFetchSrvsNeeded(uint fetch, uint used_mask) {
  uint4 signs = XeTextureSigns(fetch);
  bool all_signed = true, any_signed = false;
  [unroll] for (uint i = 0; i < 4; ++i) {
    if (used_mask & (1u << i)) {
      all_signed = all_signed && signs[i] == 1u;
      any_signed = any_signed || signs[i] == 1u;
    }
  }
  return bool2(!all_signed, any_signed);
}

float4 XeSample2DArray(uint srv_slot, uint sampler_slot, float3 coords, bool gradients,
                       float2 gradient_x, float2 gradient_y, float lod) {
  uint resource_index = XeDescriptorIndex(srv_slot);
  uint sampler_index = XeDescriptorIndex(sampler_slot);
  if (gradients) {
    return xe_textures_2d[resource_index].SampleGrad(xe_samplers[sampler_index], coords,
                                                     gradient_x, gradient_y);
  }
  return xe_textures_2d[resource_index].SampleLevel(xe_samplers[sampler_index], coords, lod);
}

float4 XeSampleCube(uint srv_slot, uint sampler_slot, float3 coords, bool gradients,
                    float3 gradient_x, float3 gradient_y, float lod) {
  uint resource_index = XeDescriptorIndex(srv_slot);
  uint sampler_index = XeDescriptorIndex(sampler_slot);
  if (gradients) {
    return xe_textures_cube[resource_index].SampleGrad(xe_samplers[sampler_index], coords,
                                                       gradient_x, gradient_y);
  }
  return xe_textures_cube[resource_index].SampleLevel(xe_samplers[sampler_index], coords, lod);
}

// Width and height of a 2D or cube texture.
float2 XeTextureSize2D(uint fetch) {
  uint dword_2 = XeFetchConstantDword(fetch, 2);
  return float2((uint2(dword_2, dword_2 >> 13) & 0x1FFFu) + 1u);
}

// Normalized coordinates with the offsets (texels, with the rounding epsilon)
// applied - x and y.
float2 XeFetchCoords2D(uint fetch, float2 coords, float2 offsets, float2 size, uint flags) {
  bool2 scaled_axes = XeScaledFetchAxes(fetch);
  if (flags & XE_FETCH_UNNORMALIZED) {
    float2 scaled_offsets = scaled_axes ? offsets / XeDrawResolutionScale() : offsets;
    return (coords + scaled_offsets) / size;
  }
  float2 normalized_offsets = offsets / size;
  return scaled_axes ? normalized_offsets / XeDrawResolutionScale() + coords
                     : coords + normalized_offsets;
}

float4 XeTextureFetch2D(uint fetch, uint sampler_slot, uint srv_unsigned_slot,
                        uint srv_signed_slot, uint used_mask, float2 coords, float2 offsets,
                        uint flags, float lod_bias, float register_lod) {
  float2 size = XeTextureSize2D(fetch);
  float3 array_coords = float3(XeFetchCoords2D(fetch, coords, offsets, size, flags), 0.0);
  float lod = 0.0;
  bool gradients = false;
  float2 gradient_x = 0.0, gradient_y = 0.0;
  if (!(flags & XE_FETCH_BASE_MAP)) {
    lod = XeFetchLod(fetch, flags, lod_bias, register_lod);
    if (flags & XE_FETCH_COMPUTED_LOD) {
      float gradient_scale = exp2(lod);
      gradient_x = ddx_coarse(array_coords.xy) * gradient_scale;
      gradient_y = ddy_coarse(array_coords.xy) * gradient_scale;
      gradients = true;
    }
  }
  bool2 srvs = XeFetchSrvsNeeded(fetch, used_mask);
  float4 unsigned_value = 0.0, signed_value = 0.0;
  if (srvs.x) {
    unsigned_value = XeSample2DArray(srv_unsigned_slot, sampler_slot, array_coords, gradients,
                                     gradient_x, gradient_y, lod);
  }
  if (srvs.y) {
    signed_value = XeSample2DArray(srv_signed_slot, sampler_slot, array_coords, gradients,
                                   gradient_x, gradient_y, lod);
  }
  return XeFetchFinish(fetch, used_mask, unsigned_value, signed_value);
}

float4 XeTextureFetch1D(uint fetch, uint sampler_slot, uint srv_unsigned_slot,
                        uint srv_signed_slot, uint used_mask, float coord, float offset,
                        uint flags, float lod_bias, float register_lod) {
  float width = float((XeFetchConstantDword(fetch, 2) & 0xFFFFFFu) + 1u);
  bool scaled = XeScaledFetchAxes(fetch).x;
  float normalized;
  if (flags & XE_FETCH_UNNORMALIZED) {
    normalized = (coord + (scaled ? offset / XeDrawResolutionScale().x : offset)) / width;
  } else {
    normalized = scaled ? (offset / width) / XeDrawResolutionScale().x + coord
                        : coord + offset / width;
  }
  float3 array_coords = float3(normalized, 0.0, 0.0);
  float lod = 0.0;
  bool gradients = false;
  float2 gradient_x = 0.0, gradient_y = 0.0;
  if (!(flags & XE_FETCH_BASE_MAP)) {
    lod = XeFetchLod(fetch, flags, lod_bias, register_lod);
    if (flags & XE_FETCH_COMPUTED_LOD) {
      float gradient_scale = exp2(lod);
      gradient_x = float2(ddx_coarse(normalized) * gradient_scale, 0.0);
      gradient_y = float2(ddy_coarse(normalized) * gradient_scale, 0.0);
      gradients = true;
    }
  }
  bool2 srvs = XeFetchSrvsNeeded(fetch, used_mask);
  float4 unsigned_value = 0.0, signed_value = 0.0;
  if (srvs.x) {
    unsigned_value = XeSample2DArray(srv_unsigned_slot, sampler_slot, array_coords, gradients,
                                     gradient_x, gradient_y, lod);
  }
  if (srvs.y) {
    signed_value = XeSample2DArray(srv_signed_slot, sampler_slot, array_coords, gradients,
                                   gradient_x, gradient_y, lod);
  }
  return XeFetchFinish(fetch, used_mask, unsigned_value, signed_value);
}

// coords: the major axis S and T coordinates + 1 (1...2) in x and y, the face
// index in z (as the shader computes them with `cube`).
float4 XeTextureFetchCube(uint fetch, uint sampler_slot, uint srv_unsigned_slot,
                          uint srv_signed_slot, uint used_mask, float3 coords, float3 offsets,
                          uint flags, float lod_bias, float register_lod) {
  float2 size = XeTextureSize2D(fetch);
  float2 st = XeFetchCoords2D(fetch, coords.xy, offsets.xy, size, flags) * 2.0 - 3.0;
  uint face = min(uint(coords.z + offsets.z), 5u);
  bool negative = (face & 1u) != 0;
  float3 direction;
  switch (face >> 1) {
    case 0u:
      direction = float3(negative ? -1.0 : 1.0, -st.y, negative ? st.x : -st.x);
      break;
    case 1u:
      direction = float3(st.x, negative ? -1.0 : 1.0, negative ? -st.y : st.y);
      break;
    default:
      direction = float3(negative ? -st.x : st.x, -st.y, negative ? -1.0 : 1.0);
      break;
  }
  float lod = 0.0;
  bool gradients = false;
  float3 gradient_x = 0.0, gradient_y = 0.0;
  if (!(flags & XE_FETCH_BASE_MAP)) {
    lod = XeFetchLod(fetch, flags, lod_bias, register_lod);
    if (flags & XE_FETCH_COMPUTED_LOD) {
      float gradient_scale = exp2(lod);
      gradient_x = ddx_coarse(direction) * gradient_scale;
      gradient_y = ddy_coarse(direction) * gradient_scale;
      gradients = true;
    }
  }
  bool2 srvs = XeFetchSrvsNeeded(fetch, used_mask);
  float4 unsigned_value = 0.0, signed_value = 0.0;
  if (srvs.x) {
    unsigned_value = XeSampleCube(srv_unsigned_slot, sampler_slot, direction, gradients,
                                  gradient_x, gradient_y, lod);
  }
  if (srvs.y) {
    signed_value = XeSampleCube(srv_signed_slot, sampler_slot, direction, gradients, gradient_x,
                                gradient_y, lod);
  }
  return XeFetchFinish(fetch, used_mask, unsigned_value, signed_value);
}

// getWeights2D: the bilinear lerp factors (offsets include the -0.5 and the
// rounding epsilon).
float4 XeGetWeights2D(uint fetch, float2 coords, float2 offsets, uint flags) {
  float2 size = XeTextureSize2D(fetch);
  bool2 scaled = XeScaledFetchAxes(fetch);
  if (flags & XE_FETCH_UNNORMALIZED) {
    coords = scaled ? coords * XeDrawResolutionScale() : coords;
    return float4(frac(coords + offsets), 0.0, 0.0);
  }
  size = scaled ? size * XeDrawResolutionScale() : size;
  return float4(frac(coords * size + offsets), 0.0, 0.0);
}

// Pixel shader parameters (PsParamGen): the pixel position (back face in the
// sign of X, line in the sign of Z).
float4 XePsParamGen(float4 position, bool is_front_face) {
  float2 pixel = abs(floor(position.xy) / XeDrawResolutionScale());
  float4 param_gen = float4(pixel, 0.0, 0.0);
  if ((xe_flags & XE_SYS_FLAG_PRIMITIVE_POLYGONAL) && !is_front_face) {
    param_gen.x = asfloat(asuint(param_gen.x) | 0x80000000u);
  }
  param_gen.z = asfloat(((xe_flags >> XE_SYS_FLAG_PRIMITIVE_LINE_SHIFT) & 1u) << 31);
  return param_gen;
}

// Epilogue (dxbc_translator_om.cpp, render target path).

// Alpha test - discards the pixel if it fails.
void XeAlphaTest(float alpha) {
  uint function = (xe_flags >> XE_SYS_FLAG_ALPHA_PASS_IF_LESS_SHIFT) & 7u;
  if (function == 7u) {
    return;
  }
  float reference = xe_alpha_test_reference;
  bool fuzzy = (XeTranslationFlags() & XE_TRANSLATION_FUZZY_ALPHA_EPSILON) != 0;
  bool passed;
  if (function == 5u) {
    // Not equal (true for NaN).
    passed = fuzzy ? !(abs(alpha - reference) < 1.0e-3) : alpha != reference;
  } else {
    bool less = fuzzy ? alpha - 1.0e-3 < reference : alpha < reference;
    bool equal = fuzzy ? abs(alpha - reference) < 1.0e-3 : alpha == reference;
    bool greater = fuzzy ? reference < alpha + 1.0e-3 : reference < alpha;
    passed = ((function & 1u) && less) || ((function & 2u) && equal) ||
             ((function & 4u) && greater);
  }
  if (!passed) {
    discard;
  }
}

// Alpha to coverage - discards the pixel if no samples are covered.
uint XeAlphaToCoverage(float alpha, float4 position) {
  uint coverage = 0xFFFFFFFFu;
  if (xe_alpha_to_mask) {
    uint2 pixel = uint2(position.xy);
    uint offset_index = (pixel.y & 1u) | ((pixel.x & 1u) << 1);
    float offset = float((xe_alpha_to_mask >> (offset_index * 2u)) & 3u);
    coverage = 0u;
    if (xe_sample_count_log2.y) {
      if (xe_sample_count_log2.x) {
        coverage |= (alpha >= 0.75 - offset * (1.0 / 16.0)) ? 1u : 0u;
        coverage |= (alpha >= 0.25 - offset * (1.0 / 16.0)) ? 2u : 0u;
        coverage |= (alpha >= 0.5 - offset * (1.0 / 16.0)) ? 4u : 0u;
        coverage |= (alpha >= 1.0 - offset * (1.0 / 16.0)) ? 8u : 0u;
      } else {
        // Native 2x: top is 1, bottom is 0; 2x as 4x: top is 0, bottom is 3.
        bool msaa_2x = (XeTranslationFlags() & XE_TRANSLATION_MSAA_2X_SUPPORTED) != 0;
        coverage |= (alpha >= 0.5 - offset * (1.0 / 8.0)) ? (msaa_2x ? 2u : 1u) : 0u;
        coverage |= (alpha >= 1.0 - offset * (1.0 / 8.0)) ? (msaa_2x ? 1u : 8u) : 0u;
      }
    } else {
      coverage = (alpha >= 1.0 - offset * (1.0 / 4.0)) ? 1u : 0u;
    }
    if (!coverage) {
      discard;
    }
  }
  return coverage;
}

// The exponent bias and, for gamma render targets stored as linear, the
// conversion to gamma.
float4 XeColorOutput(float4 color, uint target) {
  color *= xe_color_exp_bias[target];
  if ((XeTranslationFlags() & XE_TRANSLATION_GAMMA_RT_AS_UNORM8) &&
      (xe_flags & (1u << (XE_SYS_FLAG_CONVERT_COLOR_0_TO_GAMMA_SHIFT + target)))) {
    color.rgb = saturate(color.rgb);
    color.r = XeLinearToPwlGamma(color.r);
    color.g = XeLinearToPwlGamma(color.g);
    color.b = XeLinearToPwlGamma(color.b);
  }
  return color;
}

#endif  // XENOS_D3D12_HLSLI_
