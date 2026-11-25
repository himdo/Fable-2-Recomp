// Recomp Lua scripts and a separate game folder (src/core/recomp_scripts.h):
// the scripts next to the executable are copied to where the guest loads them.
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

#include "catch_amalgamated.hpp"
#include "core/recomp_scripts.h"

namespace {

namespace fs = std::filesystem;
using fable2::recompscripts::Sync;
using fable2::recompscripts::SyncResult;

void Write(const fs::path& path, const std::string& text) {
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
}

std::string Read(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// An executable folder and a game folder, both fresh, removed when the section ends.
struct Folders {
  fs::path root = fs::temp_directory_path() / "fable2-recomp-scripts-test";
  fs::path exe = root / "exe";
  fs::path game = root / "game";
  fs::path shipped = exe / "data" / "scripts" / "recomp";
  fs::path loaded = game / "data" / "scripts" / "recomp";
  Folders() {
    fs::remove_all(root);
    fs::create_directories(exe);
    fs::create_directories(game / "data");
  }
  ~Folders() {
    std::error_code ec;
    fs::remove_all(root, ec);
  }
};

}  // namespace

TEST_CASE("Sync (recomp Lua scripts)", "[scripts]") {
  const Folders folders;

  SECTION("copies the scripts into a separate game folder") {
    Write(folders.shipped / "F5.lua", "print('f5')");
    Write(folders.shipped / "console_pump.lua", "print('pump')");
    const SyncResult result = Sync(folders.exe, folders.game);
    CHECK(result.copied == 2);
    CHECK(result.failed.empty());
    CHECK(Read(folders.loaded / "F5.lua") == "print('f5')");
    CHECK(Read(folders.loaded / "console_pump.lua") == "print('pump')");
  }

  SECTION("does nothing when the game folder is the executable's folder") {
    Write(folders.shipped / "F5.lua", "print('f5')");
    const SyncResult result = Sync(folders.exe, folders.exe);
    CHECK(result.same_folder);
    CHECK(result.copied == 0);
    CHECK(Read(folders.shipped / "F5.lua") == "print('f5')");
  }

  SECTION("sees the same folder through a different spelling of its path") {
    Write(folders.shipped / "F5.lua", "print('f5')");
    CHECK(Sync(folders.exe, folders.exe / "data" / "..").same_folder);
  }

  SECTION("leaves a script alone when it already matches") {
    Write(folders.shipped / "F5.lua", "print('f5')");
    Write(folders.loaded / "F5.lua", "print('f5')");
    const SyncResult result = Sync(folders.exe, folders.game);
    CHECK(result.copied == 0);
    CHECK(result.unchanged == 1);
  }

  SECTION("replaces a script that differs from the executable's copy") {
    Write(folders.shipped / "F5.lua", "print('new build')");
    Write(folders.loaded / "F5.lua", "print('old build, longer text')");
    const SyncResult result = Sync(folders.exe, folders.game);
    CHECK(result.copied == 1);
    CHECK(Read(folders.loaded / "F5.lua") == "print('new build')");
  }

  SECTION("keeps the console's own console_run.lua, which the game rewrites") {
    Write(folders.shipped / "console_run.lua", "return 0, [==[]==]");
    Write(folders.loaded / "console_run.lua", "return 41, [==[help]==]");
    const SyncResult result = Sync(folders.exe, folders.game);
    CHECK(result.copied == 0);
    CHECK(result.unchanged == 1);
    CHECK(Read(folders.loaded / "console_run.lua") == "return 41, [==[help]==]");
  }

  SECTION("copies console_run.lua when the game folder has none") {
    Write(folders.shipped / "console_run.lua", "return 0, [==[]==]");
    const SyncResult result = Sync(folders.exe, folders.game);
    CHECK(result.copied == 1);
    CHECK(Read(folders.loaded / "console_run.lua") == "return 0, [==[]==]");
  }

  SECTION("keeps scripts that exist only in the game folder") {
    Write(folders.shipped / "F5.lua", "print('f5')");
    Write(folders.loaded / "mine.lua", "print('mine')");
    Sync(folders.exe, folders.game);
    CHECK(Read(folders.loaded / "mine.lua") == "print('mine')");
  }

  SECTION("copies only .lua files, whatever the case of the extension") {
    Write(folders.shipped / "upper.LUA", "print('upper')");
    Write(folders.shipped / "notes.txt", "not a script");
    Write(folders.shipped / "nested" / "inner.lua", "print('inner')");
    const SyncResult result = Sync(folders.exe, folders.game);
    CHECK(result.copied == 1);
    CHECK(fs::exists(folders.loaded / "upper.LUA"));
    CHECK_FALSE(fs::exists(folders.loaded / "notes.txt"));
    CHECK_FALSE(fs::exists(folders.loaded / "nested"));
  }

  SECTION("leaves no temporary file behind") {
    Write(folders.shipped / "F5.lua", "print('f5')");
    Sync(folders.exe, folders.game);
    CHECK_FALSE(fs::exists(folders.loaded / "F5.lua.tmp"));
  }

  SECTION("reports that no scripts were shipped") {
    const SyncResult result = Sync(folders.exe, folders.game);
    CHECK(result.source_missing);
    CHECK_FALSE(fs::exists(folders.loaded));
  }

  SECTION("reports a script it could not write, and keeps going") {
    Write(folders.shipped / "F5.lua", "print('f5')");
    Write(folders.shipped / "console_pump.lua", "print('pump')");
    // A folder where one script should go: that script can't be written.
    fs::create_directories(folders.loaded / "F5.lua");
    const SyncResult result = Sync(folders.exe, folders.game);
    REQUIRE(result.failed.size() == 1);
    CHECK(result.failed[0] == "F5.lua");
    CHECK(result.copied == 1);
    CHECK(Read(folders.loaded / "console_pump.lua") == "print('pump')");
    CHECK_FALSE(fs::exists(folders.loaded / "F5.lua.tmp"));
  }

  SECTION("reports every script when the game folder can't take them") {
    Write(folders.shipped / "F5.lua", "print('f5')");
    // A file where the scripts folder should be.
    Write(folders.game / "data" / "scripts", "in the way");
    const SyncResult result = Sync(folders.exe, folders.game);
    CHECK(result.copied == 0);
    CHECK(result.failed.size() == 1);
  }
}
