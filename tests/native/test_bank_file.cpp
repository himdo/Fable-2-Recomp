// GUI bank files on disk: a write replaces a file in full or leaves it alone.
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "catch_amalgamated.hpp"
#include "gui/bank_file.h"

namespace {

namespace fs = std::filesystem;
using namespace fable2::gui::prompt_detail;

std::vector<uint8_t> Bytes(const char* text) {
  const std::string s(text);
  return std::vector<uint8_t>(s.begin(), s.end());
}

// A fresh, empty folder, removed again when the section ends.
struct Folder {
  fs::path dir;
  explicit Folder(const char* name)
      : dir(fs::temp_directory_path() / "fable2-bank-file-test" / name) {
    fs::remove_all(dir);
    fs::create_directories(dir);
  }
  ~Folder() {
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
};

bool NothingStaged(const fs::path& dir) {
  for (const auto& entry : fs::directory_iterator(dir))
    if (entry.path().extension() == ".tmp") return false;
  return true;
}

}  // namespace

TEST_CASE("WriteIfChanged", "[bank]") {
  const Folder folder("write");
  const fs::path bank = folder.dir / "textures.bnk";

  SECTION("writes a new file") {
    REQUIRE(WriteIfChanged(bank, Bytes("first")));
    CHECK(ReadAllBytes(bank) == Bytes("first"));
    CHECK(NothingStaged(folder.dir));
  }

  SECTION("replaces an existing file") {
    REQUIRE(WriteIfChanged(bank, Bytes("first")));
    REQUIRE(WriteIfChanged(bank, Bytes("second, longer")));
    CHECK(ReadAllBytes(bank) == Bytes("second, longer"));
    CHECK(NothingStaged(folder.dir));
  }

  SECTION("succeeds when the file already holds the data") {
    REQUIRE(WriteIfChanged(bank, Bytes("same")));
    CHECK(WriteIfChanged(bank, Bytes("same")));
    CHECK(ReadAllBytes(bank) == Bytes("same"));
  }

  SECTION("leaves the file alone when the new data can't be staged") {
    REQUIRE(WriteIfChanged(bank, Bytes("original")));
    // A folder where the temporary file would go.
    fs::create_directories(folder.dir / "textures.bnk.tmp");
    CHECK_FALSE(WriteIfChanged(bank, Bytes("replacement")));
    CHECK(ReadAllBytes(bank) == Bytes("original"));
  }

  SECTION("fails in a missing folder, and creates nothing") {
    CHECK_FALSE(WriteIfChanged(folder.dir / "missing" / "textures.bnk", Bytes("x")));
    CHECK_FALSE(fs::exists(folder.dir / "missing"));
  }
}

TEST_CASE("WriteBothIfChanged", "[bank]") {
  const Folder folder("pair");
  const fs::path textures = folder.dir / "textures.bnk";
  const fs::path headers = folder.dir / "headers.bnk";
  REQUIRE(WriteIfChanged(textures, Bytes("old textures")));
  REQUIRE(WriteIfChanged(headers, Bytes("old headers")));

  SECTION("replaces both banks") {
    const PairWrite written =
        WriteBothIfChanged(textures, Bytes("new textures"), headers, Bytes("new headers"));
    CHECK(written == PairWrite::kDone);
    CHECK(ReadAllBytes(textures) == Bytes("new textures"));
    CHECK(ReadAllBytes(headers) == Bytes("new headers"));
    CHECK(NothingStaged(folder.dir));
  }

  SECTION("succeeds when both banks already hold the data") {
    const PairWrite written =
        WriteBothIfChanged(textures, Bytes("old textures"), headers, Bytes("old headers"));
    CHECK(written == PairWrite::kDone);
    CHECK(NothingStaged(folder.dir));
  }

  SECTION("writes neither bank when the second can't be staged") {
    fs::create_directories(folder.dir / "headers.bnk.tmp");
    const PairWrite written =
        WriteBothIfChanged(textures, Bytes("new textures"), headers, Bytes("new headers"));
    CHECK(written == PairWrite::kNothingWritten);
    CHECK(ReadAllBytes(textures) == Bytes("old textures"));
    CHECK(ReadAllBytes(headers) == Bytes("old headers"));
    CHECK_FALSE(fs::exists(folder.dir / "textures.bnk.tmp"));
  }
}

TEST_CASE("KeepOriginal", "[bank]") {
  const Folder folder("original");
  const fs::path bank = folder.dir / "textures.bnk";
  const fs::path backup = folder.dir / "textures.bnk.orig";

  SECTION("keeps nothing for a missing bank") {
    CHECK_FALSE(KeepOriginal(bank));
    CHECK_FALSE(fs::exists(backup));
  }

  SECTION("copies the bank beside it") {
    REQUIRE(WriteIfChanged(bank, Bytes("untouched")));
    CHECK(KeepOriginal(bank));
    CHECK(ReadAllBytes(backup) == Bytes("untouched"));
    CHECK(NothingStaged(folder.dir));
  }

  SECTION("never overwrites an existing backup") {
    REQUIRE(WriteIfChanged(bank, Bytes("untouched")));
    REQUIRE(KeepOriginal(bank));
    REQUIRE(WriteIfChanged(bank, Bytes("rewritten")));
    CHECK(KeepOriginal(bank));
    CHECK(ReadAllBytes(backup) == Bytes("untouched"));
  }
}
