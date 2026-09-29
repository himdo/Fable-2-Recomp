// fable2_font_probe.h - locate the UI font's glyph table + atlas UVs.
//
// The glyph table is a binary search tree of nodes; each node has:
//   +0x00: prev/next ptr
//   +0x08: next ptr
//   +0x12: char code (byte)
//   +0x29 (41): flag (byte)
// We scan the guest heap for nodes whose +0x12 is a printable ASCII char and
// +0x29 is non-zero, then dump the node to learn the UV/size/advance layout
// and find the atlas texture reference.
//
//   FABLE2_FONT_PROBE=1        enable
//   FABLE2_FONT_PROBE_EVERY=N  re-scan every Nth frame (default 30)
//
// Log: fable2_font_probe.log in the CWD.

#pragma once

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <thread>
#include <unistd.h>
#endif

#include <rex/ppc/context.h>

namespace fable2::fontprobe {

inline bool enabled() {
  static const bool on = [] {
#ifdef _WIN32
    char v[8] = {};
    size_t n = 0;
    return ::getenv_s(&n, v, sizeof(v), "FABLE2_FONT_PROBE") == 0 && v[0] == '1';
#else
    const char* v = std::getenv("FABLE2_FONT_PROBE");
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
    ::getenv_s(&n, v, sizeof(v), "FABLE2_FONT_PROBE_EVERY");
    return v[0] ? std::atoi(v) : 30;
#else
    const char* v = std::getenv("FABLE2_FONT_PROBE_EVERY");
    return v ? std::atoi(v) : 30;
#endif
  }();
  return step;
}

inline FILE*& logf() {
  static FILE* f = [] {
    FILE* out = std::fopen("fable2_font_probe.log", "w");
    if (out) std::setvbuf(out, nullptr, _IONBF, 0);
    return out;
  }();
  return f;
}

inline void log_line(const char* fmt, ...) {
  FILE* f = logf();
  if (!f) return;
  va_list a;
  va_start(a, fmt);
  std::vfprintf(f, fmt, a);
  va_end(a);
}

inline bool wr(uint64_t h, size_t n) {
#ifdef _WIN32
  uint64_t cur = h, end = h + n;
  while (cur < end) {
    MEMORY_BASIC_INFORMATION m = {};
    if (!VirtualQuery((void*)cur, &m, sizeof(m))) return false;
    if (m.State != MEM_COMMIT) return false;
    if ((m.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE)) ==
        0)
      return false;
    const uint64_t pe = (uint64_t)m.BaseAddress + m.RegionSize;
    if (pe <= cur) break;
    cur = pe < end ? pe : end;
  }
  return true;
#else
  (void)h; (void)n;
  return true;
#endif
}

inline uint32_t load_be(const uint8_t* base, uint32_t a) {
  uint32_t w = 0;
  std::memcpy(&w, base + a, 4);
  return __builtin_bswap32(w);
}

// Find the UTF-16BE string "Press A to start" and dump the surrounding region
// (the text element + font are likely near the string buffer).
inline bool page_rw(uint64_t h) {
#ifdef _WIN32
  MEMORY_BASIC_INFORMATION m = {};
  if (!VirtualQuery((void*)h, &m, sizeof(m))) return false;
  return m.State == MEM_COMMIT &&
         (m.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE)) != 0;
#else
  (void)h;
  return true;
#endif
}

inline std::atomic<int>& hitcount() {
  static std::atomic<int> h{0};
  return h;
}

// The 'uv' (vertex-buffer offset) of glyph0 and glyph5, captured at layout
// time. A background thread dumps the vertex buffer a few ms later (after the
// render phase fills it).
inline std::atomic<uint32_t>& g_uv0() { static std::atomic<uint32_t> v{0}; return v; }
inline std::atomic<uint32_t>& g_uv5() { static std::atomic<uint32_t> v{0}; return v; }
inline std::atomic<bool>& vbuf_started() { static std::atomic<bool> b{false}; return b; }
// Glyph-data pointers (0x404DDxxx) captured at layout time, dumped once the
// render phase commits the pages.
inline std::atomic<uint32_t>& g_data0() { static std::atomic<uint32_t> v{0}; return v; }
inline std::atomic<uint32_t>& g_data1() { static std::atomic<uint32_t> v{0}; return v; }

inline void vbuf_dump_later(uint8_t* base) {
#ifdef _WIN32
  bool expect = false;
  if (!vbuf_started().compare_exchange_strong(expect, true)) return;
  CreateThread(nullptr, 0, [](LPVOID b) -> DWORD {
    auto* base = (uint8_t*)b;
    for (int i = 0; i < 600; ++i) {
      const uint32_t uv0 = g_uv0().load();
      const uint32_t uv5 = g_uv5().load();
      if (uv0 != 0 && page_rw((uint64_t)base + uv0)) {
        // Check if the buffer is filled (non-zero).
        uint32_t sum = 0;
        for (int c = 0; c < 24; ++c) sum += load_be(base, (uint32_t)(uv0 + c * 4));
        if (sum != 0) {
          log_line("== vbuf filled (uv0=0x%08X uv5=0x%08X)\n", uv0, uv5);
          char line[400] = {};
          int off = std::snprintf(line, sizeof(line), "  uv0:");
          for (int c = 0; c < 48; ++c)
            off += std::snprintf(line + off, sizeof(line) - off, " %08X",
                                 load_be(base, (uint32_t)(uv0 + c * 4)));
          log_line("%s\n", line);
          if (uv5 != 0 && uv5 != uv0 && page_rw((uint64_t)base + uv5)) {
            char l2[400] = {};
            int o2 = std::snprintf(l2, sizeof(l2), "  uv5:");
            for (int c = 0; c < 48; ++c)
              o2 += std::snprintf(l2 + o2, sizeof(l2) - o2, " %08X",
                                  load_be(base, (uint32_t)(uv5 + c * 4)));
            log_line("%s\n", l2);
          }
          // Dump the glyph data (64 bytes) for glyph0 and glyph1 -- these
          // differ per-character (the font-atlas UV / metrics).
          for (int d = 0; d < 2; ++d) {
            const uint32_t gd = (d == 0) ? g_data0().load() : g_data1().load();
            if (gd != 0 && gd >= 0x40000000u && gd < 0x80000000u &&
                page_rw((uint64_t)base + gd)) {
              char gdl[400] = {};
              int goff = std::snprintf(gdl, sizeof(gdl), "  data%d@0x%08X:", d, gd);
              for (int c = 0; c < 16; ++c)
                goff += std::snprintf(gdl + goff, sizeof(gdl) - goff, " %08X",
                                       load_be(base, (uint32_t)(gd + c * 4)));
              log_line("%s\n", gdl);
            }
          }
          return 0;
        }
      }
      Sleep(5);
    }
    return 0;
  }, base, 0, nullptr);
#else
  // The asynchronous probe relies on Windows page-access checks.
  (void)base;
#endif
}

// Scan [lo,hi) page-by-page (fast) for the UTF-16BE string "Press A to
// start", dumping the surrounding region on each hit.
inline void find_prompt(uint8_t* base, uint32_t lo, uint32_t hi) {
  // Distinctive substring "to start" = t o space s t a r t (UTF-16BE).
  const uint16_t pat[8] = {0x0074, 0x006F, 0x0020, 0x0073,
                           0x0074, 0x0061, 0x0072, 0x0074};
  int found = 0;
  int pages = 0;
  for (uint32_t page = lo; page < hi && found < 3; page += 0x1000) {
    if (!page_rw((uint64_t)base + page)) continue;
    ++pages;
    const uint8_t* p = base + page;
    for (uint32_t off = 0; off + 16 <= 0x1000 && found < 3; off += 2) {
      bool ok = true;
      for (int i = 0; i < 8; ++i) {
        const uint16_t ch = (uint16_t)((p[2 * i + off] << 8) | p[2 * i + off + 1]);
        if (ch != pat[i]) { ok = false; break; }
      }
      if (!ok) continue;
      const uint32_t a = page + off;
      // Only the real prompt has "Press " 28 bytes before "to start".
      const uint32_t strstart = a - 28;
      if (!page_rw((uint64_t)base + strstart)) continue;
      const uint16_t pre[6] = {0x0050, 0x0072, 0x0065, 0x0073, 0x0073, 0x0020};
      bool isprompt = true;
      for (int i = 0; i < 6; ++i) {
        const uint16_t ch = (uint16_t)((base[strstart + 2 * i] << 8) | base[strstart + 2 * i + 1]);
        if (ch != pre[i]) { isprompt = false; break; }
      }
      if (!isprompt) continue;
      ++found;
      hitcount().fetch_add(1);
      log_line("== PROMPT 'to start' at 0x%08X (strstart 0x%08X)\n", a, strstart);
      vbuf_dump_later(base);
      // Dump the font manager (0x83334A08) and the first glyph's data object to
      // locate the font's glyph table (char -> atlas UV).
      {
        auto dump16 = [&](const char* lbl, uint32_t addr) {
          if (addr < 0x20000000u || addr >= 0xE0000000u) return;
          if (!page_rw((uint64_t)base + addr)) return;
          char l[260] = {};
          int o = std::snprintf(l, sizeof(l), "  %s 0x%08X:", lbl, addr);
          for (int c = 0; c < 16; ++c)
            o += std::snprintf(l + o, sizeof(l) - o, " %08X", load_be(base, (uint32_t)(addr + c * 4)));
          log_line("%s\n", l);
        };
        dump16("fontmgr", 0x83334A08);
        dump16("fontmgr2", 0x83334AA0);
        // Follow the font manager's pointer fields to find the font table.
        for (int p = 0; p < 12; ++p) {
          uint32_t ptr = load_be(base, (uint32_t)(0x83334A08 + p * 4));
          char lbl[16] = {};
          std::snprintf(lbl, sizeof(lbl), "fm+%02X", p * 4);
          if (ptr >= 0x40000000u && ptr < 0x80000000u) dump16(lbl, ptr);
        }
        // Dump the glyph node table at 0x40102AF0+0x0C (23 nodes, 48B stride).
        if (page_rw((uint64_t)base + 0x40102AF0 + 0x0C)) {
          uint32_t tbl = load_be(base, (uint32_t)(0x40102AF0 + 0x0C));
          uint32_t cnt = load_be(base, (uint32_t)(0x40102AF0 + 0x14));
          if (cnt > 128) cnt = 128;
          log_line("  glyphtable base=0x%08X count=%u\n", tbl, (unsigned)cnt);
          for (uint32_t i = 0; i < cnt; ++i) {
            uint32_t n = (uint32_t)(tbl + i * 48);
            if (!page_rw((uint64_t)base + n)) break;
            char l[200] = {};
            int o = std::snprintf(l, sizeof(l), "    node[%2u] 0x%08X:", i, n);
            for (int c = 0; c < 12; ++c)
              o += std::snprintf(l + o, sizeof(l) - o, " %08X", load_be(base, (uint32_t)(n + c * 4)));
            log_line("%s\n", l);
          }
        }
        // First glyph obj's data object (0x404DDxxx).
        uint32_t g0 = load_be(base, (uint32_t)(strstart + 0x30));
        if (g0 >= 0x40000000u && g0 < 0x80000000u && page_rw((uint64_t)base + g0)) {
          dump16("gobj0", g0);
          dump16("gdata0", load_be(base, g0));
        }
      }
      // The glyph list starts ~0x30 after the string start (after the string
      // + NUL). Dump the first few glyph objects (the pointer targets).
      const uint32_t list = strstart + 0x30;
      if (page_rw((uint64_t)base + list)) {
        for (int g = 0; g < 6; ++g) {
          const uint32_t ptraddr = list + g * 4;
          if (!page_rw((uint64_t)base + ptraddr)) break;
          const uint32_t obj = load_be(base, ptraddr);
          if (obj == 0 || obj < 0x40000000u || obj > 0x80000000u) break;
          if (!page_rw((uint64_t)base + obj)) continue;
          const uint32_t gd = load_be(base, obj);  // +0x00 = glyph data ptr
          const uint32_t style = load_be(base, (uint32_t)(obj + 4));
          const uint32_t uvpos = load_be(base, (uint32_t)(obj + 8));
          char line[400] = {};
          int off2 = std::snprintf(line, sizeof(line),
                                    "  glyph[%d] obj=0x%08X data=0x%08X uv=0x%08X:",
                                    g, obj, gd, uvpos);
          log_line("%s\n", line);
          // Dump the glyph data (64 bytes) at the data ptr -- this holds the
          // font-atlas UV + metrics for the glyph.
          if (gd != 0 && gd >= 0x40000000u && gd < 0x80000000u &&
              page_rw((uint64_t)base + gd)) {
            char gdl[400] = {};
            int goff = std::snprintf(gdl, sizeof(gdl), "    data@0x%08X:", gd);
            for (int c = 0; c < 16; ++c)
              goff += std::snprintf(gdl + goff, sizeof(gdl) - goff, " %08X",
                                     load_be(base, (uint32_t)(gd + c * 4)));
            log_line("%s\n", gdl);
          }
          // Store the 'uv' (glyph0) for a later dump (vertex buffer is filled
          // during the render phase, after this layout-time probe).
          if (g == 0) g_uv0().store(uvpos);
          if (g == 5) g_uv5().store(uvpos);
          if (g == 0) g_data0().store(gd);
          if (g == 1) g_data1().store(gd);
          if (page_rw((uint64_t)base + uvpos)) {
            char vline[400] = {};
            int voff = std::snprintf(vline, sizeof(vline), "    vbuf@0x%08X:", uvpos);
            for (int c = 0; c < 12; ++c)
              voff += std::snprintf(vline + voff, sizeof(vline) - voff, " %08X",
                                     load_be(base, (uint32_t)(uvpos + c * 4)));
            log_line("%s\n", vline);
          }
        }
      }
      // Dump 512 bytes before the string start (the text element structure)
      // and 64 bytes after.
      for (int block = 0; block < 4; ++block) {
        char line[400] = {};
        const uint32_t start = strstart - 256 + block * 128;
        int off2 = std::snprintf(line, sizeof(line), "  0x%08X:", start);
        for (int c = 0; c < 32; ++c) {
          const uint32_t addr = start + c * 4;
          if (page_rw((uint64_t)base + addr))
            off2 += std::snprintf(line + off2, sizeof(line) - off2, " %08X", load_be(base, addr));
        }
        log_line("%s\n", line);
      }
    }
  }
  log_line("scan done\n");
}

// Scan [lo, hi) 4-byte aligned for glyph nodes: +0x12 in [0x20,0x7E] and
// +0x29 != 0, with +0x00 and +0x08 looking like heap pointers (or 0). Collect
// up to `maxhits` and dump the first one fully.
inline void scan(uint8_t* base, uint32_t lo, uint32_t hi, int maxhits) {
  int hits = 0;
  for (uint32_t a = lo; a + 0x30 <= hi && hits < maxhits; a += 4) {
    if (!wr((uint64_t)base + a, 0x30)) { a += 0x1000; continue; }
    const uint8_t* p = base + a;
    const uint32_t key = __builtin_bswap32(*(const uint32_t*)(p + 0x12));
    if (key < 0x20 || key > 0x7E) continue;
    const uint8_t flag = p[0x29];
    if (flag == 0) continue;
    // +0x00 / +0x08 must be heap ptrs or zero.
    const uint32_t prev = __builtin_bswap32(*(const uint32_t*)(p + 0));
    const uint32_t next = __builtin_bswap32(*(const uint32_t*)(p + 8));
    auto okp = [](uint32_t v) { return v == 0 || (v >= 0x40000000u && v < 0x80000000u); };
    if (!okp(prev) || !okp(next)) continue;
    ++hits;
    log_line("--- glyph node 0x%08X char='%c' (0x%02X) +00=0x%08X +08=0x%08X flag=%d\n",
             a, (char)key, key, prev, next, flag);
    char line[256] = {};
    int off = std::snprintf(line, sizeof(line), "  node:");
    for (int c = 0; c < 24; ++c)
      off += std::snprintf(line + off, sizeof(line) - off, " %08X", load_be(base, (uint32_t)(a + c * 4)));
    log_line("%s\n", line);
    if (next != 0 && wr((uint64_t)base + next, 0x30)) {
      char line2[256] = {};
      int off2 = std::snprintf(line2, sizeof(line2), "  next:");
      for (int c = 0; c < 24; ++c)
        off2 += std::snprintf(line2 + off2, sizeof(line2) - off2, " %08X", load_be(base, (uint32_t)(next + c * 4)));
      log_line("%s\n", line2);
    }
  }
  if (hits > 0) log_line("== scan found %d glyph nodes in [0x%08X,0x%08X]\n", hits, lo, hi);
}

// Called (throttled) from the glyph-probe hook. Dumps the font object region
// a few times, following its pointer fields to locate the glyph table.
inline void maybe_scan(uint8_t* base) {
  if (!enabled()) return;
  static std::atomic<int> done{0};
  if (done.load() >= 4) return;
  static std::atomic<int> frame{0};
  if (frame.fetch_add(1) % every() != 0) return;
  // Start a background thread (once) that scans the heap periodically.
  static std::atomic<bool> started{false};
  bool expect = false;
  if (started.compare_exchange_strong(expect, true)) {
#ifdef _WIN32
    CreateThread(nullptr, 0, [](LPVOID b) -> DWORD {
      auto* base = (uint8_t*)b;
      for (int i = 0; i < 400 && hitcount().load() < 6; ++i) {
        find_prompt(base, 0x40000000, 0x80000000);
        Sleep(1000);
      }
      return 0;
    }, base, 0, nullptr);
#else
    std::thread([base] {
      for (int i = 0; i < 400 && hitcount().load() < 6; ++i) {
        find_prompt(base, 0x40000000, 0x80000000);
        usleep(1000000);
      }
    }).detach();
#endif
  }
  (void)scan;
  (void)done;
}

}  // namespace fable2::fontprobe
