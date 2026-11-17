// fable2_dir_manifest_heal.h
//
// Self-heals <game_data_root>/data/dir.manifest at startup.
//
// Why: the game's VFS indexes ALL of data/ from data/dir.manifest; a file that
// is not listed there is invisible to the guest, so content loads fail with
// null pointers (observed: deterministic "read of guest 0x0" AV at startup).
// The manifest is easy to lose or truncate:
//   - content migrated with an mtime-respecting copy (xcopy /D, robocopy, ...)
//     skips a newer build-created stub, or
//   - the build (tools/ensure_recomp_manifest.cmake) creates a stub containing
//     only the staged scripts\recomp\*.lua entries before the content exists.
// Fixing this by hand works but is fragile, so the exe repairs it itself.
//
// The manifest stores paths relative to the data/ root with backslashes and in
// the game's canonical (mostly lowercase) casing; the emulated FS matches
// case-insensitively (the shipped content ships e.g. as Shaders\Shaders.sbk
// while the manifest lists shaders\shaders.sbk), so entries are compared
// case-insensitively and with / and \ treated alike, and missing entries are
// appended in lowercase (the canonical casing).
//
// Idempotent: a second run changes nothing. Existing lines are never
// reordered or rewritten, so hand-added entries (e.g. from
// tools/stage_content.cmd's merge or ensure_recomp_manifest) survive. The one
// thing removed is a duplicate of an earlier line (same path; see below).
//
// Duplicates: an earlier version compared forward-slash paths against the
// manifest's backslash entries, so every nested file counted as "missing" and
// was appended again on EVERY launch (hundreds of lines per start). Such
// duplicate lines are removed once, keeping the first occurrence and the
// original order; the previous file is kept as dir.manifest.dupes.bak next to
// default.xex.
//
// Line endings: the manifest is guest data, so its line ending is a property
// of the file, not of the host OS. Lines are written with whatever ending the
// existing manifest already uses (detected from its first line), so the file
// stays consistent on Windows, Linux and macOS alike. Only a manifest with no
// line ending to copy (empty or a single unterminated line) falls back to
// CRLF, the format of the shipped dir.manifest.
#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_set>
#include <vector>

#include <rex/logging/macros.h>  // REXSYS_* logging

namespace fable2::manifestheal {

namespace detail {
// Canonical comparison key: lowercase, backslash separators.
inline std::string Key(std::string s) {
  for (char& c : s) {
    c = c == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

// Line ending used by the existing manifest contents: CRLF or LF, taken from
// the first line break. Falls back to the shipped manifest's CRLF when the
// file has no line break yet.
inline const char* DetectEol(const std::string& contents) {
  const size_t nl = contents.find('\n');
  if (nl == std::string::npos) return "\r\n";
  return nl > 0 && contents[nl - 1] == '\r' ? "\r\n" : "\n";
}
}  // namespace detail

// Ensures every file under <game_data_root>/data/ has an entry in
// <game_data_root>/data/dir.manifest, and removes duplicate lines. Returns the
// number of entries appended (0 when the manifest was already complete). Never
// fails hard: problems are logged and the startup continues with whatever
// manifest exists.
inline int EnsureComplete(const std::filesystem::path& game_data_root) {
  const std::filesystem::path manifest = game_data_root / "data" / "dir.manifest";
  std::error_code ec;
  const std::filesystem::path data_root = manifest.parent_path();
  if (!std::filesystem::is_directory(data_root, ec)) {
    REXSYS_WARN("[manifest] no {} directory; skipping dir.manifest self-heal",
                data_root.string());
    return 0;
  }

  // 1. Existing entries, in file order; count and skip duplicates.
  std::unordered_set<std::string> existing;
  size_t duplicates = 0;
  std::vector<std::string> unique_lines;
  bool ends_with_newline = true;
  std::string eol = "\r\n";
  {
    std::ifstream in(manifest, std::ios::binary);
    std::string line;
    if (in) {
      std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      ends_with_newline = all.empty() || all.back() == '\n';
      eol = detail::DetectEol(all);
      size_t pos = 0;
      while (pos < all.size()) {
        size_t nl = all.find('\n', pos);
        if (nl == std::string::npos) nl = all.size();
        line.assign(all, pos, nl - pos);
        pos = nl + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (existing.insert(detail::Key(line)).second) {
          unique_lines.push_back(line);
        } else {
          ++duplicates;
        }
      }
    }
  }

  // 1b. Drop duplicate lines (once), keeping a backup of the old file.
  if (duplicates > 0) {
    // Next to default.xex, not inside data/ (everything under data/ is indexed).
    const std::filesystem::path backup = game_data_root / "dir.manifest.dupes.bak";
    if (!std::filesystem::exists(backup, ec)) {
      std::filesystem::copy_file(manifest, backup, ec);
    }
    if (!ec) {
      std::ofstream out(manifest, std::ios::binary | std::ios::trunc);
      for (const auto& l : unique_lines) out << l << eol;
      out.flush();
      if (out) {
        ends_with_newline = true;
        REXSYS_INFO("[manifest] removed {} duplicate line(s) from {} (backup: {})", duplicates,
                    manifest.string(), backup.string());
      } else {
        REXSYS_ERROR("[manifest] could not rewrite {}; duplicates kept", manifest.string());
      }
    } else {
      REXSYS_WARN("[manifest] could not back up {}; duplicates kept", manifest.string());
    }
  }

  // 2. Everything under data/ (relative, backslash form, lowercased).
  std::vector<std::string> missing;
  // increment(ec), not a range-for: operator++ throws on an unreadable entry.
  std::filesystem::recursive_directory_iterator it(
      data_root, std::filesystem::directory_options::skip_permission_denied, ec);
  for (; !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
    const auto& entry = *it;
    std::error_code file_ec;
    if (!entry.is_regular_file(file_ec)) continue;
    std::string rel = detail::Key(entry.path().lexically_relative(data_root).generic_string());
    if (rel.empty() || rel == "dir.manifest") continue;  // the index itself
    if (existing.count(rel)) continue;
    missing.push_back(std::move(rel));
  }
  if (ec) {
    REXSYS_WARN("[manifest] stopped scanning {} early: {}", data_root.string(), ec.message());
  }
  if (missing.empty()) return 0;

  std::sort(missing.begin(), missing.end());

  // 3. Append (in the manifest's own line ending), starting a fresh line if the file did not end with one.
  std::ofstream out(manifest, std::ios::binary | std::ios::app);
  if (!out) {
    REXSYS_ERROR("[manifest] cannot open {} for append; guest VFS index will "
                 "be incomplete ({} unlisted file(s))",
                 manifest.string(), missing.size());
    return 0;
  }
  if (!ends_with_newline) out << eol;
  for (const auto& m : missing) {
    out << m << eol;  // lowercase relative path with backslashes
  }
  out.flush();
  REXSYS_INFO("[manifest] appended {} missing entr{} to {}", missing.size(),
              missing.size() == 1 ? "y" : "ies", manifest.string());
  return static_cast<int>(missing.size());
}

}  // namespace fable2::manifestheal
