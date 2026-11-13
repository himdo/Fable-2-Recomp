// Keycap prompt icon rendering.
//
// Renders a rounded-rectangle keycap with bold white text (e.g. "LMB", "W",
// "Up") into a 64x64 or 32x32 RGBA surface laid out in Xenos 2D tile order,
// matching the launcher's KeyboardPrompts.RenderKeycap (WPF) output byte for
// byte in layout: [pixel_byte_count (big-endian u32), then A,R,G,B per tiled
// pixel]. The implementation (keycap_render.cpp) compiles stb_truetype once.
#ifndef FABLE2_GUI_KEYCAP_RENDER_H
#define FABLE2_GUI_KEYCAP_RENDER_H

#include <cstdint>
#include <string>
#include <vector>

namespace fable2::gui {

// Renders a keycap for `label` at the given size (32 or 64) and returns the
// bank-entry payload: a big-endian uint32 pixel_byte_count prefix (= size*size*4)
// followed by the tiled straight-alpha RGBA surface.
std::vector<uint8_t> RenderKeycap(const std::string& label, int size);

}  // namespace fable2::gui

#endif  // FABLE2_GUI_KEYCAP_RENDER_H
