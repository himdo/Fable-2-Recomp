// fable2_attack_probe.h - identify which guest functions fire when an input
// action starts (e.g. "start a melee attack"), so the action can be remapped
// natively.
//
// Built on the guest function-call tracer (src/core/fable2_func_trace.h,
// hooked into every recompiled guest function via REX_FUNC_PROLOGUE):
//
//   1. ROLLING BASELINE. While the probe is active, every SAMPLE_MS
//      (default 500) it snapshots the tracer's cumulative per-function call
//      counts and keeps the per-interval DELTAS for the last BASELINE_MS
//      (default 3000 ms). The baseline answers "which functions is the game
//      calling right now, and at what rate" (idle animation, walking,
//      render, ...).
//
//   2. PRESS DETECTION. The FINAL merged pad state (keyboard + physical pad
//      + remote, OR-merged) is observed from the guest's own XamInputGetState
//      wrapper (GetUser1InputState_822B2D60) and, as a second
//      source, from the keyboard/mouse driver, so a press is caught no matter
//      which input device drives it. A rising edge on a watched button
//      (default X,Y = Fable 2 melee light/heavy) starts a capture.
//
//   3. CAPTURE. For WINDOW_MS (default 700) after the press:
//        - the tracer's sequential log (fable2_func_trace.log) is switched on
//          and bracketed with ATTACK-PROBE markers, so the exact call
//          SEQUENCE of the attack start-up is on disk;
//        - the cumulative counts are snapshotted at open (A) and close (B).
//
//   4. REPORT (fable2_attack_probe.log, one section per capture): the
//      functions called in the window that were
//        (a) NEW       - never called in the pre-press baseline, or
//        (b) INCREASED - called far more than the baseline rate predicts,
//      plus the plain top-N of the window. The melee attack start-up chain is
//      in (a)/(b); the bracketed sequential trace gives the order, and the
//      first NEW entries right after the input poll are the entry point(s) to
//      hook for a native key mapping. Repeat the press a few times and look
//      for functions that are NEW/INCREASED in EVERY capture - those are the
//      stable attack-start set (one-off functions from unrelated events fall
//      out).
//
// Off by default (the per-function counting has a measurable CPU cost).
// Enable:
//   FABLE2_ATTACK_PROBE=1            at launch (env var)
//   Fable2AttackProbeSetEnabled(true) at runtime (extern "C")
// Tunables (env, read when the probe first activates; runtime setters too):
//   FABLE2_ATTACK_PROBE_BUTTONS=X,Y  watched names: A B X Y LB RB LT RT
//                                    Up Down Left Right L3 R3
//                                    (synonyms: LSHOULDER RSHOULDER DUP
//                                    DDOWN DLEFT DRIGHT LTHUMB RTHUMB
//                                    LTRIGGER RTRIGGER)
//   FABLE2_ATTACK_PROBE_WINDOW_MS=700    capture window after the press
//   FABLE2_ATTACK_PROBE_BASELINE_MS=3000 baseline window length
//   FABLE2_ATTACK_PROBE_SAMPLE_MS=500    baseline sample period
//   FABLE2_ATTACK_PROBE_COOLDOWN_MS=1000 min gap between captures
//   FABLE2_ATTACK_PROBE_TOP=200          max lines per report section
//   Fable2AttackProbeCaptureNow()        manual capture (no press needed)
//
// Files (next to the exe):
//   fable2_attack_probe.log   - reports (append; delete between sessions)
//   fable2_func_trace.log     - sequential trace with ATTACK-PROBE markers
//   fable2_func_summary.log   - live count summary while the probe runs (a
//                               side effect of the tracer's summary mode)
//
// Usage (melee attack): launch with FABLE2_ATTACK_PROBE=1, get to an open
// spot with a MELEE weapon equipped, stand still for a few seconds (fills
// the baseline), then press the mapped attack key. Walk-and-attack works
// too; the baseline then contains movement and the report is the
// attack-specific delta on top of it.

#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <rex/ppc/context.h>

#include "fable2_func_trace.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace fable2 {
namespace attackprobe {

using CountMap = std::unordered_map<std::string, uint64_t>;

namespace detail {

inline bool env_flag(const char* name) {
#ifdef _WIN32
  char v[8] = {};
  size_t n = 0;
  return ::getenv_s(&n, v, sizeof(v), name) == 0 && v[0] == '1';
#else
  const char* v = std::getenv(name);
  return v != nullptr && v[0] == '1';
#endif
}

inline long env_int(const char* name, long def) {
  const char* v = std::getenv(name);
  return v ? std::strtol(v, nullptr, 10) : def;
}

inline std::string env_str(const char* name, const char* def) {
  const char* v = std::getenv(name);
  return v ? std::string(v) : std::string(def);
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

// Safe guest read (commit-on-fault arena; every page is checked before it is
// touched). Mirrors fable2_state_probe.h's aread/gread.
inline bool safe_read(const uint8_t* p, void* dst, size_t n) {
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

}  // namespace detail

// --- Watched input targets -------------------------------------------------
// X_INPUT_GAMEPAD_BUTTON masks (Xbox 360 ABI; see the SDK's input.h).

struct Watch {
  uint16_t buttons = 0x4000u | 0x8000u;  // X + Y (Fable 2 melee light/heavy)
  bool lt = false;
  bool rt = false;
  std::string names = "X,Y";
};

// Parses "X,Y,LT,..." (case-insensitive) into a Watch. If NOTHING parses,
// falls back to the default watch (X,Y) so a typo does not silently disable
// the probe.
inline void parse_watch(std::string_view text, Watch& w) {
  Watch parsed;
  parsed.buttons = 0;  // (Watch's default is the X,Y watch)
  std::string names;
  size_t start = 0;
  auto add = [&](std::string_view tok) {
    std::string u;
    u.reserve(tok.size());
    for (char c : tok)
      u.push_back(std::toupper(static_cast<unsigned char>(c)));
    while (!u.empty() && u.front() == ' ') u.erase(u.begin());
    while (!u.empty() && u.back() == ' ') u.pop_back();
    uint16_t mask = 0;
    if (u == "A") {
      mask = 0x1000u;
    } else if (u == "B") {
      mask = 0x2000u;
    } else if (u == "X") {
      mask = 0x4000u;
    } else if (u == "Y") {
      mask = 0x8000u;
    } else if (u == "LB" || u == "LSHOULDER") {
      mask = 0x0100u;
    } else if (u == "RB" || u == "RSHOULDER") {
      mask = 0x0200u;
    } else if (u == "UP" || u == "DUP") {
      mask = 0x0001u;
    } else if (u == "DOWN" || u == "DDOWN") {
      mask = 0x0002u;
    } else if (u == "LEFT" || u == "DLEFT") {
      mask = 0x0004u;
    } else if (u == "RIGHT" || u == "DRIGHT") {
      mask = 0x0008u;
    } else if (u == "L3" || u == "LTHUMB") {
      mask = 0x0040u;
    } else if (u == "R3" || u == "RTHUMB") {
      mask = 0x0080u;
    } else if (u == "LT" || u == "LTRIGGER") {
      parsed.lt = true;
    } else if (u == "RT" || u == "RTRIGGER") {
      parsed.rt = true;
    } else {
      return;  // unknown name: skip
    }
    parsed.buttons |= mask;
    if (!names.empty()) names += ",";
    names += u;
  };
  for (size_t i = 0; i <= text.size(); ++i) {
    if (i == text.size() || text[i] == ',') {
      if (i > start) add(text.substr(start, i - start));
      start = i + 1;
    }
  }
  if (parsed.buttons == 0 && !parsed.lt && !parsed.rt) {
    w = Watch{};  // default X,Y
    w.names = "X,Y (default; nothing in the button list parsed)";
    return;
  }
  w.buttons = parsed.buttons;
  w.lt = parsed.lt;
  w.rt = parsed.rt;
  w.names = names;
}

struct Config {
  Watch watch;
  int window_ms = 700;
  int baseline_ms = 3000;
  int sample_ms = 500;
  int cooldown_ms = 1000;
  int top = 200;
};

inline void load_env_config(Config& c) {
  parse_watch(detail::env_str("FABLE2_ATTACK_PROBE_BUTTONS", "X,Y"), c.watch);
  c.window_ms = std::clamp(
      static_cast<int>(detail::env_int("FABLE2_ATTACK_PROBE_WINDOW_MS", 700)),
      20, 30000);
  c.baseline_ms = std::clamp(static_cast<int>(
                                 detail::env_int("FABLE2_ATTACK_PROBE_BASELINE_MS",
                                                 3000)),
                             1000, 60000);
  c.sample_ms = std::clamp(
      static_cast<int>(detail::env_int("FABLE2_ATTACK_PROBE_SAMPLE_MS", 500)),
      50, c.baseline_ms / 2);
  c.cooldown_ms = std::clamp(static_cast<int>(
                                 detail::env_int("FABLE2_ATTACK_PROBE_COOLDOWN_MS",
                                                 1000)),
                             0, 60000);
  c.top = std::clamp(static_cast<int>(detail::env_int("FABLE2_ATTACK_PROBE_TOP",
                                                      200)),
                     10, 5000);
}

// --- State ------------------------------------------------------------------

inline std::atomic<bool>& enabled() {
  static std::atomic<bool> e{detail::env_flag("FABLE2_ATTACK_PROBE")};
  return e;
}

// Calls in one [t0, t1) interval: name -> count. Consecutive deltas telescope:
// the sum over a run equals the net call count over the run's span.
struct BaselineDelta {
  int64_t t0_ms = 0, t1_ms = 0;
  CountMap calls;
};

// Tracer state to restore when the probe turns off.
struct SavedTracerState {
  bool enabled = false, summary = false, log = false, subs_only = false;
  std::string filter;
};

struct State {
  Config cfg;
  bool active = false;  // tracer reconfigured by this probe (see set_enabled)
  // Rising-edge tracking, per source: the keyboard driver only reports the
  // keys it contributes, the guest wrapper reports the FINAL merged state
  // (keyboard + physical + remote). Tracking each source against its own
  // last value keeps a physical-pad press from re-"rising" every frame the
  // keyboard source reports 0.
  struct PrevState {
    uint16_t buttons = 0;
    bool lt = false, rt = false;
  };
  PrevState prev_kbd;
  PrevState prev_pad;
  // Rolling baseline.
  CountMap last_cum;
  int64_t last_cum_ms = 0;
  std::deque<BaselineDelta> deltas;
  // In-flight capture.
  bool capturing = false;
  int capture_n = 0;
  int64_t capture_start_ms = 0, capture_end_ms = 0, cooldown_until_ms = 0;
  CountMap snap_before;
  CountMap baseline;
  int64_t baseline_span_ms = 0;
  std::string trigger;
  std::string pad_line;
  // Tracer state to restore on disable.
  SavedTracerState saved;
  bool saved_ok = false;
};

// Everything below runs under this lock; the critical section is tiny except
// the 500 ms baseline sample and capture open/close (a map snapshot each).
inline std::mutex& lock() {
  static std::mutex m;
  return m;
}

inline State& st() {
  static State* s = new State;  // leaked on purpose (stable address)
  return *s;
}

inline int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

inline std::FILE* report_file() {
  static std::FILE* f = [] {
#ifdef _WIN32
    std::FILE* out = nullptr;
    if (::fopen_s(&out, "fable2_attack_probe.log", "a") != 0) out = nullptr;
    return out;
#else
    return std::fopen("fable2_attack_probe.log", "a");
#endif
  }();
  return f;
}

// Writes a bracketing marker into the tracer's sequential log (same FILE* and
// same lock as the traced lines, so markers interleave cleanly).
inline void trace_marker(const std::string& text) {
  std::lock_guard<std::mutex> l(fable2::functrace::file_lock());
  if (std::FILE* f = fable2::functrace::file()) {
    std::fprintf(f, "===== ATTACK-PROBE %s =====\n", text.c_str());
    std::fflush(f);
  }
}

// Merged tracer call counts (name -> cumulative calls). A copy of the
// tracer's own merge (fable2_func_trace.h's write_summary); kept here so the
// tracer header - force-included into the recomp target's PCH - stays
// untouched (any edit to it would force a full recompile of the generated
// guest code). Requires tracing to be enabled (counts are only accumulated
// then).
inline void tracer_counts(CountMap& out) {
  out.clear();
  std::vector<fable2::functrace::ThreadState*> snapshot;
  {
    std::lock_guard<std::mutex> rl(fable2::functrace::registry_lock());
    snapshot = fable2::functrace::registry();
    out = fable2::functrace::global_counts();
  }
  for (fable2::functrace::ThreadState* t : snapshot) {
    if (t->dead.load(std::memory_order_acquire)) continue;  // already folded
    std::lock_guard<std::mutex> tl(t->m);
    for (auto& kv : t->counts) out[kv.first] += kv.second;
  }
}

// --- Enable/disable ---------------------------------------------------------

// Closes the capture window and writes the report. Caller holds lock()
// (defined below; used by deactivate_locked).
inline void end_capture_locked(State& s, int64_t now);

// Takes over the tracer: counts ON (baseline + snapshots need them),
// sequential log OFF (capture windows turn it on), no filters. Caller holds
// lock().
inline void activate_locked(State& s) {
  if (s.active) return;
  if (!s.saved_ok) {
    s.saved = {fable2::functrace::enabled().load(std::memory_order_relaxed),
               fable2::functrace::summary_enabled().load(std::memory_order_relaxed),
               fable2::functrace::trace_log_enabled().load(std::memory_order_relaxed),
               fable2::functrace::subs_only().load(std::memory_order_relaxed),
               fable2::functrace::filter()};
    s.saved_ok = true;
  }
  load_env_config(s.cfg);
  Fable2FuncTraceSetEnabled(true);
  Fable2FuncTraceSetSummaryEnabled(true);
  Fable2FuncTraceSetLogEnabled(false);
  Fable2FuncTraceSetSubsOnly(false);
  Fable2FuncTraceSetFilter("");
  s.last_cum.clear();
  s.last_cum_ms = now_ms();
  s.deltas.clear();
  s.active = true;
  if (std::FILE* f = report_file()) {
    std::fprintf(
        f,
        "\n# fable2_attack_probe enabled: watch=%s window=%dms "
        "baseline=%dms sample=%dms cooldown=%dms\n",
        s.cfg.watch.names.c_str(), s.cfg.window_ms, s.cfg.baseline_ms,
        s.cfg.sample_ms, s.cfg.cooldown_ms);
    std::fflush(f);
  }
}

// Puts the tracer back the way the probe found it. Caller holds lock().
inline void deactivate_locked(State& s) {
  if (!s.active) return;
  if (s.capturing) end_capture_locked(s, now_ms());
  Fable2FuncTraceSetEnabled(s.saved.enabled);
  Fable2FuncTraceSetSummaryEnabled(s.saved.summary);
  Fable2FuncTraceSetLogEnabled(s.saved.log);
  Fable2FuncTraceSetSubsOnly(s.saved.subs_only);
  Fable2FuncTraceSetFilter(s.saved.filter.c_str());
  s.saved_ok = false;
  s.active = false;
}

inline void set_enabled(bool on) {
  std::lock_guard<std::mutex> l(lock());
  State& s = st();
  if (on) {
    activate_locked(s);
  } else {
    deactivate_locked(s);
  }
  enabled().store(on, std::memory_order_relaxed);
}

// --- Baseline ----------------------------------------------------------------

// One baseline tick: snapshot the tracer's cumulative counts, keep only the
// delta from the previous snapshot. Caller holds lock().
inline void sample_baseline_locked(State& s, int64_t now) {
  CountMap cum;
  tracer_counts(cum);
  BaselineDelta d;
  d.t0_ms = s.last_cum_ms != 0 ? s.last_cum_ms : now;
  d.t1_ms = now;
  d.calls.reserve(cum.size() / 2);
  for (auto& kv : cum) {
    uint64_t prev = 0;
    if (auto it = s.last_cum.find(kv.first); it != s.last_cum.end())
      prev = it->second;
    if (kv.second > prev) d.calls.emplace(kv.first, kv.second - prev);
  }
  s.last_cum = std::move(cum);
  s.last_cum_ms = now;
  if (!d.calls.empty()) s.deltas.push_back(std::move(d));
  while (!s.deltas.empty() &&
         now - s.deltas.front().t0_ms > s.cfg.baseline_ms)
    s.deltas.pop_front();
}

// --- Capture ------------------------------------------------------------------

// Starts a capture window. Caller holds lock().
inline void begin_capture_locked(State& s, int64_t now, const std::string& trigger,
                                 const std::string& pad_line) {
  s.capture_n += 1;
  s.capturing = true;
  s.capture_start_ms = now;
  s.capture_end_ms = now + s.cfg.window_ms;
  s.trigger = trigger;
  s.pad_line = pad_line;
  // Pre-press baseline: sum the rolling deltas (consecutive, so the sum
  // telescopes to the net call count over the span).
  s.baseline.clear();
  s.baseline_span_ms = 0;
  for (BaselineDelta& d : s.deltas) {
    s.baseline_span_ms += d.t1_ms - d.t0_ms;
    for (auto& kv : d.calls) s.baseline[kv.first] += kv.second;
  }
  tracer_counts(s.snap_before);
  Fable2FuncTraceSetLogEnabled(true);  // sequential trace for the window
  trace_marker(std::string("CAPTURE #") + std::to_string(s.capture_n) +
               " START trigger=" + s.trigger + " t=" + std::to_string(now));
  if (std::FILE* f = report_file()) {
    std::fprintf(f, "\n==== CAPTURE #%d  t=%lld (steady ms) ====\n",
                 s.capture_n, static_cast<long long>(now));
    std::fprintf(f, "trigger: %s\n", s.trigger.c_str());
    std::fprintf(f, "%s", s.pad_line.c_str());
    std::fflush(f);
  }
}

// Closes the capture window and writes the report. Caller holds lock().
inline void end_capture_locked(State& s, int64_t now) {
  if (!s.capturing) return;
  s.capturing = false;
  s.cooldown_until_ms = now + s.cfg.cooldown_ms;
  const int64_t window_ms = now - s.capture_start_ms;
  const int n = s.capture_n;
  Fable2FuncTraceFlush();  // commit the calling thread's in-flight run
  CountMap after;
  tracer_counts(after);
  Fable2FuncTraceSetLogEnabled(false);

  struct Row {
    uint64_t c;           // calls in the capture window
    double b_scaled;      // baseline calls, rate-scaled to the window length
    std::string name;
  };
  std::vector<Row> nw;    // NEW: 0 baseline calls
  std::vector<Row> inc;   // INCREASED above the baseline rate
  std::vector<Row> all;   // everything called in the window
  uint64_t total_calls = 0;
  for (auto& kv : after) {
    const uint64_t ca = [&] {
      auto it = s.snap_before.find(kv.first);
      return it != s.snap_before.end() ? it->second : 0;
    }();
    const uint64_t c = kv.second >= ca ? kv.second - ca : 0;
    if (c == 0) continue;
    total_calls += c;
    const uint64_t b = [&] {
      auto it = s.baseline.find(kv.first);
      return it != s.baseline.end() ? it->second : 0;
    }();
    const double b_scaled =
        s.baseline_span_ms > 0
            ? static_cast<double>(b) *
                  static_cast<double>(window_ms) /
                  static_cast<double>(s.baseline_span_ms)
            : static_cast<double>(b);
    if (b == 0) {
      nw.push_back({c, 0.0, kv.first});
    } else if (static_cast<double>(c) > b_scaled + 0.5) {
      inc.push_back({c, b_scaled, kv.first});
    }
    all.push_back({c, b_scaled, kv.first});
  }
  std::sort(nw.begin(), nw.end(), [](const Row& a, const Row& b) {
    if (a.c != b.c) return a.c > b.c;
    return a.name < b.name;
  });
  std::sort(inc.begin(), inc.end(), [](const Row& a, const Row& b) {
    const double ra = a.b_scaled > 0 ? static_cast<double>(a.c) / a.b_scaled
                                     : 1e9;
    const double rb = b.b_scaled > 0 ? static_cast<double>(b.c) / b.b_scaled
                                     : 1e9;
    if (ra != rb) return ra > rb;
    if (a.c != b.c) return a.c > b.c;
    return a.name < b.name;
  });
  std::sort(all.begin(), all.end(), [](const Row& a, const Row& b) {
    if (a.c != b.c) return a.c > b.c;
    return a.name < b.name;
  });

  if (std::FILE* f = report_file()) {
    std::fprintf(f, "window: %lld ms   baseline: %lld ms\n",
                 static_cast<long long>(window_ms),
                 static_cast<long long>(s.baseline_span_ms));
    std::fprintf(f, "calls in window: %llu across %zu functions\n",
                 static_cast<unsigned long long>(total_calls), all.size());
    std::fprintf(
        f,
        "\n--- NEW: called in window, never in the pre-press baseline - %zu ---\n",
        nw.size());
    for (size_t i = 0; i < nw.size() && i < static_cast<size_t>(s.cfg.top); ++i)
      std::fprintf(f, " %llu x %s\n", static_cast<unsigned long long>(nw[i].c),
                   nw[i].name.c_str());
    std::fprintf(
        f,
        "\n--- INCREASED: window count above the baseline rate - %zu ---\n",
        inc.size());
    for (size_t i = 0; i < inc.size() && i < static_cast<size_t>(s.cfg.top); ++i)
      std::fprintf(f, " %llu x %s    (baseline-scaled %.1f)\n",
                   static_cast<unsigned long long>(inc[i].c), inc[i].name.c_str(),
                   inc[i].b_scaled);
    const size_t topn =
        std::min(static_cast<size_t>(s.cfg.top), all.size());
    std::fprintf(f,
                 "\n--- TOP: most called functions in the window - top %zu "
                 "of %zu ---\n",
                 topn, all.size());
    for (size_t i = 0; i < topn; ++i)
      std::fprintf(f, " %llu x %s\n", static_cast<unsigned long long>(all[i].c),
                   all[i].name.c_str());
    std::fflush(f);
  }
  trace_marker(std::string("CAPTURE #") + std::to_string(n) +
               " END window=" + std::to_string(window_ms) +
               " ms new=" + std::to_string(nw.size()) +
               " increased=" + std::to_string(inc.size()));
}

// --- Observation --------------------------------------------------------------

// Shared tail of the two observe sources: end an in-flight window past its
// deadline, tick the baseline, and start a capture on a fresh rising edge.
// Caller holds lock().
inline void maybe_begin_capture_locked(State& s, int64_t now, uint16_t edge,
                                       bool edge_lt, bool edge_rt,
                                       uint16_t buttons, uint8_t lt,
                                       uint8_t rt, int16_t lx, int16_t ly,
                                       int16_t rx, int16_t ry) {
  if (s.capturing) {
    if (now >= s.capture_end_ms) end_capture_locked(s, now);
    return;
  }
  if (now - s.last_cum_ms >= s.cfg.sample_ms) sample_baseline_locked(s, now);
  if ((edge != 0 || edge_lt || edge_rt) && now >= s.cooldown_until_ms) {
    std::string t;
    auto add_name = [&](uint16_t mask, const char* name) {
      if (edge & mask) {
        if (!t.empty()) t += "+";
        t += name;
      }
    };
    add_name(0x4000u, "X");
    add_name(0x8000u, "Y");
    add_name(0x1000u, "A");
    add_name(0x2000u, "B");
    add_name(0x0100u, "LB");
    add_name(0x0200u, "RB");
    add_name(0x0001u, "Up");
    add_name(0x0002u, "Down");
    add_name(0x0004u, "Left");
    add_name(0x0008u, "Right");
    add_name(0x0040u, "L3");
    add_name(0x0080u, "R3");
    if (edge_lt) {
      if (!t.empty()) t += "+";
      t += "LT";
    }
    if (edge_rt) {
      if (!t.empty()) t += "+";
      t += "RT";
    }
    char pad[128];
    std::snprintf(pad, sizeof pad,
                  "pad state at press: btns=0x%04X lt=%u rt=%u "
                  "lx=%d ly=%d rx=%d ry=%d\n",
                  buttons, lt, rt, lx, ly, rx, ry);
    begin_capture_locked(s, now, t.empty() ? "manual" : t, pad);
  }
}

// Feeds the buttons the KEYBOARD/MOUSE driver contributes (its own state, not
// the merged one). A press from a host key is detected even if the guest
// input wrapper is not observed. No-op unless the probe is enabled.
inline void observe_keyboard(uint16_t buttons, uint8_t lt, uint8_t rt,
                             int16_t lx, int16_t ly, int16_t rx, int16_t ry) {
  if (!enabled().load(std::memory_order_relaxed)) return;
  std::lock_guard<std::mutex> l(lock());
  State& s = st();
  if (!s.active) activate_locked(s);
  const int64_t now = now_ms();
  const bool lt_on = lt > 0;
  const bool rt_on = rt > 0;
  const uint16_t edge =
      static_cast<uint16_t>(buttons & ~s.prev_kbd.buttons & s.cfg.watch.buttons);
  const bool edge_lt = s.cfg.watch.lt && lt_on && !s.prev_kbd.lt;
  const bool edge_rt = s.cfg.watch.rt && rt_on && !s.prev_kbd.rt;
  s.prev_kbd = {buttons, lt_on, rt_on};
  maybe_begin_capture_locked(s, now, edge, edge_lt, edge_rt, buttons, lt, rt,
                             lx, ly, rx, ry);
}

// Feeds the FINAL merged pad state read from guest memory. `base` is the
// arena base the hook's PPCContext carries (== the guest arena); `state_ptr`
// is the X_INPUT_STATE the game passes to XamInputGetState. Layout (SDK
// input.h, big-endian fields): u32 packet @0, gamepad @4 = { u16 buttons;
// u8 left_trigger; u8 right_trigger; i16 lx; i16 ly; i16 rx; i16 ry }.
inline void observe_state(const uint8_t* base, uint32_t state_ptr) {
  if (!enabled().load(std::memory_order_relaxed)) return;
  uint8_t buf[12] = {};
  if (!detail::safe_read(base + state_ptr + 4, buf, sizeof buf)) return;
  const uint16_t buttons = static_cast<uint16_t>((buf[0] << 8) | buf[1]);
  const int16_t lx = static_cast<int16_t>(static_cast<uint16_t>((buf[4] << 8) | buf[5]));
  const int16_t ly = static_cast<int16_t>(static_cast<uint16_t>((buf[6] << 8) | buf[7]));
  const int16_t rx = static_cast<int16_t>(static_cast<uint16_t>((buf[8] << 8) | buf[9]));
  const int16_t ry = static_cast<int16_t>(static_cast<uint16_t>((buf[10] << 8) | buf[11]));
  // Full merged-state observation (rising edges against the last merged
  // state; a physical-pad press is seen here even though the keyboard
  // source reports 0).
  std::lock_guard<std::mutex> l(lock());
  State& s = st();
  if (!s.active) activate_locked(s);
  const int64_t now = now_ms();
  const bool lt_on = buf[2] > 0;
  const bool rt_on = buf[3] > 0;
  const uint16_t edge =
      static_cast<uint16_t>(buttons & ~s.prev_pad.buttons & s.cfg.watch.buttons);
  const bool edge_lt = s.cfg.watch.lt && lt_on && !s.prev_pad.lt;
  const bool edge_rt = s.cfg.watch.rt && rt_on && !s.prev_pad.rt;
  s.prev_pad = {buttons, lt_on, rt_on};
  maybe_begin_capture_locked(s, now, edge, edge_lt, edge_rt, buttons, buf[2],
                             buf[3], lx, ly, rx, ry);
}

// Manual capture: starts a window immediately, no press required (use it to
// capture whatever you do during the window).
inline void capture_now() {
  if (!enabled().load(std::memory_order_relaxed)) return;
  std::lock_guard<std::mutex> l(lock());
  State& s = st();
  if (!s.active) activate_locked(s);
  const int64_t now = now_ms();
  if (s.capturing || now < s.cooldown_until_ms) return;
  begin_capture_locked(s, now, "manual",
                       "manual capture (Fable2AttackProbeCaptureNow)\n");
}

// Runtime config setters (take effect immediately; the env vars are read once,
// when the probe first activates).
inline void set_buttons(const char* names) {
  std::lock_guard<std::mutex> l(lock());
  parse_watch(names ? std::string_view(names) : std::string_view(),
              st().cfg.watch);
}
inline void set_window_ms(int ms) {
  std::lock_guard<std::mutex> l(lock());
  st().cfg.window_ms = std::clamp(ms, 20, 30000);
}
inline void set_baseline_ms(int ms) {
  std::lock_guard<std::mutex> l(lock());
  st().cfg.baseline_ms = std::clamp(ms, 1000, 60000);
}

}  // namespace attackprobe
}  // namespace fable2

// --- Runtime API (extern "C") -------------------------------------------------

extern "C" {

inline void Fable2AttackProbeSetEnabled(bool on) {
  fable2::attackprobe::set_enabled(on);
}

// Fed by the keyboard/mouse driver (src/input/keyboard_gamepad.h) with the
// buttons it contributes; no-op unless the probe is enabled.
inline void Fable2AttackProbeObserve(uint16_t buttons, uint8_t lt, uint8_t rt,
                                     int16_t lx, int16_t ly, int16_t rx,
                                     int16_t ry) {
  fable2::attackprobe::observe_keyboard(buttons, lt, rt, lx, ly, rx, ry);
}

inline void Fable2AttackProbeSetButtons(const char* names) {
  fable2::attackprobe::set_buttons(names);
}
inline void Fable2AttackProbeSetWindowMs(int ms) {
  fable2::attackprobe::set_window_ms(ms);
}
inline void Fable2AttackProbeSetBaselineMs(int ms) {
  fable2::attackprobe::set_baseline_ms(ms);
}
inline void Fable2AttackProbeCaptureNow() {
  fable2::attackprobe::capture_now();
}

}  // extern "C"

// --- Guest hook ----------------------------------------------------------------
//
// The guest's XamInputGetState wrapper: it stores the state pointer in r5,
// sets user index 1 in r4, and calls __imp__XamInputGetState. Observing AFTER
// the original body means the merged pad state (keyboard + physical + remote)
// is already in the guest buffer. In Debug builds (FABLE2_REMOTE_CONTROL)
// fable2_state_probe.h owns this override and forwards to observe_state()
// too, so the hook is only defined here for the other configurations.
#ifndef FABLE2_REMOTE_CONTROL
extern "C" void __imp__GetUser1InputState_822B2D60(PPCContext&, uint8_t*);
extern "C" void GetUser1InputState_822B2D60(PPCContext& ctx, uint8_t* base) {
  const uint32_t state_ptr = ctx.r4.u32;
  __imp__GetUser1InputState_822B2D60(ctx, base);
  fable2::attackprobe::observe_state(base, state_ptr);
}
#endif  // !FABLE2_REMOTE_CONTROL
