// Synthetic standalone test for the keyboard/mouse -> guest gamepad map
// parsing (src/input/keyboard_gamepad.h). No game, GPU, SDL or audio device
// needed: it links the SDK's real keybinds.cpp (rex::ui::ParseVirtualKey)
// plus a stub for the one SDK cvar symbol that keybinds.cpp references.

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
void poll_f5(bool focused);
}

#include "src/input/keyboard_gamepad.h"
// The built-in default map (src/core/fable2_config.h) must parse fully too.
#include "src/core/fable2_config.h"

#include <optional>
#include <string>

#include "catch_amalgamated.hpp"

using B = rex::input::X_INPUT_GAMEPAD_BUTTON;
using M = fable2::input_detail::Mapping;
using V = rex::ui::VirtualKey;
using fable2::input_detail::FillGuestTarget;
using fable2::input_detail::ParseKey;
using fable2::input_detail::ParseMap;

// Find a binding by VirtualKey code (returns nullopt when absent).
static std::optional<M> Find(const std::vector<M>& map, V key) {
  for (const auto& m : map) {
    if (m.vk == static_cast<uint16_t>(key)) return m;
  }
  return std::nullopt;
}

static void CheckBinding(const std::vector<M>& map, V key, uint16_t button, char trigger = 0,
                         int8_t axis = 0, int8_t axis_sign = 0) {
  INFO("key 0x" << std::hex << static_cast<int>(key));
  const auto m = Find(map, key);
  REQUIRE(m.has_value());
  CHECK(m->button == button);
  CHECK(m->trigger == trigger);
  CHECK(int(m->axis) == int(axis));
  CHECK(int(m->axis_sign) == int(axis_sign));
}

TEST_CASE("ParseMap", "[input]") {
  SECTION("parses the requested mouse-button config in full") {
    const auto map = ParseMap(
        "LMB:X,RMB:Y,MMB:B,Shift:A,Space:A,E:A,Q:LT,Tab:RB,R:RT,Z:LB,B:B,"
        "W:StickUp,S:StickDown,A:StickLeft,D:StickRight,Escape:Pause,M:Select,"
        "Up:Up,Down:Down,Left:Left,Right:Right,F1:Up,F2:Down,F3:Left");
    CHECK(map.size() == 24);  // every entry valid (duplicate targets are fine)
    // Mouse buttons -> guest face buttons.
    CheckBinding(map, V::kLButton, B::X_INPUT_GAMEPAD_X);
    CheckBinding(map, V::kRButton, B::X_INPUT_GAMEPAD_Y);
    CheckBinding(map, V::kMButton, B::X_INPUT_GAMEPAD_B);
    // Keyboard keys.
    CheckBinding(map, V::kShift, B::X_INPUT_GAMEPAD_A);
    CheckBinding(map, V::kSpace, B::X_INPUT_GAMEPAD_A);
    CheckBinding(map, V::kE, B::X_INPUT_GAMEPAD_A);
    CheckBinding(map, V::kQ, 0, 'L');
    CheckBinding(map, V::kTab, B::X_INPUT_GAMEPAD_RIGHT_SHOULDER);
    CheckBinding(map, V::kR, 0, 'R');
    CheckBinding(map, V::kZ, B::X_INPUT_GAMEPAD_LEFT_SHOULDER);
    CheckBinding(map, V::kB, B::X_INPUT_GAMEPAD_B);
    // Left stick axes (signs match the driver's convention).
    CheckBinding(map, V::kW, 0, 0, 2, 1);
    CheckBinding(map, V::kS, 0, 0, 2, -1);
    CheckBinding(map, V::kA, 0, 0, 1, -1);
    CheckBinding(map, V::kD, 0, 0, 1, 1);
    // Dpad / menu / back.
    CheckBinding(map, V::kEscape, B::X_INPUT_GAMEPAD_START);
    CheckBinding(map, V::kM, B::X_INPUT_GAMEPAD_BACK);
    CheckBinding(map, V::kUp, B::X_INPUT_GAMEPAD_DPAD_UP);
    CheckBinding(map, V::kDown, B::X_INPUT_GAMEPAD_DPAD_DOWN);
    CheckBinding(map, V::kLeft, B::X_INPUT_GAMEPAD_DPAD_LEFT);
    CheckBinding(map, V::kRight, B::X_INPUT_GAMEPAD_DPAD_RIGHT);
    CheckBinding(map, V::kF1, B::X_INPUT_GAMEPAD_DPAD_UP);
    CheckBinding(map, V::kF2, B::X_INPUT_GAMEPAD_DPAD_DOWN);
    CheckBinding(map, V::kF3, B::X_INPUT_GAMEPAD_DPAD_LEFT);
  }

  SECTION("binds the mouse side buttons") {
    const auto side = ParseMap("XMB1:A,XMB2:LT");
    CHECK(side.size() == 2);
    CheckBinding(side, V::kXButton1, B::X_INPUT_GAMEPAD_A);
    CheckBinding(side, V::kXButton2, 0, 'L');
  }

  SECTION("skips malformed and unknown entries and keeps the rest") {
    const auto bad = ParseMap("NotAKey:A,LMB:NotAButton,,E:A,Space:");
    CHECK(bad.size() == 1);
    CheckBinding(bad, V::kE, B::X_INPUT_GAMEPAD_A);
    CHECK(ParseMap("").empty());
    CHECK(ParseMap(",,,").empty());
  }

  SECTION("parses the built-in default map in full") {
    const auto def = ParseMap(fable2::config::kDefaultKeyboardGamepadMap);
    CHECK(def.size() == 20);
    CheckBinding(def, V::kLButton, B::X_INPUT_GAMEPAD_X);
    CheckBinding(def, V::kRButton, B::X_INPUT_GAMEPAD_Y);
    CheckBinding(def, V::kMButton, B::X_INPUT_GAMEPAD_B);
    CheckBinding(def, V::kShift, B::X_INPUT_GAMEPAD_A);
    CheckBinding(def, V::kE, B::X_INPUT_GAMEPAD_A);
    CheckBinding(def, V::kB, B::X_INPUT_GAMEPAD_B);
    CheckBinding(def, V::kQ, 0, 'L');
    CheckBinding(def, V::kTab, B::X_INPUT_GAMEPAD_RIGHT_SHOULDER);
    CheckBinding(def, V::kR, 0, 'R');
    CheckBinding(def, V::kZ, B::X_INPUT_GAMEPAD_LEFT_SHOULDER);
    CheckBinding(def, V::kW, 0, 0, 2, 1);
    CheckBinding(def, V::kS, 0, 0, 2, -1);
    CheckBinding(def, V::kA, 0, 0, 1, -1);
    CheckBinding(def, V::kD, 0, 0, 1, 1);
    CheckBinding(def, V::kEscape, B::X_INPUT_GAMEPAD_START);
    CheckBinding(def, V::kM, B::X_INPUT_GAMEPAD_BACK);
    CheckBinding(def, V::kUp, B::X_INPUT_GAMEPAD_DPAD_UP);
    CheckBinding(def, V::kDown, B::X_INPUT_GAMEPAD_DPAD_DOWN);
    CheckBinding(def, V::kLeft, B::X_INPUT_GAMEPAD_DPAD_LEFT);
    CheckBinding(def, V::kRight, B::X_INPUT_GAMEPAD_DPAD_RIGHT);
  }
}

TEST_CASE("ParseKey", "[input]") {
  SECTION("reads the mouse buttons from the SDK's table") {
    CHECK(ParseKey("LMB") == V::kLButton);
    CHECK(ParseKey("RMB") == V::kRButton);
    CHECK(ParseKey("MMB") == V::kMButton);
  }

  SECTION("reads mouse-button names in any case") {
    CHECK(ParseKey("lmb") == V::kLButton);
    CHECK(ParseKey("rmb") == V::kRButton);
    CHECK(ParseKey("mmb") == V::kMButton);
  }

  SECTION("reads the side buttons under each name, ignoring stray spaces") {
    CHECK(ParseKey(" XMB1 ") == V::kXButton1);
    CHECK(ParseKey("xmb2") == V::kXButton2);
    CHECK(ParseKey("XButton1") == V::kXButton1);
    CHECK(ParseKey("SideButton2") == V::kXButton2);
  }
}

TEST_CASE("FillGuestTarget", "[input]") {
  M m;

  SECTION("resolves button aliases in any case") {
    FillGuestTarget(m, "lShoulder");
    CHECK(m.button == B::X_INPUT_GAMEPAD_LEFT_SHOULDER);
    FillGuestTarget(m, "VIEW");
    CHECK(m.button == B::X_INPUT_GAMEPAD_BACK);
  }

  SECTION("resolves trigger and stick aliases") {
    FillGuestTarget(m, "ltrigger");
    CHECK(m.trigger == 'L');
    FillGuestTarget(m, "StIckRighT");
    CHECK(int(m.axis) == 1);
    CHECK(int(m.axis_sign) == 1);
  }

  SECTION("leaves an unknown target invalid") {
    FillGuestTarget(m, "Bogus");
    CHECK_FALSE(m.valid());
  }
}
