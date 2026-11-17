// Standalone test for the mouse-look helpers (src/input/keyboard_gamepad.h,
// src/input/mouse_stick.h and src/input/mouse_diag.h). No game, GPU, SDL or
// audio device needed.

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
#include <string>

#include "catch_amalgamated.hpp"

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::EndsWith;
using Catch::Matchers::StartsWith;
using Catch::Matchers::WithinAbs;
using fable2::input_detail::kStickMax;
using fable2::input_detail::MouseToStick;
using fable2::mousediag::FormatWindow;
using fable2::mousediag::InjectPlan;
using fable2::mousediag::Window;
namespace ms = fable2::mousestick;

TEST_CASE("MouseToStick", "[mouse][stick]") {
  int32_t rx = 1, ry = 1;

  SECTION("centres the stick for no movement, with nothing lost") {
    CHECK_FALSE(MouseToStick(0, 0, 256, rx, ry));
    CHECK(rx == 0);
    CHECK(ry == 0);
  }

  SECTION("scales by 256 per pixel by default, mouse down being look down") {
    CHECK_FALSE(MouseToStick(10, 4, 256, rx, ry));
    CHECK(rx == 2560);
    CHECK(ry == -1024);
  }

  SECTION("looks up for mouse up") {
    CHECK_FALSE(MouseToStick(0, -3, 256, rx, ry));
    CHECK(rx == 0);
    CHECK(ry == 768);
  }

  SECTION("clamps at full stick and reports the loss") {
    CHECK_FALSE(MouseToStick(127, 0, 256, rx, ry));  // 127 px fits
    CHECK(rx == 32512);
    CHECK(MouseToStick(128, 0, 256, rx, ry));  // 32768: one past full stick
    CHECK(rx == kStickMax);
    CHECK(MouseToStick(-500, 0, 256, rx, ry));
    CHECK(rx == -kStickMax);
  }

  SECTION("clamps at 8 px at the maximum scale") {
    CHECK_FALSE(MouseToStick(1, 0, 4096, rx, ry));  // one pixel is 1/8 of the range
    CHECK(rx == 4096);
    CHECK(MouseToStick(8, 0, 4096, rx, ry));
    CHECK(rx == kStickMax);
  }

  SECTION("doesn't overflow int32 on huge movement at the maximum scale") {
    CHECK(MouseToStick(1000000, -1000000, 4096, rx, ry));
    CHECK(rx == kStickMax);
    CHECK(ry == kStickMax);
  }

  SECTION("reports the loss when only one axis is clamped") {
    CHECK(MouseToStick(1, 200, 256, rx, ry));
    CHECK(rx == 256);
    CHECK(ry == -kStickMax);
  }
}

// Delivers `plan` in 1 ms steps up to `end`. Returns whether it only ever grew
// toward the total (`sign_x`, `sign_y`), and the summed differences, which is
// what the injector thread sends.
static bool DeliverInSteps(const InjectPlan& plan, int64_t end, int sign_x, int sign_y,
                           int32_t& sum_x, int32_t& sum_y) {
  int32_t x = 0, y = 0, px = 0, py = 0;
  bool monotonic = true;
  sum_x = sum_y = 0;
  for (int64_t t = 0; t <= end; t += 1000) {
    plan.DeliveredBy(t, x, y);
    if ((x - px) * sign_x < 0 || (y - py) * sign_y < 0) monotonic = false;
    sum_x += x - px;
    sum_y += y - py;
    px = x;
    py = y;
  }
  return monotonic;
}

TEST_CASE("InjectPlan (f2 mouse inject)", "[mouse][diag]") {
  int32_t x = 7, y = 7;

  SECTION("spreads the movement evenly over the duration and stops at the total") {
    const InjectPlan p{1000, -300, 1000000};  // 1000, -300 px over one second
    p.DeliveredBy(0, x, y);
    CHECK(x == 0);
    CHECK(y == 0);
    p.DeliveredBy(-5, x, y);
    CHECK(x == 0);
    CHECK(y == 0);
    p.DeliveredBy(250000, x, y);
    CHECK(x == 250);
    CHECK(y == -75);
    p.DeliveredBy(500000, x, y);
    CHECK(x == 500);
    CHECK(y == -150);
    p.DeliveredBy(1000000, x, y);
    CHECK(x == 1000);
    CHECK(y == -300);
    p.DeliveredBy(5000000, x, y);
    CHECK(x == 1000);
    CHECK(y == -300);
  }

  SECTION("never goes backwards in 1 ms steps and ends on the exact total") {
    int32_t sum_x = 0, sum_y = 0;
    CHECK(DeliverInSteps(InjectPlan{777, 3, 333000}, 340000, 1, 1, sum_x, sum_y));
    CHECK(sum_x == 777);
    CHECK(sum_y == 3);
  }

  SECTION("does the same leftward and upward") {
    int32_t sum_x = 0, sum_y = 0;
    CHECK(DeliverInSteps(InjectPlan{-777, -3, 333000}, 340000, -1, -1, sum_x, sum_y));
    CHECK(sum_x == -777);
    CHECK(sum_y == -3);
  }

  SECTION("delivers everything at once for a zero duration") {
    const InjectPlan z{5, 6, 0};
    z.DeliveredBy(0, x, y);
    CHECK(x == 5);
    CHECK(y == 6);
  }

  SECTION("works in 64 bits at the console's limits (1,000,000 px over 600 s)") {
    // total * elapsed is ~6e14 here, far past int32
    const InjectPlan big{1000000, -1000000, 600000000};
    big.DeliveredBy(300000000, x, y);
    CHECK(x == 500000);
    CHECK(y == -500000);
    big.DeliveredBy(599999999, x, y);
    CHECK(x == 999999);
    CHECK(y == -999999);
    big.DeliveredBy(600000000, x, y);
    CHECK(x == 1000000);
    CHECK(y == -1000000);
  }
}

TEST_CASE("TakeInjected", "[mouse][diag]") {
  SECTION("gives a poll all the pending movement once") {
    // The injector thread adds to the pending movement.
    fable2::mousediag::PendingX().fetch_add(5);
    fable2::mousediag::PendingY().fetch_add(-7);
    int32_t x = 0, y = 0;
    fable2::mousediag::TakeInjected(x, y);
    CHECK(x == 5);
    CHECK(y == -7);
    fable2::mousediag::TakeInjected(x, y);  // the next poll gets nothing new
    CHECK(x == 0);
    CHECK(y == 0);
  }
}

TEST_CASE("FormatWindow (per-second summary)", "[mouse][diag]") {
  SECTION("reports rates, the gap mean and spread, movement, clamps and threads") {
    Window w;
    w.start_us = 0;
    w.polls = 60;
    w.updates = 30;
    w.gaps = 2;
    w.gap_sum = 2000.0;  // gaps of 900 and 1100 us
    w.gap_sumsq = 900.0 * 900.0 + 1100.0 * 1100.0;
    w.gap_max_us = 1100;
    w.px_x = 120;
    w.px_y = 30;
    w.clamped = 4;
    w.AddThread(11);
    w.AddThread(11);
    w.AddThread(22);
    const std::string s = FormatWindow("S", 2000000, w);  // two seconds
    CHECK_THAT(s, StartsWith("S t=2000000 "));
    CHECK_THAT(s, ContainsSubstring(" polls_s=30.0 "));
    CHECK_THAT(s, ContainsSubstring(" upd_s=15.0 "));
    CHECK_THAT(s, ContainsSubstring(" gapmean=1000 "));
    CHECK_THAT(s, ContainsSubstring(" gapsd=100 "));
    CHECK_THAT(s, ContainsSubstring(" gapmax=1100 "));
    CHECK_THAT(s, ContainsSubstring(" px=120,30 "));
    CHECK_THAT(s, ContainsSubstring(" clamp=4 "));
    CHECK_THAT(s, ContainsSubstring(" threads=2 "));
    CHECK_THAT(s, ContainsSubstring(" dropped=0"));
  }

  SECTION("reports zeros for a window without gaps, and the dropped lines") {
    Window empty;
    empty.dropped = 3;
    const std::string e = FormatWindow("S", 1000000, empty);
    CHECK_THAT(e, ContainsSubstring(" gapmean=0 gapsd=0 gapmax=0 "));
    CHECK_THAT(e, EndsWith(" dropped=3"));
  }

  SECTION("tells only the first 8 polling threads apart") {
    Window many;
    for (uint64_t id = 1; id <= 9; ++id) many.AddThread(id);
    CHECK(many.thread_count == 8);
    CHECK_THAT(FormatWindow("S", 1000000, many), ContainsSubstring(" threads=8 "));
  }
}

TEST_CASE("GameControl (the game's stick response)", "[mouse][stick]") {
  double cx = 0, cy = 0;

  SECTION("matches the 2026-11-11 console-pad measurements") {
    struct M { int rx, ry; double cx, cy; };
    const M data[] = {{20000, 0, 0.443, 0}, {20000, 8000, 0.567, 0.227}, {10000, 10000, 0.188, 0.188},
                      {15000, 15000, 0.496, 0.496}, {20000, 20000, 0.707, 0.707},
                      {17321, 10000, 0.503, 0.291}, {-14142, -14142, -0.443, -0.443}};
    for (const M& m : data) {
      CAPTURE(m.rx, m.ry);
      ms::GameControl(m.rx / 32767.0, m.ry / 32767.0, cx, cy);
      CHECK_THAT(cx, WithinAbs(m.cx, 0.001));
      CHECK_THAT(cy, WithinAbs(m.cy, 0.001));
    }
  }

  SECTION("sends nothing to the camera inside the deadzone") {
    ms::GameControl(0.29, 0.0, cx, cy);
    CHECK(cx == 0.0);
    CHECK(cy == 0.0);
  }

  SECTION("caps a full diagonal, which the game boosts past 1, back to full speed") {
    ms::GameControl(1.0, 1.0, cx, cy);
    CHECK_THAT(std::hypot(cx, cy), WithinAbs(1.0, 1e-9));
    CHECK_THAT(cx - cy, WithinAbs(0.0, 1e-12));
  }
}

TEST_CASE("StickForControl", "[mouse][stick]") {
  int32_t rx = 7, ry = 7;
  double gx = 0, gy = 0;

  SECTION("centres the stick for no input") {
    CHECK_FALSE(ms::StickForControl(0.0, 0.0, rx, ry));
    CHECK(rx == 0);
    CHECK(ry == 0);
  }

  SECTION("gives full stick for full speed, and caps and reports more") {
    CHECK_FALSE(ms::StickForControl(1.0, 0.0, rx, ry));
    CHECK(rx == 32767);
    CHECK(ry == 0);
    CHECK(ms::StickForControl(3.0, 0.0, rx, ry));
    CHECK(rx == 32767);
    CHECK(ry == 0);
  }

  SECTION("makes the game deliver what was asked, in every direction and at every speed") {
    // One stick unit is ~0.00006 of control after the rescale, so 0.0002
    // allows for rounding. Even the tiniest movement lands past the deadzone.
    double worst = 0.0;
    bool all_register = true;
    const double mags[] = {0.0005, 0.01, 0.05, 0.2, 0.5, 0.8, 1.0};
    for (int a = 0; a < 48; ++a) {
      const double th = a * (2.0 * 3.14159265358979323846 / 48.0);
      for (double m : mags) {
        const double wx = m * std::cos(th), wy = m * std::sin(th);
        ms::StickForControl(wx, wy, rx, ry);
        ms::GameControl(rx / 32767.0, ry / 32767.0, gx, gy);
        worst = std::max(worst, std::max(std::fabs(gx - wx), std::fabs(gy - wy)));
        if (gx == 0.0 && gy == 0.0) all_register = false;
      }
    }
    CHECK(worst < 0.0002);
    CHECK(all_register);
  }

  SECTION("doesn't boost diagonals: half speed at 45 degrees is half speed") {
    ms::StickForControl(0.5 / std::sqrt(2.0), 0.5 / std::sqrt(2.0), rx, ry);
    ms::GameControl(rx / 32767.0, ry / 32767.0, gx, gy);
    CHECK_THAT(std::hypot(gx, gy), WithinAbs(0.5, 0.0002));
  }

  SECTION("caps more than full speed on a diagonal to exactly full speed") {
    // the game's diagonal boost doesn't push it past 1
    CHECK(ms::StickForControl(3.0, 3.0, rx, ry));
    CHECK(rx == ry);
    CHECK(rx > 0);
    CHECK(rx < 32767);
    ms::GameControl(rx / 32767.0, ry / 32767.0, gx, gy);
    CHECK_THAT(std::hypot(gx, gy), WithinAbs(1.0, 0.0002));
  }
}

TEST_CASE("VelocityWindow", "[mouse][stick]") {
  ms::VelocityWindow w;
  double x = 1, y = 1;

  SECTION("sums nothing when empty") {
    w.Sum(0, 66667, x, y);
    CHECK(x == 0);
    CHECK(y == 0);
  }

  SECTION("sums the movement inside the window, in proportion at its edge") {
    // 20 px over (0, 40 ms], then 10 px over (40, 60 ms]
    w.Add(0, 40000, 20, -4);
    w.Add(40000, 60000, 10, 2);
    w.Add(60000, 61000, 0, 0);  // no movement: not stored
    // Sum() drops what has left the window, so `now` only moves forward.
    w.Sum(60000, 66667, x, y);  // covers everything
    CHECK_THAT(x, WithinAbs(30, 1e-6));
    CHECK_THAT(y, WithinAbs(-2, 1e-6));
    w.Sum(70000, 50000, x, y);  // (20, 70]: half of the first, all of the second
    CHECK_THAT(x, WithinAbs(20, 1e-6));
    CHECK_THAT(y, WithinAbs(0, 1e-6));
    w.Sum(80000, 40000, x, y);  // (40, 80]: only the second
    CHECK_THAT(x, WithinAbs(10, 1e-6));
    CHECK_THAT(y, WithinAbs(2, 1e-6));
    w.Sum(130000, 66667, x, y);  // both have left
    CHECK_THAT(x, WithinAbs(0, 1e-6));
    CHECK_THAT(y, WithinAbs(0, 1e-6));
  }

  SECTION("counts a poll after a long stall in full") {
    w.Add(0, 1000000, 90, 0);
    w.Sum(1000000, 66667, x, y);
    CHECK_THAT(x, WithinAbs(90, 1e-6));
  }

  SECTION("drops the oldest entries when the ring is full, and Clear empties it") {
    for (int i = 0; i < 300; ++i) w.Add(2000000 + i * 10, 2000000 + i * 10 + 10, 1, 0);
    w.Sum(2003000, 1000000, x, y);
    CHECK_THAT(x, WithinAbs(static_cast<double>(ms::VelocityWindow::kCapacity), 1e-6));
    w.Clear();
    w.Sum(2003000, 1000000, x, y);
    CHECK(x == 0);
    CHECK(y == 0);
  }

  SECTION("counts a zero-length interval (the first smoothed poll) in full while inside") {
    // The first poll on the smoothing path has no previous poll, so the
    // driver adds its movement with t0 == t1.
    w.Add(100000, 100000, 12, -6);
    w.Sum(100000, 66667, x, y);
    CHECK_THAT(x, WithinAbs(12, 1e-6));
    CHECK_THAT(y, WithinAbs(-6, 1e-6));
    w.Sum(150000, 66667, x, y);  // (83.3, 150 ms]: still inside
    CHECK_THAT(x, WithinAbs(12, 1e-6));
    w.Sum(170000, 66667, x, y);  // (103.3, 170 ms]: gone
    CHECK(x == 0);
    CHECK(y == 0);
  }

  SECTION("treats a start after the end (clock skew) as zero-length") {
    w.Add(300000, 200000, 5, 0);
    w.Sum(200000, 66667, x, y);
    CHECK_THAT(x, WithinAbs(5, 1e-6));
    CHECK_THAT(y, WithinAbs(0, 1e-6));
  }

  SECTION("stays within 3% of steady movement under the measured poll jitter") {
    // In game: moving polls ~33.3 ms apart with gaps alternating 32.7 / 33.9
    // ms, every other one followed by two polls with no movement. At a steady
    // 3000 px/s the 66.7 ms window must always hold ~200 px; counting whole
    // polls gave 200 or 300 depending on the jitter.
    const int64_t window = 66667;
    int64_t t = 1000000, prev = t;
    double lo = 1e9, hi = -1e9;
    for (int i = 0; i < 120; ++i) {
      t += (i % 2) ? 33900 : 32700;
      const int32_t px = static_cast<int32_t>(std::lround(3000.0 * (t - prev) / 1e6));
      w.Add(prev, t, px, 0);
      prev = t;
      for (int k = 0; k <= ((i % 2) ? 2 : 0); ++k) {
        w.Sum(t + k * 50, window, x, y);
        if (i >= 4) {
          lo = std::min(lo, x);
          hi = std::max(hi, x);
        }
      }
    }
    CHECK(lo > 194.0);
    CHECK(hi < 206.0);
  }
}

TEST_CASE("WindowToControl", "[mouse][stick]") {
  double cx = 0, cy = 0;

  SECTION("keeps the previous top speed with a 1/30 s window") {
    // the wanted input equals the previous raw stick value for the same pixels
    ms::WindowToControl(10, 0, 1.0 / 30.0, 256, cx, cy);
    int32_t rx, ry;
    MouseToStick(10, 0, 256, rx, ry);
    CHECK_THAT(cx, WithinAbs(rx / 32767.0, 1e-9));
  }

  SECTION("gives the same input for twice the pixels over twice the window") {
    double cx2 = 0, cy2 = 0;
    ms::WindowToControl(10, 0, 1.0 / 30.0, 256, cx, cy);
    ms::WindowToControl(20, 0, 2.0 / 30.0, 256, cx2, cy2);
    CHECK_THAT(cx2, WithinAbs(cx, 1e-9));
  }

  SECTION("looks up for mouse up, and multiplies by the scale") {
    ms::WindowToControl(0, -5, 1.0 / 30.0, 512, cx, cy);
    CHECK(cx == 0.0);
    CHECK_THAT(cy, WithinAbs(5.0 * 512 / 32767.0, 1e-9));
  }

  SECTION("asks for no turn for an empty window rather than dividing by zero") {
    ms::WindowToControl(10, 5, 0.0, 256, cx, cy);
    CHECK(cx == 0.0);
    CHECK(cy == 0.0);
  }

  SECTION("uses a 66.7 ms window by default, 33.3 ms at the high tick rate") {
    CHECK(ms::DefaultWindowUs(false) == 66667);
    CHECK(ms::DefaultWindowUs(true) == 33333);
  }
}
