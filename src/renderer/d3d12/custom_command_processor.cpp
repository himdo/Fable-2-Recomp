// custom_command_processor.cpp - see custom_command_processor.h.

#include "custom_command_processor.h"

#include "../common/progress_checklist.h"

#include <rex/graphics/registers.h>
#include <rex/logging.h>

namespace customrenderer {
namespace d3d12 {

namespace {
// Progress checklist indices (order fixed by progress::Init()).
constexpr int kFeatRegisterStream = 2;
constexpr int kFeatSwap = 3;
constexpr int kFeatCopies = 6;
constexpr int kFeatShaders = 8;
constexpr int kFeatDraws = 9;
}  // namespace

CustomCommandProcessor::CustomCommandProcessor(
    rex::graphics::GraphicsSystem* graphics_system,
    rex::system::KernelState* kernel_state)
    : rex::graphics::CommandProcessor(graphics_system, kernel_state) {}

CustomCommandProcessor::~CustomCommandProcessor() = default;

bool CustomCommandProcessor::SetupContext() {
  // M0: no D3D12 objects are needed yet (nothing is rendered). The base
  // class handles the worker thread; this just has to succeed so
  // WorkerThreadMain does not FatalError.
  REXGPU_INFO("[custom-renderer] command processor context ready (M0 no-op backend)");
  return rex::graphics::CommandProcessor::SetupContext();
}

void CustomCommandProcessor::ShutdownContext() {
  rex::graphics::CommandProcessor::ShutdownContext();
}

void CustomCommandProcessor::IssueSwap(uint32_t frontbuffer_ptr, uint32_t frontbuffer_width,
                                       uint32_t frontbuffer_height) {
  progress::Bump(kFeatSwap);
  LogFirstSwap(frontbuffer_ptr, frontbuffer_width, frontbuffer_height);
  // Intentionally no present: the window stays black until M1.
}

rex::graphics::Shader* CustomCommandProcessor::LoadShader(
    rex::graphics::xenos::ShaderType shader_type, uint32_t guest_address,
    const uint32_t* host_address, uint32_t dword_count) {
  (void)host_address;
  progress::Bump(kFeatShaders);
  static bool logged = false;
  if (!logged) {
    logged = true;
    REXGPU_INFO(
        "[custom-renderer] first shader load (no-op until M4): type={} addr={:#010x} "
        "dwords={}",
        uint32_t(shader_type), guest_address, dword_count);
  }
  return nullptr;
}

bool CustomCommandProcessor::IssueDraw(rex::graphics::xenos::PrimitiveType prim_type,
                                       uint32_t index_count,
                                       IndexBufferInfo* index_buffer_info,
                                       bool major_mode_explicit) {
  (void)prim_type;
  (void)index_count;
  (void)index_buffer_info;
  (void)major_mode_explicit;
  progress::Bump(kFeatDraws);
  static bool logged = false;
  if (!logged) {
    logged = true;
    REXGPU_INFO("[custom-renderer] first draw (no-op until M4): prim={} indices={}",
                uint32_t(prim_type), index_count);
  }
  return true;
}

bool CustomCommandProcessor::IssueCopy() {
  progress::Bump(kFeatCopies);
  static bool logged = false;
  if (!logged) {
    logged = true;
    REXGPU_INFO("[custom-renderer] first copy/blit (no-op until M3)");
  }
  return true;
}

void CustomCommandProcessor::WriteRegister(uint32_t index, uint32_t value) {
  progress::Bump(kFeatRegisterStream);
  rex::graphics::CommandProcessor::WriteRegister(index, value);
  LogFirstInterestingRegister(index, value);
}

void CustomCommandProcessor::LogFirstSwap(uint32_t frontbuffer_ptr, uint32_t width,
                                          uint32_t height) {
  auto* feat = progress::Find(kFeatSwap);
  if (!feat || feat->first_logged) {
    return;
  }
  feat->first_logged = true;
  REXGPU_INFO("[custom-renderer] first IssueSwap: frontbuffer={:#010x} {}x{}",
              frontbuffer_ptr, width, height);
}

void CustomCommandProcessor::LogFirstInterestingRegister(uint32_t index, uint32_t value) {
  // One-time visibility into the state the guest is programming, to confirm
  // the base-class register decode is live (M0 acceptance evidence).
  struct Interesting {
    uint32_t index;
    const char* name;
    bool logged;
  };
  static Interesting interesting[] = {
      {rex::graphics::XE_GPU_REG_VGT_DRAW_INITIATOR, "VGT_DRAW_INITIATOR", false},
      {rex::graphics::XE_GPU_REG_RB_COPY_DEST_BASE, "RB_COPY_DEST_BASE", false},
      {rex::graphics::XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL, "PA_SC_WINDOW_SCISSOR_TL",
       false},
  };
  for (auto& reg : interesting) {
    if (!reg.logged && reg.index == index) {
      reg.logged = true;
      REXGPU_INFO("[custom-renderer] first write to {}: {:08X}", reg.name, value);
    }
  }
}

}  // namespace d3d12
}  // namespace customrenderer
