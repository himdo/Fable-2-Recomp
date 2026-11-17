// fable_2 - The Advanced Options page.
//
// Opened from the "Advanced Options" item the recomp adds to the main menu
// (src/menu/main_menu.cpp) and from the "Advanced" row it adds to the pause
// menu's Options (src/core/port_settings_lua.h). Lists the fable2_config.toml settings
// from src/core/port_settings_schema.h on pages (Display, Frame Rate, Mouse,
// Fixes, Photo Mode) and lets the player change them.
//
// Drawn like the game's own options pages (Game, Video, Audio), not as a
// desktop window: a sheet of the book's framed parchment on the right with the
// menu stepping back on the left, the tab's title over the gold ornament,
// each setting's name centred with its value between copper arrows, a lighter
// band on the selected value, and the prompts at the bottom right, name
// first. The game's own typeface (src/core/photo_mode_font.h), art and button
// icons (src/core/photo_mode_icons.h, kArtNames below), all straight into
// ImGui's foreground draw list, with no ImGui windows or widgets.
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

// The game's own art the page is drawn with (gui_textures.bnk, loaded by
// Fable2App with the photo mode icons): the framed parchment (top and bottom
// halves), and the ornament and slider pieces.
inline constexpr const char* kArtPaperTop = "art\\gui\\frames\\papera.tex";
inline constexpr const char* kArtPaperBottom = "art\\gui\\frames\\paperb.tex";
inline constexpr const char* kArtMotifs = "art\\gui\\frames\\motifs.tex";
inline constexpr const char* kArtSlider = "art\\gui\\frames\\sliderframe.tex";
inline constexpr const char* kArtNames[] = {kArtPaperTop, kArtPaperBottom, kArtMotifs, kArtSlider};

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
  float cx_ = 0.0f;  // screen centre, in pixels

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
      if (TitleArrow(false).Contains(m)) {
        SetPage(page_ - 1);
        return;
      }
      if (TitleArrow(true).Contains(m)) {
        SetPage(page_ + 1);
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
          if (m.x >= BarX0(r - scroll_) - X(10) && m.x <= BarX1(r - scroll_) + X(10)) drag_row_ = r;
        } else {
          Step(rows[r], m.x < PageCX() ? -1 : 1);  // the left / right arrow side
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
      const int vis = drag_row_ - scroll_;
      const float t = std::clamp((m.x - BarX0(vis)) / (BarX1(vis) - BarX0(vis)), 0.0f, 1.0f);
      const double v = s.lo + (s.hi - s.lo) * t;
      Set(rows[drag_row_], s.def.type == Type::kInt ? Int(int64_t(std::llround(v))) : Float(v));
    }
    // Keep the selection on screen.
    if (sel_ < scroll_) scroll_ = sel_;
    if (sel_ >= scroll_ + shown) scroll_ = sel_ - shown + 1;
  }

  // --- Layout ------------------------------------------------------------------------
  // The page is a sheet of the book's parchment on the right-hand side, laid
  // out like the game's own options pages (Game, Video, Audio): a centred
  // title over the gold ornament, each setting's name centred with its value
  // under it between two copper arrows, a lighter band behind the selected
  // value, and the button prompts at the bottom right, name first. Units are
  // the game's 720p canvas, the sheet placed where the book's right page is.
  struct Box {
    ImVec2 Min, Max;
    bool Contains(ImVec2 p) const { return p.x >= Min.x && p.y >= Min.y && p.x < Max.x && p.y < Max.y; }
  };
  static constexpr float kLarge = 32.0f;  // the game's three menu font sizes
  static constexpr float kNormal = 27.0f;
  static constexpr float kSmall = 24.0f;
  static constexpr float kPageLeft = -14.0f;   // sheet edges from the screen centre
  static constexpr float kPageRight = 396.0f;
  static constexpr float kCaptionY = 40.0f;
  static constexpr float kTitleY = 66.0f;
  static constexpr float kOrnamentY = 106.0f;
  static constexpr float kListY = 134.0f;
  static constexpr float kRow = 80.0f;
  static constexpr float kArrowX = 150.0f;  // value arrows from the page centre
  static constexpr int kMaxRows = 4;  // like the game's own options pages
  static constexpr float kDescY = 462.0f;

  float X(float v) const { return v * k_; }
  float PageX0() const { return cx_ + X(kPageLeft); }
  float PageX1() const { return cx_ + X(kPageRight); }
  float PageCX() const { return (PageX0() + PageX1()) * 0.5f; }
  int VisibleRows() const { return kMaxRows; }
  float RowY(int i) const { return X(kListY + kRow * i); }
  Box RowBox(int i) const {
    return Box{ImVec2(PageCX() - X(180), RowY(i)), ImVec2(PageCX() + X(180), RowY(i) + X(kRow))};
  }
  float ValueCY(int i) const { return RowY(i) + X(53); }
  float BarX0(int) const { return PageCX() - X(130); }
  float BarX1(int) const { return PageCX() + X(86); }
  Box TitleArrow(bool right) const {
    const float w = TextW(PageName(page_), kLarge);
    const float x = PageCX() + (right ? 1.0f : -1.0f) * (w * 0.5f + X(26));
    const float cy = X(kTitleY) + X(kLarge) * 0.5f;
    return Box{ImVec2(x - X(16), cy - X(16)), ImVec2(x + X(16), cy + X(16))};
  }

  // --- Colours: ink on parchment ------------------------------------------------------
  static constexpr ImU32 kInk = IM_COL32(72, 50, 30, 255);
  static constexpr ImU32 kInkSoft = IM_COL32(104, 80, 58, 255);
  static constexpr ImU32 kInkFaint = IM_COL32(128, 104, 80, 220);
  static constexpr ImU32 kRust = IM_COL32(150, 64, 34, 255);  // restart reminders
  static constexpr ImU32 kCream = IM_COL32(240, 230, 204, 255);
  static constexpr ImU32 kParchment = IM_COL32(222, 210, 186, 255);  // when the art is missing
  static ImU32 Alpha(ImU32 c, float a) {
    const int na = std::clamp(int(((c >> IM_COL32_A_SHIFT) & 0xFF) * a), 0, 255);
    return (c & ~IM_COL32_A_MASK) | (ImU32(na) << IM_COL32_A_SHIFT);
  }

  // --- The game's art (gui_textures.bnk, loaded by Fable2App) -------------------------------
  // Draws the pixel rect [sx0,sy0]-[sx1,sy1] of extra image `name` into
  // [p0,p1]. False when the image is not loaded (the caller draws a stand-in).
  bool Art(const char* name, float sx0, float sy0, float sx1, float sy1, ImVec2 p0, ImVec2 p1, ImU32 col) {
    const int i = gameicons::FindExtra(name);
    if (!gameicons::ExtraReady(i)) return false;
    const auto& img = gameicons::extras()[i].img;
    ImFontAtlasRect r;
    if (!img.atlas || !img.atlas->GetCustomRect(img.rect_id, &r)) return false;
    const float du = (r.uv1.x - r.uv0.x) / float(img.w), dv = (r.uv1.y - r.uv0.y) / float(img.h);
    dl_->AddImage(dl_->_CmdHeader.TexRef, p0, p1, ImVec2(r.uv0.x + sx0 * du, r.uv0.y + sy0 * dv),
                  ImVec2(r.uv0.x + sx1 * du, r.uv0.y + sy1 * dv), col);
    return true;
  }
  // The game's copper arrow (its art only comes pointing up, and the runtime
  // has no rotated image call, so it is drawn): a dark rim, the copper face
  // and a lighter top facet. dir: 0 left, 1 right, 2 up, 3 down.
  void Arrow(ImVec2 c, float r, int dir, float alpha) {
    auto tri = [&](float rr, ImVec2 o, ImU32 col) {
      ImVec2 p[3];
      switch (dir) {
        case 0: p[0] = {c.x - rr, c.y}; p[1] = {c.x + rr * 0.7f, c.y - rr}; p[2] = {c.x + rr * 0.7f, c.y + rr}; break;
        case 1: p[0] = {c.x + rr, c.y}; p[1] = {c.x - rr * 0.7f, c.y + rr}; p[2] = {c.x - rr * 0.7f, c.y - rr}; break;
        case 2: p[0] = {c.x, c.y - rr}; p[1] = {c.x + rr, c.y + rr * 0.7f}; p[2] = {c.x - rr, c.y + rr * 0.7f}; break;
        default: p[0] = {c.x, c.y + rr}; p[1] = {c.x - rr, c.y - rr * 0.7f}; p[2] = {c.x + rr, c.y - rr * 0.7f}; break;
      }
      dl_->AddTriangleFilled(ImVec2(p[0].x + o.x, p[0].y + o.y), ImVec2(p[1].x + o.x, p[1].y + o.y),
                             ImVec2(p[2].x + o.x, p[2].y + o.y), col);
    };
    tri(r * 1.18f, ImVec2(X(1), X(1.5f)), Alpha(IM_COL32(40, 22, 12, 200), alpha));  // shadow / rim
    tri(r, ImVec2(0, 0), Alpha(IM_COL32(184, 104, 62, 255), alpha));                  // copper
    tri(r * 0.55f, ImVec2(0, -r * 0.12f), Alpha(IM_COL32(226, 160, 112, 255), alpha)); // highlight
  }

  // --- Text ------------------------------------------------------------------------
  float TextW(const char* t, float size) const {
    if (gamefont::AnyReady()) return gamefont::Measure(t, X(size), size);
    return float(std::strlen(t)) * 7.0f;  // default ImGui font fallback
  }
  void Text(ImVec2 p, float size, ImU32 col, const char* t) {
    if (gamefont::AnyReady()) gamefont::Draw(dl_, p, X(size), size, col, t);
    else dl_->AddText(p, col, t);
  }
  void TextCentred(float cx, float y, float size, ImU32 col, const char* t) {
    Text(ImVec2(cx - TextW(t, size) * 0.5f, y), size, col, t);
  }
  // Word-wrapped and centred; at most `max_lines`. Returns the height used.
  float TextWrapped(float cx, float y0, float size, float width, ImU32 col, const std::string& all,
                    int max_lines = 3) {
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
    float y = y0;
    for (int n = 0; n < int(lines.size()) && n < max_lines; ++n) {
      TextCentred(cx, y, size, col, lines[n].c_str());
      y += X(size) * 1.02f;
    }
    return y - y0;
  }

  // A prompt the way the game's pages show them: the name, then the button.
  // The game's button art when loaded, else a key cap. Returns the width.
  float Prompt(float right, float y, const char* button, const char* label, ImU32 ink, bool measure = false) {
    const float h = X(28);
    const float cy = y + h * 0.5f;
    const int icon = gameicons::Find(button);
    float cap = h;
    if (!gameicons::Ready(icon)) cap = std::max(h, TextW(button, kSmall) * 0.8f + X(14));
    const float tw = TextW(label, kSmall);
    const float w = tw + X(8) + cap;
    if (measure) return w;
    const float x = right - w;
    Text(ImVec2(x, cy - X(kSmall) * 0.52f), kSmall, ink, label);
    const float bx = x + tw + X(8);
    if (gameicons::Ready(icon)) {
      const float s = h * 1.25f;
      gameicons::Draw(dl_, icon, ImVec2(bx - (s - h) * 0.5f, cy - s * 0.5f), s);
    } else {
      dl_->AddRectFilled(ImVec2(bx, y), ImVec2(bx + cap, y + h), IM_COL32(28, 22, 16, 235), X(5));
      const float bw = TextW(button, kSmall) * 0.8f;
      if (gamefont::AnyReady())
        gamefont::Draw(dl_, ImVec2(bx + (cap - bw) * 0.5f, cy - X(kSmall) * 0.8f * 0.52f), X(kSmall) * 0.8f, kSmall,
                       kCream, button);
      else
        dl_->AddText(ImVec2(bx + 4, y + 2), kCream, button);
    }
    return w;
  }

  // --- Drawing -------------------------------------------------------------------------
  void Draw(ImGuiIO& io) {
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    const float fade = std::clamp(float(detail::now_ms() - opened_ms_) / 160.0f, 0.0f, 1.0f);
    cx_ = W * 0.5f;
    const float x0 = PageX0(), x1 = PageX1(), pcx = PageCX();

    // The menu on the left steps back while the page is open, as the game's
    // own menu does when one of its options pages opens.
    dl_->AddRectFilled(ImVec2(0, 0), ImVec2(x0 + X(10), H), IM_COL32(8, 6, 4, int(120 * fade)));

    // The sheet: the two halves of the game's framed parchment.
    const ImU32 white = Alpha(IM_COL32_WHITE, fade);
    const float mid = H * 0.5f;
    const bool paper = Art(kArtPaperTop, 0, 0, 512, 512, ImVec2(x0, 0), ImVec2(x1, mid), white) &&
                       Art(kArtPaperBottom, 0, 0, 512, 512, ImVec2(x0, mid), ImVec2(x1, H), white);
    if (!paper) {
      dl_->AddRectFilled(ImVec2(x0, 0), ImVec2(x1, H), Alpha(kParchment, fade));
      dl_->AddRect(ImVec2(x0 + X(6), X(6)), ImVec2(x1 - X(6), H - X(6)), Alpha(IM_COL32(110, 70, 40, 255), fade), 0,
                   0, std::max(1.0f, X(3)));
    }

    // Caption, the tab's title between arrows, and the gold ornament.
    TextCentred(pcx, X(kCaptionY), kSmall, Alpha(kInkFaint, fade), "Advanced Options");
    TextCentred(pcx, X(kTitleY), kLarge, Alpha(kInk, fade), PageName(page_));
    for (bool right : {false, true}) {
      const Box b = TitleArrow(right);
      const ImVec2 c((b.Min.x + b.Max.x) * 0.5f, (b.Min.y + b.Max.y) * 0.5f);
      Arrow(c, X(10), right ? 1 : 0, fade);
      // LB / RB (or Page Up / Down) beside the arrows.
      const char* key = pad_prompts_ ? (right ? "RB" : "LB") : (right ? "PgDn" : "PgUp");
      const int icon = gameicons::Find(key);
      const float s = X(30);
      const float kx = right ? c.x + X(16) : c.x - X(16) - s;
      if (gameicons::Ready(icon)) {
        gameicons::Draw(dl_, icon, ImVec2(kx, c.y - s * 0.5f), s, white);
      } else {
        const float tw = TextW(key, kSmall) * 0.7f;
        const float bx = right ? c.x + X(16) : c.x - X(16) - tw - X(10);
        dl_->AddRectFilled(ImVec2(bx, c.y - X(11)), ImVec2(bx + tw + X(10), c.y + X(11)),
                           Alpha(IM_COL32(28, 22, 16, 220), fade), X(4));
        if (gamefont::AnyReady())
          gamefont::Draw(dl_, ImVec2(bx + X(5), c.y - X(kSmall) * 0.7f * 0.52f), X(kSmall) * 0.7f, kSmall,
                         Alpha(kCream, fade), key);
      }
    }
    if (!Art(kArtMotifs, 22, 8, 362, 29, ImVec2(pcx - X(165), X(kOrnamentY)), ImVec2(pcx + X(165), X(kOrnamentY + 20)),
             white))
      dl_->AddRectFilled(ImVec2(pcx - X(150), X(kOrnamentY + 9)), ImVec2(pcx + X(150), X(kOrnamentY + 11)),
                         Alpha(IM_COL32(170, 130, 60, 255), fade));

    // The settings: name, then the value between the arrows.
    const std::vector<int> rows = Rows();
    const int shown = std::min(int(rows.size()), VisibleRows());
    for (int r = scroll_; r < scroll_ + shown && r < int(rows.size()); ++r) {
      const int i = rows[r];
      const Setting& s = list_[i];
      const int vis = r - scroll_;
      const bool cur = r == sel_;
      const float y = RowY(vis);
      const float vcy = ValueCY(vis);

      // Name, with the restart note under it in small print when it has one.
      std::string label = s.label;
      TextCentred(pcx, y + X(4), kNormal, Alpha(cur ? kInk : kInkSoft, fade), label.c_str());
      if (s.when == When::kRestart) {
        const bool pending = values_[i] != at_launch_[i];
        const char* note = pending ? "restart to apply" : "restart";
        const float nw = TextW(note, kSmall) * 0.72f;
        const float lw = TextW(label.c_str(), kNormal);
        if (gamefont::AnyReady())
          gamefont::Draw(dl_, ImVec2(pcx + lw * 0.5f + X(8), y + X(10)), X(kSmall) * 0.72f, kSmall,
                         Alpha(pending ? kRust : kInkFaint, fade), note);
        (void)nw;
      }

      // The selected value sits on a lighter band, as in the game.
      if (cur) {
        const ImU32 band = IM_COL32(255, 252, 240, int(120 * fade)), clear = IM_COL32(255, 252, 240, 0);
        const float bx0 = pcx - X(175), bx1 = pcx + X(175), by0 = vcy - X(17), by1 = vcy + X(17);
        dl_->AddRectFilledMultiColor(ImVec2(bx0, by0), ImVec2(pcx, by1), clear, band, band, clear);
        dl_->AddRectFilledMultiColor(ImVec2(pcx, by0), ImVec2(bx1, by1), band, clear, clear, band);
      }
      const std::string val = detail::ValueText(s, values_[i]);
      if (s.control == Control::kSlider) {
        const float t = float(std::clamp((values_[i].Number() - s.lo) / (s.hi - s.lo), 0.0, 1.0));
        const float b0 = BarX0(vis), b1 = BarX1(vis);
        if (!Art(kArtSlider, 7, 108, 495, 122, ImVec2(b0, vcy - X(5)), ImVec2(b1, vcy + X(5)), white))
          dl_->AddRectFilled(ImVec2(b0, vcy - X(2)), ImVec2(b1, vcy + X(2)), Alpha(kInkSoft, fade), X(2));
        const float kx = b0 + (b1 - b0) * t;
        dl_->AddRectFilled(ImVec2(kx - X(4), vcy - X(9)), ImVec2(kx + X(4), vcy + X(9)),
                           Alpha(IM_COL32(196, 118, 72, 255), fade), X(2));
        dl_->AddRect(ImVec2(kx - X(4), vcy - X(9)), ImVec2(kx + X(4), vcy + X(9)), Alpha(IM_COL32(90, 50, 28, 255), fade),
                     X(2), 0, std::max(1.0f, X(1)));
        Text(ImVec2(b1 + X(14), vcy - X(kSmall) * 0.52f), kSmall, Alpha(cur ? kInk : kInkSoft, fade), val.c_str());
      } else {
        TextCentred(pcx, vcy - X(kNormal) * 0.52f, kNormal, Alpha(cur ? kInk : kInkSoft, fade), val.c_str());
        Arrow(ImVec2(pcx - X(kArrowX), vcy), X(cur ? 11 : 9), 0, fade * (cur ? 1.0f : 0.55f));
        Arrow(ImVec2(pcx + X(kArrowX), vcy), X(cur ? 11 : 9), 1, fade * (cur ? 1.0f : 0.55f));
      }
    }
    // More settings above / below.
    if (scroll_ > 0) Arrow(ImVec2(pcx, X(kListY - 6)), X(7), 2, fade);
    if (scroll_ + shown < int(rows.size())) Arrow(ImVec2(pcx, X(kListY + kRow * shown + 2)), X(7), 3, fade);

    // The selected setting's description and when a change takes effect.
    float y = X(kDescY);
    const float wrap = (x1 - x0) - X(80);
    if (!rows.empty()) {
      const int i = rows[std::clamp(sel_, 0, int(rows.size()) - 1)];
      const Setting& s = list_[i];
      std::string when = s.when == When::kRestart    ? "Takes effect the next time the game starts."
                         : s.when == When::kNextLoad ? "Takes effect when a save is loaded."
                                                     : "Takes effect at once.";
      if (i < int(pinned_.size()) && pinned_[i]) when += " fable_2.toml or the command line also sets this.";
      y += TextWrapped(pcx, y, kSmall, wrap, Alpha(kInkSoft, fade), s.desc, 3);
      y += TextWrapped(pcx, y, kSmall, wrap, Alpha(kInkFaint, fade), when, 1);
    }
    std::string pending;
    for (size_t i = 0; i < list_.size(); ++i)
      if (list_[i].when == When::kRestart && values_[i] != at_launch_[i])
        pending += std::string(pending.empty() ? "" : ", ") + list_[i].label;
    if (!file_error_.empty())
      TextWrapped(pcx, y + X(4), kSmall, wrap, Alpha(kRust, fade), file_error_, 1);
    else if (!pending.empty())
      TextWrapped(pcx, y + X(4), kSmall, wrap, Alpha(kRust, fade), "Restart to apply: " + pending, 1);

    DrawPrompts(io, fade);
  }

  void DrawPrompts(ImGuiIO& io, float fade) {
    const float H = io.DisplaySize.y;
    struct P { const char* button; const char* label; };
    std::vector<P> list;
    const std::vector<int> rows = Rows();
    const bool slider = !rows.empty() && list_[rows[std::clamp(sel_, 0, int(rows.size()) - 1)]].control == Control::kSlider;
    if (pad_prompts_) {
      list = {{"B", "Back"}, {"Y", "Default"}, slider ? P{"D-Pad", "Adjust"} : P{"A", "Change"}};
    } else {
      list = {{"Esc", "Back"}, {"Delete", "Default"}, slider ? P{"Arrows", "Adjust"} : P{"Enter", "Change"}};
    }
    // Stacked at the bottom right of the sheet, like Cancel / Accept.
    const float right = PageX1() - X(40);
    float y = H - X(34) - X(30) * float(list.size());
    for (const P& p : list) {
      Prompt(right, y, p.button, p.label, Alpha(kInk, fade));
      y += X(30);
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
    const float y = X(46);
    // Soft dark backing so it reads over any picture.
    dl_->AddRectFilled(ImVec2(x - X(16), y - X(8)), ImVec2(W - X(30), y + X(38)), IM_COL32(0, 0, 0, int(120 * a)), X(6));
    Text(ImVec2(x, y), kNormal, Alpha(kCream, a), toast_.c_str());
  }
};

}  // namespace fable2::portsettings
