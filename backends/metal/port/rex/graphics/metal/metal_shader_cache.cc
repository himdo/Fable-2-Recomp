/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license.                                          *
 * See backends/metal/licenses/XeniOS-LICENSE for the full terms.             *
 ******************************************************************************
 */

#include "rex/graphics/metal/metal_shader_cache.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>

#include "xxhash.h"
#include "rex/filesystem.h"
#include "rex/logging.h"
#include "rex/hash.h"

namespace rex {
namespace graphics {
namespace metal {

std::unique_ptr<MetalArtifactStore> g_metal_artifact_store =
    std::make_unique<MetalArtifactStore>();

namespace {

constexpr uint32_t kArtifactFileMagic = 0x46414D58;    // 'XMAF'
constexpr uint32_t kArtifactRecordMagic = 0x52414D58;  // 'XMAR'
constexpr uint32_t kMaxArtifactPayloadSize = 128u * 1024u * 1024u;

static constexpr size_t kStageCompileSerializedKeySize =
    MetalStageCompileKey::kSerializedWordCount * sizeof(uint64_t);
static constexpr size_t kDxilBytecodeSerializedKeySize =
    MetalDxilBytecodeKey::kSerializedWordCount * sizeof(uint64_t);

REXPACKEDSTRUCT(ArtifactFileHeader, {
  uint32_t magic;
  uint32_t version;
});

REXPACKEDSTRUCT(ArtifactRecordHeader, {
  uint32_t magic;
  uint32_t version;
  uint64_t primary_hash;
  uint64_t modification;
  uint32_t stage;
  uint32_t kind;
  uint32_t payload_size;
  uint32_t reserved;
  uint64_t payload_hash;
});

REXPACKEDSTRUCT(StageCompilePayloadHeader, {
  uint32_t key_size;
  uint32_t function_name_size;
  uint32_t error_message_size;
  uint32_t metallib_size;
  uint32_t stage_in_metallib_size;
  uint32_t success;
  uint32_t has_reflection;
  uint32_t has_mesh_stage;
  uint32_t has_geometry_stage;
  uint32_t vertex_output_size_in_bytes;
  uint32_t vertex_input_count;
  uint32_t gs_max_input_primitives_per_mesh_threadgroup;
  uint32_t function_constant_count;
  uint32_t has_hull_info;
  uint32_t hs_max_patches_per_object_threadgroup;
  uint32_t hs_max_object_threads_per_patch;
  uint32_t hs_patch_constants_size;
  uint32_t hs_input_control_point_count;
  uint32_t hs_output_control_point_count;
  uint32_t hs_output_control_point_size;
  uint32_t hs_tessellator_domain;
  uint32_t hs_tessellator_partitioning;
  uint32_t hs_tessellator_output_primitive;
  uint32_t hs_tessellation_type_half;
  float hs_max_tessellation_factor;
  uint32_t has_domain_info;
  uint32_t ds_max_input_prims_per_mesh_threadgroup;
  uint32_t ds_input_control_point_count;
  uint32_t ds_input_control_point_size;
  uint32_t ds_patch_constants_size;
  uint32_t ds_tessellator_domain;
  uint32_t ds_tessellation_type_half;
});

REXPACKEDSTRUCT(StageCompileStringRecord, {
  uint32_t string_size;
  uint32_t value;
});

REXPACKEDSTRUCT(DxilBytecodePayloadHeader, {
  uint32_t key_size;
  uint32_t dxil_size;
});

template <typename T>
bool ReadPod(const uint8_t*& ptr, const uint8_t* end, T* out) {
  if (size_t(end - ptr) < sizeof(T)) {
    return false;
  }
  std::memcpy(out, ptr, sizeof(T));
  ptr += sizeof(T);
  return true;
}

bool ReadBytes(const uint8_t*& ptr, const uint8_t* end, void* out,
               size_t size) {
  if (size_t(end - ptr) < size) {
    return false;
  }
  std::memcpy(out, ptr, size);
  ptr += size;
  return true;
}

bool ReadString(const uint8_t*& ptr, const uint8_t* end, size_t size,
                std::string* out) {
  if (!out || size_t(end - ptr) < size) {
    return false;
  }
  out->assign(reinterpret_cast<const char*>(ptr), size);
  ptr += size;
  return true;
}

void AppendBytes(std::vector<uint8_t>& out, const void* data, size_t size) {
  if (!size) {
    return;
  }
  const uint8_t* bytes = static_cast<const uint8_t*>(data);
  out.insert(out.end(), bytes, bytes + size);
}

void AppendStringRecord(std::vector<uint8_t>& out, std::string_view value,
                        uint32_t metadata_value) {
  StageCompileStringRecord record = {};
  record.string_size = uint32_t(value.size());
  record.value = metadata_value;
  AppendBytes(out, &record, sizeof(record));
  AppendBytes(out, value.data(), value.size());
}

}  // namespace

size_t MetalArtifactStore::ArtifactKeyHasher::operator()(
    const ArtifactKey& key) const {
  const uint64_t words[] = {key.primary_hash, key.modification,
                            uint64_t(key.stage), uint64_t(key.kind)};
  return size_t(XXH3_64bits(words, sizeof(words)));
}

MetalArtifactStore::ArtifactKey MetalArtifactStore::MakeStageCompileKey(
    const MetalStageCompileKey& stage_key) {
  const auto words = stage_key.SerializeWords();
  XXH128_hash_t hash =
      XXH3_128bits(words.data(), words.size() * sizeof(uint64_t));
  ArtifactKey key;
  key.primary_hash = hash.low64;
  key.modification = hash.high64;
  key.stage = stage_key.stage;
  key.kind = ArtifactKind::kStageCompile;
  return key;
}

MetalArtifactStore::ArtifactKey MetalArtifactStore::MakeDxilBytecodeKey(
    const MetalDxilBytecodeKey& dxil_key) {
  const auto words = dxil_key.SerializeWords();
  XXH128_hash_t hash =
      XXH3_128bits(words.data(), words.size() * sizeof(uint64_t));
  ArtifactKey key;
  key.primary_hash = hash.low64;
  key.modification = hash.high64;
  key.stage = 0;
  key.kind = ArtifactKind::kDxilBytecode;
  return key;
}

void MetalArtifactStore::Initialize(
    const std::filesystem::path& artifact_path) {
  std::lock_guard<std::mutex> lock(mutex_);
  Shutdown();
  artifact_path_ = artifact_path;
  rex::filesystem::CreateParentFolder(artifact_path_);
  artifact_file_ = rex::filesystem::OpenFile(artifact_path_, "a+b");
  if (!artifact_file_) {
    REXLOG_WARN("MetalArtifactStore: failed to open {}",
           rex::path_to_utf8(artifact_path_));
    return;
  }
  initialized_ = LoadIndex();
  if (!initialized_) {
    std::fclose(artifact_file_);
    artifact_file_ = nullptr;
    index_.clear();
    artifact_path_.clear();
  }
}

void MetalArtifactStore::Shutdown() {
  if (artifact_file_) {
    std::fflush(artifact_file_);
    std::fclose(artifact_file_);
    artifact_file_ = nullptr;
  }
  index_.clear();
  artifact_path_.clear();
  initialized_ = false;
}

bool MetalArtifactStore::LoadIndex() {
  if (!artifact_file_) {
    return false;
  }
  rex::filesystem::Seek(artifact_file_, 0, SEEK_SET);

  ArtifactFileHeader header = {};
  if (std::fread(&header, sizeof(header), 1, artifact_file_) != 1 ||
      header.magic != kArtifactFileMagic || header.version != kStorageVersion) {
    rex::filesystem::TruncateStdioFile(artifact_file_, 0);
    header.magic = kArtifactFileMagic;
    header.version = kStorageVersion;
    if (std::fwrite(&header, sizeof(header), 1, artifact_file_) != 1) {
      return false;
    }
    std::fflush(artifact_file_);
    return true;
  }

  uint64_t valid_bytes = sizeof(header);
  while (true) {
    ArtifactRecordHeader record = {};
    if (std::fread(&record, sizeof(record), 1, artifact_file_) != 1) {
      break;
    }
    if (record.magic != kArtifactRecordMagic ||
        record.version != kStorageVersion ||
        record.payload_size > kMaxArtifactPayloadSize) {
      break;
    }
    const int64_t payload_offset = rex::filesystem::Tell(artifact_file_);
    if (payload_offset < 0) {
      break;
    }
    if (!rex::filesystem::Seek(artifact_file_, record.payload_size, SEEK_CUR)) {
      break;
    }
    ArtifactKey key;
    key.primary_hash = record.primary_hash;
    key.modification = record.modification;
    key.stage = record.stage;
    key.kind = static_cast<ArtifactKind>(record.kind);
    index_[key] = RecordLocation{uint64_t(payload_offset), record.payload_size,
                                 record.payload_hash};
    valid_bytes = uint64_t(payload_offset) + record.payload_size;
  }

  rex::filesystem::TruncateStdioFile(artifact_file_, valid_bytes);
  rex::filesystem::Seek(artifact_file_, 0, SEEK_END);
  return true;
}

bool MetalArtifactStore::ReadPayload(const RecordLocation& location,
                                     std::vector<uint8_t>* out) {
  if (!artifact_file_ || !out ||
      location.payload_size > kMaxArtifactPayloadSize) {
    return false;
  }
  out->resize(location.payload_size);
  if (!rex::filesystem::Seek(artifact_file_, int64_t(location.payload_offset),
                            SEEK_SET)) {
    return false;
  }
  if (location.payload_size &&
      std::fread(out->data(), location.payload_size, 1, artifact_file_) != 1) {
    return false;
  }
  if (XXH3_64bits(out->data(), out->size()) != location.payload_hash) {
    return false;
  }
  return true;
}

void MetalArtifactStore::StorePayload(const ArtifactKey& key,
                                      const uint8_t* payload,
                                      size_t payload_size) {
  if (!artifact_file_ || !payload || payload_size == 0 ||
      payload_size > kMaxArtifactPayloadSize) {
    return;
  }
  ArtifactRecordHeader record = {};
  record.magic = kArtifactRecordMagic;
  record.version = kStorageVersion;
  record.primary_hash = key.primary_hash;
  record.modification = key.modification;
  record.stage = key.stage;
  record.kind = uint32_t(key.kind);
  record.payload_size = uint32_t(payload_size);
  record.payload_hash = XXH3_64bits(payload, payload_size);

  rex::filesystem::Seek(artifact_file_, 0, SEEK_END);
  if (std::fwrite(&record, sizeof(record), 1, artifact_file_) != 1) {
    return;
  }
  const int64_t payload_offset = rex::filesystem::Tell(artifact_file_);
  if (payload_offset < 0) {
    return;
  }
  if (std::fwrite(payload, payload_size, 1, artifact_file_) != 1) {
    return;
  }
  std::fflush(artifact_file_);
  index_[key] = RecordLocation{uint64_t(payload_offset), uint32_t(payload_size),
                               record.payload_hash};
}

bool MetalArtifactStore::LoadStageCompile(const MetalStageCompileKey& key,
                                          MetalStageCompileResult* out) {
  if (!out) {
    return false;
  }
  std::vector<uint8_t> payload;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
      return false;
    }
    ArtifactKey artifact_key = MakeStageCompileKey(key);
    auto it = index_.find(artifact_key);
    if (it == index_.end()) {
      return false;
    }
    if (!ReadPayload(it->second, &payload)) {
      return false;
    }
  }

  const uint8_t* ptr = payload.data();
  const uint8_t* end = ptr + payload.size();
  StageCompilePayloadHeader header = {};
  if (!ReadPod(ptr, end, &header)) {
    return false;
  }
  if (header.key_size != kStageCompileSerializedKeySize ||
      header.vertex_input_count > 4096 ||
      header.function_constant_count > 4096) {
    return false;
  }

  std::array<uint64_t, MetalStageCompileKey::kSerializedWordCount>
      stored_key_words = {};
  if (!ReadBytes(ptr, end, stored_key_words.data(),
                 kStageCompileSerializedKeySize) ||
      stored_key_words != key.SerializeWords()) {
    return false;
  }

  MetalStageCompileResult result;
  result.success = header.success != 0;
  result.has_reflection = header.has_reflection != 0;
  result.has_mesh_stage = header.has_mesh_stage != 0;
  result.has_geometry_stage = header.has_geometry_stage != 0;
  if (!ReadString(ptr, end, header.function_name_size, &result.function_name)) {
    return false;
  }
  if (!ReadString(ptr, end, header.error_message_size, &result.error_message)) {
    return false;
  }
  result.metallib_data.resize(header.metallib_size);
  if (header.metallib_size &&
      !ReadBytes(ptr, end, result.metallib_data.data(), header.metallib_size)) {
    return false;
  }
  result.stage_in_metallib.resize(header.stage_in_metallib_size);
  if (header.stage_in_metallib_size &&
      !ReadBytes(ptr, end, result.stage_in_metallib.data(),
                 header.stage_in_metallib_size)) {
    return false;
  }

  result.reflection.vertex_output_size_in_bytes =
      header.vertex_output_size_in_bytes;
  result.reflection.vertex_input_count = header.vertex_input_count;
  result.reflection.gs_max_input_primitives_per_mesh_threadgroup =
      header.gs_max_input_primitives_per_mesh_threadgroup;
  result.reflection.has_hull_info = header.has_hull_info != 0;
  result.reflection.hs_max_patches_per_object_threadgroup =
      header.hs_max_patches_per_object_threadgroup;
  result.reflection.hs_max_object_threads_per_patch =
      header.hs_max_object_threads_per_patch;
  result.reflection.hs_patch_constants_size = header.hs_patch_constants_size;
  result.reflection.hs_input_control_point_count =
      header.hs_input_control_point_count;
  result.reflection.hs_output_control_point_count =
      header.hs_output_control_point_count;
  result.reflection.hs_output_control_point_size =
      header.hs_output_control_point_size;
  result.reflection.hs_tessellator_domain = header.hs_tessellator_domain;
  result.reflection.hs_tessellator_partitioning =
      header.hs_tessellator_partitioning;
  result.reflection.hs_tessellator_output_primitive =
      header.hs_tessellator_output_primitive;
  result.reflection.hs_tessellation_type_half =
      header.hs_tessellation_type_half != 0;
  result.reflection.hs_max_tessellation_factor =
      header.hs_max_tessellation_factor;
  result.reflection.has_domain_info = header.has_domain_info != 0;
  result.reflection.ds_max_input_prims_per_mesh_threadgroup =
      header.ds_max_input_prims_per_mesh_threadgroup;
  result.reflection.ds_input_control_point_count =
      header.ds_input_control_point_count;
  result.reflection.ds_input_control_point_size =
      header.ds_input_control_point_size;
  result.reflection.ds_patch_constants_size = header.ds_patch_constants_size;
  result.reflection.ds_tessellator_domain = header.ds_tessellator_domain;
  result.reflection.ds_tessellation_type_half =
      header.ds_tessellation_type_half != 0;

  result.reflection.vertex_inputs.reserve(header.vertex_input_count);
  for (uint32_t i = 0; i < header.vertex_input_count; ++i) {
    StageCompileStringRecord record = {};
    if (!ReadPod(ptr, end, &record)) {
      return false;
    }
    MetalShaderReflectionInput input;
    input.attribute_index = uint8_t(record.value);
    if (!ReadString(ptr, end, record.string_size, &input.name)) {
      return false;
    }
    result.reflection.vertex_inputs.push_back(std::move(input));
  }
  result.reflection.function_constants.reserve(header.function_constant_count);
  for (uint32_t i = 0; i < header.function_constant_count; ++i) {
    StageCompileStringRecord record = {};
    if (!ReadPod(ptr, end, &record)) {
      return false;
    }
    MetalShaderFunctionConstant constant;
    constant.type = record.value;
    if (!ReadString(ptr, end, record.string_size, &constant.name)) {
      return false;
    }
    result.reflection.function_constants.push_back(std::move(constant));
  }

  if (ptr != end) {
    return false;
  }
  *out = std::move(result);
  return true;
}

void MetalArtifactStore::StoreStageCompile(
    const MetalStageCompileKey& key, const MetalStageCompileResult& result) {
  if (!result.success || result.metallib_data.empty()) {
    return;
  }

  StageCompilePayloadHeader header = {};
  const auto key_words = key.SerializeWords();
  header.key_size = uint32_t(key_words.size() * sizeof(uint64_t));
  header.function_name_size = uint32_t(result.function_name.size());
  header.error_message_size = uint32_t(result.error_message.size());
  header.metallib_size = uint32_t(result.metallib_data.size());
  header.stage_in_metallib_size = uint32_t(result.stage_in_metallib.size());
  header.success = result.success ? 1u : 0u;
  header.has_reflection = result.has_reflection ? 1u : 0u;
  header.has_mesh_stage = result.has_mesh_stage ? 1u : 0u;
  header.has_geometry_stage = result.has_geometry_stage ? 1u : 0u;
  header.vertex_output_size_in_bytes =
      result.reflection.vertex_output_size_in_bytes;
  header.vertex_input_count = uint32_t(result.reflection.vertex_inputs.size());
  header.gs_max_input_primitives_per_mesh_threadgroup =
      result.reflection.gs_max_input_primitives_per_mesh_threadgroup;
  header.function_constant_count =
      uint32_t(result.reflection.function_constants.size());
  header.has_hull_info = result.reflection.has_hull_info ? 1u : 0u;
  header.hs_max_patches_per_object_threadgroup =
      result.reflection.hs_max_patches_per_object_threadgroup;
  header.hs_max_object_threads_per_patch =
      result.reflection.hs_max_object_threads_per_patch;
  header.hs_patch_constants_size = result.reflection.hs_patch_constants_size;
  header.hs_input_control_point_count =
      result.reflection.hs_input_control_point_count;
  header.hs_output_control_point_count =
      result.reflection.hs_output_control_point_count;
  header.hs_output_control_point_size =
      result.reflection.hs_output_control_point_size;
  header.hs_tessellator_domain = result.reflection.hs_tessellator_domain;
  header.hs_tessellator_partitioning =
      result.reflection.hs_tessellator_partitioning;
  header.hs_tessellator_output_primitive =
      result.reflection.hs_tessellator_output_primitive;
  header.hs_tessellation_type_half =
      result.reflection.hs_tessellation_type_half ? 1u : 0u;
  header.hs_max_tessellation_factor =
      result.reflection.hs_max_tessellation_factor;
  header.has_domain_info = result.reflection.has_domain_info ? 1u : 0u;
  header.ds_max_input_prims_per_mesh_threadgroup =
      result.reflection.ds_max_input_prims_per_mesh_threadgroup;
  header.ds_input_control_point_count =
      result.reflection.ds_input_control_point_count;
  header.ds_input_control_point_size =
      result.reflection.ds_input_control_point_size;
  header.ds_patch_constants_size = result.reflection.ds_patch_constants_size;
  header.ds_tessellator_domain = result.reflection.ds_tessellator_domain;
  header.ds_tessellation_type_half =
      result.reflection.ds_tessellation_type_half ? 1u : 0u;

  std::vector<uint8_t> payload;
  payload.reserve(sizeof(header) + header.key_size +
                  result.function_name.size() + result.error_message.size() +
                  result.metallib_data.size() +
                  result.stage_in_metallib.size());
  AppendBytes(payload, &header, sizeof(header));
  AppendBytes(payload, key_words.data(), header.key_size);
  AppendBytes(payload, result.function_name.data(),
              result.function_name.size());
  AppendBytes(payload, result.error_message.data(),
              result.error_message.size());
  AppendBytes(payload, result.metallib_data.data(),
              result.metallib_data.size());
  AppendBytes(payload, result.stage_in_metallib.data(),
              result.stage_in_metallib.size());
  for (const MetalShaderReflectionInput& input :
       result.reflection.vertex_inputs) {
    AppendStringRecord(payload, input.name, input.attribute_index);
  }
  for (const MetalShaderFunctionConstant& constant :
       result.reflection.function_constants) {
    AppendStringRecord(payload, constant.name, constant.type);
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (initialized_) {
    StorePayload(MakeStageCompileKey(key), payload.data(), payload.size());
  }
}

bool MetalArtifactStore::LoadDxilBytecode(const MetalDxilBytecodeKey& key,
                                          std::vector<uint8_t>* out) {
  if (!out) {
    return false;
  }
  std::vector<uint8_t> payload;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
      return false;
    }
    ArtifactKey artifact_key = MakeDxilBytecodeKey(key);
    auto it = index_.find(artifact_key);
    if (it == index_.end()) {
      return false;
    }
    if (!ReadPayload(it->second, &payload)) {
      return false;
    }
  }

  const uint8_t* ptr = payload.data();
  const uint8_t* end = ptr + payload.size();
  DxilBytecodePayloadHeader header = {};
  if (!ReadPod(ptr, end, &header)) {
    return false;
  }
  if (header.key_size != kDxilBytecodeSerializedKeySize ||
      header.dxil_size > kMaxArtifactPayloadSize) {
    return false;
  }

  std::array<uint64_t, MetalDxilBytecodeKey::kSerializedWordCount>
      stored_key_words = {};
  if (!ReadBytes(ptr, end, stored_key_words.data(),
                 kDxilBytecodeSerializedKeySize) ||
      stored_key_words != key.SerializeWords()) {
    return false;
  }

  out->resize(header.dxil_size);
  if (header.dxil_size && !ReadBytes(ptr, end, out->data(), header.dxil_size)) {
    return false;
  }
  return ptr == end;
}

void MetalArtifactStore::StoreDxilBytecode(
    const MetalDxilBytecodeKey& key, const std::vector<uint8_t>& dxil_data) {
  if (dxil_data.empty()) {
    return;
  }

  const auto key_words = key.SerializeWords();
  DxilBytecodePayloadHeader header = {};
  header.key_size = uint32_t(key_words.size() * sizeof(uint64_t));
  header.dxil_size = uint32_t(dxil_data.size());

  std::vector<uint8_t> payload;
  payload.reserve(sizeof(header) + header.key_size + dxil_data.size());
  AppendBytes(payload, &header, sizeof(header));
  AppendBytes(payload, key_words.data(), header.key_size);
  AppendBytes(payload, dxil_data.data(), dxil_data.size());

  std::lock_guard<std::mutex> lock(mutex_);
  if (initialized_) {
    StorePayload(MakeDxilBytecodeKey(key), payload.data(), payload.size());
  }
}

MetalArtifactStore::Stats MetalArtifactStore::GetStats() const {
  std::lock_guard<std::mutex> lock(mutex_);
  Stats stats;
  stats.entry_count = index_.size();
  for (const auto& [key, location] : index_) {
    stats.total_payload_bytes += location.payload_size;
  }
  return stats;
}

}  // namespace metal
}  // namespace gpu
}  // namespace xe
