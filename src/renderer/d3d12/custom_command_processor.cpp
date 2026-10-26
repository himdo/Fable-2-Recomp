// custom_command_processor.cpp - The custom D3D12 renderer.
//
// CustomCommandProcessor delegates every render operation to the built-in
// D3D12CommandProcessor (the known-working renderer) and adds the custom
// renderer's progress-HUD accounting. This is the guest-facing command
// processor: CustomGraphicsSystem returns an instance of this class.
#include "custom_command_processor.h"

#include "../common/progress_checklist.h"

#include <rex/logging.h>

namespace customrenderer {
namespace d3d12 {

namespace {
// Progress checklist indices (order fixed by progress::Init()).
constexpr int kFeatRegisterStream = 2;
constexpr int kFeatSwap = 3;
constexpr int kFeatCopies = 7;
constexpr int kFeatShaders = 9;
constexpr int kFeatDraws = 10;
}  // namespace

CustomCommandProcessor::CustomCommandProcessor(
    rex::graphics::d3d12::D3D12GraphicsSystem* graphics_system,
    rex::system::KernelState* kernel_state)
    : D3D12CommandProcessor(graphics_system, kernel_state) {}

CustomCommandProcessor::~CustomCommandProcessor() = default;

bool CustomCommandProcessor::SetupContext() {
  bool ok = D3D12CommandProcessor::SetupContext();
  REXGPU_INFO("Custom D3D12 renderer: context ready via built-in D3D12 engine (ok={})", ok);
  return ok;
}

void CustomCommandProcessor::ShutdownContext() { D3D12CommandProcessor::ShutdownContext(); }

void CustomCommandProcessor::WriteRegister(uint32_t index, uint32_t value) {
  progress::Bump(kFeatRegisterStream);
  D3D12CommandProcessor::WriteRegister(index, value);
}

rex::graphics::Shader* CustomCommandProcessor::LoadShader(
    rex::graphics::xenos::ShaderType shader_type, uint32_t guest_address,
    const uint32_t* host_address, uint32_t dword_count) {
  rex::graphics::Shader* shader =
      D3D12CommandProcessor::LoadShader(shader_type, guest_address, host_address, dword_count);
  if (shader != nullptr) {
    progress::Bump(kFeatShaders);
  }
  return shader;
}

bool CustomCommandProcessor::IssueDraw(rex::graphics::xenos::PrimitiveType prim_type,
                                       uint32_t index_count,
                                       rex::graphics::CommandProcessor::IndexBufferInfo* index_buffer_info,
                                       bool major_mode_explicit) {
  progress::Bump(kFeatDraws);
  return D3D12CommandProcessor::IssueDraw(prim_type, index_count, index_buffer_info,
                                          major_mode_explicit);
}

bool CustomCommandProcessor::IssueCopy() {
  progress::Bump(kFeatCopies);
  return D3D12CommandProcessor::IssueCopy();
}

void CustomCommandProcessor::IssueSwap(uint32_t frontbuffer_ptr, uint32_t frontbuffer_width,
                                       uint32_t frontbuffer_height) {
  progress::Bump(kFeatSwap);
  ++swap_count_;
  D3D12CommandProcessor::IssueSwap(frontbuffer_ptr, frontbuffer_width, frontbuffer_height);
}

}  // namespace d3d12
}  // namespace customrenderer
