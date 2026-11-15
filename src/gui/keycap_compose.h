// Drawing keycaps into prompt textures: full-RGBA (A,R,G,B) surfaces in Xenos
// tile order, the layout of an uncompressed 0x00A2 bank entry.
#ifndef FABLE2_GUI_KEYCAP_COMPOSE_H
#define FABLE2_GUI_KEYCAP_COMPOSE_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "keycap_render.h"
#include "xenos_tiling.h"

namespace fable2::gui {

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

// Renders a keycap for `label` at `width` x `height` pixels (any size; height
// defaults to width for a square) by rendering at 64x64 and bilinearly
// resampling non-uniformly. Returns linear RGBA (width*height*4 bytes,
// A,R,G,B per pixel in row-major order). Use CompositeLinearRegion to
// write this into a tiled atlas.
inline std::vector<uint8_t> RenderKeycapLinear(const std::string& label, int width,
                                               int height = 0) {
  if (height <= 0) height = width;
  if (width <= 0 || height <= 0) return {};
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
  // Premultiplied bilinear resample to width x height.
  std::vector<uint8_t> out((size_t)width * height * 4, 0);
  const float sx_scale = 64.0f / (float)width;
  const float sy_scale = 64.0f / (float)height;
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x) {
      const float sx = (x + 0.5f) * sx_scale - 0.5f;
      const float sy = (y + 0.5f) * sy_scale - 0.5f;
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
      uint8_t* o = out.data() + ((size_t)y * width + x) * 4;
      o[0] = (uint8_t)std::min(255, A);
      if (A > 0) {
        o[1] = (uint8_t)std::min(255.0f, pmr * 255.0f / A + 0.5f);
        o[2] = (uint8_t)std::min(255.0f, pmg * 255.0f / A + 0.5f);
        o[3] = (uint8_t)std::min(255.0f, pmb * 255.0f / A + 0.5f);
      } else {
        o[1] = o[2] = o[3] = 0;
      }
    }
  // Circular clip: the crop stays a circle (diameter = the smaller dimension) even
  // when the base image is a non-square region. Instead of an anti-aliased fade
  // (which leaves a semi-transparent, color-fringed rainbow edge), the boundary is
  // drawn as a 1px opaque ring in the keycap border color, giving a solid outline.
  const float cx = (width - 1) / 2.0f, cy = (height - 1) / 2.0f;
  const float r = (float)std::min(width, height) / 2.0f - 0.5f;
  const uint8_t kRingA = 255, kRingR = 220, kRingG = 231, kRingB = 242;
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x) {
      const float dx = x - cx, dy = y - cy;
      const float dist = (float)std::sqrt(dx * dx + dy * dy);
      uint8_t* o = out.data() + ((size_t)y * width + x) * 4;
      if (dist > r + 0.5f) {
        o[0] = o[1] = o[2] = o[3] = 0;
      } else if (dist > r - 1.0f) {
        // Solid opaque ring (matches the keycap border color). The inner edge is
        // pulled in a bit so the keycap's own resampled border can't bleed through
        // as a color-fringed speck just inside the ring.
        o[0] = kRingA; o[1] = kRingR; o[2] = kRingG; o[3] = kRingB;
      }
    }
  return out;
}

// Porter-Duff "over": blends a linear ARGB region (region_w x region_h) onto a
// tiled atlas, preserving the underlying pixels where the source alpha is zero.
inline void CompositeLinearRegion(std::vector<uint8_t>& atlas, const uint8_t* linear,
                                  int region_w, int region_h, int atlas_w, int atlas_h,
                                  int ox, int oy) {
  for (int y = 0; y < region_h; ++y)
    for (int x = 0; x < region_w; ++x) {
      const int bx = ox + x, by = oy + y;
      if (bx < 0 || by < 0 || bx >= atlas_w || by >= atlas_h) continue;
      const uint8_t* src = linear + ((size_t)y * region_w + x) * 4;
      uint8_t* dst = atlas.data() + 4 + XenosTiledOffset(bx, by, atlas_w);
      const int sa = src[0];
      if (sa == 0) continue;
      if (sa == 255) { dst[0] = 255; dst[1] = src[1]; dst[2] = src[2]; dst[3] = src[3]; continue; }
      // Pre-multiplied blend, stored back as straight alpha.
      const int inv = 255 - sa;
      const int da = dst[0];
      const int orp = src[1] * sa + dst[1] * da * inv / 255;
      const int ogp = src[2] * sa + dst[2] * da * inv / 255;
      const int obp = src[3] * sa + dst[3] * da * inv / 255;
      const int oa = sa + da * inv / 255;
      dst[0] = (uint8_t)oa;
      if (oa > 0) {
        dst[1] = (uint8_t)(orp * 255 / oa);
        dst[2] = (uint8_t)(ogp * 255 / oa);
        dst[3] = (uint8_t)(obp * 255 / oa);
      }
    }
}

}  // namespace fable2::gui

#endif  // FABLE2_GUI_KEYCAP_COMPOSE_H
