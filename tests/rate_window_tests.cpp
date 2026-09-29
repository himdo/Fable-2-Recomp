#include "rate_window.h"
#include <cassert>
#include <cmath>
using fable2::measurement::RateWindow;
int main() {
  RateWindow w;
  assert(!w.tick(0));
  for(int i=1;i<300;++i) assert(!w.tick((int64_t(i)*5'000'000)/300));
  auto a=w.tick(5'000'000);assert(a && std::abs(a->rate-60)<1e-9 && a->total==301);
  for(int i=1;i<300;++i) assert(!w.tick(5'000'000+(int64_t(i)*5'000'000)/300));
  auto b=w.tick(10'000'000);assert(b && std::abs(b->rate-60)<1e-9 && b->total==601);
  auto stall=w.tick(16'000'000);assert(stall && stall->max_gap_ms==6000 && std::abs(stall->rate-1.0/6)<1e-9);
  assert(!w.tick(1));assert(!w.tick(2)); // clock reset does not emit bogus rates
}
