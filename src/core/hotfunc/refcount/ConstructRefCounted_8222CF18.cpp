// Hand-tuned override of recompiled ConstructRefCounted_8222CF18
// (recompiler output: fable_2_recomp.109.cpp; generated transforms nv=1 mfmsr=1 glock=1).
// The "// Hand-tuned override" first line makes tools/make_hotfunc_overrides.py
// skip this file; re-run that script to regenerate the literal reference copy.
//
// The recompiler emits recompiled guest functions as WEAK extern "C" aliases
// of __imp__ConstructRefCounted_8222CF18; fable_2_register.cpp registers the
// ALIAS in the indirect-dispatch table. This strong definition intercepts both
// direct bl calls and bctrl dispatch. __imp__ConstructRefCounted_8222CF18
// remains the original.
//
// Behavior-identical to the generated copy: same guest RAM reads/writes (same
// addresses, same order), same global critical-region lock + lock-count
// side effects, same callee targets and guest return addresses, same
// r3/r1/lr/r30/r31/r12/cr6 side effects at every call site and on return.
// Optimizations versus the generated copy (both safe, both documented below):
//   1. The mfmsr/mtmsrd/mtmsrd MSR interrupt-lock sequence and its three
//      std::memory_order_seq_cst fences are dropped. No code in the entire
//      recompiled binary ever *reads* ctx.msr: every mtmsrd write uses the
//      self-preserving pattern (REG & 0x8020) | (ctx.msr & ~0x8020), the
//      mfmsr fast path reads the global lock count (not ctx.msr), and the
//      0x8020 bits written here are masked away by every later mtmsrd read of
//      the same pattern. The mfmsr result (r10) only fed the unobservable
//      mtmsrd. The mutex lock/unlock already supplies the ordering the fences
//      provided.
//   2. The lwarx/stwcx retry loop around the global live-object counter is
//      reduced to a plain read-modify-write inside the lock. The counter is
//      written only by this function (increment) and
//      DestructRefCounted_82214F08 (decrement), and both take the same global
//      critical-region lock, so the CAS can never fail and the locked
//      compare-exchange is unnecessary; the lock already serializes the RMW.
//      (The counter is a live-object telemetry count, not a correctness
//      refcount, so this is safe even in the unlikely case a writer skipped
//      the lock.)
// (Volatile non-argument registers r8-r11 may end up holding different
// values than in the generated copy; under the PPC32 ABI they are
// caller-saved, so well-formed guest code writes them before use - only r2,
// r13-r30, r1, and the argument registers are preserved across calls, and
// those are preserved exactly here. cr0 is still left in the stwcx-success
// state, matching the generated copy's post-loop state.)
//
// Guest logic (see per-line disassembly references):
//
//   // r3 = object, r4 = source string (COM-style string member assign)
//   void ConstructRefCounted(void* obj, const char* src)
//   {
//     obj->ref = nullptr;                    // stw r30(0),0(r31)
//     global_critical_region {               // mfmsr/mtmsrd + SDK global lock
//       ++g_live_object_count;               // lwarx/addi/stwcx @ 0x83496EB8
//     }
//     if (src && *src)
//       obj->ref = AllocateString(obj, src); // bl 0x822079D8 (r30 = result)
//     if (obj->ref)                          // lwz r11,0(r31)
//       Release_RefCounted(obj);             // bl 0x821C67D8
//     if (obj->ref_new) obj->ref = obj->ref_new;
//     return obj;                            // mr r3,r31
//   }

#include "fable_2_pch.h"

extern "C" void AllocateString_822079D8(PPCContext& ctx, uint8_t* base);
extern "C" void Release_RefCounted_821C67D8(PPCContext& ctx, uint8_t* base);

namespace {

// Non-volatile guest-RAM access. Same address math and byte order as the pch's
// REX_LOAD_*/REX_STORE_* macros, without `volatile` (safe: this function
// touches no MMIO, and every call it makes is opaque to the optimizer, so no
// access can be CSE'd across one).
//
// Marked always_inline so each access expands to a direct memory op even at
// -O0 (the Debug build). A plain `inline` here would make clang emit a call
// per access at -O0, which would make this override *slower* than the
// generated copy (whose GV*/SV* macros are always direct memory ops).
#define HOTFUNC_ALWAYS_INLINE __attribute__((always_inline))
HOTFUNC_ALWAYS_INLINE uint8_t* gaddr(u32 addr, uint8_t* base) {
  return base + (u32)addr + REX_PHYS_HOST_OFFSET(addr);
}
HOTFUNC_ALWAYS_INLINE u8 gload8(u32 addr, u8* base) {
  return *reinterpret_cast<const u8*>(gaddr(addr, base));
}
HOTFUNC_ALWAYS_INLINE u32 gload32(u32 addr, u8* base) {
  return __builtin_bswap32(*reinterpret_cast<const u32*>(gaddr(addr, base)));
}
HOTFUNC_ALWAYS_INLINE u64 gload64(u32 addr, u8* base) {
  return __builtin_bswap64(*reinterpret_cast<const u64*>(gaddr(addr, base)));
}
HOTFUNC_ALWAYS_INLINE void gstore32(u32 addr, u8* base, u32 v) {
  *reinterpret_cast<u32*>(gaddr(addr, base)) = __builtin_bswap32(v);
}
HOTFUNC_ALWAYS_INLINE void gstore64(u32 addr, u8* base, u64 v) {
  *reinterpret_cast<u64*>(gaddr(addr, base)) = __builtin_bswap64(v);
}

// Cached reference to the SDK's global critical region mutex: same mutex and
// same lock/unlock order as REX_ENTER/LEAVE_GLOBAL_LOCK, without the
// per-use DLL call to global_critical_region::mutex().
inline std::recursive_mutex& hotfunc_gcr_mutex() {
  static thread_local std::recursive_mutex& cached =
      rex::thread::global_critical_region::mutex();
  return cached;
}

// Global live-object count (guest word at 0x83496EB8; lis r9,-31927 =
// 0x83490000, addi r8,r9,28344 = 0x83496EB8). Incremented here (construct),
// decremented by DestructRefCounted_82214F08. Both writers take the same
// global critical-region lock. (A live-object telemetry count, not the
// per-object refcount that Release_RefCounted manages.)
constexpr u32 kLiveObjectCountAddr = 0x83496EB8;

}  // namespace

extern "C" void ConstructRefCounted_8222CF18(PPCContext& __restrict ctx, uint8_t* base) {
  REX_FUNC_PROLOGUE();

  // --- Guest prologue: mflr r12; stw r12,-8(r1); std r30,-24(r1); std r31,-16(r1);
  //     stwu r1,-112(r1) ---
  const u32 entry_sp = ctx.r1.u32;
  const u32 frame_sp = entry_sp - 112;
  ctx.r12.u64 = ctx.lr;
  gstore32(entry_sp - 8, base, ctx.r12.u32);
  gstore64(entry_sp - 24, base, ctx.r30.u64);
  gstore64(entry_sp - 16, base, ctx.r31.u64);
  gstore32(frame_sp, base, entry_sp);
  ctx.r1.u32 = frame_sp;

  // mr r31,r3; li r30,0: hold the object in r31, start with result r30 = 0.
  ctx.r31.u64 = ctx.r3.u64;
  ctx.r30.u64 = 0;
  const u32 obj = ctx.r3.u32;
  const u32 src = ctx.r4.u32;

  // stw r30,0(r31): clear the object's reference.
  gstore32(obj, base, 0);

  // Global critical region { lwarx r11,0,r8; addi r11,r11,1; stwcx. r11,0,r8 }:
  // increment the global live-object count. See header note 1 (MSR/fence dance
  // dropped - unobservable) and note 2 (lwarx/stwcx retry reduced to a plain
  // RMW, safe because construct+destruct are the only writers and both hold
  // this same lock).
  hotfunc_gcr_mutex().lock();
  ppc_global_lock_count_().fetch_add(1);
  gstore32(kLiveObjectCountAddr, base, gload32(kLiveObjectCountAddr, base) + 1);
  { auto old_ = ppc_global_lock_count_().fetch_sub(1); assert(old_ >= 1); }
  hotfunc_gcr_mutex().unlock();
  // stwcx. success (always, under the lock): cr0 = {0, 0, 1, xer.so}.
  ctx.cr0.lt = 0;
  ctx.cr0.gt = 0;
  ctx.cr0.eq = 1;
  ctx.cr0.so = ctx.xer.so;

  // if (src && *src): r30 = AllocateString(obj, src)
  ctx.cr6.compare<u32>(src, 0, ctx.xer);  // cmplwi cr6,r4,0
  if (src != 0) {
    const u8 first = gload8(src, base);
    ctx.cr6.compare<u32>(first, 0, ctx.xer);  // cmplwi cr6,r11,0
    if (first != 0) {
      ctx.r3.u64 = obj;
      ctx.lr = 0x8222CF78;
      AllocateString_822079D8(ctx, base);
      ctx.r30.u64 = ctx.r3.u64;  // mr r30,r3
    }
  }

  // if (obj->ref): Release_RefCounted(obj)
  const u32 old_ref = gload32(obj, base);  // lwz r11,0(r31)
  ctx.cr6.compare<u32>(old_ref, 0, ctx.xer);
  if (old_ref != 0) {
    ctx.r3.u64 = obj;
    ctx.lr = 0x8222CF90;
    Release_RefCounted_821C67D8(ctx, base);
  }

  // if (r30): obj->ref = r30
  ctx.cr6.compare<u32>(ctx.r30.u32, 0, ctx.xer);
  if (ctx.r30.u32 != 0) {
    gstore32(obj, base, ctx.r30.u32);
  }

  // mr r3,r31: return the object.
  ctx.r3.u64 = obj;

  // --- Guest epilogue: addi r1,r1,112; lwz r12,-8(r1); mtlr r12;
  //     ld r30,-24(r1); ld r31,-16(r1); blr ---
  ctx.r1.u32 = entry_sp;
  ctx.r12.u64 = gload32(entry_sp - 8, base);
  ctx.lr = ctx.r12.u64;
  ctx.r30.u64 = gload64(entry_sp - 24, base);
  ctx.r31.u64 = gload64(entry_sp - 16, base);
}
