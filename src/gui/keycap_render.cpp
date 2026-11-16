// Keycap prompt icon rendering (see keycap_render.h).
//
// This translation unit compiles the stb_truetype implementation exactly once
// (STB_TRUETYPE_IMPLEMENTATION) so the rest of the app can link against it.
//
// The renderer draws, at 4x supersampling (256x256), then box-downsamples to
// the target size (64 or 32):
//   1. A rounded-rectangle keycap: fill (20,25,35), 2px border (220,231,242),
//      rect (2,8,60,48) corner radius 8 in a 64-unit design space.
//   2. White text (bundled Google Sans via stb_truetype), centered, with the
//      font em shrinking until the widest row fits 53 units.
// The result is written in Xenos 2D tile order as straight A,R,G,B.
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#undef STB_TRUETYPE_IMPLEMENTATION

#include "keycap_render.h"
#include "xenos_tiling.h"
#include <rex/filesystem.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace fable2::gui {
namespace {

struct Px {
  uint8_t r = 0, g = 0, b = 0, a = 0;  // straight alpha
};

// Porter-Duff "over": composite src (sr, sg, sb, sa) on top of dst.
void PxOver(Px& dst, uint8_t sr, uint8_t sg, uint8_t sb, uint8_t sa) {
  if (sa == 0) return;
  int oa = sa + (int)dst.a * (255 - sa) / 255;
  if (oa == 0) {
    dst = {sr, sg, sb, 0};
    return;
  }
  dst.r = (uint8_t)((sr * sa + (int)dst.r * dst.a * (255 - sa) / 255) / oa);
  dst.g = (uint8_t)((sg * sa + (int)dst.g * dst.a * (255 - sa) / 255) / oa);
  dst.b = (uint8_t)((sb * sa + (int)dst.b * dst.a * (255 - sa) / 255) / oa);
  dst.a = (uint8_t)oa;
}

struct KeycapFont {
  stbtt_fontinfo info{};
  std::vector<uint8_t> bytes;
  bool valid = false;
  int units_per_em = 2048;
};

KeycapFont& LoadFont() {
  static KeycapFont f;
  if (f.valid) return f;
  // The keycap glyphs are rendered from the font bundled with the app so the
  // look is identical on every OS (no dependency on host-installed fonts). The
  // build stages it next to the exe (assets/fonts/Google Sans/...); see the
  // POST_BUILD copy in CMakeLists.txt. The C:/Windows system-font entries are a
  // best-effort fallback only (e.g. dev runs without the staged assets) -- the
  // C:/Windows paths simply never open on non-Windows hosts, so the bundled
  // font is the sole source there.
  const std::filesystem::path exe_dir = rex::filesystem::GetExecutableFolder();
  const std::vector<std::filesystem::path> candidates = {
      exe_dir / "assets/fonts/Google Sans/GoogleSans-VariableFont_GRAD,opsz,wght.ttf",
  };
  for (const std::filesystem::path& path : candidates) {
    std::ifstream file(path, std::ios::binary);
    if (!file) continue;
    f.bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    if (f.bytes.empty()) continue;
    int offset = stbtt_GetFontOffsetForIndex(f.bytes.data(), 0);
    if (offset < 0) continue;
    f.info.userdata = nullptr;  // use the default STBTT_malloc/free
    if (!stbtt_InitFont(&f.info, f.bytes.data(), offset)) continue;
    f.units_per_em = (int)(1.0 / stbtt_ScaleForMappingEmToPixels(&f.info, 1.0) + 0.5);
    f.valid = true;
    break;
  }
  return f;
}

std::vector<std::string> SplitRows(const std::string& label) {
  std::vector<std::string> rows;
  std::string cur;
  for (char c : label) {
    if (c == '\n') {
      rows.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  rows.push_back(cur);
  return rows;
}

// Decode a UTF-8 string into Unicode codepoints (labels can contain the arrow
// glyphs U+2190..U+2193 for the D-pad/direction keys).
std::vector<uint32_t> DecodeUtf8(const std::string& s) {
  std::vector<uint32_t> cps;
  size_t i = 0;
  while (i < s.size()) {
    uint8_t b0 = (uint8_t)s[i];
    uint32_t cp = 0;
    int extra = 0;
    if (b0 < 0x80)
      cp = b0;
    else if (b0 < 0xE0) {
      cp = b0 & 0x1F;
      extra = 1;
    } else if (b0 < 0xF0) {
      cp = b0 & 0x0F;
      extra = 2;
    } else {
      cp = b0 & 0x07;
      extra = 3;
    }
    if (i + size_t(extra) >= s.size()) {
      cps.push_back(0xFFFD);
      break;
    }
    for (int k = 1; k <= extra; ++k) cp = (cp << 6) | ((uint8_t)s[i + k] & 0x3F);
    i += 1 + extra;
    cps.push_back(cp);
  }
  return cps;
}

double RowWidthDu(const KeycapFont& font, const std::string& row, double em) {
  double w = 0.0;
  for (uint32_t cp : DecodeUtf8(row)) {
    int adv = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(&font.info, int(cp), &adv, &lsb);
    w += adv * em / font.units_per_em;
  }
  return w;
}

}  // namespace

std::vector<uint8_t> RenderKeycap(const std::string& label, int size) {
  constexpr int kDesign = 64;
  constexpr int kSS = 4;
  constexpr int kRes = kDesign * kSS;
  const KeycapFont& font = LoadFont();

  std::vector<Px> buf((size_t)kRes * kRes);

  // 1. Rounded rectangle: fill (20,25,35), 2px border (220,231,242).
  {
    const float cx = 32.0f * kSS, cy = 32.0f * kSS;
    const float hw = 30.0f * kSS, hh = 24.0f * kSS, rad = 8.0f * kSS;
    for (int py = 0; py < kRes; ++py) {
      for (int px = 0; px < kRes; ++px) {
        float qx = std::fabs((px + 0.5f) - cx) - (hw - rad);
        float qy = std::fabs((py + 0.5f) - cy) - (hh - rad);
        float dx = qx > 0 ? qx : 0.0f;
        float dy = qy > 0 ? qy : 0.0f;
        float sd = std::fmin(std::fmax(qx, qy), 0.0f) + std::sqrt(dx * dx + dy * dy) - rad;
        float fill = std::fmin(std::fmax(0.5f - sd, 0.0f), 1.0f);
        float border = std::fmin(std::fmax((float)kSS - std::fabs(sd) + 0.5f, 0.0f), 1.0f);
        Px& p = buf[(size_t)py * kRes + px];
        if (fill > 0) PxOver(p, 20, 25, 35, (uint8_t)(fill * 255.0f));
        if (border > 0) PxOver(p, 220, 231, 242, (uint8_t)(border * 255.0f));
      }
    }
  }

  // 2. Bold white text, centered (shrink em until the widest row fits 53 units).
  if (font.valid) {
    std::vector<std::string> rows = SplitRows(label);
    double em = rows.size() == 1 ? 32.0 : 20.0;
    auto any_too_wide = [&](double e) {
      for (const auto& r : rows)
        if (RowWidthDu(font, r, e) > 53.0) return true;
      return false;
    };
    while (em >= 6.0 && any_too_wide(em)) em -= 1.0;

    const double scale = stbtt_ScaleForMappingEmToPixels(&font.info, em * kSS);
    const double la_px = 1.2 * em * kSS;  // baseline-to-baseline advance

    // Center the text block on its actual ink, not the font's overall bounding
    // box (which is far taller than the rendered text and font-specific). The
    // ink extent is measured from the rendered glyph bitmaps: stbtt's
    // stbtt_GetGlyph*Box functions return wildly wrong boxes for variable
    // fonts like Google Sans, whereas stbtt_GetGlyphBitmap is reliable. This
    // places the block so its vertical center lands at y=32 for any font.
    auto row_extent = [&](const std::string& r, double& top, double& bottom) {
      top = 0.0; bottom = 0.0;
      bool any = false;
      for (uint32_t cp : DecodeUtf8(r)) {
        int glyph = stbtt_FindGlyphIndex(&font.info, int(cp));
        if (glyph <= 0) continue;
        int wpx = 0, hpx = 0, xoff = 0, yoff = 0;
        unsigned char* bmp = stbtt_GetGlyphBitmap(&font.info, (float)scale, (float)scale, glyph,
                                                  &wpx, &hpx, &xoff, &yoff);
        if (bmp) {
          double gtop = (double)yoff;          // top above baseline (negative)
          double gbot = (double)(yoff + hpx);  // bottom (descenders go positive)
          if (gtop < top) top = gtop;
          if (gbot > bottom) bottom = gbot;
          any = true;
          stbtt_FreeBitmap(bmp, font.info.userdata);
        }
      }
      if (!any) { top = 0.0; bottom = 0.0; }
    };
    double T = 0.0, D = 0.0;
    for (size_t i = 0; i < rows.size(); ++i) {
      double rtop, rbot;
      row_extent(rows[i], rtop, rbot);
      if (i * la_px + rtop < T) T = i * la_px + rtop;
      if (i * la_px + rbot > D) D = i * la_px + rbot;
    }
    const double first_baseline = (kDesign * kSS) * 0.5 - (T + D) * 0.5;

    for (size_t ri = 0; ri < rows.size(); ++ri) {
      const std::string& row = rows[ri];
      const double row_w_px = RowWidthDu(font, row, em) * kSS;
      double pen_x = (kDesign * kSS - row_w_px) * 0.5;  // center the row
      const double baseline = first_baseline + ri * la_px;
      for (uint32_t cp : DecodeUtf8(row)) {
        int glyph = stbtt_FindGlyphIndex(&font.info, int(cp));
        if (glyph > 0) {
          int wpx = 0, hpx = 0, xoff = 0, yoff = 0;
          unsigned char* bmp = stbtt_GetGlyphBitmap(&font.info, (float)scale, (float)scale, glyph,
                                                    &wpx, &hpx, &xoff, &yoff);
          if (bmp) {
            for (int gy = 0; gy < hpx; ++gy) {
              int by = (int)std::lround(baseline + yoff) + gy;
              if (by < 0 || by >= kRes) continue;
              for (int gx = 0; gx < wpx; ++gx) {
                uint8_t cov = bmp[gy * wpx + gx];
                if (cov == 0) continue;
                int bx = (int)std::lround(pen_x + xoff) + gx;
                if (bx < 0 || bx >= kRes) continue;
                PxOver(buf[(size_t)by * kRes + bx], 255, 255, 255, cov);
              }
            }
            stbtt_FreeBitmap(bmp, font.info.userdata);
          }
        }
        int adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&font.info, int(cp), &adv, &lsb);
        pen_x += adv * scale;
      }
    }
  }

  // 3. Downsample kRes x kRes -> size x size (box filter, premultiplied).
  const int factor = kRes / size;
  const int n = factor * factor;
  std::vector<Px> small((size_t)size * size);
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      double pmr = 0, pmg = 0, pmb = 0, a = 0;
      for (int dy = 0; dy < factor; ++dy) {
        for (int dx = 0; dx < factor; ++dx) {
          const Px& p = buf[(size_t)(y * factor + dy) * kRes + (x * factor + dx)];
          pmr += p.r * p.a / 255.0;
          pmg += p.g * p.a / 255.0;
          pmb += p.b * p.a / 255.0;
          a += p.a;
        }
      }
      const double avg_a = a / n;
      Px& out = small[(size_t)y * size + x];
      if (avg_a <= 0.5) continue;  // transparent
      auto unpremult = [&](double pmc) {
        double c = pmc / n * 255.0 / avg_a;
        return (uint8_t)std::lround(std::fmin(std::fmax(c, 0.0), 255.0));
      };
      out.r = unpremult(pmr);
      out.g = unpremult(pmg);
      out.b = unpremult(pmb);
      out.a = (uint8_t)std::lround(std::fmin(std::fmax(avg_a, 0.0), 255.0));
    }
  }

  // 4. Lay out in Xenos 2D tile order: [A, R, G, B] per pixel.
  const int pixbytes = size * size * 4;
  std::vector<uint8_t> result(4 + (size_t)pixbytes, 0);
  result[0] = (uint8_t)((pixbytes >> 24) & 0xFF);
  result[1] = (uint8_t)((pixbytes >> 16) & 0xFF);
  result[2] = (uint8_t)((pixbytes >> 8) & 0xFF);
  result[3] = (uint8_t)(pixbytes & 0xFF);
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      const Px& p = small[(size_t)y * size + x];
      if (p.a == 0) continue;
      int off = 4 + XenosTiledOffset(x, y, size);
      result[off] = p.a;
      result[off + 1] = p.r;
      result[off + 2] = p.g;
      result[off + 3] = p.b;
    }
  }
  return result;
}

}  // namespace fable2::gui
