# Native Lua GC (plan, in progress)

## Why

Profiling the main game thread (`GameThread`) in gameplay (WSL, `perf`, snowy
Bowerstone opening) puts ~52% of its time in six neighbouring functions that are
Lua 5.1's incremental garbage collector (`lgc.c`), run as recompiled PowerPC
code. Recompiled, every guest register write is materialised in `ctx` and every
memory access is a byte-swapped volatile load/store, so pointer-chasing loops
like these are far slower than native code. The main thread is the Deck's
bottleneck (one core pegged, CPU ~33% overall, GPU ~60% at low power).

| Share of main thread | Generated name | Lua 5.1 function |
|---|---|---|
| 11.3% | `ProcessStructure_Recursive_82BCB610` | `sweeplist(L, p, count)` |
| 9.8% | `StateDispatch_82BCA7D8` | `reallymarkobject(g, o)` |
| 9.4% | `StateDispatch_82BCBB40` | `singlestep(L)` |
| 9.0% | `StateDispatch2_82BCAA58` | `traversetable(g, h)` |
| 7.8% | `ProcessElementList_82BCAC98` | `traverseproto(g, f)` |
| 5.2% | `StateDispatch_82BCB0A8` | `propagatemark(g)` |
| 2.5% | `StateHandler_82BCAE28` | `traverseclosure(g, cl)` |

Others: `ProcessStateAndRange_82BCAF40` = `traversestack`,
`DispatchOnKind7_82BCB480` = `freeobj`, `FindAndSetFlagBit_82BCD5E0` =
`luaT_gettm` (via `gfasttm`, used for `__mode`), `FindChar_82CA3980` = `strchr`.

## Layouts (from the generated code; 32-bit big-endian guest pointers)

- `GCheader`: `next` +0, `tt` +4 (u8), `marked` +5 (u8). White bits = `marked & 3`,
  black bit = 4, weak-key/value bits = 8/16.
- Type tags: string 4, table 5, function 6, userdata 7, thread 8, proto 9, upval 10.
  `iscollectable(v)` = `v->tt >= 4`.
- `TValue` = 8 bytes: value +0 (u32), tt +4 (s32). Lua numbers are 32-bit floats.
- `Table` (32 bytes): flags +6, lsizenode +7, metatable +8, array +12, node +16,
  gclist +24, sizearray +28. `Node` = 20 bytes: i_val +0, i_key value +8,
  key tt +12 (dead key tag written: 11).
- `Closure`: isC +6, nupvalues +7, gclist +8, env +12. C closure upvalues
  (TValue) from +20; Lua closure: proto +16, upval pointers from +20.
- `Udata`: metatable +8, env +12. `UpVal`: v +8 (TValue*), closed value at +12.
- `Proto`: k +8 (TValue[]), p +16 (Proto*[]), locvars +24 (12 bytes each,
  varname at +0), upvalues +28 (TString*[]), source +32, sizeupvalues +36,
  sizek +40, sizep +52, sizelocvars +56, gclist +68.
- `lua_State`: gclist +88; size_ci +44 / stacksize +48 (as used by propagatemark).
- `global_State` (g = L->l_G at L+16): currentwhite +20, rootgc / sweepgc +28
  (sweeplist updates it when the swept object is the current one), gray +36,
  grayagain +40, weak +44, tmname[] at +172 (TM_MODE = 3 passed to luaT_gettm).

`propagatemark` return values (work estimates used by `singlestep`) must be
reproduced exactly: table `8*(sizearray+4) + 20*(1<<lsizenode)`, C closure
`8*nup + 20`, Lua closure `4*(nup + 5)`, thread `8*(3*[+48] + [+44]) + 100`,
proto `4*(19 + 2*sizelocvars + [+52] + [+48] + [+44] + [+40] + [+36] + ...)`
(see `StateDispatch_82BCB0A8` for the exact field list).

## Plan

1. Hotfunc overrides (strong symbols, `src/core/hotfunc/luagc/`) with native
   C++ for `reallymarkobject`, `traversetable` (weak `__mode` path keeps calling
   the guest `luaT_gettm`/`strchr` through `ctx`), `traverseclosure`,
   `traverseproto`, `propagatemark` (thread case calls the guest
   `traversestack`), then `sweeplist` (calls the guest `freeobj`).
   Only `r3` (return value) and guest memory must match: the PPC ABI makes
   volatile registers dead after return, and non-volatiles are untouched.
2. Runtime switch (`[perf] native_lua_gc`, default on once verified) that
   forwards to `__imp__<name>` when off.
3. Verify mode for the mark functions: run native against a read/write overlay,
   then the original, and compare every overlaid address plus `r3`.
4. Measure main-thread CPU per frame over frames 600-1200 (same method as the
   code-model A/B), then package for the Deck.

## Status (2026-11-09)

Implemented in `src/core/hotfunc/luagc/NativeLuaGC.cpp` for the six functions
above (all but `singlestep`), behind `[perf] native_lua_gc` (default on).
`FABLE2_LUAGC_VERIFY=1` verified 30.4M calls during gameplay with no byte or
return-value mismatch (0.1% of calls not verifiable: they call guest code).
On the Steam Deck the worst spot in the opening area stayed at 20-22 fps
(about +1 fps at most), so the collector is not what limits that scene; the
earlier profile was taken in the opening movie, not in that spot.
