#pragma once
#include <cstdint>
#include <optional>
namespace fable2::measurement {
struct Report { double seconds, rate, max_gap_ms; uint64_t total; };
// One counter per calling render thread. Counts elapsed frame intervals,
// excluding the initial timestamp; never emits an empty/first-frame window.
struct RateWindow {
  bool started=false;
  int64_t start=0,last=0,max_gap=0;
  uint64_t intervals=0,total=0;
  std::optional<Report> tick(int64_t us) {
    ++total;
    if(!started || us<last) {
      started=true;start=last=us;intervals=0;max_gap=0;return {};
    }
    auto gap=us-last;last=us;
    if(gap>max_gap) max_gap=gap;
    ++intervals;
    if(us-start<5'000'000) return {};
    double seconds=double(us-start)/1e6;
    Report result{seconds,intervals/seconds,max_gap/1000.0,total};
    start=us;intervals=0;max_gap=0;
    return result;
  }
};
}
