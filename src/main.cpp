// fable_2 - ReXGlue Recompiled Project

#include "generated/default/fable_2_init.h"

#include "fable_2_app.h"
#include "keyboard_gamepad.h"

#include <rex/cvar.h>

// Host keyboard -> guest gamepad map (see src/keyboard_gamepad.h).
// Format: "Key:Button,Key:Button,...". Default layout:
//   E=A  2=B  1=X  3=Y   WASD=left stick   Escape=pause(Start)  M=select(Back)
//   Q=left trigger  Tab=right trigger  F1/F2/F3/F4 = dpad Up/Down/Left/Right
// Override at runtime with --keyboard_gamepad_map "..." or in the console.
REXCVAR_DEFINE_STRING(
    keyboard_gamepad_map,
    "E:A,2:B,1:X,3:Y,"
    "W:StickUp,S:StickDown,A:StickLeft,D:StickRight,"
    "Escape:Pause,M:Select,Q:LT,Tab:RT,"
    "F1:Up,F2:Down,F3:Left,F4:Right",
    "Input",
    "Map host keyboard keys to guest gamepad input "
    "(Key:Button,...; targets: A/B/X/Y, LB/RB, LT/RT, Up/Down/Left/Right, "
    "Pause, Select, L3/R3, StickUp/StickDown/StickLeft/StickRight)");

// Mouse -> right stick (camera look). See src/keyboard_gamepad.h.
REXCVAR_DEFINE_BOOL(mouse_look, true, "Input",
                    "Map mouse movement to the guest right stick (camera "
                    "look): sweep to look, stop to stop.");
REXCVAR_DEFINE_INT32(mouse_look_scale, 256, "Input",
                     "Mouse-look sensitivity: right-stick units per pixel of "
                     "mouse movement (larger = more sensitive).")
    .range(1, 4096);

// Key that toggles the debug menu (F4). While the menu is open the mouse lock
// is released (free cursor); closing it re-locks. This key is excluded from
// the gamepad map so it doesn't double as a button. Set empty to always lock
// while focused (no menu-based unlock).
REXCVAR_DEFINE_STRING(
    mouse_unlock_key,
    "F4",
    "Input",
    "Key that toggles the debug menu; while the menu is open the mouse lock is "
    "released (free cursor) and re-engaged when it closes. Empty = always lock "
    "while focused. This key is excluded from the gamepad map.");

REX_DEFINE_APP(fable_2, Fable2App::Create)
