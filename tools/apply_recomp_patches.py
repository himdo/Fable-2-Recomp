#!/usr/bin/env python3
"""Apply recomp-level patches to codegen's output (generated/default/*.cpp).

Recomps run NATIVE code, so a Xenia code-region patch (a guest .text byte
change) has no runtime effect on its own. PREFERRED mechanism: the SDK's
mid-asm hooks ([[entrypoint.midasm_hook]] in fable_2_manifest.toml + a hook
function in src/fable2_hooks.cpp) - see docs/patches.md. This script is the
fallback for patches that cannot be expressed as hooks (e.g. constants
baked into memory operands). It runs as a build step between codegen and
compiling fable_2_recomp (wired in CMakeLists.txt), keyed on
codegen.build.stamp, so patches survive codegen re-runs.

Patches are applied as anchored text replacements with a unique marker
comment (idempotent: already-patched files are left alone). Missing anchors
are a hard error - a silently-skipped patch is worse than a failed build.

Set FABLE2_RECOMP_PATCHES=0 to skip all patches (used for A/B runs).
"""

import os
import sys

GENERATED_DIR = sys.argv[1] if len(sys.argv) > 1 else "generated/default"

# Each patch: (name, anchor, replacement). The anchor must occur exactly once
# per generated file that contains the target function. The replacement ends
# with the patch's marker comment, which is the idempotency check.
# Patches that cannot be expressed as a [[entrypoint.midasm_hook]] (see
# docs/patches.md).
PATCHES = []

MARKER = "// [recomp-patch: "


def main() -> int:
    if os.environ.get("FABLE2_RECOMP_PATCHES") == "0":
        print("[recomp-patches] FABLE2_RECOMP_PATCHES=0 - skipping")
        return 0

    files = sorted(
        f for f in os.listdir(GENERATED_DIR) if f.startswith("fable_2_recomp.") and f.endswith(".cpp")
    )
    if not files:
        print(f"[recomp-patches] ERROR: no fable_2_recomp.*.cpp in {GENERATED_DIR}", file=sys.stderr)
        return 1

    applied_total = 0
    for name, anchor, replacement in PATCHES:
        marker = MARKER + name + "]"  # MARKER ends with a space
        found = False
        for fname in files:
            path = os.path.join(GENERATED_DIR, fname)
            with open(path, "rb") as f:
                data = f.read().decode("utf-8")
            if marker in data:
                found = True  # already applied (idempotent)
                continue
            if anchor not in data:
                continue
            data = data.replace(anchor, replacement, 1)
            with open(path, "wb") as f:
                f.write(data.encode("utf-8"))
            found = True
            print(f"[recomp-patches] applied '{name}' in {fname}")
            applied_total += 1
        if not found:
            print(f"[recomp-patches] ERROR: anchor for '{name}' not found in any "
                  f"{len(files)} file(s) under {GENERATED_DIR}", file=sys.stderr)
            return 1

    print(f"[recomp-patches] done ({applied_total} applied now, rest already present)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
