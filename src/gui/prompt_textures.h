// Builds the controller and keycap versions of every texture the game draws a
// controller button on, and rewrites the GUI banks so each one is stored in a
// form the live swap (src/core/fable2_keycap_swap.h) can patch in memory.
//
// Which textures get keycaps: every controller icon IconLabel knows (the whole
// icon becomes a keycap), plus the atlases in Atlases() (keycaps drawn into
// regions of a larger sheet). Adding a prompt means adding a row there.
#ifndef FABLE2_GUI_PROMPT_TEXTURES_H
#define FABLE2_GUI_PROMPT_TEXTURES_H

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "bank_rebuild.h"
#include "gui_tex_codec.h"
#include "gui_texture_bank.h"
#include "keycap_compose.h"
#include "keycap_render.h"
#include "prompt_labels.h"
#include "prompt_texture.h"

namespace fable2::gui {

enum class KeycapStyle {
  kWhole,    // the whole texture becomes the keycap
  kCell,     // a 64x64 atlas cell is replaced by a smaller keycap
  kOverlay,  // a w x h keycap is blended over the art
};

struct KeycapRegion {
  const char* button;  // lowercase guest button, as in ResolveLabels
  int x, y, w, h;
  KeycapStyle style;
};

struct AtlasSpec {
  const char* name;
  int width, height;
  std::vector<KeycapRegion> regions;
};

inline const std::vector<AtlasSpec>& Atlases() {
  using enum KeycapStyle;
  static const std::vector<AtlasSpec> atlases = {
      // In-game A/B/Y/X sheet: normal buttons in row 0, dimmed ones in row 1.
      {"art\\gui\\persistant\\abyx.tex", 256, 256,
       {{"a", 0, 0, 64, 64, kCell}, {"b", 64, 0, 64, 64, kCell},
        {"y", 128, 0, 64, 64, kCell}, {"x", 192, 0, 64, 64, kCell},
        {"a", 0, 64, 64, 64, kCell}, {"b", 64, 64, 64, 64, kCell},
        {"y", 128, 64, 64, 64, kCell}, {"x", 192, 64, 64, 64, kCell}}},
      // Menu frames: the selection medallion's A and the Back button's B.
      {"art\\gui\\frames\\frames_elements.tex", 512, 512,
       {{"b", 46, 185, 46, 62, kOverlay}, {"a", 13, 364, 46, 62, kOverlay}}},
  };
  return atlases;
}

struct TextureHeader {
  int format, width, height;
};

inline std::optional<TextureHeader> ReadHeader(const GuiTextureBank& headers,
                                               const std::string& name) {
  auto it = headers.entries().find(name);
  if (it == headers.entries().end() || it->second.length != 84) return std::nullopt;
  const uint8_t* h = headers.Payload(it->second);
  return TextureHeader{GuiTextureBank::ReadIntBE(h + 12), GuiTextureBank::ReadIntBE(h + 16),
                       GuiTextureBank::ReadIntBE(h + 20)};
}

using Keycaps = std::vector<std::pair<KeycapRegion, std::string>>;

// The keycaps each prompt texture gets, by lowercase entry name.
inline std::unordered_map<std::string, Keycaps> PlanKeycaps(
    const GuiTextureBank& textures, const GuiTextureBank& headers,
    const std::unordered_map<std::string, std::string>& labels, bool mouse_look) {
  std::unordered_map<std::string, Keycaps> plan;
  for (const auto& [name, entry] : textures.entries()) {
    const auto label = IconLabel(entry.name, labels, mouse_look);
    const auto h = ReadHeader(headers, name);
    if (!label || !h || h->width != h->height || (h->width != 32 && h->width != 64)) continue;
    plan[name].push_back({{"", 0, 0, h->width, h->height, KeycapStyle::kWhole}, *label});
  }
  for (const auto& atlas : Atlases()) {
    const std::string name = Ci(atlas.name);
    const auto h = ReadHeader(headers, name);
    if (!textures.entries().count(name) || !h || h->width != atlas.width ||
        h->height != atlas.height)
      continue;
    for (const auto& region : atlas.regions) {
      auto label = labels.find(region.button);
      if (label != labels.end()) plan[name].push_back({region, label->second});
    }
  }
  return plan;
}

inline void DrawKeycap(std::vector<uint8_t>& rgba, const TextureHeader& h,
                       const KeycapRegion& r, const std::string& label) {
  switch (r.style) {
    case KeycapStyle::kWhole:
      rgba = RenderKeycap(label, h.width);
      break;
    case KeycapStyle::kCell:
      ReplaceKeycapRegion(rgba, RenderKeycapInset(label, 10, 4, -3), h.width, h.height, r.x, r.y);
      break;
    case KeycapStyle::kOverlay: {
      const auto cap = RenderKeycapLinear(label, r.w, r.h);
      if (cap.size() == size_t(r.w) * r.h * 4)
        CompositeLinearRegion(rgba, cap.data(), r.w, r.h, h.width, h.height, r.x, r.y);
      break;
    }
  }
}

// Both versions of a prompt texture as full RGBA payloads (size prefix, then
// A,R,G,B in Xenos tile order): [0] the game's art, [1] with the keycaps.
struct PromptImages {
  std::vector<uint8_t> rgba[2];
};

inline std::optional<PromptImages> DrawPromptImages(const uint8_t* payload, size_t len,
                                                    const TextureHeader& h,
                                                    const Keycaps& keycaps) {
  const size_t rgba_size = 4 + size_t(h.width) * h.height * 4;
  PromptImages images;
  auto& original = images.rgba[0];
  if (h.format == 0xA2) {
    if (len != rgba_size) return std::nullopt;
    original.assign(payload, payload + len);
  } else if (!DecodeCompressedIconToTile(payload, len, h.width, h.height, original) ||
             original.size() != rgba_size) {
    return std::nullopt;
  }
  images.rgba[1] = original;
  for (const auto& [region, label] : keycaps) DrawKeycap(images.rgba[1], h, region, label);
  if (images.rgba[1].size() != rgba_size) return std::nullopt;
  return images;
}

namespace prompt_detail {

inline std::vector<uint8_t> ReadAllBytes(const std::filesystem::path& path) {
  std::vector<uint8_t> out;
  std::FILE* f = std::fopen(path.string().c_str(), "rb");
  if (!f) return out;
  std::fseek(f, 0, SEEK_END);
  const long len = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (len > 0) {
    out.resize(size_t(len));
    if (std::fread(out.data(), 1, out.size(), f) != out.size()) out.clear();
  }
  std::fclose(f);
  return out;
}

inline bool WriteIfChanged(const std::filesystem::path& path, const std::vector<uint8_t>& data) {
  if (ReadAllBytes(path) == data) return true;
  std::FILE* f = std::fopen(path.string().c_str(), "wb");
  if (!f) return false;
  const bool wrote = data.empty() || std::fwrite(data.data(), 1, data.size(), f) == data.size();
  return std::fclose(f) == 0 && wrote;
}

// The untouched bank: the backup kept when it was first rewritten, if any.
inline std::filesystem::path Pristine(const std::filesystem::path& root, const char* bank) {
  std::error_code ec;
  const auto orig = root / (std::string(bank) + ".orig");
  return std::filesystem::exists(orig, ec) ? orig : root / bank;
}

// BC3 keeps a texture at its original memory size (a 512x512 atlas stored as
// full RGBA is 1 MB and stopped the pause menu highlight drawing). Only for
// textures whose 4x4 blocks fill whole 32-block tiles, where the game lays
// the blocks out exactly as RawBc3LiveSurface does; smaller ones stay RGBA.
inline bool StoreAsBc3(const TextureHeader& h) {
  return h.format != 0xA2 && h.width % 128 == 0 && h.height % 128 == 0;
}

// The keycap version's BC3 blocks: the original blocks, re-encoded only where
// a keycap changed the pixels.
inline std::vector<uint8_t> ReencodeChangedBlocks(const std::vector<uint8_t>& bc3,
                                                  const std::vector<uint8_t>& original,
                                                  const std::vector<uint8_t>& keyboard, int w,
                                                  int h) {
  std::vector<uint8_t> out = bc3;
  for (int by = 0; by < h / 4; ++by) {
    for (int bx = 0; bx < w / 4; ++bx) {
      uint8_t px[64];
      bool changed = false;
      for (int i = 0; i < 16; ++i) {
        const int off = 4 + XenosTiledOffset(bx * 4 + i % 4, by * 4 + i / 4, w);
        std::memcpy(px + i * 4, keyboard.data() + off, 4);
        changed |= std::memcmp(px + i * 4, original.data() + off, 4) != 0;
      }
      if (changed) EncodeBc3Block(px, out.data() + (size_t(by) * (w / 4) + bx) * 16);
    }
  }
  return out;
}

inline std::optional<PromptTexture> BuildTexture(const GuiTextureBank& bank,
                                                 const GuiTextureBank::Entry& entry,
                                                 const std::string& name, const TextureHeader& h,
                                                 const Keycaps& keycaps) {
  const uint8_t* src = bank.Payload(entry);
  const size_t len = size_t(entry.length);
  auto images = DrawPromptImages(src, len, h, keycaps);
  if (!images) return std::nullopt;
  PromptTexture t;
  t.name = name;
  t.offset = uint64_t(entry.offset);
  if (StoreAsBc3(h)) {
    std::vector<uint8_t> bc3;
    if (!DecodeCompressedToBc3(src, len, h.width, h.height, bc3)) return std::nullopt;
    t.raw_bc3 = true;
    t.payload[0] = BuildRawBc3Body(bc3);
    t.payload[1] = BuildRawBc3Body(
        ReencodeChangedBlocks(bc3, images->rgba[0], images->rgba[1], h.width, h.height));
    for (int m = 0; m < 2; ++m) t.live[m] = RawBc3LiveSurface(t.payload[m], h.width);
  } else {
    for (int m = 0; m < 2; ++m) {
      t.payload[m] = std::move(images->rgba[m]);
      t.live[m].assign(t.payload[m].begin() + 4, t.payload[m].end());
    }
  }
  return t;
}

// Puts the untouched banks back (prompts turned off after a rewrite).
inline void RestoreBanks(const std::filesystem::path& root) {
  for (const char* bank : {kTextureBank, kHeaderBank}) {
    const auto orig = root / (std::string(bank) + ".orig");
    std::error_code ec;
    if (std::filesystem::exists(orig, ec)) WriteIfChanged(root / bank, ReadAllBytes(orig));
  }
}

}  // namespace prompt_detail

// Builds every prompt texture for this key map and rewrites the banks to hold
// them. Returns nothing (and puts the untouched banks back) when no key is
// bound and mouse look is off.
inline std::vector<PromptTexture> BuildPromptTextures(const std::filesystem::path& root,
                                                      const std::string& key_map,
                                                      bool mouse_look) {
  using namespace prompt_detail;
  std::error_code ec;
  std::filesystem::remove(root / "keyboard_prompts.bin", ec);  // written by 1.6.0

  const auto labels = ResolveLabels(key_map, "");
  if (labels.empty() && !mouse_look) {
    RestoreBanks(root);
    return {};
  }
  // Always start from the untouched banks, so a bank an older build rewrote
  // is rebuilt with the current layout.
  const auto texture_bytes = ReadAllBytes(Pristine(root, kTextureBank));
  const auto header_bytes = ReadAllBytes(Pristine(root, kHeaderBank));
  GuiTextureBank textures, headers;
  if (!textures.Parse(texture_bytes.data(), texture_bytes.size()) ||
      !headers.Parse(header_bytes.data(), header_bytes.size()))
    return {};

  std::vector<PromptTexture> built;
  std::unordered_map<std::string, std::vector<uint8_t>> replacements;
  std::unordered_map<std::string, uint32_t> header_sizes;
  std::unordered_set<std::string> raw_bc3;
  for (const auto& [name, keycaps] : PlanKeycaps(textures, headers, labels, mouse_look)) {
    const auto h = ReadHeader(headers, name);
    auto t = BuildTexture(textures, textures.entries().at(name), name, *h, keycaps);
    if (!t) continue;
    if (h->format != 0xA2) {  // stored differently from the original
      replacements[name] = t->payload[0];
      header_sizes[name] = uint32_t(t->payload[0].size());
      if (t->raw_bc3) raw_bc3.insert(name);
    }
    built.push_back(std::move(*t));
  }

  if (replacements.empty()) {
    RestoreBanks(root);
    return built;
  }
  std::unordered_map<std::string, uint64_t> new_offsets;
  std::vector<uint8_t> new_textures, new_headers;
  const bool rebuilt = RebuildBank(texture_bytes, replacements, new_textures, new_offsets) &&
                       RebuildHeaderBank(header_bytes, header_sizes, new_headers, raw_bc3);
  if (rebuilt) {
    for (const char* bank : {kTextureBank, kHeaderBank}) {
      const auto orig = root / (std::string(bank) + ".orig");
      if (!std::filesystem::exists(orig, ec)) std::filesystem::copy_file(root / bank, orig, ec);
    }
    if (!WriteIfChanged(root / kTextureBank, new_textures) ||
        !WriteIfChanged(root / kHeaderBank, new_headers))
      new_offsets.clear();
  }
  // A converted texture is only swappable where the rewritten bank holds it.
  std::vector<PromptTexture> out;
  for (auto& t : built) {
    if (replacements.count(t.name)) {
      auto it = new_offsets.find(t.name);
      if (it == new_offsets.end()) continue;
      t.offset = it->second;
    }
    out.push_back(std::move(t));
  }
  return out;
}

}  // namespace fable2::gui

#endif  // FABLE2_GUI_PROMPT_TEXTURES_H
