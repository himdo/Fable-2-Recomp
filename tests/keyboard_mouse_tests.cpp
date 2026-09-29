#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace fable2::f5lua {
inline int requests = 0;
inline void request_run() { ++requests; }
}
#include "keyboard_gamepad.h"
#include "fable2_config.h"
#include <rex/ui/sdl_virtual_key.h>

REXCVAR_DEFINE_STRING(keyboard_gamepad_map, "", "Input", "Test map");
REXCVAR_DEFINE_BOOL(mouse_look, true, "Input", "Test mouse");
REXCVAR_DEFINE_INT32(mouse_look_scale, 256, "Input", "Test sensitivity");
REXCVAR_DEFINE_STRING(mouse_unlock_key, "F4", "Input", "Test unlock");

#define CHECK(condition) do { if (!(condition)) { \
  std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
  std::abort(); } } while (false)

// A real SDK Window event dispatcher with no desktop dependency. Cursor
// operations assert UI-thread affinity; input polling also runs off-thread.
class Context : public rex::ui::WindowedAppContext {
  void NotifyUILoopOfPendingFunctions() override {}
  void PlatformQuitFromUIThread() override {}
};
class Window : public rex::ui::Window {
 public:
  explicit Window(Context& context) : rex::ui::Window(context, "Input test", 1280, 720) {}
  ~Window() override { EnterDestructor(); }
  bool relative = false;
  bool relative_supported = true;
  bool SetRelativeMouseMode(bool enable) override {
    CHECK(app_context().IsInUIThread());
    relative = enable && relative_supported;
    return relative;
  }
  void Focused(bool focused) {
    WindowDestructionReceiver receiver(this);
    OnFocusUpdate(focused, receiver);
  }
  void Key(rex::ui::VirtualKey key, bool down, bool repeat = false,
           bool shift_still_held = false) {
    rex::ui::KeyEvent event(this, key, 1, repeat, shift_still_held, false, false, false);
    WindowDestructionReceiver receiver(this);
    if (down) OnKeyDown(event, receiver); else OnKeyUp(event, receiver);
  }
  void Motion(float dx, float dy) {
    rex::ui::MouseEvent event(this, rex::ui::MouseEvent::Button::kNone, 0, 0, 0, 0, dx, dy);
    WindowDestructionReceiver receiver(this);
    OnMouseMove(event, receiver);
  }
  void Button(rex::ui::MouseEvent::Button button, bool down) {
    rex::ui::MouseEvent event(this, button, 0, 0);
    WindowDestructionReceiver receiver(this);
    if (down) OnMouseDown(event, receiver); else OnMouseUp(event, receiver);
  }
 protected:
  bool OpenImpl() override { return true; }
  void RequestCloseImpl() override {}
  std::unique_ptr<rex::ui::Surface> CreateSurfaceImpl(rex::ui::Surface::TypeFlags) override {
    return nullptr;
  }
  void RequestPaintImpl() override {}
};

int main() {
  using rex::X_RESULT;
  using K = rex::ui::VirtualKey;
  using B = rex::input::X_INPUT_GAMEPAD_BUTTON;
  using MB = rex::ui::MouseEvent::Button;
  Context context;
  Window window(context);
  CHECK(window.Open());
  REXCVAR_SET(keyboard_gamepad_map, std::string(fable2::config::kDefaultKeyboardGamepadMap));
  {
    fable2::KeyboardGamepadDriver driver(nullptr, 0);
    driver.OnWindowAvailable(&window);
    std::vector<rex::input::DeviceInfo> devices;
    driver.EnumerateDevices(devices);
    CHECK(devices.size() == 1 && devices[0].synthetic);
    auto poll = [&] {
      rex::input::X_INPUT_STATE state{};
      CHECK(driver.GetDeviceState(devices[0].id, &state) == X_ERROR_SUCCESS);
      return state;
    };
    CHECK(poll().gamepad.buttons == 0);
    window.Focused(true);
    CHECK(window.relative);
    window.Key(K::kE, true);
    window.Key(K::kW, true);
    window.Key(K::kD, true);
    window.Key(K::kQ, true);
    window.Key(K::kTab, true);
    auto state = poll();
    CHECK(state.gamepad.buttons & B::X_INPUT_GAMEPAD_A);
    CHECK(state.gamepad.thumb_ly == 32767 && state.gamepad.thumb_lx == 32767);
    CHECK(state.gamepad.left_trigger == 255 && state.gamepad.right_trigger == 255);
    window.Key(K::kS, true);
    window.Key(K::kA, true);
    CHECK(poll().gamepad.thumb_ly == 0 && poll().gamepad.thumb_lx == 0);
    window.Key(K::kE, false);
    CHECK(poll().gamepad.buttons == 0);
    window.Motion(0.25f, -0.5f);
    window.Motion(0.75f, -0.5f);
    state = poll();
    CHECK(state.gamepad.thumb_rx == 256 && state.gamepad.thumb_ry == 256);
    CHECK(poll().gamepad.thumb_rx == 0 && poll().gamepad.thumb_ry == 0);
    window.Motion(1e8f, 1e8f);
    state = poll();
    CHECK(state.gamepad.thumb_rx == 32767 && state.gamepad.thumb_ry == -32767);
    window.Key(K::kF4, true);
    CHECK(!window.relative);
    window.Key(K::kF4, true, true);
    CHECK(!window.relative);
    CHECK(!(poll().gamepad.buttons & B::X_INPUT_GAMEPAD_DPAD_RIGHT));
    window.Motion(50, 50);
    CHECK(poll().gamepad.thumb_rx == 0);
    window.Key(K::kF4, false);
    window.Key(K::kF4, true);
    CHECK(window.relative);
    window.Key(K::kF4, false);
    window.Key(K::kF5, true);
    window.Key(K::kF5, true, true);
    CHECK(fable2::f5lua::requests == 1);
    window.Key(K::kF5, false);
    window.Key(K::kF5, true);
    CHECK(fable2::f5lua::requests == 2);
    window.Motion(20, 20);
    window.Focused(false);
    CHECK(!window.relative);
    state = poll();
    CHECK(state.gamepad.buttons == 0 && state.gamepad.thumb_lx == 0);
    CHECK(state.gamepad.left_trigger == 0 && state.gamepad.thumb_rx == 0);
    window.Key(K::kE, true);
    window.Key(K::kF5, true);
    CHECK(fable2::f5lua::requests == 2);
    window.Focused(true);
    CHECK(poll().gamepad.buttons == 0 && poll().gamepad.thumb_ly == 0);
    REXCVAR_SET(keyboard_gamepad_map, "Space:B,Shift:LT,Control:RT,LMB:X,invalid:A");
    window.Key(K::kSpace, true);
    const auto shift = rex::ui::TranslateSDLScancode(SDL_SCANCODE_LSHIFT);
    window.Key(shift, true);
    window.Key(rex::ui::TranslateSDLScancode(SDL_SCANCODE_RCTRL), true);
    window.Button(MB::kLeft, true);
    state = poll();
    CHECK(state.gamepad.buttons == (B::X_INPUT_GAMEPAD_B | B::X_INPUT_GAMEPAD_X));
    CHECK(state.gamepad.left_trigger == 255 && state.gamepad.right_trigger == 255);
    window.Key(shift, false, false, true);
    CHECK(poll().gamepad.left_trigger == 255);
    window.Key(shift, false);
    CHECK(poll().gamepad.left_trigger == 0);
    window.Key(K::kControl, false);
    window.Button(MB::kLeft, false);
    CHECK(poll().gamepad.right_trigger == 0);
    CHECK(poll().gamepad.buttons == B::X_INPUT_GAMEPAD_B);
    REXCVAR_SET(mouse_look, false);
    poll();
    CHECK(!window.relative);
    window.Motion(30, 30);
    CHECK(poll().gamepad.thumb_rx == 0);
    REXCVAR_SET(mouse_look, true);
    // UI work queued by the guest thread must release the cursor on teardown.
    std::thread guest([&] { poll(); });
    guest.join();
    context.ExecutePendingFunctionsFromUIThread();
    CHECK(window.relative);
  }
  CHECK(!window.relative);

  using Input = fable2::input_detail::WindowKeyboardMouse;
  Input::Options options{true, false, uint16_t(K::kF4)};
  auto input = std::make_shared<Input>([&] { return options; }, [] {});
  input->Attach(&window);
  CHECK(window.relative);
  window.Key(K::kE, true);
  options.overlay_captures_input = true;
  input->RequestRefresh();
  CHECK(!window.relative && !input->Consume().active);
  window.Key(K::kW, true);
  options.overlay_captures_input = false;
  input->RequestRefresh();
  auto snapshot = input->Consume();
  CHECK(window.relative && snapshot.active);
  CHECK(!snapshot.Down(uint16_t(K::kE)) && !snapshot.Down(uint16_t(K::kW)));
  // Closing the settings UI with its mouse close button also clears F4.
  window.Key(K::kF4, true);
  window.Key(K::kF4, false);
  options.overlay_captures_input = true;
  input->RequestRefresh();
  options.overlay_captures_input = false;
  input->RequestRefresh();
  CHECK(window.relative);
  double total = 0;
  std::atomic<bool> done{false};
  std::thread consumer([&] {
    while (!done) total += input->Consume().dx;
    total += input->Consume().dx;
  });
  for (int i = 0; i < 1000; ++i) window.Motion(0.25f, 0);
  done = true;
  consumer.join();
  CHECK(total == 250);
  std::thread detach([&] { input->RequestRefresh(); input->RequestDetach(); });
  detach.join();
  input.reset();
  context.ExecutePendingFunctionsFromUIThread();
  CHECK(!window.relative);
  std::puts("Keyboard/mouse regression checks passed.");
}
