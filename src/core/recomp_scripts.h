// recomp_scripts.h
//
// Keeps the recomp Lua scripts where the game can load them.
//
// The scripts (F5.lua, the console pump, the direct mouse camera and so on)
// ship in data/scripts/recomp next to fable_2.exe, but the guest loads them by
// path from the game folder. When the two are the same folder there is nothing
// to do. With the game files somewhere else (--game_data_root) the guest never
// finds them, so Sync copies them across at start-up.
//
// The copy next to the executable is the one that counts: a script in the game
// folder that differs from it is replaced. That keeps the scripts matched to
// the executable when several builds share one game folder. Scripts that only
// exist in the game folder are left alone.
//
// One script is different: the console writes console_run.lua in the game
// folder each time it runs a Lua command, so it is only copied when missing.
//
// Run it before the runtime mounts the game folder. The folder is listed once,
// at the mount, and the guest cannot open a file that appears later, so a
// script copied after that only works from the next start. It must also run
// before manifestheal::EnsureComplete, which lists the copied files in
// data/dir.manifest. Standard library only, so the native test can build it.
#pragma once

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

namespace fable2::recompscripts {

namespace detail {

inline bool ReadFile(const std::filesystem::path& path, std::string& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return !in.bad();
}

// Writes to a temporary file and renames it into place, so a failed write
// never leaves half a script (or a stray file for the manifest to index).
inline bool ReplaceFile(const std::filesystem::path& dest, const std::string& data) {
  std::filesystem::path staged = dest;
  staged += ".tmp";
  std::error_code ec;
  {
    std::ofstream out(staged, std::ios::binary | std::ios::trunc);
    if (out) out.write(data.data(), static_cast<std::streamsize>(data.size()));
    out.flush();
    if (!out) {
      out.close();
      std::filesystem::remove(staged, ec);
      return false;
    }
  }
  std::filesystem::rename(staged, dest, ec);
  if (ec) {
    std::filesystem::remove(staged, ec);
    return false;
  }
  return true;
}

inline bool SameFolder(const std::filesystem::path& a, const std::filesystem::path& b) {
  std::error_code ec;
  if (std::filesystem::equivalent(a, b, ec)) return true;
  return a.lexically_normal() == b.lexically_normal();
}

inline std::string Lower(std::string text) {
  for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return text;
}

inline bool IsLua(const std::filesystem::path& path) {
  return Lower(path.extension().string()) == ".lua";
}

// The console's scratch file (dispatch_lua in fable2_console.h): the game
// folder's copy holds the last command and is rewritten for the next one.
inline bool IsWrittenAtRunTime(const std::filesystem::path& name) {
  return Lower(name.string()) == "console_run.lua";
}

}  // namespace detail

struct SyncResult {
  bool same_folder = false;     // nothing to do: the game folder is the executable's
  bool source_missing = false;  // no data/scripts/recomp next to the executable
  int copied = 0;
  int unchanged = 0;
  std::vector<std::string> failed;  // file names that could not be read or written
};

// Makes <game_data_root>/data/scripts/recomp hold every .lua file from
// <exe_dir>/data/scripts/recomp. Never throws; problems come back in the result.
inline SyncResult Sync(const std::filesystem::path& exe_dir,
                       const std::filesystem::path& game_data_root) {
  SyncResult result;
  if (detail::SameFolder(exe_dir, game_data_root)) {
    result.same_folder = true;
    return result;
  }
  const std::filesystem::path from = exe_dir / "data" / "scripts" / "recomp";
  const std::filesystem::path to = game_data_root / "data" / "scripts" / "recomp";
  std::error_code ec;
  if (!std::filesystem::is_directory(from, ec)) {
    result.source_missing = true;
    return result;
  }
  // A failure here shows up below, as every script failing to copy.
  std::filesystem::create_directories(to, ec);

  // increment(ec), not a range-for: operator++ throws on an unreadable entry.
  std::filesystem::directory_iterator it(from, ec);
  for (; !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
    std::error_code file_ec;
    if (!it->is_regular_file(file_ec) || !detail::IsLua(it->path())) continue;
    const std::filesystem::path name = it->path().filename();
    if (detail::IsWrittenAtRunTime(name) && std::filesystem::exists(to / name, file_ec)) {
      ++result.unchanged;
      continue;
    }
    std::string wanted;
    if (!detail::ReadFile(it->path(), wanted)) {
      result.failed.push_back(name.string());
      continue;
    }
    std::string present;
    if (detail::ReadFile(to / name, present) && present == wanted) {
      ++result.unchanged;
    } else if (detail::ReplaceFile(to / name, wanted)) {
      ++result.copied;
    } else {
      result.failed.push_back(name.string());
    }
  }
  return result;
}

}  // namespace fable2::recompscripts
