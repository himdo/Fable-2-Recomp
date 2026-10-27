// custom_command_processor.h
//
// The custom renderer's guest-facing command processor.
//
// CustomCommandProcessor IS the go-to renderer: CustomGraphicsSystem returns an
// instance of THIS class, and the guest's ring-buffer dispatches into it. It
// reuses the built-in D3D12 rendering engine (rex::graphics::d3d12::
// D3D12CommandProcessor) for the heavy lifting - Xenos->DXBC->DXIL shader
// translation, shared memory, texture/render-target caches, primitive
// processing, the pipeline cache, and the present path - and layers the custom
// renderer's behavior (progress HUD, cvars, and future fast-path hooks) on top.
#pragma once

#include <cstdint>

#include <rex/graphics/command_processor.h>
#include <rex/graphics/d3d12/command_processor.h>
#include <rex/graphics/d3d12/graphics_system.h>
#include <rex/graphics/xenos.h>

namespace rex::system {
class KernelState;
}

namespace customrenderer {
namespace d3d12 {

class CustomCommandProcessor : public rex::graphics::d3d12::D3D12CommandProcessor {
 public:
  CustomCommandProcessor(rex::graphics::d3d12::D3D12GraphicsSystem* graphics_system,
                         rex::system::KernelState* kernel_state);
  ~CustomCommandProcessor() override;

  bool SetupContext() override;
  void ShutdownContext() override;
  void WriteRegister(uint32_t index, uint32_t value) override;
  rex::graphics::Shader* LoadShader(rex::graphics::xenos::ShaderType shader_type,
                                    uint32_t guest_address, const uint32_t* host_address,
                                    uint32_t dword_count) override;
  bool IssueDraw(rex::graphics::xenos::PrimitiveType prim_type, uint32_t index_count,
                 rex::graphics::CommandProcessor::IndexBufferInfo* index_buffer_info,
                 bool major_mode_explicit) override;
  bool IssueCopy() override;
  void IssueSwap(uint32_t frontbuffer_ptr, uint32_t frontbuffer_width,
                 uint32_t frontbuffer_height) override;

 private:
  // M6 (CPU fast path) self-profiling on the GPU-Commands thread.
  void M6MaybeSample();

  uint64_t swap_count_ = 0;
  uint64_t m6_qpc_freq_ = 0;
  int64_t m6_last_qpc_ = 0;
  uint64_t m6_last_cpu100ns_ = 0;
  bool m6_breakdown_ = false;
  uint64_t m6_draw_n = 0, m6_copy_n = 0, m6_swap_n = 0, m6_write_n = 0, m6_shader_n = 0;
  uint64_t m6_draw_ns = 0, m6_copy_ns = 0, m6_swap_ns = 0, m6_write_ns = 0, m6_shader_ns = 0;
};

}  // namespace d3d12
}  // namespace customrenderer
