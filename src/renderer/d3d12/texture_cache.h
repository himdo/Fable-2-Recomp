// texture_cache.h - Custom D3D12 renderer: M2 texture cache.
//
// Decodes Xenos texture fetch constants into D3D12 textures. On a cache miss it
// creates a default-heap D3D12 texture, CPU-untiles the guest texture data
// (tiled or linear, all mips) into an upload-heap staging buffer and copies it
// in with CopyTextureRegion, then publishes an SRV whose swizzle is the
// fetch-constant swizzle composed with the format's host swizzle.
//
// Reuses the SDK's backend-agnostic texture pipeline (compiled into this
// plugin): TextureInfo (fetch decode + mip/tile layout), GetTiledOffset2D
// (tile offset) and FormatInfo (per-format block size). Mirrors the built-in
// D3D12TextureCache's format->DXGI + swizzle table, but untiles on the CPU
// (the built-in does it with a compute shader; CPU is fine here since texture
// uploads are infrequent).
//
// See plans/custom-d3d12-renderer.md (M2).

#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>
#include <wrl/client.h>

#include <d3d12.h>

#include <rex/graphics/xenos.h>
#include <rex/system/xmemory.h>

namespace rex::graphics {
struct TextureInfo;
struct FormatInfo;
}

namespace customrenderer {
namespace d3d12 {

class TextureCache {
 public:
  struct Key {
    uint32_t base_address = 0;
    uint32_t format = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t tiled = 0;
    uint32_t pitch = 0;
    // NOTE: mip_count is deliberately NOT part of the key. The guest
    // progressively re-requests the same texture (same base address) with a
    // growing mip count each frame; keying on mip_count would create a new
    // D3D12 texture for every mip count, thrashing the cache and pushing the
    // running CreateCommittedResource count up to a point where the NVIDIA
    // driver's allocation check stack-overflows (0xC0000409).
    bool operator==(const Key& o) const {
      return base_address == o.base_address && format == o.format && width == o.width &&
             height == o.height && tiled == o.tiled && pitch == o.pitch;
    }
  };
  struct KeyHash {
    size_t operator()(const Key& k) const {
      uint64_t h = k.base_address;
      h = h * 0x100000001b3ULL + k.format;
      h = h * 0x100000001b3ULL + k.width;
      h = h * 0x100000001b3ULL + k.height;
      h = h * 0x100000001b3ULL + k.tiled;
      h = h * 0x100000001b3ULL + k.pitch;
      return static_cast<size_t>(h);
    }
  };

  struct CachedTexture {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    D3D12_CPU_DESCRIPTOR_HANDLE srv{};
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mip_count = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    uint32_t swizzle = 0;  // current SRV Shader4ComponentMapping
    bool resident = false;
    uint64_t bytes = 0;
  };

  TextureCache(ID3D12Device* device, ID3D12CommandQueue* queue, rex::memory::Memory* memory);
  ~TextureCache();

  // Request a texture from a fetch constant. Creates + uploads on a miss and
  // (re)sets the SRV swizzle. Returns the cached texture, or nullptr on an
  // unsupported format / allocation failure.
  CachedTexture* RequestTexture(const rex::graphics::xenos::xe_gpu_texture_fetch_t& fetch);

  D3D12_CPU_DESCRIPTOR_HANDLE NullSRV() const { return null_srv_; }
  // Create this texture's 2D SRV at `dest` (a CPU handle in any CBV_SRV_UAV heap).
  // Used to publish texture SRVs straight into the draw heap (avoids
  // CopyDescriptorsSimple, which the compat driver rejects for this layout).
  void PublishSRV(const CachedTexture* tex, D3D12_CPU_DESCRIPTOR_HANDLE dest) const;
  // Create a 1x1 null-texture SRV at `dest`.
  void PublishNullSRV(D3D12_CPU_DESCRIPTOR_HANDLE dest) const;
  void WaitForIdle();

  uint64_t hits() const { return hits_; }
  uint64_t misses() const { return misses_; }
  uint64_t uploads() const { return uploads_; }
  uint32_t resident() const { return static_cast<uint32_t>(textures_.size()); }
  uint64_t bytes() const { return bytes_; }
  uint32_t unsupported() const { return unsupported_; }

 private:
  struct HostFormat {
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    uint32_t host_swizzle = 0;  // guest encoding (RGBA = identity)
    bool supported = false;
  };
  static const HostFormat& GetHostFormat(rex::graphics::xenos::TextureFormat f);
  static uint32_t MakeSRVSwizzle(uint32_t guest_swizzle, uint32_t host_swizzle);

  bool CreateAndUpload(CachedTexture& tex, const rex::graphics::xenos::xe_gpu_texture_fetch_t& fetch);
  void UntileLevel(uint8_t* dst_row, uint32_t row_pitch, uint32_t base_addr, uint32_t offx,
                   uint32_t offy, const rex::graphics::TextureInfo& info, uint32_t level,
                   const rex::graphics::FormatInfo* fi);
  void WaitForFenceValue(uint64_t value);

  Microsoft::WRL::ComPtr<ID3D12Device> device_;
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
  rex::memory::Memory* memory_ = nullptr;

  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srv_heap_;
  D3D12_CPU_DESCRIPTOR_HANDLE srv_start_{};
  uint32_t srv_desc_size_ = 0;
  uint32_t srv_used_ = 0;
  D3D12_CPU_DESCRIPTOR_HANDLE null_srv_{};
  // 1x1 texture backing the null SRV (CreateShaderResourceView(nullptr,...)
  // device-removes on this driver, so a real resource is required).
  Microsoft::WRL::ComPtr<ID3D12Resource> null_tex_;

  Microsoft::WRL::ComPtr<ID3D12Resource> staging_;
  uint8_t* staging_map_ = nullptr;
  uint64_t staging_size_ = 0;
  Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator_;
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list_;
  Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
  void* fence_event_ = nullptr;
  uint64_t fence_value_ = 0;
  uint64_t fence_pending_ = 0;

  std::unordered_map<Key, int, KeyHash> index_;
  std::vector<CachedTexture> textures_;
  uint64_t hits_ = 0;
  uint64_t misses_ = 0;
  uint64_t uploads_ = 0;
  uint64_t bytes_ = 0;
  uint32_t unsupported_ = 0;
};

}  // namespace d3d12
}  // namespace customrenderer
