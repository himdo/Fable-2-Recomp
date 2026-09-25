# Plan: AI Remote Input Control for the Fable 2 Recomp

**Goal:** Let an external AI (a script, agent, or LLM-driven harness) send messages to the
running `fable_2.exe` so that guest gamepad inputs happen on demand — press/hold/release
buttons, move sticks, run timed input sequences — so that AI debugging and investigation
can drive the game automatically without a human at the keyboard.

---

## 0. Status: Implemented & verified

The feature is built and running. Verification against the live `fable_2.exe`:

- **Protocol:** `tools/remote_protocol_test.py` → **37/37 checks pass** (basics, cvar
  get/set, press/get_state/release, sticky `state`, `stick`, atomic `script` with correct
  pending/timing, `enable`/`disable`, and robustness: unknown commands, malformed JSON,
  unknown inputs all rejected without dropping the connection).
- **End-to-end (guest actually reacts):** holding `A` on the title fired a cluster of
  *new* guest functions in the func trace (`LoadString_FastOrSlow`, `LoadFrom_Source`,
  `ProcessAndDispatchLocked`, `Construct*`, `Array_PushElement`) — the signature of a
  screen transition / UI build — and the captured frame showed a persistent, large layout
  change (mean brightness 109→60, ~2× the scene's own animation). So the full chain works:
  command → server → store → driver → guest `XamInputGetState` → game transition.

Implementation details that settled while building (supersede §4/§5 where they differ):

- **Two server threads** (not one): a *connection* thread does accept + command dispatch,
  and a *timer* thread sleeps until the next timeline deadline and republishes the
  resolved snapshot, so timed releases land on time even with no incoming command. Both
  funnel into the same mutex-protected store; the guest-thread path is a single struct
  copy.
- **`hold_ms` ≤ 0 / omitted = hold until released/cleared** (an open timeline interval).
  Positive `hold_ms` = auto-release after that many ms. (Default is `0`, not `-1`.)
- **`script` `delay_ms` are cumulative gaps** since the previous step (each step's
  `delay_ms` is added to a running clock from the script start), matching "wait before
  this step." The reply's `duration_ms` is when the last input stops being active.
- Device id `1ull << 61`, `synthetic = true`, **not** focus-gated; OR-merges with the
  keyboard/physical pad. `packet_number` advances only on emitted-state change.
- Responses use `std::format` with a hand-rolled mini-JSON **parser** (request side) and
  `JEsc` for any dynamic string embedded in a response (the `get_state` button list is
  built as quoted, escaped JSON strings).
- **Debug-only:** the whole channel is gated on a `FABLE2_REMOTE_CONTROL` macro
  (defined only when `CMAKE_BUILD_TYPE == "Debug"` in `CMakeLists.txt`). Release /
  RelWithDebInfo builds `#ifdef` out the pad driver, the server, and its
  start/stop in `fable_2_app.h`, and don't stage `fable2_control.py` — so a shipped
  build opens no TCP port and exposes no input channel. Rationale: a hidden localhost
  input channel in a player-facing build erodes trust; it's only useful for debugging.

### Game state API (implemented & verified)

A 1-second, evidence-based classifier on a background thread reports which screen
the game is on. **Unknown (`?`) is the default**: a known state is reported only
while its evidence is present, so gameplay, cutscenes, pause/options sub-screens and
early boot all read `?`. Verified end-to-end via `game_state` + the
`FABLE2_STATE_PROBE=1` file log (`tools/state_probe_test.py` drives a full run):

- `?` (boot) -> `PreMainMenu` (splash) -> `PressAScreen` ("Press A") ->
  `MainMenuMovie` (idle movie, after ~40 s) -> `PressAScreen` (loops), and
  `PressAScreen ->(press A)-> MainMenu`; leaving the front end (gameplay) -> `?`.

Evidence (all memory-derived, no screenshots): the classifier (1 Hz) reads:

- **front-end text** - a 1 Hz heap scan of the front-end string window
  (`0x42000000`-`0x42800000`, `scan_front_end()` in `fable2_state_probe.h`) matches
  marker words (UTF-16BE + plain ASCII):
  - "to start" present -> `prompt_alloc` (latched from the first prompt).
  - >=2 front-end menu words (`New Game`, `Load Game`, `Downloadable Content`,
    `Options`, `License`, `Quit`, `Language`) -> the menu option words are present.
  The heap holds ALLOCATED strings (the prompt is never freed), so this says which
  screen's text exists, not whether it is being drawn -- render activity supplies
  the "being drawn" bit.
- **`pe_rate`** - the prompt/manager element-list fetch rate. `sub_82B458C0` (the
  element fetch/next) is hooked; non-zero returns for lists in the 0x8333xxxx
  manager region are counted per second. High (~15-17/s) when the prompt **or** the
  menu is drawn, and it drops to **0 for the whole attract movie** (~15-30 s of
  idle). This is the movie signal: `render_active = pe_rate > 8`, so the movie
  (element manager paused) reads `!render_active` -> `MainMenuMovie`.
- **`ui_rate`** - UI text item dispatches per second. Logged for debugging but
  **not** a usable state signal: the prompt/menu text stays composited over the
  movie, so `ui_rate` stays ~700-800/s the entire time (it does *not* drop to 0
  during the movie). An earlier `pe>8 || ui>100` gate therefore never saw the
  movie; `pe` alone is the correct signal.
- **`prompt_alloc`** - a heap scan for the UTF-16BE string "to start"
  (0x42640000-0x42700000, 64 KB chunks). Allocated once at the first prompt and
  never freed, so it separates `PreMainMenu` from everything after it.
- **the front-end flag** - the XEX keeps the front-end cvar/flag strings as static
  constants (`CAN_PRESS_A` @ 0x820CD0C0, `GUI_FRONT_END` @ 0x820A8064, default
  value `DefaultScenario` @ 0x820A8094; gameplay starts in `QC010_ChildhoodStart`).
  A one-time heap hunt (log mode) locates the runtime copy of the value; while it
  stays a front-end value the game is in the front end. Once it changes (a gameplay
  scenario loads) the whole front-end branch is skipped and everything reads `?` --
  this is what makes gameplay / cutscenes / pause / options report `?`.
- **the A / B press** - rising A and B edges in the **final merged pad state**
  (all drivers OR-merged: remote + keyboard + physical). The guest's
  `XamInputGetState` wrapper (`sub_822B2D60`) is hooked and the button flags
  (`X_INPUT_GAMEPAD_A = 0x1000`, `X_INPUT_GAMEPAD_B = 0x2000`) are read from the
  filled `X_INPUT_STATE` after each call. The prompt and the menu both render UI
  at the same rate, so a rising **A on the prompt** is the discrete event that
  opens the menu: it sets a **sticky `in_menu`** flag (also set when >=2 menu
  words are found). `in_menu` is cleared by a **B** (back out to the prompt), by
  leaving the front end, or when the UI element manager pauses (movie) for 2
  samples.

Classification (Unknown is the default; a 2-sample (2 s) hysteresis stops a
one-second dip - e.g. the prompt blink-off phase - from flickering the state, which
was the old `MainMenuMovie` misfire):

- `!ready`            -> `?`
- `!front_end`        -> `?` (gameplay / cutscenes / pause / loading)
- `!prompt_alloc`     -> `PreMainMenu`
- `!render_active`    -> `MainMenuMovie` (front end, UI element manager paused)
- `in_menu`           -> `MainMenu`
- else                -> `PressAScreen`

The `game_state` command returns `{"code":N,"name":"...","front_end":"..."}`;
`fable2_control.py game-state` wraps it. State changes are logged via `REXSYS_INFO`
(transition-only, to avoid spam) and every sample to `fable2_state_probe.log`.

---

## 1. Background: how input works today

The guest game only ever sees XInput. The full path (see `src/keyboard_gamepad.h` header):

```
game (recompiled PPC)
  -> __imp__XamInputGetState
  -> rex::input::InputSystem::GetState(user_index)
     -> each InputDriver::GetDeviceState(...)   (polled once per guest input query)
     -> MergeInto: buttons OR-ed, triggers max'd, sticks summed
  -> merged X_INPUT_STATE returned to the guest
```

Existing pieces this plan builds on:

| Piece | What it gives us |
|---|---|
| `fable2::KeyboardGamepadDriver` (`src/keyboard_gamepad.h`) | Proof that a **synthetic `InputDriver`** registered in `Fable2App::OnPreSetup` (`config.input_factory`) is the correct, guest-transparent injection point. Synthetic devices route to guest user 0 and OR-merge with real pads. |
| Cvar system (`rex::cvar.h`, F3 console, `fable_2.toml`) | Hot-reloadable knobs; the keyboard map already hot-reloads on cvar text change. |
| `fable2_config.toml` (`src/fable2_config.{h,cpp}`) | Per-recomp user config loaded next to the exe; seeded into cvars in `OnPostInitLogging`. New keys are additive and non-breaking. |
| Host-side background threads | `Fable2App::OnPostSetup` already spawns a detached `std::thread` (alloc-watch), so a network server thread is an established pattern in this codebase. |
| Guest-visible state readers | `GuestPtr`/`GuestByte` helpers + probes (e.g. `fable2_heap_scan.h`, `fable2_ui_input_probe.h`) show how to safely read guest memory for observation endpoints later. |

Key property of the injection point: **the game reads input the same way in menus,
cutscenes, gameplay, and death screens.** Anything that produces an `X_INPUT_STATE`
works in every context — no per-context UI patching needed. This is exactly what an AI
debug harness needs (it must navigate menus to reach crash sites).

## 2. Non-goals (for this phase)

- Not changing how human keyboard/mouse input works (the existing driver stays untouched
  and keeps working alongside the remote driver — inputs OR-merge).
- Not deterministic frame-stepping / full sim-lockstep (noted in §9 as a future option).
- Not a general-purpose remote shell — the interface is a small, typed command set.

## 3. Architecture overview

```
AI harness (any language, any machine on localhost)
   |  TCP JSON-lines to 127.0.0.1:<port>
   v
+-----------------------------------------------------------+
| fable_2.exe                                               |
|                                                           |
|  RemoteControlServer (dedicated std::thread)              |
|   - accept loop, one connection at a time (simplest)      |
|   - parses JSON command lines                             |
|   - maintains a scheduled event queue (timed holds,       |
|     keyframed scripts) on a monotonic clock               |
|   - writes to RemoteInputState under a small mutex        |
|   - replies with JSON result lines (ok / error / state)   |
|                                                           |
|  RemoteInputState (shared struct)                         |
|   - buttons bitmask, triggers, 4 stick axes, packet#      |
|   - active/until timestamps for timed events              |
|                                                           |
|  Guest thread (main)                                      |
|   Fable2App::OnPreSetup -> input_factory                  |
|     + KeyboardGamepadDriver (existing)                    |
|     + RemoteGamepadDriver (new, this plan)                |
|         GetDeviceState(): snapshot RemoteInputState,      |
|         emit as synthetic device id (1 << 61)             |
|         -> OR-merged into guest user 0's X_INPUT_STATE    |
+-----------------------------------------------------------+
```

Two moving parts, deliberately small:
1. **`RemoteControlServer`** — the "message inbox" (a localhost TCP + JSON-lines server).
2. **`RemoteGamepadDriver`** — an SDK `InputDriver` that turns the shared state into a
   synthetic gamepad, mirroring the structure of `KeyboardGamepadDriver`.

## 4. Key design decisions

### 4.1 Inject at the input-driver layer (not via keyboard simulation)

Options considered:

- **Synthetic `InputDriver` (chosen):** in-process, zero latency, no dependency on window
  focus or foreground state, works headlessly, precise trigger/stick values, guest sees a
  normal gamepad. Reuses the exact pattern already proven by the keyboard driver.
- **`SendInput`/virtual-keyboard simulation:** fragile (focus-dependent, per-key polling,
  cannot express stick values or exact durations, interferes with human input).
- **Guest-side hook that fakes `XamInputGetState` return values:** more powerful (could
  spoof per-user state) but requires a recompiled hook + guest memory writes; unnecessary
  when the input system already merges driver state.

### 4.2 Transport: localhost TCP, JSON-lines

- **TCP on `127.0.0.1`** (default port `8791`, configurable) rather than:
  - *file watching* — no request/response, polling latency, awkward sequencing;
  - *named pipes* — Windows-only (project also builds for Android);
  - *WebSocket/HTTP* — more machinery for no benefit at this scale.
- **JSON lines** (one JSON object per request, one per response, `\n`-terminated):
  trivially generated and parsed by any AI harness, no binary framing, no dependencies.
  A request without a `req` field that expects no reply can be "fire and forget",
  but by default every line gets a response line so the harness gets confirmation.
- Bind loopback-only by default; optional shared `token` field for multi-user hosts.
- One connection at a time is fine for a debug harness; subsequent connections queue.
  (Trivially upgradeable to multi-client later.)

### 4.3 Timing model: server-side schedule, driver-side snapshot

Commands specify intent; the server translates to a **timeline of state changes** on a
monotonic clock; the driver just reads "what is active right now" each poll:

- `press` with `hold_ms` → button active from *now* to *now + hold_ms*.
- `press` + later explicit `release` → active in between.
- `stick` with `value`/`hold_ms` → axis held at a value for a duration.
- `script` → list of steps, each an input op with an optional `delay_ms` before it; the
  server expands it into the same timeline. Scripts give the AI a single atomic message
  to reproduce a bug: e.g. "hold RT, wait 300 ms, tap X, release."

The driver is polled by the guest input system (~30 Hz, the guest's frame cadence).
That polling rate is the actuation rate — matches a real controller and is what the game
logic expects. Millisecond precision in *scheduling* (from the server thread) is enough;
no sub-frame precision is needed or even physically meaningful to the guest.

`packet_number` increments only when the emitted state actually changes (guest code may
use it for edge detection; the keyboard driver already maintains one).

### 4.4 Threading and safety

- The server runs on its own `std::thread` (pattern already used by alloc-watch in
  `OnPostSetup`).
- Shared state crosses exactly one boundary: server thread → guest/main thread, via a
  small `std::mutex`-protected snapshot (`RemoteInputState` is < 64 bytes; the critical
  section is a struct copy — nanoseconds, invisible in the guest hot path).
  If profiling ever shows it matters, swap to an atomic double-buffered pointer —
  the design doesn't depend on that.
- The event queue lives entirely in the server thread; the mutex-protected state is
  just the current resolved snapshot (buttons/triggers/axes/packet). This keeps the
  guest-thread path to a single lock acquisition and a copy.
- The remote driver must **not** gate on window focus (unlike the keyboard driver):
  the AI must be able to drive the game even if the window isn't foreground.
- Shutdown: join the server thread in `OnShutdown` (or use the same detach pattern if
  joining is awkward at that hook; the thread must not outlive the runtime it reads
  window/cvar state from).

### 4.5 Device identity

- New synthetic device id: `1ull << 61` — must not collide with SDL driver's sequential
  ids (start at 1) or the keyboard driver's `1ull << 60` (`keyboard_gamepad.h`).
- `DeviceInfo.synthetic = true` so default assignment routes it to guest user 0 and
  it OR-merges with the keyboard/physical pad: the human can keep playing while the AI
  injects a button.

## 5. Component design

### 5.1 New files

```
src/remote_input_state.h      - RemoteInputState struct + shared snapshot type (mutex or
                                atomic snapshot helper). ~80 lines.
src/remote_control_server.h   - TCP accept loop, JSON-lines framing, command dispatch,
                                event timeline, connection handling. ~400-600 lines.
src/remote_gamepad_driver.h   - RemoteGamepadDriver (InputDriver) + registration helper.
                                ~150 lines, closely mirrors keyboard_gamepad.h.
tools/fable2_control.py       - Thin CLI client for humans + AI harnesses:
                                  python tools/fable2_control.py press A
                                  python tools/fable2_control.py script --file seq.json
                                  python tools/fable2_control.py state
tools/fable2_control_client.h (optional) - tiny embedded C++ client (connect + send
                                one JSON line) so other tools/tests can use it too.
```

### 5.2 Modified files

- **`src/fable_2_app.h`**
  - `OnPreSetup`: `input_factory` adds `RemoteGamepadDriver` alongside the keyboard driver.
  - `OnPostInitLogging`: seed new cvars from `fable2_config.toml [remote]`.
  - Start the `RemoteControlServer` thread here (config already loaded), stop it in
    `OnShutdown`.
- **`src/fable2_config.{h,cpp}`**
  - New `[remote]` section: `enabled` (bool, default `true`), `host` (`"127.0.0.1"`),
    `port` (`8791`), `token` (string, empty = none).
    Update the embedded default template in `fable2_config.cpp` and the staged
    `config/fable2_config.toml` in sync (existing convention).
- **`CMakeLists.txt`** — add the three new sources/headers (headers may need no entry if
  the build globs them; match the existing style).
- **`README.md`** — short section: what the port is, protocol summary, example commands.

### 5.3 `RemoteGamepadDriver` (details)

- Copy the skeleton of `KeyboardGamepadDriver` (Setup / EnumerateDevices /
  GetDeviceState / GetDeviceCapabilities / SetDeviceVibration / GetDeviceKeystroke).
- `GetDeviceState`: lock → copy snapshot → unlock; fill `X_INPUT_STATE`
  (buttons, triggers, 4 axes clamped to ±32767, `packet_number_++` only on change).
- Expose `GetState()` for the server/state-query command (same locked copy).
- An `enabled` flag (cvar, default on) that makes the driver report
  `X_ERROR_DEVICE_NOT_CONNECTED` so a command (`{"cmd":"disable"}` / cvar set) fully
  removes the AI pad without rebuilding.

### 5.4 `RemoteControlServer` (details)

- `Start(host, port, token, state_ptr)`:
  - `socket()` / `bind()` / `listen()` — WinSock on Windows (link `ws2_32`, init once),
    POSIX sockets otherwise. Loopback bind by default.
  - Accept loop (blocking, or `select`/`poll` with a shutdown flag + close-on-wait
    trick); read lines up to a sane cap (e.g. 64 KB).
  - On each command: validate JSON → apply to the timeline → resolve snapshot → reply.
- Timeline (owned by the server thread):
  - `std::deque` of events `{due_time, apply_fn, undo_fn?}` or, simpler, a list of
    `{input_spec, start_time, end_time}` intervals; re-resolve the snapshot whenever a
    timer expires or a command arrives. A background `sleep_until(next_event)` makes the
    release of a timed hold accurate without polling.
  - `now()` = `std::chrono::steady_clock`.
- JSON parsing: **keep a dependency-free hand-rolled minimal parser** (objects/strings/
  numbers/arrays/bools) to avoid adding a third-party lib — the grammar is small and
  fixed. (Alternative if it grows: the project already vendors tomlplusplus, but a
  purpose-built mini-JSON parser for this schema is ~150 lines and keeps CMake simple.)
  Response/serialization: `std::format` string building is fine at this scale.
- Errors never kill the connection: reply `{"ok":false,"error":"..."}` and keep serving.
- Log every accepted command at `REXSYS_INFO`/debug level (audit trail for the AI
  harness — which message caused which crash is gold for debugging).

### 5.5 Protocol

JSON lines over TCP. Every request is one JSON object; every request gets one response
line. Time units are **milliseconds**.

Common response:

```json
{"ok": true, "id": 7}                    // echo request "id" if provided
{"ok": false, "id": 7, "error": "unknown input: ZZZ"}
```

Input names (reuse the exact vocabulary in `FillGuestTarget`, `keyboard_gamepad.h`,
so humans know the names already): `A B X Y LB RB LT RT Up Down Left Right Start Back
L3 R3` plus stick axes `StkLx StkLy StkRx StkRy` (values `-32768..32767`; `StkUp/
StkDown/StkLeft/StkRight` also accepted for max-deflection shorthands).

Commands:

| Command | Fields | Effect |
|---|---|---|
| `{"cmd":"press","input":"A"}` | `input` (required), `hold_ms` (optional) | Press now; auto-release after `hold_ms` (default: stay pressed until `release`/`clear`). |
| `{"cmd":"release","input":"A"}` | `input` | Release that input (cancels any pending auto-release). |
| `{"cmd":"stick","input":"StkLx","value":20000,"hold_ms":120}` | `input` = one of the 4 axes, `value`, `hold_ms` (default 1) | Set an axis to `value` for `hold_ms`, then back to 0. |
| `{"cmd":"state","buttons":[...],"triggers":{"LT":255,"RT":0},"stk":{"lx":0,"ly":1000,"rx":0,"ry":0}}` | any subset | Replace the *persistent* baseline state atomically (sticks are sticky, unlike the keyboard driver's key-hold model). |
| `{"cmd":"clear"}` | — | Release everything remote (baseline + timed). |
| `{"cmd":"script","steps":[...],"id_tag":"jump_over_pit"}` | steps = ordered list; each step: an input op (`press`/`stick`/`state` fragment) + optional `delay_ms` (wait *before* this step) + optional `hold_ms` | Atomically enqueue a sequence. Server returns the total duration. This is the primitive for reproducible bug scripts. |
| `{"cmd":"get_state"}` | — | Reply with current resolved snapshot: `{buttons:["A","RT"], triggers:{...}, sticks:{...}, packet_number, ms_until_release: {A: 120}}`. |
| `{"cmd":"cvar","name":"mouse_look_scale","value":"512"}` | `name`, optional `value` | Get/set any cvar by name via `rex::cvar` — gives the AI access to the existing knob surface (input map, patches toggles, etc.) for free. |
| `{"cmd":"enable"}` / `{"cmd":"disable"}` | — | Toggle the remote pad device (reconnect/disconnect from the guest's view). |
| `{"cmd":"ping"}` | — | `{"ok":true,"ms":<server processing time>}`. |

Examples (what an AI harness would actually send):

```json
{"cmd":"press","input":"Start","hold_ms":120}
{"cmd":"state","buttons":["RB"]}
{"cmd":"stick","input":"StkLx","value":32767,"hold_ms":900}
{"cmd":"release","input":"RB"}
{"cmd":"script","id_tag":"repro_114","steps":[
  {"delay_ms":500,"op":"press","input":"LB","hold_ms":2000},
  {"delay_ms":2100,"op":"press","input":"A","hold_ms":80},
  {"delay_ms":2200,"op":"press","input":"B","hold_ms":80}
]}
{"cmd":"cvar","name":"mouse_look_scale"}
```

A "script" is a single message, so it is **atomic** from the guest's perspective — no
inter-keystroke latency from the network round-trip, which is exactly the property a
debugging harness needs for flaky, timing-sensitive repros.

### 5.6 Client tooling (`tools/fable2_control.py`)

Stdlib-only Python (`socket` + `json`), subcommands mirroring the protocol:

```
python tools/fable2_control.py press A --hold 200
python tools/fable2_control.py state --set-buttons A,RT --ly 1000
python tools/fable2_control.py clear
python tools/fable2_control.py script --file repro.json
python tools/fable2_control.py get-state
python tools/fable2_control.py cvar set mouse_look_scale 512
```

- `--port` / `--token` flags (default 8791).
- Exit code 0/nonzero + machine-parseable output so shell-driven AI loops can use it
  directly; the AI harness can also skip it and speak JSON-lines natively.

## 6. Implementation phases

**Phase 0 — Shared state + driver (no network yet).**
- `remote_input_state.h`, `remote_gamepad_driver.h`; register driver in `OnPreSetup`.
- Expose two cvars (`remote_input_test`) that a human can poke from the F3 console to
  validate end-to-end: e.g. set a test bitmask/axis and watch the in-game menu react.
- Acceptance: with no server running, setting the test cvar makes the guest pad change;
  nothing else in the game behaves differently.

**Phase 1 — Server + minimal commands.**
- `remote_control_server.h` with TCP + JSON-lines; `ping`, `get_state`, `press`,
  `release`, `clear`, `state`.
- `[remote]` config section + cvar seeding; start/stop of the thread.
- `tools/fable2_control.py` for the same set.
- Acceptance: from a second terminal (and with the game window *not* focused), commands
  press/release buttons and set sticks; F3 test cvars and the server agree; no crash on
  repeated connect/disconnect, malformed lines, or 10 KB garbage.

**Phase 2 — Timeline + scripts + cvar passthrough.**
- Timed holds (`hold_ms`), `stick`, `script`, `enable`/`disable`, `cvar` get/set,
  command audit logging.
- Acceptance: a script that reproduces a known human-driven interaction (e.g. navigate
  main menu → new game) works unattended; `get_state` mid-script shows correct pending
  releases; the log file contains the full command history.

**Phase 3 — Integration + docs.**
- README section; default-on in config; smoke test entry (e.g. a `tools/remote_smoke.cmd`
  that launches the game, waits for the port, sends a `ping` + scripted menu nav, and
  reports).
- Acceptance: a fresh build + `tools/fable2.cmd` + one `fable2_control.py` call works
  with zero extra setup.

## 7. Testing / acceptance criteria (whole feature)

1. **Isolation:** with `enabled=false` (or no server) the game behaves byte-for-byte as
   before (no perf regression visible in FPS overlay; input merge identical).
2. **Precision:** a 100 ms hold is active for 100 ± 1 frame of guest time; two 80 ms
   taps separated by 80 ms produce two distinct presses (verify via in-game effect or
   an existing func-trace of the input consumer).
3. **Coexistence:** human keyboard pad + remote pad simultaneously active; a human key
   and a remote button OR-merge correctly.
4. **Robustness:** bad JSON, unknown commands, unknown input names, oversized lines,
   half-closed connections, 50 rapid connect/disconnect cycles — server stays up and
   the game never hangs (no unbounded locks on the guest thread; the mutex is held for
   a struct copy only).
5. **Reproducibility:** the same `script` message, run 5×, produces the same guest
   behavior modulo frame timing; `id_tag` + audit log line up a crash with the commands.
6. **Headless-ish operation:** works when the window is not foreground (proves the
   focus-gating that limits the keyboard driver is absent).

## 8. Closing the AI-debug loop (recommended follow-ups)

Input is the actuation half. For *automatic* investigations the AI also wants cheap
observations over the same channel (all Phase 2-style additions, same transport):

- `{"cmd":"screenshot"}` — grab the rendered frame (the GPU plugin already owns the
  final image; the existing `tools/capgame*.ps1` scripts prove capture works
  externally — exposing it in-process avoids that indirection).
- `{"cmd":"log","since_ms":...}` — tail `logs/` or an in-memory ring of `REXSYS_*`
  lines, so the harness correlates "my command at t=X" with "guest error at t=X+Y".
- `{"cmd":"mem_read","addr":0x83496BD4,"count":4,"u32":true}` and a `mem_write` —
  safe reads/writes of guest memory (reuse the `committed()`/`GuestPtr` machinery from
  `fable_2_app.h`), enabling the AI to assert on game state (reputation, position,
  save-slot state) instead of only watching pixels.
- `{"cmd":"probe","name":"heap_scan"}` — trigger existing probe modules on demand.

These turn the channel from "remote control" into a **test-harness API**:
`set up state → send inputs → assert on memory/screenshot/log`.

## 9. Risks and open questions

- **Xenon input edge semantics:** some game code keys off packet-number edges or
  "newly connected" state. Mitigation: increment `packet_number` only on change
  (as above), and keep the device continuously *connected* rather than
  connect/disconnect per command (`enable`/`disable` is the only path that changes
  connectivity).
- **Windows socket shutdown while guest thread is mid-copy:** low risk (mutex-protected
  struct), but the server thread must be joined before process teardown — use an
  explicit `OnShutdown` hook rather than detach.
- **Port conflict:** if 8791 is taken, log clearly and retry a few ports (8791..8799)
  or fail with a message; make the bound port discoverable via `{"cmd":"info"}` / log line.
- **DoS from localhost:** not a real threat (loopback + token option), but cap the
  event-queue size (e.g. 10k pending timed events) so a malicious script can't OOM us.
- **Android builds:** WinSock vs POSIX branch in the server — keep it a thin `#ifdef`
  seam; the design is otherwise platform-neutral.

## 10. File-level summary

```
NEW  src/remote_input_state.h        ~80   shared snapshot struct + accessors
NEW  src/remote_gamepad_driver.h    ~150   synthetic pad driver (mirrors keyboard_gamepad.h)
NEW  src/remote_control_server.h   ~600   TCP/JSON-lines server + event timeline
NEW  tools/fable2_control.py       ~200   CLI client (stdlib only)
MOD  src/fable_2_app.h                 +~30  register driver, start/stop server, seed cvars
MOD  src/fable2_config.h/.cpp          +~20  [remote] section
MOD  config/fable2_config.toml         +~10  [remote] defaults (kept in sync with .cpp)
MOD  CMakeLists.txt                    +~5
MOD  README.md                          +~40
```
