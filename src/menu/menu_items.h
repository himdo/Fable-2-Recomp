// Menu items the recomp adds to the game's own menus.

#pragma once

#include <functional>
#include <string_view>

namespace fable2::menu {

struct MenuItem {
  // Unique item name the game passes back when the item is chosen.
  std::string_view id;
  // Key into kLabels (menu_labels.h).
  std::string_view label;
  void (*activate)();
};

// Called on the game thread; the app closes the window from the UI thread.
void SetQuitHandler(std::function<void()> handler);
void QuitGame();

inline constexpr MenuItem kMainMenuItems[] = {
    {"RecompQuit", "quit", QuitGame},
};

}  // namespace fable2::menu
