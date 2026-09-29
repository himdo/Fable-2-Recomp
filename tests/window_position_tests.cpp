#include "fable2_window_position.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <unistd.h>

#include <toml++/toml.hpp>

#define CHECK(condition) do { if (!(condition)) { \
  std::fprintf(stderr, "FAIL line %d: %s (SDL: %s)\n", __LINE__, #condition, SDL_GetError()); \
  std::abort(); } } while (false)

namespace {
constexpr const char* kTitle = "Fable window position test";

SDL_Point Position(SDL_Window* window) {
  SDL_Point point;
  CHECK(SDL_GetWindowPosition(window, &point.x, &point.y));
  return point;
}

bool At(SDL_Window* window, int x, int y) {
  const auto position = Position(window);
  return position.x == x && position.y == y;
}

bool SavedAt(const std::filesystem::path& path, int x, int y) {
  const auto table = toml::parse_file(path.string());
  return table["x"].value<int>() == x && table["y"].value<int>() == y;
}

void Move(SDL_Window* window, int x, int y) {
  CHECK(SDL_SetWindowPosition(window, x, y));
  CHECK(SDL_SyncWindow(window));
  SDL_PumpEvents();
}
}  // namespace

int main(int argc, char** argv) {
  CHECK(SDL_Init(SDL_INIT_VIDEO));
  char temp[] = "/tmp/fable-window-test.XXXXXX";
  CHECK(mkdtemp(temp));
  const std::filesystem::path folder(temp);
  const auto file = folder / "position.toml";
  const bool desktop = argc > 1 && std::string(argv[1]) == "--desktop";
  const SDL_WindowFlags flags = desktop ? 0 : SDL_WINDOW_HIDDEN;

  auto* window = SDL_CreateWindow(kTitle, 320, 240, flags);
  CHECK(window);
  Move(window, 100, 100);
  {
    fable2::WindowPosition position;
    CHECK(position.Attach(kTitle, file));
    CHECK(!std::filesystem::exists(file));
    Move(window, 240, 180);
    CHECK(At(window, 240, 180));
    CHECK(SavedAt(file, 240, 180));
    const auto saved_time = std::filesystem::last_write_time(file);

    // Moves from other windows and injected off-thread events must not
    // change the game window's saved coordinates.
    auto* other = SDL_CreateWindow("Other window", 100, 100, SDL_WINDOW_HIDDEN);
    CHECK(other);
    Move(other, 400, 300);
    CHECK(std::filesystem::last_write_time(file) == saved_time);
    SDL_Event event{};
    event.type = SDL_EVENT_WINDOW_MOVED;
    event.window.windowID = SDL_GetWindowID(window);
    event.window.data1 = 999;
    event.window.data2 = 999;
    std::thread thread([&] { CHECK(SDL_PushEvent(&event)); });
    thread.join();
    CHECK(SavedAt(file, 240, 180));
    SDL_DestroyWindow(other);

    // Exercise fullscreen filtering while hidden. The visible test avoids
    // switching the user's Space.
    if (!desktop) {
      CHECK(SDL_SetWindowFullscreen(window, true));
      CHECK(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN);
      CHECK(SDL_PushEvent(&event));
      CHECK(SavedAt(file, 240, 180));
      CHECK(SDL_SetWindowFullscreen(window, false));
      SDL_PumpEvents();
      CHECK(SavedAt(file, 240, 180));
    }
    SDL_DestroyWindow(window);
  }

  window = SDL_CreateWindow(kTitle, 400, 260, flags);
  CHECK(window);
  // Leave the initial placement pending, as it is during game startup.
  CHECK(SDL_SetWindowPosition(window, 80, 100));
  {
    fable2::WindowPosition position;
    CHECK(position.Attach(kTitle, file));
    CHECK(SDL_SyncWindow(window));
    SDL_PumpEvents();
    CHECK(At(window, 240, 180));
    int width, height;
    CHECK(SDL_GetWindowSize(window, &width, &height));
    CHECK(width == 400 && height == 260);  // Position saving must not resize.
  }
  Move(window, 100, 100);
  {
    fable2::WindowPosition position;
    CHECK(position.Attach(kTitle, file, false));
    CHECK(At(window, 100, 100));  // Explicit monitor placement wins.
    Move(window, 260, 200);
    CHECK(SavedAt(file, 260, 200));
  }

  for (const auto* text : {"x = 999999\ny = -999999\n", "invalid = [", "x = 'wrong type'\ny = 200\n"}) {
    std::ofstream(file) << text;
    fable2::WindowPosition position;
    CHECK(position.Attach(kTitle, file));
    CHECK(At(window, 260, 200));  // Bad/off-screen state keeps normal placement.
  }
  {
    fable2::WindowPosition position;
    CHECK(position.Attach(kTitle, folder / "missing-directory/position.toml"));
    Move(window, 280, 220);  // An unwritable state path must not break movement.
    CHECK(At(window, 280, 220));
  }
  SDL_DestroyWindow(window);
  SDL_Quit();
  std::filesystem::remove_all(folder);
  std::puts("Window position save/restore checks passed.");
}
