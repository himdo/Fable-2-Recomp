// fable_2 - The Advanced Options page.
//
// Opened from the "Advanced Options" item the recomp adds to the main menu
// (src/menu/main_menu.cpp) and from the "Advanced" row it adds to the pause
// menu's Options (src/core/port_settings_lua.h). Lists the fable2_config.toml settings
// from src/core/port_settings_schema.h on pages (Display, Frame Rate, Mouse,
// Fixes, Photo Mode) and lets the player change them.
//
// Drawn like the game's own menus, not as a desktop window: the game's GUI
// typeface (src/core/photo_mode_font.h) in the ink colours of the game's
// pages, on its paper between its page rims, with its title flourish and
// controller button art (src/core/photo_mode_icons.h), all straight into
// ImGui's foreground draw
// list, with no ImGui windows or widgets.
//
// Controls, as on the game's own options pages (left / right change a line,
// A accepts, B cancels):
//   controller  D-pad / left stick: move and change, Y: default, LB / RB:
//               page, A: accept (saves), B or Start: cancel (puts back)
//   keyboard    the keys mapped onto the controller (arrows, E, B, Tab, Z,
//               Escape) plus Enter / Space: accept, Delete: default,
//               Page Up / Page Down and 1-5: page, Backspace: cancel
//   mouse       point to select, click to change (left half goes back),
//               drag a bar, wheel to change, right click: cancel
// The controller is read from the game's own pad reads, which are blanked for
// the game while the page is open (src/core/port_settings_state.h).
//
// Changes apply at once (see the schema). Accept writes them to
// fable2_config.toml, keeping the file's comments and layout
// (src/core/fable2_config_edit.h); cancel puts them all back.
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

REXCVAR_DECLARE(bool, fit_window_hud_to_edges);

#ifdef _WIN32
#include <windows.h>
#endif

namespace fable2::portsettings {

// The game's Options icon (gui_textures.bnk), requested by Fable2App.
// The flourish the game's pages draw under their title (motifs.tex, texels
// x 22-362, y 2-29).
inline constexpr const char* kMotifs = "art\\gui\\frames\\motifs.tex";
// The game's parchment, the paper its book pages are made of.
inline constexpr const char* kPaperTexture = "art\\gui\\frames\\frames_page_texture.tex";
// The book's right-hand leather cover, which the game draws 1:1 at the right
// of its 1280x720 screen: the top piece's 512 rows, then the bottom piece's
// first 208. The page draws them again over its paper so the cover stays on
// top of the page.
inline constexpr const char* kCoverTop = "art\\gui\\frames\\frame_new_right_top.tex";
inline constexpr const char* kCoverBottom = "art\\gui\\frames\\frame_new_right_bottom.tex";
// The page's copper rims, strips in the game's frame sheet: along the cover
// (texels x 330-425, y 152-512) and, mirrored, along the outer edge where
// the page lies on the book (texels x 430-512). Each is the top half of the
// page; the bottom half is the same strip upside down.
inline constexpr const char* kFrameSheet = "art\\gui\\frames\\frames_elements.tex";

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

// The value's text on the right of its line, without the recommendation.
inline std::string PlainValueText(const Setting& s, const Value& v) {
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

// The value's text on the right of its line: "On (Recommended)" for the
// recommended value.
inline std::string ValueText(const Setting& s, const Value& v) {
  std::string t = PlainValueText(s, v);
  if (s.has_recommended && v == s.recommended) t += " (Recommended)";
  return t;
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
    if (is_open().load() && photomode::active()) Close(true);  // photo mode took over
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
  float ox_ = 0.0f;  // left edge of the text column, in pixels
  float cx_ = 0.0f;  // screen centre (the book's spine), in pixels
  float shift_ = 0.0f;  // how far the game moved its right cover on a wide screen, in pixels
  bool paper_ = false;         // drawing on the paper (dark ink gets a light emboss)

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

  // Accept keeps the changes and saves them; cancel puts every value changed
  // on this visit back (live too) and saves nothing, as the game's own
  // options pages do.
  void Close(bool accept) {
    if (!accept) {
      int undone = 0;
      for (size_t i = 0; i < list_.size(); ++i)
        if (values_[i] != in_file_[i]) {
          Set(int(i), in_file_[i]);
          ++undone;
        }
      REXSYS_INFO("[advanced-options] cancelled, {} change(s) put back", undone);
      hold_until_released().store(true);
      is_open().store(false);
      return;
    }
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
                s.when == When::kRestart    ? " (after a restart)"
                : s.when == When::kNextLoad ? " (next save load)"
                : s.when == When::kAtStart  ? " (next start)"
                                            : "");
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

    // As on the game's own options pages: B cancels, A accepts.
    if ((pressed & (0x2000 | 0x0010)) || k_back || io.MouseClicked[1]) {  // B, Start (Esc), right click
      Close(false);
      return;
    }
    if ((pressed & 0x1000) || k_enter) {  // A, Enter
      Close(true);
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

  // --- Layout ------------------------------------------------------------------------
  // The page is the book's right-hand page, as the game's own options pages
  // (Game, Video, Audio) are: the game's menu stays on the left page, the
  // settings fill the right one. Units are the game's 720p canvas; the page
  // runs from the book's spine (the screen centre) to its right edge.
  struct Box {
    ImVec2 Min, Max;
    bool Contains(ImVec2 p) const { return p.x >= Min.x && p.y >= Min.y && p.x < Max.x && p.y < Max.y; }
  };
  static constexpr float kLarge = 32.0f;   // the game's three menu font sizes
  static constexpr float kNormal = 27.0f;
  static constexpr float kSmall = 24.0f;
  // Where the page-edge strip's rim ends (its outer side), per 20 rows of the
  // strip from the top (texel x from 330), measured from the game's frame
  // sheet: the curve of the page's outer edge.
  static constexpr int kRimSlices = 18;
  static constexpr uint8_t kRimRight[kRimSlices] = {54, 58, 61, 65, 68, 70, 73, 75, 76,
                                                    78, 79, 80, 81, 82, 83, 83, 83, 83};
  // The rim's outer side at strip row `r` (0-360), between the measured rows.
  static float RimRight(float r) {
    const float t = std::clamp((r - 10.0f) / 20.0f, 0.0f, float(kRimSlices - 1));
    const int k = std::min(int(t), kRimSlices - 2);
    return float(kRimRight[k]) + (float(kRimRight[k + 1]) - float(kRimRight[k])) * (t - float(k));
  }
  static constexpr float kPageLeft = 10.0f;    // text area edges from the screen centre (inside the rims)
  static constexpr float kPageRight = 396.0f;
  static constexpr float kMargin = 10.0f;      // text inset from the page edges
  static constexpr float kWidth = kPageRight - kPageLeft - 2 * kMargin;  // text column
  static constexpr float kTitleY = 30.0f;
  static constexpr float kTabsY = 96.0f;
  static constexpr float kListY = 140.0f;
  static constexpr float kRow = 34.0f;
  static constexpr float kBar = 84.0f;
  static constexpr int kMaxRows = 10;

  float X(float v) const { return v * k_; }
  int VisibleRows() const { return kMaxRows; }
  // The text column moves by half of the cover's shift, so it stays in the
  // middle of the wider paper.
  float PageX0() const { return cx_ + X(kPageLeft) + shift_ * 0.5f; }
  float PageX1() const { return cx_ + X(kPageRight) + shift_ * 0.5f; }
  Box RowBox(int i) const {
    const float y = X(kListY + kRow * i);
    return Box{ImVec2(ox_ - X(12), y), ImVec2(ox_ + X(kWidth + 12), y + X(kRow))};
  }
  float ValueRight() const { return ox_ + X(kWidth); }
  float BarX1() const { return ValueRight(); }
  float BarX0() const { return ValueRight() - X(kBar); }
  // Only one tab's name fits on the page: the chevrons either side of it are
  // the boxes for the previous and next tab; other tabs have none.
  Box TabBox(int p) const {
    const float cx = ox_ + X(kWidth * 0.5f);
    const float half = TextW(PageName(page_), kNormal) * 0.5f;
    const float cy = X(kTabsY) + X(kNormal) * 0.5f;
    const int prev = (page_ + kPageCount - 1) % kPageCount, next = (page_ + 1) % kPageCount;
    if (p == prev && p != page_) return Box{ImVec2(cx - half - X(34), cy - X(16)), ImVec2(cx - half - X(4), cy + X(16))};
    if (p == next && p != page_) return Box{ImVec2(cx + half + X(4), cy - X(16)), ImVec2(cx + half + X(34), cy + X(16))};
    return Box{ImVec2(0, 0), ImVec2(0, 0)};
  }

  // --- Drawing helpers ------------------------------------------------------------------
  // Off the page (the toast, the button caps): cream and gold.
  static constexpr ImU32 kCream = IM_COL32(240, 230, 204, 255);
  static constexpr ImU32 kCreamDim = IM_COL32(196, 184, 156, 255);
  static constexpr ImU32 kGold = IM_COL32(218, 182, 98, 255);
  static constexpr ImU32 kShadow = IM_COL32(0, 0, 0, 200);
  // On the paper, the colours of the game's own pages (measured from its
  // Audio and Clothing pages): a warm brown title, muted brown names,
  // near-black values and copper arrows and bars. No shadow.
  static constexpr ImU32 kTitleInk = IM_COL32(86, 46, 22, 255);
  static constexpr ImU32 kInk = IM_COL32(54, 28, 13, 255);       // the selected name
  static constexpr ImU32 kInkDim = IM_COL32(95, 61, 39, 255);    // names
  static constexpr ImU32 kValueInk = IM_COL32(40, 38, 31, 255);  // values
  static constexpr ImU32 kPromptInk = IM_COL32(67, 45, 31, 255);
  static constexpr ImU32 kDescInk = IM_COL32(80, 56, 38, 255);
  static constexpr ImU32 kFaint = IM_COL32(128, 104, 80, 235);
  static constexpr ImU32 kAccent = IM_COL32(176, 98, 58, 255);
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
      if (!paper_)  // the game's pages have no shadow under their ink
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
  // Word-wrapped text; returns the height used. Lines that would end below
  // `bottom` are left out, the last one shown ending in "...".
  float TextWrapped(ImVec2 p, float size, float width, ImU32 col, const std::string& all,
                    float bottom = 1e9f) {
    std::vector<std::string> lines;
    std::string line;
    size_t i = 0;
    while (i <= all.size()) {
      const size_t j = all.find(' ', i);
      const std::string word = all.substr(i, j == std::string::npos ? std::string::npos : j - i);
      const std::string trial = line.empty() ? word : line + " " + word;
      if (!line.empty() && TextW(trial.c_str(), size) > width) {
        lines.push_back(line);
        line = word;
      } else {
        line = trial;
      }
      if (j == std::string::npos) break;
      i = j + 1;
    }
    if (!line.empty()) lines.push_back(line);
    const float step = X(size) * 1.05f;
    float y = p.y;
    for (size_t n = 0; n < lines.size(); ++n) {
      if (y + X(size) > bottom) break;
      std::string text = lines[n];
      if (n + 1 < lines.size() && y + step + X(size) > bottom) {
        while (!text.empty() && TextW((text + "...").c_str(), size) > width) text.pop_back();
        text += "...";
      }
      Text(ImVec2(p.x, y), size, col, text.c_str());
      y += step;
    }
    return y - p.y;
  }

  void Diamond(ImVec2 c, float r, ImU32 col) {
    dl_->AddQuadFilled(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y), col);
  }
  // Gold rule fading out at both ends, a diamond in the middle.
  void Rule(float x0, float x1, float y, float a, ImU32 colour = kGold) {
    const float mid = (x0 + x1) * 0.5f;
    const float h = std::max(1.0f, X(1.2f));
    const ImU32 g = Alpha(colour, 0.85f * a), z = Alpha(colour, 0.0f);
    dl_->AddRectFilledMultiColor(ImVec2(x0, y), ImVec2(mid, y + h), z, g, g, z);
    dl_->AddRectFilledMultiColor(ImVec2(mid, y), ImVec2(x1, y + h), g, z, z, g);
    Diamond(ImVec2(mid, y + h * 0.5f), X(3.5f), g);
  }
  void Chevron(ImVec2 c, float r, bool right, ImU32 col) {
    const float d = right ? 1.0f : -1.0f;
    dl_->AddTriangleFilled(ImVec2(c.x + d * r, c.y), ImVec2(c.x - d * r * 0.6f, c.y - r),
                           ImVec2(c.x - d * r * 0.6f, c.y + r), col);
  }
  // A button prompt as the game's pages have it: the label, then the button
  // (the game's button art when loaded, else a cap with the name). Returns
  // the width.
  float Prompt(float x, float y, const char* button, const char* label, bool measure = false) {
    const float h = X(30);
    const float cy = y + h * 0.5f;
    float lw = 0.0f;
    if (*label) {
      if (!measure) Text(ImVec2(x, cy - X(kSmall) * 0.52f), kSmall, kPromptInk, label);
      lw = TextW(label, kSmall) + X(8);
      x += lw;
    }
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
    return lw + w;
  }

  // --- Drawing -------------------------------------------------------------------------
  void Draw(ImGuiIO& io) {
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    const float fade = std::clamp(float(detail::now_ms() - opened_ms_) / 150.0f, 0.0f, 1.0f);
    cx_ = W * 0.5f;
    // On a wide screen with the HUD at the edges, the game moves its book's
    // right side out by a multiple of the extra width per side (360 * aspect
    // - 640 at 720p; src/core/fable2_fit_window_ui.h): the main menu's frame
    // by 1x, the pause menu's right cover by 1.2x. The page's cover and the
    // paper under it go where the game's are, so only one cover shows.
    {
      const float extra = std::max(0.0f, 360.0f * (W / std::max(H, 1.0f)) - 640.0f);
      const bool main_menu = from_main_menu().load(std::memory_order_relaxed);
      shift_ = REXCVAR_GET(fit_window_hud_to_edges) ? X(extra * (main_menu ? 1.0f : 1.2f)) : 0.0f;
    }
    ox_ = PageX0() + X(kMargin);
    const float px0 = PageX0(), px1 = PageX1();

    // The game's menu on the left page steps back, as it does when one of
    // its own options pages opens.
    dl_->AddRectFilled(ImVec2(0, 0), ImVec2(px0, H), IM_COL32(4, 3, 2, int(90 * fade)));
    // The right-hand page turns to paper: the game's parchment texture,
    // shaded at its edges, between two copper rims as on the game's pages:
    // the outer edge, which curves in towards the middle of the page, and
    // the edge along the cover. Both rims are the frame sheet's page-edge
    // strip, the top half of an edge, upside down for the bottom half; the
    // outer one is drawn rim only, in slices that follow its curve, with the
    // paper starting under it. Without the textures (not in the bank) plain
    // paper stands in.
    paper_ = true;
    const int paper = gameicons::FindExtra(kPaperTexture);
    const int sheet = gameicons::FindExtra(kFrameSheet);
    const ImU32 tint = Alpha(IM_COL32_WHITE, fade);
    const bool textured = gameicons::ExtraReady(paper) && gameicons::ExtraReady(sheet);
    const float rim_left = cx_ + X(562 - 640);   // the outer strip's left end (x 562 at 720p)
    const float paper_x0 = cx_ + X(600 - 640);   // where the paper texture's left edge is laid
    const float under = cx_ + X(1064 - 640) + shift_;  // the paper runs on under the cover
    const float span = under - paper_x0;
    const ImU32 dark = IM_COL32(40, 24, 8, int(90 * fade)), soft = IM_COL32(40, 24, 8, int(45 * fade));
    const ImU32 clear = IM_COL32(40, 24, 8, 0);
    // Slices of 5 rows (at 720p) so the cut follows the rim's curve.
    constexpr float kSlice = 5.0f;
    for (int i = 0; i < int(720.0f / kSlice); ++i) {
      const float vy0 = kSlice * i, vy1 = vy0 + kSlice;  // 720p rows
      const bool bottom = vy0 >= 360.0f;
      const float r0 = bottom ? 720.0f - vy1 : vy0;       // strip rows from its top
      const float y0 = X(vy0), y1 = X(vy1);
      const float e = RimRight(r0 + kSlice * 0.5f);       // the rim's outer side in the strip
      const float sx0 = rim_left + X(e - 8);              // the paper starts under the rim
      if (textured) {
        // Paper: the texture laid from paper_x0 to `under`, top half and
        // the same upside down below, cut to this slice.
        const float ty0 = 512.0f * r0 / 360.0f, ty1 = 512.0f * (r0 + kSlice) / 360.0f;
        gameicons::DrawExtraRegion(dl_, paper, ImVec2(sx0, y0), ImVec2(under, y1),
                                   ImVec2(512.0f * (sx0 - paper_x0) / span, ty0), ImVec2(512.0f, ty1), tint,
                                   bottom);
      } else {
        dl_->AddRectFilled(ImVec2(sx0, y0), ImVec2(under, y1), IM_COL32(222, 208, 176, int(255 * fade)));
      }
      dl_->AddRectFilledMultiColor(ImVec2(sx0, y0), ImVec2(sx0 + X(30), y1), dark, clear, clear, dark);
      // The outer rim, and its shadow on the paper.
      gameicons::DrawExtraRegion(dl_, sheet, ImVec2(rim_left + X(e - 18), y0), ImVec2(rim_left + X(e + 12), y1),
                                 ImVec2(330.0f + e - 18, 152.0f + r0), ImVec2(330.0f + e + 12, 152.0f + r0 + kSlice),
                                 tint, bottom);
    }
    dl_->AddRectFilledMultiColor(ImVec2(px1 + shift_ * 0.5f - X(22), 0), ImVec2(under, H), clear, soft, soft, clear);
    dl_->AddRectFilledMultiColor(ImVec2(rim_left, 0), ImVec2(under, X(22)), soft, soft, clear, clear);
    dl_->AddRectFilledMultiColor(ImVec2(rim_left, H - X(22)), ImVec2(under, H), clear, clear, soft, soft);
    // The rim along the cover (x 986 at 720p).
    {
      const float x0 = cx_ + X(986 - 640) + shift_, x1 = x0 + X(95);
      const ImVec2 t0(330, 152), t1(425, 512);
      gameicons::DrawExtraRegion(dl_, sheet, ImVec2(x0, 0), ImVec2(x1, X(360)), t0, t1, tint, false);
      gameicons::DrawExtraRegion(dl_, sheet, ImVec2(x0, X(360)), ImVec2(x1, X(720)), t0, t1, tint, true);
    }
    // The leather cover back on top of the paper, where the game has it
    // (x 1008-1280 of its 1280x720 screen).
    {
      const float x0 = cx_ + X(1008 - 640) + shift_, x1 = x0 + X(512);  // the art is 512 wide, its right part clear
      gameicons::DrawExtraRect(dl_, gameicons::FindExtra(kCoverTop), ImVec2(x0, 0), ImVec2(x1, X(512)), tint, false,
                               1.0f);
      gameicons::DrawExtraRect(dl_, gameicons::FindExtra(kCoverBottom), ImVec2(x0, X(512)), ImVec2(x1, X(720)), tint,
                               false, 208.0f / 512.0f);
    }

    // Title, centred over the game's flourish as on its own pages.
    {
      const float mid = ox_ + X(kWidth * 0.5f);
      const char* title = "Advanced Options";
      Text(ImVec2(mid - TextW(title, kLarge) * 0.5f, X(kTitleY)), kLarge, Alpha(kTitleInk, fade), title);
      const float fy = X(kTitleY + 30);
      if (!gameicons::DrawExtraRegion(dl_, gameicons::FindExtra(kMotifs), ImVec2(mid - X(170), fy),
                                      ImVec2(mid + X(170), fy + X(27)), ImVec2(22, 2), ImVec2(362, 29),
                                      Alpha(IM_COL32_WHITE, fade), false))
        Rule(ox_ - X(10), ox_ + X(kWidth + 10), X(kTitleY + 42), fade, kAccent);
    }

    // The tab:  LB  <  Display  >  RB
    {
      const float cx = ox_ + X(kWidth * 0.5f);
      const char* name = PageName(page_);
      const float w = TextW(name, kNormal);
      Text(ImVec2(cx - w * 0.5f, X(kTabsY)), kNormal, Alpha(kTitleInk, fade), name);
      dl_->AddRectFilled(ImVec2(cx - w * 0.5f, X(kTabsY + 30)),
                         ImVec2(cx + w * 0.5f, X(kTabsY + 30) + std::max(1.0f, X(1.5f))), Alpha(kAccent, fade));
      const float cy = X(kTabsY) + X(kNormal) * 0.5f;
      Chevron(ImVec2(cx - w * 0.5f - X(16), cy), X(6), false, Alpha(kAccent, fade));
      Chevron(ImVec2(cx + w * 0.5f + X(16), cy), X(6), true, Alpha(kAccent, fade));
      const char* lb = pad_prompts_ ? "LB" : "PgUp";
      const char* rb = pad_prompts_ ? "RB" : "PgDn";
      const float lw = Prompt(0, 0, lb, "", true);
      Prompt(cx - w * 0.5f - X(32) - lw, X(kTabsY - 1), lb, "");
      Prompt(cx + w * 0.5f + X(32), X(kTabsY - 1), rb, "");
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
      if (cur) {
        const ImU32 a = IM_COL32(255, 252, 240, int(110 * fade)), z = IM_COL32(255, 252, 240, 0);
        dl_->AddRectFilledMultiColor(box.Min, box.Max, a, z, z, a);
        dl_->AddRectFilled(box.Min, ImVec2(box.Min.x + std::max(1.0f, X(2)), box.Max.y), Alpha(kAccent, fade));
      }
      const ImU32 col = Alpha(cur ? kInk : kInkDim, fade);
      const std::string val = detail::ValueText(s, values_[i]);
      // The value's width decides whether the name fits at the normal size.
      const float value_w = s.control == Control::kSlider ? X(kBar + 12) + TextW(val.c_str(), kNormal)
                                                          : TextW(val.c_str(), kNormal) + X(30);
      // Name and value never meet: the name drops to the small font, then
      // the value, then the name is shortened.
      const float room = X(kWidth) - X(4) - X(8);
      float label_size = kNormal, value_size = kNormal;
      const auto value_width = [&](float size) {
        return s.control == Control::kSlider ? X(kBar + 12) + TextW(val.c_str(), size)
                                             : TextW(val.c_str(), size) + X(30);
      };
      if (TextW(s.label, kNormal) + value_width(kNormal) > room) label_size = kSmall;
      if (TextW(s.label, kSmall) + value_width(kNormal) > room) value_size = kSmall;
      std::string label = s.label;
      while (label.size() > 4 && TextW(label.c_str(), label_size) + value_width(value_size) > room)
        label = label.substr(0, label.size() - 4) + "...";
      Text(ImVec2(ox_ + X(4), cy - X(label_size) * 0.52f), label_size, col, label.c_str());
      // Restart marker: copper once changed since the game started.
      if (s.when == When::kRestart) {
        const bool pending = values_[i] != at_launch_[i];
        const float lx = ox_ + X(4) + TextW(s.label, label_size) + X(10);
        Text(ImVec2(lx, cy - X(kSmall) * 0.5f), kSmall, Alpha(pending ? kAccent : kFaint, fade * (pending ? 1.0f : 0.8f)),
             pending ? "Restart to apply" : "Restart");
      }
      switch (s.control) {
        case Control::kToggle:
        case Control::kChoice: {
          const float right = cur ? ValueRight() - X(14) : ValueRight();
          TextRight(right, cy - X(value_size) * 0.52f, value_size, Alpha(kValueInk, fade), val.c_str());
          if (cur) {
            Chevron(ImVec2(right - TextW(val.c_str(), value_size) - X(11), cy), X(5), false, Alpha(kAccent, fade));
            Chevron(ImVec2(ValueRight() - X(4), cy), X(5), true, Alpha(kAccent, fade));
          }
          break;
        }
        case Control::kSlider: {
          const float t = float(std::clamp((values_[i].Number() - s.lo) / (s.hi - s.lo), 0.0, 1.0));
          const float bh = std::max(2.0f, X(3));
          const float x0 = BarX0(), x1 = BarX1();
          dl_->AddRectFilled(ImVec2(x0, cy - bh * 0.5f), ImVec2(x1, cy + bh * 0.5f),
                             IM_COL32(60, 40, 20, int(60 * fade)), bh * 0.5f);
          dl_->AddRectFilled(ImVec2(x0, cy - bh * 0.5f), ImVec2(x0 + (x1 - x0) * t, cy + bh * 0.5f),
                             Alpha(cur ? kAccent : kInkDim, fade * (cur ? 1.0f : 0.7f)), bh * 0.5f);
          Diamond(ImVec2(x0 + (x1 - x0) * t, cy), X(cur ? 6.0f : 4.5f), Alpha(cur ? kAccent : kInkDim, fade));
          TextRight(x0 - X(12), cy - X(value_size) * 0.52f, value_size, Alpha(kValueInk, fade), val.c_str());
          break;
        }
      }
    }
    // More lines above / below.
    if (scroll_ > 0) Chevron(ImVec2(ox_ + X(kWidth * 0.5f), X(kListY - 6)), X(5), false, Alpha(kAccent, fade));
    if (scroll_ + shown < int(rows.size()))
      Chevron(ImVec2(ox_ + X(kWidth * 0.5f), X(kListY + kRow * shown + 6)), X(5), true, Alpha(kAccent, fade));

    // Description of the selected line, and when a change takes effect.
    float y = X(kListY + kRow * VisibleRows() + 14);
    Rule(ox_ - X(10), ox_ + X(kWidth + 10), y, 0.5f * fade, kAccent);
    y += X(10);
    if (!rows.empty()) {
      const int i = rows[std::clamp(sel_, 0, int(rows.size()) - 1)];
      const Setting& s = list_[i];
      // Nothing runs into the prompts (or the restart line above them).
      const float bottom = H - X(88);
      y += TextWrapped(ImVec2(ox_, y), kSmall, X(kWidth), Alpha(kDescInk, fade), s.desc, bottom);
      std::string when;
      if (s.when == When::kRestart) when = "Takes effect the next time the game starts.";
      else if (s.when == When::kNextLoad) when = "Takes effect from the next save load.";
      else if (s.when == When::kAtStart) when = "Used when the game starts: the videos only play then.";
      else when = "Takes effect at once.";
      if (s.has_recommended) when = "Recommended: " + detail::PlainValueText(s, s.recommended) + ". " + when;
      if (i < int(pinned_.size()) && pinned_[i])
        when += " fable_2.toml or the command line also sets this, and wins when the game starts.";
      y += X(2);
      TextWrapped(ImVec2(ox_, y), kSmall, X(kWidth), Alpha(kFaint, fade), when, bottom);
    }

    // Restart reminder and file problems, above the prompts.
    std::string pending;
    for (size_t i = 0; i < list_.size(); ++i)
      if (list_[i].when == When::kRestart && values_[i] != at_launch_[i])
        pending += std::string(pending.empty() ? "" : ", ") + list_[i].label;
    const float by = H - X(84);
    if (!file_error_.empty())
      TextWrapped(ImVec2(ox_, by - X(26)), kSmall, X(kWidth), Alpha(IM_COL32(150, 30, 14, 255), fade), file_error_);
    else if (!pending.empty())
      TextWrapped(ImVec2(ox_, by - X(26)), kSmall, X(kWidth), Alpha(kAccent, fade), "Restart to apply: " + pending);

    DrawPrompts(io, fade);
    paper_ = false;
  }

  void DrawPrompts(ImGuiIO& io, float fade) {
    const float H = io.DisplaySize.y;
    struct P {
      const char* button;
      const char* label;
    };
    std::vector<P> list;
    const std::vector<int> rows = Rows();
    const bool slider =
        !rows.empty() && list_[rows[std::clamp(sel_, 0, int(rows.size()) - 1)]].control == Control::kSlider;
    (void)slider;  // left / right change every kind of line
    if (pad_prompts_) {
      list = {{"Y", "Default"}, {"B", "Cancel"}, {"A", "Accept"}};
    } else {
      list = {{"Delete", "Default"}, {"Esc", "Cancel"}, {"Enter", "Accept"}};
    }
    // Along the bottom of the page, right-aligned.
    const float gap = X(20);
    float total = 0;
    for (const P& p : list) total += Prompt(0, 0, p.button, p.label, true) + gap;
    float x = ox_ + X(kWidth) - (total - gap);
    const float y = H - X(52);
    for (const P& p : list) x += Prompt(x, y, p.button, p.label) + gap;
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
