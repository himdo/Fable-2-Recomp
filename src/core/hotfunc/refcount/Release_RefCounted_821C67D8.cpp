// Hand-tuned override of recompiled Release_RefCounted_821C67D8
// (recompiler output: fable_2_recomp.15.cpp; generated transforms nv=1 mfmsr=1 glock=1 gt=0).
// The "// Hand-tuned override" first line makes tools/make_hotfunc_overrides.py
// skip this file; re-run that script to regenerate the literal reference copy.
//
// Behavior-identical to the generated copy: same guest RAM reads/writes (same
// addresses, same order), same global critical-region lock + lock-count side
// effects, same callee targets and guest return addresses, same r1/r12/r30/
// r31/r3 side effects at every call site and on return. cr0 is left in the
// stwcx-success state and cr6 in its last-compare state, matching the
// generated copy (both caller-saved, so unobservable across the call).
//
// Optimizations versus the generated copy (both safe, both documented):
//   1. The mfmsr/mtmsrd/mtmsrd MSR "interrupt-lock" sequence and its three
//      std::memory_order_seq_cst fences are dropped. No code in the recompiled
//      binary ever *reads* ctx.msr (every mtmsrd write is the self-preserving
//      (REG & 0x8020) | (ctx.msr & ~0x8020) form, and the mfmsr fast path reads
//      the lock count, not ctx.msr), and the mfmsr result (r9) only fed the
//      mtmsrd. The recursive mutex lock()/unlock() already supplies the
//      acquire/release ordering the fences provided. (Same reasoning as
//      ConstructRefCounted_8222CF18, note 1.)
//   2. The lwarx/stwcx retry is kept (structured as a do-loop rather than a
//      goto). It is NOT reduced to a plain RMW the way ConstructRefCounted
//      does: this decrements a *per-object* refcount at obj+12 that is also
//      written by ProcessGrowBitArray_821EC668, so the reserve/CAS is the
//      faithful, self-synchronizing form. Under the held mutex it succeeds on
//      the first pass, so the loop body runs exactly once in practice.
// (Volatile non-argument registers r8-r11 may end up holding different values
// than in the generated copy; under the PPC32 ABI they are caller-saved. r2,
// r13-r30, r1, and the argument registers are preserved exactly.)
//
// Guest logic (see per-line disassembly references):
//
//   // r3 = pointer-to-slot; the slot holds a refcounted object pointer whose
//   // refcount lives at object + 12.
//   void Release_RefCounted(u32* slot)
//   {
//     u32 obj = *slot;
//     if (obj) {
//       global_critical_region {            // mfmsr/mtmsrd + SDK global lock
//         for (;;) {                        // lwarx/stwcx retry
//           newref = *(obj + 12) - 1;
//           break if stwcx succeeded;
//         }
//       }
//       if (newref <= 0) {                  // last reference
//         u32 obj2 = *slot;                 // reload (safety vs. concurrent clear)
//         if (obj2) {
//           Dispose(obj2);                   // bl 0x82B39A30
//           Free_SizeBucketed_Thunk(obj2);   // bl 0x8221BE68 (r3 = its return)
//         }
//       }
//       *slot = 0;                          // stw r11(0),0(r30)
//     }
//     return;                               // r3 = slot (or Free's return)
//   }

#include "fable_2_pch.h"

extern "C" void Dispose_82B39A30(PPCContext& ctx, uint8_t* base);
extern "C" void Free_SizeBucketed_Thunk_8221BE68(PPCContext& ctx, uint8_t* base);

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

}  // namespace

extern "C" void Release_RefCounted_821C67D8(PPCContext& __restrict ctx, uint8_t* base) {
  REX_FUNC_PROLOGUE();

  // --- Guest prologue: mflr r12; stw r12,-8(r1); std r30,-24(r1); std r31,-16(r1);
  //     stwu r1,-112(r1) ---
  const u32 entry_sp = ctx.r1.u32;
  const u32 frame_sp = entry_sp - 112;
  ctx.r12.u64 = ctx.lr;                      // mflr r12
  gstore32(entry_sp - 8, base, ctx.r12.u32); // stw r12,-8(r1)
  gstore64(entry_sp - 24, base, ctx.r30.u64);// std r30,-24(r1)
  gstore64(entry_sp - 16, base, ctx.r31.u64);// std r31,-16(r1)
  gstore32(frame_sp, base, entry_sp);        // stwu r1,-112(r1)
  ctx.r1.u32 = frame_sp;

  // mr r30,r3: hold the slot in r30.
  ctx.r30.u64 = ctx.r3.u64;
  const u32 slot = ctx.r3.u32;

  const u32 obj = gload32(slot, base);       // lwz r11,0(r30)
  ctx.cr6.compare<u32>(obj, 0, ctx.xer);     // cmplwi cr6,r11,0
  if (obj != 0) {                            // beq cr6 -> epilogue
    // Decrement the object's refcount (obj + 12) under the global critical
    // region. See header note 1 (MSR/fences dropped) and note 2 (retry kept as
    // a do-loop).
    u32* refcount = reinterpret_cast<u32*>(gaddr(obj + 12, base)); // addi r11,r11,12
    do {
      hotfunc_gcr_mutex().lock(); ppc_global_lock_count_().fetch_add(1);
      ctx.reserved.u32 = *refcount;          // lwarx r10,0,r11
      ctx.r10.u64 = __builtin_bswap32(ctx.reserved.u32);
      ctx.r10.s64 -= 1;                      // addi r10,r10,-1
      ctx.cr0.lt = 0;                        // stwcx. r10,0,r11
      ctx.cr0.gt = 0;
      ctx.cr0.eq = __sync_bool_compare_and_swap(
          refcount, ctx.reserved.s32, __builtin_bswap32(ctx.r10.s32));
      ctx.cr0.so = ctx.xer.so;
      { auto old_ = ppc_global_lock_count_().fetch_sub(1); assert(old_ >= 1); }
      hotfunc_gcr_mutex().unlock();
    } while (!ctx.cr0.eq);                   // bne 0x821c6800

    // mr r11,r10; cmpwi cr6,r11,0: this is the last reference?
    ctx.r11.u64 = ctx.r10.u64;
    ctx.cr6.compare<int32_t>(ctx.r11.s32, 0, ctx.xer);
    if (!ctx.cr6.gt) {                       // bgt cr6 -> zero_out (skip dispose)
      const u32 obj2 = gload32(slot, base);  // lwz r31,0(r30) (reload)
      ctx.cr6.compare<u32>(obj2, 0, ctx.xer); // cmplwi cr6,r31,0
      ctx.r31.u64 = obj2;                    // r31 = obj2 (lwz zero-extends)
      if (obj2 != 0) {                       // beq cr6 -> zero_out (skip dispose)
        ctx.r3.u64 = obj2;                   // mr r3,r31
        ctx.lr = 0x821C683C;                 // bl 0x82b39a30
        Dispose_82B39A30(ctx, base);
        ctx.r3.u64 = obj2;                   // mr r3,r31
        ctx.lr = 0x821C6844;                 // bl 0x8221be68 (r3 = its return)
        Free_SizeBucketed_Thunk_8221BE68(ctx, base);
      }
    }
    // zero_out: stw r11(0),0(r30) - clear the slot.
    gstore32(slot, base, 0);
  }

  // --- Guest epilogue: addi r1,r1,112; lwz r12,-8(r1); mtlr r12;
  //     ld r30,-24(r1); ld r31,-16(r1); blr ---
  ctx.r1.u32 = entry_sp;
  ctx.r12.u64 = gload32(entry_sp - 8, base); // lwz r12,-8(r1)
  ctx.lr = ctx.r12.u64;                      // mtlr r12
  ctx.r30.u64 = gload64(entry_sp - 24, base);// ld r30,-24(r1)
  ctx.r31.u64 = gload64(entry_sp - 16, base);// ld r31,-16(r1)
}
