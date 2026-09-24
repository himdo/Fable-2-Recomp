// Hand-tuned override of recompiled PollAndProcessOsNotifications_8229A9D8
// (recompiler output: fable_2_recomp.225.cpp; generated transforms nv=1 mfmsr=1 glock=0 gt=0).
// The "// Hand-tuned override" first line makes tools/make_hotfunc_overrides.py
// skip this file; re-run that script to regenerate the literal reference copy.
//
// Behavior-identical to the generated copy: same guest RAM reads/writes, same
// call order/args/guest return addresses, same r1/r12/r31 side effects, same
// cr6 state at each branch, and the same volatile-register state (r3-r11)
// observed by every guest callee. The three dispatch handlers (258/257/255)
// are chained calls that read args from r3-r10 left by the previous call, so
// the exact r3-r10 state at each call site is preserved. Volatile r7-r11 live
// in C++ locals where they don't feed a callee argument.
//
// The recompiler emits recompiled guest functions as WEAK extern "C" aliases
// of __imp__PollAndProcessOsNotifications_8229A9D8; fable_2_register.cpp registers the ALIAS in the
// indirect-dispatch table. This strong definition intercepts both direct bl
// calls and bctrl dispatch. __imp__PollAndProcessOsNotifications_8229A9D8 remains the original.
//
// Guest logic:
//
//   void PollAndProcessOsNotifications(?, u32 flags /* r4 */)
//   {
//     if ((flags & 1) == 0)
//       ProcessAndProcessAndProcess506(*(u32*)0x83496BCC);
//     u32 handle = *(u32*)0x833197D0;
//     if (handle != 0) {
//       u32 status = XNotifyGetNext(handle, 0, &out_id, &out_param);
//       if (status != 0) {
//         // r9/r10 only set on the second branch (matches the guest's fall-through)
//         if (out_id == 11 || out_id == 0x2000007) {
//           ProcessAndProcessAndProcess258();   // args = r3-r10 from XNotifyGetNext
//           ProcessAndProcessAndProcess257();   // args = r3-r10 from 258
//           ProcessAndProcessAndProcess255(1);  // r3 = 1
//         }
//       }
//     }
//     if (*(u8*)0x834BE630 != 0) {
//       sub_82BEB0A8(0, 0);
//       *(u8*)0x834BE630 = 0;
//     }
//   }

#include "fable_2_pch.h"

extern "C" void ProcessAndProcessAndProcess255_823737D0(PPCContext& ctx, uint8_t* base);
extern "C" void ProcessAndProcessAndProcess257_82374808(PPCContext& ctx, uint8_t* base);
extern "C" void ProcessAndProcessAndProcess258_82374B38(PPCContext& ctx, uint8_t* base);
extern "C" void ProcessAndProcessAndProcess506_821DB798(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__XNotifyGetNext(PPCContext& ctx, uint8_t* base);
extern "C" void sub_82BEB0A8(PPCContext& ctx, uint8_t* base);

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

}  // namespace

extern "C" void PollAndProcessOsNotifications_8229A9D8(PPCContext& __restrict ctx, uint8_t* base) {
  REX_FUNC_PROLOGUE();

  // --- Guest prologue: mflr r12; stw r12,-8(r1); std r31,-16(r1); stwu r1,-112(r1) ---
  const u32 entry_sp = ctx.r1.u32;
  const u32 frame_sp = entry_sp - 112;
  ctx.r12.u64 = ctx.lr;              // mflr r12
  gstore32(entry_sp - 8, base, ctx.r12.u32);    // stw r12,-8(r1)
  gstore64(entry_sp - 16, base, ctx.r31.u64);   // std r31,-16(r1)
  gstore32(frame_sp, base, entry_sp);           // stwu r1,-112(r1)
  ctx.r1.u32 = frame_sp;

  // --- Step 1: optional call (guest 0x821db798) when r4 bit 0 is clear ---
  ctx.cr6.compare<i32>(static_cast<i32>(ctx.r4.u32 & 1), 0, ctx.xer);  // clrlwi r11,r4,31; cmpwi cr6,r11,0
  if ((ctx.r4.u32 & 1) == 0) {
    ctx.r3.u64 = gload32(0x83496BCC, base);      // lis r11,-31927; lwz r3,27596(r11)
    ctx.lr = 0x8229AA00;                         // bl 0x821db798
    ProcessAndProcessAndProcess506_821DB798(ctx, base);
  }

  // --- Step 2: poll the OS notification queue (guest 0x8229AA00) ---
  // The three dispatch handlers are chained: each reads its args from r3-r10
  // as left by the previous call, so the exact register state at each call
  // must be reproduced. Factored into a lambda that touches nothing but ctx.
  auto dispatchNotifications = [&]() {
    ctx.lr = 0x8229AA4C;                          // bl 0x82374b38
    ProcessAndProcessAndProcess258_82374B38(ctx, base);
    ctx.lr = 0x8229AA50;                          // bl 0x82374808
    ProcessAndProcessAndProcess257_82374808(ctx, base);
    ctx.r3.s64 = 1;                               // li r3,1
    ctx.lr = 0x8229AA58;                          // bl 0x823737d0
    ProcessAndProcessAndProcess255_823737D0(ctx, base);
  };

  const u32 handle = gload32(0x833197D0, base);   // lis r11,-31950; addi r10,r11,-26692; lwz r3,20(r10)
  ctx.r3.u64 = handle;
  ctx.cr6.compare<u32>(handle, 0, ctx.xer);       // cmplwi cr6,r3,0
  if (handle != 0) {
    ctx.r6.s64 = frame_sp + 84;                   // addi r6,r1,84
    ctx.r5.s64 = frame_sp + 80;                   // addi r5,r1,80
    ctx.r4.s64 = 0;                               // li r4,0
    ctx.lr = 0x8229AA24;                          // bl 0x832b222c
    __imp__XNotifyGetNext(ctx, base);
    const u32 status = ctx.r3.u32;                // (r3 = XNotifyGetNext return)
    ctx.cr6.compare<i32>(static_cast<i32>(status), 0, ctx.xer);  // cmpwi cr6,r3,0
    if (status != 0) {
      const u32 out_id = gload32(frame_sp + 80, base);  // lwz r11,80(r1)
      ctx.cr6.compare<u32>(out_id, 11, ctx.xer);        // cmplwi cr6,r11,11
      if (out_id == 11) {
        dispatchNotifications();                  // bne cr6 -> guest 0x8229AA48
      } else {
        ctx.r10.s64 = 0x2000000;                  // lis r10,512
        ctx.r9.u64 = 0x2000007;                   // ori r9,r10,7
        ctx.cr6.compare<u32>(out_id, 0x2000007, ctx.xer);  // cmplw cr6,r11,r9
        if (out_id == 0x2000007) {
          dispatchNotifications();                // (fall through to guest 0x8229AA48)
        }
      }
    }
  }

  // --- Step 3: one-shot byte flag (guest 0x8229AA58) ---
  ctx.r31.s64 = static_cast<i64>(static_cast<i32>(0x834C0000));  // lis r31,-31924
  const u8 flag = gload8(0x834BE630, base);         // lbz r11,-6608(r31)
  ctx.cr6.compare<u32>(flag, 0, ctx.xer);           // cmplwi cr6,r11,0
  if (flag != 0) {
    ctx.r4.s64 = 0;                                 // li r4,0
    ctx.r3.s64 = 0;                                 // li r3,0
    ctx.lr = 0x8229AA74;                            // bl 0x82beb0a8
    sub_82BEB0A8(ctx, base);
    gstore8(0x834BE630, base, 0);                   // li r11,0; stb r11,-6608(r31)
  }

  // --- Guest epilogue (guest 0x8229AA7C): addi r1,r1,112; lwz r12; mtlr; ld r31; blr ---
  ctx.r1.u32 = entry_sp;
  ctx.r12.u64 = gload32(entry_sp - 8, base);        // lwz r12,-8(r1)
  ctx.lr = ctx.r12.u64;                             // mtlr r12
  ctx.r31.u64 = gload64(entry_sp - 16, base);       // ld r31,-16(r1)
}
