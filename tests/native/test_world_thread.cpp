// Standalone test for the RunScript gate (src/core/world_thread.h). No game,
// GPU, SDL or audio device needed.
//
// f5lua::run_external refuses to run a script unless the calling thread is
// inside the world-update service (photomode::on_world_update opens a
// ServiceScope around console::service_world_thread), or inside the research
// scope that FABLE2_RUNSCRIPT_STRESS=render opens.

#include <thread>

#include "catch_amalgamated.hpp"
#include "src/core/world_thread.h"

namespace wt = fable2::worldthread;

TEST_CASE("ServiceScope", "[worldthread]") {
  SECTION("lets nothing run outside any scope") {
    CHECK_FALSE(wt::InService());
    CHECK_FALSE(wt::MayRunScript());
  }

  SECTION("nests and unwinds") {
    {
      const wt::ServiceScope outer;
      CHECK(wt::InService());
      {
        const wt::ServiceScope inner;
        CHECK(wt::MayRunScript());
      }
      CHECK(wt::InService());  // still inside the outer scope
    }
    CHECK_FALSE(wt::InService());
    CHECK_FALSE(wt::MayRunScript());
  }

  SECTION("belongs to its own thread") {
    // In the game, the render thread sees nothing while the world thread is
    // inside a scope.
    const wt::ServiceScope world;
    bool other_in_service = true, other_may_run = true;
    std::thread render([&] {
      other_in_service = wt::InService();
      other_may_run = wt::MayRunScript();
    });
    render.join();
    CHECK_FALSE(other_in_service);
    CHECK_FALSE(other_may_run);
    CHECK(wt::MayRunScript());
  }
}

TEST_CASE("ResearchOffServiceScope", "[worldthread]") {
  SECTION("allows a run only while open, without counting as the service") {
    {
      const wt::ResearchOffServiceScope research;
      CHECK(wt::MayRunScript());
      CHECK_FALSE(wt::InService());
    }
    CHECK_FALSE(wt::MayRunScript());
  }
}

TEST_CASE("FirstRefusal", "[worldthread]") {
  // One section: the set of refused paths lasts for the whole process.
  SECTION("is true once per script path") {
    CHECK(wt::FirstRefusal("scripts/recomp/mouse_direct.lua"));
    CHECK_FALSE(wt::FirstRefusal("scripts/recomp/mouse_direct.lua"));
    CHECK(wt::FirstRefusal("scripts/recomp/console_step.lua"));
  }
}
