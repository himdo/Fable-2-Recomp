// custom_graphics_system.cpp - see custom_graphics_system.h.

#include "custom_graphics_system.h"

#include "custom_command_processor.h"
#include "../common/progress_checklist.h"
#include <rex/graphics/d3d12/command_processor.h>

#include <rex/logging.h>
#include <rex/ui/d3d12/d3d12_provider.h>

namespace customrenderer {
namespace d3d12 {

namespace {
// Progress checklist indices (order fixed by progress::Init()).
constexpr int kFeatRing = 0;
constexpr int kFeatInterrupts = 1;
}  // namespace

void CustomGraphicsSystem::CreateProvider(bool with_presentation) {
  provider_ = rex::ui::d3d12::D3D12Provider::Create();
  if (provider_) {
    REXGPU_INFO("[custom-renderer] D3D12 provider created (presentation={})",
                with_presentation);
  }
}

std::unique_ptr<rex::graphics::CommandProcessor>
CustomGraphicsSystem::CreateCommandProcessor() {
  // The custom renderer IS the guest-facing command processor. It drives the
  // frame and delegates the actual D3D12 rendering to the built-in engine.
  return std::unique_ptr<rex::graphics::CommandProcessor>(
      new CustomCommandProcessor(this, kernel_state_));
}

void CustomGraphicsSystem::InitializeRingBuffer(uint32_t ptr, uint32_t size_log2) {
  progress::Bump(kFeatRing);
  REXGPU_INFO("[custom-renderer] ring buffer: ptr={:#010x} size={} bytes",
              ptr, uint64_t(1) << (size_log2 + 3));
  rex::graphics::GraphicsSystem::InitializeRingBuffer(ptr, size_log2);
}

void CustomGraphicsSystem::EnableReadPointerWriteBack(uint32_t ptr,
                                                      uint32_t block_size_log2) {
  REXGPU_INFO("[custom-renderer] read-pointer writeback: ptr={:#010x} block_log2={}",
              ptr, block_size_log2);
  rex::graphics::GraphicsSystem::EnableReadPointerWriteBack(ptr, block_size_log2);
}

void CustomGraphicsSystem::SetInterruptCallback(uint32_t callback, uint32_t user_data) {
  progress::Bump(kFeatInterrupts);
  REXGPU_INFO("[custom-renderer] interrupt callback: cb={:#010x} user_data={:#010x}",
              callback, user_data);
  rex::graphics::GraphicsSystem::SetInterruptCallback(callback, user_data);
}

}  // namespace d3d12
}  // namespace customrenderer
