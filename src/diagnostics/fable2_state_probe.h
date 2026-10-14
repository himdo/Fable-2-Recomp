// fable2_state_probe.h - evidence-based UI state classifier (remote "game_state").
//
// Reports which screen the game is on:
//
//   PreMainMenu   - splash/logos, before the front-end prompt is allocated
//   PressAScreen  - the "Press A to start" prompt is on screen
//   MainMenuMovie - front-end idle attract movie (no UI elements, no text)
//   MainMenu      - the interactive front-end menu
//   Unknown (?)   - anything else: early boot, gameplay, cutscenes, pause
//                   menus, options sub-screens, ...
//
// The classifier runs once per second on a background thread (it keeps
// classifying when the UI passes stop firing, e.g. during the movie). It
// decides from live evidence, and Unknown is the DEFAULT: a known state is
// reported only while its evidence is present.
//
// Evidence (all memory-derived, no screenshots):
//
//  1. front-end text - a 1 Hz heap scan of the front-end string window
//     (0x42000000-0x42800000) matches marker words:
//       "to start"                     -> prompt_alloc (latched from boot)
//       >=2 front-end menu words       -> the menu is on screen
//       ("New Game", "Load Game", "Downloadable Content", "Options",
//        "License", "Quit", "Language")
//     The heap holds ALLOCATED strings (the prompt is never freed), so this
//     tells us WHICH screen's text exists but not whether it is being drawn --
//     that "being drawn" bit comes from render activity (below).
//  2. ui_rate - UI text item dispatches per second (0 during the movie and
//     gameplay without text).
//  3. pe_rate - prompt/manager element-list fetches per second (sub_82B458C0
//     hook; ~15-17/s while the prompt or menu is drawn, ~0-7 during the movie).
//     render_active = (pe_rate > 8) || (ui_rate > 100): UI is being drawn.
//  4. prompt_alloc - the "to start" prompt string present in the heap.
//     Allocated once at the first prompt and never freed, so it separates
//     PreMainMenu (before) from everything after it.
//  5. front_end - the runtime value of the game's GUI_FRONT_END cvar (default
//     "DefaultScenario"). The XEX keeps the front-end flag/cvar strings as
//     static constants ("CAN_PRESS_A" @ 0x820CD0C0, "GUI_FRONT_END" @
//     0x820A8064, "DefaultScenario" @ 0x820A8094); the hunt below locates the
//     runtime copy in the heap once, then re-reads it. While the value stays a
//     front-end value the game is still in the front end; once it changes
//     (a gameplay scenario loads) EVERYTHING reads Unknown ("?"), which is
//     what lets gameplay/cutscenes/pause/options report "?".
//  6. the A-press - rising edge of A in the final merged pad state
//     (sub_822B2D60 hook), latched with its timestamp. The prompt and the menu
//     both render UI at the same rate, so a rising A on the prompt is the
//     discrete event that opens the menu: it sets a STICKY in_menu flag that
//     persists until the game leaves the front end or the UI goes text-less.
//
// Classification (Unknown is the default; a 2-sample hysteresis stops a
// one-second dip from flickering the state -- the old MainMenuMovie misfire):
//   !ready                       -> ?
//   !front_end                   -> ?   (gameplay / cutscenes / pause / ...)
//   !prompt_alloc                -> PreMainMenu
//   !render_active               -> MainMenuMovie   (front end, no UI drawn)
//   in_menu                      -> MainMenu
//   else                         -> PressAScreen
//
// Transitions use 2-sample hysteresis so a one-second dip (e.g. the prompt
// blink-off phase) cannot flicker the state.
//
// Investigation: FABLE2_STATE_PROBE=1 logs every sample (all evidence + the
// strings seen) to fable2_state_probe.log and enables the one-time heap hunt
// for the GUI_FRONT_END runtime value.

#pragma once

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <rex/ppc/context.h>
#include <rex/logging/macros.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace fable2::stateprobe {

constexpr uintptr_t kArena = 0x100000000ull;

// State ids (stable values used by the remote API + the snapshot).
enum State : int {
  kUnknown = 0,
  kPreMainMenu = 1,
  kPressAScreen = 2,
  kMainMenuMovie = 3,
  kMainMenu = 4,
};

inline const char* StateName(int s) {
  switch (s) {
    case kPreMainMenu: return "PreMainMenu";
    case kPressAScreen: return "PressAScreen";
    case kMainMenuMovie: return "MainMenuMovie";
    case kMainMenu: return "MainMenu";
    default: return "Unknown";
  }
}

inline bool logging() {
  static const bool e = [] {
#ifdef _WIN32
    char v[8] = {};
    size_t n = 0;
    return ::getenv_s(&n, v, sizeof(v), "FABLE2_STATE_PROBE") == 0 && v[0] == '1';
#else
    const char* v = std::getenv("FABLE2_STATE_PROBE");
    return v && v[0] == '1';
#endif
  }();
  return e;
}

inline bool page_ok(const void* p) {
#ifdef _WIN32
  MEMORY_BASIC_INFORMATION m;
  if (::VirtualQuery(const_cast<void*>(p), &m, sizeof m) == 0) return false;
  return m.State == MEM_COMMIT &&
         (m.Protect & (PAGE_READWRITE | PAGE_READONLY | PAGE_WRITECOPY |
                       PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) != 0;
#else
  (void)p;
  return true;
#endif
}

// Safe guest read from the fixed 0x100000000 arena (commit-on-fault).
inline bool aread(uint32_t ga, void* dst, size_t n) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(kArena + ga);
  size_t off = 0;
  while (off < n) {
    size_t chunk = 0x1000u - ((reinterpret_cast<uintptr_t>(p + off)) & 0xFFFu);
    if (chunk > n - off) chunk = n - off;
    if (!page_ok(p + off)) return false;
    std::memcpy(static_cast<uint8_t*>(dst) + off, p + off, chunk);
    off += chunk;
  }
  return true;
}

// Safe guest read at `base` + guest address (render-thread hooks pass their
// own base; it equals kArena).
inline bool gread(const uint8_t* base, uint32_t ga, void* dst, size_t n) {
  const uint8_t* p = base + ga;
  size_t off = 0;
  while (off < n) {
    size_t chunk = 0x1000u - ((reinterpret_cast<uintptr_t>(p + off)) & 0xFFFu);
    if (chunk > n - off) chunk = n - off;
    if (!page_ok(p + off)) return false;
    std::memcpy(static_cast<uint8_t*>(dst) + off, p + off, chunk);
    off += chunk;
  }
  return true;
}

// A pointer is "plausible" if it is inside the guest address space and its
// arena page is committed + readable.
inline bool plausible(uint32_t a) {
  if (a < 0x00400000u || a >= 0xC0000000u) return false;
  return page_ok(reinterpret_cast<const void*>(kArena + a));
}

inline uint32_t be32(const void* p) {
  return __builtin_bswap32(*reinterpret_cast<const uint32_t*>(p));
}

// ---------------------------------------------------------------------------
// Latest-sample snapshot (written only by the classifier thread, read by the
// remote server thread). Kept tiny + atomic so the read is lock-free.
// ---------------------------------------------------------------------------
struct Snapshot {
  std::atomic<int> state{kUnknown};
  std::atomic<int> item_count{0};   // pe_rate (element-list fetches / s)
  std::atomic<int> prompt_seen{0};  // prompt string on screen
  std::atomic<int> menu_seen{0};    // main-menu option text on screen
  // Most recent distinct strings (for debugging / tuning the classifier).
  std::atomic<int> str_n{0};
  char str_buf[512];
};

inline Snapshot& snap() {
  static Snapshot* s = new Snapshot;  // leaked on purpose (stable address)
  return *s;
}

// Thread-safe accessors for the current game state (used by the remote
// control server's "game_state" command and any host-side poller).
inline int CurrentState() {
  return snap().state.load(std::memory_order_relaxed);
}
inline const char* CurrentStateName() { return StateName(CurrentState()); }

inline FILE* logf() {
  static FILE* f = [] {
#ifdef _WIN32
    FILE* out = nullptr;
    if (::fopen_s(&out, "fable2_state_probe.log", "w") != 0) out = nullptr;
    return out;
#else
    return std::fopen("fable2_state_probe.log", "w");
#endif
  }();
  return f;
}

inline int64_t t_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

inline int64_t now_us() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// Time in ms since the process (static is initialised on first use, which is
// effectively process start since the probe is referenced early).
inline int64_t boot_ms() {
  static const int64_t start = now_us() / 1000;
  return t_ms() - start;
}

// --- Rolling call-rate tracking. ------------------------------------------
// prompt_elem_total counts the prompt/manager element-list fetches (sub_82B458C0
// non-zero returns). A non-negligible rate means the prompt OR the main menu is
// being drawn; ~0 means the movie (video, no UI elements).
inline std::atomic<uint64_t>& prompt_elem_total() {
  static std::atomic<uint64_t> c{0};
  return c;
}
inline void tick_prompt_elem() {
  prompt_elem_total().fetch_add(1, std::memory_order_relaxed);
}
// A-press timestamp (ms). Latched on the rising edge of A in the final merged
// pad state (see observe_a_button, called from the sub_822B2D60 hook), so it
// works no matter which input source drives A.
inline std::atomic<int64_t>& last_a_press_ms() {
  static std::atomic<int64_t> t{0};
  return t;
}
inline void record_a_press(int64_t ms) {
  last_a_press_ms().store(ms, std::memory_order_relaxed);
}
// Current A-button state from the FINAL pad state (all drivers OR-merged:
// remote + keyboard + physical).
inline std::atomic<int>& a_button_state() {
  static std::atomic<int> b{0};
  return b;
}
// B-button state (X_INPUT_GAMEPAD_B = 0x2000): used to clear the sticky
// in_menu flag when the user backs out of the menu to the prompt.
inline std::atomic<int>& b_button_state() {
  static std::atomic<int> b{0};
  return b;
}
inline std::atomic<int64_t>& last_b_press_ms() {
  static std::atomic<int64_t> t{0};
  return t;
}
inline uint16_t read_a_button_mask(uint32_t state_ptr) {
  uint8_t buf[2] = {0, 0};
  if (!aread(state_ptr + 4, buf, 2)) return 0;
  return (uint16_t)((buf[0] << 8) | buf[1]);
}
// Records rising A and B press edges from the final merged pad state.
inline void observe_a_button(uint32_t state_ptr) {
  const uint16_t mask = read_a_button_mask(state_ptr);
  const int a_now = (mask & 0x1000u) ? 1 : 0;
  const int a_prev = a_button_state().load(std::memory_order_relaxed);
  a_button_state().store(a_now, std::memory_order_relaxed);
  if (a_now && !a_prev) record_a_press(t_ms());
  const int b_now = (mask & 0x2000u) ? 1 : 0;
  const int b_prev = b_button_state().load(std::memory_order_relaxed);
  b_button_state().store(b_now, std::memory_order_relaxed);
  if (b_now && !b_prev) last_b_press_ms().store(t_ms(), std::memory_order_relaxed);
}

// --- Render activity + front-end text (heap scan). --------------------------
// The render thread ticks one counter per UI text item dispatched (hooked in
// fable2_state_probe.h via fable2_text_probe.h); ui_rate (items / s) is high
// while UI text is being drawn and ~0 during the attract movie (video) and
// text-less scenes. sample_text_item is a single relaxed atomic increment.
inline std::atomic<uint64_t>& ui_text_total() {
  static std::atomic<uint64_t> c{0};
  return c;
}
// Called from the UITextItem_Dispatch hook for every UI text item rendered.
inline void sample_text_item(const uint8_t* base, PPCContext& ctx) {
  (void)base;
  (void)ctx;
  ui_text_total().fetch_add(1, std::memory_order_relaxed);
}

// The dynamic (heap) region where the front-end allocates its live UI strings
// (the "to start" prompt sits at 0x4266xxxx; the menu option labels are
// allocated in the front-end heap). Scan it once per sample for the marker
// words that identify the front-end screens. The prompt ("to start") and the
// menu option labels are allocated at a NON-DETERMINISTIC heap address that
// spans roughly 0x405xxxxx-0x42xxxxxx across runs, so the window must be wide
// enough to cover that whole span (the old 0x42000000-0x42800000 window
// routinely missed them, leaving the classifier stuck in PreMainMenu).
constexpr uint32_t kTextLo = 0x40000000u;
constexpr uint32_t kTextHi = 0x44000000u;

// Builds the UTF-16BE byte pattern for an ASCII name.
inline std::vector<uint8_t> utf16be_pat(const char* s) {
  std::vector<uint8_t> p;
  for (; *s; ++s) {
    p.push_back(0);
    p.push_back((uint8_t)*s);
  }
  return p;
}

struct FrontEndText {
  bool prompt = false;  // "to start" present (latched from first prompt)
  int menu_hits = 0;    // distinct front-end menu words present
  std::vector<std::string> menu_words;  // which words (log/tuning)
};

// The front-end menu option words. Matched as UTF-16BE (and plain ASCII) so
// both a heap copy and an image string landing in the window are caught.
inline const char* kMenuWords[] = {"New Game", "Load Game",
                                   "Downloadable Content", "Options",
                                   "License", "Quit", "Language"};

// Scans the front-end heap window for the marker words. Runs on the 1 Hz
// classifier thread (the read is page-guarded; uncommitted pages are skipped
// quickly). Each pattern is located by memchr on its first non-zero byte
// (UTF-16BE patterns are 0x00,c0,0x00,c1,... so c0 is a good key) and then
// verified with a single memcmp -- this keeps the wide window scan fast
// (~tens of ms) instead of a naive O(window*pattern) memcmp storm.
struct Pat {
  std::string name;
  std::vector<uint8_t> bytes;
  uint8_t key = 0;
  size_t key_off = 0;
};
inline FrontEndText scan_front_end() {
  FrontEndText e;
  std::vector<Pat> pats;
  auto add_pat = [&](const std::string& name, const std::vector<uint8_t>& b) {
    Pat p;
    p.name = name;
    p.bytes = b;
    for (size_t i = 0; i < b.size(); ++i)
      if (b[i] != 0) { p.key = b[i]; p.key_off = i; break; }
    pats.push_back(std::move(p));
  };
  add_pat("to start", utf16be_pat("to start"));
  for (const char* m : kMenuWords) {
    add_pat(std::string(m), utf16be_pat(m));
    add_pat(std::string(m) + "|asc", std::vector<uint8_t>(m, m + std::strlen(m)));
  }
  const size_t BS = 65536;
  uint8_t buf[BS];
  for (uint32_t off = kTextLo; off + BS <= kTextHi; off += BS) {
    if (!aread(off, buf, BS)) continue;
    for (const auto& p : pats) {
      bool found = false;
      size_t from = p.key_off;
      while (!found && from + p.bytes.size() <= BS) {
        const uint8_t* pos =
            (const uint8_t*)std::memchr(buf + from, p.key, BS - from);
        if (!pos) break;
        const size_t i = (size_t)(pos - buf);  // position of the key byte
        if (i >= p.key_off &&
            std::memcmp(buf + (i - p.key_off), p.bytes.data(),
                        p.bytes.size()) == 0)
          found = true;
        else from = i + 1;
      }
      if (!found) continue;
      if (p.name == "to start") e.prompt = true;
      else {
        const std::string base =
            p.name.rfind("|asc") == std::string::npos
                ? p.name
                : p.name.substr(0, p.name.rfind("|asc"));
        if (std::find(e.menu_words.begin(), e.menu_words.end(), base) ==
            e.menu_words.end()) {
          e.menu_words.push_back(base);
          ++e.menu_hits;
        }
      }
    }
  }
  return e;
}

// --- Front-end memory flag hunt. ------------------------------------------
// The XEX image keeps the front-end cvar/flag strings as static constants:
//   "CAN_PRESS_A"       @ 0x820CD0C0   (front-end flag name)
//   "GUI_FRONT_END"     @ 0x820A8064   (cvar name)
//   "DefaultScenario"   @ 0x820A8094   (its default value; gameplay starts in
//                                       "QC010_ChildhoodStart")
// The hunt scans the heap (log mode only) for (a) a runtime copy of the value
// string and (b) a table entry holding a pointer to the flag-name constant.
// Once the value copy is found it is re-read every sample, so the classifier
// knows whether the game is still in the front end.
struct FrontEndFlag {
  uint32_t value_addr = 0;  // heap copy of the GUI_FRONT_END value string
  bool value_found = false;
  char value[64] = {};
};
inline FrontEndFlag& fe_flag() {
  static FrontEndFlag* f = new FrontEndFlag;  // leaked on purpose
  return *f;
}

// The value still counts as "front end" while it is empty or one of the
// known front-end values; a changed value (gameplay scenario, ...) means the
// game left the front end.
inline bool front_end_value_active() {
  FrontEndFlag& f = fe_flag();
  if (!f.value_found) return true;  // no evidence yet: assume front end
  if (f.value[0] == 0) return true;
  return std::strcmp(f.value, "DefaultScenario") == 0;
}

// The current GUI_FRONT_END value (for the game_state JSON / debugging).
// Empty until the heap hunt locates the runtime copy.
inline std::string CurrentFrontEndValue() {
  FrontEndFlag& f = fe_flag();
  if (!f.value_found) return std::string();
  return std::string(f.value);
}

inline void fe_flag_read() {
  FrontEndFlag& f = fe_flag();
  if (!f.value_found) return;
  uint8_t b[64] = {};
  if (aread(f.value_addr, b, sizeof b)) {
    size_t n = 0;
    while (n < 63 && b[n]) ++n;
    std::memcpy(f.value, b, n);
    f.value[n] = 0;
  }
}

// One heap pass (log mode only) looking for the runtime value copy + any
// table entries that point at the static constants above.
inline void fe_flag_hunt() {
  FrontEndFlag& f = fe_flag();
  static int hunts = 0;
  // The value copy is present from early boot; stop the full-heap scan once
  // it is found (and the flag-name context is logged), or after ~5 min.
  if (f.value_found && hunts > 20) return;
  if (++hunts > 300) return;
  const uint32_t search_base = 0x40000000u;
  const uint32_t kEnd = 0x50000000u;
  const size_t chunk = 65536;
  uint8_t buf[chunk];
  const char* needle = "DefaultScenario";
  const uint32_t kFlagName = 0x820CD0C0u;  // "CAN_PRESS_A"
  static int logged_ctx = 0;
  for (uint32_t a = search_base; a + chunk <= kEnd; a += chunk) {
    if (!aread(a, buf, sizeof buf)) continue;
    for (size_t i = 0; i + 4 <= chunk; ++i) {
      const uint32_t w = be32(buf + i);
      // (b) table entry holding a pointer to the flag-name constant
      if (w == kFlagName) {
        if (logging() && logged_ctx < 8) {
          char l[220];
          std::snprintf(l, sizeof l,
                        "  [fe-hunt] 0x%08X holds ptr to CAN_PRESS_A; ctx:",
                        a + (uint32_t)i);
          std::fputs(l, logf());
          for (int k = -4; k < 8; ++k) {
            if (k < 0 || (size_t)k * 4 + 4 > chunk) continue;
            char h[12];
            std::snprintf(h, sizeof h, " %08X", be32(buf + (size_t)k * 4));
            std::fputs(h, logf());
          }
          std::fputc('\n', logf());
          ++logged_ctx;
        }
      }
      // (a) runtime copy of the value string (ASCII or UTF-16BE)
      if (f.value_found) continue;
      if (std::memcmp(buf + i, needle, 15) == 0) {
        f.value_found = true;
        f.value_addr = a + (uint32_t)i;
        if (logging()) {
          char l[160];
          std::snprintf(l, sizeof l,
                        "  [fe-hunt] GUI_FRONT_END value copy @ 0x%08X\n",
                        f.value_addr);
          std::fputs(l, logf());
        }
        return;
      }
    }
  }
}

inline void do_sample();

inline std::atomic<bool>& running() {
  static std::atomic<bool> r{false};
  return r;
}
inline void start() {
  static std::atomic<bool> started{false};
  bool exp = false;
  if (!started.compare_exchange_strong(exp, true)) return;
  running().store(true);
  std::thread([] {
    while (running().load()) {
      for (int i = 0; i < 10 && running().load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      if (running().load()) do_sample();
    }
  }).detach();
}
inline void stop() { running().store(false); }

// ---------------------------------------------------------------------------
// The 1 Hz classifier.
// ---------------------------------------------------------------------------
inline void do_sample() {
  static uint64_t last_pet = 0, last_ut = 0;
  static int64_t last_t = 0, last_press_seen = -1;
  static int diff_count = 0, stable = kUnknown;
  const int64_t t = t_ms();
  double pe_rate = 0.0, ui_rate = 0.0;
  const uint64_t pet = prompt_elem_total().load(std::memory_order_relaxed);
  const uint64_t ut = ui_text_total().load(std::memory_order_relaxed);
  if (last_t == 0) {
    last_pet = pet;
    last_ut = ut;
    last_t = t;
  } else {
    const int64_t dt = (t - last_t > 0) ? (t - last_t) : 1;
    pe_rate = (double)(pet - last_pet) * 1000.0 / (double)dt;
    ui_rate = (double)(ut - last_ut) * 1000.0 / (double)dt;
    last_pet = pet;
    last_ut = ut;
    last_t = t;
  }

  // Before the guest image is mapped (early boot) we cannot read anything:
  // report Unknown.
  const bool ready = page_ok(
      reinterpret_cast<const void*>(kArena + 0x820A8064u));  // image strings

  // The GUI_FRONT_END runtime value: hunt the heap until the value copy is
  // found (bounded; see fe_flag_hunt), then just re-read it. While it stays a
  // front-end value the game is in the front end; a changed value (a gameplay
  // scenario) marks "left the front end" so unknown states read "?".
  fe_flag_hunt();
  fe_flag_read();
  const bool front_end = front_end_value_active();

  // --- front-end text evidence (heap window) ------------------------------
  // scan_front_end() finds the marker words ALLOCATED in the front-end heap.
  // The prompt ("to start") latches from the first prompt onward (never freed);
  // the menu option words are present while the menu is allocated. This tells
  // us WHICH screen's text exists; render_active below supplies the "is UI
  // being drawn right now" bit (the heap doesn't), which drops to ~0 during the
  // attract movie (video) and text-less scenes.
  FrontEndText fe = ready ? scan_front_end() : FrontEndText{};
  const bool prompt_alloc = fe.prompt;
  const int menu_hits = fe.menu_hits;
  const std::vector<std::string>& seen = fe.menu_words;
  // The attract movie is detected by pe_rate (the 3D UI element-manager fetch,
  // sub_82B458C0 for lists in 0x8333xxxx) dropping to ~0: the movie is a video
  // the element manager doesn't draw, so pe goes 15-16 -> 0 for the whole movie
  // (~15-30 s) and back. The UI text dispatch rate (ui_rate) is NOT a usable
  // signal -- the prompt/menu text stays composited over the movie, so ui stays
  // ~700-800/s the entire time. Hence render_active is pe-only.
  const bool render_active = pe_rate > 8.0;
  const int64_t a_press_t = last_a_press_ms().load(std::memory_order_relaxed);

  // --- sticky "in the front-end menu" flag --------------------------------
  // The prompt and the menu both run the UI element manager (pe ~15-17), so
  // pe alone can't tell them apart. Entering the menu is a discrete event -- a
  // rising A on the prompt, or the menu option words appearing in the heap. It
  // stays set until the game leaves the front end or the UI goes text-less
  // (movie).
  static bool in_menu = false;
  static int movie_run = 0;
  // The menu option words appearing in the heap is the reliable "menu is
  // open" signal (the prompt string alone is not -- it is allocated at a
  // non-deterministic address and can fall outside the scan window, and an
  // A-press on the prompt used to latch in_menu here, which false-positived
  // the main menu a frame before the menu words were allocated).
  if (menu_hits >= 2) in_menu = true;          // menu option words in the heap
  // B-press backs out of the menu to the prompt: clear in_menu.
  const int64_t b_press_t = last_b_press_ms().load(std::memory_order_relaxed);
  static int64_t last_b_seen = -1;
  if (b_press_t > 0 && b_press_t != last_b_seen) {
    last_b_seen = b_press_t;
    if (in_menu && prompt_alloc && render_active) in_menu = false;  // B on menu
  }
  // Note: prompt_alloc is deliberately NOT used here -- the "to start" prompt
  // string is allocated at a non-deterministic heap address that can fall
  // outside the scan window, so gating in_menu on it would drop the main menu
  // back to PreMainMenu. in_menu is driven by the menu option words instead.
  if (!ready || !front_end) in_menu = false;  // left the front end
  if (render_active) movie_run = 0;
  else if (++movie_run >= 2) in_menu = false;  // UI manager paused (movie/attract)

  // --- classify -----------------------------------------------------------
  // Unknown is the default: a known state is reported only while its
  // evidence is present. Gameplay, cutscenes, pause/options and loading all
  // read "?" (the front-end flag is no longer active).
  int raw;
  if (!ready) {
    raw = kUnknown;
  } else if (!front_end) {
    raw = kUnknown;  // gameplay, cutscenes, pause/options, loading, ...
  } else if (in_menu) {
    raw = kMainMenu;  // menu option words present -> the menu is open
  } else if (!render_active) {
    // Front end, no UI being drawn: the attract video if the prompt has been
    // seen, otherwise the splash/logos before it.
    raw = prompt_alloc ? kMainMenuMovie : kPreMainMenu;
  } else if (prompt_alloc) {
    raw = kPressAScreen;  // the "to start" prompt is drawn
  } else {
    raw = kPreMainMenu;  // front end + render active, but no prompt/menu yet
  }

  // 2-sample hysteresis: a single one-second dip (e.g. the prompt blink-off
  // phase) cannot flicker the reported state.
  if (raw == stable) {
    diff_count = 0;
  } else if (++diff_count >= 2) {
    stable = raw;
  }
  const int state = stable;

  Snapshot& sn = snap();
  sn.prompt_seen.store(prompt_alloc ? 1 : 0, std::memory_order_relaxed);
  sn.menu_seen.store(in_menu ? 1 : 0, std::memory_order_relaxed);
  sn.item_count.store((int)(pe_rate + 0.5), std::memory_order_relaxed);
  sn.state.store(state, std::memory_order_relaxed);
  {
    char* w = sn.str_buf;
    size_t n = sizeof sn.str_buf - 1;
    int wlen = 0;
    for (const std::string& s : seen) {
      if (wlen >= 4) break;
      if (wlen) {
        if (wlen + 2 > (int)n) break;
        w[wlen++] = '|';
      }
      const size_t cap = (int)n - wlen;
      std::memcpy(w + wlen, s.data(), std::min(s.size(), cap));
      wlen += (int)std::min(s.size(), cap);
      if ((size_t)wlen >= n) break;
    }
    w[wlen] = 0;
    sn.str_n.store(wlen, std::memory_order_relaxed);
  }

  static const char* names[] = {"?", "PreMainMenu", "PressAScreen",
                                "MainMenuMovie", "MainMenu"};
  static int last_state = -1;
  const char* cur_name = names[state];

  // In-game logger (same channel as the remote control server): state changes
  // only, so the AI / player can see transitions without log spam.
  if (state != last_state) {
    const char* prev_name =
        (last_state >= 0 && last_state <= 4) ? names[last_state] : "?";
    last_state = state;
    REXSYS_INFO(
        "[state] boot={}ms: {} -> {} (pe={:.0f} ui={:.0f} ra={} pa={} menu={} fe={})",
        (long long)boot_ms(), prev_name, cur_name, pe_rate, ui_rate,
        render_active ? 1 : 0, prompt_alloc ? 1 : 0, in_menu ? 1 : 0,
        front_end ? 1 : 0);
  }

  // File log (FABLE2_STATE_PROBE=1): every sample + the evidence, for tuning.
  if (logging()) {
    FILE* f = logf();
    if (f) {
      const int64_t a_age = (a_press_t > 0) ? (t - a_press_t) : -1;
      char hdr[256];
      std::snprintf(
          hdr, sizeof hdr,
          "boot=%lldms  %-13s  (raw=%-13s pe=%.0f ui=%.0f ra=%d pa=%d menu=%d a_age=%lld a_btn=%d fe=%s%s)\n",
          (long long)boot_ms(), cur_name, names[raw], pe_rate, ui_rate,
          render_active ? 1 : 0, prompt_alloc ? 1 : 0, in_menu ? 1 : 0, (long long)a_age,
          a_button_state().load(std::memory_order_relaxed),
          fe_flag().value_found ? "v=" : "noV",
          fe_flag().value_found ? fe_flag().value : "");
      std::fputs(hdr, f);
      if (!seen.empty()) {
        std::fputs("    txt:", f);
        for (const std::string& s : seen) {
          if (s.size() > 32) continue;  // skip long blobs in the log line
          std::fputc(' ', f);
          std::fwrite(s.data(), 1, s.size(), f);
        }
        std::fputc('\n', f);
      }
      std::fflush(f);
    }
  }
}

}  // namespace fable2::stateprobe

// Strong override of the element-list fetch. Runs on the render thread at
// high frequency, so the tick is a single relaxed atomic increment (no I/O,
// no allocation). Debug/remote builds only (FABLE2_REMOTE_CONTROL); release
// builds use the original weak body untouched.
#ifdef FABLE2_REMOTE_CONTROL
// sub_82B458C0 = prompt/manager element-list fetch/next (returns element, 0 =
// done). Count the non-zero returns for lists in the 0x8333xxxx manager
// region: the rate is high (~15-17/s) while the "Press A" prompt OR the main
// menu is being drawn and ~0 during the movie (video, no UI elements).
extern "C" void __imp__ProcessAndProcessAndProcess573_82B458C0(PPCContext&, uint8_t*);
extern "C" void ProcessAndProcessAndProcess573_82B458C0(PPCContext& ctx, uint8_t* base) {
  const uint32_t in_list = ctx.r3.u32;
  __imp__ProcessAndProcessAndProcess573_82B458C0(ctx, base);
  if (ctx.r3.u32 != 0 && in_list >= 0x83330000u && in_list < 0x83340000u) {
    fable2::stateprobe::tick_prompt_elem();  // any manager element list
  }
}

// The guest's XamInputGetState wrapper: reads the FINAL merged pad state (all
// drivers OR-merged: remote + keyboard + physical). Capture the A button from
// it so the A-press is detected no matter which input source drives it.
namespace fable2::attackprobe {
// Input-press function probe (src/diagnostics/fable2_attack_probe.h, single-TU
// include from main.cpp): also feed it the merged pad state for its press
// edges. Forward-declared here to avoid the include.
void observe_state(const uint8_t* base, uint32_t state_ptr);
}
extern "C" void GetUser1InputState_822B2D60(PPCContext& ctx, uint8_t* base) {
  const uint32_t state_ptr = ctx.r4.u32;
  __imp__GetUser1InputState_822B2D60(ctx, base);
  fable2::stateprobe::observe_a_button(state_ptr);
  fable2::attackprobe::observe_state(base, state_ptr);
}
#endif  // FABLE2_REMOTE_CONTROL
