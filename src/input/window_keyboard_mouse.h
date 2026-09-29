#pragma once

#include <array>
#include <atomic>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>

#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>

namespace fable2::input_detail {

// SDL-backed Window events already use the SDK's portable virtual keys.
// All window operations stay on the UI thread. Guest threads only consume a
// locked snapshot, and queued UI work owns the listener until it is detached.
class WindowKeyboardMouse final
    : public rex::ui::WindowListener,
      public rex::ui::WindowInputListener,
      public std::enable_shared_from_this<WindowKeyboardMouse> {
 public:
  struct Options {
    bool mouse_look = true;
    bool overlay_captures_input = false;
    uint16_t unlock_key = 0;
  };
  struct Snapshot {
    std::array<bool, 256> keys{};
    bool active = false;
    double dx = 0, dy = 0;
    bool Down(uint16_t key) const {
      using K = rex::ui::VirtualKey;
      if (key == uint16_t(K::kShift))
        return keys[key] || keys[uint16_t(K::kLShift)] || keys[uint16_t(K::kRShift)];
      if (key == uint16_t(K::kControl))
        return keys[key] || keys[uint16_t(K::kLControl)] || keys[uint16_t(K::kRControl)];
      if (key == uint16_t(K::kMenu))
        return keys[key] || keys[uint16_t(K::kLMenu)] || keys[uint16_t(K::kRMenu)];
      return key < keys.size() && keys[key];
    }
  };

  WindowKeyboardMouse(std::function<Options()> options,
                      std::function<void()> request_lua)
      : options_(std::move(options)), request_lua_(std::move(request_lua)) {}

  // Called by InputSystem::AttachWindow on the UI thread.
  void Attach(rex::ui::Window* window) {
    if (window_ == window) return;
    Detach();
    window_ = window;
    if (!window_) return;
    context_ = &window_->app_context();
    window_->AddListener(this);
    // Observe releases even when an overlay consumes the event. Never mark
    // events handled: SDK hotkeys and text entry must continue to work.
    window_->AddInputListener(this, std::numeric_limits<size_t>::max());
    focused_ = window_->HasFocus();
    Refresh();
  }

  void RequestDetach() {
    if (context_) {
      context_->CallInUIThread([self = shared_from_this()] { self->Detach(); });
    }
  }

  void RequestRefresh() {
    if (!context_ || refresh_pending_.exchange(true)) return;
    if (!context_->CallInUIThread([self = shared_from_this()] {
          self->refresh_pending_ = false;
          self->Refresh();
        })) {
      refresh_pending_ = false;
    }
  }

  Snapshot Consume() {
    std::lock_guard lock(mutex_);
    auto result = snapshot_;
    snapshot_.dx = snapshot_.dy = 0;
    return result;
  }

  void OnGotFocus(rex::ui::UISetupEvent&) override {
    focused_ = true;
    Clear();
    Refresh();
  }
  void OnLostFocus(rex::ui::UISetupEvent&) override {
    focused_ = false;
    Clear();
    Refresh();
  }
  void OnClosing(rex::ui::UIEvent&) override { Detach(); }

  void OnKeyDown(rex::ui::KeyEvent& event) override {
    Refresh();
    if (!focused_) return;
    const auto key = uint16_t(event.virtual_key());
    const bool edge = SetKey(key, true) && !event.prev_state();
    if (!edge) return;
    if (key == uint16_t(rex::ui::VirtualKey::kF5)) request_lua_();
    if (key && key == options_().unlock_key) {
      manual_release_ = !manual_release_;
      Refresh();
    }
  }
  void OnKeyUp(rex::ui::KeyEvent& event) override {
    // SDL maps both left/right modifiers to one virtual key. Releasing one
    // side must not release the binding while the other side remains held.
    using K = rex::ui::VirtualKey;
    const auto key = event.virtual_key();
    const bool down = (key == K::kShift && event.is_shift_pressed()) ||
                      (key == K::kControl && event.is_ctrl_pressed()) ||
                      (key == K::kMenu && event.is_alt_pressed());
    SetKey(uint16_t(key), focused_ && down);
  }
  void OnMouseDown(rex::ui::MouseEvent& event) override {
    Refresh();
    if (focused_) SetKey(MouseKey(event.button()), true);
  }
  void OnMouseUp(rex::ui::MouseEvent& event) override {
    SetKey(MouseKey(event.button()), false);
  }
  void OnMouseMove(rex::ui::MouseEvent& event) override {
    Refresh();
    if (!relative_) return;
    std::lock_guard lock(mutex_);
    snapshot_.dx += event.dx();
    snapshot_.dy += event.dy();
  }

 private:
  static uint16_t MouseKey(rex::ui::MouseEvent::Button button) {
    using B = rex::ui::MouseEvent::Button;
    using K = rex::ui::VirtualKey;
    switch (button) {
      case B::kLeft: return uint16_t(K::kLButton);
      case B::kRight: return uint16_t(K::kRButton);
      case B::kMiddle: return uint16_t(K::kMButton);
      case B::kX1: return uint16_t(K::kXButton1);
      case B::kX2: return uint16_t(K::kXButton2);
      default: return 0;
    }
  }
  bool SetKey(uint16_t key, bool down) {
    std::lock_guard lock(mutex_);
    if (!key || key >= snapshot_.keys.size()) return false;
    const bool edge = down && !snapshot_.keys[key];
    snapshot_.keys[key] = down;
    return edge;
  }
  void Clear() {
    std::lock_guard lock(mutex_);
    snapshot_ = {};
  }
  void Refresh() {
    if (!window_) return;
    const auto options = options_();
    // Closing an overlay with its close button must release the F4 latch too.
    if (overlay_was_open_ && !options.overlay_captures_input)
      manual_release_ = false;
    overlay_was_open_ = options.overlay_captures_input;
    const bool active = focused_ && !options.overlay_captures_input;
    const bool looking = active && options.mouse_look && !manual_release_;
    if (looking != relative_) {
      relative_ = window_->SetRelativeMouseMode(looking);
      window_->SetCursorVisibility(relative_
          ? rex::ui::Window::CursorVisibility::kHidden
          : rex::ui::Window::CursorVisibility::kVisible);
      std::lock_guard lock(mutex_);
      snapshot_.dx = snapshot_.dy = 0;
    }
    std::lock_guard lock(mutex_);
    if (snapshot_.active != active) snapshot_ = {};
    snapshot_.active = active;
  }
  void Detach() {
    if (window_) {
      if (relative_) window_->SetRelativeMouseMode(false);
      window_->SetCursorVisibility(rex::ui::Window::CursorVisibility::kVisible);
      window_->RemoveInputListener(this);
      window_->RemoveListener(this);
      window_ = nullptr;
    }
    focused_ = relative_ = manual_release_ = overlay_was_open_ = false;
    Clear();
  }

  std::function<Options()> options_;
  std::function<void()> request_lua_;
  rex::ui::Window* window_ = nullptr;  // UI thread only
  rex::ui::WindowedAppContext* context_ = nullptr;
  bool focused_ = false, relative_ = false;
  bool manual_release_ = false, overlay_was_open_ = false;
  std::atomic<bool> refresh_pending_{false};
  std::mutex mutex_;
  Snapshot snapshot_;
};

}  // namespace fable2::input_detail
