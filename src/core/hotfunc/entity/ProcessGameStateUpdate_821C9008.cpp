// Hand-tuned override of recompiled ProcessGameStateUpdate_821C9008
// (recompiler output: fable_2_recomp.140.cpp; generated transforms nv=1 mfmsr=1 glock=0 gt=0).
// The "// Hand-tuned override" first line makes tools/make_hotfunc_overrides.py
// skip this file; re-run that script to regenerate the literal reference copy.
//
// Behavior-identical to the generated copy: same guest RAM reads/writes (same
// addresses, same order), same calls in the same order with the same
// register state (r1-r12, r30-r31, r3-r10, f1) and the same guest return
// addresses. Every register the generated code writes is written to ctx at
// the same point with the same value, including the volatile r11 (a callee
// may read it before overwriting it, so it is set exactly as generated).
// Registers the guest never writes keep whatever the previous callee left
// them - both versions do that for free, since neither touches those ctx
// fields in between.
//
// The recompiler emits recompiled guest functions as WEAK extern "C" aliases
// of __imp__ProcessGameStateUpdate_821C9008; fable_2_register.cpp registers the ALIAS in the
// indirect-dispatch table. This strong definition intercepts both direct bl
// calls and bctrl dispatch. __imp__ProcessGameStateUpdate_821C9008 remains the original.
//
// Guest logic (see per-line disassembly references below): a straight-line
// "run one state-update tick for `self`" routine - 11 static calls plus two
// vtable dispatches through a global singleton at 0x8349E6EC, no branches:
//
//   u32 ProcessGameStateUpdate(u32 self)
//   {
//     sub_8218CA10(self);                                 // (1)
//     u32 p = self->p88->p4;
//     sub_82231A70(*p);                  // r10 = p
//     p = self->p88->p4;
//     sub_828581B0(p + 180);             // r9  = self->p88
//     ProcessAndProcessAndProcess1290(self->p140);        // (2)
//     u32 g = *(u32*)0x8349E6EC;  u32 vt = *g;
//     vt[6](g);                               // (3) vtable call, this = g
//     ProcessAndProcessAndProcess1292(self->p124, self->p156,
//         0x820A0000, 1, f32[0x820994C0]);    // (4) f1 float arg
//     u32 q = self->p128;  sub_82176088(q->p4);
//     u32 n = self->u252;  self->b260 = 1;  self->u252 = n + 1;
//     sub_824F9BE0(self->p92);          // r9 = n + 1, r10 = 1
//     sub_8218A3D8(self->p156);
//     u32 a = self->p164;  sub_82184580(a->p4);  // r8 = a
//     u32 b = self->p168;  sub_8227BE08(b->p4);  // r7 = b
//     g = *(u32*)0x8349E6EC;  vt = *g;
//     vt[7](g);                               // (5) vtable call, this = g
//     return sub_82195930(self->p172, 0);
//   }

#include "fable_2_pch.h"

namespace {

// Non-volatile guest-RAM access. Same address math and byte order as the pch's
// REX_LOAD_*/REX_STORE_* macros, without `volatile` (safe: this function
// touches no MMIO, and every guest call it makes is opaque to the optimizer,
// so no access can be CSE'd across a call).
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
HOTFUNC_ALWAYS_INLINE void gstore8(u32 addr, u8* base, u8 v) {
  *reinterpret_cast<u8*>(gaddr(addr, base)) = v;
}

}  // namespace

extern "C" void ProcessAndProcessAndProcess1290_821961A0(PPCContext& ctx, uint8_t* base);
extern "C" void ProcessAndProcessAndProcess1292_821CCB20(PPCContext& ctx, uint8_t* base);
extern "C" void sub_82176088(PPCContext& ctx, uint8_t* base);
extern "C" void sub_82184580(PPCContext& ctx, uint8_t* base);
extern "C" void sub_8218A3D8(PPCContext& ctx, uint8_t* base);
extern "C" void sub_8218CA10(PPCContext& ctx, uint8_t* base);
extern "C" void sub_82195930(PPCContext& ctx, uint8_t* base);
extern "C" void sub_82231A70(PPCContext& ctx, uint8_t* base);
extern "C" void sub_8227BE08(PPCContext& ctx, uint8_t* base);
extern "C" void sub_824F9BE0(PPCContext& ctx, uint8_t* base);
extern "C" void sub_828581B0(PPCContext& ctx, uint8_t* base);

extern "C" void ProcessGameStateUpdate_821C9008(PPCContext& __restrict ctx, uint8_t* base) {
  REX_FUNC_PROLOGUE();

  // --- Guest prologue: mflr r12; stw r12,-8(r1); std r30,-24(r1); std
  //     r31,-16(r1); stwu r1,-112(r1); mr r31,r3 ---
  const u32 entry_sp = ctx.r1.u32;
  const u32 frame_sp = entry_sp - 112;
  ctx.r12.u64 = ctx.lr;                         // mflr r12
  gstore32(entry_sp - 8, base, ctx.r12.u32);    // stw r12,-8(r1)
  gstore64(entry_sp - 24, base, ctx.r30.u64);   // std r30,-24(r1)
  gstore64(entry_sp - 16, base, ctx.r31.u64);   // std r31,-16(r1)
  gstore32(frame_sp, base, entry_sp);           // stwu r1,-112(r1)
  ctx.r1.u32 = frame_sp;
  ctx.r31.u64 = ctx.r3.u64;                     // mr r31,r3
  const u32 self = ctx.r3.u32;

  // (1) First update step: r3 = self (entry arg, untouched); r4-r10 and
  //     f1-f13 keep their entry values.
  ctx.lr = 0x821C9024;
  sub_8218CA10(ctx, base);

  // (2) Manager chain: p = self->p88->p4; call with *p (r10 = p).
  const u32 m1 = gload32(self + 88, base);      // lwz r11,88(r31)
  const u32 p1 = gload32(m1 + 4, base);         // lwz r10,4(r11)
  ctx.r11.u64 = m1;
  ctx.r10.u64 = p1;
  ctx.r3.u64 = gload32(p1, base);               // lwz r3,0(r10)
  ctx.lr = 0x821C9034;
  sub_82231A70(ctx, base);

  // (3) Re-read the chain (r11 may have been clobbered by the callee) and
  //     call with p + 180 (r9 = self->p88).
  const u32 m2 = gload32(self + 88, base);      // lwz r9,88(r31)
  const u32 p2 = gload32(m2 + 4, base);         // lwz r11,4(r9)
  ctx.r9.u64 = m2;
  ctx.r11.u64 = p2;
  ctx.r3.u64 = (u64)p2 + 180;                   // addi r3,r11,180
  ctx.lr = 0x821C9044;
  sub_828581B0(ctx, base);

  // (4)
  ctx.r3.u64 = gload32(self + 140, base);       // lwz r3,140(r31)
  ctx.lr = 0x821C904C;
  ProcessAndProcessAndProcess1290_821961A0(ctx, base);

  // (5) Vtable dispatch #1 through the global singleton.
  //     lis r30,-31926 -> r30 = 0x834A0000 (sign-extended);
  //     lwz r3,-6420(r30) -> 0x834A0000 - 6420 = 0x8349E6EC. The address is
  //     computed at runtime from r30 exactly as the generated code does.
  ctx.r30.s64 = -2092302336;                    // lis r30,-31926
  const u32 singleton = ctx.r30.u32 - 6420;     // lwz r3,-6420(r30) -> 0x8349E6EC
  const u32 g1 = gload32(singleton, base);
  const u32 vt1 = gload32(g1, base);            // lwz r8,0(r3)
  ctx.r3.u64 = g1;
  ctx.r8.u64 = vt1;
  const u32 target1 = gload32(vt1 + 24, base);  // lwz r7,24(r8)
  ctx.r7.u64 = target1;
  ctx.ctr.u64 = target1;                        // mtctr r7
  ctx.lr = 0x821C9064;
  REX_CALL_INDIRECT_FUNC(target1);

  // (6) lis r5,-32246 -> r5 = 0x820A0000 (sign-extended);
  //     lfs f1,-27456(r5) -> float constant at 0x820A0000 - 27456 = 0x820994C0.
  //     The address is computed at runtime from r5 exactly as the generated code does.
  ctx.r5.s64 = -2113273856;                     // lis r5,-32246
  ctx.r6.u64 = 1;                               // li r6,1
  const u32 a4 = gload32(self + 156, base);     // lwz r4,156(r31)
  const u32 a3 = gload32(self + 124, base);     // lwz r3,124(r31)
  ctx.fpscr.disableFlushMode();                 // lfs f1: match generated fpscr side effect
  PPCRegister ftemp{};
  ftemp.u32 = gload32(ctx.r5.u32 - 27456, base); // lfs f1,-27456(r5) -> 0x820994C0
  ctx.f1.f64 = (double)ftemp.f32;
  ctx.r4.u64 = a4;
  ctx.r3.u64 = a3;
  ctx.lr = 0x821C907C;
  ProcessAndProcessAndProcess1292_821CCB20(ctx, base);

  // (7)
  const u32 q = gload32(self + 128, base);      // lwz r4,128(r31)
  ctx.r4.u64 = q;
  ctx.r3.u64 = gload32(q + 4, base);            // lwz r3,4(r4)
  ctx.lr = 0x821C9088;
  sub_82176088(ctx, base);

  // (8) Bump the 32-bit counter at self+252 and set the byte at self+260
  //     to 1, then call (r9 = new counter value, r10 = 1).
  const u64 n = gload32(self + 252, base);      // lwz r11,252(r31)
  const u32 a8 = gload32(self + 92, base);      // lwz r3,92(r31)
  const u64 n1 = n + 1;                         // addi r9,r11,1 (wraps at 2^32)
  ctx.r11.u64 = n;
  gstore8(self + 260, base, 1);                 // stb r10,260(r31)  (li r10,1)
  gstore32(self + 252, base, (u32)n1);          // stw r9,252(r31)
  ctx.r3.u64 = a8;
  ctx.r9.u64 = n1;
  ctx.r10.u64 = 1;
  ctx.lr = 0x821C90A4;
  sub_824F9BE0(ctx, base);

  // (9)
  ctx.r3.u64 = gload32(self + 156, base);       // lwz r3,156(r31)
  ctx.lr = 0x821C90AC;
  sub_8218A3D8(ctx, base);

  // (10)
  const u32 a = gload32(self + 164, base);      // lwz r8,164(r31)
  ctx.r8.u64 = a;
  ctx.r3.u64 = gload32(a + 4, base);            // lwz r3,4(r8)
  ctx.lr = 0x821C90B8;
  sub_82184580(ctx, base);

  // (11)
  const u32 b = gload32(self + 168, base);      // lwz r7,168(r31)
  ctx.r7.u64 = b;
  ctx.r3.u64 = gload32(b + 4, base);            // lwz r3,4(r7)
  ctx.lr = 0x821C90C4;
  sub_8227BE08(ctx, base);

  // (12) Vtable dispatch #2: re-read the singleton, call vt[7](g).
  const u32 g2 = gload32(singleton, base);      // lwz r3,-6420(r30) (r30 unchanged since (5))
  const u32 vt2 = gload32(g2, base);            // lwz r6,0(r3)
  ctx.r3.u64 = g2;
  ctx.r6.u64 = vt2;
  const u32 target2 = gload32(vt2 + 28, base);  // lwz r5,28(r6)
  ctx.r5.u64 = target2;
  ctx.ctr.u64 = target2;                        // mtctr r5
  ctx.lr = 0x821C90D8;
  REX_CALL_INDIRECT_FUNC(target2);

  // (13) Final step; r3 at exit is its return value (the generated epilogue
  //      never overwrites r3, so we don't either).
  ctx.r4.u64 = 0;                               // li r4,0
  ctx.r3.u64 = gload32(self + 172, base);       // lwz r3,172(r31)
  ctx.lr = 0x821C90E4;
  sub_82195930(ctx, base);

  // --- Guest epilogue: addi r1,r1,112; lwz r12,-8(r1); mtlr r12; ld
  //     r30,-24(r1); ld r31,-16(r1); blr ---
  ctx.r1.u32 = entry_sp;
  ctx.r12.u64 = gload32(entry_sp - 8, base);    // lwz r12,-8(r1)
  ctx.lr = ctx.r12.u64;                         // mtlr r12
  ctx.r30.u64 = gload64(entry_sp - 24, base);   // ld r30,-24(r1)
  ctx.r31.u64 = gload64(entry_sp - 16, base);   // ld r31,-16(r1)
}
