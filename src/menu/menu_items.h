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

// Opens the PC Settings page (src/core/port_settings_dialog.h).
void OpenPortSettings();

inline constexpr MenuItem kMainMenuItems[] = {
    {"RecompSettings", "pc_settings", OpenPortSettings},
    {"RecompQuit", "quit", QuitGame},
};

// Added to the pause (Start) menu, after the game's own items.
inline constexpr MenuItem kPauseMenuItems[] = {
    {"RecompSettings", "pc_settings", OpenPortSettings},
};

}  // namespace fable2::menu
