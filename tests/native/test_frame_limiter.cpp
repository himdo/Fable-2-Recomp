// Synthetic standalone test; no game or GPU.
#include <rex/graphics/frame_limiter.h>

#include <chrono>
#include <thread>

#include "catch_amalgamated.hpp"

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

TEST_CASE("FrameLimiter", "[graphics]") {
  rex::graphics::FrameLimiter limiter;

  SECTION("caps 30, 60 and 144 FPS") {
    for (int rate : {30, 60, 144}) {
      CAPTURE(rate);
      limiter.Pace(0);
      const auto start = Clock::now();
      for (int i = 0; i < 7; ++i) limiter.Pace(rate);
      const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
      CHECK(seconds >= 5.9 / rate);  // six intervals
    }
  }

  SECTION("never waits when disabled, out of range, or right after a rate change") {
    for (int rate : {0, -1, 241, 30, 0, 60, 30, 60}) {
      CAPTURE(rate);
      const auto start = Clock::now();
      limiter.Pace(rate);
      CHECK(Clock::now() - start <= 15ms);
    }
  }

  SECTION("doesn't catch up with a burst after a late frame") {
    limiter.Pace(60);
    std::this_thread::sleep_for(100ms);
    limiter.Pace(60);
    const auto start = Clock::now();
    limiter.Pace(60);
    CHECK(Clock::now() - start >= 15ms);
  }
}
