// texture_cache.cpp - Custom D3D12 renderer: M2 texture cache (see .h).

#include "texture_cache.h"
#include "ccr_trace.h"

#include <algorithm>
#include <cstring>
#include <unordered_set>

#include <rex/graphics/pipeline/texture/info.h>
#include <rex/graphics/pipeline/texture/util.h>
#include <rex/logging.h>
#include <rex/memory.h>

namespace customrenderer {
namespace d3d12 {
using rex::graphics::xenos::TextureFormat;
using rex::graphics::xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA;
using rex::graphics::xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR;
namespace {

// D3D12_SHADER_COMPONENT_MAPPING_ALWAYS_SET_BIT = 1 << (3*4) = 0x1000. Each
// component is a 3-bit value (0-3 = component, 4 = force 0, 5 = force 1), which
// maps directly from the Xenos guest swizzle encoding (no +1).
constexpr uint32_t kAlwaysSetBit = 0x1000;
constexpr uint64_t kMaxStagingBytes = 64u << 20;
constexpr uint32_t kSrvHeapSize = 4096;

uint32_t Log2Floor(uint32_t v) {
  uint32_t r = 0;
  while (v > 1) {
    v >>= 1;
    ++r;
  }
  return r;
}

}  // namespace

TextureCache::TextureCache(ID3D12Device* device, ID3D12CommandQueue* queue,
                           rex::memory::Memory* memory)
    : device_(device), queue_(queue), memory_(memory) {
  // SRV heap (SHADER_VISIBLE so the SRVs can be bound to shaders in M4).
  D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heap_desc.NumDescriptors = kSrvHeapSize;
  heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (SUCCEEDED(device_->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&srv_heap_)))) {
    srv_start_ = srv_heap_->GetCPUDescriptorHandleForHeapStart();
    srv_desc_size_ =
        device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    // NOTE: CreateShaderResourceView(nullptr, ...) device-removes on this driver
    // (even with a valid mapping), so the "no texture" SRV is not created here;
    // a real 1x1 null view is added in M4 when sampling begins. null_srv_ stays
    // an unused heap slot for now.
    null_srv_ = srv_start_;
  }

  // 1x1 null texture backing the null SRV (GENERIC_READ so it is immediately
  // sampleable). CreateShaderResourceView(nullptr,...) device-removes here.
  {
    D3D12_RESOURCE_DESC td{};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = 1;
    td.Height = 1;
    td.DepthOrArraySize = 1;
    td.MipLevels = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_GENERIC_READ,
                                     nullptr, IID_PPV_ARGS(&null_tex_));
  }

  if (SUCCEEDED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                IID_PPV_ARGS(&allocator_)))) {
    device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator_.Get(), nullptr,
                               IID_PPV_ARGS(&command_list_));
    // A freshly created command list must be closed before its allocator can be
    // reset (the driver's COMMAND_ALLOCATOR_CANNOT_RESET / COMMAND_LIST_OPEN
    // checks require a closed list); matches the built-in's create-then-close.
    if (command_list_) {
      command_list_->Close();
    }
  }
  if (SUCCEEDED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)))) {
    fence_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  }
}

TextureCache::~TextureCache() {
  WaitForIdle();
  for (auto& tex : textures_) {
    if (tex.resource) {
      tex.resource->Release();
    }
  }
  if (staging_map_ && staging_) {
    staging_->Unmap(0, nullptr);
  }
  if (fence_event_) {
    CloseHandle(fence_event_);
  }
}

void TextureCache::PublishSRV(const CachedTexture* tex, D3D12_CPU_DESCRIPTOR_HANDLE dest) const {
  if (!tex || !tex->resident || !tex->resource) {
    PublishNullSRV(dest);
    return;
  }
  D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
  desc.Format = tex->format;
  desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  desc.Shader4ComponentMapping = tex->swizzle;
  desc.Texture2D.MostDetailedMip = 0;
  desc.Texture2D.MipLevels = tex->mip_count;
  desc.Texture2D.PlaneSlice = 0;
  desc.Texture2D.ResourceMinLODClamp = 0.0f;
  device_->CreateShaderResourceView(tex->resource.Get(), &desc, dest);
}

void TextureCache::PublishNullSRV(D3D12_CPU_DESCRIPTOR_HANDLE dest) const {
  D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  desc.Texture2D.MostDetailedMip = 0;
  desc.Texture2D.MipLevels = 1;
  desc.Texture2D.PlaneSlice = 0;
  desc.Texture2D.ResourceMinLODClamp = 0.0f;
  device_->CreateShaderResourceView(null_tex_.Get(), &desc, dest);
}

const TextureCache::HostFormat& TextureCache::GetHostFormat(TextureFormat f) {
  static HostFormat table[64] = {};
  static bool inited = false;
  if (!inited) {
    inited = true;
    auto set = [] (TextureFormat fmt, DXGI_FORMAT dxgi, uint32_t host_swizzle) {
      table[uint32_t(fmt)] = HostFormat{dxgi, host_swizzle, true};
    };
    // Common formats (identity host swizzle unless noted). Mirrors the built-in
    // D3D12TextureCache::host_formats_ for the formats Fable 2 uses.
    set(TextureFormat::k_8, DXGI_FORMAT_R8_UNORM,
        XE_GPU_TEXTURE_SWIZZLE_RRRR);
    set(TextureFormat::k_8_8_8_8, DXGI_FORMAT_R8G8B8A8_UNORM,
        XE_GPU_TEXTURE_SWIZZLE_RGBA);
    set(TextureFormat::k_8_8_8_8_A, DXGI_FORMAT_R8G8B8A8_UNORM,
        XE_GPU_TEXTURE_SWIZZLE_RGBA);
    set(TextureFormat::k_2_10_10_10, DXGI_FORMAT_R10G10B10A2_UNORM,
        XE_GPU_TEXTURE_SWIZZLE_RGBA);
    set(TextureFormat::k_DXT1, DXGI_FORMAT_BC1_UNORM, XE_GPU_TEXTURE_SWIZZLE_RGBA);
    set(TextureFormat::k_DXT2_3, DXGI_FORMAT_BC2_UNORM, XE_GPU_TEXTURE_SWIZZLE_RGBA);
    set(TextureFormat::k_DXT4_5, DXGI_FORMAT_BC3_UNORM, XE_GPU_TEXTURE_SWIZZLE_RGBA);
    set(TextureFormat::k_16_16_16_16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT,
        XE_GPU_TEXTURE_SWIZZLE_RGBA);
  }
  return table[uint32_t(f) & 63];
}

uint32_t TextureCache::MakeSRVSwizzle(uint32_t guest_swizzle, uint32_t host_swizzle) {
  uint32_t result = 0;
  for (uint32_t i = 0; i < 4; ++i) {
    const uint32_t c = (guest_swizzle >> (3 * i)) & 0x7;
    // Guest 3-bit value: 0-3 = channel, 4 = zero, 5 = one (6/7 clamped). The
    // host format swizzle remaps the guest's native channel order onto the
    // D3D12 format's channels; then +1 converts guest encoding to D3D12's
    // Shader4ComponentMapping (R=1,G=2,B=3,A=4,0=5,1=6).
    const uint32_t hc = (c >= 4) ? (c & 0x5) : ((host_swizzle >> (3 * c)) & 0x7);
    result |= hc << (3 * i);
  }
  return result | kAlwaysSetBit;
}

void TextureCache::WaitForFenceValue(uint64_t value) {
  if (!fence_ || !fence_event_) {
    return;
  }
  // Wait until the fence is ACTUALLY complete. The auto-reset event can be
  // signaled by a catch-up (or the prior SetEventOnCompletion), so a single
  // WaitForSingleObject does not guarantee GetCompletedValue() >= value. Resetting
  // the single command allocator before the prior upload's command list has fully
  // executed makes D3D12 raise COMMAND_ALLOCATOR_RESET and remove the device, so
  // loop on the completed value. The GPU always finishes the upload, so this
  // terminates (a slow DD adapter just stalls the frame).
  while (fence_->GetCompletedValue() < value) {
    fence_->SetEventOnCompletion(value, reinterpret_cast<HANDLE>(fence_event_));
    WaitForSingleObject(reinterpret_cast<HANDLE>(fence_event_), 5000);
  }
}

void TextureCache::WaitForIdle() {
  WaitForFenceValue(fence_value_);
}

bool TextureCache::CreateAndUpload(CachedTexture& tex,
                                   const rex::graphics::xenos::xe_gpu_texture_fetch_t& fetch) {
  const auto& hf = GetHostFormat(fetch.format);
  if (!hf.supported || hf.format == DXGI_FORMAT_UNKNOWN) {
    static std::unordered_set<uint32_t> logged;
    uint32_t fk = uint32_t(fetch.format);
    if (logged.insert(fk).second) {
      REXGPU_WARN("[custom-renderer] texture cache: unsupported format {} - skipping", fk);
    }
    ++unsupported_;
    return false;
  }

  rex::graphics::TextureInfo info;
  if (!rex::graphics::TextureInfo::Prepare(fetch, &info)) {
    return false;
  }
  const uint32_t width = info.width + 1;
  const uint32_t height = info.height + 1;
  uint32_t mip_count = info.mip_max_level - info.mip_min_level + 1;
  if (!width || !height || !mip_count) {
    return false;
  }
  // Fable 2 textures are at most 4096 wide/tall; reject oversized progressive
  // uploads (the guest re-requests a growing base address each frame) which
  // would otherwise thrash the cache and stall the GPU-commands thread.
  const uint32_t max_dim = 4096u;
  if (width > max_dim || height > max_dim || mip_count > 16) {
    return false;
  }
  // D3D12 cannot create a texture with more mips than its size allows
  // (max = floor(log2(max(w,h))) + 1). The guest can request an excessive mip
  // count (e.g. 14 for a 1028x1361 texture), which triggers a stack buffer
  // overrun in the NVIDIA driver's CreateCommittedResource allocation check
  // (0xC0000409) and kills the process. Cap to the D3D12 maximum.
  {
    const uint32_t maxd = width > height ? width : height;
    uint32_t max_mips = 1;
    uint32_t t = maxd;
    while (t > 1) {
      t >>= 1;
      ++max_mips;
    }
    if (mip_count > max_mips) {
      mip_count = max_mips;
    }
  }

  tex.width = width;
  tex.height = height;
  tex.mip_count = mip_count;
  tex.format = hf.format;
  tex.swizzle = 0;
  {
    static uint64_t s_create = 0;
    if (s_create < 12) {
      REXGPU_INFO("[custom-renderer] tex create #{}: base={:#010x} fmt={} {}x{} mips={}", s_create,
                  fetch.base_address << 12, uint32_t(fetch.format), width, height, mip_count);
    }
    ++s_create;
  }

  const auto* fi = rex::graphics::FormatInfo::Get(uint32_t(fetch.format));

  // D3D12 footprints for every mip subresource (row pitch, dims, copy size).
  D3D12_RESOURCE_DESC rdesc{};
  rdesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  rdesc.Width = width;
  rdesc.Height = height;
  rdesc.DepthOrArraySize = 1;
  rdesc.MipLevels = mip_count;
  rdesc.Format = hf.format;
  rdesc.SampleDesc.Count = 1;
  rdesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(mip_count);
  std::vector<UINT> num_rows(mip_count);
  std::vector<UINT64> row_size(mip_count);
  std::vector<UINT64> total_bytes(mip_count);
  device_->GetCopyableFootprints(&rdesc, 0, mip_count, 0, layouts.data(), num_rows.data(),
                                 row_size.data(), total_bytes.data());

  // Compute the total staging size across all mip subresources.
  uint64_t total = 0;
  for (uint32_t i = 0; i < mip_count; ++i) {
    total = (total + 255) & ~255ULL;  // keep 256-byte alignment between mips
    total += total_bytes[i];
  }
  if (total > kMaxStagingBytes) {
    return false;
  }

  // (Re)create the staging buffer if it is too small.
  if (!staging_ || total > staging_size_) {
    staging_.Reset();
    if (staging_map_) {
      staging_->Unmap(0, nullptr);
      staging_map_ = nullptr;
    }
    uint64_t size = (total + 255) & ~255ULL;
    D3D12_RESOURCE_DESC buf_desc{};
    buf_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buf_desc.Width = size;
    buf_desc.Height = 1;
    buf_desc.DepthOrArraySize = 1;
    buf_desc.MipLevels = 1;
    buf_desc.Format = DXGI_FORMAT_UNKNOWN;
    buf_desc.SampleDesc.Count = 1;
    buf_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES heap_props{};
    heap_props.Type = D3D12_HEAP_TYPE_UPLOAD;
    heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    // A buffer (Format=UNKNOWN) must be created with a null clear value.
    REXGPU_INFO("[custom-renderer] STAGING CCR size={} bytes (recreate)", size);
    CcrTrace("TEXSTG", 0, 0, 0, 0, size);
    HRESULT hr = device_->CreateCommittedResource(
        &heap_props, D3D12_HEAP_FLAG_NONE, &buf_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&staging_));
    if (FAILED(hr)) {
      char b[128];
      snprintf(b, sizeof(b), "staging CreateCommittedResource FAILED hr=%#x size=%llu",
               (unsigned)hr, (unsigned long long)size);
      return false;
    }
    if (FAILED(staging_->Map(0, nullptr, reinterpret_cast<void**>(&staging_map_)))) {
      return false;
    }
    staging_size_ = size;
  }

  // Create the D3D12 texture (default heap, COPY_DEST for the immediate upload).
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = width;
  desc.Height = height;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = mip_count;
  desc.Format = hf.format;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  D3D12_HEAP_PROPERTIES heap_props{};
  heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;
  {
    static uint64_t s_ccr = 0;
    ++s_ccr;
    // Log every texture CreateCommittedResource so the exact params of the
    // call that triggers the NVIDIA driver stack-overflow are captured.
    REXGPU_INFO("[custom-renderer] TEX CCR #{} {}x{} mips={} fmt=0x{:04X} heap=DEFAULT",
                s_ccr, width, height, mip_count, uint32_t(hf.format));
  }
  CcrTrace("TEX", width, height, mip_count, uint32_t(hf.format), total);
  if (FAILED(device_->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE, &desc,
                                              D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                              IID_PPV_ARGS(&tex.resource)))) {
    return false;
  }
  tex.resident = true;
  tex.bytes = total;

  // Untile each mip into the staging buffer, then copy into the texture via a
  // placed-footprint buffer source (offset + per-row pitch known to D3D12).
  if (allocator_ && command_list_ && memory_) {
    WaitForFenceValue(fence_value_);  // staging buffer must be free of GPU reads
    if (fence_ && fence_->GetCompletedValue() < fence_value_) {
      REXGPU_WARN("[custom-renderer] DIAG texcache reset while fence incomplete: "
                  "fence_value_={} completed={} (TIMED OUT?)",
                  fence_value_, fence_->GetCompletedValue());
    }
    allocator_->Reset();
    command_list_->Reset(allocator_.Get(), nullptr);
    uint64_t offset = 0;
    for (uint32_t i = 0; i < mip_count; ++i) {
      D3D12_SUBRESOURCE_FOOTPRINT fp = layouts[i].Footprint;
      offset = (offset + 255) & ~255ULL;
      uint32_t offx = 0, offy = 0;
      uint32_t base_addr = info.GetMipLocation(i + info.mip_min_level, &offx, &offy, true);
      if (base_addr) {
        UntileLevel(staging_map_ + offset, fp.RowPitch, base_addr, offx, offy, info,
                    i + info.mip_min_level, fi);
      }
      // Copy from the staging buffer using a placed-footprint source (the byte
      // offset + per-row pitch are known to D3D12).
      D3D12_TEXTURE_COPY_LOCATION src_loc{};
      src_loc.pResource = staging_.Get();
      src_loc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
      src_loc.PlacedFootprint.Offset = offset;
      src_loc.PlacedFootprint.Footprint = fp;
      D3D12_TEXTURE_COPY_LOCATION dst_loc{};
      dst_loc.pResource = tex.resource.Get();
      dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      dst_loc.SubresourceIndex = i;
      command_list_->CopyTextureRegion(&dst_loc, 0, 0, 0, &src_loc, nullptr);
      offset += total_bytes[i];
    }
    {
      D3D12_RESOURCE_BARRIER b{};
      b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      b.Transition.pResource = tex.resource.Get();
      b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
      b.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
      command_list_->ResourceBarrier(1, &b);
    }
    const HRESULT close_hr = command_list_->Close();
    if (SUCCEEDED(close_hr)) {
      ID3D12CommandList* lists[1] = {command_list_.Get()};
      queue_->ExecuteCommandLists(1, lists);
      ++fence_value_;
      fence_->SetEventOnCompletion(fence_value_, reinterpret_cast<HANDLE>(fence_event_));
      queue_->Signal(fence_.Get(), fence_value_);
      ++uploads_;
      {
        static uint64_t s_updone = 0;
        if (s_updone < 12) {
          REXGPU_INFO("[custom-renderer] tex upload submitted #{} ({} bytes)", s_updone, total);
        }
        ++s_updone;
      }
    }
  }

  // (Re)set the SRV swizzle from the fetch constant.
  const uint32_t srv_swizzle = MakeSRVSwizzle(fetch.swizzle, hf.host_swizzle);
  tex.swizzle = srv_swizzle;
  if (srv_heap_) {
    D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format = hf.format;
    desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    desc.Shader4ComponentMapping = srv_swizzle;
    desc.Texture2D.MostDetailedMip = 0;
    desc.Texture2D.MipLevels = mip_count;
    desc.Texture2D.PlaneSlice = 0;
    desc.Texture2D.ResourceMinLODClamp = 0.0f;
    if (srv_used_ >= kSrvHeapSize) {
      return true;  // out of SRVs; texture is resident but unviewable
    }
    tex.srv = srv_start_;
    tex.srv.ptr += srv_used_ * srv_desc_size_;
    ++srv_used_;
    device_->CreateShaderResourceView(tex.resource.Get(), &desc, tex.srv);
  }
  return true;
}

void TextureCache::UntileLevel(uint8_t* dst_row, uint32_t row_pitch, uint32_t base_addr,
                               uint32_t offx, uint32_t offy, const rex::graphics::TextureInfo& info,
                               uint32_t level, const rex::graphics::FormatInfo* fi) {
  const bool tiled = info.is_tiled;
  const uint32_t bytes_per_block = fi ? fi->bytes_per_block() : 4;
  const uint32_t log2_bpb = Log2Floor(bytes_per_block);
  const auto& ext = info.GetMipExtent(level, true);
  const uint32_t blocks_x = ext.block_width;
  const uint32_t blocks_y = ext.block_height;
  const uint32_t pitch_blocks = ext.block_pitch_h;
  // Bounds-check the guest address: base_addr must be in the 31-bit guest
  // physical space with a generous margin after it (so a misprogrammed fetch
  // constant cannot read out of bounds and crash the process).
  const uint64_t kGuestMin = 0x20000000ull;
  const uint64_t kGuestMax = 0x20000000ull + (512ull << 20) - 1;
  if (base_addr < kGuestMin || base_addr > kGuestMax - (64ull << 20) || !blocks_x || !blocks_y) {
    return;
  }
  const uint8_t* guest = memory_->TranslatePhysical<const uint8_t*>(base_addr);
  if (!guest) {
    return;
  }
  for (uint32_t by = 0; by < blocks_y; ++by) {
    uint8_t* dst = dst_row + by * row_pitch;
    for (uint32_t bx = 0; bx < blocks_x; ++bx) {
      const uint32_t gx = bx + offx;
      const uint32_t gy = by + offy;
      uint32_t offset;
      if (tiled) {
        offset = uint32_t(
            rex::graphics::texture_util::GetTiledOffset2D(int32_t(gx), int32_t(gy), pitch_blocks,
                                                          log2_bpb));
      } else {
        offset = (gy * pitch_blocks + gx) * bytes_per_block;
      }
      std::memcpy(dst + bx * bytes_per_block, guest + offset, bytes_per_block);
    }
  }
}

TextureCache::CachedTexture* TextureCache::RequestTexture(
    const rex::graphics::xenos::xe_gpu_texture_fetch_t& fetch) {
  if (!device_ || !srv_heap_) {
    return nullptr;
  }
  const uint32_t width = fetch.size_2d.width + 1;
  const uint32_t height = fetch.size_2d.height + 1;
  if (!width || !height) {
    return nullptr;
  }
  const uint32_t format = uint32_t(fetch.format);
  if (!GetHostFormat(fetch.format).supported) {
    return nullptr;
  }
  // Effective mip count: 1 + mip_max_level (mip_min_level is assumed 0 for the
  // common case; the fetch's mip_max_level is clamped to 0 when there is no
  // mip address, matching TextureInfo::Prepare).
  uint32_t mip_count = fetch.mip_max_level + 1;
  if (mip_count > 16) {
    mip_count = 16;
  }

  Key key;
  key.base_address = fetch.base_address << 12;
  key.format = format;
  key.width = width;
  key.height = height;
  // mip_count is intentionally excluded from the key (see Key in the header).
  key.tiled = fetch.tiled ? 1 : 0;
  key.pitch = fetch.pitch << 5;

  {
    static uint64_t s_req = 0;
    if (s_req < 8) {
      REXGPU_INFO("[custom-renderer] tex request #{}: base={:#010x} format={} {}x{} mips={} "
                  "tiled={} pitch={} sw={:#05x}",
                  s_req, key.base_address, format, width, height, mip_count, key.tiled, key.pitch,
                  fetch.swizzle);
    }
    ++s_req;
  }

  auto it = index_.find(key);
  if (it != index_.end()) {
    auto* tex = &textures_[it->second];
    ++hits_;
    const auto& hf = GetHostFormat(fetch.format);
    const uint32_t srv_swizzle = MakeSRVSwizzle(fetch.swizzle, hf.host_swizzle);
    if (tex->swizzle != srv_swizzle) {
      tex->swizzle = srv_swizzle;
      D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
      desc.Format = tex->format;
      desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      desc.Shader4ComponentMapping = srv_swizzle;
      desc.Texture2D.MipLevels = tex->mip_count;
      device_->CreateShaderResourceView(tex->resource.Get(), &desc, tex->srv);
    }
    return tex;
  }

  CachedTexture tex;
  if (!CreateAndUpload(tex, fetch)) {
    return nullptr;
  }
  ++misses_;
  const int idx = static_cast<int>(textures_.size());
  index_[key] = idx;
  textures_.push_back(std::move(tex));
  bytes_ += textures_.back().bytes;
  return &textures_.back();
}

}  // namespace d3d12
}  // namespace customrenderer
