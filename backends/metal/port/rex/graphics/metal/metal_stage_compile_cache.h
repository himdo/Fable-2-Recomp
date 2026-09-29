/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license.                                          *
 * See backends/metal/licenses/XeniOS-LICENSE for the full terms.             *
 ******************************************************************************
 */

#ifndef XENIA_GPU_METAL_METAL_STAGE_COMPILE_CACHE_H_
#define XENIA_GPU_METAL_METAL_STAGE_COMPILE_CACHE_H_

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "rex/graphics/metal/metal_shader_converter.h"

namespace rex {
namespace graphics {
namespace metal {

struct MetalStageCompileCacheStats {
  uint64_t requests = 0;
  uint64_t memory_hits = 0;
  uint64_t memory_misses = 0;
  uint64_t waits = 0;
  uint64_t failures = 0;
  uint64_t persistent_hits = 0;
  uint64_t persistent_misses = 0;
  uint64_t dxil_bytes = 0;
  uint64_t metallib_bytes = 0;
  uint64_t owner_compile_ms_total = 0;
  uint64_t owner_compile_ms_max = 0;
  uint64_t wait_ms_total = 0;
  uint64_t wait_ms_max = 0;
};

struct MetalStageCompileKey {
  static constexpr size_t kSerializedWordCount = 23;

  uint32_t key_version = 2;
  uint32_t stage = 0;
  uint64_t dxil_size = 0;
  uint64_t dxil_hash_low = 0;
  uint64_t dxil_hash_high = 0;
  uint32_t compatibility_flags = 0;
  uint32_t validation_flags = 0;
  uint32_t stage_in_generation_mode = 0;
  uint32_t function_constant_resource_space = 0;
  uint32_t minimum_gpu_family = 0;
  uint32_t minimum_os = 0;
  uint64_t minimum_os_version_hash = 0;
  uint32_t root_signature_abi_version = 0;
  uint32_t root_signature_stage = 0;
  uint32_t root_signature_bindless_resources_used = 0;
  uint32_t ignore_root_signature = 0;
  uint32_t ignore_debug_information = 0;
  uint32_t input_topology = 0;
  uint32_t geometry_or_tessellation_emulation_enabled = 0;
  uint32_t stage_in_metallib_requested = 0;
  uint32_t reserved = 0;
  uint64_t input_layout_hash_low = 0;
  uint64_t input_layout_hash_high = 0;

  std::array<uint64_t, kSerializedWordCount> SerializeWords() const {
    return {{
        key_version,
        stage,
        dxil_size,
        dxil_hash_low,
        dxil_hash_high,
        compatibility_flags,
        validation_flags,
        stage_in_generation_mode,
        function_constant_resource_space,
        minimum_gpu_family,
        minimum_os,
        minimum_os_version_hash,
        root_signature_abi_version,
        root_signature_stage,
        root_signature_bindless_resources_used,
        ignore_root_signature,
        ignore_debug_information,
        input_topology,
        geometry_or_tessellation_emulation_enabled,
        stage_in_metallib_requested,
        reserved,
        input_layout_hash_low,
        input_layout_hash_high,
    }};
  }

  bool operator==(const MetalStageCompileKey& other) const {
    return key_version == other.key_version && stage == other.stage &&
           dxil_size == other.dxil_size &&
           dxil_hash_low == other.dxil_hash_low &&
           dxil_hash_high == other.dxil_hash_high &&
           compatibility_flags == other.compatibility_flags &&
           validation_flags == other.validation_flags &&
           stage_in_generation_mode == other.stage_in_generation_mode &&
           function_constant_resource_space ==
               other.function_constant_resource_space &&
           minimum_gpu_family == other.minimum_gpu_family &&
           minimum_os == other.minimum_os &&
           minimum_os_version_hash == other.minimum_os_version_hash &&
           root_signature_abi_version == other.root_signature_abi_version &&
           root_signature_stage == other.root_signature_stage &&
           root_signature_bindless_resources_used ==
               other.root_signature_bindless_resources_used &&
           ignore_root_signature == other.ignore_root_signature &&
           ignore_debug_information == other.ignore_debug_information &&
           input_topology == other.input_topology &&
           geometry_or_tessellation_emulation_enabled ==
               other.geometry_or_tessellation_emulation_enabled &&
           stage_in_metallib_requested == other.stage_in_metallib_requested &&
           reserved == other.reserved &&
           input_layout_hash_low == other.input_layout_hash_low &&
           input_layout_hash_high == other.input_layout_hash_high;
  }
};

struct MetalStageCompileKeyHasher {
  size_t operator()(const MetalStageCompileKey& key) const;
};

class MetalStageCompileCache {
 public:
  using CompileFn =
      std::function<std::shared_ptr<const MetalStageCompileResult>()>;

  MetalStageCompileCache() = default;
  ~MetalStageCompileCache() = default;

  MetalStageCompileCache(const MetalStageCompileCache&) = delete;
  MetalStageCompileCache& operator=(const MetalStageCompileCache&) = delete;

  std::shared_ptr<const MetalStageCompileResult> GetOrCompile(
      const MetalStageCompileRequest& request, CompileFn compile_fn);
  std::shared_ptr<const MetalStageCompileResult> LoadPersistent(
      const MetalStageCompileRequest& request);

  MetalStageCompileCacheStats GetStats() const;
  MetalStageCompileCacheStats GetAndResetStats();

 private:
  class PendingStageCompile {
   public:
    std::shared_ptr<const MetalStageCompileResult> Wait();
    void Publish(std::shared_ptr<const MetalStageCompileResult> result);

   private:
    std::mutex mutex_;
    std::condition_variable ready_cv_;
    bool ready_ = false;
    std::shared_ptr<const MetalStageCompileResult> result_;
  };

  static MetalStageCompileKey BuildKey(const MetalStageCompileRequest& request);
  std::shared_ptr<const MetalStageCompileResult> LoadPersistent(
      const MetalStageCompileKey& key);
  void StorePersistent(const MetalStageCompileKey& key,
                       const MetalStageCompileResult& result);
  void AddOwnerCompileTime(uint64_t ms);
  void AddWaitTime(uint64_t ms);

  mutable std::mutex mutex_;
  std::unordered_map<MetalStageCompileKey, std::shared_ptr<PendingStageCompile>,
                     MetalStageCompileKeyHasher>
      entries_;

  std::atomic<uint64_t> requests_{0};
  std::atomic<uint64_t> memory_hits_{0};
  std::atomic<uint64_t> memory_misses_{0};
  std::atomic<uint64_t> waits_{0};
  std::atomic<uint64_t> failures_{0};
  std::atomic<uint64_t> persistent_hits_{0};
  std::atomic<uint64_t> persistent_misses_{0};
  std::atomic<uint64_t> persistent_miss_log_count_{0};
  std::atomic<uint64_t> dxil_bytes_{0};
  std::atomic<uint64_t> metallib_bytes_{0};
  std::atomic<uint64_t> owner_compile_ms_total_{0};
  std::atomic<uint64_t> owner_compile_ms_max_{0};
  std::atomic<uint64_t> wait_ms_total_{0};
  std::atomic<uint64_t> wait_ms_max_{0};
};

}  // namespace metal
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_METAL_METAL_STAGE_COMPILE_CACHE_H_
