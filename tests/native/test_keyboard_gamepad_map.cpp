// Synthetic standalone test for the keyboard/mouse -> guest gamepad map
// parsing (src/input/keyboard_gamepad.h). No game, GPU, SDL or audio device
// needed: it links the SDK's real keybinds.cpp (rex::ui::ParseVirtualKey)
// plus a stub for the one SDK cvar symbol that keybinds.cpp references.
//
// Covers:
//   - the requested mouse-button config ("LMB:X,RMB:Y,MMB:B,Shift:A,...")
//     parses fully and every entry lands on the expected VirtualKey +
//     guest button/trigger/stick target;
//   - mouse-button key names: LMB/RMB/MMB via the SDK table, case-insensitive
//     spellings, and XMB1/XMB2 side buttons (VK_XBUTTON1/2);
//   - malformed / unknown entries are skipped without killing the rest;
//   - guest-target aliases (LSHOULDER, View, ltrigger, ...) still resolve.

#include <rex/cvar.h>  // FlagEntry (needed for the stub below)

// The single SDK symbol the real keybinds.cpp references. The test never
// registers a cvar, so an empty stub is enough to satisfy the linker.
namespace rex::cvar {
std::optional<size_t> RegisterFlag(FlagEntry) { return {}; }
}

// src/input/keyboard_gamepad.h calls fable2::f5lua::poll_f5() from the
// driver's per-frame poll (real declaration: src/core/fable2_f5_lua.h).
// This test never instantiates the driver, so a bare declaration suffices
// and the symbol is never emitted.
namespace fable2::f5lua {
void poll_f5();
void poll_f5(bool down);
}

#include "src/input/keyboard_gamepad.h"
// The built-in default map (src/core/fable2_config.h) must parse fully too.
#include "src/core/fable2_config.h"

#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

using B = rex::input::X_INPUT_GAMEPAD_BUTTON;
using M = fable2::input_detail::Mapping;
using fable2::input_detail::FillGuestTarget;
using fable2::input_detail::ParseKey;
using fable2::input_detail::ParseMap;

static int failures = 0;
#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
      ++failures;                                                            \
    }                                                                        \
  } while (0)

// Find a binding by VirtualKey code (returns nullopt when absent).
static std::optional<M> Find(const std::vector<M>& map, uint16_t vk) {
  for (const auto& m : map) {
    if (m.vk == vk) return m;
  }
  return std::nullopt;
}

static void CheckBinding(const std::vector<M>& map, uint16_t vk, uint16_t button,
                         char trigger = 0, int8_t axis = 0, int8_t axis_sign = 0) {
  auto m = Find(map, vk);
  CHECK(m.has_value());
  if (!m) return;
  CHECK(m->button == button);
  CHECK(m->trigger == trigger);
  CHECK(m->axis == axis);
  CHECK(m->axis_sign == axis_sign);
}

int main() {
  using V = rex::ui::VirtualKey;

  // 1) The requested mouse-button config, verbatim, must parse fully:
  //    LMB:X,RMB:Y,MMB:B,Shift:A,Space:A,E:A,Q:LT,Tab:RB,R:RT,Z:LB,B:B,
  //    W:StickUp,S:StickDown,A:StickLeft,D:StickRight,Escape:Pause,M:Select,
  //    Up:Up,Down:Down,Left:Right? no - Left:Left,Right:Right,F1:Up,F2:Down,
  //    F3:Left
  const std::string kRequested =
      "LMB:X,RMB:Y,MMB:B,Shift:A,Space:A,E:A,Q:LT,Tab:RB,R:RT,Z:LB,B:B,"
      "W:StickUp,S:StickDown,A:StickLeft,D:StickRight,Escape:Pause,M:Select,"
      "Up:Up,Down:Down,Left:Left,Right:Right,F1:Up,F2:Down,F3:Left";
  auto map = ParseMap(kRequested);
  CHECK(map.size() == 24);  // every entry valid (duplicate targets are fine)

  // Mouse buttons -> guest face buttons.
  CheckBinding(map, static_cast<uint16_t>(V::kLButton), B::X_INPUT_GAMEPAD_X);
  CheckBinding(map, static_cast<uint16_t>(V::kRButton), B::X_INPUT_GAMEPAD_Y);
  CheckBinding(map, static_cast<uint16_t>(V::kMButton), B::X_INPUT_GAMEPAD_B);
  // Keyboard keys.
  CheckBinding(map, static_cast<uint16_t>(V::kShift), B::X_INPUT_GAMEPAD_A);
  CheckBinding(map, static_cast<uint16_t>(V::kSpace), B::X_INPUT_GAMEPAD_A);
  CheckBinding(map, static_cast<uint16_t>(V::kE), B::X_INPUT_GAMEPAD_A);
  CheckBinding(map, static_cast<uint16_t>(V::kQ), 0, 'L');
  CheckBinding(map, static_cast<uint16_t>(V::kTab), B::X_INPUT_GAMEPAD_RIGHT_SHOULDER);
  CheckBinding(map, static_cast<uint16_t>(V::kR), 0, 'R');
  CheckBinding(map, static_cast<uint16_t>(V::kZ), B::X_INPUT_GAMEPAD_LEFT_SHOULDER);
  CheckBinding(map, static_cast<uint16_t>(V::kB), B::X_INPUT_GAMEPAD_B);
  // Left stick axes (signs match the driver's convention).
  CheckBinding(map, static_cast<uint16_t>(V::kW), 0, 0, 2, 1);
  CheckBinding(map, static_cast<uint16_t>(V::kS), 0, 0, 2, -1);
  CheckBinding(map, static_cast<uint16_t>(V::kA), 0, 0, 1, -1);
  CheckBinding(map, static_cast<uint16_t>(V::kD), 0, 0, 1, 1);
  // Dpad / menu / back.
  CheckBinding(map, static_cast<uint16_t>(V::kEscape), B::X_INPUT_GAMEPAD_START);
  CheckBinding(map, static_cast<uint16_t>(V::kM), B::X_INPUT_GAMEPAD_BACK);
  CheckBinding(map, static_cast<uint16_t>(V::kUp), B::X_INPUT_GAMEPAD_DPAD_UP);
  CheckBinding(map, static_cast<uint16_t>(V::kDown), B::X_INPUT_GAMEPAD_DPAD_DOWN);
  CheckBinding(map, static_cast<uint16_t>(V::kLeft), B::X_INPUT_GAMEPAD_DPAD_LEFT);
  CheckBinding(map, static_cast<uint16_t>(V::kRight), B::X_INPUT_GAMEPAD_DPAD_RIGHT);
  CheckBinding(map, static_cast<uint16_t>(V::kF1), B::X_INPUT_GAMEPAD_DPAD_UP);
  CheckBinding(map, static_cast<uint16_t>(V::kF2), B::X_INPUT_GAMEPAD_DPAD_DOWN);
  CheckBinding(map, static_cast<uint16_t>(V::kF3), B::X_INPUT_GAMEPAD_DPAD_LEFT);

  // 2) Mouse-button key names: SDK table (case-sensitive) and the local
  //    case-insensitive / side-button fallback.
  CHECK(ParseKey("LMB") == V::kLButton);
  CHECK(ParseKey("RMB") == V::kRButton);
  CHECK(ParseKey("MMB") == V::kMButton);
  CHECK(ParseKey("lmb") == V::kLButton);
  CHECK(ParseKey("rmb") == V::kRButton);
  CHECK(ParseKey("mmb") == V::kMButton);
  CHECK(ParseKey(" XMB1 ") == V::kXButton1);  // side button 1, stray spaces ok
  CHECK(ParseKey("xmb2") == V::kXButton2);    // side button 2
  CHECK(ParseKey("XButton1") == V::kXButton1);
  CHECK(ParseKey("SideButton2") == V::kXButton2);
  auto side = ParseMap("XMB1:A,XMB2:LT");
  CHECK(side.size() == 2);
  CheckBinding(side, static_cast<uint16_t>(V::kXButton1), B::X_INPUT_GAMEPAD_A);
  CheckBinding(side, static_cast<uint16_t>(V::kXButton2), 0, 'L');

  // 3) Malformed / unknown entries are skipped, the rest still parse.
  auto bad = ParseMap("NotAKey:A,LMB:NotAButton,,E:A,Space:");
  CHECK(bad.size() == 1);
  if (bad.size() == 1) {
    CheckBinding(bad, static_cast<uint16_t>(V::kE), B::X_INPUT_GAMEPAD_A);
  }
  CHECK(ParseMap("").empty());
  CHECK(ParseMap(",,,").empty());

  // 3b) The built-in default map (kDefaultKeyboardGamepadMap) parses fully
  //     and lands every entry on the intended target.
  auto def = ParseMap(fable2::config::kDefaultKeyboardGamepadMap);
  CHECK(def.size() == 20);
  CheckBinding(def, static_cast<uint16_t>(V::kLButton), B::X_INPUT_GAMEPAD_X);
  CheckBinding(def, static_cast<uint16_t>(V::kRButton), B::X_INPUT_GAMEPAD_Y);
  CheckBinding(def, static_cast<uint16_t>(V::kMButton), B::X_INPUT_GAMEPAD_B);
  CheckBinding(def, static_cast<uint16_t>(V::kShift), B::X_INPUT_GAMEPAD_A);
  CheckBinding(def, static_cast<uint16_t>(V::kE), B::X_INPUT_GAMEPAD_A);
  CheckBinding(def, static_cast<uint16_t>(V::kB), B::X_INPUT_GAMEPAD_B);
  CheckBinding(def, static_cast<uint16_t>(V::kQ), 0, 'L');
  CheckBinding(def, static_cast<uint16_t>(V::kTab), B::X_INPUT_GAMEPAD_RIGHT_SHOULDER);
  CheckBinding(def, static_cast<uint16_t>(V::kR), 0, 'R');
  CheckBinding(def, static_cast<uint16_t>(V::kZ), B::X_INPUT_GAMEPAD_LEFT_SHOULDER);
  CheckBinding(def, static_cast<uint16_t>(V::kW), 0, 0, 2, 1);
  CheckBinding(def, static_cast<uint16_t>(V::kS), 0, 0, 2, -1);
  CheckBinding(def, static_cast<uint16_t>(V::kA), 0, 0, 1, -1);
  CheckBinding(def, static_cast<uint16_t>(V::kD), 0, 0, 1, 1);
  CheckBinding(def, static_cast<uint16_t>(V::kEscape), B::X_INPUT_GAMEPAD_START);
  CheckBinding(def, static_cast<uint16_t>(V::kM), B::X_INPUT_GAMEPAD_BACK);
  CheckBinding(def, static_cast<uint16_t>(V::kUp), B::X_INPUT_GAMEPAD_DPAD_UP);
  CheckBinding(def, static_cast<uint16_t>(V::kDown), B::X_INPUT_GAMEPAD_DPAD_DOWN);
  CheckBinding(def, static_cast<uint16_t>(V::kLeft), B::X_INPUT_GAMEPAD_DPAD_LEFT);
  CheckBinding(def, static_cast<uint16_t>(V::kRight), B::X_INPUT_GAMEPAD_DPAD_RIGHT);

  // 4) Guest-target aliases still resolve.
  M m;
  FillGuestTarget(m, "lShoulder");
  CHECK(m.button == B::X_INPUT_GAMEPAD_LEFT_SHOULDER);
  FillGuestTarget(m, "VIEW");
  CHECK(m.button == B::X_INPUT_GAMEPAD_BACK);
  FillGuestTarget(m, "ltrigger");
  CHECK(m.trigger == 'L');
  FillGuestTarget(m, "StIckRighT");
  CHECK(m.axis == 1 && m.axis_sign == 1);
  FillGuestTarget(m, "Bogus");
  CHECK(!m.valid());

  if (failures) {
    std::printf("%d failure(s)\n", failures);
    return 1;
  }
  std::printf("PASS: keyboard/mouse gamepad map parsing\n");
  return 0;
}
