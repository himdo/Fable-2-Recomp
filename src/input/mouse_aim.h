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

 private:
  std::atomic<int64_t> last_tick_us_{0};
  std::atomic<uint64_t> cursor_bits_{0};  // float x (high), float y (low); 0 = 0.0, 0.0
  std::atomic<uint64_t> ticks_{0};
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
