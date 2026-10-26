// render_target_cache.cpp - see render_target_cache.h.

#include "render_target_cache.h"
#include "ccr_trace.h"

#include <algorithm>

#include <rex/logging.h>
#include <rex/ui/d3d12/d3d12_util.h>

namespace customrenderer {
namespace d3d12 {

RenderTargetCache::RenderTargetCache(ID3D12Device* device, ID3D12CommandQueue* queue)
    : device_(device), queue_(queue) {
  if (!device_) {
    return;
  }
  // Separate RTV and DSV heaps (distinct heap types in D3D12).
  D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  heap_desc.NumDescriptors = kDescriptorCount;
  heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  if (SUCCEEDED(device_->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&rtv_heap_)))) {
    rtv_heap_start_ = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    rtv_desc_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  }
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
  if (SUCCEEDED(device_->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&dsv_heap_)))) {
    dsv_heap_start_ = dsv_heap_->GetCPUDescriptorHandleForHeapStart();
    dsv_desc_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
  }
}

RenderTargetCache::~RenderTargetCache() = default;

void RenderTargetCache::EvictIfNeeded() {
  if (entries_.size() <= kMaxRTs) {
    return;
  }
  // Evict the least-recently-used entries down to the capacity.
  while (entries_.size() > kMaxRTs) {
    uint32_t victim = 0;
    uint64_t victim_lru = UINT64_MAX;
    for (auto& kv : entries_) {
      if (kv.second.last_used < victim_lru) {
        victim_lru = kv.second.last_used;
        victim = kv.first;
      }
    }
    if (victim == 0) {
      return;
    }
    auto it = entries_.find(victim);
    if (it != entries_.end()) {
      bytes_ -= 0;  // we do not track per-RT bytes precisely on eviction
      it->second.color.Reset();
      it->second.depth.Reset();
      it->second.valid = false;
      entries_.erase(it);
      ++evictions_;
    } else {
      return;
    }
  }
}

RenderTargetCache::Entry* RenderTargetCache::GetOrCreate(
    uint32_t address, uint32_t width, uint32_t height, DXGI_FORMAT color_format,
    bool has_depth) {
  static uint32_t s_fail = 0;
  auto faillog = [&](const char* why) {
    if (s_fail < 5) {
      REXGPU_INFO("[custom-renderer] RT GetOrCreate FAIL: {} ({:#010x} {}x{} fmt={})",
                  why, address, width, height, uint32_t(color_format));
      ++s_fail;
    }
  };
  if (!device_ || !rtv_heap_ || !dsv_heap_) {
    faillog("no device/heap");
    return nullptr;
  }
  if (!width || !height || width > 16384 || height > 16384) {
    faillog("bad dims");
    return nullptr;
  }
  if (color_format == DXGI_FORMAT_UNKNOWN) {
    faillog("bad format");
    return nullptr;
  }

  auto it = entries_.find(address);
  if (it != entries_.end() && it->second.valid) {
    it->second.last_used = ++lru_counter_;
    it->second.just_created = false;
    return &it->second;
  }

  // Create the color render target (default heap, COPY_DEST initial state).
  // (RTV/DSV descriptor slots land in M4 once the driver's RTV device-removal
  // is worked around; for now the clear uses a CPU staging-buffer upload.)
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = width;
  desc.Height = height;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = color_format;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  // Required to create a RenderTargetView of this resource (validation ERROR #42).
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  D3D12_HEAP_PROPERTIES heap_props{};
  heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;

  Entry entry;
  entry.width = width;
  entry.height = height;
  entry.color_format = color_format;
  entry.has_depth = has_depth;
  // COPY_DEST initial state (matches the working M2 texture creation); the
  // present path copies the clear into it, then copies it to the guest output.
  {
    static uint64_t s_rt = 0;
    REXGPU_INFO("[custom-renderer] RT CCR color #{} {}x{} fmt=0x{:04X} has_depth={}", ++s_rt,
                width, height, uint32_t(color_format), has_depth ? 1 : 0);
  }
  CcrTrace("RTCOL", width, height, 1, uint32_t(color_format), uint64_t(width) * height * 4);
  HRESULT hr = device_->CreateCommittedResource(
      &heap_props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
      IID_PPV_ARGS(&entry.color));
  if (FAILED(hr)) {
    static uint32_t s_hr = 0;
    if (s_hr < 3) {
      REXGPU_INFO("[custom-renderer] RT CreateCommittedResource hr={:#x} ({:#010x} {}x{} "
                  "fmt={} state=RT)",
                  (unsigned)hr, address, width, height, uint32_t(color_format));
      ++s_hr;
    }
    return nullptr;
  }

  // Optional depth target (D32_FLOAT_S8X24_UINT). DEPTH_WRITE is not a valid
  // CreateCommittedResource initial state (only COMMON/COPY_DEST/GENERIC_READ/
  // PRESENT/RENDER_TARGET are), so create in COMMON and transition when used.
  if (has_depth) {
    D3D12_RESOURCE_DESC ddesc = desc;
    ddesc.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    // Required to create a DepthStencilView of this resource (validation ERROR #49).
    ddesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    {
      static uint64_t s_ds = 0;
      REXGPU_INFO("[custom-renderer] RT CCR depth #{} {}x{} fmt=D32S8X24", ++s_ds, width,
                  height);
    }
    CcrTrace("RTDEP", width, height, 1, 0x0000, uint64_t(width) * height * 8);
    if (FAILED(device_->CreateCommittedResource(
            &heap_props, D3D12_HEAP_FLAG_NONE, &ddesc, D3D12_RESOURCE_STATE_COMMON,
            nullptr, IID_PPV_ARGS(&entry.depth)))) {
      entry.color.Reset();
      return nullptr;
    }
  }

  // Allocate RTV + DSV descriptor slots (wrapping cursor; LRU evicts oldest
  // RTs before the cursor revisits their slots). Create the views - the RTV is
  // what M4 draws into; the DSV holds the depth/stencil target.
  if (rtv_heap_) {
    entry.rtv = rtv_heap_start_;
    entry.rtv.ptr += size_t(rtv_cursor_) * rtv_desc_size_;
    D3D12_RENDER_TARGET_VIEW_DESC rtv_desc{};
    rtv_desc.Format = color_format;
    rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtv_desc.Texture2D.MipSlice = 0;
    rtv_desc.Texture2D.PlaneSlice = 0;
    device_->CreateRenderTargetView(entry.color.Get(), &rtv_desc, entry.rtv);
    rtv_cursor_ = (rtv_cursor_ + 1) % kDescriptorCount;
  }
  if (has_depth && dsv_heap_) {
    entry.dsv = dsv_heap_start_;
    entry.dsv.ptr += size_t(dsv_cursor_) * dsv_desc_size_;
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc{};
    dsv_desc.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsv_desc.Texture2D.MipSlice = 0;
    device_->CreateDepthStencilView(entry.depth.Get(), &dsv_desc, entry.dsv);
    dsv_cursor_ = (dsv_cursor_ + 1) % kDescriptorCount;
  }

  // Ensure the CPU staging buffer is large enough for this RT's clear color.
  EnsureStaging(width, height);

  entry.valid = true;
  entry.just_created = true;
  entry.last_used = ++lru_counter_;
  ++create_count_;
  bytes_ += uint64_t(width) * height * 4;  // approx (color only)
  auto [res, inserted] = entries_.emplace(address, std::move(entry));
  if (!inserted) {
    // Should not happen (we checked above), but be safe.
    res->second.color.Reset();
    res->second.depth.Reset();
  }
  EvictIfNeeded();
  return &entries_[address];
}

void RenderTargetCache::EnsureStaging(uint32_t width, uint32_t height) {
  if (staging_ && staging_w_ >= width && staging_h_ >= height) {
    return;
  }
  // (Re)create the upload-heap staging buffer for the clear color. R10G10B10A2
  // is 4 bytes/texel, tightly packed (row pitch = width * 4).
  const uint32_t w = std::max<uint32_t>(width, staging_w_ ? staging_w_ : width);
  const uint32_t h = std::max<uint32_t>(height, staging_h_ ? staging_h_ : height);
  if (staging_map_) {
    staging_->Unmap(0, nullptr);
    staging_map_ = nullptr;
  }
  staging_.Reset();
  const uint64_t size = (uint64_t(w) * h * 4 + 15) & ~15ULL;  // 16-byte aligned
  D3D12_RESOURCE_DESC desc{};
  rex::ui::d3d12::util::FillBufferResourceDesc(desc, size, D3D12_RESOURCE_FLAG_NONE);
  CcrTrace("RTSTG", w, h, 0, 0, size);
  const HRESULT hr_create = device_->CreateCommittedResource(
      &rex::ui::d3d12::util::kHeapPropertiesUpload, D3D12_HEAP_FLAG_NONE, &desc,
      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&staging_));
  const HRESULT hr_map =
      SUCCEEDED(hr_create)
          ? staging_->Map(0, nullptr, reinterpret_cast<void**>(&staging_map_))
          : E_FAIL;
  static uint32_t s_ensure = 0;
  if (s_ensure < 3) {
    REXGPU_INFO(
        "[custom-renderer] RT EnsureStaging: {}x{} bytes={} create_hr=0x{:08X} map_hr=0x{:08X} "
        "(call {})",
        w, h, (unsigned long long)desc.Width, static_cast<uint32_t>(hr_create),
        static_cast<uint32_t>(hr_map), s_ensure);
    ++s_ensure;
  }
  if (FAILED(hr_create) || FAILED(hr_map)) {
    staging_.Reset();
    staging_map_ = nullptr;
    return;
  }
  staging_w_ = w;
  staging_h_ = h;
}

void RenderTargetCache::WriteClearColor(uint32_t width, uint32_t height, float r,
                                        float g, float b, float a) {
  if (!staging_map_) {
    return;
  }
  // R10G10B10A2 packing: [31:30] A, [29:20] B, [19:10] G, [9:0] R.
  const uint32_t r10 = uint32_t(std::min(1.0f, std::max(0.0f, r)) * 1023.0f + 0.5f);
  const uint32_t g10 = uint32_t(std::min(1.0f, std::max(0.0f, g)) * 1023.0f + 0.5f);
  const uint32_t b10 = uint32_t(std::min(1.0f, std::max(0.0f, b)) * 1023.0f + 0.5f);
  const uint32_t a2 = uint32_t(std::min(1.0f, std::max(0.0f, a)) * 3.0f + 0.5f);
  const uint32_t pixel = (a2 << 30) | (b10 << 20) | (g10 << 10) | r10;
  uint8_t* p = staging_map_;
  for (uint32_t y = 0; y < height; ++y) {
    for (uint32_t x = 0; x < width; ++x) {
      *reinterpret_cast<uint32_t*>(p) = pixel;
      p += 4;
    }
  }
}

}  // namespace d3d12
}  // namespace customrenderer
