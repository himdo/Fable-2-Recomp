# Test report: vexptefp range fix (branch `vexptefp-range`)

Tested 2026-11-17 on the development PC (Windows 11). The game branch is
master (c764cba) plus one commit. The SDK submodule is the SDK's current
`main` (22ef700) plus one commit: the fix and its test.

## The change

`vexptefp`/`vexptefp128` compute 2^x for each of four lanes. The SDK codegen
built 2^floor(x) from exponent bits with no range check, so the old code
went wrong at the edges:

- x < -127 gave garbage;
- x >= 128 gave about 0;
- +-inf gave NaN.

The new code clamps the exponent to [-126, 127] and then fixes the
out-of-range lanes:

- below 2^-126 the result is 0;
- from 128 up it's +inf;
- NaN passes through.

## Results

- **Numeric check against `exp2f`:**
  - Inputs: -inf, -1000, -200, -127, -126.5, -126, -10, -1, 0, 0.5, 1, 10,
    127, 127.9, 128, 129, 1000, +inf and NaN.
  - All equal. The only difference is that results below 2^-126 flush to 0,
    the VMX flush-to-zero result.
- **SDK test:**
  - `tests/ppc/asm/instr_vexptefp.s` has a new case, `test_vexptefp_range`.
    Inputs -200, 128, -inf and +inf give 0, +inf, 0 and +inf.
  - `rexglue recompile-tests` generated the test code from the assembled
    tests.
  - Both vexptefp cases pass: `test_1` and `test_range`.
  - Negative control: with the range lines removed (the old code),
    `test_range` fails on three lanes (-200, -inf, +inf) and `test_1` still
    passes.
- **How the SDK test was run:** the full `ppc_tests` binary doesn't link on
  Windows at the SDK base. `rex::memory::Memory` and parts of the core
  library aren't exported from the shared `rexruntime`. That's unrelated to
  this change. So the generated `instr_vexptefp_*` functions and both test
  cases were compiled into a small standalone program, with the same
  compiler flags as `ppc_tests`.
- **Regenerated game code:** all 7 vexptefp sites change, and nothing else
  does.

  | Function | Instruction |
  |---|---|
  | `sub_82BA34B0` | `vexptefp v10,v13` |
  | `sub_82BA3598` | `vexptefp v9,v3` |
  | `sub_82CE24B0` | `vexptefp128 v61,v62` |
  | `sub_82D09D00` | `vexptefp128 v62,v61` and `vexptefp128 v63,v63` |
  | `sub_82D0A168` | `vexptefp128 v63,v63` |
  | `sub_82D0B2C0` | `vexptefp128 v63,v63` |

- **In game:** 9 shots were compared between builds without and with the
  fix: Bower Lake dusk/dawn/night, Wraithmarsh now/dusk/night, and
  Bloodstone now/dawn/dusk.
  - The mean absolute difference was 0.1-5 per channel, from moving NPCs
    and the dog.
  - The share of strongly blue pixels was identical.
  - **This is a correctness fix with no visible change in these scenes.** It
    does not fix the blue/purple sky, which is a separate issue already fixed
    on master.
- **Build:** Release build of `fable_2` (D3D12) and the Vulkan plugin
  (`tools\build_sdk_vulkan.cmd`) with this submodule commit: both passed. Both backends were then
  boot-checked into Bowerstone (see below).

## Boot check

Each backend was launched off-screen with the Release build. Console
`boot slot 1` loaded the adult save in Bowerstone Market. Nothing was saved,
and only one-shot console commands were used.

- **D3D12:** in game after the load, still running at the end of the
  window.
- **Vulkan:** in game after the load, still running at the end of the
  window. The log shows the Vulkan presenter and no missing-cvar warnings.

## Note on the submodule bump

The submodule moves from c26d8b6 to the SDK's current `main` plus this fix.
That also brings in SDK #33, which is already merged upstream: a Linux build
fix, and Fable II's CPU skinning and terrain tessellation on Vulkan. Open
PR #201 bumps the submodule to that same commit (22ef700). Whichever merges
first, the other only needs its submodule pointer updated.
