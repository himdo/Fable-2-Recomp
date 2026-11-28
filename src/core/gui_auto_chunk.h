// fable_2 - a one-shot GameFace (GUI) Lua chunk that a feature asks for, run by the GUI Lua
// bridge (src/core/fable2_gui_lua.h) in the first GameFace state that calls a hooked native
// and meets `where`.
//
// Why: GUI Lua may only run on the state that is calling a native right now, on the GUI
// thread (running it on any other state corrupted the VM; see fable2_gui_lua.h drain). A
// feature in another file can't include that header (it defines the hooks), so it posts its
// chunk here instead. Used by the difficulty row in Options > Game
// (src/core/hotfunc/difficulty/OptionsDifficultyRow.cpp).
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

#include <rex/ppc/context.h>

namespace fable2::guilua {

struct AutoChunk {
  std::atomic<bool> pending{false};
  std::mutex mu;
  std::string where;  // a Lua expression; the chunk only runs in a state where it is true
  std::string code;   // the chunk body; it returns a value (the bridge returns "ok <value>")
  // Called on the GUI thread with the chunk's result ("ok ..." / "error ..."); ctx may be used
  // for guest calls but must be left as it was.
  std::function<void(PPCContext&, uint8_t*, const std::string&)> done;
};

inline AutoChunk& Auto() {
  static AutoChunk a;
  return a;
}

// Any thread. Replaces a chunk that hasn't run yet.
inline void PostAutoChunk(std::string where, std::string code,
                          std::function<void(PPCContext&, uint8_t*, const std::string&)> done) {
  AutoChunk& a = Auto();
  std::lock_guard<std::mutex> lock(a.mu);
  a.where = std::move(where);
  a.code = std::move(code);
  a.done = std::move(done);
  a.pending.store(true);
}

}  // namespace fable2::guilua
