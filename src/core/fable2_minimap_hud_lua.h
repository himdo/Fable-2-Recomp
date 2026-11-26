// fable_2 - Live minimap: the HUD's report of where its health bar is.
//
// Run in every GameFace presentation as it loads (prepended to the fit-window
// script, src/core/fable2_fit_window_ui.h); only the HUD has
// Scene.Layer.UpperElements.PlayerHealth. When a message shows in the bar
// across the top of the screen the game pushes the health bar down (its
// TopBoxY behaviour), and the minimap in the opposite corner follows it.
//
// The game moves PlayerHealth.HealthAndMana (y 9.24 at rest, -65 under a
// two-line message; PlayerHealth itself stays at y 295). Every GUI update
// (GUI.onUpdate, the UIContract behaviour's update) this sends that y and the
// HUD layer's opacity with setAttribute(el, "__f2mm_y" / "__f2mm_o", value).
// The native setAttribute hook (src/core/fable2_gui_lua.h) takes those calls
// for the minimap (src/core/minimap_state.h) and they never reach GameFace.
#pragma once

namespace fable2::minimap {

inline constexpr const char kHudReportLua[] = R"lua(
pcall(function()
  local okp, ph = pcall(getElement, "Scene.Layer.UpperElements.PlayerHealth.HealthAndMana")
  if not okp or not ph then return end
  if type(GUI) ~= "table" or GUI.f2mmHooked then return end
  GUI.f2mmHooked = true
  local _, layer = pcall(getElement, "Scene.Layer")
  local function num(e, n)
    if not e then return 0 end
    local ok, v = pcall(getAttribute, e, n)
    if ok and type(v) == "number" then return v end
    return 0
  end
  local prev = GUI.onUpdate
  GUI.onUpdate = function(...)
    if prev then pcall(prev, ...) end
    local y = num(ph, "position.y")
    local o = layer and num(layer, "opacity") or 100
    pcall(setAttribute, ph, "__f2mm_o", o)
    pcall(setAttribute, ph, "__f2mm_y", y)
  end
end)
)lua";

}  // namespace fable2::minimap
