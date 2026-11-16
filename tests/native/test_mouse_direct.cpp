// Standalone test for the direct mouse camera helpers (src/input/mouse_direct.h)
// and the inject speed profiles (src/input/mouse_diag.h). No game, GPU, SDL or
// audio device needed.
//
// Covers:
//   - ToCamera: degrees per count, mouse_look_scale (including 0 and negative),
//     the yaw sign (mouse right turns like stick right) and the up/down sign
//     and inversion;
//   - DirectState: nothing is kept until the camera has asked for movement,
//     everything is delivered once, smoothing holds a share back without a
//     tail of fractions (also at the 0.9 maximum, and below 0), and movement
//     is discarded (never released later) when the camera can't take it or
//     stops asking;
//   - time 0: NowUs() starts at 0, so a camera update or report at t=0 counts;
//   - RouteTimeoutUs and Boot: the route timeout and the retry schedule for
//     starting mouse_direct.lua, which starts again from the shortest pause
//     after the script has been running;
//   - EncodeNumbers: the Lua 5.1 number TValues the cprint override returns,
//     one or two of them;
//   - InjectPlan profiles: ramp and flick deliver the same total as even, move
//     monotonically, and follow their curves;
//   - non-finite settings: a NaN or infinite sensitivity or smoothing from
//     fable2_config.toml or the console falls back to the default.
//
// KNOWN_BUG checks are the native form of pytest's strict xfail: they test the
// correct behaviour of a confirmed bug, print "KNOWN BUG" while it is broken,
// and fail once it is fixed, as a reminder to turn them into CHECKs.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

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

static int known_bugs = 0;
#define KNOWN_BUG(cond, reason)                                                       \
  do {                                                                                \
    if (cond) {                                                                       \
      std::printf("FAIL %s:%d: known bug now fixed, remove the KNOWN_BUG marker: %s\n", \
                  __FILE__, __LINE__, #cond);                                         \
      ++failures;                                                                     \
    } else {                                                                          \
      std::printf("KNOWN BUG %s:%d: %s\n", __FILE__, __LINE__, reason);               \
      ++known_bugs;                                                                   \
    }                                                                                 \
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
  s.scale = -256;  // nor turns the other way: a negative scale counts as the smallest
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
  {
    // Negative smoothing is clamped to 0: everything is delivered at once.
    mdir::DirectState st;
    st.Drain(true, 1000, 0.0, dx, dy);
    st.Feed(true, 2000, timeout, 10, -6);
    st.Drain(true, 3000, -1.0, dx, dy);
    CHECK(Near(dx, 10) && Near(dy, -6));
  }
  {
    // At the 0.9 maximum a large flick still arrives in full, and the tail
    // ends (after ~94 camera updates for 1000 counts: about 6 s at 15 Hz).
    mdir::DirectState st;
    st.Drain(true, 1000, 0.0, dx, dy);
    st.Feed(true, 2000, timeout, 1000, 0);
    double sx = 0;
    int drains = 0;
    for (int i = 0; i < 1000; ++i) {
      st.Drain(true, 3000 + i, 0.9, dx, dy);
      if (dx == 0 && dy == 0) break;
      sx += dx;
      ++drains;
    }
    CHECK(Near(sx, 1000, 1e-6));
    CHECK(drains < 100);
  }
}

// NowUs() counts from the first call, so the first camera update or report can
// be at t=0. Each "last seen" time stores 0 as 1, so t=0 still counts as seen.
static void TestTimeZero() {
  double dx = 0, dy = 0;
  mdir::DirectState st;
  st.Drain(true, 0, 0.0, dx, dy);
  CHECK(st.Feed(true, 0, 200000, 1, 0));
  mdir::Boot b;
  b.Alive(0);
  CHECK(b.IsAlive(0));
  b.OldKeeperSeen(0);
  CHECK(b.OldKeeperAlive(0));
}

// Settings come from fable2_config.toml, where `nan` and `inf` are valid
// numbers, and the config reader doesn't reject them. They fall back to the
// defaults: no smoothing, and the default sensitivity.
static void TestNonFiniteSettings() {
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();
  double dx = 0, dy = 0;
  {
    // NaN smoothing gets past std::clamp, so it has to be caught first.
    mdir::DirectState st;
    st.Drain(true, 1000, 0.0, dx, dy);
    st.Feed(true, 2000, 200000, 10, 0);
    st.Drain(true, 3000, nan, dx, dy);
    CHECK(dx == 10 && dy == 0);  // as with smoothing 0: everything at once
    // Nothing poisoned is kept for the next update.
    st.Feed(true, 4000, 200000, 5, 0);
    st.Drain(true, 5000, 0.0, dx, dy);
    CHECK(dx == 5);
    st.Feed(true, 6000, 200000, 5, 0);
    st.Drain(true, 7000, inf, dx, dy);
    CHECK(dx == 5);
  }
  {
    const mdir::Settings defaults;
    const double yaw = mdir::ToCamera(10, 0, defaults).yaw;
    const double height = mdir::ToCamera(0, 10, defaults).height;
    mdir::Settings s;
    s.sens_x_deg = nan;
    CHECK(mdir::ToCamera(10, 0, s).yaw == yaw);
    s.sens_x_deg = inf;
    CHECK(mdir::ToCamera(10, 0, s).yaw == yaw);
    s.sens_x_deg = -inf;
    CHECK(mdir::ToCamera(10, 0, s).yaw == yaw);
    s = defaults;
    s.sens_y_deg = nan;
    CHECK(mdir::ToCamera(0, 10, s).height == height);
    s.sens_y_deg = inf;
    CHECK(mdir::ToCamera(0, 10, s).height == height);
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
  // An early version's keeper counts as alive for a while after it reports in.
  mdir::Boot e;
  CHECK(!e.OldKeeperAlive(0));
  e.OldKeeperSeen(1000);
  CHECK(e.OldKeeperAlive(1000 + B::kOldKeeperUs));
  CHECK(!e.OldKeeperAlive(1000 + B::kOldKeeperUs + 1));
  // After the script has been running, a new search starts from the shortest
  // pause again, not from the pause the earlier searches had grown to.
  d.Alive(u);
  u += B::kAliveUs + 1;
  d.Wanted(u);
  CHECK(d.ShouldTry(u));  // gone while wanted: at once
  d.Wanted(u + B::kMinBackoffUs - 1);
  CHECK(!d.ShouldTry(u + B::kMinBackoffUs - 1));
  d.Wanted(u + B::kMinBackoffUs);
  CHECK(d.ShouldTry(u + B::kMinBackoffUs));  // 5 s later, not 30
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

  // The reply to "old" is one number: the slot after it stays untouched.
  const float one[1] = {1.0f};
  std::memset(out, 0xAA, sizeof out);
  mdir::EncodeNumbers(one, 1, out);
  CHECK(std::memcmp(out, want, 8) == 0);
  bool untouched = true;
  for (int i = 8; i < 16; ++i) untouched = untouched && out[i] == 0xAA;
  CHECK(untouched);
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
  TestTimeZero();
  TestRouteTimeout();
  TestBoot();
  TestEncodeNumbers();
  TestInjectProfiles();
  TestNonFiniteSettings();
  if (failures) {
    std::printf("%d failure(s)\n", failures);
    return 1;
  }
  if (known_bugs) std::printf("%d known bug(s), see KNOWN BUG above\n", known_bugs);
  std::printf("mouse direct tests passed\n");
  return 0;
}
