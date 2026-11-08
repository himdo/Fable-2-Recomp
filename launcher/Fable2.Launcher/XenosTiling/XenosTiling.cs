// Adapted from ReXGlue's texture_util::GetTiledOffset2D, which derives from
// Xenia. Covered by the BSD 3-clause licence in LICENSE.txt in this folder;
// the rest of the launcher is not.

namespace Fable2Launcher;

internal static class XenosTiling
{
    // Xenos 2D tiled byte address for a four-byte pixel.
    internal static int TiledOffset(int x, int y, int pitch)
    {
        int macro = ((x >> 5) + (y >> 5) * (pitch >> 5)) << 9;
        int micro = ((x & 7) + ((y & 14) << 2)) << 2;
        int offset = macro + ((micro & ~15) << 1) + (micro & 15) + ((y & 1) << 4);
        int bank = (((y & 8) >> 2) + (x >> 3)) & 3;
        return ((offset & ~511) << 3) + ((y & 16) << 7) + ((offset & 448) << 2) +
            (bank << 6) + (offset & 63);
    }
}
