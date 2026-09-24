// Hand-tuned override of recompiled RegisterFrameCallback_821DCF10
// (recompiler output: fable_2_recomp.153.cpp; generated transforms nv=1 mfmsr=1 glock=0 gt=0).
// The "// Hand-tuned override" first line makes tools/make_hotfunc_overrides.py
// skip this file; re-run that script to regenerate the literal reference copy.
//
// Behavior-identical to the generated copy: same guest RAM reads/writes (same
// addresses, same order), same call sequence and guest return addresses, same
// r1/r12/r31 side effects, and the same argument-register state (r3-r10) at
// each sub_ call site. Volatile r5-r9/r11 live in C++ locals (caller-saved
// per the PPC32 ABI); r3 and r10 are argument registers the two sub_ calls
// read, so they are set explicitly before each call to match the generated
// copy exactly.
//
// The recompiler emits recompiled guest functions as WEAK extern "C" aliases
// of __imp__RegisterFrameCallback_821DCF10; fable_2_register.cpp registers the ALIAS in the
// indirect-dispatch table. This strong definition intercepts both direct bl
// calls and bctrl dispatch. __imp__RegisterFrameCallback_821DCF10 remains the original.
//
// Guest logic:
//
//   u32 RegisterFrameCallback(u32 arg1, u32 arg4)
//   {
//     u32 root = *(u32*)0x83496920;
//     u8  flag = root ? *(u8*)0x834968C5 : 0;
//     if (flag && *(u8*)(*(u32*)(root + 12) + 256) == 0) {
//         u32 src = *(u32*)(arg4 + 4);
//         if (src == 0) {
//             sub_82422090(arg1);            // lr = 0x821DCFA0 (alt registration)
//         } else {
//             u32 tmp = sub_82265F50(src);   // lr = 0x821DCF70 (source allocator)
//             memcpy(arg1, tmp, 64);         // 8 x 8-byte, bdnz loop
//         }
//         return arg1;
//     }
//     // Default path: build a fresh frame-callback descriptor in place at
//     // arg1.  mask = splat32(*(f32*)0x820994C0) (all four 32-bit lanes):
//     *(u128*)(arg1 + 0)  = 0;
//     *(u128*)(arg1 + 16) = *(u128*)0x820991B0 & mask;
//     *(u128*)(arg1 + 32) = *(u128*)0x820991A0 & mask;
//     *(f32*)(arg1 + 48)  = *(f32*)0x82100204;
//     *(u8*)(arg1 + 52)   = 0;
//     return arg1;
//   }
//
// Note: the guest splat path does lfs f0,0x820994C0, stores it to the stack,
// loads it back via lvlx, then vspltw's lane 0 - which is exactly the 32-bit
// word at 0x820994C0 (the float->double->float round trip is lossless). It is
// reduced to a single gload32 here (the dead stack scratch writes are omitted).
// Likewise each lvx128/vand/stvx128 pair is four 32-bit gload32/gstore32 ANDs,
// so no VectorMaskL/simde is needed. The single fpscr.disableFlushMode() the
// generated copy performs before the lfs is kept so the guest fpscr state
// matches.
#include "fable_2_pch.h"

extern "C" void sub_82265F50(PPCContext& ctx, uint8_t* base);
extern "C" void sub_82422090(PPCContext& ctx, uint8_t* base);

extern "C" void RegisterFrameCallback_821DCF10(PPCContext& __restrict ctx, uint8_t* base) {
	REX_FUNC_PROLOGUE();
	auto gaddr = [&](u32 addr) { return base + (u32)addr + REX_PHYS_HOST_OFFSET(addr); };
	auto gload32 = [&](u32 addr) -> u32 { return __builtin_bswap32(*(const u32*)gaddr(addr)); };
	auto gload8 = [&](u32 addr) -> u32 { return *(const u8*)gaddr(addr); };
	auto gload64 = [&](u32 addr) -> u64 { return __builtin_bswap64(*(const u64*)gaddr(addr)); };
	auto gstore32 = [&](u32 addr, u32 v) { *(u32*)gaddr(addr) = __builtin_bswap32(v); };
	auto gstore64 = [&](u32 addr, u64 v) { *(u64*)gaddr(addr) = __builtin_bswap64(v); };
	auto gstore8 = [&](u32 addr, u32 v) { *(u8*)gaddr(addr) = (u8)v; };

	// --- prologue: 112-byte frame, save r12 and r31; r31 = arg1 ---
	u64 saved_sp = ctx.r1.u64;
	u32 frame_sp = ctx.r1.u32 - 112;
	gstore32(frame_sp + 104, (u32)ctx.lr);      // stw r12,-8(r1)  (r12 = lr)
	gstore64(frame_sp + 96, ctx.r31.u64);       // std r31,-16(r1) (original r31)
	gstore32(frame_sp, (u32)saved_sp);          // stwu r1,-112(r1) (old r1)
	ctx.r1.u32 = frame_sp;
	const u64 arg1_full = ctx.r3.u64;           // mr r31,r3 (full value, the return value)
	const u32 arg1 = (u32)arg1_full;
	const u32 arg4 = ctx.r4.u32;
	ctx.r31.u64 = arg1_full;

	// --- gates: root live? flag byte? free slot? ---
	const u32 root = gload32(0x83496920);
	const u32 flag = root ? (gload8(0x834968C5) & 0xFF) : 0;

	bool tookInlinePath = false;
	if (flag && gload8(gload32(root + 12) + 256) == 0) {
		const u32 src = gload32(arg4 + 4);      // lwz r3,4(r4)
		ctx.r10.u64 = 0;                        // lbz r10,256(r11): the gate byte (== 0)
		if (src == 0) {
			// No source: let the helper register the callback itself.
			// mr r3,r31; bl sub_82422090
			ctx.r3.u64 = arg1_full;
			ctx.lr = 0x821DCFA0;
			sub_82422090(ctx, base);
			tookInlinePath = true;
		} else {
			// Copy the 64-byte descriptor the allocator returned into arg1.
			// bl sub_82265F50 (first argument r3 = src, per lwz r3,4(r4))
			ctx.r3.u64 = src;
			ctx.lr = 0x821DCF70;
			sub_82265F50(ctx, base);
			const u32 srcPtr = ctx.r3.u32;      // mr r11,r3 (return value)
			for (u32 i = 0; i < 64; i += 8) {   // bdnz: 8 x 8-byte copy
				gstore64(arg1 + i, gload64(srcPtr + i));
			}
			tookInlinePath = true;
		}
	}

	if (!tookInlinePath) {
		// Default path: synthesize the descriptor in place at arg1.
		ctx.fpscr.disableFlushMode();           // lfs f0: match generated fpscr side effect
		const u32 mask = gload32(0x820994C0);   // splat float, all four lanes
		for (u32 i = 0; i < 16; i += 4) {       // vspltisw v0,0 / stvx128 v0
			gstore32(arg1 + i, 0);
		}
		for (u32 i = 0; i < 16; i += 4) {       // lvx128/vand/stvx128 v8 (from 0x820991B0)
			gstore32(arg1 + 16 + i, gload32(0x820991B0 + i) & mask);
		}
		for (u32 i = 0; i < 16; i += 4) {       // lvx128/vand/stvx128 v7 (from 0x820991A0)
			gstore32(arg1 + 32 + i, gload32(0x820991A0 + i) & mask);
		}
		gstore8(arg1 + 52, 0);                  // stb r9,52(r31)
		gstore32(arg1 + 48, gload32(0x82100204));  // lfs f0,516(r10); stfs f0,48(r31)
	}

	// --- epilogue: return arg1, then restore r1/r12/lr/r31 ---
	ctx.r3.u64 = arg1_full;                     // mr r3,r31 (working r31 = arg1)
	ctx.r1.u64 = saved_sp;                      // addi r1,r1,112
	ctx.r12.u64 = gload32(frame_sp + 104);      // lwz r12,-8(r1)
	ctx.lr = ctx.r12.u64;                       // mtlr r12
	ctx.r31.u64 = gload64(frame_sp + 96);       // ld r31,-16(r1)
	return;                                     // blr
}
