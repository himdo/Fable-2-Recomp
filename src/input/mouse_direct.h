// fable_2 - direct mouse camera: mouse movement turns the gameplay camera by a
// fixed angle per count, instead of driving the right stick.
//
// Why: through the stick (mouse_stick.h) the mouse inherits the stick's top
// speed (8° per camera update, ~120°/s at in-game sensitivity 0.5) and the
// camera script's own smoothing, which ramps up over ~0.5 s and keeps turning
// for ~0.3 s after the input stops. A flick then feels like a held stick. In
// direct mode the mouse is a displacement: the turn depends only on how far
// the mouse moved, it starts on the next camera update and stops with the mouse.
//
// How (scripts/recomp/mouse_direct.lua does the camera side):
//   1. Each input poll adds its mouse movement (cursor + `f2 mouse inject`) to
//      DirectState while the route is "direct".
//   2. Once per camera update, the Lua wrapper on SACCamera.CheckForTeleport
//      (the first method SACCamera.FrameUpdate calls) asks for it through the
//      game's cprint ("[f2mouse] 1"). The host's cprint override drains the
//      movement, converts it (ToCamera) and returns two numbers to Lua.
//   3. The wrappers on ApplyRotateVector and GetHeight apply them after the
//      game's own smoothing: the yaw rotates the camera about its focal point,
//      the height is added to the script's height offset (still clamped at
//      ±0.7, still drifting back to the middle like the stick).
//
// The host keeps no camera angle, only movement not yet delivered, so the
// game stays the single owner of where the camera points: the stick, collision
// and the height limit keep working, and nothing can drift out of sync.
//
// Routing: the route is "direct" while the camera reported "accepting" within
// the last RouteTimeoutUs. Otherwise (lock-on, cutscenes, menus, other
// cameras, or the Lua side not running) the mouse falls back to the stick path
// and anything collected is discarded, so nothing is released later as a snap.
//
// Pure apart from std; tested by tests/native/test_mouse_direct.cpp.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace fable2::mousedirect {

constexpr double kDegToRad = 0.017453292519943295;

// Settings that turn mouse counts into camera units.
struct Settings {
  double sens_x_deg = 0.06;  // yaw degrees per count (mouse_look_sens_x)
  double sens_y_deg = 0.06;  // height per count, as an angle (mouse_look_sens_y)
  int32_t scale = 256;       // mouse_look_scale, 256 = 1x (the launcher slider)
  bool invert_y = false;     // mouse_look_invert_y
};

struct CameraDelta {
  double yaw = 0.0;     // degrees, same sign as SACCamera.RotateRight
  double height = 0.0;  // added to SACCamera.ManualHeightOffset
};

// Mouse counts (x right, y down) -> what the camera script adds.
// Yaw: the script turns by -control_x * speed, so mouse right (dx > 0) is a
// negative angle, the same as pushing the stick right.
// Height: the offset is a slope (rise over run) and stick up lowers it (the
// camera drops and looks up), so mouse up (dy < 0) lowers it too. Near the
// middle a slope of h is an angle of about h radians, so sens_y in degrees per
// count keeps up/down and left/right at the same angular sensitivity there.
// The in-game camera sensitivity (0.5 + sens) is applied by the Lua side.
inline CameraDelta ToCamera(double dx, double dy, const Settings& s) {
  const double k = std::max<int32_t>(s.scale, 1) / 256.0;
  CameraDelta d;
  d.yaw = -dx * s.sens_x_deg * k;
  d.height = dy * s.sens_y_deg * kDegToRad * k * (s.invert_y ? -1.0 : 1.0);
  return d;
}

// How long without an "accepting" camera update before the mouse goes back to
// the stick: a few camera updates (66.7 ms each at 15 Hz), so one late update
// doesn't flip the route, but at least 200 ms for frame hitches.
inline int64_t RouteTimeoutUs(int64_t camera_update_us) {
  return std::max<int64_t>(camera_update_us * 5 / 2, 200000);
}

// Movement waiting for the camera, plus the route. Polls and the camera's Lua
// run on different guest threads, so everything is under one mutex.
class DirectState {
 public:
  // Input poll. In direct mode while the route is direct, keeps the movement
  // and returns true (the caller reports a centred stick). Otherwise discards
  // anything kept and returns false (the caller uses the stick path).
  bool Feed(bool direct_mode, int64_t now_us, int64_t timeout_us, double dx, double dy) {
    std::lock_guard<std::mutex> lock(mu_);
    const bool direct = direct_mode && last_accept_us_ != 0 && now_us - last_accept_us_ <= timeout_us;
    if (direct) {
      acc_x_ += dx;
      acc_y_ += dy;
    } else {
      acc_x_ = acc_y_ = 0.0;
    }
    route_direct_ = direct;
    return direct;
  }

  // Mouse look off, debug menu open, focus lost: drop what's waiting.
  void Clear() {
    std::lock_guard<std::mutex> lock(mu_);
    acc_x_ = acc_y_ = 0.0;
  }

  // Camera update (cprint). accepting = the camera can be turned right now.
  // smoothing in [0, 0.9]: share of the movement kept for later updates
  // (0 = deliver everything now). Not accepting discards everything and sends
  // the mouse back to the stick at once.
  void Drain(bool accepting, int64_t now_us, double smoothing, double& dx, double& dy) {
    std::lock_guard<std::mutex> lock(mu_);
    ++drains_;
    if (!accepting) {
      acc_x_ = acc_y_ = 0.0;
      last_accept_us_ = 0;
      dx = dy = 0.0;
      return;
    }
    last_accept_us_ = now_us == 0 ? 1 : now_us;
    const double keep = std::clamp(smoothing, 0.0, 0.9);
    dx = acc_x_ * (1.0 - keep);
    dy = acc_y_ * (1.0 - keep);
    acc_x_ -= dx;
    acc_y_ -= dy;
    // Don't leave a tail of tiny fractions after the mouse stops.
    if (std::abs(acc_x_) < 0.05 && std::abs(acc_y_) < 0.05) {
      dx += acc_x_;
      dy += acc_y_;
      acc_x_ = acc_y_ = 0.0;
    }
  }

  bool RouteDirect() {
    std::lock_guard<std::mutex> lock(mu_);
    return route_direct_;
  }
  uint64_t Drains() {
    std::lock_guard<std::mutex> lock(mu_);
    return drains_;
  }

 private:
  std::mutex mu_;
  double acc_x_ = 0.0, acc_y_ = 0.0;
  int64_t last_accept_us_ = 0;  // 0 = no accepting camera update yet
  bool route_direct_ = false;
  uint64_t drains_ = 0;
};

inline DirectState& State() {
  static DirectState s;
  return s;
}

// Starting the Lua side. The host runs scripts/recomp/mouse_direct.lua through
// the game's RunScript, which only works once a save is loaded. The script
// reports in at every camera update. It has no per-frame script of its own:
// the game writes those into save games, and an older copy restored from a
// save fought the current one over the camera (2026-11-12).
//
// So the host (re)starts it when it is needed and missing: the mouse is being
// moved in direct mode but isn't reaching the camera (Wanted), and nothing has
// been heard from the script for a while (after a camera script reload, say).
// The game's scripts stand still in menus and while the window is in the
// background, so silence alone isn't a reason (measured 2026-11-12: that
// restarted it every 15 s in menus). Retries back off from 5 to 30 s, and a
// run with nothing to do changes nothing.
class Boot {
 public:
  static constexpr int64_t kFirstDelayUs = 8'000'000;
  static constexpr int64_t kMinBackoffUs = 5'000'000;
  static constexpr int64_t kMaxBackoffUs = 30'000'000;
  static constexpr int64_t kAliveUs = 15'000'000;

  static constexpr int64_t kWantedUs = 2'000'000;

  // Input poll: the mouse moved in direct mode but went to the stick.
  void Wanted(int64_t now_us) {
    std::lock_guard<std::mutex> lock(mu_);
    last_wanted_us_ = now_us == 0 ? 1 : now_us;
  }

  // A per-frame keeper from an early version of this branch reported in
  // (only those send heartbeats, or the old "camera wrapped" note). One comes
  // back with a save game made while it ran. It re-wraps the camera every 2 s,
  // so the current script must not wrap while it is alive: the two would end
  // up calling each other. Its own wrappers use the same channel and drive the
  // direct mode instead.
  static constexpr int64_t kOldKeeperUs = 20'000'000;
  void OldKeeperSeen(int64_t now_us) {
    std::lock_guard<std::mutex> lock(mu_);
    old_keeper_us_ = now_us == 0 ? 1 : now_us;
  }
  bool OldKeeperAlive(int64_t now_us) {
    std::lock_guard<std::mutex> lock(mu_);
    return old_keeper_us_ != 0 && now_us - old_keeper_us_ <= kOldKeeperUs;
  }

  // Called once per frame in direct mode; true = run the script now.
  bool ShouldTry(int64_t now_us) {
    std::lock_guard<std::mutex> lock(mu_);
    if (!started_) {
      started_ = true;
      next_try_us_ = now_us + kFirstDelayUs;
    }
    if (last_alive_us_ != 0 && now_us - last_alive_us_ <= kAliveUs) return false;
    if (last_wanted_us_ == 0 || now_us - last_wanted_us_ > kWantedUs) return false;
    if (now_us < next_try_us_) return false;
    next_try_us_ = now_us + backoff_us_;
    backoff_us_ = std::min(backoff_us_ * 2, kMaxBackoffUs);
    ++tries_;
    return true;
  }

  // The script reported in (boot, heartbeat or a camera update).
  void Alive(int64_t now_us) {
    std::lock_guard<std::mutex> lock(mu_);
    last_alive_us_ = now_us == 0 ? 1 : now_us;
    backoff_us_ = kMinBackoffUs;
    next_try_us_ = 0;  // if it goes missing, try again as soon as it counts as gone
  }

  // "boot" resets it; true for the first heartbeat after that, so the log
  // shows the per-frame keeper is running.
  void Booted() {
    std::lock_guard<std::mutex> lock(mu_);
    heartbeat_seen_ = false;
  }
  bool FirstHeartbeat() {
    std::lock_guard<std::mutex> lock(mu_);
    const bool first = !heartbeat_seen_;
    heartbeat_seen_ = true;
    return first;
  }

  bool IsAlive(int64_t now_us) {
    std::lock_guard<std::mutex> lock(mu_);
    return last_alive_us_ != 0 && now_us - last_alive_us_ <= kAliveUs;
  }
  uint32_t Tries() {
    std::lock_guard<std::mutex> lock(mu_);
    return tries_;
  }

 private:
  std::mutex mu_;
  bool started_ = false;
  int64_t next_try_us_ = 0;
  int64_t last_alive_us_ = 0;
  int64_t last_wanted_us_ = 0;
  int64_t backoff_us_ = kMinBackoffUs;
  bool heartbeat_seen_ = false;
  int64_t old_keeper_us_ = 0;
  uint32_t tries_ = 0;
};

inline Boot& BootState() {
  static Boot b;
  return b;
}

// Lua 5.1 TValues for numbers as the game's Lua stores them: an 8-byte slot
// with the 4-byte float at +0 and the type tag (LUA_TNUMBER = 3) at +4, both
// big-endian. Writes n * 8 bytes to out.
inline void EncodeNumbers(const float* values, int n, uint8_t* out) {
  for (int i = 0; i < n; ++i) {
    uint32_t bits = 0;
    std::memcpy(&bits, &values[i], 4);
    uint8_t* p = out + 8 * i;
    p[0] = static_cast<uint8_t>(bits >> 24);
    p[1] = static_cast<uint8_t>(bits >> 16);
    p[2] = static_cast<uint8_t>(bits >> 8);
    p[3] = static_cast<uint8_t>(bits);
    p[4] = p[5] = p[6] = 0;
    p[7] = 3;  // LUA_TNUMBER
  }
}

}  // namespace fable2::mousedirect
