// fable_2 - Photo mode, guest side (world freeze, free camera, effects).
//
// Included once, from main.cpp (after the generated declarations). Shared
// state lives in src/input/photo_mode_state.h, the panel in
// src/core/photo_mode_dialog.h, the hotkeys in src/input/keyboard_gamepad.h.
//
// Three recompiled functions are overridden with strong definitions (the
// originals stay callable as __imp__*):
//
//   sub_821D43D8  world game-state update (vtable 0x8200249C slot 6). It does
//                 nothing unless the world state is on top of the game-state
//                 stack (that is how the pause menu freezes the world), then
//                 updates the world, the theme (colour grading) and finally the
//                 cameras. Photo mode runs only the camera part while "Pause the
//                 world" is on, so everything stands still but the view moves.
//   CECCameraUpdateFrame_822A47C0
//                 per-camera update. Before the original runs, photo mode writes
//                 the free-camera pose and the lens settings into the camera's
//                 current AND target fields with the ease coefficient at 1, so
//                 they apply exactly, the same frame.
//   ProcessAndProcessAndProcess1013_822B2D60
//                 the game's only XamInputGetState call. Photo mode reads the pad
//                 here, merged with a host XInput read (src/input/host_pad.h, which also
//                 sees Guide for the controller shortcut), then blanks the
//                 state the game sees, so the hero and the menus ignore it.
//
// CECCamera fields (bytes from the camera object, floats unless noted):
//   +24/+28 FOV current/target (radians)   +32..52/+56..76 depth of field x6
//   +80/+84 DOF alpha                       +96/+100 roll current/target
//   +144 eye (x,y,z)                        +160/+176 orientation quat (x,y,z,w)
//   +192 accumulated follow dt              +196/+200, +204/+208 screen darkening
//   +212/+216 full-screen blur              +220 ease countdown (u32), +228 ease
// Camera local axes: X right, Y forward, Z up; the world is Z-up.
//
// Theme: World = *(*(*(*0x83496920 + 12) + 88) + 4); the blended theme the
// renderer uses is *(*(World + 80) + 12); parameter i is the float at
// +16 + 16*i. The game rebuilds it inside the world update, so photo mode scales
// it right after; with the world paused it is not rebuilt and keeps the values.
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include <rex/logging/macros.h>
#include <rex/ui/color_grade.h>
#include <rex/ui/presenter.h>

#include "fable2_config.h"
#include "fable2_frame_capture.h"
#include "host_pad.h"
#include "mouse_look.h"
#include "photo_mode_state.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace fable2::photomode {

namespace detail {

// Guest addresses.
constexpr uint32_t kHub = 0x83496920;                 // *kHub + 12 = game
constexpr uint32_t kDrawGuiFlag = 0x8331930E;         // byte SetDrawGUI writes
constexpr uint32_t kIconsRoot = 0x834968A4;           // world-icons state root
constexpr uint32_t kSetDrawGUI = 0x823AC520;          // (r3 = bool)
constexpr uint32_t kSetShouldDrawWorldIcons = 0x823AC5B8;  // (r3 = bool)
constexpr uint32_t kSetTimeOfDayAbsolute = 0x8249A790;     // (r3 timing, f1 0..1)
constexpr uint32_t kResolveSubsystemReference = 0x821F8760;
constexpr uint32_t kUpdateCamerasTimebase = 0x821B9320;
constexpr uint32_t kThemeLightUpdate = 0x822BD510;    // (r3 = theme manager)
constexpr uint32_t kThemeUpdate = 0x8218EB30;         // (r3 = theme manager)

constexpr float kPi = 3.14159265358979f;
constexpr float kDeg = kPi / 180.0f;

inline bool ptr_ok(uint32_t a) { return a != 0 && a != 0xFFFFFFFFu && (a & 3) == 0; }

// Camera fields photo mode writes and restores on exit (byte ranges, inclusive).
struct Range { uint32_t from, to; };
constexpr Range kSavedRanges[] = {{24, 100}, {144, 188}, {196, 228}};

struct CameraSnapshot {
  uint32_t cam = 0;
  bool restore_pending = false;
  uint32_t words[((100 - 24) / 4 + 1) + ((188 - 144) / 4 + 1) + ((228 - 196) / 4 + 1)] = {};
};

struct ThemeSlotState {
  int index;
  float Settings::*mult;
  float base = 0.0f;     // the game's own value
  uint32_t written = 0;  // bits photo mode last wrote (detects a rebuild)
  bool valid = false;
};

enum class Phase { kOff, kArming, kOn };

// Guest-thread-only state.
struct State {
  Phase phase = Phase::kOff;
  int arm_calls = 0;

  // Free camera.
  mouselook::Vec3 start_pos{};
  mouselook::Vec3 pos{};
  float yaw = 0.0f, pitch = 0.0f;
  float start_yaw = 0.0f, start_pitch = 0.0f, start_fov = 60.0f;
  std::chrono::steady_clock::time_point last_tick{};
  bool tick_valid = false;

  // Pad, captured in the input override (merged over all users). The pad read
  // and the world update may run on different guest threads.
  std::mutex pad_mutex;
  Pad pad[4]{};
  Pad pad_prev_frame{};  // merged pad at the previous world update
  uint16_t toggle_prev[4] = {0, 0, 0, 0};
  Pad host_pad{};           // controllers read on the host (XInput), merged in
  uint16_t rebind_peak = 0;  // buttons held so far while recording a shortcut
  bool rebind_armed = false; // all buttons let go since recording started
  int64_t rebind_session = 0;  // pad_rebind_started_ms of the recording seen
  Repeater rep_up, rep_down, rep_left, rep_right;  // D-pad menu navigation
  std::atomic<bool> suppress_pad{false};  // keep blanking until all released

  // Cameras touched this session. After leaving, each is restored on its next
  // update; leftovers are dropped after a short while so a freed camera's
  // address is never written to later.
  CameraSnapshot cams[8];
  int cam_count = 0;
  int restore_updates_left = 0;

  // HUD.
  bool hud_saved = false;
  uint8_t saved_draw_gui = 1;
  uint8_t saved_icons = 1;
  int applied_hud = -1;    // -1 = untouched, else 0/1 hidden
  int applied_icons = -1;

  // Time of day.
  bool tod_saved = false;
  float saved_tod = 0.5f;
  float applied_tod = -1.0f;

  // Exposure / contrast / saturation / brightness are not here: they go
  // through the GPU colour grade (build_grade), which always works on the
  // finished frame, instead of the game's tone-mapping inputs.
  ThemeSlotState theme[14] = {
      {kBloomStrength, &Settings::bloom},
      {kBloomThreshold, &Settings::bloom_threshold},
      {kSunBeams, &Settings::sun_beams},
      {kOverlayStrength, &Settings::overlay},
      {kFogNearDensity, &Settings::fog},
      {kFogFarDensity, &Settings::fog},
      {kGroundMist, &Settings::ground_mist},
      {kRainDensity, &Settings::rain},
      {kRainSize, &Settings::rain},
      {kSnowSize, &Settings::snow},
      {kSnowFallSpeed, &Settings::snow},
      {kStarBrightness, &Settings::stars},
      {kMoonGlare, &Settings::moon_glare},
      {kSunDiscIntensity, &Settings::sun_disc},
  };
  uint32_t theme_out = 0;

  Settings s;  // this frame's copy of the panel settings

  // Diagnostics (logged once a second while photo mode is on).
  int64_t diag_last_ms = 0;
  uint32_t diag_world = 0, diag_cam_calls = 0, diag_cam_writes = 0, diag_frozen = 0;
};

// Pad reads per user (any thread) and the last buttons seen, for the log.
inline std::atomic<uint32_t>& diag_pad_reads() {
  static std::atomic<uint32_t> v{0};
  return v;
}
inline std::atomic<uint32_t>& diag_pad_buttons() {
  static std::atomic<uint32_t> v{0};
  return v;
}

inline State& st() {
  static State s;
  return s;
}

// --- Guest memory -------------------------------------------------------------
inline uint32_t ld32(uint8_t* base, uint32_t a) { return REX_LOAD_U32(a); }
inline uint8_t ld8(uint8_t* base, uint32_t a) { return REX_LOAD_U8(a); }
inline float ldf(uint8_t* base, uint32_t a) {
  const uint32_t bits = REX_LOAD_U32(a);
  float f;
  std::memcpy(&f, &bits, 4);
  return f;
}
inline void stf(uint8_t* base, uint32_t a, float f) {
  uint32_t bits;
  std::memcpy(&bits, &f, 4);
  REX_STORE_U32(a, bits);
}
inline void st32(uint8_t* base, uint32_t a, uint32_t v) { REX_STORE_U32(a, v); }
inline uint32_t fbits(float f) {
  uint32_t b;
  std::memcpy(&b, &f, 4);
  return b;
}

inline uint32_t game_ptr(uint8_t* base) {
  const uint32_t hub = ld32(base, kHub);
  if (!ptr_ok(hub)) return 0;
  const uint32_t game = ld32(base, hub + 12);
  return ptr_ok(game) ? game : 0;
}
inline uint32_t world_ptr(uint8_t* base) {
  const uint32_t game = game_ptr(base);
  if (!game) return 0;
  const uint32_t a = ld32(base, game + 88);
  if (!ptr_ok(a)) return 0;
  const uint32_t w = ld32(base, a + 4);
  return ptr_ok(w) ? w : 0;
}
inline uint32_t theme_manager(uint8_t* base) {
  const uint32_t w = world_ptr(base);
  if (!w) return 0;
  const uint32_t m = ld32(base, w + 80);
  return ptr_ok(m) ? m : 0;
}
inline uint32_t theme_out(uint8_t* base) {
  const uint32_t m = theme_manager(base);
  if (!m) return 0;
  const uint32_t o = ld32(base, m + 12);
  return ptr_ok(o) ? o : 0;
}
inline uint32_t timing_ptr(uint8_t* base) {
  const uint32_t w = world_ptr(base);
  if (!w) return 0;
  const uint32_t t = ld32(base, w + 20);
  return ptr_ok(t) ? t : 0;
}
inline uint32_t icons_state(uint8_t* base) {
  const uint32_t a = ld32(base, kIconsRoot);
  if (!ptr_ok(a)) return 0;
  const uint32_t b = ld32(base, a + 32);
  if (!ptr_ok(b)) return 0;
  const uint32_t c = ld32(base, b + 28);
  return ptr_ok(c) ? c : 0;
}

inline void call_guest(PPCContext& ctx, uint8_t* base, uint32_t addr) {
  REX_CALL_INDIRECT_FUNC(addr);
}

// True when the world state is on top of the game-state stack, i.e. the
// original world update would run (same test as 0x821D4430..0x821D4498).
inline bool world_is_top(PPCContext& ctx, uint8_t* base, uint32_t self) {
  const uint32_t game = game_ptr(base);
  if (!game || !ptr_ok(self)) return false;
  const uint32_t a = ld32(base, game + 128);
  if (!ptr_ok(a)) return false;
  const uint32_t b = ld32(base, a + 4);
  if (!ptr_ok(b)) return false;
  const uint32_t head = ld32(base, b + 4);
  if (!ptr_ok(head)) return false;
  const uint32_t node = ld32(base, head + 4);
  if (!ptr_ok(node) || node == head) return false;  // empty stack
  const uint32_t top = ld32(base, node + 8);
  if (!ptr_ok(top)) return false;
  const uint32_t top_vt = ld32(base, top);
  const uint32_t self_vt = ld32(base, self);
  if (!ptr_ok(top_vt) || !ptr_ok(self_vt)) return false;
  ctx.r3.u64 = top;
  call_guest(ctx, base, ld32(base, top_vt + 4));
  const uint32_t top_type = ctx.r3.u32;
  ctx.r3.u64 = self;
  call_guest(ctx, base, ld32(base, self_vt + 4));
  return ctx.r3.u32 == top_type;
}

// The camera half of the world update (0x821D47F4..0x821D4824).
inline void update_cameras(PPCContext& ctx, uint8_t* base) {
  const uint32_t game = game_ptr(base);
  if (!game || ld8(base, game + 256) != 0) return;
  const uint32_t sub = ld32(base, game + 156);
  if (!ptr_ok(sub)) return;
  ctx.r3.u64 = sub;
  call_guest(ctx, base, kResolveSubsystemReference);
  if (!ptr_ok(ctx.r3.u32)) return;
  const uint32_t cams = ld32(base, ctx.r3.u32 + 4);
  if (!ptr_ok(cams)) return;
  ctx.r3.u64 = cams;
  call_guest(ctx, base, kUpdateCamerasTimebase);
}

// --- Free-camera maths ----------------------------------------------------------
inline mouselook::Vec3 forward_of(float yaw, float pitch) {
  return {-std::sin(yaw) * std::cos(pitch), std::cos(yaw) * std::cos(pitch), std::sin(pitch)};
}
inline mouselook::Quat orientation(float yaw, float pitch, float roll) {
  using namespace mouselook;
  const Vec3 fwd = forward_of(yaw, pitch);
  const Vec3 right = vec3_normalize(Vec3{fwd.y, -fwd.x, 0.0f});  // cross(fwd, +Z)
  const Vec3 up = vec3_cross(right, fwd);
  Quat q = quat_from_basis(right, fwd, up);
  if (roll != 0.0f) q = quat_mul(q, axis_quat(1, roll));  // around local forward
  return q;
}

inline void log_line(const std::string& s) { REXSYS_INFO("[photo-mode] {}", s); }

// --- Camera snapshot / restore -------------------------------------------------------
inline CameraSnapshot* find_snapshot(uint32_t cam) {
  State& g = st();
  for (int i = 0; i < g.cam_count; ++i)
    if (g.cams[i].cam == cam) return &g.cams[i];
  return nullptr;
}
inline CameraSnapshot* take_snapshot(uint8_t* base, uint32_t cam) {
  State& g = st();
  if (CameraSnapshot* s = find_snapshot(cam)) return s;
  if (g.cam_count >= 8) return nullptr;
  CameraSnapshot& s = g.cams[g.cam_count++];
  s = CameraSnapshot{};
  s.cam = cam;
  int w = 0;
  for (const Range& r : kSavedRanges)
    for (uint32_t off = r.from; off <= r.to; off += 4) s.words[w++] = ld32(base, cam + off);
  return &s;
}
inline void restore_snapshot(uint8_t* base, CameraSnapshot& s) {
  int w = 0;
  for (const Range& r : kSavedRanges)
    for (uint32_t off = r.from; off <= r.to; off += 4) {
      const uint32_t v = s.words[w++];
      if (off != 192) st32(base, s.cam + off, v);
    }
  s.restore_pending = false;
}

// --- HUD ---------------------------------------------------------------------------
inline void set_hud(PPCContext& ctx, uint8_t* base, bool hidden) {
  State& g = st();
  if (g.applied_hud == (hidden ? 1 : 0)) return;
  ctx.r3.u64 = hidden ? 0 : g.saved_draw_gui;
  call_guest(ctx, base, kSetDrawGUI);
  g.applied_hud = hidden ? 1 : 0;
}
inline void set_icons(PPCContext& ctx, uint8_t* base, bool hidden) {
  State& g = st();
  if (g.applied_icons == (hidden ? 1 : 0)) return;
  if (!icons_state(base)) return;
  ctx.r3.u64 = hidden ? 0 : g.saved_icons;
  call_guest(ctx, base, kSetShouldDrawWorldIcons);
  g.applied_icons = hidden ? 1 : 0;
}

// --- Time of day ---------------------------------------------------------------------
// Returns true when it changed the time (the theme then needs a rebuild).
inline bool set_time_of_day(PPCContext& ctx, uint8_t* base, float t01) {
  const uint32_t timing = timing_ptr(base);
  if (!timing) return false;
  ctx.r3.u64 = timing;
  ctx.f1.f64 = t01;
  call_guest(ctx, base, kSetTimeOfDayAbsolute);
  return true;
}

// Rebuild the blended theme for the current time of day while the world is
// paused (the same calls the world update makes at 0x82276094..0x8227609C).
inline bool rebuild_theme(PPCContext& ctx, uint8_t* base) {
  const uint32_t mgr = theme_manager(base);
  if (!mgr || ld8(base, mgr + 70) != 0) return false;
  ctx.r3.u64 = mgr;
  call_guest(ctx, base, kThemeLightUpdate);
  ctx.r3.u64 = mgr;
  call_guest(ctx, base, kThemeUpdate);
  return true;
}

// --- Theme (colour grading) ------------------------------------------------------------
inline void apply_theme(uint8_t* base) {
  State& g = st();
  const uint32_t out = theme_out(base);
  if (!out) return;
  if (out != g.theme_out) {
    for (auto& t : g.theme) t.valid = false;
    g.theme_out = out;
  }
  for (auto& t : g.theme) {
    const uint32_t a = out + 16 + 16 * static_cast<uint32_t>(t.index);
    const uint32_t cur = ld32(base, a);
    // A value photo mode did not write means the game rebuilt the theme:
    // take it as the new base.
    if (!t.valid || cur != t.written) {
      std::memcpy(&t.base, &cur, 4);
      t.valid = true;
    }
    const float v = t.base * (g.s.*(t.mult));
    stf(base, a, v);
    t.written = fbits(v);
  }
}
inline void restore_theme(uint8_t* base) {
  State& g = st();
  const uint32_t out = theme_out(base);
  if (out && out == g.theme_out) {
    for (auto& t : g.theme) {
      if (!t.valid) continue;
      const uint32_t a = out + 16 + 16 * static_cast<uint32_t>(t.index);
      if (ld32(base, a) == t.written) stf(base, a, t.base);
    }
  }
  for (auto& t : g.theme) t.valid = false;
  g.theme_out = 0;
}

// --- Colour grade (photo mode filters) ----------------------------------------------
// Same maths as the prototype the presets were tuned with: saturation and
// toning as a colour matrix, white balance, exposure; then contrast around a
// 0.4 pivot, gamma, split toning and faded blacks in the shader.
inline rex::ui::SwapColorGrade build_grade(const Settings& s) {
  const ColorFilter& f = kFilterPresets[std::clamp(s.filter, 0, kFilterPresetCount - 1)];
  const float k = std::clamp(s.filter_strength, 0.0f, 1.0f);
  auto mix = [k](float a, float b) { return a + (b - a) * k; };
  const float sat = std::max(0.0f, mix(1.0f, f.saturation) * s.saturation);
  const float warm = mix(0.0f, f.warmth) + s.warmth;
  const float tint = mix(0.0f, f.tint) + s.tint;
  const float tone = f.tone_amount * k;
  constexpr float L[3] = {0.2126f, 0.7152f, 0.0722f};
  const float wb[3] = {1.0f + 0.10f * warm + 0.03f * tint, 1.0f - 0.06f * tint,
                       1.0f - 0.10f * warm + 0.03f * tint};
  rex::ui::SwapColorGrade g;
  float* rows[3] = {g.row_r, g.row_g, g.row_b};
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) {
      const float satm = (1.0f - sat) * L[j] + (i == j ? sat : 0.0f);
      const float tonem = f.tone[i] * L[j];
      rows[i][j] = s.exposure * wb[i] * ((1.0f - tone) * satm + tone * tonem);
    }
    rows[i][3] = 0.0f;
  }
  g.contrast = std::max(0.05f, mix(1.0f, f.contrast) * s.contrast);
  g.gamma = std::clamp(mix(1.0f, f.gamma) / std::max(0.05f, s.brightness), 0.2f, 5.0f);
  for (int i = 0; i < 3; ++i) {
    g.shadows[i] = f.shadows[i] * k;
    g.highlights[i] = f.highlights[i] * k;
    g.fade[i] = f.fade[i];
  }
  g.shadows[3] = 0.4f;  // contrast pivot
  g.fade[3] = f.fade_amount * k;
  // Nothing to do at all: leave the pass off.
  const bool identity = s.filter == 0 && s.exposure == 1.0f && s.contrast == 1.0f &&
                        s.saturation == 1.0f && s.brightness == 1.0f && s.warmth == 0.0f &&
                        s.tint == 0.0f;
  g.enabled = identity ? 0u : 1u;
  return g;
}

// --- Screenshots ------------------------------------------------------------------------
inline std::filesystem::path exe_dir() {
#ifdef _WIN32
  wchar_t buf[MAX_PATH];
  const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  if (n > 0 && n < MAX_PATH) return std::filesystem::path(buf).parent_path();
#endif
  return std::filesystem::current_path();
}

inline void take_screenshot() {
  auto provider = presenter_provider();
  rex::ui::Presenter* presenter = provider ? provider() : nullptr;
  std::filesystem::path dir = config::Get().photo_mode_screenshot_folder;
  if (dir.empty()) dir = "screenshots";
  if (dir.is_relative()) dir = exe_dir() / dir;
  const std::time_t now = std::time(nullptr);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &now);
#else
  localtime_r(&now, &tm);
#endif
  char name[64];
  std::strftime(name, sizeof(name), "Fable2_%Y%m%d_%H%M%S", &tm);
  std::filesystem::path file = dir / (std::string(name) + ".png");
  for (int i = 2; std::filesystem::exists(file) && i < 100; ++i)
    file = dir / (std::string(name) + "_" + std::to_string(i) + ".png");
  // The capture waits for a GPU readback, so keep it off the game thread.
  std::thread([presenter, file]() {
    const auto r = framecapture::CaptureGuestOutputToPng(presenter, file.string());
    if (r.ok) {
      set_status("Saved " + file.filename().string() + " (" + std::to_string(r.width) + "x" +
                 std::to_string(r.height) + ")");
      log_line("screenshot saved: " + file.string() + " " + std::to_string(r.width) + "x" +
               std::to_string(r.height) + (r.looked_black ? " (looks black)" : ""));
    } else {
      set_status("Screenshot failed: " + r.error);
      log_line("screenshot failed: " + r.error);
    }
  }).detach();
}

// --- Input ------------------------------------------------------------------------------
inline bool key_down(int vk) {
#ifdef _WIN32
  return (GetAsyncKeyState(vk) & 0x8000) != 0;
#else
  (void)vk;
  return false;
#endif
}

inline Pad merged_pad() {
  State& g = st();
  std::lock_guard<std::mutex> lock(g.pad_mutex);
  Pad m{};
  auto bigger = [](int16_t a, int16_t b) { return std::abs(int(b)) > std::abs(int(a)) ? b : a; };
  for (const Pad* pp : {&g.pad[0], &g.pad[1], &g.pad[2], &g.pad[3], &g.host_pad}) {
    const Pad& p = *pp;
    m.buttons |= p.buttons;
    m.lt = std::max(m.lt, p.lt);
    m.rt = std::max(m.rt, p.rt);
    m.lx = bigger(m.lx, p.lx);
    m.ly = bigger(m.ly, p.ly);
    m.rx = bigger(m.rx, p.rx);
    m.ry = bigger(m.ry, p.ry);
  }
  return m;
}

inline float stick(int16_t v) {
  constexpr float kDead = 7849.0f;
  const float f = static_cast<float>(v);
  if (std::fabs(f) < kDead) return 0.0f;
  const float n = (std::fabs(f) - kDead) / (32767.0f - kDead);
  return std::copysign(std::min(n, 1.0f), f);
}

// Move / look the free camera from keyboard, mouse and pad. Writes roll and FOV
// changes back to the shared settings so the panel follows them.
inline void integrate(float dt) {
  State& g = st();
  const auto& cfg = config::Get();
  const bool keys = window_focused().load(std::memory_order_relaxed) &&
                    !ui_wants_keyboard().load(std::memory_order_relaxed);
  Pad pad = merged_pad();
  if (pad_rebinding().load()) {
    // Recording a new controller shortcut: the pad drives nothing else.
    pad = Pad{};
  }
  pad.buttons &= ~kPadGuide;  // Guide is only ever the shortcut
  const Pad prev = g.pad_prev_frame;
  g.pad_prev_frame = pad;
  auto pressed = [&](uint16_t mask) { return (pad.buttons & mask) && !(prev.buttons & mask); };

  // Pad: A select / take photo (the menu decides), B or Start exit, X reset
  // camera, Y or Back show / hide the menu, R3 level the horizon. The D-pad
  // moves through the menu and LB / RB change page; the sticks and triggers
  // fly the camera.
  if (pressed(0x1000)) nav_activate().fetch_add(1);
  if (pressed(0x2000) || pressed(0x0010)) request_exit();
  if (pressed(0x4000)) reset_camera_request().store(true);
  if (pressed(0x8000) || pressed(0x0020)) panel_visible().store(!panel_visible().load());
  if (pressed(0x0100)) nav_tab().fetch_sub(1);
  if (pressed(0x0200)) nav_tab().fetch_add(1);
  const bool level = pressed(0x0080) && !(pad.buttons & 0x0040);
  const int64_t now = now_ms();
  nav_y().fetch_add(g.rep_down.Fire(pad.buttons & 0x0002, now) - g.rep_up.Fire(pad.buttons & 0x0001, now));
  nav_x().fetch_add(g.rep_right.Fire(pad.buttons & 0x0008, now) - g.rep_left.Fire(pad.buttons & 0x0004, now));
  if (pad.buttons != 0 || pad.lt > 30 || pad.rt > 30 || stick(pad.lx) != 0.0f || stick(pad.ly) != 0.0f ||
      stick(pad.rx) != 0.0f || stick(pad.ry) != 0.0f)
    prompts_pad().store(true, std::memory_order_relaxed);

  float speed = g.s.move_speed;
  if (keys && key_down(0x10)) speed *= 4.0f;   // Shift
  if (keys && key_down(0x11)) speed *= 0.25f;  // Ctrl
  if (pad.buttons & 0x0040) speed *= 4.0f;     // L3 held

  float fwd = 0, side = 0, up = 0, yaw_rate = 0, pitch_rate = 0, roll_rate = 0, zoom = 0;
  if (keys) {
    fwd += key_down('W') ? 1.0f : 0.0f;
    fwd -= key_down('S') ? 1.0f : 0.0f;
    side += key_down('D') ? 1.0f : 0.0f;
    side -= key_down('A') ? 1.0f : 0.0f;
    up += key_down('E') ? 1.0f : 0.0f;
    up -= key_down('Q') ? 1.0f : 0.0f;
    roll_rate -= key_down('Z') ? 1.0f : 0.0f;
    roll_rate += key_down('C') ? 1.0f : 0.0f;
    zoom -= key_down(0x21) ? 1.0f : 0.0f;  // PageUp: narrower FOV
    zoom += key_down(0x22) ? 1.0f : 0.0f;  // PageDown: wider FOV
  }
  fwd += stick(pad.ly);
  side += stick(pad.lx);
  up += (pad.rt - pad.lt) / 255.0f;
  yaw_rate -= stick(pad.rx);
  pitch_rate += stick(pad.ry);

  const float look = g.s.look_speed;
  const int32_t mdx = look_dx().exchange(0, std::memory_order_relaxed);
  const int32_t mdy = look_dy().exchange(0, std::memory_order_relaxed);
  const float msens = cfg.photo_mode_mouse_sensitivity * look;
  // Slower look when zoomed in, so aiming a long lens stays precise.
  const float zoom_scale = std::clamp(g.s.fov_deg / 60.0f, 0.15f, 1.5f);
  g.yaw += (yaw_rate * 1.6f * look * dt - mdx * msens) * zoom_scale;
  g.pitch += (pitch_rate * 1.2f * look * dt - mdy * msens) * zoom_scale;
  g.pitch = std::clamp(g.pitch, -1.55f, 1.55f);
  if (g.yaw > kPi) g.yaw -= 2 * kPi;
  if (g.yaw < -kPi) g.yaw += 2 * kPi;

  const float wheel = wheel_acc().exchange(0.0f, std::memory_order_relaxed);
  float fov = g.s.fov_deg - wheel * 3.0f + zoom * 30.0f * dt;
  fov = std::clamp(fov, 5.0f, 120.0f);
  float roll = level ? 0.0f : g.s.roll_deg + roll_rate * 45.0f * dt;
  if (roll > 180.0f) roll -= 360.0f;
  if (roll < -180.0f) roll += 360.0f;
  if (fov != g.s.fov_deg || roll != g.s.roll_deg) {
    std::lock_guard<std::mutex> lock(settings_mutex());
    settings_unlocked().fov_deg = g.s.fov_deg = fov;
    settings_unlocked().roll_deg = g.s.roll_deg = roll;
  }

  using namespace mouselook;
  const Vec3 f = forward_of(g.yaw, g.pitch);
  const Vec3 r = vec3_normalize(Vec3{f.y, -f.x, 0.0f});
  const float step = speed * dt;
  g.pos.x += (f.x * fwd + r.x * side) * step;
  g.pos.y += (f.y * fwd + r.y * side) * step;
  g.pos.z += (f.z * fwd + r.z * side + up) * step;

  const float max_d = cfg.photo_mode_max_distance;
  if (max_d > 0.0f) {
    Vec3 d{g.pos.x - g.start_pos.x, g.pos.y - g.start_pos.y, g.pos.z - g.start_pos.z};
    const float len = vec3_len(d);
    if (len > max_d) {
      const float k = max_d / len;
      g.pos = Vec3{g.start_pos.x + d.x * k, g.start_pos.y + d.y * k, g.start_pos.z + d.z * k};
    }
  }
}

// --- Enter / leave ------------------------------------------------------------------------
inline void enter(PPCContext& ctx, uint8_t* base) {
  State& g = st();
  g.phase = Phase::kArming;
  g.arm_calls = 0;
  g.cam_count = 0;
  g.tick_valid = false;
  g.suppress_pad = false;
  g.pad_prev_frame = merged_pad();  // a held button must not fire on entry
  look_dx().store(0);
  look_dy().store(0);
  wheel_acc().store(0.0f);
  panel_visible().store(true);
  screenshot_request().store(false);
  reset_camera_request().store(false);

  // The effects start from "as the game has it" every time.
  {
    std::lock_guard<std::mutex> lock(settings_mutex());
    Settings& s = settings_unlocked();
    const Settings defaults;
    s.dof_mode = defaults.dof_mode;
    s.roll_deg = 0.0f;
    s.exposure = s.contrast = s.saturation = s.brightness = 1.0f;
    s.bloom = s.bloom_threshold = s.sun_beams = s.overlay = 1.0f;
    s.fog = s.ground_mist = 1.0f;
    s.rain = s.snow = s.stars = s.moon_glare = s.sun_disc = 1.0f;
    s.override_time = false;
    s.filter = 0;
    s.filter_strength = 1.0f;
    s.warmth = s.tint = 0.0f;
    if (s.move_speed <= 0.0f) s.move_speed = config::Get().photo_mode_move_speed;
  }

  g.hud_saved = true;
  g.saved_draw_gui = ld8(base, kDrawGuiFlag);
  const uint32_t icons = icons_state(base);
  g.saved_icons = icons ? ld8(base, icons + 68) : 1;
  g.applied_hud = -1;
  g.applied_icons = -1;

  g.tod_saved = false;
  g.applied_tod = -1.0f;
  if (const uint32_t timing = timing_ptr(base)) {
    g.saved_tod = ldf(base, timing + 4);
    g.tod_saved = true;
    std::lock_guard<std::mutex> lock(settings_mutex());
    settings_unlocked().time_hours = std::clamp(g.saved_tod, 0.0f, 1.0f) * 24.0f;
  }
  for (auto& t : g.theme) t.valid = false;
  g.theme_out = 0;

  active_flag().store(true, std::memory_order_release);
  set_status("");
  log_line("entered (draw_gui=" + std::to_string(g.saved_draw_gui) +
           " icons=" + std::to_string(g.saved_icons) + " tod=" + std::to_string(g.saved_tod) + ")");
}

inline void leave(PPCContext& ctx, uint8_t* base) {
  State& g = st();
  pad_rebinding().store(false);
  if (g.applied_hud >= 0) {
    g.applied_hud = -1;
    ctx.r3.u64 = g.saved_draw_gui;
    call_guest(ctx, base, kSetDrawGUI);
  }
  if (g.applied_icons >= 0) {
    g.applied_icons = -1;
    if (icons_state(base)) {
      ctx.r3.u64 = g.saved_icons;
      call_guest(ctx, base, kSetShouldDrawWorldIcons);
    }
  }
  bool rebuilt = false;
  if (g.tod_saved && g.applied_tod >= 0.0f) {
    if (set_time_of_day(ctx, base, g.saved_tod)) rebuilt = rebuild_theme(ctx, base);
  }
  if (!rebuilt) restore_theme(base);
  for (auto& t : g.theme) t.valid = false;
  for (int i = 0; i < g.cam_count; ++i) g.cams[i].restore_pending = true;
  g.restore_updates_left = 30;
  rex_set_swap_color_grade(nullptr);  // filters off
  g.phase = Phase::kOff;
  g.suppress_pad = true;  // Esc / B may still be held: keep it from the game
  active_flag().store(false, std::memory_order_release);
  set_status("");
  log_line("left");
}

}  // namespace detail

// --- Per-camera hook (guest thread) -------------------------------------------------------
inline void on_camera_update(PPCContext& ctx, uint8_t* base) {
  using namespace detail;
  State& g = st();
  const uint32_t cam = ctx.r3.u32;
  if (!ptr_ok(cam)) return;

  if (g.phase == Phase::kOff) {
    if (g.cam_count > 0) {
      if (CameraSnapshot* s = find_snapshot(cam)) {
        if (s->restore_pending) restore_snapshot(base, *s);
      }
      bool any = false;
      for (int i = 0; i < g.cam_count; ++i) any |= g.cams[i].restore_pending;
      if (!any) g.cam_count = 0;
    }
    return;
  }

  ++g.diag_cam_calls;
  const mouselook::Vec3 eye{ldf(base, cam + 144), ldf(base, cam + 148), ldf(base, cam + 152)};
  const bool has_eye = eye.x != 0.0f || eye.y != 0.0f || eye.z != 0.0f;

  if (g.phase == Phase::kArming) {
    // Take the starting pose from the camera the game renders with (the one
    // updated with the render flag in r4), or any live camera if none shows up.
    ++g.arm_calls;
    const bool render_cam = (ctx.r4.u32 & 0xFF) != 0;
    if (!has_eye || !(render_cam || g.arm_calls > 24)) return;
    const mouselook::Quat q{ldf(base, cam + 176), ldf(base, cam + 180), ldf(base, cam + 184),
                            ldf(base, cam + 188)};
    const mouselook::Vec3 f = mouselook::quat_rotate_vec(q, mouselook::Vec3{0.0f, 1.0f, 0.0f});
    g.pitch = std::asin(std::clamp(f.z, -1.0f, 1.0f));
    g.yaw = std::atan2(-f.x, f.y);
    g.pos = eye;
    g.start_pos = eye;
    g.start_yaw = g.yaw;
    g.start_pitch = g.pitch;
    const float fov = ldf(base, cam + 24) / kDeg;
    g.start_fov = (fov > 1.0f && fov < 170.0f) ? fov : 60.0f;
    {
      std::lock_guard<std::mutex> lock(settings_mutex());
      Settings& s = settings_unlocked();
      s.fov_deg = g.start_fov;
      for (int i = 0; i < 6; ++i) s.dof_raw[i] = ldf(base, cam + 32 + 4 * i);
      s.dof_alpha = ldf(base, cam + 80);
      s.vignette_a = ldf(base, cam + 196);
      s.vignette_b = ldf(base, cam + 204);
      s.blur = ldf(base, cam + 212);
      g.s = s;
    }
    g.phase = Phase::kOn;
    char buf[200];
    std::snprintf(buf, sizeof(buf),
                  "camera 0x%08X eye=(%.2f, %.2f, %.2f) yaw=%.1f pitch=%.1f fov=%.1f "
                  "render=%d after %d updates",
                  cam, eye.x, eye.y, eye.z, g.yaw / kDeg, g.pitch / kDeg, g.start_fov,
                  render_cam ? 1 : 0, g.arm_calls);
    log_line(buf);
  }

  if (!has_eye) return;  // not a live view camera
  if (!take_snapshot(base, cam)) return;

  ++g.diag_cam_writes;
  const Settings& s = g.s;
  const mouselook::Quat q = orientation(g.yaw, g.pitch, s.roll_deg * kDeg);
  stf(base, cam + 144, g.pos.x);
  stf(base, cam + 148, g.pos.y);
  stf(base, cam + 152, g.pos.z);
  for (uint32_t off : {160u, 176u}) {
    stf(base, cam + off + 0, q.x);
    stf(base, cam + off + 4, q.y);
    stf(base, cam + off + 8, q.z);
    stf(base, cam + off + 12, q.w);
  }
  stf(base, cam + 96, 0.0f);   // roll is in the quaternion
  stf(base, cam + 100, 0.0f);
  stf(base, cam + 192, 0.0f);  // no follow integration
  st32(base, cam + 220, 0);    // no easing: apply this frame
  stf(base, cam + 228, 1.0f);
  const float fov = s.fov_deg * kDeg;
  stf(base, cam + 24, fov);
  stf(base, cam + 28, fov);

  float dof[6];
  bool write_dof = true;
  switch (s.dof_mode) {
    case DofMode::kGame:
      write_dof = false;
      break;
    case DofMode::kOff:
      for (int i = 0; i < 6; ++i) dof[i] = ldf(base, cam + 56 + 4 * i);
      dof[0] = dof[1] = 0.0f;
      break;
    case DofMode::kFocus: {
      const float near_focus = std::max(0.05f, s.dof_focus - s.dof_range * 0.5f);
      const float far_focus = s.dof_focus + s.dof_range * 0.5f;
      dof[0] = dof[1] = std::clamp(s.dof_strength, 0.0f, 1.0f);
      dof[2] = std::max(0.01f, near_focus - s.dof_falloff);
      dof[3] = near_focus;
      dof[4] = far_focus;
      dof[5] = far_focus + s.dof_falloff;
      break;
    }
    case DofMode::kRaw:
      for (int i = 0; i < 6; ++i) dof[i] = s.dof_raw[i];
      break;
  }
  if (write_dof) {
    for (int i = 0; i < 6; ++i) {
      stf(base, cam + 32 + 4 * i, dof[i]);
      stf(base, cam + 56 + 4 * i, dof[i]);
    }
    if (s.dof_mode != DofMode::kOff) {
      const float alpha = s.dof_mode == DofMode::kFocus ? 1.0f : s.dof_alpha;
      stf(base, cam + 80, alpha);
      stf(base, cam + 84, alpha);
    }
  }
  stf(base, cam + 196, s.vignette_a);
  stf(base, cam + 200, s.vignette_a);
  stf(base, cam + 204, s.vignette_b);
  stf(base, cam + 208, s.vignette_b);
  stf(base, cam + 212, s.blur);
  stf(base, cam + 216, s.blur);
}

// --- World update (guest thread). Returns after running (or skipping) the
// original. ---------------------------------------------------------------------------------
inline void on_world_update(PPCContext& ctx, uint8_t* base) {
  using namespace detail;
  State& g = st();
  const auto& cfg = config::Get();
  const uint32_t self = ctx.r3.u32;

  const bool want = cfg.photo_mode_enabled && want_active().load();
  if (g.phase == Phase::kOff && !want) {
    if (g.restore_updates_left > 0 && --g.restore_updates_left == 0) g.cam_count = 0;
    __imp__sub_821D43D8(ctx, base);
    return;
  }
  const PPCContext saved = ctx;

  if (g.phase == Phase::kOff) {
    if (now_ms() - toggle_time_ms().load() > 1000) {
      want_active().store(false);  // asked for somewhere it could not start
    } else if (want) {
      if (world_is_top(ctx, base, self)) {
        enter(ctx, base);
      } else {
        want_active().store(false);
        log_line("not available here (the world is not running, e.g. a menu is open)");
      }
    }
    ctx = saved;
    if (g.phase == Phase::kOff) {
      __imp__sub_821D43D8(ctx, base);
      return;
    }
  } else if (!want) {
    leave(ctx, base);
    ctx = saved;
    __imp__sub_821D43D8(ctx, base);
    return;
  }

  // Photo mode is on.
  g.s = settings_copy();
  const auto now = std::chrono::steady_clock::now();
  float dt = g.tick_valid ? std::chrono::duration<float>(now - g.last_tick).count() : 0.0f;
  g.last_tick = now;
  g.tick_valid = true;
  dt = std::clamp(dt, 0.0f, 0.1f);

  if (g.phase == Phase::kOn) {
    if (reset_camera_request().exchange(false)) {
      g.pos = g.start_pos;
      g.yaw = g.start_yaw;
      g.pitch = g.start_pitch;
      std::lock_guard<std::mutex> lock(settings_mutex());
      settings_unlocked().roll_deg = g.s.roll_deg = 0.0f;
      settings_unlocked().fov_deg = g.s.fov_deg = g.start_fov;
    }
    integrate(dt);
  }

  const bool top = world_is_top(ctx, base, self);
  set_hud(ctx, base, g.s.hide_hud);
  set_icons(ctx, base, g.s.hide_world_icons);

  bool tod_changed = false;
  if (g.tod_saved) {
    const float want_tod =
        g.s.override_time ? std::clamp(g.s.time_hours / 24.0f, 0.0f, 0.9999f) : -1.0f;
    if (want_tod >= 0.0f && want_tod != g.applied_tod) {
      tod_changed = set_time_of_day(ctx, base, want_tod);
      g.applied_tod = want_tod;
    } else if (want_tod < 0.0f && g.applied_tod >= 0.0f) {
      tod_changed = set_time_of_day(ctx, base, g.saved_tod);
      g.applied_tod = -1.0f;
    }
  }

  ++g.diag_world;
  if (g.s.freeze_world && top) {
    ++g.diag_frozen;
    update_cameras(ctx, base);
    if (tod_changed) rebuild_theme(ctx, base);
    ctx = saved;
  } else {
    ctx = saved;
    __imp__sub_821D43D8(ctx, base);
  }
  apply_theme(base);
  {
    const rex::ui::SwapColorGrade grade = build_grade(g.s);
    rex_set_swap_color_grade(&grade);
  }

  if (now_ms() - g.diag_last_ms >= 1000) {
    g.diag_last_ms = now_ms();
    const Pad p = merged_pad();
    char buf[420];
    std::snprintf(buf, sizeof(buf),
                  "tick phase=%d top=%d world=%u frozen=%u cam_calls=%u cam_writes=%u "
                  "pad_reads=%u pad=%04X lx=%d ly=%d rx=%d ry=%d lt=%u rt=%u focus=%d ui_kb=%d "
                  "pos=(%.2f, %.2f, %.2f) yaw=%.1f pitch=%.1f filter=%s/%.0f%% grade=%d",
                  int(g.phase), top ? 1 : 0, g.diag_world, g.diag_frozen, g.diag_cam_calls,
                  g.diag_cam_writes, diag_pad_reads().exchange(0), p.buttons, p.lx, p.ly, p.rx,
                  p.ry, p.lt, p.rt, window_focused().load() ? 1 : 0,
                  ui_wants_keyboard().load() ? 1 : 0, g.pos.x, g.pos.y, g.pos.z, g.yaw / kDeg,
                  g.pitch / kDeg, kFilterPresets[std::clamp(g.s.filter, 0, kFilterPresetCount - 1)].name,
                  g.s.filter_strength * 100.0f, rex_get_swap_color_grade(nullptr) ? 1 : 0);
    log_line(buf);
    g.diag_world = g.diag_frozen = g.diag_cam_calls = g.diag_cam_writes = 0;
  }

  if (screenshot_request().exchange(false)) {
    // A plain shot goes through the same schedule as timed and burst shots.
    ShotPlan p;
    {
      std::lock_guard<std::mutex> lock(shot_plan_mutex());
      p = shot_plan_unlocked();
    }
    shots_left().store(p.shots);
    shot_next_ms().store(now_ms() + 1000LL * p.delay_s);
  }
  const int64_t due = shot_next_ms().load();
  if (due != 0 && now_ms() >= due && shots_left().load() > 0) {
    take_screenshot();
    if (shots_left().fetch_sub(1) > 1) {
      ShotPlan p;
      {
        std::lock_guard<std::mutex> lock(shot_plan_mutex());
        p = shot_plan_unlocked();
      }
      shot_next_ms().store(now_ms() + p.spacing_ms);
    } else {
      shot_next_ms().store(0);
    }
  }
}

// --- Pad read (guest thread): r3 = user, r4 = X_INPUT_STATE, returns r3 = 0 ok. ---------
inline void on_pad_read(uint8_t* base, uint32_t user, uint32_t state_ptr, uint32_t result) {
  using namespace detail;
  State& g = st();
  diag_pad_reads().fetch_add(1, std::memory_order_relaxed);
  if (result != 0 || !ptr_ok(state_ptr)) {
    std::lock_guard<std::mutex> lock(g.pad_mutex);
    g.pad[user & 3] = Pad{};  // disconnected: no stale stick drift
    return;
  }
  Pad p;
  p.buttons = REX_LOAD_U16(state_ptr + 4);
  p.lt = REX_LOAD_U8(state_ptr + 6);
  p.rt = REX_LOAD_U8(state_ptr + 7);
  p.lx = static_cast<int16_t>(REX_LOAD_U16(state_ptr + 8));
  p.ly = static_cast<int16_t>(REX_LOAD_U16(state_ptr + 10));
  p.rx = static_cast<int16_t>(REX_LOAD_U16(state_ptr + 12));
  p.ry = static_cast<int16_t>(REX_LOAD_U16(state_ptr + 14));
  const uint32_t u = user & 3;
  {
    std::lock_guard<std::mutex> lock(g.pad_mutex);
    g.pad[u] = p;
  }

  const auto& cfg = config::Get();
  // Controllers read on the host (XInput, with Guide), merged as user 0.
  uint16_t host_buttons = 0;
  if (u == 0) {
    const hostpad::State hs = hostpad::Poll();
    const bool focused = window_focused().load(std::memory_order_relaxed);
    {
      std::lock_guard<std::mutex> lock(g.pad_mutex);
      g.host_pad = (hs.connected && focused) ? hs.pad : Pad{};
    }
    if (hs.connected && focused) host_buttons = hs.pad.buttons;
    static bool logged = false;
    if (hs.connected && !logged) {
      logged = true;
      char buf[128];
      std::snprintf(buf, sizeof(buf), "host controller found (XInput%s); shortcut %s",
                    hostpad::has_guide() ? ", Guide readable" : ", no Guide",
                    PadComboName(pad_toggle_mask().load()).c_str());
      log_line(buf);
    }
  }
  const uint16_t all = p.buttons | host_buttons;
  if (all != diag_pad_buttons().load() && (all & 0x04C0)) {
    // Guide / L3 / R3 changes, rate-limited: shows what the shortcut sees.
    static int64_t last = 0;
    if (now_ms() - last > 250) {
      last = now_ms();
      char buf[96];
      std::snprintf(buf, sizeof(buf), "pad user %u buttons %04X (game %04X, host %04X)", u, all, p.buttons,
                    host_buttons);
      log_line(buf);
    }
  }
  diag_pad_buttons().store(all);

  // Recording a new shortcut from the menu (user 0 carries the host pad).
  if (u == 0 && pad_rebinding().load()) {
    if (g.rebind_session != pad_rebind_started_ms().load()) {
      g.rebind_session = pad_rebind_started_ms().load();
      g.rebind_armed = false;
      g.rebind_peak = 0;
    }
    if (!g.rebind_armed) {
      g.rebind_armed = all == 0;  // wait for the A press that started it to end
      g.rebind_peak = 0;
    } else if (all != 0) {
      g.rebind_peak |= all;
    } else if (g.rebind_peak != 0) {
      set_pad_toggle(g.rebind_peak);
      set_status("Shortcut set to " + PadComboName(g.rebind_peak));
      log_line(("shortcut recorded: " + PadComboName(g.rebind_peak)).c_str());
      g.rebind_peak = 0;
      g.rebind_armed = false;
      pad_rebinding().store(false);
    }
    if (pad_rebinding().load() && now_ms() - pad_rebind_started_ms().load() > 10000) {
      set_status("Shortcut unchanged");
      g.rebind_peak = 0;
      g.rebind_armed = false;
      pad_rebinding().store(false);
    }
    g.toggle_prev[u] = all;
  } else {
    const uint16_t mask = pad_toggle_mask().load();
    if (cfg.photo_mode_enabled && mask != 0) {
      const bool now = (all & mask) == mask;
      const bool before = (g.toggle_prev[u] & mask) == mask;
      if (now && !before) request_toggle();
    }
    g.toggle_prev[u] = all;
  }

  if (g.suppress_pad && !active()) {
    const bool neutral = p.buttons == 0 && p.lt < 30 && p.rt < 30 && std::abs(int(p.lx)) < 7849 &&
                         std::abs(int(p.ly)) < 7849 && std::abs(int(p.rx)) < 8689 &&
                         std::abs(int(p.ry)) < 8689;
    if (neutral) g.suppress_pad = false;
  }
  if (active() || g.suppress_pad) {
    REX_STORE_U16(state_ptr + 4, 0);
    REX_STORE_U8(state_ptr + 6, 0);
    REX_STORE_U8(state_ptr + 7, 0);
    REX_STORE_U16(state_ptr + 8, 0);
    REX_STORE_U16(state_ptr + 10, 0);
    REX_STORE_U16(state_ptr + 12, 0);
    REX_STORE_U16(state_ptr + 14, 0);
  }
}

}  // namespace fable2::photomode

// --- Strong overrides of the recompiled functions --------------------------------------------
extern "C" void sub_821D43D8(PPCContext& ctx, uint8_t* base) {
  fable2::photomode::on_world_update(ctx, base);
}

extern "C" void CECCameraUpdateFrame_822A47C0(PPCContext& ctx, uint8_t* base) {
  fable2::photomode::on_camera_update(ctx, base);
  __imp__CECCameraUpdateFrame_822A47C0(ctx, base);
}

// The remote-control build already overrides the pad read (state probe); it
// calls fable2::photomode::on_pad_read from there.
#ifndef FABLE2_REMOTE_CONTROL
extern "C" void ProcessAndProcessAndProcess1013_822B2D60(PPCContext& ctx, uint8_t* base) {
  const uint32_t user = ctx.r3.u32;
  const uint32_t state_ptr = ctx.r4.u32;
  __imp__ProcessAndProcessAndProcess1013_822B2D60(ctx, base);
  fable2::photomode::on_pad_read(base, user, state_ptr, ctx.r3.u32);
}
#endif
