// fable_2 - The Back button's prompt icon in each button-icon scheme
// ([input] deck_button_prompts), for host-drawn text that names the button
// (the Deck Recomp page's Minimap description): the game's own Xbox 360 Back
// icon, and the Steam Deck View glyph on the game's green medallion in the
// dark and light sets, made the way the game's textures get them
// (src/gui/deck_prompts.h).
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "../gui/deck_prompts.h"
#include "photo_mode_icons.h"

namespace fable2::backicon {

enum Scheme { kXbox, kDark, kLight, kSchemeCount };

// "off" / "dark" / "light" (deck_button_prompts) -> scheme.
inline int SchemeFor(const std::string& value) {
  if (value == "dark") return kDark;
  if (value == "light") return kLight;
  return kXbox;
}

// Decodes the three icons (straight-alpha RGBA) from the game data under
// `root`. Returns how many were made; `log` names what failed.
inline int Build(const std::filesystem::path& root, fable2::gameicons::Image out[kSchemeCount],
                 std::string& log) {
  namespace gi = fable2::gameicons;
  const auto dir = root / "data" / "art" / "gui";
  auto pick = [&](const char* file) {
    std::error_code ec;
    const auto orig = dir / (std::string(file) + ".orig");  // pristine copy kept by the prompt swap
    return std::filesystem::exists(orig, ec) ? orig : dir / file;
  };
  gi::detail::Bank tex, hdr;
  if (!tex.Open(pick("gui_textures.bnk")) || !hdr.Open(pick("gui_texture_headers.bnk"))) {
    log = "gui_textures.bnk not found under " + dir.string();
    return 0;
  }
  const std::string name = fable2::gui::kDeckMedallion64;
  const auto* te = tex.Get(name);
  const auto* he = hdr.Get(name);
  std::vector<uint8_t> data, header;
  if (!te || !he || !tex.Read(*te, data) || !hdr.Read(*he, header) || header.size() < 32) {
    log = name + " not readable";
    return 0;
  }
  const uint32_t w = gi::detail::be32(&header[16]), h = gi::detail::be32(&header[20]);
  const uint32_t fmt = gi::detail::be32(&header[24]);
  int made = 0;
  std::string why;
  if (gi::detail::Decode(data, w, h, fmt, out[kXbox], why)) {
    out[kXbox].loaded = true;
    ++made;
  } else {
    log += " xbox: " + why + ";";
  }
  std::vector<uint8_t> payload;
  if (!gi::detail::ToFullPayload(data, w, h, fmt, payload)) {
    log += " no full payload for the Deck icons;";
    return made;
  }
  fable2::gui::DeckArt art;
  art.medallion64 = fable2::gui::deck_detail::MakeMedallion(payload, int(w));
  for (int s : {kDark, kLight}) {
    art.set = s == kDark ? "dark" : "light";
    std::vector<uint8_t> styled = payload;
    why.clear();
    if (fable2::gui::ApplyDeckStyle(name, styled, int(w), int(h), art) &&
        gi::detail::Decode(styled, w, h, 2, out[s], why)) {
      out[s].loaded = true;
      ++made;
    } else {
      log += " " + art.set + ": " + (why.empty() ? std::string("no Deck glyph") : why) + ";";
    }
  }
  return made;
}

}  // namespace fable2::backicon
