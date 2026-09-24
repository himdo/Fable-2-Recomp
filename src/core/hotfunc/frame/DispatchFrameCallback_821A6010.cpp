// Hand-tuned override of recompiled DispatchFrameCallback_821A6010
// (recompiler output: fable_2_recomp.25.cpp; generated transforms nv=1 mfmsr=1 glock=0 gt=0).
// The "// Hand-tuned override" first line makes tools/make_hotfunc_overrides.py
// skip this file; re-run that script to regenerate the literal reference copy.
//
// The recompiler emits recompiled guest functions as WEAK extern "C" aliases
// of __imp__DispatchFrameCallback_821A6010; fable_2_register.cpp registers the ALIAS in the
// indirect-dispatch table. This strong definition intercepts both direct bl
// calls and bctrl dispatch. __imp__DispatchFrameCallback_821A6010 remains the original.
//
// Behavior-identical to the generated copy: same guest RAM reads/writes, same
// indirect-dispatch targets and guest return addresses, same r1/r3/r5/r12/r29/r30/
// r31/f1/f31/ctr/cr6 side effects, same fpscr flush-mode disables, same trap calls.
// (Volatile non-argument registers r7-r11 may end up holding different values than in
// the generated copy; under the PPC32 ABI they are caller-saved, so well-formed guest
// code writes them before use - only r2, r13-r30, r1 and the argument registers are
// preserved across calls, and those are preserved exactly here.)
//
// Guest logic (see per-instruction disassembly references):
//
//   void DispatchFrameCallback(struct { ... }* table /* r3 */,
//                              u32 frame           /* r5 */,
//                              f64 delta           /* f1 */)
//   {
//     u32 slot = table->p128->p4;               // r31 (callee-saved)
//     if (slot->p8 != 0) {                      // "callback registered" gate
//       u32 hook = *(u32*)0x83496918;           // global hook pointer
//       if (hook != 0) {
//         u32 node  = slot->p4;
//         u32 entry = node->p4;
//         if (entry == node) { trap(22); trap(22); }  // corruption check
//         u32 target = entry->p8;
//         target->vtable[1](target);            // lr = 0x821A6078
//         *(u32*)0x83496918 reload; call it     // lr = 0x821A6084 (r3 = vcall result)
//       }
//       u32 node  = slot->p4;                   // reload (guest calls may mutate memory)
//       u32 entry = node->p4;
//       if (entry == node) { trap(22); trap(22); }  // corruption check
//       u32 target = entry->p8;
//       target->vtable[7](target, frame, delta); // lr = 0x821A60B8
//     }
//   }
//   (f31/f31-stack-slot and r29 are used to protect the delta and frame arguments
//    across the guest calls; r5/f1 are restored from them before the final vcall.)

#include "fable_2_pch.h"

extern "C" void __restgprlr_29(PPCContext& ctx, uint8_t* base);
extern "C" void __savegprlr_29(PPCContext& ctx, uint8_t* base);

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
HOTFUNC_ALWAYS_INLINE u32 gload32(u32 addr, uint8_t* base) {
  return __builtin_bswap32(*reinterpret_cast<const u32*>(gaddr(addr, base)));
}
HOTFUNC_ALWAYS_INLINE u64 gload64(u32 addr, uint8_t* base) {
  return __builtin_bswap64(*reinterpret_cast<const u64*>(gaddr(addr, base)));
}
HOTFUNC_ALWAYS_INLINE void gstore32(u32 addr, uint8_t* base, u32 v) {
  *reinterpret_cast<u32*>(gaddr(addr, base)) = __builtin_bswap32(v);
}
HOTFUNC_ALWAYS_INLINE void gstore64(u32 addr, uint8_t* base, u64 v) {
  *reinterpret_cast<u64*>(gaddr(addr, base)) = __builtin_bswap64(v);
}

// Guest virtual call (thiscall): r3 = this, ctr = *(this + vtbl_off), dispatch
// through the indirect-call table with the given guest return address in LR.
// Returns the callee's r3. (Mirrors: lwz r3,<this>; lwz r11,0(r3);
// lwz r10,<off>(r11); mtctr r10; bctrl.)
inline u32 vcall(PPCContext& ctx, uint8_t* base, u32 this_ptr, u32 vtbl_off, u32 guest_ret_addr) {
  ctx.r3.u64 = this_ptr;
  ctx.ctr.u64 = gload32(gload32(this_ptr, base) + vtbl_off, base);
  ctx.lr = guest_ret_addr;
  REX_CALL_INDIRECT_FUNC(ctx.ctr.u32);
  return ctx.r3.u32;
}

}  // namespace

extern "C" void DispatchFrameCallback_821A6010(PPCContext& __restrict ctx, uint8_t* base) {
  REX_FUNC_PROLOGUE();

  // --- Guest prologue: mflr r12; bl 0x82ca2bec (save r29-r31 + lr);
  //     stfd f31,-40(r1); stwu r1,-128(r1) ---
  const u32 entry_sp = ctx.r1.u32;
  const u32 frame_sp = entry_sp - 128;
  ctx.r12.u64 = ctx.lr;                 // mflr r12
  ctx.lr = 0x821A6018;                  // bl 0x82ca2bec
  __savegprlr_29(ctx, base);
  ctx.fpscr.disableFlushMode();         // stfd f31,-40(r1): protect callee-saved f31
  gstore64(entry_sp - 40, base, ctx.f31.u64);
  gstore32(frame_sp, base, entry_sp);   // stwu r1,-128(r1)
  ctx.r1.u32 = frame_sp;

  // Arguments: table in r3, frame counter in r5, delta time in f1.
  const u32 table = ctx.r3.u32;
  // lwz r11,128(r3); lwz r31,4(r11)
  const u32 slot = gload32(gload32(table + 128, base) + 4, base);
  ctx.fpscr.disableFlushMode();         // fmr f31,f1: protect the delta argument
  ctx.f31.f64 = ctx.f1.f64;
  ctx.r29.u64 = ctx.r5.u64;             // mr r29,r5: protect the frame argument
  ctx.r31.u64 = slot;                   // r31 is callee-saved; guest callees see it

  // "Callback registered" gate: lwz r10,8(r31); cmplwi cr6,r10,0; beq -> epilogue.
  const u32 gate = gload32(slot + 8, base);
  ctx.cr6.compare<u32>(gate, 0, ctx.xer);
  if (gate != 0) {
    // lis r30,-31927; global hook pointer lives at 0x83490000+0x6918 = 0x83496918.
    ctx.r30.s64 = -2092367872;          // lis r30,-31927 (0x83490000, as sign-extended by the recompiler)
    const u32 hook = gload32(0x83496918, base);  // lwz r11,26904(r30)
    ctx.cr6.compare<u32>(hook, 0, ctx.xer);      // cmplwi cr6,r11,0
    if (hook != 0) {
      const u32 node = gload32(slot + 4, base);  // lwz r11,4(r31)
      const u32 entry = gload32(node + 4, base); // lwz r10,4(r11)
      ctx.cr6.compare<u32>(entry, node, ctx.xer);  // cmplw cr6,r10,r11
      if (entry == node) {
        // Corruption check: `entry` must not be `node` itself. The recompiler
        // models twi as a non-fatal log and falls through, so we do the same
        // (twice, as disassembled).
        ppc_trap(ctx, base, 22);        // twi 31,r0,22
        ppc_trap(ctx, base, 22);        // twi 31,r0,22
      }
      // lwz r3,8(r10); target->vtable[1](target); guest return 0x821A6078.
      vcall(ctx, base, gload32(entry + 8, base), 4, 0x821A6078);
      // lwz r9,26904(r30); mtctr r9; bctrl: the hook is reloaded (the vcall may
      // have updated it) and called as a plain function pointer, with the vcall's
      // r3 return value left live in ctx.r3. Guest return 0x821A6084.
      const u32 hook_now = gload32(0x83496918, base);
      ctx.ctr.u64 = hook_now;
      ctx.lr = 0x821A6084;
      REX_CALL_INDIRECT_FUNC(ctx.ctr.u32);
    }

    // Block 2 (guest addr 0x821A6084): reload the chain (the guest calls above
    // may have mutated it), then dispatch target->vtable[7](target, frame, delta).
    const u32 node = gload32(slot + 4, base);   // lwz r11,4(r31)
    const u32 entry = gload32(node + 4, base);  // lwz r10,4(r11)
    ctx.cr6.compare<u32>(entry, node, ctx.xer); // cmplw cr6,r10,r11
    if (entry == node) {
      // Corruption check (twi 31,r0,22 x2, as recompiled).
      ppc_trap(ctx, base, 22);
      ppc_trap(ctx, base, 22);
    }
    const u32 target = gload32(entry + 8, base); // lwz r3,8(r10)
    ctx.r5.u64 = ctx.r29.u64;                    // mr r5,r29: restore frame argument
    ctx.fpscr.disableFlushMode();                // fmr f1,f31: restore delta argument
    ctx.f1.f64 = ctx.f31.f64;
    vcall(ctx, base, target, 28, 0x821A60B8);    // target->vtable[7](target); ret 0x821A60B8
  }

  // --- Guest epilogue: addi r1,r1,128; lfd f31,-40(r1); b 0x82ca2c3c
  //     (restore r29-r31 + lr from the entry SP, then blr) ---
  ctx.r1.u32 = entry_sp;                 // addi r1,r1,128
  ctx.fpscr.disableFlushMode();          // lfd f31,-40(r1)
  ctx.f31.u64 = gload64(entry_sp - 40, base);
  __restgprlr_29(ctx, base);             // b 0x82ca2c3c
}
