// Steam Deck button icons in the pub games (Spinnerbox, Keystone, Fortune's
// Tower). Those are Scaleform movies, data/pubgames/<game>/game.swf, and draw
// their buttons as vector art: a shared movie clip whose frames are labelled
// a, b, x, y, ls, rs, lt, rt, lb, rb, each showing one sub-clip. At start-up
// each sub-clip is replaced by a clip holding one image of the matching Deck
// glyph (src/gui/deck_prompts.h), at the same place and size; the gold ring the
// movies draw behind the face buttons stays. The untouched movie is kept as
// game.swf.orig, the rewritten one is written uncompressed (FWS) only when it
// differs, and the original comes back when the Deck icons are turned off.
#ifndef FABLE2_GUI_PUBGAME_PROMPTS_H
#define FABLE2_GUI_PUBGAME_PROMPTS_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <zlib-ng.h>

#include "bank_file.h"
#include "deck_prompts.h"

namespace fable2::gui {
namespace pubgame_detail {

struct BitReader {
  const uint8_t* b;
  size_t size, p;
  int bit = 0;
  uint32_t ub(int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; ++i) {
      const uint32_t x = p < size ? (b[p] >> (7 - bit)) & 1u : 0u;
      v = (v << 1) | x;
      if (++bit == 8) bit = 0, ++p;
    }
    return v;
  }
  int32_t sb(int n) {
    if (n == 0) return 0;
    const uint32_t v = ub(n);
    return (v & (1u << (n - 1))) ? int32_t(v | (~0u << n)) : int32_t(v);
  }
  void align() {
    if (bit) bit = 0, ++p;
  }
};

struct BitWriter {
  std::vector<uint8_t> out;
  int bit = 0;
  void ub(uint32_t v, int n) {
    for (int i = n - 1; i >= 0; --i) {
      if (bit == 0) out.push_back(0);
      if ((v >> i) & 1u) out.back() |= uint8_t(0x80 >> bit);
      bit = (bit + 1) & 7;
    }
  }
  void sb(int32_t v, int n) { ub(uint32_t(v) & (n >= 32 ? ~0u : ((1u << n) - 1)), n); }
  void align() { bit = 0; }
  void u8(uint8_t v) { align(); out.push_back(v); }
  void u16(uint16_t v) { align(); out.push_back(uint8_t(v)); out.push_back(uint8_t(v >> 8)); }
};

// Bits needed to store v as a signed value.
inline int SignedBits(int32_t v) {
  uint32_t m = uint32_t(v < 0 ? ~v : v);
  int n = 1;
  while (m) ++n, m >>= 1;
  return n;
}

struct Rect {
  int32_t x0 = 0, x1 = 0, y0 = 0, y1 = 0;  // twips
};

inline Rect ReadRect(BitReader& r) {
  const int n = int(r.ub(5));
  Rect v;
  v.x0 = r.sb(n); v.x1 = r.sb(n); v.y0 = r.sb(n); v.y1 = r.sb(n);
  r.align();
  return v;
}

inline void WriteRect(BitWriter& w, const Rect& v) {
  const int n = std::max({SignedBits(v.x0), SignedBits(v.x1), SignedBits(v.y0), SignedBits(v.y1)});
  w.align();
  w.ub(uint32_t(n), 5);
  w.sb(v.x0, n); w.sb(v.x1, n); w.sb(v.y0, n); w.sb(v.y1, n);
  w.align();
}

struct Matrix {
  double sx = 1, r0 = 0, r1 = 0, sy = 1, tx = 0, ty = 0;
};

inline Matrix ReadMatrix(BitReader& r) {
  Matrix m;
  if (r.ub(1)) {
    const int n = int(r.ub(5));
    m.sx = r.sb(n) / 65536.0; m.sy = r.sb(n) / 65536.0;
  }
  if (r.ub(1)) {
    const int n = int(r.ub(5));
    m.r0 = r.sb(n) / 65536.0; m.r1 = r.sb(n) / 65536.0;
  }
  const int n = int(r.ub(5));
  m.tx = r.sb(n); m.ty = r.sb(n);
  r.align();
  return m;
}

struct Tag {
  uint16_t code = 0;
  size_t start = 0, data = 0, len = 0;  // header offset, data offset, data length
};

inline std::vector<Tag> ReadTags(const std::vector<uint8_t>& b, size_t p, size_t end) {
  std::vector<Tag> tags;
  while (p + 2 <= end) {
    Tag t;
    t.start = p;
    const uint16_t cl = uint16_t(b[p] | (b[p + 1] << 8));
    p += 2;
    t.code = cl >> 6;
    size_t len = cl & 0x3F;
    if (len == 0x3F) {
      if (p + 4 > end) break;
      len = size_t(b[p]) | size_t(b[p + 1]) << 8 | size_t(b[p + 2]) << 16 | size_t(b[p + 3]) << 24;
      p += 4;
    }
    if (p + len > end) break;
    t.data = p;
    t.len = len;
    tags.push_back(t);
    p += len;
    if (t.code == 0) break;
  }
  return tags;
}

inline void PutTag(std::vector<uint8_t>& out, uint16_t code, const std::vector<uint8_t>& data) {
  if (data.size() < 0x3F) {
    const uint16_t cl = uint16_t(code << 6 | data.size());
    out.push_back(uint8_t(cl)); out.push_back(uint8_t(cl >> 8));
  } else {
    const uint16_t cl = uint16_t(code << 6 | 0x3F);
    out.push_back(uint8_t(cl)); out.push_back(uint8_t(cl >> 8));
    const uint32_t n = uint32_t(data.size());
    for (int i = 0; i < 4; ++i) out.push_back(uint8_t(n >> (8 * i)));
  }
  out.insert(out.end(), data.begin(), data.end());
}

inline uint16_t U16(const std::vector<uint8_t>& b, size_t p) { return uint16_t(b[p] | (b[p + 1] << 8)); }

// Tags whose first two bytes are the id of the character they define.
inline bool DefinesCharacter(uint16_t code) {
  static const std::set<uint16_t> codes = {2,  6,  7,  10, 11, 14, 20, 21, 22, 32, 33, 34, 35, 36,
                                           37, 39, 46, 48, 60, 75, 83, 84, 87, 90};
  return codes.count(code) != 0;
}

inline bool IsShape(uint16_t code) { return code == 2 || code == 22 || code == 32 || code == 83 || code == 46 || code == 84; }

struct Placement {
  uint16_t character;
  Matrix matrix;
};

// The characters a sprite places (in any frame), with their matrices.
inline std::vector<Placement> SpritePlacements(const std::vector<uint8_t>& b, const Tag& sprite) {
  std::vector<Placement> out;
  for (const Tag& t : ReadTags(b, sprite.data + 4, sprite.data + sprite.len)) {
    if (t.code != 26 || t.len < 3) continue;  // PlaceObject2
    const uint8_t flags = b[t.data];
    size_t p = t.data + 3;
    if (!(flags & 0x02)) continue;
    Placement pl{U16(b, p), {}};
    p += 2;
    if (flags & 0x04) {
      BitReader r{b.data(), t.data + t.len, p};
      pl.matrix = ReadMatrix(r);
    }
    out.push_back(pl);
  }
  return out;
}

struct Movie {
  std::vector<uint8_t> body;  // after the 8-byte header, uncompressed
  uint8_t version = 0;
  size_t tags_begin = 0;      // offset of the first tag in body
  std::vector<Tag> tags;
  std::map<uint16_t, size_t> defs;  // character id -> index in tags
};

inline bool LoadMovie(const std::vector<uint8_t>& file, Movie& m) {
  if (file.size() < 8 || file[1] != 'W' || file[2] != 'S') return false;
  m.version = file[3];
  const uint32_t length = uint32_t(file[4]) | uint32_t(file[5]) << 8 | uint32_t(file[6]) << 16 |
                          uint32_t(file[7]) << 24;
  if (length < 8 || length > (64u << 20)) return false;
  if (file[0] == 'F') {
    m.body.assign(file.begin() + 8, file.end());
  } else if (file[0] == 'C') {
    m.body.resize(length - 8);
    size_t out_len = m.body.size();
    if (zng_uncompress(m.body.data(), &out_len, file.data() + 8, file.size() - 8) != Z_OK ||
        out_len != m.body.size())
      return false;
  } else {
    return false;
  }
  BitReader r{m.body.data(), m.body.size(), 0};
  ReadRect(r);
  m.tags_begin = r.p + 4;  // frame rate, frame count
  m.tags = ReadTags(m.body, m.tags_begin, m.body.size());
  for (size_t i = 0; i < m.tags.size(); ++i)
    if (DefinesCharacter(m.tags[i].code) && m.tags[i].len >= 2)
      m.defs[U16(m.body, m.tags[i].data)] = i;
  return !m.tags.empty();
}

// A character's bounds in its own space (shapes: their bounds; sprites: what
// they place, through the placement matrices).
inline bool Bounds(const Movie& m, uint16_t id, Rect& out, int depth = 0) {
  auto it = m.defs.find(id);
  if (it == m.defs.end() || depth > 8) return false;
  const Tag& t = m.tags[it->second];
  if (IsShape(t.code)) {
    BitReader r{m.body.data(), t.data + t.len, t.data + 2};
    out = ReadRect(r);
    return true;
  }
  if (t.code != 39) return false;
  bool any = false;
  for (const Placement& pl : SpritePlacements(m.body, t)) {
    Rect c;
    if (!Bounds(m, pl.character, c, depth + 1)) continue;
    const Matrix& x = pl.matrix;
    for (int i = 0; i < 4; ++i) {
      const double px = (i & 1) ? c.x1 : c.x0, py = (i & 2) ? c.y1 : c.y0;
      const int32_t tx = int32_t(std::lround(px * x.sx + py * x.r1 + x.tx));
      const int32_t ty = int32_t(std::lround(px * x.r0 + py * x.sy + x.ty));
      if (!any) { out = {tx, tx, ty, ty}; any = true; }
      out.x0 = std::min(out.x0, tx); out.x1 = std::max(out.x1, tx);
      out.y0 = std::min(out.y0, ty); out.y1 = std::max(out.y1, ty);
    }
  }
  return any;
}

// A button sub-clip's core: the first character it places (the button itself;
// the sparkles placed after it stretch the clip's own bounds), in the clip's
// space.
inline bool CoreBounds(const Movie& m, uint16_t id, Rect& out) {
  auto it = m.defs.find(id);
  if (it == m.defs.end() || m.tags[it->second].code != 39) return false;
  const auto places = SpritePlacements(m.body, m.tags[it->second]);
  if (places.empty()) return false;
  Rect c;
  if (!Bounds(m, places[0].character, c)) return false;
  const Matrix& x = places[0].matrix;
  bool any = false;
  for (int i = 0; i < 4; ++i) {
    const double px = (i & 1) ? c.x1 : c.x0, py = (i & 2) ? c.y1 : c.y0;
    const int32_t tx = int32_t(std::lround(px * x.sx + py * x.r1 + x.tx));
    const int32_t ty = int32_t(std::lround(px * x.r0 + py * x.sy + x.ty));
    if (!any) { out = {tx, tx, ty, ty}; any = true; }
    out.x0 = std::min(out.x0, tx); out.x1 = std::max(out.x1, tx);
    out.y0 = std::min(out.y0, ty); out.y1 = std::max(out.y1, ty);
  }
  return true;
}

// The pack's round stick icon (a ring with L or R) without the direction
// arrow its *_up glyph has: everything but the smallest separate part.
inline const deck_detail::Image& StickGlyph(const std::string& set, bool right) {
  using deck_detail::Image;
  static std::map<std::string, Image> cache;
  const std::string key = set + (right ? "/r" : "/l");
  auto it = cache.find(key);
  if (it != cache.end()) return it->second;
  Image& out = cache[key];
  const Image& g = deck_detail::LoadGlyph(set, right ? "shared_rstick_up" : "shared_lstick_up");
  if (g.px.empty()) return out;
  std::vector<int> label(size_t(g.w) * g.h, 0);
  std::vector<int> sizes(1, 0);
  for (int y = 0; y < g.h; ++y)
    for (int x = 0; x < g.w; ++x) {
      if (g.at(x, y)[0] <= 8 || label[size_t(y) * g.w + x]) continue;
      const int id = int(sizes.size());
      sizes.push_back(0);
      std::vector<std::pair<int, int>> stack = {{x, y}};
      label[size_t(y) * g.w + x] = id;
      while (!stack.empty()) {
        auto [cx, cy] = stack.back();
        stack.pop_back();
        ++sizes[id];
        const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto& d : nb) {
          const int nx = cx + d[0], ny = cy + d[1];
          if (nx < 0 || ny < 0 || nx >= g.w || ny >= g.h) continue;
          int& l = label[size_t(ny) * g.w + nx];
          if (l || g.at(nx, ny)[0] <= 8) continue;
          l = id;
          stack.push_back({nx, ny});
        }
      }
    }
  int smallest = 0;
  if (sizes.size() > 2)
    smallest = int(std::min_element(sizes.begin() + 1, sizes.end()) - sizes.begin());
  int x0 = g.w, y0 = g.h, x1 = -1, y1 = -1;
  for (int y = 0; y < g.h; ++y)
    for (int x = 0; x < g.w; ++x) {
      const int l = label[size_t(y) * g.w + x];
      if (!l || l == smallest) continue;
      x0 = std::min(x0, x); y0 = std::min(y0, y);
      x1 = std::max(x1, x); y1 = std::max(y1, y);
    }
  if (x1 < 0) return out;
  out = Image(x1 - x0 + 1, y1 - y0 + 1);
  for (int y = y0; y <= y1; ++y)
    for (int x = x0; x <= x1; ++x) {
      const int l = label[size_t(y) * g.w + x];
      // Keep the faint edge pixels of the kept parts too.
      if (l == smallest && l != 0) continue;
      std::copy_n(g.at(x, y), 4, out.at(x - x0, y - y0));
    }
  return out;
}

// The button clip: a sprite with frames labelled a, b, x and y. Returns, per
// label, the character the labelled frame places last (the button on top).
inline std::map<std::string, uint16_t> ButtonFrames(const Movie& m) {
  for (const Tag& t : m.tags) {
    if (t.code != 39) continue;
    // A frame's label and placements can come in either order before its
    // ShowFrame, so each frame is settled there.
    std::map<std::string, uint16_t> frames;
    std::string label;
    int last = -1;  // last character placed in the current frame
    for (const Tag& c : ReadTags(m.body, t.data + 4, t.data + t.len)) {
      if (c.code == 43) {  // FrameLabel
        const char* s = reinterpret_cast<const char*>(m.body.data() + c.data);
        label.assign(s, strnlen(s, c.len));
      } else if (c.code == 26 && c.len >= 5 && (m.body[c.data] & 0x02)) {  // PlaceObject2
        last = U16(m.body, c.data + 3);
      } else if (c.code == 1) {  // ShowFrame
        if (!label.empty() && last >= 0) frames[label] = uint16_t(last);
        label.clear();
        last = -1;
      }
    }
    if (frames.count("a") && frames.count("b") && frames.count("x") && frames.count("y")) return frames;
  }
  return {};
}

inline const char* GlyphForLabel(const std::string& label) {
  static const std::map<std::string, const char*> glyphs = {
      {"a", "shared_button_a"}, {"b", "shared_button_b"}, {"x", "shared_button_x"},
      {"y", "shared_button_y"}, {"ls", "shared_lstick"},  {"rs", "shared_rstick"},
      {"lt", "sd_l2"},          {"rt", "sd_r2"},          {"lb", "sd_l1"},
      {"rb", "sd_r1"},
  };
  auto it = glyphs.find(label);
  return it == glyphs.end() ? nullptr : it->second;
}

// DefineBitsLossless2 (format 5: premultiplied A,R,G,B).
inline std::vector<uint8_t> BitmapTag(uint16_t id, const deck_detail::Image& im) {
  std::vector<uint8_t> argb(im.px.size());
  for (size_t i = 0; i < im.px.size(); i += 4) {
    const uint32_t a = im.px[i];
    argb[i] = uint8_t(a);
    for (int c = 1; c < 4; ++c) argb[i + c] = uint8_t((im.px[i + c] * a + 127) / 255);
  }
  std::vector<uint8_t> z(zng_compressBound(argb.size()));
  size_t zlen = z.size();
  if (zng_compress2(z.data(), &zlen, argb.data(), argb.size(), 6) != Z_OK) return {};
  z.resize(zlen);
  std::vector<uint8_t> d = {uint8_t(id), uint8_t(id >> 8), 5, uint8_t(im.w), uint8_t(im.w >> 8),
                            uint8_t(im.h), uint8_t(im.h >> 8)};
  d.insert(d.end(), z.begin(), z.end());
  return d;
}

// DefineShape3: one rectangle filled with the bitmap, mapped onto `area`.
inline std::vector<uint8_t> ShapeTag(uint16_t id, uint16_t bitmap, int bw, int bh, const Rect& area) {
  BitWriter w;
  w.u16(id);
  WriteRect(w, area);
  w.u8(1);     // one fill style
  w.u8(0x41);  // clipped bitmap
  w.u16(bitmap);
  // Bitmap matrix: a bitmap pixel is one twip unscaled.
  const int32_t sx = int32_t(std::lround(double(area.x1 - area.x0) / bw * 65536.0));
  const int32_t sy = int32_t(std::lround(double(area.y1 - area.y0) / bh * 65536.0));
  w.align();
  w.ub(1, 1);
  const int ns = std::max(SignedBits(sx), SignedBits(sy));
  w.ub(uint32_t(ns), 5); w.sb(sx, ns); w.sb(sy, ns);
  w.ub(0, 1);
  const int nt = std::max(SignedBits(area.x0), SignedBits(area.y0));
  w.ub(uint32_t(nt), 5); w.sb(area.x0, nt); w.sb(area.y0, nt);
  w.align();
  w.u8(0);  // no line styles
  w.ub(1, 4); w.ub(0, 4);  // 1 fill bit, 0 line bits
  // Move to the top-left corner and select fill style 1 on the right side.
  const int nm = std::max(SignedBits(area.x0), SignedBits(area.y0));
  w.ub(0, 1); w.ub(0, 1); w.ub(0, 1); w.ub(1, 1); w.ub(0, 1); w.ub(1, 1);
  w.ub(uint32_t(nm), 5); w.sb(area.x0, nm); w.sb(area.y0, nm);
  w.ub(1, 1);
  // Clockwise: right, down, left, up.
  const int32_t W = area.x1 - area.x0, H = area.y1 - area.y0;
  const int ne = std::max(2, std::max(SignedBits(W), SignedBits(H)) + 1);
  auto edge = [&](bool vertical, int32_t delta) {
    w.ub(1, 1); w.ub(1, 1); w.ub(uint32_t(ne - 2), 4);
    w.ub(0, 1); w.ub(vertical ? 1 : 0, 1); w.sb(delta, ne);
  };
  edge(false, W); edge(true, H); edge(false, -W); edge(true, -H);
  w.ub(0, 6);  // end of shape
  w.align();
  return w.out;
}

// DefineSprite: one frame showing `shape`.
inline std::vector<uint8_t> SpriteTag(uint16_t id, uint16_t shape) {
  std::vector<uint8_t> d = {uint8_t(id), uint8_t(id >> 8), 1, 0};
  PutTag(d, 26, {0x02, 1, 0, uint8_t(shape), uint8_t(shape >> 8)});  // PlaceObject2
  PutTag(d, 1, {});                                                   // ShowFrame
  PutTag(d, 0, {});                                                   // End
  return d;
}

}  // namespace pubgame_detail

// The pub game movie with the Deck glyphs of art.set in its button clip, as an
// uncompressed SWF; empty when it has no button clip or a glyph is missing.
inline std::vector<uint8_t> RewritePubGameMovie(const std::vector<uint8_t>& file, const DeckArt& art,
                                                int* replaced = nullptr) {
  using namespace pubgame_detail;
  using deck_detail::Image;
  Movie m;
  if (!LoadMovie(file, m)) return {};
  const auto frames = ButtonFrames(m);
  if (frames.empty()) return {};
  uint16_t next_id = 1;
  for (const auto& [id, index] : m.defs) next_id = std::max<uint16_t>(next_id, uint16_t(id + 1));

  // Replacement tags, keyed by the tag index of the sprite they replace.
  std::map<size_t, std::vector<uint8_t>> inserts;
  for (const auto& [label, child] : frames) {
    const char* stem = GlyphForLabel(label);
    auto def = m.defs.find(child);
    if (!stem || def == m.defs.end() || m.tags[def->second].code != 39 ||
        inserts.count(def->second))
      continue;
    // Sticks: the round stick icon, set like a face button in the gold ring.
    const bool stick = label == "ls" || label == "rs";
    const Image& glyph = stick ? StickGlyph(art.set, label == "rs")
                               : deck_detail::LoadGlyph(art.set, stem);
    if (glyph.px.empty()) return {};
    // Placed by the button's core shape, which the gold ring is centred on.
    Rect b;
    if (!CoreBounds(m, child, b) || b.x1 <= b.x0 || b.y1 <= b.y0) continue;
    const bool round = stick || deck_detail::IsFaceGlyph(stem);
    // Face buttons and sticks fill the core (the ring's opening); triggers and
    // bumpers get a square as large as the core's long side.
    const int32_t side = round ? std::min(b.x1 - b.x0, b.y1 - b.y0)
                               : std::max(b.x1 - b.x0, b.y1 - b.y0);
    Rect area;
    area.x0 = (b.x0 + b.x1 - side) / 2; area.x1 = area.x0 + side;
    area.y0 = (b.y0 + b.y1 - side) / 2; area.y1 = area.y0 + side;
    // Twice the movie's size, so Scaleform's scaling stays sharp.
    const int size = std::clamp(int(std::lround(side / 20.0 * 2.0)), 8, 256);
    Image im;
    if (round) {
      const int margin = std::max(2, size / 21);
      im = deck_detail::Orb(glyph, size - 2 * margin, margin);
    } else {
      im = Image(size, size);
      deck_detail::FitInto(im, glyph, 0, 0, im.w, im.h);
    }
    const uint16_t bitmap_id = next_id++, shape_id = next_id++;
    std::vector<uint8_t> tags;
    const auto bits = BitmapTag(bitmap_id, im);
    if (bits.empty()) return {};
    PutTag(tags, 36, bits);
    PutTag(tags, 32, ShapeTag(shape_id, bitmap_id, im.w, im.h, area));
    PutTag(tags, 39, SpriteTag(child, shape_id));
    inserts[def->second] = std::move(tags);
  }
  if (inserts.empty()) return {};

  std::vector<uint8_t> body(m.body.begin(), m.body.begin() + m.tags_begin);
  for (size_t i = 0; i < m.tags.size(); ++i) {
    auto it = inserts.find(i);
    if (it != inserts.end()) {
      body.insert(body.end(), it->second.begin(), it->second.end());
    } else {
      const Tag& t = m.tags[i];
      body.insert(body.end(), m.body.begin() + t.start, m.body.begin() + t.data + t.len);
    }
  }
  std::vector<uint8_t> out = {'F', 'W', 'S', m.version};
  const uint32_t total = uint32_t(body.size() + 8);
  for (int i = 0; i < 4; ++i) out.push_back(uint8_t(total >> (8 * i)));
  out.insert(out.end(), body.begin(), body.end());
  if (replaced) *replaced = int(inserts.size());
  return out;
}

// Puts the Deck glyphs of `deck_set` into every pub game under
// <root>/data/pubgames, or the original movies back when `deck_set` is empty.
// Returns one line per game for the log.
inline std::vector<std::string> BuildPubGamePrompts(const std::filesystem::path& root,
                                                    const std::string& deck_set) {
  using namespace prompt_detail;
  std::vector<std::string> report;
  std::error_code ec;
  const auto dir = root / "data" / "pubgames";
  if (!std::filesystem::is_directory(dir, ec)) return report;
  DeckArt art;
  art.set = deck_set;
  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    const auto movie = entry.path() / "game.swf";
    const auto orig = WithSuffix(movie, ".orig");
    if (!std::filesystem::is_regular_file(movie, ec)) continue;
    const std::string name = entry.path().filename().string();
    const bool have_orig = std::filesystem::exists(orig, ec);
    if (deck_set.empty()) {
      if (have_orig) report.push_back(name + (WriteIfChanged(movie, ReadAllBytes(orig))
                                                  ? ": original movie" : ": restore failed"));
      continue;
    }
    int replaced = 0;
    const auto out = RewritePubGameMovie(ReadAllBytes(have_orig ? orig : movie), art, &replaced);
    if (out.empty()) {
      report.push_back(name + ": no button clip, left as it is");
      continue;
    }
    if (!KeepOriginal(movie)) {
      report.push_back(name + ": could not keep the original movie, left as it is");
      continue;
    }
    report.push_back(name + (WriteIfChanged(movie, out)
                                 ? ": " + std::to_string(replaced) + " Deck buttons"
                                 : ": write failed"));
  }
  return report;
}

}  // namespace fable2::gui

#endif  // FABLE2_GUI_PUBGAME_PROMPTS_H
