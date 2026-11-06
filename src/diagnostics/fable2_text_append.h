// fable2_text_append.h - append " DEADBEEF" to every UI text element.
//
// Hooks the guest UI text rendering pipeline:
//   UIText_RenderSegment (0x82C0A8F8)  - walks one segment's glyph/element
//                                       linked list once per frame.
//   UITextElement_Draw                      - the per-element draw called for every
//                                       visible element (r3 = element object,
//                                       r4 = render context).
//
// APPEND MODE (FABLE2_TEXT_APPEND=1):
//   For each segment, clone the LAST drawn element 8 times into guest heap
//   blocks (via the game's own allocator), set each clone's character field
//   to one char of "DEADBEEF", advance its position, and re-invoke the
//   original per-element draw with the live render context. The appended
//   text is rendered by the game's own font pipeline, anchored to the end
//   of the segment, and only exists while the segment is on screen.
//
// PROBE MODE (FABLE2_TEXT_APPEND_PROBE=1):
//   Dumps element objects (with printable-char field candidates), render
//   contexts, and the segment/node list structure to fable2_text_append.log
//   so the char/position field offsets can be confirmed.
//
// Env vars:
//   FABLE2_TEXT_APPEND=1         enable appending
//   FABLE2_TEXT_APPEND_PROBE=1   verbose probe dump
//   FABLE2_TEXT_APPEND_EVERY=N   probe throttle, every Nth call (default 40)
//   FABLE2_TA_CHAROFF=N          element offset of the char field (default 0x30)
//   FABLE2_TA_XOFF=N             element offset of the x position float (0x34)
//   FABLE2_TA_ADV=F              x advance per appended char (default 0.4)
//
// Log: fable2_text_append.log in the CWD (exe dir).

#pragma once

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include <rex/ppc/context.h>

#include "fable2_ui_render_probe.h"

namespace fable2::textappend {

// ---------------------------------------------------------------------------
// Config / logging
// ---------------------------------------------------------------------------

inline bool append_on() {
  static const bool on = [] {
#ifdef _WIN32
    char v[8] = {};
    size_t n = 0;
    return ::getenv_s(&n, v, sizeof(v), "FABLE2_TEXT_APPEND") == 0 && v[0] == '1';
#else
    const char* v = std::getenv("FABLE2_TEXT_APPEND");
    return v != nullptr && v[0] == '1';
#endif
  }();
  return on;
}

inline bool probe_on() {
  static const bool on = [] {
#ifdef _WIN32
    char v[8] = {};
    size_t n = 0;
    return ::getenv_s(&n, v, sizeof(v), "FABLE2_TA_PROBE") == 0 && v[0] == '1';
#else
    const char* v = std::getenv("FABLE2_TA_PROBE");
    return v != nullptr && v[0] == '1';
#endif
  }();
  return on;
}

inline int every() {
  static const int step = [] {
#ifdef _WIN32
    char v[16] = {};
    size_t n = 0;
    ::getenv_s(&n, v, sizeof(v), "FABLE2_TEXT_APPEND_EVERY");
    return v[0] ? std::atoi(v) : 40;
#else
    const char* v = std::getenv("FABLE2_TEXT_APPEND_EVERY");
    return v ? std::atoi(v) : 40;
#endif
  }();
  return step;
}

inline uint32_t charoff() {
  static const uint32_t o = [] {
#ifdef _WIN32
    char v[16] = {};
    size_t n = 0;
    ::getenv_s(&n, v, sizeof(v), "FABLE2_TA_CHAROFF");
    return v[0] ? (uint32_t)std::strtoul(v, nullptr, 0) : 0x30u;
#else
    const char* v = std::getenv("FABLE2_TA_CHAROFF");
    return v ? (uint32_t)std::strtoul(v, nullptr, 0) : 0x30u;
#endif
  }();
  return o;
}

inline uint32_t xoff() {
  static const uint32_t o = [] {
#ifdef _WIN32
    char v[16] = {};
    size_t n = 0;
    ::getenv_s(&n, v, sizeof(v), "FABLE2_TA_XOFF");
    return v[0] ? (uint32_t)std::strtoul(v, nullptr, 0) : 0x34u;
#else
    const char* v = std::getenv("FABLE2_TA_XOFF");
    return v ? (uint32_t)std::strtoul(v, nullptr, 0) : 0x34u;
#endif
  }();
  return o;
}

inline float adv() {
  static const float a = [] {
#ifdef _WIN32
    char v[32] = {};
    size_t n = 0;
    ::getenv_s(&n, v, sizeof(v), "FABLE2_TA_ADV");
    return v[0] ? std::strtof(v, nullptr) : 0.4f;
#else
    const char* v = std::getenv("FABLE2_TA_ADV");
    return v ? std::strtof(v, nullptr) : 0.4f;
#endif
  }();
  return a;
}

inline FILE*& logf() {
  static FILE* f = [] {
    FILE* out = std::fopen("fable2_text_append.log", "w");
    if (out) std::setvbuf(out, nullptr, _IONBF, 0);
    return out;
  }();
  return f;
}

inline std::atomic<long>& log_bytes() {
  static std::atomic<long> b{0};
  return b;
}

inline void log_line(const char* fmt, ...) {
  FILE* f = logf();
  if (!f) return;
  if (log_bytes().load() > 4000000) return;  // hard cap ~4MB
  va_list args;
  va_start(args, fmt);
  int n = std::vfprintf(f, fmt, args);
  va_end(args);
  if (n > 0) log_bytes().fetch_add(n);
}

// ---------------------------------------------------------------------------
// Guest memory helpers (big-endian guest, host base = arena base)
// ---------------------------------------------------------------------------

inline uint64_t host_addr(const uint8_t* base, uint32_t a) {
  return (uint64_t)(base + (a >= 0xE0000000u ? a + 0x1000u : a));
}

inline bool wr(uint64_t h, size_t n) {
#ifdef _WIN32
  uint64_t end = h + n;
  uint64_t cur = h;
  while (cur < end) {
    MEMORY_BASIC_INFORMATION m = {};
    if (!VirtualQuery((void*)cur, &m, sizeof(m))) return false;
    if (m.State != MEM_COMMIT) return false;
    if ((m.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE)) ==
        0)
      return false;
    const uint64_t page_end = (uint64_t)m.BaseAddress + m.RegionSize;
    if (page_end <= cur) break;
    cur = page_end < end ? page_end : end;
  }
  return true;
#else
  (void)h;
  (void)n;
  return true;
#endif
}

// Readable (committed, not NOACCESS) -- vtables live in read-only pages.
inline bool rdable(uint64_t h, size_t n) {
#ifdef _WIN32
  uint64_t end = h + n;
  uint64_t cur = h;
  while (cur < end) {
    MEMORY_BASIC_INFORMATION m = {};
    if (!VirtualQuery((void*)cur, &m, sizeof(m))) return false;
    if (m.State != MEM_COMMIT) return false;
    if (m.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    const uint64_t page_end = (uint64_t)m.BaseAddress + m.RegionSize;
    if (page_end <= cur) break;
    cur = page_end < end ? page_end : end;
  }
  return true;
#else
  (void)h;
  (void)n;
  return true;
#endif
}

inline bool rdable2(const uint8_t* base, uint32_t a, size_t n) {
  return rdable(host_addr(base, a), n);
}

inline bool wr2(const uint8_t* base, uint32_t a, size_t n) {
  return wr(host_addr(base, a), n);
}

inline uint32_t load_be(const uint8_t* base, uint32_t a) {
  uint32_t w = 0;
  std::memcpy(&w, base + (a >= 0xE0000000u ? a + 0x1000u : a), 4);
  return __builtin_bswap32(w);
}

inline float load_f32(const uint8_t* base, uint32_t a) {
  uint32_t w = load_be(base, a);
  float f;
  std::memcpy(&f, &w, 4);
  return f;
}

inline void dump_words(const char* name, const uint8_t* base, uint32_t a, int nwords) {
  if (nwords > 128) nwords = 128;
  for (int row = 0; row < nwords; row += 8) {
    char line[200] = {};
    int off = std::snprintf(line, sizeof(line), "%s +0x%02X:", name, row * 4);
    for (int c = row; c < row + 8 && c < nwords; ++c) {
      uint32_t w = 0;
      bool ok = rdable2(base, a + c * 4, 4);
      if (ok) w = load_be(base, a + c * 4);
      off += std::snprintf(line + off, sizeof(line) - off, ok ? " %08X" : " XXXXXXXX", w);
    }
    log_line("%s\n", line);
  }
}

// Scan 256 bytes of the element for printable ASCII bytes (u8) and
// printable UTF-16BE pairs, and log the candidate char-field offsets.
inline void dump_char_cands(const char* name, const uint8_t* base, uint32_t el) {
  uint8_t b[256] = {};
  if (!wr2(base, el, sizeof(b))) return;
  std::memcpy(b, base + el, sizeof(b));
  char u8line[220] = "cand8  ";
  char u16line[220] = "cand16 ";
  int o8 = std::snprintf(u8line + 8, sizeof(u8line) - 8, "0x%08X:", el);
  int o16 = std::snprintf(u16line + 8, sizeof(u16line) - 8, "0x%08X:", el);
  for (int i = 0; i < 256 && o8 < 200; ++i)
    if (b[i] >= 0x20 && b[i] < 0x7F)
      o8 += std::snprintf(u8line + o8, sizeof(u8line) - o8, " %d=%c", i, b[i]);
  for (int i = 0; i + 1 < 256 && o16 < 200; i += 2)
    if (b[i] == 0 && b[i + 1] >= 0x20 && b[i + 1] < 0x7F)
      o16 += std::snprintf(u16line + o16, sizeof(u16line) - o16, " %d=%c", i, b[i + 1]);
  log_line("%s\n", u8line);
  log_line("%s\n", u16line);
  (void)name;
}

// ---------------------------------------------------------------------------
// Guest allocator (the game's primary malloc thunk, 3725 call sites):
//   Allocate_SizeBucketed_Thunk_8221F3F0(r3 = size) -> r3 = pointer (0 on OOM)
// ---------------------------------------------------------------------------

extern "C" void __imp__Allocate_SizeBucketed_Thunk_8221F3F0(PPCContext& __restrict,
                                                            uint8_t*);
extern "C" void __imp__UITextElement_Draw(PPCContext& __restrict, uint8_t*);
// Full per-element draw (setup + state build + UITextElement_Draw + vertex emit).
extern "C" void __imp__UIText_RenderElement(PPCContext& __restrict, uint8_t*);

inline uint32_t guest_alloc(PPCContext& ctx, uint8_t* base, size_t size) {
  uint32_t saved_r3 = (uint32_t)ctx.r3.u64;
  ctx.r3.u64 = size;
  __imp__Allocate_SizeBucketed_Thunk_8221F3F0(ctx, base);
  uint32_t p = (uint32_t)ctx.r3.u64;
  ctx.r3.u64 = saved_r3;
  return p;
}

// ---------------------------------------------------------------------------
// Probe dumps
// ---------------------------------------------------------------------------

inline void probe_element(const uint8_t* base, uint32_t el, uint32_t rtx) {
  log_line("--- element r3=0x%08X r4=0x%08X ---\n", el, rtx);
  if (!wr2(base, el, 512)) {
    log_line("element not readable\n");
    return;
  }
  dump_words("el   ", base, el, 128);
  dump_char_cands("el", base, el);
  // The element's draw object at +16 and its vtable (slot 9 = +36 build,
  // slot 13 = +52 frame draw, slot 14 = +56 regen, slot 7 = +28 alt).
  // The per-glyph quad template lives at draw+344 (0x158).
  uint32_t d = load_be(base, (uint32_t)(el + 16));
  log_line("el+16 draw=0x%08X\n", d);
  if (d >= 0x3F000000u && d < 0x84000000u && rdable2(base, d, 512)) {
    dump_words("draw ", base, d, 128);
    dump_char_cands("draw", base, d);
    uint32_t vt = load_be(base, d);
    log_line("draw vtable=0x%08X:", vt);
    if (vt >= 0x82000000u && vt < 0x84000000u && rdable2(base, vt, 64)) {
      for (int i = 0; i < 16; ++i) log_line(" %08X", load_be(base, (uint32_t)(vt + i * 4)));
      log_line("\n");
    }
  }
  dump_words("ctx  ", base, rtx, 16);
}

inline void probe_segment(const uint8_t* base, uint32_t seg, uint32_t r4) {
  log_line("=== segment r3=0x%08X r4=0x%08X ===\n", seg, r4);
  dump_words("seg  ", base, seg, 16);
  dump_words("r4   ", base, r4, 16);
  uint32_t h3 = 0, h4 = 0;
  if (wr2(base, seg + 4, 4)) h3 = load_be(base, (uint32_t)(seg + 4));
  if (wr2(base, r4 + 4, 4)) h4 = load_be(base, (uint32_t)(r4 + 4));
  log_line("seg+4=0x%08X r4+4=0x%08X\n", h3, h4);
  // Dump whatever the heads point at (node list or element array).
  for (uint32_t h : {h3, h4}) {
    if (h < 0x3F000000u || h >= 0x84000000u) continue;
    dump_words("head ", base, h, 16);
    // Follow a possible linked list: try +4 then +0 as next pointer.
    uint32_t p = h;
    for (int i = 0; i < 6; ++i) {
      if (p < 0x3F000000u || p >= 0x84000000u) break;
      if (!wr2(base, p, 64)) break;
      uint32_t w0 = load_be(base, p);
      uint32_t w4 = load_be(base, (uint32_t)(p + 4));
      uint32_t w12 = load_be(base, (uint32_t)(p + 12));
      log_line("node[%d]@0x%08X w0=0x%08X w4=0x%08X w12=0x%08X\n", i, p, w0, w4, w12);
      // Element object candidate at +12.
      if (w12 >= 0x3F000000u && w12 < 0x84000000u && wr2(base, w12, 256)) {
        dump_words("  el ", base, w12, 32);
      }
      uint32_t nxt = (w4 >= 0x3F000000u && w4 < 0x84000000u && wr2(base, w4, 16))
                         ? w4
                         : (w0 >= 0x3F000000u && w0 < 0x84000000u && wr2(base, w0, 16) ? w0 : 0);
      if (nxt == 0 || nxt == h) break;
      p = nxt;
    }
  }
  // The per-element render context *(seg+80).
  if (wr2(base, seg + 80, 4)) {
    uint32_t c = load_be(base, (uint32_t)(seg + 80));
    log_line("seg+80 ctx=0x%08X\n", c);
    dump_words("segctx", base, c, 16);
  }
}

// ---------------------------------------------------------------------------
// Append state: per-segment clones of the last element
// ---------------------------------------------------------------------------

struct SegmentClones {
  uint32_t seg = 0;
  uint32_t last_el = 0;
  uint32_t clones[8] = {};
};

inline std::unordered_map<uint32_t, SegmentClones>& table() {
  static std::unordered_map<uint32_t, SegmentClones> t;
  return t;
}

const char* const kAppendText = "DEADBEEF";
constexpr int kLen = 8;
constexpr uint32_t kCloneSize = 256;   // element object
constexpr uint32_t kDrawSize = 1024;    // draw object (>=672 bytes)
constexpr uint32_t kSubSize = 128;      // glyph sub-item object

// Last element actually drawn (tracked by the UITextElement_Draw hook). The
// RenderSegment hook reads this AFTER __imp__ returns to anchor the append
// on the last glyph of the segment just drawn.
inline uint32_t& last_drawn_el() {
  static uint32_t v = 0;
  return v;
}
inline uint32_t& last_drawn_ctx() {
  static uint32_t v = 0;
  return v;
}

// Collect the element's glyph sub-item pointers. The glyph sub-items live at
// fixed offsets in the element (el+0x28 and el+0x44..el+0x5C) and are heap
// objects in the 0x425xxxxx range. We restrict to those offsets AND that
// range so we don't grab the element self-ref, draw object, or shared
// resources (0x404xxxxx).
inline std::vector<uint32_t> collect_subitems(const uint8_t* base, uint32_t el) {
  std::vector<uint32_t> out;
  static const uint32_t kOffs[] = {0x28, 0x44, 0x48, 0x4C, 0x50, 0x54, 0x58, 0x5C};
  for (uint32_t off : kOffs) {
    if (!wr2(base, el + off, 4)) continue;
    uint32_t p = load_be(base, el + off);
    if (p >= 0x42400000u && p < 0x42600000u && wr2(base, p, kSubSize)) {
      bool dup = false;
      for (uint32_t q : out)
        if (q == p) dup = true;
      if (!dup) out.push_back(p);
    }
  }
  return out;
}



// Find the last element object in the segment's node list.
// RenderSegment's recompiled walk: sentinel = *(r4+4); node = *(sentinel+0);
// loop { el = *(node+12); node = *(node+4); } until node == sentinel
// (a ring list whose sentinel is the head wrapper). Element at node+12.
inline uint32_t find_last_element(const uint8_t* base, uint32_t seg, uint32_t r4) {
  auto okptr = [](uint32_t p) {
    return p >= 0x3F000000u && p < 0x84000000u;
  };
  uint32_t sentinel = 0;
  if (wr2(base, r4 + 4, 4)) sentinel = load_be(base, (uint32_t)(r4 + 4));
  if (!okptr(sentinel)) {
    sentinel = 0;
    if (wr2(base, seg + 4, 4)) sentinel = load_be(base, (uint32_t)(seg + 4));
  }
  if (!okptr(sentinel) || !wr2(base, sentinel, 16)) return 0;

  // Try the ring form first: sentinel+0 = first node, link via node+4.
  uint32_t node = load_be(base, (uint32_t)(sentinel + 0));
  uint32_t last_el = 0;
  if (okptr(node) && wr2(base, node, 16)) {
    uint32_t cur = node;
    for (int i = 0; i < 64 && okptr(cur) && cur != sentinel; ++i) {
      if (!wr2(base, cur, 16)) break;
      uint32_t el = load_be(base, (uint32_t)(cur + 12));
      if (okptr(el) && wr2(base, el, 256)) last_el = el;
      cur = load_be(base, (uint32_t)(cur + 4));
    }
    if (last_el) return last_el;
  }
  // Fallback: plain singly-linked list starting AT the sentinel (node = head,
  // link via +4, element at +12, ends at a zero/invalid next).
  uint32_t cur = sentinel;
  for (int i = 0; i < 64; ++i) {
    if (!okptr(cur) || !wr2(base, cur, 16)) break;
    uint32_t el = load_be(base, (uint32_t)(cur + 12));
    if (okptr(el) && wr2(base, el, 256)) last_el = el;
    uint32_t nxt = load_be(base, (uint32_t)(cur + 4));
    if (!okptr(nxt) || nxt == cur) break;
    cur = nxt;
  }
  if (last_el) return last_el;
  // Last resort: the head itself may be an element object.
  if (wr2(base, sentinel, 256)) return sentinel;
  return 0;
}

inline void write_f32_be(uint8_t* base, uint32_t a, float f) {
  uint32_t w;
  std::memcpy(&w, &f, 4);
  w = __builtin_bswap32(w);
  std::memcpy(base + (a >= 0xE0000000u ? a + 0x1000u : a), &w, 4);
}

inline void write_u16_be(uint8_t* base, uint32_t a, uint16_t v) {
  uint16_t w = (uint16_t)((v >> 8) | (v << 8));
  std::memcpy(base + (a >= 0xE0000000u ? a + 0x1000u : a), &w, 2);
}

inline void write_ptr_be(uint8_t* base, uint32_t a, uint32_t v) {
  uint32_t w = __builtin_bswap32(v);
  std::memcpy(base + (a >= 0xE0000000u ? a + 0x1000u : a), &w, 4);
}

// Recently-drawn element ring (filled by the UITextElement_Draw hook). Only
// elements that were actually drawn this frame are safe append targets.
inline std::vector<uint32_t>& recent_draws() {
  static std::vector<uint32_t> v;
  return v;
}

inline bool was_recently_drawn(uint32_t el) {
  auto& v = recent_draws();
  for (uint32_t e : v) if (e == el) return true;
  return false;
}

// The element's draw object must be a committed object whose vtable lives in
// the guest .text range, otherwise drawing the clone would fault.
inline bool element_is_safe(const uint8_t* base, uint32_t el) {
  if (!wr2(base, el, 256)) return false;
  uint32_t d = load_be(base, (uint32_t)(el + 16));
  if (d < 0x3F000000u || d >= 0x84000000u) return false;
  if (!wr2(base, d, 64)) return false;
  uint32_t vt = load_be(base, d);
  // vtables live in read-only (PAGE_EXECUTE_READ) guest pages: use rdable,
  // not wr2, or the gate always fails.
  return vt >= 0x82000000u && vt < 0x83438000u && rdable2(base, vt, 64);
}

// Throttled reason logging for why an append was skipped.
inline void log_skip(const char* why, uint32_t seg, uint32_t last_el) {
  static std::atomic<int> n{0};
  if (n.fetch_add(1) < 40)
    log_line("[skip] %s seg=0x%08X last_el=0x%08X\n", why, seg, last_el);
}

// Every guest object we allocate for cloning (element, draw object,
// sub-items) is registered here so the draw hooks can tell a clone apart
// from a real element and not use it as an append anchor.
inline std::vector<uint32_t>& clone_set() {
  static std::vector<uint32_t> v;
  return v;
}
inline void mark_clone(PPCContext& ctx, uint32_t a) {
  auto& v = clone_set();
  if (v.size() > 4096) v.clear();  // bound; stale entries are harmless
  if (std::find(v.begin(), v.end(), a) == v.end()) v.push_back(a);
}

inline bool is_clone(uint32_t el) {
  auto& v = clone_set();
  for (uint32_t a : v)
    if (a == el) return true;
  auto& t = table();
  for (auto& kv : t)
    for (int i = 0; i < kLen; ++i)
      if (kv.second.clones[i] == el) return true;
  return false;
}

// Replace every occurrence of `from` with `to` in a guest object.
inline void repoint_obj(uint8_t* base, uint32_t obj, size_t size, uint32_t from,
                        uint32_t to) {
  for (uint32_t off = 0; off + 4 <= size; off += 4) {
    if (!wr2(base, obj + off, 4)) continue;
    if (load_be(base, obj + off) == from) write_ptr_be(base, obj + off, to);
  }
}

// Append: clone the last-drawn element (a whole text block: element + draw
// object + glyph sub-items), repoint cross-references, shift the clones to
// the right, and draw them. Produces a copy of the block immediately after
// the original; the probe refines this into the literal "DEADBEEF".
inline void append_segment(PPCContext& ctx, uint8_t* base, uint32_t seg, uint32_t r4) {
  uint32_t last_el = last_drawn_el();
  uint32_t rtx = last_drawn_ctx();
  if (!last_el) { log_skip("no-last-element", seg, 0); return; }
  if (!element_is_safe(base, last_el)) { log_skip("unsafe-element", seg, last_el); return; }
  if (!wr2(base, last_el, kCloneSize)) { log_skip("el-not-wr", seg, last_el); return; }

  // Rebuild the clone set when the anchor element changes.
  auto& t = table();
  if (t.size() > 96) t.clear();  // scene change; drop stale clones
  SegmentClones& sc = t[seg];
  if (sc.last_el != last_el) {
    sc.last_el = last_el;
    sc.seg = seg;
    for (int i = 0; i < kLen; ++i) sc.clones[i] = 0;

    // 1. Clone the glyph sub-items; remember the original->clone map.
    std::vector<uint32_t> subs = collect_subitems(base, last_el);
    // Probe: dump the original sub-items (once per anchor) to confirm the
    // glyph-node layout (vtable, x, y, glyph-data pointer).
    if (probe_on()) {
      log_line("[subitems] last_el=0x%08X count=%d\n", last_el, (int)subs.size());
      for (size_t i = 0; i < subs.size() && i < 12; ++i) {
        log_line("[subitem] #%d addr=0x%08X x8=%.9g x12=%.9g\n", (int)i, subs[i],
                 (double)load_f32(base, subs[i] + 8),
                 (double)load_f32(base, subs[i] + 12));
        dump_words("sub", base, subs[i], 16);
      }
    }
    std::vector<std::pair<uint32_t, uint32_t>> map;  // (orig, clone)
    for (uint32_t s : subs) {
      uint32_t cs = guest_alloc(ctx, base, kSubSize);
      if (!cs) continue;
      std::memcpy(base + cs, base + s, kSubSize);
      mark_clone(ctx, cs);
      map.push_back({s, cs});
    }

    // 2. Clone the element + draw object, then repoint all cross-refs.
    uint32_t c = guest_alloc(ctx, base, kCloneSize);
    if (!c) { log_skip("clone-alloc-fail", seg, last_el); return; }
    sc.clones[0] = c;
    std::memcpy(base + c, base + last_el, kCloneSize);
    mark_clone(ctx, c);
    uint32_t d = load_be(base, (uint32_t)(last_el + 16));  // draw object
    uint32_t cd = 0;
    if (d >= 0x3F000000u && d < 0x84000000u && wr2(base, d, kDrawSize)) {
      cd = guest_alloc(ctx, base, kDrawSize);
      if (cd) {
        std::memcpy(base + cd, base + d, kDrawSize);
        mark_clone(ctx, cd);
        write_ptr_be(base, (uint32_t)(c + 16), cd);
      }
    }
    for (auto& m : map) {
      repoint_obj(base, c, kCloneSize, m.first, m.second);
      if (cd) repoint_obj(base, cd, kDrawSize, m.first, m.second);
    }
    // 3. Shift each clone sub-item's x (+8: glyph node transform, per
    //    UIFont_RenderGlyph node+8 = x, node+12 = y) to the right so the
    //    copied block sits just after the original instead of on top of it.
    for (auto& m : map) {
      if (wr2(base, m.second + 8, 4)) {
        float x0 = load_f32(base, m.second + 8);
        write_f32_be(base, m.second + 8, x0 + adv());
      }
    }
    log_line("[append] seg=0x%08X last_el=0x%08X clone=0x%08X draw=0x%08X subs=%d adv=%.4f\n",
             seg, last_el, c, cd, (int)subs.size(), (double)adv());
  }

  if (!sc.clones[0]) return;
  uint32_t s3 = (uint32_t)ctx.r3.u64;
  uint32_t s4 = (uint32_t)ctx.r4.u64;
  ctx.r3.u64 = sc.clones[0];
  ctx.r4.u64 = rtx;
  // Full per-element draw: setup + state build + UITextElement_Draw + vertex emit.
  __imp__UIText_RenderElement(ctx, base);
  ctx.r3.u64 = s3;
  ctx.r4.u64 = s4;
}

}  // namespace fable2::textappend

// ===========================================================================
// Probe: the rare font layout/draw calls (UIFont_LookupGlyph emits one
// glyph quad; UIFont_EmitGlyphQuad writes the quad). These fire only when
// text is (re)laid out, so dump every call (capped).
// ===========================================================================

namespace fable2::textappend {

extern "C" void UIFont_LookupGlyph(PPCContext& __restrict ctx, uint8_t* base) {
  if (fable2::textappend::probe_on()) {
    static std::atomic<int> n{0};
    if (n.fetch_add(1) < 300) {
      uint32_t font = (uint32_t)ctx.r3.u64;
      float ch;
      std::memcpy(&ch, &ctx.f1.f64, 4);
      log_line("[LookupGlyph] font=0x%08X f1=%.9g r4=0x%08X r6=0x%08X\n", font, (double)ch,
               (uint32_t)ctx.r4.u64, (uint32_t)ctx.r6.u64);
      fable2::textappend::dump_words("font", base, font, 48);
      uint32_t s = (uint32_t)ctx.r6.u64;
      if (s >= 0x3F000000u && s < 0x84000000u)
        fable2::textappend::dump_words("out ", base, s, 32);
    }
  }
  if (fable2::uir::hook("UIFont_LookupGlyph", ctx, base)) return;
  __imp__UIFont_LookupGlyph(ctx, base);
}

// x-shift applied to each emitted glyph (FABLE2_TA_SHIFT, default 50000).
// The glyph-node x is a large integer (e.g. 114535), so a tens-of-thousands
// shift is a few glyph-widths.
inline uint32_t adv_shift() {
#ifdef _WIN32
  static const uint32_t a = [] {
    char v[32] = {};
    size_t n = 0;
    ::getenv_s(&n, v, sizeof(v), "FABLE2_TA_SHIFT");
    return v[0] ? (uint32_t)std::strtoul(v, nullptr, 0) : 50000u;
  }();
  return a;
#else
  static const uint32_t a = [] {
    const char* v = std::getenv("FABLE2_TA_SHIFT");
    return v ? (uint32_t)std::strtoul(v, nullptr, 0) : 50000u;
  }();
  return a;
#endif
}

// Controllable on-screen move of the prompt text (FABLE2_MOVE_X, FABLE2_MOVE_Y).
// Applied to the glyph-node x (r4, the layout x) for the layout pass only.
// The glyph-node x values are integers (~50..144058); a shift of ~30000 is a
// large visible fraction of the layout width.
inline int64_t move_x() {
  static const int64_t v = [] {
    const char* e = std::getenv("FABLE2_MOVE_X");
    return e ? std::atoll(e) : 0;
  }();
  return v;
}
inline int64_t move_y() {
  static const int64_t v = [] {
    const char* e = std::getenv("FABLE2_MOVE_Y");
    return e ? std::atoll(e) : 0;
  }();
  return v;
}

extern "C" void UIFont_EmitGlyphQuad(PPCContext& __restrict ctx, uint8_t* base) {
  // Move the prompt text: shift the glyph-node x (r4) for the layout pass.
  if (move_x() != 0) {
    const int64_t nx = (int64_t)(uint32_t)ctx.r4.u64 + move_x();
    if (nx >= 0) ctx.r4.u64 = (uint32_t)nx;
  }
  if (fable2::textappend::probe_on()) {
    static std::atomic<int> n{0};
    if (n.fetch_add(1) < 300) {
      uint32_t r3 = (uint32_t)ctx.r3.u64, r4 = (uint32_t)ctx.r4.u64;
      uint32_t r5 = (uint32_t)ctx.r5.u64, r6 = (uint32_t)ctx.r6.u64;
      float xf = 0;
      if (fable2::textappend::wr2(base, r5, 4)) xf = fable2::textappend::load_f32(base, r5);
      log_line("[EmitQuad] r3=0x%08X r4=0x%08X r5=0x%08X(x=%.9g) r6=0x%08X\n", r3, r4, r5,
               (double)xf, r6);
      if (r3 >= 0x3F000000u && r3 < 0x84000000u)
        fable2::textappend::dump_words("qsrc", base, r3, 24);
      if (r6 >= 0x3F000000u && r6 < 0x84000000u)
        fable2::textappend::dump_words("qdst", base, r6, 32);
    }
  }
  if (fable2::uir::hook("UIFont_EmitGlyphQuad", ctx, base)) return;
  __imp__UIFont_EmitGlyphQuad(ctx, base);
}

// ===========================================================================
// Probe: the layout pass. ProcessAndProcessAndProcess561_82C55AD8(r3 = glyph item, r4 = text-source
// wrapper); it calls the source object's vtable slot 0 ("fetch next
// glyph"). ProcessAndProcessAndProcess758_82C52A80(r3 = draw object, r5 = text-source wrapper) is the
// per-text layout entry.
// ===========================================================================

inline void probe_source(const char* who, const uint8_t* base, uint32_t wrap) {
  if (wrap < 0x3F000000u || wrap >= 0x84000000u) return;
  if (!wr2(base, wrap, 4)) return;
  uint32_t src = load_be(base, wrap);
  if (src < 0x3F000000u || src >= 0x84000000u) return;
  if (!wr2(base, src, 256)) return;
  log_line("[%s] wrapper=0x%08X src=0x%08X\n", who, wrap, src);
  dump_words("src  ", base, src, 64);
  dump_char_cands("src", base, src);
  uint32_t vt = load_be(base, src);
  if (vt >= 0x82000000u && vt < 0x84000000u && rdable2(base, vt, 64)) {
    log_line("src vtable=0x%08X:", vt);
    for (int i = 0; i < 16; ++i) log_line(" %08X", load_be(base, (uint32_t)(vt + i * 4)));
    log_line("\n");
  }
}

extern "C" void ProcessAndProcessAndProcess561_82C55AD8(PPCContext& __restrict ctx, uint8_t* base) {
  if (fable2::textappend::probe_on()) {
    static std::atomic<int> n{0};
    if (n.load() < 400) {
      const int i = n.fetch_add(1);
      if (i % 2 == 0) {
        uint32_t r3 = (uint32_t)ctx.r3.u64, r4 = (uint32_t)ctx.r4.u64;
        uint32_t r5 = (uint32_t)ctx.r5.u64, r6 = (uint32_t)ctx.r6.u64;
        log_line("[fetch]#%d in: r3=0x%08X r4=0x%08X r5=0x%08X r6=0x%08X\n", i, r3, r4, r5, r6);
        // The source object lives at *(r4) when r4 is a wrapper pointer.
        if (r4 >= 0x3F000000u && r4 < 0x84000000u && rdable2(base, r4, 4)) {
          uint32_t src = load_be(base, r4);
          log_line("[fetch]#%d src=0x%08X\n", i, src);
          if (src >= 0x3F000000u && src < 0x84000000u) {
            dump_words("src  ", base, src, 48);
            dump_char_cands("src", base, src);
            uint32_t vt = load_be(base, src);
            if (vt >= 0x82000000u && vt < 0x84000000u && rdable2(base, vt, 64)) {
              log_line("src vtable=0x%08X:", vt);
              for (int k = 0; k < 12; ++k)
                log_line(" %08X", load_be(base, (uint32_t)(vt + k * 4)));
              log_line("\n");
            }
          }
        }
        __imp__ProcessAndProcessAndProcess561_82C55AD8(ctx, base);
        float f1;
        std::memcpy(&f1, &ctx.f1.f64, 4);
        log_line("[fetch]#%d out: r3=0x%08X f1=%.9g\n", i, (uint32_t)ctx.r3.u64,
                 (double)f1);
        return;
      }
    }
  }
  __imp__ProcessAndProcessAndProcess561_82C55AD8(ctx, base);
}

extern "C" void ProcessAndProcessAndProcess758_82C52A80(PPCContext& __restrict ctx, uint8_t* base) {
  if (fable2::textappend::probe_on()) {
    static std::atomic<int> n{0};
    const int i = n.fetch_add(1);
    if (i < 60) {
      uint32_t r3 = (uint32_t)ctx.r3.u64, r4 = (uint32_t)ctx.r4.u64;
      uint32_t r5 = (uint32_t)ctx.r5.u64, r6 = (uint32_t)ctx.r6.u64;
      log_line("[layout]#%d in: r3=0x%08X r4=0x%08X r5=0x%08X r6=0x%08X\n", i, r3, r4, r5, r6);
      // r3 = draw object; +660/+668 are the x/y layout cursors.
      if (r3 >= 0x3F000000u && r3 < 0x84000000u && rdable2(base, r3 + 660, 16)) {
        float x0 = load_f32(base, r3 + 660), y0 = load_f32(base, r3 + 668);
        log_line("[layout]#%d cursors(before): x=%.9g y=%.9g\n", i, (double)x0, (double)y0);
      }
      __imp__ProcessAndProcessAndProcess758_82C52A80(ctx, base);
      if (r3 >= 0x3F000000u && r3 < 0x84000000u && rdable2(base, r3 + 660, 16)) {
        float x1 = load_f32(base, r3 + 660), y1 = load_f32(base, r3 + 668);
        log_line("[layout]#%d cursors(after):  x=%.9g y=%.9g\n", i, (double)x1, (double)y1);
      }
      return;
    }
  }
  __imp__ProcessAndProcessAndProcess758_82C52A80(ctx, base);
}

}  // namespace fable2::textappend

// ===========================================================================
// Hook: per-element draw (r3 = element, r4 = render context)
// ===========================================================================

extern "C" void UITextElement_Draw(PPCContext& __restrict ctx, uint8_t* base) {
  // Track which elements are actually being drawn (for safe append targets).
  {
    uint32_t el = (uint32_t)ctx.r3.u64;
    if (el >= 0x3F000000u && el < 0x84000000u &&
        !fable2::textappend::is_clone(el)) {
      auto& v = fable2::textappend::recent_draws();
      v.push_back(el);
      if (v.size() > 4096) v.erase(v.begin(), v.begin() + 2048);
      // Last-drawn anchor for the append.
      fable2::textappend::last_drawn_el() = el;
      fable2::textappend::last_drawn_ctx() = (uint32_t)ctx.r4.u64;
    }
  }
  if (fable2::textappend::probe_on()) {
    static std::atomic<int> n{0};
    const int step = fable2::textappend::every() > 0 ? fable2::textappend::every() : 40;
    if (n.fetch_add(1) % step == 0 && n.load() < 6000)
      fable2::textappend::probe_element(base, (uint32_t)ctx.r3.u64, (uint32_t)ctx.r4.u64);
  }
  if (fable2::uir::hook("UITextElement_Draw", ctx, base)) return;
  __imp__UITextElement_Draw(ctx, base);
  // Shift the per-glyph vertex x AFTER the state builder built the vertex data
  // (and BEFORE the vertex-emitting calls UITextElement_GrowBuffer/sub_82C0A230 run), so
  // the draw uses the shifted x.
  {
    static uint32_t shift = 0xFFFFFFFFu;
    if (shift == 0xFFFFFFFFu) {
      const char* es = std::getenv("FABLE2_TA_SHIFT");
      shift = es ? (uint32_t)std::atoi(es) : 0;
    }
    if (shift > 0)
      fable2::glyphprobe::shift_vertex_x(base, (uint32_t)ctx.r3.u64, shift);
  }
}

// ===========================================================================
// Hook: segment render (r3 = segment, r4 = seg2 / list carrier)
// ===========================================================================

// (The UIText_RenderElement hook lives in fable2_glyph_probe.h and calls
// fable2::glyphprobe::shift_run_x to move the on-screen text.)

extern "C" void UIText_RenderSegment(PPCContext& __restrict ctx, uint8_t* base) {
  const bool do_append = fable2::textappend::append_on();
  if (fable2::textappend::probe_on()) {
    static std::atomic<int> n{0};
    const int step = fable2::textappend::every() > 0 ? fable2::textappend::every() : 40;
    if (n.fetch_add(1) % step == 0 && n.load() < 6000)
      fable2::textappend::probe_segment(base, (uint32_t)ctx.r3.u64, (uint32_t)ctx.r4.u64);
  }
  if (fable2::uir::hook("UIText_RenderSegment", ctx, base)) return;
  __imp__UIText_RenderSegment(ctx, base);
  // The append is now done by shifting the glyph x in UIFont_EmitGlyphQuad
  // (see below); the element-clone path is disabled.
  (void)do_append;
}
