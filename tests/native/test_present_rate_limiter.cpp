// Synthetic standalone test; no game or GPU.
#include <rex/ui/present_rate_limiter.h>

#include <chrono>

#include "catch_amalgamated.hpp"

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

TEST_CASE("PresentRateLimiter", "[ui]") {
  const auto start = PresentRateLimiter::Clock::time_point{} + 100s;
  PresentRateLimiter limiter;

  SECTION("caps 30 to 240 FPS to within one present") {
    for (int fps : {30, 60, 120, 144, 165, 240}) {
      CAPTURE(fps);
      PresentRateLimiter fresh;
      const int presents = PresentsInOneSecond(fresh, fps, 100us, start);
      CHECK(presents >= fps - 1);
      CHECK(presents <= fps + 1);
    }
  }

  SECTION("doesn't thin out a source just under the cap that jitters on the grid") {
    int presents = 0;
    auto now = start;
    for (int i = 0; i < 60; ++i, now += 16667us) {
      const auto jittered = now + (i % 2 ? 1ms : -1ms);
      if (limiter.Allows(60, jittered)) {
        limiter.NotePresent(60, jittered);
        ++presents;
      }
    }
    CHECK(presents >= 59);
  }

  SECTION("never skips with a limit of 0, below 0 or out of range") {
    for (int fps : {0, -1, PresentRateLimiter::kMaxFramesPerSecond + 1}) {
      CAPTURE(fps);
      PresentRateLimiter fresh;
      CHECK(PresentsInOneSecond(fresh, fps, 1ms, start) == 1000);
    }
  }

  SECTION("restarts the grid after a long gap: one present, then the normal spacing") {
    PresentsInOneSecond(limiter, 60, 100us, start);
    const auto later = start + 10s;
    CHECK(limiter.Allows(60, later));
    limiter.NotePresent(60, later);
    CHECK_FALSE(limiter.Allows(60, later + 1ms));
  }

  SECTION("takes a raised cap within one new period") {
    limiter.NotePresent(30, start);
    limiter.NotePresent(240, start);
    CHECK(limiter.Allows(240, start + 5ms));
  }
}
