// fable_2 - ReXGlue Recompiled Project

#include "generated/default/fable_2_init.h"

// F5 -> run external Lua file. Must be included before fable_2_app.h (which
// pulls in keyboard_gamepad.h, which calls fable2::f5lua::poll_f5() per frame).
#include "fable2_f5_lua.h"
#include "fable2_console.h"  // opt-in developer command line

// Photo mode (F6): world freeze, free camera, lens and colour settings,
// screenshots. Defines strong overrides of three recompiled functions and must
// come before fable2_state_probe.h (which chains the pad-read override).
#include "fable2_photo_mode.h"

#include "fable_2_app.h"
#include "fps_meter.h"
#include "fable2_text_probe.h"
#include "fable2_glyph_probe.h"
#include "fable2_hotfuncs.h"
#include "fable2_heap_scan.h"
#include "fable2_text_append.h"
#include "fable2_state_probe.h"
#include "fable2_menu_probe.h"  // main-menu input path (mod-menu hook points)
#include "fable2_modmenu.h"  // 6th "MOD OPTION" selectable main-menu item
#include "fable2_av_probe.h"  // self-installs a guest-PC AV logger (VEH)
#include "fable2_stall_dump.h"  // self-installs a render-stall thread dumper
// #include "fable2_ui_render_probe.h"
#include "keyboard_gamepad.h"

#include <rex/cvar.h>

// Host keyboard -> guest gamepad map (see src/input/keyboard_gamepad.h).
// Format: "Key:Button,Key:Button,...".
// The default layout is NOT baked in here: it lives in the config layer
// (fable2::config::kDefaultKeyboardGamepadMap) and is editable in
// fable2_config.toml under [input]. Fable2App::OnPostInitLogging seeds this
// cvar from the config at startup (only when no higher-priority source set
// it). Override at runtime with --keyboard_gamepad_map "..." or in the
// console; empty string disables the keyboard gamepad.
REXCVAR_DEFINE_STRING(
    keyboard_gamepad_map,
    "",
    "Input",
    "Map host keyboard keys / mouse buttons to guest gamepad input "
    "(Key:Button,...; key names: keyboard keys plus LMB/RMB/MMB and "
    "XMB1/XMB2 mouse buttons; targets: A/B/X/Y, LB/RB, LT/RT, "
    "Up/Down/Left/Right, Pause, Select, L3/R3, StickUp/StickDown/"
    "StickLeft/StickRight). "
    "Default comes from fable2_config.toml [input] keyboard_gamepad_map.");

// Mouse -> right stick (camera look). See src/input/keyboard_gamepad.h.
// The defaults below are a fallback only: Fable2App::OnPostInitLogging
// seeds these cvars from fable2_config.toml [input] at startup (when no
// higher-priority source set them). Keep the values in sync with the
// defaults in fable2::config::Values (src/core/fable2_config.h).
REXCVAR_DEFINE_BOOL(mouse_look, true, "Input",
                    "Map mouse movement to the guest right stick (camera "
                    "look): sweep to look, stop to stop. Default comes from "
                    "fable2_config.toml [input] mouse_look.");
REXCVAR_DEFINE_INT32(mouse_look_scale, 256, "Input",
                     "Mouse-look sensitivity: right-stick units per pixel of "
                     "mouse movement (larger = more sensitive). Default comes "
                     "from fable2_config.toml [input] mouse_look_scale.")
    .range(1, 4096);
REXCVAR_DEFINE_BOOL(mouse_look_smoothing, true, "Input",
                    "Turn the mouse into an even camera turn: report the mouse's "
                    "velocity over one camera update and undo the game's stick "
                    "deadzone and diagonal boost (src/input/mouse_stick.h). false = "
                    "the previous per-poll mapping. Default comes from "
                    "fable2_config.toml [input] mouse_look_smoothing.");
REXCVAR_DEFINE_INT32(mouse_look_window_ms, 0, "Input",
                     "Mouse-look velocity window in milliseconds; 0 = one camera "
                     "update (67 ms, or 33 ms with high_tick_rate). Default comes "
                     "from fable2_config.toml [input] mouse_look_window_ms.")
    .range(0, 500);
REXCVAR_DEFINE_BOOL(mouse_look_diag, false, "Input",
                    "Write mouse-look diagnostics to fable2_mouse.log next to the "
                    "exe: every input poll, camera updates from `f2 mouse trace`, "
                    "and a per-second summary (see src/input/mouse_diag.h). "
                    "Default comes from fable2_config.toml [input] mouse_look_diag.");
// Direct mouse camera (src/input/mouse_direct.h, scripts/recomp/mouse_direct.lua).
REXCVAR_DEFINE_STRING(mouse_look_mode, "direct", "Input",
                      "How the mouse turns the camera: \"direct\" (the gameplay camera "
                      "turns by a fixed angle per count, no top speed or coasting) or "
                      "\"stick\" (the mouse drives the right stick). Default comes from "
                      "fable2_config.toml [input] mouse_look_mode.");
REXCVAR_DEFINE_DOUBLE(mouse_look_sens_x, 0.06, "Input",
                      "Direct mouse camera: yaw degrees per count of mouse movement "
                      "(times mouse_look_scale / 256 and the in-game camera sensitivity).");
REXCVAR_DEFINE_DOUBLE(mouse_look_sens_y, 0.06, "Input",
                      "Direct mouse camera: up/down sensitivity, as degrees per count "
                      "(moves the camera height; times mouse_look_scale / 256).");
REXCVAR_DEFINE_BOOL(mouse_look_invert_y, false, "Input",
                    "Direct mouse camera: true = moving the mouse up makes the camera look down.");
REXCVAR_DEFINE_DOUBLE(mouse_look_direct_smoothing, 0.0, "Input",
                      "Direct mouse camera: share of the mouse movement held back at each "
                      "camera update for the following ones (0 = none, up to 0.9).");

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

// Skip the default.xex SHA-256 integrity check at startup (see
// Fable2App::OnLoadXexImage). When set, the XEX hash is still computed and
// written to the log, but a mismatch (or an unreadable file) no longer blocks
// the launch. Useful for running a modified or non-catalogue default.xex.
REXCVAR_DEFINE_BOOL(skipHash, false, "Launch",
                    "Skip the default.xex SHA-256 integrity check. The hash is "
                    "still logged, but a mismatch no longer blocks the launch "
                    "(useful for a modified or non-catalogue XEX). Pass "
                    "--skipHash on the command line.");

#ifdef _WIN32
// Hybrid-graphics laptops (Intel/AMD iGPU + NVIDIA/AMD dGPU): ask the drivers
// to run us on the discrete GPU. Without this Windows may hand the game the
// integrated GPU, which hits D3D12 DEVICE_HUNG (TDR) shortly after boot
// (issue #42). These must be exported from the .exe itself; a user's own
// per-app setting in Windows Graphics settings / NVIDIA Control Panel still
// takes priority.
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

REX_DEFINE_APP(fable_2, Fable2App::Create)
