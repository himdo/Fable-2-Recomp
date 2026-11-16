// Keycap prompt textures: the memory scan, the raw BC3 path and label lookup.
#include <cstdio>
#include <cstring>
#include <vector>

#include "core/keycap_texture_scan.h"
#include "gui/gui_tex_codec.h"
#include "gui/prompt_labels.h"

namespace {

int failures = 0;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

void ScanFindsAlignedCopiesOnly() {
  std::vector<uint8_t> memory(0x10000, 0);
  std::vector<uint8_t> texture(256);
  for (size_t i = 0; i < texture.size(); ++i) texture[i] = uint8_t(i * 7 + 1);
  std::memcpy(memory.data() + 0x2000, texture.data(), texture.size());
  std::memcpy(memory.data() + 0x5000, texture.data(), texture.size());
  std::memcpy(memory.data() + 0x3001, texture.data(), texture.size());  // not 4 KB aligned

  std::vector<fable2::keycap::ScanTarget> targets(2);
  targets[0].bytes = texture;
  targets[1].bytes.assign(256, 0);  // blank: would match every empty page
  fable2::keycap::FindAll(memory.data(), memory.size(), targets);

  Check(targets[0].found == 2, "scan finds both aligned copies");
  Check(targets[0].offsets[0] == 0x2000 && targets[0].offsets[1] == 0x5000, "scan offsets");
  Check(targets[1].found == 0, "scan skips an all-zero target");
}

void ScanStopsAtFourCopies() {
  std::vector<uint8_t> memory(0x10000, 0);
  std::vector<uint8_t> texture(64, 0x5A);
  texture[10] = 1;
  for (int i = 0; i < 6; ++i) std::memcpy(memory.data() + i * 0x1000, texture.data(), texture.size());
  std::vector<fable2::keycap::ScanTarget> targets(1);
  targets[0].bytes = texture;
  fable2::keycap::FindAll(memory.data(), memory.size(), targets);
  Check(targets[0].found == 4, "scan records at most four copies");
}

// 128x128 is the smallest size stored as BC3: 32x32 blocks, one Xenos tile.
void RawBc3BodyRoundTripsAndTiles() {
  const int w = 128, blocks = (w / 4) * (w / 4);
  std::vector<uint8_t> bc3(size_t(blocks) * 16);
  for (int b = 0; b < blocks; ++b)
    for (int i = 0; i < 16; ++i) bc3[size_t(b) * 16 + i] = uint8_t(b * 3 + i);

  const auto body = fable2::gui::BuildRawBc3Body(bc3);
  Check(fable2::gui::IsRawBc3Body(body), "raw BC3 body is recognised");
  std::vector<uint8_t> decoded;
  Check(fable2::gui::DecodeCompressedToBc3(body.data(), body.size(), w, w, decoded) &&
            decoded == bc3,
        "raw BC3 body decodes back to the same blocks");

  const auto live = fable2::gui::RawBc3LiveSurface(body, w);
  bool tiled = live.size() == bc3.size();
  for (int by = 0; by < w / 4 && tiled; ++by)
    for (int bx = 0; bx < w / 4 && tiled; ++bx)
      tiled = std::memcmp(live.data() + fable2::gui::XenosTiledOffset16(bx, by, w / 4),
                          body.data() + 48 + (size_t(by) * (w / 4) + bx) * 16, 16) == 0;
  Check(tiled, "live surface holds the stored blocks in tile order");
}

void EncodedSolidBlockDecodesExactly() {
  uint8_t px[64];
  for (int i = 0; i < 16; ++i) {
    px[i * 4] = 200;      // alpha
    px[i * 4 + 1] = 255;  // red
    px[i * 4 + 2] = 0;
    px[i * 4 + 3] = 0;
  }
  uint8_t block[16];
  fable2::gui::EncodeBc3Block(px, block);
  uint8_t alpha[16], colour[64];
  fable2::gui::texcodec::ExpandBc4(block, alpha);
  fable2::gui::texcodec::ExpandBc1(block + 8, colour);
  Check(alpha[0] == 200 && alpha[15] == 200, "encoded alpha");
  Check(colour[1] == 255 && colour[2] == 0 && colour[3] == 0, "encoded colour");
}

void LabelsPreferRegularKeys() {
  const auto labels = fable2::gui::ResolveLabels("Shift:A,E:A,MMB:B,B:B,LMB:X", "");
  Check(labels.at("a") == "E", "a regular key beats Shift");
  Check(labels.at("b") == "B", "a regular key beats a mouse button");
  Check(labels.at("x") == "LMB", "a mouse button alone is used");
  const auto icon = fable2::gui::IconLabel("Art\\GUI\\Controller\\icon_button_a.tex", labels, false);
  Check(icon && *icon == "E", "controller icon maps to its button's key");
  Check(!fable2::gui::IconLabel("Art\\GUI\\Frames\\vignette256.tex", labels, false),
        "other textures get no label");
}

}  // namespace

int main() {
  ScanFindsAlignedCopiesOnly();
  ScanStopsAtFourCopies();
  RawBc3BodyRoundTripsAndTiles();
  EncodedSolidBlockDecodesExactly();
  LabelsPreferRegularKeys();
  if (failures) return 1;
  std::printf("PASS: keycap prompt textures (scan, raw BC3, labels)\n");
  return 0;
}
