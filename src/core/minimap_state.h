// fable_2 - Live minimap: state shared by the game thread (src/core/fable2_minimap.h),
// the HUD's GameFace Lua (src/core/fable2_minimap_hud_lua.h) and the overlay that
// draws it on the UI thread (src/core/minimap_dialog.h).
//
// No dependencies on the recompiled code, so any of them can include it.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>

namespace fable2::minimap {

inline int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// What the overlay needs for one frame, written by the game thread after each
// world update (under snapshot_mutex()).
struct Snapshot {
  // The region's map, from its MinimapInfo record (what the pause menu shows).
  uint32_t level = 0;          // level object the record was read for
  bool region_ok = false;      // record read and the region has a proper map
  // MinimapBackgroundImage as the game's resource id: FNV-1 (32-bit, 0x811C9DC5,
  // x16777619 then xor) of the lower-case path; the overlay matches it against
  // the bank's entry names.
  uint32_t texture_id = 0;
  float north_deg = 0.0f;      // MinimapDegreesRotationToNorth (the image's rotation)
  float icon_rot_deg = 0.0f;   // IconTransformRotation
  float icon_x = 0.0f, icon_y = 0.0f;  // IconTransformX / IconTransformZ (canvas x, y)
  float icon_scale = 1.0f;     // IconTransformScale
  float extent = 0.0f;         // larger side of the level's world rectangle
  // The hero and the dog (world units, Z up; heading in radians from atan2).
  bool hero_ok = false;
  float hero_x = 0.0f, hero_y = 0.0f, hero_heading = 0.0f;
  bool dog_ok = false;
  float dog_x = 0.0f, dog_y = 0.0f;
  bool world_on_top = false;   // the world runs (no menu, loading screen ...)
  bool gui_drawn = true;       // the game draws its GUI (photo mode hides it)
  int64_t time_ms = 0;         // when this was taken
};

inline std::mutex& snapshot_mutex() {
  static std::mutex m;
  return m;
}
inline Snapshot& snapshot() {
  static Snapshot s;
  return s;
}

// The minimap is wanted: [minimap] enabled and shown (the Back button flips
// shown). Read by the game thread every frame, so the work stops while off.
inline std::atomic<bool>& feature_enabled() {
  static std::atomic<bool> v{true};
  return v;
}
inline std::atomic<bool>& shown() {
  static std::atomic<bool> v{true};
  return v;
}
// [minimap] size as a fraction of the screen height.
inline std::atomic<float>& size_fraction() {
  static std::atomic<float> v{0.30f};
  return v;
}
inline float SizeFraction(const std::string& size) {
  if (size == "small") return 0.24f;
  if (size == "large") return 0.54f;
  return 0.30f;
}
// [minimap] opacity, 0.1 to 1.
inline std::atomic<float>& opacity() {
  static std::atomic<float> v{1.0f};
  return v;
}

// Called (game thread) when the overlay should appear: it registers itself with
// the drawer on the UI thread. Overlays stay unregistered while hidden.
inline std::atomic<void (*)()>& show_hook() {
  static std::atomic<void (*)()> v{nullptr};
  return v;
}

// --- The HUD's top-box offset ------------------------------------------------
// When a message shows in the bar across the top of the screen, the game moves
// the health bar down (PlayerHealth.HealthAndMana: y 9.24 at rest, lower by the
// message box's height). The HUD presentation reports that y every GUI update
// by calling setAttribute(el, "__f2mm_y", y) and
// setAttribute(el, "__f2mm_o", layer opacity); the native setAttribute hook in
// src/core/fable2_gui_lua.h catches those names (see capture_set_attribute) and
// they never reach GameFace.
// HealthAndMana's y with no message (Steam Deck, 2026-11-26).
inline constexpr float kHealthRestY = 9.238f;
// Screen pixels per HealthAndMana unit, as a fraction of the screen height:
// the heart moved 105 px at 800 px for 74.2 units.
inline constexpr float kHudDropPerUnit = 105.0f / 74.24f / 800.0f;
struct HudReport {
  std::atomic<float> y{kHealthRestY};  // health bar y (canvas units)
  std::atomic<float> y_rest{kHealthRestY};  // its resting y: the highest seen (canvas y is up)
  std::atomic<float> opacity{100.0f};  // Scene.Layer opacity (HUD Disable sets 0)
  std::atomic<int64_t> time_ms{0};     // last report
  std::atomic<uint32_t> state{0};      // GUI lua_State that reported (a new HUD resets y_rest)
};
inline HudReport& hud() {
  static HudReport h;
  return h;
}

// The HUD reported recently: it is on screen and updating (a menu or loading
// screen stops its updates).
inline bool HudLive(int64_t now, int64_t max_age_ms = 250) {
  return now - hud().time_ms.load(std::memory_order_relaxed) < max_age_ms &&
         hud().opacity.load(std::memory_order_relaxed) > 1.0f;
}

// How far the health bar is pushed down, in canvas units (0 = resting).
inline float HudDrop() {
  const float y = hud().y.load(std::memory_order_relaxed);
  const float rest = hud().y_rest.load(std::memory_order_relaxed);
  const float d = rest - y;
  return d > 0.0f ? d : 0.0f;
}

// Native setAttribute (GameFace Lua C function, r3 = lua_State). Returns true
// (and the call must return 0 results without running the original) when the
// call is one of the minimap's reports. Lua 5.1 with float numbers: TValue =
// {value, tt} 8 bytes, L->base at +0xC, L->top at +8, TString text at +16,
// length at +12. `load32` reads a big-endian guest word, `readable` checks a
// guest range and `host` turns a guest address into a host pointer.
template <typename Load32, typename Readable, typename Host>
inline bool capture_set_attribute(uint32_t L, Load32 load32, Readable readable, Host host) {
  if (!L || !readable(L, 0x20)) return false;
  const uint32_t base = load32(L + 0xC);
  const uint32_t top = load32(L + 0x8);
  if (top < base + 24 || top - base > 64 * 8 || !readable(base, 24)) return false;
  if (load32(base + 8 + 4) != 4) return false;   // name is a string
  const uint32_t ts = load32(base + 8);
  if (!readable(ts, 24)) return false;
  if (load32(ts + 12) != 8) return false;        // "__f2mm_?"
  const char* s = reinterpret_cast<const char*>(host(ts + 16));
  if (std::memcmp(s, "__f2mm_", 7) != 0) return false;
  if (load32(base + 16 + 4) != 3) return true;   // not a number: swallow it anyway
  const uint32_t bits = load32(base + 16);
  float v;
  std::memcpy(&v, &bits, 4);
  HudReport& h = hud();
  const int64_t t = now_ms();
  if (s[7] == 'y') {
    h.state.store(L, std::memory_order_relaxed);
    h.y.store(v, std::memory_order_relaxed);
    // A slightly higher resting y than measured is taken; far-off values
    // (screen transitions) are not.
    if (v > h.y_rest.load(std::memory_order_relaxed) && v < kHealthRestY + 20.0f)
      h.y_rest.store(v, std::memory_order_relaxed);
    h.time_ms.store(t, std::memory_order_relaxed);
  } else if (s[7] == 'o') {
    h.opacity.store(v, std::memory_order_relaxed);
  }
  return true;
}

}  // namespace fable2::minimap
