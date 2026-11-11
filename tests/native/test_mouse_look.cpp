// Standalone test for the mouse-look helpers (src/input/keyboard_gamepad.h and
// src/input/mouse_diag.h). No game, GPU, SDL or audio device needed.
//
// Covers:
//   - MouseToStick: scale, Y inversion, full-deflection clamping (the speed
//     cap) and that it reports when movement was lost to the clamp;
//   - InjectPlan: `f2 mouse inject` spreads the movement evenly over time and
//     always delivers exactly the requested total;
//   - FormatWindow: the per-second summary maths (rates, gap mean / sd);
//   - StickForControl: the stick it picks makes the game's measured mapping
//     (GameControl) deliver exactly the wanted camera input, in every
//     direction, including tiny inputs just past the deadzone;
//   - VelocityWindow / WindowToControl: movement over one camera update, and
//     the same top speed as the previous mapping.

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

#include <cmath>
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

namespace ms = fable2::mousestick;

static void TestGameControlMatchesMeasurements() {
  // A few of the 2026-11-11 console-pad measurements (3 decimals in the log).
  struct M { int rx, ry; double cx, cy; };
  const M data[] = {{20000, 0, 0.443, 0}, {20000, 8000, 0.567, 0.227}, {10000, 10000, 0.188, 0.188},
                    {15000, 15000, 0.496, 0.496}, {20000, 20000, 0.707, 0.707},
                    {17321, 10000, 0.503, 0.291}, {-14142, -14142, -0.443, -0.443}};
  for (const M& m : data) {
    double cx, cy;
    ms::GameControl(m.rx / 32767.0, m.ry / 32767.0, cx, cy);
    CHECK(std::fabs(cx - m.cx) < 0.001 && std::fabs(cy - m.cy) < 0.001);
  }
  // Inside the deadzone nothing reaches the camera.
  double cx, cy;
  ms::GameControl(0.29, 0.0, cx, cy);
  CHECK(cx == 0.0 && cy == 0.0);
}

static void TestStickForControl() {
  int32_t rx = 7, ry = 7;
  CHECK(!ms::StickForControl(0.0, 0.0, rx, ry));
  CHECK(rx == 0 && ry == 0);

  // Full speed straight right is full stick.
  CHECK(!ms::StickForControl(1.0, 0.0, rx, ry));
  CHECK(rx == 32767 && ry == 0);

  // More than full speed is capped, and reported.
  CHECK(ms::StickForControl(3.0, 0.0, rx, ry));
  CHECK(rx == 32767 && ry == 0);

  // Round trip: the game must deliver what was asked for, in every direction
  // and at every speed. One stick unit is ~0.00006 of control after the
  // rescale, so 0.0002 allows for rounding.
  double worst = 0.0;
  bool all_register = true;
  const double mags[] = {0.0005, 0.01, 0.05, 0.2, 0.5, 0.8, 1.0};
  for (int a = 0; a < 48; ++a) {
    const double th = a * (2.0 * 3.14159265358979323846 / 48.0);
    for (double m : mags) {
      const double wx = m * std::cos(th), wy = m * std::sin(th);
      ms::StickForControl(wx, wy, rx, ry);
      double gx, gy;
      ms::GameControl(rx / 32767.0, ry / 32767.0, gx, gy);
      worst = std::max(worst, std::max(std::fabs(gx - wx), std::fabs(gy - wy)));
      if (gx == 0.0 && gy == 0.0) all_register = false;
    }
  }
  CHECK(worst < 0.0002);
  CHECK(all_register);  // even the tiniest movement lands past the deadzone

  // Diagonals aren't boosted: half speed at 45 degrees is half speed.
  ms::StickForControl(0.5 / std::sqrt(2.0), 0.5 / std::sqrt(2.0), rx, ry);
  double gx, gy;
  ms::GameControl(rx / 32767.0, ry / 32767.0, gx, gy);
  CHECK(std::fabs(std::hypot(gx, gy) - 0.5) < 0.0002);
}

static bool Near(double a, double b, double tol = 1e-6) { return std::fabs(a - b) <= tol; }

static void TestVelocityWindow() {
  ms::VelocityWindow w;
  double x = 1, y = 1;
  w.Sum(0, 66667, x, y);
  CHECK(x == 0 && y == 0);

  // 20 px over (0, 40 ms], then 10 px over (40, 60 ms].
  w.Add(0, 40000, 20, -4);
  w.Add(40000, 60000, 10, 2);
  w.Add(60000, 61000, 0, 0);  // no movement: not stored
  // Sum() drops what has left the window, so `now` only moves forward.
  w.Sum(60000, 66667, x, y);  // window covers everything
  CHECK(Near(x, 30) && Near(y, -2));
  w.Sum(70000, 50000, x, y);  // window (20, 70]: half of the first + all of the second
  CHECK(Near(x, 20) && Near(y, 0));
  w.Sum(80000, 40000, x, y);  // window (40, 80]: only the second entry
  CHECK(Near(x, 10) && Near(y, 2));
  w.Sum(130000, 66667, x, y);  // both have left
  CHECK(Near(x, 0) && Near(y, 0));

  // A poll after a long stall: its movement is treated as the last 50 ms.
  w.Clear();
  w.Add(0, 1000000, 90, 0);
  w.Sum(1000000, 66667, x, y);
  CHECK(Near(x, 90));

  // More entries than the ring holds: the oldest go, nothing breaks.
  w.Clear();
  for (int i = 0; i < 300; ++i) w.Add(2000000 + i * 10, 2000000 + i * 10 + 10, 1, 0);
  w.Sum(2003000, 1000000, x, y);
  CHECK(Near(x, static_cast<double>(ms::VelocityWindow::kCapacity)));
  w.Clear();
  w.Sum(2003000, 1000000, x, y);
  CHECK(x == 0 && y == 0);
}

// The poll pattern measured in game: moving polls ~33.3 ms apart with gaps
// alternating 32.7 / 33.9 ms, each followed every other time by two polls
// with no movement. At a steady 3000 px/s the 66.7 ms window must always hold
// ~200 px. Counting whole polls gave 200 or 300 depending on the jitter.
static void TestVelocityWindowIsSteadyUnderJitter() {
  ms::VelocityWindow w;
  const int64_t window = 66667;
  int64_t t = 1000000, prev = t;
  double lo = 1e9, hi = -1e9;
  for (int i = 0; i < 120; ++i) {
    const int64_t gap = (i % 2) ? 33900 : 32700;
    t += gap;
    const int32_t px = static_cast<int32_t>(std::lround(3000.0 * (t - prev) / 1e6));
    w.Add(prev, t, px, 0);
    prev = t;
    double x, y;
    w.Sum(t, window, x, y);
    if (i >= 4) {
      lo = std::min(lo, x);
      hi = std::max(hi, x);
    }
    if (i % 2) {  // the two zero-movement polls right after
      for (int k = 1; k <= 2; ++k) {
        w.Sum(t + k * 50, window, x, y);
        if (i >= 4) {
          lo = std::min(lo, x);
          hi = std::max(hi, x);
        }
      }
    }
  }
  CHECK(lo > 194.0 && hi < 206.0);  // ~200 px, within 3%
}

static void TestWindowToControl() {
  // With a 1/30 s window the wanted input equals the previous raw stick value
  // for the same pixels, so the top speed is unchanged.
  double cx, cy;
  ms::WindowToControl(10, 0, 1.0 / 30.0, 256, cx, cy);
  int32_t rx, ry;
  MouseToStick(10, 0, 256, rx, ry);
  CHECK(std::fabs(cx - rx / 32767.0) < 1e-9);

  // Twice the window, twice the pixels: same speed, same input.
  double cx2, cy2;
  ms::WindowToControl(20, 0, 2.0 / 30.0, 256, cx2, cy2);
  CHECK(std::fabs(cx2 - cx) < 1e-9);

  // Mouse up (dy < 0) is look up (+y); scale is a straight multiplier.
  ms::WindowToControl(0, -5, 1.0 / 30.0, 512, cx, cy);
  CHECK(cx == 0.0 && cy > 0.0);
  CHECK(std::fabs(cy - 5.0 * 512 / 32767.0) < 1e-9);

  CHECK(ms::DefaultWindowUs(false) == 66667);
  CHECK(ms::DefaultWindowUs(true) == 33333);
}

int main() {
  TestMouseToStick();
  TestInjectPlan();
  TestFormatWindow();
  TestGameControlMatchesMeasurements();
  TestStickForControl();
  TestVelocityWindow();
  TestVelocityWindowIsSteadyUnderJitter();
  TestWindowToControl();
  if (failures) {
    std::printf("%d failure(s)\n", failures);
    return 1;
  }
  std::printf("PASS: mouse-look helpers\n");
  return 0;
}
