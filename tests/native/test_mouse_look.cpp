// Standalone test for the mouse-look helpers (src/input/keyboard_gamepad.h and
// src/input/mouse_diag.h). No game, GPU, SDL or audio device needed.
//
// Covers:
//   - MouseToStick: scale, Y inversion, full-deflection clamping (the speed
//     cap) and that it reports when movement was lost to the clamp;
//   - InjectPlan: `f2 mouse inject` spreads the movement evenly over time and
//     always delivers exactly the requested total;
//   - FormatWindow: the per-second summary maths (rates, gap mean / sd).

#include <rex/cvar.h>  // FlagEntry (needed for the stub below)

// The single SDK symbol the real keybinds.cpp references (see
// test_keyboard_gamepad_map.cpp). The test never registers a cvar.
namespace rex::cvar {
std::optional<size_t> RegisterFlag(FlagEntry) { return {}; }
}

// Declared for keyboard_gamepad.h; the driver is never instantiated here.
namespace fable2::f5lua {
void poll_f5(bool focused);
}

#include "src/input/keyboard_gamepad.h"

#include <cstdio>
#include <cstring>
#include <string>

using fable2::input_detail::kStickMax;
using fable2::input_detail::MouseToStick;
using fable2::mousediag::FormatWindow;
using fable2::mousediag::InjectPlan;
using fable2::mousediag::Window;

static int failures = 0;
#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
      ++failures;                                                            \
    }                                                                        \
  } while (0)

static void TestMouseToStick() {
  int32_t rx = 1, ry = 1;

  // No movement: centred, nothing lost.
  CHECK(!MouseToStick(0, 0, 256, rx, ry));
  CHECK(rx == 0 && ry == 0);

  // Default scale: 10 px right, 4 px down -> +2560, -1024 (mouse-down = look down).
  CHECK(!MouseToStick(10, 4, 256, rx, ry));
  CHECK(rx == 2560 && ry == -1024);

  // Mouse up (dy < 0) is look up (+ry).
  CHECK(!MouseToStick(0, -3, 256, rx, ry));
  CHECK(rx == 0 && ry == 768);

  // 127 px fits; 128 px at the default scale is 32768, one past full stick.
  CHECK(!MouseToStick(127, 0, 256, rx, ry));
  CHECK(rx == 32512);
  CHECK(MouseToStick(128, 0, 256, rx, ry));
  CHECK(rx == kStickMax);
  CHECK(MouseToStick(-500, 0, 256, rx, ry));
  CHECK(rx == -kStickMax);

  // Maximum scale: one pixel is 1/8 of the range, 8 px already clamps.
  CHECK(!MouseToStick(1, 0, 4096, rx, ry));
  CHECK(rx == 4096);
  CHECK(MouseToStick(8, 0, 4096, rx, ry));
  CHECK(rx == kStickMax);

  // Huge movement at the maximum scale overflows int32 if multiplied naively.
  CHECK(MouseToStick(1000000, -1000000, 4096, rx, ry));
  CHECK(rx == kStickMax && ry == kStickMax);

  // Only one axis clamped still reports the loss.
  CHECK(MouseToStick(1, 200, 256, rx, ry));
  CHECK(rx == 256 && ry == -kStickMax);
}

static void TestInjectPlan() {
  InjectPlan p{1000, -300, 1000000};  // 1000, -300 px over one second
  int32_t x = 7, y = 7;

  p.DeliveredBy(0, x, y);
  CHECK(x == 0 && y == 0);
  p.DeliveredBy(-5, x, y);
  CHECK(x == 0 && y == 0);
  p.DeliveredBy(250000, x, y);
  CHECK(x == 250 && y == -75);
  p.DeliveredBy(500000, x, y);
  CHECK(x == 500 && y == -150);
  p.DeliveredBy(1000000, x, y);
  CHECK(x == 1000 && y == -300);
  p.DeliveredBy(5000000, x, y);
  CHECK(x == 1000 && y == -300);

  // Delivering in 1 ms steps never goes backwards and ends on the exact total,
  // which is what the injector thread relies on (it sends differences).
  InjectPlan q{777, 3, 333000};
  int32_t px = 0, py = 0, sum_x = 0, sum_y = 0;
  bool monotonic = true;
  for (int64_t t = 0; t <= 340000; t += 1000) {
    q.DeliveredBy(t, x, y);
    if (x < px || y < py) monotonic = false;
    sum_x += x - px;
    sum_y += y - py;
    px = x;
    py = y;
  }
  CHECK(monotonic);
  CHECK(sum_x == 777 && sum_y == 3);

  // Zero duration delivers everything at once.
  InjectPlan z{5, 6, 0};
  z.DeliveredBy(0, x, y);
  CHECK(x == 5 && y == 6);
}

static void TestFormatWindow() {
  Window w;
  w.start_us = 0;
  w.polls = 60;
  w.updates = 30;
  w.gaps = 2;
  w.gap_sum = 2000.0;               // gaps of 900 and 1100 us
  w.gap_sumsq = 900.0 * 900.0 + 1100.0 * 1100.0;
  w.gap_max_us = 1100;
  w.px_x = 120;
  w.px_y = 30;
  w.clamped = 4;
  w.AddThread(11);
  w.AddThread(11);
  w.AddThread(22);
  const std::string s = FormatWindow("S", 2000000, w);  // two seconds
  CHECK(s.rfind("S t=2000000 ", 0) == 0);
  CHECK(s.find(" polls_s=30.0 ") != std::string::npos);
  CHECK(s.find(" upd_s=15.0 ") != std::string::npos);
  CHECK(s.find(" gapmean=1000 ") != std::string::npos);
  CHECK(s.find(" gapsd=100 ") != std::string::npos);
  CHECK(s.find(" gapmax=1100 ") != std::string::npos);
  CHECK(s.find(" px=120,30 ") != std::string::npos);
  CHECK(s.find(" clamp=4 ") != std::string::npos);
  CHECK(s.find(" threads=2 ") != std::string::npos);
}

int main() {
  TestMouseToStick();
  TestInjectPlan();
  TestFormatWindow();
  if (failures) {
    std::printf("%d failure(s)\n", failures);
    return 1;
  }
  std::printf("PASS: mouse-look helpers\n");
  return 0;
}
