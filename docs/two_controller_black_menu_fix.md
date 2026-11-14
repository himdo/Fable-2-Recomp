# Fix: Black main menu when a second controller presses a button

**Date:** 2026-10-21
**Symptom:** With 2 controllers plugged in at launch, pressing **A on the
second controller** on the front-end (title / main menu) turned the menu
black. One controller (or the second pad navigating only) was fine.

## Root cause

The recompiled guest processes input events **per user slot**. Its input
manager polls `XamInputGetState` for all 4 slots
(trampoline `InputGetPadState_822B2D60`), and the front-end
menu (`FrontMenu_PollEventQueue` → `FrontMenu_InputEvent` →
`FrontMenu_PressBitDispatch`) dispatches a press for *whichever slot
pressed it*.

On real hardware a plugged-in-but-unsigned controller is an **anonymous
user**: `XamUserGetSigninState(1)` → ANONYMOUS, `XamUserGetXUID(1)` →
SUCCESS with XUID 0. This runtime hosts exactly **one signed-in profile
(user 0)**; every user-1 query instead returns SIGNED_OUT /
`X_E_NO_SUCH_USER` (see `src/kernel/xam/xam_user.cpp`). The guest
front-end cannot complete its user-1 primary-player path against those
results, so the menu transition dies and the screen renders black (no
guest exception — the 2026-10-20 run logs are clean).

Func-trace evidence (`fable2_func_trace.log`, press-moment diff of a
bugged vs working session): the menu input thread takes a different
branch immediately after the press — working run proceeds through
`0x82189058`/`0x822B3848`/`0x821895B0` and later reaches the
`XamUserGetXUID` wrapper path (`sub_822F9408` → `0x82CBC440`); the bugged
run diverts into `0x8218C500`/`0x822B36F8`/`0x822BA4A8` and never reaches
the XUID flow.

## Fix

Guest-facing **primary-slot remap** in
`src/kernel/xam/xam_input.cpp` (host convenience, not console behavior):

- On every **button press edge** (a bit 0→down on any host slot), that
  host slot is swapped into **guest user slot 0** — "whoever is pressing
  is player 1", matching how the Fable 2 front-end actually plays.
- The swap is per press-edge (held buttons don't re-swap), so two pads
  don't ping-pong while both are pressed.
- The poll that observes the edge is re-queried against the
  now-swapped-in slot, so the guest sees a consistent post-swap frame:
  the press registers at user 0 on the next poll instead of leaking
  through as a user-N press (which is the bug trigger).
- All `XamInput*` entry points (GetState / GetCapabilities(Ex) /
  SetState / GetKeystroke(Ex)) route guest user indices through the
  mapping. `XamUser*` profile/signin APIs are untouched — they stay
  anchored to guest user 0, which always has the real profile.
- The displaced pad remains visible to the guest as another user, so the
  menu still sees two connected controllers, as on real hardware.

## Verification

- Repro (2 pads at launch, A on pad 2 at the main menu) no longer blacks
  the menu. Log confirms the swap fired at the press:
  ```
  [XAM] primary-slot remap: press edge on host slot 1 -
  guest user 0 now maps to host slot 1 (was slot 0)
  ```
  and swaps back on a pad-1 press.
- A/B: `FABLE2_PRIMARY_SLOT_REMAP=0` disables the remap (original buggy
  behavior returns).

## Notes / trade-offs

- Not real-console semantics: XInput slots are fixed by the hardware, and
  a game can see a press at any user slot. Acceptable for this project —
  the alternative (full anonymous-user emulation: ANONYMOUS signin state,
  XUID 0, `XamEnumerate` entries, sign-in notifications) is a much larger
  change and no game in scope needs it.
- The mapping is process-lifetime and only changes on press edges;
  unplug/replug can leave a stale mapping (reset by restarting the game).
- A button tap shorter than one guest poll frame (~30 ms) at the exact
  swap moment may register late by one frame; any real hold survives.
- Keyboard/mouse shim is unaffected: synthetic pad presses flow through
  the same XAM entry points and participate in the remap like any other
  slot.
- Companion diagnostic kept in-tree: `FABLE2_XAM_TRACE=1` logs all
  `XamUser*`/`XamInput*`/`XamEnumerate` calls (args + results) to
  `fable2_xam_trace.log` next to the exe
  (`src/kernel/xam/xam_diag.h` + hooks in `xam_user.cpp`,
  `xam_input.cpp`, `xam_enum.cpp`).
