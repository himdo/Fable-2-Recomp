#!/usr/bin/env python3
"""Make the recompiled code call guest functions directly, so they can be inlined.

Every generated function is defined as __imp__NAME, plus a weak `noinline`
alias NAME (DEFINE_REX_FUNC in the generated fable_2_pch.h) that hand-written
overrides (src/core/hotfunc/...) replace with a strong definition. The
generated code calls NAME, so no guest call can ever be inlined, not even an
18-store register-save helper called a billion times.

This rewrites the generated calls `NAME(ctx, base);` to `__imp__NAME(ctx, base);`
for every function that is not overridden. Overridden = anything the sources
define with `extern "C" void NAME(PPCContext& ...) {` or through
FABLE2_HOT_HOOK(NAME, ...) (2026-11-22: this finds all 112 overrides of the
built binary). --binary <fable_2> adds the strong symbols of a built binary
as a second check. --any-mention keeps every generated function named
anywhere in src/ (the most cautious mode; it also keeps helpers that hand-
written code merely calls, such as __savegprlr_N, so it inlines less).
build.sh --game-lto runs this before compiling the game code. Indirect calls (the function table) still go through NAME,
so they reach overrides as before. Run it after codegen; running it again
changes nothing.

  direct_guest_calls.py <generated dir> <source dir> [--binary <built fable_2>]
  direct_guest_calls.py <generated dir> <source dir> --any-mention
"""
import os
import re
import subprocess
import sys

IDENT_RE = re.compile(r"[A-Za-z_]\w*")
DEF_RE = re.compile(r'extern\s+"C"\s+void\s+([A-Za-z_]\w*)\s*\(\s*PPCContext\s*&[^)]*\)\s*\{')
HOOK_RE = re.compile(r"FABLE2_HOT_HOOK\(\s*([A-Za-z_]\w*)")


def defined_in_sources(src_dir):
    names = set()
    for root, _, files in os.walk(src_dir):
        for fn in files:
            if fn.endswith((".cpp", ".h", ".hpp", ".cc", ".inc", ".inl")):
                with open(os.path.join(root, fn), encoding="utf-8", errors="replace") as f:
                    text = f.read()
                names.update(DEF_RE.findall(text))
                names.update(HOOK_RE.findall(text))
    return names


CALL_RE = re.compile(r'(?<![\w.>])([A-Za-z_]\w*)\(ctx, base\);')


def names_in_sources(src_dir):
    """Every identifier in the project's own sources."""
    names = set()
    for root, _, files in os.walk(src_dir):
        for fn in files:
            if fn.endswith((".cpp", ".h", ".hpp", ".cc", ".inc", ".inl")):
                with open(os.path.join(root, fn), encoding="utf-8", errors="replace") as f:
                    names.update(IDENT_RE.findall(f.read()))
    return names


def overrides_in_binary(path):
    out = subprocess.run(["nm", path], capture_output=True, text=True, check=True).stdout
    strong, imp = set(), set()
    for line in out.splitlines():
        parts = line.split()
        if len(parts) != 3:
            continue
        kind, name = parts[1], parts[2]
        if name.startswith("__imp__"):
            imp.add(name[len("__imp__"):])
        elif kind == "T":
            strong.add(name)
    return strong & imp


def main(argv):
    if len(argv) < 2:
        sys.stderr.write(__doc__)
        return 1
    gen_dir, src_dir = argv[0], argv[1]
    defined = set()
    files = [f for f in os.listdir(gen_dir) if f.endswith(".cpp")]
    for fn in files:
        with open(os.path.join(gen_dir, fn), encoding="utf-8", errors="replace") as f:
            defined.update(re.findall(r"^DEFINE_REX_FUNC\((\w+)\)", f.read(), re.M))
    if "--any-mention" in argv:
        overrides = defined & names_in_sources(src_dir)
    else:
        overrides = defined & defined_in_sources(src_dir)
        if "--binary" in argv:
            binary = argv[argv.index("--binary") + 1]
            in_bin = overrides_in_binary(binary) & defined
            missed = sorted(in_bin - overrides)
            if missed:
                print("overridden in the binary but not defined in the sources (kept anyway):",
                      ", ".join(missed[:20]))
            overrides |= in_bin

    direct = defined - overrides

    changed_files = calls = kept = restored = 0
    for fn in files:
        p = os.path.join(gen_dir, fn)
        with open(p, encoding="utf-8", errors="replace") as f:
            text = f.read()
        n_direct = n_kept = n_restored = 0

        def repl(m):
            nonlocal n_direct, n_kept, n_restored
            name = m.group(1)
            # A function that gained an override since an earlier run: its
            # call sites were made direct then, point them at it again.
            if name.startswith("__imp__") and name[len("__imp__"):] in overrides:
                n_restored += 1
                return f"{name[len('__imp__'):]}(ctx, base);"
            if name in direct:
                n_direct += 1
                return f"__imp__{name}(ctx, base);"
            if name in overrides:
                n_kept += 1
            return m.group(0)

        new = CALL_RE.sub(repl, text)
        calls += n_direct
        kept += n_kept
        restored += n_restored
        if new != text:
            with open(p, "w", encoding="utf-8", newline="") as f:
                f.write(new)
            changed_files += 1
    print(f"{len(defined)} generated functions, {len(overrides)} overridden (kept); "
          f"{calls} calls made direct in {changed_files} files, {kept} calls to overrides kept, "
          f"{restored} restored to new overrides")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
