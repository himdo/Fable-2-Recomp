/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license.                                          *
 * See backends/metal/licenses/XeniOS-LICENSE for the full terms.             *
 ******************************************************************************
 */

#ifndef XENIA_GPU_METAL_METAL_PIPELINE_CACHE_H_
#define XENIA_GPU_METAL_METAL_PIPELINE_CACHE_H_

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "rex/hash.h"
#include "rex/string/buffer.h"
#include "rex/graphics/pipeline/shader/dxbc_translator.h"
#include "rex/graphics/metal/dxbc_to_dxil_converter.h"
#include "rex/graphics/metal/metal_geometry_shader.h"
#include "rex/graphics/metal/metal_pipeline_failure_backoff.h"
#include "rex/graphics/metal/metal_shader.h"
#include "rex/graphics/metal/metal_shader_converter.h"
#include "rex/graphics/metal/metal_stage_compile_cache.h"
#include "rex/graphics/primitive_processor.h"
#include "rex/graphics/register_file.h"
#include "rex/graphics/registers.h"
#include "rex/graphics/shader_storage.h"
// clang-format off
// Must come after Metal.hpp (included transitively via metal_shader.h)
#include "third_party/metal-shader-converter/include/metal_irconverter_runtime.h"
// clang-format on
#include "rex/ui/metal/metal_api.h"

namespace rex {
namespace graphics {
namespace metal {

// Attachment format snapshot used to decouple pipeline creation from render
// target cache / render pass descriptor state.
struct PipelineAttachmentFormats {
  MTL::PixelFormat color_formats[4];
  MTL::PixelFormat depth_format;
  MTL::PixelFormat stencil_format;
  uint32_t sample_count;
};

// Fixed-function rendering state derived from registers that is common to all
// pipeline paths (standard, geometry emulation, tessellation emulation).
// Computed once per draw and passed into each GetOrCreate* method so the
// duplicated register reads are eliminated.
struct PipelineRenderingKey {
  uint32_t normalized_color_mask;
  uint32_t alpha_to_mask_enable;
  uint32_t blendcontrol[4];
};

struct MetalPipelineRuntimeStats {
  uint64_t dxil_convert_requests = 0;
  uint64_t dxil_cache_hits = 0;
  uint64_t dxil_cache_misses = 0;
  uint64_t dxil_convert_failures = 0;
  uint64_t dxil_convert_dxbc_bytes = 0;
  uint64_t dxil_convert_dxil_bytes = 0;
  uint64_t dxil_convert_ms_total = 0;
  uint64_t dxil_convert_ms_max = 0;
  uint64_t library_requests = 0;
  uint64_t library_failures = 0;
  uint64_t library_bytes = 0;
  uint64_t library_ms_total = 0;
  uint64_t library_ms_max = 0;
  uint64_t render_pipeline_requests = 0;
  uint64_t render_pipeline_failures = 0;
  uint64_t render_pipeline_ms_total = 0;
  uint64_t render_pipeline_ms_max = 0;
};

// Derive the shared PipelineRenderingKey from the current register state and
// the pixel translation (which provides writes_color_targets).
PipelineRenderingKey ResolvePipelineRenderingKey(
    const RegisterFile& regs,
    const MetalShader::MetalTranslation* pixel_translation,
    bool use_fallback_pixel_shader);

class MetalPipelineCache {
 public:
  static constexpr size_t kLayoutUIDEmpty = 0;

  MetalPipelineCache(MTL::Device* device, const RegisterFile& register_file);
  ~MetalPipelineCache();

  // Non-copyable.
  MetalPipelineCache(const MetalPipelineCache&) = delete;
  MetalPipelineCache& operator=(const MetalPipelineCache&) = delete;

  // Initialize shader translation components.  Must be called after
  // construction; returns false on failure.
  bool InitializeShaderTranslation(bool gamma_render_target_as_unorm8,
                                   bool msaa_2x_supported,
                                   uint32_t draw_resolution_scale_x,
                                   uint32_t draw_resolution_scale_y);

  // Shader storage (disk cache) lifecycle.
  void InitializeShaderStorage(const std::filesystem::path& cache_root,
                               uint32_t title_id, bool blocking);
  void ShutdownShaderStorage();
  void EndSubmission();

  // Load or retrieve a cached shader from the ucode hash.
  Shader* LoadShader(xenos::ShaderType shader_type,
                     const uint32_t* host_address, uint32_t dword_count);
  void SetupShaderBindingLayoutUserUIDs(MetalShader& shader);

  // Per-draw shader modification selection (mirrors D3D12 PipelineCache).
  DxbcShaderTranslator::Modification GetCurrentVertexShaderModification(
      const Shader& shader,
      Shader::HostVertexShaderType host_vertex_shader_type,
      uint32_t interpolator_mask) const;
  DxbcShaderTranslator::Modification GetCurrentPixelShaderModification(
      const Shader& shader, uint32_t interpolator_mask, uint32_t param_gen_pos,
      reg::RB_DEPTHCONTROL normalized_depth_control) const;

  enum class PipelineKind : uint32_t {
    kStandard = 0,
    kGeometry = 1,
    kTessellation = 2,
  };

  REXPACKEDSTRUCT(MetalPipelineDescription, {
    uint64_t vertex_shader_hash;
    uint64_t vertex_shader_modification;
    uint64_t pixel_shader_hash;
    uint64_t pixel_shader_modification;
    uint64_t auxiliary_hash;
    uint32_t auxiliary0;
    uint32_t auxiliary1;
    uint32_t auxiliary2;
    uint32_t auxiliary3;
    uint32_t kind;
    uint32_t sample_count;
    uint32_t depth_format;
    uint32_t stencil_format;
    uint32_t color_formats[4];
    uint32_t normalized_color_mask;
    uint32_t alpha_to_mask_enable;
    uint32_t blendcontrol[4];

    static constexpr uint32_t kVersion = 0x20260530;
  });

  REXPACKEDSTRUCT(MetalPipelineStoredDescription, {
    uint64_t description_hash;
    MetalPipelineDescription description;
  });

  // Handle for a standard render pipeline.  The state pointer is set
  // atomically so background compilation threads can publish results that
  // the render thread picks up via acquire loads.
  struct PipelineHandle {
    std::atomic<MTL::RenderPipelineState*> state{nullptr};
    uint64_t description_hash = 0;
    MetalPipelineDescription description = {};
    // Data needed by background thread to create the pipeline.
    // Cleared after creation.
    MetalShader::MetalTranslation* pending_vertex_translation{nullptr};
    MetalShader::MetalTranslation* pending_pixel_translation{nullptr};
    PipelineAttachmentFormats pending_formats{};
    uint32_t pending_normalized_color_mask{0};
    uint32_t pending_blendcontrol[4]{};
    bool pending_alpha_to_mask{false};
    bool storage_write_pending = false;
    uint8_t priority{0};
  };

  // Pipeline state creation -- all accept pre-resolved attachment formats and
  // the shared rendering key derived by ResolvePipelineRenderingKey().
  PipelineHandle* GetOrCreatePipelineState(
      MetalShader::MetalTranslation* vertex_translation,
      MetalShader::MetalTranslation* pixel_translation,
      const PipelineAttachmentFormats& attachment_formats,
      const PipelineRenderingKey& rendering_key);

  // Returns true while background pipeline creation threads are busy.
  bool IsCreatingPipelines();

  MetalStageCompileCacheStats GetAndResetStageCompileStats();
  MetalPipelineRuntimeStats GetAndResetRuntimeStats();

  struct GeometryPipelineState {
    MTL::RenderPipelineState* pipeline = nullptr;
    uint32_t gs_vertex_size_in_bytes = 0;
    uint32_t gs_max_input_primitives_per_mesh_threadgroup = 0;
  };

  struct TessellationPipelineState {
    MTL::RenderPipelineState* pipeline = nullptr;
    IRRuntimeTessellationPipelineConfig config = {};
    IRRuntimePrimitiveType primitive = IRRuntimePrimitiveTypeTriangle;
  };

  GeometryPipelineState* GetOrCreateGeometryPipelineState(
      MetalShader::MetalTranslation* vertex_translation,
      MetalShader::MetalTranslation* pixel_translation,
      GeometryShaderKey geometry_shader_key,
      const PipelineAttachmentFormats& attachment_formats,
      const PipelineRenderingKey& rendering_key);

  TessellationPipelineState* GetOrCreateTessellationPipelineState(
      MetalShader::MetalTranslation* domain_translation,
      MetalShader::MetalTranslation* pixel_translation,
      const PrimitiveProcessor::ProcessingResult& primitive_processing_result,
      const PipelineAttachmentFormats& attachment_formats,
      const PipelineRenderingKey& rendering_key);

  // Ensure the shared DXBC translator has produced DXBC bytecode.
  bool EnsureDxbcTranslationReady(MetalShader::MetalTranslation* translation,
                                  const char* stage_name);
  bool EnsureDxilTranslationReady(MetalShader::MetalTranslation* translation,
                                  const char* stage_name);
  bool EnsureMetalTranslationReady(MetalShader::MetalTranslation* translation);

  // Ensure the depth-only pixel shader is compiled and ready.
  bool EnsureDepthOnlyPixelShader();

  // Ucode disassembly scratch buffer (shared with command processor).
  rex::string::StringBuffer& ucode_disasm_buffer() { return ucode_disasm_buffer_; }

  // Serialize pipeline binary archive to disk.
  void SerializePipelineBinaryArchive();

 private:
  bool InitializeShaderStorageInternal(const std::filesystem::path& cache_root,
                                       uint32_t title_id, bool blocking);
  std::string GetShaderStorageDeviceTag() const;
  std::string GetShaderStorageAbiTag() const;
  bool InitializePipelineBinaryArchive(
      const std::filesystem::path& archive_path);
  void PrewarmStoredPipelines(
      const std::vector<MetalPipelineStoredDescription>& descriptions,
      bool blocking);
  MetalShader* LoadShader(xenos::ShaderType shader_type,
                          const uint32_t* host_address, uint32_t dword_count,
                          uint64_t data_hash);
  MetalPipelineDescription BuildStandardPipelineDescription(
      MetalShader::MetalTranslation* vertex_translation,
      MetalShader::MetalTranslation* pixel_translation,
      const PipelineAttachmentFormats& attachment_formats,
      const PipelineRenderingKey& rendering_key) const;
  MetalPipelineDescription BuildPipelineDescription(
      PipelineKind kind, MetalShader::MetalTranslation* vertex_translation,
      MetalShader::MetalTranslation* pixel_translation,
      const PipelineAttachmentFormats& attachment_formats,
      const PipelineRenderingKey& rendering_key) const;
  MetalPipelineDescription BuildGeometryPipelineDescription(
      MetalShader::MetalTranslation* vertex_translation,
      MetalShader::MetalTranslation* pixel_translation,
      GeometryShaderKey geometry_shader_key,
      const PipelineAttachmentFormats& attachment_formats,
      const PipelineRenderingKey& rendering_key) const;
  MetalPipelineDescription BuildTessellationPipelineDescription(
      MetalShader::MetalTranslation* domain_translation,
      MetalShader::MetalTranslation* pixel_translation,
      const PrimitiveProcessor::ProcessingResult& primitive_processing_result,
      xenos::TessellationMode tessellation_mode,
      const PipelineAttachmentFormats& attachment_formats,
      const PipelineRenderingKey& rendering_key) const;
  void QueueStoredShader(MetalShader& shader);
  void QueueStoredPipeline(const MetalPipelineDescription& description,
                           uint64_t description_hash);
  bool EnsureDxbcTranslationReadyLocked(
      MetalShader::MetalTranslation* translation, const char* stage_name);
  bool ConvertDxbcToDxil(const std::vector<uint8_t>& dxbc_data,
                         std::vector<uint8_t>& dxil_data,
                         std::string* error_message);
  MTL::Library* NewLibraryFromBytes(const std::vector<uint8_t>& bytes,
                                    const char* label);
  void RecordLibraryCreation(size_t byte_count, bool success, uint64_t ms);
  void RecordRenderPipelineCreation(bool success, uint64_t ms);

  MTL::Device* device_;
  const RegisterFile& register_file_;

  // MSC shader translation components.
  std::unique_ptr<DxbcShaderTranslator> shader_translator_;
  std::unique_ptr<DxbcToDxilConverter> dxbc_to_dxil_converter_;
  std::unique_ptr<MetalShaderConverter> metal_shader_converter_;
  std::mutex shader_translation_mutex_;

  std::atomic<uint64_t> dxil_convert_requests_{0};
  std::atomic<uint64_t> dxil_cache_hits_{0};
  std::atomic<uint64_t> dxil_cache_misses_{0};
  std::atomic<uint64_t> dxil_convert_failures_{0};
  std::atomic<uint64_t> dxil_convert_dxbc_bytes_{0};
  std::atomic<uint64_t> dxil_convert_dxil_bytes_{0};
  std::atomic<uint64_t> dxil_convert_ms_total_{0};
  std::atomic<uint64_t> dxil_convert_ms_max_{0};
  std::atomic<uint64_t> library_requests_{0};
  std::atomic<uint64_t> library_failures_{0};
  std::atomic<uint64_t> library_bytes_{0};
  std::atomic<uint64_t> library_ms_total_{0};
  std::atomic<uint64_t> library_ms_max_{0};
  std::atomic<uint64_t> render_pipeline_requests_{0};
  std::atomic<uint64_t> render_pipeline_failures_{0};
  std::atomic<uint64_t> render_pipeline_ms_total_{0};
  std::atomic<uint64_t> render_pipeline_ms_max_{0};

  rex::string::StringBuffer ucode_disasm_buffer_;

  // MSC shader cache (keyed by ucode hash).
  std::unordered_map<uint64_t, std::unique_ptr<MetalShader>> shader_cache_;

  struct LayoutUID {
    size_t uid;
    size_t vector_span_offset;
    size_t vector_span_length;
  };
  std::mutex layouts_mutex_;
  // Texture binding layouts of different shaders, for obtaining layout UIDs.
  std::vector<MetalShader::TextureBinding> texture_binding_layouts_;
  // Map of texture binding layouts used by shaders, for obtaining UIDs. Keys
  // are XXH3 hashes of layouts, values need manual collision resolution using
  // layout_vector_offset:layout_length of texture_binding_layouts_.
  std::unordered_multimap<uint64_t, LayoutUID,
                          rex::hash::IdentityHasher<uint64_t>>
      texture_binding_layout_map_;
  // Bindless sampler indices of different shaders, for obtaining layout UIDs.
  std::vector<uint32_t> bindless_sampler_layouts_;
  // Keys are XXH3 hashes of used bindless sampler indices.
  std::unordered_multimap<uint64_t, LayoutUID,
                          rex::hash::IdentityHasher<uint64_t>>
      bindless_sampler_layout_map_;

  class GeneratedStageCache;

  // MSC pipeline caches (keyed by shader combination).
  std::unordered_map<uint64_t, std::unique_ptr<PipelineHandle>> pipeline_cache_;
  std::unordered_map<uint64_t, GeometryPipelineState> geometry_pipeline_cache_;
  PipelineFailureBackoff geometry_pipeline_failures_;
  std::unordered_map<uint64_t, TessellationPipelineState>
      tessellation_pipeline_cache_;
  std::unique_ptr<GeneratedStageCache> generated_stages_;

  MTL::Library* depth_only_pixel_library_ = nullptr;
  std::string depth_only_pixel_function_name_;

  // Shader storage paths and writers.
  ShaderStorageWriter<MetalPipelineStoredDescription> storage_writer_;
  std::atomic<bool> shader_storage_file_flush_needed_{false};
  std::atomic<bool> pipeline_storage_file_flush_needed_{false};
  uint32_t shader_storage_title_id_ = 0;
  std::mutex stored_pipeline_mutex_;
  std::unordered_set<uint64_t> stored_pipeline_hashes_;
  std::filesystem::path shader_storage_local_root_;
  std::filesystem::path shader_storage_title_root_;
  std::filesystem::path artifact_store_path_;
  std::filesystem::path pipeline_binary_archive_path_;
  MTL::BinaryArchive* pipeline_binary_archive_ = nullptr;
  bool pipeline_binary_archive_dirty_ = false;
  std::mutex pipeline_binary_archive_mutex_;

  // Async pipeline compilation thread pool.
  void CreationThread(size_t thread_index);
  MTL::RenderPipelineState* CreatePipelineFromHandle(
      const PipelineHandle* handle);

  std::vector<std::thread> creation_threads_;
  struct PipelineCreationRequest {
    PipelineHandle* handle{nullptr};
  };
  struct PipelineCreationPriorityCompare {
    bool operator()(const PipelineCreationRequest& a,
                    const PipelineCreationRequest& b) const {
      return a.handle->priority < b.handle->priority;
    }
  };
  std::priority_queue<PipelineCreationRequest,
                      std::vector<PipelineCreationRequest>,
                      PipelineCreationPriorityCompare>
      creation_queue_;
  std::mutex creation_request_lock_;
  std::condition_variable creation_request_cond_;
  std::atomic<size_t> creation_threads_busy_{0};
  bool creation_threads_shutdown_{false};
};

}  // namespace metal
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_METAL_METAL_PIPELINE_CACHE_H_
