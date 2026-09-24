# Hand-tune ProcessGameFrame_82276C30.cpp (readable C++, no gotos)

**STATUS: DONE** — no gotos, no labels; idiomatic `if/else` + `do/while` +
`break`; behavior-identical (all 529 statements preserved in order); debug AND
release (-O3) builds green; generator skips the file.

Goal: turn `src/core/hotfunc/ProcessGameFrame_82276C30.cpp` from goto-laced
recompiler output into readable C++ — **no gotos**, proper `if/else` +
`do-while` nesting — while staying **behavior-identical** to the generated
copy (same guest RAM reads/writes, same call order/args, same guest return
addresses, same `r1/r3-r31/ctr/cr*` side effects at every call site and on
return).

Adopts the project's `// Hand-tuned override` convention (first line) so
`tools/make_hotfunc_overrides.py` stops regenerating it, and reuses the
`gload*/gstore*` + small-helper style from `CallActivePlayerMethod_821A11E8.cpp`
and `ConstructRefCounted_8222CF18.cpp`.

## Jump inventory (from grep, line numbers in current generated copy)

Forward/backward jumps (25 gotos / 21 labels):

- L96 `bne→loc_82276C88` — early `sub_8236CC28` block
- L244 `beq→loc_8227722C` — **break out to epilogue**
- L279 `loc_82276D98:` — **outer loop top**
- L285/L291 `beq→loc_82276DD8` — `frame->b256 && frame->b257` gate
- L314 `beq→loc_8227722C` — **break out to epilogue**
- L315 `loc_82276DD8:`
- L379 `ble→loc_82277208` — yield/threshold tail
- L399/L432 `beq/ble→loc_82276EAC` — OS-notification gate
- L449 `loc_82276EAC:` — `InitializeGameEntity` + frame-advance region
- L480 `ble→loc_82276EE8` / L491 `loc_82276EE8:`
- L501 `bgt→loc_82276F00` / L504 `loc_82276F00:`
- L512 `beq→loc_82276F14` / L515 `loc_82276F14:`
- L526 `beq→loc_82276FAC` — bit-array pair branch
- L557/L583 `bne→loc_82276F5C` — **CAS retry loop #1**
- L593/L619 `bne→loc_82276F88` — **CAS retry loop #2**
- L623 `b→loc_8227702C` (unconditional)
- L624 `loc_82276FAC:`
- L655/L681 `bne→loc_82276FE0` — **CAS retry loop #3**
- L691/L717 `bne→loc_8227700C` — **CAS retry loop #4**
- L720 `loc_8227702C:`
- L724 `bne→loc_8227704C` / L737 `beq→loc_82277054` — active-player method
- L738/L744 `loc_8227704C:` / `loc_82277054:`
- L814 `beq→loc_82277164` / L825 `beq→loc_8227712C` — callback registration
- L873 `loc_8227712C:` / L914 `loc_82277164:`
- L946 `beq→loc_822771D0` / L956 `bgt→loc_822771CC` / L966 `beq→loc_822771CC`
- L971 `b→loc_822771D0` (unconditional) / L972 `loc_822771CC:` / L975 `loc_822771D0:`
- L1015 `b→loc_8227720C` (unconditional)
- L1016 `loc_82277208:` / L1020 `loc_8227720C:`
- L1037 `bne→loc_82276D98` — **outer loop back-edge**
- L1038 `loc_8227722C:` — epilogue

## Stage 0 — Grounding (no behavior change) — DONE

- [x] Build the current file as-is (baseline green; fable_2.exe up-to-date, exit 0)- [x] Read `generated/default/fable_2_pch.h`; confirmed:
  - `REX_RAW_ADDR(x)` = `base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)`; on
    win32 `REX_PHYS_HOST_OFFSET` = 0x1000 for addr >= 0xE0000000, else 0.
    (Identical to the other hand-tuned files' `gaddr()` helper.)
  - `REX_QUERY_TIMEBASE()` = `rex::chrono::Clock::QueryGuestTickCount()`.
  - `ppc_global_lock_count_()` = global `std::atomic<i32>` accessor.
  - `REX_CALL_INDIRECT_FUNC(x)` = bounds-checked dispatch via
    `REX_LOOKUP_FUNC(base, x)` with `ResolveIndirectFunction` fallback;
    sets `ctx.last_indirect_target`.
  - `REX_FUNC_PROLOGUE()` = `__builtin_assume` on base 32-byte alignment
    (or Tracy zone under profiling).
  - `REX_ENTER/LEAVE_GLOBAL_LOCK` order = lock+fetch_add / fetch_sub+unlock —
    matches the generated hotfunc copy's unlock order.
- [x] Confirmed `PPCContext` fields in
  `thirdparty/rexglue-sdk-src/include/rex/ppc/context.h`:
  - `PPCRegister` union {s8/u8/s16/u16/s32/u32/s64/u64/f32/f64};
    `r0-r31`, `reserved`, `ctr`, `f0-f31` all use it.
  - `xer` = `XERRegister` {so, ov, ca}.
  - `cr0-cr7` = `CRRegister` {lt, gt, eq, so/un}; `compare<T>(l, r, xer)`
    sets so = xer.so; `compare(double, double)` sets `un` (NaN).
  - `msr` = plain `uint32_t`.
  - `fpscr` = `FPSCRRegister` {csr}; `disableFlushMode()` has a *host* side
    effect (writes the host CSR via `Platform::setcsr`), so call sites must
    be preserved, not deleted.
  - `last_indirect_target` set by `REX_CALL_INDIRECT_FUNC`.

## Stage 1 — Control-flow map (on paper) — DONE

- [x] Labeled graph built (27 gotos / 22 labels; table appended below)
- [x] Each jump classified
- [x] Outer loop + exits identified
- [x] Four CAS retry loops identified (6F5C L557, 6F88 L593, 6FE0 L655, 700C L691)
- [x] No statement's side effect lost/duplicated by re-nesting (verified: the
      only in-loop break is L314; the only epilogue exits are L244/L314;
      L379 is a clean if/else; L526 pair A/B mutually exclusive, both -> 702C)

### Verified structure

```
prologue
if (cr6.eq) { <L97-113: sub_8236CC28 block> }        // L96
...straight line: time/pacing setup, vcall @0x82276D4C...
if (!ctx.cr6.eq) {                                   // L244 gates setup+loop
  <setup L245-278: lis/addi constants r6..r24>
  do {                                               // 6D98 (L279)
    if (b256 != 0) {                                 // L285
      if (b257 != 0) {                               // L291
        LazyInitA; RtlEnterCrit; RtlLeaveCrit;
        if (ctx.cr6.eq) break;                       // L314 -> epilogue
      }
    }
    <6DD8 L316-378: timebase, frame math, subf. r30,r28,r29>
    if (ctx.cr0.gt) {                                // L379 if/else
      <BIG BLOCK L380-1014 (see below)>
    } else {
      YieldAndCheckThreshold;                        // 7208 (L1016)
    }
    <720C L1020-1036: vcall @0x82277220 tail>
  } while (!ctx.cr6.eq);                             // L1037 back-edge
}
epilogue (722C L1038-1051): lwz r3,84(r31); restore; return
```

### BIG BLOCK (L380-1014) nesting

```
if (gbyte_26940 != 0) {                       // L399
  <timebase, f12..f8 math, fcmpu cr6,f8,f22>
  if (ctx.cr6.gt) {                            // L432
    <ConstructRefCounted, InitializeGameEntity> // -> 6EAC
  }
}
<6EAC L449-479: timebase, f30 math>
if (ctx.cr6.gt) {                              // L480 (cmpwi cr6,r30,1)
  <addi r29; fmadd; stfd; fmr; mr r23>          // -> 6EE8
}
<6EE8 L491-500: divw x2, cmpw, li r10,1>
if (!ctx.cr6.gt) ctx.r10 = ctx.r20;            // L501
<6F00 L504-511: lwz r9,96(r1); clrlwi r28; cmpw>
if (!ctx.cr6.eq) SV32(r1+96, r11);             // L512
<6F14 L515-525: clrlwi r30; lfd f1; li r5,6; cmplwi cr6,r30,0>
if (ctx.cr6.eq) { <PAIR B> } else { <PAIR A> } // L526; both -> 702C
<702C L720-743: active-player method>
if (ctx.cr6.eq) {                              // L724 (r30==0)
  <QueryActivePlayerMethod; clrlwi r10; cmplwi>
  if (!ctx.cr6.eq) <CallActivePlayerMethod>;   // L737
} else <CallActivePlayerMethod>;
<7054 L744-813: vcall @0x82277068, fdivs, sub_822C1FB8, LazyInitA,
   sub_82198860, LazyInitB, sub_822A9B20>
if (ctx.cr6.eq == false) {                     // L814 (r30!=0)
  <Resolve#1; cmplwi cr6,r3,0>
  if (ctx.cr6.eq == false) {                   // L825 (r3!=0)
    <Resolve#2, RegisterFrameCallback, lvx/stvx, lfs/lbz> // -> 712C
  }
  <712C L873-913: ProcessGameStateUpdate, timebase, f200/f208> // -> 7164
}
<7164 L914-945: DispatchFrameCallback, sub_82278C90, lwz r3,27108(r18), mr r28>
if (lbz_4(r3) != 0) {                          // L946
  <lis; lwz r10,8(r3); lwz r11; cmpw cr6,r11,r10>
  if (ctx.cr6.gt) { stb r20,4(r3); }           // L956 -> 71CC
  else {
    <lwz r11,0(r3); lbz r10,144(r11); rlwinm; cmplwi cr6,r9,0>
    if (ctx.cr6.eq) { stb r20,4(r3); }         // L966 -> 71CC
    else { sub_8243A2C8; }                     // L971 -> 71D0 (skip stb)
  }
}
<71D0 L975-1014: sub_822C93C0, timebase, f216/f29>  (falls to 720C)
```

### PAIR A (L527-623) / PAIR B (L624-719) — mutually exclusive

```
PAIR A: sub_821EE858(r1+88); mr r14,r3; mr r4,r15; li r5,-1;
  ConstructRefCounted(r1+80); ProcessGrowBitArray(r1+80); Release(r1+80);
  mr r9,r24;  do { CAS dec @r9 } while (!cr0.eq);   // 6F5C
  SV32(r1+80, r20); Release(r1+88); mr r6,r24;
  do { CAS dec @r6 } while (!cr0.eq);               // 6F88
  SV32(r1+88, r20);
PAIR B: same shape with (r1+92, r1+84, r4=r16, 6FE0, 700C)
  sub_821EE858(r1+92); ... Construct/Grow/Release(r1+84);
  mr r9,r24;  do { CAS dec @r9 } while (!cr0.eq);   // 6FE0
  SV32(r1+84, r20); Release(r1+92); mr r6,r24;
  do { CAS dec @r6 } while (!cr0.eq);               // 700C
  SV32(r1+92, r20);
```

Note: in the generated copy PAIR A is the `!eq` path (fall-through) and
PAIR B is the `eq` path (L526). The restructure keeps those associations;
source order of the two branches may swap for readability only if the
comments make it unambiguous.


## Stage 2 — Local forward jumps → `if/else` — DONE (edits applied)

Statements kept byte-for-byte.

- [x] L96 `bne→6C88` (early `sub_8236CC28` block) → `if (cr6.eq) {…}`
- [x] L501/L512 (`6F00`/`6F14`) small `r10`/`u96` block → `if(!gt){…}` / `if(!eq){…}`
- [x] L526 (`6FAC`) → `if (!cr6.eq) {PAIR A} else {PAIR B}`
- [x] L724/L737 (`704C`/`7054`) active-player → `call_active` merge
- [x] L814/L825/L873 (`7164`/`712C`) callback-registration → nested `if(!eq){…}`
- [x] L946/L956/L966/L971/L972/L975 (`71D0`/`71CC`) flag region → `do_stb` merge
- [x] Compile; confirm it builds (debug build green — see Stage 6)

Also applied in the same edits (per the verified Stage-1 map):
- [x] L244 → `if (!cr6.eq) { setup + loop }` (gates setup+loop; not a loop-break)
- [x] L285/L291 → nested `if (b256)` / `if (b257)`
- [x] L314 → `if (cr6.eq) { break; }`
- [x] L379 + L1015/7208 → `if (cr0.gt) { big block } else { Yield }`
- [x] L399/L432/L449 → `if(!eq){… if(gt){…} }` (entity-init)
- [x] L480/L491 → `if (cr6.gt) {…}` (frame advance)

## Stage 3 — Retry loops → `do-while` — DONE (edits applied)

- [x] L557/L583 (`6F5C`) CAS-decrement loop → `do {…} while (!cr0.eq)`
- [x] L593/L619 (`6F88`) CAS-decrement loop → `do {…} while (!cr0.eq)`
- [x] L655/L681 (`6FE0`) CAS-decrement loop → `do {…} while (!cr0.eq)`
- [x] L691/L717 (`700C`) CAS-decrement loop → `do {…} while (!cr0.eq)`
- [ ] (Optional, deferred) Collapse the repeated mfmsr/mtmsrd/lock/CAS/unlock
      sequence into an `always_inline` helper. Left explicit so every statement
      stays visible and byte-for-byte comparable to the generated copy.

## Stage 4 — Outer loop + break-outs — DONE (edits applied)

- [x] `loc_82276D98` … back-edge (L1037) → main `do { } while (!cr6.eq)` loop
- [x] L314 break-out → `break;` (true loop-exit); L244 = pre-loop gate
- [x] All `loc_*` labels and `goto`s removed (grep: 0 remaining)
- [x] Statement-preservation verifier: 0 deleted / 0 reordered / 0 modified
      statements, +31 control-flow lines (scratch/extract_stmts.py;
      original regenerated via make_hotfunc_overrides.py and saved to
      scratch/ProcessGameFrame_ORIGINAL.cpp)
- [x] Build confirms it compiles (debug build green, task bc6273046 exit 0;
      one earlier attempt (b9c39a3ca) failed on an extra `}` at the PAIR A/B
      boundary — fixed, then clean)

## Stage 5 — Semantic readability pass — DONE

- [x] Named `constexpr` guest addresses (kBudgetCounterAddr 0x83496EB8,
      kFrameTimeDouble 0x82100CE0, kBitArrayHolder 0x8349E6EC), each used at its
      access; all 18 `lis` base literals made readable as
      `(int64_t)(int32_t)0xNNNNNNNN` (exact sign-extended value).
- [x] High-level guest-logic pseudocode comment block (per-frame update: timing
      -> work loop -> subsystem updates -> budget decrement -> yield).
- [x] Section-divider comments for the 12 major regions (prologue, self+probe,
      frame timing, per-frame setup, main loop, OS notifications, budget
      decrement, active-player, misc+callbacks, game state, yield, epilogue).
- [x] Documented the volatile-register caveat in the header.
- [ ] (Deliberately skipped) Named locals for register scratch: r24/r16/r15/r30
      cross full-width into call args (r3/r4) and address bases, and the float
      timing chain is interdependent, so keeping every `ctx.rN` preserves the 1:1
      comparison against the recompiler output (the stated goal).

Verification: statement diff vs. the generated reference shows exactly 23
value-preserving substitutions (18 lis hex + r24 + frame-time + 3 bit-array
constant uses); 0 reorders, 0 other changes. Debug build green (b412c0065);
release build pending (b2069ef3d).

## Stage 6 — Verification gate

- [x] Builds clean in debug preset (task bc6273046 exit 0; `.obj` + `.exe` rebuilt)
- [x] Builds clean in release preset (-O3) (task bde87a832 exit 0; obj + exe rebuilt)
- [x] Grep confirms **zero** `goto` and **zero** `loc_` labels remain
      (the single "goto" string hit is the header comment, not code)
- [x] Statement-preservation: all 529 guest statements kept byte-for-byte in
      the same order — this covers register/arg state and `ctx.lr` at every
      `ctx.lr = 0x…` call site (verified by scratch/extract_stmts.py diff)
- [ ] (Optional) side-by-side trace/fuzz vs. generated copy on a few frames
- [x] `tools/make_hotfunc_overrides.py` now *skips* this file
      (prints "hand-tuned override - skipping"; first-line marker + dict flag)

## Stage 7 — Break it up (Option A) — DONE

Extract the 7 regions into `static inline` helpers + collapse the 4 CAS loops and
7 inlined-GetTimebase blocks into shared helpers, so the main function reads as a
readable skeleton. Behavior-identical: `ctx` carries all guest state, so each
region reads/writes the same members it did inline.

- [x] Shared helpers: `decrement_global_counter(ctx,base,&addr,&val,&msr_save)`
      (the 4 identical mfmsr/mtmsrd/lwarx/stwcx CAS loops) +
      `query_guest_timebase(ctx,base)` (the 7 inlined GetTimebase blocks).
- [x] Region helpers (verbatim bodies): `probe_and_setup`, `compute_frame_timing`,
      `setup_frame`, `frame_gate` (break -> return bool), `compute_loop_timing`,
      `process_frame_work`, `check_loop_backedge`.
- [x] Per-region scratch-local preamble (temp/ea) re-derived by usage.
- [x] Main function is now a ~50-line skeleton (prologue + calls + do/while + epilogue).
- [x] Behavior-identity: scratch/verify_pgf_breakup.py flattens the helpers back
      into one function and confirms **all 558 guest statements preserved in order**
      vs. the pre-breakup snapshot (0 diffs).
- [x] Debug build green (bd7147c9f exit 0; obj 19:09:54, exe 19:10:44) +
      Release `-O3` build green (b13e65cb0 exit 0; obj 19:11:59, exe 19:12:17).

## Status: DONE (control-flow restructure complete + verified)

The user's goal is met: `ProcessGameFrame_82276C30.cpp` has **no gotos and no
labels**, using idiomatic `if/else` + `do { } while` + `break` nesting, while
remaining **behavior-identical** to the generated copy (every guest statement
preserved byte-for-byte and in order; only control-flow structure changed).

Deliverables / artifacts:
- `src/core/hotfunc/ProcessGameFrame_82276C30.cpp` — restructured, hand-tuned
  (first line `// Hand-tuned override`, documented header).
- `tools/make_hotfunc_overrides.py` — entry changed to `dict(hand_tuned=True)`
  so the generator skips it.
- `scratch/ProcessGameFrame_ORIGINAL.cpp` — the deterministic generated reference
  copy (regenerated via the script) used for the statement-preservation diff.
- `scratch/ProcessGameFrame_EDITED.cpp` — snapshot of the edited file.
- `scratch/extract_stmts.py` — the statement-preservation verifier.

Optional follow-up (Stage 5, not required for the goal): named `constexpr`
addresses, named locals for register scratch that isn't re-read across a call,
a high-level guest-logic pseudocode comment block, and the volatile-register
caveat note. The current file keeps every statement visible for a 1:1 comparison
against the generated copy.

## Jump classification table (all 27 gotos)

| Line | Cond   | Target | Class           | Becomes                          |
|------|--------|--------|-----------------|----------------------------------|
| 96   | !eq    | 6C88   | fwd-skip        | `if (eq) {block}`                |
| 244  | eq     | 722C   | pre-loop skip   | `if (!eq) {setup + loop}`        |
| 285  | eq     | 6DD8   | fwd-skip        | `if (b256 != 0) {…}`             |
| 291  | eq     | 6DD8   | fwd-skip        | nested `if (b257 != 0) {…}`      |
| 314  | eq     | 722C   | loop-break      | `break;`                         |
| 379  | !gt    | 7208   | fwd-skip        | `if (gt) {big} else {yield}`     |
| 399  | eq     | 6EAC   | fwd-skip        | `if (!eq) {…}`                   |
| 432  | !gt    | 6EAC   | fwd-skip        | `if (gt) {…}`                    |
| 480  | !gt    | 6EE8   | fwd-skip        | `if (gt) {…}`                    |
| 501  | gt     | 6F00   | fwd-skip        | `if (!gt) r10 = r20`             |
| 512  | eq     | 6F14   | fwd-skip        | `if (!eq) stw`                   |
| 526  | eq     | 6FAC   | fwd-skip        | `if (eq) {PAIR B} else {PAIR A}` |
| 583  | !eq c0 | 6F5C   | CAS retry       | `do {…} while (!cr0.eq)`         |
| 619  | !eq c0 | 6F88   | CAS retry       | `do {…} while (!cr0.eq)`         |
| 623  | (b)    | 702C   | uncond-fwd      | end of PAIR-A branch             |
| 681  | !eq c0 | 6FE0   | CAS retry       | `do {…} while (!cr0.eq)`         |
| 717  | !eq c0 | 700C   | CAS retry       | `do {…} while (!cr0.eq)`         |
| 724  | !eq    | 704C   | fwd-skip        | `if (eq) {Q; maybe C} else {C}`  |
| 737  | eq     | 7054   | fwd-skip        | `if (!eq) {C}` (in r30==0 arm)   |
| 814  | eq     | 7164   | fwd-skip        | `if (!eq) {…}`                   |
| 825  | eq     | 712C   | fwd-skip        | nested `if (!eq) {…}`            |
| 946  | eq     | 71D0   | fwd-skip        | `if (!eq) {…}`                   |
| 956  | gt     | 71CC   | fwd-skip        | `if (gt) {stb}`                  |
| 966  | eq     | 71CC   | fwd-skip        | `if (eq) {stb}` else sub_8243A2C8|
| 971  | (b)    | 71D0   | uncond-fwd      | end of sub_8243A2C8 arm          |
| 1015 | (b)    | 720C   | uncond-fwd      | end of BIG BLOCK (skip yield)    |
| 1037 | !eq    | 6D98   | loop back-edge  | `do {…} while (!cr0.eq)` cond    |

## Notes / open questions

1. Depth: Stage 2–4 fully removes gotos with minimal semantic risk; Stage 5
   makes it *truly* readable. Decide after Stage 4.
2. The F5C/F88 and FE0/700C pairs look like near-duplicates (decrement the
   same budget counter twice via two refcounted blocks). Keep both unless
   proven redundant.
3. Execute incrementally, compiling after each stage.

## Performance — compute_loop_timing (profiler: 13.29% of the function) DONE

`compute_loop_timing` runs every iteration of the frame loop. Profiler + asm
analysis (`scratch/asm_pgf.sh`, `-O3 -msse4.1`) found its hot spots, in order:

1. **`std::fma` → libm `fma()` call ×2** (the PPC `fmadd`). On an SSE4.1 target
   with no FMA instruction, `std::fma` tail-calls libm `fma()` (~100-300 cycles
call each). DOMINANT cost. FIXED below.
2. **`REX_QUERY_TIMEBASE()` → `Clock::QueryGuestTickCount()`** (QPC + global
   `tick_mutex_` + scaling math) — ~30-70 cycles, per iteration. In the SDK
   (global); not safely removable locally (see open items).
3. **~8 GV/SV accesses** — each carries the `REX_PHYS_HOST_OFFSET` branch
   (`cmpl $0xE0000000`/`setae`/`shll`) because the address is a runtime value.
   The offset is always 0 here (all guest RAM < 0xE0000000), but it can't be
   constant-folded without a semantic "guest-RAM-only" assumption (see open items).
4. **1 `divsd`** (guest `fdiv`), 1 `cvttsd2si` (guest `fctiwz`, native).

### Applied fix: `guest_fma` (target("fma"))
- `std::fma(a,b,c)` compiles to a libm call. A `guest_fma(a,b,c)` helper with
  `__attribute__((target("fma")))` + `__builtin_fma` compiles the SAME
  single-rounding IEEE FMA to one `vfmadd*sd` (~5 cycles, identical result).
  The target attribute keeps it a tiny out-of-line call (the compiler won't
  inline FMA code into the non-FMA callers) — still a ~10-30x win per call.
- All 3 `std::fma` sites (2 in `compute_loop_timing`, 1 in `process_frame_work`)
  now use `guest_fma`. Asm: **0 libm `fma` calls** (was 3), 3 `guest_fma` calls
  each holding one `vfmadd213sd`.
- Behavior-identical: FMA3 is the correctly-rounded multiply-add, exactly what
  libm `fma` computes; `scratch/verify_pgf_breakup.py` canonicalizes
  `guest_fma` → `std::fma` and still reports all 558 statements preserved.
- Requires an FMA3-capable x86-64 host (any modern CPU; the build already needs
  SSE4.1). Debug + Release build green; behavior-identity re-verified.

### Profiler-friendly build DONE (corrected: the real culprit was an -O0 cache override, not inlining)
**Symptom:** the `fable_2_profiler` build overstated guest-function costs (e.g.
`ProcessGameStateUpdate_821C9008` at 7.57%). 

**Root cause (verified):** the profiling build's cache had
`CMAKE_CXX_FLAGS_RELEASE` = empty (a one-time manual override), so it compiled
at **`-O0`**, not `-O3`. At `-O0` the ~384k tiny `inline` `CRRegister::compare()`
calls are *not* inlined (they become function calls). Measured in
`fable_2_recomp.189.cpp.obj`: the `-O0` build had **5733 `callq`** vs the real
`-O3` build's **2680** — ~3000 extra thunk calls per file. So the 7.57% was an
`-O0` artifact, not the real cost. (An earlier `-fno-inline` theory was a red
herring: removing it alone left the count at 5733 because the build was still
`-O0`.)

**Fix (3 parts, all verified):**
1. **Pin `-O3` in the preset** — `win-amd64-release-profiling` now sets
   `CMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG"` explicitly (matching the real build
   and Clang's default). Call count drops back to **2680 — exactly the real
   build**. This makes the profile match real-build codegen.
2. **`__attribute__((noinline))` on the region helpers** in
   `ProcessGameFrame_82276C30.cpp` (`decrement_global_counter`,
   `query_guest_timebase`, `probe_and_setup`, `compute_frame_timing`,
   `setup_frame`, `frame_gate`, `compute_loop_timing`, `process_frame_work`,
   `check_loop_backedge`) so they stay distinct symbols at `-O3` (where they'd
   otherwise inline). `guest_fma` already stays out-of-line via its `target()`.
3. **CMake option renamed `FABLE2_PROFILER_NO_INLINE` → `FABLE2_PROFILER_FRIENDLY`**
   and now only adds `-fno-omit-frame-pointer` (keeps the rbp frame chain for
   clean profiler call stacks) — no more global `-fno-inline`.

Verified: profiling build = 319 ninja rules carry `-O3`, **0** `-fno-inline`,
316 `-fno-omit-frame-pointer`; the ProcessGameFrame object still emits all 9
region helpers as distinct symbols. Regular Release/Debug builds are unaffected
(option defaults OFF, `-O3` pinned only in the profiling preset).

### Further opportunities (not applied — riskier / global)
- **Timebase:** `QueryGuestTickCount()` acquires a global `tick_mutex_` on every
  call (single-threaded here). Removing/bypassing it is an SDK change affecting
  all time queries; a local cache would change the value the guest reads.
- **`REX_PHYS_HOST_OFFSET` branch:** omitting it (assuming guest-RAM-only
  addresses) would cut ~5 instructions per access, but is only safe if no
  register-held address here can be >= 0xE0000000 (MMIO) — a game-specific
  assumption to confirm before applying.
- **`-mfma` on the whole file** was rejected: it could combine unrelated
  guest `fmul`+`fadd` pairs into FMA, changing their rounding. The per-call
  `guest_fma` avoids that by scoping FMA to the `fmadd` sites only.
