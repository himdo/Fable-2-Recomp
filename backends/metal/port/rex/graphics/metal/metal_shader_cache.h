/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license.                                          *
 * See backends/metal/licenses/XeniOS-LICENSE for the full terms.             *
 ******************************************************************************
 */

#ifndef XENIA_GPU_METAL_METAL_SHADER_CACHE_H_
#define XENIA_GPU_METAL_METAL_SHADER_CACHE_H_

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "rex/graphics/metal/metal_stage_compile_cache.h"

namespace rex {
namespace graphics {
namespace metal {

struct MetalDxilBytecodeKey {
  static constexpr size_t kSerializedWordCount = 7;

  uint32_t key_version = 1;
  uint64_t dxbc_size = 0;
  uint64_t dxbc_hash_low = 0;
  uint64_t dxbc_hash_high = 0;
  uint64_t converter_options_hash = 0;
  uint32_t converter_version = 0;
  uint32_t reserved = 0;

  std::array<uint64_t, kSerializedWordCount> SerializeWords() const {
    return {{
        key_version,
        dxbc_size,
        dxbc_hash_low,
        dxbc_hash_high,
        converter_options_hash,
        converter_version,
        reserved,
    }};
  }
};

class MetalArtifactStore {
 public:
  enum class ArtifactKind : uint32_t {
    kStageCompile = 3,
    kDxilBytecode = 4,
  };

  struct ArtifactKey {
    uint64_t primary_hash = 0;
    uint64_t modification = 0;
    uint32_t stage = 0;
    ArtifactKind kind = ArtifactKind::kStageCompile;

    bool operator==(const ArtifactKey& other) const {
      return primary_hash == other.primary_hash &&
             modification == other.modification && stage == other.stage &&
             kind == other.kind;
    }
  };

  struct ArtifactKeyHasher {
    size_t operator()(const ArtifactKey& key) const;
  };

  struct Stats {
    size_t entry_count = 0;
    size_t total_payload_bytes = 0;
  };

  MetalArtifactStore() = default;
  ~MetalArtifactStore() = default;

  void Initialize(const std::filesystem::path& artifact_path);
  void Shutdown();

  bool IsInitialized() const { return initialized_; }
  const std::filesystem::path& artifact_path() const { return artifact_path_; }

  static constexpr uint32_t kStorageVersion = 3;

  static ArtifactKey MakeStageCompileKey(const MetalStageCompileKey& stage_key);
  static ArtifactKey MakeDxilBytecodeKey(const MetalDxilBytecodeKey& dxil_key);
  bool LoadStageCompile(const MetalStageCompileKey& key,
                        MetalStageCompileResult* out);
  void StoreStageCompile(const MetalStageCompileKey& key,
                         const MetalStageCompileResult& result);
  bool LoadDxilBytecode(const MetalDxilBytecodeKey& key,
                        std::vector<uint8_t>* out);
  void StoreDxilBytecode(const MetalDxilBytecodeKey& key,
                         const std::vector<uint8_t>& dxil_data);

  Stats GetStats() const;

 private:
  struct RecordLocation {
    uint64_t payload_offset = 0;
    uint32_t payload_size = 0;
    uint64_t payload_hash = 0;
  };

  bool LoadIndex();
  bool ReadPayload(const RecordLocation& location, std::vector<uint8_t>* out);
  void StorePayload(const ArtifactKey& key, const uint8_t* payload,
                    size_t payload_size);

  mutable std::mutex mutex_;
  bool initialized_ = false;
  std::filesystem::path artifact_path_;
  FILE* artifact_file_ = nullptr;
  std::unordered_map<ArtifactKey, RecordLocation, ArtifactKeyHasher> index_;
};

extern std::unique_ptr<MetalArtifactStore> g_metal_artifact_store;

}  // namespace metal
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_METAL_METAL_SHADER_CACHE_H_
