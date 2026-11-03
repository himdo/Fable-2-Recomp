// fable_2 - run Lua inside the GameFace (GUI) Lua state, for the developer console.
//
// The HUD and menus are Anark GameFace 4.0.5 presentations. Their scripts run in
// GameFace's own Lua 5.1 state (4-byte float numbers, 8-byte TValues), separate
// from the game's Lua, and only that state has the element API:
// getElement(path[, parent]), getAttribute(el, name), setAttribute(el, name, v),
// getChildren(el). Attributes include position.x/y, scale.x/y, rotation.z,
// opacity, active, diffuse.r/g/b/a, textstring, textcolor.r/g/b.
//
// Addresses (read from the decrypted default.xex; see docs/console/GUI-LUA-BRIDGE.md):
//   0x82BB2528  GameFace registers its globals; r3 = context, lua_State* at ctx+4
//   0x82BB68D8  native getElement      0x82BB7100  native setAttribute (r3 = L)
//   0x82BC6130  lua_load(L, reader, data, chunkname)
//   0x82A1FF68  getS reader for lua_load (data = {const char* s; size_t size})
//   0x822C05F8  luaD_pcall(L, func, ud, oldtop, ef)
//   0x82BC6120  f_call(L, CallS* {StkId func; int nresults}) (what luaB_pcall passes)
// lua_State: tt byte +4, top +8, base +0xC, stack_last +0x1C, stack +0x20.
//
// A chunk is loaded and called in protected mode, so a Lua error comes back as
// a status and message instead of unwinding through the host. The chunk returns
// one string, which the host reads from the stack and pops.
//
// Threading: chunks run on the thread that runs the GUI Lua (recorded from the
// native hooks). If that is the console's thread (MainRenderLoop) they run from
// poll(); otherwise from the next native getElement/setAttribute call.

#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "fable2_f5_lua.h"

namespace fable2::guilua {

namespace ta = fable2::textappend;

constexpr uint32_t kLuaLoad = 0x82BC6130;
constexpr uint32_t kGetS = 0x82A1FF68;
constexpr uint32_t kLuaDPcall = 0x822C05F8;
constexpr uint32_t kFCall = 0x82BC6120;
constexpr uint32_t kLuaTString = 4;  // LUA_TSTRING
constexpr uint32_t kLuaTThread = 8;  // LUA_TTHREAD

struct Pending {
  std::string code;
  uint32_t L = 0;   // unused since console-v10: chunks run on the calling state
  bool probe = false;
  int tries = 0;
  std::vector<uint32_t> tried;  // states that answered "no element" (broadcast: states done)
  std::string tag;              // non-empty: a broadcast (enqueue_all); a newer one with the tag replaces it
};

struct State {
  std::mutex mu;
  std::vector<uint32_t> states;   // every GameFace lua_State seen (registration)
  uint32_t last_active = 0;       // L of the latest native getElement/setAttribute call
  uint32_t hud = 0;               // L whose globals have g_HUD
  uint32_t chosen = 0;            // set by "gui use"
  // Lua condition a state must meet before a chunk runs in it ("gui where").
  // "true" = any GameFace state (HUD, menus, title); a chunk that cannot find
  // its element moves on to the next state that calls in.
  std::string where = "true";
  std::thread::id gui_tid{};
  bool gui_tid_known = false;
  std::thread::id main_tid{};
  std::deque<Pending> pending;
  std::deque<std::string> results;
  std::vector<uint32_t> probed;
  uint32_t buf = 0, buf_size = 0;
  uint64_t runs = 0, errors = 0;
  bool busy = false;              // re-entrancy guard (our chunk calls the natives too)
  std::atomic<bool> armed{false}; // set once the console is in use; native hooks pass through until then
};
inline State& S() {
  static State s;
  return s;
}

inline void store_be(uint8_t* base, uint32_t a, uint32_t v) {
  const uint32_t w = __builtin_bswap32(v);
  std::memcpy(reinterpret_cast<void*>(ta::host_addr(base, a)), &w, 4);
}

inline bool valid_state(const uint8_t* base, uint32_t L) {
  if (L < 0x1000 || L == 0xFFFFFFFF || !ta::rdable2(base, L, 0x24)) return false;
  const uint8_t tt = *reinterpret_cast<const uint8_t*>(ta::host_addr(base, L + 4));
  if (tt != kLuaTThread) return false;
  const uint32_t top = ta::load_be(base, L + 8), last = ta::load_be(base, L + 0x1C),
                 stack = ta::load_be(base, L + 0x20);
  // stack_last sits EXTRA_STACK (5) slots below the end; a live top is between.
  return stack != 0 && top >= stack && top <= last + 8 * 5 && ta::rdable2(base, stack, 16);
}

inline bool functions_resolve() {
  static const bool ok = rex::runtime::ResolveIndirectFunction(kLuaLoad) &&
                         rex::runtime::ResolveIndirectFunction(kLuaDPcall) &&
                         rex::runtime::ResolveIndirectFunction(kFCall) &&
                         rex::runtime::ResolveIndirectFunction(kGetS);
  return ok;
}

inline uint32_t target_state(const uint8_t* base) {
  State& s = S();
  for (uint32_t L : {s.chosen, s.hud, s.last_active})
    if (L && valid_state(base, L)) return L;
  return 0;
}

// Wraps user code: a helper table (__f2) is defined once per state, then the
// code runs inside pcall and the chunk returns "ok <value>" or "error <msg>".
inline std::string wrap(const std::string& code) {
  static const char* kPrelude = R"LUA(
if not __f2 then
  __f2 = { orig = {}, names = {}, cache = {} }
  local F = __f2
  function F.name(e) local ok, n = pcall(getAttribute, e, "name") return ok and tostring(n) or "?" end
  function F.find(n)
    if type(n) ~= "string" then return n end
    local c = F.cache[n]
    if c then return c end
    for _, p in ipairs({ n, "Scene." .. n, "Scene.Layer." .. n }) do
      local ok, e = pcall(getElement, p)
      if ok and e then F.cache[n] = e return e end
    end
    local found
    local function walk(el, d)
      if found or d > 14 then return end
      if F.name(el) == n then found = el return end
      local ok, ch = pcall(getChildren, el)
      if ok and type(ch) == "table" then
        for _, c2 in ipairs(ch) do walk(c2, d + 1) end
      end
    end
    local ok, root = pcall(getElement, "Scene")
    if ok and root then walk(root, 0) end
    if found then F.cache[n] = found end
    return found
  end
  function F.need(n)
    local e = F.find(n)
    if not e then error("no element '" .. tostring(n) .. "' (ui tree lists names)", 0) end
    return e
  end
  function F.get(e, a) local ok, v = pcall(getAttribute, e, a) if ok then return v end return nil end
  function F.set(n, e, a, v)
    F.orig[n] = F.orig[n] or {}
    if F.orig[n][a] == nil then
      local cur = F.get(e, a)
      if cur ~= nil then F.orig[n][a] = cur end
    end
    F.names[n] = e
    setAttribute(e, a, v)
  end
  function F.desc(n, e)
    local function g(a) local v = F.get(e, a) if v == nil then return "-" end return tostring(v) end
    return n .. " pos=(" .. g("position.x") .. "," .. g("position.y") .. ") scale=(" .. g("scale.x") .. "," ..
           g("scale.y") .. ") rot=" .. g("rotation.z") .. " opacity=" .. g("opacity") .. " active=" .. g("active")
  end
  function F.tree(e, depth, out, d, path)
    if #out >= 300 then return end
    local nm = F.name(e)
    local p = path and (path .. "." .. nm) or nm
    out[#out + 1] = string.rep("  ", d) .. nm .. "  (" .. p .. ")"
    if d >= depth then return end
    local ok, ch = pcall(getChildren, e)
    if ok and type(ch) == "table" then
      for _, c in ipairs(ch) do F.tree(c, depth, out, d + 1, p) end
    end
  end
end
)LUA";
  return std::string(kPrelude) + "local __ok, __r = pcall(function()\n" + code +
         "\nend)\nif __ok then return 'ok ' .. tostring(__r) end\nreturn 'error ' .. tostring(__r)\n";
}

// Reads the string (or a type note) in the TValue at `slot`.
inline std::string read_tvalue(const uint8_t* base, uint32_t slot) {
  const uint32_t tt = ta::load_be(base, slot + 4);
  if (tt != kLuaTString) return "(non-string result, type " + std::to_string(tt) + ")";
  const uint32_t ts = ta::load_be(base, slot);
  if (!ta::rdable2(base, ts, 16)) return "(unreadable string)";
  uint32_t len = ta::load_be(base, ts + 12);
  if (len > 16000) len = 16000;
  if (!ta::rdable2(base, ts + 16, len)) return "(unreadable string)";
  return std::string(reinterpret_cast<const char*>(ta::host_addr(base, ts + 16)), len);
}

// Loads and runs one chunk in state L. Caller saves/restores ctx. Returns the
// chunk's string result, or the load/run error.
inline std::string run_chunk(PPCContext& ctx, uint8_t* base, uint32_t L, const std::string& code, bool& ok) {
  State& s = S();
  ok = false;
  if (!functions_resolve()) return "error GameFace Lua functions do not resolve in this build";
  if (!valid_state(base, L)) return "error GUI lua_State " + std::to_string(L) + " is not valid";
  const std::string src = wrap(code);
  const uint32_t need = static_cast<uint32_t>(src.size()) + 128;
  if (s.buf == 0 || s.buf_size < need) {
    const uint32_t size = need < 32768 ? 32768 : need + 4096;
    s.buf = ta::guest_alloc(ctx, base, size);  // the old buffer is leaked (rare)
    s.buf_size = s.buf ? size : 0;
    if (!s.buf || !ta::wr2(base, s.buf, size)) {
      s.buf = 0;
      return "error guest allocation failed";
    }
  }
  const uint32_t ls = s.buf, calls = s.buf + 8, name = s.buf + 32, text = s.buf + 128;
  std::memcpy(reinterpret_cast<void*>(ta::host_addr(base, name)), "=f2gui", 7);
  std::memcpy(reinterpret_cast<void*>(ta::host_addr(base, text)), src.data(), src.size());
  store_be(base, ls, text);
  store_be(base, ls + 4, static_cast<uint32_t>(src.size()));

  // lua_load(L, getS, &LoadS, "=f2gui"): pushes the function or an error message.
  ctx.r3.u64 = L;
  ctx.r4.u64 = kGetS;
  ctx.r5.u64 = ls;
  ctx.r6.u64 = name;
  REX_CALL_INDIRECT_FUNC(kLuaLoad);
  uint32_t status = ctx.r3.u32;
  if (status == 0) {
    // lua_pcall(L, 0, 1, 0) as luaB_pcall does it: luaD_pcall(L, f_call, &CallS, savestack(func), 0).
    const uint32_t func = ta::load_be(base, L + 8) - 8;
    const uint32_t stack = ta::load_be(base, L + 0x20);
    store_be(base, calls, func);
    store_be(base, calls + 4, 1);
    ctx.r3.u64 = L;
    ctx.r4.u64 = kFCall;
    ctx.r5.u64 = calls;
    ctx.r6.u64 = func - stack;
    ctx.r7.u64 = 0;
    REX_CALL_INDIRECT_FUNC(kLuaDPcall);
    status = ctx.r3.u32;
  }
  // Either way one value (result or error message) now sits at top-1.
  const uint32_t top = ta::load_be(base, L + 8);
  std::string res = read_tvalue(base, top - 8);
  store_be(base, L + 8, top - 8);  // pop it
  ++s.runs;
  if (status != 0) {
    ++s.errors;
    return "error " + res;
  }
  ok = res.rfind("ok ", 0) == 0;
  if (!ok) ++s.errors;
  return res;
}

inline void push_result(const std::string& r) {
  State& s = S();
  std::lock_guard<std::mutex> lock(s.mu);
  if (s.results.size() < 256) s.results.push_back(r);
}

// Runs every pending chunk that targets a state this thread may touch.
inline void drain(PPCContext& ctx, uint8_t* base) {
  State& s = S();
  if (s.busy) return;
  // Run only on the state that is calling the hooked native right now: it is
  // inside a C call, so loading and calling a chunk on it is safe. Other
  // captured states can be coroutines in the middle of a resume, and running
  // code on those corrupted the VM (in-game crash, console-v10).
  const uint32_t L = ctx.r3.u32;
  if (!valid_state(base, L)) return;
  s.busy = true;
  const PPCContext saved = ctx;
  Pending p;
  {
    std::lock_guard<std::mutex> lock(s.mu);
    if (s.pending.empty()) {
      s.busy = false;
      return;
    }
    p = s.pending.front();
    s.pending.pop_front();
  }
  bool ok = false;
  std::string r;
  if (!p.tag.empty()) {
    // Broadcast: once in each state that calls in, quietly; it stays queued
    // (at the back) until every captured state has had it or 600 calls pass.
    if (std::find(p.tried.begin(), p.tried.end(), L) == p.tried.end()) {
      try {
        run_chunk(ctx, base, L, p.code, ok);
      } catch (...) {
      }
      ctx = saved;
      p.tried.push_back(L);
    }
    {
      std::lock_guard<std::mutex> lock(s.mu);
      if (++p.tries < 600 && p.tried.size() < s.states.size()) s.pending.push_back(p);
    }
    s.busy = false;
    return;
  }
  try {
    // Chunks run in a state meeting "gui where" (any state by default); any
    // other answers "__f2_wrong_state" and the command waits for the next call.
    std::string where;
    {
      std::lock_guard<std::mutex> lock(s.mu);
      where = s.where;
    }
    r = run_chunk(ctx, base, L,
                  "local __w_ok, __w = pcall(function() return " + where + " end)\n"
                  "if not (__w_ok and __w) then return '__f2_wrong_state' end\n" + p.code, ok);
  } catch (...) {
    r = "error exception while running GUI Lua";
  }
  ctx = saved;
  // A missing element means another screen's state may have it: try each
  // state that calls in once before giving up.
  const bool missing = r.rfind("error no element '", 0) == 0;
  if (missing && std::find(p.tried.begin(), p.tried.end(), L) == p.tried.end()) p.tried.push_back(L);
  const bool retry = r == "ok __f2_wrong_state" || (missing && p.tried.size() < s.states.size());
  if (retry) {
    std::lock_guard<std::mutex> lock(s.mu);
    if (++p.tries < 300) s.pending.push_front(p);
    else s.results.push_back(missing ? r + " (in " + std::to_string(p.tried.size()) + " GUI states)"
                                     : "error no GUI Lua state met 'gui where " + s.where + "' (is that screen up?)");
  } else {
    if (r.rfind("ok", 0) == 0) s.hud = L;  // the state that last answered
    push_result(r);
  }
  s.busy = false;
}

inline void note_main_thread() { S().main_tid = std::this_thread::get_id(); }

// Called by the console every frame (game thread).
inline void poll(PPCContext& ctx, uint8_t* base) {
  State& s = S();
  static const bool bridge_on = [] {
    const char* e = std::getenv("FABLE2_GUI_BRIDGE");
    return !(e && *e == '0');
  }();
  if (!bridge_on) return;
  s.armed.store(true, std::memory_order_relaxed);
  s.main_tid = std::this_thread::get_id();
  // Chunks run only inside the hooked GUI natives (see drain), never from here.
}

inline void enqueue(const std::string& code, uint32_t L = 0, bool probe = false) {
  State& s = S();
  std::lock_guard<std::mutex> lock(s.mu);
  if (s.pending.size() < 64) s.pending.push_back({code, L, probe});
}

// Runs code once in every GameFace state (as each next calls a hooked
// native), with no "gui where" check and no reply. A newer broadcast with the
// same tag replaces one still queued.
inline void enqueue_all(const std::string& tag, const std::string& code) {
  State& s = S();
  std::lock_guard<std::mutex> lock(s.mu);
  for (auto it = s.pending.begin(); it != s.pending.end();)
    it = it->tag == tag ? s.pending.erase(it) : it + 1;
  Pending p;
  p.code = code;
  p.tag = tag;
  s.pending.push_back(p);
}

inline std::vector<std::string> take_results() {
  State& s = S();
  std::lock_guard<std::mutex> lock(s.mu);
  std::vector<std::string> out(s.results.begin(), s.results.end());
  s.results.clear();
  return out;
}

inline void note_state(const uint8_t* base, uint32_t L, bool active) {
  State& s = S();
  if (active && L == s.last_active && s.gui_tid_known) return;  // hot path (getAttribute)
  if (!valid_state(base, L)) return;
  if (active) {
    s.last_active = L;
    s.gui_tid = std::this_thread::get_id();
    s.gui_tid_known = true;
  }
  std::lock_guard<std::mutex> lock(s.mu);
  for (uint32_t x : s.states)
    if (x == L) return;
  if (s.states.size() < 64) s.states.push_back(L);
}

inline std::string status_line(const uint8_t* base) {
  State& s = S();
  char b[320];
  std::snprintf(b, sizeof b,
                "states %zu, target 0x%08X (hud 0x%08X, chosen 0x%08X, last active 0x%08X), gui thread %s, "
                "pending %zu, runs %llu, errors %llu",
                s.states.size(), target_state(base), s.hud, s.chosen, s.last_active,
                !s.gui_tid_known ? "unknown yet" : (s.gui_tid == s.main_tid ? "= console thread" : "separate"),
                s.pending.size(), static_cast<unsigned long long>(s.runs), static_cast<unsigned long long>(s.errors));
  return b;
}

}  // namespace fable2::guilua

// GameFace registers its Lua globals (getElement, setAttribute, ...) here, once
// per script context; the lua_State* is at context+4.
extern "C" void ProcessAndProcessAndProcess53_82BB2528(PPCContext& ctx, uint8_t* base) {
  namespace ta = fable2::textappend;
  const uint32_t self = ctx.r3.u32;
  __imp__ProcessAndProcessAndProcess53_82BB2528(ctx, base);
  if (self && ta::rdable2(base, self + 4, 4))
    fable2::guilua::note_state(base, ta::load_be(base, self + 4), false);
}

// Native getElement / setAttribute: r3 is the calling lua_State. Recording it
// tells us which state is live and which thread runs GUI Lua; queued chunks run
// here when that thread is not the console's.
extern "C" void ProcessAndHandleType3_82BB68D8(PPCContext& ctx, uint8_t* base) {
  auto& s = fable2::guilua::S();
  if (s.armed.load(std::memory_order_relaxed) && !s.busy) {
    fable2::guilua::note_state(base, ctx.r3.u32, true);
    if (!s.pending.empty()) fable2::guilua::drain(ctx, base);
  }
  __imp__ProcessAndHandleType3_82BB68D8(ctx, base);
}

// Native getAttribute: called constantly by the HUD scripts (~2,700/s in
// gameplay, where getElement/setAttribute are rare), so it is the drain point
// that keeps queued GUI commands moving.
extern "C" void ProcessAndHandleType_82BB6CA0(PPCContext& ctx, uint8_t* base) {
  auto& s = fable2::guilua::S();
  if (s.armed.load(std::memory_order_relaxed) && !s.busy) {
    fable2::guilua::note_state(base, ctx.r3.u32, true);
    if (!s.pending.empty()) fable2::guilua::drain(ctx, base);
  }
  __imp__ProcessAndHandleType_82BB6CA0(ctx, base);
}

extern "C" void ProcessAndReadAndProcess_82BB7100(PPCContext& ctx, uint8_t* base) {
  auto& s = fable2::guilua::S();
  if (s.armed.load(std::memory_order_relaxed) && !s.busy) {
    fable2::guilua::note_state(base, ctx.r3.u32, true);
    if (!s.pending.empty()) fable2::guilua::drain(ctx, base);
  }
  __imp__ProcessAndReadAndProcess_82BB7100(ctx, base);
}
