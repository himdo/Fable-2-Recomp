"""Idempotent, fail-closed startup hooks; never distribute generated guest code."""
from pathlib import Path

MARKER = '// [recomp-patch: macos-startup-handoff]'

def apply(directory):
    directory = Path(directory)
    targets = {
        'CriticalSection_EnterOrTry_82200688': 'lock',
        'sub_8236C360': 'release',
    }
    edits = {}
    for function, kind in targets.items():
        anchor = 'DEFINE_REX_FUNC(' + function + ') {'
        matches = []
        for path in sorted(directory.glob('fable_2_recomp.*.cpp')):
            text = edits.get(path, path.read_text())
            if anchor in text:
                if text.count(anchor) != 1:
                    raise ValueError('Duplicate function: ' + function)
                matches.append((path, text))
        if len(matches) != 1:
            raise ValueError('Expected exactly one generated function: ' + function)
        path, text = matches[0]
        start = text.index(anchor)
        end = text.find('\nDEFINE_REX_FUNC', start + len(anchor))
        if end < 0:
            end = len(text)
        body = text[start:end]
        if MARKER in body:
            continue
        if kind == 'lock':
            body = body.replace(anchor, anchor + '\n\t' + MARKER + '\n\tfable2::startup::LockCall startup_lock_call(ctx.r4.u32, ctx.lr, ctx.r5.u32 & 255, base);', 1)
        else:
            call = '__imp__RtlLeaveCriticalSection(ctx, base);'
            if body.count(call) != 5:
                raise ValueError('Unexpected render-lock release count')
            body = body.replace(call, 'fable2::startup::release(ctx, base, __imp__RtlLeaveCriticalSection); ' + MARKER)
        changed = text[:start] + body + text[end:]
        include = '#include "fable2_startup_handoff.h"\n'
        if include not in changed:
            # The guest types/macros are supplied by the generated PCH.
            changed = include + changed
        edits[path] = changed
    # Validate all anchors before modifying either unit.
    for path, text in edits.items():
        path.write_text(text)
    return len(edits)
