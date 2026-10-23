// custom_command_processor.h - Custom D3D12 renderer: command processor.
//
// Subclass of the SDK's backend-agnostic rex::graphics::CommandProcessor.
// Inherits the entire Xenos command stream pipeline (ring buffer, MMIO
// register file, PM4 packet decode, interrupts, vblank pacing) from the
// base class.
//
// M1 (first real pixels): IssueSwap reads the guest front buffer (linear
// 8_8_8_8) out of guest memory, converts it on the CPU to the presenter's
// R10G10B10A2 guest-output format (applying the DC LUT gamma table, which
// we mirror here from the register stream), uploads it to a staging buffer
// and copies it into the presenter's guest output surface via
// Presenter::RefreshGuestOutput. No shaders / pipelines yet - textures,
// render targets and draws land in M2-M4.
//
// See plans/custom-d3d12-renderer.md.

#pragma once

#include <cstdint>
#include <wrl/client.h>

#include <d3d12.h>

#include <memory>

#include <rex/graphics/command_processor.h>
#include <rex/system/kernel_state.h>

#include "texture_cache.h"

namespace customrenderer {
namespace d3d12 {

class CustomGraphicsSystem;

class CustomCommandProcessor final : public rex::graphics::CommandProcessor {
 public:
  CustomCommandProcessor(rex::graphics::GraphicsSystem* graphics_system,
                         rex::system::KernelState* kernel_state);
  ~CustomCommandProcessor() override;

 protected:
  bool SetupContext() override;
  void ShutdownContext() override;

  // M1: present the guest front buffer.
  void IssueSwap(uint32_t frontbuffer_ptr, uint32_t frontbuffer_width,
                 uint32_t frontbuffer_height) override;

  // M4: Xenos ALU -> DXBC -> DXIL. Returning nullptr makes the base class
  // drop the shader; draws are then skipped by us too.
  rex::graphics::Shader* LoadShader(rex::graphics::xenos::ShaderType shader_type,
                                    uint32_t guest_address, const uint32_t* host_address,
                                    uint32_t dword_count) override;

  // M4: real draws. Count and swallow.
  bool IssueDraw(rex::graphics::xenos::PrimitiveType prim_type, uint32_t index_count,
                 IndexBufferInfo* index_buffer_info, bool major_mode_explicit) override;

  // M3: blits / copy engine. Count and swallow.
  bool IssueCopy() override;

  // Wrap the base register write with progress tracking + gamma table
  // capture (the base does all the real work; we must call it).
  void WriteRegister(uint32_t index, uint32_t value) override;

 private:
  static constexpr int kQueueFrames = 4;  // in-flight submissions (fence ring)

  // D3D12 handles, owned by the provider (created in SetupContext).
  Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocators_[kQueueFrames];
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list_;
  Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
  void* fence_event_ = nullptr;
  uint64_t fence_value_ = 0;
  uint64_t submission_counter_ = 0;

  // Staging ring: one CPU-written upload buffer per slot; slot s is only
  // rewritten after submission s-kQueueFrames has completed, so the GPU
  // never reads a buffer we are about to overwrite.
  Microsoft::WRL::ComPtr<ID3D12Resource> staging_[kQueueFrames];
  uint8_t* staging_map_[kQueueFrames] = {};
  uint32_t staging_w_ = 0;
  uint32_t staging_h_ = 0;

  // Last-seen front buffer identity (for change-detection logging).
  uint32_t last_front_w_ = 0, last_front_h_ = 0, last_front_ptr_ = 0;
  uint32_t last_front_fmt_ = 0, last_front_tiled_ = 0, last_front_swizzle_ = 0,
      last_front_pitch_ = 0;

  // M2: texture cache (created in SetupContext; exercised in IssueSwap).
  std::unique_ptr<TextureCache> texture_cache_;
  uint64_t texture_request_count_ = 0;

  // DC LUT 256-entry gamma table, mirrored from the register stream
  // (30-bit BGR per entry, same layout as reg::DC_LUT_30_COLOR).
  uint32_t gamma_table_[256] = {};
  uint32_t gamma_lut_r_[256] = {};
  uint32_t gamma_lut_g_[256] = {};
  uint32_t gamma_lut_b_[256] = {};
  bool gamma_lut_dirty_ = true;

  bool EnsureStagingRing(uint32_t width, uint32_t height);
  void RebuildGammaLuts();
  // Converts the guest front buffer into staging_[slot]; returns the bytes
  // converted (0 on any failure).
  uint64_t ConvertFrontbuffer(uint32_t frontbuffer_ptr, uint32_t width, uint32_t height,
                              uint32_t slot);
  // Records the staging->guest-output copy on our command list and submits
  // it on the provider's direct queue (called inside the presenter's
  // RefreshGuestOutput callback).
  bool SubmitGuestOutputCopy(ID3D12Resource* guest_output, uint32_t width, uint32_t height,
                             uint32_t slot);
  void WaitForFenceValue(uint64_t value);

  void LogFirstSwap(uint32_t frontbuffer_ptr, uint32_t width, uint32_t height);
  void LogFirstInterestingRegister(uint32_t index, uint32_t value);
  // M2: request all 32 texture units from the fetch-constant register file so
  // the cache is exercised (new textures are untiled + uploaded on first sight).
  void RequestActiveTextures();
};

}  // namespace d3d12
}  // namespace customrenderer
