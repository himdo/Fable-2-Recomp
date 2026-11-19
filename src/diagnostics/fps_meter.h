// fps_meter.h - lightweight per-frame rate meter for verifying FPS changes.
//
// Strong override of the recompiled main-loop function MainRenderLoop_82B9CD68
// (0x82B9CD68; renamed from sub_82B9CD68 in fable_2_manifest.toml). One
// call per frame; ~33 ms period at the 30 fps cap, see
// docs/FPS_CAP_INVESTIGATION.md). Counts invocations over 5 s windows and
// appends one line per window to fps_meter.log (CWD = exe dir):
//   HH:MM:SS t=<s since start> mainloop rate=NN.N/s low1%=NN.N worst=NN.Nms
//   hitches>50ms=N (total N)
// low1% is the rate the slowest 1% of the window's frames ran at, worst the
// longest frame and hitches the frames longer than 50 ms, so stutter shows
// even when the average looks fine. The wall-clock time lines a window up
// with the game log.
// Forward-only: the override just counts, then calls the original
// __imp__ entry point, so it is safe to leave compiled in.
//
// Enabled with the FABLE2_FPS_METER=1 environment variable. Off by default.
// Include this header in the app's main TU (main.cpp) - the strong override
// below then binds in place of the weak recompiled symbol.

#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <vector>

#include <rex/ppc/func.h>

#include "fable2_func_trace.h"
#include "fable2_f5_lua.h"  // F5 external-Lua run (polled per frame)

namespace fable2::functrace_window {

// Traces a bounded time window of the (unfiltered) guest call stream, armed
// from the main loop so it lines up with a known on-screen moment (e.g. the
// main menu) without running a multi-GB unfiltered trace for the whole boot.
//
//   FABLE2_TRACE_WINDOW=1         arm the window
//   FABLE2_TRACE_DELAY=<sec>      start this many seconds after the first
//                                 main-loop call (default 0)
//   FABLE2_TRACE_DUR=<sec>        trace duration in seconds (default 5)
//   FABLE2_TRACE_FILTER=<substr>  optional name filter while the window is
//                                 open (default: everything)
//
// Writes the usual fable2_func_trace.log (append) + summary files.
inline bool enabled() {
#ifdef _WIN32
  char v[8] = {};
  size_t n = 0;
  return ::getenv_s(&n, v, sizeof(v), "FABLE2_TRACE_WINDOW") == 0 && v[0] == '1';
#else
  const char* v = std::getenv("FABLE2_TRACE_WINDOW");
  return v != nullptr && v[0] == '1';
#endif
}

inline double delay_seconds() {
  const char* v = std::getenv("FABLE2_TRACE_DELAY");
  return v ? std::atof(v) : 0.0;
}

inline double duration_seconds() {
  const char* v = std::getenv("FABLE2_TRACE_DUR");
  return v ? std::atof(v) : 5.0;
}

inline void run_window(int64_t now_us) {
  if (!enabled()) return;
  static const int64_t t0 = now_us;  // first main-loop call
  const double delay_s = delay_seconds();
  const double dur_s = duration_seconds();
  const int64_t start_us = t0 + static_cast<int64_t>(delay_s * 1e6);
  const int64_t end_us = start_us + static_cast<int64_t>(dur_s * 1e6);
  if (now_us < start_us) return;
  if (now_us < end_us) {
    if (!fable2::functrace::enabled()) {
      const char* f = std::getenv("FABLE2_TRACE_FILTER");
      if (f) Fable2FuncTraceSetFilter(f);
      fable2::functrace::set_enabled(true);
      std::fprintf(stderr,
                   "[functrace-window] tracing %.1fs starting now\n", dur_s);
    }
  } else {
    if (fable2::functrace::enabled()) {
      fable2::functrace::set_enabled(false);
      fable2::functrace::flush();
      std::fprintf(stderr, "[functrace-window] window done, log flushed\n");
    }
  }
}

}  // namespace fable2::functrace_window

namespace fable2::fpsmeter {

inline bool enabled() {
  static const bool e = [] {
    const char* v = std::getenv("FABLE2_FPS_METER");
    return v && v[0] == '1';
  }();
  return e;
}

}  // namespace fable2::fpsmeter

// Per-frame hook for the main-menu mod item (fable2_modmenu.h, included
// after this header). Kept as a forward declaration here so the mod menu can
// keep its label cache fresh every frame without redefining the render-loop
// override (which this header owns). Defined in fable2::modmenu::on_frame.
namespace fable2 { namespace modmenu { void on_frame(uint8_t* base); } }

extern "C" void MainRenderLoop_82B9CD68(PPCContext& ctx, uint8_t* base) {
  // Bounded unfiltered trace window (FABLE2_TRACE_WINDOW=1); see above.
  fable2::functrace_window::run_window(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
  if (fable2::fpsmeter::enabled()) {
    static std::atomic<uint64_t> calls{0};
    static std::atomic<int64_t> window_start_us{0};
    static std::atomic<uint64_t> window_calls{0};
    static std::ofstream& logf = [] -> std::ofstream& {
      std::remove("fps_meter.log");  // fresh log per run
      static std::ofstream f{"fps_meter.log", std::ios::app};
      return f;
    }();
    const int64_t now_us = std::chrono::duration_cast<std::chrono::microseconds>(
                               std::chrono::steady_clock::now().time_since_epoch())
                               .count();
    calls.fetch_add(1, std::memory_order_relaxed);
    int64_t start = window_start_us.load(std::memory_order_relaxed);
    if (start == 0) {
      // First call: open the first window. A failed exchange leaves the start
      // time another caller just set in `start`.
      if (window_start_us.compare_exchange_strong(start, now_us)) start = now_us;
    }
    const uint64_t n = window_calls.fetch_add(1, std::memory_order_relaxed) + 1;
    // Per-frame times for the window (main loop runs on one thread), so each
    // line can report the 1% low and hitches, not only the average.
    static std::vector<int32_t> frame_us = [] {
      std::vector<int32_t> v;
      v.reserve(2048);
      return v;
    }();
    static int64_t last_us = 0;
    static const int64_t run_start_us = now_us;
    if (last_us != 0) frame_us.push_back(static_cast<int32_t>(now_us - last_us));
    last_us = now_us;
    if ((now_us - start) >= 5'000'000) {
      window_calls.store(0, std::memory_order_relaxed);
      window_start_us.store(now_us, std::memory_order_relaxed);
      const double secs = (now_us - start) / 1'000'000.0;
      double low1 = 0.0, worst_ms = 0.0;
      size_t hitches = 0;
      if (!frame_us.empty()) {
        std::vector<int32_t> sorted = frame_us;
        std::sort(sorted.begin(), sorted.end());
        const size_t i99 = std::min(sorted.size() - 1, (sorted.size() * 99) / 100);
        low1 = sorted[i99] > 0 ? 1e6 / sorted[i99] : 0.0;
        worst_ms = sorted.back() / 1000.0;
        for (int32_t f : frame_us) hitches += f > 50'000;
      }
      frame_us.clear();
      if (logf) {
        char clock[16] = "";
        {
          std::time_t tt = std::time(nullptr);
          std::tm tmv{};
#ifdef _WIN32
          localtime_s(&tmv, &tt);
#else
          localtime_r(&tt, &tmv);
#endif
          std::strftime(clock, sizeof(clock), "%H:%M:%S", &tmv);
        }
        char buf[208];
        std::snprintf(buf, sizeof(buf),
                      "%s t=%6.0fs mainloop rate=%.1f/s low1%%=%.1f worst=%.1fms hitches>50ms=%zu (total %llu)\n",
                      clock, (now_us - run_start_us) / 1e6, n / secs, low1, worst_ms, hitches,
                      (unsigned long long)calls.load(std::memory_order_relaxed));
        logf << buf;
        logf.flush();
      }
    }
  }
  // F5 (run the external Lua file) is not handled here: it runs on the world
  // thread (fable2::f5lua::poll_world, from console::service_world_thread).
  fable2::console::poll(ctx, base);
  // Main-menu mod item: keep the relabeled row's string cache fresh.
  fable2::modmenu::on_frame(base);
  __imp__MainRenderLoop_82B9CD68(ctx, base);
}
