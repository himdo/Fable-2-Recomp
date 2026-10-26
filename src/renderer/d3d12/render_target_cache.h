// render_target_cache.h - Custom D3D12 renderer: M3 render-target cache.
//
// Maps a guest render-target base address (the front buffer, or a rasterizer
// back buffer) to a D3D12 color texture (+ optional depth texture) plus its
// RTV/DSV descriptors. The front buffer is the primary RT: it is what
// IssueSwap presents. By making the front buffer a real D3D12 render target
// (cleared, and drawn into by M4) and presenting it with a GPU copy, we drop
// the M1 CPU untile for the presented image and get "no flicker from stale
// RTs" (the same guest address reuses the same D3D12 resource).
//
// The RT cache owns only resources + a small RTV/DSV descriptor heap. The
// actual clear + copy are recorded by the command processor (on its own
// command list) so the presented image is produced by one ordered command
// stream on the shared direct queue.
//
// See plans/custom-d3d12-renderer.md (M3).

#pragma once

#include <cstdint>
#include <wrl/client.h>
#include <d3d12.h>
#include <unordered_map>

namespace customrenderer {
namespace d3d12 {

class RenderTargetCache {
 public:
  struct Entry {
    Microsoft::WRL::ComPtr<ID3D12Resource> color;
    Microsoft::WRL::ComPtr<ID3D12Resource> depth;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv{};  // color RTV (for clears)
    D3D12_CPU_DESCRIPTOR_HANDLE dsv{};  // depth DSV (for clears)
    uint32_t width = 0;
    uint32_t height = 0;
    DXGI_FORMAT color_format = DXGI_FORMAT_UNKNOWN;
    bool has_depth = false;
    bool valid = false;
    bool just_created = false;
    uint64_t last_used = 0;  // LRU counter
  };

  RenderTargetCache(ID3D12Device* device, ID3D12CommandQueue* queue);
  ~RenderTargetCache();

  // Get or create the RT (color + optional depth) for the given guest address
  // + dims + format. Creates the color texture in COPY_DEST (so it can be
  // cleared/copied), the depth texture in DEPTH_WRITE, and the RTV + DSV
  // descriptors. Returns nullptr on failure (bad dims/format, out of memory).
  Entry* GetOrCreate(uint32_t address, uint32_t width, uint32_t height,
                     DXGI_FORMAT color_format, bool has_depth);

  // The RTV/DSV descriptor heaps (needed by the command processor to bind the
  // RTV/DSV CPU handles via SetDescriptorHeaps).
  ID3D12DescriptorHeap* rtv_heap() const { return rtv_heap_.Get(); }
  ID3D12DescriptorHeap* dsv_heap() const { return dsv_heap_.Get(); }

  // Write a solid clear color into the CPU staging buffer for an RT of the
  // given dims (R10G10B10A2 packed). The command processor then copies the
  // staging buffer into the RT (a CPU-untile-style clear, avoiding the
  // driver's CreateRenderTargetView device-removal).
  void WriteClearColor(uint32_t width, uint32_t height, float r, float g,
                       float b, float a);
  ID3D12Resource* staging() const { return staging_.Get(); }

  uint32_t count() const { return uint32_t(entries_.size()); }
  uint64_t bytes() const { return bytes_; }
  uint32_t eviction_count() const { return evictions_; }
  // Number of distinct RT addresses created so far (misses).
  uint64_t create_count() const { return create_count_; }

 private:
  static constexpr uint32_t kMaxRTs = 128;
  static constexpr uint32_t kDescriptorCount = 512;  // RTV + DSV slots

  Microsoft::WRL::ComPtr<ID3D12Device> device_;
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;

  // Separate RTV and DSV heaps (they are distinct heap types in D3D12).
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_heap_;
  D3D12_CPU_DESCRIPTOR_HANDLE rtv_heap_start_{};
  uint32_t rtv_desc_size_ = 0;
  uint32_t rtv_cursor_ = 0;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsv_heap_;
  D3D12_CPU_DESCRIPTOR_HANDLE dsv_heap_start_{};
  uint32_t dsv_desc_size_ = 0;
  uint32_t dsv_cursor_ = 0;

  // CPU staging buffer for the clear color (R10G10B10A2, mapped).
  Microsoft::WRL::ComPtr<ID3D12Resource> staging_;
  uint8_t* staging_map_ = nullptr;
  uint32_t staging_w_ = 0;
  uint32_t staging_h_ = 0;

  std::unordered_map<uint32_t, Entry> entries_;
  uint64_t lru_counter_ = 0;
  uint64_t create_count_ = 0;
  uint32_t evictions_ = 0;
  uint64_t bytes_ = 0;

  void EvictIfNeeded();
  void EnsureStaging(uint32_t width, uint32_t height);
};

}  // namespace d3d12
}  // namespace customrenderer
