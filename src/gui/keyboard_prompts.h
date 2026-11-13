// Keyboard keycap prompt generation (in-game port of the launcher's
// src/gui/KeyboardPrompts.cs).
//
// Given the host keyboard -> guest gamepad map (and the mouse-look flag), this
// renders a keycap texture for every controller icon the game shows for a
// bound action, and writes them to a sidecar file (keyboard_prompts.bin) next
// to the game data root. The keycap swap module (src/core/fable2_keycap_swap.h)
// reads that sidecar at startup and swaps the live prompt textures between the
// original Xbox icons and these keycaps based on the active input device.
//
// Running this in-process (OnPreSetup) makes the feature always-on: no launcher
// and no hard-linked cache folder are needed, because the bank files on disk
// are never modified -- only the sidecar (a new file) carries the keycap pixels.
#ifndef FABLE2_GUI_KEYBOARD_PROMPTS_H
#define FABLE2_GUI_KEYBOARD_PROMPTS_H

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "bank_rebuild.h"
#include "gui_texture_bank.h"
#include "keycap_render.h"
#include "gui_tex_codec.h"

namespace fable2::gui {

namespace {

// Lowercase a string (for case-insensitive lookup keys).
std::string Ci(const std::string& s) {
  std::string r = s;
  for (char& c : r) c = char(std::tolower((unsigned char)c));
  return r;
}

// Guest-input action aliases (lowercased key -> canonical button).
const std::unordered_map<std::string, std::string>& ButtonAliases() {
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
const std::unordered_map<std::string, std::string>& MouseAliases() {
  static const std::unordered_map<std::string, std::string> m = {
      {"lmb", "LMB"}, {"leftmouse", "LMB"}, {"rmb", "RMB"}, {"rightmouse", "RMB"},
      {"mmb", "MMB"}, {"middlemouse", "MMB"},
      {"xmb1", "XMB1"}, {"xbutton1", "XMB1"}, {"sidebutton1", "XMB1"},
      {"xmb2", "XMB2"}, {"xbutton2", "XMB2"}, {"sidebutton2", "XMB2"},
  };
  return m;
}

// Short display names for long host keys (exact host-key name, case-sensitive).
const std::unordered_map<std::string, std::string>& ShortKeys() {
  static const std::unordered_map<std::string, std::string> m = {
      {"Escape", "Esc"}, {"Return", "Enter"}, {"Backspace", "Bksp"}, {"Control", "Ctrl"},
      {"PageUp", "PgUp"}, {"PageDown", "PgDn"}, {"PrintScreen", "PrtSc"},
      {"CapsLock", "Caps"}, {"NumLock", "NumLk"}, {"ScrollLock", "ScrLk"},
      {"Up", "\u2191"}, {"Down", "\u2193"}, {"Left", "\u2190"}, {"Right", "\u2192"},
  };
  return m;
}

// Valid host key names (rex::ui::ParseVirtualKey + mouse buttons), case-sensitive.
const std::unordered_set<std::string>& KeyNames() {
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
const std::unordered_set<std::string>& ButtonNames() {
  static const std::unordered_set<std::string> s = {
      "a", "b", "x", "y", "lb", "rb", "lt", "rt",
      "up", "down", "left", "right", "pause", "select", "l3", "r3",
      "stickup", "stickdown", "stickleft", "stickright",
  };
  return s;
}

std::vector<uint8_t> ReadAllBytes(const std::filesystem::path& path) {
  std::vector<uint8_t> out;
  std::FILE* f = std::fopen(path.string().c_str(), "rb");
  if (!f) return out;
  std::fseek(f, 0, SEEK_END);
  long len = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (len > 0) {
    out.resize(size_t(len));
    if (std::fread(out.data(), 1, out.size(), f) != out.size()) out.clear();
  }
  std::fclose(f);
  return out;
}

bool WriteAllBytes(const std::filesystem::path& path, const std::vector<uint8_t>& data) {
  std::FILE* f = std::fopen(path.string().c_str(), "wb");
  if (!f) return false;
  bool ok = data.empty() || std::fwrite(data.data(), 1, data.size(), f) == data.size();
  ok = std::fclose(f) == 0 && ok;
  return ok;
}

}  // namespace

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

// Renders a keycap for `label` at 64x64, then scales it down by `hInset` pixels
// on the left/right and `vInset` on the top/bottom, and centres the result in a
// fresh 64x64 0x00A2 tile. Used for the shared abyx atlas where the full 64x64
// keycap would crowd the neighbouring buttons. The keycap (rounded rect + text)
// is scaled as a whole, so the glyph shrinks proportionally.
inline std::vector<uint8_t> RenderKeycapInset(const std::string& label, int hInset, int vInset, int dx = 0) {
  const int kCap = 64;
  const int kW = kCap - 2 * hInset;
  const int kH = kCap - 2 * vInset;
  if (kW <= 0 || kH <= 0) return {};
  std::vector<uint8_t> keycap = RenderKeycap(label, kCap);
  // De-tile the 64x64 keycap to linear RGBA (A,R,G,B per pixel).
  std::vector<uint8_t> linear((size_t)kCap * kCap * 4, 0);
  for (int y = 0; y < kCap; ++y)
    for (int x = 0; x < kCap; ++x) {
      const uint8_t* s = keycap.data() + 4 + XenosTiledOffset(x, y, kCap);
      uint8_t* d = linear.data() + ((size_t)y * kCap + x) * 4;
      d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
    }
  // Premultiplied bilinear downscale to kW x kH (separate x/y scale).
  std::vector<uint8_t> shrunk((size_t)kW * kH * 4, 0);
  const float scaleX = (float)kCap / (float)kW;
  const float scaleY = (float)kCap / (float)kH;
  for (int y = 0; y < kH; ++y)
    for (int x = 0; x < kW; ++x) {
      const float sx = (x + 0.5f) * scaleX - 0.5f;
      const float sy = (y + 0.5f) * scaleY - 0.5f;
      const int x0 = (int)sx, y0 = (int)sy;
      const float fx = sx - x0, fy = sy - y0;
      float pmr = 0, pmg = 0, pmb = 0, pmA = 0;
      for (int dy = 0; dy < 2; ++dy)
        for (int dx = 0; dx < 2; ++dx) {
          const int px = x0 + dx, py = y0 + dy;
          if (px < 0 || py < 0 || px >= kCap || py >= kCap) continue;
          const uint8_t* s = linear.data() + ((size_t)py * kCap + px) * 4;
          const int A = s[0], R = s[1], G = s[2], B = s[3];
          const float w = (dx ? fx : 1.0f - fx) * (dy ? fy : 1.0f - fy);
          pmA += A * w;
          pmr += R * (A / 255.0f) * w;
          pmg += G * (A / 255.0f) * w;
          pmb += B * (A / 255.0f) * w;
        }
      const int A = (int)(pmA + 0.5f);  // the four weights sum to 1
      uint8_t* o = shrunk.data() + ((size_t)y * kW + x) * 4;
      o[0] = (uint8_t)std::min(255, A);
      if (A > 0) {
        o[1] = (uint8_t)std::min(255.0f, pmr * 255.0f / A + 0.5f);
        o[2] = (uint8_t)std::min(255.0f, pmg * 255.0f / A + 0.5f);
        o[3] = (uint8_t)std::min(255.0f, pmb * 255.0f / A + 0.5f);
      } else {
        o[1] = o[2] = o[3] = 0;
      }
    }
  // Re-emit a 64x64 0x00A2 tile with the shrunk keycap centred.
  const int pixbytes = kCap * kCap * 4;
  std::vector<uint8_t> result(4 + (size_t)pixbytes, 0);
  result[0] = (uint8_t)((pixbytes >> 24) & 0xFF);
  result[1] = (uint8_t)((pixbytes >> 16) & 0xFF);
  result[2] = (uint8_t)((pixbytes >> 8) & 0xFF);
  result[3] = (uint8_t)(pixbytes & 0xFF);
  for (int y = 0; y < kH; ++y)
    for (int x = 0; x < kW; ++x) {
      const uint8_t* s = shrunk.data() + ((size_t)y * kW + x) * 4;
      if (s[0] == 0) continue;
      const int px = hInset + dx + x;
      const int py = vInset + y;
      if (px < 0 || py < 0 || px >= kCap || py >= kCap) continue;
      int off = 4 + XenosTiledOffset(px, py, kCap);
      result[off] = s[0];
      result[off + 1] = s[1];
      result[off + 2] = s[2];
      result[off + 3] = s[3];
    }
  return result;
}

// Replaces a cap_size x cap_size sub-region of a larger 0x00A2 tile with a
// keycap tile of the same size, re-mapping pixels between the two Xenos tile
// pitches. `ox`/`oy` is the sub-region's top-left in the atlas. Used to swap a
// single prompt button inside a shared atlas without touching the rest.
inline void ReplaceKeycapRegion(std::vector<uint8_t>& atlas, const std::vector<uint8_t>& keycap,
                                int big_w, int big_h, int ox, int oy, int cap_size = 64) {
  for (int y = 0; y < cap_size; ++y) {
    for (int x = 0; x < cap_size; ++x) {
      const int bx = ox + x, by = oy + y;
      if (bx < 0 || by < 0 || bx >= big_w || by >= big_h) continue;
      const uint8_t* src = keycap.data() + 4 + XenosTiledOffset(x, y, cap_size);
      uint8_t* dst = atlas.data() + 4 + XenosTiledOffset(bx, by, big_w);
      dst[0] = src[0];
      dst[1] = src[1];
      dst[2] = src[2];
      dst[3] = src[3];
    }
  }
}

// Renders a keycap for `label` at `size` x `size` pixels (any size), by
// rendering at 64x64 and bilinearly downsampling. Returns linear RGBA
// (size*size*4 bytes, A,R,G,B per pixel in row-major order). Use
// PlaceLinearRegion to write this into a tiled atlas.
inline std::vector<uint8_t> RenderKeycapLinear(const std::string& label, int size) {
  if (size <= 0) return {};
  std::vector<uint8_t> keycap = RenderKeycap(label, 64);
  if (int(keycap.size()) != 4 + 64 * 64 * 4) return {};
  // De-tile to linear.
  std::vector<uint8_t> linear((size_t)64 * 64 * 4, 0);
  for (int y = 0; y < 64; ++y)
    for (int x = 0; x < 64; ++x) {
      const uint8_t* s = keycap.data() + 4 + XenosTiledOffset(x, y, 64);
      uint8_t* d = linear.data() + ((size_t)y * 64 + x) * 4;
      d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
    }
  // Premultiplied bilinear downscale to size x size.
  std::vector<uint8_t> out((size_t)size * size * 4, 0);
  const float scale = 64.0f / (float)size;
  for (int y = 0; y < size; ++y)
    for (int x = 0; x < size; ++x) {
      const float sx = (x + 0.5f) * scale - 0.5f;
      const float sy = (y + 0.5f) * scale - 0.5f;
      const int x0 = (int)sx, y0 = (int)sy;
      const float fx = sx - x0, fy = sy - y0;
      float pmr = 0, pmg = 0, pmb = 0, pmA = 0;
      for (int dy = 0; dy < 2; ++dy)
        for (int dx = 0; dx < 2; ++dx) {
          const int px = x0 + dx, py = y0 + dy;
          if (px < 0 || py < 0 || px >= 64 || py >= 64) continue;
          const uint8_t* s = linear.data() + ((size_t)py * 64 + px) * 4;
          const int A = s[0], R = s[1], G = s[2], B = s[3];
          const float w = (dx ? fx : 1.0f - fx) * (dy ? fy : 1.0f - fy);
          pmA += A * w;
          pmr += R * (A / 255.0f) * w;
          pmg += G * (A / 255.0f) * w;
          pmb += B * (A / 255.0f) * w;
        }
      const int A = (int)(pmA + 0.5f);
      uint8_t* o = out.data() + ((size_t)y * size + x) * 4;
      o[0] = (uint8_t)std::min(255, A);
      if (A > 0) {
        o[1] = (uint8_t)std::min(255.0f, pmr * 255.0f / A + 0.5f);
        o[2] = (uint8_t)std::min(255.0f, pmg * 255.0f / A + 0.5f);
        o[3] = (uint8_t)std::min(255.0f, pmb * 255.0f / A + 0.5f);
      } else {
        o[1] = o[2] = o[3] = 0;
      }
    }
  return out;
}

// Places a size x size linear RGBA region (A,R,G,B per pixel, row-major) into
// a 0x00A2 tiled atlas at position (ox, oy). Uses the atlas width for the
// Xenos tile offset (works for any region size, not just power-of-2).
inline void PlaceLinearRegion(std::vector<uint8_t>& atlas, const uint8_t* linear,
                              int region_size, int atlas_w, int atlas_h, int ox, int oy) {
  for (int y = 0; y < region_size; ++y)
    for (int x = 0; x < region_size; ++x) {
      const int bx = ox + x, by = oy + y;
      if (bx < 0 || by < 0 || bx >= atlas_w || by >= atlas_h) continue;
      const uint8_t* src = linear + ((size_t)y * region_size + x) * 4;
      uint8_t* dst = atlas.data() + 4 + XenosTiledOffset(bx, by, atlas_w);
      dst[0] = src[0];
      dst[1] = src[1];
      dst[2] = src[2];
      dst[3] = src[3];
    }
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

// Renders the keycaps and writes game_data_root/keyboard_prompts.bin. Returns
// true if a non-empty sidecar was written.
inline bool GenerateSidecar(const std::filesystem::path& game_data_root, const std::string& map,
                            bool mouseLook, const std::string& reservedKey) {
  const std::string kTextures = "data/art/gui/gui_textures.bnk";
  const std::string kHeaders = "data/art/gui/gui_texture_headers.bnk";
  const std::string kReferenceIcon = "art\\gui\\controller\\icon_button_a.tex";

  auto labels = ResolveLabels(map, reservedKey);
  if (labels.empty() && !mouseLook) return false;

  std::vector<uint8_t> textures = ReadAllBytes(game_data_root / kTextures);
  std::vector<uint8_t> headers = ReadAllBytes(game_data_root / kHeaders);
  if (textures.empty() || headers.empty()) return false;
  GuiTextureBank textureBank, headerBank;
  if (!textureBank.Parse(textures.data(), textures.size())) return false;
  if (!headerBank.Parse(headers.data(), headers.size())) return false;

  // Validate the expected controller texture format.
  auto refIt = headerBank.entries().find(Ci(kReferenceIcon));
  if (refIt == headerBank.entries().end() || refIt->second.length != 84 ||
      GuiTextureBank::ReadIntBE(headerBank.Payload(refIt->second) + 12) != 0xA2)
    return false;
  const int rawHeaderLen = refIt->second.length;

  struct SidecarEntry {
    std::string bank_path;
    std::string name_ci;  // lowercase bank entry name (for offset lookup)
    uint64_t offset = 0;
    uint32_t size = 0;
    std::vector<uint8_t> original;  // full-RGBA (0x00A2) original pixels
    std::vector<uint8_t> keyboard;  // full-RGBA (0x00A2) keycap pixels
  };
  std::vector<SidecarEntry> sidecar;
  // Texture-bank replacements: compressed icons -> decoded full-RGBA payload.
  std::unordered_map<std::string, std::vector<uint8_t>> texture_replacements;
  // Header-bank updates: name -> new full-RGBA pixel_byte_count.
  std::unordered_map<std::string, uint32_t> header_updates;
  std::unordered_map<std::string, std::vector<uint8_t>> rendered;
  for (const auto& [key, entry] : textureBank.entries()) {
    auto labelOpt = IconLabel(entry.name, labels, mouseLook);
    if (!labelOpt) continue;
    auto headerIt = headerBank.entries().find(Ci(entry.name));
    if (headerIt == headerBank.entries().end() || headerIt->second.length != rawHeaderLen) continue;
    const uint8_t* metadata = headerBank.Payload(headerIt->second);
    const int w = GuiTextureBank::ReadIntBE(metadata + 16);
    const int h = GuiTextureBank::ReadIntBE(metadata + 20);
    if ((w != 32 && w != 64) || h != w) continue;
    const int format = GuiTextureBank::ReadIntBE(metadata + 12);
    const int a2_size = 4 + w * h * 4;
    // The original full-RGBA payload: the existing bytes when the icon is already
    // uncompressed (0x00A2), else the decoded compressed icon (Huffman BC3 / raw BC3).
    std::vector<uint8_t> original;
    if (format == 0xA2) {
      if (entry.length != a2_size) continue;
      original.assign(textureBank.Payload(entry), textureBank.Payload(entry) + entry.length);
    } else {
      if (!DecodeCompressedIconToTile(textureBank.Payload(entry), entry.length, w, h, original) ||
          int(original.size()) != a2_size)
        continue;
    }
    // The keyboard (keycap) full-RGBA payload, rendered at the icon size.
    std::string renderKey = *labelOpt + "|" + std::to_string(w);
    auto it = rendered.find(renderKey);
    if (it == rendered.end()) it = rendered.emplace(renderKey, RenderKeycap(*labelOpt, w)).first;
    if (int(it->second.size()) != a2_size) continue;
    // Compressed icons must be converted to 0x00A2 (the game would otherwise try
    // to decode the full-RGBA replacement as a compressed block), so record them
    // for the bank rebuild. Full-RGBA icons are left in place.
    if (format != 0xA2) {
      texture_replacements[Ci(entry.name)] = original;
      header_updates[Ci(entry.name)] = uint32_t(a2_size);
    }
    sidecar.push_back({kTextures, Ci(entry.name), uint64_t(entry.offset), uint32_t(a2_size),
                       std::move(original), it->second});
  }

  // Shared-atlas prompts: some textures pack several controller buttons into one
  // large surface (the A/B/X/Y atlas at art\gui\persistant\abyx.tex is 256x256,
  // with the four face buttons in the top two rows and colour swatches in the
  // bottom half). For these we decode the whole atlas to 0x00A2, then overwrite
  // just the one 64x64 cell that holds the button we're remapping -- here the
  // top-left A button -- leaving every other cell (B/Y/X and the swatches) bit-
  // for-bit identical. The atlas is converted to full-RGBA (like the controller
  // icons) so the keycap swap can patch the live surface byte-for-byte.
  auto addAtlasCell = [&](const std::string& name, int ox, int oy, const std::string& btn) {
    auto at = textureBank.entries().find(Ci(name));
    if (at == textureBank.entries().end()) return;
    auto ah = headerBank.entries().find(Ci(name));
    if (ah == headerBank.entries().end() || ah->second.length != rawHeaderLen) return;
    const uint8_t* md = headerBank.Payload(ah->second);
    const int w = GuiTextureBank::ReadIntBE(md + 16);
    const int h = GuiTextureBank::ReadIntBE(md + 20);
    const int format = GuiTextureBank::ReadIntBE(md + 12);
    const int a2_size = 4 + w * h * 4;
    if (w != 256 || h != 256) return;
    auto labelIt = labels.find(btn);
    if (labelIt == labels.end()) return;
    // Shrink the keycap (6px horizontal, 4px vertical) so it sits comfortably
    // inside its 64x64 atlas cell without crowding the neighbouring buttons.
    std::vector<uint8_t> keycap = RenderKeycapInset(labelIt->second, 10, 4);
    if (int(keycap.size()) != 4 + 64 * 64 * 4) return;
    std::vector<uint8_t> original;
    if (format == 0xA2) {
      if (at->second.length != a2_size) return;
      original.assign(textureBank.Payload(at->second),
                      textureBank.Payload(at->second) + at->second.length);
    } else if (!DecodeCompressedIconToTile(textureBank.Payload(at->second), at->second.length, w, h,
                                           original) ||
               int(original.size()) != a2_size) {
      return;
    }
    std::vector<uint8_t> keyboard = original;
    ReplaceKeycapRegion(keyboard, keycap, w, h, ox, oy);
    if (format != 0xA2) {
      texture_replacements[Ci(name)] = original;
      header_updates[Ci(name)] = uint32_t(a2_size);
    }
    sidecar.push_back({kTextures, Ci(name), uint64_t(at->second.offset), uint32_t(a2_size),
                       std::move(original), std::move(keyboard)});
  };
  // The abyx atlas: replace the A button at (0,0) and the smaller A variant at
  // (0,64) with the same keycap, in a single sidecar entry.
  {
    const std::string abyxName = "art\\gui\\persistant\\abyx.tex";
    auto at = textureBank.entries().find(Ci(abyxName));
    if (at != textureBank.entries().end()) {
      auto ah = headerBank.entries().find(Ci(abyxName));
      if (ah != headerBank.entries().end() && ah->second.length == rawHeaderLen) {
        const uint8_t* md = headerBank.Payload(ah->second);
        const int w = GuiTextureBank::ReadIntBE(md + 16);
        const int h = GuiTextureBank::ReadIntBE(md + 20);
        const int format = GuiTextureBank::ReadIntBE(md + 12);
        const int a2_size = 4 + w * h * 4;
        if (w == 256 && h == 256) {
          // Render keycaps for all four face buttons (A, B, Y, X).
          const char* btnKeys[] = {"a", "b", "y", "x"};
          std::vector<uint8_t> keycaps[4];
          bool allOk = true;
          for (int i = 0; i < 4; ++i) {
            auto it = labels.find(btnKeys[i]);
            if (it == labels.end()) { allOk = false; break; }
            keycaps[i] = RenderKeycapInset(it->second, 10, 4, -3);
            if (int(keycaps[i].size()) != 4 + 64 * 64 * 4) { allOk = false; break; }
          }
          if (allOk) {
          std::vector<uint8_t> original;
          if (format == 0xA2) {
            if (at->second.length == a2_size)
              original.assign(textureBank.Payload(at->second),
                              textureBank.Payload(at->second) + at->second.length);
          } else if (DecodeCompressedIconToTile(textureBank.Payload(at->second),
                                                 at->second.length, w, h, original) &&
                     int(original.size()) == a2_size) {
            // ok
          }
          if (!original.empty()) {
          std::vector<uint8_t> keyboard = original;
          // Replace each button in the top row (y=0) and bottom row (y=64).
          for (int i = 0; i < 4; ++i) {
            const int ox = i * 64;
            ReplaceKeycapRegion(keyboard, keycaps[i], w, h, ox, 0);
            ReplaceKeycapRegion(keyboard, keycaps[i], w, h, ox, 64);
          }
          if (format != 0xA2) {
            texture_replacements[Ci(abyxName)] = original;
            header_updates[Ci(abyxName)] = uint32_t(a2_size);
          }
          sidecar.push_back({kTextures, Ci(abyxName), uint64_t(at->second.offset),
                             uint32_t(a2_size), std::move(original), std::move(keyboard)});
          }
          }  // if (allOk)
        }  // if (w == 256 && h == 256)
      }
    }
  }

  // frame_elements.tex: replace B key at (46, 193) and A key at (13, 172), size 46x46.
  {
    const std::string frameName = "art\\gui\\frames\\frames_elements.tex";
    auto at = textureBank.entries().find(Ci(frameName));
    if (at != textureBank.entries().end()) {
      auto ah = headerBank.entries().find(Ci(frameName));
      if (ah != headerBank.entries().end() && ah->second.length == rawHeaderLen) {
        const uint8_t* md = headerBank.Payload(ah->second);
        const int w = GuiTextureBank::ReadIntBE(md + 16);
        const int h = GuiTextureBank::ReadIntBE(md + 20);
        const int format = GuiTextureBank::ReadIntBE(md + 12);
        const int a2_size = 4 + w * h * 4;
        if (w == 512 && h == 512) {
          struct KeycapSpec { const char* key; int x, y; };
          const KeycapSpec specs[] = { {"b", 46, 193}, {"a", 13, 372} };
          std::vector<uint8_t> original;
          if (format == 0xA2) {
            if (at->second.length == a2_size)
              original.assign(textureBank.Payload(at->second),
                              textureBank.Payload(at->second) + at->second.length);
          } else if (DecodeCompressedIconToTile(textureBank.Payload(at->second),
                                                 at->second.length, w, h, original) &&
                     int(original.size()) == a2_size) {
            // ok
          }
          if (!original.empty()) {
            std::vector<uint8_t> keyboard = original;
            const int kSize = 46;
            for (auto& spec : specs) {
              auto labelIt = labels.find(spec.key);
              if (labelIt == labels.end()) continue;
              auto keycap = RenderKeycapLinear(labelIt->second, kSize);
              if (int(keycap.size()) != kSize * kSize * 4) continue;
              if (spec.x < 0 || spec.y < 0 || spec.x + kSize > w || spec.y + kSize > h) continue;
              PlaceLinearRegion(keyboard, keycap.data(), kSize, w, h, spec.x, spec.y);
            }
            if (format != 0xA2) {
              texture_replacements[Ci(frameName)] = original;
              header_updates[Ci(frameName)] = uint32_t(a2_size);
            }
            sidecar.push_back({kTextures, Ci(frameName), uint64_t(at->second.offset),
                               uint32_t(a2_size), std::move(original), std::move(keyboard)});
          }
        }
      }
    }
  }

  if (sidecar.empty()) return false;

  // Rebuild the banks so the compressed icons carry full-RGBA (0x00A2) payloads:
  // the game then reads them as plain pixels, which the keycap swap's post-read
  // hook and physical-RAM scan rely on (a compressed entry's decoded surface
  // would not match the full-RGBA keycap in a byte-exact scan). This mirrors the
  // original C# launcher, which rewrote the banks on disk. The pristine banks are
  // kept once as <bank>.orig so the change is reversible. A failed rebuild just
  // means the compressed icons are skipped (the full-RGBA ones still work).
  std::unordered_map<std::string, uint64_t> new_offsets;
  bool rebuilt = false;
  if (!texture_replacements.empty()) {
    std::vector<uint8_t> rebuilt_textures, rebuilt_headers;
    if (RebuildBank(textures, texture_replacements, rebuilt_textures, new_offsets) &&
        RebuildHeaderBank(headers, header_updates, rebuilt_headers)) {
      // Keep a one-time backup of the pristine banks, then overwrite them.
      const auto tex_path = game_data_root / kTextures;
      const auto hdr_path = game_data_root / kHeaders;
      const auto tex_orig = game_data_root / (kTextures + ".orig");
      const auto hdr_orig = game_data_root / (kHeaders + ".orig");
      std::error_code ec;
      if (!std::filesystem::exists(tex_orig, ec))
        std::filesystem::copy(tex_path, tex_orig, std::filesystem::copy_options::overwrite_existing, ec);
      if (!std::filesystem::exists(hdr_orig, ec))
        std::filesystem::copy(hdr_path, hdr_orig, std::filesystem::copy_options::overwrite_existing, ec);
      if (WriteAllBytes(tex_path, rebuilt_textures) &&
          WriteAllBytes(hdr_path, rebuilt_headers))
        rebuilt = true;
    }
  }
  // Point each rebuilt (compressed) entry at its new location in the rebuilt
  // bank. Drop any rebuilt entry whose bank rebuild failed (it would not swap).
  std::vector<SidecarEntry> out_entries;
  out_entries.reserve(sidecar.size());
  for (auto& e : sidecar) {
    auto oit = new_offsets.find(e.name_ci);
    if (oit != new_offsets.end()) {
      if (!rebuilt) continue;  // rebuild failed: compressed icon can't swap
      e.offset = oit->second;
    }
    out_entries.push_back(std::move(e));
  }
  if (out_entries.empty()) return false;

  // Little-endian sidecar (version 2): magic "KBPM" (u32), version (u32 = 2),
  // entry_count (u32), then per entry: bank_path_len (u32), bank_path (utf8),
  // abs_offset (u64), size (u32), original (size), keyboard (size). Read by
  // src/core/fable2_keycap_swap.h.
  auto write_le = [](std::FILE* f, const void* p, size_t n) { std::fwrite(p, 1, n, f); };
  const uint32_t magic = 0x4D50424Bu;
  const uint32_t version = 2;
  const uint32_t count = uint32_t(out_entries.size());
  std::FILE* f = std::fopen((game_data_root / "keyboard_prompts.bin").string().c_str(), "wb");
  if (!f) return false;
  bool ok = true;
  write_le(f, &magic, 4);
  write_le(f, &version, 4);
  write_le(f, &count, 4);
  for (const auto& e : out_entries) {
    const uint32_t name_len = uint32_t(e.bank_path.size());
    write_le(f, &name_len, 4);
    write_le(f, e.bank_path.data(), name_len);
    write_le(f, &e.offset, 8);
    write_le(f, &e.size, 4);
    write_le(f, e.original.data(), e.original.size());
    write_le(f, e.keyboard.data(), e.keyboard.size());
  }
  ok = std::fclose(f) == 0;
  return ok && !out_entries.empty();
}

}  // namespace fable2::gui

#endif  // FABLE2_GUI_KEYBOARD_PROMPTS_H
