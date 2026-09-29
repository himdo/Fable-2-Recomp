#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

#include <SDL3/SDL.h>

namespace fable2 {

// Owns a watch on the game's SDL window. Attach and destruction run on the UI
// thread. Only windowed moves are saved; window dimensions are not changed.
class WindowPosition {
 public:
  WindowPosition() = default;
  ~WindowPosition();
  WindowPosition(const WindowPosition&) = delete;
  WindowPosition& operator=(const WindowPosition&) = delete;

  bool Attach(std::string_view title, const std::filesystem::path& path,
              bool restore = true);

 private:
  static bool SDLCALL Watch(void* context, SDL_Event* event);
  void Restore();
  void Save(SDL_Point position);

  SDL_Window* window_ = nullptr;
  SDL_WindowID window_id_ = 0;
  std::filesystem::path path_;
  std::optional<SDL_Point> saved_;
  bool watching_ = false;
  bool restoring_ = false;
  bool warned_ = false;
};

}  // namespace fable2
