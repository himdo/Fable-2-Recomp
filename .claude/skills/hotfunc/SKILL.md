---
name: hotfunc
description: Take a recompiled guest function name, find it in generated/default/fable_2_recomp.<N>.cpp, and create a line-for-line editable strong-symbol override copy at src/core/hotfunc/unsorted/<name>.cpp, then verify with a Release build. Use when the user runs /hotfunc <function_name> or asks to "hotfunc" a function.
---

# hotfunc

Create an editable, line-for-line copy of one recompiled guest function as a
strong-symbol override under `src/core/hotfunc/unsorted/`.

Invocation: `/hotfunc <function_name>` — e.g. `/hotfunc sub_82170000`.
The argument is the exact recompiled symbol name (with its hex suffix, or a
`__imp__`-prefixed DLL export name).

## Steps

1. **Locate the function** in the generated code:

   ```
   rg -n "DEFINE_REX_FUNC\(<name>\) \{" generated/default/fable_2_recomp.*.cpp
   ```

   The generated files are `generated/default/fable_2_recomp.<N>.cpp`
   (sometimes written `generated/fable_2_recomp.#.cpp`). If there is no match,
   stop and tell the user the function was not found. If there are multiple
   matches, stop and ask which one.

2. **Extract the full function body**: from the `DEFINE_REX_FUNC(<name>) {`
   line down to (and including) the matching top-level closing `}`. In these
   generated files the top-level close is the next line that is exactly `}`
   (column 0); all nested braces are tab-indented. Sanity-check that brace
   depth returns to 0 on that line.

3. **Determine the partition**: if the source file is
   `fable_2_recomp.<N>.cpp`, the declaration header is
   `fable_2_funcs.<N>.h` (same `<N>`, also in `generated/default/`).

4. **Write `src/core/hotfunc/unsorted/<name>.cpp`** using the template below.
   The body must be a **verbatim, line-for-line copy** — do not reformat,
   "clean up", or optimize anything. Only the first macro line changes (it
   becomes a plain strong `extern "C"` definition).

   If `src/core/hotfunc/unsorted/<name>.cpp` already exists:
   - if its first line is `// Hand-tuned override`, do **not** overwrite it
     without explicit user confirmation;
   - otherwise overwrite it (it is a regenerable copy) and mention that.

   Do not modify anything under `generated/`.

5. **Build and verify** (Release):

   ```
   build_release_wrapper.cmd
   ```

   (run in the background — it is a long build). It runs
   `build.cmd -release fable_2`, writes `build_release.log`, and writes
   `build_release.status` containing `BUILD_EXIT_CODE=<n>`.
   - `BUILD_EXIT_CODE=0` → success; report the new file path and that the
     Release build passed.
   - Non-zero → **the skill run has failed**. Parse `build_release.log` and
     list every compiler/linker error verbatim, identify the offending line
     in the new file, and explicitly tell the user they must fix it before the
     skill can be considered done. Never report success on a failed build.

## Template

```cpp
// Strong-symbol override copy of recompiled <name>
// (recompiler output: fable_2_recomp.<N>.cpp). Line-for-line copy of the
// generated function body — edit freely to optimize, but keep behavior
// identical (same guest RAM accesses in the same order, same calls/return
// addresses, same register side effects) unless intentionally deviating.
// NOTE: add a "// Hand-tuned override" first line if you hand-optimize it
// and want tools/make_hotfunc_overrides.py to skip this file.
#include "fable_2_pch.h"
#include "fable_2_funcs.<N>.h"

extern "C" void <name>(PPCContext& __restrict ctx, uint8_t* base) {
	<verbatim body: everything between the DEFINE_REX_FUNC line and the closing brace>
}
```

(For a DLL-export function whose symbol is `__imp__<name>`, the definition is
`extern "C" void __imp__<name>(PPCContext& __restrict ctx, uint8_t* base)`.)

## How the hotfunc mechanism works (why this intercepts the game)

- The recompiler emits every guest function via `DEFINE_REX_FUNC(name)`
  (defined in `generated/default/fable_2_pch.h`). On non-Apple targets that
  expands to: a strong `extern "C" __imp__<name>` (the real recompiled body)
  plus a **weak** `extern "C" <name>` that is an alias of `__imp__<name>`.
- `fable_2_register.cpp` registers the **weak alias** in the
  indirect-dispatch table, so guest `bctrl` (indirect branch) dispatch lands
  on `<name>`, and direct `bl` calls between recompiled functions also call
  the `<name>` symbol.
- A **strong** `extern "C"` definition of `<name>` in any other TU (our
  override) wins over the weak alias program-wide, so both direct `bl` calls
  and `bctrl` dispatch reach the override. `__imp__<name>` remains the
  untouched original — call it explicitly if you want to fall back to it.
- CMake picks new files up automatically: `CMakeLists.txt` does
  `file(GLOB_RECURSE HOTFUNC_SOURCES CONFIGURE_DEPENDS src/core/hotfunc/*.cpp)`,
  so any `.cpp` under `src/core/hotfunc/` (including the new `unsorted/`
  folder) is compiled with no CMake edits.
- Layout: `src/core/hotfunc/unsorted/` is the landing pad for new override
  copies. Existing organized categories: `frame/`, `os_notifications/`,
  `refcount/`, `subsystems/`, `player/`, `entity/`, `ui_rendering/`,
  `camera/`, `yield/`, `vblank/` (`hotfunc_yield.h` at the root is a shared
  helper header). Move a file into a category once it is categorized;
  `tools/make_hotfunc_overrides.py` only manages the functions it lists, so
  unlisted files are never regenerated or deleted.
- **Behavior-preservation rules** for later edits (the reason the copy is
  line-for-line first):
  - `REX_LOAD_*/REX_STORE_*` are volatile guest-RAM accesses — the order of
    reads/writes is observable, and cross-thread shared state (e.g. GPU
    progress counters) depends on volatile semantics. Keep the same
    addresses, order, and value semantics.
  - Every guest call sets `ctx.lr` to the return address before calling the
    callee (`ctx.lr = 0x...;` then the call). Keep the pair intact.
  - Under the PPC32 ABI, only r1 (frame), r13–r30, and the argument
    registers survive across calls; r3–r12 / volatile regs are
    caller-saved. Preserve the values the generated copy leaves in
    r1, r13–r30, and argument registers on every return path, plus the
    `lwz r12,-8(r1) / mtlr r12 / blr` epilogue shape.
  - `ctx.lr` is restored from the saved r12 before `blr` on every path.

## Why the two includes

- `fable_2_pch.h` — generated project PCH: defines `PPCContext`, the
  `REX_LOAD_*/REX_STORE_*/REX_FUNC_PROLOGUE` macros, image constants.
- `fable_2_funcs.<N>.h` — the **same partition header the source
  `.cpp` includes**. The copied body calls other recompiled functions by
  their bare name (guest functions, e.g. `sub_82CCA3F0(ctx, base)`) or as
  `__imp__X` (DLL exports, e.g. `__imp__NtYieldExecution(ctx, base)`). Those
  symbols are declared only by the `DECLARE_REX_FUNC` list in the partition
  header, so without the include the copy will not compile.

## Typical build failures (and what to do)

- `use of undeclared identifier` / `no matching function` for a callee → the
  callee is not declared in `fable_2_funcs.<N>.h`; add an explicit
  `extern "C" void <callee>(PPCContext& ctx, uint8_t* base);` (or
  `extern "C" void __imp__<callee>(...);`) above the definition.
- Duplicate strong symbol / linker error on `<name>` → another strong
  definition of the same symbol exists (e.g. an existing override in a
  category folder or a second copy in `unsorted/`); remove or rename one.
- Compile errors that look like the body itself is invalid → the extraction
  grabbed the wrong range (mismatched braces); re-extract from the generated
  file.
- If any build error remains after fixes, list them in the report and state
  explicitly that the user must fix them — do not claim the hotfunc is done.
