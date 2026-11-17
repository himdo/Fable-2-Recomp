// fable_2 - What the Advanced Options page shows: one entry per
// fable2_config.toml key, grouped into pages.
//
// To add a setting, add one entry to Settings() below (in the page it belongs
// to). The page reads the value from fable2_config.toml itself and writes it
// back there (src/core/fable2_config_edit.h), so a key only needs:
//   - its [section] and key, the label and a one-sentence description;
//   - the control: Toggle (bool), Choice (a list of values) or Slider (int or
//     float range);
//   - its default, used when the key is missing from the file (keep it equal
//     to the template in src/core/fable2_config.cpp);
//   - when a change takes effect: Live(fn) applies it at once (fn sets the
//     cvar or the fable2::config::Values field the game reads),
//     NextLoad() when the game reads it as a save loads, Restart() when
//     only a restart picks it up. The page marks Restart keys and lists the
//     ones changed since the game started.
// Example (a frame rate key in [perf]):
//   s.push_back(Choice(kFrameRate, "perf", "frame_rate", "Frame Rate",
//                      "The highest frame rate the game runs at.",
//                      Int(30), {{Int(30), "30 FPS"}, {Int(60), "60 FPS"}, {Int(120), "120 FPS"}})
//                   .Restart());
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include <rex/cvar.h>

#include "fable2_config.h"
#include "photo_mode_state.h"

namespace fable2::portsettings {

// --- Values ----------------------------------------------------------------------------------

enum class Type { kBool, kInt, kFloat, kString };

struct Value {
  Type type = Type::kBool;
  bool b = false;
  int64_t i = 0;
  double d = 0.0;
  std::string s;

  double Number() const { return type == Type::kInt ? double(i) : d; }
  bool operator==(const Value& o) const {
    if (type != o.type) return false;
    switch (type) {
      case Type::kBool: return b == o.b;
      case Type::kInt: return i == o.i;
      case Type::kFloat: return std::fabs(d - o.d) < 1e-9;
      case Type::kString: return s == o.s;
    }
    return false;
  }
  bool operator!=(const Value& o) const { return !(*this == o); }
};
inline Value Bool(bool v) { Value x; x.type = Type::kBool; x.b = v; return x; }
inline Value Int(int64_t v) { Value x; x.type = Type::kInt; x.i = v; return x; }
inline Value Float(double v) { Value x; x.type = Type::kFloat; x.d = v; return x; }
inline Value Str(std::string v) { Value x; x.type = Type::kString; x.s = std::move(v); return x; }

// The value as cvar text ("true", "256", "0.06", "direct").
inline std::string CvarText(const Value& v) {
  switch (v.type) {
    case Type::kBool: return v.b ? "true" : "false";
    case Type::kInt: return std::to_string(v.i);
    case Type::kFloat: {
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%.6g", v.d);
      return buf;
    }
    case Type::kString: return v.s;
  }
  return {};
}

// --- Settings --------------------------------------------------------------------------------

enum Page { kDisplay, kFrameRate, kMouse, kFixes, kPhotoMode, kPageCount };
inline const char* PageName(int p) {
  static const char* kNames[kPageCount] = {"Display", "Frame Rate", "Mouse", "Fixes", "Photo Mode"};
  return kNames[p];
}

enum class Control { kToggle, kChoice, kSlider };
enum class When { kLive, kNextLoad, kRestart };

struct ChoiceItem {
  Value value;
  const char* label;
};

struct Setting {
  int page = kDisplay;
  const char* section = "";
  const char* key = "";
  const char* label = "";
  const char* desc = "";
  Control control = Control::kToggle;
  Value def;
  // Toggle: show "On" for false (a key that turns something off, shown as the
  // thing itself: disable_motion_blur as "Motion Blur").
  bool inverted = false;
  std::vector<ChoiceItem> choices;
  double lo = 0, hi = 1, step = 0.1;
  std::function<std::string(double)> format;  // slider value text
  When when = When::kRestart;
  std::function<void(const Value&)> live;  // When::kLive
  // Cvars the live change goes to. When one is pinned by fable_2.toml, an
  // environment variable or the command line, that source keeps winning: the
  // page says so instead of claiming the change applied.
  std::vector<const char*> cvars;

  Setting& Restart() { when = When::kRestart; live = nullptr; return *this; }
  Setting& NextLoad(std::function<void(const Value&)> fn) {
    when = When::kNextLoad;
    live = std::move(fn);
    return *this;
  }
  Setting& Live(std::function<void(const Value&)> fn) {
    when = When::kLive;
    live = std::move(fn);
    return *this;
  }
  // Live change through cvars (each set to the value's text).
  Setting& LiveCvars(std::vector<const char*> names, std::function<void(const Value&)> also = nullptr) {
    cvars = names;
    return Live([names, also](const Value& v) {
      const std::string text = CvarText(v);
      for (const char* n : names) rex::cvar::SetFlagByName(n, text);
      if (also) also(v);
    });
  }
};

// A cvar set by fable_2.toml, an environment variable or the command line
// wins over fable2_config.toml (Fable2App only seeds cvars still at their
// default), so a change made here would not show until that source changes.
inline bool CvarPinned(const char* name) {
  const auto src = rex::cvar::GetFlagSource(name);
  return src == rex::cvar::Source::kConfig || src == rex::cvar::Source::kEnvironment ||
         src == rex::cvar::Source::kCommandLine;
}

inline Setting Toggle(int page, const char* section, const char* key, const char* label, const char* desc,
                      bool def, bool inverted = false) {
  Setting s;
  s.page = page;
  s.section = section;
  s.key = key;
  s.label = label;
  s.desc = desc;
  s.control = Control::kToggle;
  s.def = Bool(def);
  s.inverted = inverted;
  return s;
}
inline Setting Choice(int page, const char* section, const char* key, const char* label, const char* desc,
                      Value def, std::vector<ChoiceItem> choices) {
  Setting s;
  s.page = page;
  s.section = section;
  s.key = key;
  s.label = label;
  s.desc = desc;
  s.control = Control::kChoice;
  s.def = std::move(def);
  s.choices = std::move(choices);
  return s;
}
inline Setting Slider(int page, const char* section, const char* key, const char* label, const char* desc,
                      Value def, double lo, double hi, double step, std::function<std::string(double)> format) {
  Setting s;
  s.page = page;
  s.section = section;
  s.key = key;
  s.label = label;
  s.desc = desc;
  s.control = Control::kSlider;
  s.def = std::move(def);
  s.lo = lo;
  s.hi = hi;
  s.step = step;
  s.format = std::move(format);
  return s;
}

inline std::function<std::string(double)> Printf(const char* fmt, double scale = 1.0) {
  return [fmt, scale](double v) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), fmt, v * scale);
    return std::string(buf);
  };
}

// Live setters for fable2::config::Values fields (bool / number fields only:
// other threads read them while the game runs).
template <typename T>
inline std::function<void(const Value&)> Field(T config::Values::*m) {
  return [m](const Value& v) {
    auto& cfg = config::MutableForLiveEdit();
    if constexpr (std::is_same_v<T, bool>) cfg.*m = v.b;
    else cfg.*m = static_cast<T>(v.Number());
  };
}

inline const std::vector<Setting>& Settings() {
  static const std::vector<Setting> list = [] {
    std::vector<Setting> s;
    using config::Values;

    // Display ---------------------------------------------------------------------------
    s.push_back(Toggle(kDisplay, "patches", "disable_motion_blur", "Motion Blur",
                       "The blur the camera adds to the whole screen when it moves quickly.", false, true)
                    .Live(Field(&Values::disable_motion_blur)));
    s.push_back(Toggle(kDisplay, "patches", "faster_render_targets", "Faster Render Targets",
                       "Skip copying screen data the game is about to clear. Mostly helps at higher internal "
                       "resolutions, and removes the purple bar at the bottom of the screen. D3D12 only.",
                       true)
                    .LiveCvars({"execute_unclipped_draw_vs_on_cpu", "skip_depth_clear_transfers"},
                               Field(&Values::faster_render_targets)));
    s.push_back(Toggle(kDisplay, "patches", "realtime_texture_morphing", "Real-Time Skin Textures",
                       "Build your hero's and dog's changing skin textures on the graphics card, the way the "
                       "game can. Fixes a black hero and dog.",
                       true)
                    .Restart());
    s.push_back(Toggle(kDisplay, "patches", "hero_dog_texture_readback", "Skin Texture Readback",
                       "The older fix for a black hero and dog. Only used when Real-Time Skin Textures is off.",
                       false)
                    .Restart());
    s.push_back(Toggle(kDisplay, "patches", "dog_fur_depth_bias", "Dog Fur Fix",
                       "Stops the dog's fur flickering in blocky patches.", true)
                    .Restart());

    // Frame rate ------------------------------------------------------------------------
    s.push_back(Toggle(kFrameRate, "patches", "high_tick_rate", "High Tick Rate",
                       "Run the game's logic at 30 Hz instead of 15 Hz: smoother menus and less input delay. "
                       "Turn on Higher Physics Rate with it.",
                       false)
                    .Restart());
    s.push_back(Toggle(kFrameRate, "patches", "higher_hf_tick_rate", "Higher Physics Rate",
                       "Run physics at 60 Hz to match High Tick Rate; cloth misbehaves without it. Needs High "
                       "Tick Rate.",
                       false)
                    .Restart());
    s.push_back(Toggle(kFrameRate, "patches", "gpu_frame_sync", "GPU Frame Sync",
                       "Wait for the graphics card to finish each frame before starting the next. Fixes "
                       "flickering when it falls behind, for a few frames per second. D3D12 only.",
                       true)
                    .LiveCvars({"gpu_sync_at_frame_start"}, Field(&Values::gpu_frame_sync)));
    s.push_back(Toggle(kFrameRate, "patches", "terrain_tess_cpu", "Terrain Detail on CPU",
                       "Work out the terrain's detail on the processor, so the game does not wait for the "
                       "graphics card every frame. D3D12 only.",
                       true)
                    .Restart());
    s.push_back(Toggle(kFrameRate, "patches", "skinning_cpu", "Character Skinning on CPU",
                       "Move characters' skin on the processor, removing another wait for the graphics card "
                       "every frame. D3D12 only.",
                       true)
                    .Restart());
    s.push_back(Toggle(kFrameRate, "perf", "refresh_gpu_memory_each_frame", "Refresh GPU Memory",
                       "Copy all the memory each frame uses to the graphics card again. A safety net that costs "
                       "speed; leave it off unless something is missing on screen.",
                       false)
                    .LiveCvars({"clear_memory_page_state"}, Field(&Values::refresh_gpu_memory_each_frame)));
    s.push_back(Choice(kFrameRate, "perf", "hotfunc_yield_every", "Thread Yielding",
                       "How often busy game threads give the processor to others. Less often is faster on "
                       "processors with many cores.",
                       Int(1),
                       {{Int(1), "Always"}, {Int(2), "Every 2nd"}, {Int(4), "Every 4th"}, {Int(8), "Every 8th"},
                        {Int(16), "Every 16th"}, {Int(0), "Never"}})
                    .Live(Field(&Values::hotfunc_yield_every)));

    // Mouse -----------------------------------------------------------------------------
    s.push_back(Toggle(kMouse, "input", "mouse_look", "Mouse Look", "Turn the camera with the mouse.", true)
                    .LiveCvars({"mouse_look"}, Field(&Values::mouse_look)));
    s.push_back(Choice(kMouse, "input", "mouse_look_mode", "Mouse Look Style",
                       "Direct: the camera turns as far as the mouse moves, with no top speed. Stick: the mouse "
                       "acts like the right stick of a controller.",
                       Str("direct"), {{Str("direct"), "Direct"}, {Str("stick"), "Stick"}})
                    .LiveCvars({"mouse_look_mode"}));
    s.push_back(Slider(kMouse, "input", "mouse_look_scale", "Mouse Sensitivity",
                       "How far the camera turns for the same mouse movement, in both styles.", Int(256), 16,
                       1024, 16, Printf("%.2fx", 1.0 / 256.0))
                    .LiveCvars({"mouse_look_scale"}, Field(&Values::mouse_look_scale)));
    s.push_back(Slider(kMouse, "input", "mouse_look_sens_x", "Horizontal Speed",
                       "Direct style: degrees the camera turns sideways per count of mouse movement.",
                       Float(0.06), 0.005, 0.3, 0.005, Printf("%.3f"))
                    .LiveCvars({"mouse_look_sens_x"}));
    s.push_back(Slider(kMouse, "input", "mouse_look_sens_y", "Vertical Speed",
                       "Direct style: degrees the camera turns up and down per count of mouse movement.",
                       Float(0.06), 0.005, 0.3, 0.005, Printf("%.3f"))
                    .LiveCvars({"mouse_look_sens_y"}));
    s.push_back(Toggle(kMouse, "input", "mouse_look_invert_y", "Invert Vertical Look",
                       "Direct style: moving the mouse up makes the camera look down.", false)
                    .LiveCvars({"mouse_look_invert_y"}));
    s.push_back(Slider(kMouse, "input", "mouse_look_direct_smoothing", "Direct Smoothing",
                       "Direct style: spread each movement over the next camera updates. Smoother, but the "
                       "camera keeps turning briefly after the mouse stops.",
                       Float(0.0), 0.0, 0.9, 0.05, [](double v) {
                         return v < 0.005 ? std::string("Off") : Printf("%.0f%%", 100.0)(v);
                       })
                    .LiveCvars({"mouse_look_direct_smoothing"}));
    s.push_back(Toggle(kMouse, "input", "mouse_look_smoothing", "Even Stick Look",
                       "Stick style: turn the camera evenly at slow speeds instead of not at all.", true)
                    .LiveCvars({"mouse_look_smoothing"}, Field(&Values::mouse_look_smoothing)));

    // Fixes -----------------------------------------------------------------------------
    s.push_back(Toggle(kFixes, "patches", "unlock_website", "Website Items",
                       "Give the items that came with registering on the Fable II website. They arrive when a "
                       "save is loaded.",
                       true)
                    .NextLoad(Field(&Values::unlock_website)));
    s.push_back(Toggle(kFixes, "patches", "unlock_ce", "Collector's Edition Items",
                       "Give the Collector's Edition content. It arrives when a save is loaded.", true)
                    .NextLoad(Field(&Values::unlock_ce)));
    s.push_back(Toggle(kFixes, "patches", "skip_intro_videos", "Skip Intro Videos",
                       "Leave out the Microsoft and Lionhead logo videos when the game starts.", false)
                    .Restart());

    // Photo mode ------------------------------------------------------------------------
    s.push_back(Toggle(kPhotoMode, "photo_mode", "enabled", "Photo Mode",
                       "Freeze the world, fly a free camera and take screenshots.", true)
                    .Live(Field(&Values::photo_mode_enabled)));
    s.push_back(Choice(kPhotoMode, "photo_mode", "gamepad_toggle", "Controller Shortcut",
                       "The controller buttons that open and close photo mode.", Str("Guide"),
                       {{Str("Guide"), "Guide"}, {Str("L3 + R3"), "L3 + R3"}, {Str("Back + Start"), "Back + Start"},
                        {Str("LB + RB"), "LB + RB"}, {Str("Off"), "Off"}})
                    .Live([](const Value& v) {
                      uint16_t mask = 0;
                      if (photomode::ParsePadCombo(v.s, mask)) photomode::pad_toggle_mask().store(mask);
                    }));
    s.push_back(Choice(kPhotoMode, "photo_mode", "toggle_key", "Keyboard Shortcut",
                       "The key that opens and closes photo mode.", Str("F6"),
                       {{Str("F6"), "F6"}, {Str("F7"), "F7"}, {Str("F8"), "F8"}, {Str("F9"), "F9"},
                        {Str("F10"), "F10"}, {Str("P"), "P"}})
                    .Restart());
    s.push_back(Choice(kPhotoMode, "photo_mode", "screenshot_key", "Screenshot Key",
                       "The key that takes a photo in photo mode.", Str("F12"),
                       {{Str("F12"), "F12"}, {Str("F11"), "F11"}, {Str("PrintScreen"), "Print Screen"},
                        {Str("Space"), "Space"}})
                    .Restart());
    s.push_back(Slider(kPhotoMode, "photo_mode", "move_speed", "Camera Speed",
                       "How fast the free camera flies when photo mode opens.", Float(4.0), 0.5, 20.0, 0.5,
                       Printf("%.1f m/s"))
                    .Live(Field(&Values::photo_mode_move_speed)));
    return s;
  }();
  return list;
}

}  // namespace fable2::portsettings
