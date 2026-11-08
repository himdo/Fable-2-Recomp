// fable2_ui_input_probe.h - capture the INPUTS of candidate UI-render
// functions to find which one actually renders the "Press <a_img> to start"
// prompt (and what data it consumes).
//
// Driven from fable2::uir::hook() (every hooked pipeline function passes
// through here), so no extra hook sites are needed.
//
// Two mechanisms, active only inside a time window from the first hook call:
//
//   1. STRING HUNT: every call scans r3..r10. Each register value - and the
//      first 8 words it points at (one indirection, for string objects) - is
//      searched for a UTF-16BE byte pattern (default "Press" =
//      00500072006500730073). A hit logs the full GPR/FPR register state,
//      the link register (=> the CALLER), and the decoded string, which pins
//      the exact function + call site that carries the prompt.
//
//   2. FULL INPUT DUMP (FABLE2_UIR_IN_DUMP=fn1,fn2): for the named functions,
//      log r3..r10, lr, f1..f8 plus the first 16 words of the object in r3 on
//      every call, so per-frame input changes (e.g. the alpha/visibility
//      field that drives the blink) can be watched over time.
//
// Env vars:
//   FABLE2_UIR_IN=1                 master enable (default off)
//   FABLE2_UIR_IN_DELAY=<sec>       window start (default 33)
//   FABLE2_UIR_IN_DUR=<sec>         window length (default 12)
//   FABLE2_UIR_IN_PAT=<hex>         byte pattern to hunt (default "Press")
//   FABLE2_UIR_IN_DUMP=a,b          full-input dump for these functions
//   FABLE2_UIR_IN_CAP=<n>           max full-input dump lines total (def 4000)
//   FABLE2_UIR_IN_HITCAP=<n>        max hit lines total (def 2000)
//
// Log: fable2_ui_input_probe.log in the CWD (exe dir). Line "t0=" header
// gives the epoch ms of the first call for aligning with other logs.

#pragma once

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <mutex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <intrin.h>
#else
#include <sys/uio.h>
#include <unistd.h>
#endif

#include <rex/ppc/context.h>

namespace fable2::uip {

inline bool enabled() {
#ifdef _WIN32
  static const bool on = [] {
    char v[8] = {};
    size_t n = 0;
    return ::getenv_s(&n, v, sizeof(v), "FABLE2_UIR_IN") == 0 && v[0] == '1';
  }();
  return on;
#else
  static const bool on = [] {
    const char* v = std::getenv("FABLE2_UIR_IN");
    return v != nullptr && v[0] == '1';
  }();
  return on;
#endif
}

inline double delay_seconds() {
  const char* v = std::getenv("FABLE2_UIR_IN_DELAY");
  return v ? std::atof(v) : 33.0;
}
inline double duration_seconds() {
  const char* v = std::getenv("FABLE2_UIR_IN_DUR");
  return v ? std::atof(v) : 12.0;
}
inline uint32_t dump_cap() {
  const char* v = std::getenv("FABLE2_UIR_IN_CAP");
  return v ? (uint32_t)std::strtoul(v, nullptr, 0) : 4000u;
}
inline uint32_t hit_cap() {
  const char* v = std::getenv("FABLE2_UIR_IN_HITCAP");
  return v ? (uint32_t)std::strtoul(v, nullptr, 0) : 2000u;
}

// Large shared stdio buffer for the log file (must outlive the FILE*).
inline char* log_bigbuf() {
  static char buf[1 << 20];
  return buf;
}
inline FILE* logf() {
  static FILE* f = [] {
    FILE* out = nullptr;
#ifdef _WIN32
    if (::fopen_s(&out, "fable2_ui_input_probe.log", "w") != 0) out = nullptr;
#else
    out = std::fopen("fable2_ui_input_probe.log", "w");
#endif
    if (out) {
      // Buffered with a large buffer: per-line unbuffered writes make every
      // line a separate WriteFile syscall, and Windows Defender file-scans
      // each one (ms each), which blocks the render thread and freezes the
      // game. Batch the writes (periodic flush in flush_log) so only ~10
      // large WriteFile/s reach the disk. A ~100ms flush still persists data
      // promptly, so a TerminateProcess kill loses at most the last ~100ms.
      std::setvbuf(out, log_bigbuf(), _IOFBF, 1 << 20);
    }
    return out;
  }();
  return f;
}
// All log writes take this lock: the render thread (dumps, heartbeats) and the
// heap-scanner thread (runtime-addr lines) both write to the same FILE*, and
// stdio FILE* is not thread-safe.
inline std::mutex& log_lock() {
  static std::mutex m;
  return m;
}
// Write raw bytes to the log (locked, buffered - no syscall until flush_log).
inline void log_raw(const char* s, size_t n) {
  FILE* f = logf();
  if (!f || n == 0) return;
  std::lock_guard<std::mutex> l(log_lock());
  std::fwrite(s, 1, n, f);
}
inline void log_rawf(const char* fmt, ...) {
  // thread_local: a 1.6 KB per-call stack frame would add up on the render
  // thread (see the stack-headroom note in dump_inputs).
  thread_local char buf[1600];
  va_list ap;
  va_start(ap, fmt);
  int n = std::vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n < 0) return;
  log_raw(buf, (size_t)n);
}
// Periodic batched flush (20 ms): coalesces the buffered writes into a few
// large WriteFile calls instead of one syscall per line (which triggers
// per-line antivirus scans that block the render thread). 20 ms keeps the
// in-flight (unflushed) tail small so a kill pinpoints where output stopped.
inline void flush_log(int64_t now) {
  static std::atomic<int64_t> last_flush_us{0};
  int64_t last = last_flush_us.load(std::memory_order_relaxed);
  if (now - last >= 20000 &&
      last_flush_us.compare_exchange_strong(last, now,
                                            std::memory_order_relaxed)) {
    FILE* f = logf();
    if (!f) return;
    std::lock_guard<std::mutex> l(log_lock());
    std::fflush(f);
  }
}

// Known string addresses (from the heap scanner): a register equal to any of
// them (or within +/-256B) is logged as an ADDR HIT. FABLE2_UIR_IN_ADDRS=0xA,0xB
inline const std::vector<uint32_t>& addrs() {
  static const std::vector<uint32_t> a = [] {
    std::vector<uint32_t> out;
    const char* v = std::getenv("FABLE2_UIR_IN_ADDRS");
    if (!v) return out;
    std::string s(v);
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
      if (i == s.size() || s[i] == ',') {
        std::string tok = s.substr(start, i - start);
        if (!tok.empty())
          out.push_back((uint32_t)std::strtoul(tok.c_str(), nullptr, 0));
        start = i + 1;
      }
    }
    return out;
  }();
  return a;
}

// Runtime-discovered addresses (fed by the heap scanner's needle hits, so the
// prompt address is found automatically even though it moves per run).
inline std::mutex& rt_lock() {
  static std::mutex m;
  return m;
}
inline std::vector<uint32_t>& runtime_addrs() {
  static std::vector<uint32_t> v;
  return v;
}
inline std::atomic<uint32_t>& rt_version() {
  static std::atomic<uint32_t> c{0};
  return c;
}
inline void add_runtime_addr(uint32_t a) {
  if (a == 0) return;
  std::lock_guard<std::mutex> l(rt_lock());
  auto& v = runtime_addrs();
  if (v.size() >= 64) return;
  for (uint32_t x : v)
    if (x == a) return;
  v.push_back(a);
  rt_version().fetch_add(1, std::memory_order_release);
  log_rawf("# runtime-addr 0x%08X\n", a);
}

// Per-thread cached watch list (rebuilt only when the runtime set changes).
inline const std::vector<uint32_t>& watch_list() {
  static thread_local std::vector<uint32_t> cache;
  static thread_local uint32_t cache_version = 0;
  const uint32_t v = rt_version().load(std::memory_order_acquire);
  if (v != cache_version || cache.empty()) {
    cache = addrs();
    std::lock_guard<std::mutex> l(rt_lock());
    for (uint32_t a : runtime_addrs()) cache.push_back(a);
    cache_version = v;
  }
  return cache;
}

// Pattern scan (direct + one indirection guest reads) is only run when
// FABLE2_UIR_IN_PATSCAN=1 - it is too heavy for the render thread by default.
inline bool pat_scan_on() {
#ifdef _WIN32
  static const bool on = [] {
    char v[8] = {};
    size_t n = 0;
    return ::getenv_s(&n, v, sizeof(v), "FABLE2_UIR_IN_PATSCAN") == 0 && v[0] == '1';
  }();
  return on;
#else
  static const bool on = [] {
    const char* v = std::getenv("FABLE2_UIR_IN_PATSCAN");
    return v != nullptr && v[0] == '1';
  }();
  return on;
#endif
}

// Default: UTF-16BE "Press"
inline const std::vector<uint8_t>& pat() {
  static const std::vector<uint8_t> p = [] {
    std::vector<uint8_t> out;
    const char* v = std::getenv("FABLE2_UIR_IN_PAT");
    std::string s = v ? v : "00500072006500730073";
    for (size_t i = 0; i + 1 < s.size(); i += 2)
      out.push_back((uint8_t)std::strtoul(s.substr(i, 2).c_str(), nullptr, 16));
    return out;
  }();
  return p;
}

// DUMP spec: comma list of "name" or "name:stride:percap" (per-function
// stride/percap override the global FABLE2_UIR_IN_STRIDE/PERCAP).
struct DumpFn {
  std::string name;
  uint32_t stride = 0;   // 0 = use global
  uint32_t per_cap = 0;  // 0 = use global
};
inline const std::vector<DumpFn>& dump_fns() {
  static const std::vector<DumpFn> f = [] {
    std::vector<DumpFn> out;
    const char* v = std::getenv("FABLE2_UIR_IN_DUMP");
    if (!v) return out;
    std::string s(v);
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
      if (i == s.size() || s[i] == ',') {
        std::string tok = s.substr(start, i - start);
        while (!tok.empty() && tok.front() == ' ') tok.erase(tok.begin());
        while (!tok.empty() && tok.back() == ' ') tok.pop_back();
        start = i + 1;
        if (tok.empty()) continue;
        DumpFn d;
        size_t c1 = tok.find(':');
        if (c1 == std::string::npos) {
          d.name = tok;
        } else {
          d.name = tok.substr(0, c1);
          size_t c2 = tok.find(':', c1 + 1);
          if (c2 != std::string::npos) {
            d.stride = (uint32_t)std::strtoul(tok.substr(c1 + 1, c2 - c1 - 1).c_str(), nullptr, 0);
            d.per_cap = (uint32_t)std::strtoul(tok.substr(c2 + 1).c_str(), nullptr, 0);
          } else {
            d.stride = (uint32_t)std::strtoul(tok.substr(c1 + 1).c_str(), nullptr, 0);
          }
        }
        if (!d.name.empty()) out.push_back(d);
      }
    }
    return out;
  }();
  return f;
}

inline const DumpFn* find_dump_fn(const char* name) {
  for (const DumpFn& d : dump_fns())
    if (d.name == name) return &d;
  return nullptr;
}

inline int64_t now_us() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// Render-activity telemetry for the stall detector (fable2_stall_dump.h):
// updated on EVERY hook call, even when the probe itself is disabled.
#ifdef _WIN32
inline std::atomic<int64_t>& last_hook_us() {
  static std::atomic<int64_t> v{0};
  return v;
}
inline std::atomic<uint32_t>& last_hook_tid() {
  static std::atomic<uint32_t> v{0};
  return v;
}
#endif

struct State {
  int64_t t0_us = 0;
  int64_t start_us = 0;
  int64_t end_us = 0;
  int64_t proc_start_unix_ms = 0;  // for anchoring dumps to process start
  int64_t proc_start_steady_us = 0;
  uint32_t dump_lines = 0;
  uint32_t hit_lines = 0;
};

// When set, the [delay,dur] window is anchored to PROCESS START instead of the
// first hook call (which drifts with load time). FABLE2_UIR_IN_ANCHOR=proc
inline bool anchor_is_proc() {
#ifdef _WIN32
  static const bool on = [] {
    char v[16] = {};
    size_t n = 0;
    return ::getenv_s(&n, v, sizeof(v), "FABLE2_UIR_IN_ANCHOR") == 0 &&
           std::string(v) == "proc";
  }();
  return on;
#else
  static const bool on = [] {
    const char* v = std::getenv("FABLE2_UIR_IN_ANCHOR");
    return v != nullptr && std::string(v) == "proc";
  }();
  return on;
#endif
}

// Process-creation time in unix ms (so dumps can be anchored to process start,
// independent of when the first hook fires). 0 = unavailable.
inline int64_t proc_start_unix_ms() {
  int64_t out = 0;
#ifdef _WIN32
  FILETIME c, e, k, p;
  if (::GetProcessTimes(::GetCurrentProcess(), &c, &e, &k, &p)) {
    uint64_t ft = ((uint64_t)c.dwHighDateTime << 32) | c.dwLowDateTime;
    // FILETIME is 100ns since 1601-01-01; unix epoch is 1970-01-01.
    const uint64_t k100nsPerUnixMs = 10000ull;
    const uint64_t kEpochDelta100ns = 116444736000000000ull;
    if (ft > kEpochDelta100ns) out = (int64_t)((ft - kEpochDelta100ns) / k100nsPerUnixMs);
  }
#endif
  return out;
}

// Forward declarations (used by init_once diagnostics).
inline uint32_t dump_bytes();
inline bool dump_hop_on();
inline const std::vector<int>& pts_regs();

inline State& st() {
  static State* s = new State;
  return *s;
}

inline void init_once() {
  static std::atomic<bool> done{false};
  bool expected = false;
  if (!done.compare_exchange_strong(expected, true)) return;
  State& s = st();
  s.t0_us = now_us();
  s.start_us = s.t0_us + (int64_t)(delay_seconds() * 1e6);
  s.end_us = s.start_us + (int64_t)(duration_seconds() * 1e6);
  const int64_t epoch_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();
  s.proc_start_unix_ms = proc_start_unix_ms();
  if (s.proc_start_unix_ms > 0)
    s.proc_start_steady_us = s.t0_us - (epoch_ms - s.proc_start_unix_ms) * 1000;
  // Re-anchor the window to process start when requested.
  if (anchor_is_proc() && s.proc_start_steady_us > 0) {
    s.start_us = s.proc_start_steady_us + (int64_t)(delay_seconds() * 1e6);
    s.end_us = s.start_us + (int64_t)(duration_seconds() * 1e6);
  }
  log_rawf("# fable2_ui_input_probe\n# t0=%lld (unix ms of first call)\n",
           (long long)epoch_ms);
  log_rawf("# proc_start_unix_ms=%lld\n", (long long)s.proc_start_unix_ms);
  log_rawf("# anchor=%s window=[%.1fs,%.1fs]\n",
           (anchor_is_proc() ? "proc" : "first-call"), delay_seconds(),
           delay_seconds() + duration_seconds());
  // Diagnostics: log the parsed dump spec + resolved window so a bad env
  // parse or window re-anchoring is visible immediately.
  log_rawf("# start_us=%lld end_us=%lld t0_us=%lld proc_start_steady_us=%lld\n",
           (long long)s.start_us, (long long)s.end_us, (long long)s.t0_us,
           (long long)s.proc_start_steady_us);
  {
    const std::vector<DumpFn>& dfs = dump_fns();
    log_rawf("# dump_fns(%zu):", dfs.size());
    for (const DumpFn& d : dfs)
      log_rawf(" [%s stride=%u percap=%u]", d.name.c_str(), d.stride, d.per_cap);
    log_rawf(" bytes=%u hop=%d pts=", dump_bytes(),
             (int)(dump_hop_on() ? 1 : 0));
    for (int k : pts_regs()) log_rawf(" r%d", k);
    log_rawf("\n");
  }
  flush_log(now_us());
}

inline void log_line(const char* fmt, ...) {
  thread_local char buf[1600];
  va_list ap;
  va_start(ap, fmt);
  int n = std::vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n < 0) return;
  log_raw(buf, (size_t)n);
}

// The guest arena has a fixed host mapping (NOT the recompiler's `base` arg,
// which is a code-module base). host = 0x100000000 + guest, confirmed by the
// heap scanner. Addresses >= 0xE0000000 live in a second arena above that.
inline const uint8_t* host_of(uint32_t ga) {
  const uintptr_t off = (ga >= 0xE0000000u) ? 0x100000000ull : 0ull;
  return reinterpret_cast<const uint8_t*>(0x100000000ull + ga + off);
}

// Safe guest read: the host arena is commit-on-fault, so an uncommitted guest
// page would raise a host access violation. That is FATAL here: the recompiler
// installs a vectored exception handler for GUEST faults, and a host-side AV
// from the probe is intercepted there (far from any guest context) and hangs
// the render thread. So check the page is committed+readable with VirtualQuery
// BEFORE touching it, and only then memcpy. SEH remains as a backstop for the
// rare commit/race case.
inline bool gread(const uint8_t* /*base*/, uint32_t addr, void* dst, size_t n) {
  const uint8_t* h = host_of(addr);
#ifdef _WIN32
  MEMORY_BASIC_INFORMATION mbi;
  if (::VirtualQuery(h, &mbi, sizeof(mbi)) == 0) return false;
  if (mbi.State != MEM_COMMIT) return false;
  // Reject protections that fault on read (NOACCESS, guard pages).
  if (mbi.Protect == PAGE_NOACCESS) return false;
  if (mbi.Protect & PAGE_GUARD) return false;
  __try {
    std::memcpy(dst, h, n);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
#else
  // No SEH here: have the kernel do the copy, so an unmapped or unreadable
  // page fails the call instead of faulting this thread.
  struct iovec local = {dst, n};
  struct iovec remote = {const_cast<uint8_t*>(h), n};
  return ::process_vm_readv(::getpid(), &local, 1, &remote, 1, 0) ==
         static_cast<ssize_t>(n);
#endif
}

inline bool valid_addr(uint32_t a) {
  return (a >= 0x1000u && a < 0x84000000u) && (a & 3) == 0;
}

inline uint32_t be32(const uint8_t* p) {
  return (uint32_t)((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);
}

inline bool find_pat(const uint8_t* p, size_t n, size_t& off) {
  const std::vector<uint8_t>& P = pat();
  if (P.empty() || P.size() > n) return false;
  for (size_t i = 0; i + P.size() <= n; ++i) {
    if (std::memcmp(p + i, P.data(), P.size()) == 0) {
      off = i;
      return true;
    }
  }
  return false;
}

inline const char* rname(int k) {
  static const char* n[] = {"r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10"};
  return (k >= 3 && k <= 10) ? n[k - 3] : "r?";
}

inline uint32_t regval(PPCContext& ctx, int k) {
  switch (k) {
    case 3: return (uint32_t)ctx.r3.u64;
    case 4: return (uint32_t)ctx.r4.u64;
    case 5: return (uint32_t)ctx.r5.u64;
    case 6: return (uint32_t)ctx.r6.u64;
    case 7: return (uint32_t)ctx.r7.u64;
    case 8: return (uint32_t)ctx.r8.u64;
    case 9: return (uint32_t)ctx.r9.u64;
    case 10: return (uint32_t)ctx.r10.u64;
    case 11: return (uint32_t)ctx.r11.u64;
    case 12: return (uint32_t)ctx.r12.u64;
    case 13: return (uint32_t)ctx.r13.u64;
    case 14: return (uint32_t)ctx.r14.u64;
    case 15: return (uint32_t)ctx.r15.u64;
    case 16: return (uint32_t)ctx.r16.u64;
    case 17: return (uint32_t)ctx.r17.u64;
    case 18: return (uint32_t)ctx.r18.u64;
    case 19: return (uint32_t)ctx.r19.u64;
    case 20: return (uint32_t)ctx.r20.u64;
    case 21: return (uint32_t)ctx.r21.u64;
    case 22: return (uint32_t)ctx.r22.u64;
    case 23: return (uint32_t)ctx.r23.u64;
    case 24: return (uint32_t)ctx.r24.u64;
    case 25: return (uint32_t)ctx.r25.u64;
    case 26: return (uint32_t)ctx.r26.u64;
    case 27: return (uint32_t)ctx.r27.u64;
    case 28: return (uint32_t)ctx.r28.u64;
    case 29: return (uint32_t)ctx.r29.u64;
    case 30: return (uint32_t)ctx.r30.u64;
    case 31: return (uint32_t)ctx.r31.u64;
    default: return 0;
  }
}

// Decodes a UTF-16BE buffer to ASCII for logging.
inline void decode_u16be(const uint8_t* p, size_t n) {
  char out[140];
  int o = 0;
  out[o++] = '"';
  for (size_t i = 0; i + 1 < n && i < 120 && o < (int)sizeof(out) - 2; i += 2) {
    uint16_t c = (uint16_t)((p[i] << 8) | p[i + 1]);
    if (c == 0) break;
    out[o++] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
  }
  out[o++] = '"';
  out[o] = 0;
  log_raw(out, (size_t)o);
}

// Logs one full hit: where the pattern was found + complete input state.
inline void log_hit(const char* name, PPCContext& ctx, int reg, uint32_t rval,
                    uint32_t hit_addr, size_t off, const uint8_t* buf, int ind,
                    int64_t now) {
  State& s = st();
  if (s.hit_lines >= hit_cap()) return;
  ++s.hit_lines;
  int64_t t = (now - s.t0_us) / 1000;
  log_line("HIT t=%lld %s %s=0x%08X%s->0x%08X+%zu str:", (long long)t, name,
           rname(reg), rval, ind ? "[w%d]" : "", hit_addr, off);
  decode_u16be(buf + off, 96 - off);
  log_line(" lr=0x%08X r3=0x%08X r4=0x%08X r5=0x%08X r6=0x%08X r7=0x%08X "
           "r8=0x%08X r9=0x%08X r10=0x%08X f1=%.9g f2=%.9g f3=%.9g f4=%.9g\n",
           (uint32_t)ctx.lr, (uint32_t)ctx.r3.u64, (uint32_t)ctx.r4.u64,
           (uint32_t)ctx.r5.u64, (uint32_t)ctx.r6.u64, (uint32_t)ctx.r7.u64,
           (uint32_t)ctx.r8.u64, (uint32_t)ctx.r9.u64, (uint32_t)ctx.r10.u64,
           (double)ctx.f1.f64, (double)ctx.f2.f64, (double)ctx.f3.f64,
           (double)ctx.f4.f64);
  // 16 words around the hit for context.
  log_line("     ctx:");
  for (int w = -2; w < 14; ++w) {
    if (w >= 0 && w + 1 <= (int)(96 / 4)) {
      if (w % 4 == 0) log_line("\n     +0x%02X:", off + w * 4 - 8);
      log_line(" %08X", be32(buf + off + w * 4));
    }
  }
  log_line("\n");
}

// Scans one register value (and one indirection) for the pattern.
inline void probe_addr(const char* name, PPCContext& ctx, uint8_t* base,
                       int reg, int64_t now) {
  const uint32_t rval = regval(ctx, reg);
  if (!valid_addr(rval)) return;
  uint8_t buf[96];
  if (!gread(base, rval, buf, sizeof(buf))) return;
  size_t off;
  if (find_pat(buf, sizeof(buf), off)) {
    log_hit(name, ctx, reg, rval, rval, off, buf, 0, now);
    return;
  }
  for (int w = 0; w < 16; ++w) {
    const uint32_t p = be32(buf + w * 4);
    if (!valid_addr(p)) continue;
    uint8_t b2[96];
    if (!gread(base, p, b2, sizeof(b2))) continue;
    if (find_pat(b2, sizeof(b2), off)) {
      log_hit(name, ctx, reg, rval, p, off, b2, w, now);
      return;
    }
  }
}

// One-line dump of all GPRs for the hit log.
inline std::string all_regs(PPCContext& ctx) {
  thread_local char buf[760];
  int o = 0;
  for (int k = 3; k <= 31; k += 2)
    o += std::snprintf(buf + o, sizeof(buf) - (size_t)o, "r%d=0x%08X r%d=0x%08X ",
                       k, regval(ctx, k), k + 1, regval(ctx, k + 1));
  return std::string(buf, (size_t)o);
}

// Per-function call count (for strided sampling + per-function caps).
struct DumpStats {
  uint32_t calls = 0;
  uint32_t dumped = 0;
};
inline DumpStats& dstat(const char* name) {
  static std::vector<std::pair<std::string, DumpStats>> v;
  static std::mutex m;
  std::lock_guard<std::mutex> l(m);
  for (auto& e : v)
    if (e.first == name) return e.second;
  v.emplace_back(name, DumpStats{});
  return v.back().second;
}

// Registers whose pointed-to memory we dump (default r3,r4,r5,r6,r28).
inline const std::vector<int>& pts_regs() {
  static const std::vector<int> r = [] {
    std::vector<int> out;
    const char* v = std::getenv("FABLE2_UIR_IN_PTS");
    std::string s = v ? v : "r3,r4,r5,r6,r28";
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
      if (i == s.size() || s[i] == ',') {
        std::string tok = s.substr(start, i - start);
        if (!tok.empty()) {
          if (tok[0] == 'r' || tok[0] == 'R') tok.erase(0, 1);
          out.push_back(std::atoi(tok.c_str()));
        }
        start = i + 1;
      }
    }
    return out;
  }();
  return r;
}

inline uint32_t dump_stride() {
  const char* v = std::getenv("FABLE2_UIR_IN_STRIDE");
  return v ? (uint32_t)std::strtoul(v, nullptr, 0) : 1u;
}
inline uint32_t dump_per_cap() {
  const char* v = std::getenv("FABLE2_UIR_IN_PERCAP");
  return v ? (uint32_t)std::strtoul(v, nullptr, 0) : 12u;
}

// Bytes dumped per pointee register (default 64, capped at 512).
inline uint32_t dump_bytes() {
  const char* v = std::getenv("FABLE2_UIR_IN_BYTES");
  uint32_t n = v ? (uint32_t)std::strtoul(v, nullptr, 0) : 64u;
  if (n < 16) n = 16;
  if (n > 512) n = 512;
  return n;
}
// When set, each guest-pointer word inside a dumped buffer is followed one
// hop (its first dump_bytes() bytes are dumped too). FABLE2_UIR_IN_HOP=1
inline bool dump_hop_on() {
#ifdef _WIN32
  static const bool on = [] {
    char v[8] = {};
    size_t n = 0;
    return ::getenv_s(&n, v, sizeof(v), "FABLE2_UIR_IN_HOP") == 0 &&
           v[0] == '1';
  }();
  return on;
#else
  static const bool on = [] {
    const char* v = std::getenv("FABLE2_UIR_IN_HOP");
    return v != nullptr && v[0] == '1';
  }();
#endif
}

// Decode any run of >=4 printable UTF-16BE chars in buf and log it.
inline void log_utf16_runs(const char* tag, uint32_t at, const uint8_t* buf,
                           size_t n) {
  (void)at;
  size_t i = 0;
  while (i + 1 < n) {
    uint16_t c = (uint16_t)((buf[i] << 8) | buf[i + 1]);
    if (c >= 0x20 && c < 0x7F) {
      size_t j = i;
      while (j + 1 < n) {
        uint16_t c2 = (uint16_t)((buf[j] << 8) | buf[j + 1]);
        if (c2 < 0x20 || c2 >= 0x7F) break;
        j += 2;
      }
      if (j - i >= 8) {
        char s[160];
        int sl = (int)std::min<size_t>(j - i, 30);
        for (int k = 0; k < sl; k += 2)
          s[(k - i) / 2] = (char)((buf[i + k] << 8) | buf[i + k + 1]);
        s[sl / 2] = 0;
        log_rawf("%s utf16@+0x%03zx: \"%s\"\n", tag, i, s);
      }
      i = j;
    } else {
      ++i;
    }
  }
}

// Global dump rate limiter: the -O0 debug render thread dies if dumps run
// at ~60+/s, so cap the total dump rate (default 15/s, env-overridable).
inline uint32_t dump_rate_per_s() {
  const char* v = std::getenv("FABLE2_UIR_IN_RATE");
  return v ? (uint32_t)std::strtoul(v, nullptr, 0) : 15u;
}

// Full input dump for a named function (strided + per-function cap).
inline void dump_inputs(const char* name, PPCContext& ctx, uint8_t* base,
                        int64_t now) {
  State& s = st();
  if (s.dump_lines >= dump_cap()) return;
  // Subsample + per-function cap on ACTUAL calls first (stride counts every
  // invocation, not just rate-limited ones - otherwise a high-frequency fn
  // monopolizes the global gate and the stride never reaches its target).
  const DumpFn* dfn = find_dump_fn(name);
  DumpStats& ds = dstat(name);
  ++ds.calls;
  const uint32_t stride = dfn && dfn->stride ? dfn->stride : dump_stride();
  const uint32_t per_cap = dfn && dfn->per_cap ? dfn->per_cap : dump_per_cap();
  if (stride && ds.calls % stride != 0) return;
  if (ds.dumped >= per_cap) return;
  // Global write-rate gate, applied only right before emitting, so the -O0
  // render thread is never flooded. A blocked candidate is not consumed
  // (ds.dumped unchanged), so it can retry on the next stride hit.
  static std::atomic<int64_t> last_dump_us{0};
  const int64_t min_gap_us = 1000000 / (int64_t)std::max(1u, dump_rate_per_s());
  const int64_t last = last_dump_us.load(std::memory_order_relaxed);
  if (now - last < min_gap_us) return;
  last_dump_us.store(now, std::memory_order_relaxed);
  ++ds.dumped;
  ++s.dump_lines;
  int64_t t = (now - s.t0_us) / 1000;
  // Absolute wall-clock (unix ms) so the dump can be anchored to process start.
  const int64_t unix_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();
  const int64_t t_proc_ms =
      s.proc_start_unix_ms ? (unix_ms - s.proc_start_unix_ms) : -1;
  // Stack headroom of the render thread at dump time (TEB: StackBase=gs:8,
  // StackLimit=gs:10). If this trends toward a few KB, the probe's own frames
  // are pushing the thread into its guard page (stack overflow AV -> the
  // recompiler's guest-fault VEH -> hang).
  uintptr_t sp = 0, stack_used = 0, stack_avail = 0;
#ifdef _WIN32
  sp = (uintptr_t)&t;  // 't' is the local t0rel ms value declared above
  const uintptr_t sb = __readgsqword(0x08);
  const uintptr_t sl = __readgsqword(0x10);
  stack_used = sb > sp ? sb - sp : 0;
  stack_avail = sp > sl ? sp - sl : 0;
#endif
  log_line("== IN t_proc=%.3fs t0rel=%lldms %s (call %u, dump %u) "
           "[sp=0x%llX used=%lluKB avail=%lluKB]\n",
           t_proc_ms < 0 ? -1.0 : (double)t_proc_ms / 1000.0, (long long)t, name,
           ds.calls, ds.dumped, (unsigned long long)sp,
           (unsigned long long)(stack_used / 1024),
           (unsigned long long)(stack_avail / 1024));
  log_line("GPRs: %s\n", all_regs(ctx).c_str());
  log_line("lr=0x%08X\n", (uint32_t)ctx.lr);
  log_line("FPRs: f1=%.9g f2=%.9g f3=%.9g f4=%.9g f5=%.9g f6=%.9g "
           "f7=%.9g f8=%.9g f9=%.9g f10=%.9g f11=%.9g f12=%.9g f13=%.9g "
           "f14=%.9g\n",
           (double)ctx.f1.f64, (double)ctx.f2.f64, (double)ctx.f3.f64,
           (double)ctx.f4.f64, (double)ctx.f5.f64, (double)ctx.f6.f64,
           (double)ctx.f7.f64, (double)ctx.f8.f64, (double)ctx.f9.f64,
           (double)ctx.f10.f64, (double)ctx.f11.f64, (double)ctx.f12.f64,
           (double)ctx.f13.f64, (double)ctx.f14.f64);
  // Pointed-to memory for each arg/matrix register.
  const uint32_t nbytes = dump_bytes();
  const int nwords = (int)(nbytes / 4);
  // Heap-allocated dump buffers: a 2.4 KB per-call stack frame here would
  // stack on top of the recompiler's own frames on the render thread and,
  // at high guest call depth, push the thread into its guard page (the AV is
  // then intercepted by the recompiler's guest-fault handler and hangs the
  // thread). Keep the probe's own stack usage minimal.
  std::vector<uint8_t> buf(nbytes);
  std::vector<uint8_t> hbuf(nbytes);
  for (int k : pts_regs()) {
    const uint32_t o = regval(ctx, k);
    if (!valid_addr(o)) continue;
    if (!gread(base, o, buf.data(), nbytes)) {
      log_line("  r%d=0x%08X (unreadable, host=0x%llX)\n", k, o,
               (unsigned long long)(uintptr_t)host_of(o));
      continue;
    }
    char lbl[200];
    std::snprintf(lbl, sizeof(lbl), "  r%d=0x%08X -> ", k, o);
    std::string line = lbl;
    char w8[16];
    for (int w = 0; w < nwords; ++w) {
      std::snprintf(w8, sizeof(w8), "%08X", be32(buf.data() + w * 4));
      line += w8;
    }
    line += "\n";
    log_raw(line.data(), line.size());
    char tag[32];
    std::snprintf(tag, sizeof(tag), "  %s 0x%08X", rname(k), o);
    log_utf16_runs(tag, o, buf.data(), nbytes);
    if (dump_hop_on()) {
      int hops = 0;
      // Separate buffer for hop targets: the original pointee `buf` must be
      // preserved so every word of it is examined (fan-out), not a chain that
      // overwrites the source and chases into code bytes.
      for (int w = 0; w < nwords && hops < 10; ++w) {
        const uint32_t p = be32(buf.data() + w * 4);
        // heap or image pointers only (skip floats/constants).
        const bool heap = p >= 0x10000000u && p < 0x82000000u;
        const bool image = p >= 0x82000000u && p < 0x83620000u;
        if (!heap && !image) continue;
        if (!gread(base, p, hbuf.data(), nbytes)) continue;
        std::snprintf(lbl, sizeof(lbl), "    hop w%d 0x%08X -> ", w, p);
        line = lbl;
        for (int x = 0; x < nwords; ++x) {
          std::snprintf(w8, sizeof(w8), "%08X", be32(hbuf.data() + x * 4));
          line += w8;
        }
        line += "\n";
        log_raw(line.data(), line.size());
        std::snprintf(tag, sizeof(tag), "    hop 0x%08X", p);
        log_utf16_runs(tag, p, hbuf.data(), nbytes);
        ++hops;
      }
    }
  }
  log_line("== end %s\n", name);
}

// Called on every hooked pipeline function entry.
inline void scan(const char* name, PPCContext& ctx, uint8_t* base) {
#ifdef _WIN32
  // Stall-dump telemetry (cheap: two relaxed atomics).
  last_hook_us().store(now_us(), std::memory_order_relaxed);
  last_hook_tid().store((uint32_t)::GetCurrentThreadId(),
                        std::memory_order_relaxed);
#endif
  if (!enabled()) return;
  init_once();
  const int64_t now = now_us();
  flush_log(now);  // periodic batched flush (~10/s) of the buffered log
  State& s = st();
  // 1 Hz heartbeat (always flushed): confirms scan() is live and shows the
  // window relationship (t_proc vs [start,end]) so a mis-anchored window or a
  // dead hook path is visible even when the process is killed mid-run.
  {
    static std::atomic<int64_t> last_hb_us{0};
    int64_t last = last_hb_us.load(std::memory_order_relaxed);
    if (now - last >= 1000000 &&
        last_hb_us.compare_exchange_strong(last, now,
                                           std::memory_order_relaxed)) {
      const int64_t t_proc_ms =
          s.proc_start_steady_us ? (now - s.proc_start_steady_us) / 1000 : -1;
      log_rawf("# HB t_proc=%lldms now=%lld win=[%lld,%lld) in=%d name=%s\n",
               (long long)t_proc_ms, (long long)now, (long long)s.start_us,
               (long long)s.end_us,
               (now >= s.start_us && now < s.end_us) ? 1 : 0, name);
      flush_log(now);  // persist the liveness marker promptly
    }
  }
  if (now < s.start_us || now >= s.end_us) return;
  static std::atomic<bool> first_in{false};
  if (!first_in.exchange(true))
    log_line("# first in-window call: %s now_us=%lld\n", name, (long long)now);
  if (find_dump_fn(name)) dump_inputs(name, ctx, base, now);
  // Known-address hunt: every GPR (the pointer may travel in r11+ or be kept
  // live across the call in a callee-saved register). Register-only work -
  // cheap enough to run on the render thread every call.
  const std::vector<uint32_t>& watch = watch_list();
  if (!watch.empty()) {
    for (int k = 3; k <= 31; ++k) {
      const uint32_t v = regval(ctx, k);
      if (!valid_addr(v)) continue;
      for (uint32_t a : watch) {
        if (v >= a - 256 && v <= a + 256) {
          if (s.hit_lines >= hit_cap()) return;
          ++s.hit_lines;
          int64_t t = (now - s.t0_us) / 1000;
          thread_local char line[900];
          int o = std::snprintf(
              line, sizeof(line),
              "ADDRHIT t=%lld %s r%d=0x%08X (near 0x%08X) lr=0x%08X ",
              (long long)t, name, k, v, a, (uint32_t)ctx.lr);
          std::snprintf(line + o, sizeof(line) - (size_t)o, "%s\n",
                        all_regs(ctx).c_str());
          log_raw(line, std::strlen(line));
          return;
        }
      }
    }
  }
  // Pattern hunt across r3..r10 (direct + one indirection) - heavy; opt-in.
  if (pat_scan_on()) {
    for (int k = 3; k <= 10; ++k) {
      if (s.hit_lines >= hit_cap()) return;
      probe_addr(name, ctx, base, k, now);
    }
  }
}

}  // namespace fable2::uip
