// Prints a native test run the way pytest-spec prints the Python tests: each
// TEST_CASE as a heading, and each SECTION in it on its own line, marked ok or
// FAIL. A TEST_CASE without sections is one line. A known bug (a TEST_CASE
// tagged [!shouldfail], the native form of pytest's strict xfail) is marked
// "known bug" while it still fails; Catch2 fails the run once it passes.
//
// Linked into every native test (tests/run_native_tests.cmd). Catch2's own
// console reporter still prints each failed check with its values and the
// summary at the end.

#include <cstdio>
#include <string>
#include <vector>

#include "catch_amalgamated.hpp"

namespace {

class SpecListener final : public Catch::EventListenerBase {
 public:
  using EventListenerBase::EventListenerBase;

  void testCaseStarting(Catch::TestCaseInfo const& info) override {
    name_ = info.name;
    known_bug_ = info.expectedToFail();
    heading_printed_ = false;
    printed_sections_ = false;
  }

  // Catch2 runs a TEST_CASE once per leaf SECTION; the outermost "section" is
  // the test case itself.
  void sectionStarting(Catch::SectionInfo const& section) override {
    if (!stack_.empty()) stack_.back().has_child = true;
    stack_.push_back({section.name, false});
  }

  void sectionEnded(Catch::SectionStats const& stats) override {
    const Frame frame = stack_.back();
    stack_.pop_back();
    if (stack_.empty() || frame.has_child) return;  // only leaf sections
    if (!heading_printed_) {
      std::printf("\n%s:\n", name_.c_str());
      heading_printed_ = true;
    }
    printed_sections_ = true;
    std::string path;
    for (size_t i = 1; i < stack_.size(); ++i) path += stack_[i].name + " > ";
    path += frame.name;
    std::printf("  %s %s\n", Mark(stats.assertions), path.c_str());
    std::fflush(stdout);
  }

  void testCaseEnded(Catch::TestCaseStats const& stats) override {
    if (!printed_sections_) std::printf("\n%s %s\n", Mark(stats.totals.assertions), name_.c_str());
    std::fflush(stdout);
  }

 private:
  struct Frame {
    std::string name;
    bool has_child;
  };

  const char* Mark(const Catch::Counts& counts) const {
    if (!counts.allOk()) return "FAIL";
    // [!shouldfail]: its failures count as "failed but ok". None means fixed.
    if (known_bug_) return counts.failedButOk > 0 ? "known bug" : "FAIL (fixed: remove [!shouldfail])";
    return counts.skipped > 0 ? "skip" : "ok  ";
  }

  std::string name_;
  bool known_bug_ = false;
  bool heading_printed_ = false;
  bool printed_sections_ = false;
  std::vector<Frame> stack_;
};

}  // namespace

CATCH_REGISTER_LISTENER(SpecListener)
