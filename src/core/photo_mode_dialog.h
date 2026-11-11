// fable_2 - Photo mode menu (drawn on the UI thread over the game).
//
// Drawn like one of the game's own menus rather than as a desktop window: the
// game's GUI typeface (src/core/photo_mode_font.h), cream text over a soft
// dark fade on the left, gold highlight on the selected line, pages switched
// with LB / RB, values changed with left / right, and button prompts along the
// bottom. Everything is drawn straight into ImGui's foreground draw list, so
// there are no ImGui windows or widgets.
//
// Input: the D-pad / arrow keys arrive as counters from the guest thread and
// the input poll (photo_mode_state.h nav_*); the mouse is read from ImGuiIO
// (hover selects, click changes, drag on a bar sets it, wheel adjusts).
// The menu only edits fable2::photomode::Settings (under its mutex) and raises
// one-shot requests; the guest thread applies them to the game. The screenshot
// reads the game's own frame, so nothing drawn here is ever in a photo.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <imgui.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>

#include "fable2_config.h"
#include "photo_mode_font.h"
#include "photo_mode_icons.h"
#include "photo_mode_state.h"

#ifdef _WIN32
#include <shellapi.h>
#include <windows.h>
#endif

namespace fable2::photomode {

namespace menu {

// Colours (straight alpha).
constexpr ImU32 kCream = IM_COL32(240, 230, 204, 255);
constexpr ImU32 kCreamDim = IM_COL32(196, 184, 156, 255);
constexpr ImU32 kFaint = IM_COL32(160, 150, 128, 200);
constexpr ImU32 kGold = IM_COL32(218, 182, 98, 255);
constexpr ImU32 kShadow = IM_COL32(0, 0, 0, 200);

inline ImU32 with_alpha(ImU32 c, float a) {
  const int base = int((c >> IM_COL32_A_SHIFT) & 0xFF);
  const int na = std::clamp(int(base * a), 0, 255);
  return (c & ~IM_COL32_A_MASK) | (ImU32(na) << IM_COL32_A_SHIFT);
}

enum class Kind { kToggle, kSlider, kOption, kAction };

struct Box {
  ImVec2 Min, Max;
  bool Contains(ImVec2 p) const { return p.x >= Min.x && p.y >= Min.y && p.x < Max.x && p.y < Max.y; }
  float GetHeight() const { return Max.y - Min.y; }
};

struct Item {
  const char* label;
  const char* desc;
  Kind kind;
  // Slider / toggle / option value (toggle: 0 or 1; option: index).
  std::function<float(const Settings&)> get;
  std::function<void(Settings&, float)> set;
  float lo = 0, hi = 1, step = 0.05f;
  bool log = false;  // step multiplies (1 + step) instead of adding
  std::function<std::string(float)> fmt;
  std::vector<std::string> options;
  std::function<void(Settings&)> action;
  std::function<bool(const Settings&)> visible;
  int special = 0;  // 1 = undo (handled by the menu)
};

struct Page {
  const char* name;
  std::vector<Item> items;
};

inline std::string Fmt(const char* f, float v) {
  char b[48];
  std::snprintf(b, sizeof(b), f, v);
  return b;
}

// --- Item builders -----------------------------------------------------------------
inline Item Toggle(const char* label, const char* desc, bool Settings::*m) {
  Item it{label, desc, Kind::kToggle};
  it.get = [m](const Settings& s) { return (s.*m) ? 1.0f : 0.0f; };
  it.set = [m](Settings& s, float v) { s.*m = v > 0.5f; };
  it.lo = 0;
  it.hi = 1;
  return it;
}
inline Item Slider(const char* label, const char* desc, float Settings::*m, float lo, float hi,
                   float step, const char* fmt, bool log = false) {
  Item it{label, desc, Kind::kSlider};
  it.get = [m](const Settings& s) { return s.*m; };
  it.set = [m](Settings& s, float v) { s.*m = v; };
  it.lo = lo;
  it.hi = hi;
  it.step = step;
  it.log = log;
  std::string f = fmt;
  it.fmt = [f](float v) { return Fmt(f.c_str(), v); };
  return it;
}
inline Item Multiplier(const char* label, const char* desc, float Settings::*m, float hi) {
  return Slider(label, desc, m, 0.0f, hi, 0.05f, "%.2fx");
}
inline Item Option(const char* label, const char* desc, std::vector<std::string> names,
                   std::function<float(const Settings&)> get,
                   std::function<void(Settings&, float)> set) {
  Item it{label, desc, Kind::kOption};
  it.options = std::move(names);
  it.get = std::move(get);
  it.set = std::move(set);
  it.lo = 0;
  it.hi = float(it.options.size() - 1);
  return it;
}
inline Item Action(const char* label, const char* desc, std::function<void(Settings&)> fn) {
  Item it{label, desc, Kind::kAction};
  it.action = std::move(fn);
  return it;
}

inline std::filesystem::path PhotosFolder() {
  std::filesystem::path dir = config::Get().photo_mode_screenshot_folder;
  if (dir.empty()) dir = "screenshots";
  if (dir.is_relative()) {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) return std::filesystem::path(buf).parent_path() / dir;
#endif
  }
  return dir;
}

// Values kept outside Settings (the shot plan) go through these.
inline float ShotGet(int ShotPlan::*m) {
  std::lock_guard<std::mutex> lock(shot_plan_mutex());
  return float(shot_plan_unlocked().*m);
}
inline void ShotSet(int ShotPlan::*m, float v) {
  std::lock_guard<std::mutex> lock(shot_plan_mutex());
  shot_plan_unlocked().*m = int(std::lround(v));
}

inline std::vector<Page> BuildPages() {
  std::vector<Page> pages;

  // Camera ---------------------------------------------------------------------------
  {
    Page p{"Camera"};
    p.items.push_back(Toggle("Pause the World", "Freeze people, creatures, water and time while you frame the shot.",
                             &Settings::freeze_world));
    p.items.push_back(Toggle("Hide the HUD", "Hide the health bar, minimap and other on-screen information.",
                             &Settings::hide_hud));
    p.items.push_back(Toggle("Hide World Markers", "Hide the glowing quest and interaction markers in the world.",
                             &Settings::hide_world_icons));
    p.items.push_back(Slider("Field of View", "Zoom the lens. Lower values bring distant things closer.",
                             &Settings::fov_deg, 5.0f, 120.0f, 1.0f, "%.0f\xC2\xB0"));
    p.items.push_back(Slider("Roll", "Tilt the camera to one side.", &Settings::roll_deg, -180.0f, 180.0f,
                             1.0f, "%.0f\xC2\xB0"));
    p.items.push_back(Slider("Camera Speed", "How fast the camera flies. Hold the left stick in (or Shift) to go faster.",
                             &Settings::move_speed, 0.25f, 40.0f, 0.1f, "%.1f m/s", true));
    p.items.push_back(Slider("Look Speed", "How fast the camera turns.", &Settings::look_speed, 0.2f, 3.0f, 0.1f,
                             "%.1fx"));
    p.items.push_back(Action("Reset Camera", "Put the camera back where photo mode started.",
                             [](Settings&) { reset_camera_request().store(true); }));
    p.items.push_back(Action("Level the Horizon", "Remove any roll.", [](Settings& s) { s.roll_deg = 0.0f; }));
    pages.push_back(std::move(p));
  }

  // Lens -----------------------------------------------------------------------------
  {
    Page p{"Lens"};
    auto focus = [](const Settings& s) { return s.dof_mode == DofMode::kFocus; };
    p.items.push_back(Option(
        "Depth of Field", "Blur what is nearer or further than the point you focus on.",
        {"As in the Game", "Off", "Focus"}, [](const Settings& s) { return float(int(s.dof_mode)); },
        [](Settings& s, float v) { s.dof_mode = static_cast<DofMode>(int(std::lround(v))); }));
    Item it = Slider("Focus Distance", "How far from the camera things are sharpest.", &Settings::dof_focus,
                     0.2f, 200.0f, 0.08f, "%.1f m", true);
    it.visible = focus;
    p.items.push_back(it);
    it = Slider("Sharp Depth", "How deep the sharp zone around the focus point is.", &Settings::dof_range, 0.05f,
                50.0f, 0.1f, "%.2f m", true);
    it.visible = focus;
    p.items.push_back(it);
    it = Slider("Blur Falloff", "How gradually things blur outside the sharp zone.", &Settings::dof_falloff, 0.1f,
                100.0f, 0.1f, "%.1f m", true);
    it.visible = focus;
    p.items.push_back(it);
    it = Slider("Blur Strength", "How strong the blur is.", &Settings::dof_strength, 0.0f, 1.0f, 0.05f, "%.2f");
    it.visible = focus;
    p.items.push_back(it);
    p.items.push_back(Slider("Vignette", "Darken the corners of the picture.", &Settings::vignette_a, 0.0f, 1.0f,
                             0.05f, "%.2f"));
    p.items.push_back(Slider("Edge Shadow", "A second, softer darkening around the edges.", &Settings::vignette_b,
                             0.0f, 1.0f, 0.05f, "%.2f"));
    p.items.push_back(Slider("Soft Lens", "Blur the whole picture.", &Settings::blur, 0.0f, 1.0f, 0.05f, "%.2f"));
    pages.push_back(std::move(p));
  }

  // Colour ---------------------------------------------------------------------------
  {
    Page p{"Colour"};
    std::vector<std::string> names;
    for (int i = 0; i < kFilterPresetCount; ++i) names.emplace_back(kFilterPresets[i].name);
    p.items.push_back(Option(
        "Filter", "A colour look for the whole picture. Fine-tune it with the settings below.", names,
        [](const Settings& s) { return float(s.filter); },
        [](Settings& s, float v) {
          s.filter = std::clamp(int(std::lround(v)), 0, kFilterPresetCount - 1);
          apply_filter(s, kFilterPresets[s.filter]);
        }));
    Item strength = Slider("Filter Strength", "How strongly the filter is applied.", &Settings::filter_strength,
                           0.0f, 1.0f, 0.05f, "");
    strength.fmt = [](float v) { return Fmt("%.0f%%", v * 100.0f); };
    strength.visible = [](const Settings& s) { return s.filter != 0; };
    p.items.push_back(strength);
    p.items.push_back(Slider("Exposure", "Brighten or darken the whole picture.", &Settings::exposure, 0.25f, 3.0f,
                             0.05f, "%.2fx"));
    p.items.push_back(Slider("Contrast", "The difference between light and dark.", &Settings::contrast, 0.4f, 2.5f,
                             0.05f, "%.2fx"));
    p.items.push_back(Slider("Saturation", "How strong the colours are. 0 is black and white.",
                             &Settings::saturation, 0.0f, 2.5f, 0.05f, "%.2fx"));
    p.items.push_back(Slider("Brightness", "Lift or lower the mid tones, keeping black and white.",
                             &Settings::brightness, 0.4f, 2.5f, 0.05f, "%.2fx"));
    Item warm = Slider("Warmth", "Cooler blue or warmer orange light.", &Settings::warmth, -1.0f, 1.0f, 0.05f, "");
    warm.fmt = [](float v) { return std::fabs(v) < 0.005f ? std::string("Neutral") : Fmt("%+.2f", v); };
    p.items.push_back(warm);
    Item tint = Slider("Tint", "Shift towards green or magenta.", &Settings::tint, -1.0f, 1.0f, 0.05f, "");
    tint.fmt = [](float v) { return std::fabs(v) < 0.005f ? std::string("Neutral") : Fmt("%+.2f", v); };
    p.items.push_back(tint);
    p.items.push_back(Action("Reset Colour", "No filter, and every colour setting back to the game's own.",
                             [](Settings& s) {
                               s.filter = 0;
                               s.filter_strength = 1.0f;
                               s.exposure = s.contrast = s.saturation = s.brightness = 1.0f;
                               s.warmth = s.tint = 0.0f;
                             }));
    Item undo = Action("Undo Last Change", "Put back the setting you changed last.", [](Settings&) {});
    undo.special = 1;
    p.items.push_back(undo);
    pages.push_back(std::move(p));
  }

  // Light: the game's own lighting effects ------------------------------------------------
  {
    Page p{"Light"};
    p.items.push_back(Multiplier("Bloom", "The glow around bright light.", &Settings::bloom, 5.0f));
    p.items.push_back(Multiplier("Bloom Threshold", "How bright something must be to glow.",
                                 &Settings::bloom_threshold, 3.0f));
    p.items.push_back(Multiplier("Sun Beams", "Shafts of light from the sun.", &Settings::sun_beams, 5.0f));
    p.items.push_back(Multiplier("Film Texture", "The game's own film overlay.", &Settings::overlay, 4.0f));
    p.items.push_back(Multiplier("Fog", "Haze in the distance.", &Settings::fog, 4.0f));
    p.items.push_back(Multiplier("Ground Mist", "Mist lying low on the ground.", &Settings::ground_mist, 4.0f));
    p.items.push_back(Action("Reset Light", "Back to the game's own lighting.", [](Settings& s) {
      s.bloom = s.bloom_threshold = s.sun_beams = s.overlay = s.fog = s.ground_mist = 1.0f;
    }));
    pages.push_back(std::move(p));
  }

  // World ----------------------------------------------------------------------------
  {
    Page p{"World"};
    p.items.push_back(Toggle("Set the Time of Day", "Choose the hour yourself instead of the game's clock.",
                             &Settings::override_time));
    Item t = Slider("Time of Day", "Dawn, midday, dusk or the middle of the night.", &Settings::time_hours, 0.0f,
                    23.75f, 0.25f, "");
    t.fmt = [](float v) {
      const int mins = int(std::lround(v * 60.0f)) % (24 * 60);
      char b[16];
      std::snprintf(b, sizeof(b), "%02d:%02d", mins / 60, mins % 60);
      return std::string(b);
    };
    t.visible = [](const Settings& s) { return s.override_time; };
    p.items.push_back(t);
    const char* mult = "1.00x is how the game has it.";
    p.items.push_back(Multiplier("Rain", mult, &Settings::rain, 3.0f));
    p.items.push_back(Multiplier("Snow", mult, &Settings::snow, 3.0f));
    p.items.push_back(Multiplier("Stars", "How bright the stars are at night.", &Settings::stars, 3.0f));
    p.items.push_back(Multiplier("Moon Glow", "The glow around the moon.", &Settings::moon_glare, 3.0f));
    p.items.push_back(Multiplier("Sun", "How bright the sun's disc is.", &Settings::sun_disc, 3.0f));
    pages.push_back(std::move(p));
  }

  // Capture --------------------------------------------------------------------------
  {
    Page p{"Capture"};
    p.items.push_back(Option(
        "Framing Guide", "Lines to help you compose. Never in the photo.",
        {"None", "Rule of Thirds", "Centre", "Thirds and Centre"},
        [](const Settings& s) { return float(s.guides); },
        [](Settings& s, float v) { s.guides = int(std::lround(v)); }));
    p.items.push_back(Option(
        "Cinema Bars", "Widescreen bars to frame the shot. Never in the photo.",
        {"None", "1.85 : 1", "2.35 : 1", "2.39 : 1"}, [](const Settings& s) { return float(s.letterbox); },
        [](Settings& s, float v) { s.letterbox = int(std::lround(v)); }));
    Item timer{"Self-Timer", "Wait before taking the photo.", Kind::kSlider};
    timer.get = [](const Settings&) { return ShotGet(&ShotPlan::delay_s); };
    timer.set = [](Settings&, float v) { ShotSet(&ShotPlan::delay_s, v); };
    timer.lo = 0;
    timer.hi = 30;
    timer.step = 1;
    timer.fmt = [](float v) { return v < 0.5f ? std::string("Off") : Fmt("%.0f s", v); };
    p.items.push_back(timer);
    Item burst{"Burst", "How many photos to take in a row.", Kind::kSlider};
    burst.get = [](const Settings&) { return ShotGet(&ShotPlan::shots); };
    burst.set = [](Settings&, float v) { ShotSet(&ShotPlan::shots, v); };
    burst.lo = 1;
    burst.hi = 10;
    burst.step = 1;
    burst.fmt = [](float v) { return v < 1.5f ? std::string("Single") : Fmt("%.0f photos", v); };
    p.items.push_back(burst);
    Item gap{"Burst Interval", "The time between photos in a burst.", Kind::kSlider};
    gap.get = [](const Settings&) { return ShotGet(&ShotPlan::spacing_ms); };
    gap.set = [](Settings&, float v) { ShotSet(&ShotPlan::spacing_ms, v); };
    gap.lo = 100;
    gap.hi = 2000;
    gap.step = 50;
    gap.fmt = [](float v) { return Fmt("%.2f s", v / 1000.0f); };
    p.items.push_back(gap);
    p.items.push_back(Action("Take Photo", "Saved as a PNG in the screenshots folder next to the game.",
                             [](Settings&) { screenshot_request().store(true); }));
    {
      // Controller shortcut that opens / closes photo mode (saved to
      // fable2_config.toml, [photo_mode] gamepad_toggle).
      static const uint16_t kPresets[] = {kPadGuide, 0x0040 | 0x0080, 0x0010 | 0x0020, 0x0100 | 0x0200, 0};
      Item sc = Option(
          "Controller Shortcut", "The controller buttons that open and close photo mode.",
          {"Guide", "L3 + R3", "Back + Start", "LB + RB", "Off"},
          [](const Settings&) {
            const uint16_t m = pad_toggle_mask().load();
            for (int i = 0; i < 5; ++i)
              if (kPresets[i] == m) return float(i);
            return -1.0f;  // recorded combination: the next step goes to Guide
          },
          [](Settings&, float v) { set_pad_toggle(kPresets[std::clamp(int(std::lround(v)), 0, 4)]); });
      sc.fmt = [](float) { return PadComboName(pad_toggle_mask().load()); };
      p.items.push_back(sc);
      p.items.push_back(Action("Record Shortcut",
                               "Hold the controller buttons you want, then let go of them.", [](Settings&) {
                                 pad_rebind_started_ms().store(now_ms());
                                 pad_rebinding().store(true);
                               }));
    }
    p.items.push_back(Action("Open Photo Folder", "Show your photos in Explorer.", [](Settings&) {
#ifdef _WIN32
      const std::filesystem::path dir = PhotosFolder();
      std::error_code ec;
      std::filesystem::create_directories(dir, ec);
      ShellExecuteW(nullptr, L"open", dir.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
    }));
    pages.push_back(std::move(p));
  }
  return pages;
}

}  // namespace menu

class PhotoModeDialog final : public rex::ui::ImGuiDialog {
 public:
  explicit PhotoModeDialog(rex::ui::ImGuiDrawer* drawer)
      : rex::ui::ImGuiDialog(drawer), pages_(menu::BuildPages()) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    const bool on = active();
    if (on && !was_on_) {
      // Fresh each time: first page, first line, nothing queued.
      page_ = 0;
      sel_ = 0;
      nav_y().store(0);
      nav_x().store(0);
      nav_tab().store(0);
      nav_tab_direct().store(0);
      nav_activate().store(0);
      opened_ms_ = now_ms();
    }
    was_on_ = on;
    if (!on) {
      ui_wants_mouse().store(false, std::memory_order_relaxed);
      ui_wants_keyboard().store(false, std::memory_order_relaxed);
      return;
    }
    k_ = std::max(0.5f, io.DisplaySize.y / 720.0f);
    dl_ = ImGui::GetForegroundDrawList();
    const bool shown = panel_visible().load(std::memory_order_relaxed);

    Settings s = settings_copy();
    const Settings before = s;

    DrawGuides(io, s);
    if (shown) {
      HandleInput(io, s);
      DrawMenu(io, s);
    } else {
      // Menu hidden: A / Enter still takes the photo.
      if (nav_activate().exchange(0) > 0) screenshot_request().store(true);
      nav_y().store(0);
      nav_x().store(0);
      nav_tab().store(0);
      nav_tab_direct().store(0);
      mouse_in_panel_ = false;
    }
    DrawPrompts(io, shown);
    DrawStatus(io);

    {
      std::lock_guard<std::mutex> lock(settings_mutex());
      // Only copy what the menu changed, so a value the guest moved in the
      // meantime (roll / FOV keys) is not overwritten with a stale copy.
      MergeChanged(before, s, settings_unlocked());
    }
    ui_wants_mouse().store(shown && mouse_in_panel_, std::memory_order_relaxed);
    ui_wants_keyboard().store(false, std::memory_order_relaxed);

    // Mouse wheel over the game (not over the menu) zooms.
    if (!(shown && mouse_in_panel_) && io.MouseWheel != 0.0f) {
      float w = wheel_acc().load(std::memory_order_relaxed);
      while (!wheel_acc().compare_exchange_weak(w, w + io.MouseWheel)) {
      }
    }
  }

 private:
  std::vector<menu::Page> pages_;
  int page_ = 0;
  int sel_ = 0;  // index into the page's visible items
  bool was_on_ = false;
  int64_t opened_ms_ = 0;
  float k_ = 1.0f;  // pixels per 720p unit
  ImDrawList* dl_ = nullptr;
  bool mouse_in_panel_ = false;
  ImVec2 last_mouse_{-1e9f, -1e9f};
  int drag_row_ = -1;  // slider being dragged with the mouse
  // Undo: settings before the current run of changes to one line.
  Settings undo_{};
  bool has_undo_ = false;
  int last_changed_ = -1;
  int last_changed_page_ = -1;
  // Status toast.
  std::string toast_;
  int64_t toast_ms_ = 0;

  // Layout, in 720p units.
  // Text sizes are the game's own three menu fonts at their native line
  // heights on its 720p canvas (gui_large / gui_normal / gui_small), scaled
  // with the window height exactly like the game's text.
  static constexpr float kLarge = 32.0f;
  static constexpr float kNormal = 27.0f;
  static constexpr float kSmall = 24.0f;
  static constexpr float kLeft = 64.0f;     // text column
  static constexpr float kRight = 500.0f;   // value column right edge
  static constexpr float kTitleY = 40.0f;
  static constexpr float kTabsY = 94.0f;
  static constexpr float kListY = 140.0f;
  static constexpr float kRow = 34.0f;
  static constexpr float kText = kNormal;   // menu lines
  static constexpr float kBar = 110.0f;     // slider bar width

  float X(float v) const { return v * k_; }

  std::vector<int> Visible(const Settings& s) const {
    std::vector<int> v;
    const auto& items = pages_[page_].items;
    for (int i = 0; i < int(items.size()); ++i)
      if (!items[i].visible || items[i].visible(s)) v.push_back(i);
    return v;
  }

  // --- Text ------------------------------------------------------------------------
  // `size` is one of kLarge / kNormal / kSmall; `scale` shrinks or grows it
  // for the few places that need it (button letters, the countdown).
  float TextW(const char* t, float size, float scale = 1.0f) const {
    if (gamefont::AnyReady()) return gamefont::Measure(t, X(size) * scale, size);
    return float(std::strlen(t)) * 7.0f;  // default ImGui font fallback
  }
  void Text(ImVec2 p, float size, ImU32 col, const char* t, bool shadow = true, float scale = 1.0f) {
    if (gamefont::AnyReady()) {
      const float sz = X(size) * scale;
      if (shadow) {
        const float o = std::max(1.0f, sz * 0.06f);
        gamefont::Draw(dl_, ImVec2(p.x + o, p.y + o), sz, size,
                       menu::with_alpha(menu::kShadow, ((col >> 24) & 0xFF) / 255.0f), t);
      }
      gamefont::Draw(dl_, p, sz, size, col, t);
    } else {
      if (shadow) dl_->AddText(ImVec2(p.x + 1, p.y + 1), menu::kShadow, t, nullptr);
      dl_->AddText(p, col, t, nullptr);
    }
  }
  void TextRight(float right_x, float y, float size, ImU32 col, const char* t) {
    Text(ImVec2(right_x - TextW(t, size), y), size, col, t);
  }
  // Word-wrapped text; returns the height used.
  float TextWrapped(ImVec2 p, float size, float width, ImU32 col, const char* t) {
    std::string line, word;
    float y = p.y;
    auto flush = [&]() {
      if (!line.empty()) Text(ImVec2(p.x, y), size, col, line.c_str());
      y += X(size) * 1.05f;
      line.clear();
    };
    const std::string all = t;
    size_t i = 0;
    while (i <= all.size()) {
      const size_t j = all.find(' ', i);
      word = all.substr(i, j == std::string::npos ? std::string::npos : j - i);
      const std::string trial = line.empty() ? word : line + " " + word;
      if (!line.empty() && TextW(trial.c_str(), size) > width) {
        flush();
        line = word;
      } else {
        line = trial;
      }
      if (j == std::string::npos) break;
      i = j + 1;
    }
    flush();
    return y - p.y;
  }

  // --- Ornaments ---------------------------------------------------------------------
  void Diamond(ImVec2 c, float r, ImU32 col) {
    dl_->AddQuadFilled(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y),
                       col);
  }
  // A thin gold rule that fades out at both ends, with a diamond in the middle.
  void Rule(float x0, float x1, float y, float alpha = 1.0f) {
    const float mid = (x0 + x1) * 0.5f;
    const float h = std::max(1.0f, X(1.2f));
    const ImU32 g = menu::with_alpha(menu::kGold, 0.85f * alpha);
    const ImU32 z = menu::with_alpha(menu::kGold, 0.0f);
    dl_->AddRectFilledMultiColor(ImVec2(x0, y), ImVec2(mid, y + h), z, g, g, z);
    dl_->AddRectFilledMultiColor(ImVec2(mid, y), ImVec2(x1, y + h), g, z, z, g);
    Diamond(ImVec2(mid, y + h * 0.5f), X(3.5f), g);
  }
  void Chevron(ImVec2 c, float r, bool right, ImU32 col) {
    const float d = right ? 1.0f : -1.0f;
    dl_->AddTriangleFilled(ImVec2(c.x + d * r, c.y), ImVec2(c.x - d * r * 0.6f, c.y - r),
                           ImVec2(c.x - d * r * 0.6f, c.y + r), col);
  }

  // Xbox 360-style face button / bumper / key cap, followed by its label.
  // Returns the width used.
  float Prompt(float x, float y, const char* button, const char* label, bool measure_only = false) {
    const float size = kSmall;
    float w = 0.0f;
    const float h = X(30.0f);
    const float cy = y + h * 0.5f;
    struct Face { const char* name; ImU32 col; };
    static const Face kFaces[] = {{"A", IM_COL32(92, 168, 58, 255)},
                                  {"B", IM_COL32(204, 56, 44, 255)},
                                  {"X", IM_COL32(48, 108, 206, 255)},
                                  {"Y", IM_COL32(226, 178, 34, 255)}};
    const Face* face = nullptr;
    for (const Face& f : kFaces)
      if (std::strcmp(f.name, button) == 0) face = &f;
    const int game_icon = gameicons::Find(button);
    if (gameicons::Ready(game_icon)) {
      // The game's own button art (gui_textures.bnk), a little larger than the
      // cap since the art has a margin around the button.
      const float s = h * 1.25f;
      if (!measure_only) gameicons::Draw(dl_, game_icon, ImVec2(x - (s - h) * 0.5f, cy - s * 0.5f), s);
      w = h;
    } else if (face) {
      const float r = h * 0.5f;
      if (!measure_only) {
        dl_->AddCircleFilled(ImVec2(x + r, cy), r, IM_COL32(20, 20, 20, 230), 24);
        dl_->AddCircleFilled(ImVec2(x + r, cy), r * 0.84f, face->col, 24);
        const float tw = TextW(button, size, 0.82f);
        Text(ImVec2(x + r - tw * 0.5f, cy - X(size) * 0.82f * 0.52f), size, IM_COL32(255, 255, 255, 255), button,
             false, 0.82f);
      }
      w = h;
    } else {
      // Bumpers and keys: a rounded cap with the name inside.
      const float tw = TextW(button, size, 0.78f);
      const float cw = std::max(h, tw + X(14.0f));
      if (!measure_only) {
        dl_->AddRectFilled(ImVec2(x, y), ImVec2(x + cw, y + h), IM_COL32(18, 16, 12, 220), X(6.0f), 0);
        dl_->AddRect(ImVec2(x, y), ImVec2(x + cw, y + h), menu::with_alpha(menu::kCreamDim, 0.8f), X(6.0f), 0,
                     std::max(1.0f, X(1.2f)));
        Text(ImVec2(x + (cw - tw) * 0.5f, cy - X(size) * 0.78f * 0.52f), size, menu::kCream, button, false, 0.78f);
      }
      w = cw;
    }
    if (label && *label) {
      w += X(8.0f);
      if (!measure_only) Text(ImVec2(x + w, cy - X(size) * 0.52f), size, menu::kCream, label);
      w += TextW(label, size);
    }
    return w;
  }

  // --- Input -------------------------------------------------------------------------------
  void Adjust(const menu::Item& it, Settings& s, int dir) {
    if (dir == 0) return;
    const float v = it.get(s);
    float nv = v;
    switch (it.kind) {
      case menu::Kind::kToggle:
        nv = v > 0.5f ? 0.0f : 1.0f;
        break;
      case menu::Kind::kOption: {
        const int n = int(it.options.size());
        nv = float(((int(std::lround(v)) + dir) % n + n) % n);
        break;
      }
      case menu::Kind::kSlider:
        if (it.log)
          nv = v * std::pow(1.0f + it.step, float(dir));
        else
          nv = std::round((v + it.step * dir) / it.step) * it.step;
        nv = std::clamp(nv, it.lo, it.hi);
        break;
      case menu::Kind::kAction:
        return;
    }
    if (nv != v) it.set(s, nv);
  }

  void Activate(const menu::Item& it, Settings& s) {
    if (it.special == 1) {
      if (has_undo_) {
        s = undo_;
        has_undo_ = false;
        last_changed_ = -1;
      }
    } else if (it.kind == menu::Kind::kAction) {
      it.action(s);
    } else if (it.kind == menu::Kind::kToggle || it.kind == menu::Kind::kOption) {
      Adjust(it, s, 1);
    } else {
      screenshot_request().store(true);
    }
  }

  void SetPage(int p) {
    const int n = int(pages_.size());
    page_ = ((p % n) + n) % n;
    sel_ = 0;
    drag_row_ = -1;
  }

  // Row rectangle for visible line i (screen pixels).
  menu::Box RowRect(int i) const {
    const float y = X(kListY + kRow * i);
    return menu::Box{ImVec2(X(kLeft - 26.0f), y), ImVec2(X(kRight + 18.0f), y + X(kRow))};
  }
  float BarX0() const { return X(kRight - kBar); }
  float BarX1() const { return X(kRight); }

  void HandleInput(ImGuiIO& io, Settings& s) {
    const Settings start = s;
    if (const int d = nav_tab_direct().exchange(0); d > 0 && d <= int(pages_.size())) SetPage(d - 1);
    if (const int t = nav_tab().exchange(0); t != 0) SetPage(page_ + t);

    std::vector<int> vis = Visible(s);
    if (vis.empty()) return;
    sel_ = std::clamp(sel_, 0, int(vis.size()) - 1);

    if (const int dy = nav_y().exchange(0); dy != 0) {
      const int n = int(vis.size());
      sel_ = ((sel_ + dy) % n + n) % n;
    }
    const auto& items = pages_[page_].items;
    if (const int dx = nav_x().exchange(0); dx != 0) {
      const auto& it = items[vis[sel_]];
      if (it.kind == menu::Kind::kToggle || it.kind == menu::Kind::kOption)
        Adjust(it, s, dx > 0 ? 1 : -1);
      else
        for (int i = 0; i < std::abs(dx); ++i) Adjust(it, s, dx > 0 ? 1 : -1);
    }
    for (int a = nav_activate().exchange(0); a > 0; --a) Activate(items[vis[sel_]], s);

    // Mouse.
    const ImVec2 m = io.MousePos;
    const bool moved = std::fabs(m.x - last_mouse_.x) + std::fabs(m.y - last_mouse_.y) > 0.5f;
    last_mouse_ = m;
    mouse_in_panel_ = m.x >= 0 && m.x < X(kRight + 60.0f) && m.y >= 0 && m.y < io.DisplaySize.y;
    if (moved && mouse_in_panel_) prompts_pad().store(false, std::memory_order_relaxed);

    // Page names.
    if (io.MouseClicked[0]) {
      float x = X(kLeft);
      for (int p = 0; p < int(pages_.size()); ++p) {
        const float w = TextW(pages_[p].name, kSmall);
        if (m.x >= x - X(6) && m.x < x + w + X(6) && m.y >= X(kTabsY - 4) && m.y < X(kTabsY + 30)) {
          SetPage(p);
          vis = Visible(s);
          return;
        }
        x += w + X(20.0f);
      }
    }
    if (!io.MouseDown[0]) drag_row_ = -1;
    for (int i = 0; i < int(vis.size()); ++i) {
      const menu::Box r = RowRect(i);
      if (!r.Contains(m)) continue;
      if (moved && drag_row_ < 0) sel_ = i;
      const auto& it = items[vis[i]];
      if (io.MouseClicked[0]) {
        sel_ = i;
        if (it.kind == menu::Kind::kSlider && m.x >= BarX0() - X(8)) {
          drag_row_ = i;
        } else if (it.kind == menu::Kind::kOption && m.x < BarX0() + X(kBar * 0.5f) && m.x > BarX0() - X(60)) {
          Adjust(it, s, -1);
        } else {
          Activate(it, s);
        }
      }
      if (io.MouseWheel != 0.0f && it.kind != menu::Kind::kAction) {
        sel_ = i;
        Adjust(it, s, io.MouseWheel > 0 ? 1 : -1);
      }
      break;
    }
    if (drag_row_ >= 0 && drag_row_ < int(vis.size())) {
      const auto& it = items[vis[drag_row_]];
      float t = std::clamp((m.x - BarX0()) / (BarX1() - BarX0()), 0.0f, 1.0f);
      float v = it.log ? it.lo * std::pow(it.hi / it.lo, t) : it.lo + (it.hi - it.lo) * t;
      if (!it.log && it.step > 0) v = std::round(v / it.step) * it.step;
      it.set(s, std::clamp(v, it.lo, it.hi));
    }

    // Undo keeps the settings from before a run of changes to one line.
    if (std::memcmp(&start, &s, sizeof(Settings)) != 0) {
      const int line = vis.empty() ? -1 : vis[std::min(sel_, int(vis.size()) - 1)];
      if (line != last_changed_ || page_ != last_changed_page_) {
        undo_ = start;
        has_undo_ = true;
      }
      last_changed_ = line;
      last_changed_page_ = page_;
    }
  }

  // --- Drawing -------------------------------------------------------------------------------
  void DrawMenu(ImGuiIO& io, Settings& s) {
    const float H = io.DisplaySize.y;
    // Fade in over the first 150 ms.
    const float fade = std::clamp(float(now_ms() - opened_ms_) / 150.0f, 0.0f, 1.0f);

    // Background: dark on the left, fading into the picture.
    const ImU32 dark = IM_COL32(6, 5, 3, int(205 * fade));
    const ImU32 clear = IM_COL32(6, 5, 3, 0);
    dl_->AddRectFilled(ImVec2(0, 0), ImVec2(X(kRight + 30.0f), H), dark, 0.0f, 0);
    dl_->AddRectFilledMultiColor(ImVec2(X(kRight + 30.0f), 0), ImVec2(X(kRight + 260.0f), H), dark, clear, clear,
                                 dark);

    // Title and rule.
    Text(ImVec2(X(kLeft), X(kTitleY)), kLarge, menu::with_alpha(menu::kCream, fade), "Photo Mode");
    Rule(X(kLeft - 14.0f), X(kRight + 10.0f), X(kTitleY + 40.0f), fade);

    // Pages: LB  Camera  Lens  Colour  World  Capture  RB
    const bool pad = prompts_pad().load(std::memory_order_relaxed);
    float x = X(kLeft);
    const float ty = X(kTabsY);
    if (pad) {
      const float w = Prompt(0, 0, "LB", nullptr, true);
      Prompt(X(kLeft - 12.0f) - w, ty - X(1.0f), "LB", nullptr);
    }
    for (int p = 0; p < int(pages_.size()); ++p) {
      const bool cur = p == page_;
      const float w = TextW(pages_[p].name, kSmall);
      Text(ImVec2(x, ty), kSmall, menu::with_alpha(cur ? menu::kGold : menu::kFaint, fade), pages_[p].name);
      if (cur) {
        const float uy = ty + X(28.0f);
        dl_->AddRectFilled(ImVec2(x, uy), ImVec2(x + w, uy + std::max(1.0f, X(1.5f))),
                           menu::with_alpha(menu::kGold, fade), 0.0f, 0);
      }
      x += w + X(20.0f);
    }
    if (pad) Prompt(x - X(10.0f), ty - X(1.0f), "RB", nullptr);

    // Lines.
    const auto vis = Visible(s);
    sel_ = std::clamp(sel_, 0, std::max(0, int(vis.size()) - 1));
    const auto& items = pages_[page_].items;
    for (int i = 0; i < int(vis.size()); ++i) {
      const auto& it = items[vis[i]];
      const menu::Box r = RowRect(i);
      const bool cur = i == sel_;
      const float ty2 = r.Min.y + (r.GetHeight() - X(kText)) * 0.5f;
      if (cur) {
        const ImU32 a = IM_COL32(218, 182, 98, int(70 * fade));
        const ImU32 z = IM_COL32(218, 182, 98, 0);
        dl_->AddRectFilledMultiColor(r.Min, ImVec2(r.Max.x, r.Max.y), a, z, z, a);
        dl_->AddRectFilled(ImVec2(r.Min.x, r.Min.y), ImVec2(r.Min.x + std::max(1.0f, X(2.0f)), r.Max.y),
                           menu::with_alpha(menu::kGold, fade), 0.0f, 0);
        Diamond(ImVec2(X(kLeft - 12.0f), (r.Min.y + r.Max.y) * 0.5f), X(3.5f), menu::with_alpha(menu::kGold, fade));
      }
      const ImU32 label_col = menu::with_alpha(cur ? menu::kCream : menu::kCreamDim, fade);
      Text(ImVec2(X(kLeft), ty2), kText, label_col, it.label);

      const float cy = (r.Min.y + r.Max.y) * 0.5f;
      switch (it.kind) {
        case menu::Kind::kToggle: {
          const bool v = it.get(s) > 0.5f;
          TextRight(BarX1(), ty2, kText, menu::with_alpha(v ? menu::kGold : menu::kCreamDim, fade), v ? "On" : "Off");
          break;
        }
        case menu::Kind::kOption: {
          const int idx = std::clamp(int(std::lround(it.get(s))), 0, int(it.options.size()) - 1);
          const std::string shown = it.fmt ? it.fmt(float(idx)) : it.options[idx];
          const char* name = shown.c_str();
          const float right = cur ? BarX1() - X(14.0f) : BarX1();
          TextRight(right, ty2, kText, label_col, name);
          if (cur) {
            const float w = TextW(name, kText);
            Chevron(ImVec2(right - w - X(12.0f), cy), X(5.0f), false, menu::with_alpha(menu::kGold, fade));
            Chevron(ImVec2(BarX1() - X(4.0f), cy), X(5.0f), true, menu::with_alpha(menu::kGold, fade));
          }
          break;
        }
        case menu::Kind::kSlider: {
          const float v = it.get(s);
          float t = it.log ? std::log(v / it.lo) / std::log(it.hi / it.lo) : (v - it.lo) / (it.hi - it.lo);
          t = std::clamp(t, 0.0f, 1.0f);
          const float bh = std::max(2.0f, X(3.0f));
          const float x0 = BarX0(), x1 = BarX1();
          dl_->AddRectFilled(ImVec2(x0, cy - bh * 0.5f), ImVec2(x1, cy + bh * 0.5f), IM_COL32(255, 255, 255, int(40 * fade)),
                             bh * 0.5f, 0);
          dl_->AddRectFilled(ImVec2(x0, cy - bh * 0.5f), ImVec2(x0 + (x1 - x0) * t, cy + bh * 0.5f),
                             menu::with_alpha(cur ? menu::kGold : menu::kCreamDim, fade * (cur ? 1.0f : 0.7f)), bh * 0.5f, 0);
          Diamond(ImVec2(x0 + (x1 - x0) * t, cy), X(cur ? 6.0f : 4.5f),
                  menu::with_alpha(cur ? menu::kGold : menu::kCreamDim, fade));
          const std::string val = it.fmt ? it.fmt(v) : menu::Fmt("%.2f", v);
          TextRight(x0 - X(14.0f), ty2, kText, label_col, val.c_str());
          break;
        }
        case menu::Kind::kAction:
          if (cur) Chevron(ImVec2(BarX1() - X(4.0f), cy), X(5.0f), true, menu::with_alpha(menu::kGold, fade));
          break;
      }
    }

    // Description of the selected line.
    if (!vis.empty()) {
      const float y = X(kListY + kRow * vis.size() + 14.0f);
      Rule(X(kLeft - 14.0f), X(kRight + 10.0f), y, 0.5f * fade);
      TextWrapped(ImVec2(X(kLeft), y + X(12.0f)), kSmall, X(kRight - kLeft), menu::with_alpha(menu::kFaint, fade),
                  items[vis[sel_]].desc);
    }
  }

  void DrawPrompts(ImGuiIO& io, bool shown) {
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    const bool pad = prompts_pad().load(std::memory_order_relaxed);
    struct P { std::string button, label; };
    std::vector<P> list;
    if (shown) {
      const Settings s = settings_copy();
      const auto vis = Visible(s);
      const char* a = "Take Photo";
      if (!vis.empty()) {
        const auto& it = pages_[page_].items[vis[std::min(sel_, int(vis.size()) - 1)]];
        if (it.kind == menu::Kind::kAction) a = it.label;
        else if (it.kind != menu::Kind::kSlider) a = "Change";
      }
      if (pad) {
        list = {{"A", a}, {"X", "Reset Camera"}, {"Y", "Hide Menu"}, {"B", "Exit"}};
      } else {
        list.clear();
        if (std::strcmp(a, "Take Photo") != 0) list.push_back({"Enter", a});
        list.push_back({config::Get().photo_mode_screenshot_key, "Take Photo"});
        list.push_back({"H", "Hide Menu"});
        list.push_back({"Esc", "Exit"});
      }
      // A thin dark band behind the prompts.
      dl_->AddRectFilledMultiColor(ImVec2(0, H - X(90.0f)), ImVec2(W, H), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0),
                                   IM_COL32(0, 0, 0, 150), IM_COL32(0, 0, 0, 150));
    } else {
      list = pad ? std::vector<P>{{"A", "Take Photo"}, {"Y", "Show Menu"}}
                 : std::vector<P>{{config::Get().photo_mode_screenshot_key, "Take Photo"}, {"H", "Show Menu"}};
    }
    // Right-aligned row along the bottom.
    float total = 0.0f;
    const float gap = X(30.0f);
    for (const P& p : list) total += Prompt(0, 0, p.button.c_str(), p.label.c_str(), true) + gap;
    float x = W - X(48.0f) - (total - gap);
    const float y = H - X(56.0f);
    for (const P& p : list) x += Prompt(x, y, p.button.c_str(), p.label.c_str()) + gap;
    if (shown && !pad) {
      // Camera keys on the left, when there is room beside the prompts.
      const char* keys = "WASD Move    Q / E Down, Up    Right Mouse Look";
      if (X(kLeft) + TextW(keys, kSmall) + X(40.0f) < W - X(48.0f) - (total - gap))
        Text(ImVec2(X(kLeft), y + X(30.0f) * 0.5f - X(kSmall) * 0.52f), kSmall, menu::kFaint, keys);
    }
  }

  void DrawStatus(ImGuiIO& io) {
    const float W = io.DisplaySize.x;
    const std::string st = status();
    if (st != toast_) {
      toast_ = st;
      toast_ms_ = now_ms();
    }
    // Self-timer countdown in the middle of the top edge.
    const int64_t due = shot_next_ms().load();
    if (due != 0 && shots_left().load() > 0) {
      const int64_t left = due - now_ms();
      if (left > 300) {
        char b[8];
        std::snprintf(b, sizeof(b), "%d", int((left + 999) / 1000));
        const float w = TextW(b, kLarge, 2.0f);
        Text(ImVec2(W * 0.5f - w * 0.5f, X(40.0f)), kLarge, menu::kCream, b, true, 2.0f);
      }
    }
    if (pad_rebinding().load()) {
      // Recording a controller shortcut: say so in the middle of the screen.
      const float H = io.DisplaySize.y;
      const char* t1 = "Hold the buttons for the new shortcut, then let go";
      const char* t2 = "Nothing pressed for 10 seconds keeps the current one";
      const float w1 = TextW(t1, kNormal), w2 = TextW(t2, kSmall);
      const float cy = H * 0.5f;
      dl_->AddRectFilled(ImVec2(0, cy - X(50.0f)), ImVec2(W, cy + X(50.0f)), IM_COL32(0, 0, 0, 170), 0.0f, 0);
      Rule(W * 0.5f - X(260.0f), W * 0.5f + X(260.0f), cy - X(50.0f), 1.0f);
      Rule(W * 0.5f - X(260.0f), W * 0.5f + X(260.0f), cy + X(50.0f), 1.0f);
      Text(ImVec2(W * 0.5f - w1 * 0.5f, cy - X(32.0f)), kNormal, menu::kCream, t1);
      Text(ImVec2(W * 0.5f - w2 * 0.5f, cy + X(6.0f)), kSmall, menu::with_alpha(menu::kCreamDim, 0.9f), t2);
    }
    if (toast_.empty()) return;
    const float age = float(now_ms() - toast_ms_) / 1000.0f;
    if (age > 3.5f) return;
    const float a = age < 3.0f ? 1.0f : 1.0f - (age - 3.0f) / 0.5f;
    // "Saved Fable2_....png (2560x1440)" -> "Photo saved".
    const bool saved = toast_.rfind("Saved", 0) == 0;
    const char* t = saved ? "Photo Saved" : toast_.c_str();
    const float w = TextW(t, kNormal);
    const float x = W - X(48.0f) - w;
    Rule(x - X(20.0f), W - X(28.0f), X(80.0f), a);
    Text(ImVec2(x, X(46.0f)), kNormal, menu::with_alpha(menu::kCream, a), t);
  }

  // Thirds, centre grid and cinema bars, drawn over the game (never in photos).
  void DrawGuides(ImGuiIO& io, const Settings& s) {
    const float w = io.DisplaySize.x;
    const float h = io.DisplaySize.y;
    if (s.letterbox != 0) {
      static const float kRatios[] = {0.0f, 1.85f, 2.35f, 2.39f};
      const float bar = std::max(0.0f, (h - w / kRatios[std::clamp(s.letterbox, 0, 3)]) * 0.5f);
      if (bar > 0.5f) {
        dl_->AddRectFilled(ImVec2(0, 0), ImVec2(w, bar), IM_COL32(0, 0, 0, 255), 0.0f, 0);
        dl_->AddRectFilled(ImVec2(0, h - bar), ImVec2(w, h), IM_COL32(0, 0, 0, 255), 0.0f, 0);
      }
    }
    if (s.guides == 0) return;
    const ImU32 line = IM_COL32(255, 255, 255, 90);
    if (s.guides == 1 || s.guides == 3) {
      for (int i = 1; i <= 2; ++i) {
        dl_->AddLine(ImVec2(w * i / 3.0f, 0.0f), ImVec2(w * i / 3.0f, h), line, 1.0f);
        dl_->AddLine(ImVec2(0.0f, h * i / 3.0f), ImVec2(w, h * i / 3.0f), line, 1.0f);
      }
    }
    if (s.guides == 2 || s.guides == 3) {
      const float c = X(10.0f);
      dl_->AddLine(ImVec2(w * 0.5f - c, h * 0.5f), ImVec2(w * 0.5f + c, h * 0.5f), line, 1.0f);
      dl_->AddLine(ImVec2(w * 0.5f, h * 0.5f - c), ImVec2(w * 0.5f, h * 0.5f + c), line, 1.0f);
    }
  }

  static void MergeChanged(const Settings& before, const Settings& after, Settings& live) {
#define FABLE2_PM_MERGE(f) \
  if (std::memcmp(&before.f, &after.f, sizeof(before.f)) != 0) \
    std::memcpy(&live.f, &after.f, sizeof(live.f));
    FABLE2_PM_MERGE(freeze_world)
    FABLE2_PM_MERGE(hide_hud)
    FABLE2_PM_MERGE(hide_world_icons)
    FABLE2_PM_MERGE(move_speed)
    FABLE2_PM_MERGE(look_speed)
    FABLE2_PM_MERGE(fov_deg)
    FABLE2_PM_MERGE(roll_deg)
    FABLE2_PM_MERGE(dof_mode)
    FABLE2_PM_MERGE(dof_focus)
    FABLE2_PM_MERGE(dof_range)
    FABLE2_PM_MERGE(dof_falloff)
    FABLE2_PM_MERGE(dof_strength)
    FABLE2_PM_MERGE(dof_raw)
    FABLE2_PM_MERGE(dof_alpha)
    FABLE2_PM_MERGE(vignette_a)
    FABLE2_PM_MERGE(vignette_b)
    FABLE2_PM_MERGE(blur)
    FABLE2_PM_MERGE(exposure)
    FABLE2_PM_MERGE(contrast)
    FABLE2_PM_MERGE(saturation)
    FABLE2_PM_MERGE(brightness)
    FABLE2_PM_MERGE(bloom)
    FABLE2_PM_MERGE(bloom_threshold)
    FABLE2_PM_MERGE(sun_beams)
    FABLE2_PM_MERGE(overlay)
    FABLE2_PM_MERGE(fog)
    FABLE2_PM_MERGE(ground_mist)
    FABLE2_PM_MERGE(rain)
    FABLE2_PM_MERGE(snow)
    FABLE2_PM_MERGE(stars)
    FABLE2_PM_MERGE(moon_glare)
    FABLE2_PM_MERGE(sun_disc)
    FABLE2_PM_MERGE(guides)
    FABLE2_PM_MERGE(letterbox)
    FABLE2_PM_MERGE(filter)
    FABLE2_PM_MERGE(filter_strength)
    FABLE2_PM_MERGE(warmth)
    FABLE2_PM_MERGE(tint)
    FABLE2_PM_MERGE(override_time)
    FABLE2_PM_MERGE(time_hours)
#undef FABLE2_PM_MERGE
  }
};

}  // namespace fable2::photomode
