// custom_command_processor.cpp - see custom_command_processor.h.

#include "custom_command_processor.h"
#include "ccr_trace.h"

#include "../common/progress_checklist.h"
#include "../common/cvars.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

#include <windows.h>

#include <d3dcompiler.h>

#include <rex/graphics/d3d12/shader.h>
#include <rex/graphics/graphics_system.h>
#include <rex/graphics/registers.h>
#include <rex/graphics/util/draw.h>
#include <rex/graphics/xenos.h>
#include <rex/kernel/xboxkrnl/video.h>
#include <rex/logging.h>
#include <rex/system/xmemory.h>
#include <rex/ui/d3d12/d3d12_api.h>
#include <rex/ui/d3d12/d3d12_presenter.h>
#include <rex/ui/d3d12/d3d12_provider.h>
#include <rex/ui/d3d12/d3d12_util.h>

namespace customrenderer {
namespace d3d12 {

namespace {
// Direct stderr debug log (bypasses spdlog buffering so it survives a hard
// fatal error / crash - used only while debugging the M2 device-remove).

// Progress checklist indices (order fixed by progress::Init()).
constexpr int kFeatRegisterStream = 2;
constexpr int kFeatSwap = 3;
constexpr int kFeatSwapBytes = 4;
constexpr int kFeatRenderTargets = 6;
constexpr int kFeatCopies = 7;
constexpr int kFeatClears = 8;
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
  texture_cache_ = std::make_unique<TextureCache>(device, queue_.Get(), memory_);
  rt_cache_ = std::make_unique<RenderTargetCache>(device, queue_.Get());
  REXGPU_INFO(
      "[custom-renderer] command processor context ready (M1 front-buffer + M2 texture cache + "
      "M3 render-target cache)");
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
  for (auto& kv : active_shaders_) {
    delete kv.second;
  }
  active_shaders_.clear();
  for (auto& kv : shader_cache_) {
    delete kv.second.shader;
  }
  shader_cache_.clear();
  for (auto& kv : pipeline_cache_) {
    kv.second->Release();
  }
  pipeline_cache_.clear();
  if (dxbc_converter_) {
    dxbc_converter_->Release();
    dxbc_converter_ = nullptr;
  }
  if (draw_srv_heap_) {
    draw_srv_heap_->Release();
    draw_srv_heap_ = nullptr;
  }
  if (draw_sampler_heap_) {
    draw_sampler_heap_->Release();
    draw_sampler_heap_ = nullptr;
  }
  if (upload_buffer_map_ && upload_buffer_) {
    upload_buffer_->Unmap(0, nullptr);
    upload_buffer_map_ = nullptr;
  }
  upload_buffer_.Reset();
  index_buffer_.Reset();
  shared_memory_.Reset();
  if (root_signature_) {
    root_signature_->Release();
    root_signature_ = nullptr;
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
  {
    static uint64_t s_sw = 0;
    if (s_sw == 0) {
    }
    ++s_sw;
  }
  progress::Bump(kFeatSwap);
  LogFirstSwap(frontbuffer_ptr, frontbuffer_width, frontbuffer_height);
  RequestActiveTextures();

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

  // M3: present the front buffer as a real D3D12 render target (cleared to the
  // guest's clear color, then GPU-copied to the guest output) instead of the
  // M1 CPU untile. The RT is keyed by the front buffer address so the same
  // guest address reuses the same D3D12 resource (no flicker from stale RTs).
  // M4 will draw the 3D scene into this RT; for now it is cleared each frame.
  if (rt_cache_) {
    UpdateGuestClear();
    main_rt_w_ = frontbuffer_width;
    main_rt_h_ = frontbuffer_height;
    // If no draw was recorded this frame, ensure the main RT exists so the
    // clear-only present path (SubmitRTPresent) has a target.
    if (!frame_recording_) {
      if (!(main_rt_ && main_rt_->valid && main_rt_->width == main_rt_w_ &&
            main_rt_->height == main_rt_h_)) {
        main_rt_ = rt_cache_->GetOrCreate(0, main_rt_w_, main_rt_h_,
                                          DXGI_FORMAT_R10G10B10A2_UNORM, true);
      }
    }
    if (main_rt_ && main_rt_->valid) {
      rex::system::X_VIDEO_MODE video_mode{};
      rex::kernel::xboxkrnl::VdQueryVideoMode(&video_mode);
      const uint32_t display_width = std::max<uint32_t>(1, video_mode.display_width);
      const uint32_t display_height = std::max<uint32_t>(1, video_mode.display_height);
      const bool has_draws = frame_recording_;  // draws were recorded into the RT
      {
        static uint32_t swap_diag = 0;
        ++swap_diag;
        if (swap_diag % 500 == 1) {
          REXGPU_INFO("[custom-renderer] DIAG swap #{} has_draws={} (M4) vs SubmitRTPresent (M3)",
                      swap_diag, (bool)has_draws);
        }
      }
      const float cr = guest_clear_r_, cg = guest_clear_g_, cb = guest_clear_b_,
                    ca = guest_clear_a_;
      presenter->RefreshGuestOutput(
          frontbuffer_width, frontbuffer_height, display_width, display_height,
          [this, rt = main_rt_, w = frontbuffer_width, h = frontbuffer_height, has_draws, cr, cg,
           cb, ca](rex::ui::Presenter::GuestOutputRefreshContext& context) -> bool {
            context.SetIs8bpc(true);
            auto& d3d12_context =
                static_cast<rex::ui::d3d12::D3D12Presenter::D3D12GuestOutputRefreshContext&>(
                    context);
            if (has_draws) {
              // The command list already has the clear + draws; just present the RT.
              return EndFramePresent(d3d12_context.resource_uav_capable(), w, h);
            }
            return SubmitRTPresent(d3d12_context.resource_uav_capable(), w, h, rt, cr, cg, cb,
                                   ca);
          });
      progress::Bump(kFeatRenderTargets);
      progress::Bump(kFeatClears);
      return;
    }
    // RT creation failed - fall through to the M1 CPU-untile fallback below.
  }

  // M1 fallback: CPU-untile the guest front buffer (requires 8_8_8_8).
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
    CcrTrace("M1STG", width, height, 0, 0, size);
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

  // This path shares command_list_ with the M4 draw path: close any stale open
  // list before Reset and track the open state so the next Reset doesn't hit a
  // closed/open mismatch.
  if (command_list_open_) {
    command_list_->Close();
    command_list_open_ = false;
  }
  allocators_[slot]->Reset();
  command_list_->Reset(allocators_[slot].Get(), nullptr);
  command_list_open_ = true;
  D3D12_RESOURCE_BARRIER to_copy_dest{};
  to_copy_dest.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  to_copy_dest.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  to_copy_dest.Transition.pResource = guest_output;
  to_copy_dest.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  to_copy_dest.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  to_copy_dest.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
  command_list_->ResourceBarrier(1, &to_copy_dest);
  // Buffer -> texture copy via PLACED_FOOTPRINT (the built-in's pattern). A
  // buffer source with SUBRESOURCE_INDEX + box is rejected by the driver
  // (Close -> E_INVALIDARG), so use an explicit footprint with a RowPitch.
  D3D12_TEXTURE_COPY_LOCATION src_loc{};
  src_loc.pResource = staging_[slot].Get();
  src_loc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  src_loc.PlacedFootprint.Offset = 0;
  src_loc.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
  src_loc.PlacedFootprint.Footprint.Width = width;
  src_loc.PlacedFootprint.Footprint.Height = height;
  src_loc.PlacedFootprint.Footprint.Depth = 1;
  src_loc.PlacedFootprint.Footprint.RowPitch = width * 4;
  D3D12_TEXTURE_COPY_LOCATION dst_loc{};
  dst_loc.pResource = guest_output;
  dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  dst_loc.SubresourceIndex = 0;
  command_list_->CopyTextureRegion(&dst_loc, 0, 0, 0, &src_loc, nullptr);
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
  command_list_open_ = false;
  ID3D12CommandList* lists[1] = {command_list_.Get()};
  queue_->ExecuteCommandLists(1, lists);
  fence_value_ = submission + 1;
  fence_->Signal(fence_value_);
  submission_counter_ = submission + 1;
  return true;
}

void CustomCommandProcessor::UpdateGuestClear() {
  if (!register_file_) {
    return;
  }
  const uint32_t clear_hi = register_file_->values[rex::graphics::XE_GPU_REG_RB_COLOR_CLEAR];
  const uint32_t clear_lo = register_file_->values[rex::graphics::XE_GPU_REG_RB_COLOR_CLEAR_LO];
  const uint64_t value = uint64_t(clear_hi) | (uint64_t(clear_lo) << 32);
  if (value == guest_clear_value_ && guest_clear_valid_) {
    return;
  }
  // 10-bit R/G/B in the low 30 bits; alpha in bits 30-31.
  const uint32_t r10 = uint32_t(value) & 0x3FFu;
  const uint32_t g10 = uint32_t((value >> 10) & 0x3FFu);
  const uint32_t b10 = uint32_t((value >> 20) & 0x3FFu);
  const uint32_t a2 = uint32_t((value >> 30) & 0x3u);
  guest_clear_r_ = float(r10) / 1023.0f;
  guest_clear_g_ = float(g10) / 1023.0f;
  guest_clear_b_ = float(b10) / 1023.0f;
  guest_clear_a_ = float(a2) / 3.0f;
  guest_clear_value_ = value;
  guest_clear_valid_ = true;
  static int logged = 0;
  if (logged++ < 8) {
    REXGPU_INFO(
        "[custom-renderer] guest clear color: r10={} g10={} b10={} a2={} (rgb {:.3f} "
        "{:.3f} {:.3f})",
        r10, g10, b10, a2, guest_clear_r_, guest_clear_g_, guest_clear_b_);
  }
}

bool CustomCommandProcessor::SubmitRTPresent(ID3D12Resource* guest_output, uint32_t width,
                                             uint32_t height, RenderTargetCache::Entry* rt,
                                             float cr, float cg, float cb, float ca) {
  if (!command_list_ || !rt || !rt->valid || !rt->color) {
    return false;
  }
  if (!EnsureStagingRing(width, height)) {
    return false;
  }
  const uint64_t submission = submission_counter_;
  const uint32_t slot = uint32_t(submission % kQueueFrames);
  // Write the clear color pixel-by-pixel into the staging buffer (R10G10B10A2
  // packed). This avoids the RTV descriptor (CreateRenderTargetView
  // device-removes on this driver).
  const uint32_t r10 = uint32_t(cr * 1023.0f + 0.5f);
  const uint32_t g10 = uint32_t(cg * 1023.0f + 0.5f);
  const uint32_t b10 = uint32_t(cb * 1023.0f + 0.5f);
  const uint32_t a2 = uint32_t(ca * 3.0f + 0.5f);
  const uint32_t pixel = (a2 << 30) | (b10 << 20) | (g10 << 10) | r10;
  if (staging_map_[slot]) {
    uint32_t* p = reinterpret_cast<uint32_t*>(staging_map_[slot]);
    for (uint32_t i = 0; i < uint64_t(width) * height; ++i) p[i] = pixel;
  }
  if (submission >= kQueueFrames) {
    WaitForFenceValue(submission - kQueueFrames + 1);
  }
  if (command_list_open_) {
    command_list_->Close();
    command_list_open_ = false;
  }
  allocators_[slot]->Reset();
  command_list_->Reset(allocators_[slot].Get(), nullptr);
  command_list_open_ = true;
  ID3D12Resource* rt_res = rt->color.Get();
  // The RT is in COPY_DEST state (from the previous frame). First copy the
  // staging buffer (clear color) into the RT, then copy the RT to the guest
  // output.
  // Buffer -> texture copy (staging -> RT) via PLACED_FOOTPRINT.
  D3D12_TEXTURE_COPY_LOCATION rt_loc{};
  rt_loc.pResource = rt_res;
  rt_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  rt_loc.SubresourceIndex = 0;
  D3D12_TEXTURE_COPY_LOCATION clear_loc{};
  clear_loc.pResource = staging_[slot].Get();
  clear_loc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  clear_loc.PlacedFootprint.Offset = 0;
  clear_loc.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
  clear_loc.PlacedFootprint.Footprint.Width = width;
  clear_loc.PlacedFootprint.Footprint.Height = height;
  clear_loc.PlacedFootprint.Footprint.Depth = 1;
  clear_loc.PlacedFootprint.Footprint.RowPitch = width * 4;
  command_list_->CopyTextureRegion(&rt_loc, 0, 0, 0, &clear_loc, nullptr);
  // Transition RT to COPY_SOURCE and guest output to COPY_DEST.
  D3D12_RESOURCE_BARRIER barriers[2]{};
  barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barriers[0].Transition.pResource = rt_res;
  barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barriers[1].Transition.pResource = guest_output;
  barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
  command_list_->ResourceBarrier(2, barriers);
  // Texture -> texture copy (RT -> guest output).
  D3D12_TEXTURE_COPY_LOCATION dst_loc{};
  dst_loc.pResource = guest_output;
  dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  dst_loc.SubresourceIndex = 0;
  command_list_->CopyTextureRegion(&dst_loc, 0, 0, 0, &rt_loc, nullptr);
  // Transition guest output back to PIXEL_SHADER_RESOURCE, RT back to COPY_DEST.
  D3D12_RESOURCE_BARRIER back[2]{};
  back[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  back[0].Transition.pResource = guest_output;
  back[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  back[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  back[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  back[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  back[1].Transition.pResource = rt_res;
  back[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  back[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
  back[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
  command_list_->ResourceBarrier(2, back);
  if (FAILED(command_list_->Close())) {
    return false;
  }
  command_list_open_ = false;
  ID3D12CommandList* lists[1] = {command_list_.Get()};
  queue_->ExecuteCommandLists(1, lists);
  fence_value_ = submission + 1;
  fence_->Signal(fence_value_);
  submission_counter_ = submission + 1;
  return true;
}

void CustomCommandProcessor::WaitForFenceValue(uint64_t value) {
  const uint64_t before = fence_ ? fence_->GetCompletedValue() : 0;
  if (!fence_ || before >= value) {
    return;
  }
  // Block until the fence is ACTUALLY complete. The command allocator for this
  // slot must NOT be reset until the prior frame's command list has fully
  // executed, or D3D12 raises COMMAND_ALLOCATOR_RESET and removes the device.
  // The auto-reset event can be signaled by a catch-up, so a single
  // WaitForSingleObject does not guarantee GetCompletedValue() >= value. The DD
  // basic display adapter can be slow on heavy (vertex-upload) frames, so loop
  // on the completed value; the GPU always finishes the frame, so this
  // terminates (a slow adapter just stalls the frame).
  while (fence_->GetCompletedValue() < value) {
    fence_->SetEventOnCompletion(value, static_cast<HANDLE>(fence_event_));
    WaitForSingleObject(static_cast<HANDLE>(fence_event_), 5000);
  }
}

namespace {
// FNV-1a hash over the shader ucode (plus the stage) - the shader-cache key.
uint64_t ShaderHash(rex::graphics::xenos::ShaderType type, const uint32_t* ucode,
                    uint32_t dword_count) {
  uint64_t h = 1469598103934665603ULL;
  for (uint32_t i = 0; i < dword_count; ++i) {
    h ^= ucode[i];
    h *= 1099511628211ULL;
  }
  h = (h ^ (uint64_t)type) * 1099511628211ULL;
  return h;
}

// Fill the D3D12 system-constants cbuffer (b0) from the guest register file.
// Uses draw_util::GetHostViewportInfo for the NDC scale/offset (the same source
// the built-in uses) and populates the index bounds + primitive-type flags the
// translated shaders read.
void FillSystemConstants(rex::graphics::DxbcShaderTranslator::SystemConstants& sc,
                         const rex::graphics::RegisterFile& rf,
                         rex::graphics::xenos::PrimitiveType prim) {
  std::memset(&sc, 0, sizeof(sc));
  using DxbcShaderTranslator = rex::graphics::DxbcShaderTranslator;

  // Primitive-type flags (the translator reads these to choose its behavior).
  switch (prim) {
    case rex::graphics::xenos::PrimitiveType::kTriangleList:
    case rex::graphics::xenos::PrimitiveType::kTriangleStrip:
    case rex::graphics::xenos::PrimitiveType::kRectangleList:
      sc.flags |= DxbcShaderTranslator::kSysFlag_PrimitivePolygonal;
      break;
    case rex::graphics::xenos::PrimitiveType::kLineList:
    case rex::graphics::xenos::PrimitiveType::kLineStrip:
    case rex::graphics::xenos::PrimitiveType::kQuadList:
      sc.flags |= DxbcShaderTranslator::kSysFlag_PrimitiveLine;
      break;
    case rex::graphics::xenos::PrimitiveType::kPointList:
      // No line/polygon flag.
      break;
    default:
      sc.flags |= DxbcShaderTranslator::kSysFlag_PrimitivePolygonal;
      break;
  }

  // W0 division control (PA_CL_VTE_CNTL) - CRITICAL for the clip-space transform.
  // The guest vertex shader output may have XY/Z pre-multiplied by 1/W and/or W
  // already reciprocal; the translator reverts that to recover true clip-space
  // (x,y,z,w) before applying the ndc_scale/ndc_offset viewport transform. If
  // these flags are wrong, every vertex is transformed incorrectly and the
  // geometry is off-screen / degenerate (black screen). Match the built-in
  // (command_processor.cpp UpdateSystemConstantValues).
  const auto pa_cl_vte_cntl = rf.Get<rex::graphics::reg::PA_CL_VTE_CNTL>();
  if (pa_cl_vte_cntl.vtx_xy_fmt) {
    sc.flags |= DxbcShaderTranslator::kSysFlag_XYDividedByW;
  }
  if (pa_cl_vte_cntl.vtx_z_fmt) {
    sc.flags |= DxbcShaderTranslator::kSysFlag_ZDividedByW;
  }
  if (pa_cl_vte_cntl.vtx_w0_fmt) {
    sc.flags |= DxbcShaderTranslator::kSysFlag_WNotReciprocal;
  }
  // Depth format (D24FS8 -> float24 depth path in the pixel shader).
  const auto rb_depth_info = rf.Get<rex::graphics::reg::RB_DEPTH_INFO>();
  if (rb_depth_info.depth_format == rex::graphics::xenos::DepthRenderTargetFormat::kD24FS8) {
    sc.flags |= DxbcShaderTranslator::kSysFlag_DepthFloat24;
  }
  // Alpha test (the pixel shader discards fragments based on this; a wrong
  // compare function can discard the whole draw).
  const auto rb_colorcontrol = rf.Get<rex::graphics::reg::RB_COLORCONTROL>();
  rex::graphics::xenos::CompareFunction alpha_func =
      rb_colorcontrol.alpha_test_enable ? rb_colorcontrol.alpha_func
                                        : rex::graphics::xenos::CompareFunction::kAlways;
  sc.flags |=
      (uint32_t(alpha_func) << DxbcShaderTranslator::kSysFlag_AlphaPassIfLess_Shift);
  // Per-RT gamma conversion (8_8_8_8_GAMMA render targets).
  for (int i = 0; i < 4; ++i) {
    const auto ci = rf.Get<rex::graphics::reg::RB_COLOR_INFO>(i);
    if (ci.color_format == rex::graphics::xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA) {
      sc.flags |= DxbcShaderTranslator::kSysFlag_ConvertColor0ToGamma << i;
    }
  }

  // Viewport -> NDC scale/offset + depth range. x_max/y_max are the RT dims.
  // allow_reverse_z MUST be false: D3D12 cannot clip to a reversed [1,0] z range
  // (it discards every fragment). With false, the guest z is remapped to [0,1]
  // inside the vertex shader via ndc_scale/ndc_offset (matching the built-in).
  rex::graphics::draw_util::ViewportInfo vi{};
  rex::graphics::draw_util::GetHostViewportInfo(rf, 1, 1, /*origin_bottom_left=*/true, 1280, 720,
                                                /*allow_reverse_z=*/false,
                                                rf.Get<rex::graphics::reg::RB_DEPTHCONTROL>(),
                                                /*convert_z_to_float24=*/false,
                                                /*full_float24_in_0_to_1=*/true,
                                                /*pixel_shader_writes_depth=*/false, vi);
  std::memcpy(sc.ndc_scale, vi.ndc_scale, sizeof(vi.ndc_scale));
  std::memcpy(sc.ndc_offset, vi.ndc_offset, sizeof(vi.ndc_offset));
  // DIAG: force all geometry to the screen center (tiny blob) to test whether
  // the pipeline produces any visible fragments at all, bypassing the NDC
  // transform. If a blob appears, the pipeline + pixel shader work and the
  // geometry was simply off-screen; if not, the pixel shader or vertex data is
  // the problem.
  sc.ndc_scale[0] = 0.01f;
  sc.ndc_scale[1] = 0.01f;
  sc.ndc_scale[2] = 0.01f;
  sc.ndc_offset[0] = 0.0f;
  sc.ndc_offset[1] = 0.0f;
  sc.ndc_offset[2] = 0.0f;

  // Index bounds (the vertex shader uses these for indirect / bounds checks).
  sc.vertex_index_offset = rf.Get<rex::graphics::reg::VGT_INDX_OFFSET>().indx_offset;
  sc.vertex_index_min = rf.Get<rex::graphics::reg::VGT_MIN_VTX_INDX>().min_indx;
  sc.vertex_index_max = rf.Get<rex::graphics::reg::VGT_MAX_VTX_INDX>().max_indx;
  // Index endianness from VGT_DMA_SIZE.swap_mode, matching the built-in's
  // normalization (primitive_processor.cpp): for 16-bit indices a 32-bit swap
  // mode is clamped to k8in16. The translated vertex shader byte-swaps the raw
  // index by this value before computing the vertex attribute address, so this
  // MUST match the guest's index buffer encoding or the VS reads the wrong
  // (usually zero) vertices and all geometry is degenerate.
  {
    const auto vgt_dma_size = rf.Get<rex::graphics::reg::VGT_DMA_SIZE>();
    const auto vgt_draw_init = rf.Get<rex::graphics::reg::VGT_DRAW_INITIATOR>();
    rex::graphics::xenos::Endian index_endian = vgt_dma_size.swap_mode;
    if (vgt_draw_init.index_size == rex::graphics::xenos::IndexFormat::kInt16 &&
        index_endian != rex::graphics::xenos::Endian::kNone &&
        index_endian != rex::graphics::xenos::Endian::k8in16) {
      index_endian = (index_endian == rex::graphics::xenos::Endian::k8in32)
                         ? rex::graphics::xenos::Endian::k8in16
                         : rex::graphics::xenos::Endian::kNone;
    }
    sc.vertex_index_endian = index_endian;
  }

  // Per-RT color exponent bias (floating-point format conversion in the PS).
  for (int i = 0; i < 4; ++i) {
    const auto ci = rf.Get<rex::graphics::reg::RB_COLOR_INFO>(i);
    sc.color_exp_bias[i] = float(ci.color_exp_bias);
  }

  // Alpha test reference (the PS alpha-test compares against this). RB_ALPHA_REF
  // is a single float register (kFloat) stored as a dword bit pattern.
  {
    const uint32_t alpha_bits = rf.values[rex::graphics::XE_GPU_REG_RB_ALPHA_REF];
    std::memcpy(&sc.alpha_test_reference, &alpha_bits, 4);
  }

  // Point size (default to 1 for non-point primitives).
  sc.point_constant_diameter[0] = 1.0f;
  sc.point_constant_diameter[1] = 1.0f;
}
}  // namespace

bool CustomCommandProcessor::EnsureDxilConverter() {
  if (dxbc_converter_) {
    return true;
  }
  auto* provider =
      static_cast<rex::ui::d3d12::D3D12Provider*>(graphics_system_->provider());
  if (!provider) {
    return false;
  }
  HRESULT hr = provider->DxbcConverterCreateInstance(CLSID_DxbcConverter,
                                                     IID_PPV_ARGS(&dxbc_converter_));
  if (FAILED(hr) || !dxbc_converter_) {
    REXGPU_ERROR("[custom-renderer] failed to create DXBC converter (hr={:#010x})", (uint32_t)hr);
    return false;
  }
  return true;
}

bool CustomCommandProcessor::EnsureGraphicsPipelineState() {
  if (root_signature_) {
    return true;
  }
  auto* provider =
      static_cast<rex::ui::d3d12::D3D12Provider*>(graphics_system_->provider());
  ID3D12Device* device = provider ? provider->GetDevice() : nullptr;
  if (!device) {
    return false;
  }

  // Root signature matching the DxbcShaderTranslator binding layout:
  //   b0 (all)     = system constants        (CbufferRegister::kSystemConstants)
  //   b1 (vertex)  = float constants, VS     (CbufferRegister::kFloatConstants)
  //   b1 (pixel)   = float constants, PS     (CbufferRegister::kFloatConstants)
  //   b2 (all)     = bool + loop constants   (CbufferRegister::kBoolLoopConstants)
  //   b3 (all)     = fetch constants         (CbufferRegister::kFetchConstants)
  //   b4 (all)     = descriptor indices      (CbufferRegister::kDescriptorIndices)
  //   t0..t32 (all)= SRV table: t0 = shared memory (raw R32), t1.. = textures
  //   u0..u1  (all)= UAV table (shared memory / edram)
  //   s0..s32 (all)= sampler table
  D3D12_DESCRIPTOR_RANGE srv_range = {};
  srv_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  srv_range.NumDescriptors = kMaxDrawTextures + 1;
  srv_range.BaseShaderRegister = 0;  // t0 (shared memory), textures at t1..
  srv_range.RegisterSpace = 0;
  D3D12_DESCRIPTOR_RANGE uav_range = {};
  uav_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  uav_range.NumDescriptors = 2;
  uav_range.BaseShaderRegister = 0;
  uav_range.RegisterSpace = 0;
  D3D12_DESCRIPTOR_RANGE sampler_range = {};
  sampler_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
  sampler_range.NumDescriptors = kMaxDrawTextures + 1;
  sampler_range.BaseShaderRegister = 0;
  sampler_range.RegisterSpace = 0;

  // b1 is split into vertex-visible and pixel-visible root CBVs so each stage
  // can read its own (separately packed) float constants from the same slot.
  struct CbvParam {
    uint32_t register_;      // CbufferRegister value
    D3D12_SHADER_VISIBILITY visibility;
  };
  static const CbvParam kCbvParams[] = {
      {0, D3D12_SHADER_VISIBILITY_ALL},      // b0 system
      {1, D3D12_SHADER_VISIBILITY_VERTEX},   // b1 float (VS)
      {1, D3D12_SHADER_VISIBILITY_PIXEL},    // b1 float (PS)
      {2, D3D12_SHADER_VISIBILITY_ALL},      // b2 bool/loop
      {3, D3D12_SHADER_VISIBILITY_ALL},      // b3 fetch
      {4, D3D12_SHADER_VISIBILITY_ALL},      // b4 descriptor indices
  };

  D3D12_ROOT_PARAMETER params[12];
  int num_params = 0;
  for (const auto& cbv : kCbvParams) {
    auto& p = params[num_params++];
    p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    p.Descriptor.ShaderRegister = cbv.register_;
    p.Descriptor.RegisterSpace = 0;
    p.ShaderVisibility = cbv.visibility;
  }
  {
    auto& p = params[num_params++];
    p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    p.DescriptorTable.NumDescriptorRanges = 1;
    p.DescriptorTable.pDescriptorRanges = &srv_range;
    p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }
  {
    auto& p = params[num_params++];
    p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    p.DescriptorTable.NumDescriptorRanges = 1;
    p.DescriptorTable.pDescriptorRanges = &uav_range;
    p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }
  {
    auto& p = params[num_params++];
    p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    p.DescriptorTable.NumDescriptorRanges = 1;
    p.DescriptorTable.pDescriptorRanges = &sampler_range;
    p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }

  D3D12_ROOT_SIGNATURE_DESC desc = {};
  desc.NumParameters = num_params;
  desc.pParameters = params;
  desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

  Microsoft::WRL::ComPtr<ID3DBlob> blob, err_blob;
  HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1_0, &blob,
                                           &err_blob);
  if (FAILED(hr) || !blob) {
    REXGPU_ERROR("[custom-renderer] failed to serialize root signature (hr={:#010x})", (uint32_t)hr);
    return false;
  }
  hr = device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                   IID_PPV_ARGS(&root_signature_));
  if (FAILED(hr) || !root_signature_) {
    REXGPU_ERROR("[custom-renderer] failed to create root signature (hr={:#010x})", (uint32_t)hr);
    return false;
  }
  REXGPU_INFO("[custom-renderer] M4 root signature ready (6 CBVs + SRV/UAV/sampler tables)");
  return true;
}

// Root parameter indices (order matches EnsureGraphicsPipelineState).
namespace {
constexpr int kRootCbSystem = 0;
constexpr int kRootCbFloatVertex = 1;
constexpr int kRootCbFloatPixel = 2;
constexpr int kRootCbBoolLoop = 3;
constexpr int kRootCbFetch = 4;
constexpr int kRootCbDescriptorIndices = 5;
constexpr int kRootTableSrv = 6;
constexpr int kRootTableUav = 7;
constexpr int kRootTableSampler = 8;
}  // namespace

bool CustomCommandProcessor::EnsureDrawResources() {
  auto* provider =
      static_cast<rex::ui::d3d12::D3D12Provider*>(graphics_system_->provider());
  ID3D12Device* device = provider ? provider->GetDevice() : nullptr;
  if (!device) {
    return false;
  }
  if (draw_srv_heap_ && draw_sampler_heap_ && upload_buffer_ && index_buffer_ &&
      shared_memory_) {
    return true;
  }
  if (!EnsureGraphicsPipelineState()) {
    return false;
  }

  // Shared memory: 512 MB default-heap buffer mirroring Xenon physical memory.
  // The vertex shader reads vertex data through a raw R32 buffer SRV on it.
  constexpr uint32_t kSharedMemoryBytes = 1u << 29;  // 512 MB
  D3D12_RESOURCE_DESC sm_desc = {};
  rex::ui::d3d12::util::FillBufferResourceDesc(
      sm_desc, kSharedMemoryBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  D3D12_HEAP_PROPERTIES heap_props{};
  heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;
  CcrTrace("SHMEM", 0, 0, 0, 0, kSharedMemoryBytes);
  HRESULT hr = device->CreateCommittedResource(
      &heap_props, D3D12_HEAP_FLAG_NONE, &sm_desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
      IID_PPV_ARGS(&shared_memory_));
  if (FAILED(hr) || !shared_memory_) {
    if (!draw_resources_failed_logged_) {
      draw_resources_failed_logged_ = true;
      REXGPU_ERROR("[custom-renderer] shared memory buffer creation failed (hr={:#010x})",
                   (uint32_t)hr);
    }
    return false;
  }
  shared_memory_gpu_ = shared_memory_->GetGPUVirtualAddress();
  shared_memory_size_ = kSharedMemoryBytes;

  // Index buffer (separate from shared memory to avoid the SRV/IB state clash).
  index_buffer_size_ = 16u << 20;  // 16 MB (65536 * 32-bit indices)
  D3D12_RESOURCE_DESC ib_desc = {};
  rex::ui::d3d12::util::FillBufferResourceDesc(ib_desc, index_buffer_size_,
                                               D3D12_RESOURCE_FLAG_NONE);
  CcrTrace("IDXB", 0, 0, 0, 0, index_buffer_size_);
  if (FAILED(device->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE, &ib_desc,
                                             D3D12_RESOURCE_STATE_COMMON, nullptr,
                                             IID_PPV_ARGS(&index_buffer_)))) {
    REXGPU_ERROR("[custom-renderer] index buffer creation failed");
    return false;
  }
  index_buffer_gpu_ = index_buffer_->GetGPUVirtualAddress();

  // Upload buffer (mapped) for per-draw constants + vertex/index staging.
  // 32 MB is plenty for one frame's vertex/index staging + the small constant
  // cbuffer region (staging starts at kStagingBaseOffset=16 KB). The buffer is
  // an UPLOAD-heap resource; keeping it small caps the total committed GPU
  // memory well under the threshold at which the NVIDIA driver's
  // CreateCommittedResource allocation check stack-overflows (0xC0000409) after
  // ~50 committed resources.
  upload_buffer_size_ = 32u << 20;  // 32 MB
  D3D12_RESOURCE_DESC ub_desc = {};
  rex::ui::d3d12::util::FillBufferResourceDesc(ub_desc, upload_buffer_size_,
                                               D3D12_RESOURCE_FLAG_NONE);
  D3D12_HEAP_PROPERTIES upload_props{};
  upload_props.Type = D3D12_HEAP_TYPE_UPLOAD;
  CcrTrace("UPBUF", 0, 0, 0, 0, upload_buffer_size_);
  if (FAILED(device->CreateCommittedResource(&upload_props, D3D12_HEAP_FLAG_NONE, &ub_desc,
                                             D3D12_RESOURCE_STATE_COMMON, nullptr,
                                             IID_PPV_ARGS(&upload_buffer_)))) {
    REXGPU_ERROR("[custom-renderer] upload buffer creation failed");
    return false;
  }
  upload_buffer_gpu_ = upload_buffer_->GetGPUVirtualAddress();
  if (FAILED(upload_buffer_->Map(0, nullptr, reinterpret_cast<void**>(&upload_buffer_map_)))) {
    REXGPU_ERROR("[custom-renderer] upload buffer map failed");
    return false;
  }

  // Draw SRV/UAV combined heap. D3D12 allows only ONE CBV_SRV_UAV heap bound to a
  // command list, so the SRV table (t0..t32) and the UAV table (u0..u1) must share a
  // single heap. Layout: [0..kMaxDrawTextures] = SRVs (shared memory at 0, textures at
  // 1..), [kMaxDrawTextures+1 .. kMaxDrawTextures+2] = UAVs (shared memory, edram).
  constexpr uint32_t kSrvCount = kMaxDrawTextures + 1;
  constexpr uint32_t kUavCount = 2;
  D3D12_DESCRIPTOR_HEAP_DESC srv_heap_desc = {};
  srv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  srv_heap_desc.NumDescriptors = kSrvCount + kUavCount;
  // Must be shader-visible: bound via the root signature (SetGraphicsRootDescriptorTable),
  // and we read the GPU handle with GetGPUDescriptorHandleForHeapStart.
  srv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (FAILED(device->CreateDescriptorHeap(&srv_heap_desc, IID_PPV_ARGS(&draw_srv_heap_)))) {
    REXGPU_ERROR("[custom-renderer] draw SRV/UAV heap creation failed");
    return false;
  }
  draw_srv_heap_start_ = draw_srv_heap_->GetCPUDescriptorHandleForHeapStart();
  draw_srv_heap_gpu_ = draw_srv_heap_->GetGPUDescriptorHandleForHeapStart();
  draw_srv_desc_size_ =
      device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  // Shared memory raw R32 SRV at t0.
  D3D12_SHADER_RESOURCE_VIEW_DESC sm_srv = {};
  sm_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  sm_srv.Format = DXGI_FORMAT_R32_UINT;
  sm_srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  sm_srv.Buffer.NumElements = kSharedMemoryBytes / 4;
  sm_srv.Buffer.FirstElement = 0;
  sm_srv.Buffer.StructureByteStride = 0;
  sm_srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
  device->CreateShaderResourceView(shared_memory_.Get(), &sm_srv, draw_srv_heap_start_);
  // UAV part of the combined heap (u0 = shared memory raw R32, u1 = edram/null).
  D3D12_CPU_DESCRIPTOR_HANDLE uav_cpu =
      D3D12_CPU_DESCRIPTOR_HANDLE{draw_srv_heap_start_.ptr +
                                  uint64_t(kSrvCount) * draw_srv_desc_size_};
  D3D12_GPU_DESCRIPTOR_HANDLE uav_gpu =
      D3D12_GPU_DESCRIPTOR_HANDLE{draw_srv_heap_gpu_.ptr +
                                  uint64_t(kSrvCount) * draw_srv_desc_size_};
  draw_uav_heap_ = draw_srv_heap_;  // same physical heap; the UAV table points into it
  draw_uav_heap_start_ = uav_cpu;
  draw_uav_heap_gpu_ = uav_gpu;
  D3D12_UNORDERED_ACCESS_VIEW_DESC sm_uav = {};
  sm_uav.Format = DXGI_FORMAT_R32_UINT;
  sm_uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  sm_uav.Buffer.NumElements = kSharedMemoryBytes / 4;
  device->CreateUnorderedAccessView(shared_memory_.Get(), nullptr, &sm_uav, uav_cpu);

  // Sampler heap: s0.. for the sampler table.
  D3D12_DESCRIPTOR_HEAP_DESC sampler_heap_desc = {};
  sampler_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
  sampler_heap_desc.NumDescriptors = kMaxDrawTextures + 1;
  // Must be shader-visible: bound via the root signature sampler table.
  sampler_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (FAILED(device->CreateDescriptorHeap(&sampler_heap_desc,
                                          IID_PPV_ARGS(&draw_sampler_heap_)))) {
    REXGPU_ERROR("[custom-renderer] draw sampler heap creation failed");
    return false;
  }
  draw_sampler_heap_start_ = draw_sampler_heap_->GetCPUDescriptorHandleForHeapStart();
  draw_sampler_heap_gpu_ = draw_sampler_heap_->GetGPUDescriptorHandleForHeapStart();
  draw_sampler_desc_size_ =
      device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

  // RT pixel readback (diagnostics). Two buffers: a DEFAULT-heap intermediate
  // (valid copy destination for the texture row) and a READBACK-heap buffer
  // (CPU-mapped). The old single UPLOAD-heap buffer was an invalid copy
  // destination, which poisoned the command list on the one frame it fired.
  {
    D3D12_HEAP_PROPERTIES default_props{};
    default_props.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_HEAP_PROPERTIES upload_props{};
    upload_props.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC rb{};
    rex::ui::d3d12::util::FillBufferResourceDesc(rb, 65536, D3D12_RESOURCE_FLAG_NONE);
    // Default-heap intermediate: valid destination for the texture-row copy.
    CcrTrace("RBS", 0, 0, 0, 0, 65536);
    HRESULT rhr = device->CreateCommittedResource(
        &default_props, D3D12_HEAP_FLAG_NONE, &rb, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&readback_staging_));
    // CPU-mapped upload-heap buffer: a buffer->buffer copy destination (valid),
    // unlike a texture->buffer copy (which requires a default-heap dest).
    CcrTrace("RBU", 0, 0, 0, 0, 65536);
    rhr = device->CreateCommittedResource(
        &upload_props, D3D12_HEAP_FLAG_NONE, &rb, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&readback_buf_));
    if (SUCCEEDED(rhr) && readback_buf_ && readback_staging_) {
      D3D12_RANGE range{0, 65536};
      readback_buf_->Map(0, &range, reinterpret_cast<void**>(&readback_map_));
      REXGPU_INFO("[custom-renderer] readback buffers created map={}",
                  reinterpret_cast<const void*>(readback_map_));
    } else {
      REXGPU_WARN("[custom-renderer] readback buffer creation FAILED hr=0x{:08X} staging={} "
                  "buf={}",
                  static_cast<uint32_t>(rhr), static_cast<const void*>(readback_staging_.Get()),
                  static_cast<const void*>(readback_buf_.Get()));
    }
  }

  REXGPU_INFO("[custom-renderer] M4 draw resources ready (512MB shared memory, 16MB index, "
              "256MB upload, SRV+UAV+sampler heaps)");
  return true;
}

const std::vector<uint8_t>* CustomCommandProcessor::CompileShader(
    rex::graphics::xenos::ShaderType type, const uint32_t* ucode, uint32_t dword_count) {
  if (!ucode || dword_count == 0) {
    return nullptr;
  }
  const uint64_t hash = ShaderHash(type, ucode, dword_count);
  auto it = shader_cache_.find(hash);
  if (it != shader_cache_.end()) {
    return &it->second.dxil;
  }
  if (!EnsureDxilConverter()) {
    return nullptr;
  }

  auto* shader = new (std::nothrow) rex::graphics::d3d12::D3D12Shader(type, hash, ucode,
                                                                      dword_count);
  if (!shader) {
    return nullptr;
  }
  rex::string::StringBuffer disasm_buffer;
  shader->AnalyzeUcode(disasm_buffer);
  auto* translation = shader->GetOrCreateTranslation(0);
  if (!translation) {
    delete shader;
    return nullptr;
  }
  auto* provider =
      static_cast<rex::ui::d3d12::D3D12Provider*>(graphics_system_->provider());
  rex::graphics::DxbcShaderTranslator translator(provider->GetAdapterVendorID(), /*bindless=*/false,
                                                 /*edram_rov=*/false);
  if (!translator.TranslateAnalyzedShader(*translation)) {
    REXGPU_WARN("[custom-renderer] shader translation failed (hash {:016X})", hash);
    delete shader;
    return nullptr;
  }

  const std::vector<uint8_t>& dxbc = translation->translated_binary();
  void* dxil = nullptr;
  UINT32 dxil_size = 0;
  HRESULT hr = dxbc_converter_->Convert(dxbc.data(), (UINT32)dxbc.size(), nullptr, &dxil,
                                        &dxil_size, nullptr);
  if (FAILED(hr) || !dxil) {
    REXGPU_WARN("[custom-renderer] DXBC->DXIL conversion failed (hash {:016X}, hr={:#010x})", hash,
                (uint32_t)hr);
    delete shader;
    return nullptr;
  }

  auto& entry = shader_cache_[hash];
  entry.dxil.assign((const uint8_t*)dxil, (const uint8_t*)dxil + dxil_size);
  CoTaskMemFree(dxil);
  entry.shader = shader;  // keep alive: owns translation + binding layout

  static int shader_log_count = 0;
  if (shader_log_count < 8) {
    ++shader_log_count;
    REXGPU_INFO("[custom-renderer] compiled shader #{}/8: type={} hash={:016X} dwords={} -> {}B DXIL",
                shader_log_count, uint32_t(type), hash, dword_count, dxil_size);
  }
  return &entry.dxil;
}

rex::graphics::Shader* CustomCommandProcessor::LoadShader(
    rex::graphics::xenos::ShaderType shader_type, uint32_t guest_address,
    const uint32_t* host_address, uint32_t dword_count) {
  (void)guest_address;
  progress::Bump(kFeatShaders);
  const std::vector<uint8_t>* dxil = CompileShader(shader_type, host_address, dword_count);
  if (!dxil) {
    return nullptr;
  }
  // Return a lightweight Shader (cached by hash) so the base class can track
  // it as the active vertex/pixel shader. IssueDraw reads active_*_shader()
  // and uses ucode_data_hash() to look the compiled DXIL back up in shader_cache_.
  const uint64_t hash = ShaderHash(shader_type, host_address, dword_count);
  auto it = active_shaders_.find(hash);
  if (it != active_shaders_.end()) {
    return it->second;
  }
  auto* active = new (std::nothrow) rex::graphics::Shader(shader_type, hash, host_address,
                                                          dword_count);
  if (!active) {
    return nullptr;
  }
  active_shaders_[hash] = active;
  return active;
}

namespace {
D3D12_PRIMITIVE_TOPOLOGY MapTopology(rex::graphics::xenos::PrimitiveType prim) {
  switch (prim) {
    case rex::graphics::xenos::PrimitiveType::kPointList:
      return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
    case rex::graphics::xenos::PrimitiveType::kLineList:
      return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
    case rex::graphics::xenos::PrimitiveType::kLineStrip:
      return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
    case rex::graphics::xenos::PrimitiveType::kTriangleList:
    case rex::graphics::xenos::PrimitiveType::kRectangleList:
      return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    case rex::graphics::xenos::PrimitiveType::kTriangleStrip:
      return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    default:
      return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
  }
}

// Map a D3D primitive topology to the topology-group of its PSO. The driver
// rejects a draw whose topology group differs from the bound PSO's group, so a
// point/line draw needs a point/line PSO, not the triangle one.
D3D12_PRIMITIVE_TOPOLOGY_TYPE TopologyTypeOf(D3D12_PRIMITIVE_TOPOLOGY topology) {
  switch (topology) {
    case D3D_PRIMITIVE_TOPOLOGY_POINTLIST:
      return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    case D3D_PRIMITIVE_TOPOLOGY_LINELIST:
    case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP:
      return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    default:
      return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  }
}

// Pack the float constants a shader actually reads (per its constant_register_map)
// into `dst` (a float array), reading from the register file at `base_register`.
void FillFloatConstants(const rex::graphics::Shader* shader, float* dst, uint32_t base_register,
                        const rex::graphics::RegisterFile& rf) {
  const auto& crm = shader->constant_register_map();
  for (uint32_t n = 0; n < 256; ++n) {
    const bool used = crm.float_dynamic_addressing ||
                      (crm.float_bitmap[n >> 6] >> (n & 63)) & 1;
    if (!used) {
      continue;
    }
    const uint32_t packed =
        crm.float_dynamic_addressing ? n : crm.GetPackedFloatConstantIndex(n);
    if (packed >= 256) {
      continue;
    }
    float* out = dst + packed * 4;
    for (int c = 0; c < 4; ++c) {
      const uint32_t bits = rf.values[base_register + n * 4 + c];
      std::memcpy(&out[c], &bits, 4);
    }
  }
}
}  // namespace

ID3D12PipelineState* CustomCommandProcessor::GetOrCreatePipeline(
    rex::graphics::d3d12::D3D12Shader* vs, rex::graphics::d3d12::D3D12Shader* ps,
    D3D12_PRIMITIVE_TOPOLOGY_TYPE topology_type) {
  auto* provider =
      static_cast<rex::ui::d3d12::D3D12Provider*>(graphics_system_->provider());
  ID3D12Device* device = provider ? provider->GetDevice() : nullptr;
  if (!device || !root_signature_) {
    return nullptr;
  }
  // Cache key includes the topology group: point/line/triangle draws need
  // distinct PSOs because each PSO is bound to a single topology group.
  const uint64_t key = vs->ucode_data_hash() ^
                       (ps->ucode_data_hash() * 0x9E3779B97F4A7C15ULL) ^
                       (static_cast<uint64_t>(topology_type) << 56);
  auto it = pipeline_cache_.find(key);
  if (it != pipeline_cache_.end()) {
    return it->second;
  }
  auto vs_it = shader_cache_.find(vs->ucode_data_hash());
  auto ps_it = shader_cache_.find(ps->ucode_data_hash());
  if (vs_it == shader_cache_.end() || ps_it == shader_cache_.end() ||
      vs_it->second.dxil.empty() || ps_it->second.dxil.empty()) {
    return nullptr;
  }

  D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
  desc.pRootSignature = root_signature_;
  desc.VS = {vs_it->second.dxil.data(), (UINT)vs_it->second.dxil.size()};
  desc.PS = {ps_it->second.dxil.data(), (UINT)ps_it->second.dxil.size()};
  desc.InputLayout = {};  // empty - vertex data comes from the shared-memory SRV
  // The PSO is bound to a single topology group (point/line/triangle); a draw
  // whose topology group differs is rejected. The caller passes the group that
  // matches this draw's primitive topology (see TopologyTypeOf / MapTopology).
  desc.PrimitiveTopologyType = topology_type;
  desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;  // no culling for a first pass
  desc.RasterizerState.DepthClipEnable = FALSE;  // DIAG: rule out z-clip
  desc.RasterizerState.FrontCounterClockwise = TRUE;
  desc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
  desc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
  desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  desc.SampleMask = 0xFFFFFFFF;
  desc.DepthStencilState.DepthEnable = FALSE;  // DIAG: rule out depth rejection
  desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
  desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
  desc.DepthStencilState.StencilEnable = FALSE;
  desc.DSVFormat = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
  desc.NumRenderTargets = 1;
  desc.RTVFormats[0] = DXGI_FORMAT_R10G10B10A2_UNORM;
  desc.SampleDesc.Count = 1;

  ID3D12PipelineState* pso = nullptr;
  HRESULT hr = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso));
  if (FAILED(hr) || !pso) {
    static int logged = 0;
    if (logged++ < 3) {
      REXGPU_WARN("[custom-renderer] PSO creation failed (hr={:#010x})", (uint32_t)hr);
    }
    return nullptr;
  }
  pipeline_cache_[key] = pso;
  return pso;
}

void CustomCommandProcessor::BeginFrameDrawing() {
  frame_recording_ = false;
  frame_draw_count_ = 0;
  auto* provider =
      static_cast<rex::ui::d3d12::D3D12Provider*>(graphics_system_->provider());
  ID3D12GraphicsCommandList* cl = command_list_.Get();
  if (!cl || !rt_cache_ || !provider) {
    return;
  }

  // Create/refresh the main render target (color + depth) if needed.
  if (!(main_rt_ && main_rt_->valid && main_rt_->width == main_rt_w_ &&
        main_rt_->height == main_rt_h_)) {
    main_rt_ =
        rt_cache_->GetOrCreate(0, main_rt_w_, main_rt_h_, DXGI_FORMAT_R10G10B10A2_UNORM, true);
    if (main_rt_ && main_rt_->just_created) {
      ds_state_ = 0;  // a new depth buffer starts in COMMON
    }
  }
  if (!main_rt_ || !main_rt_->valid || !main_rt_->color) {
    return;
  }
  ID3D12Resource* rt_res = main_rt_->color.Get();
  {
    static int diag_logged = 0;
    if (diag_logged++ < 3) {
      REXGPU_INFO(
          "[custom-renderer] DIAG RT ptrs: color={} depth={} same={} has_depth={} "
          "ds_state_={} valid={}",
          reinterpret_cast<const void*>(rt_res),
          reinterpret_cast<const void*>(main_rt_->depth.Get()),
          rt_res == main_rt_->depth.Get(), uint32_t(main_rt_->has_depth), ds_state_,
          uint32_t(main_rt_->valid));
    }
  }

  // Close a stale open command list (e.g. the previous frame was never
  // presented) before re-resetting. Reset clears all command-list state -
  // including the bound descriptor heaps - so the heaps are (re)bound after it.
  if (command_list_open_) {
    cl->Close();
    command_list_open_ = false;
  }
  // Reclaim an allocator slot (wait for the frame 3 ago to finish).
  const uint64_t submission = submission_counter_;
  const uint32_t slot = uint32_t(submission % kQueueFrames);
  const uint64_t wait_fence = submission >= kQueueFrames ? submission - kQueueFrames + 1 : 0;
  if (submission >= kQueueFrames) {
    WaitForFenceValue(wait_fence);
  }
  {
    static int reset_diag = 0;
    ++reset_diag;
    if (reset_diag % 250 == 0 || fence_ ? fence_->GetCompletedValue() < wait_fence : false) {
      REXGPU_INFO("[custom-renderer] DIAG M4 reset slot={} submission={} wait_fence={} "
                  "fence_completed={}",
                  slot, submission, wait_fence, fence_ ? fence_->GetCompletedValue() : 0);
    }
  }
  allocators_[slot]->Reset();
  cl->Reset(allocators_[slot].Get(), nullptr);

  // Bind the descriptor heaps (must be after Reset, which clears them). Only
  // SHADER_VISIBLE heaps may be bound here - the RTV/DSV heaps are used via CPU
  // handles in OMSetRenderTargets/Clear and must NOT be bound (binding them makes
  // the whole SetDescriptorHeaps call fail, leaving every root descriptor table
  // pointing at an unbound heap).
  {
    // At most one heap of each type; the SRV+UAV tables share the single
    // CBV_SRV_UAV heap (draw_srv_heap_), so exactly two shader-visible heaps.
    ID3D12DescriptorHeap* heaps[2] = {draw_srv_heap_, draw_sampler_heap_};
    uint32_t n = 0;
    for (uint32_t i = 0; i < 2; ++i) {
      if (heaps[i]) {
        heaps[n++] = heaps[i];
      }
    }
    cl->SetDescriptorHeaps(n, heaps);
  }

  // Transition the color RT (COPY_DEST -> RENDER_TARGET). Kept in its own
  // ResourceBarrier call: when a color (RENDER_TARGET) and depth (DEPTH_WRITE)
  // transition share one call, the driver misattributes states
  // (RESOURCE_BARRIER_MISMATCHING_MISC_FLAGS / BEFORE_AFTER_MISMATCH).
  {
    D3D12_RESOURCE_BARRIER rt_barrier{};
    rt_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    rt_barrier.Transition.pResource = rt_res;
    rt_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    rt_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    rt_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    cl->ResourceBarrier(1, &rt_barrier);
  }
  // Depth buffer in its own barrier call. Only issue it when the state actually
  // changes: once in DEPTH_WRITE it stays there (the present path never touches
  // it), and a before==after barrier is rejected (RESOURCE_BARRIER_MATCHING_STATES).
  if (main_rt_->has_depth && main_rt_->depth &&
      ds_state_ != uint32_t(D3D12_RESOURCE_STATE_DEPTH_WRITE)) {
    D3D12_RESOURCE_BARRIER ds_barrier{};
    ds_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    ds_barrier.Transition.pResource = main_rt_->depth.Get();
    ds_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    ds_barrier.Transition.StateBefore = static_cast<D3D12_RESOURCE_STATES>(ds_state_);
    ds_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    cl->ResourceBarrier(1, &ds_barrier);
  }
  ds_state_ = uint32_t(D3D12_RESOURCE_STATE_DEPTH_WRITE);  // 0x10 (NOT 0x4, which is RENDER_TARGET)
  UpdateGuestClear();  // decode this frame's clear color from the register file
  // DIAGNOSTIC: force a bright cyan clear so the present path is unambiguous.
  // If the screen shows cyan, RT->guest-output copy works; if black, it is broken.
  // Drawn geometry paints over the cyan, so visible shapes == draws are landing.
  FLOAT clear_color[4] = {1.0f, 0.0f, 1.0f, 1.0f};  // DIAG: magenta to distinguish clear vs drawn
  cl->ClearRenderTargetView(main_rt_->rtv, clear_color, 0, nullptr);
  if (main_rt_->has_depth) {
    cl->ClearDepthStencilView(main_rt_->dsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
                              1.0f, 0, 0, nullptr);
  }
  // Bind the render targets for the whole frame.
  D3D12_CPU_DESCRIPTOR_HANDLE rtv = main_rt_->rtv;
  cl->OMSetRenderTargets(1, &rtv, FALSE, main_rt_->has_depth ? &main_rt_->dsv : nullptr);

  // DIAG: trivial fullscreen triangle (bypasses the whole Xenos path) to verify
  // the raw D3D12 draw->RT->present pipeline works on this render target.
  if (debug_tri_enabled_ && !debug_tri_pso_ && !debug_tri_root_) {
    auto* dbg_provider = static_cast<rex::ui::d3d12::D3D12Provider*>(graphics_system_->provider());
    ID3D12Device* device = dbg_provider ? dbg_provider->GetDevice() : nullptr;
    HMODULE dc = LoadLibraryW(L"d3dcompiler_47.dll");
    auto d3dc = dc ? reinterpret_cast<decltype(&D3DCompile)>(GetProcAddress(dc, "D3DCompile"))
                   : nullptr;
    auto compile_vs5 = [&](const char* hlsl, const char* target, std::vector<uint8_t>& out) {
      ID3DBlob* blob = nullptr;
      ID3DBlob* err = nullptr;
      HRESULT hr = d3dc(hlsl, strlen(hlsl) + 1, "dbg", nullptr, nullptr, "main", target, 0, 0,
                        &blob, &err);
      if (FAILED(hr) || !blob) {
        if (err) REXGPU_WARN("[custom-renderer] DIAG d3dcompile err({}): {}", target,
                             (char*)err->GetBufferPointer());
        if (err) err->Release();
        return false;
      }
      if (err) err->Release();
      out.assign((const uint8_t*)blob->GetBufferPointer(),
                 (const uint8_t*)blob->GetBufferPointer() + blob->GetBufferSize());
      blob->Release();
      return true;
    };
    auto to_dxil = [&](const std::vector<uint8_t>& dxbc, std::vector<uint8_t>& out) {
      void* dxil = nullptr; UINT32 n = 0;
      HRESULT hr = dxbc_converter_->Convert(dxbc.data(), (UINT32)dxbc.size(), nullptr, &dxil, &n, nullptr);
      if (FAILED(hr) || !dxil) { REXGPU_WARN("[custom-renderer] DIAG DXBC->DXIL hr={:#010x}", (uint32_t)hr); return false; }
      out.assign((const uint8_t*)dxil, (const uint8_t*)dxil + n);
      CoTaskMemFree(dxil);
      return true;
    };
    static const char kVsHlsl[] =
        "float4 main(uint vid:SV_VertexId):SV_Position{"
        "float2 p;"
        "if(vid==0)p=float2(-1,-1);"
        "else if(vid==1)p=float2(3,-1);"
        "else p=float2(-1,3);"
        "return float4(p,0,1);}";
    static const char kPsHlsl[] = "float4 main():SV_Target{return float4(1,1,0,1);}";
    std::vector<uint8_t> vs5, ps5, vs_dxil, ps_dxil;
    if (d3dc && device && root_signature_ && EnsureDxilConverter() &&
        compile_vs5(kVsHlsl, "vs_5_0", vs5) && compile_vs5(kPsHlsl, "ps_5_0", ps5) &&
        to_dxil(vs5, vs_dxil) && to_dxil(ps5, ps_dxil)) {
      debug_tri_root_ = root_signature_;  // reuse the Xenos root signature (33 params)
      D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
      pd.VS = {vs_dxil.data(), (UINT)vs_dxil.size()};
      pd.PS = {ps_dxil.data(), (UINT)ps_dxil.size()};
      pd.InputLayout = {};
      pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
      pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
      pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
      pd.RasterizerState.DepthClipEnable = FALSE;  // DIAG: rule out z-clip
      pd.RasterizerState.FrontCounterClockwise = FALSE;
      pd.BlendState.RenderTarget[0].BlendEnable = FALSE;
      pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
      pd.DepthStencilState.DepthEnable = FALSE;
      pd.SampleMask = UINT_MAX;
      pd.SampleDesc.Count = 1;
      pd.RTVFormats[0] = DXGI_FORMAT_R10G10B10A2_UNORM;
      pd.DSVFormat = DXGI_FORMAT_UNKNOWN;
      pd.NodeMask = 0;
      pd.pRootSignature = debug_tri_root_;
      HRESULT phr = device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&debug_tri_pso_));
      if (SUCCEEDED(phr)) {
        REXGPU_INFO("[custom-renderer] DIAG debug-triangle PSO ready vs={}B ps={}B", (uint32_t)vs_dxil.size(), (uint32_t)ps_dxil.size());
      } else {
        REXGPU_WARN("[custom-renderer] DIAG debug-triangle PSO failed hr={:#010x}", (uint32_t)phr);
      }
    } else {
      REXGPU_WARN("[custom-renderer] DIAG debug-triangle setup failed (d3dc={} device={} rs={} conv={})",
                  (bool)d3dc, (bool)device, (bool)root_signature_, (bool)dxbc_converter_);
    }
  }
  if (debug_tri_enabled_ && debug_tri_pso_) {
    cl->SetGraphicsRootSignature(debug_tri_root_);
    cl->SetPipelineState(debug_tri_pso_);
    cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cl->DrawInstanced(3, 1, 0, 0);
    cl->SetGraphicsRootSignature(root_signature_);  // restore for the Xenos draws
  }

  // Reset the staging cursor in the upload buffer for this frame.
  upload_cursor_ = kStagingBaseOffset;
  frame_recording_ = true;
  command_list_open_ = true;
}

bool CustomCommandProcessor::EndFramePresent(ID3D12Resource* guest_output, uint32_t width,
                                             uint32_t height) {
  ID3D12GraphicsCommandList* cl = command_list_.Get();
  if (!cl || !main_rt_ || !main_rt_->valid) {
    frame_recording_ = false;
    return false;
  }
  ID3D12Resource* rt_res = main_rt_->color.Get();
  // RT (RENDER_TARGET) -> COPY_SOURCE; guest output -> COPY_DEST.
  D3D12_RESOURCE_BARRIER barriers[2]{};
  barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barriers[0].Transition.pResource = rt_res;
  barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
  barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barriers[1].Transition.pResource = guest_output;
  barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
  cl->ResourceBarrier(2, barriers);
  // Texture -> texture copy (RT -> guest output).
  D3D12_TEXTURE_COPY_LOCATION src{};
  src.pResource = rt_res;
  src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  src.SubresourceIndex = 0;
  D3D12_TEXTURE_COPY_LOCATION dst{};
  dst.pResource = guest_output;
  dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  dst.SubresourceIndex = 0;
  cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
  // Per-frame present count + a proper RT-row readback (diagnostic). The readback
  // fires a couple of times: RT row 360 -> default-heap buffer -> readback-heap
  // mapped buffer, all valid copy ops (unlike the old copy-to-upload-buffer).
  static uint32_t readback_present_count = 0;
  ++readback_present_count;
  if (readback_present_count % 200 == 0) {
    REXGPU_INFO("[custom-renderer] DIAG EndFramePresent count = {}", readback_present_count);
  }
  // Shared-memory vertex readback: a few frames after the copy was queued, the
  // GPU has executed it, so read the CPU-mapped buffer and compare with guest.
  if (diag_rb_pending_ && readback_map_) {
    ++diag_rb_present_;
    if (diag_rb_present_ >= 5) {
      diag_rb_pending_ = false;
      const uint32_t* gpu = readback_map_;
      const uint8_t* guest = memory_->TranslatePhysical<const uint8_t*>(diag_rb_addr_);
      const uint32_t n = diag_rb_size_ / 4;
      uint32_t match = 0, nonzero = 0;
      for (uint32_t i = 0; i < n; ++i) {
        if (guest && gpu[i] == *reinterpret_cast<const uint32_t*>(guest + i * 4)) ++match;
        if (gpu[i] != 0) ++nonzero;
      }
      REXGPU_INFO("[custom-renderer] DIAGRB gpu-shmem readback addr={:#010x} size={} match={}/{} nonzero={}",
                  diag_rb_addr_, diag_rb_size_, match, n, nonzero);
      for (uint32_t i = 0; i < n && i < 24; ++i) {
        REXGPU_INFO("[custom-renderer] DIAGRB  dw[{}]={:08X}{}", i, gpu[i],
                    (guest && *reinterpret_cast<const uint32_t*>(guest + i * 4) == gpu[i]) ? "" : " (guest differs)");
      }
    }
  }
  // Readback disabled: it blocked the GPU-commands thread on a WaitForFenceValue
  // over a heavy (vertex-upload) frame and correlated with device removal.
  const bool do_readback =
      readback_staging_ && readback_buf_ &&
      (readback_present_count == 300 || readback_present_count == 600 ||
       readback_present_count == 900 || readback_present_count == 1500 ||
       readback_present_count == 3000);
  if (do_readback) {
    D3D12_TEXTURE_COPY_LOCATION rb_src{};
    rb_src.pResource = rt_res;  // currently COPY_SOURCE (from the present barrier above)
    rb_src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    rb_src.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION rb_stg{};
    rb_stg.pResource = readback_staging_.Get();
    rb_stg.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    rb_stg.PlacedFootprint.Offset = 0;
    rb_stg.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
    rb_stg.PlacedFootprint.Footprint.Width = 1280;
    rb_stg.PlacedFootprint.Footprint.Height = 1;
    rb_stg.PlacedFootprint.Footprint.Depth = 1;
    rb_stg.PlacedFootprint.Footprint.RowPitch = 1280 * 4;
    D3D12_BOX box{};
    box.left = 0;
    box.right = 1280;
    box.top = 360;
    box.bottom = 361;
    box.front = 0;
    box.back = 1;
    cl->CopyTextureRegion(&rb_stg, 0, 0, 0, &rb_src, &box);
    cl->CopyBufferRegion(readback_buf_.Get(), 0, readback_staging_.Get(), 0, 1280 * 4);
  }
  // Guest output back to PIXEL_SHADER_RESOURCE, RT back to COPY_DEST.
  D3D12_RESOURCE_BARRIER back[2]{};
  back[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  back[0].Transition.pResource = guest_output;
  back[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  back[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  back[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  back[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  back[1].Transition.pResource = rt_res;
  back[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  back[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
  back[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
  cl->ResourceBarrier(2, back);
  HRESULT close_hr = cl->Close();
  if (FAILED(close_hr)) {
    static uint32_t close_fail_count = 0;
    ++close_fail_count;
    if (close_fail_count <= 5 || (close_fail_count % 500 == 0)) {
      REXGPU_WARN("[custom-renderer] DIAG EndFramePresent Close FAILED hr=0x{:08X} (count={})",
                  static_cast<uint32_t>(close_hr), close_fail_count);
    }
    frame_recording_ = false;
    command_list_open_ = false;
    return false;
  }
  command_list_open_ = false;
  ID3D12CommandList* lists[1] = {cl};
  queue_->ExecuteCommandLists(1, lists);
  fence_value_ = submission_counter_ + 1;
  fence_->Signal(fence_value_);
  submission_counter_ = fence_value_;
  frame_recording_ = false;
  if (do_readback && readback_map_) {
    WaitForFenceValue(fence_value_);
    const uint32_t* p = readback_map_;
    const int N = 1280;
    int nonzero = 0, magenta = 0;
    uint32_t mn = 0xFFFFFFFF, mx = 0;
    uint32_t distinct[32] = {};
    int ndistinct = 0;
    for (int i = 0; i < N; ++i) {
      const uint32_t v = p[i];
      if (v) ++nonzero;
      if (v == 0xCFF003FF) ++magenta;  // R10G10B10A2 magenta (1,0,1,1)
      if (v < mn) mn = v;
      if (v > mx) mx = v;
      if (ndistinct < 32) {
        int found = 0;
        for (int d = 0; d < ndistinct; ++d) {
          if (distinct[d] == v) {
            found = 1;
            break;
          }
        }
        if (!found) distinct[ndistinct++] = v;
      }
    }
    REXGPU_INFO(
        "[custom-renderer] DIAG RT row360[{}px] count={}: nonzero={} magenta(clear)={} "
        "DRAWN={} distinct={} min={:08X} max={:08X} d0={:08X} d1={:08X} d2={:08X}",
        N, readback_present_count, nonzero, magenta, N - magenta, ndistinct, mn, mx,
        distinct[0], distinct[1], distinct[2]);
  }
  return true;
}

bool CustomCommandProcessor::IssueDraw(rex::graphics::xenos::PrimitiveType prim_type,
                                       uint32_t index_count,
                                       IndexBufferInfo* index_buffer_info,
                                       bool major_mode_explicit) {
  progress::Bump(kFeatDraws);
  auto* vs = active_vertex_shader();
  auto* ps = active_pixel_shader();
  if (!vs || !ps) {
    return true;
  }
  auto vs_it = shader_cache_.find(vs->ucode_data_hash());
  auto ps_it = shader_cache_.find(ps->ucode_data_hash());
  if (vs_it == shader_cache_.end() || ps_it == shader_cache_.end() ||
      vs_it->second.dxil.empty() || ps_it->second.dxil.empty() || !vs_it->second.shader ||
      !ps_it->second.shader) {
    return true;
  }
  if (!EnsureDrawResources()) {
    return true;
  }
  ID3D12GraphicsCommandList* cl = command_list_.Get();
  if (!frame_recording_) {
    BeginFrameDrawing();
    if (!frame_recording_) {
      return true;
    }
  }
  auto* vs_shader = vs_it->second.shader;
  auto* ps_shader = ps_it->second.shader;
  const D3D12_PRIMITIVE_TOPOLOGY topology = MapTopology(prim_type);
  ID3D12PipelineState* pso =
      GetOrCreatePipeline(vs_shader, ps_shader, TopologyTypeOf(topology));
  if (!pso) {
    return true;
  }
  cl->SetPipelineState(pso);
  cl->SetGraphicsRootSignature(root_signature_);

  // Viewport + scissor from the guest viewport registers.
  rex::graphics::draw_util::ViewportInfo vi{};
  rex::graphics::draw_util::GetHostViewportInfo(
      *register_file_, 1, 1, /*origin_bottom_left=*/true, main_rt_w_, main_rt_h_,
      /*allow_reverse_z=*/false, register_file_->Get<rex::graphics::reg::RB_DEPTHCONTROL>(),
      /*convert_z_to_float24=*/false, /*full_float24_in_0_to_1=*/true,
      /*pixel_shader_writes_depth=*/false, vi);
  D3D12_VIEWPORT viewport{float(vi.xy_offset[0]), float(vi.xy_offset[1]),
                          float(vi.xy_extent[0]), float(vi.xy_extent[1]), vi.z_min, vi.z_max};
  D3D12_RECT scissor{int32_t(vi.xy_offset[0]), int32_t(vi.xy_offset[1]),
                     int32_t(vi.xy_offset[0] + vi.xy_extent[0]),
                     int32_t(vi.xy_offset[1] + vi.xy_extent[1])};
  cl->RSSetViewports(1, &viewport);
  cl->RSSetScissorRects(1, &scissor);
  cl->IASetPrimitiveTopology(topology);

  // Upload vertex data (guest memory -> shared memory buffer) for every vertex
  // fetch constant the VS uses. The shared memory buffer is a 1:1 mirror of
  // Xenon physical memory, so the byte offset == the guest physical address.
  const auto& crm = vs_shader->constant_register_map();
  const uint32_t kMemLimit = 0x20000000u;  // 512 MB of Xenon physical memory
  // Shared memory: transition to COPY_DEST (upload), then ALL_SHADER_RESOURCE
  // (the VS reads vertex data through the R32 buffer SRV).
  auto transition = [&](ID3D12Resource* res, uint32_t before, uint32_t after) {
    if (before == after) {
      return;
    }
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = res;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = static_cast<D3D12_RESOURCE_STATES>(before);
    b.Transition.StateAfter = static_cast<D3D12_RESOURCE_STATES>(after);
    cl->ResourceBarrier(1, &b);
  };
  const uint32_t kCommon = 0, kCopyDest = 0x400, kAllSr = 0xC0, kIndexBuf = 0x2;
  const uint32_t kGenRead = uint32_t(D3D12_RESOURCE_STATE_GENERIC_READ);
  // Determine how many vertices the draw can reference. For indexed draws the
  // index buffer can reference vertices FAR beyond index_count (e.g. indices
  // 256/512/768 with only 6 indices), so the whole array up to max_index+1 must
  // be uploaded; uploading only index_count vertices leaves the referenced
  // vertices as zero in shared memory and the geometry is degenerate.
  uint32_t vertex_count = index_count;
  if (index_buffer_info && index_count > 0) {
    const uint32_t idx_size = (index_buffer_info->format == rex::graphics::xenos::IndexFormat::kInt32) ? 4 : 2;
    const uint32_t ib_addr = index_buffer_info->guest_base;
    if (ib_addr < kMemLimit) {
      const uint8_t* ib = memory_->TranslatePhysical<const uint8_t*>(ib_addr);
      if (ib) {
        // The vertex shader byte-swaps each raw index by the index endianness
        // (VGT_DMA_SIZE.swap_mode) before using it, so the REAL vertex index is
        // GpuSwap(raw, endian) + offset. Compute the max of the real indices so
        // the upload range always covers every vertex the VS will read.
        const auto vgt_dma_size = register_file_->Get<rex::graphics::reg::VGT_DMA_SIZE>();
        const auto vgt_draw_init = register_file_->Get<rex::graphics::reg::VGT_DRAW_INITIATOR>();
        rex::graphics::xenos::Endian idx_endian = vgt_dma_size.swap_mode;
        if (vgt_draw_init.index_size == rex::graphics::xenos::IndexFormat::kInt16 &&
            idx_endian != rex::graphics::xenos::Endian::kNone &&
            idx_endian != rex::graphics::xenos::Endian::k8in16) {
          idx_endian = (idx_endian == rex::graphics::xenos::Endian::k8in32)
                           ? rex::graphics::xenos::Endian::k8in16
                           : rex::graphics::xenos::Endian::kNone;
        }
        const uint32_t idx_offset =
            register_file_->Get<rex::graphics::reg::VGT_INDX_OFFSET>().indx_offset;
        uint32_t max_idx = 0;
        for (uint32_t i = 0; i < index_count; ++i) {
          const uint32_t raw = (idx_size == 2)
              ? *reinterpret_cast<const uint16_t*>(ib + i * 2)
              : *reinterpret_cast<const uint32_t*>(ib + i * 4);
          const uint32_t ix =
              (idx_size == 2)
                  ? rex::graphics::xenos::GpuSwap(uint16_t(raw), idx_endian) + idx_offset
                  : rex::graphics::xenos::GpuSwap(raw, idx_endian) + idx_offset;
          if (ix > max_idx) max_idx = ix;
        }
        // Cap the scan at 65536 vertices (2^16) to bound the upload size; this
        // covers the index range of 16-bit index buffers and is generous for 32-bit.
        uint32_t cap = 65536u;
        vertex_count = max_idx + 1;
        if (vertex_count > cap) vertex_count = cap;
      }
    }
  }
  // The upload buffer is the COPY SOURCE of every upload and the backing
  // store for the cbuffer root parameters (which need VERTEX_AND_CONSTANT_
  // BUFFER visibility). It is created in COMMON, so transition it once to
  // GENERIC_READ (includes both COPY_SOURCE and VACB) and leave it there.
  // Without this the driver silently drops every CopyBufferRegion, so the
  // shared memory buffer on the GPU never receives the vertex/index data.
  transition(upload_buffer_.Get(), ub_state_, kGenRead);
  ub_state_ = kGenRead;
  transition(shared_memory_.Get(), sm_state_, kCopyDest);
  sm_state_ = kCopyDest;
  for (uint32_t i = 0; i < 96; ++i) {
    if (!((crm.vertex_fetch_bitmap[i >> 5] >> (i & 31)) & 1)) {
      continue;
    }
    const auto vf = register_file_->GetVertexFetch(i);
    if (vf.type != rex::graphics::xenos::FetchConstantType::kVertex) {
      if (!bad_vfetch_logged_) {
        bad_vfetch_logged_ = true;
        REXGPU_WARN("[custom-renderer] vertex fetch constant {} has type {} (expecting "
                    "kVertex main-memory) - skipping upload",
                    i, uint32_t(vf.type));
      }
      continue;
    }
    const uint32_t stride_bytes = vf.size << 2;   // per-vertex stride (bytes)
    const uint32_t byte_addr = vf.address << 2;   // vertex array base (bytes)
    if (byte_addr >= kMemLimit || stride_bytes == 0 || index_count == 0 ||
        upload_cursor_ >= upload_buffer_size_) {
      continue;
    }
    // The fetch constant only gives the per-vertex stride, not the total array
    // size. The shader reads vertex[i] for every referenced index i through the
    // shared-memory SRV (t0), so the WHOLE array up to vertex_count must be
    // uploaded (vertex_count covers max_index+1 for indexed draws).
    uint64_t total = (uint64_t)vertex_count * stride_bytes;
    // Never read past the guest arena or write past the staging buffer.
    if (total > (uint64_t)kMemLimit - byte_addr) {
      total = (uint64_t)kMemLimit - byte_addr;
    }
    const uint64_t remaining = upload_buffer_size_ - upload_cursor_;
    if (total > remaining) {
      total = remaining;
    }
    total &= ~3ull;  // CopyBufferRegion size must be a multiple of 4 bytes.
    if (total == 0) {
      continue;
    }
    const uint8_t* guest = memory_->TranslatePhysical<const uint8_t*>(byte_addr);
    if (!guest) {
      continue;
    }
    std::memcpy(upload_buffer_map_ + upload_cursor_, guest, (size_t)total);
    cl->CopyBufferRegion(shared_memory_.Get(), byte_addr, upload_buffer_.Get(), upload_cursor_,
                         (SIZE_T)total);
    upload_cursor_ += (uint32_t)total;
    // Round the cursor up to 256 for the next staged copy, but never past the end
    // (rounding up here must not re-introduce an overflow).
    upload_cursor_ = (upload_cursor_ + 255) & ~255u;
    if (upload_cursor_ > upload_buffer_size_) {
      upload_cursor_ = upload_buffer_size_;
    }
  }
  transition(shared_memory_.Get(), sm_state_, kAllSr);
  sm_state_ = kAllSr;

  // DIAG (focused): for the first few 3D triangle draws, report whether the
  // referenced vertex range is mostly non-zero (coherent mesh) or sparse, plus
  // the clip-disable state and the ACTUAL draw viewport (allow_reverse_z=false).
  {
    static uint32_t vtx_sparse_diag = 0;
    const uint32_t pt3 = uint32_t(prim_type);
    if ((pt3 == 4 || pt3 == 6) && index_count >= 3 && vtx_sparse_diag < 5) {
      const auto& vfb2 = vs_shader->constant_register_map().vertex_fetch_bitmap;
      const bool clip_disable =
          register_file_->Get<rex::graphics::reg::PA_CL_CLIP_CNTL>().clip_disable;
      rex::graphics::draw_util::ViewportInfo vi2{};
      rex::graphics::draw_util::GetHostViewportInfo(
          *register_file_, 1, 1, true, main_rt_w_, main_rt_h_, false,
          register_file_->Get<rex::graphics::reg::RB_DEPTHCONTROL>(), false, true, false, vi2);
      REXGPU_INFO("[custom-renderer] DIAGS #{} prim={} idx={} clip_disable={} viewport z=[{:.3f},{:.3f}] ext=({},{}) ndc_scale=({:.4f},{:.4f},{:.4f}) ndc_offset=({:.4f},{:.4f},{:.4f})",
                  vtx_sparse_diag, pt3, index_count, clip_disable, vi2.z_min, vi2.z_max,
                  vi2.xy_extent[0], vi2.xy_extent[1], vi2.ndc_scale[0], vi2.ndc_scale[1],
                  vi2.ndc_scale[2], vi2.ndc_offset[0], vi2.ndc_offset[1], vi2.ndc_offset[2]);
      ++vtx_sparse_diag;
      // For the first kVertex vfetch, count non-zero vertices in [0, max_index].
      if (index_buffer_info) {
        const uint32_t idx_size =
            (index_buffer_info->format == rex::graphics::xenos::IndexFormat::kInt32) ? 4 : 2;
        const uint32_t ib_addr = index_buffer_info->guest_base;
        uint32_t max_idx = 0;
        if (ib_addr < kMemLimit) {
          const uint8_t* ib = memory_->TranslatePhysical<const uint8_t*>(ib_addr);
          if (ib) {
            for (uint32_t i = 0; i < index_count; ++i) {
              uint32_t ix = (idx_size == 2) ? *reinterpret_cast<const uint16_t*>(ib + i * 2)
                                            : *reinterpret_cast<const uint32_t*>(ib + i * 4);
              if (ix > max_idx) max_idx = ix;
            }
          }
        }
        for (uint32_t vi = 0; vi < 96; ++vi) {
          if (!((vfb2[vi >> 5] >> (vi & 31)) & 1)) continue;
          const auto vf = register_file_->GetVertexFetch(vi);
          if (vf.type != rex::graphics::xenos::FetchConstantType::kVertex) continue;
          const uint32_t baddr = vf.address << 2, stride = vf.size << 2;
          if (baddr >= kMemLimit || stride == 0) continue;
          const uint8_t* g = memory_->TranslatePhysical<const uint8_t*>(baddr);
          if (!g) continue;
          const uint32_t span = std::min<uint32_t>(max_idx + 1, 65536);
          uint32_t non_zero = 0;
          uint32_t nz_list[16];
          uint32_t nz_count = 0;
          for (uint32_t v = 0; v < span; ++v) {
            const uint32_t* vd = reinterpret_cast<const uint32_t*>(g + uint64_t(v) * stride);
            bool nz = false;
            for (int k = 0; k < 4 && k < stride / 4; ++k) {
              if (vd[k] != 0) { nz = true; break; }
            }
            if (nz) { ++non_zero; if (nz_count < 16) nz_list[nz_count++] = v; }
          }
          // The index buffer contents (what the draw actually references).
          const uint8_t* ib2 = memory_->TranslatePhysical<const uint8_t*>(index_buffer_info->guest_base);
          uint32_t idx_list[8];
          uint32_t idx_n = 0;
          if (ib2) {
            for (uint32_t i = 0; i < index_count && idx_n < 8; ++i) {
              uint32_t ix = (idx_size == 2) ? *reinterpret_cast<const uint16_t*>(ib2 + i * 2)
                                            : *reinterpret_cast<const uint32_t*>(ib2 + i * 4);
              idx_list[idx_n++] = ix;
            }
          }
          std::string nz_str, idx_str;
          for (uint32_t i = 0; i < nz_count; ++i) nz_str += std::to_string(nz_list[i]) + " ";
          for (uint32_t i = 0; i < idx_n; ++i) idx_str += std::to_string(idx_list[i]) + " ";
          REXGPU_INFO("[custom-renderer] DIAGS   vfetch[{}] addr={:#010x} stride={}B span={} non_zero={} ({:.1f}%) idx=[{}] nz_idx=[{}]",
                      vi, baddr, stride, span, non_zero, 100.0f * non_zero / float(span), idx_str, nz_str);
          break;
        }
      }
    }
  }

  // DIAG: for the first real 3D triangle draw (full camera matrix), dump the
  // vertex data, MVP matrix, and NDC transform so the transform can be checked
  // by hand.
  {
    static bool vtx_diag_logged = false;
    const uint32_t pt2 = uint32_t(prim_type);
    const bool is_tri = (pt2 == 4 || pt2 == 6);
    if (!vtx_diag_logged && is_tri && index_count >= 3) {
      const uint32_t* rf = register_file_->values;
      const uint32_t vs_base = rex::graphics::XE_GPU_REG_SHADER_CONSTANT_000_X;
      int vs_nz = 0;
      for (int i = 0; i < 1024; ++i) {
        if (rf[vs_base + i]) ++vs_nz;
      }
      if (vs_nz >= 16) {
        for (uint32_t i = 0; i < 96 && !vtx_diag_logged; ++i) {
          if (!((crm.vertex_fetch_bitmap[i >> 5] >> (i & 31)) & 1)) continue;
          const auto vf = register_file_->GetVertexFetch(i);
          if (vf.type != rex::graphics::xenos::FetchConstantType::kVertex) continue;
          const uint32_t byte_addr = vf.address << 2;
          const uint32_t stride = vf.size << 2;
          if (byte_addr >= kMemLimit || stride == 0) continue;
          const uint8_t* g = memory_->TranslatePhysical<const uint8_t*>(byte_addr);
          if (!g) continue;
          const uint32_t* g32 = reinterpret_cast<const uint32_t*>(g);
          vtx_diag_logged = true;
          REXGPU_INFO(
              "[custom-renderer] DIAGX vfetch[{}] addr={:#010x} stride={}B endian={} type={} "
              "idx={} vs_nz={}",
              i, byte_addr, stride, uint32_t(vf.endian), uint32_t(vf.type), index_count, vs_nz);
          // First 3 vertices, 6 dwords each (raw dwords).
          for (int v = 0; v < 3 && (v + 1) * 6 * 4 <= (int)stride * 3; ++v) {
            const uint32_t* vd = g32 + v * (stride / 4);
            REXGPU_INFO("[custom-renderer] DIAGX  vtx[{}] raw: {:08X} {:08X} {:08X} {:08X} {:08X} {:08X}",
                        v, vd[0], vd[1], vd[2], vd[3], vd[4], vd[5]);
            float f[6];
            for (int k = 0; k < 6; ++k) std::memcpy(&f[k], &vd[k], 4);
            REXGPU_INFO("[custom-renderer] DIAGX  vtx[{}] flt: {:.3f} {:.3f} {:.3f} {:.3f} {:.3f} {:.3f}",
                        v, f[0], f[1], f[2], f[3], f[4], f[5]);
          }
          // MVP: first 4 float4s of the VS float constants (packed, so these are
          // the first-used constants).
          for (int r = 0; r < 4; ++r) {
            float f[4];
            for (int k = 0; k < 4; ++k) std::memcpy(&f[k], &rf[vs_base + r * 4 + k], 4);
            REXGPU_INFO("[custom-renderer] DIAGX  VS#{}: {:.4f} {:.4f} {:.4f} {:.4f}", r,
                        f[0], f[1], f[2], f[3]);
          }
          // NDC transform from the live viewport info (same call the draw uses).
          rex::graphics::draw_util::ViewportInfo vi{};
          rex::graphics::draw_util::GetHostViewportInfo(
              *register_file_, 1, 1, true, main_rt_w_, main_rt_h_, true,
              register_file_->Get<rex::graphics::reg::RB_DEPTHCONTROL>(), false, true, false, vi);
          REXGPU_INFO("[custom-renderer] DIAGX  ndc_scale=({:.4f},{:.4f},{:.4f}) ndc_offset=({:.4f},{:.4f},{:.4f}) "
                      "xy_off=({},{}) xy_ext=({},{}) z=[{:.3f},{:.3f}]",
                      vi.ndc_scale[0], vi.ndc_scale[1], vi.ndc_scale[2], vi.ndc_offset[0],
                      vi.ndc_offset[1], vi.ndc_offset[2], vi.xy_offset[0], vi.xy_offset[1],
                      vi.xy_extent[0], vi.xy_extent[1], vi.z_min, vi.z_max);
        }
      }
    }
  }

  // Upload the index data and set the index buffer view.
  uint32_t draw_count = index_count;
  DXGI_FORMAT index_format = DXGI_FORMAT_R16_UINT;
  D3D12_INDEX_BUFFER_VIEW ibv = {};
  bool has_index = false;
  if (index_buffer_info) {
    const uint32_t index_size =
        (index_buffer_info->format == rex::graphics::xenos::IndexFormat::kInt32) ? 4 : 2;
    index_format = (index_size == 4) ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
    const uint32_t byte_size = index_count * index_size;
    const uint32_t byte_addr = index_buffer_info->guest_base;
    if (byte_addr < kMemLimit && byte_size > 0 && byte_addr + byte_size <= kMemLimit &&
        upload_cursor_ + byte_size <= upload_buffer_size_ && byte_size <= index_buffer_size_) {
      const uint8_t* guest = memory_->TranslatePhysical<const uint8_t*>(byte_addr);
      if (guest) {
        transition(index_buffer_.Get(), ib_state_, kCopyDest);
        ib_state_ = kCopyDest;
        std::memcpy(upload_buffer_map_ + upload_cursor_, guest, byte_size);
        cl->CopyBufferRegion(index_buffer_.Get(), 0, upload_buffer_.Get(), upload_cursor_,
                             byte_size);
        upload_cursor_ = (upload_cursor_ + byte_size + 255) & ~255u;
        transition(index_buffer_.Get(), ib_state_, kIndexBuf);
        ib_state_ = kIndexBuf;
        ibv.BufferLocation = index_buffer_gpu_;
        ibv.SizeInBytes = byte_size;
        ibv.Format = index_format;
        cl->IASetIndexBuffer(&ibv);
        has_index = true;
      }
    }
  }

  // Fill the constant buffers (CPU-written into the upload buffer at fixed
  // offsets), then bind them as root CBVs.
  rex::graphics::DxbcShaderTranslator::SystemConstants sc{};
  FillSystemConstants(sc, *register_file_, prim_type);
  std::memcpy(upload_buffer_map_ + kCbSystemOffset, &sc, sizeof(sc));
  FillFloatConstants(vs_shader, reinterpret_cast<float*>(upload_buffer_map_ + kCbFloatVertexOffset),
                     rex::graphics::XE_GPU_REG_SHADER_CONSTANT_000_X, *register_file_);
  // DIAGX2: dump the PACKED VS float cbuffer (the real MVP the shader reads) for
  // the first 3D triangle draw, so the transform can be verified by hand.
  {
    static bool packed_vs_dumped = false;
    const uint32_t pt2 = uint32_t(prim_type);
    const uint64_t* vsm = vs_shader->constant_register_map().float_bitmap;
    int vsc = 0;
    for (int b = 0; b < 4; ++b) {
      vsc += __builtin_popcountll(vsm[b]);
    }
    // Only fire on a real 3D draw (a full 4x4 matrix in the packed cbuffer).
    if (!packed_vs_dumped && (pt2 == 4 || pt2 == 6) && index_count >= 3 && vsc >= 4) {
      const float* pf = reinterpret_cast<const float*>(upload_buffer_map_ + kCbFloatVertexOffset);
      packed_vs_dumped = true;
      rex::graphics::draw_util::ViewportInfo vi{};
      rex::graphics::draw_util::GetHostViewportInfo(
          *register_file_, 1, 1, true, main_rt_w_, main_rt_h_, true,
          register_file_->Get<rex::graphics::reg::RB_DEPTHCONTROL>(), false, true, false, vi);
      REXGPU_INFO("[custom-renderer] DIAGX2 packed VS float_count={} idx={} viewport off=({},{}) ext=({},{}) z=[{:.3f},{:.3f}] rt={}x{}",
                  vsc, index_count, vi.xy_offset[0], vi.xy_offset[1], vi.xy_extent[0], vi.xy_extent[1],
                  vi.z_min, vi.z_max, main_rt_w_, main_rt_h_);
      REXGPU_INFO("[custom-renderer] DIAGX2 ndc_scale=({:.4f},{:.4f},{:.4f}) ndc_offset=({:.4f},{:.4f},{:.4f})",
                  vi.ndc_scale[0], vi.ndc_scale[1], vi.ndc_scale[2], vi.ndc_offset[0], vi.ndc_offset[1], vi.ndc_offset[2]);
      {
        const auto vte = register_file_->Get<rex::graphics::reg::PA_CL_VTE_CNTL>();
        REXGPU_INFO("[custom-renderer] DIAGX2 PA_CL_VTE_CNTL xy_fmt={} z_fmt={} w0_fmt={}",
                    vte.vtx_xy_fmt, vte.vtx_z_fmt, vte.vtx_w0_fmt);
      }
      for (int r = 0; r < 16 && r < vsc; ++r) {
        REXGPU_INFO("[custom-renderer] DIAGX2  VSP#{}: {:.5f} {:.5f} {:.5f} {:.5f}", r, pf[r * 4 + 0], pf[r * 4 + 1], pf[r * 4 + 2], pf[r * 4 + 3]);
      }
      // Dump the actual vertex indices (from the index buffer) so we can see
      // which vertices the draw really references.
      if (index_buffer_info) {
        const uint32_t idx_size = (index_buffer_info->format == rex::graphics::xenos::IndexFormat::kInt32) ? 4 : 2;
        const uint32_t ib_addr = index_buffer_info->guest_base;
        if (ib_addr < kMemLimit) {
          const uint8_t* ib = memory_->TranslatePhysical<const uint8_t*>(ib_addr);
          if (ib) {
            uint32_t max_idx = 0;
            for (uint32_t i = 0; i < index_count; ++i) {
              uint32_t ix = (idx_size == 2) ? *reinterpret_cast<const uint16_t*>(ib + i * 2)
                                            : *reinterpret_cast<const uint32_t*>(ib + i * 4);
              if (ix > max_idx) max_idx = ix;
              if (i < 8) REXGPU_INFO("[custom-renderer] DIAGX2  idx[{}]={}", i, ix);
            }
            REXGPU_INFO("[custom-renderer] DIAGX2  max_idx={} (index_count={})", max_idx, index_count);
            // For the first position vfetch, read the ACTUAL referenced vertices
            // and compute their NDC (MVP row-major) to see if they clip.
            const auto& vfb = vs_shader->constant_register_map().vertex_fetch_bitmap;
            for (uint32_t vi = 0; vi < 96; ++vi) {
              if (!((vfb[vi >> 5] >> (vi & 31)) & 1)) continue;
              const auto vf = register_file_->GetVertexFetch(vi);
              if (vf.type != rex::graphics::xenos::FetchConstantType::kVertex) continue;
              const uint32_t baddr = vf.address << 2, stride = vf.size << 2;
              if (baddr >= kMemLimit || stride == 0) continue;
              const uint8_t* g = memory_->TranslatePhysical<const uint8_t*>(baddr);
              if (!g) continue;
              REXGPU_INFO("[custom-renderer] DIAGX2  NDC check vfetch[{}] stride={}B endian={}", vi, stride, uint32_t(vf.endian));
              {
                const auto vds = register_file_->Get<rex::graphics::reg::VGT_DMA_SIZE>();
                const auto vdi = register_file_->Get<rex::graphics::reg::VGT_DRAW_INITIATOR>();
                rex::graphics::xenos::Endian iend = vds.swap_mode;
                if (vdi.index_size == rex::graphics::xenos::IndexFormat::kInt16 &&
                    iend != rex::graphics::xenos::Endian::kNone &&
                    iend != rex::graphics::xenos::Endian::k8in16) {
                  iend = (iend == rex::graphics::xenos::Endian::k8in32)
                             ? rex::graphics::xenos::Endian::k8in16
                             : rex::graphics::xenos::Endian::kNone;
                }
                const uint32_t ioff = register_file_->Get<rex::graphics::reg::VGT_INDX_OFFSET>().indx_offset;
                REXGPU_INFO("[custom-renderer] DIAGX2  idx endian={} offset={} raw_swap_mode={} index_size={}",
                            uint32_t(iend), ioff, uint32_t(vds.swap_mode), uint32_t(vdi.index_size));
                for (uint32_t ii = 0; ii < index_count; ++ii) {
                  const uint32_t raw = (idx_size == 2) ? *reinterpret_cast<const uint16_t*>(ib + ii * 2)
                                                       : *reinterpret_cast<const uint32_t*>(ib + ii * 4);
                  const uint32_t ix = (idx_size == 2)
                      ? rex::graphics::xenos::GpuSwap(uint16_t(raw), iend) + ioff
                      : rex::graphics::xenos::GpuSwap(raw, iend) + ioff;
                  const uint32_t* vd = reinterpret_cast<const uint32_t*>(g + uint64_t(ix) * stride);
                  float p[4] = {0, 0, 0, 0};
                  for (int k = 0; k < 4 && k < stride / 4; ++k) {
                    uint32_t dw = vd[k];
                    if (vf.endian == rex::graphics::xenos::Endian::k8in32) dw = __builtin_bswap32(dw);
                    std::memcpy(&p[k], &dw, 4);
                  }
                  float cx = pf[0]*p[0]+pf[1]*p[1]+pf[2]*p[2]+pf[3]*p[3];
                  float cy = pf[4]*p[0]+pf[5]*p[1]+pf[6]*p[2]+pf[7]*p[3];
                  float cz = pf[8]*p[0]+pf[9]*p[1]+pf[10]*p[2]+pf[11]*p[3];
                  float cw = pf[12]*p[0]+pf[13]*p[1]+pf[14]*p[2]+pf[15]*p[3];
                  REXGPU_INFO("[custom-renderer] DIAGX2    idx{} raw={} real={} pos=({:.3f},{:.3f},{:.3f},{:.3f}) clip=({:.4f},{:.4f},{:.4f},w={:.4f})",
                              ii, raw, ix, p[0], p[1], p[2], p[3], cx, cy, cz, cw);
                }
              }
              break;
            }
          }
        }
      }
      // Dump every vertex fetch (to identify the position vfetch) and the first
      // 3 vertices of the first one.
      for (uint32_t vi = 0; vi < 96; ++vi) {
        if (!((vs_shader->constant_register_map().vertex_fetch_bitmap[vi >> 5] >> (vi & 31)) & 1)) continue;
        const auto vf = register_file_->GetVertexFetch(vi);
        const uint32_t baddr = vf.address << 2, stride = vf.size << 2;
        REXGPU_INFO("[custom-renderer] DIAGX2  vfetch[{}] addr={:#010x} stride={}B endian={} type={}", vi, baddr, stride, uint32_t(vf.endian), uint32_t(vf.type));
        if (vf.type != rex::graphics::xenos::FetchConstantType::kVertex) continue;
        if (baddr >= kMemLimit || stride == 0) continue;
        const uint8_t* g = memory_->TranslatePhysical<const uint8_t*>(baddr);
        if (!g) continue;
        // Read back the GPU shared memory at this vertex address (to confirm the
        // vertex upload reached the GPU). Copy to the CPU-mapped readback buffer;
        // read it back a few frames later in EndFramePresent.
        if (!diag_rb_pending_ && readback_buf_) {
          uint32_t rb_size = stride * 4;  // first vertex
          if (rb_size > 16384) rb_size = 16384;
          rb_size &= ~3u;
          D3D12_RESOURCE_BARRIER rb{};
          rb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
          rb.Transition.pResource = readback_buf_.Get();
          rb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
          rb.Transition.StateBefore = static_cast<D3D12_RESOURCE_STATES>(rb_buf_state_);
          rb.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
          cl->ResourceBarrier(1, &rb);
          rb_buf_state_ = uint32_t(D3D12_RESOURCE_STATE_COPY_DEST);
          diag_rb_addr_ = baddr;
          diag_rb_size_ = rb_size;
          cl->CopyBufferRegion(readback_buf_.Get(), 0, shared_memory_.Get(), baddr, rb_size);
          diag_rb_pending_ = true;
          REXGPU_INFO("[custom-renderer] DIAGX2  shared-mem readback queued addr={:#010x} size={}", baddr, rb_size);
        }
        for (int v = 0; v < 3; ++v) {
          const uint32_t* vd = reinterpret_cast<const uint32_t*>(g + v * stride);
          float p[4] = {0, 0, 0, 0};
          for (int k = 0; k < 4 && k < stride / 4; ++k) {
            uint32_t dw = vd[k];
            if (vf.endian == rex::graphics::xenos::Endian::k8in32) dw = __builtin_bswap32(dw);
            std::memcpy(&p[k], &dw, 4);
          }
          REXGPU_INFO("[custom-renderer] DIAGX2    vfetch[{}] vtx[{}] = ({:.3f},{:.3f},{:.3f},{:.3f})", vi, v, p[0], p[1], p[2], p[3]);
        }
      }
    }
  }
  // DIAGXR: track the clip x/w and y/w range across many 3D draws (row-major MVP
  // from VS float constants 192-211 applied to vertex 0) to determine if the
  // geometry is systematically just outside [-1,1].
  {
    const uint32_t pt = uint32_t(prim_type);
    if (pt == 4 || pt == 6) {
      static float mn_xw = 1e9f, mx_xw = -1e9f, mn_yw = 1e9f, mx_yw = -1e9f, mn_w = 1e9f, mx_w = -1e9f;
      static uint32_t n = 0, neg_w = 0;
      const uint32_t* rfv = register_file_->values;
      const uint32_t vbase = (uint32_t)rex::graphics::XE_GPU_REG_SHADER_CONSTANT_000_X;
      float pf[16];
      for (int i = 0; i < 16; ++i) { uint32_t dw = rfv[vbase + (192 + i / 4) * 4 + (i % 4)]; std::memcpy(&pf[i], &dw, 4); }
      for (uint32_t vi = 0; vi < 96; ++vi) {
        if (!((vs_shader->constant_register_map().vertex_fetch_bitmap[vi >> 5] >> (vi & 31)) & 1)) continue;
        const auto vf = register_file_->GetVertexFetch(vi);
        if (vf.type != rex::graphics::xenos::FetchConstantType::kVertex) continue;
        const uint32_t baddr = vf.address << 2, stride = vf.size << 2;
        if (baddr >= kMemLimit || stride < 16) continue;
        const uint8_t* g = memory_->TranslatePhysical<const uint8_t*>(baddr); if (!g) continue;
        const uint32_t* vd = reinterpret_cast<const uint32_t*>(g);
        float p[4];
        for (int k = 0; k < 4; ++k) { uint32_t dw = vd[k]; if (vf.endian == rex::graphics::xenos::Endian::k8in32) dw = __builtin_bswap32(dw); std::memcpy(&p[k], &dw, 4); }
        float cx = pf[0]*p[0]+pf[1]*p[1]+pf[2]*p[2]+pf[3]*p[3];
        float cy = pf[4]*p[0]+pf[5]*p[1]+pf[6]*p[2]+pf[7]*p[3];
        float cw = pf[12]*p[0]+pf[13]*p[1]+pf[14]*p[2]+pf[15]*p[3];
        mn_w = fminf(mn_w, cw); mx_w = fmaxf(mx_w, cw);
        if (cw <= 0.f) { ++neg_w; }
        else {
          float xw = cx / cw, yw = cy / cw;
          mn_xw = fminf(mn_xw, xw); mx_xw = fmaxf(mx_xw, xw);
          mn_yw = fminf(mn_yw, yw); mx_yw = fmaxf(mx_yw, yw);
          ++n;
        }
        break;
      }
      if (n % 5000 == 0 && n > 0) REXGPU_INFO("[custom-renderer] DIAGXR {} samples: x/w=[{:.4f},{:.4f}] y/w=[{:.4f},{:.4f}] w=[{:.2f},{:.2f}] neg_w={} MVP={:.3f},{:.3f},{:.3f},{:.3f} | {:.3f},{:.3f},{:.3f},{:.3f} | {:.3f},{:.3f},{:.3f},{:.3f} | {:.3f},{:.3f},{:.3f},{:.3f}", n, mn_xw, mx_xw, mn_yw, mx_yw, mn_w, mx_w, neg_w, pf[0],pf[1],pf[2],pf[3], pf[4],pf[5],pf[6],pf[7], pf[8],pf[9],pf[10],pf[11], pf[12],pf[13],pf[14],pf[15]);
    }
  }
  // DIAGXR: sample the clip x/w and y/w (row-major MVP) across many 3D draws to
  // determine whether the transform is systematically off-screen (all x/w just
  // outside [-1,1]) or only specific draws land off-screen.
  {
    static uint32_t xr_samples = 0;
    static float xr_xmin = 1e9f, xr_xmax = -1e9f;
    static float xr_ymin = 1e9f, xr_ymax = -1e9f;
    static float xr_wmin = 1e9f, xr_wmax = -1e9f;
    static uint32_t xr_neg_w = 0;
    const uint32_t pt2 = uint32_t(prim_type);
    if (xr_samples < 40000 && (pt2 == 4 || pt2 == 6) && index_count >= 3) {
      const auto& vfb = vs_shader->constant_register_map().vertex_fetch_bitmap;
      const float* pf = reinterpret_cast<const float*>(upload_buffer_map_ + kCbFloatVertexOffset);
      for (uint32_t vi = 0; vi < 96; ++vi) {
        if (!((vfb[vi >> 5] >> (vi & 31)) & 1)) continue;
        const auto vf = register_file_->GetVertexFetch(vi);
        if (vf.type != rex::graphics::xenos::FetchConstantType::kVertex) continue;
        const uint32_t baddr = vf.address << 2, stride = vf.size << 2;
        if (baddr >= kMemLimit || stride == 0) continue;
        const uint8_t* g = memory_->TranslatePhysical<const uint8_t*>(baddr);
        if (!g) continue;
        for (uint32_t vv = 0; vv < 2; ++vv) {
          const uint32_t* vd = reinterpret_cast<const uint32_t*>(g + uint64_t(vv) * stride);
          float p[4] = {0, 0, 0, 1};
          for (int k = 0; k < 3 && k < stride / 4; ++k) {
            uint32_t dw = vd[k];
            if (vf.endian == rex::graphics::xenos::Endian::k8in32) dw = __builtin_bswap32(dw);
            std::memcpy(&p[k], &dw, 4);
          }
          float cx = pf[0] * p[0] + pf[1] * p[1] + pf[2] * p[2] + pf[3] * p[3];
          float cy = pf[4] * p[0] + pf[5] * p[1] + pf[6] * p[2] + pf[7] * p[3];
          float cw = pf[12] * p[0] + pf[13] * p[1] + pf[14] * p[2] + pf[15] * p[3];
          if (cw < 0) { ++xr_neg_w; continue; }
          float nx = cx / cw, ny = cy / cw;
          if (nx > -1e8f) { xr_xmin = std::min(xr_xmin, nx); xr_xmax = std::max(xr_xmax, nx); }
          if (ny > -1e8f) { xr_ymin = std::min(xr_ymin, ny); xr_ymax = std::max(xr_ymax, ny); }
          xr_wmin = std::min(xr_wmin, cw); xr_wmax = std::max(xr_wmax, cw);
          ++xr_samples;
        }
        break;
      }
      if (xr_samples > 0 && xr_samples % 2000 == 0) {
        REXGPU_INFO("[custom-renderer] DIAGXR n={} neg_w={} x/w=[{:.4f},{:.4f}] y/w=[{:.4f},{:.4f}] w=[{:.2f},{:.2f}]",
                    xr_samples, xr_neg_w, xr_xmin, xr_xmax, xr_ymin, xr_ymax, xr_wmin, xr_wmax);
      }
    }
  }
  FillFloatConstants(ps_shader, reinterpret_cast<float*>(upload_buffer_map_ + kCbFloatPixelOffset),
                     rex::graphics::XE_GPU_REG_SHADER_CONSTANT_256_X, *register_file_);
  std::memcpy(upload_buffer_map_ + kCbBoolLoopOffset,
              &register_file_->values[rex::graphics::XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031],
              40 * 4);
  std::memcpy(upload_buffer_map_ + kCbFetchOffset,
              &register_file_->values[rex::graphics::XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0],
              32 * 6 * 4);
  cl->SetGraphicsRootConstantBufferView(kRootCbSystem, upload_buffer_gpu_ + kCbSystemOffset);
  cl->SetGraphicsRootConstantBufferView(kRootCbFloatVertex,
                                        upload_buffer_gpu_ + kCbFloatVertexOffset);
  cl->SetGraphicsRootConstantBufferView(kRootCbFloatPixel,
                                        upload_buffer_gpu_ + kCbFloatPixelOffset);
  cl->SetGraphicsRootConstantBufferView(kRootCbBoolLoop,
                                        upload_buffer_gpu_ + kCbBoolLoopOffset);
  cl->SetGraphicsRootConstantBufferView(kRootCbFetch, upload_buffer_gpu_ + kCbFetchOffset);
  cl->SetGraphicsRootConstantBufferView(kRootCbDescriptorIndices,
                                        upload_buffer_gpu_ + kCbDescriptorIndicesOffset);

  // Bind the textures + samplers. t0 = shared memory (set once); t1..t32 = the
  // PS's texture bindings. Each texture SRV is published straight into the draw
  // heap (not CopyDescriptorsSimple, which the compat driver rejects for this
  // layout); unbound slots get the 1x1 null SRV.
  const auto& tex_bindings = ps_shader->GetTextureBindingsAfterTranslation();
  const auto& samp_bindings = ps_shader->GetSamplerBindingsAfterTranslation();
  if (draw_srv_heap_start_.ptr != 0 && draw_srv_desc_size_ > 0 && texture_cache_) {
    for (uint32_t t = 1; t <= kMaxDrawTextures; ++t) {
      D3D12_CPU_DESCRIPTOR_HANDLE srv_slot = draw_srv_heap_start_;
      srv_slot.ptr += uint64_t(t) * draw_srv_desc_size_;
      bool bound = false;
      if (t - 1 < tex_bindings.size()) {
        const auto fetch = register_file_->GetTextureFetch(tex_bindings[t - 1].fetch_constant);
        auto* tex = texture_cache_->RequestTexture(fetch);
        if (tex && tex->resident) {
          texture_cache_->PublishSRV(tex, srv_slot);
          bound = true;
        }
      }
      if (!bound) {
        texture_cache_->PublishNullSRV(srv_slot);
      }
    }
  }
  // Samplers: s0.. = the PS's sampler bindings (linear/linear/linear default).
  auto* device = static_cast<rex::ui::d3d12::D3D12Provider*>(graphics_system_->provider())
                     ->GetDevice();
  D3D12_CPU_DESCRIPTOR_HANDLE samp_slot = draw_sampler_heap_start_;
  for (uint32_t s = 0; s <= kMaxDrawTextures; ++s) {
    D3D12_SAMPLER_DESC sd{};
    sd.MinLOD = 0.0f;
    sd.MaxLOD = 10000.0f;
    for (int c = 0; c < 4; ++c) {
      sd.BorderColor[c] = 0.0f;  // transparent black
    }
    if (s < samp_bindings.size()) {
      sd.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
      sd.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
      sd.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
      sd.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
      sd.MaxAnisotropy = 1;
      sd.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    } else {
      sd.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
      sd.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
      sd.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
      sd.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    }
    device->CreateSampler(&sd, samp_slot);
    samp_slot.ptr += draw_sampler_desc_size_;
  }

  // Set the descriptor tables (SRV, UAV, sampler); the heaps are already bound
  // in BeginFrameDrawing. The index buffer is set above. Then draw.
  cl->SetGraphicsRootDescriptorTable(kRootTableSrv, draw_srv_heap_gpu_);
  cl->SetGraphicsRootDescriptorTable(kRootTableUav, draw_uav_heap_gpu_);
  cl->SetGraphicsRootDescriptorTable(kRootTableSampler, draw_sampler_heap_gpu_);

  // One-time diagnostic at the first non-degenerate draw: log the viewport/NDC
  // and a sample of the constants so we can tell why the frame is (or isn't) visible.
  {
    static uint32_t s_total = 0, s_tri = 0, s_rect = 0, s_tri_matrix = 0;
    static uint32_t s_max_vsnz = 0, s_total_idx = 0;
    const uint32_t* rf = register_file_->values;
    const uint32_t vs_base = rex::graphics::XE_GPU_REG_SHADER_CONSTANT_000_X;
    int rf_vs_nz = 0;
    for (int i = 0; i < 1024; ++i) {
      if (rf[vs_base + i]) ++rf_vs_nz;
    }
    const uint32_t pt = uint32_t(prim_type);
    ++s_total;
    s_total_idx += index_count;
    if (pt == 4 || pt == 6) ++s_tri;
    if (pt == 8) ++s_rect;
    if ((pt == 4 || pt == 6) && rf_vs_nz >= 16) ++s_tri_matrix;
    if (uint32_t(rf_vs_nz) > s_max_vsnz) s_max_vsnz = uint32_t(rf_vs_nz);
    if (s_total % 20000 == 0) {
      REXGPU_INFO("[custom-renderer] DIAG draws={} total_idx={} tri={} rect={} tri_full_matrix={} "
                  "max_vs_nz={} cur_prim={} cur_idx={}",
                  s_total, s_total_idx, s_tri, s_rect, s_tri_matrix, s_max_vsnz, pt, index_count);
    }
  }

  if (has_index) {
    cl->DrawIndexedInstanced(draw_count, 1, 0, 0, 0);
  } else {
    cl->DrawInstanced(draw_count, 1, 0, 0);
  }
  ++frame_draw_count_;
  if (!first_frame_draw_logged_) {
    first_frame_draw_logged_ = true;
    REXGPU_INFO("[custom-renderer] first GPU draw recorded: prim={} indices={} vs={}B ps={}B "
                "tex_bindings={} samp_bindings={}",
                uint32_t(prim_type), index_count, (uint32_t)vs_it->second.dxil.size(),
                (uint32_t)ps_it->second.dxil.size(), (uint32_t)tex_bindings.size(),
                (uint32_t)samp_bindings.size());
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

void CustomCommandProcessor::RequestActiveTextures() {
  if (!texture_cache_ || !register_file_) {
    return;
  }
  // Exercise the cache with all 32 texture units. Unused units (zero size or
  // unsupported format) return early in RequestTexture. New textures are
  // untiled + uploaded on first sight; unchanged ones are cheap cache hits.
  for (uint32_t i = 0; i < 32; ++i) {
    texture_cache_->RequestTexture(register_file_->GetTextureFetch(i));
  }
  if ((texture_request_count_++ % 30) == 0) {
    REXGPU_INFO("[custom-renderer] textures: {} resident, {} hits, {} misses, {} uploads, {} "
                "unsupported, {} MB",
                texture_cache_->resident(), texture_cache_->hits(), texture_cache_->misses(),
                texture_cache_->uploads(), texture_cache_->unsupported(),
                (texture_cache_->bytes() + (1 << 19)) >> 20);
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
