// fable_2 - mouse movement -> right stick that the game turns into an even,
// complete camera turn.
//
// The mouse still drives the guest right stick, so it goes through the game's
// own camera code and shares every stick limit. What changes is how the stick
// value is chosen. All numbers below were measured on 2026-11-11 with
// `f2 mouse trace` and `f2 pad rstick` (context: plans and fable2_mouse.log):
//
//  1. The camera script (SACCamera) updates 15 times a second (LF tick; 30
//     with high_tick_rate), and uses ONE input poll per update. The game polls
//     input 4 times per update, two of which carry mouse movement. Reporting
//     each poll's own movement therefore lost about half of it, unevenly.
//     Fix: every poll reports the mouse's velocity over the last camera update
//     (VelocityWindow), so whichever poll the camera samples carries all of it.
//
//  2. The game maps the stick s = (rx, ry) / 32767 to the camera input c as
//         c = ŝ · min(1, max(0, (|s| − 0.30) / 0.70) · (1 + (√2 − 1)·sin²(2θ)))
//     a 30% circular deadzone, a linear rescale, and a diagonal boost of up to
//     √2 at 45° (GameControl, fitted to 20 measurements within 0.0005). Slow
//     mouse movement fell inside the deadzone and did nothing, and diagonals
//     turned up to 41% faster. Fix: choose the stick that produces the wanted
//     camera input exactly (StickForControl), so the response is linear from
//     the first pixel and the same in every direction.
//
// The full-stick turn speed (the cap, ~120°/s at in-game sensitivity 0.5) is
// unchanged: removing it needs the camera script itself.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace fable2::mousestick {

constexpr double kStickMax = 32767.0;
constexpr double kGameDeadzone = 0.30;                 // measured
constexpr double kDiagonalBoost = 0.41421356237309515;  // √2 − 1, measured

// sin²(2θ) of the direction (x, y), without trig: 4x²y² / (x² + y²)².
inline double Sin2Of2Theta(double x, double y) {
  const double m2 = x * x + y * y;
  if (m2 <= 0.0) return 0.0;
  const double p = 2.0 * x * y / m2;
  return p * p;
}

// The game's mapping from the raw right stick (each axis -1..1) to what the
// camera script receives. Used by the tests to check StickForControl, and as
// documentation of the measurement.
inline void GameControl(double sx, double sy, double& cx, double& cy) {
  const double m = std::hypot(sx, sy);
  if (m <= kGameDeadzone) {
    cx = cy = 0.0;
    return;
  }
  const double mag = std::min(
      1.0, (m - kGameDeadzone) / (1.0 - kGameDeadzone) * (1.0 + kDiagonalBoost * Sin2Of2Theta(sx, sy)));
  cx = sx / m * mag;
  cy = sy / m * mag;
}

// The stick (int16 units) that makes the game deliver camera input (cx, cy).
// Wanted magnitudes above 1 are capped at full speed (the game's limit); any
// non-zero wanted input lands just past the deadzone, so it always registers.
// Returns true when the wanted input was capped.
inline bool StickForControl(double cx, double cy, int32_t& rx, int32_t& ry) {
  double mc = std::hypot(cx, cy);
  if (mc < 1e-9) {
    rx = ry = 0;
    return false;
  }
  const bool capped = mc > 1.0;
  if (capped) {
    cx /= mc;
    cy /= mc;
    mc = 1.0;
  }
  const double boost = 1.0 + kDiagonalBoost * Sin2Of2Theta(cx, cy);
  const double ms = kGameDeadzone + (1.0 - kGameDeadzone) * mc / boost;
  // Round away from zero so a tiny wanted input never rounds back into the
  // deadzone (the game compares the magnitude against exactly 0.30).
  auto axis = [&](double c) {
    const double v = c / mc * ms * kStickMax;
    const double r = v >= 0 ? std::ceil(v) : std::floor(v);
    return static_cast<int32_t>(std::clamp(r, -kStickMax, kStickMax));
  };
  rx = axis(cx);
  ry = axis(cy);
  return capped;
}

// Mouse movement over a sliding time window, so every poll inside one camera
// update reports the same velocity. A poll's movement happened over the time
// since the previous poll, so it's stored as spread evenly over that interval,
// and Sum() counts the part of each interval inside (now − window, now].
// Counting whole polls instead made the sum flicker between 2 and 3 polls'
// worth (polls are ~33 ms apart with jitter, the window is 67 ms), which was
// measured as ±20% in-game on 2026-11-11. Single-threaded: the input driver
// adds and sums on the polling thread.
class VelocityWindow {
 public:
  static constexpr size_t kCapacity = 256;  // > 4 polls per 67 ms at any rate we've seen
  // Longest interval one poll's movement is spread over. Normal polls are
  // ~33 ms apart; after a stall the movement is treated as recent instead of
  // being spread thin and mostly falling outside the window.
  static constexpr int64_t kMaxSpanUs = 50000;

  // Movement (dx, dy) seen at time t1 that happened since t0 (the previous poll).
  void Add(int64_t t0_us, int64_t t1_us, int32_t dx, int32_t dy) {
    if (dx == 0 && dy == 0) return;
    t0_us = std::clamp(t0_us, t1_us - kMaxSpanUs, t1_us);
    if (count_ == kCapacity) Pop();
    Entry& e = ring_[(head_ + count_) % kCapacity];
    e = {t0_us, t1_us, dx, dy};
    ++count_;
  }

  // Movement inside (now − window, now]; entries entirely older are dropped.
  void Sum(int64_t now_us, int64_t window_us, double& sx, double& sy) {
    const int64_t lo = now_us - window_us;
    while (count_ && ring_[head_].t1 <= lo) Pop();
    sx = sy = 0.0;
    for (size_t i = 0; i < count_; ++i) {
      const Entry& e = ring_[(head_ + i) % kCapacity];
      double f = 1.0;  // share of the entry's interval inside the window
      if (e.t1 > e.t0) {
        const int64_t a = std::max(e.t0, lo);
        const int64_t b = std::min(e.t1, now_us);
        f = b > a ? static_cast<double>(b - a) / static_cast<double>(e.t1 - e.t0) : 0.0;
      } else if (e.t1 <= lo || e.t1 > now_us) {
        f = 0.0;
      }
      sx += e.dx * f;
      sy += e.dy * f;
    }
  }

  void Clear() { head_ = count_ = 0; }

 private:
  struct Entry {
    int64_t t0, t1;
    int32_t dx, dy;
  };
  void Pop() {
    head_ = (head_ + 1) % kCapacity;
    --count_;
  }
  Entry ring_[kCapacity] = {};
  size_t head_ = 0;
  size_t count_ = 0;
};

// Pixels moved within the window -> wanted camera input (1.0 = full stick),
// before the deadzone compensation. Calibrated so the top speed matches the
// old mapping: there, a poll carrying `32767 / scale` pixels gave full stick,
// and movement reached the camera through polls 1/30 s apart. So full speed is
// 30 · 32767 / scale pixels per second (3840 px/s at the default 256).
// Fable 2 inverts Y: mouse up (dy < 0) is look up (+y).
inline void WindowToControl(double px_x, double px_y, double window_s, int32_t scale,
                            double& cx, double& cy) {
  constexpr double kRefPollSeconds = 1.0 / 30.0;
  const double k = window_s > 0 ? scale * (kRefPollSeconds / window_s) / kStickMax : 0.0;
  cx = px_x * k;
  cy = -px_y * k;
}

// Default window: one camera update. The camera script runs on the LF tick,
// 15 Hz, or 30 Hz with the high_tick_rate patch.
inline int64_t DefaultWindowUs(bool high_tick_rate) {
  return high_tick_rate ? 33333 : 66667;
}

}  // namespace fable2::mousestick
