/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license.                                          *
 * See backends/metal/licenses/XeniOS-LICENSE for the full terms.             *
 ******************************************************************************
 */

#ifndef XENIA_GPU_METAL_METAL_COMMAND_PROCESSOR_H_
#define XENIA_GPU_METAL_METAL_COMMAND_PROCESSOR_H_

#include <dispatch/dispatch.h>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "rex/platform.h"
#include "rex/string/buffer.h"
#include "rex/graphics/command_processor.h"
#include "rex/graphics/util/draw.h"
#include "rex/graphics/pipeline/shader/dxbc_translator.h"
#include "rex/graphics/metal/dxbc_to_dxil_converter.h"
#include "rex/graphics/metal/metal_geometry_shader.h"
#include "rex/graphics/metal/metal_pipeline_cache.h"
#include "rex/graphics/metal/metal_primitive_processor.h"
#include "rex/graphics/metal/metal_render_target_cache.h"
#include "rex/graphics/metal/metal_shader.h"
#include "rex/graphics/metal/metal_shader_converter.h"
#include "rex/graphics/metal/metal_shared_memory.h"
#include "rex/graphics/metal/metal_texture_cache.h"
#include "rex/graphics/metal/metal_upload_buffer_pool.h"
#include "rex/graphics/metal/metal_zpd_visibility_pool.h"
// clang-format off
// Must come after metal_texture_cache.h which includes Metal.hpp
#include "third_party/metal-shader-converter/include/metal_irconverter_runtime.h"
// clang-format on
#include "rex/ui/metal/metal_api.h"
#include "rex/ui/metal/metal_provider.h"

namespace MTL {
class BlitCommandEncoder;
class ComputeCommandEncoder;
class Fence;
class Heap;
class ResidencySet;
class SharedEvent;
}  // namespace MTL

namespace rex {
namespace graphics {
namespace metal {

class MetalGraphicsSystem;

class MetalCommandProcessor final : public CommandProcessor {
 protected:

 public:
  explicit MetalCommandProcessor(MetalGraphicsSystem* graphics_system,
                                 system::KernelState* kernel_state);
  ~MetalCommandProcessor();

  void TracePlaybackWroteMemory(uint32_t base_ptr, uint32_t length);
  void RestoreEdramSnapshot(const void* snapshot);
  void ClearCaches() override;
  void InvalidateGpuMemory() override;
  void ClearReadbackBuffers();

  ui::metal::MetalProvider& GetMetalProvider() const;

  // Get the Metal device and command queue
  MTL::Device* GetMetalDevice() const { return device_; }
  MTL::CommandQueue* GetMetalCommandQueue() const { return command_queue_; }
  MTL::CommandBuffer* GetCurrentCommandBuffer() const {
    return current_command_buffer_;
  }

  // Submission coordination helpers — callers use these to query or obtain
  // command buffers for transfer/upload work without reaching into internal
  // command-processor state.
  bool HasActiveSubmission() const {
    return current_command_buffer_ != nullptr;
  }
  // Returns true when upload/transfer work can join the current submission's
  // command buffer. This is the case when a command buffer exists but no render
  // encoder is open.
  bool CanJoinActiveSubmissionForTransfer() const {
    return current_command_buffer_ != nullptr &&
           current_render_encoder_ == nullptr;
  }
  enum class TransferRequestSource : uint32_t {
    kUnknown,
    kSharedMemoryUpload,
    kGuestIndexCopy,
    kRenderTargetTransfer,
    kCount,
  };
  static constexpr size_t kTransferRequestSourceCount =
      static_cast<size_t>(TransferRequestSource::kCount);
  enum class SharedMemoryRequestReason : uint32_t {
    kUnknown,
    kVertexFetch,
    kMemexportStream,
    kGuestIndex,
    kShaderPrimitiveIndex,
    kIndexCopySource,
    kTextureBase,
    kTextureMips,
    kTextureBaseAndMips,
    kResolveCopyDest,
    kDrawMaterialization,
    kCount,
  };
  static constexpr size_t kSharedMemoryRequestReasonCount =
      static_cast<size_t>(SharedMemoryRequestReason::kCount);
  enum class SharedMemoryRequestOutcome : uint32_t {
    kAlreadyResident,
    kUploadBeforeRenderEncoder,
    kUploadInsideRenderEncoder,
    kRequestFailed,
    kNoSharedMemory,
    kTextureDeferredUploadFlush,
    kCount,
  };
  static constexpr size_t kSharedMemoryRequestOutcomeCount =
      static_cast<size_t>(SharedMemoryRequestOutcome::kCount);
  enum class SharedMemoryUploadRoute : uint32_t {
    kStagedBlit,
    kDirectWrite,
    kCount,
  };
  static constexpr size_t kSharedMemoryUploadRouteCount =
      static_cast<size_t>(SharedMemoryUploadRoute::kCount);
  enum class SharedMemoryDirectWriteRejectReason : uint32_t {
    kMainGpuAccessInFlight,
    kStandaloneAccessInFlight,
    kNoSharedBufferContents,
    kMixedRangeSplit,
    kCount,
  };
  static constexpr size_t kSharedMemoryDirectWriteRejectReasonCount =
      static_cast<size_t>(SharedMemoryDirectWriteRejectReason::kCount);
  enum class TextureUploadSourceRoute : uint32_t {
    kCpuGuestMemory,
    kResidentSharedMemory,
    kScaledResolve,
    kCount,
  };
  static constexpr size_t kTextureUploadSourceRouteCount =
      static_cast<size_t>(TextureUploadSourceRoute::kCount);
  enum class TextureUploadSourceFallbackReason : uint32_t {
    kMixedValidity,
    kScaledResolve,
    kSourceAlreadyResident,
    kCpuSourceLoadFailed,
    kUnknown,
    kCount,
  };
  static constexpr size_t kTextureUploadSourceFallbackReasonCount =
      static_cast<size_t>(TextureUploadSourceFallbackReason::kCount);
  enum class SharedMemoryUploadEncoderEndReason : uint32_t {
    kUnknown,
    kRenderBegin,
    kTransferRequest,
    kTextureCompute,
    kTextureBlit,
    kTextureDeferredBlit,
    kScaledResolveBlit,
    kCommandBufferEnd,
    kSwap,
    kUploadFailure,
    kShutdown,
    kMaterializationDrain,
    kCount,
  };
  static constexpr size_t kSharedMemoryUploadEncoderEndReasonCount =
      static_cast<size_t>(SharedMemoryUploadEncoderEndReason::kCount);
  // Returns a command buffer suitable for transfer (blit/compute) work.
  // If a render encoder is active it is ended first; if no command buffer
  // exists one is created. This is an encoder-lifetime break, not necessarily
  // a command-buffer submission break. Returns nullptr on failure.
  MTL::CommandBuffer* RequestTransferCommandBuffer(
      TransferRequestSource source = TransferRequestSource::kUnknown);
  void RecordSharedMemoryUploadRoute(SharedMemoryUploadRoute route,
                                     uint64_t bytes);
  void RecordSharedMemoryDirectWriteEligibility(uint64_t direct_bytes,
                                                uint64_t staged_bytes);
  void RecordSharedMemoryDirectWriteReject(
      SharedMemoryDirectWriteRejectReason reason, uint64_t bytes);
  void RecordTextureUploadSourceRoute(TextureUploadSourceRoute route,
                                      uint64_t bytes);
  void RecordTextureUploadSourceFallback(
      TextureUploadSourceFallbackReason reason);
  void RecordSharedMemoryUploadEncoderCopy();
  bool RequestSharedMemoryRange(SharedMemoryRequestReason reason,
                                uint32_t start, uint32_t length);
  bool RequestSharedMemoryRangeBeforeDrawPass(SharedMemoryRequestReason reason,
                                              uint32_t start, uint32_t length);
  bool RequestSharedMemoryRanges(SharedMemoryRequestReason reason,
                                 const SharedMemory::Range* ranges,
                                 uint32_t range_count);
  void RecordSharedMemoryRequestOutcome(SharedMemoryRequestOutcome outcome);
  MTL::BlitCommandEncoder* GetSharedMemoryUploadBlitEncoder();
  void EndSharedMemoryUploadBlitEncoder(
      SharedMemoryUploadEncoderEndReason reason =
          SharedMemoryUploadEncoderEndReason::kUnknown);

  // Standalone (detached) transfer command-buffer helpers.
  // These create command buffers that are independent of the active submission
  // and are used by caches for upload/transfer work that cannot join the
  // current command buffer.  The returned CB is retained; ownership transfers
  // back via CommitStandaloneAsync or CommitStandaloneAndWait.
  MTL::CommandBuffer* CreateStandaloneTransferCommandBuffer(const char* label);
  // Commit a standalone command buffer asynchronously (fire-and-forget).
  // The CB is released via a completion handler.
  void CommitStandaloneAsync(MTL::CommandBuffer* cmd);
  // Commit a standalone command buffer synchronously and wait for completion.
  // The CB is released before returning.
  void CommitStandaloneAndWait(MTL::CommandBuffer* cmd);

  uint64_t GetCurrentSubmission() const;
  uint64_t GetCompletedSubmission() const;
  uint64_t GetLatestSubmissionStarted() const { return submission_current_; }
  MTL::CommandBuffer* EnsureCommandBuffer();
  void EndRenderEncoder();
  void InvalidateRenderEncoderStateAfterDrawPassTransfers(
      MetalRenderTargetCache::DrawPassTransferEncoderMutationMask mutations);
  void ResetRenderEncoderResourceUsage();
  void UseRenderEncoderResource(MTL::Resource* resource,
                                MTL::ResourceUsage usage);
  void UseRenderEncoderResource(MTL::Resource* resource,
                                MTL::ResourceUsage usage,
                                MTL::RenderStages stages);
  bool AddResidencySetHeap(MTL::Heap* heap);
  void EnsureCommandBufferAutoreleasePool();
  void DrainCommandBufferAutoreleasePool();

  // Force issue a swap to push render target to presenter (for trace dumps)
  void ForceIssueSwap();
  bool HasSeenSwap() const { return saw_swap_; }
  void SetSwapDestSwap(uint32_t dest_base, bool swap);
  bool ConsumeSwapDestSwap(uint32_t dest_base, bool* swap_out);

  MetalSharedMemory* shared_memory() const { return shared_memory_.get(); }
  MetalRenderTargetCache* render_target_cache() const {
    return render_target_cache_.get();
  }
  MetalTextureCache* texture_cache() const { return texture_cache_.get(); }

  // D3D12-style shared-memory hazard tracking for GPU writes produced by
  // resolves and memexport draws, expressed with Metal fences/barriers.
  struct SharedMemoryRange {
    uint32_t start = 0;
    uint32_t length = 0;
  };

  struct SharedMemoryReadDependency {
    bool needs_fence_wait = false;
  };

  void MarkSharedMemoryComputeWritePending(uint32_t address, uint32_t length,
                                           MTL::ComputeCommandEncoder* encoder);
  void MarkSharedMemoryRenderWritePending(uint32_t address, uint32_t length,
                                          MTL::RenderStages stages);
  bool PrepareSharedMemoryComputeReadDependency(
      const SharedMemoryRange* ranges, uint32_t range_count,
      bool consumer_can_join_current_submission,
      SharedMemoryReadDependency* dependency_out);
  bool EncodeSharedMemoryComputeReadDependency(
      MTL::ComputeCommandEncoder* encoder,
      const SharedMemoryReadDependency& dependency,
      const SharedMemoryRange* ranges, uint32_t range_count);

  // Persistent bindless descriptor heap allocators.
  uint32_t AllocateViewBindlessIndex();
  void ReleaseViewBindlessIndex(uint32_t index);
  void RetireViewBindlessIndex(uint32_t index);
  uint32_t GetViewBindlessHeapAvailableCount() const;
  uint32_t AllocateSamplerBindlessIndex();
  void ReleaseSamplerBindlessIndex(uint32_t index);
  IRDescriptorTableEntry* GetViewBindlessHeapEntry(uint32_t index);
  IRDescriptorTableEntry* GetSamplerBindlessHeapEntry(uint32_t index);

 protected:
  bool SetupContext() override;
  void ShutdownContext() override;
  void InitializeShaderStorage(
      const std::filesystem::path& cache_root, uint32_t title_id, bool blocking) override;

  // Flush pending GPU work before entering wait state.
  // This ensures Metal command buffers are submitted and completed before
  // the autorelease pool is drained, preventing hangs from deferred
  // deallocation.
  void PrepareForWait() override;

  // Use base class WriteRegister - don't override with empty implementation!
  // The base class stores values in register_file_->values[] which we need.
  void OnPrimaryBufferEnd() override;
  void OnGammaRamp256EntryTableValueWritten() override;
  void OnGammaRampPWLValueWritten() override;

  void IssueSwap(uint32_t frontbuffer_ptr, uint32_t frontbuffer_width,
                 uint32_t frontbuffer_height) override;

  Shader* LoadShader(xenos::ShaderType shader_type, uint32_t guest_address,
                     const uint32_t* host_address,
                     uint32_t dword_count) override;

  bool IssueDraw(xenos::PrimitiveType primitive_type, uint32_t index_count,
                 IndexBufferInfo* index_buffer_info,
                 bool major_mode_explicit) override;
  bool IssueCopy() override;

  void WriteRegister(uint32_t index, uint32_t value) override;
  void WriteRegistersFromMem(uint32_t start_index, uint32_t* base,
                             uint32_t num_registers) override;
  void WriteRegisterRangeFromRing(rex::memory::RingBuffer* ring, uint32_t base,
                                  uint32_t num_registers) override;

  bool CanFastWriteRegisterRange(uint32_t start_index,
                                 uint32_t num_registers) const;
  bool TryWriteKnownRegisterRangeFromMem(uint32_t start_index, uint32_t* base,
                                         uint32_t num_registers);
  void WriteFastRegisterRangeFromRing(rex::memory::RingBuffer* ring, uint32_t base,
                                      uint32_t num_registers);
  void WriteShaderConstantsFromMem(uint32_t start_index, uint32_t* base,
                                   uint32_t num_registers);
  void WriteBoolLoopConstantsFromMem(uint32_t start_index, uint32_t* base,
                                     uint32_t num_registers);
  void WriteFetchConstantsFromMem(uint32_t start_index, uint32_t* base,
                                  uint32_t num_registers);
  bool FloatConstantRangeNeedsDirty(uint32_t start_index, const uint32_t* base,
                                    uint32_t num_registers,
                                    const uint64_t* constant_map,
                                    uint32_t stage_first_constant) const;
  void DirtyFetchConstantDwords(
      const DxbcShader::FetchConstantDwordMask& dirty_mask);

  static constexpr size_t kStageVertex = 0;
  static constexpr size_t kStagePixel = 1;
  static constexpr size_t kStageCount = 2;  // Vertex + pixel.
  static constexpr size_t kCbvSlotCount = 5;
  enum CbvSlot : size_t {
    kCbvSlotSystem,
    kCbvSlotFloat,
    kCbvSlotBoolLoop,
    kCbvSlotFetch,
    kCbvSlotDescriptorIndices,
  };
  static constexpr size_t kBindlessRootRebuildReasonCount = 4;
  enum BindlessRootRebuildReason : size_t {
    kBindlessRootRebuildFrameOpen,
    kBindlessRootRebuildDescriptorIndicesPointerChange,
    kBindlessRootRebuildOtherCbvPointerChange,
    kBindlessRootRebuildSharedMemoryUavChange,
  };
  static constexpr size_t kBindlessRootSlotsChangedBinCount = 6;
  static constexpr size_t kBindlessRootRebuildDetailCount = 7;
  enum BindlessRootRebuildDetail : size_t {
    kBindlessRootDetailSameBufferOffsetChanged,
    kBindlessRootDetailDifferentBuffer,
    kBindlessRootDetailDescriptorIndicesOnly,
    kBindlessRootDetailOtherCbvOnly,
    kBindlessRootDetailMixedDescriptorAndOther,
    kBindlessRootDetailResourceIdentityChanged,
    kBindlessRootDetailResourceIdentitySame,
  };
  static constexpr size_t kRenderEncoderBufferStageTelemetryCount = 4;
  static constexpr size_t kBindlessRootArgTelemetrySlotCount = 32;

  // Per-draw uniform buffer coordinates passed between IssueDraw sub-methods.
  struct UniformBufferInfo {
    struct Cbv {
      MTL::Buffer* buffer = nullptr;
      NS::UInteger offset = 0;
      uint64_t gpu_address = 0;
      size_t size = 0;
      // Inactive slots are canonicalized to null in the top-level argument
      // table, matching the fixed MSC layout without treating unused CBVs as
      // per-draw state.
      bool active = false;
    };

    std::array<std::array<Cbv, kCbvSlotCount>, kStageCount> cbvs = {};
    std::array<uint32_t, kStageCount> active_cbv_masks = {};
    std::array<DxbcShader::FetchConstantDwordMask, kStageCount>
        fetch_constant_dword_masks = {};
  };

  struct DrawDynamicState {
    MTL::Viewport viewport = {};
    MTL::ScissorRect scissor = {};
    draw_util::ViewportInfo viewport_info = {};
    reg::RB_DEPTHCONTROL depth_control = {};
    reg::RB_STENCILREFMASK stencil_ref_mask_front = {};
    reg::RB_STENCILREFMASK stencil_ref_mask_back = {};
    reg::PA_SU_SC_MODE_CNTL pa_su_sc_mode_cntl = {};
    MTL::CullMode cull_mode = MTL::CullModeNone;
    MTL::Winding front_facing_winding = MTL::WindingCounterClockwise;
    MTL::TriangleFillMode triangle_fill_mode = MTL::TriangleFillModeFill;
    float depth_bias_constant = 0.0f;
    float depth_bias_slope = 0.0f;
    MTL::DepthClipMode depth_clip_mode = MTL::DepthClipModeClip;
    bool primitive_polygonal = false;
    bool rasterization_enabled = true;
    float blend_constants[4] = {};
  };

  // Vertex binding range for stage-in / geometry emulation.
  struct VertexBindingRange {
    uint32_t binding_index = 0;
    uint32_t offset = 0;
    uint32_t length = 0;
    uint32_t stride = 0;
  };

  struct PreparedIndexBuffer {
    MTL::Buffer* buffer = nullptr;
    uint64_t offset = 0;
  };

  enum class PreparedDrawFlushReason : uint32_t {
    kManual,
    kRenderTargetUpdate,
    kRenderTargetKeyMismatch,
    kQueueBudget,
    kQueueReject,
    kPrepareForWait,
    kSwap,
    kIssueCopy,
    kTransferRequest,
    kRenderEncoderEnd,
    kCommandBufferEnd,
    kQuery,
    kCount,
  };
  static constexpr size_t kPreparedDrawFlushReasonCount =
      static_cast<size_t>(PreparedDrawFlushReason::kCount);

  enum class PreparedDrawQueueRejectReason : uint32_t {
    kNone,
    kResidentWithoutActiveQueue,
    kNoSharedMemoryRanges,
    kMemexport,
    kTextureUpload,
    kTextureRequestLoadData,
    kPendingDrawPassTransfers,
    kZPDActive,
    kRenderTargetKeyMismatch,
    kQueueBudget,
    kCount,
  };
  static constexpr size_t kPreparedDrawQueueRejectReasonCount =
      static_cast<size_t>(PreparedDrawQueueRejectReason::kCount);

  struct PreparedDrawRenderTargetKey {
    uint32_t rb_surface_info = 0;
    uint32_t rb_depth_info = 0;
    std::array<uint32_t, xenos::kMaxColorRenderTargets> rb_color_info = {};
    uint32_t normalized_depth_control = 0;
    uint32_t normalized_color_mask = 0;
    bool is_rasterization_done = false;

    bool operator==(const PreparedDrawRenderTargetKey& other) const {
      return rb_surface_info == other.rb_surface_info &&
             rb_depth_info == other.rb_depth_info &&
             rb_color_info == other.rb_color_info &&
             normalized_depth_control == other.normalized_depth_control &&
             normalized_color_mask == other.normalized_color_mask &&
             is_rasterization_done == other.is_rasterization_done;
    }

    bool operator!=(const PreparedDrawRenderTargetKey& other) const {
      return !(*this == other);
    }
  };

  struct RenderResourceRef {
    MTL::Resource* resource = nullptr;
    MTL::ResourceUsage usage = MTL::ResourceUsageRead;
    MTL::RenderStages stages = MTL::RenderStages(0);
  };
  struct RenderResourceSet {
    std::vector<RenderResourceRef> resources;
    uint64_t serial = 0;
    uint64_t source_serial = 0;
  };
  struct EncoderResourceUsageState {
    uint32_t usage_bits = 0;
    uint32_t read_stage_bits = 0;
    uint32_t write_stage_bits = 0;
    uint32_t sample_stage_bits = 0;
  };

  struct PreparedDraw {
    MTL::RenderPipelineState* pipeline = nullptr;
    MetalPipelineCache::TessellationPipelineState* tessellation_pipeline_state =
        nullptr;
    MetalPipelineCache::GeometryPipelineState* geometry_pipeline_state =
        nullptr;

    PrimitiveProcessor::ProcessingResult primitive_processing_result = {};
    UniformBufferInfo uniforms = {};
    DrawDynamicState dynamic_state = {};
    PreparedIndexBuffer prepared_guest_dma_index_buffer = {};
    IndexBufferInfo index_buffer_info = {};
    bool has_index_buffer_info = false;

    std::vector<Shader::VertexBinding> vertex_bindings;
    std::array<VertexBindingRange, 32> vertex_ranges = {};
    uint32_t vertex_range_count = 0;
    std::vector<SharedMemory::Range> materialization_ranges;
    bool has_invalid_shared_memory = false;

    std::array<SharedMemoryRange, 96> shared_memory_hazard_ranges = {};
    uint32_t shared_memory_hazard_range_count = 0;
    MTL::RenderStages shared_memory_consumer_stages = MTL::RenderStages(0);

    std::vector<draw_util::MemExportRange> memexport_ranges;
    MTL::RenderStages memexport_write_stages = MTL::RenderStages(0);
    MTL::ResourceUsage shared_memory_usage = MTL::ResourceUsageRead;
    PreparedDrawRenderTargetKey render_target_key = {};
    RenderResourceSet texture_resource_set = {};
    MetalTextureCache::TextureMaterializationPlan texture_materialization_plan =
        {};

    bool use_tessellation_emulation = false;
    bool use_geometry_emulation = false;
    bool shared_memory_is_uav = false;
    bool memexport_used = false;
    bool uses_vertex_fetch = false;
    bool prepare_uniforms = false;
    bool fallback_depth_attachment_required = false;
    bool texture_upload_needed = false;
    bool may_texture_request_load_data = false;
    bool has_pending_draw_pass_transfers = false;
  };

  bool PrepareGuestDMAIndexBufferForMemexport(
      const PrimitiveProcessor::ProcessingResult& primitive_processing_result,
      PreparedIndexBuffer& prepared_index_buffer_out);

  // Host draw path — prepare per-draw dynamic state and upload constant buffers
  // before entering the Metal render encoder.
  bool PrepareDrawConstants(
      const RegisterFile& regs, Shader* vertex_shader, Shader* pixel_shader,
      MetalShader* metal_vertex_shader, MetalShader* metal_pixel_shader,
      bool shared_memory_is_uav, bool is_rasterization_done,
      const PrimitiveProcessor::ProcessingResult& primitive_processing_result,
      uint32_t used_texture_mask, uint32_t normalized_color_mask,
      MTL::RenderPassDescriptor* render_pass_descriptor,
      UniformBufferInfo& uniforms_out, DrawDynamicState& dynamic_state_out);

  void ApplyDrawDynamicState(const DrawDynamicState& dynamic_state);

  PreparedDrawRenderTargetKey BuildPreparedDrawRenderTargetKey(
      const RegisterFile& regs, bool is_rasterization_done,
      reg::RB_DEPTHCONTROL normalized_depth_control,
      uint32_t normalized_color_mask) const;
  bool SubmitPreparedDraw(PreparedDraw&& draw);
  bool EncodePreparedDraw(const PreparedDraw& draw);
  bool FlushPreparedDrawQueue(PreparedDrawFlushReason reason);
  bool PreparedDrawQueueHasActiveZPD() const;
  bool CanQueuePreparedDraw(const PreparedDraw& draw,
                            PreparedDrawQueueRejectReason& reject_reason) const;
  void RecordPreparedDrawQueueReject(
      PreparedDrawQueueRejectReason reject_reason);

  // Host draw path — refresh top-level argument buffers when root CBV state
  // changes and bind them plus the stable descriptor heap buffers to the
  // render encoder when encoder state requires it.
  bool PopulateBindlessTables(bool shared_memory_is_uav,
                              MTL::ResourceUsage shared_memory_usage,
                              bool use_geometry_emulation,
                              bool use_tessellation_emulation,
                              const UniformBufferInfo& uniforms);

  // Host draw path — bind vertex buffers and dispatch the actual draw call
  // (tessellation, geometry emulation, or standard path), then track
  // memexport writes.
  bool DispatchDraw(
      const PrimitiveProcessor::ProcessingResult& primitive_processing_result,
      bool use_tessellation_emulation,
      MetalPipelineCache::TessellationPipelineState*
          tessellation_pipeline_state,
      bool use_geometry_emulation,
      MetalPipelineCache::GeometryPipelineState* geometry_pipeline_state,
      bool shared_memory_is_uav, MTL::ResourceUsage shared_memory_usage,
      bool memexport_used, MTL::RenderStages memexport_write_stages,
      bool uses_vertex_fetch, bool shared_memory_resource_registered,
      const PreparedIndexBuffer* prepared_guest_dma_index_buffer,
      const std::vector<Shader::VertexBinding>& vb_bindings,
      const VertexBindingRange* vertex_ranges, uint32_t vertex_range_count,
      const IndexBufferInfo* index_buffer_info,
      const std::vector<draw_util::MemExportRange>& memexport_ranges);

 private:
  // Command buffer management
  enum class RenderEncoderEndReason : uint32_t {
    kUnknown,
    kPrepareForWait,
    kSwap,
    kCommandBufferEnd,
    kRequestTransferCommandBuffer,
    kSharedMemoryReadDependency,
    kRenderTargetUpdateDescriptorDirty,
    kPipelineDescriptorIncompatible,
    kTextureUploadBeforeDrawPass,
    kSharedMemoryUploadBeforeDrawPass,
    kResolveNeedsBoundary,
    kBeginRenderEncoderDescriptorChanged,
    kCount,
  };

  static constexpr size_t kRenderEncoderEndReasonCount =
      static_cast<size_t>(RenderEncoderEndReason::kCount);

  enum class RenderResourceSetKind : uint32_t {
    kFixed,
    kTexture,
    kRoot,
    kCount,
  };
  static constexpr size_t kRenderResourceSetKindCount =
      static_cast<size_t>(RenderResourceSetKind::kCount);

  enum class DrawMaterializationSource : uint32_t {
    kVertexFetch,
    kGuestIndex,
    kMemexport,
    kTextureSource,
    kCount,
  };
  static constexpr size_t kDrawMaterializationSourceCount =
      static_cast<size_t>(DrawMaterializationSource::kCount);

  struct BackendTelemetryStats {
    uint64_t swaps = 0;
    uint64_t draw_calls = 0;
    uint64_t pipeline_sets = 0;
    uint64_t pipeline_set_skips = 0;
    uint64_t texture_requests_before_encoder = 0;
    uint64_t texture_requests_after_encoder_begin = 0;

    uint64_t begin_encoder_calls = 0;
    uint64_t begin_encoder_reused_compatible = 0;
    uint64_t begin_encoder_created = 0;
    uint64_t begin_encoder_descriptor_restarts = 0;
    uint64_t begin_encoder_resource_usage_resets = 0;
    uint64_t begin_encoder_descriptor_failures = 0;
    uint64_t begin_encoder_creation_failures = 0;

    uint64_t end_encoder_active = 0;
    uint64_t end_encoder_no_active = 0;
    std::array<uint64_t, kRenderEncoderEndReasonCount> end_reasons = {};
    std::array<uint64_t, kTransferRequestSourceCount>
        transfer_request_sources_total = {};
    std::array<uint64_t, kTransferRequestSourceCount>
        transfer_request_sources_active = {};
    std::array<uint64_t, kTransferRequestSourceCount>
        transfer_request_sources_no_active = {};
    std::array<uint64_t, kTransferRequestSourceCount>
        transfer_request_render_encoder_ends = {};
    std::array<uint64_t, kSharedMemoryRequestReasonCount>
        shared_memory_request_upload_bytes = {};
    std::array<uint64_t, kSharedMemoryRequestReasonCount>
        shared_memory_request_failures = {};
    std::array<uint64_t, kSharedMemoryRequestOutcomeCount>
        shared_memory_request_outcomes = {};
    std::array<uint64_t, kSharedMemoryUploadRouteCount>
        shared_memory_upload_route_counts = {};
    std::array<uint64_t, kSharedMemoryUploadRouteCount>
        shared_memory_upload_route_bytes = {};
    uint64_t shared_memory_direct_write_eligible_bytes = 0;
    uint64_t shared_memory_direct_write_staged_required_bytes = 0;
    std::array<uint64_t, kSharedMemoryDirectWriteRejectReasonCount>
        shared_memory_direct_write_reject_counts = {};
    std::array<uint64_t, kSharedMemoryDirectWriteRejectReasonCount>
        shared_memory_direct_write_reject_bytes = {};
    uint64_t shared_memory_lazy_upload_no_upload_batches = 0;
    uint64_t shared_memory_lazy_upload_direct_only_batches = 0;
    uint64_t shared_memory_lazy_upload_mixed_batches = 0;
    uint64_t shared_memory_lazy_upload_staged_only_batches = 0;
    uint64_t shared_memory_lazy_upload_direct_only_active = 0;
    uint64_t shared_memory_lazy_upload_mixed_active = 0;
    uint64_t shared_memory_lazy_upload_staged_only_active = 0;
    std::array<uint64_t, kTextureUploadSourceRouteCount>
        texture_upload_source_route_counts = {};
    std::array<uint64_t, kTextureUploadSourceRouteCount>
        texture_upload_source_route_bytes = {};
    std::array<uint64_t, kTextureUploadSourceFallbackReasonCount>
        texture_upload_source_fallback_reasons = {};
    uint64_t shared_memory_upload_batches = 0;
    uint64_t shared_memory_upload_batch_input_ranges = 0;
    uint64_t shared_memory_upload_batch_coalesced_ranges = 0;
    uint64_t shared_memory_upload_batch_bytes = 0;
    uint64_t shared_memory_upload_encoder_acquisitions = 0;
    uint64_t shared_memory_upload_encoder_reuses = 0;
    uint64_t shared_memory_upload_encoder_copies = 0;
    std::array<uint64_t, kSharedMemoryUploadEncoderEndReasonCount>
        shared_memory_upload_encoder_end_reasons = {};
    std::array<uint64_t, kDrawMaterializationSourceCount>
        draw_materialization_source_ranges = {};
    std::array<uint64_t, kDrawMaterializationSourceCount>
        draw_materialization_source_bytes = {};
    std::array<uint64_t, kDrawMaterializationSourceCount>
        draw_materialization_source_invalid_ranges = {};
    std::array<uint64_t, kDrawMaterializationSourceCount>
        draw_materialization_source_invalid_bytes = {};
    uint64_t draw_materialization_per_draw_requests = 0;
    uint64_t draw_materialization_per_draw_invalid_requests = 0;
    uint64_t draw_materialization_per_draw_resident_skips = 0;
    uint64_t prepared_draw_queue_appends = 0;
    uint64_t prepared_draw_queue_flushes = 0;
    uint64_t prepared_draw_queue_single_draw_flushes = 0;
    uint64_t prepared_draw_queue_draws_flushed = 0;
    uint64_t prepared_draw_queue_ranges_flushed = 0;
    uint64_t prepared_draw_queue_bytes_flushed = 0;
    uint64_t prepared_draw_queue_invalid_flushes = 0;
    uint64_t prepared_draw_queue_texture_plans_flushed = 0;
    uint64_t prepared_draw_queue_texture_loads_planned = 0;
    uint64_t prepared_draw_queue_texture_loads_executed = 0;
    std::array<uint64_t, kPreparedDrawFlushReasonCount>
        prepared_draw_queue_flush_reasons = {};
    std::array<uint64_t, kPreparedDrawQueueRejectReasonCount>
        prepared_draw_queue_reject_reasons = {};

    std::array<uint64_t, kCbvSlotCount> cbv_uploads = {};
    std::array<uint64_t, kCbvSlotCount> cbv_reuse_hits = {};
    std::array<uint64_t, kStageCount> descriptor_index_uploads = {};
    std::array<uint64_t, kCbvSlotCount> constant_payload_cache_hits = {};
    std::array<uint64_t, kCbvSlotCount> constant_payload_cache_misses = {};
    std::array<uint64_t, kCbvSlotCount> constant_payload_cache_bytes_saved = {};
    std::array<uint64_t, kStageCount> descriptor_index_payload_cache_hits = {};
    std::array<uint64_t, kStageCount> descriptor_index_payload_cache_misses =
        {};
    std::array<uint64_t, kStageCount>
        descriptor_index_payload_cache_bytes_saved = {};
    std::array<uint64_t, kStageCount> bindless_root_allocations = {};
    std::array<uint64_t, kStageCount> bindless_root_reuse_hits = {};
    std::array<uint64_t, kStageCount> bindless_root_arg_noop_updates = {};
    std::array<uint64_t, kStageCount> bindless_root_arg_slots_patched = {};
    std::array<uint64_t, kStageCount> bindless_root_arg_bytes_copied = {};
    std::array<uint64_t, kBindlessRootArgTelemetrySlotCount>
        bindless_root_arg_slot_patches = {};
    std::array<uint64_t, kBindlessRootRebuildReasonCount>
        bindless_root_rebuild_reasons = {};
    std::array<uint64_t, kBindlessRootSlotsChangedBinCount>
        bindless_root_slots_changed = {};
    std::array<uint64_t, kBindlessRootRebuildDetailCount>
        bindless_root_rebuild_details = {};
    std::array<uint64_t, kRenderEncoderBufferStageTelemetryCount>
        render_encoder_buffer_full_binds = {};
    std::array<uint64_t, kRenderEncoderBufferStageTelemetryCount>
        render_encoder_buffer_offset_binds = {};
    std::array<uint64_t, kRenderEncoderBufferStageTelemetryCount>
        render_encoder_buffer_noop_binds = {};
    std::array<uint64_t, kRenderEncoderBufferStageTelemetryCount>
        render_encoder_buffer_null_binds = {};
    std::array<uint64_t, kRenderEncoderBufferStageTelemetryCount>
        render_encoder_buffer_untracked_binds = {};
    uint64_t render_encoder_use_resource_calls = 0;
    uint64_t render_encoder_use_resource_skips = 0;
    uint64_t render_encoder_use_resource_upgrades = 0;
    uint64_t render_encoder_use_resources_batches = 0;
    uint64_t render_encoder_use_resources_requested = 0;
    uint64_t render_encoder_use_resources_skips = 0;
    std::array<uint64_t, kRenderResourceSetKindCount>
        render_resource_set_applies = {};
    std::array<uint64_t, kRenderResourceSetKindCount>
        render_resource_set_skips = {};
    std::array<uint64_t, kRenderResourceSetKindCount>
        render_resource_set_resources = {};
    std::array<uint64_t, kRenderResourceSetKindCount>
        render_resource_registry_serial_skips = {};
    std::array<uint64_t, kRenderResourceSetKindCount>
        render_resource_registry_builds = {};
    std::array<uint64_t, kRenderResourceSetKindCount>
        render_resource_registry_registers = {};
    uint64_t residency_set_allocations_added = 0;
    uint64_t residency_set_allocation_duplicates = 0;
    uint64_t residency_set_commits = 0;
    uint64_t residency_set_resource_refs_covered = 0;
    uint64_t residency_set_resource_refs_fallback = 0;
    uint64_t residency_set_use_resources_covered = 0;
    uint64_t residency_set_use_resources_fallback = 0;
    uint64_t residency_set_use_heaps_covered = 0;
    uint64_t residency_set_use_heaps_fallback = 0;
    uint64_t frame_slot_waits = 0;
    uint64_t frame_slot_wait_submission_count = 0;
    uint64_t frame_slot_wait_submission_last = 0;
  };

  void FlushCommandBufferAndWait(uint64_t timeout_ns, const char* context);
  MTL::RenderPassDescriptor* GetDrawRenderPassDescriptor(
      bool fallback_depth_attachment_required = false);
  bool BeginRenderEncoderForDraw(
      bool fallback_depth_attachment_required = false);
  void EndRenderEncoder(RenderEncoderEndReason reason);
  void EndCommandBuffer();
  bool CanEndSubmissionImmediately();
  void WaitForPendingCompletionHandlers();
  void ProcessCompletedSubmissions();
  void WaitForFrameSlotSubmission(uint64_t awaited_submission);
  void OpenFrameLifetime();
  void InvalidateFrameTransientBindings();
  void CloseFrameLifetime();
  void MaybeDumpBackendTelemetry(const char* reason, bool force = false);
  void ResetBackendTelemetry();
  void InitializeResidencySet();
  void ShutdownResidencySet();
  void RegisterInitialResidencySetResources();
  bool AddResidencySetResource(MTL::Resource* resource);
  bool IsResidencySetResourceCovered(MTL::Resource* resource) const;
  bool IsResidencySetHeapCovered(MTL::Heap* heap) const;
  bool AnySharedMemoryRangeInvalid(const SharedMemory::Range* ranges,
                                   uint32_t range_count) const;
  void RecordSharedMemoryLazyUploadRoute(
      const MetalSharedMemory::UploadRouteInfo& route_info,
      bool render_encoder_active);
  void PrepareSharedMemoryUploadBeforeDrawPass(
      const SharedMemory::Range* ranges, uint32_t range_count);
  bool HasActiveSharedMemoryWritePending() const;

  void UseRenderEncoderAttachmentHeaps(MTL::RenderPassDescriptor* descriptor);
  void AddRenderResourceRef(RenderResourceSet& set, MTL::Resource* resource,
                            MTL::ResourceUsage usage, MTL::RenderStages stages);
  void RestoreRenderResourceSet(RenderResourceSetKind kind,
                                RenderResourceSet& current,
                                const RenderResourceSet& snapshot);
  void PublishRenderResourceSet(RenderResourceSet& current,
                                RenderResourceSet&& next);
  uint64_t GetBindlessFixedResourceSourceSerial(
      MTL::ResourceUsage shared_memory_usage) const;
  uint64_t GetBindlessTextureResourceSourceSerial() const;
  uint64_t GetBindlessRootResourceSourceSerial(
      const UniformBufferInfo& uniforms) const;
  void PublishBindlessFixedResourceSet(MTL::ResourceUsage shared_memory_usage);
  void RestoreBindlessTextureResourceSet(const RenderResourceSet& set);
  void PublishBindlessTextureResourceSet();
  void PublishBindlessRootResourceSet(const UniformBufferInfo& uniforms);
  void ApplyRenderEncoderResourceSets();
  void ApplyRenderEncoderResourceSet(RenderResourceSetKind kind,
                                     const RenderResourceSet& set,
                                     uint64_t& applied_serial);
  EncoderResourceUsageState* FindOrInsertRenderEncoderResourceUsage(
      MTL::Resource* resource, bool& inserted);
  void GrowRenderEncoderResourceUsageTable(size_t min_capacity);
  void UseRenderEncoderResources(const MTL::Resource* const resources[],
                                 uint32_t count, MTL::ResourceUsage usage);
  void UseRenderEncoderResources(const MTL::Resource* const resources[],
                                 uint32_t count, MTL::ResourceUsage usage,
                                 MTL::RenderStages stages);
  void UseRenderEncoderHeap(MTL::Heap* heap);
  uint64_t GetBindlessDescriptorRetirementSubmission() const;
  void FreeViewBindlessIndexNow(uint32_t index);
  void FreeSamplerBindlessIndexNow(uint32_t index);

  struct PendingSharedMemoryWrite {
    uint32_t start = 0;
    uint32_t end = 0;
    uint64_t submission_id = 0;
    MTL::RenderStages producer_stages = MTL::RenderStages(0);
    bool active_render_encoder = false;
    bool fence_updated = false;
  };

  void MarkSharedMemoryWritePending(uint32_t address, uint32_t length,
                                    MTL::RenderStages producer_stages,
                                    bool active_render_encoder,
                                    bool fence_updated);
  bool PendingSharedMemoryWritesOverlapRange(uint32_t start,
                                             uint32_t length) const;
  bool PendingSharedMemoryWriteOverlapsRanges(
      const PendingSharedMemoryWrite& pending, const SharedMemoryRange* ranges,
      uint32_t range_count) const;
  bool PendingSharedMemoryWritesOverlapRanges(const SharedMemoryRange* ranges,
                                              uint32_t range_count) const;
  void UpdateSharedMemoryFenceForActiveRenderEncoder();
  void PruneCompletedSharedMemoryWrites(uint64_t completed_submission);
  void RetireFenceWaitedSharedMemoryWrites(const SharedMemoryRange* ranges,
                                           uint32_t range_count);
  bool EncodeSharedMemoryRenderReadDependencies(
      const SharedMemoryRange* ranges, uint32_t range_count,
      MTL::RenderStages consumer_stages);
  bool EncodeSharedMemoryBlitReadDependency(MTL::BlitCommandEncoder* encoder,
                                            uint32_t start, uint32_t length);

  // Fixed-function depth/stencil state (mirrors Vulkan/D3D12 dynamic state).
  void ApplyDepthStencilState(const DrawDynamicState& dynamic_state);
  void ApplyRasterizerState(const DrawDynamicState& dynamic_state);

  // Constants for the MSC path.
  static constexpr size_t kNullBufferSize = 4096;
  static constexpr size_t kCbvSizeBytes = 4096;

  // Constants for MSC descriptor heap sizes.
  static constexpr size_t kResourceHeapSlotsPerTable = 1025 + 2;
  static constexpr size_t kSamplerHeapSlotsPerTable = 257 + 2;
  static constexpr size_t kTopLevelABSlotsPerTable = 32;
  static_assert(kBindlessRootArgTelemetrySlotCount == kTopLevelABSlotsPerTable);
  static constexpr size_t kTopLevelABBytesPerTable =
      kTopLevelABSlotsPerTable * sizeof(uint64_t);
  // MSC explicit root signatures encode descriptor-table pointers and root
  // resource pointers as 64-bit entries in the top-level argument buffer.
  // Keep these in the same order as MetalShaderConverter's root signature.
  enum TopLevelABSlot : uint32_t {
    kTopLevelABSlotSRVSpace0,
    kTopLevelABSlotSRVSpace1,
    kTopLevelABSlotSRVSpace2,
    kTopLevelABSlotSRVSpace3,
    kTopLevelABSlotSRVSpace10,
    kTopLevelABSlotUAVSpace0,
    kTopLevelABSlotUAVSpace1,
    kTopLevelABSlotUAVSpace2,
    kTopLevelABSlotUAVSpace3,
    kTopLevelABSlotSamplerSpace0,
    kTopLevelABSlotCBVSystem,
    kTopLevelABSlotCBVFloat,
    kTopLevelABSlotCBVBoolLoop,
    kTopLevelABSlotCBVFetch,
    kTopLevelABSlotCBVDescriptorIndices,
  };
  // Descriptor tables and common CBVs are shared by every graphics stage, so
  // one top-level argument table can be rebound across
  // vertex/fragment/object/mesh stages. The first stage-local CBV slots are
  // kept near the shared slots for telemetry continuity.
  enum GraphicsRootABSlot : uint32_t {
    kGraphicsRootABSlotSRVSpace0 = kTopLevelABSlotSRVSpace0,
    kGraphicsRootABSlotSRVSpace1 = kTopLevelABSlotSRVSpace1,
    kGraphicsRootABSlotSRVSpace2 = kTopLevelABSlotSRVSpace2,
    kGraphicsRootABSlotSRVSpace3 = kTopLevelABSlotSRVSpace3,
    kGraphicsRootABSlotSRVSpace10 = kTopLevelABSlotSRVSpace10,
    kGraphicsRootABSlotUAVSpace0 = kTopLevelABSlotUAVSpace0,
    kGraphicsRootABSlotUAVSpace1 = kTopLevelABSlotUAVSpace1,
    kGraphicsRootABSlotUAVSpace2 = kTopLevelABSlotUAVSpace2,
    kGraphicsRootABSlotUAVSpace3 = kTopLevelABSlotUAVSpace3,
    kGraphicsRootABSlotSamplerSpace0 = kTopLevelABSlotSamplerSpace0,
    kGraphicsRootABSlotCBVSystem = kTopLevelABSlotCBVSystem,
    kGraphicsRootABSlotCBVVertexFloat = kTopLevelABSlotCBVFloat,
    kGraphicsRootABSlotCBVBoolLoop = kTopLevelABSlotCBVBoolLoop,
    kGraphicsRootABSlotCBVVertexFetch = kTopLevelABSlotCBVFetch,
    kGraphicsRootABSlotCBVVertexDescriptorIndices =
        kTopLevelABSlotCBVDescriptorIndices,
    kGraphicsRootABSlotCBVHullFloat,
    kGraphicsRootABSlotCBVHullFetch,
    kGraphicsRootABSlotCBVHullDescriptorIndices,
    kGraphicsRootABSlotCBVDomainFloat,
    kGraphicsRootABSlotCBVDomainFetch,
    kGraphicsRootABSlotCBVDomainDescriptorIndices,
    kGraphicsRootABSlotCBVPixelFloat,
    kGraphicsRootABSlotCBVPixelFetch,
    kGraphicsRootABSlotCBVPixelDescriptorIndices,
  };

  // System constants population (mirrors D3D12 implementation)
  void UpdateSystemConstantValues(bool shared_memory_is_uav,
                                  bool primitive_polygonal,
                                  uint32_t line_loop_closing_index,
                                  xenos::Endian index_endian,
                                  const draw_util::ViewportInfo& viewport_info,
                                  uint32_t used_texture_mask,
                                  reg::RB_DEPTHCONTROL normalized_depth_control,
                                  uint32_t normalized_color_mask);

  // Metal device and command queue (from provider)
  MTL::Device* device_ = nullptr;
  MTL::CommandQueue* command_queue_ = nullptr;
  MTL::ResidencySet* residency_set_ = nullptr;
  bool residency_set_supported_ = false;
  bool residency_set_enabled_ = false;
  bool residency_set_attached_ = false;
  std::unordered_set<MTL::Resource*> residency_set_resources_;
  std::unordered_set<MTL::Heap*> residency_set_heaps_;
  MTL::SharedEvent* wait_shared_event_ = nullptr;
  uint64_t wait_shared_event_value_ = 0;
  MTL::Fence* shared_memory_fence_ = nullptr;
  SharedMemoryRequestReason current_shared_memory_upload_reason_ =
      SharedMemoryRequestReason::kUnknown;
  // Current command buffer and encoder
  MTL::CommandBuffer* current_command_buffer_ = nullptr;
  MTL::RenderCommandEncoder* current_render_encoder_ = nullptr;
  MTL::BlitCommandEncoder* shared_memory_upload_blit_encoder_ = nullptr;
  MTL::RenderPassDescriptor* current_render_pass_descriptor_ = nullptr;
  bool current_render_encoder_has_zpd_visibility_ = false;
  NS::AutoreleasePool* command_buffer_autorelease_pool_ = nullptr;

  // Tracks resources marked via useResource for the current render encoder to
  // avoid redundant driver calls across draws within the same encoder.
  struct EncoderResourceUsageTableEntry {
    MTL::Resource* resource = nullptr;
    EncoderResourceUsageState state = {};
  };
  std::vector<EncoderResourceUsageTableEntry>
      render_encoder_resource_usage_table_;
  size_t render_encoder_resource_usage_count_ = 0;
  std::vector<MTL::Heap*> render_encoder_heap_usage_;
  BackendTelemetryStats backend_telemetry_;
  uint64_t backend_telemetry_last_dump_swap_ = 0;

  static constexpr size_t kPreparedDrawQueueMaxDraws = 64;
  static constexpr size_t kPreparedDrawQueueMaxRanges = 4096;
  static constexpr uint64_t kPreparedDrawQueueMaxBytes = 64ull * 1024 * 1024;
  std::vector<PreparedDraw> prepared_draw_queue_;
  PreparedDrawRenderTargetKey prepared_draw_queue_render_target_key_ = {};
  bool prepared_draw_queue_render_target_key_valid_ = false;
  bool flushing_prepared_draw_queue_ = false;

  // Shared memory for Xbox 360 memory access
  std::unique_ptr<MetalSharedMemory> shared_memory_;
  std::unique_ptr<MetalPrimitiveProcessor> primitive_processor_;
  bool frame_open_ = false;

  bool saw_swap_ = false;
  uint32_t last_swap_ptr_ = 0;
  uint32_t last_swap_width_ = 0;
  uint32_t last_swap_height_ = 0;
  std::unordered_map<uint32_t, bool> swap_dest_swaps_by_base_;

  // Pipeline cache (owns shaders, pipelines, shader translation components).
  std::unique_ptr<MetalPipelineCache> pipeline_cache_;

  struct DepthStencilStateKey {
    uint32_t depth_control;
    uint32_t stencil_ref_mask_front;
    uint32_t stencil_ref_mask_back;
    uint32_t polygonal_and_backface;
    bool operator==(const DepthStencilStateKey& other) const {
      return depth_control == other.depth_control &&
             stencil_ref_mask_front == other.stencil_ref_mask_front &&
             stencil_ref_mask_back == other.stencil_ref_mask_back &&
             polygonal_and_backface == other.polygonal_and_backface;
    }
    struct Hasher {
      size_t operator()(const DepthStencilStateKey& key) const {
        size_t h = size_t(key.depth_control);
        h ^= size_t(key.stencil_ref_mask_front) << 1;
        h ^= size_t(key.stencil_ref_mask_back) << 2;
        h ^= size_t(key.polygonal_and_backface) << 3;
        return h;
      }
    };
  };

  std::unordered_map<DepthStencilStateKey, MTL::DepthStencilState*,
                     DepthStencilStateKey::Hasher>
      depth_stencil_state_cache_;

  bool mesh_shader_supported_ = false;

  // Texture cache for guest texture uploads
  std::unique_ptr<MetalTextureCache> texture_cache_;

  // Render target cache for framebuffer management
  std::unique_ptr<MetalRenderTargetCache> render_target_cache_;

  // Null resources for unbound slots
  MTL::Buffer* null_buffer_ = nullptr;
  MTL::Texture* null_texture_ = nullptr;
  MTL::SamplerState* null_sampler_ = nullptr;

  // Persistent bindless descriptor heaps.
  // Canonical texture views and samplers get stable slot indices allocated on
  // demand and freed on destruction. Non-canonical texture views may use
  // submission-lifetime slots from the same heap. The heaps are bound once per
  // encoder.
  // 1M entries (24 MiB). Metal has no API-side descriptor heap cap — the heap
  // is just an MTLBuffer. D3D12 uses 262144 but doesn't need per-swizzle
  // texture views; 4x headroom covers the swizzled-view multiplier and avoids
  // exhausting the heap before the first submission completes.
  static constexpr uint32_t kViewBindlessHeapSize = 1048576;
  static constexpr uint32_t kSamplerBindlessHeapSize = 2048;
  MTL::Buffer* view_bindless_heap_ = nullptr;
  MTL::Buffer* sampler_bindless_heap_ = nullptr;
  // Explicit-layout top-level bindings for shared memory / EDRAM use small
  // dedicated system tables rather than overlapping the bindless texture heap.
  MTL::Buffer* system_view_tables_ = nullptr;
  static constexpr uint32_t kSystemViewTableSRVSharedMemory = 0;
  static constexpr uint32_t kSystemViewTableSRVNull = 1;
  static constexpr uint32_t kSystemViewTableUAVNullStart = 2;
  static constexpr uint32_t kSystemViewTableUAVSharedMemoryStart = 4;
  static constexpr uint32_t kSystemViewTableEntryCount = 6;

  // Simple bump allocator with free list for persistent heap slots.
  uint32_t view_bindless_heap_next_ = 0;
  std::vector<uint32_t> view_bindless_heap_free_;
  bool view_bindless_heap_exhausted_logged_ = false;
  uint32_t sampler_bindless_heap_next_ = 0;
  std::vector<uint32_t> sampler_bindless_heap_free_;
  bool sampler_bindless_heap_exhausted_logged_ = false;
  struct RetiredBindlessDescriptor {
    uint32_t index;
    uint64_t submission_id;
  };
  std::deque<RetiredBindlessDescriptor> retired_view_bindless_indices_;
  std::deque<RetiredBindlessDescriptor> retired_sampler_bindless_indices_;

  struct RetiredMetalBuffer {
    MTL::Buffer* buffer = nullptr;
    uint64_t submission_id = 0;
  };
  std::deque<RetiredMetalBuffer> retired_memexport_index_buffers_;

  MTL::Buffer* tessellator_tables_buffer_ = nullptr;

  // System constants - matches DxbcShaderTranslator::SystemConstants layout
  DxbcShaderTranslator::SystemConstants system_constants_;

  // Fixed-function dynamic state cached per render encoder.
  MTL::RenderPipelineState* current_render_pipeline_state_ = nullptr;
  float ff_blend_factor_[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  bool ff_blend_factor_valid_ = false;
  bool rasterizer_state_valid_ = false;
  MTL::CullMode current_cull_mode_ = MTL::CullModeNone;
  MTL::Winding current_front_facing_winding_ = MTL::WindingCounterClockwise;
  MTL::TriangleFillMode current_triangle_fill_mode_ = MTL::TriangleFillModeFill;
  float current_depth_bias_values_[3] = {0.0f, 0.0f, 0.0f};
  MTL::DepthClipMode current_depth_clip_mode_ = MTL::DepthClipModeClip;
  MTL::DepthStencilState* current_depth_stencil_state_ = nullptr;
  bool stencil_reference_valid_ = false;
  uint32_t current_stencil_reference_ = 0;
  bool viewport_dirty_ = true;
  MTL::Viewport cached_viewport_ = {};
  bool scissor_dirty_ = true;
  MTL::ScissorRect cached_scissor_ = {};

  // Constant buffer dirty tracking (D3D12 pattern).
  // Each binding records the pool-allocated buffer, offset, and GPU address
  // for the most recent constant upload.  When up_to_date is true, the draw
  // reuses that upload through the top-level argument buffer instead of
  // gathering and uploading the same CBV again.
  struct ConstantBufferBinding {
    MTL::Buffer* buffer = nullptr;
    NS::UInteger offset = 0;
    uint64_t gpu_address = 0;
    size_t size = 0;
    // Guest frame that originally allocated the pool slice this binding
    // references. The upload pool is frame-lifetime here, matching D3D12 and
    // Vulkan so command-buffer rollover inside one frame doesn't force a
    // re-upload of unchanged constants.
    uint64_t upload_frame = 0;
    bool up_to_date = false;
  };
  static constexpr size_t kConstantPayloadSharedStage = kStageCount;
  static constexpr size_t kConstantPayloadCacheMaxBytes = 64 * 1024 * 1024;
  struct ConstantPayloadCacheEntry {
    size_t cbv_slot = 0;
    size_t stage_index = kConstantPayloadSharedStage;
    size_t size = 0;
    ConstantBufferBinding binding;
    std::vector<uint8_t> bytes;
  };
  using StageRootArgumentEntries =
      std::array<uint64_t, kTopLevelABSlotsPerTable>;
  struct StageRootArgumentAllocation {
    MTL::Buffer* buffer = nullptr;
    NS::UInteger offset = 0;
    uint64_t gpu_address = 0;
    // Same frame-lifetime rationale as ConstantBufferBinding.
    uint64_t upload_frame = 0;
    bool valid = false;
  };
  // Mirror D3DMetal's model: track the current graphics root argument entries,
  // patch the logical slots that changed, and write one fresh frame-lifetime
  // table snapshot for Metal to bind.
  StageRootArgumentEntries BuildGraphicsRootArgumentEntries(
      const UniformBufferInfo& uniforms, bool shared_memory_is_uav,
      bool use_tessellation_emulation) const;
  bool AllocateStageRootArgument(size_t stage_index,
                                 const StageRootArgumentEntries& entries,
                                 StageRootArgumentAllocation& allocation_out);
  bool TryReuseConstantPayloadFromCache(ConstantBufferBinding& binding,
                                        size_t cbv_slot, size_t stage_index,
                                        const uint8_t* payload, size_t size);
  void StoreConstantPayloadInCache(const ConstantBufferBinding& binding,
                                   size_t cbv_slot, size_t stage_index,
                                   const uint8_t* payload, size_t size);
  ConstantBufferBinding cbuffer_binding_system_;
  ConstantBufferBinding cbuffer_binding_float_vertex_;
  ConstantBufferBinding cbuffer_binding_float_pixel_;
  ConstantBufferBinding cbuffer_binding_bool_loop_;
  // Fetch constants are one physical register file, but each shader stage gets
  // its own snapshot so one stage's fetch-register churn doesn't force the
  // other stage's root CBV pointer to move.
  std::array<ConstantBufferBinding, kStageCount> cbuffer_binding_fetch_ = {};
  ConstantBufferBinding cbuffer_binding_descriptor_indices_vertex_;
  ConstantBufferBinding cbuffer_binding_descriptor_indices_pixel_;

  // Per-CBV payload caches.  We always honour the "register write dirties
  // live constant range" semantic from metal_command_processor.cc (matches
  // D3D12/Vulkan).  But after packing the new payload into scratch, if the
  // bytes equal the previous upload we keep the previous pool slice instead
  // of allocating a fresh one.  Mirrors the descriptor-indices
  // current_/scratch_ pattern in PrepareDrawConstants.
  std::vector<uint8_t> current_payload_system_;
  std::vector<uint8_t> current_payload_float_vertex_;
  std::vector<uint8_t> current_payload_float_pixel_;
  std::vector<uint8_t> current_payload_bool_loop_;
  std::array<std::vector<uint8_t>, kStageCount> current_payload_fetch_;
  std::vector<uint8_t> scratch_payload_system_;
  std::vector<uint8_t> scratch_payload_float_vertex_;
  std::vector<uint8_t> scratch_payload_float_pixel_;
  std::vector<uint8_t> scratch_payload_bool_loop_;
  std::array<std::vector<uint8_t>, kStageCount> scratch_payload_fetch_;
  std::unordered_map<uint64_t, std::vector<ConstantPayloadCacheEntry>>
      constant_payload_cache_;
  size_t constant_payload_cache_bytes_ = 0;
  std::array<DxbcShader::FetchConstantDwordMask, kStageCount>
      fetch_constant_dirty_masks_ = {};

  // Float constant usage bitmaps for the current shader pair.
  // Used to gate WriteRegister invalidation: only dirty the float CBV
  // when the written register is in the current shader's bitmap.
  // Matches D3D12's current_float_constant_map_vertex_/pixel_ at
  // d3d12_command_processor.h:829-830.
  uint64_t current_float_constant_map_vertex_[4] = {};
  uint64_t current_float_constant_map_pixel_[4] = {};
  size_t current_texture_layout_uid_vertex_ = 0;
  size_t current_texture_layout_uid_pixel_ = 0;
  size_t current_sampler_layout_uid_vertex_ = 0;
  size_t current_sampler_layout_uid_pixel_ = 0;
  std::vector<MetalTextureCache::TextureSRVKey>
      current_texture_srv_keys_vertex_;
  std::vector<MetalTextureCache::TextureSRVKey> current_texture_srv_keys_pixel_;
  std::vector<MetalTextureCache::SamplerParameters> current_samplers_vertex_;
  std::vector<MetalTextureCache::SamplerParameters> current_samplers_pixel_;
  std::vector<MTL::Texture*> current_texture_bindless_resources_vertex_;
  std::vector<MTL::Texture*> current_texture_bindless_resources_pixel_;
  std::vector<uint32_t> current_descriptor_indices_vertex_;
  std::vector<uint32_t> current_descriptor_indices_pixel_;
  std::vector<uint32_t> scratch_texture_bindless_indices_vertex_;
  std::vector<MTL::Texture*> scratch_texture_bindless_resources_vertex_;
  std::vector<uint32_t> scratch_sampler_bindless_indices_vertex_;
  std::vector<uint32_t> scratch_texture_bindless_indices_pixel_;
  std::vector<MTL::Texture*> scratch_texture_bindless_resources_pixel_;
  std::vector<uint32_t> scratch_sampler_bindless_indices_pixel_;
  std::vector<uint32_t> scratch_descriptor_indices_vertex_;
  std::vector<uint32_t> scratch_descriptor_indices_pixel_;
  std::array<bool, kStageCount> current_bindless_stage_root_valid_ = {};
  std::array<bool, kStageCount>
      current_bindless_stage_root_frame_open_rebuild_pending_ = {};
  std::array<uint64_t, kStageCount> current_bindless_stage_root_serials_ = {};
  RenderResourceSet current_bindless_fixed_resource_set_;
  RenderResourceSet current_bindless_texture_resource_set_;
  RenderResourceSet current_bindless_root_resource_set_;
  uint64_t current_bindless_fixed_resource_source_serial_ = 0;
  uint64_t current_bindless_texture_resource_source_serial_ = 0;
  uint64_t current_bindless_root_resource_source_serial_ = 0;
  uint64_t render_encoder_bindless_fixed_resources_serial_ = 0;
  uint64_t render_encoder_bindless_texture_resources_serial_ = 0;
  uint64_t render_encoder_bindless_root_resources_serial_ = 0;
  std::array<uint64_t, kStageCount>
      render_encoder_bindless_stage_root_bind_serials_ = {};
  bool render_encoder_bindless_table_bind_mesh_path_ = false;
  bool render_encoder_bindless_table_bind_tessellation_ = false;
  std::array<StageRootArgumentAllocation, kStageCount>
      current_bindless_stage_root_arguments_ = {};
  std::array<StageRootArgumentEntries, kStageCount>
      current_bindless_stage_root_entries_ = {};
  std::array<bool, kStageCount> current_bindless_stage_root_entries_valid_ = {};
  std::array<std::array<uint64_t, kCbvSlotCount>, kStageCount>
      current_bindless_cbv_gpu_addresses_ = {};
  std::array<std::array<MTL::Buffer*, kCbvSlotCount>, kStageCount>
      current_bindless_cbv_buffers_ = {};
  std::array<std::array<NS::UInteger, kCbvSlotCount>, kStageCount>
      current_bindless_cbv_offsets_ = {};
  std::array<uint32_t, kStageCount> current_bindless_active_cbv_masks_ = {};
  std::array<DxbcShader::FetchConstantDwordMask, kStageCount>
      current_fetch_constant_dword_masks_ = {};
  bool current_bindless_shared_memory_is_uav_ = false;

  // Pool for frame-lifetime constant and top-level argument allocations
  // (replaces the ring's uniforms_buffer_ for constant data).
  std::unique_ptr<MetalUploadBufferPool> constant_buffer_pool_;
  enum class RenderEncoderBufferStage : uint32_t {
    kVertex,
    kFragment,
    kObject,
    kMesh,
    kCount,
  };
  struct RenderEncoderBufferBinding {
    MTL::Buffer* buffer = nullptr;
    NS::UInteger offset = 0;
    bool valid = false;
  };
  static constexpr size_t kTrackedRenderEncoderBufferBindingCount = 32;
  std::array<std::array<RenderEncoderBufferBinding,
                        kTrackedRenderEncoderBufferBindingCount>,
             size_t(RenderEncoderBufferStage::kCount)>
      render_encoder_buffer_bindings_ = {};
  void ResetRenderEncoderBufferBindings();
  void InvalidateRenderEncoderBufferBinding(RenderEncoderBufferStage stage,
                                            NS::UInteger index);
  void SetRenderEncoderBuffer(RenderEncoderBufferStage stage,
                              MTL::Buffer* buffer, NS::UInteger offset,
                              NS::UInteger index);
  void SetRenderEncoderVertexBuffer(MTL::Buffer* buffer, NS::UInteger offset,
                                    NS::UInteger index);
  void SetRenderEncoderFragmentBuffer(MTL::Buffer* buffer, NS::UInteger offset,
                                      NS::UInteger index);
  void SetRenderEncoderObjectBuffer(MTL::Buffer* buffer, NS::UInteger offset,
                                    NS::UInteger index);
  void SetRenderEncoderMeshBuffer(MTL::Buffer* buffer, NS::UInteger offset,
                                  NS::UInteger index);
  // Track which heap buffer binds have been set on the current encoder.
  bool heap_binds_set_on_encoder_ = false;

  std::atomic<uint64_t> completed_command_buffers_{0};
  std::atomic<uint32_t> pending_completion_handlers_{0};
  std::mutex completion_mutex_;
  std::condition_variable completion_cond_;
  uint64_t submission_current_ = 0;
  uint64_t submission_completed_processed_ = 0;
  static constexpr uint32_t kMaxFramesInFlight = 3;
  // Guest frame index. Transient constant and MSC root argument allocations are
  // tagged with this, not with Metal command-buffer submissions, so they can be
  // reused across mid-frame command-buffer rollover.
  uint64_t frame_current_ = 1;
  uint64_t frame_completed_ = 0;
  uint64_t closed_frame_submissions_[kMaxFramesInFlight] = {};

  bool submission_has_draws_ = false;

  std::vector<PendingSharedMemoryWrite> pending_shared_memory_writes_;
  MTL::RenderStages active_render_encoder_shared_memory_write_stages_ =
      MTL::RenderStages(0);

  TraceWriter trace_writer_{nullptr};
  uint64_t diagnostic_swap_count_ = 0;

  // Memexport tracking for shared memory invalidation.
  std::vector<draw_util::MemExportRange> memexport_ranges_;

  bool gamma_ramp_256_entry_table_up_to_date_ = false;
  bool gamma_ramp_pwl_up_to_date_ = false;
};

}  // namespace metal
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_METAL_METAL_COMMAND_PROCESSOR_H_
