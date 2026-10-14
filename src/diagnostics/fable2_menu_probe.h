// fable2_menu_probe.h - strong overrides for the main-menu input path.
//
// Funnel the menu input functions through fable2::uir::hook() so the
// render-probe infrastructure (timeline + FABLE2_UIR_IN_DUMP input dumps)
// can observe them:
//
//   - InputEvent_PollQueue (0x82185C90): per-frame UI input-event queue
//     poller; calls the per-press handler (InputEvent_Handle) once per
//     pending button press (Up/Down/A/Back).
//   - FrontMenu_EnsureElement (0x82C217B8): get-or-create UI element node
//     (fires on effective actions: selection change / activation).
//   - InputEvent_Handle (0x82BE3F30): the per-press handler itself.
//
// NOTE: InputEvent_DispatchButtonPress (0x82188F20, the raw press bit-dispatch)
// is overridden in fable2_modmenu.h instead, which layers the mod-menu mirror
// index / activation on top of the stock body.
//
// These same overrides are the future mod-menu hook points; they are cheap
// (one atomic window check per call) and active in all debug builds.

#pragma once

#include "fable2_ui_render_probe.h"

extern "C" void __imp__InputEvent_Handle(PPCContext& ctx, uint8_t* base);
extern "C" void InputEvent_Handle(PPCContext& ctx, uint8_t* base) {
  if (fable2::uir::hook("InputEvent_Handle", ctx, base)) return;
  __imp__InputEvent_Handle(ctx, base);
}

extern "C" void __imp__InputEvent_PollQueue(PPCContext& ctx,
                                                uint8_t* base);
extern "C" void InputEvent_PollQueue(PPCContext& ctx, uint8_t* base) {
  if (fable2::uir::hook("InputEvent_PollQueue", ctx, base)) return;
  __imp__InputEvent_PollQueue(ctx, base);
}

extern "C" void __imp__FrontMenu_EnsureElement(PPCContext& ctx,
                                               uint8_t* base);
extern "C" void FrontMenu_EnsureElement(PPCContext& ctx, uint8_t* base) {
  if (fable2::uir::hook("FrontMenu_EnsureElement", ctx, base)) return;
  __imp__FrontMenu_EnsureElement(ctx, base);
}
