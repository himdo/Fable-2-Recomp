#!/usr/bin/env python3
"""Generate strong-symbol override copies of recompiled guest functions in
src/core/hotfunc/<category>/<name>.cpp for performance optimization.

Categories (one subfolder per functional area, so new functions slot into an
existing group instead of piling up in one directory):
  frame/            per-frame tick + frame-callback registration/dispatch
  os_notifications/ OS notification queue polling/dispatch
  refcount/         refcounted shared-object lifecycle (construct/release/grow)
  subsystems/       lazy subsystem init + reference resolution
  player/           active-player method query/call
  entity/           game-entity init / state update
  ui_rendering/     UI text per-frame render + item dispatch
  camera/           per-controller input handler driving the hero-camera orbit
  yield/            thread-yield throttling (hotfunc_yield.h stays at the
                    hotfunc/ root as a shared helper header)
  vblank/           vblank counter gate (per-frame 30 fps pacing wait)

Mechanism: recompiled guest functions are WEAK extern "C" aliases of
__imp__<name>, and fable_2_register.cpp registers the ALIAS in the
indirect-dispatch table. A strong extern "C" definition with the same name
intercepts both direct bl calls and bctrl dispatch.

Transforms (all behavior-preserving):
  hand_tuned  The override file is hand-written (starts with a
          "// Hand-tuned override" first line) for readability. The script
          skips it entirely; emit() also refuses to overwrite any existing
          hand-tuned file as a safety net.
  nv      Non-volatile guest-RAM access (GV*/SV* macros over REX_RAW_ADDR).
          Requires the function to touch no MMIO (no REX_MM_*). Safe because
          each recompiled function compiles independently and every C/DLL call
          is opaque, so CSE only happens inside basic blocks.
  mfmsr   mfmsr fast path: plain atomic read of ppc_global_lock_count_()
          instead of fence + REX_CHECK_GLOBAL_LOCK() (mutex + count). Nothing
          in the SDK reads ctx.msr, so the value only feeds mtmsrd restores.
  glock   Cache the global-critical-region mutex reference per thread
          (REX_ENTER/LEAVE_GLOBAL_LOCK call rex::thread::
          global_critical_region::mutex() - a DLL function - on every use).
          Same mutex, same order of lock/unlock and count update.
  gt      Inline GetTimebase_8221EB58 call sites with the exact codegen
          expressions (r11/r10/cr0/r3 side effects preserved).
  hoist   (sub_82276C30 only, OFF by default) Per-iteration hoist of the
          28352(r27)/28360(r26) time-constant doubles. REVERTED: the loop
          yields the host thread mid-iteration (NtYieldExecution), so another
          guest thread can update the constants between the hoisted load and
          its uses. Kept as an option for re-testing.

Usage:
  python tools/make_hotfunc_overrides.py                 # generate all (skips hand-tuned)
  python tools/make_hotfunc_overrides.py <name> [<name>] # generate subset
  HOTFUNC_HOIST=1 python tools/make_hotfunc_overrides.py # re-test hoist

Generated files must stay in sync with the recompiler codegen: re-run this
script if a targeted function's codegen (or a recomp-level patch) changes.
"""
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GEN_DIR = os.path.join(ROOT, "generated", "default")
OUT_DIR = os.path.join(ROOT, "src", "core", "hotfunc")

# name -> transforms to apply
FUNCTIONS = {
    # dir = output subfolder under src/core/hotfunc/ (see module docstring).
    "ProcessGameFrame_82276C30": dict(dir="frame", hand_tuned=True),
    "DispatchFrameCallback_821A6010": dict(dir="frame", hand_tuned=True),
    "RegisterFrameCallback_821DCF10": dict(dir="frame", hand_tuned=True),
    "Release_RefCounted_821C67D8": dict(dir="refcount", hand_tuned=True),
    "ConstructRefCounted_8222CF18": dict(dir="refcount", hand_tuned=True),
    "ProcessGrowBitArray_821EC668": dict(dir="refcount", hand_tuned=True),
    "LazyInitSubsystemA_8217FD08": dict(dir="subsystems", hand_tuned=True),
    "LazyInitSubsystemB_822C6C60": dict(dir="subsystems", hand_tuned=True),
    "ResolveSubsystemReference_821F8760": dict(dir="subsystems", hand_tuned=True),
    "QueryActivePlayerMethod_821EC8A0": dict(dir="player", hand_tuned=True),
    "CallActivePlayerMethod_821A11E8": dict(dir="player", hand_tuned=True),
    "PollAndProcessOsNotifications_8229A9D8": dict(dir="os_notifications", hand_tuned=True),
    "DispatchOsNotifications_82185080": dict(dir="os_notifications", hand_tuned=True),
    "InitializeGameEntity_8233CAC8": dict(dir="entity", hand_tuned=True),
    "ProcessGameStateUpdate_821C9008": dict(dir="entity", hand_tuned=True),
    "UIText_FrameRender": dict(dir="ui_rendering", hand_tuned=True),
    "UIText_FrameRenderIter": dict(dir="ui_rendering", hand_tuned=True),
    "UITextItem_Dispatch": dict(dir="ui_rendering", hand_tuned=True),
    "ConstructString_8217AB58": dict(dir="ui_rendering", hand_tuned=True),
    "Trampoline_8217A978": dict(dir="ui_rendering", hand_tuned=True),
    # Look-stick (right-stick) input handler in the 8304x per-controller cluster.
    # Reads RY (at *(r31+24)+10), normalizes it via VectorMathThunk_821F4D88, and
    # caches the result at handler+56 to drive the hero-camera orbit. Pulled in as a
    # strong-symbol override so the mouse look offset can be folded into the stick
    # RY before the native pipeline (normalize -> controller -> orbit) runs, giving
    # 1:1 native behavior.
    "LookStickProcess_83045778": dict(dir="camera", nv=True),
    # Confirmed right-stick Y (sThumbRY) consumer on the per-frame camera-orbit path.
    # Reads raw u16 stick axes from a local XINPUT_STATE buffer (r10 = r1+708), packs
    # them, and feeds the camera look/orbit pipeline. Called 4x/frame from
    # ProcessAndProcessAndProcess1676_832BD3E8. The mouse delta is added to sThumbRY
    # (r23) at the r10+14 read site so the entire native downstream pipeline runs with
    # combined stick+mouse input, giving 1:1 native bounds and behavior.
    "RightStickLookProcess_832BB018": dict(dir="camera", nv=True),
    "Yield_82CBD098": dict(dir="yield", nv=True, batchyield=True),
    "FrameLimiterWait_82242628": dict(dir="vblank", hand_tuned=True),
}

HAND_TUNED_MARKER = "// Hand-tuned override"

OPT_HOIST = os.environ.get("HOTFUNC_HOIST", "0") != "0"


def extract_body(name):
    """Find DEFINE_REX_FUNC(name) { ... } across generated recomp files."""
    hits = []
    for fn in sorted(os.listdir(GEN_DIR)):
        if not re.match(r"fable_2_recomp\.\d+\.cpp$", fn):
            continue
        path = os.path.join(GEN_DIR, fn)
        with io.open(path, "r", encoding="utf-8", errors="replace") as f:
            L = f.read().split("\n")
        for i, l in enumerate(L):
            if l.startswith(f"DEFINE_REX_FUNC({name})"):
                j = i + 1
                # The function's closing brace is at column 0 (no indentation).
                # Nested block closers are tab-indented, so we must NOT strip
                # leading whitespace — only an exact "}" line ends the body.
                while j < len(L) and L[j].rstrip() != "}":
                    j += 1
                assert j < len(L), f"unterminated body for {name} in {fn}"
                hits.append((fn, i, j, "\n".join(L[i + 1:j])))
    assert len(hits) == 1, f"{name}: expected exactly 1 definition, got {len(hits)}: {hits}"
    fn, i, j, body = hits[0]
    if "\r\n" in body:
        body = body.replace("\r\n", "\n")
    return fn, i + 1, j + 1, body


def transform_nv(text, name):
    repl = {
        "REX_LOAD_U8(": "GV8(",
        "REX_LOAD_U16(": "GV16(",
        "REX_LOAD_U32(": "GV32(",
        "REX_LOAD_U64(": "GV64(",
        "REX_STORE_U8(": "SV8(",
        "REX_STORE_U16(": "SV16(",
        "REX_STORE_U32(": "SV32(",
        "REX_STORE_U64(": "SV64(",
    }
    assert "REX_MM_" not in text, f"{name}: has MMIO - non-volatile transform unsafe"
    total = 0
    for old, new in repl.items():
        n = text.count(old)
        text = text.replace(old, new)
        total += n
    assert "REX_LOAD_U" not in text and "REX_STORE_U" not in text, name
    print(f"    non-volatile accesses: {total}")
    return text


GT = """\t{ // inlined GetTimebase_8221EB58 (mftb r11; rotlwi. r10,r11,0; mftb-if-zero; std r11,0(r3); li r3,1)
\t\tctx.r11.u64 = REX_QUERY_TIMEBASE();
\t\tctx.r10.u64 = __builtin_rotateleft32(ctx.r11.u32, 0);
\t\tctx.cr0.compare<int32_t>(ctx.r10.s32, 0, ctx.xer);
\t\tif (ctx.cr0.eq) {
\t\t\tctx.r11.u64 = REX_QUERY_TIMEBASE();
\t\t}
\t\tSV64(ctx.r3.u32 + 0, ctx.r11.u64);
\t\tctx.r3.s64 = 1;
\t}"""


def transform_gt(text, name):
    n = text.count("\tGetTimebase_8221EB58(ctx, base);")
    text = text.replace("\tGetTimebase_8221EB58(ctx, base);", GT)
    print(f"    GetTimebase inlined:   {n}")
    return text


def transform_mfmsr(text, name):
    pat = re.compile(r"^\tstd::atomic_thread_fence\(std::memory_order_seq_cst\);\n"
                     r"^\tctx\.(r\d+)\.u64 = REX_CHECK_GLOBAL_LOCK\(\);\n", re.MULTILINE)

    def _rep(m):
        return ("\t// mfmsr fast path: ppc_global_lock_count_() is std::atomic "
                "(REX_CHECK_GLOBAL_LOCK's lock only protected a plain int).\n"
                f"\tctx.{m.group(1)}.u64 = ppc_global_lock_count_().load(std::memory_order_acquire) ? 0 : 0x8000;\n")

    text, n = pat.subn(_rep, text)
    print(f"    mfmsr fast path:       {n}")
    return text


def transform_glock(text, name):
    n_e = text.count("\tREX_ENTER_GLOBAL_LOCK();\n")
    n_l = text.count("\tREX_LEAVE_GLOBAL_LOCK();\n")
    assert n_e == n_l, f"{name}: unbalanced global lock ENTER/LEAVE ({n_e}/{n_l})"
    text = text.replace(
        "\tREX_ENTER_GLOBAL_LOCK();\n",
        "\thotfunc_gcr_mutex().lock(); ppc_global_lock_count_().fetch_add(1);\n")
    text = text.replace(
        "\tREX_LEAVE_GLOBAL_LOCK();\n",
        "\t{ auto old_ = ppc_global_lock_count_().fetch_sub(1); assert(old_ >= 1); }\n"
        "\thotfunc_gcr_mutex().unlock();\n")
    print(f"    glock mutex cached:    {n_e} ENTER + {n_l} LEAVE")
    return text, (n_e > 0)


def transform_batchyield(text, name):
    n = text.count("\t__imp__NtYieldExecution(ctx, base);\n")
    if n:
        text = text.replace(
            "\t__imp__NtYieldExecution(ctx, base);\n",
            "\t// NtYieldExecution = MaybeYield() (SwitchToThread + barrier) and always\n"
            "\t// returns X_STATUS_SUCCESS; the threshold math below is a constant as a\n"
            "\t// result. Batch the real yield (config: [perf] hotfunc_yield_every).\n"
            "\tfable2::hotfunc::BatchedYield();\n"
            "\tctx.r3.s64 = 0; // X_STATUS_SUCCESS\n")
    print(f"    batched yield:           {n}")
    return text, (n > 0)


def transform_hoist(text, name):
    assert name == "ProcessGameFrame_82276C30", "hoist only defined for ProcessGameFrame_82276C30"
    LOOP_LBL = "loc_82276D98:\n"
    LOOP_END = "loc_8227722C:\n"
    assert LOOP_LBL in text and LOOP_END in text
    li, ei = text.index(LOOP_LBL), text.index(LOOP_END)
    pre, loop, post = text[:li], text[li:ei], text[ei:]
    F0 = "\tctx.f0.u64 = GV64(ctx.r27.u32 + 28352);\n"
    F13 = "\tctx.f13.u64 = GV64(ctx.r26.u32 + 28360);\n"
    assert loop.count(F0) == 5 and loop.count(F13) == 5
    assert pre.count(F0) == 2 and pre.count(F13) == 2
    loop = loop.replace(F0, "\tctx.f0.f64 = kT0;\n")
    loop = loop.replace(F13, "\tctx.f13.f64 = kDiv;\n")
    loop = loop.replace(
        LOOP_LBL,
        LOOP_LBL +
        "\t// 28352(r27)/28360(r26) time constants loaded once per iteration.\n"
        "\tkT0 = GV64(ctx.r27.u32 + 28352);\n"
        "\tkDiv = GV64(ctx.r26.u32 + 28360);\n", 1)
    DECL = "\t// Hoisted time constants (assigned per loop iteration; see loop top).\n"
    DECL += "\tdouble kT0 = 0.0;\n\tdouble kDiv = 0.0;\n"
    anchor = "\tPPCRegister temp{};\n"
    assert anchor in pre
    pre = pre.replace(anchor, anchor + DECL, 1)
    print("    time-constant hoist:   5 f0 + 5 f13 (UNSAFE - see docstring)")
    return pre + loop + post


MACROS_NV = """\
// Non-volatile equivalents of the pch's REX_LOAD_*/REX_STORE_* macros
// (identical address math, bswap, and byte order; no volatile).
#define GV8(x)   (*(const uint8_t*)(REX_RAW_ADDR(x)))
#define GV16(x)  __builtin_bswap16(*(const uint16_t*)(REX_RAW_ADDR(x)))
#define GV32(x)  __builtin_bswap32(*(const uint32_t*)(REX_RAW_ADDR(x)))
#define GV64(x)  __builtin_bswap64(*(const uint64_t*)(REX_RAW_ADDR(x)))
#define SV8(x, y)   (*(uint8_t*)(REX_RAW_ADDR(x)) = (y))
#define SV16(x, y)  (*(uint16_t*)(REX_RAW_ADDR(x)) = __builtin_bswap16(y))
#define SV32(x, y)  (*(uint32_t*)(REX_RAW_ADDR(x)) = __builtin_bswap32(y))
#define SV64(x, y)  (*(uint64_t*)(REX_RAW_ADDR(x)) = __builtin_bswap64(y))
"""

YIELD_INCLUDE = """\n// Batched NtYieldExecution (see header).
#include "hotfunc/hotfunc_yield.h"
"""

GLOCK_HELPER = """\
// Cached reference to the SDK's global critical region mutex: same mutex and
// same lock/unlock order as REX_ENTER/LEAVE_GLOBAL_LOCK, without the
// per-use DLL call to global_critical_region::mutex().
static inline std::recursive_mutex& hotfunc_gcr_mutex() {
  static thread_local std::recursive_mutex& cached =
      rex::thread::global_critical_region::mutex();
  return cached;
}
"""


def is_hand_tuned(path):
    if not os.path.exists(path):
        return False
    with io.open(path, "r", encoding="utf-8", errors="replace") as f:
        return f.readline().startswith(HAND_TUNED_MARKER)


def emit(name, func_dir, srcfile, span, body, active):
    dst = os.path.join(OUT_DIR, func_dir, f"{name}.cpp")
    if is_hand_tuned(dst):
        print(f"  kept hand-tuned {os.path.relpath(dst, ROOT)} (not overwritten)")
        return
    callees = sorted(set(re.findall(r"^\t([A-Za-z_]\w*)\(ctx, base\);\n", body, re.MULTILINE)))
    decls = "\n".join(f'extern "C" void {n}(PPCContext& ctx, uint8_t* base);' for n in callees)
    hdr = f"""\
// Strong-symbol override copy of recompiled {name} (generated file: {srcfile}).
// Generated by tools/make_hotfunc_overrides.py - do not edit by hand.
// Active transforms: nv={int(active.get('nv', False))} mfmsr={int(active.get('mfmsr', False))} glock={int(active.get('glock', False))} gt={int(active.get('gt', False))}
//
// The recompiler emits recompiled guest functions as WEAK extern "C" aliases
// of __imp__{name}; fable_2_register.cpp registers the ALIAS in the
// indirect-dispatch table. This strong definition intercepts both direct bl
// calls and bctrl dispatch. __imp__{name} remains the original.
"""
    out = hdr
    out += '#include "fable_2_pch.h"\n\n'
    if callees:
        out += decls + "\n\n"
    out += MACROS_NV
    if active.get("batchyield"):
        out += YIELD_INCLUDE
    if active.get("glock"):
        out += GLOCK_HELPER
    out += f'extern "C" void {name}(PPCContext& __restrict ctx, uint8_t* base) {{\n'
    out += body + "\n}\n"
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with io.open(dst, "w", encoding="utf-8", newline="\n") as f:
        f.write(out)
    print(f"  wrote {os.path.relpath(dst, ROOT)} ({len(out.splitlines())} lines)")


def main():
    only = sys.argv[1:]
    for name, cfg in FUNCTIONS.items():
        if only and name not in only:
            continue
        if cfg.get("hand_tuned"):
            print(f"{name}: hand-tuned override - skipping")
            continue
        do_hoist = cfg.get("hoist") == "auto" and OPT_HOIST
        print(f"{name}:")
        srcfile, s, e, body = extract_body(name)
        active = {}
        if cfg.get("nv"):
            body = transform_nv(body, name)
            active["nv"] = True
        if cfg.get("gt"):
            body = transform_gt(body, name)
            active["gt"] = True
        else:
            active["gt"] = False
        if cfg.get("mfmsr"):
            body = transform_mfmsr(body, name)
            active["mfmsr"] = True
        else:
            active["mfmsr"] = False
        if cfg.get("glock"):
            body, used = transform_glock(body, name)
            active["glock"] = used
        else:
            active["glock"] = False
        if cfg.get("batchyield"):
            body, used = transform_batchyield(body, name)
            active["batchyield"] = used
        if do_hoist:
            body = transform_hoist(body, name)
        emit(name, cfg["dir"], srcfile, (s, e), body, active)


if __name__ == "__main__":
    main()
