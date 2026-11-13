// fable_2 - developer commands, console-v11.
//
// Typed in the SDK's `~` console as   f2 <command>   for example
//   f2 warp 1 2 3        f2 god on        f2 warp save a; wait 60; warp a
// (chain commands with ';' inside one f2 line).
// Commands only run when one is typed or written to the file transport, so a
// launch without commands behaves as before.
//
// File transport (on by default: console_enabled; FABLE2_CONSOLE=0 or
// console_enabled = false turns it off):
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
// freeze, unfreeze, trace, status, hosthelp, cvars, dumpmem; v6 adds cvar,
// history, again, repeat, every, autoexec, peekf, pokef, freezef, peekstr,
// findstr, findf, fill, scan; v11 adds widescreen (ws), pause and step. Addresses are Xbox 360 guest
// virtual addresses (hex, with or without 0x); values are big-endian as in the game.

#pragma once

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "fable2_f5_lua.h"
#include "fable2_fit_window.h"
#include "fable2_gui_lua.h"
#include "remote_input_state.h"
#include "photo_mode_state.h"

// The SDK's `~` console (ImGui overlay) logs through REXLOG_* and can register
// commands with REXCVAR_DEFINE_COMMAND_ARGS (rex/cvar.h). Both are optional here
// so the file still builds against an older SDK; see the bottom of this file.
#if __has_include(<rex/logging.h>)
#include <rex/logging.h>
#define FABLE2_CONSOLE_HAVE_REXLOG 1
#endif

#define FABLE2_CONSOLE_BUILD "console-v11"

REXCVAR_DEFINE_BOOL(console_enabled, true, "Input",
                    "Developer command file transport (console\\in.txt / out.txt / state.txt "
                    "next to the game data). On by default; false or FABLE2_CONSOLE=0 turns it off.");

namespace fable2::console {

// Console gamepad: `pad` presses guest buttons through a synthetic pad that
// fable_2_app.h always registers (the remote-pad driver, fed from this store),
// so menus and the intro can be driven from console\in.txt or autoexec.txt
// without window focus.
inline fable2::remote::InputStateStore& PadStore() {
  static fable2::remote::InputStateStore s;
  return s;
}
inline std::atomic<bool>& PadEnabled() {
  static std::atomic<bool> on{true};
  return on;
}

// Every UI aspect change (window resize, ws on/off/aspect) goes to all GUI
// screens as __fitwin_A, which the fit-window layout script re-reads each
// frame, so open menus and the HUD follow it without reloading.
inline void send_ui_aspect(float aspect) {
  char code[64];
  std::snprintf(code, sizeof(code), "__fitwin_A = %.5f", aspect);
  fable2::guilua::enqueue_all("fitwin_A", code);
}
inline const bool kUiAspectListener = (fable2::fitwindow::SetAspectListener(&send_ui_aspect), true);

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
  // History (newest last) for `history` / `again`.
  std::deque<std::string> history;
  // `every`: commands re-queued on a period (frames or real time).
  struct Every {
    bool is_frames;
    uint64_t period_frames;
    Clock::duration period_time;
    uint64_t next_frame;
    Clock::time_point next_time;
    std::string cmd;
  };
  std::vector<Every> every;
  // `scan`: value scanner candidates (address + last value seen).
  struct ScanHit {
    uint32_t addr;
    uint64_t prev;
  };
  std::vector<ScanHit> scan;
  uint32_t scan_size = 4;
  bool scan_float = false;
  bool scan_active = false;
  bool autoexec_done = false;
  // Lua command pump (console_pump.lua): the game runs it inside its own
  // script update; the cprint hook sets pump_ready / lua_done from its markers.
  std::atomic<bool> pump_ready{false};
  bool pump_requested = false;
  Clock::time_point pump_req_time{};
  std::string lua_pending;      // a Lua line held until the pump is ready
  uint64_t lua_seq = 0;
  std::atomic<uint64_t> lua_done{0};
  bool lua_wait = false;        // a dispatched line has not reported done yet
  Clock::time_point lua_deadline{};
  uint64_t pad_frames = 0;      // `pad` releases its buttons when this reaches 0
  // pause: the game thread waits at the end of the console poll; see hold_paused.
  bool paused = false;
  uint64_t step_frames = 0;     // frames to run before pausing again ("step")
};
inline State& S() {
  static State s;
  return s;
}

inline void set_root(const fs::path& p) { S().root = p; }

inline bool enabled() {
  static const int env = [] {
    const char* e = std::getenv("FABLE2_CONSOLE");
    return e && *e ? (*e != '0' ? 1 : 0) : -1;  // -1 = not set
  }();
  if (env >= 0) return env == 1;
  return REXCVAR_GET(console_enabled);
}

//--- Output -----------------------------------------------------------------

inline void out(const std::string& tag, const std::string& msg, bool err = false) {
  State& s = S();
  // Called from the console thread and, through the cprint hook, from the
  // game's script thread: serialise the shared string and file appends.
  static std::mutex out_mu;
  std::lock_guard<std::mutex> lock(out_mu);
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

// Guest floats are big-endian IEEE-754; read_be/write_be already swap bytes.
inline float bits_to_float(uint32_t u) {
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}
inline uint32_t float_to_bits(float f) {
  uint32_t u;
  std::memcpy(&u, &f, 4);
  return u;
}
inline bool parse_f(const std::string& s, double& v) {
  if (s.empty()) return false;
  char* end = nullptr;
  v = std::strtod(s.c_str(), &end);
  return end && *end == 0;
}
inline std::string fstr(double v) {
  char buf[48];
  std::snprintf(buf, sizeof buf, "%.6g", v);
  return buf;
}

// The raw text after the first `n` words of `line` (keeps inner spacing).
inline std::string rest_after(const std::string& line, size_t n) {
  size_t p = 0;
  for (size_t i = 0; i < n; ++i) {
    p = line.find_first_not_of(" \t", p);
    if (p == std::string::npos) return "";
    p = line.find_first_of(" \t", p);
    if (p == std::string::npos) return "";
  }
  p = line.find_first_not_of(" \t", p);
  return p == std::string::npos ? "" : line.substr(p);
}

// Value of a scan candidate as a signed number of its size (floats as their bits).
inline int64_t as_signed(uint64_t v, uint32_t size) {
  if (size == 1) return static_cast<int8_t>(v);
  if (size == 2) return static_cast<int16_t>(v);
  return static_cast<int32_t>(v);
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

//--- GameFace (GUI) Lua commands ---------------------------------------------

// A Lua long-bracket string literal for `s` (empty optional if it cannot be quoted).
inline bool lua_str(const std::string& s, std::string& lit) {
  if (s.find("]==]") != std::string::npos) return false;
  lit = "[==[" + s + "]==]";
  return true;
}
// A typed word as a Lua value: number, true/false, or a string.
inline std::string lua_val(const std::string& w) {
  double d;
  if (parse_f(w, d)) return fstr(d);
  if (w == "true" || w == "on") return "true";
  if (w == "false" || w == "off") return "false";
  std::string lit;
  return lua_str(w, lit) ? lit : "nil";
}

// gui <lua> | gui status|list|use <n|auto>, and the ui sub-commands that edit
// element attributes in the GUI state. Returns false for ui sub-commands that
// console_cmds.lua handles.
inline bool run_gui_command(const std::string& name, const std::vector<std::string>& W, const std::string& line,
                            uint8_t* base) {
  namespace gl = fable2::guilua;
  const std::string sub = W.size() > 1 ? W[1] : "";
  auto el = [&](size_t i, std::string& lit) {
    if (W.size() <= i || !lua_str(W[i], lit)) {
      out(name, "missing element name (ui tree lists them)", true);
      return false;
    }
    return true;
  };
  auto num = [&](size_t i, double& v) { return W.size() > i && parse_f(W[i], v); };
  // Chunks run on whichever GameFace state calls a hooked native next and
  // has g_HUD (see guilua::drain); no per-state probing.
  auto send = [&](const std::string& code) { gl::enqueue(code); };

  if (name == "gui") {
    // "gui [on|off]" is the older Lua command (Debug.SetDrawGUI); leave it to Lua.
    if (sub.empty() || sub == "on" || sub == "off" || sub == "true" || sub == "false") return false;
    if (sub == "help") {
      out("gui", "gui <lua> runs Lua in the GameFace GUI state (return a value to see it); gui status | gui list | "
                 "gui use <n|auto> | gui where any|hud|<lua condition>. Element helpers: __f2.need(name), __f2.desc(name, el), getElement, "
                 "getAttribute, setAttribute, getChildren.");
      return true;
    }
    if (sub == "status") {
      out("gui", gl::status_line(base));
      return true;
    }
    if (sub == "list") {
      auto& gs = gl::S();
      if (gs.states.empty()) out("gui", "no GameFace states captured yet (load a save)");
      for (size_t i = 0; i < gs.states.size(); ++i) {
        out("gui", std::to_string(i + 1) + ": 0x" + hex(gs.states[i], 8) +
                       (gl::valid_state(base, gs.states[i]) ? "" : " (closed)") +
                       (gs.states[i] == gs.hud ? " (last answered)" : ""));
      }
      return true;
    }
    if (sub == "where") {
      // gui where any|hud|<lua condition>: which GameFace states run chunks.
      // any (default): every screen; a chunk whose element is missing moves
      // on to the next state. e.g. gui where getElement('PressStart') ~= nil
      auto& gs = gl::S();
      std::string w = rest_after(line, 2);
      if (w == "hud") w = "g_HUD ~= nil";
      else if (w == "any") w = "true";
      std::lock_guard<std::mutex> lock(gs.mu);
      if (!w.empty()) gs.where = w;
      out("gui", "chunks run where: " + gs.where);
      return true;
    }
    if (sub == "use" && W.size() > 2) {
      auto& gs = gl::S();
      uint64_t n;
      if (W[2] == "auto") gs.chosen = 0;
      else if (parse_u(W[2], n) && n >= 1 && n <= gs.states.size()) gs.chosen = gs.states[n - 1];
      else return out("gui", "usage: gui use <n from gui list>|auto", true), true;
      out("gui", "target 0x" + hex(gl::target_state(base), 8));
      return true;
    }
    send(rest_after(line, 1));
    return true;
  }

  // ui sub-commands that need the GUI state.
  std::string e, lit;
  double a = 0, b = 0, c = 0, d = 0;
  if (sub == "tree") {
    double depth = 3;
    std::string root = "Scene";
    if (W.size() > 2 && !parse_f(W[2], depth)) root = W[2];
    if (W.size() > 3) parse_f(W[3], depth);
    if (!lua_str(root, lit)) return true;
    send("local o = {} __f2.tree(__f2.need(" + lit + "), " + fstr(depth) + ", o, 0, nil) return table.concat(o, '\\n')");
    return true;
  }
  if (sub == "get") {
    if (!el(2, e)) return true;
    if (W.size() <= 3) return send("return __f2.desc(" + e + ", __f2.need(" + e + "))"), true;
    std::string code = "local x = __f2.need(" + e + ") local o = {}";
    for (size_t i = 3; i < W.size(); ++i) {
      if (!lua_str(W[i], lit)) continue;
      code += " o[#o+1] = " + lit + " .. '=' .. tostring(__f2.get(x, " + lit + "))";
    }
    send(code + " return table.concat(o, ' ')");
    return true;
  }
  if (sub == "pos" || sub == "move") {
    if (!el(2, e) || !num(3, a) || !num(4, b)) return out("ui", "usage: ui " + sub + " <element> <x> <y>", true), true;
    if (sub == "pos")
      send("local x = __f2.need(" + e + ") __f2.set(" + e + ", x, 'position.x', " + fstr(a) + ") __f2.set(" + e +
           ", x, 'position.y', " + fstr(b) + ") return __f2.desc(" + e + ", x)");
    else
      send("local x = __f2.need(" + e + ") __f2.set(" + e + ", x, 'position.x', (__f2.get(x, 'position.x') or 0) + " +
           fstr(a) + ") __f2.set(" + e + ", x, 'position.y', (__f2.get(x, 'position.y') or 0) + " + fstr(b) +
           ") return __f2.desc(" + e + ", x)");
    return true;
  }
  if (sub == "scale") {
    if (!el(2, e) || !num(3, a)) return out("ui", "usage: ui scale <element> <sx> [sy]", true), true;
    if (!num(4, b)) b = a;
    send("local x = __f2.need(" + e + ") __f2.set(" + e + ", x, 'scale.x', " + fstr(a) + ") __f2.set(" + e +
         ", x, 'scale.y', " + fstr(b) + ") return __f2.desc(" + e + ", x)");
    return true;
  }
  if (sub == "rotate" || sub == "opacity") {
    if (!el(2, e) || !num(3, a)) return out("ui", "usage: ui " + sub + " <element> <value>", true), true;
    const char* attr = sub == "rotate" ? "rotation.z" : "opacity";
    send("local x = __f2.need(" + e + ") __f2.set(" + e + ", x, '" + attr + "', " + fstr(a) + ") return __f2.desc(" + e +
         ", x)");
    return true;
  }
  if (sub == "visible") {
    if (!el(2, e) || W.size() < 4) return out("ui", "usage: ui visible <element> on|off", true), true;
    send("local x = __f2.need(" + e + ") __f2.set(" + e + ", x, 'active', " + lua_val(W[3]) + ") return __f2.desc(" + e +
         ", x)");
    return true;
  }
  if (sub == "tint" || sub == "textcolor") {
    if (!el(2, e) || !num(3, a) || !num(4, b) || !num(5, c))
      return out("ui", "usage: ui " + sub + " <element> <r> <g> <b>" + (sub == "tint" ? " [a]" : ""), true), true;
    const std::string p = sub == "tint" ? "diffuse." : "textcolor.";
    std::string code = "local x = __f2.need(" + e + ") __f2.set(" + e + ", x, '" + p + "r', " + fstr(a) + ") __f2.set(" +
                       e + ", x, '" + p + "g', " + fstr(b) + ") __f2.set(" + e + ", x, '" + p + "b', " + fstr(c) + ")";
    if (sub == "tint" && num(6, d)) code += " __f2.set(" + e + ", x, 'diffuse.a', " + fstr(d) + ")";
    send(code + " return 'set'");
    return true;
  }
  if (sub == "label") {
    const std::string text = rest_after(line, 3);
    if (!el(2, e) || text.empty() || !lua_str(text, lit))
      return out("ui", "usage: ui label <element> <free text>", true), true;
    send("local x = __f2.need(" + e + ") __f2.set(" + e + ", x, 'textstring', " + lit + ") return 'text set'");
    return true;
  }
  if (sub == "attr") {
    if (!el(2, e) || W.size() < 5 || !lua_str(W[3], lit))
      return out("ui", "usage: ui attr <element> <attribute> <value>", true), true;
    send("local x = __f2.need(" + e + ") __f2.set(" + e + ", x, " + lit + ", " + lua_val(rest_after(line, 4)) +
         ") return " + lit + " .. ' = ' .. tostring(__f2.get(x, " + lit + "))");
    return true;
  }
  if (sub == "hudscale" || sub == "hudopacity") {
    if (!num(2, a)) return out("ui", "usage: ui " + sub + " <value>", true), true;
    if (sub == "hudscale")
      send("local x = __f2.need('Layer') __f2.set('Layer', x, 'scale.x', " + fstr(a) + ") __f2.set('Layer', x, 'scale.y', " +
           fstr(a) + ") return __f2.desc('Layer', x)");
    else
      send("local x = __f2.need('Layer') __f2.set('Layer', x, 'opacity', " + fstr(a) + ") return __f2.desc('Layer', x)");
    return true;
  }
  if (sub == "reset") {
    if (W.size() < 3) return out("ui", "usage: ui reset <element>|all", true), true;
    std::string which = "nil";
    if (W[2] != "all" && !lua_str(W[2], which)) return true;
    send("local n = 0 for name, attrs in pairs(__f2.orig) do if " + which +
         " == nil or name == " + which + " then local x = __f2.names[name] for k, v in pairs(attrs) do "
         "pcall(setAttribute, x, k, v) n = n + 1 end __f2.orig[name] = nil end end return n .. ' attributes restored'");
    return true;
  }
  if (sub == "export") {
    send("local o = {} for name, attrs in pairs(__f2.orig) do local x = __f2.names[name] for k in pairs(attrs) do "
         "o[#o+1] = 'ui attr ' .. name .. ' ' .. k .. ' ' .. tostring(__f2.get(x, k)) end end "
         "if #o == 0 then return 'nothing changed' end "
         "return table.concat(o, '\\n') .. '\\n(copy these lines into console\\\\autoexec.txt to reapply)'");
    return true;
  }
  return false;
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
    out("help", "host commands: wait waitfor exec echo peek poke expect find <number> freeze unfreeze trace status cvars dumpmem "
                "cvar history again repeat every autoexec peekf pokef freezef peekstr findstr findf fill scan widescreen pause step pad. "
                "Chain with ';'. Lua commands follow (their text appears in the log).");
    return false;  // Lua prints its own command list too
  }
  if (name == "hosthelp") {
    out("help",
        "host commands: wait waitfor exec echo peek poke expect find freeze unfreeze trace status cvars dumpmem "
        "cvar history again repeat every autoexec peekf pokef freezef peekstr findstr findf fill scan widescreen pause step pad. "
        "Everything else (warp, god, gold, giveall, ...) runs in Lua: type help. "
        "Separate commands with ';'. Addresses are hex guest addresses.");
    return true;
  }
  // Photo mode from the console (research sessions run without keys).
  if (name == "photo") {
    namespace pm = fable2::photomode;
    const std::string m = W.size() > 1 ? W[1] : "toggle";
    const bool on = pm::want_active().load();
    if (m == "toggle" || (m == "on" && !on) || (m == "off" && on)) pm::request_toggle();
    out("photo", std::string("photo mode ") + (pm::want_active().load() ? "requested on" : "requested off"));
    return true;
  }
  // traceworld [n]: trace the next n world updates (default 1) to
  // fable2_func_trace.log, even while photo mode has the world frozen.
  if (name == "traceworld") {
    int64_t n = 1;
    if (W.size() > 1 && !parse_i(W[1], n)) n = 1;
    fable2::photomode::trace_world_request().store(int(std::clamp<int64_t>(n, 1, 120)));
    out("traceworld", "tracing the next " + std::to_string(std::clamp<int64_t>(n, 1, 120)) + " world update(s)");
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

  //--- console-v9: GameFace (GUI) Lua -------------------------------------
  if (name == "gui" || name == "ui") {
    if (run_gui_command(name, W, line, base)) return true;
    return false;  // other ui sub-commands and "gui on|off" are Lua (console_cmds.lua)
  }

  //--- console-v6 host commands -------------------------------------------

  if (name == "history") {
    // history [n] | history save <file> | history clear
    if (W.size() > 1 && W[1] == "clear") {
      s.history.clear();
      out("history", "cleared");
      return true;
    }
    if (W.size() > 2 && W[1] == "save") {
      std::error_code ec;
      fs::create_directories(s.root / "console", ec);
      const fs::path p = s.root / "console" / fs::path(W[2]).filename();
      std::ofstream f(p, std::ios::trunc);
      for (const auto& h : s.history) f << h << "\n";
      out("history", std::to_string(s.history.size()) + " lines -> " + p.string() + " (run it with: exec " +
                         p.filename().string() + ")");
      return true;
    }
    uint64_t n = 20;
    if (W.size() > 1) parse_u(W[1], n);
    const size_t from = s.history.size() > n ? s.history.size() - static_cast<size_t>(n) : 0;
    for (size_t i = from; i < s.history.size(); ++i) out("history", std::to_string(i + 1) + ": " + s.history[i]);
    if (s.history.empty()) out("history", "(empty)");
    return true;
  }
  if (name == "again") {
    // again [n]: re-run the last command, or history entry n.
    if (s.history.empty()) {
      out("again", "history is empty", true);
      return true;
    }
    uint64_t n = s.history.size();
    if (W.size() > 1 && (!parse_u(W[1], n) || n == 0 || n > s.history.size())) {
      out("again", "no history entry " + W[1], true);
      return true;
    }
    const std::string cmd = s.history[static_cast<size_t>(n - 1)];
    out("again", cmd);
    enqueue_text(cmd, true);
    return true;
  }
  if (name == "repeat") {
    // repeat <n> <command...>: queue a command n times (one per frame).
    uint64_t n;
    const std::string cmd = rest_after(line, 2);
    if (W.size() < 3 || !parse_u(W[1], n) || n == 0 || n > 1000 || cmd.empty()) {
      out("repeat", "usage: repeat <1-1000> <command...>", true);
      return true;
    }
    std::lock_guard<std::mutex> lock(s.qmu);
    for (uint64_t i = 0; i < n; ++i) s.queue.push_front(cmd);
    return true;
  }
  if (name == "every") {
    // every <frames|Ns|Nms> <command...> | every list | every stop <n>|all
    if (W.size() > 1 && W[1] == "list") {
      for (size_t i = 0; i < s.every.size(); ++i) out("every", std::to_string(i + 1) + ": " + s.every[i].cmd);
      if (s.every.empty()) out("every", "(none)");
      return true;
    }
    if (W.size() > 2 && W[1] == "stop") {
      if (W[2] == "all") {
        s.every.clear();
        out("every", "stopped all");
        return true;
      }
      uint64_t i;
      if (!parse_u(W[2], i) || i == 0 || i > s.every.size()) {
        out("every", "no entry " + W[2], true);
        return true;
      }
      s.every.erase(s.every.begin() + static_cast<ptrdiff_t>(i - 1));
      out("every", "stopped " + W[2]);
      return true;
    }
    bool is_fr = false;
    uint64_t fr = 0;
    Clock::time_point dl{};
    const std::string cmd = rest_after(line, 2);
    if (W.size() < 3 || cmd.empty() || !parse_duration(W[1], is_fr, fr, dl) || (is_fr && fr == 0)) {
      out("every", "usage: every <frames|Ns|Nms> <command...> | every list | every stop <n>|all", true);
      return true;
    }
    State::Every e{};
    e.is_frames = is_fr;
    e.period_frames = fr;
    e.period_time = dl - Clock::now();
    if (!is_fr && e.period_time < std::chrono::milliseconds(16)) e.period_time = std::chrono::milliseconds(16);
    e.next_frame = s.frames + fr;
    e.next_time = Clock::now() + e.period_time;
    e.cmd = cmd;
    s.every.push_back(e);
    out("every", "#" + std::to_string(s.every.size()) + " every " + W[1] + ": " + cmd);
    return true;
  }
  if (name == "pause") {
    // pause [on|off|toggle]: stop the game thread at the end of the frame. The
    // window keeps the last frame; only host commands run (hold_paused).
    const std::string v = W.size() > 1 ? W[1] : "toggle";
    if (v == "on" || v == "1") s.paused = true;
    else if (v == "off" || v == "0") s.paused = false;
    else if (v == "toggle") s.paused = !s.paused;
    else return out("pause", "usage: pause [on|off|toggle]", true), true;
    s.step_frames = 0;
    out("pause", s.paused ? "paused (pause off or step <n> to continue)" : "running");
    return true;
  }
  if (name == "pad") {
    // pad <A|B|X|Y|LB|RB|LT|RT|Up|Down|Left|Right|Start|Back|L3|R3>[+...] [frames]
    // pad stick <lx> <ly> [frames]  (-32768..32767, ly + = forward)  |  pad off
    // Holds the input for frames (default 12: menus miss 6), then releases it. While held
    // the command queue waits, so "pad A; pad Down; pad A" are separate presses.
    static const std::pair<const char*, uint16_t> kBtn[] = {
        {"up", 0x0001}, {"down", 0x0002}, {"left", 0x0004}, {"right", 0x0008}, {"start", 0x0010},
        {"back", 0x0020}, {"l3", 0x0040}, {"r3", 0x0080}, {"lb", 0x0100}, {"rb", 0x0200},
        {"a", 0x1000}, {"b", 0x2000}, {"x", 0x4000}, {"y", 0x8000}};
    if (W.size() < 2) return out("pad", "usage: pad <button>[+button] [frames] | pad stick <lx> <ly> [frames] | pad off", true), true;
    fable2::remote::InputSnapshot snap;
    size_t fi = 2;
    std::string what = W[1];
    for (auto& c : what) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (what == "off") {
      PadStore().Publish({});
      s.pad_frames = 0;
      return out("pad", "released"), true;
    }
    if (what == "stick") {
      int64_t lx = 0, ly = 0;
      if (W.size() < 4 || !parse_i(W[2], lx) || !parse_i(W[3], ly)) return out("pad", "usage: pad stick <lx> <ly> [frames]", true), true;
      snap.stk_lx = static_cast<int32_t>(std::clamp<int64_t>(lx, -32768, 32767));
      snap.stk_ly = static_cast<int32_t>(std::clamp<int64_t>(ly, -32768, 32767));
      fi = 4;
    } else {
      std::stringstream ss(what);
      std::string b;
      while (std::getline(ss, b, '+')) {
        bool found = false;
        if (b == "lt") snap.left_trigger = 255, found = true;
        if (b == "rt") snap.right_trigger = 255, found = true;
        for (const auto& kv : kBtn)
          if (b == kv.first) snap.buttons |= kv.second, found = true;
        if (!found) return out("pad", "unknown input '" + b + "'", true), true;
      }
    }
    uint64_t frames = 12;
    if (W.size() > fi && (!parse_u(W[fi], frames) || frames == 0 || frames > 100000))
      return out("pad", "bad frame count", true), true;
    PadStore().Publish(snap);
    s.pad_frames = frames;
    s.wait_frames = frames + 2;  // hold, then a short gap before the next command
    s.wait_time = false;
    return true;
  }
  if (name == "step") {
    // step [n]: while paused, run n frames (default 1) and pause again.
    uint64_t n = 1;
    if (W.size() > 1 && (!parse_u(W[1], n) || n == 0 || n > 100000))
      return out("step", "usage: step [frames]", true), true;
    if (!s.paused) return out("step", "not paused (pause on first)", true), true;
    s.step_frames = n;
    out("step", "running " + std::to_string(n) + " frame(s)");
    return true;
  }
  if (name == "widescreen" || name == "ws") {
    // widescreen [status|on|off|aspect <a>|hud|native|log on|off|apply]: the
    // fit-window module (fable2_fit_window.h) while the game runs.
    namespace fw = fable2::fitwindow;
    const std::string sub = W.size() > 1 ? W[1] : "status";
    auto flag = [](const char* cvar) { return rex::cvar::GetFlagByName(cvar); };
    auto on_off = [&](const std::string& w, bool& v) {
      if (w == "on" || w == "1" || w == "true") return v = true, true;
      if (w == "off" || w == "0" || w == "false") return v = false, true;
      return false;
    };
    if (sub == "status") {
      uint32_t ww = 0, wh = 0;
      fw::WindowSize(ww, wh);
      const float forced = fw::ForcedAspect();
      out("ws", std::string("fit ") + flag("fit_game_to_window") + (fw::Installed() ? "" : " (not installed)") +
                    ", window " + std::to_string(ww) + "x" + std::to_string(wh) + ", aspect " +
                    fstr(fw::CurrentAspect()) + (forced > 0 ? " (forced " + fstr(forced) + ")" : " (window)") +
                    ", guest 0x8209FE7C=" + fstr(fw::GuestAspect()) + " 0x8209FDF4=" + fstr(fw::GuestReference()));
      out("ws", std::string("hud ") + flag("fit_window_hud_to_edges") + ", native16x9 " +
                    flag("fit_window_native_16x9") + ", log " + flag("fit_window_ui_log") + ", letterbox " +
                    flag("present_letterbox"));
      return true;
    }
    if (sub == "on" || sub == "off") {
      if (!fw::SetEnabled(sub == "on")) return out("ws", "could not install; see [fit-window] in the log", true), true;
      out("ws", sub == "on" ? "fit on, aspect " + fstr(fw::CurrentAspect()) : "fit off: original 16:9");
      return true;
    }
    if (sub == "aspect") {
      if (W.size() < 3) return out("ws", "usage: ws aspect <w:h | number | auto>  e.g. 21:9, 2.39, auto", true), true;
      double a = 0;
      const std::string& v = W[2];
      const size_t colon = v.find(':');
      if (v == "auto" || v == "window") {
        a = 0;
      } else if (colon != std::string::npos) {
        double num = 0, den = 0;
        if (!parse_f(v.substr(0, colon), num) || !parse_f(v.substr(colon + 1), den) || den <= 0)
          return out("ws", "bad ratio '" + v + "'", true), true;
        a = num / den;
      } else if (!parse_f(v, a) || a < 0) {
        return out("ws", "bad aspect '" + v + "'", true), true;
      }
      if (a > 0 && (a < fw::kMinAspect || a > fw::kMaxAspect))
        out("ws", "clamped to " + fstr(fw::kMinAspect) + ".." + fstr(fw::kMaxAspect));
      fw::SetForcedAspect(static_cast<float>(a));
      if (!REXCVAR_GET(fit_game_to_window)) out("ws", "saved; fit is off (ws on applies it)");
      out("ws", a > 0 ? "aspect forced to " + fstr(fw::CurrentAspect()) : "aspect follows the window (" + fstr(fw::CurrentAspect()) + ")");
      return true;
    }
    if (sub == "apply") {
      fw::Reapply();
      if (!fw::Installed() || !REXCVAR_GET(fit_game_to_window)) fable2::guilua::enqueue_all("fitwin_A",
          "__fitwin_A = " + fstr(fw::UiAspect()));
      out("ws", "re-applied, aspect " + fstr(fw::UiAspect()));
      return true;
    }
    if (sub == "tune") {
      // ws tune <name> <factor>: a menu offset factor of the layout script
      // (__fitwin_tune), sent to every GUI screen and applied next frame.
      // Each is a multiple of the extra width per side, so 16:9 stays vanilla.
      double v = 0;
      if (W.size() < 4 || !parse_f(W[3], v))
        return out("ws", "usage: ws tune <cover|page|card|bookcover|paper> <factor>  (defaults 1, 1.2, 1.8, 1.2, 1.5)", true), true;
      std::string key;
      for (char c : W[2])
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') key += c;
      if (key.empty()) return out("ws", "bad tune name", true), true;
      fable2::guilua::enqueue_all("fitwin_tune_" + key, "if type(__fitwin_tune) ~= 'table' then __fitwin_tune = {} end "
                                  "__fitwin_tune." + key + " = " + fstr(v));
      out("ws", "tune " + key + " = " + fstr(v) + " sent to the GUI screens");
      return true;
    }
    const char* cvar = sub == "hud" ? "fit_window_hud_to_edges"
                       : sub == "native" || sub == "native16x9" ? "fit_window_native_16x9"
                       : sub == "log" ? "fit_window_ui_log" : nullptr;
    if (cvar) {
      bool v = false;
      if (W.size() < 3 || !on_off(W[2], v)) return out("ws", "usage: ws " + sub + " on|off", true), true;
      rex::cvar::SetFlagByName(cvar, v ? "true" : "false");
      out("ws", std::string(cvar) + " = " + flag(cvar) +
                    (sub == "hud" ? " (takes effect when the next UI screen loads)" : ""));
      return true;
    }
    out("ws", "usage: ws [status] | on | off | aspect <w:h|n|auto> | hud on|off | native on|off | log on|off | apply | "
              "tune <name> <factor>", true);
    return true;
  }
  if (name == "cvar") {
    // cvar <name> [value...]: print or set an SDK/game cvar (same as typing it in ~,
    // but usable from exec files, aliases and chains).
    if (W.size() < 2) {
      out("cvar", "usage: cvar <name> [value]  (cvars [filter] lists them)", true);
      return true;
    }
    const rex::cvar::FlagEntry* entry = nullptr;
    for (const auto& e : rex::cvar::GetRegistry())
      if (e.name == W[1]) entry = &e;
    if (!entry) {
      out("cvar", "unknown cvar " + W[1] + " (cvars " + W[1] + " searches)", true);
      return true;
    }
    if (entry->type == rex::cvar::FlagType::Command) {
      out("cvar", W[1] + " is a command; type it in the ~ console", true);
      return true;
    }
    if (W.size() == 2) {
      out("cvar", W[1] + " = " + rex::cvar::GetFlagByName(W[1]) + "  (default " + entry->default_value + ")");
      return true;
    }
    const std::string v = rest_after(line, 2);
    if (!rex::cvar::SetFlagByName(W[1], v)) {
      out("cvar", "rejected: " + W[1] + " = " + v, true);
      return true;
    }
    out("cvar", W[1] + " = " + rex::cvar::GetFlagByName(W[1]));
    return true;
  }
  if (name == "autoexec") {
    const fs::path p = s.root / "console" / "autoexec.txt";
    std::ifstream f(p, std::ios::binary);
    if (!f) {
      out("autoexec", "no " + p.string() + " (one command per line; runs on the first f2 command)");
      return true;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    enqueue_text(ss.str(), true);
    out("autoexec", "queued " + p.string());
    return true;
  }
  if (name == "peekf") {
    // peekf <addr> [count]: big-endian floats.
    uint32_t a;
    uint64_t n = 1;
    if (W.size() < 2 || !parse_addr(W[1], a) || (W.size() > 2 && !parse_u(W[2], n))) {
      out("peekf", "usage: peekf <addr> [count]", true);
      return true;
    }
    if (n == 0 || n > 64) n = 64;
    std::string row;
    for (uint64_t i = 0; i < n; ++i) {
      uint64_t v;
      if (!read_be(base, a + static_cast<uint32_t>(4 * i), 4, v)) {
        out("peekf", hex(a + 4 * i, 8) + " not readable", true);
        break;
      }
      row += (i % 4 == 0 ? (row.empty() ? "" : "\n") + hex(a + 4 * i, 8) + ":" : std::string()) + " " +
             fstr(bits_to_float(static_cast<uint32_t>(v)));
    }
    if (!row.empty()) out("peekf", row);
    return true;
  }
  if (name == "pokef" || name == "freezef") {
    uint32_t a;
    double v;
    if (W.size() < 3 || !parse_addr(W[1], a) || !parse_f(W[2], v) || (a % 4) != 0) {
      out(name, "usage: " + name + " <4-aligned addr> <float>", true);
      return true;
    }
    const uint32_t bits = float_to_bits(static_cast<float>(v));
    if (name == "pokef") {
      uint64_t back = 0;
      if (!write_be(base, a, 4, bits) || !read_be(base, a, 4, back)) {
        out("pokef", hex(a, 8) + " not writable", true);
        return true;
      }
      out("pokef", "ok: " + hex(a, 8) + " = " + fstr(bits_to_float(static_cast<uint32_t>(back))));
    } else {
      if (!ta::wr2(base, a, 4)) {
        out("freezef", hex(a, 8) + " not writable", true);
        return true;
      }
      s.frozen[a] = {bits, 4};
      out("freezef", "ok: " + hex(a, 8) + " held at " + fstr(v));
    }
    return true;
  }
  if (name == "peekstr") {
    // peekstr <addr> [max]: a NUL-terminated string (ASCII; other bytes shown as \xNN).
    uint32_t a;
    uint64_t mx = 256;
    if (W.size() < 2 || !parse_addr(W[1], a) || (W.size() > 2 && !parse_u(W[2], mx))) {
      out("peekstr", "usage: peekstr <addr> [max bytes]", true);
      return true;
    }
    if (mx == 0 || mx > 4096) mx = 4096;
    std::string str;
    uint64_t i = 0;
    for (; i < mx; ++i) {
      if (!ta::rdable2(base, a + static_cast<uint32_t>(i), 1)) break;
      const uint8_t b = *reinterpret_cast<const uint8_t*>(ta::host_addr(base, a + static_cast<uint32_t>(i)));
      if (b == 0) break;
      if (b >= 32 && b < 127) str += static_cast<char>(b);
      else str += "\\x" + hex(b, 2);
    }
    out("peekstr", hex(a, 8) + " (" + std::to_string(i) + " bytes): \"" + str + "\"");
    return true;
  }
  if (name == "findstr" || name == "findf") {
    // findstr <text> [from] [to] | findf <float> [tolerance] [from] [to]
    if (W.size() < 2) {
      out(name, name == "findstr" ? "usage: findstr <text> [from] [to]" : "usage: findf <float> [tol] [from] [to]", true);
      return true;
    }
    size_t i = 2;
    double fv = 0, tol = 0.001;
    if (name == "findf") {
      if (!parse_f(W[1], fv)) {
        out("findf", "bad float", true);
        return true;
      }
      if (i < W.size() && W[i].find('.') != std::string::npos && parse_f(W[i], tol)) ++i;
    }
    uint32_t from = 0x40000000, to = 0x50000000;
    if (i < W.size() && !parse_addr(W[i++], from)) {
      out(name, "bad from", true);
      return true;
    }
    if (i < W.size() && !parse_addr(W[i++], to)) {
      out(name, "bad to", true);
      return true;
    }
    uint64_t hits = 0;
    std::string first;
    if (name == "findstr") {
      const std::string& needle = W[1];
      const uint32_t len = static_cast<uint32_t>(needle.size());
      for (uint64_t a = from; a + len <= to; ++a) {
        if ((a & 0xFFF) == 0 && !ta::rdable2(base, static_cast<uint32_t>(a), 1)) {
          a |= 0xFFF;
          continue;
        }
        if (!ta::rdable2(base, static_cast<uint32_t>(a), len)) continue;
        if (std::memcmp(reinterpret_cast<const void*>(ta::host_addr(base, static_cast<uint32_t>(a))), needle.data(),
                        len) == 0) {
          if (hits < 32) first += " " + hex(a, 8);
          ++hits;
        }
      }
    } else {
      // One readability check per 4 KB page, then a straight scan of it (a
      // check per word made a 256 MB search take many minutes).
      for (uint64_t page = from & ~0xFFFull; page < to; page += 0x1000) {
        if (!ta::rdable2(base, static_cast<uint32_t>(page), 0x1000)) continue;
        const uint8_t* p = reinterpret_cast<const uint8_t*>(ta::host_addr(base, static_cast<uint32_t>(page)));
        const uint64_t lo = std::max<uint64_t>(page, (from + 3) & ~3ull), hi = std::min<uint64_t>(page + 0x1000, to);
        for (uint64_t a = lo; a + 4 <= hi; a += 4) {
          uint32_t v;
          std::memcpy(&v, p + (a - page), 4);
          const double cur = bits_to_float(__builtin_bswap32(v));
          if (cur == cur && std::abs(cur - fv) <= tol) {
            if (hits < 64) first += " " + hex(a, 8);
            ++hits;
          }
        }
      }
    }
    out(name, std::to_string(hits) + " hits in " + hex(from, 8) + "-" + hex(to, 8) + ":" + first);
    return true;
  }
  if (name == "fill") {
    // fill <addr> <bytes> <byte>
    uint32_t a;
    uint64_t n, b;
    if (W.size() < 4 || !parse_addr(W[1], a) || !parse_u(W[2], n, 0) || !parse_u(W[3], b, 0) || n == 0 ||
        n > 65536 || b > 0xFF) {
      out("fill", "usage: fill <addr> <1-65536 bytes> <byte 0-255>", true);
      return true;
    }
    if (!ta::wr2(base, a, n)) {
      out("fill", hex(a, 8) + " +" + std::to_string(n) + " not writable", true);
      return true;
    }
    std::memset(reinterpret_cast<void*>(ta::host_addr(base, a)), static_cast<int>(b), static_cast<size_t>(n));
    out("fill", "ok: " + std::to_string(n) + " bytes at " + hex(a, 8) + " = 0x" + hex(b, 2));
    return true;
  }
  if (name == "scan") {
    // Value scanner (narrow down an unknown address):
    //   scan start <value>|any [8|16|32|f] [from] [to]
    //   scan next <value> | changed | same | inc | dec | gt <v> | lt <v>
    //   scan list [n] | scan clear
    const std::string sub = W.size() > 1 ? W[1] : "";
    auto read_cand = [&](uint32_t a, uint64_t& v) { return read_be(base, a, s.scan_size, v); };
    auto matches_value = [&](uint64_t cur, const std::string& txt, const std::string& op) -> bool {
      if (s.scan_float) {
        double want;
        if (!parse_f(txt, want)) return false;
        const double c = bits_to_float(static_cast<uint32_t>(cur));
        if (op == "gt") return c > want;
        if (op == "lt") return c < want;
        return std::abs(c - want) <= 0.001;
      }
      int64_t want;
      if (!parse_i(txt, want)) return false;
      const int64_t c = as_signed(cur, s.scan_size);
      if (op == "gt") return c > want;
      if (op == "lt") return c < want;
      const uint64_t mask = s.scan_size == 4 ? 0xFFFFFFFFull : (s.scan_size == 2 ? 0xFFFFull : 0xFFull);
      return (cur & mask) == (static_cast<uint64_t>(want) & mask);
    };
    if (sub == "start") {
      if (W.size() < 3) {
        out("scan", "usage: scan start <value>|any [8|16|32|f] [from] [to]", true);
        return true;
      }
      size_t i = 3;
      s.scan_size = 4;
      s.scan_float = false;
      if (i < W.size() && W[i] == "f") {
        s.scan_float = true;
        ++i;
      } else if (i < W.size() && parse_size(W[i], s.scan_size)) {
        ++i;
      }
      uint32_t from = 0x40000000, to = 0x50000000;
      if (i < W.size() && !parse_addr(W[i++], from)) {
        out("scan", "bad from", true);
        return true;
      }
      if (i < W.size() && !parse_addr(W[i++], to)) {
        out("scan", "bad to", true);
        return true;
      }
      const bool any = W[2] == "any";
      if (any && to - from > (16u << 20)) {
        out("scan", "'any' needs a range of 16 MB or less; give from/to", true);
        return true;
      }
      s.scan.clear();
      const size_t cap = 4u << 20;
      bool capped = false;
      for (uint64_t a = from; a + s.scan_size <= to; a += s.scan_size) {
        if ((a & 0xFFF) == 0 && !ta::rdable2(base, static_cast<uint32_t>(a), 0x1000)) {
          a += 0x1000 - s.scan_size;
          continue;
        }
        uint64_t v;
        if (!read_cand(static_cast<uint32_t>(a), v)) continue;
        if (any || matches_value(v, W[2], "==")) {
          if (s.scan.size() >= cap) {
            capped = true;
            break;
          }
          s.scan.push_back({static_cast<uint32_t>(a), v});
        }
      }
      s.scan_active = true;
      out("scan", std::to_string(s.scan.size()) + " candidates (" + (s.scan_float ? "float" : std::to_string(s.scan_size * 8) + "-bit") +
                      ")" + (capped ? ", capped at 4M: narrow the range" : "") +
                      ". Change the value in game, then: scan next <value>|changed|same|inc|dec");
      return true;
    }
    if (sub == "next") {
      if (!s.scan_active || W.size() < 3) {
        out("scan", s.scan_active ? "usage: scan next <value>|changed|same|inc|dec|gt <v>|lt <v>" : "no scan; use scan start", true);
        return true;
      }
      const std::string how = W[2];
      const std::string arg = W.size() > 3 ? W[3] : "";
      std::vector<State::ScanHit> keep;
      keep.reserve(s.scan.size());
      for (const auto& h : s.scan) {
        uint64_t cur;
        if (!read_cand(h.addr, cur)) continue;
        bool ok;
        const double cf = bits_to_float(static_cast<uint32_t>(cur)), pf = bits_to_float(static_cast<uint32_t>(h.prev));
        const int64_t ci = as_signed(cur, s.scan_size), pi = as_signed(h.prev, s.scan_size);
        if (how == "changed") ok = cur != h.prev;
        else if (how == "same") ok = cur == h.prev;
        else if (how == "inc") ok = s.scan_float ? cf > pf : ci > pi;
        else if (how == "dec") ok = s.scan_float ? cf < pf : ci < pi;
        else if (how == "gt" || how == "lt") ok = matches_value(cur, arg, how);
        else ok = matches_value(cur, how, "==");
        if (ok) keep.push_back({h.addr, cur});
      }
      s.scan.swap(keep);
      out("scan", std::to_string(s.scan.size()) + " candidates left" +
                      (s.scan.size() <= 8 && !s.scan.empty() ? " (scan list shows them)" : ""));
      return true;
    }
    if (sub == "list") {
      uint64_t n = 20;
      if (W.size() > 2) parse_u(W[2], n);
      for (size_t i = 0; i < s.scan.size() && i < n; ++i) {
        uint64_t cur = 0;
        read_cand(s.scan[i].addr, cur);
        out("scan", hex(s.scan[i].addr, 8) + " = " +
                        (s.scan_float ? fstr(bits_to_float(static_cast<uint32_t>(cur)))
                                      : std::to_string(as_signed(cur, s.scan_size)) + " (0x" + hex(cur, s.scan_size * 2) + ")"));
      }
      if (s.scan.size() > n) out("scan", "... " + std::to_string(s.scan.size() - n) + " more");
      return true;
    }
    if (sub == "clear") {
      s.scan.clear();
      s.scan.shrink_to_fit();
      s.scan_active = false;
      out("scan", "cleared");
      return true;
    }
    out("scan", "usage: scan start <value>|any [8|16|32|f] [from] [to] | scan next ... | scan list [n] | scan clear", true);
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
      << "\nerrors " << s.errors << "\npaused " << (s.paused ? 1 : 0) << "\nlast " << s.last << "\n";
  }
  fs::rename(tmp, s.root / "console" / "state.txt", ec);
}

// Lua commands run inside the game's own script update (console_pump.lua), not
// from this hook: calling RunScript per command from MainRenderLoop raced the
// game's Lua and corrupted the VM. RunScript is used once, to install the pump.
inline void start_pump(PPCContext& ctx, uint8_t* base) {
  State& s = S();
  s.pump_requested = true;
  s.pump_req_time = Clock::now();
  const PPCContext saved = ctx;
  try {
    const std::string path = "scripts/recomp/console_pump.lua";
    f5lua::run_external(ctx, base, &path);
  } catch (...) {
  }
  ctx = saved;
}

// Hands one line to the pump: console_run.lua = `return <seq>, [==[line]==]`.
inline void dispatch_lua(const std::string& line) {
  State& s = S();
  // The game's Lua numbers are 4-byte floats (exact integers only up to 2^24),
  // so keep the sequence small; the time-based start avoids matching a stale
  // console_run.lua left by an earlier session.
  if (s.lua_seq == 0 || s.lua_seq > 9000000) s.lua_seq = static_cast<uint64_t>(std::time(nullptr)) % 10000 * 100;
  ++s.lua_seq;
  const auto run = s.root / "data" / "scripts" / "recomp" / "console_run.lua";
  std::error_code ec;
  fs::create_directories(s.root / "console", ec);
  const auto tmp = s.root / "console" / "run.tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    f << "return " << s.lua_seq << ", [==[" << line << "]==]\n";
  }
  fs::rename(tmp, run, ec);
  if (ec) {  // e.g. a different volume: fall back to a direct write
    std::ofstream f(run, std::ios::binary | std::ios::trunc);
    f << "return " << s.lua_seq << ", [==[" << line << "]==]\n";
  }
  s.lua_wait = true;
  s.lua_deadline = Clock::now() + std::chrono::seconds(30);
}

inline void run_lua_line(const std::string& line, PPCContext& ctx, uint8_t* base) {
  State& s = S();
  if (line.find("]==]") != std::string::npos) {
    out("lua", "line contains ]==] and was refused", true);
    return;
  }
  if (s.pump_ready.load()) {
    dispatch_lua(line);
    return;
  }
  // Pump not running yet: start it and hold this line until it reports ready.
  s.lua_pending = line;
  if (!s.pump_requested) start_pump(ctx, base);
}

// F9 (game window focused) toggles pause, so the moment to stop can be picked
// by hand. Edge-detected; checked every frame and while paused.
inline void poll_pause_key() {
#ifdef _WIN32
  static bool prev = false;
  const bool down = (GetAsyncKeyState(0x78 /* VK_F9 */) & 0x8000) != 0;
  const bool edge = down && !prev;
  prev = down;
  if (!edge) return;
  DWORD pid = 0;
  GetWindowThreadProcessId(GetForegroundWindow(), &pid);
  if (pid != GetCurrentProcessId()) return;
  State& s = S();
  s.paused = !s.paused;
  s.step_frames = 0;
  out("pause", s.paused ? "paused (F9)" : "running (F9)");
#endif
}

// Host commands that work while the game thread is held: they only read or
// write memory and console state, and pause/step end the hold.
inline bool runs_while_paused(const std::string& line) {
  static const char* kSafe[] = { "pause", "step", "status", "echo", "hosthelp", "peek", "poke", "peekf",
                                 "pokef", "peekstr", "freeze", "freezef", "unfreeze", "cvar", "cvars",
                                 "widescreen", "ws", "history", "dumpmem", "find", "findf", "findstr" };
  std::string head = line.substr(0, line.find_first_of(" \t"));
  for (auto& c : head) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  for (const char* k : kSafe)
    if (head == k) return true;
  return false;
}

// pause: keeps the game thread here (no guest frames, the window shows the
// last one) while still reading console\in.txt and the ~ queue. Commands that
// need the game running (Lua, ui/gui, wait) stay queued until it resumes.
inline void hold_paused(uint8_t* base, bool file_mode) {
  State& s = S();
  if (!s.paused || s.step_frames) return;
  write_state_file();
  auto last_state = Clock::now();
  while (s.paused && !s.step_frames) {
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
    std::error_code ec;
    const auto in = s.root / "console" / "in.txt";
    if (file_mode && fs::exists(in, ec)) {
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
    std::string line;
    {
      // The first command that can run while paused, wherever it is: a queued
      // ui/Lua command must not block the step or pause off behind it.
      std::lock_guard<std::mutex> lock(s.qmu);
      for (auto it = s.queue.begin(); it != s.queue.end(); ++it) {
        if (runs_while_paused(*it)) {
          line = *it;
          s.queue.erase(it);
          break;
        }
      }
    }
    if (!line.empty()) run_host_command(line, base);
    poll_pause_key();
    for (auto& kv : s.frozen) write_be(base, kv.first, kv.second.size, kv.second.value);
    if (Clock::now() - last_state > std::chrono::milliseconds(500)) {
      write_state_file();
      last_state = Clock::now();
    }
  }
}

// Called every frame from the MainRenderLoop hook, right after the F5 poll.
inline void poll(PPCContext& ctx, uint8_t* base) {
  // Video letterboxing (fit-window) needs a per-frame tick; it does not
  // depend on the console being on.
  fable2::fitwindow::ui::TickVideo();
  State& s = S();
  const bool file_mode = enabled();
  if (!file_mode && !s.active.load(std::memory_order_relaxed)) return;
  if (s.root.empty()) return;
  ++s.frames;
  if (s.step_frames) --s.step_frames;
  if (s.pad_frames && --s.pad_frames == 0) PadStore().Publish({});
  poll_pause_key();
  if (s.frames == 1)
    f5lua::logline("[console] %s active, root=%s\n", FABLE2_CONSOLE_BUILD, s.root.string().c_str());

  // console\autoexec.txt runs once, ahead of the command that activated the console.
  if (!s.autoexec_done) {
    s.autoexec_done = true;
    std::error_code ec;
    if (fs::exists(s.root / "console" / "autoexec.txt", ec)) run_host_command("autoexec", base);
  }

  // GUI-state chunks: run them here when GameFace Lua shares this thread, and
  // print whatever finished (here or in the GUI natives) since the last frame.
  fable2::guilua::poll(ctx, base);
  for (const auto& r : fable2::guilua::take_results()) {
    std::istringstream rs(r);
    std::string ln;
    bool first = true;
    while (std::getline(rs, ln)) {
      out("gui", ln, first && r.rfind("error", 0) == 0);
      first = false;
    }
  }

  // `every` timers queue their command when due (skipped while the queue is backed up).
  if (!s.every.empty()) {
    const auto now = Clock::now();
    for (auto& e : s.every) {
      const bool due = e.is_frames ? s.frames >= e.next_frame : now >= e.next_time;
      if (!due) continue;
      if (e.is_frames) e.next_frame = s.frames + e.period_frames;
      else e.next_time = now + e.period_time;
      std::lock_guard<std::mutex> lock(s.qmu);
      if (s.queue.size() < 256) s.queue.push_back(e.cmd);
    }
  }

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

  auto idle_if_empty = [&] {
    bool empty;
    {
      std::lock_guard<std::mutex> lock(s.qmu);
      empty = s.queue.empty();
    }
    if (empty && !s.waitfor && !s.wait_frames && !s.wait_time && !s.lua_wait && s.lua_pending.empty())
      out("console", "IDLE");
  };

  // Pending waits hold the queue; otherwise run one command per frame.
  if (!s.lua_pending.empty()) {
    // Waiting for console_pump.lua to register (it needs a loaded game).
    if (s.pump_ready.load()) {
      const std::string l = s.lua_pending;
      s.lua_pending.clear();
      dispatch_lua(l);
    } else if (Clock::now() - s.pump_req_time > std::chrono::seconds(20)) {
      out("lua", "the Lua command pump did not start (load a save first); dropped: " + s.lua_pending, true);
      s.lua_pending.clear();
      s.pump_requested = false;  // retry on the next Lua command
      idle_if_empty();
    }
  } else if (s.lua_wait) {
    if (s.lua_done.load() == s.lua_seq) {
      s.lua_wait = false;
      idle_if_empty();
    } else if (Clock::now() >= s.lua_deadline) {
      s.lua_wait = false;
      out("lua", "no reply from the Lua command pump within 30 s", true);
      idle_if_empty();
    }
  } else if (s.wait_frames > 0) {
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
      // Record typed commands for history/again (not the history commands themselves).
      const std::string head = line.substr(0, line.find_first_of(" \t"));
      if (head != "again" && head != "history" && (s.history.empty() || s.history.back() != line)) {
        s.history.push_back(line);
        if (s.history.size() > 200) s.history.pop_front();
      }
      if (line[0] == '!' || !run_host_command(line, base)) run_lua_line(line, ctx, base);
      idle_if_empty();
    }
  }

  if ((s.frames % 60) == 0) write_state_file();
  hold_paused(base, file_mode);
}

}  // namespace fable2::console

// The game Lua's native cprint/print (0x82A19CD8, registered by the script API
// setup at 0x82A19FC4). In release builds its text goes nowhere, so the console's
// Lua replies (console_cmds.lua prefixes them "[console] ") were invisible.
// Forward those lines to the console output; everything else passes through.
// Game Lua: L->top +8, L->base +0xC, 8-byte TValues, string TValue tt 4,
// TString length +12 and text +16.
extern "C" void sub_82A19CD8(PPCContext& ctx, uint8_t* base) {
  namespace ta = fable2::textappend;
  auto& s = fable2::console::S();
  const uint32_t L = ctx.r3.u32;
  static const bool hook_on = [] {
    const char* e = std::getenv("FABLE2_CPRINT_HOOK");
    return !(e && *e == '0');
  }();
  if (hook_on && (s.active.load(std::memory_order_relaxed) || fable2::console::enabled())) {
    if (L && ta::rdable2(base, L, 0x10)) {
      const uint32_t top = ta::load_be(base, L + 8), bot = ta::load_be(base, L + 0xC);
      if (bot && top > bot && top - bot <= 8 * 16 && ta::rdable2(base, bot, top - bot)) {
        const uint32_t tt = ta::load_be(base, bot + 4);
        const uint32_t ts = ta::load_be(base, bot);
        if (tt == 4 && ta::rdable2(base, ts, 16)) {
          uint32_t len = ta::load_be(base, ts + 12);
          if (len > 64000) len = 64000;
          if (ta::rdable2(base, ts + 16, len)) {
            std::string text(reinterpret_cast<const char*>(ta::host_addr(base, ts + 16)), len);
            static const std::string kTag = "[console] ";
            if (text.rfind(kTag, 0) == 0) {
              const std::string body = text.substr(kTag.size());
              if (body == "__f2 ready") {
                s.pump_ready.store(true);
                ctx.r3.u64 = 0;
                return;
              }
              if (body.rfind("__f2 done ", 0) == 0) {
                s.lua_done.store(std::strtoull(body.c_str() + 10, nullptr, 10));
                ctx.r3.u64 = 0;
                return;
              }
              std::istringstream is(body);
              std::string ln;
              while (std::getline(is, ln)) fable2::console::out("lua", ln);
              // Handled here: the release cprint copies into a fixed-size guest
              // stack buffer, and long console output overran it (exit
              // 0xC0000409 / garbage reads). A Lua C function returns its result
              // count in r3; cprint returns none.
              ctx.r3.u64 = 0;
              return;
            }
          }
        }
      }
    }
  }
  __imp__sub_82A19CD8(ctx, base);
}

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
