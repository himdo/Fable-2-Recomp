#ifndef REX_GRAPHICS_METAL_PIPELINE_FAILURE_BACKOFF_H_
#define REX_GRAPHICS_METAL_PIPELINE_FAILURE_BACKOFF_H_

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <unordered_map>

namespace rex::graphics::metal {

// Used only on the GPU command thread. A failed pipeline may recover, so retry
// it periodically rather than permanently caching a failure or rebuilding on
// every draw. Successful pipelines stay in the normal pipeline cache.
class PipelineFailureBackoff {
 public:
  using Clock = std::chrono::steady_clock;

  bool CanAttempt(uint64_t key, Clock::time_point now) const {
    auto it = failures_.find(key);
    return it == failures_.end() || now >= it->second.retry_at;
  }

  // Schedule from the end of the failed attempt, including slow compilations.
  std::chrono::seconds RecordFailure(uint64_t key, Clock::time_point now) {
    auto& failure = failures_[key];
    failure.delay = failure.delay.count()
                        ? std::min(failure.delay * 2, std::chrono::seconds(30))
                        : std::chrono::seconds(1);
    failure.retry_at = now + failure.delay;
    return failure.delay;
  }

  void RecordSuccess(uint64_t key) { failures_.erase(key); }

 private:
  struct Failure {
    Clock::time_point retry_at{};
    std::chrono::seconds delay{0};
  };
  std::unordered_map<uint64_t, Failure> failures_;
};

}  // namespace rex::graphics::metal

#endif  // REX_GRAPHICS_METAL_PIPELINE_FAILURE_BACKOFF_H_
