/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license.                                          *
 * See backends/metal/licenses/XeniOS-LICENSE for the full terms.             *
 ******************************************************************************
 */

#include "rex/graphics/metal/metal_shader.h"

#include <dispatch/dispatch.h>
#include <inttypes.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>

#ifndef DISPATCH_DATA_DESTRUCTOR_NONE
#define DISPATCH_DATA_DESTRUCTOR_NONE DISPATCH_DATA_DESTRUCTOR_DEFAULT
#endif

#include "rex/assert.h"
#include "rex/filesystem.h"
#include "rex/logging.h"
#include "rex/math.h"
#include "rex/string.h"
#include "rex/graphics/pipeline/shader/dxbc.h"
#include "rex/graphics/flags.h"
#include "rex/graphics/metal/dxbc_to_dxil_converter.h"
#include "rex/graphics/metal/metal_shader_converter.h"
#include "rex/ui/metal/metal_api.h"

namespace rex {
namespace graphics {
namespace metal {

namespace {

constexpr uint32_t kAllTranslatedCbvMask =
    (uint32_t(1)
     << (uint32_t(DxbcShaderTranslator::CbufferRegister::kDescriptorIndices) +
         1)) -
    1;
constexpr uint32_t kDescriptorIndicesCbvSizeBytes = 4096;

void MarkFetchConstantDword(DxbcShader::FetchConstantDwordMask& mask,
                            uint32_t dword_index) {
  if (dword_index >= DxbcShader::kFetchConstantDwordCount) {
    assert_always();
    return;
  }
  mask[dword_index >> 5] |= uint32_t(1) << (dword_index & 31);
}

void MarkVertexFetchConstant(DxbcShader::FetchConstantDwordMask& mask,
                             uint32_t fetch_constant_index) {
  if (fetch_constant_index >= xenos::kVertexFetchConstantCount) {
    assert_always();
    return;
  }
  const uint32_t dword_index = fetch_constant_index * 2;
  MarkFetchConstantDword(mask, dword_index);
  MarkFetchConstantDword(mask, dword_index + 1);
}

void MarkTextureFetchConstant(DxbcShader::FetchConstantDwordMask& mask,
                              uint32_t fetch_constant_index) {
  if (fetch_constant_index >= xenos::kTextureFetchConstantCount) {
    assert_always();
    return;
  }
  const uint32_t dword_index = fetch_constant_index * 6;
  for (uint32_t i = 0; i < 6; ++i) {
    MarkFetchConstantDword(mask, dword_index + i);
  }
}

}  // namespace

MetalShader::MetalShader(xenos::ShaderType shader_type,
                         uint64_t ucode_data_hash, const uint32_t* ucode_dwords,
                         size_t ucode_dword_count,
                         std::endian ucode_source_endian)
    : DxbcShader(shader_type, ucode_data_hash, ucode_dwords, ucode_dword_count,
                 ucode_source_endian) {}

const MetalShader::DrawConstantMetadata& MetalShader::GetDrawConstantMetadata()
    const {
  std::call_once(draw_constant_metadata_once_, [this]() {
    DrawConstantMetadata metadata = {};

    const Shader::ConstantRegisterMap& constant_map = constant_register_map();
    for (uint32_t i = 0; i < rex::countof(constant_map.vertex_fetch_bitmap);
         ++i) {
      uint32_t vfetch_bits_remaining = constant_map.vertex_fetch_bitmap[i];
      uint32_t bit_index;
      while (rex::bit_scan_forward(vfetch_bits_remaining, &bit_index)) {
        vfetch_bits_remaining = rex::clear_lowest_bit(vfetch_bits_remaining);
        MarkVertexFetchConstant(metadata.shader_fetch_constant_dword_mask,
                                i * 32 + bit_index);
      }
    }

    for (const Shader::VertexBinding& binding : vertex_bindings()) {
      MarkVertexFetchConstant(metadata.shader_fetch_constant_dword_mask,
                              binding.fetch_constant);
    }
    for (const Shader::TextureBinding& binding : texture_bindings()) {
      MarkTextureFetchConstant(metadata.shader_fetch_constant_dword_mask,
                               binding.fetch_constant);
    }

    constexpr uint32_t kMaxDescriptorIndexWords =
        kDescriptorIndicesCbvSizeBytes / sizeof(uint32_t);
    metadata.descriptor_indices_word_count = 1;
    for (const DxbcShader::TextureBinding& binding :
         GetTextureBindingsAfterTranslation()) {
      assert_true(binding.bindless_descriptor_index < kMaxDescriptorIndexWords);
      if (binding.bindless_descriptor_index < kMaxDescriptorIndexWords) {
        metadata.descriptor_indices_word_count =
            std::max(metadata.descriptor_indices_word_count,
                     binding.bindless_descriptor_index + 1);
      }
    }
    for (const DxbcShader::SamplerBinding& binding :
         GetSamplerBindingsAfterTranslation()) {
      assert_true(binding.bindless_descriptor_index < kMaxDescriptorIndexWords);
      if (binding.bindless_descriptor_index < kMaxDescriptorIndexWords) {
        metadata.descriptor_indices_word_count =
            std::max(metadata.descriptor_indices_word_count,
                     binding.bindless_descriptor_index + 1);
      }
    }

    const uint32_t used_cbuffer_mask = GetUsedCbufferMaskAfterTranslation();
    metadata.active_cbv_mask = used_cbuffer_mask
                                   ? (used_cbuffer_mask & kAllTranslatedCbvMask)
                                   : kAllTranslatedCbvMask;

    draw_constant_metadata_ = metadata;
  });
  return draw_constant_metadata_;
}

MetalShader::MetalTranslation::~MetalTranslation() {
  if (metal_function_) {
    metal_function_->release();
    metal_function_ = nullptr;
  }
  if (metal_library_) {
    metal_library_->release();
    metal_library_ = nullptr;
  }
}

bool MetalShader::MetalTranslation::InstallMetal(
    MTL::Device* device, const MetalStageCompileResult& result,
    uint64_t* out_new_library_ms, bool* out_new_library_created) {
  if (out_new_library_ms) {
    *out_new_library_ms = 0;
  }
  if (out_new_library_created) {
    *out_new_library_created = false;
  }
  if (!device) {
    REXLOG_ERROR("MetalShader: No Metal device provided");
    return false;
  }
  std::lock_guard<std::mutex> lock(metal_translation_mutex_);
  if (metal_function_) {
    return true;
  }
  if (!result.success || result.metallib_data.empty()) {
    REXLOG_ERROR("MetalShader: invalid stage compile result: {}",
           result.error_message);
    return false;
  }
  function_name_ = result.function_name;
  metallib_data_ = result.metallib_data;

  // Debug: Dump shader artifacts (DXBC, DXIL, MetalLib) to files when enabled.
  static std::atomic<int> shader_dump_counter{0};
  if (!REXCVAR_GET(dump_shaders).empty()) {
    std::filesystem::path base_dir = std::filesystem::path(REXCVAR_GET(dump_shaders)) / "metal_shaders";

    char filename[128];
    const char* type_str =
        (shader().type() == xenos::ShaderType::kVertex) ? "vs" : "ps";
    int counter = shader_dump_counter++;

    // Dump DXBC (translated binary from DXBC translator)
    const auto& dxbc_data = translated_binary();
    if (!dxbc_data.empty()) {
      snprintf(filename, sizeof(filename), "shader_%d_%s.dxbc", counter,
               type_str);
      std::filesystem::path dxbc_path = base_dir / filename;
      rex::filesystem::CreateParentFolder(dxbc_path);
      FILE* f = rex::filesystem::OpenFile(dxbc_path, "wb");
      if (f) {
        fwrite(dxbc_data.data(), 1, dxbc_data.size(), f);
        fclose(f);
      }
    }

    // Dump DXIL
    if (!dxil_data_.empty()) {
      snprintf(filename, sizeof(filename), "shader_%d_%s.dxil", counter,
               type_str);
      std::filesystem::path dxil_path = base_dir / filename;
      rex::filesystem::CreateParentFolder(dxil_path);
      FILE* f = rex::filesystem::OpenFile(dxil_path, "wb");
      if (f) {
        fwrite(dxil_data_.data(), 1, dxil_data_.size(), f);
        fclose(f);
      }
    }

    // Dump MetalLib
    if (!metallib_data_.empty()) {
      snprintf(filename, sizeof(filename), "shader_%d_%s.metallib", counter,
               type_str);
      std::filesystem::path metallib_path = base_dir / filename;
      rex::filesystem::CreateParentFolder(metallib_path);
      FILE* f = rex::filesystem::OpenFile(metallib_path, "wb");
      if (f) {
        fwrite(metallib_data_.data(), 1, metallib_data_.size(), f);
        fclose(f);
      }
    }
  }

  NS::Error* error = nullptr;
  dispatch_data_t data =
      dispatch_data_create(metallib_data_.data(), metallib_data_.size(),
                           nullptr, DISPATCH_DATA_DESTRUCTOR_NONE);

  auto library_start = std::chrono::steady_clock::now();
  metal_library_ = device->newLibrary(data, &error);
  auto library_end = std::chrono::steady_clock::now();
  if (out_new_library_created) {
    *out_new_library_created = true;
  }
  if (out_new_library_ms) {
    *out_new_library_ms =
        uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                     library_end - library_start)
                     .count());
  }
  dispatch_release(data);

  if (!metal_library_) {
    if (error) {
      REXLOG_ERROR("MetalShader: Failed to create Metal library: {}",
             error->localizedDescription()->utf8String());
    } else {
      REXLOG_ERROR("MetalShader: Failed to create Metal library (unknown error)");
    }
    return false;
  }

  NS::String* function_name =
      NS::String::string(result.function_name.c_str(), NS::UTF8StringEncoding);

  metal_function_ = metal_library_->newFunction(function_name);

  if (!metal_function_) {
    REXLOG_ERROR("MetalShader: Function '{}' not found in metallib", function_name_);
    return false;
  }

  return true;
}

void MetalShader::MetalTranslation::SetDxilData(
    std::vector<uint8_t> dxil_data) {
  std::lock_guard<std::mutex> lock(metal_translation_mutex_);
  dxil_data_ = std::move(dxil_data);
}

std::vector<uint8_t> MetalShader::MetalTranslation::GetDxilDataCopy() const {
  std::lock_guard<std::mutex> lock(metal_translation_mutex_);
  return dxil_data_;
}

Shader::Translation* MetalShader::CreateTranslationInstance(
    uint64_t modification) {
  return new MetalTranslation(*this, modification);
}

}  // namespace metal
}  // namespace gpu
}  // namespace xe
