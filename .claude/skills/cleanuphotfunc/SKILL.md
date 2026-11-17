---
name: cleanuphotfunc
description: Take a hotfunc override under src/core/hotfunc/ and rewrite its goto/label control flow into equivalent if/else statements and loops, keeping the behavior 1:1 identical, then run a Release build and fix any problems. Use when the user runs /cleanuphotfunc [Name] or asks to clean up a hotfunc's gotos.
---

# cleanuphotfunc

Rewrite a hotfunc override so it contains **no `goto` and no `loc_*`
labels**, using ordinary C++ control flow (if/else, while/do-while,
`continue`, `break`, early `return`) that is behaviorally **1-to-1
identical** to the current code. Then rebuild in Release and fix any
problems the build reveals.

Invocation: `/cleanuphotfunc <Name>` (e.g. `/cleanuphotfunc sub_82170000`).
`<Name>` is the guest function name (the filename without `.cpp`).

## 1. Resolve the target file

- **Name given**: look for `src/core/hotfunc/unsorted/<Name>.cpp` first,
  then any category folder:
  `rg --files src/core/hotfunc | rg "<Name>\.cpp$"`.
  - If not found, stop and tell the user. Suggest running
    `/hotfunc <Name>` first if the function exists in the generated code.
  - If more than one match, stop and ask which file.
- **No name given**: use the most recent hotfunc mentioned in the current
  conversation (a `/hotfunc` result, a file that was just created/edited,
  or a function under discussion). If none is identifiable or it is
  ambiguous, **ask the user** which function to clean up before touching
  anything.

Read the whole target file first. If its first line is
`// Hand-tuned override` it is already partially hand-tuned — apply the same
rewrite to whatever goto/label control flow remains.

## 2. The rewrite (must stay 1:1)

Generated/hotfunc bodies look like this:

```cpp
	// cmpwi cr6,r9,0
	ctx.cr6.compare<int32_t>(ctx.r9.s32, 0, ctx.xer);
	// beq+ 0x82170114
	if (ctx.cr6.eq) goto loc_82170114;
	...fall-through block...
loc_82170114:
	...target block...
```

Transform patterns:

| Generated pattern | Rewrite |
|---|---|
| `if (cond) goto loc_A;` then fall-through block B, with `loc_A:` later | `if (cond) { ...A... } else { ...B... }` (or hoist the fall-through into the `if` body, whichever keeps block contents verbatim and minimal) |
| Unconditional `goto loc_A;` (tail jump; the label is adjacent or a few lines away) | delete the jump, let execution flow into block A |
| Back edge: `goto loc_L;` where label `loc_L` appears **earlier** in the function | make `loc_L` the head of a `while (true) { ... }`; the back-edge goto becomes the end of the loop body |
| Forward exit out of that loop | `break;` (or the loop's exit condition) |
| Forward jump over a region inside a loop body | `continue;` |
| Function-tail block (`lwz r12 / mtlr / blr / return`) | keep as the final straight-line code; only use early `return` if the original exits there |

**Invariants — violating any of these breaks 1:1 behavior:**

- **Statement order inside every basic block is unchanged.** No statement is
  added, removed, reordered, or edited. This includes every
  `REX_LOAD_*/REX_STORE_*`, `ctx.rN` read/write, `ctx.lr = 0x...;` +
  callee pair, and `ctx.crN.compare<...>(...)`.
- **Branch conditions are used exactly as written** (`ctx.cr0.eq`,
  `!ctx.cr6.lt`, ...). Never re-derive, inline, or "simplify" a condition —
  the compare that sets the CR field must stay in the same position
  relative to the branch.
- **Loop conversion preserves iteration semantics.** With a goto back edge,
  every statement between the label and the back edge re-executes each
  iteration — a `while (true)` loop reproduces that exactly. Crucially:
  volatile guest-RAM loads at the loop head **must** re-execute each
  iteration. Do not hoist or cache any load/store across the back edge
  (ThinLTO can see through helper functions and hoist a non-volatile
  cross-thread load, turning a spin-wait into a hang — see the
  "Shared state" notes in `src/core/hotfunc/vblank/FrameLimiterWait_82242628.cpp`).
- **All exit paths are preserved**, and on each path the same registers
  hold the same values as before (r1, r13–r30, argument registers, cr
  fields, `ctx.lr` restored before every `blr`).
- **Scope rules for the new C++ blocks:** the generated code declares all
  locals (`ea`, `temp`, ...) at the top of the function — leave them there
  so every new `{ }` block can still see them. Do not introduce new
  variables, except loop bookkeeping that is provably side-effect-free.
- **No gratuitous reformatting or renames.** Only control-flow syntax
  changes. Do not merge branches, drop "dead" stores, or add optimizations
  — that is a separate task. The in-repo exemplar of the finished style is
  `src/core/hotfunc/vblank/FrameLimiterWait_82242628.cpp` (goto-free,
  otherwise faithful transliteration).

**Header comment:** replace the file's top comment block with a
hand-tuned header whose **first line is literally**:

```cpp
// Hand-tuned override: goto-free rewrite of <Name>.
```

followed by 2–4 lines stating that jumps were replaced with if/else +
loops, everything else is identical to the generated body (same guest
loads/stores in the same order, same calls/return addresses/exit paths,
same register side effects), and that the first line makes
`tools/make_hotfunc_overrides.py` skip the file. (The marker matters: the
generator refuses to overwrite files starting with it.)

## 3. Build and clean up problems

Run the Release build in the background (it is long):

```
build_release_wrapper.cmd
```

It writes `build_release.log` and `build_release.status`
(`BUILD_EXIT_CODE=<n>`).

- `BUILD_EXIT_CODE=0` → done; report the file path, what was converted
  (how many loops/if-blocks replaced how many labels), and that the
  Release build passed.
- Non-zero → read `build_release.log`, fix the file, and **rebuild** until
  clean. Typical rewrite failures:
  - variable declared inside the new block but used after it → move the
    declaration back to the top with the other locals;
  - missing/extra braces from block conversion → re-check basic-block
    boundaries against the original label layout;
  - stray `goto`/label left behind or a label that lost its block;
  - unused-variable warnings-as-errors → the variable is only used in a
    branch that got restructured; keep the original statement (do not
    delete it), adjust structure instead.
- If a problem cannot be fixed without changing behavior, stop, list every
  remaining error verbatim from `build_release.log`, and state explicitly
  that the user must fix them. Never report success on a failed build.

## 4. Final report

- File path and that no `goto`/`loc_*` label remains
  (`rg -n "goto |loc_[0-9A-F]+:" <file>` must be empty).
- Summary of the control-flow transformation.
- Release build status (exit code).
- Any behavioral caveats (there should be none — if there are, list them).
