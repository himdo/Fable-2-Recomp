// Probe: the render-loop periodic-timer object.
//
// CheckTimeoutCondition_82C08010 is called once per iteration of the render
// thread's wait loop (sub_82C08310). It takes the timer object in r3, reads
// obj->0x18 (the period), calls GetCurrentTimeMillis, and returns 1 (a tick
// fired) when current_time >= obj->0x20 (the deadline), advancing
// obj->0x20 += obj->0x18 in that case. This function has exactly one caller
// (the render loop), so wrapping it is specific to that loop.
//
// We dump the object pointer and its period/deadline fields plus the return,
// rate-capped, so we can see whether the deadline is being reached (the timer
// working) or running away. The guest reads are guarded to plausible heap
// addresses (the object is a guest heap pointer) so a bad value cannot fault
// the render thread. Disable with FABLE2_SLEEP_PROBE=0.

#include "fable_2_pch.h"

#ifdef _WIN32
#include <windows.h>
#endif

extern "C" void __imp__CheckTimeoutCondition_82C08010(PPCContext& ctx, uint8_t* base);

namespace fable2::sleepprobe {
// Shared with the sleep probe (single log file for the wait-loop state).
inline FILE* log() {
  static FILE* f = [] {
    FILE* out = nullptr;
#ifdef _WIN32
    if (::fopen_s(&out, "fable2_sleep_probe.log", "w") != 0) out = nullptr;
#else
    out = std::fopen("fable2_sleep_probe.log", "w");
#endif
    if (out) {
      static char buf[1 << 16];
      std::setvbuf(out, buf, _IOFBF, sizeof(buf));
    }
    return out;
  }();
  return f;
}
}  // namespace fable2::sleepprobe

extern "C" void CheckTimeoutCondition_82C08010(PPCContext& ctx, uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;  // timer object pointer (guest address)
  __imp__CheckTimeoutCondition_82C08010(ctx, base);
  const uint32_t result = ctx.r3.u32 & 0xFF;  // 1 = tick fired, 0 = not yet

  // FABLE2_SLEEP_PROBE=0 disables both wait-loop probes.
  if (const char* v = std::getenv("FABLE2_SLEEP_PROBE"); v && v[0] == '0') return;

  static thread_local int64_t last_ms = 0;
  static thread_local uint32_t n = 0;
#ifdef _WIN32
  const int64_t now_ms = (int64_t)GetTickCount64();
  const DWORD tid = GetCurrentThreadId();
#else
  const int64_t now_ms = 0;
  const DWORD tid = 0;
#endif
  if (now_ms - last_ms < 100) return;
  last_ms = now_ms;
  FILE* f = fable2::sleepprobe::log();
  if (!f) return;

  uint32_t period = 0, t1 = 0, deadline = 0;
  // Only dereference when obj is a plausible guest heap pointer (the arena and
  // large-address heap), so a garbage value cannot fault this thread.
  if (obj > 0x00400000u && obj < 0x9FFFFFFFu) {
    period = REX_LOAD_U32(obj + 0x18);
    t1 = REX_LOAD_U32(obj + 0x1c);
    deadline = REX_LOAD_U32(obj + 0x20);
  }
  std::fprintf(f,
               "tid=%lu n=%u CHECK_TIMEOUT obj=%08X period=0x%08X f1c=0x%08X "
               "deadline=0x%08X ret=%u %s\n",
               (unsigned long)tid, n++, obj, period, t1, deadline, result,
               result ? "(tick)" : "(not yet)");
  std::fflush(f);
}
