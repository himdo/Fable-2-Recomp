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
    {"quit", "pt-br", u"Sair"},
};

inline std::u16string_view LabelText(std::string_view key, std::string_view language) {
  std::u16string_view fallback = {};
  for (const Label& label : kLabels) {
    if (label.key != key) continue;
    if (label.language == language) return label.text;
    if (label.language == kFallbackLanguage) fallback = label.text;
  }
  return fallback.empty() ? std::u16string_view(u"?") : fallback;
}

}  // namespace fable2::menu
