// fable2_func_trace.h - guest function-call tracing.
//
// How it hooks in: CMakeLists.txt appends this header to the fable_2_recomp
// target's precompile-header list AFTER the generated pch, so when this
// header is processed, the generated fable_2_pch.h (which defines
// REX_FUNC_PROLOGUE()) is already loaded, and the section at the bottom of
// this file redefines that macro. Rexglue codegen emits REX_FUNC_PROLOGUE()
// at the top of every recompiled guest function, so every guest function
// entry then calls Fable2FuncTraceCall(__func__) and its name is logged to
// fable2_func_trace.log (CWD = exe dir, next to logs/).
//
// Consecutive calls of the same function are run-length encoded per thread,
// so a tight loop reads as one line with a count:
//
//   GetNewGameLoadingGlobal
//   sub_82189708 x 4821
//   LoadingScreen_Virtual43
//   sub_82CC1BC0 x 3
//   ...
//
// No generated files are modified, so `rexglue codegen` re-runs never lose
// the hook.
//
// Off by default. Enable with:
//   FABLE2_FUNC_TRACE=1             before launch (env var)
//   Fable2FuncTraceSetEnabled(true) at runtime (e.g. from a named-function
//                                   override in src/, to trace a window)
// Optional substring filter (only log names containing it):
//   FABLE2_FUNC_TRACE_FILTER=LoadingScreen    (env var)
//   Fable2FuncTraceSetFilter("LoadingScreen") (runtime, "" = everything)
// Output toggles (both on by default; set 0 to produce just the other file):
//   FABLE2_FUNC_TRACE_LOG=0        no fable2_func_trace.log (summary only)
//   FABLE2_FUNC_TRACE_SUMMARY=0    no fable2_func_summary.log (trace only)
//
// Deliberately a lightweight dedicated log (same pattern as fps_probe.log),
// not the SDK spdlog logger: at Fable 2's call rate, per-call spdlog
// formatting is far too slow and would flood logs/.

#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fable2::functrace {

// The flag every guest function entry reads. Namespace-scope with a constant
// initializer, so the check is one relaxed load: a function-local static with
// a dynamic initializer would add a thread-safe-init guard check (a TLS access
// with the MSVC ABI) to millions of guest calls per second.
inline std::atomic<bool> g_enabled{false};

// FABLE2_FUNC_TRACE=1 is applied once, during static initialization.
inline const bool g_enabled_from_env = [] {
#ifdef _WIN32
  char v[8] = {};
  size_t n = 0;
  const bool on = ::getenv_s(&n, v, sizeof(v), "FABLE2_FUNC_TRACE") == 0 &&
                  v[0] == '1';
#else
  const char* v = std::getenv("FABLE2_FUNC_TRACE");
  const bool on = v != nullptr && v[0] == '1';
#endif
  if (on) g_enabled.store(true, std::memory_order_relaxed);
  return on;
}();

inline std::atomic<bool>& enabled() { return g_enabled; }

inline std::string& filter() {
  static std::string f = [] {
#ifdef _WIN32
    char v[256] = {};
    size_t n = 0;
    if (::getenv_s(&n, v, sizeof(v), "FABLE2_FUNC_TRACE_FILTER") != 0)
      return std::string();
    return std::string(v);
#else
    const char* v = std::getenv("FABLE2_FUNC_TRACE_FILTER");
    return std::string(v ? v : "");
#endif
  }();
  return f;
}

inline void set_enabled(bool on) {
  enabled().store(on, std::memory_order_relaxed);
}

// "Subs only" mode (FABLE2_FUNC_TRACE_SUBS_ONLY=1): log just the unnamed
// guest functions (sub_<hex address>) and drop everything else - named
// functions, __savegprlr_*/__restgprlr_*/__savevmx_* register helpers,
// xstart, ... For the naming workflow: it leaves only the functions that
// still need names. Composes with the substring filter (e.g. FILTER=82B9
// narrows to one address range).
inline std::atomic<bool>& subs_only() {
  static std::atomic<bool> s{[] {
#ifdef _WIN32
    char v[8] = {};
    size_t n = 0;
    return ::getenv_s(&n, v, sizeof(v), "FABLE2_FUNC_TRACE_SUBS_ONLY") == 0 &&
           v[0] == '1';
#else
    const char* v = std::getenv("FABLE2_FUNC_TRACE_SUBS_ONLY");
    return v != nullptr && v[0] == '1';
#endif
  }()};
  return s;
}

inline void set_subs_only(bool on) {
  subs_only().store(on, std::memory_order_relaxed);
}

// Output toggles (default on when tracing is enabled): "0" disables the
// sequential trace log, "0" disables the periodic summary file. The rest of
// the string is ignored ("1" = on).
namespace detail {
inline bool env_toggle_on(const char* var) {
#ifdef _WIN32
  char v[8] = {};
  size_t n = 0;
  if (::getenv_s(&n, v, sizeof(v), var) != 0) return true;
  return v[0] != '0';
#else
  const char* v = std::getenv(var);
  return v == nullptr || v[0] != '0';
#endif
}
}  // namespace detail

inline std::atomic<bool>& trace_log_enabled() {
  static std::atomic<bool> t{detail::env_toggle_on("FABLE2_FUNC_TRACE_LOG")};
  return t;
}

inline std::atomic<bool>& summary_enabled() {
  static std::atomic<bool> s{
      detail::env_toggle_on("FABLE2_FUNC_TRACE_SUMMARY")};
  return s;
}

inline void set_trace_log_enabled(bool on) {
  trace_log_enabled().store(on, std::memory_order_relaxed);
}

inline void set_summary_enabled(bool on) {
  summary_enabled().store(on, std::memory_order_relaxed);
}

// Matches the codegen naming convention for unnamed guest functions:
// sub_ followed by one or more hex digits (e.g. sub_82CC1BC0).
inline bool is_sub_name(const char* name) {
  if (std::strncmp(name, "sub_", 4) != 0) return false;
  const char* p = name + 4;
  if (*p == '\0') return false;
  for (; *p; ++p) {
    char c = *p;
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
          (c >= 'A' && c <= 'F')))
      return false;
  }
  return true;
}

inline std::mutex& file_lock() {
  static std::mutex m;
  return m;
}

inline std::FILE* file() {
  static std::FILE* f = [] {
#ifdef _WIN32
    std::FILE* out = nullptr;
    if (::fopen_s(&out, "fable2_func_trace.log", "a") != 0) out = nullptr;
    return out;
#else
    return std::fopen("fable2_func_trace.log", "a");
#endif
  }();
  return f;
}

// Per-thread tracing state: the in-flight run (same name repeating
// back-to-back), the pending log lines buffered for disk, and the session
// call counters (name -> times called) for the summary file.
struct ThreadState {
  std::string last;    // name of the in-flight run
  const char* lastPtr = nullptr;  // original name pointer of the in-flight run
  uint64_t run = 0;    // how many times in a row (0 = no run in flight)
  std::string buf;     // finished runs, waiting to be written
  std::mutex m;        // guards counts (the summary sweeper snapshots it)
  std::unordered_map<std::string, uint64_t> counts;
  std::atomic<bool> dead{false};
};

// Global counters for threads that have already exited (their per-thread
// map is folded in here) + the registry of all ThreadState pointers.
// ThreadState objects are heap-allocated and deliberately never freed, so
// registry pointers stay valid for the sweeper even after a thread exits.
inline std::mutex& registry_lock() {
  static std::mutex m;
  return m;
}

inline std::vector<ThreadState*>& registry() {
  static std::vector<ThreadState*> v;
  return v;
}

inline std::unordered_map<std::string, uint64_t>& global_counts() {
  static std::unordered_map<std::string, uint64_t> m;
  return m;
}

namespace detail {
struct Unreg {
  ThreadState* ts;
  ~Unreg() {
    // Thread is exiting: fold its counters (plus any uncommitted in-flight
    // run) into the global map so the summary keeps them, then mark the
    // state dead (it stays registered, but write_summary() skips dead
    // entries).
    {
      std::lock_guard<std::mutex> l(ts->m);
      auto& g = global_counts();
      if (ts->run > 0) g[ts->last] += ts->run;
      for (auto& kv : ts->counts) g[kv.first] += kv.second;
      ts->counts.clear();
      ts->run = 0;
    }
    ts->dead.store(true, std::memory_order_release);
  }
};
}  // namespace detail

inline ThreadState& state() {
  static thread_local ThreadState* ts = [] {
    auto* t = new ThreadState;  // leaked on purpose
    t->counts.reserve(1024);
    // Register so the sweeper can snapshot this thread's counters. Stays in
    // the registry after exit (dead flag set by Unreg's destructor).
    std::lock_guard<std::mutex> l(registry_lock());
    registry().push_back(t);
    return t;
  }();
  static thread_local detail::Unreg unreg{ts};
  return *ts;
}

// Writes the finished runs buffer to disk.
inline void flush_buffer(std::string& buf) {
  if (buf.empty()) return;
  std::lock_guard<std::mutex> l(file_lock());
  if (std::FILE* out = file()) std::fwrite(buf.data(), 1, buf.size(), out);
  buf.clear();
}

// Finalizes the in-flight run: folds its N calls into the per-thread summary
// map (one map touch per run, not per call) and appends one "name x N" line
// to the pending log buffer. Runs the per-thread lock only when a summary
// count update is actually needed.
inline void commit_run(ThreadState& ts, bool want_summary, bool want_log) {
  if (ts.run == 0) return;
  if (want_summary) {
    std::lock_guard<std::mutex> l(ts.m);
    auto it = ts.counts.find(ts.last);
    if (it == ts.counts.end())
      ts.counts.emplace(ts.last, ts.run);
    else
      it->second += ts.run;
  }
  if (want_log) {
    ts.buf.append(ts.last);
    if (ts.run > 1) {
      ts.buf.append(" x ");
      ts.buf.append(std::to_string(ts.run));
    }
    ts.buf.push_back('\n');
  }
  ts.run = 0;
}

// ---------------------------------------------------------------------------
// Session summary (fable2_func_summary.log)
//
// Every traced call is also counted per function. A background sweeper
// thread (started on the first traced call) rewrites the summary every 5 s:
// one "N x name" line per function, sorted by count (highest first), so the
// file is a near-live overview of what's being called. The periodic rewrite
// means the file is at most ~5 s stale even when the game exits through
// ExitProcess (which skips exit handlers); a final snapshot is written when
// the process exits normally (a late-constructed static's destructor, which
// is order-safe unlike a std::atexit handler).
// ---------------------------------------------------------------------------

// Merges the global counters + every live thread's counters and writes the
// summary file. Safe to call from any thread.
inline void write_summary() {
  std::unordered_map<std::string, uint64_t> merged;
  std::vector<ThreadState*> snapshot;
  {
    std::lock_guard<std::mutex> rl(registry_lock());
    snapshot = registry();
    merged = global_counts();
  }
  for (ThreadState* t : snapshot) {
    if (t->dead.load(std::memory_order_acquire)) continue;  // already folded
    std::lock_guard<std::mutex> tl(t->m);
    for (auto& kv : t->counts) merged[kv.first] += kv.second;
  }
  if (merged.empty()) return;

  std::vector<std::pair<uint64_t, std::string>> rows;
  rows.reserve(merged.size());
  for (auto& kv : merged) rows.emplace_back(kv.second, std::move(kv.first));
  std::sort(rows.begin(), rows.end(), [](auto& a, auto& b) {
    if (a.first != b.first) return a.first > b.first;
    return a.second < b.second;
  });

  std::string text;
  for (auto& r : rows) {
    text.append(std::to_string(r.first));
    text.append(" x ");
    text.append(r.second);
    text.push_back('\n');
  }
  std::lock_guard<std::mutex> fl(file_lock());
#ifdef _WIN32
  std::FILE* out = nullptr;
  if (::fopen_s(&out, "fable2_func_summary.log", "w") != 0) return;
  std::fwrite(text.data(), 1, text.size(), out);
  std::fclose(out);
#else
  if (std::FILE* out = std::fopen("fable2_func_summary.log", "w")) {
    std::fwrite(text.data(), 1, text.size(), out);
    std::fclose(out);
  }
#endif
}

inline void sweeper_loop() {
  for (;;) {
    std::this_thread::sleep_for(std::chrono::seconds(5));
    write_summary();
  }
}

// Final-summary sentinel: constructed on the first traced call (AFTER the
// registry/global-counts statics, which exist by then), so its destructor
// runs BEFORE theirs (reverse construction order). That guarantees the
// final write_summary() still finds live statics - unlike a std::atexit
// handler, which can run after the tracing statics are already destroyed.
struct final_summary_guard {
  ~final_summary_guard() { write_summary(); }
};

// Starts the sweeper exactly once (on the first traced call).
inline void ensure_sweeper() {
  static std::atomic<bool> started{false};
  bool expected = false;
  if (started.compare_exchange_strong(expected, true)) {
    // Touch every static write_summary() needs so it is constructed NOW
    // (on the first traced call), which also guarantees it is destroyed
    // AFTER the guard below (statics die in reverse construction order).
    (void)registry_lock();
    (void)registry();
    (void)global_counts();
    std::thread(sweeper_loop).detach();
    static final_summary_guard guard;  // destructor = final summary write
  }
}

// Tracing is on: a handful of atomic loads when every output is off. The
// common case (same function entered again) is a single pointer compare +
// counter bump; the per-thread summary map is touched once per RUN of
// identical calls, and finished runs flush to disk every 8 KB. Kept out of
// line so the per-call hook below stays one inlined load and branch.
[[gnu::noinline]] inline void trace_enabled(const char* name) {
  const bool want_summary = summary_enabled().load(std::memory_order_relaxed);
  const bool want_log = trace_log_enabled().load(std::memory_order_relaxed);
  if (want_summary) ensure_sweeper();
  // The function body lives in __imp__<name> (name is a weak alias), so
  // __func__ carries the __imp__ prefix - strip it for readable logs.
  if (std::strncmp(name, "__imp__", 7) == 0) name += 7;
  const std::string& f = filter();
  if (!f.empty() && std::strstr(name, f.c_str()) == nullptr) return;
  if (subs_only().load(std::memory_order_relaxed) && !is_sub_name(name))
    return;

  if (!want_summary && !want_log) return;

  ThreadState& ts = state();
  // Fast path: the same function entered again. name is the compile-time
  // __func__ literal, so the pointer is identical for the same function -
  // extend the in-flight run with a single pointer compare (no lock, no
  // map, no string copy) and get out.
  if (ts.run > 0 && (ts.lastPtr == name || ts.last == name)) {
    ts.run++;
    return;
  }
  // Name changed: commit the finished run (one locked map update per run,
  // not per call), then start a new one.
  commit_run(ts, want_summary, want_log);
  ts.last = name;
  ts.lastPtr = name;
  ts.run = 1;
  if (want_log && ts.buf.size() >= 8192) flush_buffer(ts.buf);
}

// Per-call hook: off by default, and then just this load and branch inlined
// into each recompiled function.
[[gnu::always_inline]] inline void trace(const char* name) {
  if (__builtin_expect(g_enabled.load(std::memory_order_relaxed), 0))
    trace_enabled(name);
}

// Finalizes the calling thread's in-flight run and writes everything to disk.
inline void flush() {
  ThreadState& ts = state();
  commit_run(ts, summary_enabled().load(std::memory_order_relaxed),
             trace_log_enabled().load(std::memory_order_relaxed));
  flush_buffer(ts.buf);
}

}  // namespace fable2::functrace

extern "C" {

// Called by the (redefined) REX_FUNC_PROLOGUE() in every recompiled function.
[[gnu::always_inline]] inline void Fable2FuncTraceCall(const char* name) {
  fable2::functrace::trace(name);
}

// Runtime toggle (e.g. enable right before the event you want to trace).
inline void Fable2FuncTraceSetEnabled(bool on) {
  fable2::functrace::set_enabled(on);
}

// Substring filter: only names containing `substr` are logged ("" = all).
// Not atomic; set it while the game is paused or before enabling tracing.
inline void Fable2FuncTraceSetFilter(const char* substr) {
  fable2::functrace::filter() = substr ? substr : "";
}

// "Subs only" mode: log only sub_<hex> (unnamed guest) functions.
inline void Fable2FuncTraceSetSubsOnly(bool on) {
  fable2::functrace::set_subs_only(on);
}

// Output toggles: sequential fable2_func_trace.log and periodic
// fable2_func_summary.log, each independently on/off (default on).
inline void Fable2FuncTraceSetLogEnabled(bool on) {
  fable2::functrace::set_trace_log_enabled(on);
}

inline void Fable2FuncTraceSetSummaryEnabled(bool on) {
  fable2::functrace::set_summary_enabled(on);
}

// Finalize the CALLING thread's in-flight run and write it to disk
// (the 8 KB auto-flush normally keeps the file fresh; call this right
// before turning tracing off so the last run isn't lost).
inline void Fable2FuncTraceFlush() {
  fable2::functrace::flush();
}

}  // extern "C"

// ---------------------------------------------------------------------------
// The hook. REX_CONFIG_H_INCLUDED is defined by the generated fable_2_pch.h;
// it being set means the pch was loaded BEFORE this header (this header is
// appended to the target's precompile-header list after the pch), so we can
// re-define the prologue macro codegen emits at the top of every recompiled
// function. Mirrors the pch's own variant structure (clang is this project's
// toolchain).
// ---------------------------------------------------------------------------
#if defined(REX_CONFIG_H_INCLUDED) && !defined(FABLE2_FUNC_TRACE_HOOKED)
#define FABLE2_FUNC_TRACE_HOOKED
#undef REX_FUNC_PROLOGUE
#if defined(__clang__)
#define REX_FUNC_PROLOGUE()                                            \
  __builtin_assume(((size_t)base & 0x1F) == 0),                        \
      Fable2FuncTraceCall(__func__)
#elif defined(__GNUC__)
#define REX_FUNC_PROLOGUE()                                            \
  do {                                                                 \
    if (((size_t)base & 0x1F) != 0) __builtin_unreachable();           \
    Fable2FuncTraceCall(__func__);                                     \
  } while (0)
#else
#define REX_FUNC_PROLOGUE() Fable2FuncTraceCall(__func__)
#endif
#endif
