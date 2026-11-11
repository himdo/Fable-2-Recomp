// fable_2 - The game's own GUI font for host-drawn overlays (photo mode).
//
// Fable 2 ships its menu typeface in three sizes, data/language/<lang>/fonts/
// gui_large.bft (32 px line on the game's 720p canvas), gui_normal.bft (27 px)
// and gui_small.bft (24 px). Each .bft:
//   +0   u32 version (1), u32 line height (32), u32 glyph slots (400), u32 0,
//        9 bytes of font metrics
//   +25  glyph records, 22 bytes each, big-endian:
//          f32 u0, v0, u1, v1   (pixel / (size - 1) on the atlas)
//          s16 bearing, width, advance   (pixels)
//        Glyph i is character 0x20 + i for printable ASCII; the extended
//        Latin glyphs follow (degree sign = 120, right quote = 101, em dash = 105).
//   end: 512 x H BC4 (one channel) atlas, Xbox 360 tiled, 16-bit byte-swapped
//        (H = 512 for large, 256 for normal / small).
// The files are read from the user's own game data at startup; nothing from
// them is shipped. Text is drawn with the font whose size matches and scaled
// with the window height exactly like the game's own text (1:1 at 720p). The atlas rows the glyphs use go into ImGui's font atlas as a
// custom rectangle (white, alpha = coverage), so text drawn with it batches
// with the rest of the overlay.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <imgui.h>

namespace fable2::gamefont {

struct Glyph {
  float u0 = 0, v0 = 0, u1 = 0, v1 = 0;  // atlas pixels
  int16_t bearing = 0, width = 0, advance = 0;
  bool valid = false;
};

struct Font {
  bool loaded = false;
  int tex_h = 512;
  int line_height = 32;
  std::vector<Glyph> glyphs;
  int atlas_rows = 0;                  // atlas rows copied (0..atlas_rows)
  std::vector<uint8_t> coverage;       // 512 x atlas_rows
  ImFontAtlas* atlas = nullptr;
  ImFontAtlasRectId rect_id = ImFontAtlasRectId_Invalid;
  std::string error;
};

// The three sizes the game uses, by line height on its 720p canvas.
enum Size { kLarge = 0, kNormal = 1, kSmall = 2, kSizeCount = 3 };
inline constexpr float kSizePx[kSizeCount] = {32.0f, 27.0f, 24.0f};
inline constexpr const char* kSizeFile[kSizeCount] = {"gui_large.bft", "gui_normal.bft", "gui_small.bft"};

inline Font& font(int i = kLarge) {
  static Font f[kSizeCount];
  return f[std::clamp(i, 0, kSizeCount - 1)];
}

namespace detail {

inline uint32_t be32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
inline int16_t be16(const uint8_t* p) { return int16_t((uint16_t(p[0]) << 8) | p[1]); }
inline float bef(const uint8_t* p) {
  const uint32_t b = be32(p);
  float f;
  std::memcpy(&f, &b, 4);
  return f;
}

// XGAddress2DTiledOffset for a block-compressed surface (8-byte blocks).
inline uint32_t tiled_offset(uint32_t x, uint32_t y, uint32_t width_blocks) {
  constexpr uint32_t log_bpp = 3;
  const uint32_t aligned = (width_blocks + 31) & ~31u;
  const uint32_t macro = ((x >> 5) + (y >> 5) * (aligned >> 5)) << (log_bpp + 7);
  const uint32_t micro = ((x & 7) + ((y & 0xE) << 2)) << log_bpp;
  const uint32_t offset = macro + ((micro & ~0xFu) << 1) + (micro & 0xF) + ((y & 1) << 4);
  return ((offset & ~0x1FFu) << 3) + ((y & 16) << 7) + ((offset & 0x1C0) << 2) +
         (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (offset & 0x3F);
}

inline void bc4_block(const uint8_t* src, uint8_t out[16]) {
  uint8_t b[8];
  for (int i = 0; i < 8; i += 2) {  // 16-bit byte swap (Xbox 360)
    b[i] = src[i + 1];
    b[i + 1] = src[i];
  }
  const int a0 = b[0], a1 = b[1];
  int pal[8] = {a0, a1};
  if (a0 > a1) {
    for (int i = 1; i <= 6; ++i) pal[i + 1] = ((7 - i) * a0 + i * a1) / 7;
  } else {
    for (int i = 1; i <= 4; ++i) pal[i + 1] = ((5 - i) * a0 + i * a1) / 5;
    pal[6] = 0;
    pal[7] = 255;
  }
  uint64_t bits = 0;
  for (int i = 0; i < 6; ++i) bits |= uint64_t(b[2 + i]) << (8 * i);
  for (int i = 0; i < 16; ++i) out[i] = uint8_t(pal[(bits >> (3 * i)) & 7]);
}

}  // namespace detail

// Parse the .bft and decode the atlas rows the glyphs use.
inline bool Load(const std::filesystem::path& path, int which = kLarge) {
  Font& f = font(which);
  f = Font{};
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    f.error = "not found: " + path.string();
    return false;
  }
  std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  constexpr size_t kRec = 22, kFirst = 25;
  if (d.size() < kFirst + 512 * 64 / 2 || detail::be32(d.data()) != 1) {
    f.error = "unexpected file layout";
    return false;
  }
  f.line_height = int(detail::be32(d.data() + 4));
  const uint32_t slots = std::min<uint32_t>(detail::be32(d.data() + 8), 1024);
  // The atlas is the tallest 512 x H (H a power of two) that fits after the
  // glyph table and the character map.
  const size_t tables = kFirst + slots * kRec;
  f.tex_h = 512;
  while (f.tex_h > 32 && tables + size_t(512) * f.tex_h / 2 > d.size()) f.tex_h /= 2;
  const size_t kTex = size_t(512) * f.tex_h / 2;
  // The font tool wrote texel coordinates as pixel / (size - 1); scaling by
  // size instead drifts up to a pixel across the atlas and clips glyphs.
  const float uscale = 511.0f;
  const float vscale = float(f.tex_h - 1);
  int max_row = 0;
  for (uint32_t i = 0; i < slots && kFirst + (i + 1) * kRec <= d.size() - kTex; ++i) {
    const uint8_t* r = d.data() + kFirst + i * kRec;
    Glyph g;
    g.u0 = std::round(detail::bef(r) * uscale);
    g.v0 = std::round(detail::bef(r + 4) * vscale);
    g.u1 = std::round(detail::bef(r + 8) * uscale);
    g.v1 = std::round(detail::bef(r + 12) * vscale);
    g.bearing = detail::be16(r + 16);
    g.width = detail::be16(r + 18);
    g.advance = detail::be16(r + 20);
    // Unused slots are filler bytes: reject anything off the atlas.
    g.valid = g.u0 >= 0 && g.u1 <= 512.5f && g.v0 >= 0 && g.v1 <= vscale + 0.5f && g.u1 >= g.u0 &&
              g.v1 > g.v0 && g.advance > 0 && g.advance < 128;
    if (!g.valid) break;
    max_row = std::max(max_row, int(g.v1 + 1.0f));
    f.glyphs.push_back(g);
  }
  if (f.glyphs.size() < 95) {
    f.error = "too few glyphs (" + std::to_string(f.glyphs.size()) + ")";
    return false;
  }
  f.atlas_rows = std::min(max_row, f.tex_h);
  f.coverage.assign(size_t(512) * f.atlas_rows, 0);
  const uint8_t* tex = d.data() + d.size() - kTex;
  for (uint32_t by = 0; by * 4 < uint32_t(f.atlas_rows); ++by) {
    for (uint32_t bx = 0; bx < 128; ++bx) {
      const uint32_t off = detail::tiled_offset(bx, by, 128);
      if (off + 8 > kTex) continue;
      uint8_t px[16];
      detail::bc4_block(tex + off, px);
      for (int y = 0; y < 4; ++y) {
        const int row = int(by * 4) + y;
        if (row >= f.atlas_rows) break;
        std::memcpy(&f.coverage[size_t(row) * 512 + bx * 4], px + y * 4, 4);
      }
    }
  }
  f.loaded = true;
  return true;
}

// Put the decoded glyph rows into ImGui's atlas. Call from the app's
// OnConfigureFonts (before the SDK uploads the font texture).
// Put every loaded size into ImGui's atlas. Call from the app's
// OnConfigureFonts (before the SDK uploads the font texture). All rectangles
// are reserved first and filled after, so a texture that grows while packing
// cannot lose pixels already written.
inline int AddAllToAtlas(ImFontAtlas* atlas) {
  if (!atlas) return 0;
  // Settle the default fonts first; the rects then pack into that texture.
  if (!atlas->TexIsBuilt) atlas->Build();
  for (int i = 0; i < kSizeCount; ++i) {
    Font& f = font(i);
    if (!f.loaded) continue;
    f.rect_id = atlas->AddCustomRect(512, f.atlas_rows, nullptr);
    if (f.rect_id == ImFontAtlasRectId_Invalid) {
      f.error = "no room in the ImGui atlas";
      f.loaded = false;
    }
  }
  ImTextureData* tex = atlas->TexData;
  int added = 0;
  for (int i = 0; i < kSizeCount; ++i) {
    Font& f = font(i);
    if (!f.loaded) continue;
    ImFontAtlasRect r;
    if (!tex || !tex->Pixels || !atlas->GetCustomRect(f.rect_id, &r)) {
      f.error = "ImGui atlas has no pixels";
      f.loaded = false;
      continue;
    }
    for (int y = 0; y < f.atlas_rows; ++y) {
      for (int x = 0; x < 512; ++x) {
        const uint8_t a = f.coverage[size_t(y) * 512 + x];
        uint8_t* p = static_cast<uint8_t*>(tex->GetPixelsAt(r.x + x, r.y + y));
        if (tex->BytesPerPixel == 4) {
          p[0] = p[1] = p[2] = 255;
          p[3] = a;
        } else {
          p[0] = a;
        }
      }
    }
    f.atlas = atlas;
    f.coverage.clear();
    f.coverage.shrink_to_fit();
    ++added;
  }
  return added;
}

inline bool Ready(int which = kLarge) { return font(which).loaded && font(which).atlas != nullptr; }

inline bool AnyReady() { return Ready(kLarge) || Ready(kNormal) || Ready(kSmall); }

// The loaded font closest to `size` (720p line height), large as the fallback.
inline const Font& Pick(float size) {
  int best = -1;
  float err = 1e9f;
  for (int i = 0; i < kSizeCount; ++i) {
    if (!Ready(i)) continue;
    const float e = std::fabs(kSizePx[i] - size);
    if (e < err) {
      err = e;
      best = i;
    }
  }
  return font(best < 0 ? kLarge : best);
}

// Next code point from UTF-8 (advances p).
inline uint32_t NextCodepoint(const char*& p) {
  const uint8_t c = uint8_t(*p++);
  if (c < 0x80) return c;
  int n = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : 1;
  uint32_t cp = c & (0x3F >> n);
  while (n-- > 0 && (uint8_t(*p) & 0xC0) == 0x80) cp = (cp << 6) | (uint8_t(*p++) & 0x3F);
  return cp;
}

inline int GlyphIndex(uint32_t cp) {
  if (cp >= 0x20 && cp <= 0x7E) return int(cp - 0x20);
  switch (cp) {
    case 0x00B0: return 120;  // degree
    case 0x2019: return 101;  // right single quote
    case 0x2014: return 105;  // em dash
    case 0x00D7: return int('x' - 0x20);  // multiplication sign: not in the font
    default: return int('?' - 0x20);
  }
}

// Width of `text` drawn `px` pixels tall with the font for design size `size`
// (the game's 720p line height: 32 / 27 / 24).
inline float Measure(const char* text, float px, float size) {
  const Font& f = Pick(size);
  if (!f.loaded) return 0.0f;
  const float s = px / float(f.line_height);
  float w = 0.0f;
  for (const char* p = text; *p;) {
    const int gi = GlyphIndex(NextCodepoint(p));
    if (gi >= 0 && gi < int(f.glyphs.size())) w += f.glyphs[gi].advance * s;
  }
  return w;
}

// Draw `text` with its top-left at `pos`, `px` pixels tall, with the font for
// design size `size`.
inline void Draw(ImDrawList* dl, ImVec2 pos, float px, float size, ImU32 col, const char* text) {
  const Font& f = Pick(size);
  if (!f.loaded || !f.atlas) return;
  ImFontAtlasRect r;
  if (!f.atlas->GetCustomRect(f.rect_id, &r)) return;
  const float s = px / float(f.line_height);
  const float du = (r.uv1.x - r.uv0.x) / 512.0f;
  const float dv = (r.uv1.y - r.uv0.y) / float(f.atlas_rows);
  const ImTextureRef tex = dl->_CmdHeader.TexRef;
  float x = pos.x;
  for (const char* p = text; *p;) {
    const int gi = GlyphIndex(NextCodepoint(p));
    if (gi < 0 || gi >= int(f.glyphs.size())) continue;
    const Glyph& g = f.glyphs[gi];
    if (g.u1 > g.u0 + 0.5f) {
      const float gx = x + g.bearing * s;
      const float gw = (g.u1 - g.u0) * s;
      const float gh = (g.v1 - g.v0) * s;
      dl->AddImage(tex, ImVec2(gx, pos.y), ImVec2(gx + gw, pos.y + gh),
                   ImVec2(r.uv0.x + g.u0 * du, r.uv0.y + g.v0 * dv),
                   ImVec2(r.uv0.x + g.u1 * du, r.uv0.y + g.v1 * dv), col);
    }
    x += g.advance * s;
  }
}

}  // namespace fable2::gamefont
