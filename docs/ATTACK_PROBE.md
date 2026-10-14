# Attack probe — finding the guest function that starts a melee attack

`src/diagnostics/fable2_attack_probe.h` identifies which recompiled guest
functions run when an input action starts (default: the melee attack buttons),
so the action can be remapped natively (hot-function override / direct call)
instead of going through the gamepad-button path.

## How it decides

Built on the guest function-call tracer (`src/core/fable2_func_trace.h`, which
hooks `REX_FUNC_PROLOGUE` in every recompiled guest function):

1. **Rolling baseline** — every 500 ms it snapshots the tracer's cumulative
   per-function call counts and keeps the last 3 s of per-interval deltas.
   This is "what the game is calling right now" (idle anim, walking, render).
2. **Press detection** — it watches the FINAL merged pad state (keyboard +
   physical pad + remote, OR-merged) from two sources: the guest's own
   `XamInputGetState` wrapper (`GetUser1InputState_822B2D60`) and
   the keyboard/mouse driver. A rising edge on a watched button (default
   `X,Y` = Fable 2 melee light/heavy) starts a capture, no matter which input
   device drove the press.
3. **Capture** — for 700 ms after the press the tracer's sequential log is on
   and bracketed with `ATTACK-PROBE` markers, and the cumulative counts are
   snapshotted at open/close.
4. **Report** — per capture, `fable2_attack_probe.log` (next to the exe,
   append) lists:
   - **NEW** — functions called in the window that never ran in the pre-press
     baseline (sorted by count);
   - **INCREASED** — functions called far more than the baseline rate predicts
     (sorted by ratio over baseline-scaled count);
   - **TOP** — the plain top-N of the window.

   The melee start-up chain is in NEW/INCREASED. The sequential trace
   (`fable2_func_trace.log`, between the `CAPTURE #N START/END` markers) gives
   the call order — the first NEW entries right after the input poll are the
   entry point(s) to hook.

## Usage

```bat
set FABLE2_ATTACK_PROBE=1
fable_2.exe
```

1. Get to an open spot with a **melee** weapon equipped.
2. Stand still ~5 s so the baseline is "idle" (walking also works — the
   baseline then contains movement and the report is the delta on top).
3. Press the mapped attack key. Each press writes one `CAPTURE #N` section.

Repeat the press several times (or in different scenes) and take the
**intersection**: functions NEW/INCREASED in every capture are the stable
attack-start set. One-off entries (GC, audio, save blips) fall out.

Cross-check candidates against `generated/default/*.cpp` (disassembly in
comments) and `FUNCTION_NAMES.md` before hooking. To remap the attack natively,
define a strong override of the entry function (see
`docs/hotfunc_overrides.md`) or call it from your key handler.

## Tunables

Env (read when the probe first activates) / runtime `extern "C"` setters:

| Env var | Default | Meaning |
|---|---|---|
| `FABLE2_ATTACK_PROBE` | off | `1` = enable at launch |
| `FABLE2_ATTACK_PROBE_BUTTONS` | `X,Y` | watched names: `A B X Y LB RB LT RT Up Down Left Right L3 R3` (synonyms `LSHOULDER RSHOULDER DUP DDOWN DLEFT DRIGHT LTHUMB RTHUMB LTRIGGER RTRIGGER`) |
| `FABLE2_ATTACK_PROBE_WINDOW_MS` | `700` | capture window after the press (`Fable2AttackProbeSetWindowMs`) |
| `FABLE2_ATTACK_PROBE_BASELINE_MS` | `3000` | baseline window (`Fable2AttackProbeSetBaselineMs`) |
| `FABLE2_ATTACK_PROBE_SAMPLE_MS` | `500` | baseline sample period |
| `FABLE2_ATTACK_PROBE_COOLDOWN_MS` | `1000` | min gap between captures |
| `FABLE2_ATTACK_PROBE_TOP` | `200` | max lines per report section |

Runtime API: `Fable2AttackProbeSetEnabled(bool)`,
`Fable2AttackProbeSetButtons("X,Y")`, `Fable2AttackProbeSetWindowMs(int)`,
`Fable2AttackProbeSetBaselineMs(int)`, and `Fable2AttackProbeCaptureNow()`
(manual capture window, no press needed — do the action during it).

## Files

- `fable2_attack_probe.log` — reports (append; delete between sessions)
- `fable2_func_trace.log` — sequential trace with the `ATTACK-PROBE` markers
- `fable2_func_summary.log` — live count summary while the probe runs (a side
  effect of the tracer's summary mode, convenient for eyeballing rates)

Notes / limits:

- The probe keeps the tracer's per-function counting on while enabled, which
  has a measurable CPU cost — treat it as a Debug tool.
- A press in the front-end/pause menu also captures; just ignore those
  sections (they say which button triggered them).
- The baseline only spans the last `BASELINE_MS`, so "idle" means *recently
  idle* — avoid big scene transitions right before the press.
