#include "rex/graphics/metal/metal_pipeline_failure_backoff.h"

#include <cassert>
#include <chrono>
#include <iostream>

using rex::graphics::metal::PipelineFailureBackoff;
using namespace std::chrono_literals;

int main() {
  PipelineFailureBackoff retry;
  const PipelineFailureBackoff::Clock::time_point start{};
  constexpr uint64_t broken = 1, healthy = 2;

  // A persistent compiler failure under a 1,000-draws/second workload must not
  // keep invoking the compiler, or stall independent, previously unseen draws.
  unsigned attempts = 0;
  for (int ms = 0; ms < 100000; ++ms) {
    auto now = start + std::chrono::milliseconds(ms);
    assert(retry.CanAttempt(healthy, now));
    if (retry.CanAttempt(broken, now)) {
      ++attempts;
      retry.RecordFailure(broken, now);
    }
  }
  assert(attempts == 8);

  // A different failing pipeline gets its own deadline. Retry exactly at the
  // boundary, and retain retries indefinitely even after reaching the cap.
  const auto end = start + 100s;
  assert(retry.RecordFailure(healthy, end) == 1s);
  assert(!retry.CanAttempt(healthy, end + 999ms));
  assert(retry.CanAttempt(healthy, end + 1s));
  assert(!retry.CanAttempt(broken, start + 121s - 1ns));
  assert(retry.CanAttempt(broken, start + 121s));
  assert(retry.RecordFailure(broken, start + 121s) == 30s);
  assert(retry.CanAttempt(broken, start + 151s));

  // If a retry succeeds, a later new failure begins at the short delay again.
  retry.RecordSuccess(broken);
  assert(retry.CanAttempt(broken, start + 151s));
  assert(retry.RecordFailure(broken, start + 151s) == 1s);

  // Cache teardown (for example a renderer restart) forgets previous failures.
  PipelineFailureBackoff fresh;
  assert(fresh.CanAttempt(broken, start));

  std::cout << "PASS: 100,000 repeated failed draws caused " << attempts
            << " compiler attempts over 100 simulated seconds; independent "
               "keys, retry boundaries, cap, recovery, and restart passed.\n";
}
