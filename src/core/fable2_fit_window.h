// fable_2 - ReXGlue Recompiled Project
//
// Fit the game to the window: render at the window's aspect ratio, so a wide
// window shows a wider view instead of a stretched or letterboxed 16:9 image.
//
// How Fable II picks its aspect ratio: the camera code (e.g.
// ProcessAndProcessAndProcess1605_821A5D80, sub_822933D8, sub_8235A7E0,
// ProcessAndProcessAndProcess1632_8322B050) checks the cached video mode's
// is_widescreen flag (0x83360158 + 12) and then loads two .rdata floats:
//   0x8209FE7C = 16/9  the projection aspect ratio (widescreen path)
//   0x8209FDF4 =  4/3  the 4:3 reference the FOV is widened from (Hor+)
// The scene is drawn into a fixed 1120x720 buffer (sub_8238DE58) and the
// presenter scales it to the display, so the buffer is already anamorphic.
// Pepper's Xenia "21:9 Widescreen" patch sets these two floats to 21/9 and
// 16/9; this keeps the same 4:3 ratio between them for any window shape:
//   0x8209FE7C = A, 0x8209FDF4 = A * 3/4, where A = window width / height.
// Both are data reads at runtime (lfs from .rdata), so writing guest memory
// is enough; no code patch is needed.
//
// The presenter then has to stretch the frame to the whole window instead of
// keeping the 16:9 video mode shape, so this also sets present_letterbox =
// false and present_safe_area_x/y = 100 (in memory only).
//
// The aspect is written at startup and again whenever the window is resized.
// Turn it off with `fit_game_to_window = false` in fable_2.toml. The developer
// console's `f2 widescreen` commands switch it, force an aspect and re-apply
// while the game runs (SetEnabled, SetForcedAspect, Reapply below).
//
// The HUD is moved out to the window edges by fable2_fit_window_ui.h.

#pragma once

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/system/xmemory.h>
#include <rex/ui/flags.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "fable2_fit_window_ui.h"

REXCVAR_DEFINE_BOOL(fit_game_to_window, true, "Display",
                    "Render the game at the window's aspect ratio (a wide window "
                    "shows a wider view) and stretch the frame to fill the window. "
                    "false = original 16:9 with letterboxing");

namespace fable2::fitwindow {

inline constexpr uint32_t kAspectAddress = 0x8209FE7C;    // 16/9 originally
inline constexpr uint32_t kReferenceAddress = 0x8209FDF4;  // 4/3 originally
inline constexpr float kOriginalAspect = 16.0f / 9.0f;
inline constexpr float kOriginalReference = 4.0f / 3.0f;
// The widescreen camera path is the one being retuned, so keep the aspect in
// its range: no narrower than 4:3, no wider than 4:1 (beyond 32:9).
inline constexpr float kMinAspect = 4.0f / 3.0f;
inline constexpr float kMaxAspect = 4.0f;

namespace detail {

inline std::atomic<uint32_t*> g_aspect{nullptr};
inline std::atomic<uint32_t*> g_reference{nullptr};
inline std::atomic<float> g_last_aspect{0.0f};
inline std::atomic<float> g_forced_aspect{0.0f};  // console override; 0 = follow the window
inline std::atomic<rex::memory::Memory*> g_memory{nullptr};
inline std::atomic<rex::ui::Window*> g_window{nullptr};
inline std::atomic<bool> g_listening{false};

inline uint32_t ToGuestFloat(float value) {
  uint32_t bits;
  std::memcpy(&bits, &value, sizeof(bits));
  return __builtin_bswap32(bits);
}

inline float FromGuestFloat(uint32_t guest) {
  const uint32_t bits = __builtin_bswap32(guest);
  float value;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

inline void Store(uint32_t* host, float value) {
  std::atomic_ref<uint32_t>(*host).store(ToGuestFloat(value),
                                         std::memory_order_relaxed);
}

inline void SetPresenterCvar(std::string_view name, std::string_view value) {
  if (rex::cvar::GetFlagByName(name) == value) return;
  if (rex::cvar::SetFlagByName(name, value)) {
    REXSYS_INFO("[fit-window] set {} = {} so the frame fills the window", name,
                value);
  } else {
    REXSYS_WARN("[fit-window] could not set {} = {}; the frame may be "
                "letterboxed",
                name, value);
  }
}

// Puts a presenter cvar back to its default (fit turned off from the console).
inline void RestorePresenterCvar(std::string_view name) {
  for (const auto& e : rex::cvar::GetRegistry()) {
    if (e.name == name) {
      rex::cvar::SetFlagByName(name, e.default_value);
      return;
    }
  }
}

}  // namespace detail

// The aspect currently written, or 0 before the first window size is known.
inline float CurrentAspect() { return detail::g_last_aspect.load(); }

// Writes a value into the game's aspect float without changing what
// CurrentAspect reports (the 2D text hook uses it for the length of a call).
inline bool WriteGuestAspect(float aspect) {
  uint32_t* aspect_ptr = detail::g_aspect.load();
  if (!aspect_ptr) return false;
  detail::Store(aspect_ptr, aspect);
  return true;
}

// Writes the aspect for a surface of width x height pixels. Safe to call from
// any thread once Install() has succeeded; does nothing before that.
inline void ApplySize(uint32_t width, uint32_t height) {
  uint32_t* aspect_ptr = detail::g_aspect.load();
  uint32_t* reference_ptr = detail::g_reference.load();
  if (!aspect_ptr || !reference_ptr || width == 0 || height == 0) return;
  if (!REXCVAR_GET(fit_game_to_window)) return;

  const float forced = detail::g_forced_aspect.load();
  float aspect = std::clamp(forced > 0.0f ? forced : float(width) / float(height),
                            kMinAspect, kMaxAspect);
  float reference = aspect * 0.75f;
  // A 16:9 window gets the game's own constants back bit for bit.
  if (std::fabs(aspect - kOriginalAspect) < 0.005f) {
    aspect = kOriginalAspect;
    reference = kOriginalReference;
  }
  if (detail::g_last_aspect.exchange(aspect) == aspect) return;

  detail::Store(aspect_ptr, aspect);
  detail::Store(reference_ptr, reference);
  REXSYS_INFO("[fit-window] window {}x{}: aspect {:.4f} (0x{:08X}), 4:3 "
              "reference {:.4f} (0x{:08X})",
              width, height, aspect, kAspectAddress, reference,
              kReferenceAddress);
}

// Follows window resizes. Static storage, so it outlives the window.
class ResizeListener final : public rex::ui::WindowListener {
 public:
  void OnResize(rex::ui::UISetupEvent& e) override {
    if (rex::ui::Window* window = e.target()) {
      ApplySize(window->GetActualPhysicalWidth(),
                window->GetActualPhysicalHeight());
    }
  }
};

inline ResizeListener& Listener() {
  static ResizeListener listener;
  return listener;
}

// Call once, after default.xex is in guest memory and the window exists
// (Fable2App::OnPostSetup). Makes the page holding the two floats
// writable for the rest of the run (it is .rdata, read-only after load),
// checks they still hold the stock values, points the presenter at the whole
// window, applies the current window size and starts following resizes.
inline void Install(rex::memory::Memory* memory, rex::ui::Window* window) {
  // Kept so the console can turn the fit on later in the run.
  if (memory) detail::g_memory.store(memory);
  if (window) detail::g_window.store(window);
  if (!REXCVAR_GET(fit_game_to_window)) {
    REXSYS_INFO("[fit-window] off (fit_game_to_window = false): original 16:9");
    return;
  }
  if (!memory) {
    REXSYS_ERROR("[fit-window] no guest memory; not installed");
    return;
  }
  auto* heap = memory->LookupHeap(kReferenceAddress);
  if (!heap) {
    REXSYS_ERROR("[fit-window] no guest heap at 0x{:08X}; not installed",
                 kReferenceAddress);
    return;
  }
  const uint32_t page_size = heap->page_size();
  const uint32_t first = kReferenceAddress & ~(page_size - 1);
  const uint32_t last = kAspectAddress & ~(page_size - 1);
  const uint32_t range = last + page_size - first;
  if (heap->QueryRangeAccess(first, first + range - 1) !=
          rex::memory::PageAccess::kReadWrite &&
      !heap->Protect(first, range,
                     rex::memory::kMemoryProtectRead |
                         rex::memory::kMemoryProtectWrite)) {
    REXSYS_ERROR("[fit-window] could not make 0x{:08X} writable; not installed",
                 first);
    return;
  }

  auto* aspect_ptr = memory->TranslateVirtual<uint32_t*>(kAspectAddress);
  auto* reference_ptr = memory->TranslateVirtual<uint32_t*>(kReferenceAddress);
  const float stock_aspect = detail::FromGuestFloat(*aspect_ptr);
  const float stock_reference = detail::FromGuestFloat(*reference_ptr);
  if (stock_aspect != kOriginalAspect || stock_reference != kOriginalReference) {
    REXSYS_ERROR("[fit-window] unexpected values at 0x{:08X} ({}) and 0x{:08X} "
                 "({}); this default.xex differs, not installed",
                 kAspectAddress, stock_aspect, kReferenceAddress,
                 stock_reference);
    return;
  }
  detail::g_aspect.store(aspect_ptr);
  detail::g_reference.store(reference_ptr);

  detail::SetPresenterCvar("present_letterbox", "false");
  detail::SetPresenterCvar("present_safe_area_x", "100");
  detail::SetPresenterCvar("present_safe_area_y", "100");
  ui::Install(memory);

  if (window) {
    if (!detail::g_listening.exchange(true)) window->AddListener(&Listener());
    ApplySize(window->GetActualPhysicalWidth(),
              window->GetActualPhysicalHeight());
    if (detail::g_last_aspect.load() == 0.0f) {
      REXSYS_INFO("[fit-window] window size not known yet; waiting for the "
                  "first resize");
    }
  } else {
    REXSYS_WARN("[fit-window] no window; the aspect stays 16:9");
  }
}

// --- Console control (f2 widescreen in fable2_console.h) ---------------------

// True once the two floats are writable and checked (Install succeeded).
inline bool Installed() { return detail::g_aspect.load() != nullptr; }

// The aspect forced from the console, or 0 when following the window.
inline float ForcedAspect() { return detail::g_forced_aspect.load(); }

inline bool WindowSize(uint32_t& width, uint32_t& height) {
  rex::ui::Window* window = detail::g_window.load();
  if (!window) return false;
  width = window->GetActualPhysicalWidth();
  height = window->GetActualPhysicalHeight();
  return width && height;
}

// Writes the aspect again from the window size (or the forced aspect).
inline void Reapply() {
  uint32_t width = 0, height = 0;
  if (!WindowSize(width, height)) return;
  detail::g_last_aspect.store(0.0f);
  ApplySize(width, height);
}

// 0 goes back to following the window. Clamped like the window aspect.
inline void SetForcedAspect(float aspect) {
  detail::g_forced_aspect.store(aspect > 0.0f ? aspect : 0.0f);
  Reapply();
}

// Turns the fit on or off while the game runs. Off writes the stock 16:9 and
// 4:3 back and returns the presenter cvars to their defaults. The HUD script
// only runs when a UI screen loads, so HUD changes show on the next screen.
inline bool SetEnabled(bool on) {
  rex::cvar::SetFlagByName("fit_game_to_window", on ? "true" : "false");
  if (on) {
    if (!Installed()) {
      Install(detail::g_memory.load(), detail::g_window.load());
      return Installed();
    }
    detail::SetPresenterCvar("present_letterbox", "false");
    detail::SetPresenterCvar("present_safe_area_x", "100");
    detail::SetPresenterCvar("present_safe_area_y", "100");
    Reapply();
    return true;
  }
  if (Installed()) {
    detail::Store(detail::g_aspect.load(), kOriginalAspect);
    detail::Store(detail::g_reference.load(), kOriginalReference);
  }
  detail::g_last_aspect.store(0.0f);
  detail::RestorePresenterCvar("present_letterbox");
  detail::RestorePresenterCvar("present_safe_area_x");
  detail::RestorePresenterCvar("present_safe_area_y");
  REXSYS_INFO("[fit-window] off from the console: original 16:9");
  return true;
}

// The two floats as the camera reads them now (0 before Install).
inline float GuestAspect() {
  uint32_t* p = detail::g_aspect.load();
  return p ? detail::FromGuestFloat(*p) : 0.0f;
}
inline float GuestReference() {
  uint32_t* p = detail::g_reference.load();
  return p ? detail::FromGuestFloat(*p) : 0.0f;
}

}  // namespace fable2::fitwindow
