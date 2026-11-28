// fable_2 - The difficulty cards (docs/feature/difficulty_settings/), drawn on the UI
// thread over the game in the style of the photo mode menu (src/core/photo_mode_dialog.h):
// the game's GUI typeface, cream text, a gold highlight and the game's own card back
// (art\gui\frontend\textures\cardback, loaded with the photo mode icons).
//
// Two uses (fable2::difficulty::DialogMode):
//   - New Game: after boy or girl, before the game starts. A card must be chosen; Easy
//     is selected first (decision 6).
//   - In game (F8): the current level is marked. Hardcore can't be entered or left in
//     game (decision 2), so its card is locked, and on a Hardcore hero nothing changes.
//
// On New Game, Y switches Hardcore saving on the Hardcore card (save on quit only,
// technical.md 23); it starts as [patches] hardcore_saving says. While a window close waits
// for the game to save and quit, a notice says so.
//
// Input arrives as counters from the pad read (Difficulty.cpp OnPadRead), so the
// controller and the keyboard map both work; the game sees an empty pad meanwhile.
// The dialog only raises the choice; the game's threads apply it.
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include <imgui.h>
#include <rex/input/input_system.h>
#include <rex/logging/macros.h>
#include <rex/runtime.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>

#include "fable2_config.h"  // [input] keyboard_gamepad_map, for the key prompts
#include "hotfunc/difficulty/difficulty.h"
#include "photo_mode_font.h"
#include "photo_mode_icons.h"
#include "prompt_labels.h"  // the game's own key prompts' labels (src/gui/)

namespace fable2::difficulty {

inline constexpr const char* kCardBackTexture = "art\\gui\\frontend\\textures\\cardback.tex";

// The cards' pictures (delta4o's choice, 2026-11-27), read from the user's game data at
// startup (gameicons::LoadPictures): scenes from the game's loading screens (the scene is
// the top 1024 x 720 of each texture) and a quest picture (512 x 512), each cropped to the
// card's shape (210 x 320) around the subject. Easy shows the boy or the girl, whichever
// was picked on New Game (the boy in game). Without the pictures the cards keep the card
// back.
inline constexpr int kPictureW = 315, kPictureH = 480, kPictureCorner = 15;  // corner = the card's rounding
inline const std::vector<gameicons::PictureSpec>& CardPictures() {
  static const std::vector<gameicons::PictureSpec> v = {
      {"difficulty:easy_boy", "mip0", "art\\gui\\loadingscreens\\loading_boy.tex", 552, 0, 1024, 720, kPictureW,
       kPictureH, kPictureCorner},
      {"difficulty:easy_girl", "mip0", "art\\gui\\loadingscreens\\loading_girl.tex", 552, 0, 1024, 720, kPictureW,
       kPictureH, kPictureCorner},
      {"difficulty:medium", "mip0", "art\\gui\\loadingscreens\\loading_fairfax_castle.tex", 64, 0, 536, 720,
       kPictureW, kPictureH, kPictureCorner},
      {"difficulty:hard", "mip0", "art\\gui\\loadingscreens\\loading_guild_cave.tex", 184, 0, 656, 720, kPictureW,
       kPictureH, kPictureCorner},
      {"difficulty:hardcore", "world", "art\\gui\\questscreenshots\\qc130_epilogue_screen_image.tex", 88, 0, 424,
       512, kPictureW, kPictureH, kPictureCorner},
  };
  return v;
}

class DifficultyDialog final : public rex::ui::ImGuiDialog {
 public:
  explicit DifficultyDialog(rex::ui::ImGuiDrawer* drawer) : rex::ui::ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    PollSaveSlots();  // the slot labels' level files (throttled, host files only)
    PollClose();      // Hardcore saving: close the window once the save and quit are done
    DialogState& d = Dialog();
    const int mode = d.mode.load();
    if (ClosePending()) {
      k_ = std::max(0.5f, io.DisplaySize.y / 720.0f);
      dl_ = ImGui::GetForegroundDrawList();
      ClosingNotice(io);
    }
    if (mode == int(DialogMode::kClosed)) {
      was_open_ = false;
      // The game's boy and girl cards have no button prompts: draw ours under them.
      if (GenderCardsWaitingForInput()) {
        k_ = std::max(0.5f, io.DisplaySize.y / 720.0f);
        dl_ = ImGui::GetForegroundDrawList();
        float px = io.DisplaySize.x * 0.5f - X(160.0f);
        const float py = io.DisplaySize.y - X(70.0f);
        Prompt(px, py, "A", "Choose");
        Prompt(px, py, "B", "Back");
      }
      return;
    }
    if (!was_open_) {
      was_open_ = true;
      current_ = mode == int(DialogMode::kInGame) ? CurrentIndex() : -1;
      sel_ = current_ >= 0 ? current_ : 0;
      locked_ = current_ == 3;  // a Hardcore hero stays Hardcore
      saving_locked_ = locked_ && SavingLocked();
      REXSYS_INFO("[difficulty] cards shown ({}), selected {}",
                  mode == int(DialogMode::kNewGame) ? "New Game" : "in game", Level(sel_).name);
    }
    k_ = std::max(0.5f, io.DisplaySize.y / 720.0f);
    dl_ = ImGui::GetForegroundDrawList();
    const bool new_game = mode == int(DialogMode::kNewGame);

    // Input.
    if (const int dx = d.nav_x.exchange(0); dx != 0 && !locked_) {
      sel_ = std::clamp(sel_ + dx, 0, kLevelCount - 1);
    }
    if (d.toggle.exchange(0) > 0 && new_game && sel_ == 3) {
      const bool lock = !d.lock_saving.load();
      d.lock_saving.store(lock);
      REXSYS_INFO("[difficulty] Hardcore saving switched {}", lock ? "on (save on quit only)" : "off");
    }
    if (d.activate.exchange(0) > 0) {
      if (locked_ || (!new_game && sel_ == current_)) {
        CloseDialog();
        return;
      }
      if (!new_game && sel_ == 3) {
        locked_hint_s_ = NowS();  // Hardcore is New Game only
      } else {
        REXSYS_INFO("[difficulty] card chosen: {}", Level(sel_).name);
        Choose(sel_);
        return;
      }
    }
    if (d.back.exchange(0) > 0) {
      if (new_game) {
        BackToGenderCards();  // back to boy or girl
      } else {
        CloseDialog();
      }
      return;
    }

    Draw(io, new_game);
  }

 private:
  bool was_open_ = false;
  int sel_ = 0;
  int current_ = -1;
  bool locked_ = false;
  bool saving_locked_ = false;  // in game: this Hardcore hero saves on quit only
  double locked_hint_s_ = -100.0;
  float k_ = 1.0f;
  ImDrawList* dl_ = nullptr;

  // 720p units, like the photo mode menu.
  static constexpr float kLarge = 32.0f;
  static constexpr float kNormal = 27.0f;
  static constexpr float kSmall = 24.0f;
  static constexpr float kCardW = 210.0f;
  static constexpr float kCardH = 320.0f;
  static constexpr float kGap = 22.0f;

  static constexpr ImU32 kCream = IM_COL32(240, 230, 204, 255);
  static constexpr ImU32 kCreamDim = IM_COL32(196, 184, 156, 255);
  static constexpr ImU32 kGold = IM_COL32(218, 182, 98, 255);
  static constexpr ImU32 kShadow = IM_COL32(0, 0, 0, 200);

  float X(float v) const { return v * k_; }

  static ImU32 Alpha(ImU32 c, float a) {
    const int base = int((c >> IM_COL32_A_SHIFT) & 0xFF);
    return (c & ~IM_COL32_A_MASK) | (ImU32(std::clamp(int(base * a), 0, 255)) << IM_COL32_A_SHIFT);
  }

  float TextW(const char* t, float size, float scale = 1.0f) const {
    if (gamefont::AnyReady()) return gamefont::Measure(t, X(size) * scale, size);
    return float(std::strlen(t)) * 7.0f;
  }
  void Text(ImVec2 p, float size, ImU32 col, const char* t, float scale = 1.0f) {
    if (gamefont::AnyReady()) {
      const float sz = X(size) * scale;
      const float o = std::max(1.0f, sz * 0.06f);
      gamefont::Draw(dl_, ImVec2(p.x + o, p.y + o), sz, size, Alpha(kShadow, ((col >> 24) & 0xFF) / 255.0f), t);
      gamefont::Draw(dl_, p, sz, size, col, t);
    } else {
      dl_->AddText(ImVec2(p.x + 1, p.y + 1), kShadow, t, nullptr);
      dl_->AddText(p, col, t, nullptr);
    }
  }
  void TextCentred(float cx, float y, float size, ImU32 col, const char* t, float scale = 1.0f) {
    Text(ImVec2(cx - TextW(t, size, scale) * 0.5f, y), size, col, t, scale);
  }

  // A thin gold rule that fades out at both ends (as in photo mode).
  void Rule(float x0, float x1, float y, float alpha) {
    const float mid = (x0 + x1) * 0.5f;
    const float h = std::max(1.0f, X(1.2f));
    const ImU32 g = Alpha(kGold, 0.85f * alpha);
    const ImU32 z = Alpha(kGold, 0.0f);
    dl_->AddRectFilledMultiColor(ImVec2(x0, y), ImVec2(mid, y + h), z, g, g, z);
    dl_->AddRectFilledMultiColor(ImVec2(mid, y), ImVec2(x1, y + h), g, z, z, g);
  }

  // The game's card back, a square texture: its middle strip, as wide as the card is
  // in proportion to its height, stretched over the card. (The runtime doesn't export
  // ImDrawList::PushClipRect, so the crop is done with the texture coordinates.)
  void CardBack(ImVec2 p0, ImVec2 p1, float alpha) {
    const int tex = gameicons::FindExtra(kCardBackTexture);
    if (!gameicons::ExtraReady(tex)) return;
    const gameicons::Image& img = gameicons::extras()[tex].img;
    ImFontAtlasRect r;
    if (!img.atlas || !img.atlas->GetCustomRect(img.rect_id, &r)) return;
    const float frac = std::clamp((p1.x - p0.x) / std::max(1.0f, p1.y - p0.y), 0.0f, 1.0f);
    const float u0 = r.uv0.x + (r.uv1.x - r.uv0.x) * (0.5f - frac * 0.5f);
    const float u1 = r.uv0.x + (r.uv1.x - r.uv0.x) * (0.5f + frac * 0.5f);
    dl_->AddImage(dl_->_CmdHeader.TexRef, p0, p1, ImVec2(u0, r.uv0.y), ImVec2(u1, r.uv1.y),
                  Alpha(IM_COL32_WHITE, alpha));
  }

  static double NowS() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
  }

  // "Prices x1.15", "Enemy damage x2" (no trailing zeros).
  static std::string Factor(const char* what, double v) {
    char n[16];
    std::snprintf(n, sizeof(n), "%.2f", v);
    std::string s = n;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return std::string(what) + " x" + s;
  }

  void Card(int i, ImVec2 p0, bool selected, bool dim, bool new_game) {
    const LevelInfo& L = Level(i);
    const float lift = selected ? X(14.0f) : 0.0f;
    p0.y -= lift;
    const ImVec2 p1(p0.x + X(kCardW), p0.y + X(kCardH));
    const float a = dim ? 0.45f : 1.0f;
    const float round = X(10.0f);

    dl_->AddRectFilled(ImVec2(p0.x + X(6), p0.y + X(8)), ImVec2(p1.x + X(6), p1.y + X(8)),
                       IM_COL32(0, 0, 0, 120), round);  // drop shadow
    dl_->AddRectFilled(p0, p1, Alpha(IM_COL32(28, 22, 14, 235), a), round);

    // Foot of the card.
    const char* foot = nullptr;
    if (!new_game && i == current_) foot = "Current";
    else if (!new_game && i == 3) foot = "New Game only";

    // The text: the name and subtitle at the top, the multipliers at the bottom.
    const float fs = 0.8f;
    const float line = X(kSmall) * fs * 1.15f;
    const int lines = i == 0 ? 3 : 5;
    const float block = X(14.0f) + lines * line + (i == 3 ? X(6.0f) + line : 0.0f);
    const float block_y = p1.y - (foot ? X(40.0f) : X(20.0f)) - block;

    const int pic = gameicons::FindExtra(PictureKey(i, new_game));
    const bool has_pic = gameicons::ExtraReady(pic);
    if (has_pic) {
      // The picture over the whole card, a dark fade under the name and a darker one
      // under the multipliers.
      Picture(pic, p0, p1, a);
      const ImU32 d0 = Alpha(IM_COL32(10, 8, 5, 255), 0.0f);
      const ImU32 dt = Alpha(IM_COL32(10, 8, 5, 255), 0.8f * a);
      const ImU32 db = Alpha(IM_COL32(10, 8, 5, 255), 0.9f * a);
      const float top = p0.y + X(96.0f);
      dl_->AddRectFilledMultiColor(ImVec2(p0.x + X(2), p0.y + X(2)), ImVec2(p1.x - X(2), top), dt, dt, d0, d0);
      const float fade = block_y - X(36.0f);
      dl_->AddRectFilledMultiColor(ImVec2(p0.x + X(2), fade), ImVec2(p1.x - X(2), block_y), d0, d0, db, db);
      dl_->AddRectFilled(ImVec2(p0.x + X(2), block_y), ImVec2(p1.x - X(2), p1.y - X(2)), db, round);
    } else {
      CardBack(p0, p1, 0.55f * a);
      // Darken the middle so the text reads over the art.
      dl_->AddRectFilled(ImVec2(p0.x + X(12), p0.y + X(60)), ImVec2(p1.x - X(12), p1.y - X(40)),
                         Alpha(IM_COL32(12, 9, 5, 170), a), X(6.0f));
    }
    dl_->AddRect(p0, p1, Alpha(selected ? kGold : kCreamDim, selected ? 1.0f : 0.55f * a), round, 0,
                 std::max(1.0f, X(selected ? 3.0f : 1.4f)));

    const float cx = (p0.x + p1.x) * 0.5f;
    float y = p0.y + X(18.0f);
    TextCentred(cx, y, kLarge, Alpha(selected ? kGold : kCream, a), L.name);
    y += X(36.0f);
    TextCentred(cx, y, kSmall, Alpha(kCreamDim, a), L.subtitle, 0.9f);
    y = has_pic ? block_y : y + X(34.0f);
    Rule(p0.x + X(20), p1.x - X(20), y, a);
    y += X(14.0f);

    if (i == 0) {
      TextCentred(cx, y, kSmall, Alpha(kCream, a), "The original game", fs);
      y += line;
      TextCentred(cx, y, kSmall, Alpha(kCreamDim, a), "Every value as it", fs);
      y += line;
      TextCentred(cx, y, kSmall, Alpha(kCreamDim, a), "always was", fs);
    } else {
      for (const std::string& s : {Factor("Gold earned", L.gold), Factor("Prices", L.prices),
                                   Factor("Enemy damage", L.enemy_damage),
                                   Factor("Enemy health", L.enemy_health), Factor("XP", L.xp)}) {
        TextCentred(cx, y, kSmall, Alpha(kCream, a), s.c_str(), fs);
        y += line;
      }
      if (i == 3) {
        y += X(6.0f);
        TextCentred(cx, y, kSmall, Alpha(kGold, a), "Chosen once, for good", fs);
      }
    }

    if (foot) TextCentred(cx, p1.y - X(34.0f), kSmall, Alpha(kGold, a), foot, 0.85f);
  }

  // The picture key for card i (Easy: the boy or the girl picked on New Game).
  static const char* PictureKey(int i, bool new_game) {
    switch (i) {
      case 0:
        return new_game && NewGameIsGirl() ? "difficulty:easy_girl" : "difficulty:easy_boy";
      case 1:
        return "difficulty:medium";
      case 2:
        return "difficulty:hard";
      default:
        return "difficulty:hardcore";
    }
  }

  // A card picture over the whole card (cropped to the card's shape, corners cut out).
  void Picture(int extra, ImVec2 p0, ImVec2 p1, float alpha) {
    const gameicons::Image& img = gameicons::extras()[extra].img;
    ImFontAtlasRect r;
    if (!img.atlas || !img.atlas->GetCustomRect(img.rect_id, &r)) return;
    dl_->AddImage(dl_->_CmdHeader.TexRef, p0, p1, r.uv0, r.uv1, Alpha(IM_COL32_WHITE, alpha));
  }

  // The last input came from the keyboard or mouse (the SDK's synthetic device), as the
  // game's own prompt swap decides it (fable2_keycap_swap.h ActiveDeviceIsKeyboard).
  static bool KeyboardActive() {
    auto* rt = rex::Runtime::instance();
    auto* input = rt ? static_cast<rex::input::InputSystem*>(rt->input_system()) : nullptr;
    return input && input->ActiveDeviceIsSynthetic(0);
  }

  // The key the keyboard map ([input] keyboard_gamepad_map, "Key:Button,...") binds to
  // `button` ("A", "B", "Y"), labelled the way the game's own keyboard prompts label it
  // (fable2::gui::ResolveLabels): a regular key beats a modifier or a mouse button, and a
  // mouse button shows as "LMB" / "RMB" / "MMB" (the default map binds Y only to RMB).
  // "" when nothing is bound.
  static std::string KeyFor(const char* button) {
    static std::string map_seen;
    static std::unordered_map<std::string, std::string> labels;
    const std::string& map = config::Get().keyboard_gamepad_map;
    if (map != map_seen || labels.empty()) {
      labels = gui::ResolveLabels(map, "");
      map_seen = map;
    }
    const auto it = labels.find(gui::Ci(button));
    return it != labels.end() ? it->second : std::string();
  }

  // A key cap with the key's name, like photo mode's keyboard prompts.
  float KeyCap(float x, float y, const char* key) {
    const float h = X(30.0f);
    const float tw = TextW(key, kSmall, 0.78f);
    const float cw = std::max(h, tw + X(14.0f));
    dl_->AddRectFilled(ImVec2(x, y), ImVec2(x + cw, y + h), IM_COL32(18, 16, 12, 220), X(6.0f), 0);
    dl_->AddRect(ImVec2(x, y), ImVec2(x + cw, y + h), Alpha(kCreamDim, 0.8f), X(6.0f), 0,
                 std::max(1.0f, X(1.2f)));
    Text(ImVec2(x + (cw - tw) * 0.5f, y + h * 0.5f - X(kSmall) * 0.78f * 0.52f), kSmall, kCream, key, 0.78f);
    return cw;
  }

  // `button` is the pad button's name ("A", "B"): the game's icon on a controller, the
  // bound key's cap after keyboard input.
  void Prompt(float& x, float y, const char* button, const char* label) {
    const float h = X(30.0f);
    float w = h;
    const std::string key = KeyboardActive() ? KeyFor(button) : std::string();
    const int icon = gameicons::Find(button);
    if (!key.empty()) {
      w = KeyCap(x, y, key.c_str());
    } else if (gameicons::Ready(icon)) {
      const float s = h * 1.25f;
      gameicons::Draw(dl_, icon, ImVec2(x - (s - h) * 0.5f, y + h * 0.5f - s * 0.5f), s);
    }
    x += w + X(8.0f);
    Text(ImVec2(x, y + h * 0.5f - X(kSmall) * 0.52f), kSmall, kCream, label);
    x += TextW(label, kSmall) + X(28.0f);
  }

  void Draw(ImGuiIO& io, bool new_game) {
    const ImVec2 ds = io.DisplaySize;
    dl_->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(0, 0, 0, new_game ? 150 : 170));

    const float cx = ds.x * 0.5f;
    float y = ds.y * 0.5f - X(kCardH * 0.5f + 120.0f);
    TextCentred(cx, y, kLarge, kCream, new_game ? "How hard shall the tale be?" : "Difficulty");
    y += X(42.0f);
    const char* sub = new_game ? "You can change Easy, Medium and Hard later. Hardcore is for good."
                      : locked_ ? "This hero is on Hardcore, which can't be changed."
                                : "Hardcore can only be chosen on a New Game.";
    TextCentred(cx, y, kSmall, kCreamDim, sub, 0.9f);
    Rule(cx - X(360), cx + X(360), y + X(36.0f), 1.0f);

    const float total = kLevelCount * kCardW + (kLevelCount - 1) * kGap;
    const float x0 = cx - X(total * 0.5f);
    const float cy = ds.y * 0.5f - X(kCardH * 0.5f) + X(20.0f);
    for (int i = 0; i < kLevelCount; ++i) {
      const bool dim = locked_ ? i != current_ : (!new_game && i == 3);
      Card(i, ImVec2(x0 + X((kCardW + kGap) * i), cy), i == sel_, dim, new_game);
    }

    // Hint after trying to pick Hardcore in game.
    const bool saving_card = new_game && sel_ == 3;
    const bool lock = Dialog().lock_saving.load();
    if (NowS() - locked_hint_s_ < 2.5) {
      TextCentred(cx, cy + X(kCardH + 28.0f), kNormal, kGold, "Hardcore can only be chosen on a New Game");
    } else if (saving_card || saving_locked_) {
      // Hardcore saving (technical.md 23): the New Game switch, or this hero's setting.
      const bool on = saving_card ? lock : true;
      TextCentred(cx, cy + X(kCardH + 22.0f), kNormal, on ? kGold : kCream,
                  on ? "Saving: only on quit" : "Saving: as in the original");
      TextCentred(cx, cy + X(kCardH + 54.0f), kSmall, kCreamDim,
                  on ? "No Save and Continue, no Quit without saving, no reloading. Closing the game saves first."
                     : "Save and Continue, Quit without saving and the reload box all stay.",
                  0.85f);
    }

    float px = cx - X(saving_card ? 250.0f : 160.0f);
    const float py = ds.y - X(70.0f);
    if (locked_) {
      Prompt(px, py, "A", "Close");
    } else {
      Prompt(px, py, "A", "Choose");
      Prompt(px, py, "B", "Back");
      if (saving_card) Prompt(px, py, "Y", lock ? "Saving: on quit only" : "Saving: normal");
    }
  }

  // While a window close waits for the game to save and quit (Hardcore saving).
  void ClosingNotice(ImGuiIO& io) {
    const ImVec2 ds = io.DisplaySize;
    const float w = X(560.0f), h = X(74.0f);
    const ImVec2 p0(ds.x * 0.5f - w * 0.5f, X(40.0f));
    dl_->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), IM_COL32(18, 14, 9, 225), X(8.0f));
    dl_->AddRect(p0, ImVec2(p0.x + w, p0.y + h), Alpha(kGold, 0.8f), X(8.0f), 0, std::max(1.0f, X(1.4f)));
    TextCentred(ds.x * 0.5f, p0.y + X(10.0f), kNormal, kGold, "Hardcore: saving before the game closes");
    TextCentred(ds.x * 0.5f, p0.y + X(42.0f), kSmall, kCreamDim,
                "It waits until the game can save. Close again after 15 s to quit without saving.", 0.8f);
  }
};

}  // namespace fable2::difficulty
