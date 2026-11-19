// Synthetic standalone test: no SDL, game process, guest memory or audio device.
#include <rex/audio/clocked_audio_sink.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "catch_amalgamated.hpp"

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

// Waits up to 2 s for `done`, polling every millisecond.
template <typename Done>
static void WaitFor(Done done) {
  const auto start = Clock::now();
  while (!done() && Clock::now() - start < 2s) std::this_thread::sleep_for(1ms);
}

TEST_CASE("ClockedAudioSink", "[audio]") {
  std::atomic<unsigned> consumed = 0;
  rex::audio::ClockedAudioSink sink([&] { ++consumed; });

  SECTION("refuses a frame before Start, and a second Start") {
    CHECK_FALSE(sink.Submit());
    CHECK(sink.Start());
    CHECK_FALSE(sink.Start());
    sink.Stop();
  }

  SECTION("hands out no permits while idle") {
    REQUIRE(sink.Start());
    std::this_thread::sleep_for(20ms);
    CHECK(consumed == 0u);
    sink.Stop();
  }

  SECTION("paces eight frames over at least 35 ms") {
    REQUIRE(sink.Start());
    const auto start = Clock::now();
    for (int i = 0; i < 8; ++i) CHECK(sink.Submit());
    WaitFor([&] { return consumed >= 8; });
    const auto elapsed = Clock::now() - start;
    CHECK(consumed == 8u);
    CHECK(elapsed >= 35ms);
    sink.Stop();
  }

  SECTION("stops without a late callback and refuses frames until restarted") {
    REQUIRE(sink.Start());
    REQUIRE(sink.Submit());
    WaitFor([&] { return consumed >= 1; });
    sink.Stop();
    const unsigned stopped = consumed;
    std::this_thread::sleep_for(20ms);
    CHECK(consumed == stopped);
    CHECK_FALSE(sink.Submit());
    CHECK(sink.Start());
    CHECK(sink.Submit());
    sink.Stop();  // a pending frame: joins, discards the queue, no late callback
    const unsigned after = consumed;
    std::this_thread::sleep_for(20ms);
    CHECK(consumed == after);
  }

  SECTION("runs two sinks independently") {
    std::atomic<unsigned> second_count = 0;
    rex::audio::ClockedAudioSink second([&] { ++second_count; });
    REQUIRE(sink.Start());
    REQUIRE(second.Start());
    CHECK(sink.Submit());
    CHECK(second.Submit());
    WaitFor([&] { return consumed >= 1 && second_count >= 1; });
    sink.Stop();
    second.Stop();
    CHECK(consumed == 1u);
    CHECK(second_count == 1u);
  }
}
