// Hand-tuned override of recompiled CallActivePlayerMethod_821A11E8
// (recompiler output: fable_2_recomp.15.cpp; generated transforms nv=1 mfmsr=1 glock=1).
// The "// Hand-tuned override" first line makes tools/make_hotfunc_overrides.py
// skip this file; re-run that script to regenerate the literal reference copy.
//
// The recompiler emits recompiled guest functions as WEAK extern "C" aliases
// of __imp__CallActivePlayerMethod_821A11E8; fable_2_register.cpp registers the
// ALIAS in the indirect-dispatch table. This strong definition intercepts both
// direct bl calls and bctrl dispatch. __imp__CallActivePlayerMethod_821A11E8
// remains the original.
//
// Behavior-identical to the generated copy: same guest RAM reads/writes, same
// indirect-dispatch targets and guest return addresses, same r3/r1/lr/r31/r12/
// ctr/cr6 side effects, same trap calls. (Volatile non-argument registers
// r7-r11 may end up holding different values than in the generated copy; under
// the PPC32 ABI they are caller-saved, so well-formed guest code writes them
// before use - only r2, r13-r30, r1 and the argument registers are preserved
// across calls, and those are preserved exactly here.)
//
// Guest logic (see per-line disassembly references):
//
//   void CallActivePlayerMethod(void* player)          // r3
//   {
//     if (g_flag_8331930E && player->b101 >= 1) {
//       const u32* node = ...global chain from *(u32*)0x83496920...
//       if (node->p4 == node) { /* trap 22 x2: corruption check */ }
//       if ((*node->p4->p8->vtable[1](*node->p4->p8)) != 7) {
//         player->p16->vtable[10](player->p16, 1);
//         player->b100 = 1;
//       }
//     }
//   }

#include "fable_2_pch.h"

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
HOTFUNC_ALWAYS_INLINE void gstore8(u32 addr, u8* base, u8 v) {
  *reinterpret_cast<u8*>(gaddr(addr, base)) = v;
}
HOTFUNC_ALWAYS_INLINE void gstore32(u32 addr, u8* base, u32 v) {
  *reinterpret_cast<u32*>(gaddr(addr, base)) = __builtin_bswap32(v);
}
HOTFUNC_ALWAYS_INLINE void gstore64(u32 addr, u8* base, u64 v) {
  *reinterpret_cast<u64*>(gaddr(addr, base)) = __builtin_bswap64(v);
}

// Guest virtual call (thiscall): r3 = this, ctr = *(this + vtbl_off), dispatch
// through the indirect-call table with the given guest return address in LR.
// Returns the callee's r3. (Mirrors: lwz r11,0(r3); lwz r10,<off>(r11);
// mtctr r10; bctrl.)
inline u32 vcall(PPCContext& ctx, uint8_t* base, u32 this_ptr, u32 vtbl_off, u32 guest_ret_addr) {
  ctx.r3.u64 = this_ptr;
  ctx.ctr.u64 = gload32(gload32(this_ptr, base) + vtbl_off, base);
  ctx.lr = guest_ret_addr;
  REX_CALL_INDIRECT_FUNC(ctx.ctr.u32);
  return ctx.r3.u32;
}

}  // namespace

extern "C" void CallActivePlayerMethod_821A11E8(PPCContext& __restrict ctx, uint8_t* base) {
  REX_FUNC_PROLOGUE();

  // --- Guest prologue: mflr r12; stw r12,-8(r1); std r31,-16(r1); stwu r1,-96(r1) ---
  const u32 entry_sp = ctx.r1.u32;
  const u32 frame_sp = entry_sp - 96;
  ctx.r12.u64 = ctx.lr;
  gstore32(entry_sp - 8, base, ctx.r12.u32);
  gstore64(entry_sp - 16, base, ctx.r31.u64);
  gstore32(frame_sp, base, entry_sp);
  ctx.r1.u32 = frame_sp;

  // mr r31,r3: hold the player object in r31 as a this pointer. (Kept in ctx so
  // any guest callee sees the same r31 real hardware would.)
  ctx.r31.u64 = ctx.r3.u64;
  const u32 player = ctx.r3.u32;

  // Gate 1: global enable flag (byte at 0x8331930E) must be non-zero.
  const u8 enabled = gload8(0x8331930E, base);
  ctx.cr6.compare<u32>(enabled, 0, ctx.xer);  // cmplwi cr6,r10,0
  if (enabled != 0) {
    // Gate 2: player->b101 must be >= 1 (cmplwi cr6,r11,1; blt).
    const u8 state = gload8(player + 101, base);
    ctx.cr6.compare<u32>(state, 1, ctx.xer);
    if (state >= 1) {
      // Walk the global context chain rooted at *(u32*)0x83496920.
      const u32 root = gload32(0x83496920, base);
      const u32 link_a = gload32(root + 12, base);
      const u32 link_b = gload32(link_a + 128, base);
      const u32 link_c = gload32(link_b + 4, base);
      const u32 node = gload32(link_c + 4, base);
      const u32 next = gload32(node + 4, base);
      // Corruption check (cmplw cr6,r11,r10; twi 31,r0,22 x2): `next` must not
      // be `node` itself. The recompiler models twi as a non-fatal log and
      // falls through, so we do the same (twice, as disassembled).
      ctx.cr6.compare<u32>(next, node, ctx.xer);
      if (next == node) {
        ppc_trap(ctx, base, 22);
        ppc_trap(ctx, base, 22);
      }
      // Dispatch: target = next->p8; call target->vtable[1](target).
      const u32 result = vcall(ctx, base, gload32(next + 8, base), 4, 0x821A125C);
      // cmpwi cr6,r3,7: the follow-up only runs when the method did not report 7.
      ctx.cr6.compare<i32>(static_cast<i32>(result), 7, ctx.xer);
      if (result != 7) {
        // subject = player->p16; call subject->vtable[10](subject, 1), then
        // mark player->b100 = 1 (li r9,1; stb r9,100(r31)).
        const u32 subject = gload32(player + 16, base);
        ctx.r4.s64 = 1;
        vcall(ctx, base, subject, 40, 0x821A127C);
        gstore8(player + 100, base, 1);
      }
    }
  }

  // --- Guest epilogue: addi r1,r1,96; lwz r12,-8(r1); mtlr r12; ld r31,-16(r1); blr ---
  ctx.r1.u32 = entry_sp;
  ctx.r12.u64 = gload32(entry_sp - 8, base);
  ctx.lr = ctx.r12.u64;
  ctx.r31.u64 = gload64(entry_sp - 16, base);
}
