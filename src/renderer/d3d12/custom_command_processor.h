// custom_command_processor.h - Custom D3D12 renderer: command processor.
//
// Subclass of the SDK's backend-agnostic rex::graphics::CommandProcessor. At
// M0 (boot) this is a no-op backend: it inherits the entire Xenos command
// stream pipeline (ring buffer, MMIO register file, PM4 packet decode,
// interrupts, vblank pacing) from the base class and just counts what it
// sees, so the guest can boot and the progress counters climb. Real D3D12
// rendering lands in M1+.
//
// See plans/custom-d3d12-renderer.md.

#pragma once

#include <rex/graphics/command_processor.h>
#include <rex/system/kernel_state.h>

namespace customrenderer {
namespace d3d12 {

class CustomGraphicsSystem;

class CustomCommandProcessor final : public rex::graphics::CommandProcessor {
 public:
  CustomCommandProcessor(rex::graphics::GraphicsSystem* graphics_system,
                         rex::system::KernelState* kernel_state);
  ~CustomCommandProcessor() override;

 protected:
  // The base class provides default (true / no-op) bodies for these; we
  // declare them to satisfy the pure-virtual interface and to track
  // progress.
  bool SetupContext() override;
  void ShutdownContext() override;

  // No-op present: the guest keeps advancing (vblank pacing, frame limiter
  // and the F3 FPS counter are all handled by the base class around this
  // call). M1 renders the guest front buffer.
  void IssueSwap(uint32_t frontbuffer_ptr, uint32_t frontbuffer_width,
                 uint32_t frontbuffer_height) override;

  // M4: Xenos ALU -> DXBC -> DXIL. For now: count and pretend success is
  // not needed - returning nullptr makes the base class drop the shader and
  // the backend (us) skips the draw.
  rex::graphics::Shader* LoadShader(rex::graphics::xenos::ShaderType shader_type,
                                    uint32_t guest_address, const uint32_t* host_address,
                                    uint32_t dword_count) override;

  // M4: real draws. Count and swallow.
  bool IssueDraw(rex::graphics::xenos::PrimitiveType prim_type, uint32_t index_count,
                 IndexBufferInfo* index_buffer_info, bool major_mode_explicit) override;

  // M3: blits / copy engine. Count and swallow.
  bool IssueCopy() override;

  // Wrap the base register write with progress tracking. The base does all
  // the real work (register file, scratch writeback, coherency flags); we
  // must call it unconditionally.
  void WriteRegister(uint32_t index, uint32_t value) override;

 private:
  void LogFirstSwap(uint32_t frontbuffer_ptr, uint32_t width, uint32_t height);
  void LogFirstInterestingRegister(uint32_t index, uint32_t value);
};

}  // namespace d3d12
}  // namespace customrenderer
