// fable_2 - The "Advanced" row in the pause menu's Options (Game, Video,
// Controls, Audio, Advanced), which opens the Advanced Options page
// (src/core/port_settings_dialog.h).
//
// The Options rows are not added by game code: the screen's GameFace Lua
// fills its ExpandableMenu from a table. g_Menu:SetPopulationTable()
// (Scripts\Gameface\ExpandableMenu\ExpandableMenuPopulationScript.lua) reads
// the menu's MenuPopulationScript attribute and, for "OptionsMenu", sets
// g_Menu.PopulationTable to four rows {Name, TextTag, IconTexture,
// EventType}; g_Menu:RepopulateFromPopulationTable() calls it, spawns the
// rows and clears the table. The menu does this in its onInitialize, and
// g_Menu itself is only set there (WheelMenu.lua: g_Menu = self).
//
// The script below runs in every GameFace presentation as it loads (through
// fable2_fit_window_ui.h's RunLuaInPresentation) and then each frame from
// GUI.onUpdate. Once per g_Menu it wraps SetPopulationTable to append the
// Advanced row to the "OptionsMenu" table, and refills an Options menu that
// filled itself before the wrapper was there.
//
// Choosing the row is handled in C++: A on the Options menu reaches
// OptionsMenu_HandleInput (src/menu/main_menu.cpp), which reads the
// highlighted row and opens the page for this one. The row's event type is the
// Game row's (70), so the worst case if that hook ever missed is the game's
// own Game page.
//
// Nothing is registered with the game's own (saved) script manager: this is
// GameFace's Lua, a separate VM.
#pragma once

namespace fable2::portsettings {

inline constexpr const char kOptionsRowLua[] = R"lua(
local NAME = "RecompSettings"
local LABEL = "Advanced"
local ICON = "Art\\GUI\\WheelIcons\\icon_options.tga"
local function patch()
  local m = g_Menu
  if type(m) ~= "table" or type(m.SetPopulationTable) ~= "function" then
    return false
  end
  if m.__recompSettings then return true end
  m.__recompSettings = true
  pcall(output, "[recomp-settings] menu hooked")
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
      pcall(output, "[recomp-settings] row added to OptionsMenu")
    end
    return r
  end
  -- The rows are usually there already (the menu fills itself in its
  -- onInitialize, before this hooks it, and then clears PopulationTable):
  -- refill it the game's own way so the wrapper adds the row.
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
-- g_Menu is set by the menu's own onInitialize (WheelMenu.lua: g_Menu =
-- self), after the screen has loaded, so check every frame which table is
-- current and hook it once.
patch()
if type(GUI) == "table" and type(GUI.onUpdate) == "function" and not rawget(GUI, "__recompSettings") then
  rawset(GUI, "__recompSettings", true)
  local prev = GUI.onUpdate
  GUI.onUpdate = function(...)
    patch()
    return prev(...)
  end
end
)lua";

}  // namespace fable2::portsettings
