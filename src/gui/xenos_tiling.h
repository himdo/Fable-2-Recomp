// Xenos 2D tiled byte address for a four-byte pixel.
// texture_util::GetTiledOffset2D (Xenia). BSD 3-clause license, see thirdparty/XenosTiling/LICENSE.txt.
#ifndef FABLE2_GUI_XENOS_TILING_H
#define FABLE2_GUI_XENOS_TILING_H

namespace fable2::gui {

// Pitch is the texture width in pixels (32 or 64). Returns the byte offset of
// the (x, y) pixel within the tiled surface.
inline int XenosTiledOffset(int x, int y, int pitch) {
  int macro = ((x >> 5) + (y >> 5) * (pitch >> 5)) << 9;
  int micro = ((x & 7) + ((y & 14) << 2)) << 2;
  int offset = macro + ((micro & ~15) << 1) + (micro & 15) + ((y & 1) << 4);
  int bank = (((y & 8) >> 2) + (x >> 3)) & 3;
  return ((offset & ~511) << 3) + ((y & 16) << 7) + ((offset & 448) << 2) +
         (bank << 6) + (offset & 63);
}

}  // namespace fable2::gui

#endif
