// fable_2 - Direct host read of Xbox controllers for photo mode.
//
// Photo mode reads the pad on the host through XInput, beside whatever the
// SDK's input driver gives the game, so the Guide button (which XInput only
// reports through the undocumented XInputGetStateEx, ordinal 100) can open
// photo mode and the camera / menu work even when the game's own pad read
// comes back empty. All four slots are merged. Disconnected slots are only
// re-checked every two seconds (XInput is slow on empty slots).
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <mutex>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "photo_mode_state.h"

namespace fable2::hostpad {

struct State {
  bool connected = false;
  photomode::Pad pad;
};

namespace detail {

#ifdef _WIN32
struct XState {  // XINPUT_STATE
  DWORD packet;
  WORD buttons;
  BYTE lt, rt;
  SHORT lx, ly, rx, ry;
};
using GetStateFn = DWORD(WINAPI*)(DWORD, XState*);

inline GetStateFn Resolve(bool& has_guide) {
  for (const wchar_t* dll : {L"xinput1_4.dll", L"xinput1_3.dll"}) {
    HMODULE m = LoadLibraryW(dll);
    if (!m) continue;
    if (auto ex = reinterpret_cast<GetStateFn>(GetProcAddress(m, reinterpret_cast<LPCSTR>(100)))) {
      has_guide = true;
      return ex;
    }
    if (auto plain = reinterpret_cast<GetStateFn>(GetProcAddress(m, "XInputGetState"))) return plain;
  }
  for (const wchar_t* dll : {L"xinput9_1_0.dll"}) {
    if (HMODULE m = LoadLibraryW(dll))
      if (auto plain = reinterpret_cast<GetStateFn>(GetProcAddress(m, "XInputGetState"))) return plain;
  }
  return nullptr;
}
#endif

inline int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

}  // namespace detail

// Whether XInputGetStateEx (Guide) was found. Valid after the first Poll().
inline bool& has_guide() {
  static bool v = false;
  return v;
}

// Merged state of every connected controller. Cheap to call often: the
// result is cached for 4 ms.
inline State Poll() {
  static std::mutex m;
  static State cached;
  static int64_t cached_ms = -1000;
  static int64_t slot_retry_ms[4] = {0, 0, 0, 0};
  std::lock_guard<std::mutex> lock(m);
  const int64_t now = detail::now_ms();
  if (now - cached_ms < 4) return cached;
  cached_ms = now;
  State out;
#ifdef _WIN32
  static bool resolved = false;
  static detail::GetStateFn get = nullptr;
  if (!resolved) {
    resolved = true;
    get = detail::Resolve(has_guide());
  }
  if (get) {
    auto bigger = [](int16_t a, int16_t b) { return std::abs(int(b)) > std::abs(int(a)) ? b : a; };
    for (DWORD i = 0; i < 4; ++i) {
      if (now < slot_retry_ms[i]) continue;
      detail::XState x{};
      if (get(i, &x) != ERROR_SUCCESS) {
        slot_retry_ms[i] = now + 2000;
        continue;
      }
      out.connected = true;
      out.pad.buttons |= x.buttons;
      out.pad.lt = std::max<uint8_t>(out.pad.lt, x.lt);
      out.pad.rt = std::max<uint8_t>(out.pad.rt, x.rt);
      out.pad.lx = bigger(out.pad.lx, x.lx);
      out.pad.ly = bigger(out.pad.ly, x.ly);
      out.pad.rx = bigger(out.pad.rx, x.rx);
      out.pad.ry = bigger(out.pad.ry, x.ry);
    }
  }
#endif
  cached = out;
  return out;
}

}  // namespace fable2::hostpad
