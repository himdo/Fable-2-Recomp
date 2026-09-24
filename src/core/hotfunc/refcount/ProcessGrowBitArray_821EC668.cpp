// Hand-tuned override of recompiled ProcessGrowBitArray_821EC668
// (recompiler output: fable_2_recomp.254.cpp; generated transforms nv=1 mfmsr=1 glock=1 gt=0).
// The "// Hand-tuned override" first line makes tools/make_hotfunc_overrides.py
// skip this file; re-run that script to regenerate the literal reference copy.
//
// Behavior-identical to the generated copy: same guest RAM reads/writes (same
// addresses, same order), same global critical-region lock + lock-count side
// effects, same callee targets and guest return addresses, same r1/r3/r12/
// r30/r31/cr0/cr6 side effects at every call site and on return.
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
//      goto). It is NOT reduced to a plain RMW the way ConstructRefCounted does:
//      this increments a *per-object* refcount at [r4]+12 that is also written
//      by Release_RefCounted_821C67D8, so the reserve/CAS is the faithful,
//      self-synchronizing form. Under the held mutex it succeeds on the first
//      pass, so the loop body runs exactly once in practice.
// (Volatile non-argument registers r8-r11 may end up holding different values
// than in the generated copy; under the PPC32 ABI they are caller-saved. r2,
// r13-r30, r1, and the argument registers are preserved exactly. cr0 is left
// in the stwcx-success state, matching the generated copy's post-loop state.)
//
// Guest logic (see per-line disassembly references):
//
//   // r3 = dst ref slot, r4 = src ref slot. "Shared reference copy": if dst
//   // is empty, take the src's object and atomically bump its refcount.
//   void ProcessGrowBitArray(u32* dst /* r3 */, u32* src /* r4 */)
//   {
//     if (*dst) {
//       AtomicIncrement5(dst);               // bl 0x8222E840 (r3 = dst)
//       if (*src) GrowBitArrayForString(*dst, **src);  // bl 0x82B3AB48
//       else      GrowBitArrayForString(*dst, 0x8209003F);
//       return dst;
//     }
//     if (src != dst && *src) {
//       *dst = *src;                         // stw
//       global_critical_region {             // mfmsr/mtmsrd + SDK global lock
//         for (;;) {                         // lwarx/stwcx retry
//           ++(**src + 3);                   //   r11 = *src + 12 (refcount)
//           break if stwcx succeeded;
//         }
//       }
//     }
//     return dst;                            // mr r3,r31
//   }

#include "fable_2_pch.h"

extern "C" void AtomicIncrement5_8222E840(PPCContext& ctx, uint8_t* base);
extern "C" void GrowBitArrayForString_82B3AB48(PPCContext& ctx, uint8_t* base);

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

extern "C" void ProcessGrowBitArray_821EC668(PPCContext& __restrict ctx, uint8_t* base) {
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

  // mr r31,r3; mr r30,r4: hold dst in r31, src in r30.
  ctx.r31.u64 = ctx.r3.u64;
  ctx.r30.u64 = ctx.r4.u64;
  const u32 dst = ctx.r3.u32;
  const u32 src = ctx.r4.u32;

  const u32 dst_ref = gload32(dst, base);    // lwz r11,0(r31)
  ctx.cr6.compare<u32>(dst_ref, 0, ctx.xer); // cmplwi cr6,r11,0
  if (dst_ref != 0) {
    // dst is already set: bump it, then grow from src.
    ctx.lr = 0x821EC694;                     // bl 0x8222e840 (r3 = dst, unchanged)
    AtomicIncrement5_8222E840(ctx, base);
    const u32 src_ref = gload32(src, base);  // lwz r11,0(r30)
    ctx.cr6.compare<u32>(src_ref, 0, ctx.xer); // cmplwi cr6,r11,0
    if (src_ref != 0) {
      // (guest 0x821EC6B4) grow from the source's pointee.
      ctx.r4.u64 = gload32(src_ref, base);   // lwz r4,0(r11)
      ctx.r3.u64 = gload32(dst, base);       // lwz r3,0(r31)
      ctx.lr = 0x821EC6C0;                   // bl 0x82b3ab48
      GrowBitArrayForString_82B3AB48(ctx, base);
    } else {
      // src is empty: grow from the fixed default key.
      ctx.r3.u64 = gload32(dst, base);       // lwz r3,0(r31)
      ctx.r4.s64 = static_cast<i64>(static_cast<i32>(0x8209003F)); // lis r11,-32247; addi r4,r11,63
      ctx.lr = 0x821EC6B0;                   // bl 0x82b3ab48
      GrowBitArrayForString_82B3AB48(ctx, base);
    }
  } else {
    // dst is empty: copy src's reference and bump its refcount. (guest 0x821EC6C4)
    ctx.cr6.compare<u32>(src, dst, ctx.xer); // cmplw cr6,r30,r31
    if (src != dst) {
      const u32 src_ref = gload32(src, base); // lwz r11,0(r30)
      ctx.cr6.compare<u32>(src_ref, 0, ctx.xer); // cmplwi cr6,r11,0
      if (src_ref != 0) {
        gstore32(dst, base, src_ref);        // stw r11,0(r31)
        // Atomically increment the copied object's refcount (*src + 12) under
        // the global critical region. See header note 1 (MSR/fences dropped)
        // and note 2 (retry kept as a do-loop).
        u32* refcount = reinterpret_cast<u32*>(gaddr(src_ref + 12, base)); // addi r11,r11,12
        do {
          hotfunc_gcr_mutex().lock(); ppc_global_lock_count_().fetch_add(1);
          ctx.reserved.u32 = *refcount;      // lwarx r10,0,r11
          ctx.r10.u64 = __builtin_bswap32(ctx.reserved.u32);
          ctx.r10.s64 += 1;                  // addi r10,r10,1
          ctx.cr0.lt = 0;                    // stwcx. r10,0,r11
          ctx.cr0.gt = 0;
          ctx.cr0.eq = __sync_bool_compare_and_swap(
              refcount, ctx.reserved.s32, __builtin_bswap32(ctx.r10.s32));
          ctx.cr0.so = ctx.xer.so;
          { auto old_ = ppc_global_lock_count_().fetch_sub(1); assert(old_ >= 1); }
          hotfunc_gcr_mutex().unlock();
        } while (!ctx.cr0.eq);               // bne 0x821ec6e0
      }
    }
  }

  // --- (guest 0x821EC6FC): return dst ---
  ctx.r3.u64 = dst;                          // mr r3,r31

  // --- Guest epilogue: addi r1,r1,112; lwz r12; mtlr; ld r30; ld r31; blr ---
  ctx.r1.u32 = entry_sp;
  ctx.r12.u64 = gload32(entry_sp - 8, base); // lwz r12,-8(r1)
  ctx.lr = ctx.r12.u64;                      // mtlr r12
  ctx.r30.u64 = gload64(entry_sp - 24, base);// ld r30,-24(r1)
  ctx.r31.u64 = gload64(entry_sp - 16, base);// ld r31,-16(r1)
}
