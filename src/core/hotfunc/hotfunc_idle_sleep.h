// hotfunc_idle_sleep.h - short host sleep for guest busy-wait loops.
//
// Used by the idle-spin backoff (hotfunc_yield.h) and the frame limiter
// override (vblank/FrameLimiterWait_82242628.cpp), both driven by
// [perf] idle_yield_sleep_us. The requested sleeps are well under a
// millisecond, which the two platforms handle very differently:
//
//   * POSIX: nanosleep honours sub-millisecond requests (plus roughly 50 us of
//     timer slack), so std::this_thread::sleep_for is used as is.
//   * Windows: Sleep() and std::this_thread::sleep_for round up to the system
//     timer tick, which is up to 15.6 ms unless something raised the timer
//     resolution - longer than a whole 60 fps frame. A high-resolution
//     waitable timer (Windows 10 1803 and later) is used instead; it wakes
//     within about 0.5 ms whatever the system timer resolution is, so a
//     request shorter than that still sleeps about 0.5 ms.
//
// IdleSleepMicros returns false, without sleeping, when no short sleep is
// available (Windows older than 10 1803). Callers then keep their original
// spin or yield rather than risk a tick-length sleep.

#pragma once

#include <cstdint>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// Defined by the Windows 10 1803 SDK and later.
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
#else
#include <chrono>
#include <thread>
#endif

namespace fable2::hotfunc {

#ifdef _WIN32

namespace detail {
// One timer per thread: a waitable timer has a single due time, so sharing one
// between guest threads would let them cut each other's sleeps short.
struct IdleSleepTimer {
  HANDLE handle = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                         TIMER_ALL_ACCESS);
  ~IdleSleepTimer() {
    if (handle) {
      CloseHandle(handle);
    }
  }
};
}  // namespace detail

inline bool IdleSleepMicros(int32_t sleep_us) {
  static thread_local detail::IdleSleepTimer timer;
  if (!timer.handle) {
    return false;
  }
  LARGE_INTEGER due_time;
  due_time.QuadPart = -static_cast<LONGLONG>(sleep_us) * 10;  // relative, 100 ns units
  if (!SetWaitableTimerEx(timer.handle, &due_time, 0, nullptr, nullptr, nullptr, 0)) {
    return false;
  }
  WaitForSingleObject(timer.handle, INFINITE);
  return true;
}

#else

inline bool IdleSleepMicros(int32_t sleep_us) {
  std::this_thread::sleep_for(std::chrono::microseconds(sleep_us));
  return true;
}

#endif

}  // namespace fable2::hotfunc
