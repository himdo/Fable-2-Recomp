// Mouse wheel → mouseaim::Wheel() (the wheel target hop of direct mouse aim,
// src/input/mouse_aim.h). The keyboard/mouse pad polls the cursor with
// GetCursorPos and has no wheel, so this listens to the window's input events.
//
// Registered above every other input listener, and it never marks the event
// handled: ImGui (photo mode zoom, the debug menu) still gets every notch.
// A notch only counts while aiming with a target highlighted; anything older
// than kWheelFreshUs when the cursor update next runs is dropped.
#pragma once

#include <cstddef>
#include <limits>

#include <rex/ui/ui_event.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>

#include "mouse_aim.h"
#include "mouse_diag.h"  // fable2::mousediag::NowUs

namespace fable2::mouseaim {

class WheelListener final : public rex::ui::WindowInputListener {
 public:
  void OnMouseWheel(rex::ui::MouseEvent& e) override {
    if (e.scroll_y() != 0) Wheel().Feed(e.scroll_y(), fable2::mousediag::NowUs());
  }
};

inline void InstallWheelListener(rex::ui::Window* window) {
  if (!window) return;
  static WheelListener listener;
  // Input listeners run from the highest z-order down until one handles the
  // event; this one never does, so it goes first and passes everything on.
  window->AddInputListener(&listener, std::numeric_limits<size_t>::max() / 2);
}

}  // namespace fable2::mouseaim
