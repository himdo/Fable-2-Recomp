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

// Opens the Advanced Options page (src/core/port_settings_dialog.h). In the
// pause menu it is the Advanced row of Options (src/core/port_settings_lua.h).
void OpenPortSettings();
// The same from the main menu, whose book frame sits elsewhere on wide screens.
void OpenPortSettingsFromMainMenu();

inline constexpr MenuItem kMainMenuItems[] = {
    {"RecompSettings", "advanced_options", OpenPortSettingsFromMainMenu},
    {"RecompQuit", "quit", QuitGame},
};

}  // namespace fable2::menu
