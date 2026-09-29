/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license.                                          *
 * See backends/metal/licenses/XeniOS-LICENSE for the full terms.             *
 ******************************************************************************
 */

#include "rex/graphics/metal/metal_stage_compile_cache.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>

#include "metal_irconverter.h"

#include "xxhash.h"
#include "rex/logging.h"
#include "rex/graphics/metal/metal_shader_cache.h"

namespace rex {
namespace graphics {
namespace metal {

namespace {

uint64_t HashString(const std::string& value) {
  return value.empty() ? 0 : XXH3_64bits(value.data(), value.size());
}

const char* StageName(uint32_t stage) {
  switch (MetalShaderStage(stage)) {
    case MetalShaderStage::kVertex:
      return "vertex";
    case MetalShaderStage::kFragment:
      return "fragment";
    case MetalShaderStage::kGeometry:
      return "geometry";
    case MetalShaderStage::kCompute:
      return "compute";
    case MetalShaderStage::kHull:
      return "hull";
    case MetalShaderStage::kDomain:
      return "domain";
    default:
      return "unknown";
  }
}

void HashInputLayout(const IRVersionedInputLayoutDescriptor& layout,
                     uint64_t* out_low, uint64_t* out_high) {
  if (!out_low || !out_high) {
    return;
  }
  XXH3_state_t* state = XXH3_createState();
  if (!state) {
    *out_low = 0;
    *out_high = 0;
    return;
  }
  XXH3_128bits_reset(state);
  XXH3_128bits_update(state, &layout.version, sizeof(layout.version));
  if (layout.version == IRInputLayoutDescriptorVersion_1) {
    const auto& desc = layout.desc_1_0;
    XXH3_128bits_update(state, &desc.numElements, sizeof(desc.numElements));
    const uint32_t count = std::min<uint32_t>(desc.numElements, 31);
    for (uint32_t i = 0; i < count; ++i) {
      const char* semantic = desc.semanticNames[i];
      uint32_t semantic_size = semantic ? uint32_t(std::strlen(semantic)) : 0;
      XXH3_128bits_update(state, &semantic_size, sizeof(semantic_size));
      if (semantic_size) {
        XXH3_128bits_update(state, semantic, semantic_size);
      }
      XXH3_128bits_update(state, &desc.inputElementDescs[i],
                          sizeof(desc.inputElementDescs[i]));
    }
  }
  XXH128_hash_t hash = XXH3_128bits_digest(state);
  XXH3_freeState(state);
  *out_low = hash.low64;
  *out_high = hash.high64;
}

void AtomicMax(std::atomic<uint64_t>& target, uint64_t value) {
  uint64_t current = target.load(std::memory_order_relaxed);
  while (current < value && !target.compare_exchange_weak(
                                current, value, std::memory_order_relaxed,
                                std::memory_order_relaxed)) {
  }
}

}  // namespace

size_t MetalStageCompileKeyHasher::operator()(
    const MetalStageCompileKey& key) const {
  const auto words = key.SerializeWords();
  return size_t(XXH3_64bits(words.data(), words.size() * sizeof(uint64_t)));
}

std::shared_ptr<const MetalStageCompileResult>
MetalStageCompileCache::PendingStageCompile::Wait() {
  std::unique_lock<std::mutex> lock(mutex_);
  ready_cv_.wait(lock, [this]() { return ready_; });
  return result_;
}

void MetalStageCompileCache::PendingStageCompile::Publish(
    std::shared_ptr<const MetalStageCompileResult> result) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    result_ = std::move(result);
    ready_ = true;
  }
  ready_cv_.notify_all();
}

MetalStageCompileKey MetalStageCompileCache::BuildKey(
    const MetalStageCompileRequest& request) {
  MetalStageCompileKey key = {};
  key.key_version = 2;
  key.stage = uint32_t(request.stage);
  if (request.dxil_data) {
    key.dxil_size = request.dxil_data->size();
    if (!request.dxil_data->empty()) {
      XXH128_hash_t dxil_hash =
          XXH3_128bits(request.dxil_data->data(), request.dxil_data->size());
      key.dxil_hash_low = dxil_hash.low64;
      key.dxil_hash_high = dxil_hash.high64;
    }
  }
  key.compatibility_flags = request.compatibility_flags;
  key.validation_flags = request.validation_flags;
  key.stage_in_generation_mode = request.stage_in_generation_mode;
  key.function_constant_resource_space =
      request.function_constant_resource_space;
  if (request.has_minimum_target) {
    key.minimum_gpu_family = request.minimum_gpu_family;
    key.minimum_os = request.minimum_os;
    key.minimum_os_version_hash = HashString(request.minimum_os_version);
  }
  key.root_signature_abi_version = request.root_signature_abi_version;
  key.root_signature_stage = uint32_t(request.stage);
  key.root_signature_bindless_resources_used =
      request.root_signature_bindless_resources_used ? 1u : 0u;
  key.ignore_root_signature = request.ignore_root_signature ? 1u : 0u;
  key.ignore_debug_information = request.ignore_debug_information ? 1u : 0u;
  key.input_topology = uint32_t(request.input_topology);
  key.geometry_or_tessellation_emulation_enabled =
      request.enable_geometry_emulation ? 1u : 0u;
  key.stage_in_metallib_requested = request.RequestsStageIn() ? 1u : 0u;
  if (request.RequestsStageIn()) {
    if (request.input_layout) {
      HashInputLayout(*request.input_layout, &key.input_layout_hash_low,
                      &key.input_layout_hash_high);
    } else {
      key.input_layout_hash_low = request.stage_in_layout_key;
      key.input_layout_hash_high = 0;
    }
  }
  return key;
}

std::shared_ptr<const MetalStageCompileResult>
MetalStageCompileCache::GetOrCompile(const MetalStageCompileRequest& request,
                                     CompileFn compile_fn) {
  requests_.fetch_add(1, std::memory_order_relaxed);
  if (request.dxil_data) {
    dxil_bytes_.fetch_add(request.dxil_data->size(), std::memory_order_relaxed);
  }

  MetalStageCompileKey key = BuildKey(request);
  std::shared_ptr<PendingStageCompile> pending;
  bool owner = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(key);
    if (it != entries_.end()) {
      pending = it->second;
      memory_hits_.fetch_add(1, std::memory_order_relaxed);
    } else {
      pending = std::make_shared<PendingStageCompile>();
      entries_.emplace(key, pending);
      owner = true;
      memory_misses_.fetch_add(1, std::memory_order_relaxed);
    }
  }

  if (!owner) {
    waits_.fetch_add(1, std::memory_order_relaxed);
    auto wait_start = std::chrono::steady_clock::now();
    auto result = pending->Wait();
    auto wait_end = std::chrono::steady_clock::now();
    AddWaitTime(uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                             wait_end - wait_start)
                             .count()));
    return result;
  }

  if (std::shared_ptr<const MetalStageCompileResult> persistent_result =
          LoadPersistent(key)) {
    metallib_bytes_.fetch_add(persistent_result->metallib_data.size(),
                              std::memory_order_relaxed);
    pending->Publish(persistent_result);
    return persistent_result;
  }

  auto compile_start = std::chrono::steady_clock::now();
  std::shared_ptr<const MetalStageCompileResult> result = compile_fn();
  auto compile_end = std::chrono::steady_clock::now();
  AddOwnerCompileTime(
      uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                   compile_end - compile_start)
                   .count()));

  if (result) {
    metallib_bytes_.fetch_add(result->metallib_data.size(),
                              std::memory_order_relaxed);
  }
  if (!result || !result->success) {
    failures_.fetch_add(1, std::memory_order_relaxed);
  }

  pending->Publish(result);

  if (result && result->success) {
    StorePersistent(key, *result);
  }

  if (!result || !result->success) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(key);
    if (it != entries_.end() && it->second == pending) {
      entries_.erase(it);
    }
  }

  return result;
}

std::shared_ptr<const MetalStageCompileResult>
MetalStageCompileCache::LoadPersistent(
    const MetalStageCompileRequest& request) {
  return LoadPersistent(BuildKey(request));
}

std::shared_ptr<const MetalStageCompileResult>
MetalStageCompileCache::LoadPersistent(const MetalStageCompileKey& key) {
  if (!g_metal_artifact_store || !g_metal_artifact_store->IsInitialized()) {
    return nullptr;
  }
  MetalStageCompileResult result;
  if (!g_metal_artifact_store->LoadStageCompile(key, &result)) {
    persistent_misses_.fetch_add(1, std::memory_order_relaxed);
    uint64_t log_index =
        persistent_miss_log_count_.fetch_add(1, std::memory_order_relaxed);
    if (log_index < 64) {
      REXLOG_INFO(
          "MetalStageCompileCache: persistent miss #{} stage={} "
          "dxil={:016X}{:016X}/{} root_abi={} compat={:#x} "
          "validation={:#x} stage_in_mode={:#x} min_target={}/{}:{:016X} "
          "topology={} geom_tess={} stage_in={} layout={:016X}{:016X}",
          log_index + 1, StageName(key.stage), key.dxil_hash_high,
          key.dxil_hash_low, key.dxil_size, key.root_signature_abi_version,
          key.compatibility_flags, key.validation_flags,
          key.stage_in_generation_mode, key.minimum_gpu_family, key.minimum_os,
          key.minimum_os_version_hash, key.input_topology,
          key.geometry_or_tessellation_emulation_enabled,
          key.stage_in_metallib_requested, key.input_layout_hash_high,
          key.input_layout_hash_low);
    }
    return nullptr;
  }
  persistent_hits_.fetch_add(1, std::memory_order_relaxed);
  return std::make_shared<MetalStageCompileResult>(std::move(result));
}

void MetalStageCompileCache::StorePersistent(
    const MetalStageCompileKey& key, const MetalStageCompileResult& result) {
  if (!result.success || !g_metal_artifact_store ||
      !g_metal_artifact_store->IsInitialized()) {
    return;
  }
  g_metal_artifact_store->StoreStageCompile(key, result);
}

void MetalStageCompileCache::AddOwnerCompileTime(uint64_t ms) {
  owner_compile_ms_total_.fetch_add(ms, std::memory_order_relaxed);
  AtomicMax(owner_compile_ms_max_, ms);
}

void MetalStageCompileCache::AddWaitTime(uint64_t ms) {
  wait_ms_total_.fetch_add(ms, std::memory_order_relaxed);
  AtomicMax(wait_ms_max_, ms);
}

MetalStageCompileCacheStats MetalStageCompileCache::GetStats() const {
  MetalStageCompileCacheStats stats;
  stats.requests = requests_.load(std::memory_order_relaxed);
  stats.memory_hits = memory_hits_.load(std::memory_order_relaxed);
  stats.memory_misses = memory_misses_.load(std::memory_order_relaxed);
  stats.waits = waits_.load(std::memory_order_relaxed);
  stats.failures = failures_.load(std::memory_order_relaxed);
  stats.persistent_hits = persistent_hits_.load(std::memory_order_relaxed);
  stats.persistent_misses = persistent_misses_.load(std::memory_order_relaxed);
  stats.dxil_bytes = dxil_bytes_.load(std::memory_order_relaxed);
  stats.metallib_bytes = metallib_bytes_.load(std::memory_order_relaxed);
  stats.owner_compile_ms_total =
      owner_compile_ms_total_.load(std::memory_order_relaxed);
  stats.owner_compile_ms_max =
      owner_compile_ms_max_.load(std::memory_order_relaxed);
  stats.wait_ms_total = wait_ms_total_.load(std::memory_order_relaxed);
  stats.wait_ms_max = wait_ms_max_.load(std::memory_order_relaxed);
  return stats;
}

MetalStageCompileCacheStats MetalStageCompileCache::GetAndResetStats() {
  MetalStageCompileCacheStats stats;
  stats.requests = requests_.exchange(0, std::memory_order_relaxed);
  stats.memory_hits = memory_hits_.exchange(0, std::memory_order_relaxed);
  stats.memory_misses = memory_misses_.exchange(0, std::memory_order_relaxed);
  stats.waits = waits_.exchange(0, std::memory_order_relaxed);
  stats.failures = failures_.exchange(0, std::memory_order_relaxed);
  stats.persistent_hits =
      persistent_hits_.exchange(0, std::memory_order_relaxed);
  stats.persistent_misses =
      persistent_misses_.exchange(0, std::memory_order_relaxed);
  stats.dxil_bytes = dxil_bytes_.exchange(0, std::memory_order_relaxed);
  stats.metallib_bytes = metallib_bytes_.exchange(0, std::memory_order_relaxed);
  stats.owner_compile_ms_total =
      owner_compile_ms_total_.exchange(0, std::memory_order_relaxed);
  stats.owner_compile_ms_max =
      owner_compile_ms_max_.exchange(0, std::memory_order_relaxed);
  stats.wait_ms_total = wait_ms_total_.exchange(0, std::memory_order_relaxed);
  stats.wait_ms_max = wait_ms_max_.exchange(0, std::memory_order_relaxed);
  return stats;
}

}  // namespace metal
}  // namespace gpu
}  // namespace xe
