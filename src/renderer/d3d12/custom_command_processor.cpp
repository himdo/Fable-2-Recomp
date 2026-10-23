// custom_command_processor.cpp - see custom_command_processor.h.

#include "custom_command_processor.h"

#include "../common/progress_checklist.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

#include <windows.h>

#include <rex/graphics/graphics_system.h>
#include <rex/graphics/registers.h>
#include <rex/kernel/xboxkrnl/video.h>
#include <rex/logging.h>
#include <rex/system/xmemory.h>
#include <rex/ui/d3d12/d3d12_presenter.h>
#include <rex/ui/d3d12/d3d12_provider.h>
#include <rex/ui/d3d12/d3d12_util.h>

namespace customrenderer {
namespace d3d12 {

namespace {
// Progress checklist indices (order fixed by progress::Init()).
constexpr int kFeatRegisterStream = 2;
constexpr int kFeatSwap = 3;
constexpr int kFeatSwapBytes = 4;
constexpr int kFeatCopies = 7;
constexpr int kFeatShaders = 9;
constexpr int kFeatDraws = 10;

// R10G10B10A2 packing: [31:30] A, [29:20] B, [19:10] G, [9:0] R.
constexpr uint32_t kOpaqueA2 = 3u << 30;

// Xenon 2D tile byte offset for pixel (x, y). Copied from the SDK's
// texture_util::GetTiledOffset2D (src/graphics/pipeline/texture/util.cpp).
// pitch is the row pitch in pixels; log2_bpp is log2(bytes per pixel).
// The result is a byte offset relative to the surface base (which must be
// aligned to a 32x32 tile).
inline uint32_t TiledByteOffset2D(int32_t x, int32_t y, uint32_t pitch, uint32_t log2_bpp) {
  pitch = (pitch + 31) & ~31u;
  const int32_t macro = ((x >> 5) + (y >> 5) * int32_t(pitch >> 5)) << (log2_bpp + 7);
  const int32_t micro = ((x & 7) + ((y & 0xE) << 2)) << log2_bpp;
  const int32_t offset = macro + ((micro & ~0xF) << 1) + (micro & 0xF) + ((y & 1) << 4);
  return uint32_t(((offset & ~0x1FF) << 3) + ((y & 16) << 7) + ((offset & 0x1C0) << 2) +
                  (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (offset & 0x3F));
}

}  // namespace

CustomCommandProcessor::CustomCommandProcessor(
    rex::graphics::GraphicsSystem* graphics_system,
    rex::system::KernelState* kernel_state)
    : rex::graphics::CommandProcessor(graphics_system, kernel_state) {
  // Linear default gamma ramp (same as the base class): 8-bit input i maps
  // to 10-bit i*1023/255 on every channel. The guest overwrites this via
  // DC_LUT_30_COLOR writes, which we capture in WriteRegister.
  for (int i = 0; i < 256; ++i) {
    const uint32_t v = uint32_t(uint64_t(i) * 1023 / 255);
    gamma_table_[i] = (v << 20) | (v << 10) | v;
  }
}

CustomCommandProcessor::~CustomCommandProcessor() = default;

bool CustomCommandProcessor::SetupContext() {
  // M1: create the minimal D3D12 submission machinery (command allocator
  // ring, one command list, fence, staging ring is created lazily on the
  // first swap). All of it on the provider's device/queue.
  auto* provider =
      static_cast<rex::ui::d3d12::D3D12Provider*>(graphics_system_->provider());
  if (!provider) {
    REXGPU_ERROR("[custom-renderer] no D3D12 provider in SetupContext");
    return false;
  }
  ID3D12Device* device = provider->GetDevice();
  queue_ = provider->GetDirectQueue();

  bool ok = true;
  for (int i = 0; i < kQueueFrames; ++i) {
    ok = ok && SUCCEEDED(device->CreateCommandAllocator(
                            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocators_[i])));
  }
  ok = ok && SUCCEEDED(
                    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              allocators_[0].Get(), nullptr,
                                              IID_PPV_ARGS(&command_list_)));
  if (command_list_) {
    command_list_->Close();
  }
  ok = ok && SUCCEEDED(
                  device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)));
  fence_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  REXGPU_INFO("[custom-renderer] command processor context ready (M1: front-buffer present)");
  return ok && fence_event_ != nullptr &&
         rex::graphics::CommandProcessor::SetupContext();
}

void CustomCommandProcessor::ShutdownContext() {
  for (int i = 0; i < kQueueFrames; ++i) {
    if (staging_map_[i] && staging_[i]) {
      staging_[i]->Unmap(0, nullptr);
      staging_map_[i] = nullptr;
    }
    staging_[i].Reset();
  }
  command_list_.Reset();
  for (int i = 0; i < kQueueFrames; ++i) {
    allocators_[i].Reset();
  }
  fence_.Reset();
  if (fence_event_) {
    CloseHandle(fence_event_);
    fence_event_ = nullptr;
  }
  queue_.Reset();
  rex::graphics::CommandProcessor::ShutdownContext();
}

void CustomCommandProcessor::IssueSwap(uint32_t frontbuffer_ptr, uint32_t frontbuffer_width,
                                       uint32_t frontbuffer_height) {
  progress::Bump(kFeatSwap);
  LogFirstSwap(frontbuffer_ptr, frontbuffer_width, frontbuffer_height);

  if (!frontbuffer_width || !frontbuffer_height) {
    return;
  }
  auto* presenter = graphics_system_ ? graphics_system_->presenter() : nullptr;
  if (!presenter) {
    static bool logged = false;
    if (!logged) {
      logged = true;
      REXGPU_WARN("[custom-renderer] IssueSwap before the presenter exists - skipping");
    }
    return;
  }

  // The front buffer is bound through fetch constant 0 (same as the built-in
  // uses for its swap/gamma pass). Only linear 8_8_8_8 is handled for now.
  const auto fetch = register_file_->GetTextureFetch(0);
  const auto format = rex::graphics::xenos::TextureFormat(fetch.dword_1 & 0x3F);
  if (format != rex::graphics::xenos::TextureFormat::k_8_8_8_8 &&
      format != rex::graphics::xenos::TextureFormat::k_8_8_8_8_A) {
    static bool logged = false;
    if (!logged) {
      logged = true;
      REXGPU_WARN(
          "[custom-renderer] front buffer format not 8_8_8_8 (format={} fetch0 dwords: "
          "{:08X} {:08X} {:08X} {:08X} {:08X} {:08X}) - staying black",
          uint32_t(format), fetch.dword_0, fetch.dword_1, fetch.dword_2, fetch.dword_3,
          fetch.dword_4, fetch.dword_5);
    }
    return;
  }
  // Guest physical memory is the low 31 bits; anything above that is not a
  // usable front buffer. (A simple range check suffices -
  // Memory::GetPhysicalAddress is not in the runtime export set.)
  if (!memory_ || frontbuffer_ptr >= 0x20000000u) {
    static bool logged = false;
    if (!logged) {
      logged = true;
      REXGPU_WARN("[custom-renderer] front buffer pointer {:#010x} not in guest memory - "
                  "staying black",
                  frontbuffer_ptr);
    }
    return;
  }

  const uint32_t slot = uint32_t(submission_counter_ % kQueueFrames);
  if (!EnsureStagingRing(frontbuffer_width, frontbuffer_height)) {
    return;
  }
  const uint64_t converted =
      ConvertFrontbuffer(frontbuffer_ptr, frontbuffer_width, frontbuffer_height, slot);
  if (!converted) {
    return;
  }
  progress::Bump(kFeatSwapBytes, converted);

  // Display aspect ratio for letterboxing (same query the built-in makes).
  rex::system::X_VIDEO_MODE video_mode{};
  rex::kernel::xboxkrnl::VdQueryVideoMode(&video_mode);
  const uint32_t display_width = std::max<uint32_t>(1, video_mode.display_width);
  const uint32_t display_height = std::max<uint32_t>(1, video_mode.display_height);

  presenter->RefreshGuestOutput(
      frontbuffer_width, frontbuffer_height, display_width, display_height,
      [this, slot, w = frontbuffer_width, h = frontbuffer_height](
          rex::ui::Presenter::GuestOutputRefreshContext& context) -> bool {
        context.SetIs8bpc(true);
        auto& d3d12_context =
            static_cast<rex::ui::d3d12::D3D12Presenter::D3D12GuestOutputRefreshContext&>(
                context);
        return SubmitGuestOutputCopy(d3d12_context.resource_uav_capable(), w, h, slot);
      });
}

bool CustomCommandProcessor::EnsureStagingRing(uint32_t width, uint32_t height) {
  if (staging_w_ == width && staging_h_ == height) {
    return true;
  }
  auto* provider =
      static_cast<rex::ui::d3d12::D3D12Provider*>(graphics_system_->provider());
  ID3D12Device* device = provider ? provider->GetDevice() : nullptr;
  if (!device) {
    return false;
  }
  for (int i = 0; i < kQueueFrames; ++i) {
    if (staging_map_[i] && staging_[i]) {
      staging_[i]->Unmap(0, nullptr);
      staging_map_[i] = nullptr;
    }
    staging_[i].Reset();
  }
  const uint64_t size = ((uint64_t(width) * height * 4 + 15) / 16) * 16;
  bool ok = true;
  bool from_map = false;
  HRESULT last_hr = S_OK;
  for (int i = 0; i < kQueueFrames; ++i) {
    D3D12_RESOURCE_DESC desc{};
    rex::ui::d3d12::util::FillBufferResourceDesc(desc, size, D3D12_RESOURCE_FLAG_NONE);
    last_hr = device->CreateCommittedResource(&rex::ui::d3d12::util::kHeapPropertiesUpload,
                                              D3D12_HEAP_FLAG_NONE, &desc,
                                              D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                              IID_PPV_ARGS(&staging_[i]));
    if (FAILED(last_hr)) {
      ok = false;
      break;
    }
    from_map = true;
    last_hr = staging_[i].Get()->Map(0, nullptr,
                                     reinterpret_cast<void**>(&staging_map_[i]));
    if (FAILED(last_hr)) {
      ok = false;
      break;
    }
  }
  if (ok) {
    staging_w_ = width;
    staging_h_ = height;
    REXGPU_INFO("[custom-renderer] staging ring created: {}x{} x{} buffers", width, height,
                kQueueFrames);
  } else {
    REXGPU_ERROR("[custom-renderer] failed to create the staging ring: hr={:#010x} from={} "
                 "size={} device={}",
                 uint32_t(last_hr), from_map ? "Map" : "CreateCommittedResource", size,
                 (void*)device);
  }
  return ok;
}

void CustomCommandProcessor::RebuildGammaLuts() {
  if (!gamma_lut_dirty_) {
    return;
  }
  for (int i = 0; i < 256; ++i) {
    gamma_lut_r_[i] = (gamma_table_[i] >> 20) & 0x3FF;
    gamma_lut_g_[i] = (gamma_table_[i] >> 10) & 0x3FF;
    gamma_lut_b_[i] = gamma_table_[i] & 0x3FF;
  }
  gamma_lut_dirty_ = false;
}

uint64_t CustomCommandProcessor::ConvertFrontbuffer(uint32_t frontbuffer_ptr, uint32_t width,
                                                    uint32_t height, uint32_t slot) {
  const auto fetch = register_file_->GetTextureFetch(0);
  // Base row pitch in pixels: the fetch field stores (pitch_pixels >> 5).
  uint32_t pitch_pixels = ((fetch.dword_0 >> 22) & 0x1FF) << 5;
  if (!pitch_pixels) {
    pitch_pixels = width;
  }

  // The fetch-constant base address is a guest PHYSICAL address (Xenos GPU
  // memory addresses are physical, like every other guest-side pointer the
  // command stream uses), so translate through the physical mapping.
  const uint8_t* src = memory_->TranslatePhysical<uint8_t*>(frontbuffer_ptr);
  uint8_t* dst = staging_map_[slot];
  if (!src || !dst) {
    return 0;
  }

  // Diagnostic (throttled ~1/s): sample a grid of the guest front buffer and
  // report how much of it is non-black. If this reads near-zero at the
  // splash moment, the GPU-rendered content never reached the front buffer
  // (because our draw/shader path is a stub) - confirming M1 presents what is
  // in the buffer, but the splash needs the real render pipeline.
  {
    static uint64_t diag_count = 0;
    static int64_t last_nonblack = -1;
    static uint32_t last_maxv = 0;
    if ((diag_count++ % 60) == 0) {
      int64_t nonblack = 0, samples = 0;
      uint32_t maxv = 0;
      for (uint32_t y = 0; y < height; y += std::max(1u, height / 32)) {
        for (uint32_t x = 0; x < width; x += std::max(1u, width / 32)) {
          const uint32_t off =
              fetch.tiled ? TiledByteOffset2D(int32_t(x), int32_t(y), pitch_pixels, 2)
                          : (y * std::max<uint64_t>(uint64_t(width) * 4, uint64_t(pitch_pixels) * 4) +
                             x * 4);
          const uint32_t pv = *reinterpret_cast<const uint32_t*>(src + off);
          maxv = std::max({maxv, pv & 0xFF, (pv >> 8) & 0xFF, (pv >> 16) & 0xFF, (pv >> 24) & 0xFF});
          if ((pv & 0xFF) + ((pv >> 8) & 0xFF) + ((pv >> 16) & 0xFF) > 30) ++nonblack;
          ++samples;
        }
      }
      if (nonblack != last_nonblack || maxv != last_maxv) {
        REXGPU_INFO("[custom-renderer] frontbuffer content: {}/{} nonblack, max channel {}",
                    nonblack, samples, maxv);
        last_nonblack = nonblack; last_maxv = maxv;
      }
    }
  }

  RebuildGammaLuts();
  // Apply the fetch-constant swizzle: it selects, for each destination
  // channel (x,y,z = R,G,B), which source byte (0-3) of the 8_8_8_8 pixel
  // feeds it. Fable 2's front buffer uses swizzle 0xA0A (x=2, y=1, z=0,
  // w=5), i.e. R<-byte2, G<-byte1, B<-byte0, A<-"one" (opaque).
  const uint32_t sw = fetch.swizzle;
  const int sx = sw & 0x7;
  const int sy = (sw >> 3) & 0x7;
  const int sz = (sw >> 6) & 0x7;
  const uint32_t r_shift = uint32_t(sx <= 3 ? sx * 8 : 0);
  const uint32_t g_shift = uint32_t(sy <= 3 ? sy * 8 : 0);
  const uint32_t b_shift = uint32_t(sz <= 3 ? sz * 8 : 0);
  auto map_pixel = [this, sx, sy, sz, r_shift, g_shift, b_shift](uint32_t p) -> uint32_t {
    const uint32_t r = (sx <= 3) ? (p >> r_shift) & 0xFF : 0;
    const uint32_t g = (sy <= 3) ? (p >> g_shift) & 0xFF : 0;
    const uint32_t b = (sz <= 3) ? (p >> b_shift) & 0xFF : 0;
    return kOpaqueA2 | (gamma_lut_b_[b] << 20) | (gamma_lut_g_[g] << 10) | gamma_lut_r_[r];
  };

  if (fetch.tiled) {
    // 2D tiled 8_8_8_8: each pixel lives at a Xenon tile offset from the
    // surface base. 4 bytes/pixel => log2_bpp = 2.
    for (uint32_t y = 0; y < height; ++y) {
      uint32_t* drow = reinterpret_cast<uint32_t*>(dst) + y * width;
      for (uint32_t x = 0; x < width; ++x) {
        const uint32_t off = TiledByteOffset2D(int32_t(x), int32_t(y), pitch_pixels, 2);
        drow[x] = map_pixel(*reinterpret_cast<const uint32_t*>(src + off));
      }
    }
  } else {
    const uint64_t stride = std::max<uint64_t>(uint64_t(width) * 4, uint64_t(pitch_pixels) * 4);
    for (uint32_t y = 0; y < height; ++y) {
      const uint32_t* row = reinterpret_cast<const uint32_t*>(src + y * stride);
      uint32_t* drow = reinterpret_cast<uint32_t*>(dst) + y * width;
      for (uint32_t x = 0; x < width; ++x) {
        drow[x] = map_pixel(row[x]);
      }
    }
  }
  return uint64_t(width) * height * 4;
}

bool CustomCommandProcessor::SubmitGuestOutputCopy(ID3D12Resource* guest_output, uint32_t width,
                                                   uint32_t height, uint32_t slot) {
  if (!command_list_ || !staging_[slot]) {
    return false;
  }
  const uint64_t submission = submission_counter_;

  // Reuse of this allocator/staging slot requires the submission from
  // kQueueFrames ago to be complete.
  if (submission >= kQueueFrames) {
    WaitForFenceValue(submission - kQueueFrames + 1);
  }

  allocators_[slot]->Reset();
  command_list_->Reset(allocators_[slot].Get(), nullptr);
  D3D12_RESOURCE_BARRIER to_copy_dest{};
  to_copy_dest.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  to_copy_dest.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  to_copy_dest.Transition.pResource = guest_output;
  to_copy_dest.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  to_copy_dest.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  to_copy_dest.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
  command_list_->ResourceBarrier(1, &to_copy_dest);
  // Buffer -> texture copy. The staging buffer is tightly packed (width*4
  // bytes per row) matching the destination R10G10B10A2 (4 bytes/texel), so
  // a buffer-source CopyTextureRegion with a width x height box copies the
  // frame verbatim.
  D3D12_TEXTURE_COPY_LOCATION src_loc{};
  src_loc.pResource = staging_[slot].Get();
  src_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  src_loc.SubresourceIndex = 0;
  D3D12_TEXTURE_COPY_LOCATION dst_loc{};
  dst_loc.pResource = guest_output;
  dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  dst_loc.SubresourceIndex = 0;
  D3D12_BOX src_box{0, 0, 0, width, height, 1};
  command_list_->CopyTextureRegion(&dst_loc, 0, 0, 0, &src_loc, &src_box);
  D3D12_RESOURCE_BARRIER back_to_srv{};
  back_to_srv.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  back_to_srv.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  back_to_srv.Transition.pResource = guest_output;
  back_to_srv.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  back_to_srv.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  back_to_srv.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  command_list_->ResourceBarrier(1, &back_to_srv);
  if (FAILED(command_list_->Close())) {
    return false;
  }
  ID3D12CommandList* lists[1] = {command_list_.Get()};
  queue_->ExecuteCommandLists(1, lists);
  fence_value_ = submission + 1;
  fence_->Signal(fence_value_);
  submission_counter_ = submission + 1;
  return true;
}

void CustomCommandProcessor::WaitForFenceValue(uint64_t value) {
  if (!fence_ || fence_->GetCompletedValue() >= value) {
    return;
  }
  fence_->SetEventOnCompletion(value, static_cast<HANDLE>(fence_event_));
  if (WaitForSingleObject(static_cast<HANDLE>(fence_event_), 2000) == WAIT_TIMEOUT) {
    static int logged = 0;
    if (logged++ < 3) {
      REXGPU_WARN("[custom-renderer] fence wait timed out at value {}", value);
    }
  }
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
  // Mirror the DC LUT 256-entry gamma table into our own copy. The base
  // class stores the entry at the current DC_LUT_RW_INDEX (when not in PWL
  // mode and the write-enable mask is set), so read the index/mode/mask
  // before delegating - the base may advance the index register as part of
  // handling this write.
  const bool is_lut_30 = (index == rex::graphics::XE_GPU_REG_DC_LUT_30_COLOR);
  uint32_t lut_mode = 0, lut_index = 0, lut_mask = 0;
  if (is_lut_30) {
    lut_mode = register_file_->values[rex::graphics::XE_GPU_REG_DC_LUT_RW_MODE];
    lut_index = register_file_->values[rex::graphics::XE_GPU_REG_DC_LUT_RW_INDEX] & 0x7F;
    lut_mask = register_file_->values[rex::graphics::XE_GPU_REG_DC_LUT_WRITE_EN_MASK] & 0x7;
  }
  rex::graphics::CommandProcessor::WriteRegister(index, value);
  if (is_lut_30 && !(lut_mode & 1) && lut_mask) {
    gamma_table_[lut_index] = value;
    gamma_lut_dirty_ = true;
  }
  LogFirstInterestingRegister(index, value);
}

void CustomCommandProcessor::LogFirstSwap(uint32_t frontbuffer_ptr, uint32_t width,
                                          uint32_t height) {
  // Log the front buffer identity whenever it changes so we can see every
  // boot-phase transition (boot screen -> splash -> menu may use different
  // buffers/formats). Runs on the GPU-Commands thread; only logs on change.
  const auto fetch = register_file_ ? register_file_->GetTextureFetch(0)
                                    : rex::graphics::xenos::xe_gpu_texture_fetch_t{};
  const uint32_t sw = width, sh = height;
  const uint32_t fmt = uint32_t(fetch.dword_1 & 0x3F);
  const uint32_t tiled = fetch.tiled ? 1u : 0u;
  const uint32_t swizzle = uint32_t(fetch.swizzle);
  const uint32_t pitch = (uint32_t((fetch.dword_0 >> 22) & 0x1FF) << 5);
  if (sw != last_front_w_ || sh != last_front_h_ || frontbuffer_ptr != last_front_ptr_ ||
      fmt != last_front_fmt_ || tiled != last_front_tiled_ || swizzle != last_front_swizzle_ ||
      pitch != last_front_pitch_) {
    last_front_w_ = sw; last_front_h_ = sh; last_front_ptr_ = frontbuffer_ptr;
    last_front_fmt_ = fmt; last_front_tiled_ = tiled; last_front_swizzle_ = swizzle;
    last_front_pitch_ = pitch;
    REXGPU_INFO(
        "[custom-renderer] front buffer: ptr={:#010x} {}x{} format={} tiled={} pitch={} "
        "swizzle={:#04x} (swap dims) fetch0=[{:08X} {:08X} {:08X}]",
        frontbuffer_ptr, sw, sh, fmt, tiled, pitch, swizzle, fetch.dword_0, fetch.dword_1,
        fetch.dword_2);
  }
}

void CustomCommandProcessor::LogFirstInterestingRegister(uint32_t index, uint32_t value) {
  // One-time visibility into the state the guest is programming, to confirm
  // the base-class register decode is live.
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
