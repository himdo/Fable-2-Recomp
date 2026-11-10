// fable_2 - Photo mode: state shared by the input driver, the guest hooks and
// the on-screen panel.
//
// Photo mode is split over three threads:
//   * the host input poll (KeyboardGamepadDriver::GetDeviceState, see
//     src/input/keyboard_gamepad.h) edge-detects the hotkeys, and while photo
//     mode is on frees the cursor, turns RMB-drag into look deltas and stops
//     feeding the keyboard map into the guest pad;
//   * the guest main thread (src/core/fable2_photo_mode.h) freezes the world,
//     flies the camera, writes the lens/colour settings into the game and
//     restores everything on exit;
//   * the UI thread draws the settings panel (src/core/photo_mode_dialog.h).
// The panel writes Settings under the mutex; the guest thread copies it once
// per world update. Everything else is a single atomic.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

namespace rex::ui {
class Presenter;
}

namespace fable2::photomode {

// Startup log tag: makes a stale exe obvious in the log.
inline constexpr const char* kBuildTag = "photo-mode test build 1";

// Theme (colour grading) parameters the panel scales. Index = slot in the
// game's 194-entry theme table (descriptor table at 0x8349E848).
enum ThemeSlot : int {
  kFogNearDensity = 16,
  kFogFarDensity = 18,
  kExposure = 22,
  kContrast = 23,
  kBloomThreshold = 34,
  kBloomStrength = 35,
  kOverlayStrength = 43,
  kSaturation = 44,
  kBrightness = 46,
  kSunBeams = 48,  // RadialBlurFactor (the "SunBeams" group)
  kGroundMist = 189,
  kRainDensity = 74,
  kRainSize = 73,
  kSnowSize = 71,
  kSnowFallSpeed = 72,
  kStarBrightness = 70,
  kMoonGlare = 67,
  kSunDiscIntensity = 94,
};

// What the depth-of-field section does.
enum class DofMode : int {
  kGame = 0,    // leave the game's own depth of field alone
  kOff = 1,     // no depth of field
  kFocus = 2,   // focus distance / range / strength sliders
  kRaw = 3,     // the camera's six raw depth-of-field values
};

struct Settings {
  // Camera.
  bool freeze_world = true;
  bool hide_hud = true;
  bool hide_world_icons = true;
  float move_speed = 4.0f;    // metres per second
  float look_speed = 1.0f;    // multiplier on mouse/stick/arrow look
  float fov_deg = 60.0f;
  float roll_deg = 0.0f;

  // Depth of field.
  DofMode dof_mode = DofMode::kGame;
  float dof_focus = 4.0f;     // metres from the camera
  float dof_range = 2.0f;     // depth that stays sharp
  float dof_falloff = 6.0f;   // distance over which blur ramps to full
  float dof_strength = 1.0f;  // 0..1
  float dof_raw[6] = {0, 0, 0, 0, 0, 0};
  float dof_alpha = 1.0f;

  // Lens.
  float vignette_a = 0.0f;  // camera "screen darkening" parameter 1 (cam+196)
  float vignette_b = 0.0f;  // camera "screen darkening" parameter 2 (cam+204)
  float blur = 0.0f;        // full-screen blur (cam+212)

  // Colour and light: multipliers on the game's own live values (1 = as is).
  float exposure = 1.0f;
  float contrast = 1.0f;
  float saturation = 1.0f;
  float brightness = 1.0f;
  float bloom = 1.0f;
  float bloom_threshold = 1.0f;
  float sun_beams = 1.0f;
  float overlay = 1.0f;
  float fog = 1.0f;
  float ground_mist = 1.0f;
  // Weather and sky (multipliers on the game's own values).
  float rain = 1.0f;
  float snow = 1.0f;
  float stars = 1.0f;
  float moon_glare = 1.0f;
  float sun_disc = 1.0f;
  int filter = 0;  // index into kFilterPresets (the panel's Filter list)
  // Composition aids (drawn on screen only; never in a screenshot).
  int guides = 0;      // 0 none, 1 rule of thirds, 2 centre grid, 3 both
  int letterbox = 0;   // 0 none, 1 1.85:1, 2 2.35:1, 3 2.39:1 (cinema bars)

  // Time of day (0..24 h). Only applied while override_time is on.
  bool override_time = false;
  float time_hours = 12.0f;
};

// Camera filters: each sets the colour, light and lens sliders to a named look.
// Values are the same controls the panel shows (colour ones multiply the game's
// own grading), so a filter is a starting point you can keep tweaking.
struct FilterPreset {
  const char* name;
  float exposure, contrast, saturation, brightness, bloom, sun_beams, overlay, fog;
  float vignette_a, vignette_b, blur;
};

inline constexpr FilterPreset kFilterPresets[] = {
    // name            exp   con   sat   bri   bloom sun   ovl   fog   vig_a vig_b blur
    {"Original",       1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 0.00f, 0.00f, 0.00f},
    {"Black and White",1.00f, 1.20f, 0.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 0.00f, 0.00f, 0.00f},
    {"Noir",           0.90f, 1.60f, 0.00f, 0.85f, 1.00f, 0.80f, 1.20f, 1.10f, 0.45f, 0.30f, 0.00f},
    {"High Contrast Mono", 1.05f, 1.80f, 0.00f, 1.00f, 0.80f, 1.00f, 1.00f, 1.00f, 0.00f, 0.00f, 0.00f},
    {"Sepia",          1.05f, 0.90f, 0.35f, 1.05f, 1.00f, 1.00f, 1.40f, 1.00f, 0.15f, 0.10f, 0.00f},
    {"Vintage",        1.00f, 0.85f, 0.70f, 1.10f, 0.90f, 1.00f, 1.60f, 1.20f, 0.30f, 0.20f, 0.00f},
    {"Faded Film",     1.10f, 0.75f, 0.65f, 1.15f, 0.90f, 1.00f, 1.50f, 1.00f, 0.00f, 0.00f, 0.00f},
    {"Teal and Orange",1.00f, 1.25f, 1.20f, 1.00f, 1.10f, 1.20f, 1.00f, 1.00f, 0.10f, 0.05f, 0.00f},
    {"Golden Hour",    1.10f, 1.05f, 1.15f, 1.10f, 1.25f, 1.80f, 1.00f, 0.90f, 0.10f, 0.05f, 0.00f},
    {"Cool Night",     0.85f, 1.10f, 0.85f, 0.90f, 1.20f, 0.60f, 1.00f, 1.30f, 0.25f, 0.15f, 0.00f},
    {"Dusk",           0.80f, 1.05f, 0.90f, 0.85f, 1.00f, 0.70f, 1.00f, 1.20f, 0.35f, 0.20f, 0.00f},
    {"Pastel",         1.15f, 0.70f, 0.75f, 1.25f, 1.10f, 1.00f, 1.00f, 1.10f, 0.00f, 0.00f, 0.00f},
    {"Dreamy Glow",    1.05f, 0.90f, 1.00f, 1.05f, 2.20f, 1.30f, 1.00f, 1.60f, 0.00f, 0.00f, 0.15f},
    {"Soft Focus",     1.05f, 0.90f, 1.00f, 1.05f, 1.80f, 1.10f, 1.00f, 1.20f, 0.00f, 0.00f, 0.25f},
    {"Polaroid",       1.10f, 0.85f, 0.95f, 1.15f, 1.00f, 1.00f, 1.30f, 1.00f, 0.10f, 0.05f, 0.00f},
    {"Kodachrome",     1.00f, 1.15f, 1.40f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 0.00f, 0.00f, 0.00f},
    {"Cross-process",  1.05f, 1.35f, 1.25f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 0.10f, 0.05f, 0.00f},
    {"Matte",          1.05f, 0.80f, 0.80f, 1.20f, 0.90f, 1.00f, 1.30f, 1.00f, 0.00f, 0.00f, 0.00f},
    {"Bleach Bypass",  1.00f, 1.50f, 0.30f, 1.00f, 1.00f, 1.00f, 1.20f, 1.00f, 0.15f, 0.10f, 0.00f},
    {"High Key",       1.35f, 0.70f, 0.95f, 1.35f, 1.30f, 1.00f, 1.00f, 0.80f, 0.00f, 0.00f, 0.00f},
    {"Low Key",        0.70f, 1.45f, 0.95f, 0.70f, 0.90f, 0.70f, 1.00f, 1.30f, 0.45f, 0.30f, 0.00f},
    {"Vivid",          1.00f, 1.30f, 1.60f, 1.00f, 1.10f, 1.00f, 1.00f, 1.00f, 0.00f, 0.00f, 0.00f},
};
inline constexpr int kFilterPresetCount = static_cast<int>(sizeof(kFilterPresets) / sizeof(kFilterPresets[0]));

// Copies a filter's colour and lens values into s; leaves the camera settings alone.
inline void apply_filter(Settings& s, const FilterPreset& f) {
  s.exposure = f.exposure;
  s.contrast = f.contrast;
  s.saturation = f.saturation;
  s.brightness = f.brightness;
  s.bloom = f.bloom;
  s.sun_beams = f.sun_beams;
  s.overlay = f.overlay;
  s.fog = f.fog;
  s.vignette_a = f.vignette_a;
  s.vignette_b = f.vignette_b;
  s.blur = f.blur;
}

inline std::mutex& settings_mutex() {
  static std::mutex m;
  return m;
}
inline Settings& settings_unlocked() {
  static Settings s;
  return s;
}
inline Settings settings_copy() {
  std::lock_guard<std::mutex> lock(settings_mutex());
  return settings_unlocked();
}

// --- On/off -----------------------------------------------------------------
// want: what the user asked for (hotkey / pad / panel). active: what the guest
// side is actually doing (it enters on the next world update, or refuses when
// the world is not running, e.g. in a menu).
inline std::atomic<bool>& want_active() {
  static std::atomic<bool> v{false};
  return v;
}
inline std::atomic<bool>& active_flag() {
  static std::atomic<bool> v{false};
  return v;
}
inline bool active() { return active_flag().load(std::memory_order_acquire); }
// When the last toggle was asked for (steady-clock ms). The guest drops an
// "enter" request it could not serve quickly (e.g. pressed in a menu), so photo
// mode never pops up later on its own.
inline std::atomic<int64_t>& toggle_time_ms() {
  static std::atomic<int64_t> v{0};
  return v;
}
inline int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
inline void request_toggle() {
  toggle_time_ms().store(now_ms());
  want_active().store(!want_active().load());
}
inline void request_exit() { want_active().store(false); }

// --- One-shot requests from the input poll / panel ---------------------------
inline std::atomic<bool>& screenshot_request() {
  static std::atomic<bool> v{false};
  return v;
}
// Timed and burst screenshots: the panel sets the count, delay and spacing;
// the guest thread takes the shots when their time comes.
struct ShotPlan {
  int shots = 1;           // frames to capture (burst)
  int delay_s = 0;         // wait before the first shot (self-timer)
  int spacing_ms = 400;    // gap between burst shots
};
inline std::mutex& shot_plan_mutex() {
  static std::mutex m;
  return m;
}
inline ShotPlan& shot_plan_unlocked() {
  static ShotPlan p;
  return p;
}
inline std::atomic<int64_t>& shot_next_ms() {  // 0 = nothing scheduled
  static std::atomic<int64_t> v{0};
  return v;
}
inline std::atomic<int>& shots_left() {
  static std::atomic<int> v{0};
  return v;
}
inline std::atomic<bool>& reset_camera_request() {
  static std::atomic<bool> v{false};
  return v;
}
// Panel visibility (H / pad Y). The panel is never in the screenshot anyway.
inline std::atomic<bool>& panel_visible() {
  static std::atomic<bool> v{true};
  return v;
}

// --- Menu navigation (pad on the guest thread, keys on the input poll -> the
// menu on the UI thread). Counters: the menu drains them each frame. ----------
inline std::atomic<int>& nav_y() {  // +1 = down
  static std::atomic<int> v{0};
  return v;
}
inline std::atomic<int>& nav_x() {  // +1 = right (increase)
  static std::atomic<int> v{0};
  return v;
}
inline std::atomic<int>& nav_tab() {  // +1 = next tab
  static std::atomic<int> v{0};
  return v;
}
inline std::atomic<int>& nav_tab_direct() {  // 1..N = jump to tab, 0 = none
  static std::atomic<int> v{0};
  return v;
}
inline std::atomic<int>& nav_activate() {  // A / Enter
  static std::atomic<int> v{0};
  return v;
}
// Which prompts to show: true after pad input, false after keys / mouse.
inline std::atomic<bool>& prompts_pad() {
  static std::atomic<bool> v{false};
  return v;
}

// Key / button auto-repeat: fires on press, then repeats after a pause, faster
// the longer it is held.
struct Repeater {
  bool held = false;
  int64_t start = 0, next = 0;
  int Fire(bool down, int64_t now) {
    if (!down) {
      held = false;
      return 0;
    }
    if (!held) {
      held = true;
      start = next = now;
      next = now + 380;
      return 1;
    }
    if (now < next) return 0;
    const int64_t h = now - start;
    next = now + (h > 1600 ? 35 : h > 900 ? 60 : 100);
    return 1;
  }
};

// --- Host input -> guest -----------------------------------------------------
// RMB-drag look: the input poll adds cursor pixels, the guest drains them.
inline std::atomic<int32_t>& look_dx() {
  static std::atomic<int32_t> v{0};
  return v;
}
inline std::atomic<int32_t>& look_dy() {
  static std::atomic<int32_t> v{0};
  return v;
}
// Mouse wheel over the game (not over the panel): FOV steps, drained by guest.
inline std::atomic<float>& wheel_acc() {
  static std::atomic<float> v{0.0f};
  return v;
}
// Game window focused (set by the input poll; keys are ignored otherwise).
inline std::atomic<bool>& window_focused() {
  static std::atomic<bool> v{false};
  return v;
}
// ImGui wants the mouse / keyboard (cursor over the panel, typing a value).
inline std::atomic<bool>& ui_wants_mouse() {
  static std::atomic<bool> v{false};
  return v;
}
inline std::atomic<bool>& ui_wants_keyboard() {
  static std::atomic<bool> v{false};
  return v;
}

// Pad state captured before photo mode blanks it for the game (guest thread
// writes and reads; kept here so the panel can show "pad connected").
struct Pad {
  uint16_t buttons = 0;
  uint8_t lt = 0, rt = 0;
  int16_t lx = 0, ly = 0, rx = 0, ry = 0;
};

// Presenter for screenshots (set by Fable2App; the presenter may not exist yet
// at startup, so it is queried when a screenshot is taken).
inline std::function<rex::ui::Presenter*()>& presenter_provider() {
  static std::function<rex::ui::Presenter*()> f;
  return f;
}

// --- Status shown in the panel -----------------------------------------------
inline std::mutex& status_mutex() {
  static std::mutex m;
  return m;
}
inline std::string& status_text() {
  static std::string s;
  return s;
}
inline void set_status(std::string text) {
  std::lock_guard<std::mutex> lock(status_mutex());
  status_text() = std::move(text);
}
inline std::string status() {
  std::lock_guard<std::mutex> lock(status_mutex());
  return status_text();
}

}  // namespace fable2::photomode
