// Synthetic timestamp test: no game, GPU or timing-sensitive sleeps.
#include <rex/graphics/guest_frame_meter.h>

#include <cstdlib>

#include "catch_amalgamated.hpp"

using Meter = rex::graphics::GuestFrameMeter;
using namespace std::chrono;

TEST_CASE("GuestFrameMeter", "[graphics]") {
  const auto epoch = Meter::Clock::time_point{};

  SECTION("averages the guest interval at 30 to 240 FPS") {
    for (int rate : {30, 60, 120, 144, 165, 240}) {
      CAPTURE(rate);
      Meter meter;
      CHECK(meter.Record(epoch) == 0);
      int64_t mean = 0;
      for (int i = 1; i <= rate; ++i)
        mean = meter.Record(epoch + microseconds(int64_t(i) * 1000000 / rate));
      CHECK(std::abs(mean - 1000000 / rate) <= 1);
    }
  }

  SECTION("includes a stall") {
    Meter stalled;
    stalled.Record(epoch);
    CHECK(stalled.Record(epoch + seconds(2)) == 2000000);
  }

  SECTION("keeps separate meters apart") {
    Meter stalled, independent;
    stalled.Record(epoch);
    stalled.Record(epoch + seconds(2));
    CHECK(independent.Record(epoch + seconds(2)) == 0);
  }
}
