// fable_2 - ReXGlue Recompiled Project
//
// "Deck Recomp" settings: the list shown on the Deck Recomp page
// (src/core/deck_settings_page.h), opened from Options > Deck Recomp in the
// pause menu.
//
// The pause menu's Options screen fills its rows from GameFace Lua:
// g_Menu:SetPopulationTable() (ExpandableMenuPopulationScript.lua) picks the
// rows for the menu's MenuPopulationScript ("OptionsMenu": Game, Video,
// Controls, Audio). kOptionsRowLua below runs in every GameFace screen as it
// loads (src/core/fable2_fit_window_ui.h) and appends a "Deck Recomp" row to
// that table. The rest of the Options screen is run by the engine: A on a row
// reaches OptionsMenu_HandleInput (sub_82582E50), which src/menu/main_menu.cpp
// catches for this row to open the page instead of one of the game's own.
// (The row script and the input hook follow himdo/Fable-2-Recomp PR #213,
// Advanced Options, by Locme.)

#pragma once

#include "fable2_config.h"
#include "fable2_patches.h"
#include "minimap_state.h"

#include <rex/cvar.h>
#include <rex/logging.h>

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace fable2::deckmenu {

struct Choice {
  const char* value;  // as in fable2_config.toml
  const char* label;  // shown on the page
};

// The page's tabs.
enum Tab { kPerformance, kDisplay, kInterface, kTabCount };
inline const char* TabName(int t) {
  static const char* kNames[kTabCount] = {"Performance", "Display", "Interface"};
  return kNames[t];
}

// When a change reaches the game.
enum class When {
  kLive,        // at once
  kNextScreen,  // menu screens opened from now on (each reads it as it loads)
  kNextLoad,    // objects loaded from now on (the next area or save)
  kAtStart,     // only matters while the game starts (the intro videos)
  kRestart,     // the next time the game starts
};

struct Setting {
  const char* key;      // [section] key
  const char* section;
  const char* label;
  int tab;
  When when;
  std::vector<Choice> choices;
  std::string (*current)();                    // the value now, as in the file
  void (*set)(fable2::config::Values&, const std::string&);  // the in-memory value
  const char* extra_key = nullptr;             // set to the same value (same section)
  bool quote = false;                          // a TOML string
  void (*live)(const std::string&) = nullptr;  // applies it to the running game
};

inline const char* BoolText(bool b) { return b ? "true" : "false"; }

inline void SetCvar(const char* name, const std::string& value) {
  if (!rex::cvar::SetFlagByName(name, value)) {
    REXSYS_WARN("[deck-menu] cvar {} missing", name);
  }
}

inline std::string FormatFloat(double v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%g", v);
  return buf;
}

inline const std::vector<Setting>& Settings() {
  using fable2::config::Get;
  using fable2::config::Values;
  static const std::vector<Setting> settings = {
      // Performance -------------------------------------------------------------
      {"fps_limit", "perf", "Frame Rate Cap", kPerformance, When::kLive,
       {{"30", "30 fps"}, {"40", "40 fps"}, {"45", "45 fps"}, {"60", "60 fps"}, {"0", "Unlimited"}},
       [] { return std::to_string(Get().fps_limit); },
       [](Values& v, const std::string& s) { v.fps_limit = std::stoi(s); }, nullptr, false,
       [](const std::string& s) {
         // As at start-up (fable_2_app.h), but applied now; 30 = the game's
         // own vblank lock.
         if (s == "30") {
           SetCvar("guest_vblank_pacing", "true");
           SetCvar("frame_limit", "60");
         } else {
           SetCvar("frame_limit", s);
           SetCvar("guest_vblank_pacing", "false");
         }
       }},
      {"async_pipelines", "patches", "Async Shader Compilation", kPerformance, When::kLive,
       {{"false", "Off"}, {"true", "On"}},
       [] { return std::string(BoolText(Get().async_pipelines)); },
       [](Values& v, const std::string& s) { v.async_pipelines = s == "true"; }, nullptr, false,
       [](const std::string& s) {
         if (s == "true") SetCvar("async_shader_compilation", "true");
         SetCvar("vulkan_async_skip_draws", s);
       }},
      {"high_tick_rate", "patches", "High Tick Rate", kPerformance, When::kLive,
       {{"false", "Off"}, {"true", "On"}},
       [] { return std::string(BoolText(Get().high_tick_rate && Get().higher_hf_tick_rate)); },
       [](Values& v, const std::string& s) { v.high_tick_rate = v.higher_hf_tick_rate = s == "true"; },
       "higher_hf_tick_rate", false,
       [](const std::string& s) { fable2::patches::SetTickRates(s == "true", s == "true"); }},
      // Display -----------------------------------------------------------------
      {"view_distance_multiplier", "patches", "Draw Distance", kDisplay, When::kNextLoad,
       {{"1", "1x"}, {"1.5", "1.5x"}, {"2", "2x"}, {"3", "3x"}, {"4", "4x"}, {"6", "6x"}, {"10", "10x"}},
       [] { return FormatFloat(Get().view_distance_multiplier); },
       [](Values& v, const std::string& s) { v.view_distance_multiplier = std::stof(s); }, nullptr, false,
       [](const std::string& s) { fable2::patches::SetDrawDistance(std::stof(s)); }},
      {"lod_distance_multiplier", "patches", "Detail Distance", kDisplay, When::kNextLoad,
       {{"1", "1x"}, {"1.5", "1.5x"}, {"2", "2x"}, {"3", "3x"}, {"4", "4x"}, {"6", "6x"}, {"10", "10x"}},
       [] { return FormatFloat(Get().lod_distance_multiplier); },
       [](Values& v, const std::string& s) { v.lod_distance_multiplier = std::stof(s); }},
      // Read when the renderer sets up its targets: the next start.
      {"disable_msaa", "patches", "Anti-Aliasing", kDisplay, When::kRestart,
       {{"false", "2x MSAA"}, {"true", "Off"}},
       [] { return std::string(BoolText(Get().disable_msaa)); }, nullptr},
      {"disable_motion_blur", "patches", "Motion Blur", kDisplay, When::kLive,
       {{"false", "On"}, {"true", "Off"}},
       [] { return std::string(BoolText(Get().disable_motion_blur)); },
       [](Values& v, const std::string& s) { v.disable_motion_blur = s == "true"; }},
      // A string the GUI reads at start-up: written to the file only.
      {"deck_button_prompts", "input", "Button Icons", kDisplay, When::kRestart,
       {{"dark", "Steam Deck (Dark)"}, {"light", "Steam Deck (Light)"}, {"off", "Xbox 360"}},
       [] { return Get().deck_button_prompts; }, nullptr, nullptr, true},
      // Interface ---------------------------------------------------------------
      {"menu_repeat_delay", "patches", "Menu Hold Scroll Delay", kInterface, When::kNextScreen,
       {{"0.25", "0.25 s"}, {"0.18", "0.18 s"}, {"0.12", "0.12 s"}},
       [] { return FormatFloat(Get().menu_repeat_delay); },
       [](Values& v, const std::string& s) { v.menu_repeat_delay = std::stod(s); }},
      {"menu_animation_speed", "patches", "Menu Animation Speed", kInterface, When::kNextScreen,
       {{"1", "1x"}, {"1.5", "1.5x"}, {"2.5", "2.5x"}, {"4", "4x"}},
       [] { return FormatFloat(Get().menu_animation_speed); },
       [](Values& v, const std::string& s) { v.menu_animation_speed = std::stod(s); }},
      {"menu_instant_response", "patches", "Instant Menu Response", kInterface, When::kNextScreen,
       {{"false", "Off"}, {"true", "On"}},
       [] { return std::string(BoolText(Get().menu_instant_response)); },
       [](Values& v, const std::string& s) { v.menu_instant_response = s == "true"; }},
      {"menu_scroll_acceleration", "patches", "Menu Scroll Acceleration", kInterface, When::kNextScreen,
       {{"1", "Off"}, {"1.5", "1.5x"}, {"2", "2x"}, {"3", "3x"}},
       [] { return FormatFloat(Get().menu_scroll_acceleration); },
       [](Values& v, const std::string& s) { v.menu_scroll_acceleration = std::stod(s); }},
      {"skip_intro_videos", "patches", "Skip Intro Videos", kInterface, When::kAtStart,
       {{"false", "Off"}, {"true", "On"}},
       [] { return std::string(BoolText(Get().skip_intro_videos)); },
       [](Values& v, const std::string& s) { v.skip_intro_videos = s == "true"; }},
      // [minimap] (src/core/fable2_minimap.h): keys "enabled" and "size".
      {"enabled", "minimap", "Minimap", kInterface, When::kLive,
       {{"true", "On"}, {"false", "Off"}},
       [] { return std::string(BoolText(Get().minimap_enabled)); },
       [](Values& v, const std::string& s) { v.minimap_enabled = s == "true"; }, nullptr, false,
       [](const std::string& s) { fable2::minimap::feature_enabled().store(s == "true"); }},
      {"size", "minimap", "Minimap Size", kInterface, When::kLive,
       {{"small", "Small"}, {"medium", "Medium"}, {"large", "Large"}},
       [] { return Get().minimap_size; },
       [](Values& v, const std::string& s) { v.minimap_size = s; }, nullptr, true,
       [](const std::string& s) {
         fable2::minimap::size_fraction().store(fable2::minimap::SizeFraction(s));
       }},
      {"opacity", "minimap", "Minimap Opacity", kInterface, When::kLive,
       {{"100", "100%"}, {"90", "90%"}, {"80", "80%"}, {"70", "70%"}, {"60", "60%"},
        {"50", "50%"}, {"40", "40%"}, {"30", "30%"}, {"20", "20%"}, {"10", "10%"}},
       [] { return std::to_string(Get().minimap_opacity); },
       [](Values& v, const std::string& s) { v.minimap_opacity = std::stoi(s); }, nullptr, false,
       [](const std::string& s) { fable2::minimap::opacity().store(std::stoi(s) / 100.0f); }},
  };
  return settings;
}

// The explanation shown under the list while the setting is highlighted.
inline const char* Explanation(const std::string& key) {
  static const std::pair<const char*, const char*> kTexts[] = {
      {"high_tick_rate", "Runs the game's updates twice as often: smoother menus and HUD, less input delay. Experimental: it may cause issues."},
      {"menu_repeat_delay", "How long a held direction waits before the menu starts scrolling. The original game waits 0.25 s."},
      {"menu_animation_speed", "How fast the menus slide and fade. Faster shows the result of a press sooner."},
      {"menu_instant_response", "Menus react in the same update as the press, about 0.1 s sooner than the original game."},
      {"menu_scroll_acceleration", "Holding a direction speeds up scrolling over 1.5 s, up to this many times the normal rate."},
      {"enabled", "The region map from the pause menu, in the top-right corner while you play, with your hero and dog on it. The {BACK} button shows and hides it."},
      {"size", "How big the minimap is drawn."},
      {"opacity", "How solid the minimap is drawn. Lower lets more of the game show through."},
      {"skip_intro_videos", "Skips the Microsoft and Lionhead logo videos when the game starts. This may also make the game start faster."},
      {"disable_motion_blur", "The camera's full-screen blur when it moves."},
      {"view_distance_multiplier", "How far away objects, buildings, villagers and creatures are drawn before they pop out. Above 3x is not recommended: busy places like Bowerstone lose a lot of frame rate."},
      {"lod_distance_multiplier", "How far out models keep their full-detail version. Above 3x is not recommended: busy places like Bowerstone lose a lot of frame rate."},
      {"disable_msaa", "The game's 2x multisample anti-aliasing smooths jagged edges. Off frees GPU time for a steadier frame rate, with slightly jaggier edges."},
      {"deck_button_prompts", "Button prompts as Steam Deck buttons in dark or light, or the original Xbox 360 buttons."},
      {"fps_limit", "The highest frame rate. 30 fps is the original game's lock. Unlimited removes the cap."},
      {"async_pipelines", "New shaders are compiled in the background instead of pausing the game. This reduces stutters, but whatever needs a new shader (part of an object, or an effect) is left out for a moment the first time it appears."},
  };
  for (const auto& [k, text] : kTexts) {
    if (key == k) return text;
  }
  return "";
}

// The value each setting had when the game started, for "Restart to apply".
inline std::vector<std::string>& StartupValues() {
  static std::vector<std::string> values = [] {
    std::vector<std::string> v;
    for (const Setting& s : Settings()) v.push_back(s.current());
    return v;
  }();
  return values;
}

// Saves one setting to fable2_config.toml (and its extra key). True if saved.
inline bool Save(const Setting& s, const std::string& value) {
  const std::string toml = s.quote ? "\"" + value + "\"" : value;
  bool saved = fable2::config::SaveSetting(s.section, s.key, toml);
  if (s.extra_key) saved = fable2::config::SaveSetting(s.section, s.extra_key, toml) && saved;
  return saved;
}

// The "Deck Recomp" row in the pause menu's Options (run in every GameFace
// screen as it loads). Once per menu it wraps SetPopulationTable to append the
// row, and refills an Options menu that filled itself before the wrapper was
// there; GUI.onUpdate checks every frame which menu is current. The row's
// event type is the Game row's (70), so if the input hook ever missed, A would
// open the game's own Game page, nothing worse. The TextTag is shown as
// written (the game has no string with that name).
inline constexpr const char kOptionsRowLua[] = R"lua(
pcall(function()
local NAME = "F2DeckRecomp"
local LABEL = "Deck Recomp Settings"
local ICON = "Art\\GUI\\WheelIcons\\icon_options.tga"
local function patch()
  local m = rawget(_G, "g_Menu")
  if type(m) ~= "table" or type(m.SetPopulationTable) ~= "function" then
    return false
  end
  if m.__f2deckRow then return true end
  m.__f2deckRow = true
  local setTable = m.SetPopulationTable
  m.SetPopulationTable = function(self, ...)
    local r = setTable(self, ...)
    local okp, parent = pcall(getElement, "parent", self.element)
    local oks, script = false, nil
    if okp and parent then oks, script = pcall(getAttribute, parent, "MenuPopulationScript") end
    local t = self.PopulationTable
    if oks and script == "OptionsMenu" and type(t) == "table" then
      for i = 1, #t do
        if type(t[i]) == "table" and t[i].Name == NAME then return r end
      end
      local ev = self.EventConst and self.EventConst.EVENT_TYPE_OPTIONS_GAME_ITEM or 70
      t[#t + 1] = { Name = NAME, TextTag = LABEL, IconTexture = ICON, EventType = ev }
      pcall(output, "[f2deck] row added to OptionsMenu")
    end
    return r
  end
  local okp, parent = pcall(getElement, "parent", m.element)
  local oks, script = false, nil
  if okp and parent then oks, script = pcall(getAttribute, parent, "MenuPopulationScript") end
  if oks and script == "OptionsMenu" then
    local okg, item = pcall(m.GetMenuItem, m, NAME)
    if not (okg and item) and type(m.RepopulateMenuFromPopulationScript) == "function" then
      pcall(m.RepopulateMenuFromPopulationScript, m, "OptionsMenu")
      if type(m.DirtyLayout) == "function" then pcall(m.DirtyLayout, m) end
      if type(m.DirtyHighlight) == "function" then pcall(m.DirtyHighlight, m) end
    end
  end
  return true
end
patch()
if type(GUI) == "table" and type(GUI.onUpdate) == "function" and not rawget(GUI, "__f2deckRow") then
  rawset(GUI, "__f2deckRow", true)
  local prev = GUI.onUpdate
  GUI.onUpdate = function(...)
    patch()
    return prev(...)
  end
end
end)
)lua";

}  // namespace fable2::deckmenu
