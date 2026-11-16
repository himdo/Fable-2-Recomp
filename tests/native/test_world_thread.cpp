// Standalone test for the RunScript gate (src/core/world_thread.h). No game,
// GPU, SDL or audio device needed.
//
// f5lua::run_external refuses to run a script unless the calling thread is
// inside the world-update service (photomode::on_world_update opens a
// ServiceScope around console::service_world_thread), or inside the research
// scope that FABLE2_RUNSCRIPT_STRESS=render opens. Covers:
//   - outside any scope, nothing may run;
//   - the service scope nests and unwinds;
//   - the scope belongs to its own thread: another thread (the render thread,
//     in the game) sees nothing while the world thread is inside a scope;
//   - the research scope allows a run only while it is open, without counting
//     as the service;
//   - a refused script is logged once per path.

#include <cstdio>
#include <string>
#include <thread>

#include "src/core/world_thread.h"

namespace wt = fable2::worldthread;

static int failures = 0;
#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
      ++failures;                                                            \
    }                                                                        \
  } while (0)

static void TestOutsideAnyScope() {
  CHECK(!wt::InService());
  CHECK(!wt::MayRunScript());
}

static void TestServiceScopeNests() {
  {
    const wt::ServiceScope outer;
    CHECK(wt::InService());
    {
      const wt::ServiceScope inner;
      CHECK(wt::MayRunScript());
    }
    CHECK(wt::InService());  // still inside the outer scope
  }
  CHECK(!wt::InService());
  CHECK(!wt::MayRunScript());
}

static void TestScopeIsPerThread() {
  const wt::ServiceScope world;
  bool other_in_service = true, other_may_run = true;
  std::thread render([&] {
    other_in_service = wt::InService();
    other_may_run = wt::MayRunScript();
  });
  render.join();
  CHECK(!other_in_service);
  CHECK(!other_may_run);
  CHECK(wt::MayRunScript());
}

static void TestResearchScope() {
  {
    const wt::ResearchOffServiceScope research;
    CHECK(wt::MayRunScript());
    CHECK(!wt::InService());
  }
  CHECK(!wt::MayRunScript());
}

static void TestFirstRefusal() {
  CHECK(wt::FirstRefusal("scripts/recomp/mouse_direct.lua"));
  CHECK(!wt::FirstRefusal("scripts/recomp/mouse_direct.lua"));
  CHECK(wt::FirstRefusal("scripts/recomp/console_step.lua"));
}

int main() {
  TestOutsideAnyScope();
  TestServiceScopeNests();
  TestScopeIsPerThread();
  TestResearchScope();
  TestFirstRefusal();
  if (failures) {
    std::printf("%d failure(s)\n", failures);
    return 1;
  }
  std::printf("world thread tests passed\n");
  return 0;
}
