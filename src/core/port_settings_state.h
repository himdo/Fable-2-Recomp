// fable_2 - State shared by the Advanced Options page and the game threads.
//
// The page (src/core/port_settings_dialog.h) is drawn on the UI thread. It is
// opened from the game's own menus (the "Advanced Options" item that
// src/menu/main_menu.cpp adds to the main menu, and the "Advanced" row
// src/core/port_settings_lua.h adds to the pause menu's Options), on game
// or GUI threads: they only raise open_request().
//
// While the page is open the game must not see the controller or the keys
// mapped onto it (the menu underneath would move), so every guest pad read
// (InputGetPadState_822B2D60, see on_pad_read) is captured for the page and
// then blanked. After the page closes the blanking goes on until every
// button is released, so the B / Start that closed the page does not also
// close the game's menu. The pad hook itself is in port_settings_pad.h (it
// needs the recompiled code's memory macros); this header has no
// dependencies, so the input code and its tests can include it.
#pragma once

#include <atomic>
#include <cstdint>
#include <cstdlib>

namespace fable2::portsettings {

// Set by the menu item (game thread), taken by the page (UI thread).
inline std::atomic<bool>& open_request() {
  static std::atomic<bool> v{false};
  return v;
}
// Whether the last open came from the main menu (else the pause menu's
// Options). Their book frames are moved by different amounts on wide screens.
inline std::atomic<bool>& from_main_menu() {
  static std::atomic<bool> v{false};
  return v;
}
// True while the page is on screen (written by the UI thread).
inline std::atomic<bool>& is_open() {
  static std::atomic<bool> v{false};
  return v;
}
inline bool active() { return is_open().load(std::memory_order_relaxed); }

// The controller as the game would have seen it, per guest user (keyboard
// keys mapped onto the pad included).
struct PadSnapshot {
  uint16_t buttons = 0;
  uint8_t lt = 0, rt = 0;
  int16_t lx = 0, ly = 0;
};
inline std::atomic<uint64_t>& pad_word(uint32_t user) {
  static std::atomic<uint64_t> v[4];
  return v[user & 3];
}
inline uint64_t pack(const PadSnapshot& p) {
  return uint64_t(p.buttons) | uint64_t(p.lt) << 16 | uint64_t(p.rt) << 24 | uint64_t(uint16_t(p.lx)) << 32 |
         uint64_t(uint16_t(p.ly)) << 48;
}
inline PadSnapshot unpack(uint64_t w) {
  PadSnapshot p;
  p.buttons = uint16_t(w);
  p.lt = uint8_t(w >> 16);
  p.rt = uint8_t(w >> 24);
  p.lx = int16_t(uint16_t(w >> 32));
  p.ly = int16_t(uint16_t(w >> 48));
  return p;
}
// All users' pads OR-merged (sticks: the larger deflection).
inline PadSnapshot merged_pad() {
  PadSnapshot m;
  for (uint32_t u = 0; u < 4; ++u) {
    const PadSnapshot p = unpack(pad_word(u).load(std::memory_order_relaxed));
    m.buttons |= p.buttons;
    m.lt = p.lt > m.lt ? p.lt : m.lt;
    m.rt = p.rt > m.rt ? p.rt : m.rt;
    if (std::abs(int(p.lx)) > std::abs(int(m.lx))) m.lx = p.lx;
    if (std::abs(int(p.ly)) > std::abs(int(m.ly))) m.ly = p.ly;
  }
  return m;
}

// Keep blanking after the page closed until the pad is neutral.
inline std::atomic<bool>& hold_until_released() {
  static std::atomic<bool> v{false};
  return v;
}

}  // namespace fable2::portsettings
