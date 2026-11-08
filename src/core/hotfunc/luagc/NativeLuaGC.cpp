// Native Lua 5.1 garbage collector routines (the game's LuaPlus build).
//
// Profiling the main game thread in gameplay put about half of its time in
// these recompiled functions (see plans/lua-gc-native.md). Recompiled, every
// guest register write is stored back into ctx and every memory access is a
// byte-swapped volatile load or store; the collector is all pointer chasing,
// so it suffers the most. Here the same algorithms run on host locals.
//
//   0x82BCA7D8 StateDispatch_82BCA7D8        reallymarkobject(g, o)
//   0x82BCAA58 StateDispatch2_82BCAA58       traversetable(g, h) -> weak
//   0x82BCAC98 ProcessElementList_82BCAC98   traverseproto(g, f)
//   0x82BCAE28 StateHandler_82BCAE28         traverseclosure(g, cl)
//   0x82BCB0A8 StateDispatch_82BCB0A8        propagatemark(g) -> work estimate
//   0x82BCB610 ProcessStructure_Recursive_.. sweeplist(L, p, count) -> p
//
// Every step, including the order objects are visited and pushed on the gray
// lists, follows the guest code instruction by instruction, so the heap ends
// up exactly as the original would leave it. Only r3 (the return value) and
// guest memory are outputs: under the PowerPC ABI the volatile registers are
// dead after a call and the non-volatile ones are left untouched here. Guest
// functions this code needs (luaT_gettm for a metatable's __mode,
// traversestack, freeobj) are called through ctx as the original does.
//
// [perf] native_lua_gc in fable2_config.toml turns this off (original code).
// FABLE2_LUAGC_VERIFY=1 runs every verifiable call twice: first the native
// code against a private overlay of guest memory, then the original for real,
// and logs any byte or return value that differs.
#include "fable_2_pch.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

#include <rex/logging.h>

#include "fable2_config.h"

extern "C" void __imp__StateDispatch_82BCA7D8(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__StateDispatch2_82BCAA58(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__ProcessElementList_82BCAC98(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__StateHandler_82BCAE28(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__StateDispatch_82BCB0A8(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__ProcessStructure_Recursive_82BCB610(PPCContext& ctx, uint8_t* base);
// Guest functions called from here (through their public names, so any
// override of them still applies).
extern "C" void FindAndSetFlagBit_82BCD5E0(PPCContext& ctx, uint8_t* base);     // luaT_gettm
extern "C" void ProcessStateAndRange_82BCAF40(PPCContext& ctx, uint8_t* base);  // traversestack
extern "C" void DispatchOnKind7_82BCB480(PPCContext& ctx, uint8_t* base);       // freeobj

namespace {

// --- Lua 5.1 as compiled for the game (offsets from the guest code) ---------
constexpr uint32_t kNext = 0, kTt = 4, kMarked = 5;
constexpr uint8_t kWhiteBits = 0x03, kBlackBit = 0x04;
constexpr int32_t kFirstCollectable = 4;  // LUA_TSTRING
constexpr uint32_t kTDeadKey = 11;
// global_State
constexpr uint32_t kGCurrentWhite = 20, kGRootGc = 28, kGGray = 36, kGGrayAgain = 40,
                   kGWeak = 44, kGModeName = 172;
constexpr uint32_t kLGlobal = 16;  // lua_State::l_G

// --- Guest memory access ----------------------------------------------------
// Direct: guest memory itself. Plain (non-volatile) accesses: the collector
// runs on the thread that owns the Lua state, nothing else touches the heap.
struct Direct {
  static constexpr bool kOverlay = false;
  uint8_t* base;
  uint8_t* P(uint32_t a) const { return REX_RAW_ADDR(a); }
  uint8_t Ld8(uint32_t a) { return *P(a); }
  uint32_t Ld32(uint32_t a) {
    uint32_t v;
    std::memcpy(&v, P(a), 4);
    return __builtin_bswap32(v);
  }
  void St8(uint32_t a, uint8_t v) { *P(a) = v; }
  void St32(uint32_t a, uint32_t v) {
    v = __builtin_bswap32(v);
    std::memcpy(P(a), &v, 4);
  }
};

// Overlay: reads fall through to guest memory once and are remembered; writes
// stay private. Afterwards every remembered byte must match guest memory as
// the original code left it.
struct Overlay {
  static constexpr bool kOverlay = true;
  uint8_t* base;
  std::unordered_map<uint32_t, uint8_t> bytes;
  bool unverifiable = false;  // would have had to call guest code
  uint8_t Ld8(uint32_t a) {
    auto [it, inserted] = bytes.try_emplace(a, 0);
    if (inserted) it->second = *REX_RAW_ADDR(a);
    return it->second;
  }
  uint32_t Ld32(uint32_t a) {
    return (uint32_t(Ld8(a)) << 24) | (uint32_t(Ld8(a + 1)) << 16) | (uint32_t(Ld8(a + 2)) << 8) |
           Ld8(a + 3);
  }
  void St8(uint32_t a, uint8_t v) { bytes[a] = v; }
  void St32(uint32_t a, uint32_t v) {
    St8(a, uint8_t(v >> 24));
    St8(a + 1, uint8_t(v >> 16));
    St8(a + 2, uint8_t(v >> 8));
    St8(a + 3, uint8_t(v));
  }
};

template <class M>
inline bool IsWhite(M& m, uint32_t o) {
  return (m.Ld8(o + kMarked) & kWhiteBits) != 0;
}

// The guest's `markvalue`: a TValue whose tag is collectable and whose object
// is white gets marked.
template <class M>
void Mark(M& m, uint32_t g, uint32_t o);

template <class M>
inline void MarkValue(M& m, uint32_t g, uint32_t tv) {
  if (int32_t(m.Ld32(tv + 4)) < kFirstCollectable) return;
  const uint32_t gc = m.Ld32(tv);
  if (IsWhite(m, gc)) Mark(m, g, gc);
}

// reallymarkobject (0x82BCA7D8). Userdata tail-loops on its environment.
template <class M>
void Mark(M& m, uint32_t g, uint32_t o) {
  for (;;) {
    m.St8(o + kMarked, m.Ld8(o + kMarked) & ~kWhiteBits);  // white2gray
    const uint32_t kind = uint32_t(m.Ld8(o + kTt)) - 4;
    uint32_t gclist;
    switch (kind) {
      case 1: gclist = 24; break;  // table
      case 2: gclist = 8; break;   // function
      case 4: gclist = 88; break;  // thread
      case 5: gclist = 68; break;  // proto
      case 3: {                    // userdata: black, mark metatable, then env
        m.St8(o + kMarked, m.Ld8(o + kMarked) | kBlackBit);
        const uint32_t mt = m.Ld32(o + 8);
        if (mt != 0 && IsWhite(m, mt)) Mark(m, g, mt);
        o = m.Ld32(o + 12);
        if (!IsWhite(m, o)) return;
        continue;
      }
      case 6: {  // upvalue: mark its value; closed upvalues turn black
        MarkValue(m, g, m.Ld32(o + 8));
        if (m.Ld32(o + 8) == o + 12) m.St8(o + kMarked, m.Ld8(o + kMarked) | kBlackBit);
        return;
      }
      default:  // string (0) and anything out of range: done
        return;
    }
    m.St32(o + gclist, m.Ld32(g + kGGray));
    m.St32(g + kGGray, o);
    return;
  }
}

// traverseclosure (0x82BCAE28).
template <class M>
void TraverseClosure(M& m, uint32_t g, uint32_t cl) {
  const uint32_t env = m.Ld32(cl + 12);
  if (IsWhite(m, env)) Mark(m, g, env);
  if (m.Ld8(cl + 6) != 0) {  // C closure: TValue upvalues from +20
    for (uint32_t i = 0; int32_t(i) < int32_t(m.Ld8(cl + 7)); ++i) MarkValue(m, g, cl + 20 + 8 * i);
    return;
  }
  const uint32_t p = m.Ld32(cl + 16);  // Lua closure: proto, then UpVal pointers
  if (IsWhite(m, p)) Mark(m, g, p);
  for (uint32_t i = 0; int32_t(i) < int32_t(m.Ld8(cl + 7)); ++i) {
    const uint32_t uv = m.Ld32(cl + 20 + 4 * i);
    if (IsWhite(m, uv)) Mark(m, g, uv);
  }
}

template <class M>
inline void StringMark(M& m, uint32_t s) {  // white2gray, no gray list
  m.St8(s + kMarked, m.Ld8(s + kMarked) & ~kWhiteBits);
}

// traverseproto (0x82BCAC98).
template <class M>
void TraverseProto(M& m, uint32_t g, uint32_t f) {
  if (const uint32_t source = m.Ld32(f + 32)) StringMark(m, source);
  for (uint32_t i = 0; int32_t(i) < int32_t(m.Ld32(f + 40)); ++i)  // constants
    MarkValue(m, g, m.Ld32(f + 8) + 8 * i);
  for (uint32_t i = 0; int32_t(i) < int32_t(m.Ld32(f + 36)); ++i)  // upvalue names
    if (const uint32_t s = m.Ld32(m.Ld32(f + 28) + 4 * i)) StringMark(m, s);
  for (uint32_t i = 0; int32_t(i) < int32_t(m.Ld32(f + 52)); ++i) {  // nested protos
    const uint32_t p = m.Ld32(m.Ld32(f + 16) + 4 * i);
    if (p != 0 && IsWhite(m, p)) Mark(m, g, p);
  }
  for (uint32_t i = 0; int32_t(i) < int32_t(m.Ld32(f + 56)); ++i)  // local names
    if (const uint32_t s = m.Ld32(m.Ld32(f + 24) + 12 * i)) StringMark(m, s);
}

// strchr on a guest string, as the guest's FindChar_82CA3980 does.
template <class M>
inline bool HasChar(M& m, uint32_t s, uint8_t c) {
  for (;; ++s) {
    const uint8_t v = m.Ld8(s);
    if (v == c) return true;
    if (v == 0) return false;
  }
}

// traversetable (0x82BCAA58). Returns 1 for a weak table, else 0.
template <class M>
uint32_t TraverseTable(M& m, PPCContext& ctx, uint8_t* base, uint32_t g, uint32_t h) {
  uint32_t weakkey = 0, weakvalue = 0;
  uint32_t mt = m.Ld32(h + 8);
  if (mt != 0 && IsWhite(m, mt)) Mark(m, g, mt);
  mt = m.Ld32(h + 8);
  bool traverse_array = true;
  if (mt != 0 && (m.Ld8(mt + 6) & 0x08) == 0) {  // __mode not known absent
    uint32_t mode;
    if constexpr (M::kOverlay) {
      m.unverifiable = true;  // needs the guest luaT_gettm
      return 0;
    } else {
      ctx.r3.u64 = mt;
      ctx.r4.u64 = 3;  // TM_MODE
      ctx.r5.u64 = m.Ld32(g + kGModeName);
      ctx.lr = 0x82BCAABC;
      FindAndSetFlagBit_82BCD5E0(ctx, base);
      mode = ctx.r3.u32;
    }
    if (mode != 0 && int32_t(m.Ld32(mode + 4)) == 4) {  // a string
      const uint32_t s = m.Ld32(mode) + 16;              // getstr
      weakkey = HasChar(m, s, 'k') ? 1 : 0;
      weakvalue = HasChar(m, s, 'v') ? 1 : 0;
      if (weakkey || weakvalue) {
        const uint8_t marked = m.Ld8(h + kMarked);
        m.St8(h + kMarked, uint8_t((marked & ~0x18) | (((weakvalue << 1) | weakkey) << 3)));
        m.St32(h + 24, m.Ld32(g + kGWeak));
        m.St32(g + kGWeak, h);
      }
      if (weakkey && weakvalue) return 1;
      traverse_array = !weakvalue;
    }
  }
  if (traverse_array) {
    const uint32_t size = m.Ld32(h + 28);
    for (uint32_t i = size; i != 0;) {  // last to first
      --i;
      MarkValue(m, g, m.Ld32(h + 12) + 8 * i);
    }
  }
  const uint32_t lsize = m.Ld8(h + 7);
  const uint32_t nodes = lsize & 0x20 ? 0 : (1u << (lsize & 0x1F));
  for (uint32_t i = nodes; i != 0;) {  // last to first
    --i;
    const uint32_t n = m.Ld32(h + 16) + 20 * i;
    if (m.Ld32(n + 4) == 0) {  // nil value: removeentry
      if (int32_t(m.Ld32(n + 12)) >= kFirstCollectable) m.St32(n + 12, kTDeadKey);
      continue;
    }
    if (!weakkey) MarkValue(m, g, n + 8);
    if (!weakvalue) MarkValue(m, g, n);
  }
  return (weakkey || weakvalue) ? 1 : 0;
}

// propagatemark (0x82BCB0A8). Returns the work estimate singlestep uses.
template <class M>
uint32_t PropagateMark(M& m, PPCContext& ctx, uint8_t* base, uint32_t g) {
  const uint32_t o = m.Ld32(g + kGGray);
  m.St8(o + kMarked, m.Ld8(o + kMarked) | kBlackBit);  // gray2black
  switch (uint32_t(m.Ld8(o + kTt)) - 5) {
    case 0: {  // table
      m.St32(g + kGGray, m.Ld32(o + 24));
      const uint32_t weak = TraverseTable(m, ctx, base, g, o);
      if constexpr (M::kOverlay) {
        if (m.unverifiable) return 0;
      }
      if (weak) m.St8(o + kMarked, m.Ld8(o + kMarked) & ~kBlackBit);
      const uint32_t lsize = m.Ld8(o + 7);
      const uint32_t nodes = lsize & 0x20 ? 0 : (1u << (lsize & 0x1F));
      return 4 * (nodes + 4 * nodes) + 8 * (m.Ld32(o + 28) + 4);
    }
    case 1: {  // function
      m.St32(g + kGGray, m.Ld32(o + 8));
      TraverseClosure(m, g, o);
      const uint32_t nup = m.Ld8(o + 7);
      return m.Ld8(o + 6) != 0 ? 8 * nup + 20 : 4 * (nup + 5);
    }
    case 3: {  // thread
      const uint32_t next_gray = m.Ld32(o + 88);
      const uint32_t gray_again = m.Ld32(g + kGGrayAgain);
      m.St32(g + kGGray, next_gray);
      m.St32(o + 88, gray_again);
      m.St32(g + kGGrayAgain, o);
      m.St8(o + kMarked, m.Ld8(o + kMarked) & ~kBlackBit);
      if constexpr (M::kOverlay) {
        m.unverifiable = true;  // needs the guest traversestack
        return 0;
      } else {
        ctx.r3.u64 = g;
        ctx.r4.u64 = o;
        ctx.lr = 0x82BCB1F0;
        ProcessStateAndRange_82BCAF40(ctx, base);
      }
      return 8 * (3 * m.Ld32(o + 48) + m.Ld32(o + 44)) + 100;
    }
    case 4: {  // proto
      m.St32(g + kGGray, m.Ld32(o + 68));
      TraverseProto(m, g, o);
      return 4 * (3 * m.Ld32(o + 56) + 2 * m.Ld32(o + 40) + m.Ld32(o + 52) + m.Ld32(o + 48) +
                  m.Ld32(o + 44) + m.Ld32(o + 36) + 19);
    }
    default:  // userdata (2) and anything out of range
      return 0;
  }
}

// sweeplist (0x82BCB610). Calls the guest freeobj for dead objects, so it
// only runs directly on guest memory. Returns the position it stopped at.
uint32_t SweepList(Direct& m, PPCContext& ctx, uint8_t* base, uint32_t L, uint32_t p,
                   uint32_t count) {
  const uint32_t g = m.Ld32(L + kLGlobal);
  const uint32_t deadmask = m.Ld8(g + kGCurrentWhite) ^ kWhiteBits;  // otherwhite
  uint32_t curr = m.Ld32(p);
  while (curr != 0) {
    if (count-- == 0) break;
    if (m.Ld8(curr + kTt) == 8) SweepList(m, ctx, base, L, curr + 84, 0xFFFFFFFDu);  // open upvalues
    const uint8_t marked = m.Ld8(curr + kMarked);
    if (((marked ^ kWhiteBits) & deadmask) != 0) {  // alive: makewhite
      const uint8_t white = m.Ld8(g + kGCurrentWhite);
      m.St8(curr + kMarked, uint8_t(((marked & 0xF8) | (white & 0x07)) & ~kBlackBit));
      p = curr;  // &curr->next
    } else {  // dead: unlink and free
      m.St32(p, m.Ld32(curr + kNext));
      if (curr == m.Ld32(g + kGRootGc)) m.St32(g + kGRootGc, m.Ld32(curr + kNext));
      ctx.r3.u64 = L;
      ctx.r4.u64 = curr;
      ctx.lr = 0x82BCB6C4;
      DispatchOnKind7_82BCB480(ctx, base);
    }
    curr = m.Ld32(p);
  }
  return p;
}

// --- Mode, verification and statistics --------------------------------------
enum class Mode { kOriginal, kNative, kVerify };

Mode GetMode() {
  static const Mode mode = [] {
    if (!fable2::config::Get().native_lua_gc) return Mode::kOriginal;
    const char* v = std::getenv("FABLE2_LUAGC_VERIFY");
    if (v && v[0] == '1') {
      REXSYS_WARN("[lua-gc] verify mode: native GC code is checked against the original");
      return Mode::kVerify;
    }
    REXSYS_INFO("[lua-gc] native Lua GC routines active");
    return Mode::kNative;
  }();
  return mode;
}

std::atomic<uint64_t> g_verified{0}, g_mismatched{0}, g_unverifiable{0};

void Compare(Overlay& ov, uint8_t* base, const char* fn, bool has_ret, uint32_t native_ret,
             uint32_t original_ret) {
  if (ov.unverifiable) {
    g_unverifiable.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  uint32_t bad = 0, first = 0;
  uint8_t want = 0, got = 0;
  for (const auto& [addr, value] : ov.bytes) {
    const uint8_t real = *REX_RAW_ADDR(addr);
    if (real != value) {
      if (bad++ == 0) first = addr, want = value, got = real;
    }
  }
  const bool ret_bad = has_ret && native_ret != original_ret;
  const uint64_t n = g_verified.fetch_add(1, std::memory_order_relaxed) + 1;
  if (bad || ret_bad) {
    const uint64_t m = g_mismatched.fetch_add(1, std::memory_order_relaxed) + 1;
    if (m <= 40) {
      REXSYS_WARN(
          "[lua-gc] MISMATCH in {}: {} of {} bytes differ (first {:08X}: native {:02X}, "
          "original {:02X}); return native {} original {}",
          fn, bad, ov.bytes.size(), first, want, got, native_ret, original_ret);
    }
  }
  if ((n & 0xFFFFF) == 0) {
    REXSYS_INFO("[lua-gc] verified {} calls, {} mismatched, {} not verifiable", n,
                g_mismatched.load(), g_unverifiable.load());
  }
}

}  // namespace

// --- Overrides ----------------------------------------------------------------

extern "C" void StateDispatch_82BCA7D8(PPCContext& __restrict ctx, uint8_t* base) {
  const uint32_t g = ctx.r3.u32, o = ctx.r4.u32;
  switch (GetMode()) {
    case Mode::kNative: {
      Direct m{base};
      Mark(m, g, o);
      return;
    }
    case Mode::kVerify: {
      Overlay ov{base};
      Mark(ov, g, o);
      __imp__StateDispatch_82BCA7D8(ctx, base);
      Compare(ov, base, "reallymarkobject", false, 0, 0);
      return;
    }
    default:
      __imp__StateDispatch_82BCA7D8(ctx, base);
  }
}

extern "C" void StateHandler_82BCAE28(PPCContext& __restrict ctx, uint8_t* base) {
  const uint32_t g = ctx.r3.u32, cl = ctx.r4.u32;
  switch (GetMode()) {
    case Mode::kNative: {
      Direct m{base};
      TraverseClosure(m, g, cl);
      return;
    }
    case Mode::kVerify: {
      Overlay ov{base};
      TraverseClosure(ov, g, cl);
      __imp__StateHandler_82BCAE28(ctx, base);
      Compare(ov, base, "traverseclosure", false, 0, 0);
      return;
    }
    default:
      __imp__StateHandler_82BCAE28(ctx, base);
  }
}

extern "C" void ProcessElementList_82BCAC98(PPCContext& __restrict ctx, uint8_t* base) {
  const uint32_t g = ctx.r3.u32, f = ctx.r4.u32;
  switch (GetMode()) {
    case Mode::kNative: {
      Direct m{base};
      TraverseProto(m, g, f);
      return;
    }
    case Mode::kVerify: {
      Overlay ov{base};
      TraverseProto(ov, g, f);
      __imp__ProcessElementList_82BCAC98(ctx, base);
      Compare(ov, base, "traverseproto", false, 0, 0);
      return;
    }
    default:
      __imp__ProcessElementList_82BCAC98(ctx, base);
  }
}

extern "C" void StateDispatch2_82BCAA58(PPCContext& __restrict ctx, uint8_t* base) {
  const uint32_t g = ctx.r3.u32, h = ctx.r4.u32;
  switch (GetMode()) {
    case Mode::kNative: {
      Direct m{base};
      ctx.r3.u64 = TraverseTable(m, ctx, base, g, h);
      return;
    }
    case Mode::kVerify: {
      Overlay ov{base};
      const uint32_t native_ret = TraverseTable(ov, ctx, base, g, h);
      __imp__StateDispatch2_82BCAA58(ctx, base);
      Compare(ov, base, "traversetable", true, native_ret, ctx.r3.u32);
      return;
    }
    default:
      __imp__StateDispatch2_82BCAA58(ctx, base);
  }
}

extern "C" void StateDispatch_82BCB0A8(PPCContext& __restrict ctx, uint8_t* base) {
  const uint32_t g = ctx.r3.u32;
  switch (GetMode()) {
    case Mode::kNative: {
      Direct m{base};
      ctx.r3.u64 = PropagateMark(m, ctx, base, g);
      return;
    }
    case Mode::kVerify: {
      Overlay ov{base};
      const uint32_t native_ret = PropagateMark(ov, ctx, base, g);
      __imp__StateDispatch_82BCB0A8(ctx, base);
      Compare(ov, base, "propagatemark", true, native_ret, ctx.r3.u32);
      return;
    }
    default:
      __imp__StateDispatch_82BCB0A8(ctx, base);
  }
}

extern "C" void ProcessStructure_Recursive_82BCB610(PPCContext& __restrict ctx, uint8_t* base) {
  // sweeplist frees objects through the guest, so verify mode runs the original.
  if (GetMode() != Mode::kNative) {
    __imp__ProcessStructure_Recursive_82BCB610(ctx, base);
    return;
  }
  Direct m{base};
  const uint32_t L = ctx.r3.u32, p = ctx.r4.u32, count = ctx.r5.u32;
  ctx.r3.u64 = SweepList(m, ctx, base, L, p, count);
}
