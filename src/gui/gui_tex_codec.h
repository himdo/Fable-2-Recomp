// Decoder for Fable II's compressed GUI texture format. The .bnk controller
// prompt icons are not stored as raw pixels; they use a custom block-compressed
// format with two on-disk variants (selected by the mip header's CompFlag):
//
//   CompFlag 1/11 : a Huffman-coded BC1 (DXT1) colour plane, optionally paired
//                   with a Huffman-coded "variant" alpha plane (the dual plane,
//                   CompFlag 2/3/4), together forming a BC3 (DXT5) RGBA surface.
//   CompFlag 7    : raw (uncompressed) block-compressed data -- standard BC3
//                   blocks stored with their 16-bit fields byte-swapped.
//
// Both decode to a straight 8-bit RGBA surface, which is the same pixel layout
// the uncompressed 0x00A2 controller icons (A/B/X/Y) already use.
#ifndef FABLE2_GUI_GUI_TEX_CODEC_H
#define FABLE2_GUI_GUI_TEX_CODEC_H

#include <cstdint>
#include <cstddef>
#include <utility>
#include <vector>

#include "xenos_tiling.h"

namespace fable2::gui {
namespace texcodec {

// Consecutive bits are packed into 32-bit words that are assembled big-endian
// from the byte stream (the first byte of each quad forms the word's high byte);
// a single bit cursor then walks each word from its low bit upward.
class BitStream {
 public:
  BitStream(const uint8_t* data, size_t size) : p_(data), n_(size) {}

  uint32_t take(int bits) {
    if (bits == 0) return 0;
    const size_t within = cursor_ & 31u;
    const size_t slot = cursor_ >> 5;
    const uint32_t keep = (bits >= 32) ? 0xFFFFFFFFu : ((1u << bits) - 1u);
    uint32_t v;
    if (within + static_cast<size_t>(bits) > 32u) {
      const uint32_t lo = word_at(slot);
      const uint32_t hi = word_at(slot + 1);
      v = ((hi << (32 - within)) | (lo >> within)) & keep;
    } else {
      v = (word_at(slot) >> within) & keep;
    }
    cursor_ += static_cast<size_t>(bits);
    return v;
  }

 private:
  uint32_t word_at(size_t i) const {
    const size_t base = i * 4;
    uint32_t v = 0;
    for (int k = 0; k < 4 && base + static_cast<size_t>(k) < n_; ++k) {
      v |= static_cast<uint32_t>(p_[base + static_cast<size_t>(k)]) << (24 - 8 * k);
    }
    return v;
  }

  const uint8_t* p_ = nullptr;
  size_t n_ = 0;
  size_t cursor_ = 0;
};

// A compressed frequency byte packs a 4-bit mantissa (low nibble) and a 4-bit
// exponent (high nibble); the effective symbol weight is mantissa << exponent.
constexpr uint32_t FreqWeight(uint8_t b) {
  return static_cast<uint32_t>(b & 0x0F) << ((b >> 4) & 0x0F);
}

// A Huffman codebook built as an index-based binary tree. The build order is
// deterministic (equal weights are promoted, and the left child wins ties); the
// encoder's symbol-to-code assignment depends on exactly this order.
class Codebook {
 public:
  // Returns the root node index, or -1 when no symbol has a non-zero weight.
  int Build(const uint32_t* weights, size_t count) {
    nodes_.clear();
    nodes_.reserve(count * 2);
    std::vector<int> frontier;
    frontier.reserve(count * 2);
    for (size_t s = 0; s < count; ++s) {
      nodes_.push_back({weights[s], static_cast<int>(s), -1, -1});
      frontier.push_back(static_cast<int>(nodes_.size()) - 1);
      Promote(frontier, static_cast<int>(frontier.size()) - 1);
    }
    while (frontier.size() > 1) {
      const int a = Extract(frontier);
      const int b = Extract(frontier);
      nodes_.push_back({nodes_[a].weight + nodes_[b].weight, -1, a, b});
      frontier.push_back(static_cast<int>(nodes_.size()) - 1);
      Promote(frontier, static_cast<int>(frontier.size()) - 1);
    }
    root_ = frontier.empty() ? -1 : frontier.front();
    return root_;
  }

  // Walks one symbol off the stream; returns the symbol id or -1 on failure.
  int Symbol(BitStream& stream) const {
    int i = root_;
    while (i >= 0 && nodes_[i].symbol < 0) {
      i = stream.take(1) ? nodes_[i].b : nodes_[i].a;
    }
    return i >= 0 ? nodes_[i].symbol : -1;
  }

 private:
  struct Node {
    uint32_t weight;
    int symbol;  // leaf id, or -1 for an internal node
    int a, b;    // child node indices
  };
  void Promote(std::vector<int>& heap, int i) {
    while (i > 0) {
      const int parent = (i - 1) >> 1;
      if (nodes_[heap[parent]].weight < nodes_[heap[i]].weight) break;
      std::swap(heap[parent], heap[i]);
      i = parent;
    }
  }
  int Extract(std::vector<int>& heap) {
    const int top = heap.front();
    const int last = heap.back();
    heap.pop_back();
    if (!heap.empty()) {
      heap[0] = last;
      int i = 0;
      const int n = static_cast<int>(heap.size());
      for (;;) {
        const int l = 2 * i + 1;
        if (l >= n) break;
        const int r = l + 1;
        int child = l;
        if (r < n && nodes_[heap[r]].weight < nodes_[heap[l]].weight) child = r;
        if (nodes_[heap[i]].weight < nodes_[heap[child]].weight) break;
        std::swap(heap[i], heap[child]);
        i = child;
      }
    }
    return top;
  }
  std::vector<Node> nodes_;
  int root_ = -1;
};

// Standard DXT1 (BC1) block -> 16 straight-alpha RGBA pixels.
inline void ExpandBc1(const uint8_t b[8], uint8_t rgba[64]) {
  auto ex5 = [](int v) { return (v * 255 + 15) / 31; };
  auto ex6 = [](int v) { return (v * 255 + 31) / 63; };
  const uint16_t e0 = static_cast<uint16_t>(b[0] | (b[1] << 8));
  const uint16_t e1 = static_cast<uint16_t>(b[2] | (b[3] << 8));
  const int r0 = ex5(e0 >> 11), g0 = ex6((e0 >> 5) & 63), b0 = ex5(e0 & 31);
  const int r1 = ex5(e1 >> 11), g1 = ex6((e1 >> 5) & 63), b1 = ex5(e1 & 31);
  uint8_t pal[16];
  pal[0] = 255; pal[1] = static_cast<uint8_t>(r0); pal[2] = static_cast<uint8_t>(g0); pal[3] = static_cast<uint8_t>(b0);
  pal[4] = 255; pal[5] = static_cast<uint8_t>(r1); pal[6] = static_cast<uint8_t>(g1); pal[7] = static_cast<uint8_t>(b1);
  if (e0 > e1) {
    pal[8] = 255; pal[9] = static_cast<uint8_t>((2 * r0 + r1 + 1) / 3);
    pal[10] = static_cast<uint8_t>((2 * g0 + g1 + 1) / 3);
    pal[11] = static_cast<uint8_t>((2 * b0 + b1 + 1) / 3);
    pal[12] = 255; pal[13] = static_cast<uint8_t>((r0 + 2 * r1 + 1) / 3);
    pal[14] = static_cast<uint8_t>((g0 + 2 * g1 + 1) / 3);
    pal[15] = static_cast<uint8_t>((b0 + 2 * b1 + 1) / 3);
  } else {
    pal[8] = 255; pal[9] = static_cast<uint8_t>((r0 + r1) >> 1);
    pal[10] = static_cast<uint8_t>((g0 + g1) >> 1);
    pal[11] = static_cast<uint8_t>((b0 + b1) >> 1);
    pal[12] = 0; pal[13] = 0; pal[14] = 0; pal[15] = 0;
  }
  const uint32_t sel = static_cast<uint32_t>(b[4]) | (static_cast<uint32_t>(b[5]) << 8) |
                       (static_cast<uint32_t>(b[6]) << 16) | (static_cast<uint32_t>(b[7]) << 24);
  for (int i = 0; i < 16; ++i) {
    const uint8_t* c = pal + ((sel >> (2 * i)) & 3) * 4;
    rgba[i * 4] = c[0];
    rgba[i * 4 + 1] = c[1];
    rgba[i * 4 + 2] = c[2];
    rgba[i * 4 + 3] = c[3];
  }
}

// Standard BC4 (single-channel) block -> 16 values.
inline void ExpandBc4(const uint8_t b[8], uint8_t out[16]) {
  const uint8_t e0 = b[0], e1 = b[1];
  uint64_t sel = 0;
  for (int i = 0; i < 6; ++i) sel |= static_cast<uint64_t>(b[2 + i]) << (8 * i);
  uint8_t ramp[8] = {e0, e1};
  if (e0 > e1) {
    for (int i = 1; i <= 6; ++i) ramp[i + 1] = static_cast<uint8_t>(((7 - i) * e0 + i * e1 + 3) / 7);
  } else {
    for (int i = 1; i <= 4; ++i) ramp[i + 1] = static_cast<uint8_t>(((5 - i) * e0 + i * e1 + 2) / 5);
    ramp[6] = 0;
    ramp[7] = 255;
  }
  for (int i = 0; i < 16; ++i) out[i] = ramp[(sel >> (3 * i)) & 7];
}

// Neighbour-block reference operations for the colour plane (62 entries). Each
// entry names how to derive a block's endpoints from a nearby block: op 0 copies
// the neighbour's endpoints, op 1/2/3 nudges c0/c1/both by a delta, op 4 is a
// literal (both endpoints are read directly from the stream).
struct NeighborOp {
  int op, dx, dy;
};
inline const NeighborOp* NeighborOps() {
  static const NeighborOp k[] = {
      {0, -1, 0}, {0, -2, 0}, {0, -3, 0}, {0, -4, 0}, {0, -5, 0},
      {0, 0, -1}, {0, -1, -1}, {0, -2, -1}, {0, -3, -1}, {0, -4, -1}, {0, -5, -1},
      {0, 0, -2}, {0, -1, -2}, {0, -2, -2}, {0, -3, -2},
      {0, 0, -3}, {0, -1, -3}, {0, -2, -3},
      {0, 0, -4}, {0, -1, -4},
      {0, 0, -5}, {0, -1, -5},
      {1, -1, 0}, {1, -2, 0}, {1, -3, 0}, {1, -4, 0},
      {1, 0, -1}, {1, -1, -1}, {1, -2, -1}, {1, -3, -1}, {1, -4, -1},
      {1, 0, -2}, {1, -1, -2}, {1, -2, -2},
      {1, 0, -3}, {1, -1, -3},
      {1, 0, -4}, {1, -1, -4},
      {2, -1, 0}, {2, -2, 0}, {2, -3, 0}, {2, -4, 0},
      {2, 0, -1}, {2, -1, -1}, {2, -2, -1}, {2, -3, -1}, {2, -4, -1},
      {2, 0, -2}, {2, -1, -2}, {2, -2, -2},
      {2, 0, -3}, {2, -1, -3},
      {2, 0, -4}, {2, -1, -4},
      {3, -1, 0}, {3, -2, 0},
      {3, 0, -1}, {3, -1, -1}, {3, -2, -1},
      {3, 0, -2}, {3, -1, -2},
      {4, 0, 0},
  };
  return k;
}

// Endpoint nudges for the colour plane (122 RGB565 deltas, sorted by magnitude).
struct RgbNudge {
  int dr, dg, db;
};
inline const RgbNudge* Nudges() {
  static const RgbNudge k[] = {
      {-3, -5, -3}, {-3, -5, -2}, {-3, -5, -1},
      {-3, -3, -3}, {-3, -3, -2}, {-3, -3, -1},
      {-2, -5, -3}, {-2, -5, -2}, {-2, -5, -1}, {-2, -5, 0},
      {-2, -3, -3}, {-2, -3, -2}, {-2, -3, -1}, {-2, -3, 0},
      {-2, -1, -2}, {-2, -1, -1}, {-2, -1, 0},
      {-2, 0, -2}, {-2, 0, -1}, {-2, 0, 0},
      {-2, 1, -2}, {-2, 1, -1}, {-2, 1, 0},
      {-1, -5, -3}, {-1, -5, -2}, {-1, -5, -1}, {-1, -5, 0},
      {-1, -3, -3}, {-1, -3, -2}, {-1, -3, -1}, {-1, -3, 0}, {-1, -3, 1},
      {-1, -1, -2}, {-1, -1, -1}, {-1, -1, 0}, {-1, -1, 1},
      {-1, 0, -2}, {-1, 0, -1}, {-1, 0, 0}, {-1, 0, 1},
      {-1, 1, -2}, {-1, 1, -1}, {-1, 1, 0}, {-1, 1, 1},
      {-1, 3, -1}, {-1, 3, 0}, {-1, 3, 1},
      {0, -5, -2}, {0, -5, -1}, {0, -5, 0},
      {0, -3, -2}, {0, -3, -1}, {0, -3, 0}, {0, -3, 1},
      {0, -1, -2}, {0, -1, -1}, {0, -1, 0}, {0, -1, 1}, {0, -1, 2},
      {0, 0, -2}, {0, 0, -1}, {0, 0, 1}, {0, 0, 2},
      {0, 1, -2}, {0, 1, -1}, {0, 1, 0}, {0, 1, 1}, {0, 1, 2},
      {0, 3, -1}, {0, 3, 0}, {0, 3, 1}, {0, 3, 2},
      {0, 5, 0}, {0, 5, 1}, {0, 5, 2},
      {1, -3, -1}, {1, -3, 0}, {1, -3, 1},
      {1, -1, -1}, {1, -1, 0}, {1, -1, 1}, {1, -1, 2},
      {1, 0, -1}, {1, 0, 0}, {1, 0, 1}, {1, 0, 2},
      {1, 1, -1}, {1, 1, 0}, {1, 1, 1}, {1, 1, 2},
      {1, 3, -1}, {1, 3, 0}, {1, 3, 1}, {1, 3, 2}, {1, 3, 3},
      {1, 5, 0}, {1, 5, 1}, {1, 5, 2}, {1, 5, 3},
      {2, -1, 0}, {2, -1, 1}, {2, -1, 2},
      {2, 0, 0}, {2, 0, 1}, {2, 0, 2},
      {2, 1, 0}, {2, 1, 1}, {2, 1, 2},
      {2, 3, 0}, {2, 3, 1}, {2, 3, 2}, {2, 3, 3},
      {2, 5, 0}, {2, 5, 1}, {2, 5, 2}, {2, 5, 3},
      {3, 3, 1}, {3, 3, 2}, {3, 3, 3},
      {3, 5, 1}, {3, 5, 2}, {3, 5, 3},
  };
  return k;
}
inline uint16_t Nudge565(uint16_t c, const RgbNudge& n) {
  auto clampi = [](int v, int hi) { return v < 0 ? 0 : (v > hi ? hi : v); };
  const int r = clampi(((c >> 11) & 31) + n.dr, 31);
  const int g = clampi(((c >> 5) & 63) + n.dg, 63);
  const int b = clampi((c & 31) + n.db, 31);
  return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

// Decodes the Huffman BC1 colour plane into raw BC1 blocks ((w/4)*(h/4)*8 bytes)
// and reports the plane's dimensions (stored in-band).
inline bool DecodeColourMip(const uint8_t* body, size_t size, int& w, int& h,
                            std::vector<uint8_t>& blocks) {
  if (size < 444) return false;
  BitStream s(body, size);
  w = static_cast<int>(s.take(16));
  h = static_cast<int>(s.take(16));
  if (w <= 0 || h <= 0 || w > 8192 || h > 8192 || (w & 3) || (h & 3)) return false;
  uint32_t widx[256], wop[62], wnud[122];
  for (int i = 0; i < 256; ++i) widx[i] = FreqWeight(static_cast<uint8_t>(s.take(8)));
  for (int i = 0; i < 62; ++i) wop[i] = FreqWeight(static_cast<uint8_t>(s.take(8)));
  for (int i = 0; i < 122; ++i) wnud[i] = FreqWeight(static_cast<uint8_t>(s.take(8)));
  Codebook cb, co, cd;
  const int tidx = cb.Build(widx, 256);
  const int top = co.Build(wop, 62);
  const int tnud = cd.Build(wnud, 122);
  if (tidx < 0 || top < 0 || tnud < 0) return false;
  const auto* ops = NeighborOps();
  const auto* nudges = Nudges();
  const int cols = w / 4, rows = h / 4;
  blocks.assign(static_cast<size_t>(cols) * rows * 8, 0);
  auto read_end = [&](int x, int y, uint16_t& c0, uint16_t& c1) {
    if (x < 0 || y < 0 || x >= cols || y >= rows) {
      c0 = c1 = 0;
      return;
    }
    const uint8_t* p = blocks.data() + (static_cast<size_t>(y) * cols + x) * 8;
    c0 = static_cast<uint16_t>(p[0] | (p[1] << 8));
    c1 = static_cast<uint16_t>(p[2] | (p[3] << 8));
  };
  for (int ry = 0; ry < rows; ++ry) {
    for (int rx = 0; rx < cols; ++rx) {
      const int oi = co.Symbol(s);
      if (oi < 0 || oi >= 62) return false;
      const NeighborOp& o = ops[oi];
      uint16_t c0, c1;
      if (o.op == 4) {
        c0 = static_cast<uint16_t>(s.take(16));
        c1 = static_cast<uint16_t>(s.take(16));
      } else {
        read_end(rx + o.dx, ry + o.dy, c0, c1);
        if (o.op == 1 || o.op == 3) {
          const int d = cd.Symbol(s);
          if (d < 0 || d >= 122) return false;
          c0 = Nudge565(c0, nudges[d]);
        }
        if (o.op == 2 || o.op == 3) {
          const int d = cd.Symbol(s);
          if (d < 0 || d >= 122) return false;
          c1 = Nudge565(c1, nudges[d]);
        }
        if (c0 < c1) std::swap(c0, c1);
      }
      uint8_t* out = blocks.data() + (static_cast<size_t>(ry) * cols + rx) * 8;
      out[0] = static_cast<uint8_t>(c0 & 0xFF);
      out[1] = static_cast<uint8_t>(c0 >> 8);
      out[2] = static_cast<uint8_t>(c1 & 0xFF);
      out[3] = static_cast<uint8_t>(c1 >> 8);
      if (c0 == c1) {
        out[4] = out[5] = out[6] = out[7] = 0;
      } else {
        int s0 = cb.Symbol(s), s1 = cb.Symbol(s);
        int s2 = cb.Symbol(s), s3 = cb.Symbol(s);
        if (s0 < 0 || s1 < 0 || s2 < 0 || s3 < 0) return false;
        out[4] = static_cast<uint8_t>((s0 & 0x0F) | ((s1 & 0x0F) << 4));
        out[5] = static_cast<uint8_t>(((s0 >> 4) & 0x0F) | (s1 & 0xF0));
        out[6] = static_cast<uint8_t>((s2 & 0x0F) | ((s3 & 0x0F) << 4));
        out[7] = static_cast<uint8_t>(((s2 >> 4) & 0x0F) | (s3 & 0xF0));
      }
    }
  }
  return true;
}

// Decodes the "variant" alpha plane into raw BC4 blocks ((w/4)*(h/4)*8 bytes).
// The block stream is run-length encoded: an op symbol carries a run length and
// selects one of four behaviours (clear, fill-0xFF, fill-value, or a coded
// 8-entry ramp + 16 three-bit indices).
inline bool DecodeAlphaMip(const uint8_t* body, size_t size, int w, int h,
                           std::vector<uint8_t>& blocks) {
  if (size < 32) return false;
  BitStream s(body, size);
  s.take(16);  // stored width (unused; the caller supplies the dimensions)
  s.take(16);  // stored height
  s.take(4);   // mode flag (unused)
  s.take(8);   // reserved byte
  uint32_t wa[64], wb[32], wc[64], wo[32];
  for (int i = 0; i < 64; ++i) wa[i] = FreqWeight(static_cast<uint8_t>(s.take(8)));
  for (int i = 0; i < 32; ++i) wb[i] = FreqWeight(static_cast<uint8_t>(s.take(8)));
  for (int i = 0; i < 64; ++i) wc[i] = FreqWeight(static_cast<uint8_t>(s.take(8)));
  for (int i = 0; i < 32; ++i) wo[i] = FreqWeight(static_cast<uint8_t>(s.take(8)));
  Codebook ca, cb2, cc, co;
  const int ta = ca.Build(wa, 64);
  const int tb = cb2.Build(wb, 32);
  const int tc = cc.Build(wc, 64);
  const int to = co.Build(wo, 32);
  if (ta < 0 || tb < 0 || tc < 0 || to < 0) return false;
  uint8_t dq[64];
  for (int v = 0; v < 64; ++v) dq[v] = static_cast<uint8_t>((v * 255 + 31) / 63);
  auto clamp6 = [](int v) { return v < 0 ? 0 : (v > 63 ? 63 : v); };
  const int cols = w / 4, rows = h / 4;
  blocks.assign(static_cast<size_t>(cols) * rows * 8, 0);
  int run_left = 0, prev_op = -1;
  uint8_t prev_fill = 0;
  for (int ry = 0; ry < rows; ++ry) {
    for (int rx = 0; rx < cols; ++rx) {
      int op;
      uint8_t fill = 0;
      if (run_left > 0) {
        --run_left;
        op = prev_op;
        fill = prev_fill;
      } else {
        const int sym = co.Symbol(s);
        if (sym < 0) return false;
        const int len = sym & 7;
        op = sym >> 3;
        const uint32_t extra = len > 0 ? s.take(len) : 0;
        run_left = (1 << len) + static_cast<int>(extra) - 1;
        if (op == 2) fill = static_cast<uint8_t>(s.take(8));
        prev_op = op;
        prev_fill = fill;
      }
      const size_t at = (static_cast<size_t>(ry) * cols + rx) * 8;
      switch (op) {
        case 0:  // clear (already zeroed)
          break;
        case 1:
          for (int i = 0; i < 8; ++i) blocks[at + i] = 0xFF;
          break;
        case 2:
          blocks[at] = fill;
          blocks[at + 1] = fill;
          for (int i = 2; i < 8; ++i) blocks[at + i] = 0;
          break;
        case 3: {
          const int sa = ca.Symbol(s), sb = cb2.Symbol(s);
          if (sa < 0 || sb < 0) return false;
          const int a0 = dq[clamp6(sa + sb)], a1 = dq[clamp6(sa - sb)];
          uint64_t packed = 0;
          for (int r = 0; r < 4; ++r) {
            const int p1 = cc.Symbol(s), p2 = cc.Symbol(s);
            if (p1 < 0 || p2 < 0) return false;
            const int idx[4] = {p1 & 7, (p1 >> 3) & 7, p2 & 7, (p2 >> 3) & 7};
            for (int i = 0; i < 4; ++i) {
              packed |= static_cast<uint64_t>(idx[i] & 7) << ((r * 4 + i) * 3);
            }
          }
          blocks[at] = static_cast<uint8_t>(a0);
          blocks[at + 1] = static_cast<uint8_t>(a1);
          for (int i = 0; i < 6; ++i) blocks[at + 2 + i] = static_cast<uint8_t>((packed >> (8 * i)) & 0xFF);
          break;
        }
        default:
          return false;
      }
    }
  }
  return true;
}

}  // namespace texcodec

// Decodes a compressed controller-icon bank entry (CompFlag 1/11 Huffman BC3, or
// CompFlag 7 raw BC3) into a straight 8-bit RGBA surface in Xenos 2D tile order,
// prefixed by the big-endian uint32 pixel_byte_count -- the exact layout of an
// uncompressed 0x00A2 entry (matching keycap_render.h output). width and height
// are the texture dimensions from the header bank (used for the raw path, which
// does not store them in-band). Returns false for unrecognized entries.
inline bool DecodeCompressedIconToTile(const uint8_t* payload, size_t size, int width, int height,
                                       std::vector<uint8_t>& out) {
  using namespace texcodec;
  if (size < 48) return false;
  auto u32be = [&](int o) {
    return static_cast<uint32_t>(payload[o]) << 24 | static_cast<uint32_t>(payload[o + 1]) << 16 |
           static_cast<uint32_t>(payload[o + 2]) << 8 | static_cast<uint32_t>(payload[o + 3]);
  };
  const int compflag = static_cast<int>(u32be(0));
  const int data_offset = static_cast<int>(u32be(4));
  const int data_size = static_cast<int>(u32be(8));
  const int dual_flag = static_cast<int>(u32be(12));
  const int dual_offset = static_cast<int>(u32be(16));
  const int dual_size = static_cast<int>(u32be(20));
  if (data_offset != 48 || data_size <= 0 ||
      static_cast<size_t>(data_offset) + static_cast<size_t>(data_size) > size)
    return false;
  if (compflag < 1 || compflag > 7 || compflag == 5 || compflag == 6) return false;

  std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4, 0);  // A,R,G,B per pixel

  auto emit = [&]() {
    const int w = width, h = height;
    out.assign(4 + static_cast<size_t>(w) * h * 4, 0);
    const uint32_t pbc = static_cast<uint32_t>(static_cast<size_t>(w) * h * 4);
    out[0] = static_cast<uint8_t>(pbc >> 24);
    out[1] = static_cast<uint8_t>(pbc >> 16);
    out[2] = static_cast<uint8_t>(pbc >> 8);
    out[3] = static_cast<uint8_t>(pbc);
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        const uint8_t* src = rgba.data() + (static_cast<size_t>(y) * w + x) * 4;
        int off = 4 + XenosTiledOffset(x, y, w);
        out[off] = src[0];
        out[off + 1] = src[1];
        out[off + 2] = src[2];
        out[off + 3] = src[3];
      }
    }
  };

  if (compflag == 7) {
    // Raw BC3: 16 bytes per 4x4 block = [8 BC4 alpha][8 BC1 colour], fields swapped.
    const int cols = (width + 3) / 4, rows = (height + 3) / 4;
    const size_t nblocks = static_cast<size_t>(cols) * rows;
    if (static_cast<size_t>(data_size) < nblocks * 16) return false;
    std::vector<uint8_t> sw(static_cast<size_t>(data_size));
    for (size_t i = 0; i + 1 < sw.size(); i += 2) {
      sw[i] = payload[static_cast<size_t>(data_offset) + i + 1];
      sw[i + 1] = payload[static_cast<size_t>(data_offset) + i];
    }
    size_t o = 0;
    for (int ry = 0; ry < rows; ++ry) {
      for (int rx = 0; rx < cols; ++rx) {
        uint8_t a[16], col[64];
        ExpandBc4(sw.data() + o, a);
        ExpandBc1(sw.data() + o + 8, col);
        o += 16;
        for (int py = 0; py < 4; ++py) {
          const int Y = ry * 4 + py;
          if (Y >= height) break;
          for (int px = 0; px < 4; ++px) {
            const int X = rx * 4 + px;
            if (X >= width) break;
            uint8_t* d = rgba.data() + (static_cast<size_t>(Y) * width + X) * 4;
            d[0] = a[py * 4 + px];
            d[1] = col[(py * 4 + px) * 4 + 1];
            d[2] = col[(py * 4 + px) * 4 + 2];
            d[3] = col[(py * 4 + px) * 4 + 3];
          }
        }
      }
    }
    emit();
    return true;
  }

  // CompFlag 1/11 (and the 2/3/4 colour path): Huffman BC1 colour + variant alpha.
  std::vector<uint8_t> bc1;
  int w, h;
  if (!DecodeColourMip(payload + data_offset, static_cast<size_t>(data_size), w, h, bc1)) return false;
  const int cols = w / 4, rows = h / 4;
  for (int ry = 0; ry < rows; ++ry) {
    for (int rx = 0; rx < cols; ++rx) {
      uint8_t col[64];
      ExpandBc1(bc1.data() + (static_cast<size_t>(ry) * cols + rx) * 8, col);
      for (int py = 0; py < 4; ++py) {
        const int Y = ry * 4 + py;
        if (Y >= h) break;
        for (int px = 0; px < 4; ++px) {
          const int X = rx * 4 + px;
          if (X >= w) break;
          uint8_t* d = rgba.data() + (static_cast<size_t>(Y) * w + X) * 4;
          d[0] = col[(py * 4 + px) * 4];
          d[1] = col[(py * 4 + px) * 4 + 1];
          d[2] = col[(py * 4 + px) * 4 + 2];
          d[3] = col[(py * 4 + px) * 4 + 3];
        }
      }
    }
  }
  if (dual_size > 0 && static_cast<size_t>(dual_offset) + static_cast<size_t>(dual_size) <= size &&
      (dual_flag == 2 || dual_flag == 3 || dual_flag == 4)) {
    std::vector<uint8_t> bc4;
    if (DecodeAlphaMip(payload + dual_offset, static_cast<size_t>(dual_size), w, h, bc4)) {
      for (int ry = 0; ry < rows; ++ry) {
        for (int rx = 0; rx < cols; ++rx) {
          uint8_t a[16];
          ExpandBc4(bc4.data() + (static_cast<size_t>(ry) * cols + rx) * 8, a);
          for (int py = 0; py < 4; ++py) {
            const int Y = ry * 4 + py;
            if (Y >= h) break;
            for (int px = 0; px < 4; ++px) {
              const int X = rx * 4 + px;
              if (X >= w) break;
              rgba[(static_cast<size_t>(Y) * w + X) * 4] = a[py * 4 + px];
            }
          }
        }
      }
    }
  }
  emit();
  return true;
}

}  // namespace fable2::gui

#endif  // FABLE2_GUI_GUI_TEX_CODEC_H
