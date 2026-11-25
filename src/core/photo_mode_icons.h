// fable_2 - The game's own controller button icons for host-drawn overlays
// (photo mode prompts), read straight from the user's game data at startup:
// data/art/gui/gui_textures.bnk (pixels) + gui_texture_headers.bnk (sizes),
// with data/art/localised/<lang>/gui/ checked first, the way the game does
// (and the pristine <bank>.orig when the keyboard-prompt swap rewrote a bank).
// Nothing from them is shipped, and a replaced (higher resolution) bank is
// picked up as long as its textures use a format decoded here.
//
// Bank layout (big-endian):
//   +0  u32 data offset (0x8000), u32 version (3), u8 0,
//       u32 compressed index size, u32 index size
//   +17 zlib index (sync-flushed, no end marker):
//       u32 count, then per entry u32 name length, name (NUL included),
//       u32 offset (from the data offset), u32 size
// Texture header (84 bytes): +16 u32 width, +20 height, +24 format, +28 mips.
// Texture data is either the raw surface after a u32 byte count (format 2,
// 32-bit ARGB) or a chunk table of {u32 type, offset, size} at +0 where type 7
// is the raw surface (format 39, DXT5). Other chunk types are Lionhead's own
// image codec, decoded with src/gui/gui_tex_codec.h (the game's prompt-icon
// decoder) into the format 2 layout.
// Surfaces are Xbox 360 tiled when large enough to fill a tile, else linear;
// DXT blocks are 16-bit byte-swapped.
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <imgui.h>
#include <zlib-ng.h>

#include "../gui/deck_prompts.h"
#include "../gui/gui_tex_codec.h"

namespace fable2::gameicons {

enum Icon { kA, kB, kX, kY, kLB, kRB, kLT, kRT, kLS, kRS, kStart, kBack, kDPad, kIconCount };

// Prompt name -> icon, as used by the photo mode menu.
inline int Find(const char* name) {
  static const char* kNames[kIconCount] = {"A",  "B",  "X",  "Y",     "LB",   "RB",  "LT",
                                           "RT", "LS", "RS", "Start", "Back", "D-Pad"};
  for (int i = 0; i < kIconCount; ++i)
    if (std::strcmp(kNames[i], name) == 0) return i;
  if (std::strcmp(name, "L3") == 0) return kLS;
  if (std::strcmp(name, "R3") == 0) return kRS;
  return -1;
}

struct Image {
  bool loaded = false;
  int w = 0, h = 0;
  std::string source;               // bank entry it came from
  std::vector<uint8_t> rgba;        // straight alpha, dropped once in the atlas
  ImFontAtlasRectId rect_id = ImFontAtlasRectId_Invalid;
  ImFontAtlas* atlas = nullptr;
};

inline Image& icon(int i) {
  static Image img[kIconCount];
  return img[std::clamp(i, 0, kIconCount - 1)];
}

namespace detail {

inline uint32_t be32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

// XGAddress2DTiledOffset, in elements (pixels or blocks) of 1 << log_bpp bytes.
inline uint32_t tiled_offset(uint32_t x, uint32_t y, uint32_t width, uint32_t log_bpp) {
  const uint32_t aligned = (width + 31) & ~31u;
  const uint32_t macro = ((x >> 5) + (y >> 5) * (aligned >> 5)) << (log_bpp + 7);
  const uint32_t micro = ((x & 7) + ((y & 0xE) << 2)) << log_bpp;
  const uint32_t offset = macro + ((micro & ~0xFu) << 1) + (micro & 0xF) + ((y & 1) << 4);
  return (((offset & ~0x1FFu) << 3) + ((y & 16) << 7) + ((offset & 0x1C0) << 2) +
          (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (offset & 0x3F)) >>
         log_bpp;
}

struct Entry {
  std::string name;  // lower case, without the trailing NUL
  uint32_t offset, size;
};

struct Bank {
  std::filesystem::path path;
  uint32_t data_offset = 0;
  std::vector<Entry> entries;

  bool Open(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    uint8_t h[17];
    if (!f.read(reinterpret_cast<char*>(h), sizeof(h))) return false;
    data_offset = be32(h);
    const uint32_t csz = be32(h + 9), usz = be32(h + 13);
    if (csz == 0 || csz > (64u << 20) || usz < 4 || usz > (64u << 20)) return false;
    std::vector<uint8_t> comp(csz), idx(usz);
    if (!f.read(reinterpret_cast<char*>(comp.data()), csz)) return false;
    zng_stream z{};
    if (zng_inflateInit(&z) != Z_OK) return false;
    z.next_in = comp.data();
    z.avail_in = csz;
    z.next_out = idx.data();
    z.avail_out = usz;
    zng_inflate(&z, Z_SYNC_FLUSH);
    const bool full = z.avail_out == 0;
    zng_inflateEnd(&z);
    if (!full) return false;
    const uint32_t n = be32(idx.data());
    size_t p4 = 4;
    entries.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
      if (p4 + 4 > usz) return false;
      const uint32_t len = be32(&idx[p4]);
      p4 += 4;
      if (p4 + len + 8 > usz) return false;
      Entry e;
      e.name.assign(reinterpret_cast<const char*>(&idx[p4]), len);
      while (!e.name.empty() && e.name.back() == '\0') e.name.pop_back();
      for (char& c : e.name) c = char(std::tolower(uint8_t(c)));
      p4 += len;
      e.offset = be32(&idx[p4]);
      e.size = be32(&idx[p4 + 4]);
      p4 += 8;
      entries.push_back(std::move(e));
    }
    path = p;
    return true;
  }
  const Entry* Get(const std::string& lower_name) const {
    for (const Entry& e : entries)
      if (e.name == lower_name) return &e;
    return nullptr;
  }
  bool Read(const Entry& e, std::vector<uint8_t>& out) const {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.resize(e.size);
    f.seekg(std::streamoff(data_offset) + e.offset);
    return bool(f.read(reinterpret_cast<char*>(out.data()), e.size));
  }
};

inline void rgb565(uint16_t v, int out[3]) {
  out[0] = ((v >> 11) & 31) * 255 / 31;
  out[1] = ((v >> 5) & 63) * 255 / 63;
  out[2] = (v & 31) * 255 / 31;
}

// One DXT5 block (Xbox 360 16-bit swapped) into a 4x4 RGBA tile at dst.
inline void dxt5_block(const uint8_t* src, uint8_t* dst, int stride) {
  uint8_t b[16];
  for (int i = 0; i < 16; i += 2) {
    b[i] = src[i + 1];
    b[i + 1] = src[i];
  }
  const int a0 = b[0], a1 = b[1];
  int ap[8] = {a0, a1};
  if (a0 > a1) {
    for (int i = 1; i <= 6; ++i) ap[i + 1] = ((7 - i) * a0 + i * a1) / 7;
  } else {
    for (int i = 1; i <= 4; ++i) ap[i + 1] = ((5 - i) * a0 + i * a1) / 5;
    ap[6] = 0;
    ap[7] = 255;
  }
  uint64_t abits = 0;
  for (int i = 0; i < 6; ++i) abits |= uint64_t(b[2 + i]) << (8 * i);
  const uint16_t c0 = uint16_t(b[8] | (b[9] << 8)), c1 = uint16_t(b[10] | (b[11] << 8));
  int cols[4][3];
  rgb565(c0, cols[0]);
  rgb565(c1, cols[1]);
  for (int k = 0; k < 3; ++k) {
    cols[2][k] = (2 * cols[0][k] + cols[1][k]) / 3;
    cols[3][k] = (cols[0][k] + 2 * cols[1][k]) / 3;
  }
  const uint32_t cbits = uint32_t(b[12]) | (uint32_t(b[13]) << 8) | (uint32_t(b[14]) << 16) |
                         (uint32_t(b[15]) << 24);
  for (int i = 0; i < 16; ++i) {
    uint8_t* p = dst + (i / 4) * stride + (i % 4) * 4;
    const int* c = cols[(cbits >> (2 * i)) & 3];
    p[0] = uint8_t(c[0]);
    p[1] = uint8_t(c[1]);
    p[2] = uint8_t(c[2]);
    p[3] = uint8_t(ap[(abits >> (3 * i)) & 7]);
  }
}

// Bytes a tiled surface of `wb` x `hb` elements spans.
inline size_t tiled_extent(uint32_t wb, uint32_t hb, uint32_t log_bpp) {
  return size_t((wb + 31) & ~31u) * ((hb + 31) & ~31u) << log_bpp;
}

inline bool Decode(const std::vector<uint8_t>& data, uint32_t w, uint32_t h, uint32_t fmt, Image& img,
                   std::string& why) {
  if (w == 0 || h == 0 || w > 4096 || h > 4096) {
    why = "bad size";
    return false;
  }
  const uint8_t* surf = nullptr;
  size_t surf_size = 0;
  if (fmt == 2 && data.size() >= 4 && be32(data.data()) + 4 == data.size()) {
    surf = data.data() + 4;
    surf_size = data.size() - 4;
  } else if (data.size() >= 12) {
    const uint32_t type = be32(data.data()), off = be32(&data[4]), size = be32(&data[8]);
    if (type != 7) {
      // Lionhead's codec: decode to the format 2 layout (u32 byte count + tiled ARGB).
      std::vector<uint8_t> argb;
      if (!fable2::gui::DecodeCompressedIconToTile(data.data(), data.size(), int(w), int(h), argb)) {
        why = "chunk type " + std::to_string(type) + " did not decode";
        return false;
      }
      return Decode(argb, w, h, 2, img, why);
    }
    if (size_t(off) + size > data.size()) {
      why = "chunk out of range";
      return false;
    }
    surf = data.data() + off;
    surf_size = size;
  }
  if (!surf) {
    why = "unknown layout";
    return false;
  }
  img.w = int(w);
  img.h = int(h);
  img.rgba.assign(size_t(w) * h * 4, 0);
  if (fmt == 2) {  // 32-bit ARGB
    if (surf_size < size_t(w) * h * 4) {
      why = "short surface";
      return false;
    }
    const bool tiled = surf_size >= tiled_extent(w, h, 2);
    for (uint32_t y = 0; y < h; ++y) {
      for (uint32_t x = 0; x < w; ++x) {
        const uint8_t* s = surf + size_t(tiled ? tiled_offset(x, y, w, 2) : y * w + x) * 4;
        uint8_t* d = &img.rgba[(size_t(y) * w + x) * 4];
        d[0] = s[1];
        d[1] = s[2];
        d[2] = s[3];
        d[3] = s[0];
      }
    }
    return true;
  }
  if (fmt == 39) {  // DXT5
    const uint32_t wb = (w + 3) / 4, hb = (h + 3) / 4;
    if (surf_size < size_t(wb) * hb * 16) {
      why = "short surface";
      return false;
    }
    const bool tiled = surf_size >= tiled_extent(wb, hb, 4);
    std::vector<uint8_t> full(size_t(wb) * 4 * hb * 4 * 4);
    const int stride = int(wb * 4 * 4);
    for (uint32_t by = 0; by < hb; ++by)
      for (uint32_t bx = 0; bx < wb; ++bx)
        dxt5_block(surf + size_t(tiled ? tiled_offset(bx, by, wb, 4) : by * wb + bx) * 16,
                   &full[size_t(by) * 4 * stride + bx * 16], stride);
    for (uint32_t y = 0; y < h; ++y) std::memcpy(&img.rgba[size_t(y) * w * 4], &full[size_t(y) * stride], w * 4);
    return true;
  }
  why = "format " + std::to_string(fmt);
  return false;
}

}  // namespace detail

// Extra images by bank entry name (lower case), e.g. the expression icons the
// pose cards show. Filled by Load().
struct Extra {
  std::string name;
  Image img;
};
inline std::vector<Extra>& extras() {
  static std::vector<Extra> v;
  return v;
}

namespace detail {
struct BankPair {
  Bank tex, hdr;
};
// With [input] deck_button_prompts on, the controller icons get the Steam Deck
// prompts the game's own textures get (src/gui/deck_prompts.h); null = off.
inline const fable2::gui::DeckArt*& deck_art() {
  static const fable2::gui::DeckArt* art = nullptr;
  return art;
}
// A texture as a full RGBA payload (u32 byte count, then tiled A,R,G,B), the
// form the Deck restyle works on; false for formats it doesn't take.
inline bool ToFullPayload(const std::vector<uint8_t>& data, uint32_t w, uint32_t h, uint32_t fmt,
                          std::vector<uint8_t>& out) {
  const size_t full = 4 + size_t(w) * h * 4;
  if (fmt == 2 && data.size() == full && be32(data.data()) + 4 == data.size()) {
    out = data;
    return true;
  }
  if (data.size() >= 12 && be32(data.data()) != 7) {
    return fable2::gui::DecodeCompressedIconToTile(data.data(), data.size(), int(w), int(h), out) &&
           out.size() == full;
  }
  return false;
}
// Decode bank entry `name` (lower case) from the first bank that has it.
inline bool LoadEntry(const std::vector<BankPair>& banks, const std::string& name, Image& img, std::string& why_all) {
  std::vector<uint8_t> data, hdr;
  for (const BankPair& b : banks) {
    const auto* te = b.tex.Get(name);
    const auto* he = b.hdr.Get(name);
    if (!te || !he) continue;  // the localised bank only has a few entries
    if (!b.hdr.Read(*he, hdr) || hdr.size() < 32 || !b.tex.Read(*te, data)) {
      why_all += " " + name + ": read failed;";
      continue;
    }
    std::string why;
    uint32_t fmt = be32(&hdr[24]);
    const uint32_t w = be32(&hdr[16]), h = be32(&hdr[20]);
    if (deck_art() && fable2::gui::HasDeckStyle(name)) {
      std::vector<uint8_t> payload;
      if (ToFullPayload(data, w, h, fmt, payload) &&
          fable2::gui::ApplyDeckStyle(name, payload, int(w), int(h), *deck_art())) {
        data = std::move(payload);
        fmt = 2;
      }
    }
    if (Decode(data, w, h, fmt, img, why)) {
      img.loaded = true;
      img.source = name;
      return true;
    }
    why_all += " " + name + ": " + why + ";";
  }
  return false;
}
}  // namespace detail

// Load every button icon, plus the `extra` entries (lower-case bank names),
// from the game data under `root`. Returns how many button icons loaded;
// `log` gets one line per icon that fell back or failed. `deck_set` is the
// Steam Deck prompt set ([input] deck_button_prompts: "dark", "light"), empty
// for the game's own Xbox 360 icons.
inline int Load(const std::filesystem::path& root, std::string& log, const std::vector<std::string>& extra = {},
                const std::string& deck_set = "") {
  using detail::BankPair;
  extras().clear();
  std::vector<BankPair> banks;  // localised first
  for (const char* lang : {"en-uk", "en-us", "en"}) {
    const auto dir = root / "data" / "art" / "localised" / lang / "gui";
    BankPair p;
    if (p.tex.Open(dir / "gui_textures.bnk") && p.hdr.Open(dir / "gui_texture_headers.bnk")) {
      banks.push_back(std::move(p));
      break;
    }
  }
  {
    // The keyboard-prompt swap (src/gui/prompt_textures.h) rewrites these banks
    // and keeps the pristine ones as <bank>.orig: read those when they exist,
    // so the controller icons are always the game's own.
    const auto dir = root / "data" / "art" / "gui";
    auto pick = [&](const char* file) {
      std::error_code ec;
      const auto orig = dir / (std::string(file) + ".orig");
      return std::filesystem::exists(orig, ec) ? orig : dir / file;
    };
    BankPair p;
    if (p.tex.Open(pick("gui_textures.bnk")) && p.hdr.Open(pick("gui_texture_headers.bnk")))
      banks.push_back(std::move(p));
  }
  if (banks.empty()) {
    log = "gui_textures.bnk not found under " + root.string();
    return 0;
  }
  // The Steam Deck prompts: the glyph set and the game's green medallion, taken
  // from its Back icons, as src/gui/prompt_textures.h makes them.
  static fable2::gui::DeckArt deck;
  detail::deck_art() = nullptr;
  if (!deck_set.empty()) {
    deck = fable2::gui::DeckArt{};
    deck.set = deck_set;
    auto medallion = [&](const char* name, uint32_t size) -> std::vector<uint8_t> {
      for (const detail::BankPair& b : banks) {
        const auto* te = b.tex.Get(name);
        const auto* he = b.hdr.Get(name);
        std::vector<uint8_t> data, hdr, tile;
        if (!te || !he || !b.hdr.Read(*he, hdr) || hdr.size() < 32 || !b.tex.Read(*te, data)) continue;
        if (detail::be32(&hdr[16]) != size || detail::be32(&hdr[20]) != size) continue;
        if (detail::ToFullPayload(data, size, size, detail::be32(&hdr[24]), tile))
          return fable2::gui::deck_detail::MakeMedallion(tile, int(size));
      }
      return {};
    };
    deck.medallion64 = medallion(fable2::gui::kDeckMedallion64, 64);
    deck.medallion32 = medallion(fable2::gui::kDeckMedallion32, 32);
    detail::deck_art() = &deck;
  }
  // Candidate entries per icon, best first (the 64 px art, then the 32 px HUD set).
  static const char* const kCandidates[kIconCount][2] = {
      {"icon_button_a", nullptr},      {"icon_button_b", nullptr},     {"icon_button_x", nullptr},
      {"icon_button_y", nullptr},      {"leftbutton", "leftbutton"},   {"rightbutton", "rightbutton"},
      {"lefttrigger", "lefttrigger"},  {"righttrigger", "righttrigger"}, {"left", "left"},
      {"right", "right"},              {"start", "start"},             {"back", "back"},
      {"dpad", "dpad"}};
  int loaded = 0;
  for (int i = 0; i < kIconCount; ++i) {
    Image& img = icon(i);
    std::string why_all;
    for (int c = 0; c < 2 && !img.loaded; ++c) {
      if (!kCandidates[i][c]) continue;
      const std::string name = std::string("art\\gui\\controller\\") + (c ? "24x24\\" : "") + kCandidates[i][c] + ".tex";
      detail::LoadEntry(banks, name, img, why_all);
    }
    if (img.loaded) {
      ++loaded;
      if (!why_all.empty()) log += "\n  " + img.source + " used (" + why_all + ")";
    } else {
      log += "\n  icon " + std::to_string(i) + " not loaded:" + (why_all.empty() ? std::string(" not in the bank") : why_all);
    }
  }
  int extra_loaded = 0;
  for (const std::string& name : extra) {
    Extra e;
    e.name = name;
    std::string why_all;
    if (detail::LoadEntry(banks, name, e.img, why_all)) {
      ++extra_loaded;
    } else {
      log += "\n  " + name + " not loaded:" + (why_all.empty() ? std::string(" not in the bank") : why_all);
    }
    extras().push_back(std::move(e));
  }
  if (!extra.empty())
    log += "\n  extra images: " + std::to_string(extra_loaded) + " of " + std::to_string(extra.size());
  return loaded;
}

// Put the loaded icons and extra images into ImGui's atlas (call from
// OnConfigureFonts, after the fonts: rectangles are reserved first, then filled).
inline int AddAllToAtlas(ImFontAtlas* atlas) {
  if (!atlas) return 0;
  if (!atlas->TexIsBuilt) atlas->Build();
  std::vector<Image*> all;
  for (int i = 0; i < kIconCount; ++i) all.push_back(&icon(i));
  for (Extra& e : extras()) all.push_back(&e.img);
  for (Image* img : all) {
    if (!img->loaded) continue;
    img->rect_id = atlas->AddCustomRect(img->w, img->h, nullptr);
    if (img->rect_id == ImFontAtlasRectId_Invalid) img->loaded = false;
  }
  ImTextureData* tex = atlas->TexData;
  int added = 0;
  for (Image* img : all) {
    if (!img->loaded) continue;
    ImFontAtlasRect r;
    if (!tex || !tex->Pixels || tex->BytesPerPixel != 4 || !atlas->GetCustomRect(img->rect_id, &r)) {
      img->loaded = false;
      continue;
    }
    for (int y = 0; y < img->h; ++y)
      std::memcpy(tex->GetPixelsAt(r.x, r.y + y), &img->rgba[size_t(y) * img->w * 4], size_t(img->w) * 4);
    img->atlas = atlas;
    img->rgba.clear();
    img->rgba.shrink_to_fit();
    ++added;
  }
  return added;
}

inline bool Ready(int i) { return i >= 0 && i < kIconCount && icon(i).loaded && icon(i).atlas; }

inline void DrawImage(ImDrawList* dl, const Image& img, ImVec2 p0, float size, ImU32 col) {
  ImFontAtlasRect r;
  if (!img.atlas || !img.atlas->GetCustomRect(img.rect_id, &r)) return;
  dl->AddImage(dl->_CmdHeader.TexRef, p0, ImVec2(p0.x + size, p0.y + size), r.uv0, r.uv1, col);
}

// Draw icon `i` filling the square [p0, p0 + size].
inline void Draw(ImDrawList* dl, int i, ImVec2 p0, float size, ImU32 col = IM_COL32_WHITE) {
  if (Ready(i)) DrawImage(dl, icon(i), p0, size, col);
}

// Extra images: index by bank entry name (-1 if not requested at load).
inline int FindExtra(const char* name) {
  const auto& v = extras();
  for (int i = 0; i < int(v.size()); ++i)
    if (v[i].name == name) return i;
  return -1;
}
inline bool ExtraReady(int i) {
  return i >= 0 && i < int(extras().size()) && extras()[i].img.loaded && extras()[i].img.atlas;
}
inline void DrawExtra(ImDrawList* dl, int i, ImVec2 p0, float size, ImU32 col = IM_COL32_WHITE) {
  if (ExtraReady(i)) DrawImage(dl, extras()[i].img, p0, size, col);
}
// The texels [t0, t1) of extra image `i` stretched over [p0, p1], upside
// down when `flip_v`. Half a texel is kept off each edge so the atlas
// neighbours do not bleed in. False if the image is not ready.
// (From himdo/Fable-2-Recomp PR #213 by Locme.)
inline bool DrawExtraRegion(ImDrawList* dl, int i, ImVec2 p0, ImVec2 p1, ImVec2 t0, ImVec2 t1, ImU32 col,
                            bool flip_v) {
  if (!ExtraReady(i)) return false;
  const Image& img = extras()[i].img;
  ImFontAtlasRect r;
  if (!img.atlas->GetCustomRect(img.rect_id, &r)) return false;
  const float du = (r.uv1.x - r.uv0.x) / float(img.w), dv = (r.uv1.y - r.uv0.y) / float(img.h);
  const float u0 = r.uv0.x + (t0.x + 0.5f) * du, u1 = r.uv0.x + (t1.x - 0.5f) * du;
  const float v0 = r.uv0.y + (t0.y + 0.5f) * dv, v1 = r.uv0.y + (t1.y - 0.5f) * dv;
  dl->AddImage(dl->_CmdHeader.TexRef, p0, p1, ImVec2(u0, flip_v ? v1 : v0), ImVec2(u1, flip_v ? v0 : v1), col);
  return true;
}
// Extra image `i` (its rows down to `v_max` of its height) stretched over
// [p0, p1], upside down when `flip_v` (so two copies meet without a seam).
inline bool DrawExtraRect(ImDrawList* dl, int i, ImVec2 p0, ImVec2 p1, ImU32 col, bool flip_v,
                          float v_max = 1.0f) {
  if (!ExtraReady(i)) return false;
  const Image& img = extras()[i].img;
  return DrawExtraRegion(dl, i, p0, p1, ImVec2(0, 0), ImVec2(float(img.w), float(img.h) * v_max), col, flip_v);
}

}  // namespace fable2::gameicons
