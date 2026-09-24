// Hand-tuned override of recompiled LazyInitSubsystemA_8217FD08
// (recompiler output: fable_2_recomp.216.cpp; generated transforms nv=1 mfmsr=1 glock=0 gt=0).
// The "// Hand-tuned override" first line makes tools/make_hotfunc_overrides.py
// skip this file; re-run that script to regenerate the literal reference copy.
//
// Behavior-identical to the generated copy: same guest RAM reads/writes, same
// call order/args/guest return addresses, same r1/r12/r31 side effects, same
// cr6 state, and the same subsystem pointer returned in r3 on every path.
// Volatile r8-r11 live in C++ locals (caller-saved per the PPC32 ABI).
//
// The recompiler emits recompiled guest functions as WEAK extern "C" aliases
// of __imp__LazyInitSubsystemA_8217FD08; fable_2_register.cpp registers the ALIAS in the
// indirect-dispatch table. This strong definition intercepts both direct bl
// calls and bctrl dispatch. __imp__LazyInitSubsystemA_8217FD08 remains the original.
//
// Guest logic - one-time subsystem initializer that returns the subsystem
// object pointer (0x834C1CE8) in r3 on every path:
//
//   u32* LazyInitSubsystemA()
//   {
//     u32& flag = *(u32*)0x834C3504;          // bit 0 = "already initialized"
//     if ((flag & 1) == 0) {
//       flag |= 1;                             // claim the slot (not atomic)
//       sub_825AB160(0x834C1CE8);              // construct the subsystem
//       ProcessAndAllocateAndProcess2_82CA3700(0x832A4280);  // register/allocate
//     }
//     return 0x834C1CE8;
//   }

#include "fable_2_pch.h"

extern "C" void ProcessAndAllocateAndProcess2_82CA3700(PPCContext& ctx, uint8_t* base);
extern "C" void sub_825AB160(PPCContext& ctx, uint8_t* base);

namespace {

// Non-volatile guest-RAM access. Same address math and byte order as the pch's
// REX_LOAD_*/REX_STORE_* macros, without `volatile` (safe: this function
// touches no MMIO, and both calls it makes are opaque to the optimizer, so no
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

}  // namespace

extern "C" void LazyInitSubsystemA_8217FD08(PPCContext& __restrict ctx, uint8_t* base) {
  REX_FUNC_PROLOGUE();

  // --- Guest prologue: mflr r12; stw r12,-8(r1); std r31,-16(r1); stwu r1,-96(r1) ---
  const u32 entry_sp = ctx.r1.u32;
  const u32 frame_sp = entry_sp - 96;
  ctx.r12.u64 = ctx.lr;              // mflr r12
  gstore32(entry_sp - 8, base, ctx.r12.u32);    // stw r12,-8(r1)
  gstore64(entry_sp - 16, base, ctx.r31.u64);   // std r31,-16(r1)
  gstore32(frame_sp, base, entry_sp);           // stwu r1,-96(r1)
  ctx.r1.u32 = frame_sp;

  // lis r10,-31924 / lis r9,-31924: 0x834C0000, sign-extended the way the
  // recompiler lowers lis (s64 = signext16(imm) << 16). Kept as the sign-
  // extended 64-bit value so r31/r3 match the recompiler bit-for-bit.
  const i64 kBase = static_cast<i64>(static_cast<i32>(0x834C0000)); // = -2092171264

  // One-time init guard on flag bit 0 at 0x834C3504.
  const u32 kFlag = 0x834C3504;               // r10.u32 + 13572
  const u32 flag = gload32(kFlag, base);       // lwz r11,13572(r10)
  ctx.cr6.compare<u32>(flag & 1, 0, ctx.xer);  // clrlwi r9,r11,31; cmplwi cr6,r9,0
  if ((flag & 1) == 0) {             // bne cr6 -> already initialized, skip
    gstore32(kFlag, base, flag | 1); // ori r11,r11,1; stw r11,13572(r10)

    const i64 subsystem = kBase + 7400;        // addi r31,r9,7400 -> 0x834C1CE8 (sign-extended)
    ctx.r31.s64 = subsystem;          // (callee-saved; the construct call sees it)
    ctx.r3.s64 = subsystem;           // mr r3,r31
    ctx.lr = 0x8217FD44;              // bl 0x825ab160
    sub_825AB160(ctx, base);
    // lis r8,-31958 (0x832A0000, sign-extended); addi r3,r8,17024 -> 0x832A4280
    ctx.r3.s64 = static_cast<i64>(static_cast<i32>(0x832A0000)) + 17024;
    ctx.lr = 0x8217FD50;              // bl 0x82ca3700
    ProcessAndAllocateAndProcess2_82CA3700(ctx, base);
  }

  // --- Return value on every path: r3 = 0x834C1CE8 (sign-extended) ---
  ctx.r3.s64 = kBase + 7400;          // (init path: mr r3,r31; else path: addi r3,r11,7400)

  // --- Guest epilogue: addi r1,r1,96; lwz r12,-8(r1); mtlr r12; ld r31,-16(r1); blr ---
  ctx.r1.u32 = entry_sp;
  ctx.r12.u64 = gload32(entry_sp - 8, base);    // lwz r12,-8(r1)
  ctx.lr = ctx.r12.u64;                 // mtlr r12
  ctx.r31.u64 = gload64(entry_sp - 16, base);   // ld r31,-16(r1)
}
