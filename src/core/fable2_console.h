// fable_2 - developer commands, console-v5.
//
// Typed in the SDK's `~` console as   f2 <command>   for example
//   f2 warp 1 2 3        f2 god on        f2 warp save a; wait 60; warp a
// (chain commands with ';' inside one f2 line).
// Nothing runs until the first `f2` command is typed (or console_enabled /
// FABLE2_CONSOLE=1 turns on the file transport below), so a normal launch is
// unaffected.
//
// File transport (optional, only when console_enabled or FABLE2_CONSOLE=1):
//   <game root>\console\in.txt   lines written by an external tool
//   <game root>\console\out.txt  host command output, appended
//   <game root>\console\state.txt  status, rewritten about once a second
//
// Every line goes into one FIFO queue; "a; b" is two commands. The queue runs
// on the game thread from the MainRenderLoop hook, one command per frame unless
// a wait is pending. Host commands (below) are handled here; any other line is
// handed to Lua (console_cmds.lua) through the F5 runner, so Lua results go to
// the game log via cprint while host results go to out.txt.
//
// Host commands: wait, waitfor, exec, echo, peek, poke, expect, find <number>,
// freeze, unfreeze, trace, status, hosthelp. Addresses are Xbox 360 guest
// virtual addresses (hex, with or without 0x); values are big-endian as in the game.

#pragma once

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "fable2_f5_lua.h"

// The SDK's `~` console (ImGui overlay) logs through REXLOG_* and can register
// commands with REXCVAR_DEFINE_COMMAND_ARGS (rex/cvar.h). Both are optional here
// so the file still builds against an older SDK; see the bottom of this file.
#if __has_include(<rex/logging.h>)
#include <rex/logging.h>
#define FABLE2_CONSOLE_HAVE_REXLOG 1
#endif

#define FABLE2_CONSOLE_BUILD "console-v5"

REXCVAR_DEFINE_BOOL(console_enabled, false, "Input",
                    "Enable the developer command line (f2-console.cmd). Off by default.");

namespace fable2::console {

namespace fs = std::filesystem;
namespace ta = fable2::textappend;
using Clock = std::chrono::steady_clock;

struct State {
  fs::path root;
  std::mutex qmu;                // guards `queue` (UI thread pushes, game thread pops)
  std::atomic<bool> active{false};  // set by the first `f2` command
  std::deque<std::string> queue;
  uint64_t frames = 0;
  uint32_t errors = 0;
  std::string last;
  // Pending wait.
  uint64_t wait_frames = 0;
  bool wait_time = false;
  Clock::time_point wait_until{};
  // Pending waitfor ram.
  bool waitfor = false;
  uint32_t wf_addr = 0, wf_size = 4;
  int64_t wf_value = 0;
  std::string wf_op = "==";
  Clock::time_point wf_deadline{};
  // freeze / trace.
  struct Frozen {
    uint64_t value;
    uint32_t size;
  };
  std::map<uint32_t, Frozen> frozen;
  bool tracing = false;
  uint32_t tr_addr = 0, tr_words = 1;
  int tr_lines_left = 0;
  std::vector<uint32_t> tr_prev;
};
inline State& S() {
  static State s;
  return s;
}

inline void set_root(const fs::path& p) { S().root = p; }

inline bool enabled() {
  static const bool env = [] {
    const char* e = std::getenv("FABLE2_CONSOLE");
    return e && *e && *e != '0';
  }();
  return env || REXCVAR_GET(console_enabled);
}

//--- Output -----------------------------------------------------------------

inline void out(const std::string& tag, const std::string& msg, bool err = false) {
  State& s = S();
  const std::string line = "[" + tag + "] " + (err ? "ERR " : "") + msg;
  s.last = line;
  if (err) ++s.errors;
#ifdef FABLE2_CONSOLE_HAVE_REXLOG
  REXLOG_INFO("{}", line);
#endif
  std::error_code ec;
  fs::create_directories(s.root / "console", ec);
  std::ofstream f(s.root / "console" / "out.txt", std::ios::app);
  f << line << "\n";
}

//--- Parsing helpers --------------------------------------------------------

inline std::vector<std::string> split_words(const std::string& l) {
  std::vector<std::string> w;
  std::istringstream is(l);
  std::string t;
  while (is >> t) w.push_back(t);
  return w;
}

inline bool parse_u(const std::string& s, uint64_t& v, int dflt_base = 10) {
  if (s.empty()) return false;
  char* end = nullptr;
  int base = dflt_base;
  if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) base = 16;
  v = std::strtoull(s.c_str(), &end, base);
  return end && *end == 0;
}
inline bool parse_addr(const std::string& s, uint32_t& a) {
  uint64_t v;
  if (!parse_u(s, v, 16) || v > 0xFFFFFFFFull) return false;
  a = static_cast<uint32_t>(v);
  return true;
}
inline bool parse_i(const std::string& s, int64_t& v) {
  if (s.empty()) return false;
  char* end = nullptr;
  v = std::strtoll(s.c_str(), &end, 0);
  return end && *end == 0;
}
// 8/16/32 or b/h/w.
inline bool parse_size(const std::string& s, uint32_t& sz) {
  if (s == "8" || s == "b") sz = 1;
  else if (s == "16" || s == "h") sz = 2;
  else if (s == "32" || s == "w") sz = 4;
  else return false;
  return true;
}
// Size implied by how a value is written: 0x70 = byte, 0x0070 = half, else word.
inline uint32_t implied_size(const std::string& s) {
  if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    const size_t digits = s.size() - 2;
    if (digits <= 2) return 1;
    if (digits <= 4) return 2;
  }
  return 4;
}

//--- Guest memory -----------------------------------------------------------

inline bool read_be(const uint8_t* base, uint32_t a, uint32_t size, uint64_t& v) {
  if (size == 0 || (a % size) != 0) return false;
  if (!ta::rdable2(base, a, size)) return false;
  uint8_t b[4];
  std::memcpy(b, reinterpret_cast<const void*>(ta::host_addr(base, a)), size);
  v = 0;
  for (uint32_t i = 0; i < size; ++i) v = (v << 8) | b[i];
  return true;
}
inline bool write_be(const uint8_t* base, uint32_t a, uint32_t size, uint64_t v) {
  if (size == 0 || (a % size) != 0) return false;
  if (!ta::wr2(base, a, size)) return false;
  uint8_t b[4];
  for (uint32_t i = 0; i < size; ++i) b[size - 1 - i] = static_cast<uint8_t>(v >> (8 * i));
  std::memcpy(reinterpret_cast<void*>(ta::host_addr(base, a)), b, size);
  return true;
}
inline std::string hex(uint64_t v, int width) {
  char buf[24];
  std::snprintf(buf, sizeof buf, "%0*llX", width, static_cast<unsigned long long>(v));
  return buf;
}

inline bool compare(int64_t cur, const std::string& op, int64_t want) {
  if (op == "==") return cur == want;
  if (op == "!=") return cur != want;
  if (op == "<") return cur < want;
  if (op == ">") return cur > want;
  if (op == "<=") return cur <= want;
  if (op == ">=") return cur >= want;
  if (op == "&") return (cur & want) != 0;
  return false;
}
inline bool is_op(const std::string& s) {
  return s == "==" || s == "!=" || s == "<" || s == ">" || s == "<=" || s == ">=" || s == "&";
}

// "30" -> frames, "500ms"/"5s" -> real time.
inline bool parse_duration(const std::string& t, bool& is_frames, uint64_t& frames,
                           Clock::time_point& deadline) {
  std::string d = t;
  uint64_t n = 0;
  is_frames = false;
  if (d.size() > 2 && d.compare(d.size() - 2, 2, "ms") == 0) {
    d.resize(d.size() - 2);
    if (!parse_u(d, n)) return false;
    deadline = Clock::now() + std::chrono::milliseconds(n);
    return true;
  }
  if (!d.empty() && d.back() == 's') {
    d.pop_back();
    if (!parse_u(d, n)) return false;
    deadline = Clock::now() + std::chrono::seconds(n);
    return true;
  }
  if (!parse_u(d, n)) return false;
  is_frames = true;
  frames = n;
  return true;
}

//--- Queue ------------------------------------------------------------------

inline void enqueue_text(const std::string& text, bool front = false) {
  State& s = S();
  std::vector<std::string> lines;
  std::istringstream is(text);
  std::string ln;
  while (std::getline(is, ln)) {
    while (!ln.empty() && ln.back() == '\r') ln.pop_back();
    const size_t b = ln.find_first_not_of(" \t");
    if (b == std::string::npos) continue;
    ln = ln.substr(b);
    if (ln[0] == '#') continue;
    // Raw Lua and "!" lines keep their semicolons; everything else splits on ';'.
    std::string head = ln.substr(0, 4);
    for (auto& c : head) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (head == "lua " || ln[0] == '!') {
      lines.push_back(ln);
      continue;
    }
    std::string part;
    std::istringstream ps(ln);
    while (std::getline(ps, part, ';')) {
      const size_t pb = part.find_first_not_of(" \t");
      if (pb != std::string::npos) lines.push_back(part.substr(pb));
    }
  }
  std::lock_guard<std::mutex> lock(s.qmu);
  if (front) {
    for (auto it = lines.rbegin(); it != lines.rend(); ++it) s.queue.push_front(*it);
  } else {
    for (auto& l : lines) s.queue.push_back(l);
  }
}

//--- Host commands ----------------------------------------------------------

// Returns true when the line was a host command (handled here), false to run it in Lua.
inline bool run_host_command(const std::string& line, uint8_t* base) {
  State& s = S();
  const auto W = split_words(line);
  if (W.empty()) return true;
  std::string name = W[0];
  for (auto& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

  if (name == "echo") {
    const size_t p = line.find_first_of(" \t");
    out("echo", p == std::string::npos ? "" : line.substr(p + 1));
    return true;
  }
  if (name == "help") {
    out("help", "host commands: wait waitfor exec echo peek poke expect find <number> freeze unfreeze trace status cvars dumpmem. "
                "Chain with ';'. Lua commands follow (their text appears in the log).");
    return false;  // Lua prints its own command list too
  }
  if (name == "hosthelp") {
    out("help",
        "host commands: wait waitfor exec echo peek poke expect find freeze unfreeze trace status cvars dumpmem. "
        "Everything else (warp, god, gold, giveall, ...) runs in Lua: type help. "
        "Separate commands with ';'. Addresses are hex guest addresses.");
    return true;
  }
  if (name == "status") {
    out("status", "frames " + std::to_string(s.frames) + ", queued " + std::to_string(s.queue.size()) +
                      ", frozen " + std::to_string(s.frozen.size()) + ", trace " +
                      (s.tracing ? "on" : "off") + ", errors " + std::to_string(s.errors));
    return true;
  }
  if (name == "wait") {
    if (W.size() < 2) {
      out("wait", "usage: wait <frames> | <n>ms | <n>s", true);
      return true;
    }
    bool is_fr = false;
    uint64_t fr = 0;
    Clock::time_point dl{};
    if (!parse_duration(W[1], is_fr, fr, dl)) {
      out("wait", "bad time '" + W[1] + "'", true);
      return true;
    }
    if (is_fr) {
      s.wait_frames = fr;
      s.wait_time = false;
    } else {
      s.wait_until = dl;
      s.wait_time = true;
      s.wait_frames = 0;
    }
    return true;
  }
  if (name == "waitfor" || name == "until") {
    // waitfor ram <addr> [op] <value> [size] [timeout <t>]
    if (W.size() < 4 || W[1] != "ram") {
      out("waitfor", "usage: waitfor ram <addr> [op] <value> [size] [timeout <t>]  (ops == != < > <= >= &)", true);
      return true;
    }
    size_t i = 2;
    if (!parse_addr(W[i++], s.wf_addr)) {
      out("waitfor", "bad address", true);
      return true;
    }
    s.wf_op = "==";
    if (i < W.size() && is_op(W[i])) s.wf_op = W[i++];
    if (i >= W.size() || !parse_i(W[i], s.wf_value)) {
      out("waitfor", "bad value", true);
      return true;
    }
    s.wf_size = implied_size(W[i++]);
    if (i < W.size() && W[i] != "timeout") {
      if (!parse_size(W[i++], s.wf_size)) {
        out("waitfor", "bad size", true);
        return true;
      }
    }
    Clock::time_point dl = Clock::now() + std::chrono::seconds(30);
    if (i + 1 < W.size() && W[i] == "timeout") {
      bool is_fr = false;
      uint64_t fr = 0;
      Clock::time_point d{};
      if (parse_duration(W[i + 1], is_fr, fr, d)) dl = is_fr ? Clock::now() + std::chrono::milliseconds(fr * 17) : d;
    }
    s.wf_deadline = dl;
    s.waitfor = true;
    return true;
  }
  if (name == "exec") {
    if (W.size() < 2) {
      out("exec", "usage: exec <file>", true);
      return true;
    }
    fs::path p = W[1];
    if (p.is_relative()) p = s.root / "console" / p;
    std::ifstream f(p, std::ios::binary);
    if (!f) {
      out("exec", "cannot open " + p.string(), true);
      return true;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    enqueue_text(ss.str(), true);
    out("exec", "queued " + p.filename().string());
    return true;
  }
  if (name == "peek") {
    uint32_t a;
    uint64_t n = 16;
    if (W.size() < 2 || !parse_addr(W[1], a)) {
      out("peek", "usage: peek <addr> [bytes]", true);
      return true;
    }
    if (W.size() > 2 && !parse_u(W[2], n)) {
      out("peek", "bad length", true);
      return true;
    }
    if (n == 0 || n > 256) n = 256;
    if (!ta::rdable2(base, a, n)) {
      out("peek", hex(a, 8) + " not readable", true);
      return true;
    }
    for (uint64_t off = 0; off < n; off += 16) {
      std::string row = hex(a + off, 8) + ":";
      std::string asc;
      for (uint64_t j = 0; j < 16 && off + j < n; ++j) {
        const uint8_t b =
            *reinterpret_cast<const uint8_t*>(ta::host_addr(base, a + static_cast<uint32_t>(off + j)));
        row += " " + hex(b, 2);
        asc += (b >= 32 && b < 127) ? static_cast<char>(b) : '.';
      }
      out("peek", row + "  " + asc);
    }
    return true;
  }
  if (name == "poke" || name == "expect" || name == "freeze") {
    uint32_t a;
    int64_t v;
    if (W.size() < 3 || !parse_addr(W[1], a) || !parse_i(W[2], v)) {
      out(name, "usage: " + name + " <addr> <value> [8|16|32]", true);
      return true;
    }
    uint32_t sz = implied_size(W[2]);
    if (W.size() > 3 && !parse_size(W[3], sz)) {
      out(name, "bad size", true);
      return true;
    }
    if ((a % sz) != 0) {
      out(name, hex(a, 8) + " not aligned to " + std::to_string(sz * 8) + " bits", true);
      return true;
    }
    const uint64_t mask = sz == 4 ? 0xFFFFFFFFull : (sz == 2 ? 0xFFFFull : 0xFFull);
    const uint64_t uv = static_cast<uint64_t>(v) & mask;
    if (name == "poke") {
      uint64_t back = 0;
      if (!write_be(base, a, sz, uv) || !read_be(base, a, sz, back)) {
        out("poke", hex(a, 8) + " not writable", true);
        return true;
      }
      out("poke", "ok: " + hex(a, 8) + " = 0x" + hex(back, sz * 2));
    } else if (name == "expect") {
      uint64_t cur = 0;
      if (!read_be(base, a, sz, cur)) {
        out("expect", hex(a, 8) + " not readable", true);
        return true;
      }
      if (cur == uv) out("expect", "ok: " + hex(a, 8) + " = 0x" + hex(cur, sz * 2));
      else out("expect", hex(a, 8) + " = 0x" + hex(cur, sz * 2) + ", expected 0x" + hex(uv, sz * 2), true);
    } else {
      if (!ta::wr2(base, a, sz)) {
        out("freeze", hex(a, 8) + " not writable", true);
        return true;
      }
      s.frozen[a] = {uv, sz};
      out("freeze", "ok: " + hex(a, 8) + " held at 0x" + hex(uv, sz * 2));
    }
    return true;
  }
  if (name == "unfreeze") {
    if (W.size() < 2) {
      out("unfreeze", "usage: unfreeze <addr>|all", true);
      return true;
    }
    if (W[1] == "all") {
      s.frozen.clear();
      out("unfreeze", "ok: all");
      return true;
    }
    uint32_t a;
    if (!parse_addr(W[1], a)) {
      out("unfreeze", "bad address", true);
      return true;
    }
    out("unfreeze", s.frozen.erase(a) ? "ok" : "not frozen");
    return true;
  }
  if (name == "find") {
    // A number searches memory; "find hero" (a word) stays the Lua name search.
    uint64_t probe;
    if (W.size() < 2 || !parse_u(W[1], probe, 0)) return false;
    uint32_t sz = implied_size(W[1]);
    size_t i = 2;
    if (i < W.size() && parse_size(W[i], sz)) ++i;
    uint32_t from = 0x40000000, to = 0x50000000;
    if (i < W.size() && !parse_addr(W[i++], from)) {
      out("find", "bad from", true);
      return true;
    }
    if (i < W.size() && !parse_addr(W[i++], to)) {
      out("find", "bad to", true);
      return true;
    }
    uint64_t hits = 0;
    std::string first;
    for (uint64_t a = from; a + sz <= to; a += sz) {
      if (!ta::rdable2(base, static_cast<uint32_t>(a), sz)) {
        a |= 0xFFF;
        continue;
      }
      uint64_t cur;
      if (read_be(base, static_cast<uint32_t>(a), sz, cur) && cur == probe) {
        if (hits < 32) first += " " + hex(a, 8);
        ++hits;
      }
    }
    out("find", std::to_string(hits) + " hits in " + hex(from, 8) + "-" + hex(to, 8) + ":" + first);
    return true;
  }
  if (name == "trace") {
    if (W.size() >= 2 && W[1] == "off") {
      s.tracing = false;
      out("trace", "off");
      return true;
    }
    uint32_t a;
    uint64_t bytes = 4, lines = 200;
    if (W.size() < 2 || !parse_addr(W[1], a)) {
      out("trace", "usage: trace <addr> [bytes] [lines] | off", true);
      return true;
    }
    if (W.size() > 2) parse_u(W[2], bytes);
    if (W.size() > 3) parse_u(W[3], lines);
    if (bytes < 4) bytes = 4;
    if (bytes > 256) bytes = 256;
    if ((a % 4) != 0) {
      out("trace", "address must be 4-byte aligned", true);
      return true;
    }
    s.tr_addr = a;
    s.tr_words = static_cast<uint32_t>(bytes / 4);
    s.tr_lines_left = static_cast<int>(lines);
    s.tr_prev.assign(s.tr_words, 0);
    for (uint32_t k = 0; k < s.tr_words; ++k) {
      uint64_t v = 0;
      read_be(base, a + 4 * k, 4, v);
      s.tr_prev[k] = static_cast<uint32_t>(v);
    }
    s.tracing = true;
    out("trace", "on: " + hex(a, 8) + " +" + std::to_string(bytes) + " bytes, " + std::to_string(lines) +
                     " lines max");
    return true;
  }
  if (name == "cvars") {
    // cvars [filter]: every SDK/game cvar with value, default, range and description.
    // Written to console\\cvars.txt. Change one by typing its name and a value in the ~ console.
    std::string filter = W.size() > 1 ? W[1] : "";
    for (auto& c : filter) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::error_code ec;
    fs::create_directories(s.root / "console", ec);
    std::ofstream f(s.root / "console" / "cvars.txt", std::ios::trunc);
    size_t n = 0;
    for (const auto& e : rex::cvar::GetRegistry()) {
      std::string low = e.name + " " + e.category + " " + e.description;
      for (auto& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if (!filter.empty() && low.find(filter) == std::string::npos) continue;
      ++n;
      f << e.name << " = ";
      if (e.type == rex::cvar::FlagType::Command) f << "<command>";
      else f << (e.getter ? e.getter() : std::string("?"));
      f << "   [" << e.category << "] default=" << e.default_value;
      if (e.constraints.min) f << " min=" << *e.constraints.min;
      if (e.constraints.max) f << " max=" << *e.constraints.max;
      if (!e.constraints.allowed_values.empty()) {
        f << " allowed=";
        for (const auto& a : e.constraints.allowed_values) f << a << "|";
      }
      f << "  -- " << e.description << "\n";
    }
    out("cvars", std::to_string(n) + " written to " + (s.root / "console" / "cvars.txt").string() +
                     ". Set one by typing its name and a value in the ~ console.");
    return true;
  }
  if (name == "dumpmem") {
    // dumpmem <addr> <bytes> [file]: raw guest memory to console\\<file> (default mem-<addr>.bin).
    uint32_t a;
    uint64_t n;
    if (W.size() < 3 || !parse_addr(W[1], a) || !parse_u(W[2], n, 0) || n == 0 || n > (64ull << 20)) {
      out("dumpmem", "usage: dumpmem <addr> <bytes up to 64MB> [file]", true);
      return true;
    }
    if (!ta::rdable2(base, a, n)) {
      out("dumpmem", hex(a, 8) + " +" + std::to_string(n) + " not fully readable", true);
      return true;
    }
    std::error_code ec;
    fs::create_directories(s.root / "console", ec);
    const std::string fname = W.size() > 3 ? W[3] : "mem-" + hex(a, 8) + ".bin";
    const fs::path p = s.root / "console" / fs::path(fname).filename();
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(ta::host_addr(base, a)), static_cast<std::streamsize>(n));
    out("dumpmem", "ok: " + std::to_string(n) + " bytes -> " + p.string());
    return true;
  }
  return false;
}

//--- Per-frame entry --------------------------------------------------------

inline void write_state_file() {
  State& s = S();
  std::error_code ec;
  fs::create_directories(s.root / "console", ec);
  const fs::path tmp = s.root / "console" / "state.tmp";
  {
    std::ofstream f(tmp, std::ios::trunc);
    f << "build " << FABLE2_CONSOLE_BUILD << "\nframes " << s.frames << "\nqueued " << s.queue.size()
      << "\nbusy " << ((!s.queue.empty() || s.waitfor || s.wait_frames || s.wait_time) ? 1 : 0)
      << "\nerrors " << s.errors << "\nlast " << s.last << "\n";
  }
  fs::rename(tmp, s.root / "console" / "state.txt", ec);
}

inline void run_lua_line(const std::string& line, PPCContext& ctx, uint8_t* base) {
  if (line.find("]==]") != std::string::npos) {
    out("lua", "line contains ]==] and was refused", true);
    return;
  }
  const auto run = S().root / "data" / "scripts" / "recomp" / "console_run.lua";
  {
    std::ofstream f(run, std::ios::binary | std::ios::trunc);
    f << "local fn = loadfile(\"scripts/recomp/console_cmds.lua\")\n"
         "if fn then fn([==["
      << line << "]==]) else pcall(cprint, \"console: console_cmds.lua not found\") end\n";
  }
  const PPCContext saved = ctx;
  try {
    const std::string path = "scripts/recomp/console_run.lua";
    f5lua::run_external(ctx, base, &path);
  } catch (...) {
  }
  ctx = saved;
}

// Called every frame from the MainRenderLoop hook, right after the F5 poll.
inline void poll(PPCContext& ctx, uint8_t* base) {
  State& s = S();
  const bool file_mode = enabled();
  if (!file_mode && !s.active.load(std::memory_order_relaxed)) return;
  if (s.root.empty()) return;
  ++s.frames;
  if (s.frames == 1)
    f5lua::logline("[console] %s active, root=%s\n", FABLE2_CONSOLE_BUILD, s.root.string().c_str());

  // New input file (checked every 10 frames; cheap).
  if (file_mode && (s.frames % 10) == 0) {
    std::error_code ec;
    const auto in = s.root / "console" / "in.txt";
    if (fs::exists(in, ec)) {
      std::string text;
      {
        std::ifstream f(in, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        text = ss.str();
      }
      fs::remove(in, ec);
      enqueue_text(text);
    }
  }

  // Freeze: rewrite held values.
  for (auto& kv : s.frozen) write_be(base, kv.first, kv.second.size, kv.second.value);

  // Trace: print changed words.
  if (s.tracing) {
    for (uint32_t k = 0; k < s.tr_words && s.tr_lines_left > 0; ++k) {
      uint64_t v = 0;
      if (!read_be(base, s.tr_addr + 4 * k, 4, v)) continue;
      if (static_cast<uint32_t>(v) != s.tr_prev[k]) {
        out("trace", hex(s.tr_addr, 8) + " f" + std::to_string(s.frames) + " +" + std::to_string(4 * k) +
                         " 0x" + hex(s.tr_prev[k], 8) + "->0x" + hex(v, 8));
        s.tr_prev[k] = static_cast<uint32_t>(v);
        --s.tr_lines_left;
      }
    }
    if (s.tr_lines_left <= 0) {
      s.tracing = false;
      out("trace", "line limit reached, off");
    }
  }

  // Pending waits hold the queue; otherwise run one command per frame.
  if (s.wait_frames > 0) {
    --s.wait_frames;
  } else if (s.wait_time) {
    if (Clock::now() >= s.wait_until) s.wait_time = false;
  } else if (s.waitfor) {
    uint64_t cur = 0;
    bool met = false;
    if (read_be(base, s.wf_addr, s.wf_size, cur)) {
      int64_t sc;
      if (s.wf_size == 1) sc = static_cast<int8_t>(cur);
      else if (s.wf_size == 2) sc = static_cast<int16_t>(cur);
      else sc = static_cast<int32_t>(cur);
      met = compare(sc, s.wf_op, s.wf_value);
    }
    if (met) {
      s.waitfor = false;
      out("waitfor", "met: " + hex(s.wf_addr, 8));
    } else if (Clock::now() >= s.wf_deadline) {
      s.waitfor = false;
      out("waitfor", "timed out: " + hex(s.wf_addr, 8), true);
    }
  } else {
    std::string line;
    {
      std::lock_guard<std::mutex> lock(s.qmu);
      if (!s.queue.empty()) {
        line = s.queue.front();
        s.queue.pop_front();
      }
    }
    if (!line.empty()) {
      if (line[0] == '!' || !run_host_command(line, base)) run_lua_line(line, ctx, base);
      bool empty;
      {
        std::lock_guard<std::mutex> lock(s.qmu);
        empty = s.queue.empty();
      }
      if (empty && !s.waitfor && !s.wait_frames && !s.wait_time) out("console", "IDLE");
    }
  }

  if ((s.frames % 60) == 0) write_state_file();
}

}  // namespace fable2::console

// `f2 <command>` in the SDK's `~` console. One registered name, so it cannot
// collide with the SDK's own cvars and commands (echo, find, ...). Skipped when
// the SDK has no argument-taking command macro.
#if defined(REXCVAR_DEFINE_COMMAND_ARGS)
namespace fable2::console {
inline void f2_command(std::string_view args) {
  S().active.store(true, std::memory_order_relaxed);
  std::string text(args);
  if (text.find_first_not_of(" \t") == std::string::npos) text = "help";
  enqueue_text(text);
}
}  // namespace fable2::console
REXCVAR_DEFINE_COMMAND_ARGS(f2, fable2::console::f2_command, "Fable2",
                            "Fable 2 dev commands: f2 help | f2 warp <x> <y> <z> | f2 god on | f2 peek <addr>");
#endif
