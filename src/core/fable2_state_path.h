#pragma once

#include <cstdlib>
#include <filesystem>
#include <rex/filesystem.h>

namespace fable2 {
// Only the native app launcher sets this. Command builds retain their layout.
inline std::filesystem::path StateDirectory() {
#ifdef __APPLE__
  if (const char* value = std::getenv("FABLE2_APP_STATE_ROOT")) {
    std::filesystem::path path(value);
    if (path.is_absolute()) return path;
  }
#endif
  return rex::filesystem::GetExecutableFolder();
}
}  // namespace fable2
