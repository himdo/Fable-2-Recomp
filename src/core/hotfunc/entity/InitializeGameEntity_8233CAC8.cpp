// Hand-tuned override of recompiled InitializeGameEntity_8233CAC8
// (recompiler output: fable_2_recomp.131.cpp; generated transforms nv=1 mfmsr=1 glock=0 gt=0).
// The "// Hand-tuned override" first line makes tools/make_hotfunc_overrides.py
// skip this file; re-run that script to regenerate the literal reference copy.
//
// Behavior-identical to the generated copy: same guest RAM reads/writes (both
// reads of [0x834A5B6C] stay separate - the setup calls may mutate it), same
// call order/args/guest return addresses, same r1/r3-r5/r12/r29/r30/r31/ctr-
// free cr0/cr6 side effects, same fpscr usage (none here), same trap call.
// Volatile r7-r11 live in C++ locals (caller-saved per the PPC32 ABI).
//
// The recompiler emits recompiled guest functions as WEAK extern "C" aliases
// of __imp__InitializeGameEntity_8233CAC8; fable_2_register.cpp registers the ALIAS in the
// indirect-dispatch table. This strong definition intercepts both direct bl
// calls and bctrl dispatch. __imp__InitializeGameEntity_8233CAC8 remains the original.
//
// Guest logic (see per-instruction disassembly references):
//
//   void InitializeGameEntity(StructA* a /* r3 */, RefCounted* ref /* r4 */)
//   {
//     x = f_82172EE8(0x820A6194, *(u32*)0x834A5B6C);
//     y = f_8217E3F8(x.r3, x.r4);
//     f_822EC348(y.r3, 1);
//     if (a->p140->p184 != 0) {
//       u32 g = *(u32*)0x834A5B6C;            // re-read (may have changed)
//       f_822F9EC0(a->p140->p184, 0, g ? *g : 0x8209003F);
//     }
//     f_829F7E18(*(u32*)0x83496BCC);
//     u32* state = a->p140;         // re-read (calls above may have changed it)
//     a->b256 = 1;
//     if (state->p88 != 0 && (i32)(state->p92 - state->p88) / 24 != 0) {
//       if (state->p88 > state->p92) trap(22); // bounds check
//       f_8230FEB0(state, (u64)(state + 84) << 32 | state->p88);
//     }
//     f_8233D250(a);
//     if (ref->p0 && ref->p0->p4) {
//       u32 root = *(u32*)0x83496920;         // shared global context chain
//       u32 target = root->p8->p36;
//       CopyAssign_RefCounted2(target + 16, ref);
//       target->p20 = 0;
//     }
//     DestructRefCounted(ref);
//   }

#include "fable_2_pch.h"

extern "C" void CopyAssign_RefCounted2_82265160(PPCContext& ctx, uint8_t* base);
extern "C" void DestructRefCounted_82214F08(PPCContext& ctx, uint8_t* base);
extern "C" void ProcessAndProcessAndProcess1309_822EC348(PPCContext& ctx, uint8_t* base);
extern "C" void ProcessAndProcessAndProcess2254_82172EE8(PPCContext& ctx, uint8_t* base);
extern "C" void ProcessAndProcessAndProcess938_8217E3F8(PPCContext& ctx, uint8_t* base);
extern "C" void __restgprlr_29(PPCContext& ctx, uint8_t* base);
extern "C" void __savegprlr_29(PPCContext& ctx, uint8_t* base);
extern "C" void sub_822F9EC0(PPCContext& ctx, uint8_t* base);
extern "C" void sub_8230FEB0(PPCContext& ctx, uint8_t* base);
extern "C" void sub_8233D250(PPCContext& ctx, uint8_t* base);
extern "C" void sub_829F7E18(PPCContext& ctx, uint8_t* base);

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
HOTFUNC_ALWAYS_INLINE void gstore8(u32 addr, u8* base, u8 v) {
  *reinterpret_cast<u8*>(gaddr(addr, base)) = v;
}
HOTFUNC_ALWAYS_INLINE void gstore32(u32 addr, u8* base, u32 v) {
  *reinterpret_cast<u32*>(gaddr(addr, base)) = __builtin_bswap32(v);
}

}  // namespace

extern "C" void InitializeGameEntity_8233CAC8(PPCContext& __restrict ctx, uint8_t* base) {
  REX_FUNC_PROLOGUE();

  // --- Guest prologue: mflr r12; bl 0x82ca2bec (save r29-r31 + lr); stwu r1,-128(r1) ---
  const u32 entry_sp = ctx.r1.u32;
  const u32 frame_sp = entry_sp - 128;
  ctx.r12.u64 = ctx.lr;            // mflr r12
  ctx.lr = 0x8233CAD0;             // bl 0x82ca2bec
  __savegprlr_29(ctx, base);
  gstore32(frame_sp, base, entry_sp);  // stwu r1,-128(r1)
  ctx.r1.u32 = frame_sp;

  // Argument registers for the whole body: r30 = a, r29 = ref (callee-saved,
  // so every guest callee below sees these).
  const u32 a = ctx.r3.u32;
  const u32 ref = ctx.r4.u32;
  ctx.r30.u64 = a;                 // mr r30,r3
  ctx.r29.u64 = ref;               // mr r29,r4
  ctx.r31.u64 = 0x834A0000;        // lis r31,-31926 (global base for [0x834A5B6C])

  // --- Setup calls: values flow through r3/r4 return registers ---
  ctx.r3.s64 = 0x820A6194;         // lis r11,-32246; addi r3,r11,24980
  ctx.r4.u64 = gload32(0x834A5B6C, base);  // lwz r4,23404(r31)  (first read)
  ctx.lr = 0x8233CAF0;             // bl 0x82172ee8
  ProcessAndProcessAndProcess2254_82172EE8(ctx, base);
  ctx.lr = 0x8233CAF4;             // bl 0x8217e3f8 (args = previous returns)
  ProcessAndProcessAndProcess938_8217E3F8(ctx, base);
  ctx.r4.s64 = 1;                  // li r4,1
  ctx.lr = 0x8233CAFC;             // bl 0x822ec348
  ProcessAndProcessAndProcess1309_822EC348(ctx, base);

  // --- a->p140->p184 gate ---
  const u32 state = gload32(a + 140, base);      // lwz r10,140(r30)
  const u32 p184 = gload32(state + 184, base);   // lwz r3,184(r10)
  ctx.r3.u64 = p184;
  ctx.cr6.compare<i32>(static_cast<i32>(p184), 0, ctx.xer);  // cmpwi cr6,r3,0
  if (p184 != 0) {
    // bne cr6 -> re-read the global (the setup calls above may have mutated it).
    const u32 g = gload32(0x834A5B6C, base);     // lwz r11,23404(r31)  (second read)
    ctx.cr6.compare<u32>(g, 0, ctx.xer);         // cmplwi cr6,r11,0
    const u32 r5 = (g != 0)
        ? gload32(g, base)                        // lwz r5,0(r11)
        : 0x8209003F;                             // lis r11,-32247; addi r5,r11,63
    ctx.r4.s64 = 0;                               // li r4,0
    ctx.r5.u64 = r5;
    ctx.lr = 0x8233CB30;                          // bl 0x822f9ec0
    sub_822F9EC0(ctx, base);
  }

  // --- (guest 0x8233CB30) ---
  ctx.r3.u64 = gload32(0x83496BCC, base);         // lis r11,-31927; lwz r3,27596(r11)
  ctx.lr = 0x8233CB3C;                            // bl 0x829f7e18
  sub_829F7E18(ctx, base);

  // --- Bounds-checked slot dispatch on state2 = a->p140 ---
  // (a->p140 is REloaded here: the calls above may have changed it - the
  //  guest deliberately re-reads it.)
  const u32 state2 = gload32(a + 140, base);      // lwz r3,140(r30)
  ctx.r3.u64 = state2;
  gstore8(a + 256, base, 1);                      // li r10,1; stb r10,256(r30)
  const u32 start = gload32(state2 + 88, base);   // lwz r10,88(r3)
  ctx.cr6.compare<u32>(start, 0, ctx.xer);        // cmplwi cr6,r10,0
  if (start != 0) {
    const u32 end = gload32(state2 + 92, base);   // lwz r9,8(r11) (r11 = r3+84)
    // subf r7,r10,r9; divw. r6,r7,24: count = (i32)(end - start) / 24 (24-byte
    // stride; the divisor is a constant, so the overflow guard can never fire).
    const i32 count = static_cast<i32>(static_cast<u32>(end) - start) / 24;
    ctx.cr0.compare<i32>(count, 0, ctx.xer);      // divw. cr0 = (count vs 0)
    if (count != 0) {
      const u32 start2 = gload32(state2 + 88, base);  // lwz r10,4(r11) (reloaded)
      ctx.cr6.compare<u32>(start2, end, ctx.xer);    // cmplw cr6,r10,r9 (rotlwi r9,r9,0 is a no-op)
      if (start2 > end) {
        ppc_trap(ctx, base, 22);                  // twi 31,r0,22
      }
      // stw r11,80(r1); stw r10,84(r1); ld r4,80(r1): big-endian pack of the
      // slot address (state2+84, high 32) and start2 (low 32) into r4.
      ctx.r4.u64 = (static_cast<u64>(static_cast<u32>(state2) + 84) << 32) | start2;
      ctx.lr = 0x8233CB90;                        // bl 0x8230feb0
      sub_8230FEB0(ctx, base);
    }
  }

  // --- (guest 0x8233CB90) ---
  ctx.r3.u64 = a;                                 // mr r3,r30
  ctx.lr = 0x8233CB98;                            // bl 0x8233d250
  sub_8233D250(ctx, base);

  // --- Ref-counted copy from the shared global context chain ---
  const u32 p0 = gload32(ref, base);              // lwz r11,0(r29)
  ctx.cr6.compare<u32>(p0, 0, ctx.xer);           // cmplwi cr6,r11,0
  if (p0 != 0) {
    const u32 p4 = gload32(p0 + 4, base);         // lwz r11,4(r11)
    ctx.cr6.compare<i32>(static_cast<i32>(p4), 0, ctx.xer);  // cmpwi cr6,r11,0
    if (p4 != 0) {
      const u32 root = gload32(0x83496920, base); // lis r11,-31927; lwz r11,26912(r11)
      const u32 mid = gload32(root + 8, base);  // lwz r10,8(r11)
      const u32 target = gload32(mid + 36, base);  // lwz r31,36(r10)
      ctx.r31.u64 = target;                       // (callee-saved; the store below uses it)
      ctx.r3.s64 = ctx.r31.s64 + 16;              // addi r3,r31,16
      ctx.r4.u64 = ref;                           // mr r4,r29
      ctx.lr = 0x8233CBCC;                        // bl 0x82265160
      CopyAssign_RefCounted2_82265160(ctx, base);
      gstore32(target + 20, base, 0);             // li r9,0; stw r9,20(r31)
    }
  }

  // --- (guest 0x8233CBD4): destruct the reference argument ---
  ctx.r3.u64 = ref;                               // mr r3,r29
  ctx.lr = 0x8233CBDC;                            // bl 0x82214f08
  DestructRefCounted_82214F08(ctx, base);

  // --- Guest epilogue: addi r1,r1,128; b 0x82ca2c3c (restore r29-r31 + lr; blr) ---
  ctx.r1.u32 = entry_sp;
  __restgprlr_29(ctx, base);
}
