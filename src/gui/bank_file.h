// Reading and replacing the GUI bank files on disk. The banks are the player's
// own game data, so a write either replaces a file in full or leaves it as it
// was. Kept free of the other GUI headers so tests/native/test_bank_file.cpp
// can build it on its own.
#ifndef FABLE2_GUI_BANK_FILE_H
#define FABLE2_GUI_BANK_FILE_H

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <system_error>
#include <vector>

namespace fable2::gui::prompt_detail {

inline std::vector<uint8_t> ReadAllBytes(const std::filesystem::path& path) {
  std::vector<uint8_t> out;
  std::FILE* f = std::fopen(path.string().c_str(), "rb");
  if (!f) return out;
  std::fseek(f, 0, SEEK_END);
  const long len = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (len > 0) {
    out.resize(size_t(len));
    if (std::fread(out.data(), 1, out.size(), f) != out.size()) out.clear();
  }
  std::fclose(f);
  return out;
}

inline std::filesystem::path WithSuffix(std::filesystem::path path, const char* suffix) {
  path += suffix;
  return path;
}

// Writes `data` to <path>.tmp, beside the file it will replace.
inline bool StageFile(const std::filesystem::path& path, const std::vector<uint8_t>& data) {
  std::FILE* f = std::fopen(WithSuffix(path, ".tmp").string().c_str(), "wb");
  if (!f) return false;
  const bool wrote = data.empty() || std::fwrite(data.data(), 1, data.size(), f) == data.size();
  return std::fclose(f) == 0 && wrote;
}

// Renames <path>.tmp over `path`.
inline bool CommitStaged(const std::filesystem::path& path) {
  std::error_code ec;
  std::filesystem::rename(WithSuffix(path, ".tmp"), path, ec);
  return !ec;
}

inline void DropStaged(const std::filesystem::path& path) {
  std::error_code ec;
  std::filesystem::remove(WithSuffix(path, ".tmp"), ec);
}

// Replaces `path` with `data`, or leaves it exactly as it was: a full disk or
// a failed write can't leave half a file behind.
inline bool WriteIfChanged(const std::filesystem::path& path, const std::vector<uint8_t>& data) {
  if (ReadAllBytes(path) == data) return true;
  if (StageFile(path, data) && CommitStaged(path)) return true;
  DropStaged(path);
  return false;
}

enum class PairWrite {
  kDone,              // both files hold the new data
  kNothingWritten,    // neither file was touched
  kOnlyFirstWritten,  // the first was replaced and the second could not be
};

// Replaces two files that must match each other. Both new files are written in
// full before either one is replaced, so the usual failures (no space, no
// permission) touch neither.
inline PairWrite WriteBothIfChanged(const std::filesystem::path& first,
                                    const std::vector<uint8_t>& first_data,
                                    const std::filesystem::path& second,
                                    const std::vector<uint8_t>& second_data) {
  const bool write_first = ReadAllBytes(first) != first_data;
  const bool write_second = ReadAllBytes(second) != second_data;
  if ((write_first && !StageFile(first, first_data)) ||
      (write_second && !StageFile(second, second_data))) {
    DropStaged(first);
    DropStaged(second);
    return PairWrite::kNothingWritten;
  }
  if (write_first && !CommitStaged(first)) {
    DropStaged(first);
    DropStaged(second);
    return PairWrite::kNothingWritten;
  }
  if (write_second && !CommitStaged(second)) {
    DropStaged(second);
    return write_first ? PairWrite::kOnlyFirstWritten : PairWrite::kNothingWritten;
  }
  return PairWrite::kDone;
}

// Keeps the first, untouched copy of `bank` beside it as <bank>.orig. False
// when there is no such copy and one could not be made in full.
inline bool KeepOriginal(const std::filesystem::path& bank) {
  const auto orig = WithSuffix(bank, ".orig");
  const auto temp = WithSuffix(bank, ".orig.tmp");
  std::error_code ec;
  if (std::filesystem::exists(orig, ec)) return true;
  if (std::filesystem::copy_file(bank, temp, std::filesystem::copy_options::overwrite_existing,
                                 ec) &&
      !ec) {
    std::filesystem::rename(temp, orig, ec);
    if (!ec) return true;
  }
  std::filesystem::remove(temp, ec);
  return false;
}

}  // namespace fable2::gui::prompt_detail

#endif  // FABLE2_GUI_BANK_FILE_H
