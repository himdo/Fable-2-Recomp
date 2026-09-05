// fps_probe.h - temporary instrumentation to identify the 30fps frame cap.
//
// Mechanism: generated recompiled functions are WEAK extern "C" symbols
// (aliases of __imp__<name>, the always-original entry point). Defining a
// strong extern "C" symbol with the same name in this TU makes the linker
// bind every call site to our probe, which logs and forwards to the
// __imp__ original. Output goes to fps_probe.log (CWD = exe dir).

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <mutex>

#include <rex/logging.h>
#include <rex/ppc/func.h>

namespace fable2::fpsprobe {

// Always-original entry points (strong symbols in the generated code).
extern "C" void __imp__sub_82B9CD68(PPCContext& ctx, uint8_t* base);  // main loop (calls VdSwap)
extern "C" void __imp__sub_82CC2028(PPCContext& ctx, uint8_t* base);  // KeDelayExecutionThread wrapper
extern "C" void __imp__sub_82CBD098(PPCContext& ctx, uint8_t* base);  // NtYieldExecution wrapper
extern "C" void __imp__sub_83004C20(PPCContext& ctx, uint8_t* base);  // NtSetTimerEx wrapper
extern "C" void __imp__sub_83004C90(PPCContext& ctx, uint8_t* base);  // NtCreateTimer wrapper
extern "C" void __imp__sub_82BA32C8(PPCContext& ctx, uint8_t* base);  // per-frame helper (calls the frame-wait)
extern "C" void __imp__sub_82BA2568(PPCContext& ctx, uint8_t* base);  // frame-wait (KeQuerySystemTime poller)

struct Probe {
  const char* name;
  std::atomic<uint64_t> calls{0};
  std::atomic<int64_t> last_us{0};
  std::atomic<int64_t> min_us{INT64_MAX};
  std::atomic<int64_t> max_us{0};
  std::atomic<int> logged{0};
};

inline std::ofstream& log_file() {
  static std::ofstream f;
  static bool ok = [] {
    f.open("fps_probe.log", std::ios::trunc);
    return f.is_open();
  }();
  (void)ok;
  return f;
}

inline void note(Probe& p, PPCContext& ctx) {
  static std::mutex m;
  int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::steady_clock::now().time_since_epoch()).count();
  uint64_t n = p.calls.fetch_add(1) + 1;
  int64_t last = p.last_us.exchange(us);
  std::string line;
  if (last != 0) {
    int64_t d = us - last;
    for (int64_t cur = p.min_us.load(); d < cur; )
      if (p.min_us.compare_exchange_weak(cur, d)) break;
    for (int64_t cur = p.max_us.load(); d > cur; )
      if (p.max_us.compare_exchange_weak(cur, d)) break;
    if (p.logged.fetch_add(1) < 80) {
      char buf[256];
      snprintf(buf, sizeof(buf), "%s #%llu d=%.3fms r3=%08X r4=%08X r5=%08X r6=%08X\n",
               p.name, (unsigned long long)n, d / 1000.0, ctx.r3.u32, ctx.r4.u32,
               ctx.r5.u32, ctx.r6.u32);
      line = buf;
    } else if (n % 300 == 0) {
      char buf[256];
      snprintf(buf, sizeof(buf), "%s summary #%llu min=%.3fms max=%.3fms\n", p.name,
               (unsigned long long)n, p.min_us.load() / 1000.0, p.max_us.load() / 1000.0);
      line = buf;
    }
  }
  if (!line.empty()) {
    std::lock_guard<std::mutex> l(m);
    log_file() << line;
    log_file().flush();
  }
}

inline Probe probe_main_loop{"mainloop 82B9CD68", 0, 0, INT64_MAX, 0, 0};
inline Probe probe_ke_delay{"ke_delay 82CC2028", 0, 0, INT64_MAX, 0, 0};
inline Probe probe_yield{"yield 82CBD098", 0, 0, INT64_MAX, 0, 0};
inline Probe probe_nt_set_timer{"nt_set_timer 83004C20", 0, 0, INT64_MAX, 0, 0};
inline Probe probe_nt_create_timer{"nt_create_timer 83004C90", 0, 0, INT64_MAX, 0, 0};
inline Probe probe_frame_helper{"frame_helper 82BA32C8", 0, 0, INT64_MAX, 0, 0};
inline Probe probe_frame_wait{"frame_wait 82BA2568", 0, 0, INT64_MAX, 0, 0};

}  // namespace fable2::fpsprobe

// Strong overrides: every generated call site of sub_X now lands here.
extern "C" void sub_82B9CD68(PPCContext& ctx, uint8_t* base) {
  fable2::fpsprobe::note(fable2::fpsprobe::probe_main_loop, ctx);
  __imp__sub_82B9CD68(ctx, base);
}
extern "C" void sub_82CC2028(PPCContext& ctx, uint8_t* base) {
  fable2::fpsprobe::note(fable2::fpsprobe::probe_ke_delay, ctx);
  __imp__sub_82CC2028(ctx, base);
}
extern "C" void sub_82CBD098(PPCContext& ctx, uint8_t* base) {
  fable2::fpsprobe::note(fable2::fpsprobe::probe_yield, ctx);
  __imp__sub_82CBD098(ctx, base);
}
extern "C" void sub_83004C20(PPCContext& ctx, uint8_t* base) {
  fable2::fpsprobe::note(fable2::fpsprobe::probe_nt_set_timer, ctx);
  __imp__sub_83004C20(ctx, base);
}
extern "C" void sub_83004C90(PPCContext& ctx, uint8_t* base) {
  fable2::fpsprobe::note(fable2::fpsprobe::probe_nt_create_timer, ctx);
  __imp__sub_83004C90(ctx, base);
}
extern "C" void sub_82BA32C8(PPCContext& ctx, uint8_t* base) {
  fable2::fpsprobe::note(fable2::fpsprobe::probe_frame_helper, ctx);
  __imp__sub_82BA32C8(ctx, base);
}
extern "C" void sub_82BA2568(PPCContext& ctx, uint8_t* base) {
  fable2::fpsprobe::note(fable2::fpsprobe::probe_frame_wait, ctx);
  __imp__sub_82BA2568(ctx, base);
}
