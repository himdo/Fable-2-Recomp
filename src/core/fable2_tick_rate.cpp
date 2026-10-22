// fable2_tick_rate - see fable2_tick_rate.h.

#include "fable2_tick_rate.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>

namespace fable2::tickrate {
namespace {

using Clock = std::chrono::steady_clock;

// Frame rate sampling: count presented frames over kSampleSeconds, then fold
// the sample into an exponential average (kSmoothing = weight of the new
// sample).
constexpr double kSampleSeconds = 0.25;
constexpr double kSmoothing = 0.5;

// The smoothed rate is only republished when it is more than this far
// (relative) from the current one, so the render delay it sets (see
// fable2_hook_render_time_lf) drifts only on a real change.
constexpr double kRateTolerance = 0.01;

std::atomic<uint64_t> g_frames{0};
std::atomic<double> g_fps{0.0};
// Plan packed into one word so the game thread never sees half of an update:
// high 32 bits = HF in mHz, low 32 bits = frames per tick.
std::atomic<uint64_t> g_plan{uint64_t(kMinHfHz * 1000.0) << 32};

uint64_t Pack(double hf_hz, int frames_per_tick) {
  return (uint64_t(std::llround(hf_hz * 1000.0)) << 32) |
         uint32_t(frames_per_tick);
}

struct State {
  bool started = false;
  Clock::time_point sample_start;
  uint32_t sample_frames = 0;
  double fps = 0.0;
};

void Publish(double fps) {
  const Plan cur = CurrentPlan();
  if (fps < kMinHfHz) {
    // Too slow to tick every frame at the original rate or above: the game's
    // own 30/15 Hz timer, as in the original.
    if (cur.frames_per_tick != 0 || cur.hf_hz != kMinHfHz) {
      g_plan.store(Pack(kMinHfHz, 0), std::memory_order_relaxed);
    }
    return;
  }
  const int n = std::max(1, static_cast<int>(std::ceil(fps / kMaxHfHz)));
  const double hf = fps / n;
  if (cur.frames_per_tick == n &&
      std::fabs(hf - cur.hf_hz) <= cur.hf_hz * kRateTolerance) {
    return;
  }
  g_plan.store(Pack(hf, n), std::memory_order_relaxed);
}

}  // namespace

void OnRenderFrame() {
  g_frames.fetch_add(1, std::memory_order_release);

  // The main loop is normally entered from one thread; try_lock keeps a
  // second caller from racing the state instead of blocking it.
  static std::mutex mutex;
  static State s;
  std::unique_lock lock(mutex, std::try_to_lock);
  if (!lock.owns_lock()) return;

  const auto now = Clock::now();
  if (!s.started) {
    s.started = true;
    s.sample_start = now;
    return;
  }
  ++s.sample_frames;
  const double elapsed =
      std::chrono::duration<double>(now - s.sample_start).count();
  if (elapsed < kSampleSeconds) return;

  const double sample = s.sample_frames / elapsed;
  s.fps = s.fps == 0.0 ? sample : s.fps + kSmoothing * (sample - s.fps);
  g_fps.store(s.fps, std::memory_order_relaxed);
  s.sample_start = now;
  s.sample_frames = 0;
  Publish(s.fps);
}

uint64_t FramesPresented() { return g_frames.load(std::memory_order_acquire); }

Plan CurrentPlan() {
  const uint64_t p = g_plan.load(std::memory_order_relaxed);
  return {static_cast<double>(p >> 32) / 1000.0, static_cast<int>(uint32_t(p))};
}

double MeasuredFps() { return g_fps.load(std::memory_order_relaxed); }

}  // namespace fable2::tickrate
