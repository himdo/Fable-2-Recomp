// Standalone test for the aim stick swap helpers (src/input/mouse_aim.h). No
// game, GPU, SDL or audio device needed.
//
// Covers:
//   - mode names: parsing (any case), the name of each mode, unknown names
//     rejected, and an unknown cvar value falling back to "both";
//   - ShouldSwap: the truth table of each mode;
//   - SwapActive: never in photo mode or with the mouse not live; an
//     unfocused inject only swaps in "detect" mode;
//   - AimState and AimTimeoutUs: nothing detected before the first cursor
//     update, a report at time 0 still counts, the timeout edge, the
//     timeout's 100 ms floor, a hook timestamp slightly ahead of the poll's
//     clock, and the RecentlyAimed grace that holds off restarting
//     mouse_direct.lua;
//   - the logged cursor: round trip of the floats and the update count;
//   - the logged highlight state (Targeting): nothing noted at first, each
//     flag alone, the age round trip and its saturation, last note wins;
//   - the logged target position (TargetPos): NaN and "not computed" at
//     first, an exact round trip in front of the camera, x/y dropped in
//     every other state, NoteNoTarget;
//   - mouse release: the ShouldRelease truth table, HoldPicks edges (never
//     moved, off, inside/at/after kPickHoldUs, clock skew), the last movement
//     and counters, and PickHere being neutral by default and per thread;
//   - aim assist and the wheel hop: PickScale (on/off, mouse used, hop, bad
//     values, clamping), NewAimSession edges, MouseUsedThisAim across aims,
//     WheelHop (direction, one notch per Take, the pending cap, turning back,
//     smooth-scroll fractions, stale dropped, Clear), HopDirection, HopReady
//     and LockPull (holds within the stickiness, lets go with the whole pull,
//     0/NaN/over-max stickiness, non-finite movement);
//   - slow-movement magnetism: TickSpeed (clamped time, NaN never slow),
//     SlowMove, NudgeStick (direction, just above the threshold, bad epsilon,
//     cap), PackStick/UnpackStick/StickIsZero (word order, -0), and
//     NudgeOvershoot (forward only, clamp and big moves left alone);
//   - StickForAim: through the game's response (GameAimControl) it gives back
//     the wanted input, in any direction; tiny inputs still move the stick;
//     exactly 1 is full stick and not capped; magnitudes over 1 are capped;
//     odd symmetry; monotonic; every direction stays inside int16 (never
//     -32768); NaN and infinity give a centred stick; the y sign follows the
//     input (mouse up = aim up comes from mousestick::WindowToControl);
//   - the same mouse speed gives the same stick at 15 and 30 Hz, and the
//     scale is linear;
//   - AimStick, the driver's aim path: a steady stick for an even speed,
//     exactly 0 one tick after the mouse stops (not before), Reset() so a new
//     aim doesn't replay old movement, a stalled poll doesn't spike, a poll
//     without movement adds nothing, and the scale matches double speed;
//   - direct aim: the input names, ToCursor (signs, up/down following the
//     screen shape at 16:9, 21:9 and 4:3 with a fallback for a bad aspect,
//     linear with no cap, one count moves it, the ratio, invert, non-finite
//     fallback), AimDirect
//     (adds up, taken once, Clear, no overflow, nothing lost across threads)
//     and the settings the hook reads.

#include <atomic>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <thread>

#include "src/input/mouse_aim.h"
#include "src/input/mouse_stick.h"

namespace ma = fable2::mouseaim;
using ma::AimSwapMode;

static int failures = 0;
#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
      ++failures;                                                            \
    }                                                                        \
  } while (0)

static void TestModes() {
  AimSwapMode m = AimSwapMode::kOff;
  CHECK(ma::ParseAimSwapMode("rmb", m) && m == AimSwapMode::kRmb);
  CHECK(ma::ParseAimSwapMode("DETECT", m) && m == AimSwapMode::kDetect);
  CHECK(ma::ParseAimSwapMode("Both", m) && m == AimSwapMode::kBoth);
  CHECK(ma::ParseAimSwapMode("off", m) && m == AimSwapMode::kOff);
  m = AimSwapMode::kRmb;
  CHECK(!ma::ParseAimSwapMode("aim", m) && m == AimSwapMode::kRmb);  // unchanged
  CHECK(!ma::ParseAimSwapMode("", m));
  for (AimSwapMode each : {AimSwapMode::kOff, AimSwapMode::kRmb, AimSwapMode::kDetect, AimSwapMode::kBoth}) {
    AimSwapMode back = AimSwapMode::kOff;
    CHECK(ma::ParseAimSwapMode(ma::AimSwapModeName(each), back) && back == each);
  }
  CHECK(ma::ModeOrDefault("rubbish") == AimSwapMode::kBoth);
  CHECK(ma::ModeOrDefault("detect") == AimSwapMode::kDetect);
}

static void TestShouldSwap() {
  for (bool key : {false, true}) {
    for (bool detected : {false, true}) {
      CHECK(!ma::ShouldSwap(AimSwapMode::kOff, key, detected));
      CHECK(ma::ShouldSwap(AimSwapMode::kRmb, key, detected) == key);
      CHECK(ma::ShouldSwap(AimSwapMode::kDetect, key, detected) == detected);
      CHECK(ma::ShouldSwap(AimSwapMode::kBoth, key, detected) == (key && detected));
    }
  }
}

static void TestSwapActive() {
  const AimSwapMode kAll[] = {AimSwapMode::kOff, AimSwapMode::kRmb, AimSwapMode::kDetect, AimSwapMode::kBoth};
  for (AimSwapMode m : kAll) {
    // Photo mode's free camera owns the mouse, whatever the signals say.
    CHECK(!ma::SwapActive(true, true, true, m, true, true));
    // Mouse not live (unfocused, F4 menu open, mouse_look off) and no inject: never.
    CHECK(!ma::SwapActive(false, false, false, m, true, true));
    // Live mouse: exactly the mode's rule.
    for (bool key : {false, true})
      for (bool det : {false, true})
        CHECK(ma::SwapActive(false, true, false, m, key, det) == ma::ShouldSwap(m, key, det));
  }
  // An unfocused `f2 mouse inject` sees no aim key (the key is only read with
  // focus), so only "detect" can swap for it.
  CHECK(ma::SwapActive(false, false, true, AimSwapMode::kDetect, false, true));
  CHECK(!ma::SwapActive(false, false, true, AimSwapMode::kBoth, false, true));
  CHECK(!ma::SwapActive(false, false, true, AimSwapMode::kRmb, false, true));
}

static void TestAimState() {
  ma::AimState s;
  CHECK(s.LastTickUs() == 0);
  CHECK(!s.Detected(0, 1000000));  // never aimed
  s.NoteAimTick(0);  // NowUs() starts at 0: a cursor update then still counts
  CHECK(s.Detected(0, 100000));
  s.NoteAimTick(1000000);
  CHECK(s.Detected(1000000, 166667));
  CHECK(s.Detected(1166667, 166667));   // at the edge
  CHECK(!s.Detected(1166668, 166667));  // just past it
  // Five LF ticks at 15 Hz, and the 250 ms floor at 30 Hz: the game was seen
  // updating the cursor only every 3rd tick (200 ms) while aiming.
  CHECK(ma::AimTimeoutUs(fable2::mousestick::DefaultWindowUs(false)) == 333335);
  CHECK(ma::AimTimeoutUs(fable2::mousestick::DefaultWindowUs(true)) == 250000);
  CHECK(ma::AimTimeoutUs(0) == 250000);
  CHECK(ma::AimTimeoutUs(fable2::mousestick::DefaultWindowUs(false)) > 3 * 66667);

  // The hook runs on a guest thread: its timestamp can be a little ahead of
  // the `now` the poll read just before. That still counts.
  s.NoteAimTick(2000000);
  CHECK(s.Detected(1999990, 166667));

  // The grace before mouse_direct.lua may be restarted after aiming.
  s.NoteAimTick(5000000);
  CHECK(s.RecentlyAimed(5000000 + ma::kAimBootGraceUs));
  CHECK(!s.RecentlyAimed(5000001 + ma::kAimBootGraceUs));
  CHECK(ma::kAimBootGraceUs > ma::AimTimeoutUs(fable2::mousestick::DefaultWindowUs(false)));
  ma::AimState never;
  CHECK(!never.RecentlyAimed(0) && !never.RecentlyAimed(123456789));
}

static void TestCursor() {
  ma::AimState s;
  float x = 1, y = 1;
  uint64_t ticks = 7;
  s.Cursor(x, y, ticks);
  CHECK(x == 0.0f && y == 0.0f && ticks == 0);  // nothing yet
  s.NoteCursor(-0.8f, 0.25f);
  s.Cursor(x, y, ticks);
  CHECK(x == -0.8f && y == 0.25f && ticks == 1);
  s.NoteCursor(0.0f, -0.0f);
  s.NoteCursor(0.5f, -0.75f);
  s.Cursor(x, y, ticks);
  CHECK(x == 0.5f && y == -0.75f && ticks == 3);
  // Whatever bits the game left there come back as they were (the log shows
  // them), including non-finite ones.
  s.NoteCursor(std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN());
  s.Cursor(x, y, ticks);
  CHECK(std::isinf(x) && std::isnan(y) && ticks == 4);
}

static void TestTargeting() {
  // Nothing noted yet: the log prints hl=-1.
  ma::AimState s;
  ma::Targeting t = s.LastTargeting();
  CHECK(!t.noted && !t.handle_set && !t.flick && !t.held && !t.b234 && t.age == 0);

  // Each flag on its own comes back alone, and "noted" is always set.
  t = ma::UnpackTargeting(ma::PackTargeting(false, false, false, false, 0));
  CHECK(t.noted && !t.handle_set && !t.flick && !t.held && !t.b234 && t.age == 0);
  t = ma::UnpackTargeting(ma::PackTargeting(true, false, false, false, 0));
  CHECK(t.handle_set && !t.flick && !t.held && !t.b234);
  t = ma::UnpackTargeting(ma::PackTargeting(false, true, false, false, 0));
  CHECK(!t.handle_set && t.flick && !t.held && !t.b234);
  t = ma::UnpackTargeting(ma::PackTargeting(false, false, true, false, 0));
  CHECK(!t.handle_set && !t.flick && t.held && !t.b234);
  t = ma::UnpackTargeting(ma::PackTargeting(false, false, false, true, 0));
  CHECK(!t.handle_set && !t.flick && !t.held && t.b234);

  // The age round-trips, doesn't spill into the flags, and saturates.
  t = ma::UnpackTargeting(ma::PackTargeting(true, true, true, true, 12345));
  CHECK(t.handle_set && t.flick && t.held && t.b234 && t.age == 12345);
  t = ma::UnpackTargeting(ma::PackTargeting(false, false, false, false, ma::kTargetingAgeMax));
  CHECK(t.age == ma::kTargetingAgeMax && !t.handle_set && !t.b234);
  t = ma::UnpackTargeting(ma::PackTargeting(false, false, false, false, 0xFFFFFFFFu));
  CHECK(t.age == ma::kTargetingAgeMax && !t.handle_set && !t.flick);

  // Through AimState: the last note wins.
  s.NoteTargeting(ma::PackTargeting(true, false, false, false, 40));
  s.NoteTargeting(ma::PackTargeting(false, true, false, false, 0));
  t = s.LastTargeting();
  CHECK(t.noted && !t.handle_set && t.flick && t.age == 0);
}

static void TestTargetPos() {
  // Nothing noted yet: the log prints tgt=nan,nan tf=-1.
  ma::AimState s;
  ma::TargetPos p = s.Target();
  CHECK(std::isnan(p.x) && std::isnan(p.y) && p.front == ma::kTargetNotComputed);

  // In front: the position round-trips exactly, negative values included.
  s.NoteTarget(0.3125f, -0.71875f, ma::kTargetInFront);
  p = s.Target();
  CHECK(p.x == 0.3125f && p.y == -0.71875f && p.front == ma::kTargetInFront);

  // Behind the camera, no live target, not computed: x/y are dropped (NaN),
  // even when the hook passes numbers.
  for (const int32_t front : {ma::kTargetBehind, ma::kTargetInvalid, ma::kTargetNotComputed}) {
    s.NoteTarget(0.5f, 0.5f, ma::kTargetInFront);
    s.NoteTarget(0.25f, -0.25f, front);
    p = s.Target();
    CHECK(std::isnan(p.x) && std::isnan(p.y) && p.front == front);
  }

  // NoteNoTarget is the not-computed state.
  s.NoteTarget(0.5f, 0.5f, ma::kTargetInFront);
  s.NoteNoTarget();
  p = s.Target();
  CHECK(std::isnan(p.x) && std::isnan(p.y) && p.front == ma::kTargetNotComputed);
}

static void TestMouseRelease() {
  // Lets go only with release on, mouse movement this tick, and a live target.
  for (int bits = 0; bits < 8; ++bits) {
    const bool on = bits & 1, moved = bits & 2, valid = bits & 4;
    CHECK(ma::ShouldRelease(on, moved, valid) == (on && moved && valid));
  }

  // Picks are held while the mouse moved within kPickHoldUs, never when the
  // mouse never moved or release is off.
  const int64_t t = 5000000;
  CHECK(!ma::HoldPicks(true, t, 0));
  CHECK(!ma::HoldPicks(false, t, t));
  CHECK(ma::HoldPicks(true, t, t));
  CHECK(ma::HoldPicks(true, t + ma::kPickHoldUs - 1, t));
  CHECK(!ma::HoldPicks(true, t + ma::kPickHoldUs, t));
  CHECK(!ma::HoldPicks(true, t + 10 * ma::kPickHoldUs, t));
  // A movement stamped just after the hook's clock read still holds.
  CHECK(ma::HoldPicks(true, t - 100, t));

  // The last movement and the counters, through AimState.
  ma::AimState s;
  CHECK(s.LastMoveUs() == 0 && s.Releases() == 0 && s.SuppressedPicks() == 0);
  s.NoteMouseMove(0);  // time 0 still counts as moved
  CHECK(s.LastMoveUs() != 0);
  s.NoteMouseMove(t);
  CHECK(s.LastMoveUs() == t);
  s.NoteRelease();
  s.NoteRelease();
  s.NoteSuppressedPick();
  CHECK(s.Releases() == 2 && s.SuppressedPicks() == 1);

  // The pick mode is neutral by default and per thread: setting it on one
  // thread never changes a search on another.
  CHECK(!ma::PickHere().suppress && ma::PickHere().scale == 1.0);
  ma::PickHere() = ma::PickMode{true, 3.0};
  ma::PickMode other{true, 3.0};
  std::thread([&] { other = ma::PickHere(); }).join();
  CHECK(ma::PickHere().suppress && ma::PickHere().scale == 3.0);
  CHECK(!other.suppress && other.scale == 1.0);
  ma::PickHere() = ma::PickMode{};
}

static void TestAimAssistAndWheel() {
  // The rest zone is scaled only with release on, for a mouse user, never on
  // a hop; bad values give the game's own (1); good ones are clamped.
  CHECK(ma::PickScale(true, true, false, 2.0) == 2.0);
  CHECK(ma::PickScale(false, true, false, 2.0) == 1.0);
  CHECK(ma::PickScale(true, false, false, 2.0) == 1.0);
  CHECK(ma::PickScale(true, true, true, 2.0) == 1.0);
  CHECK(ma::PickScale(true, true, false, std::numeric_limits<double>::quiet_NaN()) == 1.0);
  CHECK(ma::PickScale(true, true, false, std::numeric_limits<double>::infinity()) == 1.0);
  CHECK(ma::PickScale(true, true, false, 0.0) == 1.0);
  CHECK(ma::PickScale(true, true, false, -2.0) == 1.0);
  CHECK(ma::PickScale(true, true, false, 0.1) == ma::kAimAssistMin);
  CHECK(ma::PickScale(true, true, false, 100.0) == ma::kAimAssistMax);

  // A new aim: the first update, or a gap longer than kAimSessionGapUs.
  const int64_t t = 9000000;
  CHECK(ma::NewAimSession(0, t));
  CHECK(!ma::NewAimSession(t, t + 33000));
  CHECK(!ma::NewAimSession(t, t + ma::kAimSessionGapUs));
  CHECK(ma::NewAimSession(t, t + ma::kAimSessionGapUs + 1));

  // "The mouse moved during this aim".
  ma::AimState s;
  CHECK(!s.MouseUsedThisAim());
  s.NoteAimSession(t);
  CHECK(!s.MouseUsedThisAim());
  s.NoteMouseMove(t - 1000);  // before this aim (an earlier one)
  CHECK(!s.MouseUsedThisAim());
  s.NoteMouseMove(t);  // the same update the aim began in
  CHECK(s.MouseUsedThisAim());
  s.NoteAimSession(t + 2000000);  // the next aim
  CHECK(!s.MouseUsedThisAim());
  CHECK(s.Hops() == 0 && s.LastHopUs() == 0);
  s.NoteHop(t);
  CHECK(s.Hops() == 1 && s.LastHopUs() == t);
  s.NoteHop(0);  // time 0 still counts as a hop
  CHECK(s.Hops() == 2 && s.LastHopUs() != 0);

  // Wheel: up = right, down = left, one notch per Take.
  const int32_t d = ma::kScrollPerDetent;
  ma::WheelHop w;
  CHECK(w.Take(t) == 0);
  w.Feed(0, t);
  CHECK(w.Take(t) == 0 && w.Pending() == 0);
  w.Feed(d, t);
  CHECK(w.Take(t + 1000) == 1);
  CHECK(w.Take(t + 1000) == 0);  // taken
  // Several notches wait their turn (capped), one per Take.
  w.Feed(-2 * d, t);
  CHECK(w.Pending() == -2);
  CHECK(w.Take(t) == -1 && w.Take(t) == -1 && w.Take(t) == 0);
  w.Feed(10 * d, t);
  CHECK(w.Pending() == ma::kHopPendingMax);
  w.Clear();
  CHECK(w.Pending() == 0 && w.Take(t) == 0);
  // Turning the wheel back replaces what was waiting.
  w.Feed(2 * d, t);
  w.Feed(-d, t + 10);
  CHECK(w.Pending() == -1 && w.Take(t + 20) == -1 && w.Take(t + 20) == 0);
  // A smooth-scrolling wheel's fractions add up to whole notches.
  for (int i = 0; i < 9; ++i) w.Feed(d / 10, t);
  CHECK(w.Pending() == 0);
  w.Feed(d / 10, t);
  CHECK(w.Pending() == 1);
  w.Clear();
  // ...and a fraction the other way starts the count again.
  for (int i = 0; i < 9; ++i) w.Feed(d / 10, t);
  w.Feed(-d / 10, t);
  for (int i = 0; i < 9; ++i) w.Feed(-d / 10, t);
  CHECK(w.Pending() == -1);
  w.Clear();
  // Nothing scrolled for kWheelFreshUs: everything waiting is dropped.
  w.Feed(2 * d, t);
  CHECK(w.Take(t + ma::kWheelFreshUs) == 1);
  CHECK(w.Take(t + ma::kWheelFreshUs + 1) == 0 && w.Pending() == 0);
  w.Feed(d, 0);  // time 0 still counts
  CHECK(w.Take(100) == 1);

  // Hops at most every mouse_aim_hop_ms.
  CHECK(ma::HopReady(t, 0, 100));        // the first hop
  CHECK(ma::HopReady(t, t, 0));          // no wait
  CHECK(!ma::HopReady(t + 99999, t, 100));
  CHECK(ma::HopReady(t + 100000, t, 100));
  CHECK(!ma::HopReady(t + 999999, t, 5000));  // capped at kHopIntervalMaxMs
  CHECK(ma::HopReady(t + 1000000, t, 5000));
  CHECK(ma::kHopAge > 1000u);  // far above the game's flick gate (a few ticks)

  // Stickiness: small pulls hold, a long one lets go with only the part of
  // the pull beyond the stickiness (no jump by the whole pull).
  ma::LockPull pull;
  double mx = 9.0, my = 9.0;
  CHECK(!pull.Add(0.02, 0.0, 0.05, mx, my) && mx == 0.0 && my == 0.0);
  CHECK(!pull.Add(0.0, 0.03, 0.05, mx, my));  // |(0.02, 0.03)| = 0.036
  CHECK(pull.Add(0.02, 0.01, 0.05, mx, my));  // |(0.04, 0.04)| = 0.0566
  const double over = std::hypot(0.04, 0.04) - 0.05;
  CHECK(std::abs(std::hypot(mx, my) - over) < 1e-12);
  CHECK(std::abs(mx - my) < 1e-15 && mx > 0.0);  // in the pull's direction
  CHECK(pull.X() == 0.0 && pull.Y() == 0.0);     // starts again
  // A slow, steady pull: the release move is about one tick's movement.
  for (int i = 0; i < 9; ++i) CHECK(!pull.Add(0.011, 0.0, 0.1, mx, my));  // 0.099
  CHECK(pull.Add(0.011, 0.0, 0.1, mx, my) && std::abs(mx - 0.01) < 1e-12 && my == 0.0);
  // Exactly the stickiness still holds; pulls in opposite ways cancel.
  CHECK(!pull.Add(0.05, 0.0, 0.05, mx, my));
  CHECK(!pull.Add(-0.05, 0.0, 0.05, mx, my));
  CHECK(!pull.Add(0.05, 0.0, 0.05, mx, my));
  pull.Reset();
  // 0, negative or NaN stickiness: any movement lets go with just itself.
  for (const double st : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN()}) {
    CHECK(pull.Add(0.001, -0.002, st, mx, my) && mx == 0.001 && my == -0.002);
  }
  // Over the maximum counts as the maximum.
  CHECK(!pull.Add(0.4, 0.0, 100.0, mx, my));
  CHECK(pull.Add(0.2, 0.0, 100.0, mx, my) && std::abs(mx - 0.1) < 1e-12);
  // A non-finite movement never sticks, and never poisons the next pull.
  CHECK(pull.Add(std::numeric_limits<double>::infinity(), 0.0, 0.05, mx, my));
  CHECK(!pull.Add(0.01, 0.0, 0.05, mx, my));
}

static void TestMagnetism() {
  const double inf = std::numeric_limits<double>::infinity();
  const double nan = std::numeric_limits<double>::quiet_NaN();

  // Speed: units per second from the move and the time since the last update,
  // the time clamped to 5..100 ms.
  CHECK(std::abs(ma::TickSpeed(0.03, 0.04, 100000) - 0.5) < 1e-12);
  CHECK(std::abs(ma::TickSpeed(0.01, 0.0, 33333) - 0.01 * 1e6 / 33333) < 1e-9);
  CHECK(std::abs(ma::TickSpeed(0.01, 0.0, 0) - 2.0) < 1e-12);         // first tick: 5 ms
  CHECK(std::abs(ma::TickSpeed(0.01, 0.0, 5000000) - 0.1) < 1e-12);   // a stall: 100 ms
  CHECK(ma::TickSpeed(nan, 0.0, 33333) == inf);                       // never "slow"

  // Slow only with release on, movement, a positive speed limit, under it.
  CHECK(ma::SlowMove(true, true, 0.2, 0.5));
  CHECK(!ma::SlowMove(true, true, 0.5, 0.5));
  CHECK(!ma::SlowMove(false, true, 0.2, 0.5));
  CHECK(!ma::SlowMove(true, false, 0.2, 0.5));
  CHECK(!ma::SlowMove(true, true, 0.2, 0.0));   // off
  CHECK(!ma::SlowMove(true, true, 0.2, nan));
  CHECK(!ma::SlowMove(true, true, inf, 0.5));
  CHECK(ma::SlowMove(true, true, 4.9, 100.0));  // capped at kMagnetSpeedMax
  CHECK(!ma::SlowMove(true, true, 5.0, 100.0));

  // The nudge: the move's direction, just above the idle threshold.
  float sx = 0.0f, sy = 0.0f;
  CHECK(ma::NudgeStick(0.03, -0.04, 0.1f, sx, sy));
  CHECK(std::abs(std::hypot(sx, sy) - 0.105) < 1e-6);
  CHECK(sx > 0.0f && sy < 0.0f && std::abs(sx / sy + 0.75) < 1e-5);
  CHECK(std::hypot(sx, sy) > 0.1f);  // above the threshold: not idle
  CHECK(!ma::NudgeStick(0.0, 0.0, 0.1f, sx, sy));
  CHECK(!ma::NudgeStick(nan, 0.0, 0.1f, sx, sy));
  CHECK(ma::NudgeStick(1.0, 0.0, std::numeric_limits<float>::quiet_NaN(), sx, sy));
  CHECK(std::abs(sx - 0.0105f) < 1e-6 && sy == 0.0f);  // bad epsilon: 0.01
  CHECK(ma::NudgeStick(0.0, 1.0, -1.0f, sx, sy) && std::abs(sy - 0.0105f) < 1e-6);
  CHECK(ma::NudgeStick(1.0, 0.0, 10.0f, sx, sy) && sx == 0.25f);  // capped

  // Packing: x in the high word, as the update reads it; round trip, and
  // both zeros count as no stick.
  const uint64_t p = ma::PackStick(0.5f, -0.25f);
  CHECK(p == 0x3F000000BE800000ull);
  float ux = 0.0f, uy = 0.0f;
  ma::UnpackStick(p, ux, uy);
  CHECK(ux == 0.5f && uy == -0.25f);
  CHECK(ma::StickIsZero(0));
  CHECK(ma::StickIsZero(ma::PackStick(-0.0f, 0.0f)));
  CHECK(ma::StickIsZero(ma::PackStick(0.0f, -0.0f)));
  CHECK(!ma::StickIsZero(p));
  CHECK(!ma::StickIsZero(ma::PackStick(0.0f, 1e-9f)));

  // Taking the nudge's own move back: only forward along it, up to the cap.
  CHECK(std::abs(ma::NudgeOvershoot(0.002, 0.0, 1.0, 0.0, 0.05) - 0.002) < 1e-12);
  CHECK(std::abs(ma::NudgeOvershoot(0.003, 0.004, 0.6, 0.8, 0.05) - 0.005) < 1e-12);
  CHECK(ma::NudgeOvershoot(-0.002, 0.0, 1.0, 0.0, 0.05) == 0.0);  // the clamp: leave it
  CHECK(ma::NudgeOvershoot(0.0, 0.002, 1.0, 0.0, 0.05) == 0.0);   // sideways only
  CHECK(ma::NudgeOvershoot(0.06, 0.0, 1.0, 0.0, 0.05) == 0.0);    // too big: not the nudge
  CHECK(ma::NudgeOvershoot(nan, 0.0, 1.0, 0.0, 0.05) == 0.0);

  // Fast and slow movements are tracked apart (HoldPicks follows fast ones).
  ma::AimState s;
  CHECK(s.LastFastMoveUs() == 0 && s.MagnetTicks() == 0);
  s.NoteFastMove(0);
  CHECK(s.LastFastMoveUs() != 0);
  s.NoteFastMove(1234);
  s.NoteMouseMove(5678);
  CHECK(s.LastFastMoveUs() == 1234 && s.LastMoveUs() == 5678);
  s.NoteMagnetTick();
  CHECK(s.MagnetTicks() == 1);

  // A hop only with release on and a highlighted target.
  CHECK(ma::HopDirection(true, 1, true) == 1);
  CHECK(ma::HopDirection(true, -1, true) == -1);
  CHECK(ma::HopDirection(true, 0, true) == 0);
  CHECK(ma::HopDirection(false, 1, true) == 0);
  CHECK(ma::HopDirection(true, 1, false) == 0);
}

// Stick (int16 units) back through the game's response.
static void Response(int32_t lx, int32_t ly, double& cx, double& cy) {
  ma::GameAimControl(lx / ma::kStickMax, ly / ma::kStickMax, cx, cy);
}

static void TestStickForAim() {
  int32_t lx = 1, ly = 1;
  CHECK(!ma::StickForAim(0.0, 0.0, lx, ly) && lx == 0 && ly == 0);

  // Round trip: the game receives what was asked for. Rounding the stick to
  // whole units moves the result by at most about 3·s²/32767 of the input.
  const double wanted[][2] = {{0.5, 0.0}, {0.0, -0.25}, {0.3, 0.3}, {-0.01, 0.002}, {0.9, -0.2}, {1e-4, 0.0}};
  for (const auto& w : wanted) {
    CHECK(!ma::StickForAim(w[0], w[1], lx, ly));
    double cx = 0, cy = 0;
    Response(lx, ly, cx, cy);
    CHECK(std::abs(cx - w[0]) <= 1e-3 && std::abs(cy - w[1]) <= 1e-3);
    // Same direction, in each axis.
    CHECK((w[0] > 0) == (lx > 0) && (w[0] < 0) == (lx < 0));
    CHECK((w[1] > 0) == (ly > 0) && (w[1] < 0) == (ly < 0));
  }

  // The cubic response is undone: a small wanted input needs a large stick.
  ma::StickForAim(0.001, 0.0, lx, ly);
  CHECK(std::abs(lx - 3277) <= 1 && ly == 0);  // cbrt(0.001) = 0.1 of full stick

  // Tiny inputs still move the stick.
  ma::StickForAim(1e-9, 0.0, lx, ly);
  CHECK(lx >= 1 && ly == 0);
  ma::StickForAim(0.0, -1e-9, lx, ly);
  CHECK(lx == 0 && ly <= -1);

  // Over full speed: capped at full stick, direction kept.
  CHECK(ma::StickForAim(3.0, 0.0, lx, ly) && lx == 32767 && ly == 0);
  CHECK(ma::StickForAim(-2.0, -2.0, lx, ly));
  CHECK(std::abs(lx - ly) <= 1 && std::abs(std::hypot(lx, ly) - 32767) <= 2);

  // Non-finite input: centred.
  const double inf = std::numeric_limits<double>::infinity();
  const double nan = std::numeric_limits<double>::quiet_NaN();
  CHECK(!ma::StickForAim(nan, 0.1, lx, ly) && lx == 0 && ly == 0);
  CHECK(!ma::StickForAim(0.1, inf, lx, ly) && lx == 0 && ly == 0);

  // Mouse up (dy < 0) is aim up (+ly), through the same window conversion the
  // input driver uses.
  double cx = 0, cy = 0;
  fable2::mousestick::WindowToControl(0.0, -40.0, 1.0 / 15.0, 256, cx, cy);
  ma::StickForAim(cx, cy, lx, ly);
  CHECK(lx == 0 && ly > 0);

  // Exactly full speed is full stick, not "capped".
  CHECK(!ma::StickForAim(1.0, 0.0, lx, ly) && lx == 32767 && ly == 0);
  CHECK(!ma::StickForAim(0.0, -1.0, lx, ly) && lx == 0 && ly == -32767);

  // Odd symmetry: the opposite input gives the opposite stick.
  for (double c : {1e-6, 0.003, 0.2, 0.7}) {
    int32_t ax = 0, ay = 0, bx = 0, by = 0;
    ma::StickForAim(c, c / 2, ax, ay);
    ma::StickForAim(-c, -c / 2, bx, by);
    CHECK(ax == -bx && ay == -by);
  }

  // Monotonic: a faster mouse never gives a smaller stick.
  int32_t prev = 0;
  for (int i = 1; i <= 2000; ++i) {
    ma::StickForAim(i / 1000.0, 0.0, lx, ly);  // 0.001 .. 2.0 (capped above 1)
    CHECK(lx >= prev);
    prev = lx;
  }
  CHECK(prev == 32767);

  // Every direction at and over full speed stays inside the int16 stick range
  // (never -32768) and on the unit circle within rounding.
  for (int deg = 0; deg < 360; deg += 15) {
    const double a = deg * 3.14159265358979323846 / 180.0;
    for (double mag : {1.0, 5.0}) {
      ma::StickForAim(mag * std::cos(a), mag * std::sin(a), lx, ly);
      CHECK(lx >= -32767 && lx <= 32767 && ly >= -32767 && ly <= 32767);
      CHECK(std::abs(std::hypot(lx, ly) - 32767.0) <= 2.0);
    }
  }
}

// The aim path sums the mouse over one LF tick and divides by that window, so
// with high_tick_rate (30 Hz, half the window) the same mouse speed gives the
// same stick: aim sensitivity doesn't change with the tick rate.
static void TestTickRateInvariance() {
  namespace ms = fable2::mousestick;
  const double px_per_s = 900.0;
  double cx15 = 0, cy15 = 0, cx30 = 0, cy30 = 0;
  const double w15 = ms::DefaultWindowUs(false) / 1e6;
  const double w30 = ms::DefaultWindowUs(true) / 1e6;
  ms::WindowToControl(px_per_s * w15, -px_per_s * w15 / 3, w15, 256, cx15, cy15);
  ms::WindowToControl(px_per_s * w30, -px_per_s * w30 / 3, w30, 256, cx30, cy30);
  CHECK(std::abs(cx15 - cx30) < 1e-9 && std::abs(cy15 - cy30) < 1e-9);
  int32_t a = 0, b = 0, c = 0, d = 0;
  ma::StickForAim(cx15, cy15, a, b);
  ma::StickForAim(cx30, cy30, c, d);
  CHECK(std::abs(a - c) <= 1 && std::abs(b - d) <= 1);
  // mouse_aim_scale is linear: double the scale, double the wanted input.
  double cx2 = 0, cy2 = 0;
  ms::WindowToControl(px_per_s * w15, 0, w15, 512, cx2, cy2);
  CHECK(std::abs(cx2 - 2 * cx15) < 1e-9);
}

// The input driver's aim path, poll by poll (polls ~60 Hz, one LF tick =
// 66.7 ms at 15 Hz).
static void TestAimStick() {
  const int64_t tick = fable2::mousestick::DefaultWindowUs(false);
  const int64_t poll = 16667;
  int32_t lx = 0, ly = 0, wx = 0, wy = 0;
  {
    // An even mouse speed gives a steady stick once a whole tick has passed.
    ma::AimStick st;
    int32_t first_full = 0;
    int64_t t = 1000;
    for (int i = 0; i < 40; ++i, t += poll) {
      st.Update(t, tick, true, 10, 0, 256, lx, ly, wx, wy);
      if (t >= 1000 + tick + poll) {
        if (!first_full) first_full = lx;
        CHECK(std::abs(lx - first_full) <= first_full / 100 + 1);  // within 1%
        CHECK(ly == 0 && lx > 0);
      }
    }
    // The mouse stops: the stick holds until the last movement leaves the
    // window, then is exactly 0 (so the reticle doesn't creep).
    const int64_t last = t - poll;  // the last poll that moved
    st.Update(last + tick - 1000, tick, false, 0, 0, 256, lx, ly, wx, wy);
    CHECK(lx > 0);
    st.Update(last + tick, tick, false, 0, 0, 256, lx, ly, wx, wy);
    CHECK(lx == 0 && ly == 0 && wx == 0 && wy == 0);
    st.Update(last + tick + poll, tick, false, 0, 0, 256, lx, ly, wx, wy);
    CHECK(lx == 0 && ly == 0);

    // Reset (aiming ended): the next aim doesn't replay old movement.
    st.Update(last + tick + 2 * poll, tick, true, 30, -20, 256, lx, ly, wx, wy);
    CHECK(lx > 0 && ly > 0);  // mouse up (dy < 0) = aim up
    st.Reset();
    st.Update(last + tick + 3 * poll, tick, false, 0, 0, 256, lx, ly, wx, wy);
    CHECK(lx == 0 && ly == 0 && wx == 0 && wy == 0);
    // The first poll of a new aim counts only its own movement.
    st.Update(last + tick + 4 * poll, tick, true, 7, 0, 256, lx, ly, wx, wy);
    CHECK(wx == 7 && wy == 0);
  }
  {
    // A poll after a 500 ms stall: its movement is spread over at most
    // kMaxSpanUs, so it gives no more stick than the same movement normally.
    ma::AimStick steady, stalled;
    int32_t sx = 0, sy = 0;
    int64_t t = 1000;
    for (int i = 0; i < 10; ++i, t += poll) steady.Update(t, tick, true, 10, 0, 256, sx, sy, wx, wy);
    stalled.Update(1000, tick, true, 10, 0, 256, lx, ly, wx, wy);
    stalled.Update(501000, tick, true, 10, 0, 256, lx, ly, wx, wy);
    CHECK(lx > 0 && lx <= sx);
    CHECK(wx == 10);  // the movement before the stall has left the window
  }
  {
    // No movement read this poll (moved = false): nothing is added, even if
    // dx/dy hold stale values.
    ma::AimStick st;
    st.Update(1000, tick, false, 500, 500, 256, lx, ly, wx, wy);
    CHECK(lx == 0 && ly == 0 && wx == 0 && wy == 0);
  }
  {
    // mouse_aim_scale: double the scale gives the stick of double the speed.
    ma::AimStick a, b;
    int32_t ax = 0, ay = 0, bx = 0, by = 0;
    int64_t t = 1000;
    for (int i = 0; i < 8; ++i, t += poll) {
      a.Update(t, tick, true, 4, 0, 512, ax, ay, wx, wy);
      b.Update(t, tick, true, 8, 0, 256, bx, by, wx, wy);
    }
    CHECK(ax == bx && ay == by);
  }
}

static void TestAimInput() {
  ma::AimInput in = ma::AimInput::kStick;
  CHECK(ma::ParseAimInput("DIRECT", in) && in == ma::AimInput::kDirect);
  CHECK(ma::ParseAimInput("stick", in) && in == ma::AimInput::kStick);
  CHECK(!ma::ParseAimInput("mouse", in) && in == ma::AimInput::kStick);  // unchanged
  CHECK(ma::AimInputOrDefault("rubbish") == ma::AimInput::kDirect);
  CHECK(std::string(ma::AimInputName(ma::AimInput::kStick)) == "stick");
  CHECK(std::string(ma::AimInputName(ma::AimInput::kDirect)) == "direct");
}

static void TestToCursor() {
  const double wide = 16.0 / 9.0;
  ma::AimSens s;
  CHECK(s.x == 0.0007 && s.y_ratio == 1.0 && !s.invert_y);  // the defaults
  double cx = 0, cy = 0;
  // Signs copy the left stick (measured 2026-11-14; mouse up = reticle up
  // confirmed on screen): mouse right raises x, mouse down raises y.
  ma::ToCursor(100, 0, s, wide, cx, cy);
  CHECK(std::abs(cx - 0.07) < 1e-12 && cy == 0);
  ma::ToCursor(-50, -25, s, wide, cx, cy);
  CHECK(cx < 0 && cy < 0);
  // Up/down follows the screen shape: x times the aspect, so Rachelle's tuned
  // 16:9 pair (0.0007 sideways, 0.00125 up/down) comes out of the default ratio.
  CHECK(std::abs(ma::VerticalSens(s, wide) - 0.0007 * wide) < 1e-15);
  CHECK(std::abs(ma::VerticalSens(s, wide) - 0.00125) < 0.00001);
  ma::ToCursor(0, 100, s, wide, cx, cy);
  CHECK(cx == 0 && std::abs(cy - 0.07 * wide) < 1e-12);
  // The same on-screen speed on other shapes: 21:9 and 4:3.
  CHECK(std::abs(ma::VerticalSens(s, 21.0 / 9.0) - 0.0007 * 21.0 / 9.0) < 1e-15);
  CHECK(std::abs(ma::VerticalSens(s, 4.0 / 3.0) - 0.0007 * 4.0 / 3.0) < 1e-15);
  // Fit-to-window off reports 0, and a nonsense aspect isn't trusted: 16:9.
  for (double bad : {0.0, -1.0, 0.1, 100.0, std::numeric_limits<double>::quiet_NaN(),
                     std::numeric_limits<double>::infinity()})
    CHECK(std::abs(ma::VerticalSens(s, bad) - 0.0007 * wide) < 1e-15);
  // Linear: no curve, cap or deadzone. One count moves the reticle, and a
  // fast flick moves it exactly as far as the same distance slowly.
  ma::ToCursor(1, 0, s, wide, cx, cy);
  CHECK(std::abs(cx - 0.0007) < 1e-15);
  ma::ToCursor(5000, 0, s, wide, cx, cy);
  CHECK(std::abs(cx - 3.5) < 1e-9);  // the game clamps it to its box, not us
  // The ratio scales up/down only, and the invert flips it.
  s.y_ratio = 0.5;
  s.invert_y = true;
  ma::ToCursor(10, 10, s, wide, cx, cy);
  CHECK(std::abs(cx - 0.007) < 1e-12 && std::abs(cy + 0.007 * wide * 0.5) < 1e-12);
  // A NaN or infinite setting falls back to the default.
  s.x = std::numeric_limits<double>::quiet_NaN();
  s.y_ratio = std::numeric_limits<double>::infinity();
  s.invert_y = false;
  ma::ToCursor(10, 10, s, wide, cx, cy);
  CHECK(std::abs(cx - 0.007) < 1e-12 && std::abs(cy - 0.007 * wide) < 1e-12);
}

static void TestAimDirect() {
  ma::AimDirect d;
  int64_t dx = 7, dy = 7;
  CHECK(!d.Take(dx, dy) && dx == 0 && dy == 0);  // nothing waiting
  // Polls add up until the next cursor update takes it all, once.
  d.Feed(10, -3);
  d.Feed(5, 0);
  d.Feed(0, 0);
  d.Feed(-2, 8);
  CHECK(d.Take(dx, dy) && dx == 13 && dy == 5);
  CHECK(!d.Take(dx, dy) && dx == 0 && dy == 0);
  // Movement that cancels out still counts as nothing waiting.
  d.Feed(4, 0);
  d.Feed(-4, 0);
  CHECK(!d.Take(dx, dy));
  // Clear (not aiming, stick input) drops what's waiting.
  d.Feed(100, 100);
  d.Clear();
  CHECK(!d.Take(dx, dy) && dx == 0 && dy == 0);
  // Large totals don't overflow (int64 behind int32 feeds).
  for (int i = 0; i < 1000; ++i) d.Feed(2000000000, -2000000000);
  CHECK(d.Take(dx, dy) && dx == 2000000000000LL && dy == -2000000000000LL);
  // Two threads feeding while a third takes: nothing lost or counted twice.
  ma::AimDirect shared;
  std::atomic<bool> go{true};
  int64_t taken_x = 0, taken_y = 0;
  std::thread taker([&] {
    while (go.load()) {
      int64_t x = 0, y = 0;
      if (shared.Take(x, y)) taken_x += x, taken_y += y;
    }
  });
  std::thread a([&] { for (int i = 0; i < 100000; ++i) shared.Feed(1, -1); });
  std::thread b([&] { for (int i = 0; i < 100000; ++i) shared.Feed(2, 3); });
  a.join();
  b.join();
  go.store(false);
  taker.join();
  int64_t x = 0, y = 0;
  shared.Take(x, y);
  CHECK(taken_x + x == 300000 && taken_y + y == 200000);
}

static float UltraWide() { return 21.0f / 9.0f; }

// The aspect the hook uses comes from the app (fitwindow::CurrentAspect);
// before that's set it reads as 0, which means 16:9.
static void TestAspectSource() {
  CHECK(ma::AspectSource().load() == nullptr && ma::GameAspect() == 0.0);
  ma::AimSens s;
  CHECK(std::abs(ma::VerticalSens(s, ma::GameAspect()) - 0.0007 * 16.0 / 9.0) < 1e-15);
  ma::AspectSource().store(&UltraWide);
  CHECK(std::abs(ma::GameAspect() - 21.0 / 9.0) < 1e-6);
  CHECK(std::abs(ma::VerticalSens(s, ma::GameAspect()) - 0.0007 * 21.0 / 9.0) < 1e-9);
  ma::AspectSource().store(nullptr);
}

static void TestDirectSettings() {
  ma::AimDirectSettings st;
  ma::AimSens got = st.Get();
  CHECK(got.x == 0.0007 && got.y_ratio == 1.0 && !got.invert_y);  // before the first poll
  ma::AimSens s;
  s.x = 0.0005;
  s.y_ratio = 1.25;
  s.invert_y = true;
  st.Publish(s);
  got = st.Get();
  CHECK(got.x == 0.0005 && got.y_ratio == 1.25 && got.invert_y);
}

int main() {
  TestModes();
  TestShouldSwap();
  TestSwapActive();
  TestAimState();
  TestCursor();
  TestTargeting();
  TestTargetPos();
  TestMouseRelease();
  TestAimAssistAndWheel();
  TestMagnetism();
  TestStickForAim();
  TestTickRateInvariance();
  TestAimStick();
  TestAimInput();
  TestToCursor();
  TestAimDirect();
  TestDirectSettings();
  TestAspectSource();
  if (failures) {
    std::printf("%d failure(s)\n", failures);
    return 1;
  }
  std::printf("mouse aim tests passed\n");
  return 0;
}
