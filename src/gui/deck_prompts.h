// Steam Deck button prompts: the game's Xbox 360 controller icons show Steam
// Deck buttons instead, in the style of the game's own icons.
//
// The glyphs (letters, symbols, shapes) come from PNG files in
// assets/deck_prompts/<set>/ next to the executable ("dark": dark grey
// buttons, "light": white ones):
//
//   A/B/X/Y        shared_button_a/b/x/y
//   LB/RB, LT/RT   sd_l1/sd_r1, sd_l2/sd_r2
//   Back/Start     sd_button_view/sd_button_menu
//   D-pad, sticks  shared_dpad, shared_lstick(_up/_down/_left/_right), shared_rstick(...)
//
// They are set the way the game sets its own: A/B/X/Y as shaded orbs with a
// faint dark outline, everything else on the game's green medallion (taken
// from its Back icon, with the button removed). The two atlases, the in-game
// A/B/Y/X sheet and the menu frames' A and B medallions, get the same orbs.
// Works on full-RGBA payloads (size prefix, then A,R,G,B in Xenos tile order),
// the form src/gui/prompt_textures.h decodes every prompt texture to. A glyph
// file that is missing leaves its texture as the game has it.
#ifndef FABLE2_GUI_DECK_PROMPTS_H
#define FABLE2_GUI_DECK_PROMPTS_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <rex/filesystem.h>
#include <rex/ui/image_decode.h>

#include "xenos_tiling.h"

namespace fable2::gui {

// What the Deck prompts are made from: the glyph set, and the game's green
// medallion at 64 and 32 pixels (linear A,R,G,B; empty when not found).
struct DeckArt {
  std::string set;
  std::vector<uint8_t> medallion64, medallion32;
};

namespace deck_detail {

inline constexpr const char* kGlyphFolder = "assets/deck_prompts";

// A linear A,R,G,B image.
struct Image {
  int w = 0, h = 0;
  std::vector<uint8_t> px;
  Image() = default;
  Image(int w_, int h_) : w(w_), h(h_), px((size_t)w_ * h_ * 4, 0) {}
  uint8_t* at(int x, int y) { return px.data() + ((size_t)y * w + x) * 4; }
  const uint8_t* at(int x, int y) const { return px.data() + ((size_t)y * w + x) * 4; }
};

inline Image FromTiled(const std::vector<uint8_t>& tiled, int w, int h) {
  Image im(w, h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) std::copy_n(tiled.data() + 4 + XenosTiledOffset(x, y, w), 4, im.at(x, y));
  return im;
}

inline void ToTiled(const Image& im, std::vector<uint8_t>& tiled, int tex_w, int ox = 0,
                    int oy = 0) {
  for (int y = 0; y < im.h; ++y)
    for (int x = 0; x < im.w; ++x)
      std::copy_n(im.at(x, y), 4, tiled.data() + 4 + XenosTiledOffset(ox + x, oy + y, tex_w));
}

inline uint8_t Clamp8(float v) { return (uint8_t)std::lround(std::fmin(std::fmax(v, 0.0f), 255.0f)); }

// Porter-Duff "over" of a colour with alpha a (0..1) onto a pixel.
inline void Over(uint8_t* d, float r, float g, float b, float a) {
  if (a <= 0.0f) return;
  a = std::fmin(a, 1.0f);
  const float da = d[0] / 255.0f, oa = a + da * (1.0f - a);
  d[1] = Clamp8((r * a + d[1] * da * (1.0f - a)) / oa);
  d[2] = Clamp8((g * a + d[2] * da * (1.0f - a)) / oa);
  d[3] = Clamp8((b * a + d[3] * da * (1.0f - a)) / oa);
  d[0] = Clamp8(oa * 255.0f);
}

inline void Over(Image& dst, const Image& src, int ox, int oy) {
  for (int y = 0; y < src.h; ++y)
    for (int x = 0; x < src.w; ++x) {
      const int tx = ox + x, ty = oy + y;
      if (tx < 0 || ty < 0 || tx >= dst.w || ty >= dst.h) continue;
      const uint8_t* s = src.at(x, y);
      Over(dst.at(tx, ty), s[1], s[2], s[3], s[0] / 255.0f);
    }
}

// Loads assets/deck_prompts/<set>/<stem>.png once, cropped to its opaque
// pixels; an empty image when missing.
inline const Image& LoadGlyph(const std::string& set, const std::string& stem) {
  static std::mutex lock;
  static std::map<std::string, Image> cache;
  std::lock_guard guard(lock);
  const std::string key = set + "/" + stem;
  auto it = cache.find(key);
  if (it != cache.end()) return it->second;
  Image& out = cache[key];
  const auto path =
      rex::filesystem::GetExecutableFolder() / kGlyphFolder / set / (stem + ".png");
  std::ifstream file(path, std::ios::binary);
  if (!file) return out;
  const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file), {}};
  int w = 0, h = 0;
  const auto rgba = rex::ui::DecodeImageRGBA(bytes.data(), bytes.size(), w, h);
  if (rgba.size() != size_t(w) * h * 4) return out;
  int x0 = w, y0 = h, x1 = -1, y1 = -1;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
      if (rgba[((size_t)y * w + x) * 4 + 3] > 8) {
        x0 = std::min(x0, x); y0 = std::min(y0, y);
        x1 = std::max(x1, x); y1 = std::max(y1, y);
      }
  if (x1 < 0) return out;
  out = Image(x1 - x0 + 1, y1 - y0 + 1);
  for (int y = 0; y < out.h; ++y)
    for (int x = 0; x < out.w; ++x) {
      const uint8_t* s = rgba.data() + ((size_t)(y0 + y) * w + x0 + x) * 4;
      uint8_t* d = out.at(x, y);
      d[0] = s[3]; d[1] = s[0]; d[2] = s[1]; d[3] = s[2];
    }
  return out;
}

// An image scaled to w x h (area average with premultiplied alpha).
inline Image Scaled(const Image& g, int w, int h) {
  Image out(w, h);
  const double sx = double(g.w) / w, sy = double(g.h) / h;
  for (int y = 0; y < h; ++y) {
    const double y0 = y * sy, y1 = (y + 1) * sy;
    for (int x = 0; x < w; ++x) {
      const double x0 = x * sx, x1 = (x + 1) * sx;
      double a = 0, r = 0, gr = 0, b = 0, area = 0;
      for (int py = int(y0); py < int(std::ceil(y1)) && py < g.h; ++py) {
        const double wy = std::min(y1, py + 1.0) - std::max(y0, double(py));
        for (int px = int(x0); px < int(std::ceil(x1)) && px < g.w; ++px) {
          const double wgt = wy * (std::min(x1, px + 1.0) - std::max(x0, double(px)));
          const uint8_t* s = g.at(px, py);
          const double sa = s[0] / 255.0 * wgt;
          r += s[1] * sa; gr += s[2] * sa; b += s[3] * sa; a += sa;
          area += wgt;
        }
      }
      if (a <= 0.0 || area <= 0.0) continue;
      uint8_t* o = out.at(x, y);
      o[0] = Clamp8(float(a / area * 255.0));
      o[1] = Clamp8(float(r / a));
      o[2] = Clamp8(float(gr / a));
      o[3] = Clamp8(float(b / a));
    }
  }
  return out;
}

// A soft outline: the image's alpha grown by `radius` pixels, in one colour.
inline Image Outline(const Image& src, float radius, float r, float g, float b, float strength) {
  const int pad = int(std::ceil(radius)) + 1;
  Image out(src.w + 2 * pad, src.h + 2 * pad);
  const int ri = int(std::ceil(radius));
  for (int y = 0; y < out.h; ++y)
    for (int x = 0; x < out.w; ++x) {
      float best = 0.0f;
      for (int dy = -ri; dy <= ri; ++dy)
        for (int dx = -ri; dx <= ri; ++dx) {
          const int sx = x - pad + dx, sy = y - pad + dy;
          if (sx < 0 || sy < 0 || sx >= src.w || sy >= src.h) continue;
          const float d = std::sqrt(float(dx * dx + dy * dy));
          const float w = std::fmin(std::fmax(radius + 0.5f - d, 0.0f), 1.0f);
          best = std::fmax(best, src.at(sx, sy)[0] / 255.0f * w);
        }
      uint8_t* o = out.at(x, y);
      o[0] = Clamp8(best * strength * 255.0f);
      o[1] = Clamp8(r); o[2] = Clamp8(g); o[3] = Clamp8(b);
    }
  return out;
}

// The game's orb look on a round glyph of diameter `d`: shading towards the
// bottom, a soft shine on top, and a faint dark outline. `dim` darkens it (the
// A/B/Y/X sheet's second row). Returns an image of d + 2 * margin pixels.
inline Image Orb(const Image& glyph, int d, int margin, float dim = 1.0f) {
  Image g = Scaled(glyph, d, d);
  for (int y = 0; y < d; ++y)
    for (int x = 0; x < d; ++x) {
      uint8_t* p = g.at(x, y);
      if (!p[0]) continue;
      const float ny = (y + 0.5f) / d;          // 0 top .. 1 bottom
      const float shade = (1.0f - 0.22f * ny) * dim;
      for (int c = 1; c < 4; ++c) p[c] = Clamp8(p[c] * shade);
      // Shine: a soft ellipse across the upper part.
      const float ex = ((x + 0.5f) / d - 0.5f) / 0.36f, ey = (ny - 0.27f) / 0.19f;
      const float shine = std::fmax(0.0f, 1.0f - (ex * ex + ey * ey));
      Over(p, 255, 255, 255, 0.20f * shine * shine * dim);
    }
  Image out(d + 2 * margin, d + 2 * margin);
  const float edge = std::fmin(std::fmax(d * 0.035f, 1.0f), margin * 0.75f);
  Over(out, Outline(g, edge, 20, 15, 10, 1.0f), margin - int(std::ceil(edge)) - 1,
       margin - int(std::ceil(edge)) - 1);
  Over(out, g, margin, margin);
  return out;
}

// The game's medallion without its button, at the Back icon's size: the ring
// is kept as drawn, and the inside (where the button was) is filled from the
// ring's inner edge, so the lighting around the ring carries through.
inline std::vector<uint8_t> MakeMedallion(const std::vector<uint8_t>& back_tiled, int size) {
  if (back_tiled.size() != 4 + (size_t)size * size * 4) return {};
  const Image src = FromTiled(back_tiled, size, size);
  Image out = src;
  const float c = size * 0.5f, cut = size * (24.0f / 64.0f);
  auto sample = [&](float x, float y, int ch) {
    x = std::fmin(std::fmax(x - 0.5f, 0.0f), size - 1.001f);
    y = std::fmin(std::fmax(y - 0.5f, 0.0f), size - 1.001f);
    const int x0 = int(x), y0 = int(y);
    const float fx = x - x0, fy = y - y0;
    auto v = [&](int xx, int yy) { return float(src.at(xx, yy)[ch]); };
    return (v(x0, y0) * (1 - fx) + v(x0 + 1, y0) * fx) * (1 - fy) +
           (v(x0, y0 + 1) * (1 - fx) + v(x0 + 1, y0 + 1) * fx) * fy;
  };
  for (int y = 0; y < size; ++y)
    for (int x = 0; x < size; ++x) {
      const float dx = x + 0.5f - c, dy = y + 0.5f - c, r = std::hypot(dx, dy);
      if (r >= cut) continue;
      // Average the ring's inner edge over a small arc, and darken towards the
      // middle a little, like a shallow dish.
      const float a = std::atan2(dy, dx);
      float col[4] = {0, 0, 0, 0};
      for (int k = -2; k <= 2; ++k) {
        const float aa = a + k * 0.08f;
        const float sx = c + (cut + 0.5f) * std::cos(aa), sy = c + (cut + 0.5f) * std::sin(aa);
        for (int ch = 0; ch < 4; ++ch) col[ch] += sample(sx, sy, ch) / 5.0f;
      }
      const float depth = 1.0f - 0.18f * (1.0f - r / cut);
      uint8_t* o = out.at(x, y);
      o[0] = Clamp8(col[0]);
      for (int ch = 1; ch < 4; ++ch) o[ch] = Clamp8(col[ch] * depth);
    }
  return out.px;
}

// A glyph on the medallion: scaled to sit inside the ring, with a thin dark
// edge so it stands off the green.
inline Image OnMedallion(const std::vector<uint8_t>& medallion, int size, const Image& glyph) {
  Image out(size, size);
  out.px = medallion;
  // Half-diagonal of the glyph's box: about as big as the game's own buttons,
  // which fill most of the medallion and overlap its ring.
  const float fit = size * 0.47f;
  const float s = fit / std::hypot(glyph.w * 0.5f, glyph.h * 0.5f);
  const int w = std::max(1, int(std::lround(glyph.w * s)));
  const int h = std::max(1, int(std::lround(glyph.h * s)));
  const Image g = Scaled(glyph, w, h);
  const float edge = std::fmax(0.8f, size / 48.0f);
  const Image o = Outline(g, edge, 18, 26, 12, 0.65f);
  const int ox = (size - w) / 2, oy = (size - h) / 2;
  Over(out, o, ox - int(std::ceil(edge)) - 1, oy - int(std::ceil(edge)) - 1);
  Over(out, g, ox, oy);
  return out;
}

// A glyph scaled to fit a w x h box (keeping its shape, centred) with a thin
// dark edge, drawn over `im` at (x0, y0) after clearing the box.
inline void FitInto(Image& im, const Image& glyph, int x0, int y0, int w, int h) {
  for (int y = y0; y < y0 + h; ++y)
    for (int x = x0; x < x0 + w; ++x)
      if (x >= 0 && y >= 0 && x < im.w && y < im.h) std::fill_n(im.at(x, y), 4, uint8_t(0));
  const float edge = std::fmax(0.8f, std::fmin(w, h) / 32.0f);
  const int pad = int(std::ceil(edge)) + 1;
  const float s = std::fmin(float(w - 2 * pad) / glyph.w, float(h - 2 * pad) / glyph.h);
  const int gw = std::max(1, int(std::lround(glyph.w * s)));
  const int gh = std::max(1, int(std::lround(glyph.h * s)));
  const Image g = Scaled(glyph, gw, gh);
  const int ox = x0 + (w - gw) / 2, oy = y0 + (h - gh) / 2;
  Over(im, Outline(g, edge, 20, 15, 10, 0.9f), ox - pad, oy - pad);
  Over(im, g, ox, oy);
}

// The glyph for a controller icon, by lowercase bank entry name; "" when the
// icon isn't a button prompt.
inline std::string GlyphFor(const std::string& name) {
  const std::string prefix = "art\\gui\\controller\\";
  if (name.rfind(prefix, 0) != 0 || name.size() < prefix.size() + 4 ||
      name.compare(name.size() - 4, 4, ".tex") != 0)
    return "";
  const size_t bs = name.rfind('\\');
  std::string leaf = name.substr(bs + 1, name.size() - 4 - (bs + 1));
  if (leaf.rfind("anim", 0) == 0) leaf = leaf.substr(4);  // animated stick icons
  static const std::map<std::string, std::string> glyphs = {
      {"icon_button_a", "shared_button_a"}, {"icon_button_b", "shared_button_b"},
      {"icon_button_x", "shared_button_x"}, {"icon_button_y", "shared_button_y"},
      {"leftbutton", "sd_l1"},              {"rightbutton", "sd_r1"},
      {"lefttrigger", "sd_l2"},             {"icon_button_lt", "sd_l2"},
      {"righttrigger", "sd_r2"},            {"back", "sd_button_view"},
      {"start", "sd_button_menu"},          {"dpad", "shared_dpad"},
      {"left", "shared_lstick"},            {"leftup", "shared_lstick_up"},
      {"leftdown", "shared_lstick_down"},   {"leftleft", "shared_lstick_left"},
      {"leftright", "shared_lstick_right"}, {"right", "shared_rstick"},
      {"rightup", "shared_rstick_up"},      {"rightdown", "shared_rstick_down"},
      {"rightleft", "shared_rstick_left"},  {"rightright", "shared_rstick_right"},
  };
  auto it = glyphs.find(leaf);
  return it == glyphs.end() ? "" : it->second;
}

inline bool IsFaceGlyph(const std::string& stem) { return stem.rfind("shared_button_", 0) == 0; }

inline constexpr const char* kFaceAtlas = "art\\gui\\persistant\\abyx.tex";
inline constexpr const char* kFramesAtlas = "art\\gui\\frames\\frames_elements.tex";
// Small leftovers: the HUD sheet's Back and Start ovals, and frames_04's
// D-pad and coloured A/B/X/Y cluster.
inline constexpr const char* kHudAtlas = "art\\gui\\persistant\\hud.tex";
inline constexpr const char* kFrames04 = "art\\gui\\frames\\frames_04.tex";

}  // namespace deck_detail

// The bank entries the medallion is taken from (the Back icon, which leaves
// the most of the ring visible), at 64 and 32 pixels.
inline constexpr const char* kDeckMedallion64 = "art\\gui\\controller\\back.tex";
inline constexpr const char* kDeckMedallion32 = "art\\gui\\controller\\24x24\\back.tex";

// True for the prompt textures the Steam Deck prompts replace.
inline bool HasDeckStyle(const std::string& lower_name) {
  using namespace deck_detail;
  return lower_name == kFaceAtlas || lower_name == kFramesAtlas || lower_name == kHudAtlas ||
         lower_name == kFrames04 || !GlyphFor(lower_name).empty();
}

// Puts the Steam Deck prompt into one texture (full-RGBA tiled payload, w x h).
// Returns false, leaving it untouched, when it isn't a Deck prompt or what it
// needs is missing.
inline bool ApplyDeckStyle(const std::string& lower_name, std::vector<uint8_t>& tiled, int w,
                           int h, const DeckArt& art) {
  using namespace deck_detail;
  if (tiled.size() != 4 + (size_t)w * h * 4) return false;
  auto glyph = [&art](const std::string& stem) -> const Image* {
    const Image& g = LoadGlyph(art.set, stem);
    return g.px.empty() ? nullptr : &g;
  };
  if (lower_name == kFaceAtlas) {
    // A, B, Y, X in row 0, the same buttons dimmed in row 1; the game's orbs
    // are about 46 pixels across in their 64-pixel cells.
    if (w != 256 || h != 256) return false;
    const char* stems[4] = {"shared_button_a", "shared_button_b", "shared_button_y",
                            "shared_button_x"};
    for (const char* s : stems)
      if (!glyph(s)) return false;
    for (int i = 0; i < 4; ++i)
      for (int row = 0; row < 2; ++row)
        ToTiled(Orb(*glyph(stems[i]), 46, 9, row ? 0.6f : 1.0f), tiled, w, i * 64, row * 64);
    return true;
  }
  if (lower_name == kFramesAtlas) {
    // The Back medallion's B and the selection medallion's A: an orb over the
    // coloured one inside each.
    if (w != 512 || h != 512) return false;
    struct Spot { const char* stem; float cx, cy, r; };
    const Spot spots[2] = {{"shared_button_b", 69.0f, 215.0f, 24.0f},
                           {"shared_button_a", 35.5f, 395.0f, 23.0f}};
    for (const Spot& s : spots)
      if (!glyph(s.stem)) return false;
    Image im = FromTiled(tiled, w, h);
    for (const Spot& s : spots) {
      const int d = int(std::lround(s.r * 2.0f)) - 2, m = 2;
      const Image orb = Orb(*glyph(s.stem), d, m);
      Over(im, orb, int(std::lround(s.cx - orb.w / 2.0f)), int(std::lround(s.cy - orb.h / 2.0f)));
    }
    ToTiled(im, tiled, w);
    return true;
  }
  if (lower_name == kHudAtlas) {
    // Back and Start: small grey ovals with an arrow.
    if (w != 512 || h != 512 || !glyph("sd_button_view") || !glyph("sd_button_menu")) return false;
    Image im = FromTiled(tiled, w, h);
    FitInto(im, *glyph("sd_button_view"), 394, 162, 46, 37);
    FitInto(im, *glyph("sd_button_menu"), 393, 202, 46, 38);
    ToTiled(im, tiled, w);
    return true;
  }
  if (lower_name == kFrames04) {
    // A D-pad, and the four coloured face buttons on a gold four-lobed plate.
    // The plate stays: its coloured dots are painted over with the plate
    // around them, and a small Deck button goes in each lobe.
    if (w != 512 || h != 512 || !glyph("shared_dpad")) return false;
    Image im = FromTiled(tiled, w, h);
    FitInto(im, *glyph("shared_dpad"), 10, 10, 45, 45);
    // The dots are fully saturated and bright; the plate is a darker gold
    // (hue 20-30 degrees).
    const int px0 = 70, px1 = 122, py0 = 5, py1 = 60;
    auto is_dot = [&](const uint8_t* p) {
      const float r = p[1] / 255.0f, g = p[2] / 255.0f, b = p[3] / 255.0f;
      const float mx = std::fmax(r, std::fmax(g, b)), mn = std::fmin(r, std::fmin(g, b));
      if (p[0] < 32 || mx < 0.7f || mx <= 0.0f || (mx - mn) / mx < 0.8f) return false;
      float hue = mx == r ? 60.0f * std::fmod((g - b) / (mx - mn), 6.0f)
                  : mx == g ? 60.0f * ((b - r) / (mx - mn) + 2.0f)
                            : 60.0f * ((r - g) / (mx - mn) + 4.0f);
      if (hue < 0) hue += 360.0f;
      return hue < 15.0f || hue >= 35.0f;
    };
    std::vector<uint8_t> mask(size_t(px1 - px0) * (py1 - py0), 0);
    auto m = [&](int x, int y) -> uint8_t& { return mask[size_t(y - py0) * (px1 - px0) + (x - px0)]; };
    int left = 0;
    for (int y = py0; y < py1; ++y)
      for (int x = px0; x < px1; ++x)
        if (is_dot(im.at(x, y))) m(x, y) = 1, ++left;
    // Fill from the edges inwards with the average of the plate pixels around.
    for (int pass = 0; pass < 16 && left > 0; ++pass) {
      std::vector<std::pair<int, int>> done;
      for (int y = py0; y < py1; ++y)
        for (int x = px0; x < px1; ++x) {
          if (!m(x, y)) continue;
          float sum[3] = {0, 0, 0};
          int n = 0;
          for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
              const int nx = x + dx, ny = y + dy;
              if (nx < px0 || ny < py0 || nx >= px1 || ny >= py1 || m(nx, ny)) continue;
              const uint8_t* q = im.at(nx, ny);
              if (q[0] < 128) continue;
              for (int c = 0; c < 3; ++c) sum[c] += q[1 + c];
              ++n;
            }
          if (n == 0) continue;
          uint8_t* p = im.at(x, y);
          for (int c = 0; c < 3; ++c) p[1 + c] = Clamp8(sum[c] / n);
          done.push_back({x, y});
        }
      for (const auto& [x, y] : done) m(x, y) = 0;
      left -= int(done.size());
    }
    // The plate spans x 73..118, y 9..55 (centre 96, 32.5). Each button sits on
    // a lobe's axis, 5.5 pixels in from the plate's outer edge (radius 7).
    struct Lobe { const char* stem; float cx, cy; };
    const Lobe lobes[4] = {{"shared_button_y", 96.0f, 21.5f}, {"shared_button_a", 96.0f, 43.5f},
                           {"shared_button_x", 85.5f, 32.5f}, {"shared_button_b", 106.5f, 32.5f}};
    for (const Lobe& l : lobes) {
      if (!glyph(l.stem)) return false;
      // Drawn at 4x and placed to a quarter pixel, since the centres fall
      // between pixels.
      const Image big = Orb(*glyph(l.stem), 56, 4);  // 64 x 64: a 14-pixel button
      const float x = l.cx - big.w / 8.0f, y = l.cy - big.h / 8.0f;
      const int ix = int(std::floor(x)), iy = int(std::floor(y));
      const int fx = int(std::lround((x - ix) * 4)), fy = int(std::lround((y - iy) * 4));
      Image padded((big.w + fx + 3) / 4 * 4, (big.h + fy + 3) / 4 * 4);
      Over(padded, big, fx, fy);
      Over(im, Scaled(padded, padded.w / 4, padded.h / 4), ix, iy);
    }
    ToTiled(im, tiled, w);
    return true;
  }
  if (w != h) return false;
  const std::string stem = GlyphFor(lower_name);
  const Image* g = stem.empty() ? nullptr : glyph(stem);
  if (!g) return false;
  if (IsFaceGlyph(stem)) {
    // The game's orbs fill about three quarters of their icon.
    const int d = int(std::lround(w * 0.75f)) & ~1, m = (w - d) / 2;
    ToTiled(Orb(*g, d, m), tiled, w);
    return true;
  }
  const std::vector<uint8_t>& medallion = w == 64 ? art.medallion64 : art.medallion32;
  if (medallion.size() != (size_t)w * h * 4) return false;
  ToTiled(OnMedallion(medallion, w, *g), tiled, w);
  return true;
}

}  // namespace fable2::gui

#endif  // FABLE2_GUI_DECK_PROMPTS_H
