// Modern lighting for the material shaders: GGX specular with Fresnel, and
// the sampling patterns of the soft shadows.

#ifndef MATERIAL_LIGHTING_HLSLI_
#define MATERIAL_LIGHTING_HLSLI_

static const float kMaterialPi = 3.14159265;

float MaterialPow5(float x) {
  float x2 = x * x;
  return x2 * x2 * x;
}

float3 FresnelSchlick(float3 f0, float cos_theta) {
  return f0 + (1.0 - f0) * MaterialPow5(saturate(1.0 - cos_theta));
}

// For image-based lighting: rough surfaces reflect less at grazing angles.
float3 FresnelSchlickRoughness(float3 f0, float cos_theta, float roughness) {
  return f0 + (max(1.0 - roughness, f0) - f0) * MaterialPow5(saturate(1.0 - cos_theta));
}

// Trowbridge-Reitz (GGX) normal distribution, alpha = roughness^2.
float DistributionGgx(float n_dot_h, float alpha) {
  float alpha2 = alpha * alpha;
  float d = n_dot_h * n_dot_h * (alpha2 - 1.0) + 1.0;
  return alpha2 / (kMaterialPi * d * d);
}

// Height-correlated Smith visibility G / (4 N.L N.V), approximated (Hammon).
float VisibilitySmithGgx(float n_dot_l, float n_dot_v, float alpha) {
  float l = n_dot_v * (n_dot_l * (1.0 - alpha) + alpha);
  float v = n_dot_l * (n_dot_v * (1.0 - alpha) + alpha);
  return 0.5 / max(l + v, 1.0e-5);
}

// Specular reflectance times N.L of light from direction l (toward the
// light) seen from direction v (toward the eye).
float3 SpecularGgx(float3 n, float3 v, float3 l, float3 f0, float roughness) {
  float n_dot_l = saturate(dot(n, l));
  float n_dot_v = max(dot(n, v), 1.0e-4);
  float3 h = normalize(l + v);
  float n_dot_h = saturate(dot(n, h));
  float v_dot_h = saturate(dot(v, h));
  float alpha = max(roughness * roughness, 2.0e-3);
  return DistributionGgx(n_dot_h, alpha) * VisibilitySmithGgx(n_dot_l, n_dot_v, alpha) *
         FresnelSchlick(f0, v_dot_h) * n_dot_l;
}

// Interleaved gradient noise (Jimenez 2014) - a per-pixel rotation that the
// eye reads as smooth.
float InterleavedGradientNoise(float2 pixel) {
  return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

float2 Rotate(float2 offset, float2 cos_sin) {
  return float2(offset.x * cos_sin.x - offset.y * cos_sin.y,
                offset.x * cos_sin.y + offset.y * cos_sin.x);
}

// A 16-point Poisson disk in the unit circle.
static const float2 kPoissonDisk16[16] = {
    float2(-0.94201624, -0.39906216), float2(0.94558609, -0.76890725),
    float2(-0.09418410, -0.92938870), float2(0.34495938, 0.29387760),
    float2(-0.91588581, 0.45771432), float2(-0.81544232, -0.87912464),
    float2(-0.38277543, 0.27676845), float2(0.97484398, 0.75648379),
    float2(0.44323325, -0.97511554), float2(0.53742981, -0.47373420),
    float2(-0.26496911, -0.41893023), float2(0.79197514, 0.19090188),
    float2(-0.24188840, 0.99706507), float2(-0.81409955, 0.91437590),
    float2(0.19984126, 0.78641367), float2(0.14383161, -0.14100790),
};

#endif  // MATERIAL_LIGHTING_HLSLI_
