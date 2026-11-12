// Standalone test for the direct mouse camera helpers (src/input/mouse_direct.h)
// and the inject speed profiles (src/input/mouse_diag.h). No game, GPU, SDL or
// audio device needed.
//
// Covers:
//   - ToCamera: degrees per count, mouse_look_scale, the yaw sign (mouse right
//     turns like stick right) and the up/down sign and inversion;
//   - DirectState: nothing is kept until the camera has asked for movement,
//     everything is delivered once, smoothing holds a share back without a
//     tail of fractions, and movement is discarded (never released later) when
//     the camera can't take it or stops asking;
//   - RouteTimeoutUs and Boot: the route timeout and the retry schedule for
//     starting mouse_direct.lua;
//   - EncodeNumbers: the Lua 5.1 number TValues the cprint override returns;
//   - InjectPlan profiles: ramp and flick deliver the same total as even, move
//     monotonically, and follow their curves.

#include <cmath>
#include <cstdio>
#include <cstring>

#include "src/input/mouse_diag.h"
#include "src/input/mouse_direct.h"

namespace mdir = fable2::mousedirect;
using fable2::mousediag::InjectPlan;
using fable2::mousediag::InjectProfile;

static int failures = 0;
#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
      ++failures;                                                            \
    }                                                                        \
  } while (0)

static bool Near(double a, double b, double eps = 1e-9) { return std::abs(a - b) <= eps; }

static mdir::Settings Sens012() {
  mdir::Settings s;  // scale 256
  s.sens_x_deg = s.sens_y_deg = 0.12;
  return s;
}

static void TestToCamera() {
  CHECK(mdir::Settings{}.sens_x_deg == 0.06 && mdir::Settings{}.sens_y_deg == 0.06);  // the default
  mdir::Settings s = Sens012();
  mdir::CameraDelta d = mdir::ToCamera(100, 0, s);
  CHECK(Near(d.yaw, -12.0));  // mouse right = negative, like RotateRight for stick right
  CHECK(Near(d.height, 0.0));
  d = mdir::ToCamera(-50, 0, s);
  CHECK(Near(d.yaw, 6.0));
  s.scale = 512;  // 2x
  CHECK(Near(mdir::ToCamera(100, 0, s).yaw, -24.0));
  s.scale = 0;  // never divides by or multiplies with zero
  CHECK(Near(mdir::ToCamera(256, 0, s).yaw, -0.12));
  s = Sens012();
  // Mouse up (dy < 0) lowers the height offset, as stick up does (look up).
  d = mdir::ToCamera(0, -100, s);
  CHECK(Near(d.height, -100 * 0.12 * mdir::kDegToRad));
  CHECK(Near(d.yaw, 0.0));
  s.invert_y = true;
  CHECK(Near(mdir::ToCamera(0, -100, s).height, 100 * 0.12 * mdir::kDegToRad));
  s = Sens012();
  s.sens_x_deg = 0.5;
  s.sens_y_deg = 0.25;
  d = mdir::ToCamera(10, 10, s);
  CHECK(Near(d.yaw, -5.0));
  CHECK(Near(d.height, 2.5 * mdir::kDegToRad));
}

static void TestDirectState() {
  const int64_t timeout = 200000;
  double dx = 0, dy = 0;
  {
    // Before the camera has asked, the mouse stays on the stick and nothing is kept.
    mdir::DirectState st;
    CHECK(!st.Feed(true, 1000, timeout, 10, 5));
    st.Drain(true, 2000, 0.0, dx, dy);
    CHECK(dx == 0 && dy == 0);
    // Now the route is direct: movement is kept and delivered once.
    CHECK(st.Feed(true, 3000, timeout, 10, 5));
    CHECK(st.Feed(true, 4000, timeout, -3, 1));
    CHECK(st.RouteDirect());
    st.Drain(true, 5000, 0.0, dx, dy);
    CHECK(Near(dx, 7) && Near(dy, 6));
    st.Drain(true, 6000, 0.0, dx, dy);
    CHECK(dx == 0 && dy == 0);
    CHECK(st.Drains() == 3);
  }
  {
    // Stick mode: never kept, even with the camera asking.
    mdir::DirectState st;
    st.Drain(true, 1000, 0.0, dx, dy);
    CHECK(!st.Feed(false, 2000, timeout, 10, 0));
    CHECK(!st.RouteDirect());
    st.Drain(true, 3000, 0.0, dx, dy);
    CHECK(dx == 0);
  }
  {
    // The camera can't be turned (target lock): kept movement is discarded and
    // the mouse goes back to the stick at once.
    mdir::DirectState st;
    st.Drain(true, 1000, 0.0, dx, dy);
    CHECK(st.Feed(true, 2000, timeout, 40, 0));
    st.Drain(false, 3000, 0.0, dx, dy);
    CHECK(dx == 0 && dy == 0);
    CHECK(!st.Feed(true, 4000, timeout, 40, 0));
    st.Drain(true, 5000, 0.0, dx, dy);
    CHECK(dx == 0);  // nothing from before the lock comes out later
  }
  {
    // The camera stops asking (cutscene, menu, other camera): after the
    // timeout the mouse goes back to the stick and the kept movement is dropped.
    mdir::DirectState st;
    st.Drain(true, 1000, 0.0, dx, dy);
    CHECK(st.Feed(true, 100000, timeout, 25, 0));
    CHECK(st.Feed(true, 201000, timeout, 25, 0));  // exactly at the timeout: still direct
    CHECK(!st.Feed(true, 201001, timeout, 25, 0));
    st.Drain(true, 300000, 0.0, dx, dy);
    CHECK(dx == 0);
    CHECK(st.Feed(true, 300001, timeout, 25, 0));  // and back once it asks again
  }
  {
    // Clear (mouse look off, debug menu open).
    mdir::DirectState st;
    st.Drain(true, 1000, 0.0, dx, dy);
    st.Feed(true, 2000, timeout, 9, 9);
    st.Clear();
    st.Drain(true, 3000, 0.0, dx, dy);
    CHECK(dx == 0 && dy == 0);
  }
  {
    // Smoothing holds a share back and the total still arrives, with no
    // endless tail of fractions.
    mdir::DirectState st;
    st.Drain(true, 1000, 0.5, dx, dy);
    st.Feed(true, 2000, timeout, 8, -4);
    double sx = 0, sy = 0;
    int drains = 0;
    for (int i = 0; i < 100; ++i) {
      st.Drain(true, 3000 + i, 0.5, dx, dy);
      if (dx == 0 && dy == 0) break;
      if (i == 0) CHECK(Near(dx, 4) && Near(dy, -2));
      sx += dx;
      sy += dy;
      ++drains;
    }
    CHECK(Near(sx, 8, 1e-9) && Near(sy, -4, 1e-9));
    CHECK(drains < 10);
    // Out-of-range smoothing is clamped to 0.9, so movement always arrives.
    st.Feed(true, 5000, timeout, 10, 0);
    st.Drain(true, 6000, 5.0, dx, dy);
    CHECK(Near(dx, 1.0, 1e-9));
  }
}

static void TestRouteTimeout() {
  CHECK(mdir::RouteTimeoutUs(66667) == 200000);  // 2.5 updates is shorter than the floor
  CHECK(mdir::RouteTimeoutUs(33333) == 200000);
  CHECK(mdir::RouteTimeoutUs(100000) == 250000);
}

static void TestBoot() {
  using B = mdir::Boot;
  mdir::Boot b;
  const int64_t t0 = 1'000'000;
  // Never without mouse movement that the camera didn't take (e.g. sitting in
  // a menu or alt-tabbed, where the game's scripts stand still).
  CHECK(!b.ShouldTry(t0));
  CHECK(!b.ShouldTry(t0 + 60 * B::kFirstDelayUs));
  // Waits a moment after the game starts, then tries while the mouse is moved.
  mdir::Boot c;
  c.Wanted(t0);
  CHECK(!c.ShouldTry(t0));
  int64_t t = t0 + B::kFirstDelayUs;
  c.Wanted(t);
  CHECK(c.ShouldTry(t));
  // Then retries with a growing pause, still only while it's wanted.
  c.Wanted(t + B::kMinBackoffUs - 1);
  CHECK(!c.ShouldTry(t + B::kMinBackoffUs - 1));
  CHECK(c.ShouldTry(t + B::kMinBackoffUs));
  t += B::kMinBackoffUs;
  CHECK(!c.ShouldTry(t + 2 * B::kMinBackoffUs));  // the wish is older than kWantedUs
  c.Wanted(t + 2 * B::kMinBackoffUs);
  CHECK(c.ShouldTry(t + 2 * B::kMinBackoffUs));
  CHECK(c.Tries() == 3);
  // Once the script reports in, no tries while it keeps doing so.
  t += 2 * B::kMinBackoffUs;
  c.Alive(t);
  c.Wanted(t + B::kAliveUs);
  CHECK(c.IsAlive(t + B::kAliveUs));
  CHECK(!c.ShouldTry(t + B::kAliveUs));
  // Silent for a long time is fine on its own...
  CHECK(!c.IsAlive(t + B::kAliveUs + 1));
  CHECK(!c.ShouldTry(t + 10 * B::kAliveUs));
  // ...but silent while the mouse is moved means it's gone: try at once.
  c.Wanted(t + 10 * B::kAliveUs);
  CHECK(c.ShouldTry(t + 10 * B::kAliveUs));
  // The pause never grows past the cap.
  mdir::Boot d;
  int64_t u = 0;
  d.ShouldTry(u);
  u += B::kFirstDelayUs;
  for (int i = 0; i < 20; ++i) {
    d.Wanted(u);
    CHECK(d.ShouldTry(u));
    u += B::kMaxBackoffUs;
  }
  // An old keeper's heartbeat asks for a run even while the script reports in,
  // once, and not before the start-up delay.
  mdir::Boot e;
  e.RequestRun();
  CHECK(!e.ShouldTry(0));
  e.Alive(B::kFirstDelayUs);
  CHECK(e.ShouldTry(B::kFirstDelayUs));
  CHECK(!e.ShouldTry(B::kFirstDelayUs + 1));
  e.RequestRun();
  CHECK(!e.ShouldTry(B::kFirstDelayUs + B::kMinBackoffUs - 1));
  CHECK(e.ShouldTry(B::kFirstDelayUs + B::kMinBackoffUs));
  // The first heartbeat after each start is reported once.
  d.Booted();
  CHECK(d.FirstHeartbeat());
  CHECK(!d.FirstHeartbeat());
  d.Booted();
  CHECK(d.FirstHeartbeat());
}

static void TestEncodeNumbers() {
  const float v[2] = {1.0f, -12.5f};
  uint8_t out[16];
  std::memset(out, 0xAA, sizeof out);
  mdir::EncodeNumbers(v, 2, out);
  const uint8_t want[16] = {0x3F, 0x80, 0x00, 0x00, 0, 0, 0, 3,
                            0xC1, 0x48, 0x00, 0x00, 0, 0, 0, 3};
  CHECK(std::memcmp(out, want, sizeof want) == 0);
}

static void TestInjectProfiles() {
  for (InjectProfile p : {InjectProfile::kEven, InjectProfile::kRamp, InjectProfile::kFlick}) {
    const InjectPlan plan{4000, -1000, 200000, p};
    int32_t x = 0, y = 0, px = 0, py = 0;
    bool monotonic = true;
    for (int64_t t = 0; t <= 200000; t += 1000) {
      plan.DeliveredBy(t, x, y);
      if (x < px || y > py) monotonic = false;
      px = x;
      py = y;
    }
    CHECK(monotonic);
    plan.DeliveredBy(200000, x, y);
    CHECK(x == 4000 && y == -1000);  // the same total for every profile
    plan.DeliveredBy(250000, x, y);
    CHECK(x == 4000 && y == -1000);
    plan.DeliveredBy(0, x, y);
    CHECK(x == 0 && y == 0);
  }
  int32_t x = 0, y = 0;
  InjectPlan ramp{4000, 0, 200000, InjectProfile::kRamp};
  ramp.DeliveredBy(100000, x, y);
  CHECK(x == 1000);  // half the time, a quarter of the distance
  InjectPlan flick{4000, 0, 200000, InjectProfile::kFlick};
  flick.DeliveredBy(100000, x, y);
  CHECK(x == 2000);  // symmetric: half the distance at the midpoint
  flick.DeliveredBy(20000, x, y);
  CHECK(x < 400 / 4);  // slow start: well under the even 400 px after 10%
  // Peak speed (midpoint) is 1.875x the average speed.
  int32_t a = 0, b = 0;
  flick.DeliveredBy(99000, a, y);
  flick.DeliveredBy(101000, b, y);
  CHECK(std::abs((b - a) - 2 * 4000.0 / 200 * 1.875) <= 2);
  CHECK(std::string(fable2::mousediag::ProfileName(InjectProfile::kFlick)) == "flick");
  InjectProfile parsed = InjectProfile::kEven;
  CHECK(fable2::mousediag::ParseProfile("ramp", parsed) && parsed == InjectProfile::kRamp);
  CHECK(!fable2::mousediag::ParseProfile("fast", parsed));
}

int main() {
  TestToCamera();
  TestDirectState();
  TestRouteTimeout();
  TestBoot();
  TestEncodeNumbers();
  TestInjectProfiles();
  if (failures) {
    std::printf("%d failure(s)\n", failures);
    return 1;
  }
  std::printf("mouse direct tests passed\n");
  return 0;
}
