// fable_2 - mouse aiming: while the player aims a ranged weapon, the mouse
// moves the aim reticle instead of the camera, and the keyboard's WASD stops
// moving it.
//
// Why: in aim mode (player mode 23) the game turns the left stick into its
// own "move the aim cursor" action, and the aim camera (RangedCombatCamera)
// only follows that cursor; it never reads the right stick. So without this,
// the mouse (right stick, or the direct camera of mouse_direct.h) does nothing
// useful while aiming.
//
// Two inputs (mouse_aim_input, AimInput below):
//   direct (step 2, the default)  the movement is added straight to the
//       game's aim cursor once per update (AimDirect, applied by the
//       cursor-function hook). The reticle moves exactly as far as the mouse.
//   stick (step 1, the "stick swap")  the mouse drives the guest LEFT stick
//       (AimStick). The game's stick handling stays in charge: tested on
//       2026-11-14 it has a ~30% deadzone, a top speed that cut 92% of fast
//       hand movement, and 15 Hz steps, so it "feels like a stick".
//
// When to swap (cvar mouse_aim_mode, `f2 mouse aim <mode>`):
//   off     never
//   rmb     while the aim button (any key bound to pad Y) is held
//   detect  while the game is aiming: sub_827B80B0, which only runs while
//           aiming, reported in within AimTimeoutUs
//           (src/core/hotfunc/camera/AimCursorUpdate_827B80B0.cpp)
//   both    both of the above (the default): releasing the button stops the
//           swap at once, and holding it before aim begins doesn't walk the
//           player with the mouse
//
// Pure apart from std; tested by tests/native/test_mouse_aim.cpp. Times are
// microseconds on the caller's clock (fable2::mousediag::NowUs in the game).

#pragma once

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>

#include "mouse_stick.h"  // VelocityWindow, WindowToControl (also std only)

namespace fable2::mouseaim {

enum class AimSwapMode { kOff, kRmb, kDetect, kBoth };

// Case-insensitive; false (out unchanged) for an unknown name.
inline bool ParseAimSwapMode(std::string_view name, AimSwapMode& out) {
  std::string n;
  n.reserve(name.size());
  for (char c : name) n.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  if (n == "off") return out = AimSwapMode::kOff, true;
  if (n == "rmb") return out = AimSwapMode::kRmb, true;
  if (n == "detect") return out = AimSwapMode::kDetect, true;
  if (n == "both") return out = AimSwapMode::kBoth, true;
  return false;
}

inline const char* AimSwapModeName(AimSwapMode m) {
  switch (m) {
    case AimSwapMode::kOff: return "off";
    case AimSwapMode::kRmb: return "rmb";
    case AimSwapMode::kDetect: return "detect";
    case AimSwapMode::kBoth: return "both";
  }
  return "off";
}

// An unknown cvar value falls back to the default.
inline AimSwapMode ModeOrDefault(std::string_view name) {
  AimSwapMode m = AimSwapMode::kBoth;
  ParseAimSwapMode(name, m);
  return m;
}

inline bool ShouldSwap(AimSwapMode mode, bool aim_key_held, bool aim_detected) {
  switch (mode) {
    case AimSwapMode::kOff: return false;
    case AimSwapMode::kRmb: return aim_key_held;
    case AimSwapMode::kDetect: return aim_detected;
    case AimSwapMode::kBoth: return aim_key_held && aim_detected;
  }
  return false;
}

// The input driver's decision for one poll. Never in photo mode (its free
// camera owns the mouse), and only while the mouse is live: `looking` (focus,
// mouse_look on, F4 menu closed) or a running `f2 mouse inject`, which ignores
// focus so tests can be scripted. The aim key is only seen with focus, so an
// unfocused inject can only swap in "detect" mode.
inline bool SwapActive(bool photo, bool looking, bool inject_live, AimSwapMode mode,
                       bool aim_key_held, bool aim_detected) {
  return !photo && (looking || inject_live) && ShouldSwap(mode, aim_key_held, aim_detected);
}

// How long after the cursor function last ran the player still counts as
// aiming. It normally runs once per LF tick (66.7 ms, 33.3 with
// high_tick_rate), but in game it was seen skipping to every 2nd or 3rd tick
// while aiming (133-200 ms gaps, 2026-11-14), which a 167 ms timeout turned
// into dropouts. Five ticks, at least 250 ms. The cost is a longer tail after
// aiming ends in "detect" mode only; in "both" releasing the button ends the
// swap at once.
inline int64_t AimTimeoutUs(int64_t tick_us) {
  return std::max<int64_t>(tick_us * 5, 250000);
}

// How the mouse moves the reticle while the swap is on (cvar mouse_aim_input,
// `f2 mouse aiminput`):
//   direct  the movement is added straight to the game's aim cursor, before
//           its own update, by the cursor-function hook (AimDirect below):
//           no top speed, deadzone, curve or stick smoothing; the game still
//           clamps the cursor to its box (the default)
//   stick   the mouse drives the left stick (AimStick below), through the
//           game's own stick handling
enum class AimInput { kDirect, kStick };

inline bool ParseAimInput(std::string_view name, AimInput& out) {
  std::string n;
  for (char c : name) n.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  if (n == "direct") return out = AimInput::kDirect, true;
  if (n == "stick") return out = AimInput::kStick, true;
  return false;
}
inline const char* AimInputName(AimInput i) { return i == AimInput::kStick ? "stick" : "direct"; }
inline AimInput AimInputOrDefault(std::string_view name) {
  AimInput i = AimInput::kDirect;
  ParseAimInput(name, i);
  return i;
}

// Direct aim: mouse counts (x right, y down) -> change of the game's aim
// cursor, in cursor units (its box is about -0.8..0.8 on both axes). The
// signs copy what the left stick does, as measured on 2026-11-14 with the
// stick swap: mouse right raises x, mouse down raises y (mouse up = reticle
// up, confirmed on screen).
//
// The cursor's units are stretched to the screen: one unit sideways covers
// `aspect` times the screen distance of one unit up/down. Rachelle's tuned
// pair on a 16:9 screen, 0.0007 sideways and 0.00125 up/down, is that ratio
// (1.79 against 1.78). So vertical = horizontal x the game's aspect x
// y_ratio, and y_ratio = 1 moves the reticle at the same on-screen speed both
// ways on any screen shape. The game renders at the window's aspect
// (src/core/fable2_fit_window.h), or 16:9 with that off.
constexpr double kDefaultAspect = 16.0 / 9.0;

struct AimSens {
  double x = 0.0007;      // horizontal cursor units per count (mouse_aim_sens)
  double y_ratio = 1.0;   // vertical on-screen speed against horizontal (mouse_aim_y_ratio)
  bool invert_y = false;  // mouse_aim_invert_y
};

// Where the game's render aspect comes from: fable2::fitwindow::CurrentAspect,
// handed over by the app at start-up (fable_2_app.h), so this header and the
// cursor hook don't depend on the fit-window code. Unset (tests, or before
// start-up) means 16:9.
using AspectFn = float (*)();
inline std::atomic<AspectFn>& AspectSource() {
  static std::atomic<AspectFn> fn{nullptr};
  return fn;
}
inline double GameAspect() {
  const AspectFn fn = AspectSource().load(std::memory_order_relaxed);
  return fn ? static_cast<double>(fn()) : 0.0;
}

// The aspect to use: a plausible width/height, else 16:9 (fit-to-window off
// reports 0).
inline double AimAspect(double aspect) {
  return std::isfinite(aspect) && aspect >= 0.5 && aspect <= 8.0 ? aspect : kDefaultAspect;
}

// Vertical cursor units per count for these settings and screen shape.
inline double VerticalSens(const AimSens& s, double aspect) {
  auto finite_or = [](double v, double d) { return std::isfinite(v) ? v : d; };
  return finite_or(s.x, AimSens{}.x) * AimAspect(aspect) * finite_or(s.y_ratio, AimSens{}.y_ratio);
}

inline void ToCursor(double dx, double dy, const AimSens& s, double aspect, double& cx, double& cy) {
  cx = dx * (std::isfinite(s.x) ? s.x : AimSens{}.x);
  cy = dy * VerticalSens(s, aspect) * (s.invert_y ? -1.0 : 1.0);
}

// Mouse movement waiting for the next cursor update. The input poll adds
// (Feed) on its thread; the cursor-function hook takes it all (Take) on a
// guest thread, once per update. Whole counts in two atomics: no lock in the
// game's hot path, and nothing is lost between the two.
class AimDirect {
 public:
  void Feed(int32_t dx, int32_t dy) {
    if (dx) dx_.fetch_add(dx, std::memory_order_relaxed);
    if (dy) dy_.fetch_add(dy, std::memory_order_relaxed);
  }
  // Everything since the last Take; false (and 0, 0) when nothing is waiting.
  bool Take(int64_t& dx, int64_t& dy) {
    dx = dx_.exchange(0, std::memory_order_relaxed);
    dy = dy_.exchange(0, std::memory_order_relaxed);
    return dx != 0 || dy != 0;
  }
  // Not aiming (or the stick input): drop what's waiting, so it can't jump the
  // reticle when aiming starts.
  void Clear() {
    dx_.store(0, std::memory_order_relaxed);
    dy_.store(0, std::memory_order_relaxed);
  }

 private:
  std::atomic<int64_t> dx_{0};
  std::atomic<int64_t> dy_{0};
};

inline AimDirect& Direct() {
  static AimDirect d;
  return d;
}

// The cursor-function hook's settings, published by the input poll (which
// reads the cvars) so the hook reads only atomics. Bit-cast doubles.
class AimDirectSettings {
 public:
  void Publish(const AimSens& s) {
    store(x_, s.x);
    store(y_, s.y_ratio);
    invert_.store(s.invert_y, std::memory_order_relaxed);
  }
  AimSens Get() const {
    AimSens s;
    s.x = load(x_, s.x);
    s.y_ratio = load(y_, s.y_ratio);
    s.invert_y = invert_.load(std::memory_order_relaxed);
    return s;
  }

 private:
  static void store(std::atomic<uint64_t>& a, double v) {
    uint64_t b = 0;
    std::memcpy(&b, &v, 8);
    a.store(b, std::memory_order_relaxed);
  }
  static double load(const std::atomic<uint64_t>& a, double fallback) {
    const uint64_t b = a.load(std::memory_order_relaxed);
    if (b == 0) return fallback;  // never published
    double v = 0;
    std::memcpy(&v, &b, 8);
    return v;
  }
  std::atomic<uint64_t> x_{0}, y_{0};
  std::atomic<bool> invert_{false};
};

inline AimDirectSettings& DirectSettings() {
  static AimDirectSettings s;
  return s;
}

// After aiming ends the gameplay camera (SACCamera) takes a camera update to
// report in again, and during a long aim it has been silent for longer than
// the direct mouse camera's "script alive" window (mousedirect::Boot::kAliveUs).
// Within this grace the input driver doesn't ask for mouse_direct.lua to be
// restarted: it is about to report in by itself.
constexpr int64_t kAimBootGraceUs = 1000000;

// The targeting object's highlight state around one cursor update, for the
// log (read from the generated code of sub_827B6228 / sub_827B80B0 /
// sub_827B5888; field meanings not yet confirmed in game):
//   handle_set  +60 != 0 after the update: a highlighted target's handle is
//               set (the game also checks the target is still valid, which
//               the hook doesn't, to stay clear of guest calls);
//   flick       +232 before the update: the left-stick detector saw a flick;
//   held        +233 before the update: the stick stayed pushed after it;
//   b234        +234 before the update (meaning unknown, set by the same
//               detector);
//   age         +160 after the update: ticks the highlight has been held.
// While a target is highlighted the game only re-picks (or lets go) on flick
// or held, which only the raw left stick sets: the direct mouse never does.
struct Targeting {
  bool noted = false;  // false until the first cursor update
  bool handle_set = false;
  bool flick = false;
  bool held = false;
  bool b234 = false;
  uint32_t age = 0;  // saturates at kTargetingAgeMax
};
constexpr uint32_t kTargetingAgeMax = 0xFFFFFFu;

// Bits 0..4: noted, handle_set, flick, held, b234; bits 8..31: age.
inline uint32_t PackTargeting(bool handle_set, bool flick, bool held, bool b234, uint32_t age) {
  const uint32_t a = age > kTargetingAgeMax ? kTargetingAgeMax : age;
  return 1u | (handle_set ? 2u : 0u) | (flick ? 4u : 0u) | (held ? 8u : 0u) | (b234 ? 16u : 0u) |
         (a << 8);
}
inline Targeting UnpackTargeting(uint32_t bits) {
  Targeting t;
  t.noted = (bits & 1u) != 0;
  t.handle_set = (bits & 2u) != 0;
  t.flick = (bits & 4u) != 0;
  t.held = (bits & 8u) != 0;
  t.b234 = (bits & 16u) != 0;
  t.age = bits >> 8;
  return t;
}

// Where the highlighted target sits in cursor units (the +32/+36 space), for
// the log (phase A of the highlight fix: is the reticle drawn there?). The
// hook projects the target's world position (sub_827BB550) through the game
// camera (sub_827BB948), only while a highlight is set and the mouse log is
// on. `front`:
constexpr int32_t kTargetNotComputed = -1;  // no highlight, or the log is off
constexpr int32_t kTargetBehind = 0;        // behind the camera: no x/y
constexpr int32_t kTargetInFront = 1;       // x/y valid
constexpr int32_t kTargetInvalid = -2;      // handle set, but no live target
struct TargetPos {
  float x = std::numeric_limits<float>::quiet_NaN();
  float y = std::numeric_limits<float>::quiet_NaN();
  int32_t front = kTargetNotComputed;
};

// Letting go of a highlight with the mouse (cvar mouse_aim_release, PR #183
// review). While a target is highlighted, the targeting update
// (sub_827B6228) re-projects the target into the cursor every tick, so the
// direct mouse add never builds up, and only the left stick's flick/held
// flags let go. With release on, the cursor hook:
//   - lets go of the highlight on any tick with mouse movement (the game's own
//     empty-handle assign), after pointing the stored aim point (+128) at the
//     target, so the next tick's re-projection starts where the reticle was;
//   - keeps the game's own target pick (sub_827B9CD0) off while the mouse is
//     moving and for kPickHoldUs after, so a sweep doesn't snap onto everyone
//     it passes. Once the mouse rests, the pick (the game's aim assist) runs
//     as usual.
constexpr int64_t kPickHoldUs = 150000;

inline bool ShouldRelease(bool enabled, bool moved, bool target_valid) {
  return enabled && moved && target_valid;
}
// last_move_us 0 = the mouse never moved while aiming. A movement stamped
// slightly after now_us (clock read order) still holds.
inline bool HoldPicks(bool enabled, int64_t now_us, int64_t last_move_us) {
  return enabled && last_move_us != 0 && now_us - last_move_us < kPickHoldUs;
}

// How the game's target search (sub_827B9CD0) behaves inside one cursor
// update. Set by the cursor hook around the original update on its own
// thread, read by the target-search hook: other callers of the search (other
// threads, or outside the update) are never affected.
//   suppress  answer "nobody" (the mouse is moving: HoldPicks);
//   scale     multiply the search's reach (f2) and size weight (f3): a
//             candidate is kept when distance - size x f3 <= f2, so scaling
//             both scales the whole catch zone around the reticle (PickScale).
struct PickMode {
  bool suppress = false;
  double scale = 1.0;
};
inline PickMode& PickHere() {
  static thread_local PickMode mode;
  return mode;
}

// Bigger rest zone (cvar mouse_aim_assist, `f2 mouse aimassist <x>`): only
// for someone aiming with the mouse (it moved during this aim), never on a
// wheel hop (the game's own hop reach), and 1 for anything not a positive
// finite number.
constexpr double kAimAssistMin = 0.5;
constexpr double kAimAssistMax = 8.0;
inline double PickScale(bool enabled, bool mouse_used, bool hop, double assist) {
  if (!enabled || !mouse_used || hop || !std::isfinite(assist) || assist <= 0.0) return 1.0;
  return std::clamp(assist, kAimAssistMin, kAimAssistMax);
}

// One aim = cursor updates without a gap longer than this (the game skips
// ticks while aiming, so well above a tick).
constexpr int64_t kAimSessionGapUs = 500000;
inline bool NewAimSession(int64_t prev_tick_us, int64_t now_us) {
  return prev_tick_us == 0 || now_us - prev_tick_us > kAimSessionGapUs;
}

// Wheel target hop: while a target is highlighted, a wheel notch hops to the
// next target to the right (wheel up) or left (wheel down), through the game's
// own flick re-pick (+232 with the direction at +256). The game only takes a
// flick once the highlight is older than its own gate (a few ticks); wheel
// hops skip that gate and use mouse_aim_hop_ms instead (HopReady).
//
// Notches come from the window's input thread as scroll amounts
// (kScrollPerDetent per notch; smooth-scrolling wheels send fractions, which
// add up). Up to kHopPendingMax notches wait, one hop per cursor update; all
// of them are dropped when nothing was scrolled for kWheelFreshUs, and when a
// new aim begins (Clear), so scrolling while not aiming never hops later.
constexpr int32_t kScrollPerDetent = 120;  // rex::ui::MouseEvent, = WHEEL_DELTA
constexpr int32_t kHopPendingMax = 3;
constexpr int64_t kWheelFreshUs = 1000000;
class WheelHop {
 public:
  // scroll_y: the window event's scroll amount (positive = wheel up).
  // Single producer (the window's input thread).
  void Feed(int32_t scroll_y, int64_t now_us) {
    if (scroll_y == 0) return;
    int32_t acc = acc_.load(std::memory_order_relaxed);
    if ((acc > 0 && scroll_y < 0) || (acc < 0 && scroll_y > 0)) acc = 0;  // turned back
    acc += scroll_y;
    const int32_t steps = acc / kScrollPerDetent;  // toward zero
    acc -= steps * kScrollPerDetent;
    acc_.store(acc, std::memory_order_relaxed);
    at_us_.store(now_us == 0 ? 1 : now_us, std::memory_order_relaxed);
    if (steps == 0) return;
    int32_t p = pending_.load(std::memory_order_relaxed);
    int32_t next = 0;
    do {
      // A turn of the wheel the other way replaces what was waiting.
      const int32_t base = (p > 0 && steps < 0) || (p < 0 && steps > 0) ? 0 : p;
      next = std::clamp(base + steps, -kHopPendingMax, kHopPendingMax);
    } while (!pending_.compare_exchange_weak(p, next, std::memory_order_relaxed));
  }
  // One waiting notch: +1 right, -1 left, 0 none. Stale notches are dropped.
  int32_t Take(int64_t now_us) {
    const int64_t at = at_us_.load(std::memory_order_relaxed);
    if (at == 0 || now_us - at > kWheelFreshUs) {
      pending_.store(0, std::memory_order_relaxed);
      return 0;
    }
    int32_t p = pending_.load(std::memory_order_relaxed);
    while (p != 0) {
      const int32_t step = p > 0 ? 1 : -1;
      if (pending_.compare_exchange_weak(p, p - step, std::memory_order_relaxed)) return step;
    }
    return 0;
  }
  int32_t Pending() const { return pending_.load(std::memory_order_relaxed); }
  void Clear() { pending_.store(0, std::memory_order_relaxed); }

 private:
  std::atomic<int32_t> acc_{0};      // scroll not yet a whole notch
  std::atomic<int32_t> pending_{0};  // notches waiting (sign = direction)
  std::atomic<int64_t> at_us_{0};    // last scroll
};

// Wheel hops at most every mouse_aim_hop_ms; last_hop_us 0 = none yet.
constexpr int32_t kHopIntervalMaxMs = 1000;
inline bool HopReady(int64_t now_us, int64_t last_hop_us, int32_t interval_ms) {
  if (last_hop_us == 0 || interval_ms <= 0) return true;
  const int32_t ms = std::min(interval_ms, kHopIntervalMaxMs);
  return now_us - last_hop_us >= static_cast<int64_t>(ms) * 1000;
}
// The highlight age the hook gives the game on a wheel hop tick, far above
// its flick gate (+160 > gate, the gate being max(1, a few ticks)).
constexpr uint32_t kHopAge = 0x10000u;

// Stickiness (cvar mouse_aim_stickiness, `f2 mouse aimsticky <x>`): while a
// target is highlighted, mouse movement (in cursor units) adds up as a pull,
// and the reticle stays on the target. Once the pull is longer than the
// stickiness, the lock breaks and the reticle moves on by the part beyond
// it (so it leaves the target smoothly, at the hand's speed). The pull starts again when the mouse rests (kPickHoldUs) or
// nothing is highlighted. Stickiness 0 = any movement lets go.
constexpr double kStickinessMax = 0.5;
class LockPull {
 public:
  // True when this movement breaks the lock; then (out_x, out_y) is the move
  // to apply to the cursor: only the part of the pull beyond the stickiness,
  // so the reticle leaves the target at the hand's speed instead of jumping
  // by the whole pull (the B4 test: a 0.11 jump on every release). The pull
  // starts again.
  bool Add(double cx, double cy, double stickiness, double& out_x, double& out_y) {
    x_ += cx;
    y_ += cy;
    const bool finite = std::isfinite(x_) && std::isfinite(y_);
    const bool sticky = std::isfinite(stickiness) && stickiness > 0.0;
    const double s = sticky ? std::min(stickiness, kStickinessMax) : 0.0;
    const double len = finite ? std::hypot(x_, y_) : 0.0;
    if (finite && sticky && len <= s) {
      out_x = out_y = 0.0;
      return false;
    }
    if (!finite) {
      out_x = cx;
      out_y = cy;
    } else {
      const double k = len > 0.0 ? (len - s) / len : 0.0;
      out_x = x_ * k;
      out_y = y_ * k;
    }
    Reset();
    return true;
  }
  void Reset() { x_ = y_ = 0.0; }
  double X() const { return x_; }
  double Y() const { return y_; }

 private:
  double x_ = 0.0, y_ = 0.0;
};
// The cursor hook's pull (its thread only).
inline LockPull& Pull() {
  static LockPull p;
  return p;
}

// Slow-movement magnetism (cvar mouse_aim_magnet_speed, `f2 mouse aimmagnet
// <x>`): while nothing is highlighted and the mouse moves the reticle slower
// than this (aim cursor units per second), the game's own moving-stick pick
// runs, as on the pad: the hook hands the update a tiny stick in the mouse's
// direction (NudgeStick), just above the game's idle threshold, so the pick
// looks ahead in a narrow cone (the person just left behind is not grabbed
// again). Faster movement keeps the pick off (HoldPicks). 0 = off.
constexpr double kMagnetSpeedMax = 5.0;
// The cursor's speed this update, from its move and the time since the last
// update (clamped to 5..100 ms, so a stall or a first tick can't fake one).
inline double TickSpeed(double cx, double cy, int64_t dt_us) {
  const int64_t dt = std::clamp<int64_t>(dt_us, 5000, 100000);
  const double v = std::hypot(cx, cy) * 1e6 / static_cast<double>(dt);
  return std::isfinite(v) ? v : std::numeric_limits<double>::infinity();
}
inline bool SlowMove(bool enabled, bool moved, double speed, double magnet_speed) {
  return enabled && moved && std::isfinite(magnet_speed) && magnet_speed > 0.0 &&
         speed < std::min(magnet_speed, kMagnetSpeedMax);
}
// The stick to hand the update: the direction of (cx, cy) at length
// idle_eps x 1.05 (the game treats a stick shorter than idle_eps, the float at
// 0x82099AE0, as idle). False for no direction. A bad or missing epsilon
// falls back to 0.01; the length is capped at 0.25.
inline bool NudgeStick(double cx, double cy, float idle_eps, float& sx, float& sy) {
  const double len = std::hypot(cx, cy);
  if (!std::isfinite(len) || len <= 0.0) return false;
  double eps = static_cast<double>(idle_eps);
  if (!std::isfinite(eps) || eps <= 0.0) eps = 0.01;
  const double m = std::min(eps * 1.05, 0.25);
  sx = static_cast<float>(cx / len * m);
  sy = static_cast<float>(cy / len * m);
  return true;
}
// The update's stick argument (r4): two floats packed by value, x in the high
// word (the update stores r4 with std and reads x at +0, y at +4).
inline uint64_t PackStick(float x, float y) {
  uint32_t bx = 0, by = 0;
  std::memcpy(&bx, &x, 4);
  std::memcpy(&by, &y, 4);
  return (static_cast<uint64_t>(bx) << 32) | by;
}
inline void UnpackStick(uint64_t packed, float& x, float& y) {
  const uint32_t bx = static_cast<uint32_t>(packed >> 32);
  const uint32_t by = static_cast<uint32_t>(packed);
  std::memcpy(&x, &bx, 4);
  std::memcpy(&y, &by, 4);
}
// No real stick in the update's argument (+0 and -0 both count as zero).
inline bool StickIsZero(uint64_t packed) {
  float x = 0.0f, y = 0.0f;
  UnpackStick(packed, x, y);
  return x == 0.0f && y == 0.0f;
}
// After the update: the part of its cursor move (dx, dy) that went forward
// along the nudge's unit direction (ux, uy), to take back out, so only the
// pick changes. A move backwards (the box clamp) or longer than `cap` (not
// the nudge) is left alone: returns 0.
inline double NudgeOvershoot(double dx, double dy, double ux, double uy, double cap) {
  const double along = dx * ux + dy * uy;
  return std::isfinite(along) && along > 0.0 && along <= cap ? along : 0.0;
}
inline WheelHop& Wheel() {
  static WheelHop w;
  return w;
}
// The hop to ask the game for this update: 0 unless release is on and a
// target is highlighted (the game's flick re-pick only runs then).
inline int32_t HopDirection(bool enabled, int32_t wheel, bool target_valid) {
  return enabled && target_valid ? wheel : 0;
}

// Written by the cursor-function hook on a guest thread, read by the input
// poll on another, so only relaxed atomics (no lock in the game's hot path).
// A torn read between the cursor's x/y and the tick count only affects the log.
class AimState {
 public:
  void NoteAimTick(int64_t now_us) {
    last_tick_us_.store(now_us == 0 ? 1 : now_us, std::memory_order_relaxed);
  }
  // 0 = never.
  int64_t LastTickUs() const { return last_tick_us_.load(std::memory_order_relaxed); }
  // A hook timestamp slightly ahead of now_us (read on another thread just
  // before the hook stored it) counts as detected.
  bool Detected(int64_t now_us, int64_t timeout_us) const {
    const int64_t t = LastTickUs();
    return t != 0 && now_us - t <= timeout_us;
  }
  bool RecentlyAimed(int64_t now_us) const { return Detected(now_us, kAimBootGraceUs); }

  // The aim cursor after the game's update (targeting object +32/+36, cursor
  // units, about -0.8..0.8), for the log. Counts the updates.
  void NoteCursor(float x, float y) {
    uint32_t bx = 0, by = 0;
    std::memcpy(&bx, &x, 4);
    std::memcpy(&by, &y, 4);
    cursor_bits_.store((static_cast<uint64_t>(bx) << 32) | by, std::memory_order_relaxed);
    ticks_.fetch_add(1, std::memory_order_relaxed);
  }
  void Cursor(float& x, float& y, uint64_t& ticks) const {
    const uint64_t bits = cursor_bits_.load(std::memory_order_relaxed);
    const uint32_t bx = static_cast<uint32_t>(bits >> 32);
    const uint32_t by = static_cast<uint32_t>(bits);
    std::memcpy(&x, &bx, 4);
    std::memcpy(&y, &by, 4);
    ticks = ticks_.load(std::memory_order_relaxed);
  }

  // The highlight state of the last update (PackTargeting), for the log.
  void NoteTargeting(uint32_t packed) { targeting_bits_.store(packed, std::memory_order_relaxed); }
  Targeting LastTargeting() const {
    return UnpackTargeting(targeting_bits_.load(std::memory_order_relaxed));
  }

  // The highlighted target's cursor position (TargetPos), for the log. x/y
  // are only kept when front is kTargetInFront; otherwise they read as NaN.
  // A torn read between x/y and front only affects the log.
  void NoteTarget(float x, float y, int32_t front) {
    if (front != kTargetInFront) x = y = std::numeric_limits<float>::quiet_NaN();
    uint32_t bx = 0, by = 0;
    std::memcpy(&bx, &x, 4);
    std::memcpy(&by, &y, 4);
    target_bits_.store((static_cast<uint64_t>(bx) << 32) | by, std::memory_order_relaxed);
    target_front_.store(front, std::memory_order_relaxed);
  }
  void NoteNoTarget() { NoteTarget(0.0f, 0.0f, kTargetNotComputed); }
  TargetPos Target() const {
    TargetPos t;
    const uint64_t bits = target_bits_.load(std::memory_order_relaxed);
    const uint32_t bx = static_cast<uint32_t>(bits >> 32);
    const uint32_t by = static_cast<uint32_t>(bits);
    std::memcpy(&t.x, &bx, 4);
    std::memcpy(&t.y, &by, 4);
    t.front = target_front_.load(std::memory_order_relaxed);
    return t;
  }

  // Mouse release (ShouldRelease / HoldPicks): when the mouse last moved the
  // reticle (0 = never), and counts for the log.
  void NoteMouseMove(int64_t now_us) {
    last_move_us_.store(now_us == 0 ? 1 : now_us, std::memory_order_relaxed);
  }
  int64_t LastMoveUs() const { return last_move_us_.load(std::memory_order_relaxed); }
  // The last movement faster than the magnet speed: HoldPicks follows only
  // these (after slow movement the pick may run at once).
  void NoteFastMove(int64_t now_us) {
    last_fast_move_us_.store(now_us == 0 ? 1 : now_us, std::memory_order_relaxed);
  }
  int64_t LastFastMoveUs() const { return last_fast_move_us_.load(std::memory_order_relaxed); }
  void NoteMagnetTick() { magnet_ticks_.fetch_add(1, std::memory_order_relaxed); }
  uint64_t MagnetTicks() const { return magnet_ticks_.load(std::memory_order_relaxed); }
  void NoteRelease() { releases_.fetch_add(1, std::memory_order_relaxed); }
  void NoteSuppressedPick() { suppressed_picks_.fetch_add(1, std::memory_order_relaxed); }
  void NoteHop(int64_t now_us) {
    hops_.fetch_add(1, std::memory_order_relaxed);
    last_hop_us_.store(now_us == 0 ? 1 : now_us, std::memory_order_relaxed);
  }
  int64_t LastHopUs() const { return last_hop_us_.load(std::memory_order_relaxed); }
  uint64_t Releases() const { return releases_.load(std::memory_order_relaxed); }
  uint64_t SuppressedPicks() const { return suppressed_picks_.load(std::memory_order_relaxed); }
  uint64_t Hops() const { return hops_.load(std::memory_order_relaxed); }
  // When this aim began (NewAimSession), so "the mouse moved during this aim"
  // is LastMoveUs() >= AimSessionUs().
  void NoteAimSession(int64_t now_us) {
    aim_session_us_.store(now_us == 0 ? 1 : now_us, std::memory_order_relaxed);
  }
  int64_t AimSessionUs() const { return aim_session_us_.load(std::memory_order_relaxed); }
  bool MouseUsedThisAim() const {
    const int64_t m = LastMoveUs();
    return m != 0 && m >= AimSessionUs();
  }

 private:
  static uint64_t NaNPairBits() {
    const float n = std::numeric_limits<float>::quiet_NaN();
    uint32_t b = 0;
    std::memcpy(&b, &n, 4);
    return (static_cast<uint64_t>(b) << 32) | b;
  }
  std::atomic<int64_t> last_tick_us_{0};
  std::atomic<uint64_t> cursor_bits_{0};  // float x (high), float y (low); 0 = 0.0, 0.0
  std::atomic<uint64_t> ticks_{0};
  std::atomic<uint32_t> targeting_bits_{0};  // PackTargeting; 0 = never noted
  std::atomic<uint64_t> target_bits_{NaNPairBits()};  // float x (high), y (low)
  std::atomic<int32_t> target_front_{kTargetNotComputed};
  std::atomic<int64_t> last_move_us_{0};
  std::atomic<int64_t> last_fast_move_us_{0};
  std::atomic<uint64_t> magnet_ticks_{0};
  std::atomic<uint64_t> releases_{0};
  std::atomic<uint64_t> suppressed_picks_{0};
  std::atomic<uint64_t> hops_{0};
  std::atomic<int64_t> last_hop_us_{0};
  std::atomic<int64_t> aim_session_us_{0};
};

inline AimState& State() {
  static AimState s;
  return s;
}

// The aim chain (sub_827B60C8) maps the stick s to |s|³ in the same direction
// before smoothing: sub_822A8EF8 normalises the vector and returns its length,
// which is then cubed (read from the generated code; not yet measured in game).
// Where the left stick becomes the aim action isn't located, so any deadzone
// there is unknown; 0 until measured with `f2 pad stick` (context/mouse
// aiming/, test 3).
constexpr double kStickMax = 32767.0;
constexpr double kAimDeadzone = 0.0;

// The game's response, for the tests: stick (each axis -1..1) -> cursor input.
inline void GameAimControl(double sx, double sy, double& cx, double& cy) {
  const double m = std::hypot(sx, sy);
  if (m <= kAimDeadzone || m <= 0.0) {
    cx = cy = 0.0;
    return;
  }
  const double r = std::min(1.0, (m - kAimDeadzone) / (1.0 - kAimDeadzone));
  const double out = r * r * r;
  cx = sx / m * out;
  cy = sy / m * out;
}

// The left stick (int16 units) that makes the aim chain receive (cx, cy).
// Wanted magnitudes above 1 are capped at full stick (returns true); a NaN or
// infinite input gives a centred stick. Rounds away from zero so a tiny wanted
// input never rounds back to nothing.
inline bool StickForAim(double cx, double cy, int32_t& lx, int32_t& ly) {
  lx = ly = 0;
  if (!std::isfinite(cx) || !std::isfinite(cy)) return false;
  double mc = std::hypot(cx, cy);
  if (mc < 1e-12) return false;
  const bool capped = mc > 1.0;
  if (capped) {
    cx /= mc;
    cy /= mc;
    mc = 1.0;
  }
  const double ms = kAimDeadzone + (1.0 - kAimDeadzone) * std::cbrt(mc);
  auto axis = [&](double c) {
    const double v = c / mc * ms * kStickMax;
    const double r = v >= 0 ? std::ceil(v) : std::floor(v);
    return static_cast<int32_t>(std::clamp(r, -kStickMax, kStickMax));
  };
  lx = axis(cx);
  ly = axis(cy);
  return capped;
}

// The aim path of the input driver: mouse movement in, left stick out. Each
// poll's movement goes into a velocity window one LF tick long (the aim
// cursor moves once per tick and reads one poll per tick, so every poll must
// carry the whole tick's movement, as mousestick::VelocityWindow does for the
// camera), which becomes the wanted cursor input (mousestick::WindowToControl,
// scale = mouse_aim_scale), then the stick that undoes the game's cubic curve
// (StickForAim). Single-threaded: the input driver's polling thread.
class AimStick {
 public:
  // One poll while aiming. moved = this poll read the cursor or carries
  // injected movement (dx, dy, screen pixels, y down). Returns true when the
  // wanted input was capped at full stick.
  bool Update(int64_t now_us, int64_t tick_us, bool moved, int32_t dx, int32_t dy, int32_t scale,
              int32_t& lx, int32_t& ly, int32_t& win_x, int32_t& win_y) {
    const int64_t since = last_poll_us_ ? last_poll_us_ : now_us;
    last_poll_us_ = now_us;
    if (moved) window_.Add(since, now_us, dx, dy);
    double sum_x = 0.0;
    double sum_y = 0.0;
    window_.Sum(now_us, tick_us, sum_x, sum_y);
    win_x = static_cast<int32_t>(std::lround(sum_x));
    win_y = static_cast<int32_t>(std::lround(sum_y));
    double cx = 0.0;
    double cy = 0.0;
    mousestick::WindowToControl(sum_x, sum_y, tick_us / 1e6, scale, cx, cy);
    return StickForAim(cx, cy, lx, ly);
  }

  // Not aiming: the next aim starts from a still mouse.
  void Reset() {
    window_.Clear();
    last_poll_us_ = 0;
  }

 private:
  mousestick::VelocityWindow window_;
  int64_t last_poll_us_ = 0;
};

}  // namespace fable2::mouseaim
