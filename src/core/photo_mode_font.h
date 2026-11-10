// fable_2 - The game's own GUI font for host-drawn overlays (photo mode).
//
// Fable 2 ships its menu typeface as data/language/<lang>/fonts/gui_large.bft:
//   +0   u32 version (1), u32 line height (32), u32 glyph slots (400), u32 0,
//        9 bytes of font metrics
//   +25  glyph records, 22 bytes each, big-endian:
//          f32 u0, v0, u1, v1   (0..1 on the atlas)
//          s16 bearing, width, advance   (pixels)
//        Glyph i is character 0x20 + i for printable ASCII; the extended
//        Latin glyphs follow (degree sign = 120, right quote = 101, em dash = 105).
//   end - 131072: 512 x 512 BC4 (one channel) atlas, Xbox 360 tiled, 16-bit
//        byte-swapped.
// The file is read from the user's own game data at startup; nothing from it
// is shipped. The atlas rows the glyphs use go into ImGui's font atlas as a
// custom rectangle (white, alpha = coverage), so text drawn with it batches
// with the rest of the overlay.
#pragma once

#include <algorithm>
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
  int line_height = 32;
  std::vector<Glyph> glyphs;
  int atlas_rows = 0;                  // atlas rows copied (0..atlas_rows)
  std::vector<uint8_t> coverage;       // 512 x atlas_rows
  ImFontAtlas* atlas = nullptr;
  ImFontAtlasRectId rect_id = ImFontAtlasRectId_Invalid;
  std::string error;
};

inline Font& font() {
  static Font f;
  return f;
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
inline bool Load(const std::filesystem::path& path) {
  Font& f = font();
  f = Font{};
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    f.error = "not found: " + path.string();
    return false;
  }
  std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  constexpr size_t kTex = 512 * 512 / 2;
  constexpr size_t kRec = 22, kFirst = 25;
  if (d.size() < kFirst + kTex || detail::be32(d.data()) != 1) {
    f.error = "unexpected file layout";
    return false;
  }
  f.line_height = int(detail::be32(d.data() + 4));
  const uint32_t slots = std::min<uint32_t>(detail::be32(d.data() + 8), 1024);
  int max_row = 0;
  for (uint32_t i = 0; i < slots && kFirst + (i + 1) * kRec <= d.size() - kTex; ++i) {
    const uint8_t* r = d.data() + kFirst + i * kRec;
    Glyph g;
    g.u0 = detail::bef(r) * 512.0f;
    g.v0 = detail::bef(r + 4) * 512.0f;
    g.u1 = detail::bef(r + 8) * 512.0f;
    g.v1 = detail::bef(r + 12) * 512.0f;
    g.bearing = detail::be16(r + 16);
    g.width = detail::be16(r + 18);
    g.advance = detail::be16(r + 20);
    // Unused slots are filler bytes: reject anything off the atlas.
    g.valid = g.u0 >= 0 && g.u1 <= 512.5f && g.v0 >= 0 && g.v1 <= 512.5f && g.u1 >= g.u0 &&
              g.v1 > g.v0 && g.advance > 0 && g.advance < 128;
    if (!g.valid) break;
    max_row = std::max(max_row, int(g.v1 + 1.0f));
    f.glyphs.push_back(g);
  }
  if (f.glyphs.size() < 95) {
    f.error = "too few glyphs (" + std::to_string(f.glyphs.size()) + ")";
    return false;
  }
  f.atlas_rows = std::min(max_row, 512);
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
inline bool AddToAtlas(ImFontAtlas* atlas) {
  Font& f = font();
  if (!f.loaded || !atlas) return false;
  atlas->Build();  // settle the default fonts first; the rect then packs into that texture
  ImFontAtlasRect r;
  f.rect_id = atlas->AddCustomRect(512, f.atlas_rows, &r);
  if (f.rect_id == ImFontAtlasRectId_Invalid || !atlas->GetCustomRect(f.rect_id, &r)) {
    f.error = "no room in the ImGui atlas";
    f.loaded = false;
    return false;
  }
  ImTextureData* tex = atlas->TexData;
  if (!tex || !tex->Pixels) {
    f.error = "ImGui atlas has no pixels";
    f.loaded = false;
    return false;
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
  return true;
}

inline bool Ready() { return font().loaded && font().atlas != nullptr; }

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

// Width of `text` at `size` pixels (line height).
inline float Measure(const char* text, float size) {
  const Font& f = font();
  if (!f.loaded) return 0.0f;
  const float s = size / float(f.line_height);
  float w = 0.0f;
  for (const char* p = text; *p;) {
    const int gi = GlyphIndex(NextCodepoint(p));
    if (gi >= 0 && gi < int(f.glyphs.size())) w += f.glyphs[gi].advance * s;
  }
  return w;
}

// Draw `text` with its top-left at `pos`; `size` is the line height in pixels.
inline void Draw(ImDrawList* dl, ImVec2 pos, float size, ImU32 col, const char* text) {
  const Font& f = font();
  if (!f.loaded || !f.atlas) return;
  ImFontAtlasRect r;
  if (!f.atlas->GetCustomRect(f.rect_id, &r)) return;
  const float s = size / float(f.line_height);
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
