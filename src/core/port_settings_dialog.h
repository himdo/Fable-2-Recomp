// fable_2 - The Advanced Options page.
//
// Opened from the "Advanced Options" item the recomp adds to the main menu
// (src/menu/main_menu.cpp) and from the "Advanced" row it adds to the pause
// menu's Options (src/core/port_settings_lua.h). Lists the fable2_config.toml settings
// from src/core/port_settings_schema.h on pages (Display, Frame Rate, Mouse,
// Fixes, Photo Mode) and lets the player change them.
//
// Drawn like the game's own menus, not as a desktop window: the game's GUI
// typeface (src/core/photo_mode_font.h), cream text with a gold highlight
// and gold rules, the game's Options icon and controller button art
// (src/core/photo_mode_icons.h), all straight into ImGui's foreground draw
// list, with no ImGui windows or widgets.
//
// Controls, as in the game's menus:
//   controller  D-pad / left stick: move and change, A: change, Y: default,
//               LB / RB: page, B or Start: back (saves)
//   keyboard    the keys mapped onto the controller (arrows, E, B, Tab, Z,
//               Escape) plus Enter / Space: change, Delete: default,
//               Page Up / Page Down and 1-5: page, Backspace: back
//   mouse       point to select, click to change (left half of a choice goes
//               back), drag a bar, wheel to change, right click: back
// The controller is read from the game's own pad reads, which are blanked for
// the game while the page is open (src/core/port_settings_state.h).
//
// Changes apply at once where the game allows it (see the schema), and are
// written to fable2_config.toml when the page closes, keeping the file's
// comments and layout (src/core/fable2_config_edit.h).
#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <imgui.h>
#include <rex/input/input_system.h>
#include <rex/logging/macros.h>
#include <rex/runtime.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <toml++/toml.hpp>

#include "fable2_config.h"
#include "fable2_config_edit.h"
#include "photo_mode_font.h"
#include "photo_mode_icons.h"
#include "photo_mode_state.h"
#include "port_settings_schema.h"
#include "port_settings_state.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace fable2::portsettings {

// The game's Options icon (gui_textures.bnk), requested by Fable2App.
inline constexpr const char* kOptionsIcon = "art\\gui\\wheelicons\\icon_options.tex";

namespace detail {

inline int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// Reads every setting from fable2_config.toml; a key that is missing or of
// the wrong kind gets its default. `error` is set when the file can't be read.
inline std::vector<Value> ReadFile(const std::vector<Setting>& list, std::string& error) {
  std::vector<Value> out;
  for (const Setting& s : list) out.push_back(s.def);
  const auto& path = config::Path();
  if (path.empty()) {
    error = "fable2_config.toml was not loaded at startup, so changes can't be saved.";
    return out;
  }
  toml::table root;
  try {
    root = toml::parse_file(path.native());
  } catch (const toml::parse_error& e) {
    error = std::string("fable2_config.toml has an error: ") + std::string(e.description());
    return out;
  }
  for (size_t i = 0; i < list.size(); ++i) {
    const Setting& s = list[i];
    const auto node = root[s.section][s.key];
    if (!node) continue;
    Value& v = out[i];
    switch (s.def.type) {
      case Type::kBool:
        if (auto b = node.value_exact<bool>()) v.b = *b;
        break;
      case Type::kInt:
        if (auto n = node.value_exact<int64_t>()) v.i = *n;
        else if (auto d = node.value<double>()) v.i = int64_t(std::llround(*d));
        break;
      case Type::kFloat:
        if (auto d = node.value<double>()) v.d = *d;
        break;
      case Type::kString:
        if (auto str = node.value<std::string>()) {
          v.s = *str;
          if (std::strcmp(s.key, "mouse_look_mode") == 0)
            for (char& c : v.s) c = char(std::tolower(static_cast<unsigned char>(c)));
        } else if (auto b = node.value_exact<bool>()) {
          // Older files: photo_mode.gamepad_toggle = true / false.
          v.s = *b ? s.def.s : "Off";
        }
        break;
    }
  }
  return out;
}

inline std::string Literal(const Value& v) {
  switch (v.type) {
    case Type::kBool: return config::TomlBool(v.b);
    case Type::kInt: return config::TomlInt(v.i);
    case Type::kFloat: return config::TomlDouble(v.d);
    case Type::kString: return config::TomlString(v.s);
  }
  return {};
}

// Index of `v` in a choice list; -1 if it is not one of them.
inline int ChoiceIndex(const Setting& s, const Value& v) {
  for (int i = 0; i < int(s.choices.size()); ++i) {
    const Value& c = s.choices[i].value;
    if (c == v) return i;
    if (c.type == Type::kString && v.type == Type::kString) {
      bool same = c.s.size() == v.s.size();
      for (size_t k = 0; same && k < c.s.size(); ++k)
        same = std::tolower(static_cast<unsigned char>(c.s[k])) == std::tolower(static_cast<unsigned char>(v.s[k]));
      if (same) return i;
    }
  }
  return -1;
}

// The value's text on the right of its line.
inline std::string ValueText(const Setting& s, const Value& v) {
  switch (s.control) {
    case Control::kToggle: return (v.b != s.inverted) ? "On" : "Off";
    case Control::kChoice: {
      const int i = ChoiceIndex(s, v);
      if (i >= 0) return s.choices[i].label;
      return v.type == Type::kString ? v.s : CvarText(v);  // edited by hand: as it is
    }
    case Control::kSlider: return s.format ? s.format(v.Number()) : CvarText(v);
  }
  return {};
}

}  // namespace detail

class PortSettingsDialog final : public rex::ui::ImGuiDialog {
 public:
  explicit PortSettingsDialog(rex::ui::ImGuiDrawer* drawer) : rex::ui::ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    dl_ = ImGui::GetForegroundDrawList();
    k_ = std::max(0.5f, io.DisplaySize.y / 720.0f);
    if (open_request().exchange(false)) {
      if (!is_open().load() && !photomode::active()) Open();
    }
    if (is_open().load() && photomode::active()) Close();  // photo mode took over
    if (!is_open().load()) {
      DrawToast(io);
      return;
    }
    HandleInput(io);
    if (!is_open().load()) {  // closed by this frame's input
      DrawToast(io);
      return;
    }
    Draw(io);
  }

 private:
  // --- Model --------------------------------------------------------------------------
  const std::vector<Setting>& list_ = Settings();
  std::vector<Value> values_;   // as shown (changes applied)
  std::vector<Value> in_file_;  // as read when the page opened
  // As the game started (first open), to tell which changes need a restart.
  std::vector<Value> at_launch_;
  std::vector<bool> pinned_;    // a cvar is set by fable_2.toml / env / command line
  std::string file_error_;

  int page_ = 0;
  int sel_ = 0;     // row on the page
  int scroll_ = 0;  // first row shown
  int64_t opened_ms_ = 0;

  // Input.
  uint16_t prev_buttons_ = 0;
  bool prev_keys_[256] = {};
  photomode::Repeater rep_up_, rep_down_, rep_left_, rep_right_;
  ImVec2 last_mouse_{-1e9f, -1e9f};
  int drag_row_ = -1;
  bool pad_prompts_ = true;  // prompts follow the last device used

  // Toast after closing.
  std::string toast_;
  int64_t toast_ms_ = 0;

  // Drawing.
  ImDrawList* dl_ = nullptr;
  float k_ = 1.0f;   // pixels per 720p unit
  float ox_ = 0.0f;  // left edge of the page, in pixels

  std::vector<int> Rows() const {
    std::vector<int> r;
    for (int i = 0; i < int(list_.size()); ++i)
      if (list_[i].page == page_) r.push_back(i);
    return r;
  }

  void Open() {
    values_ = detail::ReadFile(list_, file_error_);
    in_file_ = values_;
    if (at_launch_.empty()) {
      at_launch_ = values_;
      for (const Setting& s : list_) {
        bool p = false;
        for (const char* c : s.cvars) p = p || CvarPinned(c);
        pinned_.push_back(p);
      }
    }
    page_ = 0;
    sel_ = 0;
    scroll_ = 0;
    drag_row_ = -1;
    opened_ms_ = detail::now_ms();
    // Whatever is held now (the A that chose the menu item) is not a press.
    prev_buttons_ = merged_pad().buttons;
    for (int vk = 0; vk < 256; ++vk) prev_keys_[vk] = KeyDown(vk);
    rep_up_ = rep_down_ = rep_left_ = rep_right_ = photomode::Repeater{};
    pad_prompts_ = !LastInputWasKeyboard();
    is_open().store(true);
    REXSYS_INFO("[advanced-options] opened ({} settings{}{})", list_.size(), file_error_.empty() ? "" : "; ",
                file_error_);
  }

  void Close() {
    std::vector<config::FileEdit> edits;
    std::string names;
    for (size_t i = 0; i < list_.size(); ++i) {
      if (values_[i] == in_file_[i]) continue;
      edits.push_back({list_[i].section, list_[i].key, detail::Literal(values_[i])});
      names += std::string(names.empty() ? "" : ", ") + list_[i].section + "." + list_[i].key + " = " +
               edits.back().literal;
    }
    if (!edits.empty()) {
      const bool ok = file_error_.empty() && config::SaveFileEdits(edits);
      toast_ = ok ? "Settings Saved" : "Settings Not Saved";
      toast_ms_ = detail::now_ms();
      if (ok) REXSYS_INFO("[advanced-options] saved {}", names);
      else REXSYS_WARN("[advanced-options] could not save {} ({})", names, file_error_);
    } else {
      REXSYS_INFO("[advanced-options] closed, nothing changed");
    }
    hold_until_released().store(true);
    is_open().store(false);
  }

  void Set(int i, Value v) {
    const Setting& s = list_[i];
    if (s.control == Control::kSlider) {
      double n = std::clamp(v.Number(), s.lo, s.hi);
      n = std::round(n / s.step) * s.step;
      if (s.def.type == Type::kInt) v = Int(int64_t(std::llround(n)));
      else v = Float(std::round(n * 1e6) / 1e6);  // 0.065, not 0.06500000000000001
    }
    if (v == values_[i]) return;
    values_[i] = v;
    if (s.live && s.when != When::kRestart) s.live(v);
    REXSYS_INFO("[advanced-options] {}.{} = {}{}", s.section, s.key, detail::Literal(v),
                s.when == When::kRestart ? " (after a restart)" : s.when == When::kNextLoad ? " (next save load)" : "");
  }

  void Step(int i, int dir) {
    const Setting& s = list_[i];
    const Value& v = values_[i];
    switch (s.control) {
      case Control::kToggle:
        Set(i, Bool(!v.b));
        break;
      case Control::kChoice: {
        const int n = int(s.choices.size());
        int at = detail::ChoiceIndex(s, v);
        at = at < 0 ? (dir > 0 ? 0 : n - 1) : ((at + dir) % n + n) % n;
        Set(i, s.choices[at].value);
        break;
      }
      case Control::kSlider: {
        Value nv = v;
        if (nv.type == Type::kInt) nv.i += int64_t(std::llround(s.step)) * dir;
        else nv.d += s.step * dir;
        Set(i, nv);
        break;
      }
    }
  }

  // --- Input --------------------------------------------------------------------------
  // Keys reach the game as a synthetic controller, which the input system
  // tells apart from a real one.
  static bool LastInputWasKeyboard() {
    auto* rt = rex::Runtime::instance();
    auto* input = rt ? static_cast<rex::input::InputSystem*>(rt->input_system()) : nullptr;
    return input && input->ActiveDeviceIsSynthetic(0);
  }
  static bool Focused() { return photomode::window_focused().load(std::memory_order_relaxed); }
  static bool KeyDown(int vk) {
#ifdef _WIN32
    return Focused() && (GetAsyncKeyState(vk) & 0x8000) != 0;
#else
    (void)vk;
    return false;
#endif
  }
  bool KeyPressed(int vk) {
    const bool d = KeyDown(vk);
    const bool hit = d && !prev_keys_[vk & 0xFF];
    prev_keys_[vk & 0xFF] = d;
    return hit;
  }

  void SetPage(int p) {
    page_ = ((p % kPageCount) + kPageCount) % kPageCount;
    sel_ = 0;
    scroll_ = 0;
    drag_row_ = -1;
  }

  void HandleInput(ImGuiIO& io) {
#ifdef _WIN32
    constexpr int kEnter = VK_RETURN, kSpace = VK_SPACE, kBack = VK_BACK, kDelete = VK_DELETE, kPgUp = VK_PRIOR,
                  kPgDn = VK_NEXT;
#else
    constexpr int kEnter = 0, kSpace = 0, kBack = 0, kDelete = 0, kPgUp = 0, kPgDn = 0;
#endif
    const PadSnapshot pad = merged_pad();
    const uint16_t b = pad.buttons;
    const uint16_t pressed = b & ~prev_buttons_;
    prev_buttons_ = b;
    // Keys that are not mapped onto the controller (pressing one that also is
    // counts once: the edges are taken separately but act the same).
    const bool k_enter = KeyPressed(kEnter) | KeyPressed(kSpace);
    const bool k_back = KeyPressed(kBack);
    const bool k_default = KeyPressed(kDelete);
    const bool k_prev = KeyPressed(kPgUp), k_next = KeyPressed(kPgDn);
    int k_digit = 0;
    for (int d = 1; d <= kPageCount; ++d)
      if (KeyPressed('0' + d)) k_digit = d;
    // Prompts follow the device used last: keys reach the game as a synthetic
    // controller, which the input system tells apart.
    if (pressed != 0 || std::abs(int(pad.lx)) > 16000 || std::abs(int(pad.ly)) > 16000)
      pad_prompts_ = !LastInputWasKeyboard();
    if (k_enter || k_back || k_default || k_prev || k_next || k_digit) pad_prompts_ = false;
    // The mouse buttons are mapped onto the controller too (right = Y,
    // middle = B by default): a right click means back here, not "default".
    const bool rmb = io.MouseDown[1] || io.MouseClicked[1];

    if ((pressed & (0x2000 | 0x0010)) || k_back || io.MouseClicked[1]) {  // B, Start, right click
      Close();
      return;
    }
    if ((pressed & 0x0100) || k_prev) SetPage(page_ - 1);  // LB
    if ((pressed & 0x0200) || k_next) SetPage(page_ + 1);  // RB
    if (k_digit) SetPage(k_digit - 1);

    const std::vector<int> rows = Rows();
    if (rows.empty()) return;
    sel_ = std::clamp(sel_, 0, int(rows.size()) - 1);
    const int64_t now = detail::now_ms();
    // D-pad (arrow keys arrive through it) and the left stick, with repeat.
    const int dy = rep_down_.Fire((b & 0x0002) || pad.ly < -16000, now) - rep_up_.Fire((b & 0x0001) || pad.ly > 16000, now);
    const int dx = rep_right_.Fire((b & 0x0008) || pad.lx > 16000, now) - rep_left_.Fire((b & 0x0004) || pad.lx < -16000, now);
    if (dy != 0) {
      const int n = int(rows.size());
      sel_ = ((sel_ + dy) % n + n) % n;
    }
    if (dx != 0) Step(rows[sel_], dx > 0 ? 1 : -1);
    if ((pressed & 0x1000) || k_enter) {  // A
      const Setting& s = list_[rows[sel_]];
      if (s.control != Control::kSlider) Step(rows[sel_], 1);
    }
    if (((pressed & 0x8000) && !rmb) || k_default) Set(rows[sel_], list_[rows[sel_]].def);  // Y

    // Mouse.
    const ImVec2 m = io.MousePos;
    const bool moved = std::fabs(m.x - last_mouse_.x) + std::fabs(m.y - last_mouse_.y) > 0.5f;
    last_mouse_ = m;
    if (moved || io.MouseClicked[0] || io.MouseWheel != 0.0f) pad_prompts_ = false;
    if (io.MouseClicked[0]) {
      for (int p = 0; p < kPageCount; ++p)
        if (TabBox(p).Contains(m)) {
          SetPage(p);
          return;
        }
    }
    if (!io.MouseDown[0]) drag_row_ = -1;
    const int shown = VisibleRows();
    for (int r = scroll_; r < std::min(int(rows.size()), scroll_ + shown); ++r) {
      const Box box = RowBox(r - scroll_);
      if (!box.Contains(m)) continue;
      if (moved && drag_row_ < 0) sel_ = r;
      const Setting& s = list_[rows[r]];
      if (io.MouseClicked[0]) {
        sel_ = r;
        if (s.control == Control::kSlider) {
          if (m.x >= BarX0() - X(10)) drag_row_ = r;
        } else if (s.control == Control::kChoice && m.x < ValueRight() - X(70)) {
          Step(rows[r], -1);
        } else {
          Step(rows[r], 1);
        }
      }
      if (io.MouseWheel != 0.0f) {
        sel_ = r;
        Step(rows[r], io.MouseWheel > 0 ? 1 : -1);
      }
      break;
    }
    if (drag_row_ >= 0 && drag_row_ < int(rows.size())) {
      const Setting& s = list_[rows[drag_row_]];
      const float t = std::clamp((m.x - BarX0()) / (BarX1() - BarX0()), 0.0f, 1.0f);
      const double v = s.lo + (s.hi - s.lo) * t;
      Set(rows[drag_row_], s.def.type == Type::kInt ? Int(int64_t(std::llround(v))) : Float(v));
    }
    // Keep the selection on screen.
    if (sel_ < scroll_) scroll_ = sel_;
    if (sel_ >= scroll_ + shown) scroll_ = sel_ - shown + 1;
  }

  // --- Layout (720p units, the page centred) -------------------------------------------
  struct Box {
    ImVec2 Min, Max;
    bool Contains(ImVec2 p) const { return p.x >= Min.x && p.y >= Min.y && p.x < Max.x && p.y < Max.y; }
  };
  static constexpr float kLarge = 32.0f;   // the game's three menu font sizes
  static constexpr float kNormal = 27.0f;
  static constexpr float kSmall = 24.0f;
  static constexpr float kWidth = 820.0f;  // page width
  static constexpr float kTitleY = 46.0f;
  static constexpr float kTabsY = 108.0f;
  static constexpr float kListY = 160.0f;
  static constexpr float kRow = 36.0f;
  static constexpr float kBar = 150.0f;
  static constexpr int kMaxRows = 9;

  float X(float v) const { return v * k_; }
  int VisibleRows() const { return kMaxRows; }
  Box RowBox(int i) const {
    const float y = X(kListY + kRow * i);
    return Box{ImVec2(ox_ - X(18), y), ImVec2(ox_ + X(kWidth + 18), y + X(kRow))};
  }
  float ValueRight() const { return ox_ + X(kWidth); }
  float BarX1() const { return ValueRight(); }
  float BarX0() const { return ValueRight() - X(kBar); }
  Box TabBox(int p) const {
    float x = ox_;
    for (int i = 0; i < p; ++i) x += TextW(PageName(i), kNormal) + X(30);
    return Box{ImVec2(x - X(8), X(kTabsY - 6)), ImVec2(x + TextW(PageName(p), kNormal) + X(8), X(kTabsY + 34))};
  }

  // --- Drawing helpers ------------------------------------------------------------------
  static constexpr ImU32 kCream = IM_COL32(240, 230, 204, 255);
  static constexpr ImU32 kCreamDim = IM_COL32(196, 184, 156, 255);
  static constexpr ImU32 kFaint = IM_COL32(160, 150, 128, 220);
  static constexpr ImU32 kGold = IM_COL32(218, 182, 98, 255);
  static constexpr ImU32 kShadow = IM_COL32(0, 0, 0, 200);
  static ImU32 Alpha(ImU32 c, float a) {
    const int na = std::clamp(int(((c >> IM_COL32_A_SHIFT) & 0xFF) * a), 0, 255);
    return (c & ~IM_COL32_A_MASK) | (ImU32(na) << IM_COL32_A_SHIFT);
  }

  float TextW(const char* t, float size) const {
    if (gamefont::AnyReady()) return gamefont::Measure(t, X(size), size);
    return float(std::strlen(t)) * 7.0f;  // default ImGui font fallback
  }
  void Text(ImVec2 p, float size, ImU32 col, const char* t) {
    if (gamefont::AnyReady()) {
      const float px = X(size);
      const float o = std::max(1.0f, px * 0.06f);
      gamefont::Draw(dl_, ImVec2(p.x + o, p.y + o), px, size, Alpha(kShadow, ((col >> 24) & 0xFF) / 255.0f), t);
      gamefont::Draw(dl_, p, px, size, col, t);
    } else {
      dl_->AddText(ImVec2(p.x + 1, p.y + 1), kShadow, t);
      dl_->AddText(p, col, t);
    }
  }
  void TextRight(float right, float y, float size, ImU32 col, const char* t) {
    Text(ImVec2(right - TextW(t, size), y), size, col, t);
  }
  float TextWrapped(ImVec2 p, float size, float width, ImU32 col, const std::string& all) {
    std::string line;
    float y = p.y;
    size_t i = 0;
    while (i <= all.size()) {
      const size_t j = all.find(' ', i);
      const std::string word = all.substr(i, j == std::string::npos ? std::string::npos : j - i);
      const std::string trial = line.empty() ? word : line + " " + word;
      if (!line.empty() && TextW(trial.c_str(), size) > width) {
        Text(ImVec2(p.x, y), size, col, line.c_str());
        y += X(size) * 1.05f;
        line = word;
      } else {
        line = trial;
      }
      if (j == std::string::npos) break;
      i = j + 1;
    }
    if (!line.empty()) {
      Text(ImVec2(p.x, y), size, col, line.c_str());
      y += X(size) * 1.05f;
    }
    return y - p.y;
  }
  void Diamond(ImVec2 c, float r, ImU32 col) {
    dl_->AddQuadFilled(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y), col);
  }
  // Gold rule fading out at both ends, a diamond in the middle.
  void Rule(float x0, float x1, float y, float a) {
    const float mid = (x0 + x1) * 0.5f;
    const float h = std::max(1.0f, X(1.2f));
    const ImU32 g = Alpha(kGold, 0.85f * a), z = Alpha(kGold, 0.0f);
    dl_->AddRectFilledMultiColor(ImVec2(x0, y), ImVec2(mid, y + h), z, g, g, z);
    dl_->AddRectFilledMultiColor(ImVec2(mid, y), ImVec2(x1, y + h), g, z, z, g);
    Diamond(ImVec2(mid, y + h * 0.5f), X(3.5f), g);
  }
  void Chevron(ImVec2 c, float r, bool right, ImU32 col) {
    const float d = right ? 1.0f : -1.0f;
    dl_->AddTriangleFilled(ImVec2(c.x + d * r, c.y), ImVec2(c.x - d * r * 0.6f, c.y - r),
                           ImVec2(c.x - d * r * 0.6f, c.y + r), col);
  }
  // A button prompt: the game's button art when loaded, else a cap with the
  // name; then its label. Returns the width.
  float Prompt(float x, float y, const char* button, const char* label, bool measure = false) {
    const float h = X(30);
    const float cy = y + h * 0.5f;
    float w;
    const int icon = gameicons::Find(button);
    if (gameicons::Ready(icon)) {
      const float s = h * 1.25f;
      if (!measure) gameicons::Draw(dl_, icon, ImVec2(x - (s - h) * 0.5f, cy - s * 0.5f), s);
      w = h;
    } else {
      const float tw = TextW(button, kSmall) * 0.8f;
      w = std::max(h, tw + X(14));
      if (!measure) {
        dl_->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(18, 16, 12, 220), X(6));
        dl_->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), Alpha(kCreamDim, 0.8f), X(6), 0, std::max(1.0f, X(1.2f)));
        if (gamefont::AnyReady())
          gamefont::Draw(dl_, ImVec2(x + (w - tw) * 0.5f, cy - X(kSmall) * 0.8f * 0.52f), X(kSmall) * 0.8f, kSmall,
                         kCream, button);
        else
          dl_->AddText(ImVec2(x + 4, y + 2), kCream, button);
      }
    }
    w += X(8);
    if (!measure) Text(ImVec2(x + w, cy - X(kSmall) * 0.52f), kSmall, kCream, label);
    return w + TextW(label, kSmall);
  }

  // --- Drawing -------------------------------------------------------------------------
  void Draw(ImGuiIO& io) {
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    const float fade = std::clamp(float(detail::now_ms() - opened_ms_) / 150.0f, 0.0f, 1.0f);
    ox_ = std::max(X(40), (W - X(kWidth)) * 0.5f);

    // Darken the game's menu behind the page, most behind the page itself.
    dl_->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(4, 3, 2, int(160 * fade)));
    const float px0 = ox_ - X(70), px1 = ox_ + X(kWidth + 70);
    const ImU32 dark = IM_COL32(6, 5, 3, int(200 * fade)), clear = IM_COL32(6, 5, 3, 0);
    dl_->AddRectFilled(ImVec2(px0, 0), ImVec2(px1, H), dark);
    dl_->AddRectFilledMultiColor(ImVec2(px0 - X(120), 0), ImVec2(px0, H), clear, dark, dark, clear);
    dl_->AddRectFilledMultiColor(ImVec2(px1, 0), ImVec2(px1 + X(120), H), dark, clear, clear, dark);

    // Title with the game's Options icon.
    float tx = ox_;
    const int icon = gameicons::FindExtra(kOptionsIcon);
    if (gameicons::ExtraReady(icon)) {
      const float s = X(46);
      gameicons::DrawExtra(dl_, icon, ImVec2(ox_ - X(4), X(kTitleY) - X(8)), s, Alpha(IM_COL32_WHITE, fade));
      tx += s + X(6);
    }
    Text(ImVec2(tx, X(kTitleY)), kLarge, Alpha(kCream, fade), "Advanced Options");
    Rule(ox_ - X(14), ox_ + X(kWidth + 14), X(kTitleY + 44), fade);

    // Pages: LB  Display  Frame Rate  Mouse  Fixes  Photo Mode  RB
    for (int p = 0; p < kPageCount; ++p) {
      const Box b = TabBox(p);
      const bool cur = p == page_;
      const float x = b.Min.x + X(8);
      Text(ImVec2(x, X(kTabsY)), kNormal, Alpha(cur ? kGold : kFaint, fade), PageName(p));
      if (cur)
        dl_->AddRectFilled(ImVec2(x, X(kTabsY + 30)), ImVec2(b.Max.x - X(8), X(kTabsY + 30) + std::max(1.0f, X(1.5f))),
                           Alpha(kGold, fade));
    }
    if (pad_prompts_) {
      const float w = Prompt(0, 0, "LB", "", true) - X(8);
      Prompt(ox_ - X(16) - w, X(kTabsY - 1), "LB", "");
      Prompt(TabBox(kPageCount - 1).Max.x + X(8), X(kTabsY - 1), "RB", "");
    }

    // Lines.
    const std::vector<int> rows = Rows();
    const int shown = std::min(int(rows.size()), VisibleRows());
    for (int r = scroll_; r < scroll_ + shown && r < int(rows.size()); ++r) {
      const int i = rows[r];
      const Setting& s = list_[i];
      const Box box = RowBox(r - scroll_);
      const bool cur = r == sel_;
      const float cy = (box.Min.y + box.Max.y) * 0.5f;
      const float ty = cy - X(kNormal) * 0.52f;
      if (cur) {
        const ImU32 a = IM_COL32(218, 182, 98, int(70 * fade)), z = IM_COL32(218, 182, 98, 0);
        dl_->AddRectFilledMultiColor(box.Min, box.Max, a, z, z, a);
        dl_->AddRectFilled(box.Min, ImVec2(box.Min.x + std::max(1.0f, X(2)), box.Max.y), Alpha(kGold, fade));
        Diamond(ImVec2(ox_ - X(8), cy), X(3.5f), Alpha(kGold, fade));
      }
      const ImU32 col = Alpha(cur ? kCream : kCreamDim, fade);
      Text(ImVec2(ox_ + X(6), ty), kNormal, col, s.label);
      // Restart marker: gold once changed since the game started.
      if (s.when == When::kRestart) {
        const bool pending = values_[i] != at_launch_[i];
        const float lx = ox_ + X(6) + TextW(s.label, kNormal) + X(12);
        Text(ImVec2(lx, cy - X(kSmall) * 0.5f), kSmall, Alpha(pending ? kGold : kFaint, fade * (pending ? 1.0f : 0.8f)),
             pending ? "Restart to apply" : "Restart");
      }
      const std::string val = detail::ValueText(s, values_[i]);
      switch (s.control) {
        case Control::kToggle: {
          const bool on = values_[i].b != s.inverted;
          const float right = cur ? ValueRight() - X(16) : ValueRight();
          TextRight(right, ty, kNormal, Alpha(on ? kGold : kCreamDim, fade), val.c_str());
          if (cur) {
            Chevron(ImVec2(right - TextW(val.c_str(), kNormal) - X(12), cy), X(5), false, Alpha(kGold, fade));
            Chevron(ImVec2(ValueRight() - X(4), cy), X(5), true, Alpha(kGold, fade));
          }
          break;
        }
        case Control::kChoice: {
          const float right = cur ? ValueRight() - X(16) : ValueRight();
          TextRight(right, ty, kNormal, col, val.c_str());
          if (cur) {
            Chevron(ImVec2(right - TextW(val.c_str(), kNormal) - X(12), cy), X(5), false, Alpha(kGold, fade));
            Chevron(ImVec2(ValueRight() - X(4), cy), X(5), true, Alpha(kGold, fade));
          }
          break;
        }
        case Control::kSlider: {
          const float t = float(std::clamp((values_[i].Number() - s.lo) / (s.hi - s.lo), 0.0, 1.0));
          const float bh = std::max(2.0f, X(3));
          const float x0 = BarX0(), x1 = BarX1();
          dl_->AddRectFilled(ImVec2(x0, cy - bh * 0.5f), ImVec2(x1, cy + bh * 0.5f), IM_COL32(255, 255, 255, int(40 * fade)),
                             bh * 0.5f);
          dl_->AddRectFilled(ImVec2(x0, cy - bh * 0.5f), ImVec2(x0 + (x1 - x0) * t, cy + bh * 0.5f),
                             Alpha(cur ? kGold : kCreamDim, fade * (cur ? 1.0f : 0.7f)), bh * 0.5f);
          Diamond(ImVec2(x0 + (x1 - x0) * t, cy), X(cur ? 6.0f : 4.5f), Alpha(cur ? kGold : kCreamDim, fade));
          TextRight(x0 - X(16), ty, kNormal, col, val.c_str());
          break;
        }
      }
    }
    // More lines above / below.
    if (scroll_ > 0) Chevron(ImVec2(ox_ + X(kWidth * 0.5f), X(kListY - 6)), X(5), false, Alpha(kGold, fade));
    if (scroll_ + shown < int(rows.size()))
      Chevron(ImVec2(ox_ + X(kWidth * 0.5f), X(kListY + kRow * shown + 6)), X(5), true, Alpha(kGold, fade));

    // Description of the selected line, and when a change takes effect.
    float y = X(kListY + kRow * VisibleRows() + 16);
    Rule(ox_ - X(14), ox_ + X(kWidth + 14), y, 0.5f * fade);
    y += X(12);
    if (!rows.empty()) {
      const int i = rows[std::clamp(sel_, 0, int(rows.size()) - 1)];
      const Setting& s = list_[i];
      y += TextWrapped(ImVec2(ox_, y), kSmall, X(kWidth), Alpha(kCreamDim, fade), s.desc);
      std::string when;
      if (s.when == When::kRestart) when = "Takes effect the next time the game starts.";
      else if (s.when == When::kNextLoad) when = "Takes effect when a save is loaded.";
      else when = "Takes effect at once.";
      if (i < int(pinned_.size()) && pinned_[i])
        when += " fable_2.toml or the command line also sets this, and wins when the game starts.";
      y += X(4);
      TextWrapped(ImVec2(ox_, y), kSmall, X(kWidth), Alpha(kFaint, fade), when);
    }

    // Restart reminder and file problems, above the prompts.
    std::string pending;
    for (size_t i = 0; i < list_.size(); ++i)
      if (list_[i].when == When::kRestart && values_[i] != at_launch_[i])
        pending += std::string(pending.empty() ? "" : ", ") + list_[i].label;
    const float by = H - X(118);
    if (!file_error_.empty())
      TextWrapped(ImVec2(ox_, by - X(24)), kSmall, X(kWidth), Alpha(IM_COL32(230, 120, 90, 255), fade), file_error_);
    else if (!pending.empty())
      TextWrapped(ImVec2(ox_, by - X(24)), kSmall, X(kWidth), Alpha(kGold, fade), "Restart the game to apply: " + pending);

    DrawPrompts(io, fade);
  }

  void DrawPrompts(ImGuiIO& io, float fade) {
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    struct P { const char* button; const char* label; };
    std::vector<P> list;
    const std::vector<int> rows = Rows();
    const bool slider = !rows.empty() && list_[rows[std::clamp(sel_, 0, int(rows.size()) - 1)]].control == Control::kSlider;
    if (pad_prompts_) {
      list.push_back(slider ? P{"D-Pad", "Adjust"} : P{"A", "Change"});
      list.push_back({"Y", "Default"});
      list.push_back({"B", "Back"});
    } else {
      if (!slider) list.push_back({"Enter", "Change"});
      list.push_back({"Delete", "Default"});
      list.push_back({"Esc", "Back"});
    }
    dl_->AddRectFilledMultiColor(ImVec2(0, H - X(90)), ImVec2(W, H), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0),
                                 IM_COL32(0, 0, 0, int(150 * fade)), IM_COL32(0, 0, 0, int(150 * fade)));
    const float gap = X(30);
    float total = 0;
    for (const P& p : list) total += Prompt(0, 0, p.button, p.label, true) + gap;
    float x = ox_ + X(kWidth) - (total - gap);
    const float y = H - X(56);
    for (const P& p : list) x += Prompt(x, y, p.button, p.label) + gap;
    if (!pad_prompts_) {
      const char* keys = "Arrows Move and Change    Page Up / Down Page";
      if (ox_ + TextW(keys, kSmall) + X(40) < ox_ + X(kWidth) - (total - gap))
        Text(ImVec2(ox_, y + X(15) - X(kSmall) * 0.52f), kSmall, Alpha(kFaint, fade), keys);
    }
  }

  void DrawToast(ImGuiIO& io) {
    if (toast_.empty()) return;
    const float age = float(detail::now_ms() - toast_ms_) / 1000.0f;
    if (age > 3.0f) {
      toast_.clear();
      return;
    }
    const float a = age < 2.5f ? 1.0f : 1.0f - (age - 2.5f) / 0.5f;
    const float W = io.DisplaySize.x;
    const float w = TextW(toast_.c_str(), kNormal);
    const float x = W - X(48) - w;
    Rule(x - X(20), W - X(28), X(80), a);
    Text(ImVec2(x, X(46)), kNormal, Alpha(kCream, a), toast_.c_str());
  }
};

}  // namespace fable2::portsettings
