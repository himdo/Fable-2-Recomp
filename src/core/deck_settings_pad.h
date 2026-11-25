// fable_2 - The Deck Recomp page's view of the guest pad reads
// (src/core/deck_settings_state.h). Included where InputGetPadState_822B2D60
// is overridden (src/core/fable2_photo_mode.h and the remote-control state
// probe), after the recompiled code's headers. From himdo/Fable-2-Recomp
// PR #213 by Locme.
#pragma once

#include "deck_settings_state.h"

namespace fable2::decksettings {

// Guest pad read (game thread): r3 = user, r4 = X_INPUT_STATE, result 0 = ok.
// Call after the original read, before anything else looks at the state.
inline void on_pad_read(uint8_t* base, uint32_t user, uint32_t state_ptr, uint32_t result) {
  if (result != 0 || state_ptr == 0) {
    pad_word(user).store(0, std::memory_order_relaxed);
    return;
  }
  PadSnapshot p;
  p.buttons = REX_LOAD_U16(state_ptr + 4);
  p.lt = REX_LOAD_U8(state_ptr + 6);
  p.rt = REX_LOAD_U8(state_ptr + 7);
  p.lx = static_cast<int16_t>(REX_LOAD_U16(state_ptr + 8));
  p.ly = static_cast<int16_t>(REX_LOAD_U16(state_ptr + 10));
  pad_word(user).store(pack(p), std::memory_order_relaxed);

  bool blank = active();
  if (!blank && hold_until_released().load(std::memory_order_relaxed)) {
    const PadSnapshot m = merged_pad();  // every user's pad released
    const bool neutral = m.buttons == 0 && m.lt < 30 && m.rt < 30 && std::abs(int(m.lx)) < 7849 &&
                         std::abs(int(m.ly)) < 7849;
    if (neutral) hold_until_released().store(false, std::memory_order_relaxed);
    blank = !neutral;
  }
  if (!blank) return;
  REX_STORE_U16(state_ptr + 4, 0);
  REX_STORE_U8(state_ptr + 6, 0);
  REX_STORE_U8(state_ptr + 7, 0);
  REX_STORE_U16(state_ptr + 8, 0);
  REX_STORE_U16(state_ptr + 10, 0);
  REX_STORE_U16(state_ptr + 12, 0);
  REX_STORE_U16(state_ptr + 14, 0);
}

}  // namespace fable2::decksettings
