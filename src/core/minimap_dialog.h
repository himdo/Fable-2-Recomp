// fable_2 - Live minimap, overlay: draws the pause menu's region map in the
// top-right corner while the game runs, with the hero's arrow and the dog's
// paw on it (game data from src/core/fable2_minimap.h via
// src/core/minimap_state.h). Back (View on a Steam Deck) shows and hides it.
//
// Frame rate: the dialog opts out of the drawer's continuous repaint
// (WantsContinuousRepaint() = false), so while it shows the presenter still
// paints once per game frame and frame rate counters keep showing the game's
// rate. It is unregistered while hidden (the game thread asks for it again
// through show_hook()).
//
// Layout: anchored in the top-right corner, the rim as far from the right
// screen edge as from the top one at every size. When a message shows in the
// bar across the top,
// the game pushes the health bar down; the map moves down by the same amount
// (the HUD reports it, see src/core/fable2_minimap_hud_lua.h).
//
// Drawn like the pause menu's map (canvas: x right, y up; GameFace turns an
// element counter-clockwise by a positive rotation.z): the region image, 350
// units across, turned by MinimapDegreesRotationToNorth; icons at
//   RotCCW((north + iconRot) deg) * ((pos / extent - 0.5) * iconScale * 240)
//   + (IconTransformX, IconTransformZ)
// from its centre; the wooden rim and the north keystone on top.
// Heading-up mode rotates the texture and marker positions about the centre
// of the region map. The hero arrow stays upright and the frame is fixed.
//
// Art from the user's gui_textures.bnk (decoded off the UI thread):
//   the region map: the bank entry whose FNV-1 name hash is the record's id
//   art\gui\frames\received_item_circle     the pause map's rim
//   art\gui\wheelicons\minimap\minimap16    hero arrow (points up at 0)
//   art\gui\wheelicons\minimap\minimap11    dog paw
//   art\gui\frames\mapkeystonenorth         north keystone
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <imgui.h>
#include <rex/cvar.h>
#include <rex/logging/macros.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/immediate_drawer.h>
#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context.h>

#include "deck_settings_state.h"
#include "fable2_config.h"
#include "minimap_state.h"
#include "photo_mode_icons.h"
#include "photo_mode_state.h"

REXCVAR_DECLARE(bool, minimap_debug);
REXCVAR_DECLARE(double, minimap_image_units);
REXCVAR_DECLARE(bool, minimap_flip_y);
REXCVAR_DECLARE(double, minimap_rot_sign);

namespace fable2::minimap {

inline constexpr const char kHeroIcon[] = "art\\gui\\wheelicons\\minimap\\minimap16.tex";
inline constexpr const char kDogIcon[] = "art\\gui\\wheelicons\\minimap\\minimap11.tex";
inline constexpr const char kNorthIcon[] = "art\\gui\\frames\\mapkeystonenorth.tex";
inline constexpr const char kRim[] = "art\\gui\\frames\\received_item_circle.tex";

// The game's resource id of a name (sub_822CA580): FNV-1 over the characters.
inline uint32_t ResourceId(const std::string& lower) {
  uint32_t h = 0x811C9DC5u;
  for (unsigned char c : lower) h = (h * 16777619u) ^ c;
  return h;
}

// Decodes bank textures on its own thread; the UI thread uploads the results.
// A request is a bank entry name, or "#xxxxxxxx" for a resource id.
class TextureLoader {
 public:
  struct Decoded {
    std::string key;
    bool ok = false;
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
  };

  explicit TextureLoader(std::filesystem::path root) : root_(std::move(root)) {}
  ~TextureLoader() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stop_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
  }

  void Request(const std::string& key) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!thread_.joinable()) thread_ = std::thread([this] { Run(); });
      requests_.push_back(key);
    }
    cv_.notify_one();
  }
  bool TakeDone(Decoded& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (done_.empty()) return false;
    out = std::move(done_.front());
    done_.pop_front();
    return true;
  }

 private:
  void OpenBanks() {
    namespace gi = fable2::gameicons;
    for (const char* lang : {"en-uk", "en-us", "en"}) {
      const auto dir = root_ / "data" / "art" / "localised" / lang / "gui";
      gi::detail::BankPair p;
      if (p.tex.Open(dir / "gui_textures.bnk") && p.hdr.Open(dir / "gui_texture_headers.bnk")) {
        banks_.push_back(std::move(p));
        break;
      }
    }
    const auto dir = root_ / "data" / "art" / "gui";
    auto pick = [&](const char* file) {
      std::error_code ec;
      const auto orig = dir / (std::string(file) + ".orig");
      return std::filesystem::exists(orig, ec) ? orig : dir / file;
    };
    gi::detail::BankPair p;
    if (p.tex.Open(pick("gui_textures.bnk")) && p.hdr.Open(pick("gui_texture_headers.bnk")))
      banks_.push_back(std::move(p));
    if (banks_.empty()) {
      REXSYS_WARN("[minimap] gui_textures.bnk not found under {}", root_.string());
      return;
    }
    // Resource ids of every entry, under the names the game data may use
    // for it (the bank has ".tex"; the records were made from source files).
    for (const auto& b : banks_) {
      for (const auto& e : b.tex.entries) {
        const size_t dot = e.name.find_last_of('.');
        const std::string stem = dot == std::string::npos ? e.name : e.name.substr(0, dot);
        for (const char* ext : {".tex", ".tga", ".dds", ".bmp", ".png", ""}) {
          ids_.emplace(ResourceId(stem + ext), e.name);
          ids_.emplace(ResourceId("data\\" + stem + ext), e.name);
        }
      }
    }
  }
  void Run() {
    OpenBanks();
    for (;;) {
      std::string key;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return stop_ || !requests_.empty(); });
        if (stop_) return;
        key = std::move(requests_.front());
        requests_.pop_front();
      }
      Decoded d;
      d.key = key;
      std::string name = key;
      if (!key.empty() && key[0] == '#') {
        const uint32_t id = uint32_t(std::strtoul(key.c_str() + 1, nullptr, 16));
        const auto it = ids_.find(id);
        if (it == ids_.end()) {
          REXSYS_WARN("[minimap] no texture in gui_textures.bnk has resource id {:08X}", id);
          name.clear();
        } else {
          name = it->second;
          REXSYS_INFO("[minimap] region map {:08X} = {}", id, name);
        }
      }
      fable2::gameicons::Image img;
      std::string why;
      if (!name.empty() && !banks_.empty() &&
          fable2::gameicons::detail::LoadEntry(banks_, name, img, why)) {
        d.ok = true;
        d.w = img.w;
        d.h = img.h;
        d.rgba = std::move(img.rgba);
      } else if (!name.empty()) {
        REXSYS_WARN("[minimap] texture {} not loaded:{}", name, why.empty() ? " not in the bank" : why);
      }
      std::lock_guard<std::mutex> lock(mutex_);
      done_.push_back(std::move(d));
    }
  }

  std::filesystem::path root_;
  std::vector<fable2::gameicons::detail::BankPair> banks_;  // worker thread only
  std::unordered_map<uint32_t, std::string> ids_;           // worker thread only
  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<std::string> requests_;
  std::deque<Decoded> done_;
  bool stop_ = false;
  std::thread thread_;
};

class MinimapDialog final : public rex::ui::ImGuiDialog {
 public:
  using ImmediateTexture = rex::ui::ImmediateTexture;

  MinimapDialog(rex::ui::ImGuiDrawer* drawer, std::filesystem::path game_root)
      : rex::ui::ImGuiDialog(drawer), loader_(std::move(game_root)) {
    const auto& cfg = fable2::config::Get();
    feature_enabled().store(cfg.minimap_enabled);
    shown().store(cfg.minimap_shown);
    size_fraction().store(SizeFraction(cfg.minimap_size));
    opacity().store(cfg.minimap_opacity / 100.0f);
    rotation_enabled().store(cfg.minimap_rotation);
#ifndef _WIN32
    imgui_drawer()->RemoveDialog(this);
#else
    registered_ = true;
    want_registered_.store(true);
#endif
    instance_.store(this, std::memory_order_release);
    show_hook().store(&OnShowRequested, std::memory_order_release);
  }
  ~MinimapDialog() override {
    show_hook().store(nullptr, std::memory_order_release);
    instance_.store(nullptr, std::memory_order_release);
    if (registered_) imgui_drawer()->RemoveDialog(this);
  }

  // Only overlays the game image: redraw with each game frame, no more.
  bool WantsContinuousRepaint() const override { return false; }

 protected:
  void OnDraw(ImGuiIO& io) override {
    UploadDone();
    Snapshot s;
    {
      std::lock_guard<std::mutex> lock(snapshot_mutex());
      s = snapshot();
    }
    const int64_t t = now_ms();
    const bool want = feature_enabled().load(std::memory_order_relaxed) &&
                      shown().load(std::memory_order_relaxed) && t - s.time_ms < 250 &&
                      s.world_on_top && s.gui_drawn && s.region_ok && s.hero_ok && HudLive(t) &&
                      !fable2::photomode::active() && !fable2::decksettings::active();
    // The region's image may still be decoding (first visit): wait for it.
    ImmediateTexture* map = nullptr;
    if (want) {
      char key[16];
      std::snprintf(key, sizeof(key), "#%08X", s.texture_id);
      map = Texture(key);
    }
    const bool ready = want && map;
    if (ready) {
      last_ = s;
      last_map_ = map;
    }
    const float dt = std::clamp(io.DeltaTime, 0.0f, 0.1f);
    // Fade in / out over about 0.2 s.
    fade_ = std::clamp(fade_ + (ready ? dt : -dt) / 0.2f, 0.0f, 1.0f);
    // Follow the health bar's drop smoothly (its own updates come at 30 Hz).
    const float drop = ready ? HudDrop() : drop_;
    drop_ += (drop - drop_) * std::min(1.0f, dt / 0.05f);
    if (fade_ <= 0.0f || !last_map_) {
      if (!want) Unregister();
      return;
    }
    Draw(io, last_, last_map_);
  }

 private:
  struct Tex {
    bool requested = false;
    std::unique_ptr<ImmediateTexture> tex;
  };

  // Game thread: the minimap should be on screen.
  static void OnShowRequested() {
    MinimapDialog* d = instance_.load(std::memory_order_acquire);
    if (!d || d->want_registered_.exchange(true)) return;
    rex::ui::Window* window = d->imgui_drawer()->window();
    if (!window) {
      d->want_registered_.store(false);
      return;
    }
    window->app_context().CallInUIThreadDeferred([] {
      MinimapDialog* p = instance_.load(std::memory_order_acquire);
      if (p && !p->registered_) {
        p->registered_ = true;
        p->imgui_drawer()->AddDialog(p);
      }
    });
  }
  void Unregister() {
#ifndef _WIN32
    if (registered_) {
      registered_ = false;
      imgui_drawer()->RemoveDialog(this);
    }
    want_registered_.store(false);
#endif
  }

  // The texture for a request key, or null while it is decoding (asks for it).
  ImmediateTexture* Texture(const std::string& key) {
    Tex& t = textures_[key];
    if (t.tex) return t.tex.get();
    if (!t.requested) {
      t.requested = true;
      loader_.Request(key);
    }
    return nullptr;
  }
  void UploadDone() {
    rex::ui::ImmediateDrawer* drawer = imgui_drawer()->immediate_drawer();
    TextureLoader::Decoded d;
    while (drawer && loader_.TakeDone(d)) {
      if (!d.ok || d.w <= 0 || d.h <= 0) continue;
      textures_[d.key].tex = drawer->CreateTexture(uint32_t(d.w), uint32_t(d.h),
                                                   rex::ui::ImmediateTextureFilter::kLinear,
                                                   false, d.rgba.data());
      if (REXCVAR_GET(minimap_debug))
        REXSYS_INFO("[minimap] texture {} ({}x{}) uploaded", d.key, d.w, d.h);
    }
  }

  static ImVec2 Rot(ImVec2 v, float c, float s) { return ImVec2(v.x * c - v.y * s, v.x * s + v.y * c); }
  static ImVec2 Add(ImVec2 a, ImVec2 b) { return ImVec2(a.x + b.x, a.y + b.y); }

  // A square image of half-size h turned by `angle` (screen radians, clockwise).
  static void Quad(ImDrawList* dl, ImmediateTexture* tex, ImVec2 c, float h, float angle, ImU32 col) {
    const float cs = std::cos(angle), sn = std::sin(angle);
    const ImVec2 p1 = Add(c, Rot(ImVec2(-h, -h), cs, sn)), p2 = Add(c, Rot(ImVec2(h, -h), cs, sn));
    const ImVec2 p3 = Add(c, Rot(ImVec2(h, h), cs, sn)), p4 = Add(c, Rot(ImVec2(-h, h), cs, sn));
    dl->AddImageQuad(ImTextureRef(reinterpret_cast<ImTextureID>(tex)), p1, p2, p3, p4, ImVec2(0, 0),
                     ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1), col);
  }

  void Draw(ImGuiIO& io, const Snapshot& s, ImmediateTexture* map) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    const float D = size_fraction().load(std::memory_order_relaxed) * H;  // map image size
    const float R = 0.5f * D;
    // The rim's inner edge (222 of 256 texels from its centre) covers the
    // map's edge; its outer edge is at 248.
    const float rim_half = R * 256.0f / 228.0f;
    const float outer = rim_half * 248.0f / 256.0f;
    // The rim is as far from the right screen edge as from the top (32 px at
    // 800), whatever the size; the map moves down by the message bar's
    // height as the health bar does (kHudDropPerUnit).
    const float margin = 0.040f * H;
    const ImVec2 c(W - margin - outer, margin + outer + drop_ * kHudDropPerUnit * H);
    const float a = fade_ * std::clamp(opacity().load(std::memory_order_relaxed), 0.1f, 1.0f);
    auto col = [a](float alpha) {
      return IM_COL32(255, 255, 255, int(std::clamp(alpha * a, 0.0f, 1.0f) * 255.0f));
    };

    // Canvas -> screen: y up (minimap_flip_y: down), rotations clockwise.
    const float ysign = REXCVAR_GET(minimap_flip_y) ? 1.0f : -1.0f;
    const float rsign = float(REXCVAR_GET(minimap_rot_sign));
    const float units = std::max(1.0f, float(REXCVAR_GET(minimap_image_units)));
    const float kmap = D / units;
    constexpr float kDeg = 0.017453292f;

    // Start with the pause map's texture orientation and marker offsets.
    const float map_angle = -rsign * s.north_deg * kDeg;
    const float ang = rsign * (s.north_deg + s.icon_rot_deg) * kDeg;
    const float ca = std::cos(ang), sa = std::sin(ang);
    auto offset = [&](float wx, float wy) {
      const float m = s.icon_scale * 240.0f;
      const ImVec2 r = Rot(ImVec2((wx / s.extent - 0.5f) * m, (wy / s.extent - 0.5f) * m), ca, sa);
      return ImVec2((r.x + s.icon_x) * kmap, ysign * (r.y + s.icon_y) * kmap);
    };
    // Heading is clockwise from world +y. Subtract the pause map's marker
    // rotation for the arrow, then undo the result across the map contents.
    const float hero_angle = rsign * s.hero_heading - ang;
    const bool heading_up = rotation_enabled().load(std::memory_order_relaxed);
    const float turn = heading_up ? -hero_angle : 0.0f;
    const float cos_turn = std::cos(turn), sin_turn = std::sin(turn);
    Quad(dl, map, c, R, map_angle + turn, col(1.0f));

    auto place = [&](float wx, float wy) {
      const ImVec2 p_offset = Rot(offset(wx, wy), cos_turn, sin_turn);
      ImVec2 p = Add(c, p_offset);
      const float dx = p.x - c.x, dy = p.y - c.y, len = std::sqrt(dx * dx + dy * dy), lim = R * 0.9f;
      if (len > lim) p = ImVec2(c.x + dx * lim / len, c.y + dy * lim / len);
      return p;
    };
    if (s.dog_ok) {
      if (ImmediateTexture* paw = Texture(kDogIcon))
        Quad(dl, paw, place(s.dog_x, s.dog_y), 0.05f * D, 0.0f, col(0.95f));
    }
    if (ImmediateTexture* arrow = Texture(kHeroIcon))
      Quad(dl, arrow, place(s.hero_x, s.hero_y), 0.065f * D, heading_up ? 0.0f : hero_angle, col(1.0f));
    if (ImmediateTexture* rim = Texture(kRim)) Quad(dl, rim, c, rim_half, 0.0f, col(1.0f));
    if (!heading_up) {
      if (ImmediateTexture* north = Texture(kNorthIcon))
        Quad(dl, north, ImVec2(c.x, c.y - outer + 0.012f * D), 0.09f * D, 0.0f, col(1.0f));
    }
  }

  static inline std::atomic<MinimapDialog*> instance_{nullptr};
  std::atomic<bool> want_registered_{false};
  bool registered_ = false;  // UI thread only
  TextureLoader loader_;
  std::map<std::string, Tex> textures_;  // kept for the session (up to 1 MB each)
  ImmediateTexture* last_map_ = nullptr;
  Snapshot last_;
  float fade_ = 0.0f;
  float drop_ = 0.0f;
};

}  // namespace fable2::minimap
