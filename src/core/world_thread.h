// fable_2 - where the host may run game Lua (CScriptManager::RunScript).
//
// Every RunScript the host makes must run inside the world-update service:
// console::service_world_thread(), which photo mode's GameWorldState_Update
// hook (photomode::on_world_update) calls at the start of each world update,
// on the world's own host thread. RunScript from anywhere else, such as the
// MainRenderLoop hook (console::poll) on the render thread, races the world
// update and corrupts the game's script engine:
//   - #184 (2026-11-14) moved the photo-mode pump and F5 there for that reason;
//   - #177 (merged 12 minutes earlier) started mouse_direct.lua from
//     console::poll, and 1.6 crashed after alt-tab (guest access violations
//     and ppc_longjmp aborts in the Lua VM) until #189 moved it.
//
// photomode::on_world_update opens a ServiceScope around the hook, and
// f5lua::run_external refuses (and logs once per script) when no scope is
// open on the calling thread. tests/core/test_world_thread_runscript.py
// checks the same rule in the source, in CI.
//
// No dependencies, so the native test (tests/native/test_world_thread.cpp)
// builds it on its own.

#pragma once

#include <mutex>
#include <string>
#include <unordered_set>

namespace fable2::worldthread {

namespace detail {
inline int& service_depth() {
  static thread_local int depth = 0;
  return depth;
}
inline int& research_depth() {
  static thread_local int depth = 0;
  return depth;
}
}  // namespace detail

// Open while the world-update service runs on this thread.
struct ServiceScope {
  ServiceScope() { ++detail::service_depth(); }
  ~ServiceScope() { --detail::service_depth(); }
  ServiceScope(const ServiceScope&) = delete;
  ServiceScope& operator=(const ServiceScope&) = delete;
};

// Research only: FABLE2_RUNSCRIPT_STRESS=render runs RunScript from the render
// thread on purpose, to reproduce the race (console::stress_tick).
struct ResearchOffServiceScope {
  ResearchOffServiceScope() { ++detail::research_depth(); }
  ~ResearchOffServiceScope() { --detail::research_depth(); }
  ResearchOffServiceScope(const ResearchOffServiceScope&) = delete;
  ResearchOffServiceScope& operator=(const ResearchOffServiceScope&) = delete;
};

inline bool InService() { return detail::service_depth() > 0; }

// True when the calling thread may run RunScript now.
inline bool MayRunScript() { return InService() || detail::research_depth() > 0; }

// True the first time a script is refused, so a retrying caller logs once.
inline bool FirstRefusal(const std::string& path) {
  static std::mutex mu;
  static std::unordered_set<std::string> seen;
  std::lock_guard<std::mutex> lock(mu);
  return seen.insert(path).second;
}

}  // namespace fable2::worldthread
