// Which keycap label each controller prompt shows, from the host keyboard ->
// guest gamepad map ("Key:Button,...").
#ifndef FABLE2_GUI_PROMPT_LABELS_H
#define FABLE2_GUI_PROMPT_LABELS_H

#include <cctype>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fable2::gui {

// Lowercase a string (for case-insensitive lookup keys).
inline std::string Ci(const std::string& s) {
  std::string r = s;
  for (char& c : r) c = char(std::tolower((unsigned char)c));
  return r;
}

// Guest-input action aliases (lowercased key -> canonical button).
inline const std::unordered_map<std::string, std::string>& ButtonAliases() {
  static const std::unordered_map<std::string, std::string> m = {
      {"lshoulder", "LB"}, {"rshoulder", "RB"}, {"ltrigger", "LT"}, {"rtrigger", "RT"},
      {"dup", "Up"}, {"ddown", "Down"}, {"dleft", "Left"}, {"dright", "Right"},
      {"start", "Pause"}, {"back", "Select"}, {"view", "Select"},
      {"lthumb", "L3"}, {"rthumb", "R3"},
      {"lu", "StickUp"}, {"ld", "StickDown"}, {"ll", "StickLeft"}, {"lr", "StickRight"},
  };
  return m;
}

// Mouse-button aliases (lowercased key -> canonical mouse name).
inline const std::unordered_map<std::string, std::string>& MouseAliases() {
  static const std::unordered_map<std::string, std::string> m = {
      {"lmb", "LMB"}, {"leftmouse", "LMB"}, {"rmb", "RMB"}, {"rightmouse", "RMB"},
      {"mmb", "MMB"}, {"middlemouse", "MMB"},
      {"xmb1", "XMB1"}, {"xbutton1", "XMB1"}, {"sidebutton1", "XMB1"},
      {"xmb2", "XMB2"}, {"xbutton2", "XMB2"}, {"sidebutton2", "XMB2"},
  };
  return m;
}

// Short display names for long host keys (exact host-key name, case-sensitive).
inline const std::unordered_map<std::string, std::string>& ShortKeys() {
  static const std::unordered_map<std::string, std::string> m = {
      {"Escape", "Esc"}, {"Return", "Enter"}, {"Backspace", "Bksp"}, {"Control", "Ctrl"},
      {"PageUp", "PgUp"}, {"PageDown", "PgDn"}, {"PrintScreen", "PrtSc"},
      {"CapsLock", "Caps"}, {"NumLock", "NumLk"}, {"ScrollLock", "ScrLk"},
      {"Up", "\u2191"}, {"Down", "\u2193"}, {"Left", "\u2190"}, {"Right", "\u2192"},
  };
  return m;
}

// Valid host key names (rex::ui::ParseVirtualKey + mouse buttons), case-sensitive.
inline const std::unordered_set<std::string>& KeyNames() {
  static const std::unordered_set<std::string> s = {
      "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
      "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
      "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
      "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12", "F13", "F14",
      "F15", "F16", "F17", "F18", "F19", "F20", "F21", "F22", "F23", "F24",
      "Backtick", "Minus", "Plus", "Comma", "Period", "Semicolon", "Slash", "Backslash",
      "LBracket", "RBracket", "Quote",
      "Escape", "Return", "Space", "Tab", "Backspace", "Delete", "Insert", "Home", "End",
      "PageUp", "PageDown", "Left", "Right", "Up", "Down", "Shift", "Control", "Alt",
      "PrintScreen", "Pause", "CapsLock", "NumLock", "ScrollLock",
      "Numpad0", "Numpad1", "Numpad2", "Numpad3", "Numpad4", "Numpad5", "Numpad6",
      "Numpad7", "Numpad8", "Numpad9", "NumpadEnter", "NumpadPlus", "NumpadMinus",
      "NumpadStar", "NumpadSlash",
      "LMB", "RMB", "MMB", "XMB1", "XMB2",
  };
  return s;
}

// Valid guest-input button names (lowercased for case-insensitive lookup).
inline const std::unordered_set<std::string>& ButtonNames() {
  static const std::unordered_set<std::string> s = {
      "a", "b", "x", "y", "lb", "rb", "lt", "rt",
      "up", "down", "left", "right", "pause", "select", "l3", "r3",
      "stickup", "stickdown", "stickleft", "stickright",
  };
  return s;
}

// One Key:Button binding parsed from the map.
struct KeybindEntry {
  std::string key;
  std::string button;
};

inline std::string Trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t");
  if (a == std::string::npos) return "";
  size_t b = s.find_last_not_of(" \t");
  return s.substr(a, b - a + 1);
}

inline std::vector<KeybindEntry> ParseKeybinds(const std::string& map) {
  std::vector<KeybindEntry> entries;
  if (Trim(map).empty()) return entries;
  size_t start = 0;
  while (start <= map.size()) {
    size_t comma = map.find(',', start);
    std::string part = Trim(comma == std::string::npos ? map.substr(start)
                                                        : map.substr(start, comma - start));
    if (!part.empty()) {
      size_t colon = part.find(':');
      if (colon != std::string::npos)
        entries.push_back({Trim(part.substr(0, colon)), Trim(part.substr(colon + 1))});
    }
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  return entries;
}

// Resolves the display label for each guest button (the "target" of a binding):
// a mouse button or a regular key wins over an alternative modifier/mouse button
// for the same action (e.g. E over Shift, B over MMB). `reservedKey` (a host key
// used for UI rather than an action) is excluded.
inline std::unordered_map<std::string, std::string> ResolveLabels(const std::string& map,
                                                                  const std::string& reservedKey) {
  std::unordered_map<std::string, std::string> labels;
  std::unordered_map<std::string, int> priorities;
  for (const auto& binding : ParseKeybinds(map)) {
    const auto& mouse = MouseAliases();
    bool isMouse = mouse.find(Ci(binding.key)) != mouse.end();
    std::string key = isMouse ? mouse.at(Ci(binding.key)) : binding.key;
    if (KeyNames().find(key) == KeyNames().end() || key == reservedKey) continue;
    const auto& aliases = ButtonAliases();
    std::string target = aliases.count(Ci(binding.button)) ? aliases.at(Ci(binding.button))
                                                           : binding.button;
    if (ButtonNames().find(Ci(target)) == ButtonNames().end()) continue;

    // A regular key beats an alternative modifier or mouse button for the same
    // action (e.g. E over Shift, B over MMB).
    int priority = (isMouse || key == "Shift" || key == "Control" || key == "Alt") ? 1 : 2;
    auto pit = priorities.find(Ci(target));
    if (pit != priorities.end() && priority <= pit->second) continue;
    priorities[Ci(target)] = priority;
    const auto& shortkeys = ShortKeys();
    std::string display = shortkeys.count(key) ? shortkeys.at(key) : key;
    labels[Ci(target)] = display;
  }
  return labels;
}

inline std::optional<std::string> Directions(
    const std::unordered_map<std::string, std::string>& labels, const std::string& prefix) {
  const char* order[4] = {"Up", "Left", "Down", "Right"};
  std::string keys[4];
  for (int i = 0; i < 4; ++i) {
    auto it = labels.find(Ci(prefix + order[i]));
    if (it == labels.end()) return std::nullopt;
    keys[i] = it->second;
  }
  return keys[0] + " " + keys[1] + "\n" + keys[2] + " " + keys[3];
}

// Maps a bank entry name (a controller .tex, e.g. "Art\GUI\Controller\icon_button_a.tex")
// to its keycap label, or nullopt when the entry is not a controller prompt icon.
inline std::optional<std::string> IconLabel(const std::string& name,
                                            const std::unordered_map<std::string, std::string>& labels,
                                            bool mouseLook) {
  const std::string prefix = "art\\gui\\controller\\";
  std::string lower = Ci(name);
  if (lower.rfind(prefix, 0) != 0 || lower.size() < 4 + prefix.size() ||
      lower.compare(lower.size() - 4, 4, ".tex") != 0)
    return std::nullopt;
  // leaf = everything after the last backslash, minus the trailing ".tex".
  size_t bs = lower.rfind('\\');
  if (bs == std::string::npos) return std::nullopt;
  std::string leaf = lower.substr(bs + 1, lower.size() - 4 - (bs + 1));
  if (leaf.rfind("anim", 0) == 0) leaf = leaf.substr(4);
  auto get = [&](const char* target) -> std::optional<std::string> {
    auto it = labels.find(Ci(target));
    return it != labels.end() ? std::optional<std::string>(it->second) : std::nullopt;
  };
  if (leaf == "icon_button_a") return get("A");
  if (leaf == "icon_button_b") return get("B");
  if (leaf == "icon_button_x") return get("X");
  if (leaf == "icon_button_y") return get("Y");
  if (leaf == "leftbutton") return get("LB");
  if (leaf == "rightbutton") return get("RB");
  if (leaf == "lefttrigger" || leaf == "icon_button_lt") return get("LT");
  if (leaf == "righttrigger") return get("RT");
  if (leaf == "start") return get("Pause");
  if (leaf == "back") return get("Select");
  if (leaf == "leftup") return get("StickUp");
  if (leaf == "leftdown") return get("StickDown");
  if (leaf == "leftleft") return get("StickLeft");
  if (leaf == "leftright") return get("StickRight");
  if (leaf == "left") return Directions(labels, "Stick");
  if (leaf == "dpad") return Directions(labels, "");
  if (leaf == "right" || leaf == "rightup" || leaf == "rightdown" || leaf == "rightleft" ||
      leaf == "rightright")
    return mouseLook ? std::optional<std::string>("Mouse") : std::nullopt;
  return std::nullopt;
}

}  // namespace fable2::gui

#endif  // FABLE2_GUI_PROMPT_LABELS_H
