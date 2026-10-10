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
    "LMB:X,RMB:Y,MMB:B,Shift:A,E:A,"
    "Q:LT,Tab:RB,R:RT,Z:LB,B:B,"
    "W:StickUp,S:StickDown,A:StickLeft,D:StickRight,"
    "Escape:Pause,M:Select,"
    "Up:Up,Down:Down,Left:Left,Right:Right";

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
  // can be A/B'd with no rebuild. high_tick_rate / higher_hf_tick_rate also
  // drive the guest-image data writes in src/core/fable2_patches.cpp.
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
  // Skip the Microsoft and Lionhead logo videos at boot (just-harry's "Skip
  // intro videos"; hook fable2_hook_skip_intro_videos in
  // src/core/fable2_hooks.cpp). false = original.
  bool skip_intro_videos = false;
  // [patches] high_tick_rate: LF tick 15 -> 30 Hz (Xenia "High Tick Rate" by
  // Guy; data write in src/core/fable2_patches.cpp + hook
  // fable2_hook_high_tick_rate_skip_store). false = original.
  bool high_tick_rate = false;
  // [patches] higher_hf_tick_rate: HF tick 30 -> 60 Hz (Xenia "Higher HF Tick
  // Rate" by Ultra; data write in src/core/fable2_patches.cpp + hook
  // fable2_hook_high_hf_tick_rate_skip_store). Requires high_tick_rate.
  bool higher_hf_tick_rate = false;
  // [patches] disable_motion_blur: zero the full-screen motion blur amount the
  // camera passes to the renderer (hook fable2_hook_disable_motion_blur in
  // src/core/fable2_hooks.cpp). false = original.
  bool disable_motion_blur = false;
  // [perf] - hot-function override tuning.
  // hotfunc_yield_every: NtYieldExecution batching factor for the hotfunc
  // overrides (see src/core/hotfunc/hotfunc_yield.h). Every Nth call does
  // the real SwitchToThread; the others take a full memory fence. 1 =
  // original (yield every call), 0 = never yield. Larger = fewer context
  // switches, less frequent CPU rotation to other guest threads.
  int32_t hotfunc_yield_every = 1;
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
