// fable2_deadbeef.h - "DEADBEEF" UI-text canary.
//
// Appends " DEADBEEF" (UTF-16BE) to every UI text string so the on-screen
// prompt reads, e.g., "Press <a_img> to start DEADBEEF".
//
// Why it works: Fable 2's UI text items are reconstructed ~84x/s (per frame)
// from their *source* UTF-16BE strings, then laid out into glyph nodes and
// drawn. So mutating the source string in place is picked up on the next
// frame. We hook the per-item dispatch (UITextItem_Dispatch, r3 = item),
// walk the item's referenced memory, and append the suffix to any UTF-16BE
// string we find (guarded so it is applied once per string).
//
//   FABLE2_DEADBEEF=1      enable the canary
//   FABLE2_DEADBEEF_LOG=1  log appends to fable2_deadbeef.log (exe dir)
//   FABLE2_DEADBEEF_EVERY=1  also try the known title-prompt heap addresses
//
// Guest arena (big-endian): host = base + guest_addr (base = 0x100000000).
#pragma once

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <thread>

#include <rex/ppc/context.h>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fable2::deadbeef {

// ---- self-contained guest-memory helpers (big-endian arena) --------------
inline void gread(const uint8_t* base, uint32_t addr, void* dst, size_t n) {
  const uint64_t off = (addr >= 0xE0000000u) ? (addr + 0x1000u) : addr;
  std::memcpy(dst, base + off, n);
}

inline void gwrite(const uint8_t* base, uint32_t addr, const void* src, size_t n) {
  const uint64_t off = (addr >= 0xE0000000u) ? (addr + 0x1000u) : addr;
  std::memcpy(const_cast<uint8_t*>(base + off), src, n);
}

inline bool in_image(uint32_t a) {
  // Image 0x82000000 + 0x1620000 (see fable_2_pch.h REX_IMAGE_SIZE).
  return a >= 0x82000000u && a < 0x82000000u + 0x1620000u;
}

inline bool valid_guest_addr(uint32_t a) {
  return ((a >= 0x1000u && a < 0x82000000u) ||
          (a >= 0x82000000u && a < 0x84000000u)) &&
         (a & 3) == 0;
}

inline bool is_heap_ptr(uint32_t a) {
  return valid_guest_addr(a) && !in_image(a);
}

inline uint64_t host_addr(const uint8_t* base, uint32_t a) {
  const uint64_t off = (a >= 0xE0000000u) ? (a + 0x1000u) : a;
  return reinterpret_cast<uintptr_t>(base + off);
}

// True if [h, h+len) is committed + writable host memory.
inline bool host_writable(uint64_t h, size_t len) {
#ifdef _WIN32
  MEMORY_BASIC_INFORMATION mbi = {};
  if (!::VirtualQuery(reinterpret_cast<LPCVOID>(h), &mbi, sizeof(mbi)))
    return false;
  if (mbi.State != MEM_COMMIT) return false;
  if ((mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY)) == 0) return false;
  const uint64_t start = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
  if (h < start || h + len > start + mbi.RegionSize) return false;
  return true;
#else
  (void)h;
  (void)len;
  return false;
#endif
}

// Big-endian 32-bit guest read. Returns 0 if the page is not committed
// (the arena is commit-on-fault, so an uncommitted read would AV).
inline uint32_t rd32(const uint8_t* base, uint32_t a) {
  if (!host_writable(host_addr(base, a), 4)) return 0;
  uint32_t v = 0;
  gread(base, a, &v, 4);
  return __builtin_bswap32(v);
}

// ---- env / logging --------------------------------------------------------
inline bool enabled() {
#ifdef _WIN32
  char v[8] = {};
  size_t n = 0;
  return ::getenv_s(&n, v, sizeof(v), "FABLE2_DEADBEEF") == 0 && v[0] == '1';
#else
  const char* v = std::getenv("FABLE2_DEADBEEF");
  return v != nullptr && v[0] == '1';
#endif
}

inline bool logging() {
#ifdef _WIN32
  char v[8] = {};
  size_t n = 0;
  return ::getenv_s(&n, v, sizeof(v), "FABLE2_DEADBEEF_LOG") == 0 &&
         v[0] == '1';
#else
  const char* v = std::getenv("FABLE2_DEADBEEF_LOG");
  return v != nullptr && v[0] == '1';
#endif
}

inline bool try_known() {
#ifdef _WIN32
  char v[8] = {};
  size_t n = 0;
  return ::getenv_s(&n, v, sizeof(v), "FABLE2_DEADBEEF_EVERY") == 0 &&
         v[0] == '1';
#else
  const char* v = std::getenv("FABLE2_DEADBEEF_EVERY");
  return v != nullptr && v[0] == '1';
#endif
}

inline FILE*& logf() {
  static FILE* f = [] {
#ifdef _WIN32
    FILE* out = nullptr;
    if (::fopen_s(&out, "fable2_deadbeef.log", "w") != 0) out = nullptr;
    return out;
#else
    return std::fopen("fable2_deadbeef.log", "w");
#endif
  }();
  return f;
}

inline void log_line(const char* fmt, ...) {
  FILE* f = logf();
  if (!f) return;
  va_list ap;
  va_start(ap, fmt);
  std::vfprintf(f, fmt, ap);
  va_end(ap);
  std::fflush(f);
}

inline void log_init() {
  static bool done = false;
  if (done) return;
  done = true;
  log_line("== fable2_deadbeef canary enabled ==\n");
}

inline void log_append(uint32_t a) { log_line("append @0x%08X\n", a); }

// ---- UTF-16BE string helpers ---------------------------------------------
// Byte length of the UTF-16BE string at a (NUL-terminated, capped).
inline size_t utf16_strlen(const uint8_t* base, uint32_t a, size_t cap) {
  if (cap > 512) cap = 512;
  if (!host_writable(host_addr(base, a), cap)) return 0;
  uint8_t b[512];
  gread(base, a, b, cap);
  size_t i = 0;
  while (i + 1 < cap) {
    if (b[i] == 0 && b[i + 1] == 0) break;
    i += 2;
  }
  return i;
}

// True if a points to a UTF-16BE string of >= min_chars printable ASCII.
inline bool is_text(const uint8_t* base, uint32_t a, size_t min_chars) {
  size_t cap = min_chars * 2 + 4;
  if (cap > 128) cap = 128;
  if (!host_writable(host_addr(base, a), cap)) return false;
  uint8_t b[128];
  gread(base, a, b, cap);
  int chars = 0;
  for (size_t i = 0; i + 1 < cap; i += 2) {
    uint16_t c = static_cast<uint16_t>((b[i] << 8) | b[i + 1]);
    if (c == 0) break;
    if (c < 0x20 || c > 0x7E) return false;
    if (++chars >= static_cast<int>(min_chars)) break;
  }
  return chars >= static_cast<int>(min_chars);
}

// " DEADBEEF" + NUL, as UTF-16BE bytes.
static const uint8_t kSuffix[] = {
    0x00, 0x20,  // ' '
    0x00, 0x44,  // 'D'
    0x00, 0x45,  // 'E'
    0x00, 0x41,  // 'A'
    0x00, 0x44,  // 'D'
    0x00, 0x42,  // 'B'
    0x00, 0x45,  // 'E'
    0x00, 0x45,  // 'E'
    0x00, 0x46,  // 'F'
    0x00, 0x00,  // NUL
};

inline bool append(const uint8_t* base, uint32_t a);

// ---- background scan for the live UTF-16BE prompt ------------------------
// The title prompt ("Press <a_img> to start") is a live UTF-16BE string in the
// heap, laid out into glyph nodes. It is not referenced within a couple of
// pointer hops of the per-item UI objects, so we scan a heap window for it on a
// background thread and append the suffix in place.
namespace scan {
std::atomic<const uint8_t*> g_base{nullptr};
std::atomic<int64_t> g_last_active_us{0};
std::atomic<bool> g_started{false};

// "to start" as UTF-16BE.
const uint8_t kNeedle[] = {0x00, 0x74, 0x00, 0x6F, 0x00, 0x20, 0x00, 0x73,
                           0x00, 0x74, 0x00, 0x61, 0x00, 0x72, 0x00, 0x74};
const size_t kNeedleLen = sizeof(kNeedle);

// Write a big-endian 32-bit value at a guest address.
void write_be32(const uint8_t* base, uint32_t addr, uint32_t v) {
  const uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16),
                        (uint8_t)(v >> 8), (uint8_t)v};
  gwrite(base, addr, b, 4);
}

// Build "<original prompt> DEADBEEF" in a fresh buffer and point every heap
// pointer that referenced the original string at it. Keeps the original text
// (incl. the <a_img> keycap and "to start") intact and appends the canary.
bool redirect_prompt(const uint8_t* base, uint32_t S) {
  static const uint32_t kNewBuf = 0x4F000000u;  // free guest-arena slot
  uint8_t orig[128] = {};
  gread(base, S, orig, sizeof(orig));
  const size_t olen = utf16_strlen(base, S, 128);
  if (olen < 8 || olen > 100) return false;
  // Guard: skip if the string already contains " DEADBEEF" (no double-up).
  {
    static const uint8_t kDB[] = {0x00, 0x20, 0x00, 0x44, 0x00, 0x45, 0x00, 0x41,
                                  0x00, 0x44, 0x00, 0x42, 0x00, 0x45, 0x00, 0x45,
                                  0x00, 0x46};
    for (size_t i = 0; i + sizeof(kDB) <= olen; i += 2)
      if (std::memcmp(orig + i, kDB, sizeof(kDB)) == 0) return false;
  }
  log_line("redirect: start S=0x%08X olen=%zu\n", S, olen);
  static const uint8_t kSuf[] = {0x00, 0x20, 0x00, 0x44, 0x00, 0x45, 0x00, 0x41,
                                 0x00, 0x44, 0x00, 0x42, 0x00, 0x45, 0x00, 0x45,
                                 0x00, 0x46, 0x00, 0x00};
  const size_t total = olen + sizeof(kSuf);
  if (total > 160) return false;
  uint8_t newstr[160] = {};
  std::memcpy(newstr, orig, olen);
  std::memcpy(newstr + olen, kSuf, sizeof(kSuf));
  {
    // The guest arena is commit-on-fault; commit our slot before writing.
    const uint64_t hb = host_addr(base, kNewBuf);
    if (!host_writable(hb, total)) {
#ifdef _WIN32
      void* p = VirtualAlloc((void*)hb, 256, MEM_COMMIT, PAGE_READWRITE);
      if (!p)
        p = VirtualAlloc((void*)hb, 256, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
      if (!p || !host_writable(hb, total)) return false;
#else
      // This optional canary needs Windows guest-page commitment support.
      return false;
#endif
    }
  }
  gwrite(base, kNewBuf, newstr, total);
  log_line("redirect: wrote newbuf, scanning for pointers to S=0x%08X\n", S);
  int redirects = 0;
  const uint32_t plo = 0x40000000u, phi = 0x43000000u;
  const size_t chunk = 8192;
  uint8_t buf[chunk];
  for (uint32_t a = plo; a + chunk < phi; a += chunk) {
    if (!host_writable(host_addr(base, a), chunk)) continue;
    gread(base, a, buf, chunk);
    for (size_t i = 0; i + 4 <= chunk; i += 4) {
      const uint32_t v = static_cast<uint32_t>(
          (buf[i] << 24) | (buf[i + 1] << 16) | (buf[i + 2] << 8) | buf[i + 3]);
      if (v == S && host_writable(host_addr(base, a + (uint32_t)i), 4)) {
        write_be32(base, a + (uint32_t)i, kNewBuf);
        if (redirects < 8)
          log_line("redirect: 0x%08X: 0x%08X -> 0x%08X\n",
                   a + (uint32_t)i, S, kNewBuf);
        ++redirects;
      }
    }
  }
  log_line("redirect: newbuf=0x%08X olen=%zu redirects=%d\n", kNewBuf, olen,
           redirects);
  return redirects > 0;
}

void loop() {
  const uint32_t lo = 0x40000000u, hi = 0x50000000u;
  const size_t chunk = 8192;
  uint8_t buf[chunk];
  for (;;) {
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    const uint8_t* base = g_base.load(std::memory_order_acquire);
    if (!base) continue;
    const int64_t now = std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
    if (now - g_last_active_us.load(std::memory_order_acquire) > 2000000)
      continue;  // title UI not active recently; skip the scan

    for (uint32_t a = lo; a + kNeedleLen < hi; a += chunk) {
      if (!host_writable(host_addr(base, a), chunk)) continue;
      gread(base, a, buf, chunk);
      for (size_t i = 0; i + kNeedleLen <= chunk; ++i) {
        if (std::memcmp(buf + i, kNeedle, kNeedleLen) != 0) continue;
        const uint32_t hit = a + static_cast<uint32_t>(i);
        // Back up to the start of the UTF-16BE string (previous NUL).
        uint32_t start = hit;
        while (start > a && start >= 4) {
          uint8_t b2[2];
          gread(base, start - 2, b2, 2);
          if (b2[0] == 0 && b2[1] == 0) break;
          start -= 2;
        }
        if (start + 4 <= hit) {
          // Only append to prompt-like strings ("Press ... to start"). Other
          // "to start" hits sit in tight buffers (float arrays, struct fields)
          // that crash when clobbered.
          uint8_t pre[10] = {};
          gread(base, start, pre, sizeof(pre));
          const uint8_t kPress[] = {0x00, 0x50, 0x00, 0x72, 0x00, 0x65,
                                    0x00, 0x73, 0x00, 0x73};
          const bool is_prompt =
              std::memcmp(pre, kPress, sizeof(kPress)) == 0;
          // Log the buffer layout of the first few hits (decoded string + the
          // bytes after the NUL) so the append target can be verified.
          static int logged_hits = 0;
          if (logged_hits < 40) {
            char dec[200] = {};
            int dl = 0;
            for (int c = 0; c < 60; ++c) {
              uint8_t b2[2] = {};
              gread(base, start + c * 2, b2, 2);
              if (b2[0] == 0 && b2[1] == 0) break;
              if (b2[1] < 0x20 || b2[1] >= 0x7F) break;
              dec[dl++] = (char)b2[1];
            }
            dec[dl] = 0;
            const uint32_t slen = (uint32_t)utf16_strlen(base, start, 512);
            char hex[160] = {};
            int ho = 0;
            for (int b = 0; b < 40; ++b) {
              uint8_t byte = 0;
              gread(base, start + slen + b, &byte, 1);
              char tmp[8];
              std::snprintf(tmp, sizeof(tmp), " %02X", byte);
              if (ho + (int)std::strlen(tmp) < (int)sizeof(hex) - 1) {
                std::strcat(hex, tmp);
                ho += (int)std::strlen(tmp);
              }
            }
            log_line("hit start=0x%08X len=%u \"%s\" after-NUL:%s\n", start,
                     (unsigned)slen, dec, hex);
            logged_hits++;
          }
          if (is_prompt && append(base, start))
            log_line("scan append @0x%08X\n", start);
        }
        i = chunk - kNeedleLen;  // skip past this match
      }
    }
  }
}

inline void start(const uint8_t* base) {
  if (g_started.exchange(true)) return;
  g_base.store(base, std::memory_order_release);
  std::thread(loop).detach();
}
}  // namespace scan

// Append " DEADBEEF" to the UTF-16BE string at a (after its NUL), keeping the
// original text intact. Returns true if it wrote.
inline bool append(const uint8_t* base, uint32_t a) {
  // " DEADBEEF" + NUL, UTF-16BE.
  static const uint8_t kSuf[] = {0x00, 0x20, 0x00, 0x44, 0x00, 0x45, 0x00, 0x41,
                                 0x00, 0x44, 0x00, 0x42, 0x00, 0x45, 0x00, 0x45,
                                 0x00, 0x46, 0x00, 0x00};
  const size_t len = utf16_strlen(base, a, 512);
  if (len < 4 || len > 120) return false;
  const uint64_t host = host_addr(base, a);
  if (!host_writable(host + len, sizeof(kSuf))) return false;
  if (!is_text(base, a, 4)) return false;
  // Guard: skip if the string already contains " DEADBEEF" (no double-up).
  {
    static const uint8_t kDB[] = {0x00, 0x20, 0x00, 0x44, 0x00, 0x45, 0x00, 0x41,
                                  0x00, 0x44, 0x00, 0x42, 0x00, 0x45, 0x00, 0x45,
                                  0x00, 0x46};
    const uint8_t* s = reinterpret_cast<const uint8_t*>(host);
    for (size_t i = 0; i + sizeof(kDB) <= len; i += 2)
      if (std::memcmp(s + i, kDB, sizeof(kDB)) == 0) return false;
  }
  // Safety: the 24 bytes we write (from the NUL on) must not contain a live
  // heap pointer. Float/zero slack is fine to overwrite; a pointer is not.
  {
    uint8_t room[24] = {};
    gread(base, a + len, room, sizeof(room));
    for (size_t i = 0; i + 4 <= sizeof(room); i += 4) {
      const uint32_t w = static_cast<uint32_t>(
          (room[i] << 24) | (room[i + 1] << 16) | (room[i + 2] << 8) |
          room[i + 3]);
      if (w >= 0x40500000u && w <= 0x43000000u) return false;
    }
  }
  gwrite(base, a + len, kSuf, sizeof(kSuf));
  log_line("append 0x%08X: appended DEADBEEF (len=%zu)\n", a, len);
  return true;
}

// ---- main entry -----------------------------------------------------------
// Walk `item` (and one/two levels of referenced heap pointers, plus the
// "current text" object) and append the suffix to every UTF-16BE string found.
inline void process(const uint8_t* base, PPCContext& ctx) {
  // Throttle: run at most ~7x/sec (source strings persist, so this is enough).
  static int64_t last_us = 0;
  const int64_t now_us =
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count();
  if (now_us - last_us < 150000) return;
  last_us = now_us;

  const uint32_t item = ctx.r3.u32;
  const uint32_t cur = rd32(base, 0x83334D2C);  // current text object ptr
  log_init();
  // Kick off / keep the background prompt scan alive while the title is up.
  scan::g_last_active_us.store(now_us, std::memory_order_release);
  scan::start(base);

  // Diagnostic tick (~2/sec): dump the item + current-text objects so the
  // string-storage layout is visible in the log.
  static int64_t last_diag_us = 0;
  if (now_us - last_diag_us >= 500000) {
    last_diag_us = now_us;
    log_line("tick lr=0x%08X item=0x%08X cur=0x%08X\n",
             static_cast<uint32_t>(ctx.lr), item, cur);
    auto dump_words = [&](const char* label, uint32_t p) {
      if (!is_heap_ptr(p) && !in_image(p)) return;
      char l[600];
      int o = std::snprintf(l, sizeof(l), "   %s 0x%08X:", label, p);
      for (int i = 0; i < 24; ++i) {
        const uint32_t w = rd32(base, p + i * 4);
        char tmp[16];
        std::snprintf(tmp, sizeof(tmp), " %08X", w);
        if (o + static_cast<int>(std::strlen(tmp)) <
            static_cast<int>(sizeof(l)) - 1) {
          std::strcat(l, tmp);
          o += static_cast<int>(std::strlen(tmp));
        }
      }
      log_line("%s\n", l);
    };
    dump_words("item", item);
    dump_words("cur", cur);
    // Follow cur's heap pointers one level deeper (segment/glyph nodes and
    // any text storage) so the layout is visible.
    for (int i = 0; i < 16; ++i) {
      const uint32_t w = rd32(base, cur + i * 4);
      if (is_heap_ptr(w)) dump_words("cur+", w);
    }
  }

  static std::set<uint32_t> done;  // already-appended strings (avoids rework)
  if (done.size() > 8000) done.clear();

  auto try_append = [&](uint32_t a) {
    if (!is_heap_ptr(a)) return;
    if (done.count(a)) return;
    if (!is_text(base, a, 4)) return;
    if (append(base, a)) {
      done.insert(a);
      log_append(a);
    }
  };

  // Candidate containers: the item itself + the "current text" object.
  uint32_t cont[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  int nc = 0;
  if (is_heap_ptr(item)) cont[nc++] = item;
  if (is_heap_ptr(cur)) cont[nc++] = cur;
  // Optional known title-prompt heap addresses (may be stale across launches).
  if (try_known()) {
    const uint32_t known[] = {0x426690F0u, 0x42668C2Cu, 0x42668DACu,
                              0x42668E0Cu, 0x4266910Cu, 0x4266922Cu};
    for (uint32_t k : known)
      if (is_heap_ptr(k) && nc < 8) cont[nc++] = k;
  }

  for (int ci = 0; ci < nc; ++ci) {
    const uint32_t c = cont[ci];
    try_append(c);  // the container itself might be a string
    // Scan its first words for string pointers (2 levels, capped).
    for (int i = 0; i < 48; ++i) {
      const uint32_t w = rd32(base, c + i * 4);
      if (!is_heap_ptr(w)) continue;
      try_append(w);
      if (i < 16) {
        for (int j = 0; j < 16; ++j) {
          const uint32_t w2 = rd32(base, w + j * 4);
          if (is_heap_ptr(w2)) try_append(w2);
        }
      }
    }
  }
}

}  // namespace fable2::deadbeef
