#include "fable2_window_position.h"

#include <cstdio>
#include <cstdint>
#include <string>
#include <unistd.h>

#include <rex/logging.h>
#include <toml++/toml.hpp>

namespace fable2 {

WindowPosition::~WindowPosition() {
  if (watching_) SDL_RemoveEventWatch(Watch, this);
}

bool WindowPosition::Attach(std::string_view title, const std::filesystem::path& path,
                            bool restore) {
  if (watching_ || !SDL_IsMainThread()) return false;
  int count = 0;
  auto** windows = SDL_GetWindows(&count);
  for (int i = 0; i < count; ++i) {
    if (title == SDL_GetWindowTitle(windows[i])) {
      window_ = windows[i];
      break;
    }
  }
  SDL_free(windows);
  if (!window_) return false;
  // Cocoa may still have placement events pending from opening the window.
  // Finish those before restoring and observing moves so they cannot replace
  // the saved position with the initial default location.
  SDL_SyncWindow(window_);
  window_id_ = SDL_GetWindowID(window_);
  path_ = path;
  std::error_code error;
  if (std::filesystem::exists(path_, error)) {
    try {
      const auto table = toml::parse_file(path_.string());
      const auto x = table["x"].value<int32_t>();
      const auto y = table["y"].value<int32_t>();
      if (x && y) saved_ = SDL_Point{*x, *y};
    } catch (const toml::parse_error&) {
      REXLOG_WARN("Ignoring invalid saved window position in {}", path_.string());
    }
  }
  // An explicit monitor selection takes priority over the saved location.
  if (restore) Restore();
  else saved_.reset();
  watching_ = SDL_AddEventWatch(Watch, this);
  return watching_;
}

void WindowPosition::Restore() {
  if (!saved_ || !window_ ||
      (SDL_GetWindowFlags(window_) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MAXIMIZED))) return;

  int top = 0;
  SDL_GetWindowBordersSize(window_, &top, nullptr, nullptr, nullptr);
  int count = 0;
  auto* displays = SDL_GetDisplays(&count);
  bool visible = false;
  for (int i = 0; i < count; ++i) {
    SDL_Rect bounds;
    if (!SDL_GetDisplayUsableBounds(displays[i], &bounds)) continue;
    // Keep a usable portion of the title bar on a connected display. Use
    // wide arithmetic for coordinates read from disk, including negative X/Y
    // on secondary displays. If the display disappeared, keep SDL's placement.
    const int64_t x = saved_->x;
    const int64_t y = int64_t(saved_->y) - top;
    if (x >= int64_t(bounds.x) - 64 && x + 64 <= int64_t(bounds.x) + bounds.w &&
        y >= bounds.y && y + 32 <= int64_t(bounds.y) + bounds.h) {
      visible = true;
      break;
    }
  }
  SDL_free(displays);
  if (visible) {
    restoring_ = true;
    if (SDL_SetWindowPosition(window_, saved_->x, saved_->y)) {
      REXLOG_INFO("Restored window position: {}, {}", saved_->x, saved_->y);
    }
    restoring_ = false;
  }
}

void WindowPosition::Save(SDL_Point position) {
  if (saved_ && saved_->x == position.x && saved_->y == position.y) return;
  // Save on movement rather than shutdown: the SDK hard-exits when the game
  // window closes. A unique temporary file preserves the last good position
  // if writing fails and keeps simultaneous launches from sharing a temp file.
  std::string temporary = path_.string() + ".tmp.XXXXXX";
  const int fd = mkstemp(temporary.data());
  bool success = false;
  if (fd >= 0) {
    if (FILE* file = fdopen(fd, "w")) {
      const bool written = std::fprintf(file, "# Last windowed position (screen coordinates).\nx = %d\ny = %d\n",
                                        position.x, position.y) > 0;
      success = std::fclose(file) == 0 && written;
    } else {
      close(fd);
    }
    std::error_code error;
    if (success) {
      std::filesystem::rename(temporary, path_, error);
      success = !error;
    }
    if (!success) std::filesystem::remove(temporary, error);
  }
  if (success) {
    saved_ = position;
  } else if (!warned_) {
    warned_ = true;
    REXLOG_WARN("Could not save window position to {}", path_.string());
  }
}

bool SDLCALL WindowPosition::Watch(void* context, SDL_Event* event) {
  // SDL event watches may also be invoked by non-UI threads. Native window
  // moves are delivered on the UI thread; never call video APIs elsewhere.
  if (!SDL_IsMainThread()) return true;
  auto& self = *static_cast<WindowPosition*>(context);
  if (event->type < SDL_EVENT_WINDOW_FIRST || event->type > SDL_EVENT_WINDOW_LAST ||
      event->window.windowID != self.window_id_) return true;
  if (event->type == SDL_EVENT_WINDOW_DESTROYED) {
    self.window_ = nullptr;
  } else if (event->type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN) {
    self.Restore();
  } else if (event->type == SDL_EVENT_WINDOW_MOVED && self.window_ && !self.restoring_ &&
             !(SDL_GetWindowFlags(self.window_) &
               (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED))) {
    self.Save({event->window.data1, event->window.data2});
  }
  return true;  // A watch observes events; it never consumes them.
}

}  // namespace fable2
