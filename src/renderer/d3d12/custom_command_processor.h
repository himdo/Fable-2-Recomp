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
#include <unordered_map>
#include <vector>

#include <rex/graphics/command_processor.h>
#include <rex/graphics/pipeline/shader/dxbc_translator.h>
#include <rex/system/kernel_state.h>

#include "texture_cache.h"
#include "render_target_cache.h"

struct IDxbcConverter;

namespace rex::graphics::d3d12 {
class D3D12Shader;
}  // namespace rex::graphics::d3d12

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

  // M3: render-target cache (front buffer -> D3D12 RT, presented via a GPU
  // copy instead of the M1 CPU untile).
  std::unique_ptr<RenderTargetCache> rt_cache_;

  // M3: current front-buffer RT (recreated when the front buffer identity
  // changes). This is the render target M4 draws into and the present source.
  RenderTargetCache::Entry* current_rt_ = nullptr;
  uint32_t current_rt_key_ = 0;
  uint32_t current_rt_w_ = 0, current_rt_h_ = 0;

  // M3: guest clear color (decoded from RB_COLOR_CLEAR | RB_COLOR_CLEAR_LO
  // << 32, 10-bit R/G/B). Set by UpdateGuestClear when the clear registers
  // change; the RT is cleared to this color each frame.
  float guest_clear_r_ = 0.0f, guest_clear_g_ = 0.0f, guest_clear_b_ = 0.0f,
      guest_clear_a_ = 1.0f;
  uint64_t guest_clear_value_ = 0;
  bool guest_clear_valid_ = false;

  // M4: shader cache (ucode hash -> compiled DXIL + the D3D12Shader that owns
  // the translation and the post-translation texture/sampler binding layout).
  struct ShaderEntry {
    std::vector<uint8_t> dxil;
    rex::graphics::d3d12::D3D12Shader* shader = nullptr;  // owns translation
  };
  std::unordered_map<uint64_t, ShaderEntry> shader_cache_;
  // Active shaders (ucode hash -> lightweight Shader) so the base class can
  // track them via active_*_shader() without us leaking a new one per load.
  std::unordered_map<uint64_t, rex::graphics::Shader*> active_shaders_;
  IDxbcConverter* dxbc_converter_ = nullptr;
  ID3D12RootSignature* root_signature_ = nullptr;
  ID3D12PipelineState* current_pso_ = nullptr;

  // M4: shared memory - a 512 MB default-heap buffer mirroring Xenon physical
  // memory. The vertex shader reads vertex data through the R32 buffer SRV
  // (t0); the index data is uploaded to a separate buffer to avoid a state
  // clash (a buffer can't be a SHADER_RESOURCE and an INDEX_BUFFER at once).
  Microsoft::WRL::ComPtr<ID3D12Resource> shared_memory_;
  D3D12_GPU_VIRTUAL_ADDRESS shared_memory_gpu_ = 0;
  uint32_t shared_memory_size_ = 0;

  Microsoft::WRL::ComPtr<ID3D12Resource> index_buffer_;
  D3D12_GPU_VIRTUAL_ADDRESS index_buffer_gpu_ = 0;
  uint32_t index_buffer_size_ = 0;

  // M4: one mapped upload-heap buffer for per-draw constant buffers + vertex /
  // index staging. Constant buffers are 256-byte aligned; offsets are fixed.
  Microsoft::WRL::ComPtr<ID3D12Resource> upload_buffer_;
  uint8_t* upload_buffer_map_ = nullptr;
  D3D12_GPU_VIRTUAL_ADDRESS upload_buffer_gpu_ = 0;
  uint32_t upload_buffer_size_ = 0;
  static constexpr uint32_t kCbSystemOffset = 0;
  static constexpr uint32_t kCbFloatVertexOffset = 1024;
  static constexpr uint32_t kCbFloatPixelOffset = 5120;
  static constexpr uint32_t kCbBoolLoopOffset = 9216;
  static constexpr uint32_t kCbFetchOffset = 9472;
  static constexpr uint32_t kCbDescriptorIndicesOffset = 10240;

  // M4: SRV heap (shared memory at [0], textures at [1..kMaxDrawTextures]) and
  // sampler heap (s0..). Bound as descriptor tables on the root signature.
  ID3D12DescriptorHeap* draw_srv_heap_ = nullptr;
  D3D12_CPU_DESCRIPTOR_HANDLE draw_srv_heap_start_{};
  D3D12_GPU_DESCRIPTOR_HANDLE draw_srv_heap_gpu_{};
  uint32_t draw_srv_desc_size_ = 0;
  ID3D12DescriptorHeap* draw_sampler_heap_ = nullptr;
  D3D12_CPU_DESCRIPTOR_HANDLE draw_sampler_heap_start_{};
  D3D12_GPU_DESCRIPTOR_HANDLE draw_sampler_heap_gpu_{};
  uint32_t draw_sampler_desc_size_ = 0;
  ID3D12DescriptorHeap* draw_uav_heap_ = nullptr;
  D3D12_CPU_DESCRIPTOR_HANDLE draw_uav_heap_start_{};
  D3D12_GPU_DESCRIPTOR_HANDLE draw_uav_heap_gpu_{};
  static constexpr uint32_t kMaxDrawTextures = 32;  // texture SRV table size (t1..t32)

  // M4: pipeline cache (packed VS+PS hash -> PSO).
  std::unordered_map<uint64_t, ID3D12PipelineState*> pipeline_cache_;

  // M4: per-frame draw state. The RT is cleared + drawn into between the first
  // draw and the swap; IssueSwap presents it.
  bool frame_recording_ = false;
  // True while the command list is open (between Reset and Close). Used to close
  // a stale open list before Reset and to avoid double-close.
  bool command_list_open_ = false;
  uint32_t frame_draw_count_ = 0;
  bool first_frame_draw_logged_ = false;
  // DIAG: trivial fullscreen-triangle test (bypasses the Xenos path) to verify
  // the raw D3D12 draw->RT->present pipeline.
  ID3D12RootSignature* debug_tri_root_ = nullptr;
  ID3D12PipelineState* debug_tri_pso_ = nullptr;
  bool debug_tri_enabled_ = false;  // sm_5_0 DXIL can't be a D3D12 PSO; disabled
  bool debug_tri_logged_ = false;
  // M4: the single main render target (color + depth) all draws target and that
  // IssueSwap presents. Keyed by 0 in the RT cache (one persistent framebuffer).
  RenderTargetCache::Entry* main_rt_ = nullptr;
  uint32_t main_rt_w_ = 1280;
  uint32_t main_rt_h_ = 720;
  uint32_t upload_cursor_ = 0;  // staging cursor in the upload buffer
  static constexpr uint32_t kStagingBaseOffset = 16384;
  uint32_t sm_state_ = 0;  // current D3D12_RESOURCE_STATE of the shared memory buffer
  uint32_t ib_state_ = 0;  // current D3D12_RESOURCE_STATE of the index buffer
  uint32_t ub_state_ = 0;  // current D3D12_RESOURCE_STATE of the upload buffer
  uint32_t ds_state_ = 0;  // current D3D12_RESOURCE_STATE of the main RT depth buffer
  bool draw_resources_failed_logged_ = false;
  bool bad_vfetch_logged_ = false;
  // RT pixel readback (diagnostics): RT row -> default-heap buffer (copy dest)
  // -> readback-heap mapped buffer (CPU readable). Uses only valid copy ops.
  Microsoft::WRL::ComPtr<ID3D12Resource> readback_staging_;
  Microsoft::WRL::ComPtr<ID3D12Resource> readback_buf_;
  uint32_t* readback_map_ = nullptr;
  // Shared-memory vertex readback (diagnostic): copy a region of the GPU shared
  // memory buffer to the CPU-mapped readback_buf_ on the first 3D draw, then
  // read it back a few frames later to confirm the vertex upload reached the GPU.
  uint32_t diag_rb_addr_ = 0;
  uint32_t diag_rb_size_ = 0;
  bool diag_rb_pending_ = false;
  uint32_t diag_rb_present_ = 0;
  uint32_t rb_buf_state_ =
      uint32_t(D3D12_RESOURCE_STATE_GENERIC_READ);  // initial state of readback_buf_

  // DC LUT 256-entry gamma table, mirrored from the register stream
  // (30-bit BGR per entry, same layout as reg::DC_LUT_30_COLOR).
  uint32_t gamma_table_[256] = {};
  uint32_t gamma_lut_r_[256] = {};
  uint32_t gamma_lut_g_[256] = {};
  uint32_t gamma_lut_b_[256] = {};
  bool gamma_lut_dirty_ = true;

  // M4: build the shared memory / index buffer / upload buffer / draw heaps.
  bool EnsureDrawResources();
  // M4: create + cache a PSO for a (vs, ps, topology-group) triple (empty input
  // layout, 1 RT). The topology group must match the draw's primitive topology or
  // the driver rejects the draw (PRIMITIVE_TOPOLOGY_MISMATCH_PIPELINE_STATE).
  ID3D12PipelineState* GetOrCreatePipeline(rex::graphics::d3d12::D3D12Shader* vs,
                                           rex::graphics::d3d12::D3D12Shader* ps,
                                           D3D12_PRIMITIVE_TOPOLOGY_TYPE topology_type);
  // M4: begin a frame's draw recording (reset command list, transition RT to
  // RENDER_TARGET, clear, set render targets). Called by the first draw.
  void BeginFrameDrawing();
  // M4: finish the frame (present the RT to the guest output, close + submit).
  bool EndFramePresent(ID3D12Resource* guest_output, uint32_t width, uint32_t height);
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
  // M3: records the RT->guest-output copy (present) on our command list and
  // submits it. The RT is the front buffer, cleared to the guest's clear
  // color and drawn into by M4.
  bool SubmitRTPresent(ID3D12Resource* guest_output, uint32_t width, uint32_t height,
                       RenderTargetCache::Entry* rt, float cr, float cg, float cb,
                       float ca);
  // M3: decodes the guest's clear color from the register file.
  void UpdateGuestClear();
  void WaitForFenceValue(uint64_t value);

  // M4: shader compilation + D3D12 pipeline setup.
  bool EnsureDxilConverter();
  bool EnsureGraphicsPipelineState();
  // Compile (and cache) a guest shader to DXIL. Returns the DXIL byte span, or
  // nullptr on failure.
  const std::vector<uint8_t>* CompileShader(rex::graphics::xenos::ShaderType type,
                                            const uint32_t* ucode, uint32_t dword_count);

  void LogFirstSwap(uint32_t frontbuffer_ptr, uint32_t width, uint32_t height);
  void LogFirstInterestingRegister(uint32_t index, uint32_t value);
  // M2: request all 32 texture units from the fetch-constant register file so
  // the cache is exercised (new textures are untiled + uploaded on first sight).
  void RequestActiveTextures();
};

}  // namespace d3d12
}  // namespace customrenderer
