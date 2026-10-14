// fable2_modmenu.h - Replace the main-menu "Downloadable Content" item with a
// working "MOD MENU" item.
//
// The stock GOTY main menu has 5 rows (New Game, Continue, Downloadable
// Content, Language, Subtitles) and clamps its selection to indices 0..4.
// "Downloadable Content" is a dead button in this build, so we repurpose that
// row (index 2) as the mod item:
//
//   * While the main menu is open we rewrite the row's live source string
//     "Downloadable Content" -> "MOD MENU" in place (UI text items are rebuilt
//     from their source every frame, so the change is picked up next frame -
//     same trick as fable2_deadbeef.h). It is therefore always visible as the
//     3rd item, no scrolling required.
//   * We keep a mirror of the selection index (0..4) driven by the raw press
//     dispatcher (InputEvent_DispatchButtonPress). Pressing A while the mirror is
//     on index 2 runs fable2::modmenu::Activate() and relabels the row
//     "LOADED" to confirm it fired.
//   * Leaving the main menu restores "Downloadable Content".
//
// The other rows (New Game, Continue, Language, Subtitles) are untouched and
// keep working.
//
//   FABLE2_MODMENU=1      enable the feature
//   FABLE2_MODMENU_LOG=1  log to fable2_modmenu.log (exe dir)
//
// Button bits measured live from InputEvent_DispatchButtonPress r5 (the
// pressed-button mask): Up=1, Down=2, Back=32, A=4096. Each physical press
// fires the dispatcher ~twice (135..270 ms apart); we time-dedup with a 400 ms
// gate so one press == one action.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <mutex>
#include <thread>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif
#include <rex/ppc/context.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "fable2_state_probe.h"  // CurrentState() / kMainMenu gating

namespace fable2::modmenu {

// ---- button bits + selection model ---------------------------------------
constexpr uint32_t kBtnUp = 1;      // bit 0
constexpr uint32_t kBtnDown = 2;    // bit 1
constexpr uint32_t kBtnBack = 32;   // bit 5
constexpr uint32_t kBtnA = 4096;    // bit 12
constexpr int kStockMax = 4;        // stock menu clamps to 0..4
constexpr int kModIndex = 2;        // "Downloadable Content" row -> mod item
// One action per physical press. The window must be large enough to swallow
// the SECOND raw-press callback of a physical press (the two callbacks of one
// press are 150-270 ms apart), but well under the gap between distinct driver
// presses (~2.2s settle). The 64MB scan now runs on a background thread, so it
// no longer stretches the gap between the two callbacks of one press.
constexpr int64_t kPressDedupUs = 500000;

// True while the game is in the main menu. The state probe transiently reports
// kMainMenuMovie (render_active drops to 0 for a couple of 1 Hz samples while
// the menu is idle) even though the main menu is still open; treat that as
// "in the menu" too so a press landing in that window is not misprocessed as
// "leaving the menu" (which would drop the A-press activation).
inline bool in_main_menu() {
  const int s = fable2::stateprobe::CurrentState();
  return s == fable2::stateprobe::kMainMenu ||
         s == fable2::stateprobe::kMainMenuMovie;
}

inline bool enabled() {
#ifdef _WIN32
  char v[8] = {};
  size_t n = 0;
  return ::getenv_s(&n, v, sizeof(v), "FABLE2_MODMENU") == 0 && v[0] == '1';
#else
  const char* v = std::getenv("FABLE2_MODMENU");
  return v != nullptr && v[0] == '1';
#endif
}

inline bool logging() {
#ifdef _WIN32
  char v[8] = {};
  size_t n = 0;
  return ::getenv_s(&n, v, sizeof(v), "FABLE2_MODMENU_LOG") == 0 && v[0] == '1';
#else
  const char* v = std::getenv("FABLE2_MODMENU_LOG");
  return v != nullptr && v[0] == '1';
#endif
}

inline FILE*& logf() {
  static FILE* f = [] -> FILE* {
#ifdef _WIN32
    // Open with FILE_SHARE_READ so the verification driver can poll the log
    // live (a plain fopen("w") holds an exclusive lock and the driver gets
    // PermissionError). We only need write access here; the share mode grants
    // concurrent readers.
    HANDLE h = ::CreateFileA(
        "fable2_modmenu.log", GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return nullptr;
    FILE* out = _fdopen(_open_osfhandle((intptr_t)h, _O_WRONLY), "w");
    return out;
#else
    return std::fopen("fable2_modmenu.log", "w");
#endif
  }();
  return f;
}

inline std::mutex& log_mu() {
  static std::mutex m;
  return m;
}

inline void log_line(const char* fmt, ...) {
  FILE* f = logf();
  if (!f) return;
  // The background scanner thread and the render thread both log; serialize.
  std::lock_guard<std::mutex> lk(log_mu());
  va_list ap;
  va_start(ap, fmt);
  std::vfprintf(f, fmt, ap);
  va_end(ap);
  std::fflush(f);
}

// ---- guest memory (big-endian arena, commit-on-fault) ---------------------
// host = base + guest_addr (guest >= 0xE0000000 uses base + guest + 0x1000).
inline uint64_t host_addr(const uint8_t* base, uint32_t a) {
  const uint64_t off = (a >= 0xE0000000u) ? (a + 0x1000u) : (uint64_t)a;
  return reinterpret_cast<uintptr_t>(base) + off;
}

// Page-level commit checks (commit-on-fault: check page by page, not the whole
// region - a single VirtualQuery region may not span the read). Mirrors the
// state probe's page_ok/aread.
inline bool page_ok_read(const void* p) {
#ifdef _WIN32
  MEMORY_BASIC_INFORMATION m = {};
  if (!::VirtualQuery(const_cast<void*>(p), &m, sizeof(m))) return false;
  return m.State == MEM_COMMIT &&
         (m.Protect & (PAGE_READWRITE | PAGE_READONLY | PAGE_WRITECOPY |
                       PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) != 0;
#else
  (void)p;
  return true;
#endif
}
inline bool page_ok_write(const void* p) {
#ifdef _WIN32
  MEMORY_BASIC_INFORMATION m = {};
  if (!::VirtualQuery(const_cast<void*>(p), &m, sizeof(m))) return false;
  return m.State == MEM_COMMIT &&
         (m.Protect & (PAGE_READWRITE | PAGE_WRITECOPY)) != 0;
#else
  (void)p;
  return true;
#endif
}
// Region-aligned guest read (faster than the state probe's per-page aread):
// follow the committed MEMORY regions (VirtualQuery) and memcpy each one, so a
// 64MB scan issues one VirtualQuery per committed region instead of per 4KB
// page. Returns false if any needed region is not committed+readable.
inline bool read_g(const uint8_t* base, uint32_t a, void* dst, size_t n) {
#ifdef _WIN32
  const uint8_t* p = reinterpret_cast<const uint8_t*>(host_addr(base, a));
  uint8_t* d = static_cast<uint8_t*>(dst);
  size_t off = 0;
  while (off < n) {
    MEMORY_BASIC_INFORMATION m = {};
    if (!::VirtualQuery(static_cast<const void*>(p + off), &m, sizeof(m))) return false;
    if (m.State != MEM_COMMIT) return false;
    if (!(m.Protect & (PAGE_READWRITE | PAGE_READONLY | PAGE_WRITECOPY |
                       PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) return false;
    const uint8_t* region_end =
        static_cast<const uint8_t*>(m.BaseAddress) + m.RegionSize;
    const uint8_t* src = p + off;
    if (src >= region_end) return false;  // zero-length overlap
    size_t avail = (size_t)(region_end - src);
    size_t want = n - off;
    size_t len = avail < want ? avail : want;
    std::memcpy(d + off, src, len);
    off += len;
  }
  return true;
#else
  (void)base; (void)a;
  return true;
#endif
}

inline bool is_live_ptr(uint32_t a) {
  return ((a >= 0x1000u && a < 0x82000000u) ||
          (a >= 0x82000000u && a < 0x84000000u)) &&
         (a & 3) == 0;
}

// ---- UTF-16BE text --------------------------------------------------------
// "Downloadable Content" (20 chars, 42 bytes with NUL) is the stock label we
// replace. The replacements are NUL-terminated and written from the start of
// the slot; the renderer reads until NUL, so a shorter blob just shows the
// shorter text (verified with the "Subtitles" -> "MOD MENU" experiment).
static const uint8_t kDownloadable[] = {
    // "Downloadable Content"
    0x00, 0x44, 0x00, 0x6F, 0x00, 0x77, 0x00, 0x6E, 0x00, 0x6C, 0x00, 0x6F,
    0x00, 0x61, 0x00, 0x64, 0x00, 0x61, 0x00, 0x62, 0x00, 0x6C, 0x00, 0x65,
    0x00, 0x20, 0x00, 0x43, 0x00, 0x6F, 0x00, 0x6E, 0x00, 0x74, 0x00, 0x65,
    0x00, 0x6E, 0x00, 0x74, 0x00, 0x00};
static const uint8_t kModOption[] = {  // "MOD MENU" (8)
    0x00, 0x4D, 0x00, 0x4F, 0x00, 0x44, 0x00, 0x20, 0x00, 0x4D, 0x00, 0x45,
    0x00, 0x4E, 0x00, 0x55, 0x00, 0x00};
static const uint8_t kModLoaded[] = {  // "LOADED" (6)
    0x00, 0x4C, 0x00, 0x4F, 0x00, 0x41, 0x00, 0x44, 0x00, 0x45, 0x00, 0x44,
    0x00, 0x00};

// The live "Downloadable Content" source string(s) in the front-end heap.
struct StrRefs {
  uint32_t addrs[8];
  int count = 0;
};

// Scan [lo,hi) for a UTF-16BE needle (must be followed by NUL), storing up to
// `cap` hit addresses into out. Uses the page-aligned read_g (commit-on-fault).
inline void find_needle(const uint8_t* base, uint32_t lo, uint32_t hi,
                        const uint8_t* needle, size_t nlen, uint32_t* out,
                        int cap, int* count) {
  // Appends hits to out[0..*count); the caller initializes *count. (This no
  // longer resets *count, so a caller can accumulate a low+high sweep.)
  // memchr-based search (mirrors the state probe's scan_front_end exactly):
  // find the distinctive key byte (first non-zero byte) then verify with a
  // single memcmp. Starting the search at key_off and checking i >= key_off
  // avoids the underflow a naive start-at-0 loop has.
  size_t key_off = 0;
  while (key_off < nlen && needle[key_off] == 0) key_off++;
  if (key_off >= nlen) return;  // all-zero needle: nothing to match
  const uint8_t key = needle[key_off];
  const size_t chunk = 65536;
  thread_local uint8_t buf[chunk];
  for (uint32_t a = lo; a + chunk <= hi; a += chunk) {
    if (!read_g(base, a, buf, chunk)) continue;
    // Find ALL copies (up to cap), not just the first (the menu has several
    // live copies of the label in the heap).
    size_t from = key_off;
    while (from + nlen <= chunk) {
      const uint8_t* pos =
          static_cast<const uint8_t*>(std::memchr(buf + from, key, chunk - from));
      if (!pos) break;
      const size_t i = (size_t)(pos - buf);  // position of the key byte
      if (i >= key_off &&
          std::memcmp(buf + (i - key_off), needle, nlen) == 0) {
        // The front-end menu labels are NOT reliably NUL-terminated in this
        // slot (the state probe finds them without a terminator), so do not
        // require a trailing NUL. put_text writes its own NUL.
        if (*count < cap) { out[*count] = a + (uint32_t)(i - key_off); (*count)++; }
        from = (i - key_off) + nlen;  // advance past this match
      } else {
        from = i + 1;
      }
    }
  }
}

static const uint8_t kNeedleDownloadable[] = {  // "Downloadable Content" UTF-16BE
    0x00, 0x44, 0x00, 0x6F, 0x00, 0x77, 0x00, 0x6E, 0x00, 0x6C, 0x00, 0x6F,
    0x00, 0x61, 0x00, 0x64, 0x00, 0x61, 0x00, 0x62, 0x00, 0x6C, 0x00, 0x65,
    0x00, 0x20, 0x00, 0x43, 0x00, 0x6F, 0x00, 0x6E, 0x00, 0x74, 0x00, 0x65,
    0x00, 0x6E, 0x00, 0x74};

// Find the live "Downloadable Content" source string(s). The menu label
// strings land in the low part of the front-end heap (0x405xxxxx-0x40Axxxxx
// observed across runs), so scan that 16MB region first (fast) and only fall
// back to the rest of the 64MB window if it is absent.
inline void find_downloadable(const uint8_t* base, int shown, StrRefs& out) {
  // Always search the STOCK label ("Downloadable Content"). The main menu's
  // rotating background scenes re-allocate the rendered copy as the stock
  // label, so that is what the rendered copy actually says between scene
  // changes. Searching the stock label is what keeps the rendered copy in the
  // cache; searching the current label (MOD MENU/LOADED) misses the
  // re-allocated copy, so the write lands on a non-rendered copy and the
  // relabel never appears on screen. A 0-result re-scan keeps the prior cache
  // (see scanner_thread), so a relabeled copy is not lost between scene
  // changes either.
  (void)shown;
  // Sweep a wide front-end heap window, collecting every live copy. The heap
  // layout varies per run: the rendered copy has landed anywhere from the low
  // 0x405xxxxx cluster up past the 128MB mark (a run found it at no address
  // in 0x40000000-0x50000000 at all), so we sweep 0x40000000-0x60000000
  // (256MB) to cover the observed range. The sweep runs on the background
  // scanner thread (region-based read_g skips uncommitted pages), so the wider
  // window stays off the render thread.
  out.count = 0;
  find_needle(base, 0x40000000u, 0x44000000u, kNeedleDownloadable,
              sizeof(kNeedleDownloadable), out.addrs, 8, &out.count);
  find_needle(base, 0x44000000u, 0x50000000u, kNeedleDownloadable,
              sizeof(kNeedleDownloadable), out.addrs, 8, &out.count);
  find_needle(base, 0x50000000u, 0x54000000u, kNeedleDownloadable,
              sizeof(kNeedleDownloadable), out.addrs, 8, &out.count);
  find_needle(base, 0x54000000u, 0x60000000u, kNeedleDownloadable,
              sizeof(kNeedleDownloadable), out.addrs, 8, &out.count);
}

// ---- diagnostic: which main-menu labels are findable as UTF-16BE? --------
// The state probe finds "New Game"/"Language"/"Options" but (almost) never
// "Downloadable Content", yet the earlier "Subtitles" relabel DID render.
// Search the low front-end heap for each menu item's label (full phrase plus
// the first word, to detect a split storage) and log every hit. Gated on
// FABLE2_MODMENU_SCANLOG=1; throttled to ~0.5 Hz while in the menu.
static const uint8_t kNwNewGame[] = {
    0x00, 0x4E, 0x00, 0x65, 0x00, 0x77, 0x00, 0x20, 0x00, 0x47,
    0x00, 0x61, 0x00, 0x6D, 0x00, 0x65};
static const uint8_t kNwContinue[] = {
    0x00, 0x43, 0x00, 0x6F, 0x00, 0x6E, 0x00, 0x74, 0x00, 0x69,
    0x00, 0x6E, 0x00, 0x75, 0x00, 0x65};
static const uint8_t kNwDownloadable[] = {  // first word only
    0x00, 0x44, 0x00, 0x6F, 0x00, 0x77, 0x00, 0x6E, 0x00, 0x6C, 0x00, 0x6F,
    0x00, 0x61, 0x00, 0x64, 0x00, 0x61, 0x00, 0x62, 0x00, 0x6C, 0x00, 0x65};
static const uint8_t kNwLanguage[] = {
    0x00, 0x4C, 0x00, 0x61, 0x00, 0x6E, 0x00, 0x67, 0x00, 0x75,
    0x00, 0x61, 0x00, 0x67, 0x00, 0x65};
static const uint8_t kNwSubtitles[] = {
    0x00, 0x53, 0x00, 0x75, 0x00, 0x62, 0x00, 0x74, 0x00, 0x69,
    0x00, 0x74, 0x00, 0x6C, 0x00, 0x65, 0x00, 0x73};
static const uint8_t kNwContent[] = {  // second word of "Downloadable Content"
    0x00, 0x43, 0x00, 0x6F, 0x00, 0x6E, 0x00, 0x74, 0x00, 0x65,
    0x00, 0x6E, 0x00, 0x74};

inline bool scanlog_enabled() {
  char v[8] = {};
#ifdef _WIN32
  size_t n = 0;
  return ::getenv_s(&n, v, sizeof(v), "FABLE2_MODMENU_SCANLOG") == 0 && v[0] == '1';
#else
  const char* e = std::getenv("FABLE2_MODMENU_SCANLOG");
  return e && e[0] == '1';
#endif
}

inline std::string read_u16be(const uint8_t* base, uint32_t a, int maxc);

// Dump UTF-16BE text at a, going back `back` chars first (to reveal a preceding
// word such as "Downloadable" before "Content").
inline std::string read_u16be_around(const uint8_t* base, uint32_t a, int back,
                                     int fwd) {
  uint8_t b[128] = {};
  const uint32_t start = (back > 0 && a >= back * 2) ? a - (uint32_t)(back * 2) : 0;
  if (!read_g(base, start, b, sizeof(b))) return "<unreadable>";
  (void)fwd;
  std::string out;
  for (size_t i = 0; i + 1 < sizeof(b); i += 2) {
    const uint16_t c = (uint16_t)((b[i] << 8) | b[i + 1]);
    if (c == 0) break;
    if (c >= 32 && c < 127) out += (char)c; else out += '?';
  }
  return out;
}

inline void scan_diag_menu_words(const uint8_t* base) {
  // Full 64MB window for the two "Downloadable Content" words (the label is not
  // in the low 16MB), plus the low window for the other four short items.
  struct W { const char* name; const uint8_t* bytes; uint32_t lo, hi; int back; }
      words[] = {
          {"Downloadable", kNwDownloadable, 0x40000000u, 0x44000000u, 0},
          {"Content", kNwContent, 0x40000000u, 0x44000000u, 14},
          {"NewGame", kNwNewGame, 0x40000000u, 0x41000000u, 0},
          {"Continue", kNwContinue, 0x40000000u, 0x41000000u, 0},
          {"Language", kNwLanguage, 0x40000000u, 0x41000000u, 0},
          {"Subtitles", kNwSubtitles, 0x40000000u, 0x41000000u, 0},
  };
  for (auto& w : words) {
    StrRefs r;
    find_needle(base, w.lo, w.hi, w.bytes, sizeof(w.bytes), r.addrs, 4, &r.count);
    std::string first = "-";
    if (r.count > 0)
      first = (w.back > 0) ? read_u16be_around(base, r.addrs[0], w.back, 24)
                           : read_u16be(base, r.addrs[0], 24);
    log_line("scanlog: %-13s n=%d first=0x%08X '%s'", w.name, r.count,
             r.count ? r.addrs[0] : 0, first.c_str());
  }
}

// Write a UTF-16BE byte blob at a. The first safe_len bytes are the original
// string we matched (text - always safe to overwrite); only the overflow
// beyond safe_len is guarded against clobbering a live guest pointer.
// Returns true if the write was committed.
inline bool put_text(const uint8_t* base, uint32_t a, const uint8_t* s,
                     size_t n, size_t safe_len) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(host_addr(base, a));
  // The write region must be committed + writable (page by page).
  for (size_t off = 0; off < n; off += 0x1000)
    if (!page_ok_write(p + off)) return false;
  // Guard only the overflow beyond the original string.
  const size_t g0 = (safe_len + 3) & ~size_t(3);
  if (g0 < n) {
    uint8_t room[4] = {};
    std::memcpy(room, p + g0, 4);
    const uint32_t w = (uint32_t)((room[0] << 24) | (room[1] << 16) |
                                  (room[2] << 8) | room[3]);
    if (w != 0 && is_live_ptr(w)) return false;
  }
  std::memcpy(const_cast<uint8_t*>(p), s, n);
  return true;
}

// Re-read a UTF-16BE string at a for logging (up to maxc chars).
inline std::string read_u16be(const uint8_t* base, uint32_t a, int maxc) {
  uint8_t b[64] = {};
  if (!read_g(base, a, b, sizeof(b))) return "<unreadable>";
  std::string out;
  for (int i = 0; i + 1 < 64 && out.size() < (size_t)maxc; i += 2) {
    const uint16_t c = (uint16_t)((b[i] << 8) | b[i + 1]);
    if (c == 0) break;
    if (c >= 32 && c < 127) out += (char)c; else out += '?';
  }
  return out;
}

// ---- per-session state ----------------------------------------------------
struct State {
  std::atomic<int> sel{0};           // mirror selection index 0..4
  std::atomic<int64_t> last_press_us{0};
  std::atomic<int> shown{0};         // 0=stock, 1=MOD MENU, 2=LOADED
  std::atomic<int> activated{0};     // set when A pressed at kModIndex
  uint32_t dl[8] = {};               // "Downloadable Content" string addrs
  int dl_n = 0;
  std::mutex dl_mu;                  // protects dl[]/dl_n (scanner writes, render reads)
  std::atomic<uintptr_t> base_ptr{0};  // host memory base (captured from first frame)
  std::atomic<bool> scanner_started{false};
};
inline State& st() {
  static State s;
  return s;
}

// Forward declarations (defined later, after set_row / scanner infra).
inline void arm_scanner(uint8_t* base);
inline void set_row(const uint8_t* base, int shown, bool verbose = true);

inline int64_t now_us() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// Set the mod row's text to one of: stock "Downloadable Content", "MOD MENU",
// or "LOADED". All are written from the start of the slot and NUL-terminated;
// none overflow the original 42-byte slot.
inline void set_row(const uint8_t* base, int shown, bool verbose) {
  State& s = st();
  const uint8_t* blob = shown == 1 ? kModOption
                     : shown == 2 ? kModLoaded
                                  : kDownloadable;
  const size_t n = shown == 1 ? sizeof(kModOption)
                 : shown == 2 ? sizeof(kModLoaded)
                              : sizeof(kDownloadable);
  // Read the cached addresses under the lock (the scanner thread may update
  // them). The guest writes themselves are safe either way.
  int wrote = 0;
  int cnt = 0;
  uint32_t first = 0;
  {
    std::lock_guard<std::mutex> lk(s.dl_mu);
    for (int i = 0; i < s.dl_n; ++i) {
      if (i == 0) first = s.dl[i];
      if (put_text(base, s.dl[i], blob, n, sizeof(kDownloadable))) ++wrote;
      ++cnt;
    }
  }
  // Confirm the write landed by re-reading the first slot. Per-frame writes
  // pass verbose=false so they don't flood the log.
  if (verbose && cnt > 0)
    log_line("set_row(shown=%d) wrote %d/%d slot(s); slot0 now '%s'",
             shown, wrote, cnt, read_u16be(base, first, 20).c_str());
}

// The mod function the menu item runs. (Forward-declared here so on_press can
// call it; defined below.)
inline void Activate(const uint8_t* base);

// (The live "Downloadable Content" string cache is maintained by the background
// scanner thread; on_frame/on_press only read it via set_row.)

// Called from the InputEvent_DispatchButtonPress hook on every raw press.
inline void on_press(PPCContext& ctx, uint8_t* base) {
  if (!enabled()) return;
  State& s = st();
  arm_scanner(base);  // capture the host base (in case no frame has run yet)
  const bool in_menu = in_main_menu();

  if (!in_menu) {
    // Leaving the menu: restore the stock label and reset the mirror. Also
    // clear the cached strings so the scanner re-finds them on re-entry.
    int dl_n0;
    {
      std::lock_guard<std::mutex> lk(s.dl_mu);
      dl_n0 = s.dl_n;
    }
    if (s.shown.load(std::memory_order_relaxed) != 0 || dl_n0 != 0) {
      set_row(base, 0);
      s.shown.store(0, std::memory_order_relaxed);
      s.activated.store(0, std::memory_order_relaxed);
      s.sel.store(0, std::memory_order_relaxed);
      std::lock_guard<std::mutex> lk(s.dl_mu);
      s.dl_n = 0;
    }
    return;
  }

  // In the menu. Handle the raw press (time-deduped to one action per press).
  const uint32_t mask = ctx.r5.u32 & 0xFFFF;
  const int64_t now = now_us();
  if (now - s.last_press_us.load(std::memory_order_relaxed) >= kPressDedupUs) {
    s.last_press_us.store(now, std::memory_order_relaxed);
    int sel = s.sel.load(std::memory_order_relaxed);
    if (mask & kBtnDown) {
      if (sel < kStockMax) sel = sel + 1;
    } else if (mask & kBtnUp) {
      if (sel > 0) sel = sel - 1;
    } else if (mask & kBtnA) {
      if (sel == kModIndex) {
        Activate(base);
        s.activated.store(1, std::memory_order_relaxed);
      }
    }
    // Back: no mirror action (the stock engine handles it).
    s.sel.store(sel, std::memory_order_relaxed);
  }

  // Keep the visible mod row in sync. The background scanner thread caches the
  // "Downloadable Content" string addresses (the string is allocated LATE, so
  // the scanner finds it a moment after it appears). on_press only reads the
  // cache (no scan on the render thread) and does the fast set_row write.
  const int want = s.activated.load(std::memory_order_relaxed) ? 2 : 1;
  {
    int dl_n;
    {
      std::lock_guard<std::mutex> lk(s.dl_mu);
      dl_n = s.dl_n;
    }
    if (dl_n > 0 && want != s.shown.load(std::memory_order_relaxed)) {
      set_row(base, want);
      s.shown.store(want, std::memory_order_relaxed);
    }
  }
  // Diagnostic: timestamp + element (r3) memory to find the game's real selection index.
  uint32_t el[12] = {};
  if (read_g(base, ctx.r3.u32, el, 48))
    log_line("press t=%lldms sel=%d want=%d shown=%d dl_n=%d mask=0x%X act=%d el=%08X %08X %08X %08X %08X %08X %08X %08X %08X %08X",
             (long long)now, s.sel.load(std::memory_order_relaxed), want,
             s.shown.load(std::memory_order_relaxed), s.dl_n,
             (unsigned)(ctx.r5.u32 & 0xFFFF),
             s.activated.load(std::memory_order_relaxed),
             el[0], el[1], el[2], el[3], el[4], el[5], el[6], el[7], el[8], el[9]);
  else
    log_line("press t=%lldms sel=%d want=%d shown=%d dl_n=%d mask=0x%X act=%d",
             (long long)now, s.sel.load(std::memory_order_relaxed), want,
             s.shown.load(std::memory_order_relaxed), s.dl_n,
             (unsigned)(ctx.r5.u32 & 0xFFFF),
             s.activated.load(std::memory_order_relaxed));
}

// The mod function the menu item runs.
inline void Activate(const uint8_t* base) {
  State& s = st();
  log_line("ACTIVATED t=%lld sel=%d\n", (long long)now_us(),
           s.sel.load(std::memory_order_relaxed));
  (void)base;
}

// Background scanner thread (declared first so arm_scanner can reference it).
// Runs the slow 64MB "Downloadable Content" search OFF the render thread: a
// scan blocks the render thread ~0.7s, and doing it every frame caused stalls.
// It caches the string addresses in State so on_frame/on_press only do the
// fast set_row write. Mirrors the state probe's off-thread scanning design.
inline void scanner_thread();

inline void arm_scanner(uint8_t* base) {
  State& s = st();
  if (!base) return;
  s.base_ptr.store(reinterpret_cast<uintptr_t>(base), std::memory_order_release);
  if (s.scanner_started.exchange(true, std::memory_order_acq_rel)) return;
  std::thread(scanner_thread).detach();
}

// Per-frame hook (render thread): the "Downloadable Content" source string is
// allocated LATE (tens of seconds after the menu opens, non-deterministically),
// so the label is written as soon as the scanner thread finds it -- independent
// of input. This hook only reads the cached addresses (no scan) and does the
// fast set_row write, so it never stalls the render thread.
inline void on_frame(uint8_t* base) {
  if (!enabled()) return;
  State& s = st();
  arm_scanner(base);
  if (!in_main_menu()) return;
  int dl_n;
  {
    std::lock_guard<std::mutex> lk(s.dl_mu);
    dl_n = s.dl_n;
  }
  if (dl_n <= 0) return;
  const int want = s.activated.load(std::memory_order_relaxed) ? 2 : 1;
  // Re-write the label EVERY frame: the engine can re-populate the rendered
  // string, so a single write on state-change is not enough to persist it.
  set_row(base, want, /*verbose=*/false);
  s.shown.store(want, std::memory_order_relaxed);
}

inline void scanner_thread() {
  State& s = st();
  while (true) {
    const uintptr_t b = s.base_ptr.load(std::memory_order_acquire);
    if (b != 0) {
      uint8_t* base = reinterpret_cast<uint8_t*>(b);
      const bool in_menu = in_main_menu();
      // Diagnostic (gated): sample which menu labels are findable right now.
      static int64_t last_scanlog_us = 0;
      if (in_menu && scanlog_enabled()) {
        const int64_t now = now_us();
        if (now - last_scanlog_us > 2000000) {
          last_scanlog_us = now;
          scan_diag_menu_words(base);
        }
      }
      int cur_n;
      {
        std::lock_guard<std::mutex> lk(s.dl_mu);
        cur_n = s.dl_n;
      }
      // Re-scan while in the menu: scan as soon as possible (cur_n==0), then
      // refresh every 500ms so the cache tracks the persistent RENDERED copy
      // (high heap) as well as any transient copies that appear/disappear.
      static int64_t last_scan_us = 0;
      const int64_t now_scan = now_us();
      if (in_menu && (cur_n == 0 || now_scan - last_scan_us > 500000)) {
        last_scan_us = now_scan;
        // One-time diagnostic: verify the base and count readable chunks.
        static bool diag_done = false;
        if (!diag_done) {
          diag_done = true;
          log_line("scanner: base=0x%llX", (unsigned long long)b);
          uint8_t smp[16] = {};
          bool ok0 = read_g(base, 0x40113800u, smp, 16);  // 'DefaultScenario' per state probe
          log_line("scanner: diag read@0x40113800 ok=%d = %02X %02X %02X %02X %02X %02X %02X %02X",
                   ok0 ? 1 : 0, smp[0], smp[1], smp[2], smp[3], smp[4], smp[5], smp[6], smp[7]);
          int rd = 0, tot = 0;
          for (uint32_t a = 0x40000000u; a + 65536u <= 0x41000000u; a += 65536u) {
            uint8_t tmp[8] = {};
            tot++;
            if (read_g(base, a, tmp, 8)) rd++;
          }
          log_line("scanner: diag readable chunks %d/%d in 0x40000000-0x41000000", rd, tot);
        }
        StrRefs refs;
        find_downloadable(base, s.shown.load(std::memory_order_relaxed), refs);  // stock label
        std::lock_guard<std::mutex> lk(s.dl_mu);
        // Keep the existing cached addresses when a re-scan finds nothing:
        // once the row is relabeled (MOD MENU / LOADED) the original
        // "Downloadable Content" needle no longer matches, so a 0-result scan
        // must NOT clear the live addresses (otherwise on_frame stops
        // re-writing and the LOADED state never renders). Only replace the
        // cache when the scan actually finds the label.
        if (refs.count > 0) {
          bool changed = (refs.count != s.dl_n);
          if (!changed)
            for (int i = 0; i < refs.count; ++i)
              if (s.dl[i] != refs.addrs[i]) { changed = true; break; }
          for (int i = 0; i < refs.count && i < 8; ++i) s.dl[i] = refs.addrs[i];
          s.dl_n = (refs.count < 8) ? refs.count : 8;
          if (changed) {
            log_line("scanner: cached %d 'Downloadable Content' string(s):", s.dl_n);
            for (int i = 0; i < s.dl_n; ++i) {
              const uint8_t* p =
                  reinterpret_cast<const uint8_t*>(host_addr(base, s.dl[i]));
              log_line(" 0x%08X writable=%d", s.dl[i], page_ok_write(p) ? 1 : 0);
            }
          }
        }
      }
    }
    // ~10ms cadence: the first scan fires immediately (cur_n==0) and then the
    // 500ms refresh keeps the cache current while in the menu.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

}  // namespace fable2::modmenu

// Strong override of the raw press bit-dispatch. Forwards to the stock body
// and layers the mod-menu mirror index / activation on top.
extern "C" void __imp__InputEvent_DispatchButtonPress(PPCContext& ctx,
                                                  uint8_t* base);
extern "C" void InputEvent_DispatchButtonPress(PPCContext& ctx, uint8_t* base) {
  fable2::modmenu::on_press(ctx, base);
  __imp__InputEvent_DispatchButtonPress(ctx, base);
}

// NOTE: on_frame is called from the per-frame MainRenderLoop_82B9CD68 hook,
// which is owned by fps_meter.h (included earlier). See the forward call there.
