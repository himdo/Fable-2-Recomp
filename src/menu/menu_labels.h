// Text for the menu items the recomp adds to the game's menus.

#pragma once

#include <string_view>

namespace fable2::menu {

struct Label {
  std::string_view key;
  std::string_view language;
  std::u16string_view text;
};

inline constexpr std::string_view kFallbackLanguage = "en";

inline constexpr Label kLabels[] = {
    {"quit", "en", u"Quit"},
};

// Only English exists so far, so this always returns the fallback.
inline std::string_view CurrentLanguage() { return kFallbackLanguage; }

inline std::u16string_view LabelText(std::string_view key) {
  const std::string_view language = CurrentLanguage();
  std::u16string_view fallback = {};
  for (const Label& label : kLabels) {
    if (label.key != key) continue;
    if (label.language == language) return label.text;
    if (label.language == kFallbackLanguage) fallback = label.text;
  }
  return fallback.empty() ? std::u16string_view(u"?") : fallback;
}

}  // namespace fable2::menu
