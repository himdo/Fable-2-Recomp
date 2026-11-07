// Synthetic standalone test; no game or GPU.
#include <rex/ui/present_rate_limiter.h>
#include <chrono>
#include <iostream>

using rex::ui::PresentRateLimiter;
using namespace std::chrono_literals;

// Offers a present every `step` for one second and counts the ones let through.
static int PresentsInOneSecond(PresentRateLimiter& limiter, int fps,
                               PresentRateLimiter::Clock::duration step,
                               PresentRateLimiter::Clock::time_point start) {
  int presents = 0;
  for (auto now = start; now < start + 1s; now += step) {
    if (limiter.Allows(fps, now)) {
      limiter.NotePresent(fps, now);
      ++presents;
    }
  }
  return presents;
}

int main() {
  const auto start = PresentRateLimiter::Clock::time_point{} + 100s;
  for (int fps : {30, 60, 120, 144, 165, 240}) {
    PresentRateLimiter limiter;
    int presents = PresentsInOneSecond(limiter, fps, 100us, start);
    if (presents < fps - 1 || presents > fps + 1) return 1;
    std::cout << fps << " FPS cap: " << presents << " presents\n";
  }

  // A source just under the cap is not thinned out by jitter on the grid.
  {
    PresentRateLimiter limiter;
    int presents = 0;
    auto now = start;
    for (int i = 0; i < 60; ++i, now += 16667us) {
      auto jittered = now + (i % 2 ? 1ms : -1ms);
      if (limiter.Allows(60, jittered)) {
        limiter.NotePresent(60, jittered);
        ++presents;
      }
    }
    if (presents < 59) return 2;
  }

  // 0, negative and out-of-range limits never skip.
  for (int fps : {0, -1, PresentRateLimiter::kMaxFramesPerSecond + 1}) {
    PresentRateLimiter limiter;
    if (PresentsInOneSecond(limiter, fps, 1ms, start) != 1000) return 3;
  }

  // After a long gap the grid restarts: one present, then the normal spacing.
  {
    PresentRateLimiter limiter;
    PresentsInOneSecond(limiter, 60, 100us, start);
    auto later = start + 10s;
    if (!limiter.Allows(60, later)) return 4;
    limiter.NotePresent(60, later);
    if (limiter.Allows(60, later + 1ms)) return 5;
  }

  // Raising the cap takes effect within one new period.
  {
    PresentRateLimiter limiter;
    limiter.NotePresent(30, start);
    limiter.NotePresent(240, start);
    if (!limiter.Allows(240, start + 5ms)) return 6;
  }

  std::cout << "PASS: caps, jitter, disabled mode, late reset and rate change\n";
}
