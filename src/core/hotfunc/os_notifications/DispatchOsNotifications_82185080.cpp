// Hand-tuned override of recompiled DispatchOsNotifications_82185080
// (recompiler output: fable_2_recomp.260.cpp; generated transforms nv=1 mfmsr=1 glock=0 gt=0).
// The "// Hand-tuned override" first line makes tools/make_hotfunc_overrides.py
// skip this file; re-run that script to regenerate the literal reference copy.
//
// Structured + optimized override: no gotos, folded constants, volatile
// registers in C++ locals. Behavior-identical to the generated copy (same
// guest RAM reads/writes, same call order/args/return addresses, same
// r1/r3-r6/r12/r30/r31/ctr/cr6/fpscr side effects). Safe elisions:
//  - The two `stw r30,80/84(r1)` out-param pre-zero stores (ran every frame):
//    host XNotifyGetNext (xam_notify.cpp, XNotifyGetNext_entry) unconditionally
//    writes *id_ptr and *param_ptr (both non-null here) before returning, and
//    nothing reads the slots in between.
//  - lis/ori/addi sequences folded to absolute guest addresses:
//    0x82000D30, 0x02000007, 0x8233A8D8, 0x8209FFD8, 0x82000CA8.
//  - Volatile r7-r11 and f0/f13 in C++ locals (caller-saved per the PPC32 ABI;
//    the PPC32 ISA has no 64-bit compares, so re-sign-extended high bits are
//    unobservable). The fpscr flush-mode disable before each lfd is preserved
//    at every point a preceding guest call could have re-enabled it (idempotent;
//    elided calls were guaranteed no-ops).
//
// The recompiler emits recompiled guest functions as WEAK extern "C" aliases
// of __imp__DispatchOsNotifications_82185080; fable_2_register.cpp registers the ALIAS in the
// indirect-dispatch table. This strong definition intercepts both direct bl
// calls and bctrl dispatch. __imp__DispatchOsNotifications_82185080 remains the original.
#include "fable_2_pch.h"

extern "C" void ProcessAndInitializeAndProcess2_82369EE0(PPCContext& ctx, uint8_t* base);
extern "C" void ProcessAndProcessAndProcess1100_8221EB78(PPCContext& ctx, uint8_t* base);
extern "C" void ProcessAndSearchAndProcess_8236A530(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__XNotifyGetNext(PPCContext& ctx, uint8_t* base);
extern "C" void sub_8233C1D8(PPCContext& ctx, uint8_t* base);
extern "C" void sub_8233C480(PPCContext& ctx, uint8_t* base);
extern "C" void sub_8233C600(PPCContext& ctx, uint8_t* base);
extern "C" void sub_82352AC8(PPCContext& ctx, uint8_t* base);
extern "C" void sub_82368B20(PPCContext& ctx, uint8_t* base);
extern "C" void sub_824E5668(PPCContext& ctx, uint8_t* base);

// Non-volatile equivalents of the pch's REX_LOAD_*/REX_STORE_* macros
// (identical address math, bswap, and byte order; no volatile).
#define GV8(x)   (*(const uint8_t*)(REX_RAW_ADDR(x)))
#define GV32(x)  __builtin_bswap32(*(const uint32_t*)(REX_RAW_ADDR(x)))
#define GV64(x)  __builtin_bswap64(*(const uint64_t*)(REX_RAW_ADDR(x)))
#define SV8(x, y)   (*(uint8_t*)(REX_RAW_ADDR(x)) = (y))
#define SV32(x, y)  (*(uint32_t*)(REX_RAW_ADDR(x)) = __builtin_bswap32(y))
#define SV64(x, y)  (*(uint64_t*)(REX_RAW_ADDR(x)) = __builtin_bswap64(y))
extern "C" void DispatchOsNotifications_82185080(PPCContext& __restrict ctx, uint8_t* base) {
	REX_FUNC_PROLOGUE();

	// --- Guest prologue: mflr r12; stw r12,-8(r1); std r30,-24(r1); std r31,-16(r1); stwu r1,-128(r1) ---
	const u32 entry_sp = ctx.r1.u32;
	const u32 frame_sp = entry_sp - 128;
	ctx.r12.u64 = ctx.lr;                  // mflr r12
	SV32(entry_sp - 8, ctx.r12.u32);       // stw r12,-8(r1)
	SV64(entry_sp - 24, ctx.r30.u64);      // std r30,-24(r1)
	SV64(entry_sp - 16, ctx.r31.u64);      // std r31,-16(r1)
	SV32(frame_sp, entry_sp);              // stwu r1,-128(r1)
	ctx.r1.u32 = frame_sp;

	// --- Poll the queue: XNotifyGetNext(r3 = this->p264 listener, r4 = 0,
	//     r5 = &id@sp+80, r6 = &param@sp+84). The out params are written by
	//     the host entry unconditionally, so no pre-zero stores are needed. ---
	ctx.r30.s64 = 0;                       // li r30,0 (also used by the type-9 case)
	ctx.r31.u64 = ctx.r3.u64;              // mr r31,r3
	const u32 self = ctx.r3.u32;
	ctx.r6.s64 = ctx.r1.s64 + 84;          // addi r6,r1,84
	ctx.r5.s64 = ctx.r1.s64 + 80;          // addi r5,r1,80
	ctx.r4.s64 = 0;                        // li r4,0
	ctx.r3.u64 = GV32(self + 264);         // lwz r3,264(r31)
	ctx.lr = 0x821850B8;                   // bl 0x832b222c
	__imp__XNotifyGetNext(ctx, base);
	// cmpwi cr6,r3,0: the host returns 1 when a notification was dequeued.
	ctx.cr6.compare<int32_t>(ctx.r3.s32, 0, ctx.xer);

	if (!ctx.cr6.eq) {
		const u32 type = GV32(frame_sp + 80);            // lwz r11,80(r1)
		ctx.cr6.compare<u32>(type, 11, ctx.xer);         // cmplwi cr6,r11,11

		if (ctx.cr6.gt) {
			// type > 11
			ctx.cr6.compare<u32>(type, 18, ctx.xer);     // cmplwi cr6,r11,18
			if (ctx.cr6.eq) {
				// type == 18
				ctx.r3.u64 = GV32(self + 156);           // lwz r3,156(r31)
				ctx.r4.s64 = ctx.r1.s64 + 88;            // addi r4,r1,88
				SV32(frame_sp + 88, 0xFFFFFFFFu);         // li r11,-1; stw r11,88(r1)
				ctx.lr = 0x82185188;                      // bl 0x82352ac8
				sub_82352AC8(ctx, base);
				const u32 node = GV32(self + 128);        // lwz r10,128(r31)
				const u32 out = GV32(frame_sp + 88);      // lwz r9,88(r1)
				const u32 target = GV32(node + 4);        // lwz r8,4(r10)
				SV8(target + 20, ctx.r3.u8);              // stb r3,20(r8): the callee's r3 (return value)
				SV32(target + 12, out);                   // stw r9,12(r8)
			} else {
				// lis r10,512; ori r9,r10,7; cmplw cr6,r11,r9  =>  type == 0x02000007
				ctx.cr6.compare<u32>(type, 0x02000007, ctx.xer);
				if (ctx.cr6.eq) {
					ctx.lr = 0x82185170;                  // bl 0x82368b20
					sub_82368B20(ctx, base);
				}
			}
		} else if (ctx.cr6.eq) {
			// type == 11
			ctx.r3.u64 = GV32(self + 148);            // lwz r3,148(r31)
			ctx.lr = 0x82185118;                      // bl 0x824e5668
			sub_824E5668(ctx, base);
			ctx.lr = 0x8218511C;                      // bl 0x82369ee0 (r3 = previous return value)
			ProcessAndInitializeAndProcess2_82369EE0(ctx, base);
			ctx.r4.u64 = ctx.r3.u64;                  // mr r4,r3
			ctx.r6.s64 = 0;                           // li r6,0
			ctx.r5.s64 = 0xFFFFFFFF8233A8D8ull;       // lis r11,-32204; addi r5,r11,-22312
			ctx.r3.s64 = ctx.r1.s64 + 88;             // addi r3,r1,88
			ctx.lr = 0x82185134;                      // bl 0x8236a530
			ProcessAndSearchAndProcess_8236A530(ctx, base);
			const u32 found = GV32(frame_sp + 88);    // lwz r3,88(r1)
			ctx.cr6.compare<u32>(found, 0, ctx.xer);  // cmplwi cr6,r3,0
			if (!ctx.cr6.eq) {
				// Virtual call found->vtable[2](found); guest return 0x82185150.
				ctx.r3.u64 = found;
				ctx.ctr.u64 = GV32(GV32(found) + 8);
				ctx.lr = 0x82185150;
				REX_CALL_INDIRECT_FUNC(ctx.ctr.u32);
			}
		} else {
			ctx.cr6.compare<u32>(type, 9, ctx.xer);   // cmplwi cr6,r11,9
			if (ctx.cr6.eq) {
				// type == 9
				const u32 node = GV32(self + 128);     // lwz r11,128(r31)
				const u32 param = GV32(frame_sp + 84); // lwz r10,84(r1)
				const u32 clz = param == 0 ? 32 : __builtin_clz(param); // cntlzw r9,r10
				const u32 target = GV32(node + 4);     // lwz r8,4(r11)
				// rlwinm r7,r9,27,31,31; xori r6,r7,1 - recompiler-faithful lowering
				// (equals (clz == 32) ^ 1, i.e. param != 0, for clz in 0..32).
				const u32 flag =
					(__builtin_rotateleft64((u64)clz | ((u64)clz << 32), 27) & 0x1) ^ 1;
				SV8(target + 21, (u8)flag);            // stb r6,21(r8)
				SV32(target + 16, ctx.r30.u32);        // stw r30,16(r8) (r30 == 0)
			} else {
				ctx.cr6.compare<u32>(type, 10, ctx.xer); // cmplwi cr6,r11,10
				if (ctx.cr6.eq) {
					// type == 10
					ctx.r3.u64 = self;                 // mr r3,r31
					ctx.lr = 0x821850E8;               // bl 0x8233c1d8
					sub_8233C1D8(ctx, base);
				}
			}
		}
	}

	// --- Post-dispatch block (guest 0x8218519C): every path lands here ---
	const u8 gate = GV8(self + 260);              // lbz r11,260(r31)
	ctx.cr6.compare<u32>(gate, 0, ctx.xer);       // cmplwi cr6,r11,0
	if (!ctx.cr6.eq) {
		ctx.fpscr.disableFlushMode();             // (lfd f13,232 / lfd f0,3376)
		PPCRegister t;
		t.u64 = GV64(self + 232);                 // lfd f13,232(r31)
		const double f13 = t.f64;
		t.u64 = GV64(0x82000D30);                 // lfd f0,3376(r11) (lis r11,-32256)
		const double f0 = t.f64;
		ctx.cr6.compare(f13, f0);                 // fcmpu cr6,f13,f0
		if (ctx.cr6.gt) {
			ctx.lr = 0x821851C0;                  // bl 0x8221eb78
			ProcessAndProcessAndProcess1100_8221EB78(ctx, base);
			ctx.fpscr.disableFlushMode();         // (lfd f0,232 / lfd f0,-40)
			t.u64 = GV64(self + 232);             // lfd f0,232(r31)
			const double f13b = ctx.f1.f64 - t.f64; // fsub f13,f1,f0
			t.u64 = GV64(0x8209FFD8);             // lfd f0,-40(r11) (lis r11,-32246)
			ctx.cr6.compare(f13b, t.f64);         // fcmpu cr6,f13,f0
			if (ctx.cr6.gt) {
				const u8 b258 = GV8(self + 258);   // lbz r11,258(r31)
				ctx.cr6.compare<u32>(b258, 0, ctx.xer);
				if (ctx.cr6.eq) {
					const u8 b259 = GV8(self + 259); // loc_821851F0: lbz r11,259(r31)
					ctx.cr6.compare<u32>(b259, 0, ctx.xer);
					if (!ctx.cr6.eq) {
						ctx.r3.u64 = self;         // mr r3,r31
						ctx.lr = 0x82185204;       // bl 0x8233c600
						sub_8233C600(ctx, base);
					}
				} else {
					ctx.r3.u64 = self;             // mr r3,r31
					ctx.lr = 0x821851EC;           // bl 0x8233c480
					sub_8233C480(ctx, base);
				}
				// Reset the time field (lis r11,-32256; lfd f0,3240(r11); stfd f0,232(r31)).
				ctx.fpscr.disableFlushMode();
				SV64(self + 232, GV64(0x82000CA8));
			}
		}
	}

	// --- Guest epilogue: addi r1,r1,128; lwz r12,-8(r1); mtlr r12; ld r30,-24(r1); ld r31,-16(r1); blr ---
	ctx.r1.u32 = entry_sp;
	ctx.r12.u64 = GV32(entry_sp - 8);
	ctx.lr = ctx.r12.u64;
	ctx.r30.u64 = GV64(entry_sp - 24);
	ctx.r31.u64 = GV64(entry_sp - 16);
	return;
}
