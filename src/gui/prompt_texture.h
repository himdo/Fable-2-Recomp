// A button prompt texture in both versions, as the live swap needs it.
#ifndef FABLE2_GUI_PROMPT_TEXTURE_H
#define FABLE2_GUI_PROMPT_TEXTURE_H

#include <cstdint>
#include <string>
#include <vector>

namespace fable2::gui {

inline constexpr const char* kTextureBank = "data/art/gui/gui_textures.bnk";
inline constexpr const char* kHeaderBank = "data/art/gui/gui_texture_headers.bnk";

// One prompt texture, ready for the live swap. Index 0 is the controller
// version, 1 the keycap version.
struct PromptTexture {
  std::string name;     // lowercase bank entry name
  uint64_t offset = 0;  // payload offset in the texture bank as rebuilt
  bool raw_bc3 = false;
  std::vector<uint8_t> payload[2];  // bank payload
  std::vector<uint8_t> live[2];     // the bytes in guest memory once loaded
};

}  // namespace fable2::gui

#endif  // FABLE2_GUI_PROMPT_TEXTURE_H
