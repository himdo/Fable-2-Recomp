// fable_2 - The Deck Recomp settings page.
//
// Opened from Options > Deck Recomp Settings in the pause menu (the row is added by
// src/core/fable2_deck_menu.h, the A press caught in src/menu/main_menu.cpp).
// Lists the settings of fable2::deckmenu::Settings() on tabs and lets the
// player change them.
//
// Drawn like the game's own options pages, not as a desktop window: the
// game's GUI typeface (src/core/photo_mode_font.h) in the ink colours of the
// game's pages, on its paper between its page rims, with its title flourish
// and controller button art (src/core/photo_mode_icons.h), all straight into
// ImGui's foreground draw list, with no ImGui windows or widgets. The page
// slides out from under the book's right cover as the game's pages do.
//
// Controls, as on the game's own options pages: D-pad / left stick up and
// down to move, left and right to change a setting, Y: default, LB / RB: tab,
// A: accept (saves), B or Start: cancel (puts back). The controller is read
// from the game's own pad reads, which are blanked for the game while the
// page is open (src/core/deck_settings_state.h).
//
// Changes apply at once where they can (fable2::deckmenu::When). Accept
// writes them to fable2_config.toml; cancel puts them all back.
//
// The page's look, layout and input handling are ported from himdo/
// Fable-2-Recomp PR #213 (Advanced Options) by Locme.
#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <imgui.h>
#include <rex/cvar.h>
#include <rex/logging/macros.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context.h>

#include "deck_settings_state.h"
#include "fable2_config.h"
#include "fable2_deck_menu.h"
#include "fable2_fit_window.h"
#include "photo_mode_font.h"
#include "photo_mode_icons.h"
#include "photo_mode_state.h"

REXCVAR_DECLARE(bool, fit_window_hud_to_edges);

namespace fable2::decksettings {

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

// Index of `value` among the setting's choices; -1 if it is none of them.
inline int ChoiceIndex(const deckmenu::Setting& s, const std::string& value) {
  for (int i = 0; i < int(s.choices.size()); ++i)
    if (value == s.choices[i].value) return i;
  return -1;
}

}  // namespace detail

class DeckSettingsPage final : public rex::ui::ImGuiDialog {
 public:
  explicit DeckSettingsPage(rex::ui::ImGuiDrawer* drawer) : rex::ui::ImGuiDialog(drawer) {
#ifndef _WIN32
    // A registered dialog makes the presenter repaint the window at the
    // display refresh rate instead of presenting once per game frame (frame
    // rate overlays then show 90 on the Steam Deck OLED). Stay unregistered
    // until the page is chosen in Options; OnDraw unregisters again once it
    // has closed. (Photo mode's panel does the same.)
    imgui_drawer()->RemoveDialog(this);
    instance_.store(this, std::memory_order_release);
    open_hook().store(&OnOpenRequested, std::memory_order_release);
#endif
  }
#ifndef _WIN32
  ~DeckSettingsPage() override {
    open_hook().store(nullptr, std::memory_order_release);
    instance_.store(nullptr, std::memory_order_release);
  }
#endif

 protected:
  void OnDraw(ImGuiIO& io) override {
    dl_ = ImGui::GetForegroundDrawList();
    k_ = std::max(0.5f, io.DisplaySize.y / 720.0f);
    if (open_request().exchange(false)) {
      if (!is_open().load() && !photomode::active()) Open();
    }
    if (is_open().load() && photomode::active() && closing_ms_ == 0) Close(false);  // photo mode took over
    // The closing slide: the page is still up (the game still sees no
    // input) until it is back under the cover.
    if (closing_ms_ != 0 && float(detail::now_ms() - closing_ms_) >= kSlideMs) {
      closing_ms_ = 0;
      hold_until_released().store(true);
      is_open().store(false);
    }
    if (!is_open().load()) {
      DrawToast(io);
      Unregister();
      return;
    }
    if (closing_ms_ == 0) HandleInput();
    if (!is_open().load()) {  // closed by this frame's input
      DrawToast(io);
      return;
    }
    Draw(io);
  }

 private:
#ifndef _WIN32
  // Game thread: the page was chosen in Options - register it on the UI
  // thread so it is drawn.
  static void OnOpenRequested() {
    DeckSettingsPage* page = instance_.load(std::memory_order_acquire);
    if (!page) return;
    rex::ui::Window* window = page->imgui_drawer()->window();
    if (!window) return;
    window->app_context().CallInUIThreadDeferred([] {
      DeckSettingsPage* p = instance_.load(std::memory_order_acquire);
      if (p && !p->registered_) {
        p->registered_ = true;
        p->imgui_drawer()->AddDialog(p);
      }
    });
  }
  static inline std::atomic<DeckSettingsPage*> instance_{nullptr};
  bool registered_ = false;  // UI thread only.
#endif
  // Closed and the "Settings Saved" note gone: stop the continuous repaints.
  void Unregister() {
#ifndef _WIN32
    if (registered_ && toast_.empty() && !open_request().load()) {
      registered_ = false;
      imgui_drawer()->RemoveDialog(this);
    }
#endif
  }

  // --- Model --------------------------------------------------------------------------
  // Each value is the setting's value text as in fable2_config.toml.
  const std::vector<deckmenu::Setting>& list_ = deckmenu::Settings();
  std::vector<std::string> values_;   // as shown (changes applied)
  std::vector<std::string> in_file_;  // as saved when the page opened
  std::vector<std::string> saved_;    // what the file holds (kept across visits)

  int tab_ = 0;
  int sel_ = 0;  // row on the tab
  int64_t opened_ms_ = 0;

  // Input.
  uint16_t prev_buttons_ = 0;
  photomode::Repeater rep_up_, rep_down_, rep_left_, rep_right_;

  // Toast after closing.
  std::string toast_;
  int64_t toast_ms_ = 0;

  // Drawing.
  ImDrawList* dl_ = nullptr;
  float k_ = 1.0f;      // pixels per 720p unit
  float ox_ = 0.0f;     // left edge of the text column, in pixels
  float cx_ = 0.0f;     // screen centre (the book's spine), in pixels
  float shift_ = 0.0f;  // how far the game moved its right cover on a wide screen, in pixels
  float po_ = 0.0f;     // the page's offset: shift_ plus the slide, in pixels
  int64_t closing_ms_ = 0;  // when the closing slide started, 0 when not closing
  // The game's options pages slide out from under the right cover in about
  // 0.4 s, easing in and out, and slide back under it when they close.
  static constexpr float kSlideMs = 380.0f;
  bool paper_ = false;  // drawing on the paper (no shadow under the ink)

  std::vector<int> Rows() const {
    std::vector<int> r;
    for (int i = 0; i < int(list_.size()); ++i)
      if (list_[i].tab == tab_) r.push_back(i);
    return r;
  }

  void Open() {
    if (saved_.empty()) {
      for (const deckmenu::Setting& s : list_) saved_.push_back(s.current());
      deckmenu::StartupValues();  // as the game started, for "Restart to apply"
    }
    values_ = saved_;
    in_file_ = saved_;
    tab_ = 0;
    sel_ = 0;
    opened_ms_ = detail::now_ms();
    closing_ms_ = 0;
    // Whatever is held now (the A that chose the row) is not a press.
    prev_buttons_ = merged_pad().buttons;
    rep_up_ = rep_down_ = rep_left_ = rep_right_ = photomode::Repeater{};
    is_open().store(true);
    REXSYS_INFO("[deck-menu] page opened ({} settings)", list_.size());
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
      REXSYS_INFO("[deck-menu] cancelled, {} change(s) put back", undone);
      StartClosing();
      return;
    }
    int changed = 0;
    bool ok = true;
    for (size_t i = 0; i < list_.size(); ++i) {
      if (values_[i] == in_file_[i]) continue;
      ++changed;
      const bool saved = deckmenu::Save(list_[i], values_[i]);
      ok = ok && saved;
      if (saved) saved_[i] = values_[i];
      REXSYS_INFO("[deck-menu] {} = {} ({})", list_[i].key, values_[i], saved ? "saved" : "NOT saved");
    }
    if (changed) {
      toast_ = ok ? "Settings Saved" : "Settings Not Saved";
      toast_ms_ = detail::now_ms();
    }
    StartClosing();
  }

  // The page slides back under the cover; OnDraw closes it when it is in.
  void StartClosing() {
    // Closing mid-slide starts from where the page is.
    const float in = std::clamp(float(detail::now_ms() - opened_ms_) / kSlideMs, 0.0f, 1.0f);
    closing_ms_ = detail::now_ms() - int64_t((1.0f - in) * kSlideMs);
  }

  // Shows a value and, unless the setting waits for a restart, gives it to
  // the game.
  void Set(int i, const std::string& v) {
    if (v == values_[i]) return;
    values_[i] = v;
    const deckmenu::Setting& s = list_[i];
    if (s.when == deckmenu::When::kRestart) return;
    if (s.set) s.set(fable2::config::Mutable(), v);
    if (s.live) s.live(v);
  }

  void Step(int i, int dir) {
    const deckmenu::Setting& s = list_[i];
    const int n = int(s.choices.size());
    int at = detail::ChoiceIndex(s, values_[i]);
    at = at < 0 ? (dir > 0 ? 0 : n - 1) : ((at + dir) % n + n) % n;
    Set(i, s.choices[at].value);
  }

  // The game's default for a setting: the value in the shipped config.
  static const char* DefaultValue(const deckmenu::Setting& s) {
    static const std::pair<const char*, const char*> kDefaults[] = {
        {"fps_limit", "60"},          {"async_pipelines", "true"},          {"high_tick_rate", "false"},
        {"disable_motion_blur", "false"}, {"deck_button_prompts", "dark"}, {"menu_repeat_delay", "0.18"},
        {"menu_animation_speed", "2.5"}, {"menu_instant_response", "true"}, {"menu_scroll_acceleration", "1.5"},
        {"skip_intro_videos", "false"}, {"view_distance_multiplier", "1"}, {"lod_distance_multiplier", "1"},
        {"disable_msaa", "false"},
    };
    for (const auto& [k, v] : kDefaults)
      if (std::strcmp(k, s.key) == 0) return v;
    return nullptr;
  }

  // --- Input --------------------------------------------------------------------------
  void SetTab(int t) {
    tab_ = ((t % deckmenu::kTabCount) + deckmenu::kTabCount) % deckmenu::kTabCount;
    sel_ = 0;
  }

  void HandleInput() {
    const PadSnapshot pad = merged_pad();
    const uint16_t b = pad.buttons;
    const uint16_t pressed = b & ~prev_buttons_;
    prev_buttons_ = b;

    // As on the game's own options pages: B cancels, A accepts.
    if (pressed & (0x2000 | 0x0010)) {  // B, Start
      Close(false);
      return;
    }
    if (pressed & 0x1000) {  // A
      Close(true);
      return;
    }
    if (pressed & 0x0100) SetTab(tab_ - 1);  // LB
    if (pressed & 0x0200) SetTab(tab_ + 1);  // RB

    const std::vector<int> rows = Rows();
    if (rows.empty()) return;
    sel_ = std::clamp(sel_, 0, int(rows.size()) - 1);
    const int64_t now = detail::now_ms();
    // D-pad and the left stick, with repeat.
    const int dy = rep_down_.Fire((b & 0x0002) || pad.ly < -16000, now) - rep_up_.Fire((b & 0x0001) || pad.ly > 16000, now);
    const int dx = rep_right_.Fire((b & 0x0008) || pad.lx > 16000, now) - rep_left_.Fire((b & 0x0004) || pad.lx < -16000, now);
    if (dy != 0) {
      const int n = int(rows.size());
      sel_ = ((sel_ + dy) % n + n) % n;
    }
    if (dx != 0) Step(rows[sel_], dx > 0 ? 1 : -1);
    if (pressed & 0x8000) {  // Y: default
      if (const char* d = DefaultValue(list_[rows[sel_]])) Set(rows[sel_], d);
    }
  }

  // --- Layout ------------------------------------------------------------------------
  // The page is the book's right-hand page, as the game's own options pages
  // (Game, Video, Audio) are: the game's menu stays on the left page, the
  // settings fill the right one. Units are the game's 720p canvas; the page
  // runs from the book's spine (the screen centre) to its right edge.
  struct Box {
    ImVec2 Min, Max;
  };
  static constexpr float kLarge = 32.0f;  // the game's three menu font sizes
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
  static constexpr float kPageLeft = 10.0f;  // text area edges from the screen centre (inside the rims)
  static constexpr float kPageRight = 396.0f;
  static constexpr float kMargin = 10.0f;    // text inset from the page edges
  static constexpr float kWidth = kPageRight - kPageLeft - 2 * kMargin;  // text column
  static constexpr float kTitleY = 30.0f;
  static constexpr float kTabsY = 96.0f;
  static constexpr float kListY = 140.0f;
  static constexpr float kRow = 34.0f;
  static constexpr int kMaxRows = 8;
  static constexpr float kPromptInset = 30.0f;  // the prompt row's gap from the text column's right end

  float X(float v) const { return v * k_; }
  // The page moves with the game's right cover on a wide screen, and slides.
  float PageX0() const { return cx_ + po_ + X(kPageLeft); }
  float PageX1() const { return cx_ + po_ + X(kPageRight); }
  Box RowBox(int i) const {
    const float y = X(kListY + kRow * i);
    return Box{ImVec2(ox_ - X(12), y), ImVec2(ox_ + X(kWidth + 12), y + X(kRow))};
  }
  float ValueRight() const { return ox_ + X(kWidth); }

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
  float TextWrapped(ImVec2 p, float size, float width, ImU32 col, const std::string& all, float bottom = 1e9f) {
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
    // How far out the page is (0 under the cover, 1 open), eased as the
    // game's slide.
    float t = std::clamp(float(detail::now_ms() - opened_ms_) / kSlideMs, 0.0f, 1.0f);
    if (closing_ms_ != 0) t = 1.0f - std::clamp(float(detail::now_ms() - closing_ms_) / kSlideMs, 0.0f, 1.0f);
    const float out = t * t * (3.0f - 2.0f * t);
    cx_ = W * 0.5f;
    // On a wide screen with the HUD at the edges, the game moves the pause
    // menu's right page and cover out by 1.2x the extra width per side (360 *
    // aspect - 640 at 720p; src/core/fable2_fit_window_ui.h). The page and
    // its cover go where the game's are, so only one cover shows. (None at
    // the Deck's 16:10.)
    {
      const float extra = std::max(0.0f, 360.0f * fitwindow::UiAspect() - 640.0f);
      shift_ = REXCVAR_GET(fit_window_hud_to_edges) ? X(extra * 1.2f) : 0.0f;
    }
    // Under the cover the page's left rim (x 562 at 720p) is at the cover's
    // edge (x 1008).
    po_ = shift_ + (1.0f - out) * X(1008 - 562);
    ox_ = PageX0() + X(kMargin);
    const float px0 = PageX0(), px1 = PageX1();

    // The game's menu on the left page steps back, as it does when one of
    // its own options pages opens.
    dl_->AddRectFilled(ImVec2(0, 0), ImVec2(std::min(px0, cx_ + shift_ + X(1008 - 640)), H),
                       IM_COL32(4, 3, 2, int(90 * out)));
    // The right-hand page turns to paper: the game's parchment texture,
    // shaded at its edges, between two copper rims as on the game's pages:
    // the outer edge, which curves in towards the middle of the page, and
    // the edge along the cover. Without the textures plain paper stands in.
    paper_ = true;
    const int paper = gameicons::FindExtra(kPaperTexture);
    const int sheet = gameicons::FindExtra(kFrameSheet);
    const ImU32 tint = IM_COL32_WHITE;
    const bool textured = gameicons::ExtraReady(paper) && gameicons::ExtraReady(sheet);
    const float rim_left = cx_ + po_ + X(562 - 640);  // the outer strip's left end (x 562 at 720p)
    const float paper_x0 = cx_ + po_ + X(600 - 640);  // where the paper texture's left edge is laid
    const float under = cx_ + po_ + X(1064 - 640);    // the paper runs on under the cover
    const float span = under - paper_x0;
    const ImU32 dark = IM_COL32(40, 24, 8, 90), soft = IM_COL32(40, 24, 8, 45);
    const ImU32 clear = IM_COL32(40, 24, 8, 0);
    // Slices of 5 rows (at 720p) so the cut follows the rim's curve.
    constexpr float kSlice = 5.0f;
    for (int i = 0; i < int(720.0f / kSlice); ++i) {
      const float vy0 = kSlice * i, vy1 = vy0 + kSlice;  // 720p rows
      const bool bottom = vy0 >= 360.0f;
      const float r0 = bottom ? 720.0f - vy1 : vy0;  // strip rows from its top
      const float y0 = X(vy0), y1 = X(vy1);
      const float e = RimRight(r0 + kSlice * 0.5f);  // the rim's outer side in the strip
      const float sx0 = rim_left + X(e - 8);         // the paper starts under the rim
      if (textured) {
        const float ty0 = 512.0f * r0 / 360.0f, ty1 = 512.0f * (r0 + kSlice) / 360.0f;
        gameicons::DrawExtraRegion(dl_, paper, ImVec2(sx0, y0), ImVec2(under, y1),
                                   ImVec2(512.0f * (sx0 - paper_x0) / span, ty0), ImVec2(512.0f, ty1), tint, bottom);
      } else {
        dl_->AddRectFilled(ImVec2(sx0, y0), ImVec2(under, y1), IM_COL32(222, 208, 176, 255));
      }
      dl_->AddRectFilledMultiColor(ImVec2(sx0, y0), ImVec2(sx0 + X(30), y1), dark, clear, clear, dark);
      // The outer rim, and its shadow on the paper.
      gameicons::DrawExtraRegion(dl_, sheet, ImVec2(rim_left + X(e - 18), y0), ImVec2(rim_left + X(e + 12), y1),
                                 ImVec2(330.0f + e - 18, 152.0f + r0), ImVec2(330.0f + e + 12, 152.0f + r0 + kSlice),
                                 tint, bottom);
    }
    dl_->AddRectFilledMultiColor(ImVec2(px1 - X(22), 0), ImVec2(under, H), clear, soft, soft, clear);
    dl_->AddRectFilledMultiColor(ImVec2(rim_left, 0), ImVec2(under, X(22)), soft, soft, clear, clear);
    dl_->AddRectFilledMultiColor(ImVec2(rim_left, H - X(22)), ImVec2(under, H), clear, clear, soft, soft);
    // The rim along the cover and the leather cover: open, under the text;
    // while the page slides, over everything (below).
    auto draw_cover = [&] {
      {
        const float x0 = cx_ + X(986 - 640) + shift_, x1 = x0 + X(95);
        const ImVec2 t0(330, 152), t1(425, 512);
        gameicons::DrawExtraRegion(dl_, sheet, ImVec2(x0, 0), ImVec2(x1, X(360)), t0, t1, tint, false);
        gameicons::DrawExtraRegion(dl_, sheet, ImVec2(x0, X(360)), ImVec2(x1, X(720)), t0, t1, tint, true);
      }
      {
        const float x0 = cx_ + X(1008 - 640) + shift_, x1 = x0 + X(512);  // the art is 512 wide, its right part clear
        gameicons::DrawExtraRect(dl_, gameicons::FindExtra(kCoverTop), ImVec2(x0, 0), ImVec2(x1, X(512)), tint, false,
                                 1.0f);
        gameicons::DrawExtraRect(dl_, gameicons::FindExtra(kCoverBottom), ImVec2(x0, X(512)), ImVec2(x1, X(720)), tint,
                                 false, 208.0f / 512.0f);
      }
    };
    if (out >= 1.0f) draw_cover();

    // Title, centred over the game's flourish as on its own pages.
    {
      const float mid = ox_ + X(kWidth * 0.5f);
      const char* title = "Deck Recomp Settings";
      Text(ImVec2(mid - TextW(title, kLarge) * 0.5f, X(kTitleY)), kLarge, kTitleInk, title);
      const float fy = X(kTitleY + 30);
      if (!gameicons::DrawExtraRegion(dl_, gameicons::FindExtra(kMotifs), ImVec2(mid - X(170), fy),
                                      ImVec2(mid + X(170), fy + X(27)), ImVec2(22, 2), ImVec2(362, 29),
                                      IM_COL32_WHITE, false))
        Rule(ox_ - X(10), ox_ + X(kWidth + 10), X(kTitleY + 42), 1.0f, kAccent);
    }

    // The tab:  LB  <  Performance  >  RB
    {
      const float cx = ox_ + X(kWidth * 0.5f);
      const char* name = deckmenu::TabName(tab_);
      const float w = TextW(name, kNormal);
      Text(ImVec2(cx - w * 0.5f, X(kTabsY)), kNormal, kTitleInk, name);
      dl_->AddRectFilled(ImVec2(cx - w * 0.5f, X(kTabsY + 30)),
                         ImVec2(cx + w * 0.5f, X(kTabsY + 30) + std::max(1.0f, X(1.5f))), kAccent);
      const float cy = X(kTabsY) + X(kNormal) * 0.5f;
      Chevron(ImVec2(cx - w * 0.5f - X(16), cy), X(6), false, kAccent);
      Chevron(ImVec2(cx + w * 0.5f + X(16), cy), X(6), true, kAccent);
      const float lw = Prompt(0, 0, "LB", "", true);
      Prompt(cx - w * 0.5f - X(32) - lw, X(kTabsY - 1), "LB", "");
      Prompt(cx + w * 0.5f + X(32), X(kTabsY - 1), "RB", "");
    }

    // Lines.
    const std::vector<deckmenu::Setting>& L = list_;
    const std::vector<std::string>& startup = deckmenu::StartupValues();
    const std::vector<int> rows = Rows();
    for (int r = 0; r < int(rows.size()) && r < kMaxRows; ++r) {
      const int i = rows[r];
      const deckmenu::Setting& s = L[i];
      const Box box = RowBox(r);
      const bool cur = r == sel_;
      const float cy = (box.Min.y + box.Max.y) * 0.5f;
      if (cur) {
        const ImU32 a = IM_COL32(255, 252, 240, 110), z = IM_COL32(255, 252, 240, 0);
        dl_->AddRectFilledMultiColor(box.Min, box.Max, a, z, z, a);
        dl_->AddRectFilled(box.Min, ImVec2(box.Min.x + std::max(1.0f, X(2)), box.Max.y), kAccent);
      }
      const ImU32 col = cur ? kInk : kInkDim;
      const int at = detail::ChoiceIndex(s, values_[i]);
      const std::string val = at >= 0 ? s.choices[at].label : values_[i];
      // Name and value never meet: the name drops to the small font, then
      // the value, then the name is shortened.
      const float room = X(kWidth) - X(4) - X(8);
      float label_size = kNormal, value_size = kNormal;
      const auto value_width = [&](float size) { return TextW(val.c_str(), size) + X(30); };
      if (TextW(s.label, kNormal) + value_width(kNormal) > room) label_size = kSmall;
      if (TextW(s.label, kSmall) + value_width(kNormal) > room) value_size = kSmall;
      std::string label = s.label;
      while (label.size() > 4 && TextW(label.c_str(), label_size) + value_width(value_size) > room)
        label = label.substr(0, label.size() - 4) + "...";
      Text(ImVec2(ox_ + X(4), cy - X(label_size) * 0.52f), label_size, col, label.c_str());
      const float right = cur ? ValueRight() - X(14) : ValueRight();
      TextRight(right, cy - X(value_size) * 0.52f, value_size, kValueInk, val.c_str());
      if (cur) {
        Chevron(ImVec2(right - TextW(val.c_str(), value_size) - X(11), cy), X(5), false, kAccent);
        Chevron(ImVec2(ValueRight() - X(4), cy), X(5), true, kAccent);
      }
    }

    // Description of the selected line, and when a change takes effect.
    float y = X(kListY + kRow * kMaxRows + 14);
    Rule(ox_ - X(10), ox_ + X(kWidth + 10), y, 0.5f, kAccent);
    y += X(10);
    const float bottom = H - X(88);  // nothing runs into the prompts
    if (!rows.empty()) {
      const int i = rows[std::clamp(sel_, 0, int(rows.size()) - 1)];
      const deckmenu::Setting& s = L[i];
      y += TextWrapped(ImVec2(ox_, y), kSmall, X(kWidth), kDescInk, deckmenu::Explanation(s.key), bottom);
      const char* when = "Takes effect at once.";
      if (s.when == deckmenu::When::kRestart) when = "Takes effect the next time the game starts.";
      else if (s.when == deckmenu::When::kNextScreen) when = "Takes effect in menus opened from now on.";
      else if (s.when == deckmenu::When::kNextLoad) when = "Takes effect from the next area or save load.";
      else if (s.when == deckmenu::When::kAtStart) when = "Used when the game starts: the videos only play then.";
      y += X(2);
      TextWrapped(ImVec2(ox_, y), kSmall, X(kWidth), kFaint, when, bottom);
    }

    // Restart reminder, above the prompts.
    std::string pending;
    for (size_t i = 0; i < L.size(); ++i)
      if (L[i].when == deckmenu::When::kRestart && i < startup.size() && values_[i] != startup[i])
        pending += std::string(pending.empty() ? "" : ", ") + L[i].label;
    if (!pending.empty())
      TextWrapped(ImVec2(ox_, H - X(84) - X(26)), kSmall, X(kWidth), kAccent, "Restart to apply: " + pending);

    DrawPrompts(io);
    paper_ = false;
    // Sliding, the page goes under the cover, so the cover is drawn last.
    if (out < 1.0f) draw_cover();
  }

  void DrawPrompts(ImGuiIO& io) {
    const float H = io.DisplaySize.y;
    struct P {
      const char* button;
      const char* label;
    };
    const P list[] = {{"Y", "Default"}, {"B", "Cancel"}, {"A", "Accept"}};
    // Along the bottom of the page, right-aligned, clear of the page's right
    // edge (which the cover's rim overlaps on the Deck's 16:10 screen).
    const float gap = X(20);
    float total = 0;
    for (const P& p : list) total += Prompt(0, 0, p.button, p.label, true) + gap;
    float x = ox_ + X(kWidth - kPromptInset) - (total - gap);
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

}  // namespace fable2::decksettings
