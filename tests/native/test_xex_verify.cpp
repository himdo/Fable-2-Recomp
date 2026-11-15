// The game-version check (src/core/xex_verify.h) in one build profile
// (FABLE2_BUILD_PROFILE), on synthetic files. Optionally also checks real
// files: xex-<profile>.exe --xex <path to default.xex> (repeatable).
#include "../../src/core/xex_verify.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "catch_amalgamated.hpp"

using namespace fable2::xexverify;
namespace fs = std::filesystem;

static std::vector<std::string>& XexPaths() {
  static std::vector<std::string> paths;
  return paths;
}

// A uniquely named folder under the temp directory, removed afterwards.
struct TempRoot {
  fs::path path = fs::temp_directory_path() /
                  ("fable2-xex-test-" +
                   std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  TempRoot() { REQUIRE(fs::create_directory(path)); }
  ~TempRoot() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

static void Touch(const fs::path& file) {
  fs::create_directories(file.parent_path());
  std::ofstream(file) << "synthetic";
}

TEST_CASE("Sha256File and Check", "[xex]") {
  TempRoot root;
  const auto fixture = root.path / "synthetic.xex";
  { std::ofstream(fixture, std::ios::binary) << "abc"; }

  SECTION("hashes a file with SHA-256") {
    std::string digest;
    CHECK(Sha256File(fixture, digest));
    CHECK(digest == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  }

  SECTION("reports a missing file as unreadable") {
    CHECK(Check(root.path / "missing", root.path / "marker").result == Result::ReadFailed);
  }

  SECTION("rejects an unknown file") {
    CHECK(Check(fixture, root.path / "marker").result == Result::Mismatch);
  }

  SECTION("knows no language for an unknown version") {
    CHECK_FALSE(IsAcceptedHash("unknown"));
    CHECK(DefaultLanguage("unknown") == 0);
  }
}

TEST_CASE("the version catalogue in this build profile", "[xex]") {
  TempRoot root;

  SECTION("accepts exactly the enabled versions, with their languages") {
    for (const auto& version : fable2::versions::kVersions) {
      CAPTURE(version.id);
      CHECK(IsAcceptedHash(version.hash) == IsEnabledVersion(version));
      CHECK(DefaultLanguage(version.hash) == version.language);
    }
  }

  SECTION("rejects content for a disabled or unsupported version") {
    for (const auto& version : fable2::versions::kVersions) {
      if (IsEnabledVersion(version)) continue;
      CAPTURE(version.id);
      CHECK_FALSE(HasCompatibleContent(root.path, version.hash));
    }
  }

  SECTION("accepts a dump only with every required file and no rejected one") {
    for (const auto& version : fable2::versions::kVersions) {
      if (!IsEnabledVersion(version)) continue;
      CAPTURE(version.id);
      const auto content = root.path / version.id;
      fs::create_directory(content);
      for (auto marker : version.required_files) Touch(content / marker);
      CHECK(HasCompatibleContent(content, version.hash));
      for (auto marker : version.required_files) {
        CAPTURE(marker);
        fs::remove(content / marker);
        CHECK_FALSE(HasCompatibleContent(content, version.hash));
        Touch(content / marker);
      }
      for (auto marker : version.reject_files) Touch(content / marker);
      for (auto marker : version.reject_directories) fs::create_directories(content / marker);
      if (version.reject_files.empty() && version.reject_directories.empty()) continue;
      CHECK_FALSE(HasCompatibleContent(content, version.hash));  // mixed content
      for (auto marker : version.reject_files) {
        CAPTURE(marker);
        fs::remove(content / marker);
        CHECK(HasCompatibleContent(content, version.hash));  // a partial rule still matches
        Touch(content / marker);
      }
    }
  }
}

// Hidden ([.]): main runs it only when --xex is given.
TEST_CASE("your own default.xex (--xex)", "[.][own-xex]") {
  REQUIRE_FALSE(XexPaths().empty());
  TempRoot root;
  for (const auto& path : XexPaths()) {
    CAPTURE(path);
    const auto first = Check(path, root.path / "marker");
    CHECK(first.result == Result::VerifiedFresh);  // also: no cache reused across roots
    CHECK(Check(path, root.path / "marker").result == Result::VerifiedCached);
    std::printf("verified %s: %s\n", path.c_str(), std::string(first.actual_hash).c_str());
  }
}

int main(int argc, char* argv[]) {
  Catch::Session session;
  using Catch::Clara::Opt;
  session.cli(session.cli() |
              Opt(XexPaths(), "path")["--xex"]("also check this default.xex (repeatable)"));
  if (const int rc = session.applyCommandLine(argc, argv)) return rc;
  // With --xex and no test filter of its own: every test, plus the hidden one
  // (a comma is "or" in a Catch2 filter; separate filters would be "and").
  if (!XexPaths().empty() && session.configData().testsOrTags.empty())
    session.configData().testsOrTags = {"*,[own-xex]"};
  std::printf("build profile: %s\n", std::string(kBuildProfile).c_str());
  return session.run();
}
