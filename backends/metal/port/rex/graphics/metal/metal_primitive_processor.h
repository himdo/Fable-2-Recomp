/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license.                                          *
 * See backends/metal/licenses/XeniOS-LICENSE for the full terms.             *
 ******************************************************************************
 */

#ifndef XENIA_GPU_METAL_METAL_PRIMITIVE_PROCESSOR_H_
#define XENIA_GPU_METAL_METAL_PRIMITIVE_PROCESSOR_H_

#include <memory>
#include <vector>
#include "third_party/metal-cpp/Metal/Metal.hpp"

#include "rex/graphics/primitive_processor.h"

namespace rex {
namespace graphics {
namespace metal {

class MetalCommandProcessor;
class MetalUploadBufferPool;

class MetalPrimitiveProcessor : public PrimitiveProcessor {
 public:
  MetalPrimitiveProcessor(MetalCommandProcessor& command_processor,
                          const RegisterFile& register_file, rex::memory::Memory& memory,
                          TraceWriter& trace_writer,
                          SharedMemory& shared_memory);
  ~MetalPrimitiveProcessor();

  bool Initialize();
  void Shutdown(bool from_destructor = false);

  void BeginFrame();
  void EndFrame();

  MTL::Buffer* GetBuiltinIndexBuffer() const { return builtin_index_buffer_; }
  MTL::Buffer* GetConvertedIndexBuffer(size_t handle,
                                       uint64_t& offset_bytes_out) const;

 protected:
  bool InitializeBuiltinIndexBuffer(
      size_t size_bytes, std::function<void(void*)> fill_callback) override;

  void* RequestHostConvertedIndexBufferForCurrentFrame(
      xenos::IndexFormat format, uint32_t index_count, bool coalign_for_simd,
      uint32_t coalignment_original_address,
      size_t& backend_handle_out) override;
  bool RequestGuestIndexSharedMemoryRange(
      uint32_t guest_index_base, uint32_t guest_index_buffer_needed_bytes,
      ProcessedIndexBufferType index_buffer_type) override;

 private:
  MetalCommandProcessor& command_processor_;

  struct ConvertedIndexBufferBinding {
    MTL::Buffer* buffer = nullptr;
    uint64_t offset_bytes = 0;
  };

  std::vector<ConvertedIndexBufferBinding> converted_index_buffers_;

  // Built-in index buffer for primitive type conversion
  MTL::Buffer* builtin_index_buffer_ = nullptr;
  size_t builtin_index_buffer_size_ = 0;
  std::unique_ptr<MetalUploadBufferPool> frame_index_buffer_pool_;
};

}  // namespace metal
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_METAL_METAL_PRIMITIVE_PROCESSOR_H_
