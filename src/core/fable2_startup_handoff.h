#pragma once

#include <cstdint>

#if defined(__APPLE__)
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>
#endif

namespace fable2::startup {
#if defined(__APPLE__)
// Limit the workaround to the observed startup lock and loading period.
inline std::atomic<uint32_t> watched_lock{0};
inline const auto start = std::chrono::steady_clock::now();
#endif

struct LockCall {
  LockCall(uint32_t address, uint32_t caller, uint32_t, uint8_t*) {
#if defined(__APPLE__)
    if (caller == 0x8237834C || caller == 0x8236CB68) {
      watched_lock.store(address, std::memory_order_relaxed);
    }
#endif
  }
};

template <class Context, class Func>
inline void release(Context& ctx, uint8_t* base, Func fn) {
#if defined(__APPLE__)
  static const bool handoff = [] {
    const char* value = std::getenv("FABLE_STARTUP_LOCK_HANDOFF");
    return value && std::strcmp(value, "1") == 0;
  }();
  bool pause = false;
  if (handoff && ctx.lr == 0x8236C4D0 &&
      ctx.r3.u32 == watched_lock.load(std::memory_order_relaxed) &&
      std::chrono::steady_clock::now() - start < std::chrono::seconds(180)) {
    int32_t count;
    std::memcpy(&count, base + ctx.r3.u32 + 16, sizeof(count));
    pause = REX_LOAD_U32(ctx.r3.u32 + 20) == 1 && count > 0;
  }
  // Release normally first, then let the already-waiting thread acquire it.
  fn(ctx, base);
  if (pause) {
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }
#else
  fn(ctx, base);
#endif
}
}  // namespace fable2::startup
