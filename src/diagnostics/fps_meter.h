// fps_meter.h - lightweight per-frame rate meter for verifying FPS changes.
//
// Strong override of the recompiled main-loop function MainRenderLoop_82B9CD68
// (0x82B9CD68; renamed from sub_82B9CD68 in fable_2_manifest.toml). One
// call per frame; ~33 ms period at the 30 fps cap, see
// docs/FPS_CAP_INVESTIGATION.md). Counts invocations over 5 s windows and
// appends `mainloop rate=NN.N/s` lines to fps_meter.log (CWD = exe dir).
// Forward-only: the override just counts, then calls the original
// __imp__ entry point, so it is safe to leave compiled in.
//
// Enabled with the FABLE2_FPS_METER=1 environment variable. Off by default.
// Include this header in the app's main TU (main.cpp) - the strong override
// below then binds in place of the weak recompiled symbol.

#pragma once
#if defined(__APPLE__)
#include "rate_window.h"
#include <mutex>
#include <pthread.h>
#endif

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>

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
    if (!fable2::functrace::enabled().load()) {
      const char* f = std::getenv("FABLE2_TRACE_FILTER");
      if (f) Fable2FuncTraceSetFilter(f);
      fable2::functrace::set_enabled(true);
      std::fprintf(stderr,
                   "[functrace-window] tracing %.1fs starting now\n", dur_s);
    }
  } else {
    if (fable2::functrace::enabled().load()) {
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

extern "C" void MainRenderLoop_82B9CD68(PPCContext& ctx, uint8_t* base) {
#if !defined(FABLE2_ENABLE_FUNC_TRACE) || FABLE2_ENABLE_FUNC_TRACE
  // Bounded unfiltered trace window (FABLE2_TRACE_WINDOW=1); see above.
  fable2::functrace_window::run_window(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
#endif
#if defined(__APPLE__)
  // Measure guest render-loop calls, not displayed frames. Separate thread
  // counters avoid combining unrelated timing windows; only log writes lock.
  if (fable2::fpsmeter::enabled()) {
    static thread_local fable2::measurement::RateWindow meter;
    const auto now = std::chrono::steady_clock::now();
    const int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();
    if (auto report = meter.tick(us)) {
      static std::mutex output_mutex;
      const std::lock_guard guard(output_mutex);
      uint64_t thread_id = 0;
      pthread_threadid_np(nullptr, &thread_id);
      static std::ofstream logf{[] {
        const char* path = std::getenv("FABLE2_FPS_LOG");
        return path ? path : "fps_meter.log";
      }(), std::ios::out};
      const auto epoch = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch()).count();
      if (logf) {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "unix_ms=%lld tid=%llu guest_render_rate=%.3f/s "
            "window_seconds=%.3f max_gap_ms=%.3f total=%llu\n",
            (long long)epoch, (unsigned long long)thread_id, report->rate,
            report->seconds, report->max_gap_ms, (unsigned long long)report->total);
        logf << buf;
        logf.flush();
      }
    }
  }
#else
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
    if (start == 0)
      window_start_us.compare_exchange_strong(start, now_us);
    const uint64_t n = window_calls.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n == 1 || (now_us - start) >= 5'000'000) {
      window_calls.store(0, std::memory_order_relaxed);
      window_start_us.store(now_us, std::memory_order_relaxed);
      const double secs = (now_us - start) / 1'000'000.0;
      if (logf) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "mainloop rate=%.1f/s (total %llu)\n",
                      n / secs,
                      (unsigned long long)calls.load(std::memory_order_relaxed));
        logf << buf;
        logf.flush();
      }
    }
  }
#endif
  // F5 (host) -> run the external Lua file (per-frame, responsive).
  fable2::f5lua::poll_mainloop(ctx, base);
  __imp__MainRenderLoop_82B9CD68(ctx, base);
}
