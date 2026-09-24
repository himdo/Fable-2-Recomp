// Hand-tuned override of recompiled QueryActivePlayerMethod_821EC8A0
// (recompiler output: fable_2_recomp.258.cpp; generated transforms nv=1 mfmsr=1 glock=0 gt=0).
// The "// Hand-tuned override" first line makes tools/make_hotfunc_overrides.py
// skip this file; re-run that script to regenerate the literal reference copy.
//
// Behavior-identical to the generated copy: same guest RAM reads (same
// addresses, same order), same indirect-dispatch target and guest return
// address, same r1/r3/r12/ctr/cr6 side effects, same trap calls. Volatile
// r8-r11 live in C++ locals (caller-saved per the PPC32 ABI); only r2, r13-
// r30, r1, and the argument registers are preserved across calls, and none of
// those are touched here.
//
// IMPORTANT - the final rlwinm is preserved EXACTLY, not "fixed". The guest
// does `rlwinm r3,r8,27,31,31` (true PPC: bit 4 of r8, r8 = cntlzw(r3-1)), but
// the recompiler lowers it to a 64-bit rotate that yields bit 5 of r8. For
// r8 in 16..32 the two differ (e.g. r8=16: true PPC=1, recompiler=0), so we
// must keep the recompiler's formula to match the generated copy:
//     r3 = rotateleft64(r8 | (r8 << 32), 27) & 1   // == bit 5 of r8
//
// The recompiler emits recompiled guest functions as WEAK extern "C" aliases
// of __imp__QueryActivePlayerMethod_821EC8A0; fable_2_register.cpp registers the ALIAS in the
// indirect-dispatch table. This strong definition intercepts both direct bl
// calls and bctrl dispatch. __imp__QueryActivePlayerMethod_821EC8A0 remains the original.
//
// Guest logic (see per-line disassembly references):
//
//   u32 QueryActivePlayerMethod()
//   {
//     u32 root = *(u32*)0x83496920;
//     u8 flag  = root ? *(u8*)0x834968C5 : 0;
//     if (flag == 0) return 0;
//     u32 node = root->p12->p128->p4->p4;      // global context chain
//     u32 next = node->p4;
//     if (next == node) { trap(22); trap(22); } // corruption check
//     u32 result = next->p8->vtable[1](next->p8);  // thiscall
//     u32 r8 = (result - 1) ? __builtin_clz(result - 1) : 32;
//     return rotateleft64(r8 | (r8 << 32), 27) & 1;  // bit 5 of r8 (recompiler quirk)
//   }

#include "fable_2_pch.h"

namespace {

// Non-volatile guest-RAM access. Same address math and byte order as the pch's
// REX_LOAD_*/REX_STORE_* macros, without `volatile` (safe: this function
// touches no MMIO, and the one indirect call it makes is opaque to the
// optimizer, so no access can be CSE'd across it).
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
HOTFUNC_ALWAYS_INLINE void gstore32(u32 addr, u8* base, u32 v) {
  *reinterpret_cast<u32*>(gaddr(addr, base)) = __builtin_bswap32(v);
}

// Guest virtual call (thiscall): r3 = this, ctr = *(this + vtbl_off) through
// the vtable, dispatch through the indirect-call table with the given guest
// return address in LR. Returns the callee's r3. (Mirrors: lwz r11,0(r3);
// lwz r10,<off>(r11); mtctr r10; bctrl.)
inline u32 vcall(PPCContext& ctx, uint8_t* base, u32 this_ptr, u32 vtbl_off, u32 guest_ret_addr) {
  ctx.r3.u64 = this_ptr;
  ctx.ctr.u64 = gload32(gload32(this_ptr, base) + vtbl_off, base);
  ctx.lr = guest_ret_addr;
  REX_CALL_INDIRECT_FUNC(ctx.ctr.u32);
  return ctx.r3.u32;
}

}  // namespace

extern "C" void QueryActivePlayerMethod_821EC8A0(PPCContext& __restrict ctx, uint8_t* base) {
  REX_FUNC_PROLOGUE();

  // --- Guest prologue: mflr r12; stw r12,-8(r1); stwu r1,-96(r1) ---
  const u32 entry_sp = ctx.r1.u32;
  const u32 frame_sp = entry_sp - 96;
  ctx.r12.u64 = ctx.lr;                        // mflr r12
  gstore32(entry_sp - 8, base, ctx.r12.u32);   // stw r12,-8(r1)
  gstore32(frame_sp, base, entry_sp);          // stwu r1,-96(r1)
  ctx.r1.u32 = frame_sp;

  // --- Resolve the context root and the enable flag byte ---
  const u32 root = gload32(0x83496920, base);  // lis r11,-31927; lwz r10,26912(r11)
  ctx.cr6.compare<u32>(root, 0, ctx.xer);      // cmplwi cr6,r10,0
  u32 flag = 0;
  if (root != 0) {                             // beq -> flag stays 0
    flag = gload8(0x834968C5, base);           // lbz r11,26821(r11)
  }
  flag &= 0xFF;                                // clrlwi r11,r11,24
  ctx.cr6.compare<u32>(flag, 0, ctx.xer);      // cmplwi cr6,r11,0

  if (flag != 0) {                             // bne -> main body
    // Walk the global context chain rooted at *(u32*)0x83496920.
    const u32 link_a = gload32(root + 12, base);     // lwz r11,12(r10)
    const u32 link_b = gload32(link_a + 128, base);  // lwz r10,128(r11)
    const u32 link_c = gload32(link_b + 4, base);    // lwz r9,4(r10)
    const u32 node = gload32(link_c + 4, base);      // lwz r8,4(r9)
    const u32 next = gload32(node + 4, base);        // lwz r11,4(r8)
    // Corruption check (cmplw cr6,r11,r10; twi 31,r0,22 x2): `next` must not
    // be `node` itself. The recompiler models twi as a non-fatal log and falls
    // through, so we do the same (twice, as disassembled).
    ctx.cr6.compare<u32>(next, node, ctx.xer);
    if (next == node) {
      ppc_trap(ctx, base, 22);
      ppc_trap(ctx, base, 22);
    }
    // Dispatch: target = next->p8; call target->vtable[1](target).
    const u32 result = vcall(ctx, base, gload32(next + 8, base), 4, 0x821EC928);
    // Tail: r8 = cntlzw(result - 1); r3 = rlwinm(r8,27,31,31). Preserved
    // EXACTLY as the recompiler lowers it (bit 5 of r8, not bit 4 - see
    // header note).
    const u32 r9 = result - 1;                                // addi r9,r3,-1
    const u64 r8 = (r9 == 0) ? 32 : __builtin_clz(r9);        // cntlzw r8,r9
    ctx.r3.u64 = __builtin_rotateleft64(r8 | (r8 << 32), 27) & 0x1;  // rlwinm r3,r8,27,31,31
  } else {
    ctx.r3.s64 = 0;                                 // li r3,0
  }

  // --- Guest epilogue: addi r1,r1,96; lwz r12,-8(r1); mtlr r12; blr ---
  ctx.r1.u32 = entry_sp;
  ctx.r12.u64 = gload32(entry_sp - 8, base);        // lwz r12,-8(r1)
  ctx.lr = ctx.r12.u64;                             // mtlr r12
}
