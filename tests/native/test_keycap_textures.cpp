// Keycap prompt textures: the memory scan, the raw BC3 path and label lookup.
#include <cstring>
#include <vector>

#include "catch_amalgamated.hpp"
#include "core/keycap_texture_scan.h"
#include "gui/gui_tex_codec.h"
#include "gui/prompt_labels.h"

TEST_CASE("FindAll (keycap texture scan)", "[keycap]") {
  SECTION("finds the 4 KB aligned copies only, and skips an all-zero target") {
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

    REQUIRE(targets[0].found == 2);
    CHECK(targets[0].offsets[0] == 0x2000);
    CHECK(targets[0].offsets[1] == 0x5000);
    CHECK(targets[1].found == 0);
  }

  SECTION("records at most four copies") {
    std::vector<uint8_t> memory(0x10000, 0);
    std::vector<uint8_t> texture(64, 0x5A);
    texture[10] = 1;
    for (int i = 0; i < 6; ++i) std::memcpy(memory.data() + i * 0x1000, texture.data(), texture.size());
    std::vector<fable2::keycap::ScanTarget> targets(1);
    targets[0].bytes = texture;
    fable2::keycap::FindAll(memory.data(), memory.size(), targets);
    CHECK(targets[0].found == 4);
  }
}

TEST_CASE("raw BC3 bodies", "[keycap]") {
  SECTION("round-trip and are stored in Xenos tile order") {
    // 128x128 is the smallest size stored as BC3: 32x32 blocks, one Xenos tile.
    const int w = 128, blocks = (w / 4) * (w / 4);
    std::vector<uint8_t> bc3(size_t(blocks) * 16);
    for (int b = 0; b < blocks; ++b)
      for (int i = 0; i < 16; ++i) bc3[size_t(b) * 16 + i] = uint8_t(b * 3 + i);

    const auto body = fable2::gui::BuildRawBc3Body(bc3);
    CHECK(fable2::gui::IsRawBc3Body(body));
    std::vector<uint8_t> decoded;
    CHECK(fable2::gui::DecodeCompressedToBc3(body.data(), body.size(), w, w, decoded));
    CHECK(decoded == bc3);

    const auto live = fable2::gui::RawBc3LiveSurface(body, w);
    REQUIRE(live.size() == bc3.size());
    bool tiled = true;
    for (int by = 0; by < w / 4 && tiled; ++by)
      for (int bx = 0; bx < w / 4 && tiled; ++bx)
        tiled = std::memcmp(live.data() + fable2::gui::XenosTiledOffset16(bx, by, w / 4),
                            body.data() + 48 + (size_t(by) * (w / 4) + bx) * 16, 16) == 0;
    CHECK(tiled);
  }

  SECTION("encode a solid block that decodes exactly") {
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
    CHECK(alpha[0] == 200);
    CHECK(alpha[15] == 200);
    CHECK(colour[1] == 255);
    CHECK(colour[2] == 0);
    CHECK(colour[3] == 0);
  }
}

TEST_CASE("ResolveLabels and IconLabel", "[keycap]") {
  const auto labels = fable2::gui::ResolveLabels("Shift:A,E:A,MMB:B,B:B,LMB:X", "");

  SECTION("prefer a regular key to Shift or a mouse button") {
    CHECK(labels.at("a") == "E");
    CHECK(labels.at("b") == "B");
  }

  SECTION("use a mouse button when it's the only binding") { CHECK(labels.at("x") == "LMB"); }

  SECTION("map a controller icon to its button's key") {
    const auto icon = fable2::gui::IconLabel("Art\\GUI\\Controller\\icon_button_a.tex", labels, false);
    REQUIRE(icon);
    CHECK(*icon == "E");
  }

  SECTION("give other textures no label") {
    CHECK_FALSE(fable2::gui::IconLabel("Art\\GUI\\Frames\\vignette256.tex", labels, false));
  }
}
