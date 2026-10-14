# Main Menu Mod Item — add a selectable item that runs its own function

## Goal

Add a 6th selectable item to the Fable 2 GOTY main menu (currently:
New Game / Continue / Downloadable Content / Language / Subtitles) that, when
activated (A), runs **our own function** at the recomp level. Deliverable: a
workable demo driven end-to-end by the remote-control channel (no human at the
keyboard), verified by screenshots + logs, with a hard timer so the game can
never stall a run.

## What we know (static investigation)

- The main menu is a *gameface* frontend screen. Its layout lives in
  `art/gui/gameface/frontendmainmenu.{fac,bgf,bsg}` inside `guiscripts.bnk`
  (extracted copies in `scratch/lua_dump/raw/guiscripts/`). The five items are
  `TEXT` elements carrying **localized string ids** (e.g. 10/220/414/608/802
  per page); the actual strings ("New Game", ...) come from the binary
  `data/language/en-uk/text/book.babel` string table — which is why "New Game"
  appears nowhere as a plain string (checked `default.xex`, `data/`, banks).
- Menu *behavior* (frontend state machine, input) is a mix of guest C++
  (gameface element manager, vtable draws) and the **GUI Lua state**
  (`guiscripts.bnk`: `scripts/gameface/frontend.lua`, `guiinput.lua`,
  `guieventqueue.lua`, `expandablemenu/*`). `frontendmainmenu` has an empty
  `PopulationTable` in `expandablemenupopulationscript.lua`, so the item list
  is structural (the .fac/.bgf layout), not a Lua table.
- Menu text is drawn through the guest UI text pipeline, already named and
  hooked: `UITextItem_Render` (0x82BFD9E8) → `UIText_RenderWithFont`
  (0x82C000F8) (see `src/diagnostics/fable2_ui_render_probe.h`).
- Automation plumbing already exists:
  - Remote control server (debug builds): `127.0.0.1:8791`, JSON-lines;
    `game_state` reports `PreMainMenu / PressAScreen / MainMenuMovie / MainMenu`
    (`src/input/remote_control_server.h`, `src/diagnostics/fable2_state_probe.h`).
    States can misfire → driver must poll + retry.
  - `tools/fable2_control.py` CLI + `script` command (atomic timed sequences).
  - Function-call tracing: `FABLE2_FUNC_TRACE=1` + `FABLE2_FUNC_TRACE_SUMMARY=1`
    (`fable2_func_summary.log` refreshed every 5 s) → we can **diff call counts
    between input actions** to find the exact navigation/activation functions.
  - UI render timeline probe: `FABLE2_UIR=1` with `FABLE2_UIR_DELAY/DUR`
    (`fable2_ui_render_probe.log`) → per-call timeline of the text pipeline.
- Hook mechanisms available (see `src/core/fable2_hooks.cpp`,
  `src/diagnostics/fable2_ui_render_probe.h`):
  1. **Strong override** — name a guest function in `fable_2_manifest.toml`
     (body becomes `__imp__<name>`), then define `extern "C" void <name>(
     PPCContext&, uint8_t*)` in src that intercepts, mutates registers, and/or
     skips the original. This is how `UIText_RenderWithFont` etc. are hooked.
  2. **Mid-asm hook** (`[[entrypoint.midasm_hook]]` in the manifest) —
     read/rewrite named registers at one instruction (gated by
     `fable2_config.toml` `[patches]` toggles, pattern in `fable2_hooks.cpp`).

## Plan

### Step 1 — Live investigation (identify the menu functions)

1. Launch the Debug build (already built at
   `out/build/win-amd64-debug/fable_2.exe`) with:
   - `FABLE2_FUNC_TRACE=1 FABLE2_FUNC_TRACE_SUMMARY=1`
     (summary only; optionally filter later).
   - A Python driver (`scratch/modmenu_drive.py`) that:
     - connects to the control server (retry until it answers — server starts
       late),
     - polls `game_state` until `PressAScreen` (retry loop; tolerate
       `MainMenuMovie`/`?`), presses A, then polls until `MainMenu` (retry),
     - performs labeled input actions with pauses and a summary snapshot before
       and after each:
       - `Down` (New Game→Continue), `Up` (Continue→New Game),
       - `Down` x4 to last item (Subtitles), `Down` once more (wrap?),
       - `Up` from top item (wrap?),
       - `A` on "Language" (safe activation — opens language screen), then
         `Back` to return.
     - hard timer: total run capped (default 240 s, > 90 s requirement),
       process killed on any stall.
   - Capture a UI-render timeline window aligned to the MainMenu
     (`FABLE2_UIR=1 FABLE2_UIR_DELAY=<t> FABLE2_UIR_DUR=<w>`) to list exactly
     which pipeline calls draw the five item labels + selection indicator.
2. From `fable2_func_summary.log` diffs: identify
   - the **navigation** function (selection-index change on Up/Down),
   - the **activation** function (A on an item; per-item dispatch),
   - the **item-text draw** call sequence (which UIText calls, what args).
3. Record findings in this doc (findings section below) and name the chosen
   functions in `fable_2_manifest.toml` (run `tools/check_manifest_collisions.py`).

### Step 2 — Implement the mod menu item

New self-contained header (e.g. `src/diagnostics/fable2_modmenu.h`) + manifest
names, all behind one master gate (debug builds; env or `[patches]` toggle):

1. **Selection extension** — hook the navigation function: selection index
   range 0..4 → 0..5 (respect the menu's observed wrap/clamp semantics; the
   new index 5 is our item).
2. **Draw the item** — while state == MainMenu, draw a "MOD OPTION" label with
   the same font/style as the other items, positioned one row below
   "Subtitles" (row pitch measured in Step 1). Reuse the existing text pipeline
   via a direct `UIText_RenderWithFont`-style call with the item font, or hook
   the per-item text draw and re-invoke it once more at the new y-offset.
   Selection highlight: mirror what index 5 would produce (if the indicator
   element is positioned from the index, the navigation hook already moves it;
   otherwise nudge the indicator element's y the same way).
3. **Run our function** — hook the activation dispatch: when item index == 5,
   suppress the original and call `fable2::modmenu::activate()`:
   - demo effect: on-screen banner text "MOD OPTION ACTIVATED" (drawn via the
     same pipeline for a few seconds) **and** append a line to
     `fable2_modmenu.log` next to the exe (deterministic, script-checkable).

### Step 3 — Verify (automated demo)

1. `scratch/modmenu_verify.py`:
   - timer 240 s default (env-overridable), kill on stall;
   - reach MainMenu (retry logic);
   - `Down` x5 → our item (verify via screenshots at each step);
   - `A` → check `fable2_modmenu.log` line appears + screenshot shows banner;
   - also verify original items still work (e.g. `A` on Language still opens
     the language screen) and the game returns to MainMenu cleanly.
2. Screenshots via `tools/capgame.ps1` (or direct BitBlt from Python).

## Risks / fallbacks

- If the selection index lives in an object field we can't cleanly reach from
  the navigation hook's registers, fall back to **tracking our own index**:
  mirror Up/Down/A from the merged pad state (state probe already merges pads)
  and drive drawing + activation from that mirror.
- If the text pipeline needs a font object we can't obtain at draw time,
  capture a reference from a live item's draw call (first MainMenu frame) and
  replay it for our label.
- State classifier misfires are expected: all driver logic is
  poll-with-timeout + retry, never a single-shot assumption.

## Findings (filled in during execution)

### Live investigation 1 — func-summary diffs (scratch/modmenu_inv/)

Ran the debug build with `FABLE2_FUNC_TRACE=1 ..._SUMMARY=1`, drove to MainMenu
(via PressAScreen + A, with retry), then performed labeled Down/Up/A/Back steps
with summary snapshots around each. Per-press normalized analysis:

- **`sub_82BE3F30`** — exactly **+1 call per menu button press** (Up/Down/A/Back,
  including clamped presses at the ends and Back). The raw menu input-event
  handler. Called from `InputEvent_PollQueue` (0x82185C90),
  which iterates UI elements (element `->44 == 13`, f64@+64 == time) and calls
  `sub_82BE3F30(r3 = element+8, r5 = local 56B struct)`.
- **`InputEvent_DispatchButtonPress`** — +2 per raw press (also in the
  0x8218xxxx "process cluster" — input processing).
- **Effective-action chain** (fires only when the press changes the selection
  or activates, NOT on clamped presses): `ProcessAndProcessAndProcess2636/2635/
  2634/2633/2632/2631?/2553` (the 0x82C20C90–0x82C23198 family), plus
  `UITextPrompt_RenderElement` (+2 per selection change) and a chain of
  `ProcessAndProcessAndProcess21xx` (0x82C10AE0…0x82C4FFD0, +4 per change).
- **Wrap semantics confirmed: the menu CLAMPS at both ends** (5 items, indices
  0–4). Evidence: the effective-action chain is absent for the extra Down at
  the bottom and for Ups past the top (up7 from index 3 → only 3 changes).
- `FrontMenu_EnsureElement` (398 lines) looks like "insert
  node into UI tree with copied fields" (r4 = dst, r5/r6 = src fields) —
  generic UI-tree manipulation, candidate hook for observing selection changes
  with object pointers in hand.

### Layout facts (static)

- `frontendmainmenu.fac` TEXT blocks reference element ids 10, 220, 414, 608,
  802, 996, 1190, 1384, 1578, 1772 (5 per page) — item labels are TEXT
  elements with localized string ids; strings resolve from `book.babel`
  (UTF-16BE in guest memory).
- Item text draw pipeline (named in manifest, strong-override hooks exist):
  `UITextItem_Render(r3 = item)` loads `font = *(0x83334A08)` (font slot[2]),
  `f1 = *(item+8)` (x?), `r4 = *(item+4)` (string), `r6 = *(item+12)`, then
  `UIText_RenderWithFont` → `UIFont_LookupGlyph` (0x82C106A8) per character.
  So a new item = a fake 16+ byte "item" object {str, x, ...} in guest memory
  + one more `UITextItem_Render`-style call per frame.

### Live investigation 2 — UIR render timeline over MainMenu (scratch/modmenu_timeline/)

- The main-menu render is dominated by the per-frame UI text pass:
  `UIText_FrameRender -> UIText_FrameRenderIter -> sub_822A2948` (~70k calls/s
  — the gameface world-text containers) plus **`UITextItem_Dispatch` at
  ~630/s (~21 real font text items per frame)** — the menu item labels.
  Per-glyph `UIFont_EmitGlyphQuad` is rare (text is laid out into glyph
  nodes, not per-glyph quads on this path).
- `UITextItem_Render` (the font-slot draw) does **not** fire during the main
  menu (zero dumps in the input-probe window) — menu labels are built and
  drawn through `UITextItem_Dispatch` (vtable[1] of the font-module text-item
  base class, vtable 0x8200A118), already strong-overridden in
  fable2_text_probe.h.
- Menu labels are live UTF-16BE strings in the front-end heap window
  0x42000000–0x42800000 (the state probe scans them 1 Hz for "New Game" etc.).
  Text items are reconstructed from the source strings every frame, so an
  in-place string mutation is picked up next frame (proven by DEADBEEF).

### Implementation refinement (from investigation 2)

- Draw hook: `UITextItem_Dispatch`. Identify the menu-label items by matching
  their source string to the 5 known labels ("New Game" … "Subtitles");
  clone/repurpose an item object for the synthetic 6th item "MOD OPTION",
  positioned one row below "Subtitles" (row pitch from the live items).
- Activation intercept: A-press edge detector in fable2_state_probe.h
  (`last_a_press_ms`) + mirrored selection index from `sub_82BE3F30`.

### Static: InputEvent_Handle (0x82BE3F30, renamed in manifest) input layout

From the generated body (fable_2_recomp.191.cpp:21862) and its caller
(InputEvent_PollQueue, fable_2_recomp.271.cpp):

- Called with r3 = r4 = element+8 (queue slot); r5 = pointer to a 56-byte
  event built by the caller from a UI element of type 13 (15 for a second
  branch):
  - event+16 (u8)  = element+8   **button code**
  - event+20 (f32) = element+12  (time/value)
  - event+44 (u32) = 1
  - event+48 (u32) = element+40
  - event+52 (u32) = 14
  - event+56/60 (f32) = element+12 / element+52
- The handler allocates a 64-byte node {+0: r28, +4: f32@element+12,
  +8..+64: event} and links it into the queue list at *(element+12),
  incrementing the counter at element+16. (A rare branch fires when the
  counter reaches 0x49490C — not part of normal menu operation.)
- So the mod menu mirrors the selection from `InputEvent_Handle` by
  reading the button code at r5+16 on every call (clamp to 0..4, plus the
  synthetic index 5).

### Live investigation 3 — probe freeze fixed, first real dumps (run 8)

Two fixes made the input probe usable (src/diagnostics/fable2_ui_input_probe.h):

1. **AV-safe gread**: the render-thread freeze was a host access violation in
   the HOP pointer-chase (garbage heap-range words in image/vtable buffers
   read as pointers). The recompiler's guest-fault exception handler
   intercepts the host-side AV and hangs the thread. `gread` now checks the
   page is MEM_COMMIT + readable via VirtualQuery before the memcpy.
2. **HOP fan-out fix**: hop targets now read into a separate buffer, so all 16
   words of the pointee are examined (the old code overwrote the source buffer
   and chased a chain into code bytes).
3. **Buffered log + 20 ms batched flush + log mutex** (per-line unbuffered
   WriteFile was triggering per-line AV scans on the render thread).
4. **Stack-minimal probe frames** (heap dump buffers, thread_local format
   buffers) + per-dump stack headroom telemetry (`[sp=.. used=..KB
   avail=..KB]`) — run 8 froze at dump 2 with large per-call stack frames,
   which points at the render thread being pushed toward its stack guard page.

First clean dump (UITextItem_Dispatch, static .bss text item r3=0x832D60F0,
vtable 0x8200A114): r5=0x703CFE20 is a transform/anim struct whose +8
word is 0x83334B58 (a controller object pointing at the global string/texture
table 0x4010603C — "Wavforms", "Wavform", "Splitters", ...); r6=0x42699C74 is
the text item's data/position struct: leading ints {0x20, 3, 2, 0, 0, 0x01010000,
1} + pointer at +56 (0x42699D50, a float-heavy matrix/transform) + f32 run
(-0.0006, 0.3695, 0.3695, -1.1127, 0.0784, -0.3529, 1.5). Same r6 pattern as
the earlier deadbeef-era dumps — this is the per-item position/transform the
draw code consumes.

### Open questions (live investigation 3 — run 9 in flight)

- Whether run 9 survives the window with stack-minimal frames (stack headroom
  numbers in the dump headers answer the guard-page question).
- Exact item object layout + the field that marks the *selected* item (color/
  alpha/flag), and per-item y positions (row pitch) — from
  `FABLE2_UIR_IN_DUMP` (BYTES=64, HOP=1) of
  UITextContainer_ProcessChildren (the 70k/s container draw),
  InputEvent_Handle (per press), and the effective-action chain.
- Where the selection index lives (menu manager object) — from the per-press
  dumps' register/heap context.

### Live investigation 4 — probe-freeze root cause + button map (runs 14/15)

- **The render-thread "freeze" is a crawl, not a hang.** Full-memory dumps
  (pointee gread + HOP pointer-chase, ~48 ms each in the -O0 build) push the
  render thread past its 33 ms frame budget, so it slows to a crawl (never a
  full 4 s of silence, so the stall dumper never trips; no WER crash, no AV).
  A **registers-only** dump (empty `FABLE2_UIR_IN_PTS`, zero guest reads) is
  ~35 snprintf per emission — cheap enough that the game stays fully
  responsive (run 14: full-res 3840x2160 screenshots, complete navigation,
  `final state: MainMenu`). So: dump registers freely; only dump guest memory
  when you can afford the crawl (i.e. at the end of a run).
- **Button mask** (measured live from `InputEvent_DispatchButtonPress` r5): the
  function ANDs the pressed-button mask (r5) against the element's supported
  buttons (r10 = u16 at r4). Mask bits: **Up=1, Down=2, Back=32, A=4096**.
  Each physical press fires the dispatcher ~twice (135–270 ms apart, ~4.8 s
  between presses in the driver) → one action per press via a 400 ms time
  gate.
- **InputEvent_Handle (0x82BE3F30) does NOT fire during main-menu
  navigation** (zero dumps in a live run), so it is not the reliable per-press
  source; `InputEvent_DispatchButtonPress` (0x82188F20) is.

### Manifest renames (confirmed roles)

- `0x82188F20` → **`InputEvent_DispatchButtonPress`** — raw press bit-dispatch
  (ANDs pressed-button mask r5 vs supported r10; ~2 calls/press).
- `0x82185C90` → **`InputEvent_PollQueue`** — per-frame input poller, calls
  InputEvent_Handle once per pending event.
- `0x82C217B8` → **`FrontMenu_EnsureElement`** — get-or-create UI element node
  (binary-search key, construct node if absent; fires on effective actions).

UI text render pipeline (roles confirmed by the UIR render probe, ablation, and
the text-append/glyph probes; the old `ProcessAndProcessAndProcess*` names were
decomplier placeholders and the manifest's auto-filler descriptions were wrong):

- `0x822A2948` → **`UITextContainer_ProcessChildren`** — per-frame container
  processor: iterates the up-to-6 child element pointers, calls each child's
  vtable[2] (update) then vtable[1] (query; first non-zero wins).
- `0x82C09018` → **`UITextElement_Draw`** — per-element draw, called for every
  visible element (r3=element, r4=render context); on the true render path.
- `0x82C09870` → **`UITextElement_PostQuery`** — element.vtable[13]; runs AFTER
  the vertex-emit calls (not the draw).
- `0x82B4EEE0` → **`UITextElement_DrawDispatch`** — tail-calls `*(r3+16)->vtable[7]`
  (element draw vtable entry).
- `0x82C09B50` → **`UITextElement_GrowBuffer`** — grows the element buffer
  (self+64/72) / advances the cursor (self+68); vertex-emit path.
- `0x82C0A230` → **`UITextElement_FieldByIndex`** — returns one of four self-fields
  selected by index (r5-1); small vertex-emit helper.

### Implementation (src/diagnostics/fable2_modmenu.h, gated on FABLE2_MODMENU=1)

- **Mirror index 0..5**: `InputEvent_DispatchButtonPress` strong override keeps a
  mirror of the selection (start 0; Down→+1 capped at 5, Up→-1 floored at 0;
  400 ms time-gate dedups the ~2 calls/press). The stock engine clamps at 0..4,
  so our index 5 maps to the highlighted bottom row (Subtitles position).
- **Draw**: when the mirror reaches 5, rewrite the live "Subtitles" source
  string(s) in place (UTF-16BE). The strings live at **0x405xxxxx** (found by
  a page-aligned scan of 0x40000000–0x44000000, mirroring the state probe's
  `aread` — the earlier whole-region `host_rw` check failed on the
  commit-on-fault front-end heap). ~3 copies exist; all are rewritten. Restore
  "Subtitles" when the mirror leaves 5. Text items rebuild from source every
  frame, so it shows next frame.
  - **Label must fit the slot**: "Subtitles" is 9 chars (20 bytes). The
    replacement must be **<= 9 chars** or the overflow corrupts the adjacent
    heap field and the front end NULL-derefs (confirmed: a 10-char "MOD
    OPTION" crashed on the walk-up). We use **"MOD MENU"** (8 chars, 18 B).
- **Activate**: A with mirror==5 → `Activate()` writes `ACTIVATED` to
  `fable2_modmenu.log` and flips the label to "LOADED" (6 chars).
- **Verify**: `scratch/modmenu_verify.py` reaches MainMenu, Down x5, A (check
  the log line), then re-walks to Language and A (original items still work).
  The game log is read only after `kill_game` (the running process holds the
  handle). Watchdog timer 300 s (env `MODMENU_TIMEOUT`), > 90 s requirement.

### Results (verified live)

- **The mod menu works.** `fable2_modmenu.log`: `base=0x100000000`, cached 3
  "Subtitles" strings (0x405xxxxx, all writable=1), `set_row(shown=1) wrote
  3/3; slot0 now 'MOD MENU'`, `ACTIVATED sel=5`, `set_row(shown=0) wrote 3/3;
  slot0 now 'Subtitles'`. The mirror reached 5, the label was rewritten +
  re-read, the mod function ran, and the restore worked.
- **The crash is pre-existing, not from the mod menu.** A control run with
  `FABLE2_MODMENU=0` (no mod writes at all) crashed during a plain Down-walk
  to Subtitles (`alive=False` before any A). The fault is a NULL deref in
  `Pool_AcquireNode_83230688` (the front-end memory pool), i.e. a heap-pool
  instability that fires during main-menu navigation in the -O0 debug build.
  It is non-deterministic: the earlier investigation runs walked the menu
  without it, v4/v5 hit it on the walk-up, the control run hit it on the
  walk-down.
- The rendered label can't be confirmed by pixel analysis: the main-menu frame
  is near-white (mean 249) so the white menu text has no contrast against the
  background. The log (write + re-read) plus the deadbeef proof (source-string
  mutation changes the render) is the evidence.
- **Side effect**: the mirror index 5 maps to the stock index 4 (Subtitles)
  because the stock engine clamps at 0..4, so A on the mod item also activates
  the stock Subtitles. For the demo this is acceptable (our `Activate()` runs);
  a cleaner version would suppress the stock A press when the mirror is at 5.
- Manifest renames (0x82188F20 → InputEvent_DispatchButtonPress, 0x82185C90 →
  InputEvent_PollQueue, 0x82C217B8 → FrontMenu_EnsureElement) are applied
  and regenerate cleanly.

### Revision — vision-confirmed render + replace "Downloadable Content" (v6)

- **The label change genuinely renders on screen.** With vision enabled, the
  screenshots confirm it: `step_down4` (mirror at the bottom row) reads
  **"Subtitles"** on the highlighted row, while `selected_mod` (mirror at
  index 5) reads **"MOD MENU"** on the same row. So the source-string mutation
  IS picked up by the per-frame text rebuild — no more "can't confirm" caveat.
  (The earlier "near-white frame / no contrast" note was about the full frame;
  the menu panel is dark and the text is high-contrast, so it reads fine.)
- **Switch to replacing "Downloadable Content" (index 2).** The bottom-row
  trick (index 5 over Subtitles) was easy to miss — the mod item only showed
  when scrolled all the way down. "Downloadable Content" is a dead button in
  this build, so we now repurpose its row (index 2, middle of the menu) as the
  mod item. It is **always visible** (persistent relabel while the main menu is
  open), needs no scrolling, and the highlight lands on it naturally because it
  is a real menu row.
  - `fable2_modmenu.h` now: relabels "Downloadable Content" (20 chars, 42 B
    slot) → "MOD MENU" persistently while in the main menu; A at mirror index
    2 → `Activate()` + relabel to "LOADED"; leaving the menu restores
    "Downloadable Content". The other rows are untouched.
  - Because it is a real in-range row (index 2), there is no more "A also
    activates the stock Subtitles" side effect — the stock engine and our
    mirror agree at index 2.
- **Verification driver** (`scratch/modmenu_verify.py`) now: Down x1 (MOD MENU
  visible), Down x1 (MOD MENU selected), A (→ LOADED), Down + A on Language
  (original items still work). PASS = `ACTIVATED` line in the log.

### Revision 2 — background scanner + robust in_menu (v30, PASSED)

The "Downloadable Content" source string turns out to be **transient and
highly non-deterministic** in the 0x40000000–0x44000000 front-end heap: it
appears for a brief window at a non-deterministic time (observed 36 s to 193 s+
from process start across runs) and is then freed/moved. Three fixes were
needed to make the mod menu reliable:

1. **Background scanner thread** (off the render thread). The 64 MB string scan
   blocked the render thread ~0.7 s per scan (16384 per-4 KB-page VirtualQuery
calls in the -O0 build), causing a ~20 s stall when run per-frame. The scan now
   runs on a detached scanner thread (`scanner_thread`) that loops every ~10 ms
   (nearly continuous) calling `find_downloadable` while `in_main_menu()` and
   `dl_n==0`, caching the addresses under `s.dl_mu`. `read_g` was rewritten to
   follow committed `MEMORY_BASIC_INFORMATION` regions (one VirtualQuery per
   region, not per page), cutting the scan to ~100 ms. The render thread only
   does the fast `set_row` label write (reading the cached addresses under the
   mutex), so it never stalls.
2. **`find_needle` fixed to match the state probe's proven logic** (start the
   memchr at `key_off`, guard `i >= key_off` before computing `i - key_off`
   to avoid a `size_t` underflow, advance past each match to find all copies,
   no trailing-NUL requirement — the front-end labels are not NUL-terminated).
3. **Robust `in_main_menu()`** — the state probe transiently reports
   `kMainMenuMovie` (render_active drops to 0 for a couple of 1 Hz samples
   while the menu idles) even though the main menu is still open. The old
   `CurrentState()==kMainMenu` gate misprocessed an A-press landing in that
   window as "leaving the menu" (dropping the activation). `in_main_menu()` now
   returns true for both `kMainMenu` and `kMainMenuMovie`, used in `on_press`,
   `on_frame`, and the scanner thread.

**Verification (v30, PASSED):** `fable2_modmenu.log` shows the correct press
sequence — Down (0→1), Down (1→2), **A at sel=2 → `ACTIVATED`**, Down (2→3),
A at sel=3 (Language). The scanner found the "Downloadable Content" string and
`set_row(shown=2) wrote 1/1` ("LOADED"). `RESULT: PASS` (`ACTIVATED` in the
log). The driver polls `fable2_modmenu.log` (the game opens it with
`FILE_SHARE_READ` so the driver can read it live) for up to 200 s for the
"cached [1-9]" line before pressing A; the watchdog timeout is 400 s
(`MODMENU_TIMEOUT`) to cover the non-deterministic string-allocation time.

- The label shows "LOADED" directly (not "MOD MENU" then "LOADED") when the
  string is found *after* the A-press; it shows "MOD MENU" first when the
  string is found *before* the A-press. Either way the mod item activates and
  the original rows keep working.
