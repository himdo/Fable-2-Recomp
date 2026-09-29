/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license.                                          *
 * See backends/metal/licenses/XeniOS-LICENSE for the full terms.             *
 ******************************************************************************
 */

#include "rex/graphics/metal/metal_geometry_shader.h"

#include <unordered_map>
#include <utility>
#include <vector>

namespace rex {
namespace graphics {
namespace metal {

// Metal-local cache of generated DXBC geometry shaders (the shared generator in
// xenia/gpu/dxbc_geometry_shader.cc produces the bytes; Metal converts them via
// the Metal Shader Converter at consumption time).
static std::unordered_map<GeometryShaderKey, std::vector<uint32_t>,
                          GeometryShaderKey::Hasher>
    geometry_shaders_;

const std::vector<uint32_t>& GetGeometryShader(GeometryShaderKey key) {
  auto it = geometry_shaders_.find(key);
  if (it != geometry_shaders_.end()) {
    return it->second;
  }
  std::vector<uint32_t> shader;
  CreateDxbcGeometryShader(key, shader);
  return geometry_shaders_.emplace(key, std::move(shader)).first->second;
}

}  // namespace metal
}  // namespace gpu
}  // namespace xe
