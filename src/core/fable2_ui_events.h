#pragma once
// fable2_ui_events.h - when each Gameface UI event last fired.
//
// Gameface (the game's menu/HUD system) reports "fireEvent: <NAME>" through
// its report callback (fable2_fit_window_ui.h, OnGamefaceMessage) for every
// UI event, e.g. CAN_PRESS_A and END_LEGAL on the start-up screens,
// END_STARTED every 2 s while the title waits for a button, LOAD_CARD_ACTIVE
// while a save loads and FINISHED_TRANSITIONING_TO_GAME once play starts.
// The developer console's `waitui` / `boot` commands wait on them, so menus
// can be driven without fixed timers (and without window focus).

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace fable2::uievents {

inline int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

struct Registry {
  std::mutex m;
  std::unordered_map<std::string, int64_t> last_ms;  // event name -> last time it fired
};
inline Registry& registry() {
  static Registry r;
  return r;
}

// Called with each Gameface report message; records "fireEvent: NAME".
inline void OnMessage(const std::string& message) {
  static const std::string kPrefix = "fireEvent: ";
  if (message.rfind(kPrefix, 0) != 0) return;
  std::string name = message.substr(kPrefix.size());
  while (!name.empty() && (name.back() == '\n' || name.back() == '\r' || name.back() == ' ')) name.pop_back();
  if (name.empty()) return;
  auto& r = registry();
  std::lock_guard<std::mutex> lock(r.m);
  r.last_ms[name] = NowMs();
}

// When `name` last fired (steady-clock ms), or -1 if it never has.
inline int64_t LastFired(const std::string& name) {
  auto& r = registry();
  std::lock_guard<std::mutex> lock(r.m);
  auto it = r.last_ms.find(name);
  return it == r.last_ms.end() ? -1 : it->second;
}

}  // namespace fable2::uievents
