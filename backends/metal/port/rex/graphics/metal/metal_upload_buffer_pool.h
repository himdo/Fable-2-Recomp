/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license.                                          *
 * See backends/metal/licenses/XeniOS-LICENSE for the full terms.             *
 ******************************************************************************
 */

#ifndef XENIA_GPU_METAL_METAL_UPLOAD_BUFFER_POOL_H_
#define XENIA_GPU_METAL_METAL_UPLOAD_BUFFER_POOL_H_

#include <functional>

#include "rex/ui/graphics_upload_buffer_pool.h"
#include "rex/ui/metal/metal_api.h"

namespace rex {
namespace graphics {
namespace metal {

class MetalUploadBufferPool : public ui::GraphicsUploadBufferPool {
 public:
  MetalUploadBufferPool(MTL::Device* device,
                        size_t page_size = kDefaultPageSize);
  ~MetalUploadBufferPool() override;

  using PageCreatedCallback = std::function<void(MTL::Buffer*)>;
  void SetPageCreatedCallback(PageCreatedCallback callback);

  // Request a region of the upload buffer. Returns CPU-writable pointer.
  // buffer_out receives the MTL::Buffer* for encoder binding.
  // offset_out receives the byte offset within that buffer.
  // gpu_address_out receives the GPU virtual address (buffer gpuAddress +
  // offset).
  uint8_t* Request(uint64_t submission_index, size_t size, size_t alignment,
                   MTL::Buffer** buffer_out, size_t& offset_out,
                   uint64_t& gpu_address_out);

  // Partial request (may return less than requested).
  uint8_t* RequestPartial(uint64_t submission_index, size_t size,
                          size_t alignment, MTL::Buffer** buffer_out,
                          size_t& offset_out, uint64_t& gpu_address_out,
                          size_t& size_out);

 protected:
  Page* CreatePageImplementation() override;

 private:
  struct MetalPage : public Page {
    MetalPage(MTL::Buffer* buffer, void* mapping);
    ~MetalPage() override;
    MTL::Buffer* buffer_;
    void* mapping_;
    uint64_t gpu_address_;
  };

  MTL::Device* device_;
  PageCreatedCallback page_created_callback_;
};

}  // namespace metal
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_METAL_METAL_UPLOAD_BUFFER_POOL_H_
