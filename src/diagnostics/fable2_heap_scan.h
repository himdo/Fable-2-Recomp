// fable2_heap_scan.h - find the runtime localized prompt string in the guest
// heap, and (optionally) rewrite it to append a canary.
//
// The "Press A to start" text is a localized string decoded at runtime from
// data/language/.../book.babel into the guest heap (not present in the image).
// The guest arena is the full 4GB at host base 0x100000000 (commit-on-fault).
//
// A per-frame hook (UIText_FrameRender) just sets a request flag. A BACKGROUND
// thread performs the (slow) committed-RW-region scan so the game's render
// thread is never blocked. The scan is SEH-protected so a concurrently-freed
// page can't crash the process.
//
//   FABLE2_HEAP_SCAN=1              enable
//   FABLE2_HEAP_SCAN_REWRITE=1      also append " DEADBEEF" to hits (in place)
//   FABLE2_HEAP_SCAN_TIMES=N        number of scans (default 2)
// Log: fable2_heap_scan.log in the CWD (exe dir).

#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include <rex/ppc/context.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "fable2_state_probe.h"  // per-frame UI state sampler (render thread)

namespace fable2::heapscan {

#ifdef _WIN32
// Scanning arbitrary guest pages requires VirtualQuery and SEH. Keep this
// optional Windows diagnostic out of other hosts; retain the forwarding hook.
inline constexpr uintptr_t kArenaHostBase = 0x100000000ull;  // see alloc_watch.h

// ---------------------------------------------------------------------------
// C-style SEH-safe region scanner (no C++ objects with non-trivial ctors/dtors
// in the __try scope, so clang/MSVC SEH is clean).
// ---------------------------------------------------------------------------
extern "C" {

static FILE* g_scan_log = nullptr;
static int g_rewrite = 0;
// Run caps (shared with the C++ scanner).
static int g_ascii_runs = 0;
static int g_u16_runs = 0;
static int g_max_ascii_runs = 150;
static int g_max_u16_runs = 150;

// Needles in the plausible encodings. Guest is big-endian, so wide strings
// are UTF-16BE (high byte first): "to" = [00 74 00 6F].
static const uint8_t kN_to_start_a[] = "to start";
static const uint8_t kN_press_a[] = "Press";
static const uint8_t kN_start_a[] = "start";
// UTF-16BE
static const uint8_t kN_to_start_be[] = {0x00,'t',0x00,'o',0x00,' ',0x00,'s',0x00,'t',0x00,'a',0x00,'r',0x00,'t'};
static const uint8_t kN_press_be[] = {0x00,'P',0x00,'r',0x00,'e',0x00,'s',0x00,'s'};
static const uint8_t kN_start_be[] = {0x00,'s',0x00,'t',0x00,'a',0x00,'r',0x00,'t'};
// UTF-16LE (in case)
static const uint8_t kN_to_start_le[] = {'t',0x00,'o',0x00,' ',0x00,'s',0x00,'t',0x00,'a',0x00,'r',0x00,'t',0x00};
static const uint8_t kN_start_le[] = {'s',0x00,'t',0x00,'a',0x00,'r',0x00,'t',0x00};

// Back-reference scan: FABLE2_HEAP_SCAN_PTR=0xA,0xB - find every RW word
// that POINTS AT one of these guest addresses (big-endian). Logs the owner
// location + 8 words of context so the owning object can be identified.
static const uint32_t* g_ptr_targets = nullptr;
static int g_ptr_target_count = 0;
static int g_ptr_hits = 0;
static const int kMaxPtrHits = 200;

static void ptr_backref_search(uint8_t* hp, size_t len, uint32_t guest_lo) {
  if (g_ptr_target_count == 0 || len < 4) return;
  for (size_t i = 0; i + 4 <= len && g_ptr_hits < kMaxPtrHits; i += 4) {
    const uint32_t v =
        (uint32_t)((hp[i] << 24) | (hp[i + 1] << 16) | (hp[i + 2] << 8) | hp[i + 3]);
    for (int t = 0; t < g_ptr_target_count; ++t) {
      if (v != g_ptr_targets[t]) continue;
      uint32_t ga = guest_lo + (uint32_t)i;
      char buf[280];
      int bsz = 0;
      bsz += std::snprintf(buf + bsz, sizeof(buf) - bsz,
                           "  PTRREF 0x%08X -> 0x%08X | ", ga, v);
      for (int k = 0; k < 8 && i + (size_t)k * 4 + 4 <= len; ++k) {
        bsz += std::snprintf(buf + bsz, sizeof(buf) - bsz, "%02X%02X%02X%02X ",
                             hp[i + k * 4], hp[i + k * 4 + 1], hp[i + k * 4 + 2],
                             hp[i + k * 4 + 3]);
      }
      bsz += std::snprintf(buf + bsz, sizeof(buf) - bsz, "\n");
      std::fputs(buf, g_scan_log);
      ++g_ptr_hits;
    }
  }
  if (g_scan_log) std::fflush(g_scan_log);
}

// Targeted needle search (all encodings) across a region.
static void needle_search(uint8_t* hp, size_t len, uint32_t guest_lo) {
  ptr_backref_search(hp, len, guest_lo);
  struct N { const char* label; const uint8_t* pat; int plen; int cap; int wide; };
  static const N ns[] = {
      {"to start(a)", kN_to_start_a, 8, 32, 0}, {"Press(a)", kN_press_a, 5, 40, 0},
      {"start(a)", kN_start_a, 5, 60, 0},
      {"to start(16BE)", kN_to_start_be, 16, 32, 1},
      {"Press(16BE)", kN_press_be, 10, 40, 1},
      {"start(16BE)", kN_start_be, 10, 60, 1},
      {"to start(16LE)", kN_to_start_le, 14, 32, 1},
      {"start(16LE)", kN_start_le, 10, 60, 1},
  };
  for (int ni = 0; ni < 8; ++ni) {
    const N& n = ns[ni];
    if (len < (size_t)n.plen) continue;
    int cap = n.cap;
    uint8_t* p = hp;
    const size_t last_start = len - n.plen;
    const uint8_t fb = n.pat[0];
    while (p <= hp + last_start && cap > 0) {
      uint8_t* q = static_cast<uint8_t*>(
          std::memchr(p, fb, (hp + last_start) - p + 1));
      if (!q) break;
      p = q;
      if (std::memcmp(p, n.pat, n.plen) == 0) {
        uint32_t ga = guest_lo + static_cast<uint32_t>(p - hp);
        size_t rel = static_cast<size_t>(p - hp);
        char buf[280];
        int bsz = 0;
        bsz += std::snprintf(buf + bsz, sizeof(buf) - bsz, "  NEEDLE %-12s guest=0x%08X | ", n.label, ga);
        // hex-dump 8 bytes before + up to 80 bytes after the match
        size_t before = rel > 8 ? 8 : rel;
        for (size_t k = before; k-- < 8;) {
          uint8_t c = p[static_cast<int>(before) - 1 - k];
          bsz += std::snprintf(buf + bsz, sizeof(buf) - bsz, "%02X ", c);
        }
        bsz += std::snprintf(buf + bsz, sizeof(buf) - bsz, "[");
        for (size_t k = 0; k < 80 && rel + k < len; ++k) {
          uint8_t c = p[k];
          bsz += std::snprintf(buf + bsz, sizeof(buf) - bsz, "%02X", c);
          if (k < 79 && rel + k + 1 < len) bsz += std::snprintf(buf + bsz, sizeof(buf) - bsz, " ");
        }
        bsz += std::snprintf(buf + bsz, sizeof(buf) - bsz, "]\n");
        std::fputs(buf, g_scan_log);
        // Feed the live-string watch: the input probe now tracks these buffers
        // automatically (they move per run, so hardcoding is not possible).
        if (n.wide) {
          fable2::uip::add_runtime_addr(ga);       // match start
          fable2::uip::add_runtime_addr(ga - 0x20); // likely record base
        }
        --cap;
      }
      ++p;
    }
  }
  if (g_scan_log) std::fflush(g_scan_log);
}

__declspec(noinline) void seh_dump_runs(uint8_t* hp, size_t len,
                                        uint32_t guest_lo) {
  __try {
    needle_search(hp, len, guest_lo);
    // --- ASCII runs (length >= 7, must contain a space or be >= 12) ---
    size_t i = 0;
    while (i < len && g_ascii_runs < g_max_ascii_runs) {
      if (hp[i] >= 0x20 && hp[i] < 0x7F) {
        size_t j = i;
        while (j < len && hp[j] >= 0x20 && hp[j] < 0x7F) ++j;
        size_t runlen = j - i;
        if (runlen >= 7) {
          int has_space = 0;
          for (size_t k = i; k < j; ++k)
            if (hp[k] == ' ') { has_space = 1; break; }
          if (has_space || runlen >= 12) {
            uint32_t ga = guest_lo + static_cast<uint32_t>(i);
            char buf[256];
            int bsz = 0;
            bsz += std::snprintf(buf + bsz, sizeof(buf) - bsz, "  ASCII 0x%08X len=%zu: ", ga, runlen);
            for (size_t k = i; k < j && k < i + 96; ++k)
              bsz += std::snprintf(buf + bsz, sizeof(buf) - bsz, "%c", (char)hp[k]);
            bsz += std::snprintf(buf + bsz, sizeof(buf) - bsz, "\n");
            std::fputs(buf, g_scan_log);
            ++g_ascii_runs;
            if (g_ascii_runs % 20 == 0) std::fflush(g_scan_log);
          }
        }
        i = j;
      } else {
        ++i;
      }
    }
    // --- UTF-16LE runs (>= 5 chars, low byte printable, high byte 0) ---
    i = 0;
    while (i + 1 < len && g_u16_runs < g_max_u16_runs) {
      if ((hp[i] >= 0x20 && hp[i] < 0x7F) && hp[i + 1] == 0) {
        size_t j = i;
        while (j + 1 < len && hp[j] >= 0x20 && hp[j] < 0x7F && hp[j + 1] == 0) j += 2;
        size_t runlen = (j - i) / 2;
        if (runlen >= 5) {
          uint32_t ga = guest_lo + static_cast<uint32_t>(i);
          char buf[256];
          int bsz = 0;
          bsz += std::snprintf(buf + bsz, sizeof(buf) - bsz, "  U16LE 0x%08X len=%zu: ", ga, runlen);
          for (size_t k = i; k < j && k < i + 120; k += 2)
            bsz += std::snprintf(buf + bsz, sizeof(buf) - bsz, "%c", (char)hp[k]);
          bsz += std::snprintf(buf + bsz, sizeof(buf) - bsz, "\n");
          std::fputs(buf, g_scan_log);
          ++g_u16_runs;
          if (g_u16_runs % 20 == 0) std::fflush(g_scan_log);
        }
        i = j;
      } else {
        ++i;
      }
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    std::fputs("  (region faulted, skipped)\n", g_scan_log);
    if (g_scan_log) std::fflush(g_scan_log);
  }
}

}  // extern "C"

// ---------------------------------------------------------------------------
// Background scanner.
// ---------------------------------------------------------------------------
inline bool& armed() {
  static bool a = [] {
    const char* v = std::getenv("FABLE2_HEAP_SCAN");
    return v && v[0] == '1';
  }();
  return a;
}
inline int env_int(const char* n, int d) {
  const char* v = std::getenv(n);
  return v ? std::atoi(v) : d;
}
inline FILE*& logf() {
  static FILE* f = std::fopen("fable2_heap_scan.log", "w");
  return f;
}

inline void scan_all() {
  FILE* f = logf();
  if (!f) return;
  g_scan_log = f;
  g_ascii_runs = 0;
  g_u16_runs = 0;
  g_ptr_hits = 0;
  // (Re)parse the back-reference targets once.
  static uint32_t pt[16];
  static bool pt_init = false;
  if (!pt_init) {
    pt_init = true;
    const char* v = std::getenv("FABLE2_HEAP_SCAN_PTR");
    if (v) {
      std::string s(v);
      size_t start = 0;
      for (size_t i = 0; i <= s.size() && g_ptr_target_count < 16; ++i) {
        if (i == s.size() || s[i] == ',') {
          std::string tok = s.substr(start, i - start);
          if (!tok.empty()) pt[g_ptr_target_count++] = (uint32_t)std::strtoul(tok.c_str(), nullptr, 0);
          start = i + 1;
        }
      }
      g_ptr_targets = pt;
    }
  }
  char hdr[128];
  std::snprintf(hdr, sizeof(hdr), "\n==== heap scan (start) ====\n");
  std::fputs(hdr, f);
  std::fflush(f);

  uint8_t* region = reinterpret_cast<uint8_t*>(kArenaHostBase);
  const size_t total = 0x100000000ull;  // 4GB arena
  size_t off = 0;
  int regions = 0;
  size_t scanned_bytes = 0;
  const size_t kMaxScanBytes = 0x100000000ull;  // full 4GB arena (covers image + above)
  bool capped = false;
  while (off < total) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (::VirtualQuery(region + off, &mbi, sizeof(mbi)) == 0) break;
    size_t region_size = mbi.RegionSize ? mbi.RegionSize : 0x1000;
    uint32_t guest_lo = static_cast<uint32_t>(off);
    // Any committed read/write non-guard region could hold runtime-loaded text
    // (heap is RW; the image's read-only sections are excluded by the RW check).
    bool candidate =
        mbi.State == MEM_COMMIT &&
        (mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY)) != 0 &&
        (mbi.Protect & PAGE_GUARD) == 0;
    if (candidate) {
      if (scanned_bytes + region_size > kMaxScanBytes) {
        capped = true;
        size_t rem = kMaxScanBytes - scanned_bytes;
        if (rem > 0 && region_size > rem) {
          seh_dump_runs(region + off, rem, guest_lo);
          scanned_bytes = kMaxScanBytes;
        }
      } else {
        ++regions;
        if (regions % 8 == 1) {
          char pb[128];
          std::snprintf(pb, sizeof(pb), "  ...region #%d guest=0x%08X size=%llu\n",
                        regions, guest_lo,
                        static_cast<unsigned long long>(region_size));
          std::fputs(pb, f);
          std::fflush(f);
        }
        seh_dump_runs(region + off, region_size, guest_lo);
        scanned_bytes += region_size;
      }
    }
    off += region_size;
    if (capped) break;
    if (mbi.BaseAddress == reinterpret_cast<uint8_t*>(kArenaHostBase + total - 1))
      break;
  }
  char sum[128];
  std::snprintf(sum, sizeof(sum), "==== heap scan (end) regions=%d bytes=%llu%s ====\n",
                regions, static_cast<unsigned long long>(scanned_bytes),
                capped ? " (CAPPED)" : "");
  std::fputs(sum, f);
  std::fflush(f);
}

// Simple flag the render thread sets; a single scanner thread consumes it.
inline std::atomic<int>& scan_request() {
  static std::atomic<int> r{0};
  return r;
}
inline void start_scanner_once() {
  static std::atomic<bool> started{false};
  bool want = false;
  if (!started.compare_exchange_strong(want, true)) return;
  const int max_scans = env_int("FABLE2_HEAP_SCAN_TIMES", 6);
  std::thread([max_scans] {
    int done = 0;
    while (done < max_scans) {
      // Wait until the render thread asks for a scan.
      while (scan_request().load() == 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      scan_request().store(0);
      scan_all();
      ++done;
    }
  }).detach();
}

inline double now_s() {
  return std::chrono::duration<double>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

inline void maybe_request() {
  if (!armed()) return;
  start_scanner_once();
  static const double t0 = now_s();
  static std::atomic<int> frame{0};
  if (frame.fetch_add(1) % 30 != 0) return;  // ~once / 0.5s
  double el = now_s() - t0;
  // Ask for scans continuously after load (covers loading -> title ->
  // menu; FABLE2_HEAP_SCAN_TIMES caps the total number of full scans).
  if (el > 5.0) scan_request().store(1);
}

#else
inline void maybe_request() {}
#endif

}  // namespace fable2::heapscan

extern "C" void UIText_FrameRender(PPCContext& ctx, uint8_t* base) {
  fable2::heapscan::maybe_request();
  if (fable2::uir::hook("UIText_FrameRender", ctx, base)) return;
  __imp__UIText_FrameRender(ctx, base);
}
