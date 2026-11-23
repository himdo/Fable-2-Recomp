// Standalone test for the direct mouse camera helpers (src/input/mouse_direct.h)
// and the inject speed profiles (src/input/mouse_diag.h). No game, GPU, SDL or
// audio device needed.

#include <cmath>
#include <cstring>
#include <limits>
#include <string>

#include "catch_amalgamated.hpp"
#include "src/input/mouse_diag.h"
#include "src/input/mouse_direct.h"

namespace mdir = fable2::mousedirect;
using Catch::Matchers::WithinAbs;
using fable2::mousediag::InjectPlan;
using fable2::mousediag::InjectProfile;

static mdir::Settings Sens012() {
  mdir::Settings s;  // scale 256
  s.sens_x_deg = s.sens_y_deg = 0.12;
  return s;
}

TEST_CASE("ToCamera", "[mouse][direct]") {
  mdir::Settings s = Sens012();

  SECTION("defaults to 0.06 degrees per count") {
    CHECK(mdir::Settings{}.sens_x_deg == 0.06);
    CHECK(mdir::Settings{}.sens_y_deg == 0.06);
  }

  SECTION("turns right for mouse right, like stick right (negative yaw)") {
    const mdir::CameraDelta d = mdir::ToCamera(100, 0, s);
    CHECK_THAT(d.yaw, WithinAbs(-12.0, 1e-9));
    CHECK_THAT(d.height, WithinAbs(0.0, 1e-9));
    CHECK_THAT(mdir::ToCamera(-50, 0, s).yaw, WithinAbs(6.0, 1e-9));
  }

  SECTION("multiplies by mouse_look_scale / 256") {
    s.scale = 512;
    CHECK_THAT(mdir::ToCamera(100, 0, s).yaw, WithinAbs(-24.0, 1e-9));
  }

  SECTION("treats a zero or negative scale as the smallest, never as 0 or reversed") {
    s.scale = 0;
    CHECK_THAT(mdir::ToCamera(256, 0, s).yaw, WithinAbs(-0.12, 1e-9));
    s.scale = -256;
    CHECK_THAT(mdir::ToCamera(256, 0, s).yaw, WithinAbs(-0.12, 1e-9));
  }

  SECTION("lowers the height offset for mouse up (look up), like stick up") {
    const mdir::CameraDelta d = mdir::ToCamera(0, -100, s);
    CHECK_THAT(d.height, WithinAbs(-100 * 0.12 * mdir::kDegToRad, 1e-9));
    CHECK_THAT(d.yaw, WithinAbs(0.0, 1e-9));
  }

  SECTION("inverts up/down with invert_y") {
    s.invert_y = true;
    CHECK_THAT(mdir::ToCamera(0, -100, s).height, WithinAbs(100 * 0.12 * mdir::kDegToRad, 1e-9));
  }

  SECTION("uses separate sensitivities for x and y") {
    s.sens_x_deg = 0.5;
    s.sens_y_deg = 0.25;
    const mdir::CameraDelta d = mdir::ToCamera(10, 10, s);
    CHECK_THAT(d.yaw, WithinAbs(-5.0, 1e-9));
    CHECK_THAT(d.height, WithinAbs(2.5 * mdir::kDegToRad, 1e-9));
  }
}

TEST_CASE("DirectState", "[mouse][direct]") {
  const int64_t timeout = 200000;
  double dx = 0, dy = 0;
  mdir::DirectState st;

  SECTION("keeps nothing until the camera has asked, then delivers it once") {
    CHECK_FALSE(st.Feed(true, 1000, timeout, 10, 5));  // still on the stick
    st.Drain(true, 2000, 0.0, dx, dy);
    CHECK(dx == 0);
    CHECK(dy == 0);
    CHECK(st.Feed(true, 3000, timeout, 10, 5));  // now the route is direct
    CHECK(st.Feed(true, 4000, timeout, -3, 1));
    CHECK(st.RouteDirect());
    st.Drain(true, 5000, 0.0, dx, dy);
    CHECK_THAT(dx, WithinAbs(7, 1e-9));
    CHECK_THAT(dy, WithinAbs(6, 1e-9));
    st.Drain(true, 6000, 0.0, dx, dy);
    CHECK(dx == 0);
    CHECK(dy == 0);
    CHECK(st.Drains() == 3);
  }

  SECTION("never keeps movement in stick mode, even with the camera asking") {
    st.Drain(true, 1000, 0.0, dx, dy);
    CHECK_FALSE(st.Feed(false, 2000, timeout, 10, 0));
    CHECK_FALSE(st.RouteDirect());
    st.Drain(true, 3000, 0.0, dx, dy);
    CHECK(dx == 0);
  }

  SECTION("drops kept movement and goes back to the stick when the camera can't turn") {
    // e.g. target lock
    st.Drain(true, 1000, 0.0, dx, dy);
    CHECK(st.Feed(true, 2000, timeout, 40, 0));
    st.Drain(false, 3000, 0.0, dx, dy);
    CHECK(dx == 0);
    CHECK(dy == 0);
    CHECK_FALSE(st.Feed(true, 4000, timeout, 40, 0));
    st.Drain(true, 5000, 0.0, dx, dy);
    CHECK(dx == 0);  // nothing from before the lock comes out later
  }

  SECTION("goes back to the stick when the camera stops asking, and returns when it asks again") {
    // cutscene, menu, another camera
    st.Drain(true, 1000, 0.0, dx, dy);
    CHECK(st.Feed(true, 100000, timeout, 25, 0));
    CHECK(st.Feed(true, 201000, timeout, 25, 0));  // exactly at the timeout: still direct
    CHECK_FALSE(st.Feed(true, 201001, timeout, 25, 0));
    st.Drain(true, 300000, 0.0, dx, dy);
    CHECK(dx == 0);
    CHECK(st.Feed(true, 300001, timeout, 25, 0));
  }

  SECTION("drops kept movement on Clear") {
    // mouse look off, debug menu open
    st.Drain(true, 1000, 0.0, dx, dy);
    st.Feed(true, 2000, timeout, 9, 9);
    st.Clear();
    st.Drain(true, 3000, 0.0, dx, dy);
    CHECK(dx == 0);
    CHECK(dy == 0);
  }

  SECTION("smoothing holds a share back, delivers the total and ends the tail") {
    st.Drain(true, 1000, 0.5, dx, dy);
    st.Feed(true, 2000, timeout, 8, -4);
    st.Drain(true, 3000, 0.5, dx, dy);
    CHECK_THAT(dx, WithinAbs(4, 1e-9));  // half at once
    CHECK_THAT(dy, WithinAbs(-2, 1e-9));
    double sx = dx, sy = dy;
    int drains = 1;
    for (int i = 1; i < 100; ++i) {
      st.Drain(true, 3000 + i, 0.5, dx, dy);
      if (dx == 0 && dy == 0) break;
      sx += dx;
      sy += dy;
      ++drains;
    }
    CHECK_THAT(sx, WithinAbs(8, 1e-9));
    CHECK_THAT(sy, WithinAbs(-4, 1e-9));
    CHECK(drains < 10);
  }

  SECTION("clamps smoothing above 0.9 to 0.9, so movement always arrives") {
    st.Drain(true, 1000, 0.0, dx, dy);
    st.Feed(true, 5000, timeout, 10, 0);
    st.Drain(true, 6000, 5.0, dx, dy);
    CHECK_THAT(dx, WithinAbs(1.0, 1e-9));
  }

  SECTION("clamps negative smoothing to 0: everything at once") {
    st.Drain(true, 1000, 0.0, dx, dy);
    st.Feed(true, 2000, timeout, 10, -6);
    st.Drain(true, 3000, -1.0, dx, dy);
    CHECK_THAT(dx, WithinAbs(10, 1e-9));
    CHECK_THAT(dy, WithinAbs(-6, 1e-9));
  }

  SECTION("delivers a large flick in full at the 0.9 maximum, and the tail ends") {
    // after ~94 camera updates for 1000 counts: about 6 s at 15 Hz
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
    CHECK_THAT(sx, WithinAbs(1000, 1e-6));
    CHECK(drains < 100);
  }
}

// NowUs() counts from the first call, so the first camera update or report can
// be at t=0. Each "last seen" time stores 0 as 1, so t=0 still counts as seen.
TEST_CASE("time 0 counts as seen", "[mouse][direct]") {
  SECTION("for a camera update") {
    double dx = 0, dy = 0;
    mdir::DirectState st;
    st.Drain(true, 0, 0.0, dx, dy);
    CHECK(st.Feed(true, 0, 200000, 1, 0));
  }

  SECTION("for a script report and an old keeper's report") {
    mdir::Boot b;
    b.Alive(0);
    CHECK(b.IsAlive(0));
    b.OldKeeperSeen(0);
    CHECK(b.OldKeeperAlive(0));
  }
}

// Settings come from fable2_config.toml, where `nan` and `inf` are valid
// numbers, and the config reader doesn't reject them. They fall back to the
// defaults: no smoothing, and the default sensitivity.
TEST_CASE("non-finite settings", "[mouse][direct]") {
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();

  SECTION("NaN or infinite smoothing acts as no smoothing and poisons nothing") {
    // NaN gets past std::clamp, so it has to be caught first.
    double dx = 0, dy = 0;
    mdir::DirectState st;
    st.Drain(true, 1000, 0.0, dx, dy);
    st.Feed(true, 2000, 200000, 10, 0);
    st.Drain(true, 3000, nan, dx, dy);
    CHECK(dx == 10);
    CHECK(dy == 0);
    st.Feed(true, 4000, 200000, 5, 0);
    st.Drain(true, 5000, 0.0, dx, dy);
    CHECK(dx == 5);
    st.Feed(true, 6000, 200000, 5, 0);
    st.Drain(true, 7000, inf, dx, dy);
    CHECK(dx == 5);
  }

  SECTION("a NaN or infinite sensitivity falls back to the default") {
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

TEST_CASE("RouteTimeoutUs", "[mouse][direct]") {
  SECTION("is 2.5 camera updates, at least 200 ms") {
    CHECK(mdir::RouteTimeoutUs(66667) == 200000);
    CHECK(mdir::RouteTimeoutUs(33333) == 200000);
    CHECK(mdir::RouteTimeoutUs(100000) == 250000);
  }
}

TEST_CASE("Boot (starting mouse_direct.lua)", "[mouse][direct]") {
  using B = mdir::Boot;
  const int64_t t0 = 1'000'000;

  SECTION("never tries without mouse movement the camera didn't take") {
    // e.g. sitting in a menu or alt-tabbed, where the game's scripts stand still
    mdir::Boot b;
    CHECK_FALSE(b.ShouldTry(t0));
    CHECK_FALSE(b.ShouldTry(t0 + 60 * B::kFirstDelayUs));
  }

  SECTION("waits after the game starts, then retries with a growing pause while wanted") {
    mdir::Boot c;
    c.Wanted(t0);
    CHECK_FALSE(c.ShouldTry(t0));
    int64_t t = t0 + B::kFirstDelayUs;
    c.Wanted(t);
    CHECK(c.ShouldTry(t));
    c.Wanted(t + B::kMinBackoffUs - 1);
    CHECK_FALSE(c.ShouldTry(t + B::kMinBackoffUs - 1));
    CHECK(c.ShouldTry(t + B::kMinBackoffUs));
    t += B::kMinBackoffUs;
    CHECK_FALSE(c.ShouldTry(t + 2 * B::kMinBackoffUs));  // the wish is older than kWantedUs
    c.Wanted(t + 2 * B::kMinBackoffUs);
    CHECK(c.ShouldTry(t + 2 * B::kMinBackoffUs));
    CHECK(c.Tries() == 3);

    // Once the script reports in, no tries while it keeps doing so.
    t += 2 * B::kMinBackoffUs;
    c.Alive(t);
    c.Wanted(t + B::kAliveUs);
    CHECK(c.IsAlive(t + B::kAliveUs));
    CHECK_FALSE(c.ShouldTry(t + B::kAliveUs));
    // Silent for a long time is fine on its own...
    CHECK_FALSE(c.IsAlive(t + B::kAliveUs + 1));
    CHECK_FALSE(c.ShouldTry(t + 10 * B::kAliveUs));
    // ...but silent while the mouse is moved means it's gone: try at once.
    c.Wanted(t + 10 * B::kAliveUs);
    CHECK(c.ShouldTry(t + 10 * B::kAliveUs));
  }

  SECTION("caps the pause, and restarts from the shortest pause after the script ran") {
    mdir::Boot d;
    int64_t u = 0;
    d.ShouldTry(u);
    u += B::kFirstDelayUs;
    for (int i = 0; i < 20; ++i) {
      CAPTURE(i);
      d.Wanted(u);
      CHECK(d.ShouldTry(u));
      u += B::kMaxBackoffUs;
    }
    d.Alive(u);
    u += B::kAliveUs + 1;
    d.Wanted(u);
    CHECK(d.ShouldTry(u));  // gone while wanted: at once
    d.Wanted(u + B::kMinBackoffUs - 1);
    CHECK_FALSE(d.ShouldTry(u + B::kMinBackoffUs - 1));
    d.Wanted(u + B::kMinBackoffUs);
    CHECK(d.ShouldTry(u + B::kMinBackoffUs));  // 5 s later, not 30
  }

  SECTION("counts an early version's keeper as alive for a while after it reports in") {
    mdir::Boot e;
    CHECK_FALSE(e.OldKeeperAlive(0));
    e.OldKeeperSeen(1000);
    CHECK(e.OldKeeperAlive(1000 + B::kOldKeeperUs));
    CHECK_FALSE(e.OldKeeperAlive(1000 + B::kOldKeeperUs + 1));
  }

  SECTION("reports the first heartbeat after each start once") {
    mdir::Boot d;
    d.Booted();
    CHECK(d.FirstHeartbeat());
    CHECK_FALSE(d.FirstHeartbeat());
    d.Booted();
    CHECK(d.FirstHeartbeat());
  }
}

TEST_CASE("EncodeNumbers", "[mouse][direct]") {
  const uint8_t want[16] = {0x3F, 0x80, 0x00, 0x00, 0, 0, 0, 3,
                            0xC1, 0x48, 0x00, 0x00, 0, 0, 0, 3};
  uint8_t out[16];
  std::memset(out, 0xAA, sizeof out);

  SECTION("writes two Lua 5.1 number values") {
    const float v[2] = {1.0f, -12.5f};
    mdir::EncodeNumbers(v, 2, out);
    CHECK(std::memcmp(out, want, sizeof want) == 0);
  }

  SECTION("writes one number and leaves the next slot untouched") {
    // the reply to "old"
    const float one[1] = {1.0f};
    mdir::EncodeNumbers(one, 1, out);
    CHECK(std::memcmp(out, want, 8) == 0);
    for (int i = 8; i < 16; ++i) {
      CAPTURE(i);
      CHECK(out[i] == 0xAA);
    }
  }
}

// The aim stick swap (src/input/mouse_aim.h) feeds with direct_mode = false
// while aiming. That must drop what was kept but not forget the camera's last
// report, so the direct route is back on the first poll after aiming (within
// the timeout), and nothing moved while aiming turns the camera afterwards.
TEST_CASE("DirectState while aiming", "[mouse][direct][aim]") {
  const int64_t timeout = 200000;
  double dx = 0, dy = 0;
  mdir::DirectState st;
  st.Drain(true, 1000, 0.0, dx, dy);                    // the camera is accepting
  CHECK(st.Feed(true, 2000, timeout, 4, 0));            // kept for it
  CHECK_FALSE(st.Feed(false, 3000, timeout, 50, 50));   // aiming: to the reticle
  CHECK_FALSE(st.RouteDirect());
  CHECK_FALSE(st.Feed(false, 50000, timeout, -20, 10));  // still aiming
  CHECK(st.Feed(true, 150000, timeout, 3, 0));          // aim over, still within the timeout
  st.Drain(true, 160000, 0.0, dx, dy);
  // neither the 4 nor the aim movement
  CHECK_THAT(dx, WithinAbs(3.0, 1e-9));
  CHECK_THAT(dy, WithinAbs(0.0, 1e-9));
  // A long aim outlives the timeout: back on the stick until the camera
  // reports again, then direct.
  CHECK_FALSE(st.Feed(false, 170000, timeout, 5, 5));
  CHECK_FALSE(st.Feed(true, 160000 + timeout + 1, timeout, 7, 0));
  st.Drain(true, 400000, 0.0, dx, dy);
  CHECK(dx == 0);
  CHECK(dy == 0);
  CHECK(st.Feed(true, 401000, timeout, 2, 0));
}

TEST_CASE("InjectPlan speed profiles", "[mouse][diag]") {
  SECTION("every profile moves monotonically and delivers the same total") {
    for (InjectProfile p : {InjectProfile::kEven, InjectProfile::kRamp, InjectProfile::kFlick}) {
      CAPTURE(fable2::mousediag::ProfileName(p));
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
      CHECK(x == 4000);
      CHECK(y == -1000);
      plan.DeliveredBy(250000, x, y);
      CHECK(x == 4000);
      CHECK(y == -1000);
      plan.DeliveredBy(0, x, y);
      CHECK(x == 0);
      CHECK(y == 0);
    }
  }

  SECTION("ramp covers a quarter of the distance in half the time") {
    int32_t x = 0, y = 0;
    InjectPlan ramp{4000, 0, 200000, InjectProfile::kRamp};
    ramp.DeliveredBy(100000, x, y);
    CHECK(x == 1000);
  }

  SECTION("flick is symmetric, starts slowly and peaks at 1.875x the average speed") {
    int32_t x = 0, y = 0;
    InjectPlan flick{4000, 0, 200000, InjectProfile::kFlick};
    flick.DeliveredBy(100000, x, y);
    CHECK(x == 2000);  // half the distance at the midpoint
    flick.DeliveredBy(20000, x, y);
    CHECK(x < 400 / 4);  // well under the even 400 px after 10%
    int32_t a = 0, b = 0;
    flick.DeliveredBy(99000, a, y);
    flick.DeliveredBy(101000, b, y);
    CHECK(std::abs((b - a) - 2 * 4000.0 / 200 * 1.875) <= 2);
  }

  SECTION("names and parses the profiles") {
    CHECK(std::string(fable2::mousediag::ProfileName(InjectProfile::kFlick)) == "flick");
    InjectProfile parsed = InjectProfile::kEven;
    CHECK(fable2::mousediag::ParseProfile("ramp", parsed));
    CHECK(parsed == InjectProfile::kRamp);
    CHECK_FALSE(fable2::mousediag::ParseProfile("fast", parsed));
  }
}
