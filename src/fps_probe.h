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

#include <windows.h>

namespace fable2::fpsprobe {

// Always-original entry points (strong symbols in the generated code).
extern "C" void __imp__sub_82B9CD68(PPCContext& ctx, uint8_t* base);  // main loop (calls VdSwap)
extern "C" void __imp__sub_82CC2028(PPCContext& ctx, uint8_t* base);  // KeDelayExecutionThread wrapper
extern "C" void __imp__sub_82CBD098(PPCContext& ctx, uint8_t* base);  // NtYieldExecution wrapper
extern "C" void __imp__sub_83004C20(PPCContext& ctx, uint8_t* base);  // NtSetTimerEx wrapper
extern "C" void __imp__sub_83004C90(PPCContext& ctx, uint8_t* base);  // NtCreateTimer wrapper
extern "C" void __imp__sub_82BA32C8(PPCContext& ctx, uint8_t* base);  // per-frame helper (calls the frame-wait)
extern "C" void __imp__sub_82BA2568(PPCContext& ctx, uint8_t* base);  // frame-wait (KeQuerySystemTime poller)
extern "C" void __imp__sub_82B9B8D8(PPCContext& ctx, uint8_t* base);  // vblank/GPU interrupt callback
extern "C" void __imp__sub_82CC2948(PPCContext& ctx, uint8_t* base);  // GPU CPU-interrupt cb: KeSetEvent (render thread waker?)
extern "C" void __imp__sub_82B9F598(PPCContext& ctx, uint8_t* base);  // main render thread loop (KeWaitForSingleObject + render)
// Helpers called directly by the main render loop - measured to find the 33ms wait.
extern "C" void __imp__sub_82BA6F08(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_82BA8630(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_821D17B8(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_82242628(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_82B9BF90(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_82B9BEC8(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_82BA8350(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_822A6318(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_82CA3190(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_82B9CB88(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_82B9C7F8(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_82B9C9D8(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_821E8D20(PPCContext& ctx, uint8_t* base);

struct Probe {
  const char* name;
  std::atomic<uint64_t> calls{0};
  std::atomic<int64_t> last_us{0};
  std::atomic<int64_t> min_us{INT64_MAX};
  std::atomic<int64_t> max_us{0};
  std::atomic<int64_t> max_dur_us{0};
  std::atomic<int> logged{0};
  std::atomic<int64_t> in_flight_us{0};
};

inline std::mutex& log_mutex() {
  static std::mutex m;
  return m;
}

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
  std::mutex& m = log_mutex();
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
      // Rate between consecutive summary lines (300 calls each).
      static thread_local uint64_t prev_n = 0;
      static thread_local int64_t prev_us = 0;
      double rate = 0.0;
      if (prev_us != 0) {
        int64_t dt_us = us - prev_us;
        if (dt_us > 0) rate = 300.0 / (dt_us / 1000000.0);
      }
      prev_n = n;
      prev_us = us;
      char buf[256];
      snprintf(buf, sizeof(buf),
               "%s summary #%llu rate=%.1f/s min=%.3fms max=%.3fms maxdur=%.3fms\n",
               p.name, (unsigned long long)n, rate, p.min_us.load() / 1000.0,
               p.max_us.load() / 1000.0, p.max_dur_us.load() / 1000.0);
      line = buf;
    }
  }
  if (!line.empty()) {
    std::lock_guard<std::mutex> l(m);
    log_file() << line;
    log_file().flush();
  }
}

inline Probe probe_main_loop{"mainloop 82B9CD68"};
inline Probe probe_ke_delay{"ke_delay 82CC2028"};
inline Probe probe_yield{"yield 82CBD098"};
inline Probe probe_nt_set_timer{"nt_set_timer 83004C20"};
inline Probe probe_nt_create_timer{"nt_create_timer 83004C90"};
inline Probe probe_frame_helper{"frame_helper 82BA32C8"};
inline Probe probe_frame_wait{"frame_wait 82BA2568"};
inline Probe probe_vblank{"vblank_cb 82B9B8D8"};
inline Probe probe_gpu_cb{"gpu_cb 82CC2948"};
inline Probe probe_render_thread{"render_thread 82B9F598"};
inline Probe probe_h1{"h 82BA6F08"};
inline Probe probe_h2{"h 82BA8630"};
inline Probe probe_h3{"h 821D17B8"};
inline Probe probe_h4{"h 82242628"};
inline Probe probe_h5{"h 82B9BF90"};
inline Probe probe_h6{"h 82B9BEC8"};
inline Probe probe_h7{"h 82BA8350"};
inline Probe probe_h8{"h 822A6318"};
inline Probe probe_h9{"h 82CA3190"};
inline Probe probe_h10{"h 82B9CB88"};
inline Probe probe_h11{"h 82B9C7F8"};
inline Probe probe_h12{"h 82B9C9D8"};
inline Probe probe_h13{"h 821E8D20"};

inline void note_exit(Probe& p) {
  int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::steady_clock::now().time_since_epoch()).count();
  int64_t entered = p.in_flight_us.exchange(0);
  if (entered == 0) return;
  int64_t dur = us - entered;
  for (int64_t cur = p.max_dur_us.load(); dur > cur; )
    if (p.max_dur_us.compare_exchange_weak(cur, dur)) break;
  if (dur > 8000) {
    std::lock_guard<std::mutex> l(log_mutex());
    char buf[160];
    snprintf(buf, sizeof(buf), "%s SLOW_CALL dur=%.3fms\n", p.name, dur / 1000.0);
    log_file() << buf;
    log_file().flush();
  }
}

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
extern "C" void sub_82B9B8D8(PPCContext& ctx, uint8_t* base) {
  fable2::fpsprobe::note(fable2::fpsprobe::probe_vblank, ctx);
  __imp__sub_82B9B8D8(ctx, base);
}
extern "C" void sub_82CC2948(PPCContext& ctx, uint8_t* base) {
  fable2::fpsprobe::note(fable2::fpsprobe::probe_gpu_cb, ctx);
  __imp__sub_82CC2948(ctx, base);
}
extern "C" void sub_82B9F598(PPCContext& ctx, uint8_t* base) {
  fable2::fpsprobe::note(fable2::fpsprobe::probe_render_thread, ctx);
  __imp__sub_82B9F598(ctx, base);
}

// ---- Limiter context inspection ----
// sub_82242628(ctx, base) = limiter_wait(limit*, now, frame_len, flag).
// limit*+8 = frame start, limit*+12 = deadline. Log both per call.
extern "C" void sub_82242628(PPCContext& ctx, uint8_t* base) {
  using namespace fable2::fpsprobe;
  note(probe_h4, ctx);
  try {
    // At entry: r31 = limit*, r30 = frame-counter "now", r5 = flag.
    // Read the deadline (limit*+12) BEFORE calling the original so we can
    // see the real value the spin loop is waiting on.
    auto read32 = [&](uint32_t g) -> uint32_t {
      if (g == 0) return 0xFFFFFFFF;
      uint32_t off = (g >= 0xE0000000u) ? 0x1000u : 0u;
      volatile uint32_t* p = reinterpret_cast<volatile uint32_t*>(base + g + off);
      MEMORY_BASIC_INFORMATION mbi{};
      if (::VirtualQuery(const_cast<void*>(reinterpret_cast<volatile void*>(p)), &mbi,
                         sizeof(mbi)) == 0)
        return 0xFFFFFFFF;
      if (mbi.State != MEM_COMMIT) return 0xFFFFFFFF;
      return __builtin_bswap32(*p);
    };
    uint32_t lp = ctx.r3.u32;   // limit* (r3 at entry; r31 set inside)
    uint32_t now4 = ctx.r4.u32;
    uint32_t f10896 = read32(lp + 10896);  // ptr to global frame counter
    uint32_t f10908 = read32(lp + 10908);  // deadline / frame tick
    uint32_t gfc = (f10896 == 0) ? 0xFFFFFFFF : read32(f10896);  // deref
    // Track the counter's real advance rate + the limiter's wall wait.
    static uint32_t last_gfc = 0;
    static double last_t = 0.0;
    double t_now = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    if (last_t > 0.0 && gfc != 0xFFFFFFFF) {
      double dt = (t_now - last_t) * 1000.0;
      int64_t dg = (int64_t)(gfc - last_gfc);
      if (probe_h4.logged.fetch_add(1) % 30 == 0) {
        std::lock_guard<std::mutex> l(log_mutex());
        char buf[256];
        snprintf(buf, sizeof(buf), "counter gfc=%08X dg=%lld over %.1fms (rate %.2f/s)\n",
                 gfc, dg, dt, dt > 0 ? (double)dg / (dt / 1000.0) : 0.0);
        log_file() << buf;
        log_file().flush();
      }
    }
    last_gfc = gfc;
    last_t = t_now;
    if (probe_h4.logged.fetch_add(1) < 400) {
      // dump 16 words at lp for layout discovery
      uint32_t d[16]{};
      for (int i = 0; i < 16; i++) d[i] = read32(lp + i * 4);
      std::lock_guard<std::mutex> l(log_mutex());
      char buf[600];
      snprintf(buf, sizeof(buf),
               "limiter now4=%08X f10896=%08X gfc=%08X f10908=%08X r5=%X "
               "[0..15]=%08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X\n",
               now4, f10896, gfc, f10908, ctx.r5.u32, d[0], d[1], d[2], d[3], d[4],
               d[5], d[6], d[7], d[8], d[9], d[10], d[11], d[12], d[13], d[14], d[15]);
      log_file() << buf;
      log_file().flush();
    }
  } catch (...) {}
  probe_h4.in_flight_us.store(std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
#if FPS_PROBE_NO_LIMITER
  // EXPERIMENT: skip the GPU-wait spin entirely so the main loop runs
  // uncapped. Return 1 ("ready") like the original does when the deadline
  // is already met.
  ctx.r3.s64 = 1;
  note_exit(probe_h4);
  return;
#else
  __imp__sub_82242628(ctx, base);
  note_exit(probe_h4);
#endif
}

// NOTE: the 30 fps cap is lifted at runtime with REX_VSYNC=0 (see
// tools/fable2-uncapped.cmd); no guest code patch is needed or applied here.
// This file is measurement-only. An earlier experiment patched the begin-frame
// deadline (sub_82B9BA58) to wait 1 vblank instead of 2 - superseded and
// removed; the vblank counter gate is host-side and the env var is the fix.

#define FPS_PROBE_HOOK(name, probe, orig)                                   \
  extern "C" void name(PPCContext& ctx, uint8_t* base) {                    \
    fable2::fpsprobe::note(fable2::fpsprobe::probe, ctx);                   \
    fable2::fpsprobe::probe.in_flight_us.store(                             \
        std::chrono::duration_cast<std::chrono::microseconds>(              \
            std::chrono::steady_clock::now().time_since_epoch()).count());  \
    orig(ctx, base);                                                        \
    fable2::fpsprobe::note_exit(fable2::fpsprobe::probe);                   \
  }
FPS_PROBE_HOOK(sub_82BA6F08, probe_h1, __imp__sub_82BA6F08)
FPS_PROBE_HOOK(sub_82BA8630, probe_h2, __imp__sub_82BA8630)
FPS_PROBE_HOOK(sub_821D17B8, probe_h3, __imp__sub_821D17B8)
FPS_PROBE_HOOK(sub_82B9BF90, probe_h5, __imp__sub_82B9BF90)
FPS_PROBE_HOOK(sub_82B9BEC8, probe_h6, __imp__sub_82B9BEC8)
FPS_PROBE_HOOK(sub_82BA8350, probe_h7, __imp__sub_82BA8350)
FPS_PROBE_HOOK(sub_822A6318, probe_h8, __imp__sub_822A6318)
FPS_PROBE_HOOK(sub_82CA3190, probe_h9, __imp__sub_82CA3190)
FPS_PROBE_HOOK(sub_82B9CB88, probe_h10, __imp__sub_82B9CB88)
FPS_PROBE_HOOK(sub_82B9C7F8, probe_h11, __imp__sub_82B9C7F8)
FPS_PROBE_HOOK(sub_82B9C9D8, probe_h12, __imp__sub_82B9C9D8)
FPS_PROBE_HOOK(sub_821E8D20, probe_h13, __imp__sub_821E8D20)
