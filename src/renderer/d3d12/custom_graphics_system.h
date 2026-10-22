// custom_graphics_system.h - Custom D3D12 renderer: graphics system.
//
// Thin subclass of the SDK's backend-agnostic rex::graphics::GraphicsSystem
// (compiled into this plugin from the SDK source tree, same as the SDK's own
// plugin does). It provides a D3D12 provider and wires in our command
// processor. See plans/custom-d3d12-renderer.md.

#pragma once

#include <memory>
#include <string>

#include <rex/graphics/graphics_system.h>

namespace customrenderer {
namespace d3d12 {

class CustomGraphicsSystem : public rex::graphics::GraphicsSystem {
 public:
  CustomGraphicsSystem() = default;

  std::string name() const override { return "Custom D3D12"; }

  // IGraphicsSystem: forward to the base while reporting progress.
  void InitializeRingBuffer(uint32_t ptr, uint32_t size_log2) override;
  void EnableReadPointerWriteBack(uint32_t ptr, uint32_t block_size_log2) override;
  void SetInterruptCallback(uint32_t callback, uint32_t user_data) override;

 protected:
  void CreateProvider(bool with_presentation) override;
  std::unique_ptr<rex::graphics::CommandProcessor> CreateCommandProcessor() override;
};

}  // namespace d3d12
}  // namespace customrenderer
