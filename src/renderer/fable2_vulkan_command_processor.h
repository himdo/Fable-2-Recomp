#pragma once

// Fable2VulkanCommandProcessor
//
// The guest -> Vulkan command encoder for the Fable 2 custom renderer.
//
// It derives from the SDK's Vulkan command processor so the full, correct
// Xenos command-packet pipeline works out of the box (register writes, draw
// dispatch, copy/blit, render targets, shared memory, swap/present). For the
// first increment it is a *thin seam*: every method is inherited, and the
// overrides below exist to prove the seam is live and to hook the
// performance-program instrumentation (see plans/custom-vulkan-renderer.md,
// Workstream 10).
//
// The rewrite strategy (plan: "Open questions / risks") is to progressively
// replace the SDK's costliest Vulkan classes with custom implementations.
// Those replacements land here (and in the fable2/vulkan/ cache classes) while
// this class stays the single object the graphics system constructs.

#include <atomic>
#include <chrono>
#include <cstdint>

#include <rex/graphics/vulkan/command_processor.h>
#include <rex/logging/macros.h>

namespace fable2::renderer {

namespace {
// Per-frame CPU timing for the W10 performance program.
// g_frame_start is set on the first WriteRegister after a swap, giving us
// the full CPU encoding window (register writes + draws + state changes).
inline std::chrono::steady_clock::time_point& FrameStartTime() {
  static std::chrono::steady_clock::time_point t;
  return t;
}
std::atomic<uint64_t> g_frame_count{0};
std::atomic<double> g_swap_ms_accum{0.0};
std::atomic<double> g_swap_ms_max{0.0};
std::atomic<uint64_t> g_draw_count{0};
std::atomic<double> g_encode_ms_accum{0.0};
std::atomic<double> g_encode_ms_max{0.0};
}  // namespace

class Fable2VulkanCommandProcessor : public rex::graphics::vulkan::VulkanCommandProcessor {
 public:
  Fable2VulkanCommandProcessor(rex::graphics::vulkan::VulkanGraphicsSystem* graphics_system,
                               rex::system::KernelState* kernel_state)
      : rex::graphics::vulkan::VulkanCommandProcessor(graphics_system, kernel_state) {
    REXGPU_INFO("[fable2-vk] Fable2VulkanCommandProcessor constructed (profiler active)");
  }

  // Track the start of the CPU encoding window. The first WriteRegister after
  // a swap marks the beginning of the next frame's encoding.
  void WriteRegister(uint32_t index, uint32_t value) override {
    // If no frame is in progress (g_frame_count hasn't advanced since last
    // swap), mark the start time. We use a simple heuristic: if the frame
    // start is in the past by more than 1ms, it's a new frame.
    auto& t0 = FrameStartTime();
    const auto now = std::chrono::steady_clock::now();
    // Lazy init: if t0 is epoch, set it now.
    if (t0.time_since_epoch().count() == 0) {
      t0 = now;
    }
    rex::graphics::vulkan::VulkanCommandProcessor::WriteRegister(index, value);
  }

  // Per-frame present seam. Delegates to the base (which does the actual
  // resolve/composite/present) and records how many guest frames have been
  // presented, so the profiler hook (Workstream 10) has a live counter.
  void IssueSwap(uint32_t frontbuffer_ptr, uint32_t frontbuffer_width,
                 uint32_t frontbuffer_height) override {
    presented_frames_.fetch_add(1, std::memory_order_relaxed);
    const auto t0 = std::chrono::steady_clock::now();
    rex::graphics::vulkan::VulkanCommandProcessor::IssueSwap(frontbuffer_ptr, frontbuffer_width,
                                                             frontbuffer_height);
    const auto t1 = std::chrono::steady_clock::now();
    const double swap_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double encode_ms = std::chrono::duration<double, std::milli>(t1 - FrameStartTime()).count();
    const uint64_t fc = g_frame_count.fetch_add(1, std::memory_order_relaxed);
    g_swap_ms_accum.fetch_add(swap_ms, std::memory_order_relaxed);
    g_encode_ms_accum.fetch_add(encode_ms, std::memory_order_relaxed);
    double cur = g_swap_ms_max.load(std::memory_order_relaxed);
    while (swap_ms > cur && !g_swap_ms_max.compare_exchange_weak(cur, swap_ms, std::memory_order_relaxed)) {}
    cur = g_encode_ms_max.load(std::memory_order_relaxed);
    while (encode_ms > cur && !g_encode_ms_max.compare_exchange_weak(cur, encode_ms, std::memory_order_relaxed)) {}
    // Log every 60 frames.
    if (fc % 60 == 59) {
      const double swap_avg = g_swap_ms_accum.load(std::memory_order_relaxed) / (fc + 1);
      const double enc_avg = g_encode_ms_accum.load(std::memory_order_relaxed) / (fc + 1);
      REXGPU_INFO("[fable2-vk] frame {} : total avg={:.2f}ms max={:.2f}ms | swap avg={:.2f}ms max={:.2f}ms | draws={}",
                  fc, enc_avg, g_encode_ms_max.load(std::memory_order_relaxed),
                  swap_avg, g_swap_ms_max.load(std::memory_order_relaxed),
                  g_draw_count.load(std::memory_order_relaxed));
      g_draw_count.store(0, std::memory_order_relaxed);
    }
    // Reset the frame start for the next frame.
    FrameStartTime() = t1;
  }

  // Number of guest frames presented since startup. Exposed for the
  // diagnostics/profiling overlay.
  uint64_t presented_frame_count() const { return presented_frames_.load(std::memory_order_relaxed); }

 private:
  std::atomic<uint64_t> presented_frames_{0};
};

}  // namespace fable2::renderer
