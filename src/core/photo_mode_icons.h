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
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <imgui.h>
#include <zlib-ng.h>

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
    uint8_t h[9];
    if (!f.read(reinterpret_cast<char*>(h), sizeof(h))) return false;
    data_offset = be32(h);
    // The index is one zlib stream in chunks of {u32 compressed size, u32 size}: the GUI
    // bank has a single chunk, the world banks (data/Globals/*_textures.bnk) continue it
    // in 64 KB chunks up to the data offset.
    std::vector<uint8_t> idx;
    zng_stream z{};
    if (zng_inflateInit(&z) != Z_OK) return false;
    bool ok = true;
    for (bool first = true;; first = false) {
      uint8_t c[8];
      if (!first && uint64_t(f.tellg()) + 8 > data_offset) break;
      if (!f.read(reinterpret_cast<char*>(c), sizeof(c))) {
        ok = !first;
        break;
      }
      const uint32_t csz = be32(c), usz = be32(c + 4);
      if (csz == 0 || usz == 0) {
        ok = !first;
        break;
      }
      if (csz > (64u << 20) || usz > (64u << 20) || idx.size() + usz > (64u << 20)) {
        ok = false;
        break;
      }
      std::vector<uint8_t> comp(csz);
      if (!f.read(reinterpret_cast<char*>(comp.data()), csz)) {
        ok = false;
        break;
      }
      const size_t at = idx.size();
      idx.resize(at + usz);
      z.next_in = comp.data();
      z.avail_in = csz;
      z.next_out = idx.data() + at;
      z.avail_out = usz;
      zng_inflate(&z, Z_SYNC_FLUSH);
      if (z.avail_out != 0) {
        ok = false;
        break;
      }
      if (usz != 0x10000) break;  // a short (or single, larger) chunk ends the index
    }
    zng_inflateEnd(&z);
    if (!ok || idx.size() < 4) return false;
    const uint32_t usz = uint32_t(idx.size());
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
    if (Decode(data, be32(&hdr[16]), be32(&hdr[20]), be32(&hdr[24]), img, why)) {
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
// `log` gets one line per icon that fell back or failed.
inline int Load(const std::filesystem::path& root, std::string& log, const std::vector<std::string>& extra = {}) {
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

// A picture from one of the game's other texture banks, cropped and scaled down for an
// overlay (the difficulty cards). Banks, under <root>/data:
//   "mip0":  Globals/1024mip0_textures.bnk with the GUI header bank (the loading screens,
//            art\gui\loadingscreens\*, 1024 x 1024; the scene is the top 1024 x 720);
//   "world": Globals/globals_textures.bnk with Globals/globals_texture_headers.bnk (the
//            quest pictures, art\gui\questscreenshots\*, 512 x 512).
// Nothing is shipped: the pictures are read from the user's game data at startup.
struct PictureSpec {
  const char* key;     // the name FindExtra() knows it by
  const char* bank;    // "mip0" or "world"
  const char* entry;   // bank entry name, lower case
  int x0, y0, x1, y1;  // crop, in source pixels
  int out_w, out_h;    // size after scaling (box filter)
  int corner = 0;      // rounded corners cut out (transparent), radius in output pixels
};

// Adds each picture to extras() (call after Load(), which clears them, and before
// AddAllToAtlas()). Returns how many loaded; `log` gets a line per failure.
inline int LoadPictures(const std::filesystem::path& root, const std::vector<PictureSpec>& specs, std::string& log) {
  using detail::BankPair;
  const auto data = root / "data";
  auto pick = [](const std::filesystem::path& p) {
    std::error_code ec;
    const auto orig = std::filesystem::path(p.string() + ".orig");
    return std::filesystem::exists(orig, ec) ? orig : p;
  };
  std::vector<BankPair> mip0(1), world(1);
  bool mip0_tried = false, world_tried = false, mip0_ok = false, world_ok = false;
  int loaded = 0;
  for (const PictureSpec& s : specs) {
    const bool is_mip0 = std::strcmp(s.bank, "mip0") == 0;
    if (is_mip0 && !mip0_tried) {
      mip0_tried = true;
      mip0_ok = mip0[0].tex.Open(data / "Globals" / "1024mip0_textures.bnk") &&
                mip0[0].hdr.Open(pick(data / "art" / "gui" / "gui_texture_headers.bnk"));
    } else if (!is_mip0 && !world_tried) {
      world_tried = true;
      world_ok = world[0].tex.Open(data / "Globals" / "globals_textures.bnk") &&
                 world[0].hdr.Open(data / "Globals" / "globals_texture_headers.bnk");
    }
    Extra e;
    e.name = s.key;
    Image full;
    std::string why;
    if (!(is_mip0 ? mip0_ok : world_ok)) {
      why = std::string(" the ") + s.bank + " bank didn't open";
    } else if (detail::LoadEntry(is_mip0 ? mip0 : world, s.entry, full, why)) {
      const int x0 = std::clamp(s.x0, 0, full.w), x1 = std::clamp(s.x1, x0 + 1, full.w);
      const int y0 = std::clamp(s.y0, 0, full.h), y1 = std::clamp(s.y1, y0 + 1, full.h);
      const int cw = x1 - x0, ch = y1 - y0, ow = std::max(1, s.out_w), oh = std::max(1, s.out_h);
      e.img.w = ow;
      e.img.h = oh;
      e.img.rgba.assign(size_t(ow) * oh * 4, 0);
      for (int y = 0; y < oh; ++y) {
        const int sy0 = y0 + y * ch / oh, sy1 = std::max(sy0 + 1, y0 + (y + 1) * ch / oh);
        for (int x = 0; x < ow; ++x) {
          const int sx0 = x0 + x * cw / ow, sx1 = std::max(sx0 + 1, x0 + (x + 1) * cw / ow);
          uint32_t sum[4] = {0, 0, 0, 0}, n = 0;
          for (int sy = sy0; sy < sy1; ++sy)
            for (int sx = sx0; sx < sx1; ++sx, ++n)
              for (int c = 0; c < 4; ++c) sum[c] += full.rgba[(size_t(sy) * full.w + sx) * 4 + c];
          for (int c = 0; c < 4; ++c) e.img.rgba[(size_t(y) * ow + x) * 4 + c] = uint8_t(sum[c] / n);
        }
      }
      // Rounded corners, so a plain AddImage lines up with a rounded frame (the runtime
      // doesn't export ImDrawList::AddImageRounded).
      const float r = float(s.corner);
      for (int y = 0; y < oh && r > 0; ++y)
        for (int x = 0; x < ow; ++x) {
          const float cx = x + 0.5f < r ? r : (x + 0.5f > ow - r ? ow - r : x + 0.5f);
          const float cy = y + 0.5f < r ? r : (y + 0.5f > oh - r ? oh - r : y + 0.5f);
          const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
          const float cover = std::clamp(r + 0.5f - std::sqrt(dx * dx + dy * dy), 0.0f, 1.0f);
          uint8_t& alpha = e.img.rgba[(size_t(y) * ow + x) * 4 + 3];
          alpha = uint8_t(alpha * cover);
        }
      e.img.loaded = true;
      e.img.source = s.entry;
      ++loaded;
    }
    if (!e.img.loaded) log += std::string("\n  picture ") + s.key + " not loaded:" + why;
    extras().push_back(std::move(e));
  }
  log += "\n  pictures: " + std::to_string(loaded) + " of " + std::to_string(specs.size());
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

}  // namespace fable2::gameicons
