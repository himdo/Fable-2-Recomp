// fable_2 - ReXGlue Recompiled Project
//
// Recomp user configuration (fable2_config.toml), separate from the ReXGlue
// SDK cvar config (fable_2.toml). Loaded once at startup from next to the
// exe; created with defaults if missing.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace fable2::config {

// Default host keyboard -> guest gamepad map (see src/input/keyboard_gamepad.h).
// It lives here, in the config layer, so the mapping is editable in
// fable2_config.toml instead of being baked into the binary; the
// keyboard_gamepad_map cvar itself defaults to empty and is seeded from
// here at startup (see Fable2App::OnPostInitLogging).
inline constexpr std::string_view kDefaultKeyboardGamepadMap =
    "E:A,2:B,1:X,3:Y,"
    "W:StickUp,S:StickDown,A:StickLeft,D:StickRight,"
    "Escape:Pause,M:Select,Q:LT,Tab:RT,"
    "F1:Up,F2:Down,F3:Left,F4:Right";

// Parsed values from fable2_config.toml. To add a setting:
//   1. Add a member here with its built-in default.
//   2. Read it in Load() below (ReadInt/ReadBool/ReadString helpers).
//   3. Document the key in the template (DefaultTemplate /
//      config/fable2_config.toml - keep the two in sync).
// Missing keys keep their defaults; wrong types log a warning and keep the
// default, so the file can grow without breaking the game.
struct Values {
  // [general]
  int32_t config_version = 1;
  // [input]
  std::string keyboard_gamepad_map{std::string{kDefaultKeyboardGamepadMap}};
  // Keep in sync with the cvar defaults in main.cpp (mouse_look,
  // mouse_look_scale); the cvar keeps its compiled default as a fallback so
  // an empty/missing config can never wedge input.
  bool mouse_look = true;
  int32_t mouse_look_scale = 256;  // range 1..4096 (cvar constraint)
  // [patches] - toggles for the recomp-level (mid-asm hook) patches. The
  // hook bodies consult these at runtime (src/core/fable2_hooks.cpp), so a patch
  // can be A/B'd with no rebuild. Guest-image data patches live in
  // fable2_patches.toml instead (see src/core/fable2_patches.h).
  // Unlock the Guild-chest items that were obtainable from the (now-dead)
  // Fable 2 website. Forces both the registration gates and the grant-method
  // result in GuildChest_GetWebsiteItem_8256E368 (hooks
  // fable2_hook_website_g1/g1b/grantnew in src/core/fable2_hooks.cpp).
  bool unlock_website = true;
  // Unlock the Collectors Edition chest content. Forces both the
  // registration gates and the grant-method result in
  // GuildChest_GetCEContent_824B3528 (hooks fable2_hook_ce_g1/g1b/grantavail
  // in src/core/fable2_hooks.cpp).
  bool unlock_ce = true;
  // [perf] - hot-function override tuning.
  // hotfunc_yield_every: NtYieldExecution batching factor for the hotfunc
  // overrides (see src/core/hotfunc/hotfunc_yield.h). Every Nth call does
  // the real SwitchToThread; the others take a full memory fence. 1 =
  // original (yield every call), 0 = never yield. Larger = fewer context
  // switches, less frequent CPU rotation to other guest threads.
  int32_t hotfunc_yield_every = 8;
};

// Load the config from `path`.
//   - File missing  -> writes the default template, then parses it (returns
//                      true).
//   - Parse failure -> logs the error, shows a dialog, falls back to the
//                      in-memory defaults (returns false).
// Unknown top-level sections log a warning; everything is otherwise ignored.
// Safe to call only after the SDK logging is initialized (OnPostInitLogging).
bool Load(const std::filesystem::path& path);

// Parsed values (in-memory defaults until Load() succeeds).
const Values& Get();

// The default template written when the file is missing. Must match
// config/fable2_config.toml in the repo (staged next to the exe by the build).
const char* DefaultTemplate();

}  // namespace fable2::config
