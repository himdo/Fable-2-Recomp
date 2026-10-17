// fable_2 - Remote control server: AI/automation input channel
//
// A localhost TCP server that accepts JSON-lines commands so an external AI
// (script, agent, or LLM harness) can drive the guest gamepad without a human
// at the keyboard. See plans/ai-remote-input-control.md for the full design
// and protocol reference.
//
//   client ->  {"cmd":"press","input":"A","hold_ms":120}\n
//   server <-  {"ok":true,"input":"A","release_in_ms":120}\n
//
// One JSON object per line; every request line gets exactly one response
// line. A client may send many commands over one connection (keep-alive).
//
// Commands:
//   ping      -> {"ok":true}
//   info      -> server/port/pad-status
//   auth      -> required first line when a token is configured
//   press     -> press input now; auto-release after hold_ms (omit/0 = until release/clear)
//   release   -> release input now (also clears it from the sticky baseline)
//   stick     -> set a stick axis to value for hold_ms (default 16 ~= one frame)
//   state     -> replace the sticky baseline: buttons[], triggers{LT,RT}, stk{lx,ly,rx,ry}
//   clear     -> release everything remote
//   script    -> atomic timed sequence: steps[{delay_ms, op, input, value, hold_ms}]
//   get_state -> current resolved pad state + pending releases
//   screenshot -> save the current game frame as a PNG at "path"
//                 (reads the renderer's guest output from the GPU, so it
//                 captures the game's own frame even while other windows
//                 cover it)
//   cvar      -> get/set any cvar by name (existing SDK knob surface)
//   enable/disable -> toggle the remote pad (zero state when disabled)
//
// Input names match the keyboard_gamepad_map vocabulary (A/B/X/Y, LB/RB,
// LT/RT, Up/Down/Left/Right, Start/Back, L3/R3, StickUp/StickDown/
// StickLeft/StickRight) plus stick axes StkLx/StkLy/StkRx/StkRy (values
// -32768..32767; StkLy positive = forward, Fable 2 convention).
//
// Threading: two OS threads.
//   - conn thread: accept loop, reads command lines, mutates the event
//     timeline, publishes the resolved snapshot.
//   - timer thread: wakes when a timed event is due (or a 250 ms heartbeat),
//     resolves, and publishes, so timed holds release accurately without any
//     client traffic.
// All timeline mutation happens on the conn thread; the timer thread only
// reads under the same mutex. Guest-facing state crosses exactly one
// boundary: InputStateStore (a tiny mutex-guarded snapshot).

#pragma once

#ifdef _WIN32
// winsock2.h + ws2tcpip.h (inet_pton, SD_BOTH). Safe here because the SDK
// sets WIN32_LEAN_AND_MEAN (see rexglueTargets.cmake) for this target, so
// the SDK's own windows.h includes never pull in the conflicting winsock.h.
#include <winsock2.h>
#include <ws2tcpip.h>
using sock_t = SOCKET;
constexpr sock_t kSockInvalid = INVALID_SOCKET;
inline void CloseSock(sock_t s) {
  if (s != kSockInvalid) closesocket(s);
}
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>  // TCP_NODELAY
#include <sys/socket.h>
#include <unistd.h>
using sock_t = int;
constexpr sock_t kSockInvalid = -1;
inline void CloseSock(sock_t s) {
  if (s != kSockInvalid) ::close(s);
}
#endif

#if defined(_WIN32) && (defined(_MSC_VER) || defined(__clang__))
#pragma comment(lib, "Ws2_32.lib")
#endif

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <format>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <rex/cvar.h>
#include <rex/input/input.h>
#include <rex/logging/macros.h>

#include "remote_input_state.h"
#include "fable2_frame_capture.h"

// Game-state accessors (defined in fable2_state_probe.h); forward-declared here
// so the "game_state" command can read the classifier without pulling the whole
// probe into this header.
namespace fable2::stateprobe {
int CurrentState();
const char* CurrentStateName();
std::string CurrentFrontEndValue();
}

namespace fable2::remote {

//=============================================================================
// Minimal dependency-free JSON (objects / arrays / strings / numbers /
// booleans / null) - just enough for this fixed command grammar.
//=============================================================================
namespace json_min {

struct Value {
  enum class Type { Null, Bool, Number, String, Array, Object };
  Type type = Type::Null;
  bool boolean = false;
  double number = 0.0;
  std::string str;
  std::vector<Value> arr;
  std::vector<std::pair<std::string, Value>> obj;

  const Value* Find(std::string_view key) const {
    for (const auto& kv : obj)
      if (kv.first == key) return &kv.second;
    return nullptr;
  }
  const std::string* FindStr(std::string_view key) const {
    const Value* v = Find(key);
    return (v && v->type == Type::String) ? &v->str : nullptr;
  }
  int64_t AsInt(int64_t def) const {
    if (type == Type::Number) return static_cast<int64_t>(number);
    if (type == Type::Bool) return boolean ? 1 : 0;
    if (type == Type::String && !str.empty()) {
      try { return std::stoll(str); } catch (...) { return def; }
    }
    return def;
  }
  int64_t GetInt(std::string_view key, int64_t def) const {
    const Value* v = Find(key);
    return v ? v->AsInt(def) : def;
  }
  bool GetBool(std::string_view key, bool def) const {
    const Value* v = Find(key);
    if (!v) return def;
    if (v->type == Type::Bool) return v->boolean;
    if (v->type == Type::Number) return v->number != 0.0;
    if (v->type == Type::String)
      return v->str == "true" || v->str == "1" || v->str == "yes";
    return def;
  }
};

class Parser {
 public:
  explicit Parser(std::string_view text)
      : p_(text.data()), end_(text.data() + text.size()) {}

  bool Parse(Value& out) {
    SkipWs();
    if (!ParseValue(out)) return false;
    SkipWs();
    return p_ == end_;
  }

 private:
  bool AtEnd() const { return p_ == end_; }
  bool Fail(const char* msg) { err_ = msg; return false; }
  char Peek() const { return AtEnd() ? '\0' : *p_; }
  char Get() { return AtEnd() ? '\0' : *p_++; }
  void SkipWs() {
    while (!AtEnd()) {
      const char c = Peek();
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') ++p_;
      else break;
    }
  }
  bool Match(const char* lit) {
    for (const char* l = lit; *l != '\0'; ++l, ++p_) {
      if (AtEnd() || *p_ != *l) return false;
    }
    return true;
  }
  bool ParseValue(Value& v) {
    if (AtEnd()) return Fail("unexpected end of input");
    const char c = Peek();
    if (c == '{') return ParseObject(v);
    if (c == '[') return ParseArray(v);
    if (c == '"') {
      v.type = Value::Type::String;
      return ParseString(v.str);
    }
    if (c == 't' || c == 'f') {
      if (Match("true")) { v.type = Value::Type::Bool; v.boolean = true; return true; }
      if (Match("false")) { v.type = Value::Type::Bool; v.boolean = false; return true; }
      return Fail("bad literal");
    }
    if (c == 'n') {
      if (Match("null")) { v.type = Value::Type::Null; return true; }
      return Fail("bad literal");
    }
    return ParseNumber(v);
  }
  bool ParseNumber(Value& v) {
    const char* start = p_;
    if (Peek() == '-' || Peek() == '+') ++p_;
    bool any = false;
    while (!AtEnd()) {
      const char c = Peek();
      if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' ||
          c == '+' || c == '-') {
        any = true;
        ++p_;
      } else break;
    }
    if (!any) return Fail("bad number");
    try {
      v.number = std::stod(std::string(start, p_));
    } catch (...) {
      return Fail("bad number");
    }
    v.type = Value::Type::Number;
    return true;
  }
  int ParseHex4() {
    int code = 0;
    for (int i = 0; i < 4; ++i) {
      if (AtEnd()) return -1;
      const char h = Get();
      int d;
      if (h >= '0' && h <= '9') d = h - '0';
      else if (h >= 'a' && h <= 'f') d = h - 'a' + 10;
      else if (h >= 'A' && h <= 'F') d = h - 'A' + 10;
      else return -1;
      code = code * 16 + d;
    }
    return code;
  }
  void AppendUtf8(std::string& out, int cp) {
    if (cp < 0x80) {
      out += static_cast<char>(cp);
    } else if (cp < 0x800) {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 63));
    } else if (cp < 0x10000) {
      out += static_cast<char>(0xE0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 63));
      out += static_cast<char>(0x80 | (cp & 63));
    } else {
      out += static_cast<char>(0xF0 | (cp >> 18));
      out += static_cast<char>(0x80 | ((cp >> 12) & 63));
      out += static_cast<char>(0x80 | ((cp >> 6) & 63));
      out += static_cast<char>(0x80 | (cp & 63));
    }
  }
  bool ParseString(std::string& out) {
    if (Get() != '"') return Fail("expected string");
    for (;;) {
      if (AtEnd()) return Fail("unterminated string");
      const char c = Get();
      if (c == '"') return true;
      if (c != '\\') {
        out += c;
        continue;
      }
      if (AtEnd()) return Fail("bad escape");
      const char e = Get();
      switch (e) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          int cp = ParseHex4();
          if (cp < 0) return Fail("bad \\u escape");
          if (cp >= 0xD800 && cp <= 0xDBFF && !AtEnd() && Peek() == '\\' &&
              p_ + 1 < end_ && p_[1] == 'u') {
            p_ += 2;  // consume the second backslash-u
            const int lo = ParseHex4();
            if (lo >= 0xDC00 && lo <= 0xDFFF)
              cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          }
          AppendUtf8(out, cp);
          break;
        }
        default: return Fail("bad escape");
      }
    }
  }
  bool ParseArray(Value& v) {
    v.type = Value::Type::Array;
    Get();  // '['
    SkipWs();
    if (Peek() == ']') { ++p_; return true; }
    for (;;) {
      v.arr.emplace_back();
      SkipWs();
      if (!ParseValue(v.arr.back())) return false;
      SkipWs();
      if (AtEnd()) return Fail("unterminated array");
      const char c = Get();
      if (c == ']') return true;
      if (c != ',') return Fail("expected ',' or ']'");
    }
  }
  bool ParseObject(Value& v) {
    v.type = Value::Type::Object;
    Get();  // '{'
    SkipWs();
    if (Peek() == '}') { ++p_; return true; }
    for (;;) {
      SkipWs();
      if (Peek() != '"') return Fail("expected object key");
      std::string key;
      if (!ParseString(key)) return false;
      SkipWs();
      if (Get() != ':') return Fail("expected ':'");
      v.obj.emplace_back(std::move(key), Value{});
      SkipWs();
      if (!ParseValue(v.obj.back().second)) return false;
      SkipWs();
      if (AtEnd()) return Fail("unterminated object");
      const char c = Get();
      if (c == '}') return true;
      if (c != ',') return Fail("expected ',' or '}'");
    }
  }
  const char* p_;
  const char* end_;
  const char* err_ = nullptr;
};

inline bool Parse(std::string_view text, Value& out) {
  Parser parser(text);
  return parser.Parse(out);
}

// Escape a string for embedding in a JSON response.
inline std::string JEsc(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (const char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20)
          out += std::format("\\u{:04x}", static_cast<unsigned char>(c));
        else
          out += c;
    }
  }
  return out;
}

}  // namespace json_min

//=============================================================================
// Input slots (buttons / triggers / stick axes)
//=============================================================================

enum class Slot : uint8_t {
  kBtnA, kBtnB, kBtnX, kBtnY, kBtnLB, kBtnRB,
  kBtnUp, kBtnDown, kBtnLeft, kBtnRight, kBtnStart, kBtnBack, kBtnL3, kBtnR3,
  kTrigL, kTrigR,
  kStkLx, kStkLy, kStkRx, kStkRy,
  kCount
};

inline uint16_t SlotButtonMask(Slot s) {
  using B = rex::input::X_INPUT_GAMEPAD_BUTTON;
  switch (s) {
    case Slot::kBtnA: return static_cast<uint16_t>(B::X_INPUT_GAMEPAD_A);
    case Slot::kBtnB: return static_cast<uint16_t>(B::X_INPUT_GAMEPAD_B);
    case Slot::kBtnX: return static_cast<uint16_t>(B::X_INPUT_GAMEPAD_X);
    case Slot::kBtnY: return static_cast<uint16_t>(B::X_INPUT_GAMEPAD_Y);
    case Slot::kBtnLB: return static_cast<uint16_t>(B::X_INPUT_GAMEPAD_LEFT_SHOULDER);
    case Slot::kBtnRB: return static_cast<uint16_t>(B::X_INPUT_GAMEPAD_RIGHT_SHOULDER);
    case Slot::kBtnUp: return static_cast<uint16_t>(B::X_INPUT_GAMEPAD_DPAD_UP);
    case Slot::kBtnDown: return static_cast<uint16_t>(B::X_INPUT_GAMEPAD_DPAD_DOWN);
    case Slot::kBtnLeft: return static_cast<uint16_t>(B::X_INPUT_GAMEPAD_DPAD_LEFT);
    case Slot::kBtnRight: return static_cast<uint16_t>(B::X_INPUT_GAMEPAD_DPAD_RIGHT);
    case Slot::kBtnStart: return static_cast<uint16_t>(B::X_INPUT_GAMEPAD_START);
    case Slot::kBtnBack: return static_cast<uint16_t>(B::X_INPUT_GAMEPAD_BACK);
    case Slot::kBtnL3: return static_cast<uint16_t>(B::X_INPUT_GAMEPAD_LEFT_THUMB);
    case Slot::kBtnR3: return static_cast<uint16_t>(B::X_INPUT_GAMEPAD_RIGHT_THUMB);
    default: return 0;
  }
}

inline bool IsStickSlot(Slot s) {
  return s == Slot::kStkLx || s == Slot::kStkLy || s == Slot::kStkRx ||
         s == Slot::kStkRy;
}
inline bool IsTriggerSlot(Slot s) {
  return s == Slot::kTrigL || s == Slot::kTrigR;
}

struct SlotRef {
  Slot slot;
  int32_t default_value;  // value used when the command omits "value"
};

// Case-insensitive input name -> slot. Names match the keyboard_gamepad_map
// vocabulary (see src/input/keyboard_gamepad.h), plus stick axis names StkLx/
// StkLy/StkRx/StkRy. StkLy positive = forward/up (Fable 2 convention, same
// as the keyboard driver's StickUp).
inline bool SlotFromName(std::string_view name, SlotRef& out) {
  constexpr int32_t kMax = 32767;
  std::string u;
  u.reserve(name.size());
  for (char c : name) u.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));

#define FABLE2_SLOT(name_upper, slot, def) \
  if (u == name_upper) { out = SlotRef{slot, def}; return true; }
  FABLE2_SLOT("A", Slot::kBtnA, 1)
  FABLE2_SLOT("B", Slot::kBtnB, 1)
  FABLE2_SLOT("X", Slot::kBtnX, 1)
  FABLE2_SLOT("Y", Slot::kBtnY, 1)
  FABLE2_SLOT("LB", Slot::kBtnLB, 1)
  FABLE2_SLOT("LSHOULDER", Slot::kBtnLB, 1)
  FABLE2_SLOT("RB", Slot::kBtnRB, 1)
  FABLE2_SLOT("RSHOULDER", Slot::kBtnRB, 1)
  FABLE2_SLOT("UP", Slot::kBtnUp, 1)
  FABLE2_SLOT("DUP", Slot::kBtnUp, 1)
  FABLE2_SLOT("DOWN", Slot::kBtnDown, 1)
  FABLE2_SLOT("DDOWN", Slot::kBtnDown, 1)
  FABLE2_SLOT("LEFT", Slot::kBtnLeft, 1)
  FABLE2_SLOT("DLEFT", Slot::kBtnLeft, 1)
  FABLE2_SLOT("RIGHT", Slot::kBtnRight, 1)
  FABLE2_SLOT("DRIGHT", Slot::kBtnRight, 1)
  FABLE2_SLOT("START", Slot::kBtnStart, 1)
  FABLE2_SLOT("PAUSE", Slot::kBtnStart, 1)
  FABLE2_SLOT("BACK", Slot::kBtnBack, 1)
  FABLE2_SLOT("VIEW", Slot::kBtnBack, 1)
  FABLE2_SLOT("SELECT", Slot::kBtnBack, 1)
  FABLE2_SLOT("L3", Slot::kBtnL3, 1)
  FABLE2_SLOT("LTHUMB", Slot::kBtnL3, 1)
  FABLE2_SLOT("R3", Slot::kBtnR3, 1)
  FABLE2_SLOT("RTHUMB", Slot::kBtnR3, 1)
  FABLE2_SLOT("LT", Slot::kTrigL, 255)
  FABLE2_SLOT("LTRIGGER", Slot::kTrigL, 255)
  FABLE2_SLOT("RT", Slot::kTrigR, 255)
  FABLE2_SLOT("RTRIGGER", Slot::kTrigR, 255)
  FABLE2_SLOT("STKLX", Slot::kStkLx, kMax)
  FABLE2_SLOT("LSTICKX", Slot::kStkLx, kMax)
  FABLE2_SLOT("STKLY", Slot::kStkLy, kMax)
  FABLE2_SLOT("LSTICKY", Slot::kStkLy, kMax)
  FABLE2_SLOT("STKRX", Slot::kStkRx, kMax)
  FABLE2_SLOT("RSTICKX", Slot::kStkRx, kMax)
  FABLE2_SLOT("STKRY", Slot::kStkRy, kMax)
  FABLE2_SLOT("RSTICKY", Slot::kStkRy, kMax)
  FABLE2_SLOT("STICKUP", Slot::kStkLy, kMax)
  FABLE2_SLOT("STICKDOWN", Slot::kStkLy, -kMax)
  FABLE2_SLOT("STICKLEFT", Slot::kStkLx, -kMax)
  FABLE2_SLOT("STICKRIGHT", Slot::kStkLx, kMax)
#undef FABLE2_SLOT
  return false;
}

inline std::string SlotName(Slot s) {
  switch (s) {
    case Slot::kBtnA: return "A";
    case Slot::kBtnB: return "B";
    case Slot::kBtnX: return "X";
    case Slot::kBtnY: return "Y";
    case Slot::kBtnLB: return "LB";
    case Slot::kBtnRB: return "RB";
    case Slot::kBtnUp: return "Up";
    case Slot::kBtnDown: return "Down";
    case Slot::kBtnLeft: return "Left";
    case Slot::kBtnRight: return "Right";
    case Slot::kBtnStart: return "Start";
    case Slot::kBtnBack: return "Back";
    case Slot::kBtnL3: return "L3";
    case Slot::kBtnR3: return "R3";
    case Slot::kTrigL: return "LT";
    case Slot::kTrigR: return "RT";
    case Slot::kStkLx: return "StkLx";
    case Slot::kStkLy: return "StkLy";
    case Slot::kStkRx: return "StkRx";
    case Slot::kStkRy: return "StkRy";
    default: return "?";
  }
}

inline int32_t ClampForSlot(Slot s, int32_t v) {
  if (SlotButtonMask(s) != 0) return 1;
  if (IsTriggerSlot(s)) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return v;
  }
  constexpr int32_t kMax = 32767;
  if (v > kMax) return kMax;
  if (v < -kMax) return -kMax;
  return v;
}

inline void ClearSlotInSnapshot(InputSnapshot& s, Slot slot) {
  const uint16_t mask = SlotButtonMask(slot);
  if (mask != 0) {
    s.buttons = static_cast<uint16_t>(s.buttons & static_cast<uint16_t>(~mask));
    return;
  }
  switch (slot) {
    case Slot::kTrigL: s.left_trigger = 0; break;
    case Slot::kTrigR: s.right_trigger = 0; break;
    case Slot::kStkLx: s.stk_lx = 0; break;
    case Slot::kStkLy: s.stk_ly = 0; break;
    case Slot::kStkRx: s.stk_rx = 0; break;
    case Slot::kStkRy: s.stk_ry = 0; break;
    default: break;
  }
}

//=============================================================================
// Event timeline
//=============================================================================

struct Interval {
  Slot slot = Slot::kBtnA;
  int32_t value = 0;
  std::chrono::steady_clock::time_point start{};
  bool open = false;  // no scheduled end: held until release/clear
  std::chrono::steady_clock::time_point end{};

  bool ActiveAt(std::chrono::steady_clock::time_point now) const {
    return start <= now && (open || now < end);
  }
};

struct Timeline {
  InputSnapshot baseline{};  // sticky state (from the "state" command)
  std::vector<Interval> intervals;
};

// Resolve the timeline at `now` into a concrete snapshot. Baseline first,
// then active intervals: buttons OR, triggers max, sticks override.
inline InputSnapshot ResolveTimeline(const Timeline& tl,
                                     std::chrono::steady_clock::time_point now) {
  InputSnapshot s = tl.baseline;
  for (const Interval& iv : tl.intervals) {
    if (!iv.ActiveAt(now)) continue;
    const uint16_t mask = SlotButtonMask(iv.slot);
    if (mask != 0) {
      s.buttons = static_cast<uint16_t>(s.buttons | mask);
    } else {
      switch (iv.slot) {
        case Slot::kTrigL:
          if (iv.value > s.left_trigger) s.left_trigger = static_cast<uint8_t>(iv.value);
          break;
        case Slot::kTrigR:
          if (iv.value > s.right_trigger) s.right_trigger = static_cast<uint8_t>(iv.value);
          break;
        case Slot::kStkLx: s.stk_lx = iv.value; break;
        case Slot::kStkLy: s.stk_ly = iv.value; break;
        case Slot::kStkRx: s.stk_rx = iv.value; break;
        case Slot::kStkRy: s.stk_ry = iv.value; break;
        default: break;
      }
    }
  }
  return s;
}

//=============================================================================
// Server
//=============================================================================

class ControlServer {
 public:
  struct Config {
    std::string host = "127.0.0.1";  // "0.0.0.0" = all interfaces
    int32_t port = 8791;
    std::string token;  // empty = no auth required
  };

  explicit ControlServer(InputStateStore* state, std::atomic<bool>* pad_enabled)
      : state_(state), pad_enabled_(pad_enabled) {}
  ~ControlServer() { Stop(); }

  ControlServer(const ControlServer&) = delete;
  ControlServer& operator=(const ControlServer&) = delete;

  // Binds port..port+9 (first free wins); starts both threads.
  bool Start(Config cfg) {
    if (running_.exchange(true)) return true;  // already started
    cfg_ = std::move(cfg);
#ifdef _WIN32
    static std::once_flag wsa_once;
    std::call_once(wsa_once, [] {
      WSADATA d;
      if (WSAStartup(MAKEWORD(2, 2), &d) != 0) {
        REXSYS_ERROR("[remote] WSAStartup failed ({}); remote control disabled",
                     WSAGetLastError());
      }
    });
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    uint32_t ip = INADDR_ANY;
    if (cfg_.host != "0.0.0.0") {
      uint32_t parsed = 0;
      if (inet_pton(AF_INET, cfg_.host.c_str(), &parsed) != 1) {
        REXSYS_WARN("[remote] invalid host '{}'; using 0.0.0.0", cfg_.host);
      } else {
        ip = parsed;
      }
    }
    addr.sin_addr.s_addr = ip;

    sock_t listen = kSockInvalid;
    for (int attempt = 0; attempt < 10 && listen == kSockInvalid; ++attempt) {
      const sock_t s = ::socket(AF_INET, SOCK_STREAM, 0);
      if (s == kSockInvalid) break;
      addr.sin_port = htons(static_cast<uint16_t>(cfg_.port + attempt));
      if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0 &&
          ::listen(s, 16) == 0) {
        listen = s;
        port_ = cfg_.port + attempt;
      } else {
        CloseSock(s);
      }
    }
    if (listen == kSockInvalid) {
      running_ = false;
      REXSYS_ERROR("[remote] could not bind TCP server to {}:{}-{}", cfg_.host,
                   cfg_.port, cfg_.port + 9);
      return false;
    }
    listen_ = listen;
    conn_thread_ = std::thread(&ControlServer::ThreadConn, this);
    timer_thread_ = std::thread(&ControlServer::ThreadTimer, this);
    REXSYS_INFO("[remote] control server listening on {}:{}", cfg_.host,
                port_.load());
    return true;
  }

  void Stop() {
    if (!running_.exchange(false)) return;
    cv_.notify_all();
    // Break any blocked recv()/accept() so the threads exit promptly.
    ShutdownSock(static_cast<sock_t>(client_socket_.load()));
    const sock_t listen = listen_;
    listen_ = kSockInvalid;
    CloseSock(listen);
    if (timer_thread_.joinable()) timer_thread_.join();
    if (conn_thread_.joinable()) conn_thread_.join();
#ifdef _WIN32
    // No WSACleanup: the process is shutting down.
#endif
  }

  bool Running() const { return running_.load(); }
  int32_t BoundPort() const { return port_; }

  // The renderer presenter (rex::ui::Presenter), queried at command time (so
  // this can be set before the window/presenter exists). nullptr = unavailable.
  // Backs the "screenshot" command, which reads the guest output frame back
  // from the GPU via Presenter::CaptureGuestOutput().
  void SetPresenterProvider(std::function<rex::ui::Presenter*()> provider) {
    presenter_provider_ = std::move(provider);
  }

 private:
  using Clock = std::chrono::steady_clock;
  static constexpr size_t kMaxIntervals = 10000;
  static constexpr int64_t kMaxHoldMs = 3600000;

  static void ShutdownSock(sock_t s) {
    if (s == kSockInvalid) return;
#ifdef _WIN32
    ::shutdown(s, SD_BOTH);
#else
    ::shutdown(s, SHUT_RDWR);
#endif
  }

  static void SendAll(sock_t s, const std::string& data) {
    size_t off = 0;
    while (off < data.size()) {
      const int n = ::send(s, data.data() + off,
                           static_cast<int>(data.size() - off),
#ifdef _WIN32
                           0);
#else
                           MSG_NOSIGNAL);
#endif
      if (n <= 0) return;
      off += static_cast<size_t>(n);
    }
  }

  std::string Ok(std::string_view extra = "") const {
    return std::string("{\"ok\":true") + std::string(extra) + "}";
  }
  std::string Err(std::string_view msg) const {
    return std::format(R"({{"ok":false,"error":"{}"}})", json_min::JEsc(msg));
  }
  // Echo the request's "id" (if any) into the response.
  std::string IdEcho(const json_min::Value& v) const {
    const json_min::Value* id = v.Find("id");
    if (!id) return "";
    if (id->type == json_min::Value::Type::String)
      return std::format(R"(,"id":"{}")", json_min::JEsc(id->str));
    if (id->type == json_min::Value::Type::Number)
      return std::format(R"(,"id":{})", id->number);
    return "";
  }

  void PublishNow() {
    std::lock_guard<std::mutex> lock(tl_mu_);
    PruneAndPublishLocked();
  }

  // Caller holds tl_mu_.
  void PruneAndPublishLocked() {
    const auto now = Clock::now();
    auto& ivs = timeline_.intervals;
    for (auto it = ivs.begin(); it != ivs.end();) {
      if (!it->open && it->end <= now) it = ivs.erase(it);
      else ++it;
    }
    const InputSnapshot resolved = ResolveTimeline(timeline_, now);
    const InputSnapshot effective =
        pad_enabled_->load(std::memory_order_relaxed) ? resolved : InputSnapshot{};
    state_->PublishAndNotify(effective);
  }

  // Returns false if the interval queue cap is hit.
  bool AppendInterval(Slot slot, int32_t value, Clock::time_point start,
                      Clock::time_point end, bool open) {
    std::lock_guard<std::mutex> lock(tl_mu_);
    if (timeline_.intervals.size() >= kMaxIntervals) return false;
    timeline_.intervals.push_back(Interval{slot, value, start, open,
                                           open ? Clock::time_point{} : end});
    cv_.notify_all();
    return true;
  }

  bool FindRegisteredCvar(std::string_view name) {
    for (const auto& entry : rex::cvar::GetRegistry())
      if (entry.name == name) return true;
    return false;
  }

  //--- Threads --------------------------------------------------------------

  void ThreadConn() {
    while (running_) {
      const sock_t listen = listen_;
      if (listen == kSockInvalid) break;
      const sock_t client = ::accept(listen, nullptr, nullptr);
      if (client == kSockInvalid) {
        if (!running_) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        continue;
      }
      ServeClient(client);
    }
  }

  void ServeClient(sock_t client) {
    client_socket_ = client;
    int on = 1;
    ::setsockopt(client, IPPROTO_TCP, TCP_NODELAY,
                 reinterpret_cast<const char*>(&on), sizeof(on));

    bool authed = cfg_.token.empty();
    std::string buf;
    buf.reserve(4096);
    char tmp[8192];
    for (;;) {
      const int n = ::recv(client, tmp, sizeof(tmp), 0);
      if (n <= 0) break;  // peer closed or error
      buf.append(tmp, static_cast<size_t>(n));
      size_t pos;
      while ((pos = buf.find('\n')) != std::string::npos) {
        std::string line = buf.substr(0, pos);
        buf.erase(0, pos + 1);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        SendAll(client, HandleCommand(line, authed) + "\n");
      }
      if (buf.size() > 65536) {
        SendAll(client, Err("line too long") + "\n");
        break;
      }
    }
    CloseSock(client);
    client_socket_ = kSockInvalid;
  }

  void ThreadTimer() {
    std::unique_lock<std::mutex> lock(tl_mu_);
    while (running_) {
      const auto now = Clock::now();
      bool have_next = false;
      Clock::time_point next;
      for (const Interval& iv : timeline_.intervals) {
        if (!iv.open && iv.end > now) {
          if (!have_next || iv.end < next) next = iv.end;
          have_next = true;
        }
      }
      if (have_next) {
        cv_.wait_until(lock, next, [this] { return !running_.load(); });
      } else {
        cv_.wait_for(lock, std::chrono::milliseconds(250),
                     [this] { return !running_.load(); });
      }
      if (!running_) break;
      PruneAndPublishLocked();
    }
  }

  //--- Command dispatch ------------------------------------------------------

  std::string HandleCommand(const std::string& line, bool& authed) {
    // Audit log: which message happened when (truncated for log hygiene).
    std::string shown = line;
    if (shown.size() > 512) shown = shown.substr(0, 512) + "...";
    REXSYS_INFO("[remote] {}", shown);

    json_min::Value v;
    if (!json_min::Parse(line, v)) return Err("malformed JSON");
    if (v.type != json_min::Value::Type::Object)
      return Err("expected a JSON object");

    const std::string* cmd_str = v.FindStr("cmd");
    const std::string cmd = cmd_str ? *cmd_str : "";

    if (!authed && !cfg_.token.empty()) {
      if (cmd != "auth")
        return Err(R"(auth required: send {"cmd":"auth","token":"..."})");
      const std::string* tok = v.FindStr("token");
      if (!tok || *tok != cfg_.token) {
        REXSYS_WARN("[remote] auth failed");
        return Err("bad token");
      }
      authed = true;
      return Ok(IdEcho(v));
    }

    if (cmd == "ping") {
      return Ok(IdEcho(v));
    }

    if (cmd == "auth") {
      authed = true;
      return Ok(IdEcho(v));
    }

    if (cmd == "info") {
      // Two adjacent raw strings (concatenated) so no newline leaks into the
      // JSON - a single raw string would keep the source line break as content.
      return std::format(
          R"({{"ok":true,"name":"fable2-remote-control","version":1,"port":{},)"
          R"("pad_enabled":{},"device_guid":"fable2-remote-gamepad"{}}})",
          port_.load(), pad_enabled_->load(), IdEcho(v));
    }

    if (cmd == "enable" || cmd == "disable") {
      const bool on = cmd == "enable";
      pad_enabled_->store(on, std::memory_order_relaxed);
      PublishNow();
      return std::format(R"({{"ok":true,"pad_enabled":{}{}}})", on, IdEcho(v));
    }

    if (cmd == "press") return HandlePress(v);
    if (cmd == "release") return HandleRelease(v);
    if (cmd == "stick") return HandleStick(v);
    if (cmd == "state") return HandleState(v);
    if (cmd == "clear") return HandleClear(v);
    if (cmd == "script") return HandleScript(v);
    if (cmd == "get_state") return HandleGetState(v);
    if (cmd == "game_state") return HandleGameState(v);
    if (cmd == "screenshot") return HandleScreenshot(v);
    if (cmd == "cvar") return HandleCvar(v);

    return Err(std::format(
        "unknown command '{}'; expected one of: ping, info, auth, press, "
        "release, stick, state, clear, script, get_state, game_state, "
        "screenshot, cvar, enable, disable", cmd));
  }

  // "input" name -> slot; value defaults per slot (buttons 1, triggers 255,
  // sticks max deflection). hold_ms <= 0 => held until release/clear.
  std::string HandlePress(const json_min::Value& v) {
    const std::string* name = v.FindStr("input");
    SlotRef ref;
    if (!name || !SlotFromName(*name, ref))
      return Err("missing or unknown 'input'");
    const int64_t hold_ms = v.GetInt("hold_ms", 0);
    if (hold_ms < 0 || hold_ms > kMaxHoldMs)
      return Err("hold_ms out of range (0.." + std::to_string(kMaxHoldMs) + ")");
    const int32_t value = ClampForSlot(ref.slot, v.GetInt("value", ref.default_value));
    const auto now = Clock::now();
    const bool open = hold_ms <= 0;
    if (!AppendInterval(ref.slot, value, now,
                        open ? Clock::time_point{} : now + std::chrono::milliseconds(hold_ms),
                        open))
      return Err("event queue full");
    PublishNow();
    return std::format(
        R"({{"ok":true,"input":"{}","release_in_ms":{}{}}})",
        SlotName(ref.slot), open ? -1 : hold_ms, IdEcho(v));
  }

  std::string HandleRelease(const json_min::Value& v) {
    const std::string* name = v.FindStr("input");
    SlotRef ref;
    if (!name || !SlotFromName(*name, ref))
      return Err("missing or unknown 'input'");
    {
      std::lock_guard<std::mutex> lock(tl_mu_);
      const auto now = Clock::now();
      for (auto& iv : timeline_.intervals)
        if (iv.slot == ref.slot && iv.start <= now) {
          iv.open = false;
          iv.end = now;
        }
      ClearSlotInSnapshot(timeline_.baseline, ref.slot);
      cv_.notify_all();
    }
    PublishNow();
    return Ok(std::string(",\"input\":\"") + SlotName(ref.slot) +
              std::string("\"") + IdEcho(v));
  }

  std::string HandleStick(const json_min::Value& v) {
    const std::string* name = v.FindStr("input");
    SlotRef ref;
    if (!name || !SlotFromName(*name, ref) || !IsStickSlot(ref.slot))
      return Err("stick 'input' must be one of StkLx/StkLy/StkRx/StkRy (or "
                 "StickUp/StickDown/StickLeft/StickRight)");
    const int32_t value = ClampForSlot(ref.slot, v.GetInt("value", ref.default_value));
    const int64_t hold_ms = v.GetInt("hold_ms", 16);  // ~one frame at 60 Hz
    if (hold_ms < 1 || hold_ms > kMaxHoldMs)
      return Err("hold_ms out of range (1.." + std::to_string(kMaxHoldMs) + ")");
    const auto now = Clock::now();
    if (!AppendInterval(ref.slot, value, now,
                        now + std::chrono::milliseconds(hold_ms), false))
      return Err("event queue full");
    PublishNow();
    return std::format(
        R"({{"ok":true,"input":"{}","value":{},"release_in_ms":{}{}}})",
        SlotName(ref.slot), value, hold_ms, IdEcho(v));
  }

  // Replaces the sticky baseline atomically: buttons[], triggers{LT,RT},
  // stk{lx,ly,rx,ry}. Omitted parts reset to neutral.
  std::string HandleState(const json_min::Value& v) {
    InputSnapshot baseline{};
    if (const auto* arr = v.Find("buttons")) {
      if (arr->type != json_min::Value::Type::Array)
        return Err("'buttons' must be an array of input names");
      for (const auto& item : arr->arr) {
        if (item.type != json_min::Value::Type::String)
          return Err("'buttons' entries must be strings");
        SlotRef ref;
        if (!SlotFromName(item.str, ref))
          return Err(std::format("unknown button name: {}", item.str));
        baseline.buttons =
            static_cast<uint16_t>(baseline.buttons | SlotButtonMask(ref.slot));
      }
    }
    if (const auto* trig = v.Find("triggers")) {
      if (trig->type != json_min::Value::Type::Object)
        return Err("'triggers' must be an object like {\"LT\":255}");
      for (const auto& kv : trig->obj) {
        SlotRef ref;
        if (!SlotFromName(kv.first, ref) || !IsTriggerSlot(ref.slot))
          return Err(std::format("unknown trigger name: {}", kv.first));
        const int32_t value = ClampForSlot(ref.slot, kv.second.AsInt(0));
        if (ref.slot == Slot::kTrigL) baseline.left_trigger = static_cast<uint8_t>(value);
        else baseline.right_trigger = static_cast<uint8_t>(value);
      }
    }
    if (const auto* stk = v.Find("stk")) {
      if (stk->type != json_min::Value::Type::Object)
        return Err("'stk' must be an object like {\"lx\":0,\"ly\":1000}");
      for (const auto& kv : stk->obj) {
        Slot slot;
        if (kv.first == "lx") slot = Slot::kStkLx;
        else if (kv.first == "ly") slot = Slot::kStkLy;
        else if (kv.first == "rx") slot = Slot::kStkRx;
        else if (kv.first == "ry") slot = Slot::kStkRy;
        else return Err(std::format("unknown stick axis: {}", kv.first));
        const int32_t value = ClampForSlot(slot, kv.second.AsInt(0));
        switch (slot) {
          case Slot::kStkLx: baseline.stk_lx = value; break;
          case Slot::kStkLy: baseline.stk_ly = value; break;
          case Slot::kStkRx: baseline.stk_rx = value; break;
          case Slot::kStkRy: baseline.stk_ry = value; break;
          default: break;
        }
      }
    }
    {
      std::lock_guard<std::mutex> lock(tl_mu_);
      timeline_.baseline = baseline;
      cv_.notify_all();
    }
    PublishNow();
    return Ok(IdEcho(v));
  }

  std::string HandleClear(const json_min::Value& v) {
    {
      std::lock_guard<std::mutex> lock(tl_mu_);
      timeline_.baseline = InputSnapshot{};
      timeline_.intervals.clear();
      cv_.notify_all();
    }
    PublishNow();
    return Ok(IdEcho(v));
  }

  // Atomic timed sequence. Each step: {"delay_ms":N, "op":"press|stick|
  // release", ...same fields as the top-level command}. All steps are
  // expanded into the future in one lock, so the sequence runs with no
  // network round-trips between steps. "release" closes the most recent
  // open interval for that input.
  std::string HandleScript(const json_min::Value& v) {
    const auto* steps = v.Find("steps");
    if (!steps || steps->type != json_min::Value::Type::Array || steps->arr.empty())
      return Err("'steps' must be a non-empty array");
    const auto t0 = Clock::now();
    int64_t duration_ms = 0;
    {
      std::lock_guard<std::mutex> lock(tl_mu_);
      if (timeline_.intervals.size() + steps->arr.size() > kMaxIntervals)
        return Err("event queue full");
      Clock::time_point t = t0;
      Clock::time_point max_end = t0;
      for (const auto& step : steps->arr) {
        if (step.type != json_min::Value::Type::Object)
          return Err("each step must be an object");
        const int64_t delay = step.GetInt("delay_ms", 0);
        if (delay < 0 || delay > kMaxHoldMs)
          return Err("step 'delay_ms' out of range");
        t += std::chrono::milliseconds(delay);
        const std::string* op_str = step.FindStr("op");
        const std::string op = op_str ? *op_str : "";
        if (op == "press" || op == "stick") {
          const std::string* name = step.FindStr("input");
          SlotRef ref;
          if (!name || !SlotFromName(*name, ref))
            return Err("step: missing or unknown 'input'");
          if (op == "stick" && !IsStickSlot(ref.slot))
            return Err("step: 'stick' op needs a stick input");
          const int32_t value =
              ClampForSlot(ref.slot, step.GetInt("value", ref.default_value));
          const int64_t hold = step.GetInt("hold_ms", op == "stick" ? 16 : 0);
          if (hold < 0 || hold > kMaxHoldMs)
            return Err("step 'hold_ms' out of range");
          const bool open = hold <= 0;
          timeline_.intervals.push_back(
              Interval{ref.slot, value, t, open,
                       open ? Clock::time_point{} : t + std::chrono::milliseconds(hold)});
          if (!open && t + std::chrono::milliseconds(hold) > max_end)
            max_end = t + std::chrono::milliseconds(hold);
        } else if (op == "release") {
          const std::string* name = step.FindStr("input");
          SlotRef ref;
          if (!name || !SlotFromName(*name, ref))
            return Err("step: missing or unknown 'input'");
          for (auto it = timeline_.intervals.rbegin();
               it != timeline_.intervals.rend(); ++it) {
            if (it->slot == ref.slot && it->open && it->start <= t) {
              it->open = false;
              it->end = t;
              break;
            }
          }
        } else {
          return Err(std::format("step: unknown op '{}' (press/stick/release)", op));
        }
      }
      cv_.notify_all();
      duration_ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(max_end - t0).count();
    }
    PublishNow();  // outside the lock (it takes tl_mu_ itself)
    return std::format(R"({{"ok":true,"duration_ms":{}{}}})", duration_ms,
                       IdEcho(v));
  }

  std::string HandleGetState(const json_min::Value& v) {
    std::lock_guard<std::mutex> lock(tl_mu_);
    const auto now = Clock::now();
    const InputSnapshot resolved = ResolveTimeline(timeline_, now);
    const bool enabled = pad_enabled_->load(std::memory_order_relaxed);
    const InputSnapshot s = enabled ? resolved : InputSnapshot{};

    std::string buttons = "[";
    bool first = true;
    for (uint8_t i = 0; i < static_cast<uint8_t>(Slot::kCount); ++i) {
      const Slot sl = static_cast<Slot>(i);
      const uint16_t mask = SlotButtonMask(sl);
      if (mask != 0 && (s.buttons & mask) != 0) {
        if (!first) buttons += ",";
        buttons += "\"" + json_min::JEsc(SlotName(sl)) + "\"";  // quoted string
        first = false;
      }
    }
    buttons += "]";

    std::string pending = "[";
    first = true;
    for (const Interval& iv : timeline_.intervals) {
      if (!iv.ActiveAt(now)) continue;
      const int64_t release_in_ms =
          iv.open ? -1
                  : std::chrono::duration_cast<std::chrono::milliseconds>(
                        iv.end - now).count();
      if (!first) pending += ",";
      pending += std::format(R"({{"input":"{}","release_in_ms":{}}})",
                             SlotName(iv.slot), release_in_ms);
      first = false;
    }
    pending += "]";

    return std::format(
        R"({{"ok":true,"pad_enabled":{},"buttons":{},"triggers":{{"LT":{},"RT":{})"
        R"(}},"sticks":{{"lx":{},"ly":{},"rx":{},"ry":{}}},"pending":{}{}}})",
        enabled, buttons, s.left_trigger, s.right_trigger, s.stk_lx, s.stk_ly,
        s.stk_rx, s.stk_ry, pending, IdEcho(v));
  }

  // "game_state" -> the current boot/menu state (see fable2_state_probe.h):
  // PreMainMenu, PressAScreen, MainMenuMovie, MainMenu, or Unknown (?).
  std::string HandleGameState(const json_min::Value& v) {
    const int st = fable2::stateprobe::CurrentState();
    return std::format(
        R"({{"ok":true,"state":{{"code":{},"name":"{}","front_end":"{}"}}}})",
        st, fable2::stateprobe::CurrentStateName(),
        fable2::stateprobe::CurrentFrontEndValue());
  }

  // "screenshot" -> save the current on-screen game frame as a PNG at
  // "path" (UTF-8; parent directories are created). Captures the window's
  // client area via PrintWindow(PW_RENDERFULLCONTENT) (see
  // src/diagnostics/fable2_frame_capture.h), so the frame is complete even
  // while other windows cover the game. Takes well under a second; the
  // connection is blocked for the capture's duration.
  std::string HandleScreenshot(const json_min::Value& v) {
    const std::string* path = v.FindStr("path");
    if (!path || path->empty())
      return Err("'path' required (where to save the PNG)");

    // Read the game's rendered frame straight from the renderer's GPU output
    // (isolated from other windows, backend-agnostic, reliable on headless
    // displays - see fable2_frame_capture.h).
    rex::ui::Presenter* presenter =
        presenter_provider_ ? presenter_provider_() : nullptr;
    const fable2::framecapture::Result r =
        fable2::framecapture::CaptureGuestOutputToPng(presenter, *path);
    if (!r.ok) {
      REXSYS_WARN("[remote] screenshot failed: {}", r.error);
      return Err(r.error);
    }
    REXSYS_INFO("[remote] screenshot saved {}x{} PNG to {} ({} bytes, avg_luma={})",
                r.width, r.height, *path, r.bytes, r.avg_luma);
    return std::format(
        R"({{"ok":true,"path":"{}","width":{},"height":{},"bytes":{},"avg_luma":{}{}}})",
        json_min::JEsc(*path), r.width, r.height, r.bytes, r.avg_luma, IdEcho(v));
  }

  std::string HandleCvar(const json_min::Value& v) {
    const std::string* name = v.FindStr("name");
    if (!name || name->empty()) return Err("'name' required");
    if (!FindRegisteredCvar(*name))
      return Err(std::format("unknown cvar '{}'", *name));
    const std::string* want = v.FindStr("value");
    if (want) {
      if (!rex::cvar::SetFlagByName(*name, *want))
        return Err(std::format("cvar '{}' rejected value '{}'", *name, *want));
      return Ok(std::string(",\"name\":\"") + json_min::JEsc(*name) +
                std::string("\",\"value\":\"") + json_min::JEsc(*want) +
                std::string("\"") + IdEcho(v));
    }
    const std::string current = rex::cvar::GetFlagByName(*name);
    return std::format(R"({{"ok":true,"name":"{}","value":"{}"{}}})",
                       json_min::JEsc(*name), json_min::JEsc(current), IdEcho(v));
  }

  //--- State -----------------------------------------------------------------

  InputStateStore* state_;
  std::atomic<bool>* pad_enabled_;
  Config cfg_{};
  std::function<rex::ui::Presenter*()> presenter_provider_;  // renderer presenter

  std::atomic<bool> running_{false};
  std::atomic<int32_t> port_{0};
  sock_t listen_ = kSockInvalid;
  std::atomic<int64_t> client_socket_{static_cast<int64_t>(kSockInvalid)};

  std::mutex tl_mu_;
  std::condition_variable cv_;
  Timeline timeline_;

  std::thread conn_thread_;
  std::thread timer_thread_;
};

}  // namespace fable2::remote
