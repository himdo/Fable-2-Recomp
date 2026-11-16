// GUI bank files on disk: a write replaces a file in full or leaves it alone.
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "gui/bank_file.h"

namespace {

namespace fs = std::filesystem;
using namespace fable2::gui::prompt_detail;

int failures = 0;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

std::vector<uint8_t> Bytes(const char* text) {
  const std::string s(text);
  return std::vector<uint8_t>(s.begin(), s.end());
}

// A fresh, empty folder for one test.
fs::path Folder(const char* name) {
  const fs::path dir = fs::temp_directory_path() / "fable2-bank-file-test" / name;
  fs::remove_all(dir);
  fs::create_directories(dir);
  return dir;
}

bool NothingStaged(const fs::path& dir) {
  for (const auto& entry : fs::directory_iterator(dir))
    if (entry.path().extension() == ".tmp") return false;
  return true;
}

void WriteCreatesAndReplaces() {
  const fs::path dir = Folder("write");
  const fs::path bank = dir / "textures.bnk";
  Check(WriteIfChanged(bank, Bytes("first")), "a new file is written");
  Check(ReadAllBytes(bank) == Bytes("first"), "the new file holds the data");
  Check(WriteIfChanged(bank, Bytes("second, longer")), "an existing file is replaced");
  Check(ReadAllBytes(bank) == Bytes("second, longer"), "the file holds the new data");
  Check(WriteIfChanged(bank, Bytes("second, longer")), "the same data again succeeds");
  Check(NothingStaged(dir), "no temporary file is left after a write");
}

void FailedWriteLeavesTheFileAlone() {
  const fs::path dir = Folder("failed-write");
  const fs::path bank = dir / "textures.bnk";
  Check(WriteIfChanged(bank, Bytes("original")), "set up the file");
  // A folder where the temporary file would go: the new data can't be staged.
  fs::create_directories(dir / "textures.bnk.tmp");
  Check(!WriteIfChanged(bank, Bytes("replacement")), "a write that can't be staged fails");
  Check(ReadAllBytes(bank) == Bytes("original"), "the file is unchanged after a failed write");
  Check(!WriteIfChanged(dir / "missing" / "textures.bnk", Bytes("x")),
        "a write into a missing folder fails");
  Check(!fs::exists(dir / "missing"), "a failed write creates nothing");
}

void PairIsReplacedTogether() {
  const fs::path dir = Folder("pair");
  const fs::path textures = dir / "textures.bnk", headers = dir / "headers.bnk";
  Check(WriteIfChanged(textures, Bytes("old textures")), "set up the texture bank");
  Check(WriteIfChanged(headers, Bytes("old headers")), "set up the header bank");
  Check(WriteBothIfChanged(textures, Bytes("new textures"), headers, Bytes("new headers")) ==
            PairWrite::kDone,
        "both banks are replaced");
  Check(ReadAllBytes(textures) == Bytes("new textures"), "the texture bank holds the new data");
  Check(ReadAllBytes(headers) == Bytes("new headers"), "the header bank holds the new data");
  Check(WriteBothIfChanged(textures, Bytes("new textures"), headers, Bytes("new headers")) ==
            PairWrite::kDone,
        "the same pair again succeeds");
  Check(NothingStaged(dir), "no temporary file is left after a pair write");
}

void PairIsLeftAloneWhenOneCannotBeStaged() {
  const fs::path dir = Folder("pair-failed");
  const fs::path textures = dir / "textures.bnk", headers = dir / "headers.bnk";
  Check(WriteIfChanged(textures, Bytes("old textures")), "set up the texture bank");
  Check(WriteIfChanged(headers, Bytes("old headers")), "set up the header bank");
  fs::create_directories(dir / "headers.bnk.tmp");  // the header bank can't be staged
  Check(WriteBothIfChanged(textures, Bytes("new textures"), headers, Bytes("new headers")) ==
            PairWrite::kNothingWritten,
        "a pair that can't both be staged is not written");
  Check(ReadAllBytes(textures) == Bytes("old textures"),
        "the texture bank is unchanged when the header bank can't be written");
  Check(ReadAllBytes(headers) == Bytes("old headers"), "the header bank is unchanged");
  Check(!fs::exists(dir / "textures.bnk.tmp"), "the staged texture bank is removed");
}

void OriginalIsKeptOnce() {
  const fs::path dir = Folder("original");
  const fs::path bank = dir / "textures.bnk";
  Check(!KeepOriginal(bank), "a missing bank has no original to keep");
  Check(!fs::exists(dir / "textures.bnk.orig"), "no backup is made of a missing bank");
  Check(WriteIfChanged(bank, Bytes("untouched")), "set up the bank");
  Check(KeepOriginal(bank), "the original is kept");
  Check(ReadAllBytes(dir / "textures.bnk.orig") == Bytes("untouched"), "the backup holds the bank");
  Check(WriteIfChanged(bank, Bytes("rewritten")), "rewrite the bank");
  Check(KeepOriginal(bank), "an existing backup counts as kept");
  Check(ReadAllBytes(dir / "textures.bnk.orig") == Bytes("untouched"),
        "the backup is not overwritten by a rewritten bank");
  Check(NothingStaged(dir), "no temporary file is left after a backup");
}

}  // namespace

int main() {
  WriteCreatesAndReplaces();
  FailedWriteLeavesTheFileAlone();
  PairIsReplacedTogether();
  PairIsLeftAloneWhenOneCannotBeStaged();
  OriginalIsKeptOnce();
  std::error_code ec;
  fs::remove_all(fs::temp_directory_path() / "fable2-bank-file-test", ec);
  if (failures) return 1;
  std::printf("PASS: bank file tests\n");
  return 0;
}
