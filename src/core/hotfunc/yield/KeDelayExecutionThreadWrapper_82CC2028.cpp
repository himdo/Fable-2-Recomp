// Probe: the render thread's wait-loop sleep.
//
// The white-screen freeze lives in sub_82C08310 (a wait loop on the render
// thread). Each iteration it calls ByteValueCheck_82CBC678, which:
//   1. masks r4 to a byte (the wait "flag"),
//   2. calls KeDelayExecutionThreadWrapper_82CC2028 (the actual sleep/wait),
//   3. returns 0xC0 if the wait returned 0xC0, else 0.
// The main loop keeps looping while the sleep returns 0xC0 and exits when it
// returns anything else (the waited-on event got signalled). In the freeze the
// wait keeps returning 0xC0 forever, so the loop never exits and no frames are
// rendered (white screen).
//
// This override wraps the original and, for the render-loop path (the
// recompiled ByteValueCheck sets ctx.lr = 0x82CBC68C right before calling us),
// rate-capped-logs the wait's timeout, flag, and return status plus the host
// thread id. Reading ctx registers is safe (no guest RAM touched); output is
// buffered and rate-capped so it does not perturb the render thread. Disable
// with FABLE2_SLEEP_PROBE=0.

#include "fable_2_pch.h"

#ifdef _WIN32
#include <windows.h>
#endif

extern "C" void __imp__KeDelayExecutionThreadWrapper_82CC2028(PPCContext& ctx, uint8_t* base);

namespace fable2::sleepprobe {

inline bool enabled() {
  static const bool on = [] {
    const char* v = std::getenv("FABLE2_SLEEP_PROBE");
    return v == nullptr || v[0] != '0';
  }();
  return on;
}

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

extern "C" void KeDelayExecutionThreadWrapper_82CC2028(PPCContext& ctx, uint8_t* base) {
  // The render-loop sleep path: ByteValueCheck sets the guest LR to 0x82CBC68C
  // (the return address after its `bl 0x82cc2028`) immediately before calling
  // us. Any other caller has a different LR and passes through untouched.
  const bool is_render_loop = (ctx.lr == 0x82CBC68C);
  const uint32_t timeout_in = ctx.r3.u32;  // wait duration (ms, pre-conversion)
  const uint32_t flag_in = ctx.r4.u32;     // the wait "flag" (masked to a byte)
  __imp__KeDelayExecutionThreadWrapper_82CC2028(ctx, base);
  const uint32_t ret = ctx.r3.u32;         // 0xC0 = keep waiting, 0 = exit

  if (!is_render_loop || !fable2::sleepprobe::enabled()) return;

  // Per-thread rate cap (~10/s) so the log stays small and unperturbed.
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
  std::fprintf(f,
               "tid=%lu n=%u lr=%08X timeout_ms=%u flag=0x%08X ret=0x%08X %s\n",
               (unsigned long)tid, n++, (unsigned)ctx.lr, timeout_in, flag_in, ret,
               ret == 0xC0 ? "(keep waiting)" : "(EXIT)");
  std::fflush(f);
}
