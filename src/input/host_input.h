// Host keyboard / mouse state for the recomp's own input paths (the keyboard
// gamepad, F5 Lua, the debug console's F9 pause), on every platform.
//
// Windows keeps the original direct polling (GetAsyncKeyState, the cursor
// recentering in keyboard_gamepad.h). Elsewhere there is no global key query,
// so the state is tracked from the game window's own input events, the same
// way the SDK's MnK driver does it: a listener registered on the SDK window
// (above every other listener, and never consuming events) records held keys
// and mouse buttons by virtual key and accumulates relative mouse motion, and
// mouse look locks the pointer with the window's relative mouse mode.
//
// The listener callbacks run on the UI thread and the queries come from guest
// threads, so all shared state is atomic.

#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#else
#include <rex/ui/ui_event.h>
#include <rex/ui/virtual_key.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>
#include <rex/ui/windowed_app_context.h>
#endif

namespace fable2::hostinput {

#ifdef _WIN32

inline bool IsKeyDown(uint16_t vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

#else

class Tracker final : public rex::ui::WindowInputListener, public rex::ui::WindowListener {
 public:
  // Above every other listener, so keys are seen even while the debug menu
  // (ImGui) is open and handles them, and key-ups are never missed.
  static constexpr size_t kZOrder = size_t(1) << 40;

  // Attaches to the window on the UI thread the first time it is called with
  // a window. Safe to call every poll.
  void EnsureAttached(rex::ui::Window* window) {
    if (!window || attach_requested_.exchange(true, std::memory_order_acq_rel)) {
      return;
    }
    window->app_context().CallInUIThreadDeferred([this, window] {
      window_.store(window, std::memory_order_release);
      has_focus_.store(window->HasFocus(), std::memory_order_relaxed);
      window->AddInputListener(this, kZOrder);
      window->AddListener(this);
    });
  }

  bool IsKeyDown(uint16_t vk) const {
    return vk < keys_.size() && keys_[vk].load(std::memory_order_relaxed);
  }

  bool HasFocus() const { return has_focus_.load(std::memory_order_relaxed); }

  // Requests the pointer lock (relative mouse mode, cursor hidden) on or off.
  // Applied on the UI thread; repeated requests for the same state are free.
  void SetMouseLook(bool on) {
    if (look_requested_.exchange(on, std::memory_order_acq_rel) == on) {
      return;
    }
    rex::ui::Window* window = window_.load(std::memory_order_acquire);
    if (!window) {
      look_requested_.store(false, std::memory_order_relaxed);
      return;
    }
    window->app_context().CallInUIThreadDeferred([this] { ApplyMouseLookFromUIThread(); });
  }

  // Mouse motion since the last call, in window pixels (dx > 0 = right,
  // dy > 0 = down).
  void TakeMouseDelta(int32_t& dx, int32_t& dy) {
    dx = mouse_dx_.exchange(0, std::memory_order_relaxed);
    dy = mouse_dy_.exchange(0, std::memory_order_relaxed);
  }

  // WindowInputListener (UI thread).
  void OnKeyDown(rex::ui::KeyEvent& e) override { SetKey(uint16_t(e.virtual_key()), true); }
  void OnKeyUp(rex::ui::KeyEvent& e) override { SetKey(uint16_t(e.virtual_key()), false); }
  void OnMouseDown(rex::ui::MouseEvent& e) override { SetKey(ButtonKey(e.button()), true); }
  void OnMouseUp(rex::ui::MouseEvent& e) override { SetKey(ButtonKey(e.button()), false); }
  void OnMouseMove(rex::ui::MouseEvent& e) override {
    if (!relative_mode_) {
      // No pointer lock: only the absolute position moves, so diff it.
      if (have_prev_position_) {
        mouse_dx_.fetch_add(e.x() - prev_x_, std::memory_order_relaxed);
        mouse_dy_.fetch_add(e.y() - prev_y_, std::memory_order_relaxed);
      }
      prev_x_ = e.x();
      prev_y_ = e.y();
      have_prev_position_ = true;
      return;
    }
    // Sub-pixel motion is carried over so slow sweeps aren't lost.
    rel_carry_x_ += e.dx();
    rel_carry_y_ += e.dy();
    const int32_t dx = int32_t(rel_carry_x_);
    const int32_t dy = int32_t(rel_carry_y_);
    rel_carry_x_ -= float(dx);
    rel_carry_y_ -= float(dy);
    mouse_dx_.fetch_add(dx, std::memory_order_relaxed);
    mouse_dy_.fetch_add(dy, std::memory_order_relaxed);
  }

  // WindowListener (UI thread).
  void OnGotFocus(rex::ui::UISetupEvent&) override {
    has_focus_.store(true, std::memory_order_relaxed);
  }
  void OnLostFocus(rex::ui::UISetupEvent&) override {
    has_focus_.store(false, std::memory_order_relaxed);
    // Key-ups are not delivered to an unfocused window, so drop everything.
    for (auto& key : keys_) key.store(false, std::memory_order_relaxed);
    mouse_dx_.store(0, std::memory_order_relaxed);
    mouse_dy_.store(0, std::memory_order_relaxed);
    look_requested_.store(false, std::memory_order_relaxed);
    ApplyMouseLookFromUIThread();
  }
  void OnClosing(rex::ui::UIEvent&) override {
    if (rex::ui::Window* window = window_.exchange(nullptr, std::memory_order_acq_rel)) {
      ReleaseMouseLook(window);
      window->RemoveInputListener(this);
      window->RemoveListener(this);
    }
  }

 private:
  static uint16_t ButtonKey(rex::ui::MouseEvent::Button button) {
    using B = rex::ui::MouseEvent::Button;
    using V = rex::ui::VirtualKey;
    switch (button) {
      case B::kLeft:
        return uint16_t(V::kLButton);
      case B::kRight:
        return uint16_t(V::kRButton);
      case B::kMiddle:
        return uint16_t(V::kMButton);
      case B::kX1:
        return uint16_t(V::kXButton1);
      case B::kX2:
        return uint16_t(V::kXButton2);
      default:
        return 0;
    }
  }

  void SetKey(uint16_t vk, bool down) {
    if (vk != 0 && vk < keys_.size()) keys_[vk].store(down, std::memory_order_relaxed);
  }

  void ApplyMouseLookFromUIThread() {
    rex::ui::Window* window = window_.load(std::memory_order_acquire);
    if (!window) return;
    const bool want = look_requested_.load(std::memory_order_relaxed);
    if (want == look_applied_) return;
    if (!want) {
      ReleaseMouseLook(window);
      return;
    }
    look_applied_ = true;
    window->SetCursorVisibility(rex::ui::Window::CursorVisibility::kHidden);
    window->CaptureMouse();
    relative_mode_ = window->SetRelativeMouseMode(true);
    rel_carry_x_ = rel_carry_y_ = 0.0f;
    have_prev_position_ = false;
    mouse_dx_.store(0, std::memory_order_relaxed);
    mouse_dy_.store(0, std::memory_order_relaxed);
  }

  void ReleaseMouseLook(rex::ui::Window* window) {
    if (!look_applied_) return;
    look_applied_ = false;
    window->SetRelativeMouseMode(false);
    relative_mode_ = false;
    window->ReleaseMouse();
    window->SetCursorVisibility(rex::ui::Window::CursorVisibility::kVisible);
  }

  std::array<std::atomic<bool>, 256> keys_{};
  std::atomic<bool> has_focus_{false};
  std::atomic<bool> attach_requested_{false};
  std::atomic<bool> look_requested_{false};
  std::atomic<int32_t> mouse_dx_{0};
  std::atomic<int32_t> mouse_dy_{0};
  // Written on the UI thread, read by SetMouseLook to reach it.
  std::atomic<rex::ui::Window*> window_{nullptr};
  // UI thread only.
  bool look_applied_ = false;
  bool relative_mode_ = false;
  bool have_prev_position_ = false;
  int32_t prev_x_ = 0, prev_y_ = 0;
  float rel_carry_x_ = 0.0f, rel_carry_y_ = 0.0f;
};

inline Tracker& T() {
  static Tracker tracker;
  return tracker;
}

inline bool IsKeyDown(uint16_t vk) { return T().IsKeyDown(vk); }

#endif  // _WIN32

}  // namespace fable2::hostinput
