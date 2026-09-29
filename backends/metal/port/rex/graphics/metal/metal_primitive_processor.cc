/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license.                                          *
 * See backends/metal/licenses/XeniOS-LICENSE for the full terms.             *
 ******************************************************************************
 */

#include "rex/graphics/metal/metal_primitive_processor.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>

#include "rex/assert.h"
#include "rex/logging.h"
#include "rex/graphics/metal/metal_command_processor.h"
#include "rex/graphics/metal/metal_upload_buffer_pool.h"

namespace rex {
namespace graphics {
namespace metal {

MetalPrimitiveProcessor::MetalPrimitiveProcessor(
    MetalCommandProcessor& command_processor, const RegisterFile& register_file,
    rex::memory::Memory& memory, TraceWriter& trace_writer, SharedMemory& shared_memory)
    : PrimitiveProcessor(register_file, memory, trace_writer, shared_memory),
      command_processor_(command_processor) {}

MetalPrimitiveProcessor::~MetalPrimitiveProcessor() { Shutdown(true); }

bool MetalPrimitiveProcessor::Initialize() {
  // MSC path uses mesh shaders / geometry shaders for point sprites and
  // rectangle lists, so no vertex-shader expansion is needed.
  if (!InitializeCommon(true,   // full_32bit_vertex_indices_supported
                        false,  // triangle_fans_supported (will convert)
                        false,  // line_loops_supported (will convert)
                        false,  // quad_lists_supported (will convert)
                        true,   // point_sprites_without_expansion
                        true))  // rect_lists_without_expansion
  {
    Shutdown();
    return false;
  }
  frame_index_buffer_pool_ = std::make_unique<MetalUploadBufferPool>(
      command_processor_.GetMetalDevice(),
      std::max(size_t(kMinRequiredConvertedIndexBufferSize),
               ui::GraphicsUploadBufferPool::kDefaultPageSize));

  REXLOG_INFO("MetalPrimitiveProcessor initialized (MSC path)");
  return true;
}

void MetalPrimitiveProcessor::Shutdown(bool from_destructor) {
  converted_index_buffers_.clear();
  frame_index_buffer_pool_.reset();

  // Release built-in index buffer
  if (builtin_index_buffer_) {
    builtin_index_buffer_->release();
    builtin_index_buffer_ = nullptr;
    builtin_index_buffer_size_ = 0;
  }
  if (!from_destructor) {
    ShutdownCommon();
  }
}

void MetalPrimitiveProcessor::BeginFrame() {
  converted_index_buffers_.clear();
  if (frame_index_buffer_pool_) {
    frame_index_buffer_pool_->Reclaim(
        command_processor_.GetCompletedSubmission());
  }
}

void MetalPrimitiveProcessor::EndFrame() {
  ClearPerFrameCache();
  converted_index_buffers_.clear();
}

bool MetalPrimitiveProcessor::RequestGuestIndexSharedMemoryRange(
    uint32_t guest_index_base, uint32_t guest_index_buffer_needed_bytes,
    ProcessedIndexBufferType index_buffer_type) {
  // Guest index data read by the GPU is collected by IssueDraw and
  // materialized with the prepared draw queue. Requesting it here forces a
  // separate immediate shared-memory upload before the queue can coalesce it.
  (void)guest_index_base;
  (void)guest_index_buffer_needed_bytes;
  (void)index_buffer_type;
  return true;
}

MTL::Buffer* MetalPrimitiveProcessor::GetConvertedIndexBuffer(
    size_t handle, uint64_t& offset_bytes_out) const {
  if (handle >= converted_index_buffers_.size()) {
    REXLOG_ERROR("Converted index buffer handle {} is out of range {}", handle,
           converted_index_buffers_.size());
    offset_bytes_out = 0;
    return nullptr;
  }

  const ConvertedIndexBufferBinding& binding = converted_index_buffers_[handle];
  offset_bytes_out = binding.offset_bytes;
  return binding.buffer;
}

bool MetalPrimitiveProcessor::InitializeBuiltinIndexBuffer(
    size_t size_bytes, std::function<void(void*)> fill_callback) {
  assert_not_zero(size_bytes);
  assert_null(builtin_index_buffer_);

  MTL::Device* device = command_processor_.GetMetalDevice();

  // Create buffer with shared storage so we can write to it
  builtin_index_buffer_ =
      device->newBuffer(size_bytes, MTL::ResourceStorageModeShared);
  if (!builtin_index_buffer_) {
    REXLOG_ERROR("Failed to create Metal built-in index buffer");
    return false;
  }
  builtin_index_buffer_size_ = size_bytes;

  builtin_index_buffer_->setLabel(NS::String::string(
      "Xenia Built-in Index Buffer", NS::UTF8StringEncoding));

  // Fill the buffer with built-in indices
  void* buffer_data = builtin_index_buffer_->contents();
  fill_callback(buffer_data);

  REXLOG_INFO("Created Metal built-in index buffer ({} bytes)", size_bytes);
  return true;
}

void* MetalPrimitiveProcessor::RequestHostConvertedIndexBufferForCurrentFrame(
    xenos::IndexFormat format, uint32_t index_count, bool coalign_for_simd,
    uint32_t coalignment_original_address, size_t& backend_handle_out) {
  if (!frame_index_buffer_pool_) {
    backend_handle_out = 0;
    return nullptr;
  }

  size_t index_size = format == xenos::IndexFormat::kInt16 ? sizeof(uint16_t)
                                                           : sizeof(uint32_t);
  size_t request_size =
      index_size * index_count +
      (coalign_for_simd ? XE_GPU_PRIMITIVE_PROCESSOR_SIMD_SIZE : 0);
  MTL::Buffer* buffer = nullptr;
  size_t offset = 0;
  uint64_t gpu_address = 0;
  uint8_t* mapping = frame_index_buffer_pool_->Request(
      command_processor_.GetCurrentSubmission(), request_size, index_size,
      &buffer, offset, gpu_address);
  if (!mapping || !buffer) {
    REXLOG_ERROR("Failed to allocate Metal index buffer for primitive conversion");
    backend_handle_out = 0;
    return nullptr;
  }

  if (coalign_for_simd) {
    ptrdiff_t coalignment_offset =
        GetSimdCoalignmentOffset(mapping, coalignment_original_address);
    mapping += coalignment_offset;
    gpu_address += uint64_t(coalignment_offset);
  }

  backend_handle_out = converted_index_buffers_.size();
  converted_index_buffers_.push_back(
      {buffer, gpu_address - buffer->gpuAddress()});

  return mapping;
}

}  // namespace metal
}  // namespace gpu
}  // namespace xe
