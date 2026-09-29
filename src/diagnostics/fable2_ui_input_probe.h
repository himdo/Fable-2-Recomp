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

inline FILE* logf() {
  static FILE* f = [] {
#ifdef _WIN32
    FILE* out = nullptr;
    if (::fopen_s(&out, "fable2_ui_input_probe.log", "w") != 0) out = nullptr;
    return out;
#else
    return std::fopen("fable2_ui_input_probe.log", "w");
#endif
  }();
  return f;
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
  if (FILE* f = logf()) std::fprintf(f, "# runtime-addr 0x%08X\n", a);
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

inline const std::vector<std::string>& dump_fns() {
  static const std::vector<std::string> f = [] {
    std::vector<std::string> out;
    const char* v = std::getenv("FABLE2_UIR_IN_DUMP");
    if (!v) return out;
    std::string s(v);
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
      if (i == s.size() || s[i] == ',') {
        std::string tok = s.substr(start, i - start);
        while (!tok.empty() && tok.front() == ' ') tok.erase(tok.begin());
        while (!tok.empty() && tok.back() == ' ') tok.pop_back();
        if (!tok.empty()) out.push_back(tok);
        start = i + 1;
      }
    }
    return out;
  }();
  return f;
}

inline int64_t now_us() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

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
  if (FILE* f = logf()) {
    const int64_t epoch_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();
    s.proc_start_unix_ms = proc_start_unix_ms();
    if (s.proc_start_unix_ms > 0)
      s.proc_start_steady_us =
          s.t0_us - (epoch_ms - s.proc_start_unix_ms) * 1000;
    // Re-anchor the window to process start when requested.
    if (anchor_is_proc() && s.proc_start_steady_us > 0) {
      s.start_us = s.proc_start_steady_us + (int64_t)(delay_seconds() * 1e6);
      s.end_us = s.start_us + (int64_t)(duration_seconds() * 1e6);
    }
    std::fprintf(f, "# fable2_ui_input_probe\n# t0=%lld (unix ms of first call)\n",
                 (long long)epoch_ms);
    std::fprintf(f, "# proc_start_unix_ms=%lld\n",
                 (long long)s.proc_start_unix_ms);
    std::fprintf(f, "# anchor=%s window=[%.1fs,%.1fs]\n",
                 (anchor_is_proc() ? "proc" : "first-call"), delay_seconds(),
                 delay_seconds() + duration_seconds());
    std::fflush(f);
  }
}

inline void log_line(const char* fmt, ...) {
  FILE* f = logf();
  if (!f) return;
  char buf[1600];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  std::fwrite(buf, 1, std::strlen(buf), f);
}

// The guest arena has a fixed host mapping (NOT the recompiler's `base` arg,
// which is a code-module base). host = 0x100000000 + guest, confirmed by the
// heap scanner. Addresses >= 0xE0000000 live in a second arena above that.
inline const uint8_t* host_of(uint32_t ga) {
  const uintptr_t off = (ga >= 0xE0000000u) ? 0x100000000ull : 0ull;
  return reinterpret_cast<const uint8_t*>(0x100000000ull + ga + off);
}

// Safe guest read: host arena is commit-on-fault; SEH catches uncommitted.
inline bool gread(const uint8_t* /*base*/, uint32_t addr, void* dst, size_t n) {
#ifdef _WIN32
  __try {
    std::memcpy(dst, host_of(addr), n);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
#else
  // This diagnostic assumes the Windows arena address and SEH fault guard.
  // Do not read that address on hosts with a different guest memory mapping.
  (void)addr;
  (void)dst;
  (void)n;
  return false;
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
  FILE* f = logf();
  if (!f) return;
  std::fputc('"', f);
  for (size_t i = 0; i + 1 < n && i < 120; i += 2) {
    uint16_t c = (uint16_t)((p[i] << 8) | p[i + 1]);
    if (c == 0) break;
    std::fputc(c >= 0x20 && c < 0x7F ? (char)c : '?', f);
  }
  std::fputc('"', f);
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
  char buf[700];
  int o = 0;
  for (int k = 3; k <= 31; k += 2)
    o += std::snprintf(buf + o, sizeof(buf) - o, "r%d=0x%08X r%d=0x%08X ", k,
                       regval(ctx, k), k + 1, regval(ctx, k + 1));
  return std::string(buf);
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

// Full input dump for a named function (strided + per-function cap).
inline void dump_inputs(const char* name, PPCContext& ctx, uint8_t* base,
                        int64_t now) {
  State& s = st();
  if (s.dump_lines >= dump_cap()) return;
  DumpStats& ds = dstat(name);
  ++ds.calls;
  const uint32_t stride = dump_stride();
  if (stride && ds.calls % stride != 0) return;
  if (ds.dumped >= dump_per_cap()) return;
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
  log_line("== IN t_proc=%.3fs t0rel=%lldms %s (call %u, dump %u)\n",
           t_proc_ms < 0 ? -1.0 : (double)t_proc_ms / 1000.0, (long long)t, name,
           ds.calls, ds.dumped);
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
  for (int k : pts_regs()) {
    const uint32_t o = regval(ctx, k);
    if (!valid_addr(o)) continue;
    uint8_t buf[64];
    if (!gread(base, o, buf, sizeof(buf))) {
      log_line("  r%d=0x%08X (unreadable, host=0x%llX)\n", k, o,
               (unsigned long long)(uintptr_t)host_of(o));
      continue;
    }
    char lbl[200];
    std::snprintf(lbl, sizeof(lbl), "  r%d=0x%08X -> ", k, o);
    std::string line = lbl;
    char hb[400];
    int ho = 0;
    for (int w = 0; w < 16; ++w)
      ho += std::snprintf(hb + ho, sizeof(hb) - (size_t)ho, "%08X", be32(buf + w * 4));
    line += hb;
    line += "\n";
    FILE* f = logf();
    if (f) std::fwrite(line.data(), 1, line.size(), f);
  }
  log_line("== end %s\n", name);
}

// Called on every hooked pipeline function entry.
inline void scan(const char* name, PPCContext& ctx, uint8_t* base) {
  if (!enabled()) return;
  init_once();
  const int64_t now = now_us();
  State& s = st();
  if (now < s.start_us || now >= s.end_us) return;

  for (const std::string& d : dump_fns()) {
    if (d == name) {
      dump_inputs(name, ctx, base, now);
      break;
    }
  }
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
          char line[900];
          int o = std::snprintf(
              line, sizeof(line),
              "ADDRHIT t=%lld %s r%d=0x%08X (near 0x%08X) lr=0x%08X ",
              (long long)t, name, k, v, a, (uint32_t)ctx.lr);
          std::snprintf(line + o, sizeof(line) - (size_t)o, "%s\n",
                        all_regs(ctx).c_str());
          FILE* f = logf();
          if (f) std::fwrite(line, 1, std::strlen(line), f);
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
